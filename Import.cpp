#include "sf/IO.h"
#include "sf/MeshOps.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace sf {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

float srgbToLinear(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }

// ---- STL -----------------------------------------------------------------------------------
bool readSTL(const std::vector<uint8_t>& d, Mesh& m, std::string* err) {
    auto fail = [&](const char* s) { if (err) *err = s; return false; };
    bool binary = false;
    if (d.size() >= 84) {
        uint32_t n;
        std::memcpy(&n, d.data() + 80, 4);
        binary = d.size() == 84 + (size_t)n * 50;
    }
    if (binary) {
        uint32_t n;
        std::memcpy(&n, d.data() + 80, 4);
        m.pos.resize((size_t)n * 3);
        for (uint32_t t = 0; t < n; ++t) {
            std::memcpy(&m.pos[(size_t)t * 3], d.data() + 84 + (size_t)t * 50 + 12, 36);
            m.tri.push_back(t * 3); m.tri.push_back(t * 3 + 1); m.tri.push_back(t * 3 + 2);
        }
    } else {
        std::string text((const char*)d.data(), d.size());
        if (text.find("vertex") == std::string::npos) return fail("not a valid STL file");
        const char* p = text.c_str();
        while ((p = std::strstr(p, "vertex")) != nullptr) {
            p += 6;
            char* end;
            Vec3 v;
            v.x = std::strtof(p, &end); p = end;
            v.y = std::strtof(p, &end); p = end;
            v.z = std::strtof(p, &end); p = end;
            m.pos.push_back(v);
        }
        size_t n = m.pos.size() / 3;
        m.pos.resize(n * 3);
        for (uint32_t i = 0; i < n * 3; ++i) m.tri.push_back(i);
    }
    if (m.tri.empty()) return fail("STL file contains no triangles");
    m.ensureAttributes();
    for (auto& p : m.pos) if (!isFinite(p)) return fail("STL file contains invalid coordinates");
    weldVertices(m, std::max(length(m.bounds().size()) * 1e-6f, 1e-9f));
    return true;
}

// ---- OBJ -----------------------------------------------------------------------------------
bool readOBJ(const std::vector<uint8_t>& d, std::vector<ImportedObject>& out, std::string* err) {
    std::vector<Vec3> pos, col;
    struct Group { std::string name; std::vector<uint32_t> idx; };
    std::vector<Group> groups(1);
    groups[0].name = "Imported";
    const char* p = (const char*)d.data();
    const char* endAll = p + d.size();
    std::vector<int64_t> face;
    while (p < endAll) {
        const char* eol = (const char*)std::memchr(p, '\n', endAll - p);
        if (!eol) eol = endAll;
        std::string line(p, eol);
        p = eol + 1;
        if (line.size() < 2) continue;
        if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
            const char* s = line.c_str() + 2;
            char* e;
            float v[6];
            int n = 0;
            for (; n < 6; ++n) { v[n] = std::strtof(s, &e); if (e == s) break; s = e; }
            if (n < 3) continue;
            pos.push_back({v[0], v[1], v[2]});
            col.push_back(n >= 6 ? Vec3(v[3], v[4], v[5]) : Vec3(1, 1, 1));
        } else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            face.clear();
            const char* s = line.c_str() + 2;
            while (*s) {
                char* e;
                long long i = std::strtoll(s, &e, 10);
                if (e == s) break;
                face.push_back(i < 0 ? (int64_t)pos.size() + i : i - 1);
                s = e;
                while (*s && *s != ' ' && *s != '\t') ++s; // skip /vt/vn
            }
            for (size_t k = 2; k < face.size(); ++k) {
                int64_t tri[3] = {face[0], face[k - 1], face[k]};
                bool ok = true;
                for (int64_t i : tri) ok = ok && i >= 0 && i < (int64_t)pos.size();
                if (!ok) { if (err) *err = "OBJ face references a missing vertex"; return false; }
                for (int64_t i : tri) groups.back().idx.push_back((uint32_t)i);
            }
        } else if ((line[0] == 'o' || line[0] == 'g') && line[1] == ' ') {
            std::string name = line.substr(2);
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
            if (groups.back().idx.empty()) groups.back().name = name;
            else { groups.emplace_back(); groups.back().name = name; }
        }
    }
    std::vector<uint32_t> remap(pos.size());
    for (auto& g : groups) {
        if (g.idx.empty()) continue;
        std::fill(remap.begin(), remap.end(), UINT32_MAX);
        ImportedObject o;
        o.name = g.name.empty() ? "Imported" : g.name;
        for (uint32_t i : g.idx) {
            if (remap[i] == UINT32_MAX) {
                remap[i] = o.mesh.addVertex(pos[i]);
                o.mesh.col[remap[i]] = col[i];
            }
            o.mesh.tri.push_back(remap[i]);
        }
        o.mesh.ensureAttributes();
        out.push_back(std::move(o));
    }
    if (out.empty()) { if (err) *err = "OBJ file contains no faces"; return false; }
    return true;
}

// ---- PLY -----------------------------------------------------------------------------------
int plyTypeSize(const std::string& t) {
    if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
    if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
    if (t == "int" || t == "uint" || t == "float" || t == "int32" || t == "uint32" || t == "float32") return 4;
    if (t == "double" || t == "float64") return 8;
    return 0;
}
double plyRead(const uint8_t* p, const std::string& t) {
    if (t == "char" || t == "int8") return (int8_t)p[0];
    if (t == "uchar" || t == "uint8") return p[0];
    if (t == "short" || t == "int16") { int16_t v; std::memcpy(&v, p, 2); return v; }
    if (t == "ushort" || t == "uint16") { uint16_t v; std::memcpy(&v, p, 2); return v; }
    if (t == "int" || t == "int32") { int32_t v; std::memcpy(&v, p, 4); return v; }
    if (t == "uint" || t == "uint32") { uint32_t v; std::memcpy(&v, p, 4); return v; }
    if (t == "float" || t == "float32") { float v; std::memcpy(&v, p, 4); return v; }
    double v; std::memcpy(&v, p, 8); return v;
}

bool readPLY(const std::vector<uint8_t>& d, Mesh& m, std::string* err) {
    auto fail = [&](const char* s) { if (err) *err = s; return false; };
    std::string all((const char*)d.data(), d.size());
    size_t hEnd = all.find("end_header");
    if (all.compare(0, 3, "ply") != 0 || hEnd == std::string::npos) return fail("not a valid PLY file");
    size_t body = all.find('\n', hEnd);
    if (body == std::string::npos) return fail("PLY header is truncated");
    ++body;
    struct Prop { std::string name, type, countType; bool list = false; };
    struct Elem { std::string name; size_t count = 0; std::vector<Prop> props; };
    std::vector<Elem> elems;
    bool ascii = false, le = false;
    std::istringstream hs(all.substr(0, hEnd));
    std::string line;
    while (std::getline(hs, line)) {
        std::istringstream ls(line);
        std::string w;
        ls >> w;
        if (w == "format") { ls >> w; ascii = w == "ascii"; le = w == "binary_little_endian"; }
        else if (w == "element") { Elem e; ls >> e.name >> e.count; elems.push_back(e); }
        else if (w == "property" && !elems.empty()) {
            Prop pr;
            ls >> w;
            if (w == "list") { pr.list = true; ls >> pr.countType >> pr.type >> pr.name; }
            else { pr.type = w; ls >> pr.name; }
            elems.back().props.push_back(pr);
        }
    }
    if (!ascii && !le) return fail("only ASCII and little-endian binary PLY files are supported");
    const uint8_t* p = d.data() + body;
    const uint8_t* end = d.data() + d.size();
    std::istringstream as(ascii ? all.substr(body) : std::string());
    bool colourIsByte = false, hasColour = false;
    for (const Elem& e : elems) {
        for (size_t i = 0; i < e.count; ++i) {
            Vec3 pos(0, 0, 0), col(1, 1, 1);
            std::vector<int64_t> face;
            for (const Prop& pr : e.props) {
                auto next = [&](const std::string& type, double& v) {
                    if (ascii) return (bool)(as >> v);
                    int sz = plyTypeSize(type);
                    if (sz == 0 || end - p < sz) return false;
                    v = plyRead(p, type);
                    p += sz;
                    return true;
                };
                double v = 0;
                if (pr.list) {
                    if (!next(pr.countType, v)) return fail("PLY data ends unexpectedly");
                    int cnt = (int)v;
                    if (cnt < 0 || cnt > 4096) return fail("PLY face has an invalid vertex count");
                    for (int k = 0; k < cnt; ++k) {
                        if (!next(pr.type, v)) return fail("PLY data ends unexpectedly");
                        if (pr.name == "vertex_indices" || pr.name == "vertex_index") face.push_back((int64_t)v);
                    }
                } else {
                    if (!next(pr.type, v)) return fail("PLY data ends unexpectedly");
                    bool byteType = plyTypeSize(pr.type) == 1;
                    if (pr.name == "x") pos.x = (float)v; else if (pr.name == "y") pos.y = (float)v; else if (pr.name == "z") pos.z = (float)v;
                    else if (pr.name == "red") { col.x = (float)v; hasColour = true; colourIsByte = byteType; }
                    else if (pr.name == "green") col.y = (float)v;
                    else if (pr.name == "blue") col.z = (float)v;
                }
            }
            if (e.name == "vertex") {
                uint32_t vi = m.addVertex(pos);
                if (hasColour) {
                    if (colourIsByte) col = col / 255.0f;
                    m.col[vi] = Vec3(srgbToLinear(clamp01(col.x)), srgbToLinear(clamp01(col.y)), srgbToLinear(clamp01(col.z)));
                }
            } else if (e.name == "face") {
                for (size_t k = 2; k < face.size(); ++k) {
                    int64_t t[3] = {face[0], face[k - 1], face[k]};
                    for (int64_t x : t) if (x < 0 || x >= (int64_t)m.vcount()) return fail("PLY face references a missing vertex");
                    m.tri.push_back((uint32_t)t[0]); m.tri.push_back((uint32_t)t[1]); m.tri.push_back((uint32_t)t[2]);
                }
            }
        }
    }
    if (m.tri.empty()) return fail("PLY file contains no faces");
    m.ensureAttributes();
    return true;
}

// ---- minimal JSON (for GLB) ----------------------------------------------------------------
struct JValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0;
    std::string str;
    std::vector<JValue> arr;
    // Two parallel vectors avoid recursive pair<string,JValue> instantiation,
    // which some Clang/libc++ versions reject while JValue is incomplete.
    std::vector<std::string> objectKeys;
    std::vector<JValue> objectValues;
    const JValue* get(const char* key) const;
    double number(const char* key, double def) const;
    const JValue* at(size_t i) const;
};

// Define recursive-container accessors after JValue is complete (Clang/libc++).
const JValue* JValue::get(const char* key) const {
    for (size_t i = 0; i < objectKeys.size(); ++i)
        if (objectKeys[i] == key) return &objectValues[i];
    return nullptr;
}
double JValue::number(const char* key, double def) const {
    const JValue* v = get(key);
    return v && v->type == Num ? v->num : def;
}
const JValue* JValue::at(size_t i) const { return i < arr.size() ? &arr[i] : nullptr; }

struct JParser {
    const char* p; const char* end; bool ok = true; int depth = 0;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool lit(const char* s) { size_t n = std::strlen(s); if ((size_t)(end - p) >= n && !std::memcmp(p, s, n)) { p += n; return true; } return false; }
    std::string string() {
        std::string s;
        ++p;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                case 'n': s += '\n'; break; case 't': s += '\t'; break; case 'r': s += '\r'; break;
                case 'u': if (end - p > 4) { s += '?'; p += 4; } break;
                default: s += *p;
                }
                ++p;
            } else s += *p++;
        }
        if (p < end) ++p; else ok = false;
        return s;
    }
    JValue value() {
        JValue v;
        ws();
        if (p >= end || ++depth > 64) { ok = false; return v; }
        if (*p == '{') {
            v.type = JValue::Obj; ++p; ws();
            if (p < end && *p == '}') { ++p; --depth; return v; }
            while (ok) {
                ws();
                if (p >= end || *p != '"') { ok = false; break; }
                std::string k = string();
                ws();
                if (p >= end || *p != ':') { ok = false; break; }
                ++p;
                v.objectKeys.push_back(std::move(k));
                v.objectValues.push_back(value());
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                ok = false;
            }
        } else if (*p == '[') {
            v.type = JValue::Arr; ++p; ws();
            if (p < end && *p == ']') { ++p; --depth; return v; }
            while (ok) {
                v.arr.push_back(value());
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                ok = false;
            }
        } else if (*p == '"') { v.type = JValue::Str; v.str = string(); }
        else if (lit("true")) { v.type = JValue::Bool; v.num = 1; }
        else if (lit("false")) { v.type = JValue::Bool; }
        else if (lit("null")) {}
        else {
            char* e;
            std::string tmp(p, std::min<size_t>(end - p, 64));
            v.num = std::strtod(tmp.c_str(), &e);
            if (e == tmp.c_str()) ok = false;
            p += e - tmp.c_str();
            v.type = JValue::Num;
        }
        --depth;
        return v;
    }
};

// ---- GLB -----------------------------------------------------------------------------------
struct Glb {
    JValue root;
    const uint8_t* bin = nullptr;
    size_t binSize = 0;
    // Returns pointer/stride/count/componentType for an accessor, or false.
    bool accessor(int index, const uint8_t*& data, size_t& stride, size_t& count, int& comp, int& ncomp, bool& normalized) const {
        const JValue* accs = root.get("accessors");
        const JValue* views = root.get("bufferViews");
        if (!accs || !views || index < 0) return false;
        const JValue* a = accs->at((size_t)index);
        if (!a) return false;
        double bv = a->number("bufferView", -1);
        const JValue* view = bv >= 0 ? views->at((size_t)bv) : nullptr;
        if (!view || view->number("buffer", 0) != 0 || a->get("sparse")) return false;
        comp = (int)a->number("componentType", 0);
        count = (size_t)a->number("count", 0);
        const JValue* nv = a->get("normalized");
        normalized = nv && nv->num != 0;
        const JValue* type = a->get("type");
        std::string ts = type ? type->str : "";
        ncomp = ts == "SCALAR" ? 1 : ts == "VEC2" ? 2 : ts == "VEC3" ? 3 : ts == "VEC4" ? 4 : 0;
        int csz = comp == 5120 || comp == 5121 ? 1 : comp == 5122 || comp == 5123 ? 2 : comp == 5125 || comp == 5126 ? 4 : 0;
        if (!ncomp || !csz) return false;
        size_t elem = (size_t)ncomp * csz;
        stride = (size_t)view->number("byteStride", 0);
        if (stride == 0) stride = elem;
        size_t off = (size_t)view->number("byteOffset", 0) + (size_t)a->number("byteOffset", 0);
        if (count == 0 || off > binSize || (count - 1) * stride + elem > binSize - off) return false;
        data = bin + off;
        return true;
    }
};

double compValue(const uint8_t* p, int comp, bool normalized) {
    switch (comp) {
    case 5120: { int8_t v = (int8_t)p[0]; return normalized ? std::max(v / 127.0, -1.0) : v; }
    case 5121: return normalized ? p[0] / 255.0 : p[0];
    case 5122: { int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(v / 32767.0, -1.0) : v; }
    case 5123: { uint16_t v; std::memcpy(&v, p, 2); return normalized ? v / 65535.0 : v; }
    case 5125: { uint32_t v; std::memcpy(&v, p, 4); return v; }
    default: { float v; std::memcpy(&v, p, 4); return v; }
    }
}

Mat4 nodeMatrix(const JValue& node) {
    Mat4 m;
    if (const JValue* mat = node.get("matrix"); mat && mat->arr.size() == 16) {
        for (int i = 0; i < 16; ++i) m.m[i] = (float)mat->arr[i].num;
        return m;
    }
    Vec3 t(0, 0, 0), s(1, 1, 1);
    Quat q;
    if (const JValue* v = node.get("translation"); v && v->arr.size() == 3) t = {(float)v->arr[0].num, (float)v->arr[1].num, (float)v->arr[2].num};
    if (const JValue* v = node.get("scale"); v && v->arr.size() == 3) s = {(float)v->arr[0].num, (float)v->arr[1].num, (float)v->arr[2].num};
    if (const JValue* v = node.get("rotation"); v && v->arr.size() == 4) q = {(float)v->arr[0].num, (float)v->arr[1].num, (float)v->arr[2].num, (float)v->arr[3].num};
    return Mat4::trs(t, q, s);
}

bool readGLB(const std::vector<uint8_t>& d, std::vector<ImportedObject>& out, std::string* err) {
    auto fail = [&](const char* s) { if (err) *err = s; return false; };
    auto u32at = [&](size_t o) { uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
    if (d.size() < 20 || u32at(0) != 0x46546C67) return fail("not a GLB file");
    if (u32at(4) != 2) return fail("only glTF 2.0 binary files are supported");
    size_t jsonLen = u32at(12);
    if (u32at(16) != 0x4E4F534A || jsonLen > d.size() - 20) return fail("GLB JSON chunk is invalid");
    Glb g;
    JParser jp{(const char*)d.data() + 20, (const char*)d.data() + 20 + jsonLen};
    g.root = jp.value();
    if (!jp.ok || g.root.type != JValue::Obj) return fail("GLB JSON could not be parsed");
    size_t binAt = 20 + jsonLen;
    if (binAt + 8 <= d.size() && u32at(binAt + 4) == 0x004E4942) {
        g.binSize = std::min<size_t>(u32at(binAt), d.size() - binAt - 8);
        g.bin = d.data() + binAt + 8;
    }
    if (!g.bin) return fail("GLB has no embedded binary buffer");
    const JValue* nodes = g.root.get("nodes");
    const JValue* meshes = g.root.get("meshes");
    const JValue* materials = g.root.get("materials");
    if (!nodes || !meshes) return fail("GLB contains no meshes");

    struct Item { size_t node; Mat4 world; };
    std::vector<Item> stack;
    std::vector<uint8_t> isChild(nodes->arr.size(), 0);
    for (auto& n : nodes->arr)
        if (const JValue* ch = n.get("children"))
            for (auto& c : ch->arr) if ((size_t)c.num < isChild.size()) isChild[(size_t)c.num] = 1;
    for (size_t i = 0; i < nodes->arr.size(); ++i) if (!isChild[i]) stack.push_back({i, Mat4()});
    size_t visited = 0;
    while (!stack.empty() && visited++ < 100000) {
        Item it = stack.back();
        stack.pop_back();
        const JValue& node = nodes->arr[it.node];
        Mat4 world = it.world * nodeMatrix(node);
        if (const JValue* ch = node.get("children"))
            for (auto& c : ch->arr) if ((size_t)c.num < nodes->arr.size()) stack.push_back({(size_t)c.num, world});
        const JValue* mi = node.get("mesh");
        const JValue* mesh = mi ? meshes->at((size_t)mi->num) : nullptr;
        if (!mesh) continue;
        const JValue* prims = mesh->get("primitives");
        if (!prims) continue;
        ImportedObject o;
        const JValue* nm = node.get("name");
        if (!nm) nm = mesh->get("name");
        o.name = nm && !nm->str.empty() ? nm->str : "Imported";
        o.transform = world;
        for (auto& pr : prims->arr) {
            if ((int)pr.number("mode", 4) != 4) continue;
            const JValue* attrs = pr.get("attributes");
            if (!attrs) continue;
            const uint8_t* data; size_t stride, count; int comp, nc; bool norm;
            if (!g.accessor((int)attrs->number("POSITION", -1), data, stride, count, comp, nc, norm) || comp != 5126 || nc != 3)
                return fail("GLB mesh has unsupported position data");
            uint32_t base = (uint32_t)o.mesh.vcount();
            o.mesh.resizeVerts(base + count);
            for (size_t i = 0; i < count; ++i) std::memcpy(&o.mesh.pos[base + i], data + i * stride, 12);
            const uint8_t* cd; size_t cs, cc; int ccomp, cn; bool cnorm;
            if (g.accessor((int)attrs->number("COLOR_0", -1), cd, cs, cc, ccomp, cn, cnorm) && cc == count && cn >= 3) {
                int csz = ccomp == 5126 ? 4 : (ccomp == 5123 ? 2 : 1);
                for (size_t i = 0; i < count; ++i)
                    for (int k = 0; k < 3; ++k)
                        o.mesh.col[base + i][k] = (float)compValue(cd + i * cs + (size_t)k * csz, ccomp, ccomp != 5126);
            }
            const uint8_t* id; size_t is, ic; int icomp, in; bool inorm;
            if (pr.get("indices")) {
                if (!g.accessor((int)pr.number("indices", -1), id, is, ic, icomp, in, inorm)) return fail("GLB mesh has unsupported index data");
                for (size_t i = 0; i + 2 < ic; i += 3)
                    for (int k = 0; k < 3; ++k) {
                        uint32_t v = (uint32_t)compValue(id + (i + k) * is, icomp, false);
                        if (v >= count) return fail("GLB index out of range");
                        o.mesh.tri.push_back(base + v);
                    }
            } else {
                for (uint32_t i = 0; i + 2 < count; i += 3) { o.mesh.tri.push_back(base + i); o.mesh.tri.push_back(base + i + 1); o.mesh.tri.push_back(base + i + 2); }
            }
            const JValue* mt = materials && pr.get("material") ? materials->at((size_t)pr.number("material", 0)) : nullptr;
            if (mt && !o.hasMaterial) {
                o.hasMaterial = true;
                if (const JValue* pbr = mt->get("pbrMetallicRoughness")) {
                    if (const JValue* bc = pbr->get("baseColorFactor"); bc && bc->arr.size() >= 3) {
                        o.mat.baseColor = {(float)bc->arr[0].num, (float)bc->arr[1].num, (float)bc->arr[2].num};
                        if (bc->arr.size() >= 4) o.mat.opacity = (float)bc->arr[3].num;
                    }
                    o.mat.metallic = (float)pbr->number("metallicFactor", 1.0);
                    o.mat.roughness = (float)pbr->number("roughnessFactor", 1.0);
                }
            }
        }
        if (o.mesh.tri.empty()) continue;
        o.mesh.ensureAttributes();
        out.push_back(std::move(o));
    }
    if (out.empty()) return fail("GLB contains no triangle meshes");
    return true;
}

} // namespace

bool importFile(const std::string& path, std::vector<ImportedObject>& out, std::string* error) {
    auto fail = [&](const std::string& s) { if (error) *error = s; return false; };
    std::vector<uint8_t> data;
    if (!readWholeFile(path, data)) return fail("cannot read file");
    if (data.empty()) return fail("file is empty");
    std::string ext = lower(toPath(path).extension().string());
    std::string stem = toPath(path).stem().string();
    std::vector<ImportedObject> res;
    if (ext == ".stl" || ext == ".ply") {
        ImportedObject o;
        o.name = stem.empty() ? "Imported" : stem;
        if (!(ext == ".stl" ? readSTL(data, o.mesh, error) : readPLY(data, o.mesh, error))) return false;
        res.push_back(std::move(o));
    } else if (ext == ".obj") {
        if (!readOBJ(data, res, error)) return false;
    } else if (ext == ".glb") {
        if (!readGLB(data, res, error)) return false;
    } else {
        return fail("unsupported file type '" + ext + "' (supported: .stl .obj .ply .glb)");
    }
    for (auto& o : res) {
        o.mesh.compact(true);
        if (o.mesh.vcount() > 0) o.mesh.finalize();
        std::string why;
        if (o.mesh.tcount() == 0 || !o.mesh.validate(&why)) return fail("imported geometry is invalid: " + (why.empty() ? "no triangles" : why));
    }
    out = std::move(res);
    return true;
}

} // namespace sf

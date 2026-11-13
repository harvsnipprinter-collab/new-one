#include "sf/IO.h"
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace sf {

bool readWholeFile(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    data.resize((size_t)n);
    if (n > 0) f.read((char*)data.data(), n);
    return (bool)f;
}

bool writeWholeFile(const std::string& path, const std::vector<uint8_t>& data) {
    namespace fs = std::filesystem;
    fs::path dst = toPath(path), tmp = toPath(path + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        if (!data.empty()) f.write((const char*)data.data(), (std::streamsize)data.size());
        f.flush();
        if (!f) { f.close(); std::error_code ec; fs::remove(tmp, ec); return false; }
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

namespace {

struct Bytes {
    std::vector<uint8_t> b;
    void raw(const void* p, size_t n) { const uint8_t* s = (const uint8_t*)p; b.insert(b.end(), s, s + n); }
    void u16(uint16_t v) { raw(&v, 2); }
    void u32(uint32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void text(const std::string& s) { raw(s.data(), s.size()); }
    void pad4(uint8_t fill = 0) { while (b.size() % 4) b.push_back(fill); }
};

// World-space copy of an item (transform + scale baked, winding fixed for mirrored transforms).
Mesh baked(const ExportItem& it, float scale) {
    Mesh m = *it.mesh;
    Mat4 s;
    s.m[0] = s.m[5] = s.m[10] = scale;
    Mat4 w = s * it.world;
    for (auto& p : m.pos) p = w.point(p);
    if (w.det3() < 0)
        for (size_t t = 0; t < m.tcount(); ++t) std::swap(m.tri[t * 3 + 1], m.tri[t * 3 + 2]);
    m.computeNormals();
    return m;
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf, n > 0 ? std::min<size_t>((size_t)n, sizeof(buf) - 1) : 0);
}

std::string safeName(const std::string& s) {
    std::string r;
    for (char c : s) r += (std::isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_';
    return r.empty() ? "object" : r;
}

std::string jsonEscape(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (r.size() >= 60) break; // keep generated JSON lines bounded
        if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
        else if (c >= 0x20) r += (char)c;
    }
    return r;
}

std::vector<uint8_t> writeSTL(const std::vector<ExportItem>& items, float scale) {
    Bytes o;
    char header[80] = "SculptForge 3D binary STL";
    o.raw(header, 80);
    o.u32(0);
    uint32_t count = 0;
    for (auto& it : items) {
        Mesh m = baked(it, scale);
        for (size_t t = 0; t < m.tcount(); ++t) {
            Vec3 a = m.pos[m.tri[t * 3]], b = m.pos[m.tri[t * 3 + 1]], c = m.pos[m.tri[t * 3 + 2]];
            Vec3 n = normalize(cross(b - a, c - a));
            o.raw(&n, 12); o.raw(&a, 12); o.raw(&b, 12); o.raw(&c, 12);
            o.u16(0);
            ++count;
        }
    }
    std::memcpy(o.b.data() + 80, &count, 4);
    return o.b;
}

std::vector<uint8_t> writeOBJ(const std::vector<ExportItem>& items, float scale) {
    std::string s = "# SculptForge 3D OBJ export\n";
    size_t base = 1;
    for (auto& it : items) {
        Mesh m = baked(it, scale);
        s += "o " + safeName(it.name) + "\n";
        for (size_t i = 0; i < m.vcount(); ++i)
            s += fmt("v %.7g %.7g %.7g %.4f %.4f %.4f\n", m.pos[i].x, m.pos[i].y, m.pos[i].z,
                     clamp01(m.col[i].x * it.mat.baseColor.x), clamp01(m.col[i].y * it.mat.baseColor.y),
                     clamp01(m.col[i].z * it.mat.baseColor.z));
        for (size_t i = 0; i < m.vcount(); ++i) s += fmt("vn %.5f %.5f %.5f\n", m.nrm[i].x, m.nrm[i].y, m.nrm[i].z);
        for (size_t t = 0; t < m.tcount(); ++t) {
            size_t a = m.tri[t * 3] + base, b = m.tri[t * 3 + 1] + base, c = m.tri[t * 3 + 2] + base;
            s += fmt("f %zu//%zu %zu//%zu %zu//%zu\n", a, a, b, b, c, c);
        }
        base += m.vcount();
    }
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> writePLY(const std::vector<ExportItem>& items, float scale) {
    Mesh all;
    for (auto& it : items) {
        Mesh m = baked(it, scale);
        for (auto& c : m.col) c = c * it.mat.baseColor;
        all.append(m);
    }
    Bytes o;
    o.text("ply\nformat binary_little_endian 1.0\ncomment SculptForge 3D\n");
    o.text(fmt("element vertex %zu\n", all.vcount()));
    o.text("property float x\nproperty float y\nproperty float z\nproperty float nx\nproperty float ny\nproperty float nz\n"
           "property uchar red\nproperty uchar green\nproperty uchar blue\n");
    o.text(fmt("element face %zu\nproperty list uchar int vertex_indices\nend_header\n", all.tcount()));
    auto toSRGB = [](float v) { // PLY viewers expect display-referred 8-bit colour
        v = clamp01(v);
        float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        return (uint8_t)std::lround(clamp01(s) * 255.0f);
    };
    for (size_t i = 0; i < all.vcount(); ++i) {
        o.raw(&all.pos[i], 12); o.raw(&all.nrm[i], 12);
        uint8_t c[3] = {toSRGB(all.col[i].x), toSRGB(all.col[i].y), toSRGB(all.col[i].z)};
        o.raw(c, 3);
    }
    for (size_t t = 0; t < all.tcount(); ++t) {
        uint8_t n = 3;
        o.raw(&n, 1);
        o.raw(&all.tri[t * 3], 12);
    }
    return o.b;
}

std::vector<uint8_t> writeGLB(const std::vector<ExportItem>& items, float scale) {
    Bytes bin;
    std::string views, accessors, meshes, nodes, materials, sceneNodes;
    int viewCount = 0, accCount = 0;
    auto addView = [&](size_t offset, size_t len, int target) {
        views += fmt("%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":%d}", viewCount ? "," : "", offset, len, target);
        return viewCount++;
    };
    for (size_t n = 0; n < items.size(); ++n) {
        const ExportItem& it = items[n];
        const Mesh& m = *it.mesh;
        bool flip = it.world.det3() < 0;
        AABB bb = m.bounds();
        bin.pad4();
        size_t off = bin.b.size();
        bin.raw(m.pos.data(), m.vcount() * 12);
        int vPos = addView(off, m.vcount() * 12, 34962);
        off = bin.b.size();
        for (size_t i = 0; i < m.vcount(); ++i) {
            Vec3 nn = flip ? -m.nrm[i] : m.nrm[i];
            if (length2(nn) < 0.5f) nn = Vec3(0, 1, 0);
            bin.raw(&nn, 12);
        }
        int vNrm = addView(off, m.vcount() * 12, 34962);
        off = bin.b.size();
        for (size_t i = 0; i < m.vcount(); ++i) {
            uint8_t c[4] = {(uint8_t)std::lround(clamp01(m.col[i].x) * 255), (uint8_t)std::lround(clamp01(m.col[i].y) * 255),
                            (uint8_t)std::lround(clamp01(m.col[i].z) * 255), 255};
            bin.raw(c, 4);
        }
        int vCol = addView(off, m.vcount() * 4, 34962);
        bin.pad4();
        off = bin.b.size();
        for (size_t t = 0; t < m.tcount(); ++t) {
            uint32_t idx[3] = {m.tri[t * 3], m.tri[t * 3 + (flip ? 2 : 1)], m.tri[t * 3 + (flip ? 1 : 2)]};
            bin.raw(idx, 12);
        }
        int vIdx = addView(off, m.tcount() * 12, 34963);
        int aPos = accCount++, aNrm = accCount++, aCol = accCount++, aIdx = accCount++;
        accessors += fmt("%s{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]}",
                         aPos ? "," : "", vPos, m.vcount(), bb.mn.x, bb.mn.y, bb.mn.z, bb.mx.x, bb.mx.y, bb.mx.z);
        accessors += fmt(",{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"}", vNrm, m.vcount());
        accessors += fmt(",{\"bufferView\":%d,\"componentType\":5121,\"normalized\":true,\"count\":%zu,\"type\":\"VEC4\"}", vCol, m.vcount());
        accessors += fmt(",{\"bufferView\":%d,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}", vIdx, m.tcount() * 3);
        meshes += fmt("%s{\"name\":\"%s\",\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"COLOR_0\":%d},\"indices\":%d,\"material\":%zu,\"mode\":4}]}",
                      n ? "," : "", jsonEscape(it.name).c_str(), aPos, aNrm, aCol, aIdx, n);
        const Material& mt = it.mat;
        materials += fmt("%s{\"name\":\"%s\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.5f,%.5f,%.5f,%.5f],\"metallicFactor\":%.5f,\"roughnessFactor\":%.5f},"
                         "\"emissiveFactor\":[%.5f,%.5f,%.5f],\"alphaMode\":\"%s\",\"doubleSided\":false}",
                         n ? "," : "", jsonEscape(it.name).c_str(), clamp01(mt.baseColor.x), clamp01(mt.baseColor.y), clamp01(mt.baseColor.z),
                         clamp01(mt.opacity), clamp01(mt.metallic), clamp01(mt.roughness),
                         clamp01(mt.baseColor.x * mt.emission), clamp01(mt.baseColor.y * mt.emission), clamp01(mt.baseColor.z * mt.emission),
                         mt.opacity < 0.999f ? "BLEND" : "OPAQUE");
        Mat4 s;
        s.m[0] = s.m[5] = s.m[10] = scale;
        Mat4 w = s * it.world;
        std::string mat;
        for (int k = 0; k < 16; ++k) mat += fmt("%s%.9g", k ? "," : "", w.m[k]);
        nodes += fmt("%s{\"name\":\"%s\",\"mesh\":%zu,\"matrix\":[%s]}", n ? "," : "", jsonEscape(it.name).c_str(), n, mat.c_str());
        sceneNodes += fmt("%s%zu", n ? "," : "", n);
    }
    bin.pad4();
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"SculptForge 3D\"},\"scene\":0,\"scenes\":[{\"nodes\":[" + sceneNodes +
                       "]}],\"nodes\":[" + nodes + "],\"meshes\":[" + meshes + "],\"materials\":[" + materials + "],\"accessors\":[" +
                       accessors + "],\"bufferViews\":[" + views + "],\"buffers\":[{\"byteLength\":" + std::to_string(bin.b.size()) + "}]}";
    while (json.size() % 4) json += ' ';
    Bytes o;
    o.u32(0x46546C67); o.u32(2); o.u32((uint32_t)(12 + 8 + json.size() + 8 + bin.b.size()));
    o.u32((uint32_t)json.size()); o.u32(0x4E4F534A); o.text(json);
    o.u32((uint32_t)bin.b.size()); o.u32(0x004E4942); o.raw(bin.b.data(), bin.b.size());
    return o.b;
}

// Minimal ZIP writer (stored, no compression) - enough for a valid 3MF package.
struct Zip {
    Bytes out, central;
    uint16_t count = 0;
    void add(const std::string& name, const std::string& data) {
        uint32_t crc = crc32((const uint8_t*)data.data(), data.size());
        uint32_t offset = (uint32_t)out.b.size();
        auto header = [&](Bytes& b, bool isCentral) {
            b.u32(isCentral ? 0x02014b50 : 0x04034b50);
            if (isCentral) b.u16(20);
            b.u16(20); b.u16(0); b.u16(0); b.u16(0); b.u16(0x21); // version, flags, method, time, date
            b.u32(crc); b.u32((uint32_t)data.size()); b.u32((uint32_t)data.size());
            b.u16((uint16_t)name.size()); b.u16(0);
            if (isCentral) { b.u16(0); b.u16(0); b.u16(0); b.u32(0); b.u32(offset); }
            b.text(name);
        };
        header(out, false);
        out.text(data);
        header(central, true);
        ++count;
    }
    std::vector<uint8_t> finish() {
        uint32_t cdOffset = (uint32_t)out.b.size(), cdSize = (uint32_t)central.b.size();
        out.raw(central.b.data(), central.b.size());
        out.u32(0x06054b50); out.u16(0); out.u16(0); out.u16(count); out.u16(count);
        out.u32(cdSize); out.u32(cdOffset); out.u16(0);
        return out.b;
    }
};

bool write3MF(const std::vector<ExportItem>& items, float scale, std::vector<uint8_t>& result, std::string* error) {
    std::string model = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
                        " <metadata name=\"Application\">SculptForge 3D</metadata>\n <resources>\n";
    std::string build = " <build>\n";
    int id = 1;
    for (auto& it : items) {
        Mesh m = baked(it, scale);
        m.compact(true);
        if (m.tcount() == 0) continue;
        model += fmt("  <object id=\"%d\" type=\"model\" name=\"%s\">\n   <mesh>\n    <vertices>\n", id, safeName(it.name).c_str());
        for (size_t i = 0; i < m.vcount(); ++i)
            model += fmt("     <vertex x=\"%.6f\" y=\"%.6f\" z=\"%.6f\"/>\n", m.pos[i].x, m.pos[i].y, m.pos[i].z);
        model += "    </vertices>\n    <triangles>\n";
        for (size_t t = 0; t < m.tcount(); ++t)
            model += fmt("     <triangle v1=\"%u\" v2=\"%u\" v3=\"%u\"/>\n", m.tri[t * 3], m.tri[t * 3 + 1], m.tri[t * 3 + 2]);
        model += "    </triangles>\n   </mesh>\n  </object>\n";
        build += fmt("  <item objectid=\"%d\"/>\n", id);
        ++id;
    }
    if (id == 1) { if (error) *error = "nothing to export"; return false; }
    model += " </resources>\n" + build + " </build>\n</model>\n";
    if (model.size() > 0xfff00000ull) { if (error) *error = "model too large for 3MF export"; return false; }
    Zip z;
    z.add("[Content_Types].xml",
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
          "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
          "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>\n");
    z.add("_rels/.rels",
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
          "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>\n");
    z.add("3D/3dmodel.model", model);
    result = z.finish();
    return true;
}

} // namespace

bool exportFile(const std::vector<ExportItem>& items, FileFormat format, const std::string& path, float scale, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (!(scale > 0) || !std::isfinite(scale)) return fail("export scale must be positive");
    size_t tris = 0;
    for (auto& it : items) {
        if (!it.mesh) return fail("invalid export item");
        std::string why;
        if (!it.mesh->validate(&why)) { if (error) *error = "cannot export invalid mesh: " + why; return false; }
        tris += it.mesh->tcount();
    }
    if (tris == 0) return fail("nothing to export");
    std::vector<uint8_t> data;
    switch (format) {
    case FileFormat::STL: data = writeSTL(items, scale); break;
    case FileFormat::OBJ: data = writeOBJ(items, scale); break;
    case FileFormat::PLY: data = writePLY(items, scale); break;
    case FileFormat::GLB: data = writeGLB(items, scale); break;
    case FileFormat::ThreeMF: if (!write3MF(items, scale, data, error)) return false; break;
    default: return fail("unsupported export format");
    }
    if (!writeWholeFile(path, data)) return fail("could not write the export file");
    return true;
}

} // namespace sf

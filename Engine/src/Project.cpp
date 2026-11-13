// Native .sculptforge project format (little-endian, versioned, CRC protected)
//
//   "SFORGE3D" | u32 version | u32 objectCount | u32 nextId | str appState
//   per object: state, material, u32 vcount, u32 tcount, pos, col, rough, metal, mask, tri, faceGroup
//   u32 crc32(all preceding bytes) | "SFEND\0\0\0"
#include "sf/IO.h"
#include "sf/Scene.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace sf {

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

namespace {

struct Writer {
    std::vector<uint8_t> buf;
    void raw(const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; buf.insert(buf.end(), b, b + n); }
    void u8(uint8_t v) { buf.push_back(v); }
    void u32(uint32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void v3(Vec3 v) { f32(v.x); f32(v.y); f32(v.z); }
    void str(const std::string& s) { u32((uint32_t)s.size()); raw(s.data(), s.size()); }
    template <class T> void arr(const std::vector<T>& v) { if (!v.empty()) raw(v.data(), v.size() * sizeof(T)); }
};

struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    bool ok = true;
    bool raw(void* dst, size_t k) {
        if (!ok || k > n - at) { ok = false; return false; }
        std::memcpy(dst, p + at, k);
        at += k;
        return true;
    }
    uint8_t u8() { uint8_t v = 0; raw(&v, 1); return v; }
    uint32_t u32() { uint32_t v = 0; raw(&v, 4); return v; }
    float f32() { float v = 0; raw(&v, 4); return v; }
    Vec3 v3() { Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); return v; }
    std::string str() {
        uint32_t k = u32();
        if (!ok || k > n - at) { ok = false; return {}; }
        std::string s((const char*)p + at, k);
        at += k;
        return s;
    }
    template <class T> bool arr(std::vector<T>& v, size_t count) {
        if (!ok || count > (n - at) / sizeof(T)) { ok = false; return false; }
        v.resize(count);
        return count == 0 || raw(v.data(), count * sizeof(T));
    }
};

const char kMagic[8] = {'S', 'F', 'O', 'R', 'G', 'E', '3', 'D'};
const char kEnd[8] = {'S', 'F', 'E', 'N', 'D', 0, 0, 0};

bool parse(const std::vector<uint8_t>& data, Scene& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (data.size() < 8 + 12 + 4 + 12) return fail("file is too small to be a SculptForge project");
    if (std::memcmp(data.data(), kMagic, 8) != 0) return fail("not a SculptForge project file");
    if (std::memcmp(data.data() + data.size() - 8, kEnd, 8) != 0) return fail("project file is truncated");
    uint32_t stored;
    std::memcpy(&stored, data.data() + data.size() - 12, 4);
    if (crc32(data.data(), data.size() - 12) != stored) return fail("project file is corrupted (checksum mismatch)");
    Reader r{data.data(), data.size() - 12};
    r.at = 8;
    uint32_t version = r.u32();
    if (version == 0 || version > kProjectVersion) return fail("project was saved by a newer version of SculptForge");
    uint32_t count = r.u32();
    Scene s;
    s.nextId = r.u32();
    s.appState = r.str();
    if (!r.ok || count > 100000) return fail("project header is invalid");
    for (uint32_t i = 0; i < count; ++i) {
        ObjectState st;
        st.id = r.u32();
        st.name = r.str();
        st.position = r.v3();
        st.rotation.x = r.f32(); st.rotation.y = r.f32(); st.rotation.z = r.f32(); st.rotation.w = r.f32();
        st.scale = r.v3();
        st.parent = r.u32();
        st.visible = r.u8() != 0;
        st.locked = r.u8() != 0;
        st.mat.baseColor = r.v3();
        st.mat.roughness = r.f32(); st.mat.metallic = r.f32(); st.mat.opacity = r.f32(); st.mat.emission = r.f32();
        st.mat.usePainted = r.u8() != 0;
        uint32_t vc = r.u32(), tc = r.u32();
        Mesh m;
        if (!r.ok || !r.arr(m.pos, vc) || !r.arr(m.col, vc) || !r.arr(m.rough, vc) || !r.arr(m.metal, vc) ||
            !r.arr(m.mask, vc) || !r.arr(m.tri, (size_t)tc * 3) || !r.arr(m.faceGroup, tc))
            return fail("project data ends unexpectedly");
        m.nrm.assign(vc, Vec3(0, 1, 0));
        std::string why;
        if (st.id == 0 || s.find(st.id)) return fail("project contains an invalid object id");
        if (!m.validate(&why)) { if (error) *error = "object '" + st.name + "' has invalid geometry: " + why; return false; }
        s.insert(st, std::move(m), -1);
    }
    if (!r.ok || r.at != r.n) return fail("project file has unexpected trailing data");
    for (auto& o : s.objects) // drop dangling or cyclic parent links instead of failing the load
        if (o->st.parent && (!s.find(o->st.parent) || s.wouldCycle(o->st.id, o->st.parent))) o->st.parent = 0;
    out = std::move(s);
    return true;
}


} // namespace

bool saveProject(const Scene& s, const std::string& path, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    Writer w;
    w.raw(kMagic, 8);
    w.u32(kProjectVersion);
    w.u32((uint32_t)s.objects.size());
    w.u32(s.nextId);
    w.str(s.appState);
    for (auto& o : s.objects) {
        std::string why;
        if (!o->mesh.validate(&why)) return fail("refusing to save: object '" + o->st.name + "' is invalid (" + why + ")");
        const ObjectState& st = o->st;
        w.u32(st.id); w.str(st.name); w.v3(st.position);
        w.f32(st.rotation.x); w.f32(st.rotation.y); w.f32(st.rotation.z); w.f32(st.rotation.w);
        w.v3(st.scale); w.u32(st.parent); w.u8(st.visible); w.u8(st.locked);
        w.v3(st.mat.baseColor); w.f32(st.mat.roughness); w.f32(st.mat.metallic); w.f32(st.mat.opacity);
        w.f32(st.mat.emission); w.u8(st.mat.usePainted);
        w.u32((uint32_t)o->mesh.vcount()); w.u32((uint32_t)o->mesh.tcount());
        w.arr(o->mesh.pos); w.arr(o->mesh.col); w.arr(o->mesh.rough); w.arr(o->mesh.metal); w.arr(o->mesh.mask);
        w.arr(o->mesh.tri); w.arr(o->mesh.faceGroup);
    }
    w.u32(crc32(w.buf.data(), w.buf.size()));
    w.raw(kEnd, 8);

    namespace fs = std::filesystem;
    fs::path dst = toPath(path), tmp = toPath(path + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return fail("cannot create temporary file for saving");
        f.write((const char*)w.buf.data(), (std::streamsize)w.buf.size());
        f.flush();
        if (!f) { f.close(); std::error_code ec; fs::remove(tmp, ec); return fail("write failed (disk full?)"); }
    }
    // verify what actually reached the disk before replacing the previous project
    std::vector<uint8_t> check;
    if (!readWholeFile(path + ".tmp", check) || check != w.buf) {
        std::error_code ec; fs::remove(tmp, ec);
        return fail("verification of the written file failed");
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) { fs::remove(tmp, ec); return fail("could not replace the project file"); }
    return true;
}

bool loadProject(const std::string& path, Scene& out, std::string* error) {
    std::vector<uint8_t> data;
    if (!readWholeFile(path, data)) { if (error) *error = "cannot read file"; return false; }
    return parse(data, out, error);
}

} // namespace sf

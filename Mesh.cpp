#include "sf/Mesh.h"
#include <cstring>

namespace sf {

void Mesh::clear() {
    pos.clear(); nrm.clear(); col.clear(); rough.clear(); metal.clear(); mask.clear();
    tri.clear(); faceGroup.clear(); vtStart.clear(); vtList.clear();
}

void Mesh::resizeVerts(size_t n) {
    pos.resize(n);
    nrm.resize(n, Vec3(0, 1, 0));
    col.resize(n, Vec3(1, 1, 1));
    rough.resize(n, 0.6f);
    metal.resize(n, 0.0f);
    mask.resize(n, 0.0f);
}

void Mesh::ensureAttributes() {
    resizeVerts(pos.size());
    faceGroup.resize(tcount(), 0);
}

uint32_t Mesh::addVertex(Vec3 p) {
    uint32_t i = (uint32_t)pos.size();
    resizeVerts(i + 1);
    pos[i] = p;
    return i;
}

void Mesh::addTri(uint32_t a, uint32_t b, uint32_t c, uint32_t group) {
    tri.push_back(a); tri.push_back(b); tri.push_back(c);
    faceGroup.push_back(group);
}

void Mesh::setVertexLerp(uint32_t dst, uint32_t a, uint32_t b, float t) {
    pos[dst] = lerp(pos[a], pos[b], t);
    nrm[dst] = normalize(lerp(nrm[a], nrm[b], t));
    col[dst] = lerp(col[a], col[b], t);
    rough[dst] = lerp(rough[a], rough[b], t);
    metal[dst] = lerp(metal[a], metal[b], t);
    mask[dst] = lerp(mask[a], mask[b], t);
}

void Mesh::buildAdjacency() {
    size_t n = vcount();
    vtStart.assign(n + 1, 0);
    for (uint32_t v : tri) vtStart[v + 1]++;
    for (size_t i = 0; i < n; ++i) vtStart[i + 1] += vtStart[i];
    vtList.resize(tri.size());
    std::vector<uint32_t> cur(vtStart.begin(), vtStart.end() - 1);
    for (size_t t = 0; t < tcount(); ++t)
        for (int k = 0; k < 3; ++k) vtList[cur[tri[t * 3 + k]]++] = (uint32_t)t;
}

Vec3 Mesh::triNormalRaw(size_t t) const {
    Vec3 a = pos[tri[t * 3]], b = pos[tri[t * 3 + 1]], c = pos[tri[t * 3 + 2]];
    return cross(b - a, c - a);
}

void Mesh::computeNormals() {
    nrm.assign(vcount(), Vec3(0, 0, 0));
    for (size_t t = 0; t < tcount(); ++t) {
        Vec3 n = triNormalRaw(t);
        nrm[tri[t * 3]] += n; nrm[tri[t * 3 + 1]] += n; nrm[tri[t * 3 + 2]] += n;
    }
    for (auto& n : nrm) {
        Vec3 u = normalize(n);
        n = (u.x == 0 && u.y == 0 && u.z == 0) ? Vec3(0, 1, 0) : u;
    }
}

void Mesh::computeNormalFor(uint32_t v) {
    Vec3 n(0, 0, 0);
    for (uint32_t i = vtStart[v]; i < vtStart[v + 1]; ++i) n += triNormalRaw(vtList[i]);
    Vec3 u = normalize(n);
    if (u.x != 0 || u.y != 0 || u.z != 0) nrm[v] = u;
}

AABB Mesh::bounds() const {
    AABB b;
    for (const auto& p : pos) b.expand(p);
    return b;
}

void Mesh::finalize() {
    ensureAttributes();
    buildAdjacency();
    computeNormals();
}

bool Mesh::validate(std::string* error) const {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    size_t n = vcount();
    if (tri.size() % 3 != 0) return fail("index count is not a multiple of 3");
    if (nrm.size() != n || col.size() != n || rough.size() != n || metal.size() != n || mask.size() != n)
        return fail("attribute array size mismatch");
    if (faceGroup.size() != tcount()) return fail("face group array size mismatch");
    for (uint32_t i : tri) if (i >= n) return fail("triangle index out of range");
    for (size_t i = 0; i < n; ++i) {
        if (!isFinite(pos[i])) return fail("non-finite vertex position");
        if (!isFinite(nrm[i])) return fail("non-finite vertex normal");
        if (!isFinite(col[i])) return fail("non-finite vertex colour");
        if (!std::isfinite(mask[i]) || !std::isfinite(rough[i]) || !std::isfinite(metal[i]))
            return fail("non-finite vertex attribute");
    }
    return true;
}

void Mesh::transform(const Mat4& m) {
    for (auto& p : pos) p = m.point(p);
    if (m.det3() < 0) // mirrored: restore outward winding
        for (size_t t = 0; t < tcount(); ++t) std::swap(tri[t * 3 + 1], tri[t * 3 + 2]);
    computeNormals();
}

void Mesh::append(const Mesh& o) {
    uint32_t base = (uint32_t)vcount();
    uint32_t groupBase = 0;
    for (uint32_t g : faceGroup) groupBase = std::max(groupBase, g + 1);
    pos.insert(pos.end(), o.pos.begin(), o.pos.end());
    nrm.insert(nrm.end(), o.nrm.begin(), o.nrm.end());
    col.insert(col.end(), o.col.begin(), o.col.end());
    rough.insert(rough.end(), o.rough.begin(), o.rough.end());
    metal.insert(metal.end(), o.metal.begin(), o.metal.end());
    mask.insert(mask.end(), o.mask.begin(), o.mask.end());
    tri.reserve(tri.size() + o.tri.size());
    for (uint32_t i : o.tri) tri.push_back(i + base);
    for (uint32_t g : o.faceGroup) faceGroup.push_back(g + groupBase);
    ensureAttributes();
    buildAdjacency();
}

void Mesh::compact(bool removeDegenerate) {
    ensureAttributes();
    size_t n = vcount();
    std::vector<uint32_t> newTri;
    std::vector<uint32_t> newGroup;
    newTri.reserve(tri.size());
    for (size_t t = 0; t < tcount(); ++t) {
        uint32_t a = tri[t * 3], b = tri[t * 3 + 1], c = tri[t * 3 + 2];
        if (removeDegenerate && (a == b || b == c || a == c)) continue;
        newTri.push_back(a); newTri.push_back(b); newTri.push_back(c);
        newGroup.push_back(faceGroup[t]);
    }
    std::vector<uint32_t> remap(n, UINT32_MAX);
    uint32_t next = 0;
    for (uint32_t& i : newTri) {
        if (remap[i] == UINT32_MAX) remap[i] = next++;
        i = remap[i];
    }
    Mesh out;
    out.resizeVerts(next);
    for (size_t i = 0; i < n; ++i) {
        uint32_t r = remap[i];
        if (r == UINT32_MAX) continue;
        out.pos[r] = pos[i]; out.nrm[r] = nrm[i]; out.col[r] = col[i];
        out.rough[r] = rough[i]; out.metal[r] = metal[i]; out.mask[r] = mask[i];
    }
    out.tri = std::move(newTri);
    out.faceGroup = std::move(newGroup);
    out.buildAdjacency();
    *this = std::move(out);
}

size_t Mesh::memoryBytes() const {
    return vcount() * (sizeof(Vec3) * 3 + sizeof(float) * 3) + tri.size() * 4 + faceGroup.size() * 4 +
           vtStart.size() * 4 + vtList.size() * 4;
}

} // namespace sf

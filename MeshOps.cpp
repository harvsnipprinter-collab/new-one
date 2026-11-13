#include "sf/MeshOps.h"
#include <numeric>
#include <unordered_map>

namespace sf {

size_t weldVertices(Mesh& m, float eps) {
    m.ensureAttributes();
    size_t n = m.vcount();
    if (n == 0) return 0;
    eps = std::max(eps, 1e-12f);
    float inv = 1.0f / eps;
    float eps2 = eps * eps;
    auto cellOf = [&](Vec3 p, int64_t c[3]) {
        for (int i = 0; i < 3; ++i) c[i] = (int64_t)std::floor((double)p[i] * inv);
    };
    auto hashCell = [](int64_t x, int64_t y, int64_t z) {
        return (uint64_t)(x * 73856093LL) ^ (uint64_t)(y * 19349663LL) ^ (uint64_t)(z * 83492791LL);
    };
    std::unordered_map<uint64_t, uint32_t> head; // cell hash -> first representative vertex
    head.reserve(n);
    std::vector<uint32_t> next(n, UINT32_MAX), remap(n);
    size_t merged = 0;
    for (uint32_t v = 0; v < n; ++v) {
        int64_t c[3];
        cellOf(m.pos[v], c);
        uint32_t found = UINT32_MAX;
        for (int dx = -1; dx <= 1 && found == UINT32_MAX; ++dx)
            for (int dy = -1; dy <= 1 && found == UINT32_MAX; ++dy)
                for (int dz = -1; dz <= 1 && found == UINT32_MAX; ++dz) {
                    auto it = head.find(hashCell(c[0] + dx, c[1] + dy, c[2] + dz));
                    if (it == head.end()) continue;
                    for (uint32_t u = it->second; u != UINT32_MAX; u = next[u])
                        if (length2(m.pos[u] - m.pos[v]) <= eps2) { found = u; break; }
                }
        if (found != UINT32_MAX) {
            remap[v] = found;
            ++merged;
        } else {
            remap[v] = v;
            uint64_t h = hashCell(c[0], c[1], c[2]);
            auto it = head.find(h);
            if (it == head.end()) head[h] = v;
            else { next[v] = it->second; it->second = v; }
        }
    }
    for (uint32_t& i : m.tri) i = remap[i];
    m.compact(true);
    return merged;
}

void flipNormals(Mesh& m) {
    for (size_t t = 0; t < m.tcount(); ++t) std::swap(m.tri[t * 3 + 1], m.tri[t * 3 + 2]);
    m.computeNormals();
}

static std::vector<uint32_t> componentLabels(const Mesh& m, int& count) {
    size_t n = m.vcount();
    std::vector<uint32_t> parent(n);
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (size_t t = 0; t < m.tcount(); ++t) {
        uint32_t a = find(m.tri[t * 3]), b = find(m.tri[t * 3 + 1]), c = find(m.tri[t * 3 + 2]);
        parent[b] = a;
        parent[find(c)] = a;
    }
    std::vector<uint32_t> label(n, UINT32_MAX), triLabel(m.tcount());
    count = 0;
    for (size_t t = 0; t < m.tcount(); ++t) {
        uint32_t r = find(m.tri[t * 3]);
        if (label[r] == UINT32_MAX) label[r] = count++;
        triLabel[t] = label[r];
    }
    return triLabel;
}

int countComponents(const Mesh& m) {
    int c = 0;
    componentLabels(m, c);
    return c;
}

std::vector<Mesh> separateComponents(const Mesh& m) {
    int count = 0;
    std::vector<uint32_t> triLabel = componentLabels(m, count);
    std::vector<Mesh> out(count);
    std::vector<uint32_t> remap(m.vcount(), UINT32_MAX);
    for (size_t t = 0; t < m.tcount(); ++t) {
        Mesh& o = out[triLabel[t]];
        uint32_t idx[3];
        for (int k = 0; k < 3; ++k) {
            uint32_t v = m.tri[t * 3 + k];
            if (remap[v] == UINT32_MAX) {
                remap[v] = o.addVertex(m.pos[v]);
                uint32_t r = remap[v];
                o.nrm[r] = m.nrm[v]; o.col[r] = m.col[v]; o.rough[r] = m.rough[v];
                o.metal[r] = m.metal[v]; o.mask[r] = m.mask[v];
            }
            idx[k] = remap[v];
        }
        o.addTri(idx[0], idx[1], idx[2], m.faceGroup.size() > t ? m.faceGroup[t] : 0);
    }
    for (auto& o : out) o.finalize();
    return out;
}

void smoothMesh(Mesh& m, int iterations, float factor, bool respectMask) {
    if (m.vtStart.size() != m.vcount() + 1) m.buildAdjacency();
    std::vector<Vec3> np(m.vcount());
    for (int it = 0; it < iterations; ++it) {
        for (uint32_t v = 0; v < m.vcount(); ++v) {
            Vec3 sum(0, 0, 0);
            int cnt = 0;
            forEachNeighbor(m, v, [&](uint32_t u) { sum += m.pos[u]; ++cnt; });
            float w = factor * (respectMask ? 1.0f - m.mask[v] : 1.0f);
            np[v] = cnt ? lerp(m.pos[v], sum / (float)cnt, w) : m.pos[v];
        }
        m.pos.swap(np);
    }
    m.computeNormals();
}

Mesh mirrorMesh(const Mesh& src, int axis, bool weldSeam) {
    Mesh out = src;
    Mesh mir = src;
    for (auto& p : mir.pos) p[axis] = -p[axis];
    for (size_t t = 0; t < mir.tcount(); ++t) std::swap(mir.tri[t * 3 + 1], mir.tri[t * 3 + 2]);
    out.append(mir);
    if (weldSeam) {
        float eps = std::max(length(out.bounds().size()) * 1e-5f, 1e-7f);
        weldVertices(out, eps);
    }
    out.finalize();
    return out;
}

// ---- masks ---------------------------------------------------------------------------------
void maskClear(Mesh& m) { std::fill(m.mask.begin(), m.mask.end(), 0.0f); }
void maskInvert(Mesh& m) { for (float& v : m.mask) v = 1.0f - v; }

void maskBlur(Mesh& m, int iterations) {
    std::vector<float> nm(m.vcount());
    for (int it = 0; it < iterations; ++it) {
        for (uint32_t v = 0; v < m.vcount(); ++v) {
            float sum = m.mask[v]; int cnt = 1;
            forEachNeighbor(m, v, [&](uint32_t u) { sum += m.mask[u]; ++cnt; });
            nm[v] = sum / cnt;
        }
        m.mask.swap(nm);
    }
}

void maskSharpen(Mesh& m, float amount) {
    for (float& v : m.mask) v = clamp01(0.5f + (v - 0.5f) * (1.0f + amount));
}

void maskGrow(Mesh& m) {
    std::vector<float> nm(m.mask);
    for (uint32_t v = 0; v < m.vcount(); ++v)
        forEachNeighbor(m, v, [&](uint32_t u) { nm[v] = std::max(nm[v], m.mask[u]); });
    m.mask.swap(nm);
}

void maskShrink(Mesh& m) {
    std::vector<float> nm(m.mask);
    for (uint32_t v = 0; v < m.vcount(); ++v)
        forEachNeighbor(m, v, [&](uint32_t u) { nm[v] = std::min(nm[v], m.mask[u]); });
    m.mask.swap(nm);
}

void maskByCavity(Mesh& m, float sensitivity, bool invert) {
    for (uint32_t v = 0; v < m.vcount(); ++v) {
        Vec3 sum(0, 0, 0); float len = 0; int cnt = 0;
        forEachNeighbor(m, v, [&](uint32_t u) { sum += m.pos[u]; len += length(m.pos[u] - m.pos[v]); ++cnt; });
        float c = 0;
        if (cnt && len > 0) {
            Vec3 avg = sum / (float)cnt;
            c = dot(avg - m.pos[v], m.nrm[v]) / (len / cnt); // > 0 in concave areas
        }
        if (invert) c = -c;
        m.mask[v] = clamp01(c * sensitivity * 4.0f);
    }
}

} // namespace sf

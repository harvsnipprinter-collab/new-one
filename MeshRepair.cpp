#include "sf/MeshOps.h"
#include <numeric>
#include <unordered_map>

namespace sf {

std::vector<EdgeRef> buildSortedEdges(const Mesh& m) {
    std::vector<EdgeRef> e;
    e.reserve(m.tri.size());
    for (size_t t = 0; t < m.tcount(); ++t) {
        const uint32_t* v = &m.tri[t * 3];
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) continue;
        for (int k = 0; k < 3; ++k) {
            uint32_t a = v[k], b = v[(k + 1) % 3];
            e.push_back({((uint64_t)std::min(a, b) << 32) | std::max(a, b), (uint32_t)t, (uint8_t)k, a < b});
        }
    }
    std::sort(e.begin(), e.end(), [](const EdgeRef& x, const EdgeRef& y) { return x.key < y.key; });
    return e;
}

// Directed boundary edges a->b (edges used by exactly one triangle), as next[a] = b.
static std::unordered_map<uint32_t, uint32_t> boundaryNext(const Mesh& m) {
    std::unordered_map<uint32_t, uint32_t> next;
    std::vector<EdgeRef> e = buildSortedEdges(m);
    for (size_t i = 0; i < e.size();) {
        size_t j = i;
        while (j < e.size() && e[j].key == e[i].key) ++j;
        if (j - i == 1) {
            const uint32_t* v = &m.tri[(size_t)e[i].tri * 3];
            uint32_t a = v[e[i].corner], b = v[(e[i].corner + 1) % 3];
            next[a] = b; // non-manifold boundary vertices keep only one outgoing edge
        }
        i = j;
    }
    return next;
}

static std::vector<std::vector<uint32_t>> boundaryLoops(const Mesh& m) {
    auto next = boundaryNext(m);
    std::vector<std::vector<uint32_t>> loops;
    std::unordered_map<uint32_t, bool> used;
    for (auto& kv : next) {
        if (used[kv.first]) continue;
        std::vector<uint32_t> loop;
        uint32_t v = kv.first;
        bool closed = false;
        while (true) {
            if (used[v]) { closed = (v == kv.first); break; }
            used[v] = true;
            loop.push_back(v);
            auto it = next.find(v);
            if (it == next.end()) break;
            v = it->second;
        }
        if (closed && loop.size() >= 3) loops.push_back(std::move(loop));
    }
    return loops;
}

// Ear-clip polygon `poly` (already in the winding the new triangles should have).
static void triangulateLoop(Mesh& m, std::vector<uint32_t> poly) {
    size_t n = poly.size();
    if (n < 3) return;
    if (n > 400) { // large loop: fan around the centroid (adds one vertex)
        Vec3 c(0, 0, 0);
        for (uint32_t v : poly) c += m.pos[v];
        uint32_t cv = m.addVertex(c / (float)n);
        m.col[cv] = m.col[poly[0]]; m.rough[cv] = m.rough[poly[0]]; m.metal[cv] = m.metal[poly[0]];
        for (size_t i = 0; i < n; ++i) m.addTri(cv, poly[i], poly[(i + 1) % n]);
        return;
    }
    Vec3 N(0, 0, 0); // Newell normal
    for (size_t i = 0; i < n; ++i) {
        Vec3 a = m.pos[poly[i]], b = m.pos[poly[(i + 1) % n]];
        N += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
    }
    N = normalize(N);
    auto inside = [&](Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
        return dot(cross(b - a, p - a), N) > 0 && dot(cross(c - b, p - b), N) > 0 && dot(cross(a - c, p - c), N) > 0;
    };
    while (poly.size() > 3) {
        size_t k = poly.size(), bestI = 0;
        float bestScore = -1e30f;
        bool found = false;
        for (size_t i = 0; i < k; ++i) {
            uint32_t ia = poly[(i + k - 1) % k], ib = poly[i], ic = poly[(i + 1) % k];
            Vec3 a = m.pos[ia], b = m.pos[ib], c = m.pos[ic];
            float convex = dot(cross(b - a, c - b), N);
            if (convex <= 0) { if (!found && convex > bestScore) { bestScore = convex; bestI = i; } continue; }
            bool blocked = false;
            for (size_t j = 0; j < k && !blocked; ++j) {
                uint32_t v = poly[j];
                if (v == ia || v == ib || v == ic) continue;
                blocked = inside(m.pos[v], a, b, c);
            }
            if (blocked) continue;
            // prefer well-shaped ears: maximise the minimum edge-normalised area
            float l = std::max({length2(b - a), length2(c - b), length2(a - c)});
            float score = l > 0 ? convex / l : 0;
            if (!found || score > bestScore) { found = true; bestScore = score; bestI = i; }
        }
        size_t i = bestI; // if no valid ear exists (degenerate input) clip the least-bad corner
        m.addTri(poly[(i + k - 1) % k], poly[i], poly[(i + 1) % k]);
        poly.erase(poly.begin() + i);
    }
    m.addTri(poly[0], poly[1], poly[2]);
}

size_t fillHoles(Mesh& m, const std::vector<uint8_t>* onlyVerts) {
    m.ensureAttributes();
    auto loops = boundaryLoops(m);
    size_t filled = 0;
    for (auto& loop : loops) {
        if (onlyVerts) {
            bool ok = true;
            for (uint32_t v : loop) if (v >= onlyVerts->size() || !(*onlyVerts)[v]) { ok = false; break; }
            if (!ok) continue;
        }
        std::reverse(loop.begin(), loop.end()); // new faces must traverse boundary edges backwards
        triangulateLoop(m, loop);
        ++filled;
    }
    m.finalize();
    return filled;
}

size_t cutPlane(Mesh& m, Vec3 P, Vec3 N, bool cap) {
    m.ensureAttributes();
    N = normalize(N);
    size_t n0 = m.vcount();
    std::vector<float> d(n0);
    for (size_t i = 0; i < n0; ++i) d[i] = dot(m.pos[i] - P, N);
    std::unordered_map<uint64_t, uint32_t> cutVert;
    std::vector<uint8_t> isCut;
    auto getCut = [&](uint32_t keep, uint32_t rem) {
        uint64_t key = ((uint64_t)std::min(keep, rem) << 32) | std::max(keep, rem);
        auto it = cutVert.find(key);
        if (it != cutVert.end()) return it->second;
        float t = d[keep] / (d[keep] - d[rem]);
        uint32_t nv = m.addVertex(Vec3());
        m.setVertexLerp(nv, keep, rem, t);
        cutVert[key] = nv;
        return nv;
    };
    std::vector<uint32_t> nt, ng;
    nt.reserve(m.tri.size());
    for (size_t t = 0; t < m.tcount(); ++t) {
        uint32_t v[3] = {m.tri[t * 3], m.tri[t * 3 + 1], m.tri[t * 3 + 2]};
        bool keep[3] = {d[v[0]] <= 0, d[v[1]] <= 0, d[v[2]] <= 0};
        int nk = keep[0] + keep[1] + keep[2];
        if (nk == 0) continue;
        uint32_t g = m.faceGroup[t];
        if (nk == 3) { nt.insert(nt.end(), v, v + 3); ng.push_back(g); continue; }
        // clip polygon, preserving winding
        uint32_t poly[4]; int pc = 0;
        for (int k = 0; k < 3; ++k) {
            int k1 = (k + 1) % 3;
            if (keep[k]) poly[pc++] = v[k];
            if (keep[k] != keep[k1]) poly[pc++] = keep[k] ? getCut(v[k], v[k1]) : getCut(v[k1], v[k]);
        }
        nt.insert(nt.end(), {poly[0], poly[1], poly[2]}); ng.push_back(g);
        if (pc == 4) { nt.insert(nt.end(), {poly[0], poly[2], poly[3]}); ng.push_back(g); }
    }
    m.tri.swap(nt);
    m.faceGroup.swap(ng);
    if (cap && !cutVert.empty()) {
        isCut.assign(m.vcount(), 0);
        for (auto& kv : cutVert) isCut[kv.second] = 1;
        // vertices lying exactly on the plane also belong to the cut outline
        for (size_t i = 0; i < n0; ++i) if (d[i] == 0) isCut[i] = 1;
        fillHoles(m, &isCut);
    }
    float eps = std::max(length(m.bounds().size()) * 1e-7f, 1e-9f);
    weldVertices(m, eps);
    m.finalize();
    return m.tcount();
}

Mesh extractMasked(const Mesh& src, float thickness) {
    Mesh out;
    std::vector<uint32_t> remap(src.vcount(), UINT32_MAX);
    for (size_t t = 0; t < src.tcount(); ++t) {
        const uint32_t* v = &src.tri[t * 3];
        if (src.mask[v[0]] <= 0.5f || src.mask[v[1]] <= 0.5f || src.mask[v[2]] <= 0.5f) continue;
        uint32_t idx[3];
        for (int k = 0; k < 3; ++k) {
            if (remap[v[k]] == UINT32_MAX) {
                uint32_t r = remap[v[k]] = out.addVertex(src.pos[v[k]]);
                out.nrm[r] = src.nrm[v[k]]; out.col[r] = src.col[v[k]];
                out.rough[r] = src.rough[v[k]]; out.metal[r] = src.metal[v[k]];
            }
            idx[k] = remap[v[k]];
        }
        out.addTri(idx[0], idx[1], idx[2]);
    }
    if (out.tcount() == 0) return out;
    if (thickness > 0) {
        uint32_t n = (uint32_t)out.vcount();
        size_t t0 = out.tcount();
        out.buildAdjacency();
        auto next = boundaryNext(out);
        out.resizeVerts((size_t)n * 2);
        for (uint32_t i = 0; i < n; ++i) { // i = inner (original surface), i+n = outer
            out.pos[i + n] = out.pos[i] + out.nrm[i] * thickness;
            out.nrm[i + n] = out.nrm[i]; out.col[i + n] = out.col[i];
            out.rough[i + n] = out.rough[i]; out.metal[i + n] = out.metal[i];
        }
        for (size_t t = 0; t < t0; ++t) {
            uint32_t a = out.tri[t * 3], b = out.tri[t * 3 + 1], c = out.tri[t * 3 + 2];
            out.tri[t * 3] = a + n; out.tri[t * 3 + 1] = b + n; out.tri[t * 3 + 2] = c + n; // outer keeps winding
            out.addTri(a, c, b); // inner flipped
        }
        for (auto& kv : next) { // wall quads along the open boundary
            uint32_t a = kv.first, b = kv.second;
            out.addTri(b + n, a + n, a);
            out.addTri(b + n, a, b);
        }
    }
    out.finalize();
    return out;
}

MeshAnalysis analyzeMesh(const Mesh& m, float thinThreshold, int maxSamples) {
    MeshAnalysis r;
    r.vertices = m.vcount();
    r.triangles = m.tcount();
    r.bounds = m.bounds();
    std::vector<uint8_t> used(m.vcount(), 0);
    for (size_t t = 0; t < m.tcount(); ++t) {
        uint32_t a = m.tri[t * 3], b = m.tri[t * 3 + 1], c = m.tri[t * 3 + 2];
        used[a] = used[b] = used[c] = 1;
        Vec3 pa = m.pos[a], pb = m.pos[b], pc = m.pos[c];
        Vec3 cr = cross(pb - pa, pc - pa);
        double area = 0.5 * length(cr);
        if (a == b || b == c || a == c || area < 1e-14) r.degenerateTriangles++;
        r.surfaceArea += area;
        r.volume += (double)dot(pa, cross(pb, pc)) / 6.0;
    }
    for (uint8_t u : used) if (!u) r.looseVertices++;
    std::vector<EdgeRef> e = buildSortedEdges(m);
    for (size_t i = 0; i < e.size();) {
        size_t j = i;
        int fwd = 0;
        while (j < e.size() && e[j].key == e[i].key) { fwd += e[j].forward; ++j; }
        size_t cnt = j - i;
        if (cnt == 1) r.boundaryEdges++;
        else if (cnt > 2) r.nonManifoldEdges++;
        else if (fwd != 1) r.inconsistentEdges++;
        i = j;
    }
    r.holes = boundaryLoops(m).size();
    r.components = countComponents(m);
    r.watertight = m.tcount() > 0 && r.boundaryEdges == 0 && r.nonManifoldEdges == 0 && r.inconsistentEdges == 0;
    r.invertedNormals = r.watertight && r.volume < 0;
    if (thinThreshold > 0 && m.tcount() > 0 && m.vcount() > 0) {
        BVH bvh;
        bvh.build(m);
        size_t step = std::max<size_t>(1, m.vcount() / (size_t)std::max(1, maxSamples));
        float eps = std::max(length(r.bounds.size()) * 1e-5f, 1e-7f);
        float minT = 1e30f;
        for (size_t v = 0; v < m.vcount(); v += step) {
            if (!used[v]) continue;
            Vec3 d = -m.nrm[v];
            RayHit h = bvh.raycast(m, m.pos[v] + d * eps, d);
            if (!h.hit) continue;
            r.thicknessSamples++;
            minT = std::min(minT, h.t + eps);
            if (h.t + eps < thinThreshold) r.thinSamples++;
        }
        if (r.thicknessSamples) r.minThickness = minT;
    }
    return r;
}

} // namespace sf

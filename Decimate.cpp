#include "sf/MeshOps.h"
#include "sf/Topology.h"
#include <queue>

namespace sf {

namespace {

// Symmetric 4x4 quadric stored as 10 doubles.
struct Quadric {
    double q[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    void addPlane(double a, double b, double c, double d, double w) {
        q[0] += w * a * a; q[1] += w * a * b; q[2] += w * a * c; q[3] += w * a * d;
        q[4] += w * b * b; q[5] += w * b * c; q[6] += w * b * d;
        q[7] += w * c * c; q[8] += w * c * d; q[9] += w * d * d;
    }
    void add(const Quadric& o) { for (int i = 0; i < 10; ++i) q[i] += o.q[i]; }
    double eval(Vec3 p) const {
        double x = p.x, y = p.y, z = p.z;
        return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z +
               2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9];
    }
};

// Edge-collapse machinery shared by decimation and dynamic topology.
struct Collapser {
    Mesh& m;
    std::vector<std::vector<uint32_t>> vt; // vertex -> triangles (lazily copied from CSR adjacency)
    std::vector<uint8_t> loaded, deadT;
    std::vector<uint32_t> stamp;
    uint32_t gen = 0;
    size_t liveTris;

    explicit Collapser(Mesh& mesh) : m(mesh) {
        if (m.vtStart.size() != m.vcount() + 1) m.buildAdjacency();
        vt.resize(m.vcount());
        loaded.assign(m.vcount(), 0);
        deadT.assign(m.tcount(), 0);
        stamp.assign(m.vcount(), 0);
        liveTris = m.tcount();
    }
    std::vector<uint32_t>& tris(uint32_t v) {
        if (!loaded[v]) {
            vt[v].assign(m.vtList.begin() + m.vtStart[v], m.vtList.begin() + m.vtStart[v + 1]);
            loaded[v] = 1;
        }
        return vt[v];
    }
    bool has(uint32_t t, uint32_t v) const {
        return m.tri[(size_t)t * 3] == v || m.tri[(size_t)t * 3 + 1] == v || m.tri[(size_t)t * 3 + 2] == v;
    }
    int sharedTris(uint32_t a, uint32_t b) {
        int s = 0;
        for (uint32_t t : tris(a)) if (!deadT[t] && has(t, b)) ++s;
        return s;
    }
    bool isBoundary(uint32_t v) {
        for (uint32_t t : tris(v)) {
            if (deadT[t]) continue;
            for (int k = 0; k < 3; ++k) {
                uint32_t u = m.tri[(size_t)t * 3 + k];
                if (u != v && sharedTris(v, u) == 1) return true;
            }
        }
        return false;
    }
    bool canCollapse(uint32_t a, uint32_t b, Vec3 p) {
        int s = sharedTris(a, b);
        if (s != 1 && s != 2) return false;
        // link condition: the only common neighbours are the vertices opposite the shared triangles
        ++gen;
        for (uint32_t t : tris(a)) {
            if (deadT[t]) continue;
            for (int k = 0; k < 3; ++k) stamp[m.tri[(size_t)t * 3 + k]] = gen;
        }
        int common = 0;
        uint32_t markGen = ++gen;
        for (uint32_t t : tris(b)) {
            if (deadT[t]) continue;
            for (int k = 0; k < 3; ++k) {
                uint32_t u = m.tri[(size_t)t * 3 + k];
                if (u == a || u == b) continue;
                if (stamp[u] == markGen - 1) { stamp[u] = markGen; ++common; }
            }
        }
        if (common != s) return false;
        // reject triangles that would flip or collapse to a sliver
        for (int pass = 0; pass < 2; ++pass) {
            uint32_t v = pass == 0 ? a : b, other = pass == 0 ? b : a;
            for (uint32_t t : tris(v)) {
                if (deadT[t] || has(t, other)) continue;
                Vec3 q[3], r[3];
                for (int k = 0; k < 3; ++k) {
                    uint32_t u = m.tri[(size_t)t * 3 + k];
                    q[k] = m.pos[u];
                    r[k] = (u == v) ? p : m.pos[u];
                }
                Vec3 n0 = cross(q[1] - q[0], q[2] - q[0]), n1 = cross(r[1] - r[0], r[2] - r[0]);
                float l0 = length(n0), l1 = length(n1);
                if (l1 < 1e-12f) return false;
                if (l0 > 0 && dot(n0, n1) < 0.2f * l0 * l1) return false;
            }
        }
        return true;
    }
    // Merge b into a; a moves to position p with attributes blended by t (0 = a, 1 = b).
    void collapse(uint32_t a, uint32_t b, Vec3 p, float t) {
        Vec3 n = normalize(lerp(m.nrm[a], m.nrm[b], t));
        m.col[a] = lerp(m.col[a], m.col[b], t);
        m.rough[a] = lerp(m.rough[a], m.rough[b], t);
        m.metal[a] = lerp(m.metal[a], m.metal[b], t);
        m.mask[a] = lerp(m.mask[a], m.mask[b], t);
        m.pos[a] = p;
        if (length2(n) > 0) m.nrm[a] = n;
        auto& ta = tris(a);
        for (uint32_t tI : tris(b)) {
            if (deadT[tI]) continue;
            if (has(tI, a)) { deadT[tI] = 1; --liveTris; continue; }
            for (int k = 0; k < 3; ++k)
                if (m.tri[(size_t)tI * 3 + k] == b) m.tri[(size_t)tI * 3 + k] = a;
            ta.push_back(tI);
        }
        vt[b].clear();
        ta.erase(std::remove_if(ta.begin(), ta.end(), [&](uint32_t x) { return deadT[x] != 0; }), ta.end());
    }
    void finish() {
        std::vector<uint32_t> nt, ng;
        nt.reserve(liveTris * 3);
        for (size_t t = 0; t < m.tcount(); ++t) {
            if (deadT[t]) continue;
            nt.push_back(m.tri[t * 3]); nt.push_back(m.tri[t * 3 + 1]); nt.push_back(m.tri[t * 3 + 2]);
            ng.push_back(m.faceGroup[t]);
        }
        m.tri.swap(nt);
        m.faceGroup.swap(ng);
        m.compact(true);
    }
};

struct Candidate {
    double cost;
    uint32_t a, b, va, vb;
    bool operator<(const Candidate& o) const { return cost > o.cost; } // min-heap
};

} // namespace

size_t decimate(Mesh& m, size_t target, const DecimateOptions& opt) {
    m.ensureAttributes();
    if (m.tcount() <= target) return m.tcount();
    m.buildAdjacency();
    size_t nv = m.vcount();
    std::vector<Quadric> Q(nv);
    for (size_t t = 0; t < m.tcount(); ++t) {
        Vec3 raw = m.triNormalRaw(t);
        float area = length(raw);
        if (area < 1e-20f) continue;
        Vec3 n = raw / area;
        double d = -(double)dot(n, m.pos[m.tri[t * 3]]);
        for (int k = 0; k < 3; ++k) Q[m.tri[t * 3 + k]].addPlane(n.x, n.y, n.z, d, area);
    }
    std::vector<EdgeRef> edges = buildSortedEdges(m);
    std::vector<uint8_t> boundary(nv, 0);
    std::vector<std::pair<uint32_t, uint32_t>> unique;
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) ++j;
        uint32_t a = (uint32_t)(edges[i].key >> 32), b = (uint32_t)(edges[i].key & 0xffffffffu);
        unique.push_back({a, b});
        if (j - i == 1) {
            boundary[a] = boundary[b] = 1;
            if (opt.preserveBoundary) { // constraint plane through the edge, perpendicular to its triangle
                Vec3 raw = m.triNormalRaw(edges[i].tri);
                Vec3 e = m.pos[b] - m.pos[a];
                Vec3 pn = normalize(cross(e, raw));
                if (length2(pn) > 0) {
                    double d = -(double)dot(pn, m.pos[a]);
                    double w = (double)length2(e) * 100.0;
                    Q[a].addPlane(pn.x, pn.y, pn.z, d, w);
                    Q[b].addPlane(pn.x, pn.y, pn.z, d, w);
                }
            }
        }
        i = j;
    }
    edges.clear(); edges.shrink_to_fit();

    Collapser C(m);
    std::vector<uint32_t> version(nv, 0);
    std::vector<uint8_t> deadV(nv, 0);
    struct Choice { Vec3 p; float t; double cost; };
    auto choose = [&](uint32_t a, uint32_t b) {
        Quadric q = Q[a];
        q.add(Q[b]);
        Choice best{};
        best.cost = 1e300;
        // a boundary vertex must not be dragged off the boundary by an interior neighbour
        bool onlyA = boundary[a] && !boundary[b], onlyB = boundary[b] && !boundary[a];
        const float ts[3] = {0.5f, 0.0f, 1.0f};
        for (float t : ts) {
            if (onlyA && t != 0.0f) continue;
            if (onlyB && t != 1.0f) continue;
            Vec3 p = lerp(m.pos[a], m.pos[b], t);
            double c = q.eval(p);
            if (c < best.cost) best = {p, t, c};
        }
        return best;
    };
    std::priority_queue<Candidate> heap;
    auto push = [&](uint32_t a, uint32_t b) {
        if (opt.respectMask && (m.mask[a] > 0.5f || m.mask[b] > 0.5f)) return;
        // tiny length term breaks ties on flat regions in favour of short edges
        double cost = choose(a, b).cost + 1e-9 * (double)length2(m.pos[a] - m.pos[b]);
        heap.push({cost, a, b, version[a], version[b]});
    };
    for (auto& e : unique) push(e.first, e.second);
    unique.clear(); unique.shrink_to_fit();

    while (C.liveTris > target && !heap.empty()) {
        Candidate c = heap.top();
        heap.pop();
        if (deadV[c.a] || deadV[c.b] || version[c.a] != c.va || version[c.b] != c.vb) continue;
        if (boundary[c.a] && boundary[c.b] && C.sharedTris(c.a, c.b) != 1) continue;
        Choice ch = choose(c.a, c.b);
        if (!C.canCollapse(c.a, c.b, ch.p)) continue;
        C.collapse(c.a, c.b, ch.p, ch.t);
        deadV[c.b] = 1;
        boundary[c.a] = boundary[c.a] | boundary[c.b];
        Q[c.a].add(Q[c.b]);
        version[c.a]++;
        ++C.gen;
        uint32_t g = C.gen;
        for (uint32_t t : C.tris(c.a))
            for (int k = 0; k < 3; ++k) {
                uint32_t u = m.tri[(size_t)t * 3 + k];
                if (u == c.a || C.stamp[u] == g) continue;
                C.stamp[u] = g;
                push(c.a, u);
            }
    }
    C.finish();
    m.finalize();
    return m.tcount();
}

size_t collapseShortEdges(Mesh& m, Vec3 center, float radius, float minLen) {
    if (m.vtStart.size() != m.vcount() + 1) m.buildAdjacency();
    float r2 = radius * radius, min2 = minLen * minLen;
    std::vector<uint32_t> verts;
    for (uint32_t v = 0; v < m.vcount(); ++v)
        if (length2(m.pos[v] - center) <= r2 && m.vtStart[v + 1] > m.vtStart[v]) verts.push_back(v);
    if (verts.empty()) return 0;
    Collapser C(m);
    std::vector<uint8_t> deadV(m.vcount(), 0);
    size_t collapses = 0;
    for (uint32_t a : verts) {
        if (deadV[a] || m.mask[a] > 0.5f) continue;
        bool again = true;
        int guard = 0;
        while (again && guard++ < 8) {
            again = false;
            uint32_t best = UINT32_MAX;
            float bestL = min2;
            for (uint32_t t : C.tris(a)) {
                if (C.deadT[t]) continue;
                for (int k = 0; k < 3; ++k) {
                    uint32_t u = m.tri[(size_t)t * 3 + k];
                    if (u == a || deadV[u] || m.mask[u] > 0.5f) continue;
                    float l = length2(m.pos[u] - m.pos[a]);
                    if (l < bestL && length2(m.pos[u] - center) <= r2) { bestL = l; best = u; }
                }
            }
            if (best == UINT32_MAX) break;
            bool ba = C.isBoundary(a), bb = C.isBoundary(best);
            if (ba && bb && C.sharedTris(a, best) != 1) break;
            float t = ba == bb ? 0.5f : (ba ? 0.0f : 1.0f);
            Vec3 p = lerp(m.pos[a], m.pos[best], t);
            if (!C.canCollapse(a, best, p)) break;
            C.collapse(a, best, p, t);
            deadV[best] = 1;
            ++collapses;
            again = true;
        }
    }
    if (collapses) {
        C.finish();
        m.ensureAttributes();
        m.buildAdjacency();
    }
    return collapses;
}

} // namespace sf

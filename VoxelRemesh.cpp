// Voxel remeshing: scan-line winding-number voxelisation -> narrow-band distance -> Surface Nets.
#include "sf/BVH.h"
#include "sf/Topology.h"
#include <atomic>

namespace sf {

namespace {

struct Grid {
    Vec3 origin;
    float h = 1;
    int nx = 0, ny = 0, nz = 0;
    size_t index(int i, int j, int k) const { return ((size_t)k * ny + j) * nx + i; }
    Vec3 node(int i, int j, int k) const { return origin + Vec3((float)i, (float)j, (float)k) * h; }
};

struct Crossing {
    uint32_t col; // j + k * ny
    float x;
    int8_t s;
};

// Mark every grid node inside the mesh (non-zero winding number) by casting +X rays per column.
void voxelize(const Mesh& m, const Grid& g, std::vector<uint8_t>& inside) {
    std::vector<Crossing> cr;
    for (size_t t = 0; t < m.tcount(); ++t) {
        Vec3 a = m.pos[m.tri[t * 3]], b = m.pos[m.tri[t * 3 + 1]], c = m.pos[m.tri[t * 3 + 2]];
        Vec3 n = cross(b - a, c - a);
        if (std::fabs(n.x) < 1e-30f) continue; // parallel to the rays
        float ymin = std::min({a.y, b.y, c.y}), ymax = std::max({a.y, b.y, c.y});
        float zmin = std::min({a.z, b.z, c.z}), zmax = std::max({a.z, b.z, c.z});
        int j0 = std::max(0, (int)std::ceil((ymin - g.origin.y) / g.h)), j1 = std::min(g.ny - 1, (int)std::floor((ymax - g.origin.y) / g.h));
        int k0 = std::max(0, (int)std::ceil((zmin - g.origin.z) / g.h)), k1 = std::min(g.nz - 1, (int)std::floor((zmax - g.origin.z) / g.h));
        if (j0 > j1 || k0 > k1) continue;
        double ay = a.y, az = a.z, by = b.y, bz = b.z, cy = c.y, cz = c.z;
        double area = (by - ay) * (cz - az) - (bz - az) * (cy - ay);
        if (area == 0) continue;
        double sgn = area > 0 ? 1.0 : -1.0;
        int8_t s = n.x < 0 ? 1 : -1; // entering the solid when the face looks back along the ray
        for (int k = k0; k <= k1; ++k) {
            double z = (double)g.origin.z + (double)k * g.h;
            for (int j = j0; j <= j1; ++j) {
                double y = (double)g.origin.y + (double)j * g.h;
                // edge functions with a fixed tie rule so shared edges are counted exactly once
                double e0 = ((by - ay) * (z - az) - (bz - az) * (y - ay)) * sgn;
                double e1 = ((cy - by) * (z - bz) - (cz - bz) * (y - by)) * sgn;
                double e2 = ((ay - cy) * (z - cz) - (az - cz) * (y - cy)) * sgn;
                auto in = [&](double e, double dy, double dz) {
                    if (e != 0) return e > 0;
                    dy *= sgn; dz *= sgn;
                    return dy > 0 || (dy == 0 && dz < 0);
                };
                if (!in(e0, by - ay, bz - az) || !in(e1, cy - by, cz - bz) || !in(e2, ay - cy, az - cz)) continue;
                float x = a.x - (float)(((y - ay) * n.y + (z - az) * n.z) / n.x);
                cr.push_back({(uint32_t)(j + k * g.ny), x, s});
            }
        }
    }
    std::sort(cr.begin(), cr.end(), [](const Crossing& p, const Crossing& q) {
        return p.col != q.col ? p.col < q.col : p.x < q.x;
    });
    size_t i = 0;
    while (i < cr.size()) {
        size_t e = i;
        while (e < cr.size() && cr[e].col == cr[i].col) ++e;
        int j = cr[i].col % g.ny, k = cr[i].col / g.ny;
        int w = 0;
        size_t c = i;
        for (int ix = 0; ix < g.nx; ++ix) {
            float x = g.origin.x + ix * g.h;
            while (c < e && cr[c].x < x) w += cr[c++].s;
            if (w != 0) inside[g.index(ix, j, k)] = 1;
        }
        i = e;
    }
}

} // namespace

size_t estimateRemeshTriangles(const Mesh& m, float voxelSize) {
    double area = 0;
    for (size_t t = 0; t < m.tcount(); ++t) area += 0.5 * length(m.triNormalRaw(t));
    if (voxelSize <= 0) return 0;
    return (size_t)(area / ((double)voxelSize * voxelSize) * 2.0 * 1.3);
}

bool voxelRemesh(const std::vector<const Mesh*>& inputs, BoolOp op, const RemeshOptions& opt, Mesh& out, std::string* error) {
    auto fail = [&](const std::string& msg) { if (error) *error = msg; return false; };
    auto tick = [&](float f) { return !opt.progress || opt.progress(f); };
    if (inputs.empty()) return fail("nothing to remesh");
    AABB box;
    for (const Mesh* m : inputs) {
        if (!m || m->tcount() == 0) return fail("input mesh is empty");
        box.expand(m->bounds());
    }
    if (!(opt.voxelSize > 0) || !std::isfinite(opt.voxelSize)) return fail("voxel size must be positive");
    Grid g;
    g.h = opt.voxelSize;
    // irrational-ish offsets keep grid nodes off axis-aligned input faces
    g.origin = box.mn - Vec3(g.h * 2.317f, g.h * 2.271f, g.h * 2.193f);
    Vec3 ext = box.mx - g.origin;
    double dx = std::ceil(ext.x / g.h) + 3, dy = std::ceil(ext.y / g.h) + 3, dz = std::ceil(ext.z / g.h) + 3;
    double cells = dx * dy * dz;
    if (cells > (double)opt.maxCells) {
        double scale = std::cbrt(cells / (double)opt.maxCells);
        return fail("voxel size too small for available memory; use at least " + std::to_string(g.h * scale * 1.02));
    }
    g.nx = (int)dx; g.ny = (int)dy; g.nz = (int)dz;
    size_t total = (size_t)g.nx * g.ny * g.nz;

    // 1. inside/outside classification, combined across operands
    std::vector<uint8_t> inside(total, 0), tmp;
    for (size_t n = 0; n < inputs.size(); ++n) {
        if (!tick(0.05f + 0.25f * n / inputs.size())) return fail("cancelled");
        if (n == 0 || op == BoolOp::Union) { voxelize(*inputs[n], g, inside); continue; }
        tmp.assign(total, 0);
        voxelize(*inputs[n], g, tmp);
        if (op == BoolOp::Difference) for (size_t i = 0; i < total; ++i) inside[i] = inside[i] & (uint8_t)!tmp[i];
        else for (size_t i = 0; i < total; ++i) inside[i] = inside[i] & tmp[i];
    }
    tmp.clear(); tmp.shrink_to_fit();
    if (!tick(0.3f)) return fail("cancelled");

    // 1b. Remove ambiguous faces (two inside corners on one diagonal, two outside on the other) by
    //     filling the outside pair. With no ambiguous faces every grid face carries 0 or 2 surface
    //     crossings, which guarantees an edge-manifold result. Filling only adds nodes inside the
    //     bounding box of existing ones, so this terminates.
    for (int pass = 0; pass < 64; ++pass) {
        size_t fixed = 0;
        auto fixFace = [&](size_t a, size_t b, size_t c, size_t d) { // a,d and b,c are the diagonals
            if (inside[a] == inside[d] && inside[b] == inside[c] && inside[a] != inside[b]) {
                inside[a] = inside[b] = inside[c] = inside[d] = 1;
                ++fixed;
            }
        };
        for (int k = 0; k < g.nz; ++k)
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    size_t n = g.index(i, j, k);
                    bool xi = i + 1 < g.nx, yj = j + 1 < g.ny, zk = k + 1 < g.nz;
                    size_t sx = 1, sy = (size_t)g.nx, sz = (size_t)g.nx * g.ny;
                    if (xi && yj) fixFace(n, n + sx, n + sy, n + sx + sy);
                    if (xi && zk) fixFace(n, n + sx, n + sz, n + sx + sz);
                    if (yj && zk) fixFace(n, n + sy, n + sz, n + sy + sz);
                }
        if (!fixed) break;
    }
    if (!tick(0.35f)) return fail("cancelled");

    // 2. active cells = cells whose 8 corners are not all equal (ascending index order)
    const int cx = g.nx - 1, cy = g.ny - 1, cz = g.nz - 1;
    auto cellIndex = [&](int i, int j, int k) { return ((uint64_t)k * cy + j) * cx + i; };
    std::vector<uint64_t> active;
    for (int k = 0; k < cz; ++k)
        for (int j = 0; j < cy; ++j) {
            const uint8_t* r00 = &inside[g.index(0, j, k)];
            const uint8_t* r10 = &inside[g.index(0, j + 1, k)];
            const uint8_t* r01 = &inside[g.index(0, j, k + 1)];
            const uint8_t* r11 = &inside[g.index(0, j + 1, k + 1)];
            for (int i = 0; i < cx; ++i) {
                int s = r00[i] + r00[i + 1] + r10[i] + r10[i + 1] + r01[i] + r01[i + 1] + r11[i] + r11[i + 1];
                if (s != 0 && s != 8) active.push_back(cellIndex(i, j, k));
            }
        }
    if (active.empty()) return fail(op == BoolOp::Union ? "mesh has no enclosed volume at this voxel size"
                                                        : "boolean result is empty");
    if (active.size() > 30'000'000) return fail("result would be too dense; increase the voxel size");
    if (!tick(0.4f)) return fail("cancelled");

    // 3. signed distance at the corner nodes of active cells
    std::vector<uint64_t> nodes;
    nodes.reserve(active.size() * 3);
    for (uint64_t c : active) {
        int i = (int)(c % cx), j = (int)((c / cx) % cy), k = (int)(c / ((uint64_t)cx * cy));
        for (int d = 0; d < 8; ++d) nodes.push_back(g.index(i + (d & 1), j + ((d >> 1) & 1), k + (d >> 2)));
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    std::vector<BVH> bvh(inputs.size());
    for (size_t n = 0; n < inputs.size(); ++n) bvh[n].build(*inputs[n]);
    std::vector<float> dist(nodes.size());
    std::atomic<bool> cancelled{false};
    float maxD = g.h * 2.0f; // distances beyond two voxels are clamped (only the sign matters there)
    parallelFor(nodes.size(), [&](size_t n) {
        uint64_t id = nodes[n];
        int i = (int)(id % g.nx), j = (int)((id / g.nx) % g.ny), k = (int)(id / ((uint64_t)g.nx * g.ny));
        Vec3 p = g.node(i, j, k);
        float best = maxD * maxD;
        for (size_t b = 0; b < bvh.size(); ++b) {
            ClosestHit h = bvh[b].closestPoint(*inputs[b], p, std::sqrt(best));
            if (h.found) best = std::min(best, h.dist2);
        }
        float d = std::max(std::sqrt(best), g.h * 1e-4f);
        dist[n] = inside[id] ? -d : d;
    });
    if (cancelled || !tick(0.7f)) return fail("cancelled");
    auto valueAt = [&](int i, int j, int k) {
        uint64_t id = g.index(i, j, k);
        size_t n = std::lower_bound(nodes.begin(), nodes.end(), id) - nodes.begin();
        return dist[n];
    };

    // 4. Manifold Surface Nets vertices. A cell crossed by several separate surface sheets gets one
    //    vertex per sheet (sheets = connected groups of sign-changing cell edges, linked across faces),
    //    which keeps the output edge-manifold at sharp rims and thin gaps.
    static const int edgeA[12] = {0, 2, 4, 6, 0, 1, 4, 5, 0, 1, 2, 3};
    static const int edgeB[12] = {1, 3, 5, 7, 2, 3, 6, 7, 4, 5, 6, 7};
    static const int faceCorner[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    static const int faceEdge[6][4] = {{4, 10, 6, 8}, {5, 11, 7, 9}, {0, 9, 2, 8}, {1, 11, 3, 10}, {0, 5, 1, 4}, {2, 7, 3, 6}};
    std::vector<uint8_t> edgeComp(active.size() * 12, 255), compCount(active.size(), 0);
    std::vector<Vec3> cellPos(active.size() * 4);
    parallelFor(active.size(), [&](size_t n) {
        uint64_t c = active[n];
        int i = (int)(c % cx), j = (int)((c / cx) % cy), k = (int)(c / ((uint64_t)cx * cy));
        float f[8];
        Vec3 p[8];
        for (int d = 0; d < 8; ++d) {
            int ii = i + (d & 1), jj = j + ((d >> 1) & 1), kk = k + (d >> 2);
            f[d] = valueAt(ii, jj, kk);
            p[d] = g.node(ii, jj, kk);
        }
        bool cross[12];
        int parent[12];
        for (int e = 0; e < 12; ++e) { cross[e] = (f[edgeA[e]] < 0) != (f[edgeB[e]] < 0); parent[e] = e; }
        auto find = [&](int x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
        auto unite = [&](int a, int b) { parent[find(a)] = find(b); };
        for (int fc = 0; fc < 6; ++fc) {
            const int* E = faceEdge[fc];
            int cnt = cross[E[0]] + cross[E[1]] + cross[E[2]] + cross[E[3]];
            if (cnt == 2) {
                int first = -1;
                for (int q = 0; q < 4; ++q) if (cross[E[q]]) { if (first < 0) first = E[q]; else unite(first, E[q]); }
            } else if (cnt == 4) { // ambiguous face: keep the two inside corners separated
                if (f[faceCorner[fc][0]] < 0) { unite(E[3], E[0]); unite(E[1], E[2]); }
                else { unite(E[0], E[1]); unite(E[2], E[3]); }
            }
        }
        int roots[4], nc = 0;
        Vec3 sum[4];
        int cnt[4] = {0, 0, 0, 0};
        for (int e = 0; e < 12; ++e) {
            if (!cross[e]) continue;
            int r = find(e), ci = -1;
            for (int q = 0; q < nc; ++q) if (roots[q] == r) ci = q;
            if (ci < 0) {
                if (nc == 4) ci = 3; // cannot happen (at most 4 sheets per cell); stay in bounds regardless
                else { ci = nc; roots[nc] = r; sum[nc] = Vec3(0, 0, 0); ++nc; }
            }
            float fa = f[edgeA[e]], fb = f[edgeB[e]];
            sum[ci] += lerp(p[edgeA[e]], p[edgeB[e]], fa / (fa - fb));
            ++cnt[ci];
            edgeComp[n * 12 + e] = (uint8_t)ci;
        }
        compCount[n] = (uint8_t)nc;
        for (int q = 0; q < nc; ++q) cellPos[n * 4 + q] = sum[q] / (float)cnt[q];
    });
    std::vector<uint32_t> cellFirst(active.size() + 1, 0);
    for (size_t n = 0; n < active.size(); ++n) cellFirst[n + 1] = cellFirst[n] + compCount[n];
    Mesh res;
    res.resizeVerts(cellFirst.back());
    for (size_t n = 0; n < active.size(); ++n)
        for (int q = 0; q < compCount[n]; ++q) res.pos[cellFirst[n] + q] = cellPos[n * 4 + q];
    cellPos.clear(); cellPos.shrink_to_fit();
    if (!tick(0.8f)) return fail("cancelled");

    // 5. one quad per sign-changing grid edge, joining the four cells around it
    auto vertOf = [&](int i, int j, int k, int localEdge) -> uint32_t {
        if (i < 0 || j < 0 || k < 0 || i >= cx || j >= cy || k >= cz) return UINT32_MAX;
        uint64_t id = cellIndex(i, j, k);
        auto it = std::lower_bound(active.begin(), active.end(), id);
        if (it == active.end() || *it != id) return UINT32_MAX;
        size_t n = (size_t)(it - active.begin());
        uint8_t comp = edgeComp[n * 12 + localEdge];
        return comp == 255 ? UINT32_MAX : cellFirst[n] + comp;
    };
    auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d, bool flip) {
        if (a == UINT32_MAX || b == UINT32_MAX || c == UINT32_MAX || d == UINT32_MAX) return;
        if (flip) std::swap(b, d);
        if (length2(res.pos[a] - res.pos[c]) <= length2(res.pos[b] - res.pos[d])) {
            res.addTri(a, b, c); res.addTri(a, c, d);
        } else {
            res.addTri(a, b, d); res.addTri(b, c, d);
        }
    };
    res.tri.reserve(active.size() * 6);
    for (uint64_t c : active) {
        int i = (int)(c % cx), j = (int)((c / cx) % cy), k = (int)(c / ((uint64_t)cx * cy));
        bool in0 = inside[g.index(i, j, k)] != 0;
        if (in0 != (inside[g.index(i + 1, j, k)] != 0)) // grid edge along +X: local edges 3,2,0,1 of the cells around it
            quad(vertOf(i, j - 1, k - 1, 3), vertOf(i, j, k - 1, 2), vertOf(i, j, k, 0), vertOf(i, j - 1, k, 1), !in0);
        if (in0 != (inside[g.index(i, j + 1, k)] != 0)) // along +Y: local edges 7,5,4,6
            quad(vertOf(i - 1, j, k - 1, 7), vertOf(i - 1, j, k, 5), vertOf(i, j, k, 4), vertOf(i, j, k - 1, 6), !in0);
        if (in0 != (inside[g.index(i, j, k + 1)] != 0)) // along +Z: local edges 11,10,8,9
            quad(vertOf(i - 1, j - 1, k, 11), vertOf(i, j - 1, k, 10), vertOf(i, j, k, 8), vertOf(i - 1, j, k, 9), !in0);
    }
    edgeComp.clear(); edgeComp.shrink_to_fit();
    inside.clear(); inside.shrink_to_fit();
    if (res.tcount() == 0) return fail("remesh produced no surface");
    res.ensureAttributes();
    if (!tick(0.9f)) return fail("cancelled");

    // 6. carry colour / material / mask over from the nearest source surface point
    if (opt.transferAttributes) {
        parallelFor(res.vcount(), [&](size_t v) {
            ClosestHit best;
            size_t src = 0;
            float limit = 1e30f;
            for (size_t b = 0; b < bvh.size(); ++b) {
                ClosestHit h = bvh[b].closestPoint(*inputs[b], res.pos[v], limit);
                if (h.found && (!best.found || h.dist2 < best.dist2)) { best = h; src = b; limit = std::sqrt(h.dist2); }
            }
            if (!best.found) return;
            const Mesh& s = *inputs[src];
            const uint32_t* t = &s.tri[(size_t)best.tri * 3];
            Vec3 w = best.bary;
            res.col[v] = s.col[t[0]] * w.x + s.col[t[1]] * w.y + s.col[t[2]] * w.z;
            res.rough[v] = s.rough[t[0]] * w.x + s.rough[t[1]] * w.y + s.rough[t[2]] * w.z;
            res.metal[v] = s.metal[t[0]] * w.x + s.metal[t[1]] * w.y + s.metal[t[2]] * w.z;
            res.mask[v] = s.mask[t[0]] * w.x + s.mask[t[1]] * w.y + s.mask[t[2]] * w.z;
        });
    }
    res.compact(true);
    res.finalize();
    out = std::move(res);
    tick(1.0f);
    return true;
}

} // namespace sf

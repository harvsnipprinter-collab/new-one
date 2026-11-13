#include "sf/BVH.h"
#include <numeric>

namespace sf {

Vec3 closestPointOnTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c, Vec3* bary) {
    // Ericson, Real-Time Collision Detection 5.1.5
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    auto out = [&](float u, float v, float w) {
        if (bary) *bary = {u, v, w};
        return a * u + b * v + c * w;
    };
    if (d1 <= 0 && d2 <= 0) return out(1, 0, 0);
    Vec3 bp = p - b;
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return out(0, 1, 0);
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float v = d1 / (d1 - d3);
        return out(1 - v, v, 0);
    }
    Vec3 cp = p - c;
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return out(0, 0, 1);
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float w = d2 / (d2 - d6);
        return out(1 - w, 0, w);
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return out(0, 1 - w, w);
    }
    float sum = va + vb + vc;
    if (std::fabs(sum) < 1e-30f) return out(1, 0, 0); // degenerate triangle
    float denom = 1.0f / sum;
    float v = vb * denom, w = vc * denom;
    return out(1 - v - w, v, w);
}

static AABB triBox(const Mesh& m, uint32_t t) {
    AABB b;
    b.expand(m.pos[m.tri[t * 3]]); b.expand(m.pos[m.tri[t * 3 + 1]]); b.expand(m.pos[m.tri[t * 3 + 2]]);
    return b;
}

uint32_t BVH::buildNode(const Mesh& mesh, std::vector<Vec3>& cent, uint32_t first, uint32_t count) {
    uint32_t idx = (uint32_t)nodes_.size();
    nodes_.emplace_back();
    AABB box, cbox;
    for (uint32_t i = first; i < first + count; ++i) {
        box.expand(triBox(mesh, order_[i]));
        cbox.expand(cent[order_[i]]);
    }
    nodes_[idx].box = box;
    if (count <= 8) {
        nodes_[idx].first = first; nodes_[idx].count = count;
        for (uint32_t i = first; i < first + count; ++i) leafOfTri_[order_[i]] = idx;
        return idx;
    }
    Vec3 sz = cbox.size();
    int axis = sz.x > sz.y ? (sz.x > sz.z ? 0 : 2) : (sz.y > sz.z ? 1 : 2);
    uint32_t mid = first + count / 2;
    std::nth_element(order_.begin() + first, order_.begin() + mid, order_.begin() + first + count,
                     [&](uint32_t a, uint32_t b) { return cent[a][axis] < cent[b][axis]; });
    uint32_t l = buildNode(mesh, cent, first, mid - first);
    uint32_t r = buildNode(mesh, cent, mid, first + count - mid);
    nodes_[idx].left = l; nodes_[idx].right = r;
    return idx;
}

void BVH::build(const Mesh& mesh) {
    nodes_.clear();
    size_t n = mesh.tcount();
    order_.resize(n);
    leafOfTri_.assign(n, 0);
    std::iota(order_.begin(), order_.end(), 0u);
    stamp_.assign(mesh.vcount(), 0);
    stampGen_ = 0;
    if (n == 0) return;
    std::vector<Vec3> cent(n);
    for (size_t t = 0; t < n; ++t)
        cent[t] = (mesh.pos[mesh.tri[t * 3]] + mesh.pos[mesh.tri[t * 3 + 1]] + mesh.pos[mesh.tri[t * 3 + 2]]) / 3.0f;
    nodes_.reserve(n / 4 + 16);
    buildNode(mesh, cent, 0, (uint32_t)n);
}

void BVH::refit(const Mesh& mesh) {
    // Children always have larger indices than their parent, so a reverse sweep is bottom-up.
    for (size_t i = nodes_.size(); i-- > 0;) {
        Node& nd = nodes_[i];
        AABB b;
        if (nd.right == 0) {
            for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) b.expand(triBox(mesh, order_[k]));
        } else {
            b.expand(nodes_[nd.left].box); b.expand(nodes_[nd.right].box);
        }
        nd.box = b;
    }
}

void BVH::refitVerts(const Mesh& mesh, const std::vector<uint32_t>& verts) {
    if (nodes_.empty() || verts.empty()) return;
    if (verts.size() > mesh.vcount() / 4 || mesh.vtStart.size() != mesh.vcount() + 1) { refit(mesh); return; }
    dirty_.assign(nodes_.size(), 0);
    uint32_t lowest = (uint32_t)nodes_.size(), highest = 0;
    for (uint32_t v : verts)
        for (uint32_t i = mesh.vtStart[v]; i < mesh.vtStart[v + 1]; ++i) {
            uint32_t leaf = leafOfTri_[mesh.vtList[i]];
            dirty_[leaf] = 1;
            highest = std::max(highest, leaf);
            lowest = std::min(lowest, leaf);
        }
    if (lowest > highest) return;
    // children have larger indices than parents: sweep downwards from the highest dirty leaf
    for (size_t i = (size_t)highest + 1; i-- > 0;) {
        Node& nd = nodes_[i];
        if (nd.right == 0) {
            if (!dirty_[i]) continue;
            AABB b;
            for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) b.expand(triBox(mesh, order_[k]));
            nd.box = b;
        } else if (dirty_[nd.left] || dirty_[nd.right]) {
            AABB b;
            b.expand(nodes_[nd.left].box); b.expand(nodes_[nd.right].box);
            nd.box = b;
            dirty_[i] = 1;
        }
    }
}

static bool rayBox(const AABB& b, Vec3 o, Vec3 inv, float tMax) {
    float t0 = 0, t1 = tMax;
    for (int i = 0; i < 3; ++i) {
        float a = (b.mn[i] - o[i]) * inv[i], c = (b.mx[i] - o[i]) * inv[i];
        if (a > c) std::swap(a, c);
        // NaN-safe: comparisons with NaN keep the previous interval
        if (a > t0) t0 = a;
        if (c < t1) t1 = c;
        if (t0 > t1) return false;
    }
    return true;
}

RayHit BVH::raycast(const Mesh& mesh, Vec3 origin, Vec3 dir, bool frontOnly, float tMax) const {
    RayHit best;
    if (nodes_.empty()) return best;
    Vec3 inv{1.0f / (dir.x != 0 ? dir.x : 1e-30f), 1.0f / (dir.y != 0 ? dir.y : 1e-30f),
             1.0f / (dir.z != 0 ? dir.z : 1e-30f)};
    float bestT = tMax;
    uint32_t stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& nd = nodes_[stack[--sp]];
        if (!rayBox(nd.box, origin, inv, bestT)) continue;
        if (nd.right == 0) {
            for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) {
                uint32_t t = order_[k];
                Vec3 a = mesh.pos[mesh.tri[t * 3]], b = mesh.pos[mesh.tri[t * 3 + 1]], c = mesh.pos[mesh.tri[t * 3 + 2]];
                Vec3 e1 = b - a, e2 = c - a;
                Vec3 pv = cross(dir, e2);
                float det = dot(e1, pv);
                if (frontOnly ? det < 1e-20f : std::fabs(det) < 1e-20f) continue;
                float idet = 1.0f / det;
                Vec3 tv = origin - a;
                float u = dot(tv, pv) * idet;
                if (u < 0 || u > 1) continue;
                Vec3 qv = cross(tv, e1);
                float v = dot(dir, qv) * idet;
                if (v < 0 || u + v > 1) continue;
                float tt = dot(e2, qv) * idet;
                if (tt > 1e-7f && tt < bestT) {
                    bestT = tt;
                    best.hit = true; best.t = tt; best.tri = t;
                    best.normal = normalize(cross(e1, e2));
                }
            }
        } else if (sp < 94) {
            stack[sp++] = nd.left; stack[sp++] = nd.right;
        }
    }
    if (best.hit) best.point = origin + dir * best.t;
    return best;
}

ClosestHit BVH::closestPoint(const Mesh& mesh, Vec3 p, float maxDist) const {
    ClosestHit best;
    if (nodes_.empty()) return best;
    float bestD2 = maxDist >= 1e15f ? 1e30f : maxDist * maxDist;
    uint32_t stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& nd = nodes_[stack[--sp]];
        if (nd.box.dist2(p) > bestD2) continue;
        if (nd.right == 0) {
            for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) {
                uint32_t t = order_[k];
                Vec3 bary;
                Vec3 q = closestPointOnTriangle(p, mesh.pos[mesh.tri[t * 3]], mesh.pos[mesh.tri[t * 3 + 1]],
                                                mesh.pos[mesh.tri[t * 3 + 2]], &bary);
                float d2 = length2(q - p);
                if (d2 < bestD2) {
                    bestD2 = d2;
                    best.found = true; best.dist2 = d2; best.tri = t; best.point = q; best.bary = bary;
                }
            }
        } else if (sp < 94) {
            // visit nearer child first
            float dl = nodes_[nd.left].box.dist2(p), dr = nodes_[nd.right].box.dist2(p);
            if (dl < dr) { stack[sp++] = nd.right; stack[sp++] = nd.left; }
            else { stack[sp++] = nd.left; stack[sp++] = nd.right; }
        }
    }
    return best;
}

void BVH::sphereTris(Vec3 c, float r, std::vector<uint32_t>& out) const {
    out.clear();
    if (nodes_.empty()) return;
    float r2 = r * r;
    uint32_t stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& nd = nodes_[stack[--sp]];
        if (nd.box.dist2(c) > r2) continue;
        if (nd.right == 0) {
            for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) out.push_back(order_[k]);
        } else if (sp < 94) {
            stack[sp++] = nd.left; stack[sp++] = nd.right;
        }
    }
}

void BVH::sphereVerts(const Mesh& mesh, Vec3 c, float r, std::vector<uint32_t>& out) const {
    out.clear();
    if (nodes_.empty()) return;
    if (stamp_.size() != mesh.vcount()) { stamp_.assign(mesh.vcount(), 0); stampGen_ = 0; }
    if (++stampGen_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0u); stampGen_ = 1; }
    float r2 = r * r;
    uint32_t stack[96];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& nd = nodes_[stack[--sp]];
        if (nd.box.dist2(c) > r2) continue;
        if (nd.right == 0) {
            for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) {
                uint32_t t = order_[k];
                for (int j = 0; j < 3; ++j) {
                    uint32_t v = mesh.tri[t * 3 + j];
                    if (stamp_[v] == stampGen_) continue;
                    stamp_[v] = stampGen_;
                    if (length2(mesh.pos[v] - c) <= r2) out.push_back(v);
                }
            }
        } else if (sp < 94) {
            stack[sp++] = nd.left; stack[sp++] = nd.right;
        }
    }
}

} // namespace sf

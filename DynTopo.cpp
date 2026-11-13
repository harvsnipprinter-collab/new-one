#include "sf/Topology.h"
#include <unordered_map>

namespace sf {

size_t splitLongEdges(Mesh& m, Vec3 center, float radius, float maxLen) {
    if (m.vtStart.size() != m.vcount() + 1) m.buildAdjacency();
    float r2 = radius * radius, max2 = maxLen * maxLen;
    // 1. choose edges: longer than maxLen with the midpoint inside the brush sphere
    std::unordered_map<uint64_t, uint32_t> mid;
    std::vector<uint32_t> endpoints;
    size_t nv0 = m.vcount(), nt0 = m.tcount();
    for (size_t t = 0; t < nt0; ++t) {
        const uint32_t* v = &m.tri[t * 3];
        // quick reject: triangle entirely outside the sphere
        if (length2(m.pos[v[0]] - center) > r2 && length2(m.pos[v[1]] - center) > r2 && length2(m.pos[v[2]] - center) > r2)
            continue;
        for (int k = 0; k < 3; ++k) {
            uint32_t a = v[k], b = v[(k + 1) % 3];
            if (a == b) continue;
            if (length2(m.pos[a] - m.pos[b]) <= max2) continue;
            if (length2((m.pos[a] + m.pos[b]) * 0.5f - center) > r2) continue;
            if (m.mask[a] > 0.5f && m.mask[b] > 0.5f) continue; // masked geometry is protected
            uint64_t key = ((uint64_t)std::min(a, b) << 32) | std::max(a, b);
            if (mid.emplace(key, UINT32_MAX).second) { endpoints.push_back(a); endpoints.push_back(b); }
        }
    }
    if (mid.empty()) return 0;
    // 2. create midpoint vertices
    m.resizeVerts(nv0 + mid.size());
    uint32_t next = (uint32_t)nv0;
    for (auto& kv : mid) {
        uint32_t a = (uint32_t)(kv.first >> 32), b = (uint32_t)(kv.first & 0xffffffffu);
        kv.second = next;
        m.setVertexLerp(next, a, b, 0.5f);
        ++next;
    }
    auto midOf = [&](uint32_t a, uint32_t b) {
        auto it = mid.find(((uint64_t)std::min(a, b) << 32) | std::max(a, b));
        return it == mid.end() ? UINT32_MAX : it->second;
    };
    // 3. re-triangulate every triangle that uses a split edge (found through endpoint adjacency)
    std::vector<uint8_t> done(nt0, 0);
    for (uint32_t ep : endpoints) {
        for (uint32_t i = m.vtStart[ep]; i < m.vtStart[ep + 1]; ++i) {
            uint32_t t = m.vtList[i];
            if (done[t]) continue;
            done[t] = 1;
            uint32_t v[3] = {m.tri[(size_t)t * 3], m.tri[(size_t)t * 3 + 1], m.tri[(size_t)t * 3 + 2]};
            uint32_t e[3] = {midOf(v[0], v[1]), midOf(v[1], v[2]), midOf(v[2], v[0])};
            int n = (e[0] != UINT32_MAX) + (e[1] != UINT32_MAX) + (e[2] != UINT32_MAX);
            if (n == 0) continue;
            uint32_t g = m.faceGroup[t];
            auto set = [&](uint32_t a, uint32_t b, uint32_t c) {
                m.tri[(size_t)t * 3] = a; m.tri[(size_t)t * 3 + 1] = b; m.tri[(size_t)t * 3 + 2] = c;
            };
            if (n == 3) {
                set(v[0], e[0], e[2]);
                m.addTri(v[1], e[1], e[0], g);
                m.addTri(v[2], e[2], e[1], g);
                m.addTri(e[0], e[1], e[2], g);
            } else if (n == 1) {
                int k = e[0] != UINT32_MAX ? 0 : (e[1] != UINT32_MAX ? 1 : 2);
                uint32_t a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3], mm = e[k];
                set(a, mm, c);
                m.addTri(mm, b, c, g);
            } else { // two edges split; k = the unsplit edge
                int k = e[0] == UINT32_MAX ? 0 : (e[1] == UINT32_MAX ? 1 : 2);
                uint32_t a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3];
                uint32_t mbc = e[(k + 1) % 3], mca = e[(k + 2) % 3];
                set(c, mca, mbc);
                // split quad a,b,mbc,mca along its shorter diagonal
                if (length2(m.pos[a] - m.pos[mbc]) <= length2(m.pos[b] - m.pos[mca])) {
                    m.addTri(a, b, mbc, g);
                    m.addTri(a, mbc, mca, g);
                } else {
                    m.addTri(a, b, mca, g);
                    m.addTri(b, mbc, mca, g);
                }
            }
        }
    }
    m.buildAdjacency();
    return mid.size();
}

} // namespace sf

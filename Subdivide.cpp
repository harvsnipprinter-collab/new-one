#include "sf/MeshOps.h"
#include "sf/Topology.h"

namespace sf {

void subdivide(Mesh& m, bool smooth) {
    m.ensureAttributes();
    size_t nv = m.vcount(), nt = m.tcount();
    if (nt == 0) return;
    std::vector<EdgeRef> edges = buildSortedEdges(m);
    std::vector<uint32_t> edgeVert(nt * 3, UINT32_MAX); // per (tri, corner)

    // Old-vertex smoothing accumulators (unique edges only, so each neighbour counts once).
    std::vector<Vec3> nsum, bsum;
    std::vector<uint32_t> valence, bcount;
    if (smooth) {
        nsum.assign(nv, Vec3(0, 0, 0)); bsum.assign(nv, Vec3(0, 0, 0));
        valence.assign(nv, 0); bcount.assign(nv, 0);
    }
    std::vector<Vec3> oldPos = m.pos;
    size_t uniqueEdges = 0;
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) ++j;
        ++uniqueEdges;
        i = j;
    }
    m.resizeVerts(nv + uniqueEdges);
    uint32_t next = (uint32_t)nv;
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) ++j;
        uint32_t a = (uint32_t)(edges[i].key >> 32), b = (uint32_t)(edges[i].key & 0xffffffffu);
        uint32_t ev = next++;
        m.setVertexLerp(ev, a, b, 0.5f);
        size_t cnt = j - i;
        if (smooth) {
            if (cnt == 2) { // interior: 3/8 (a+b) + 1/8 (c+d)
                Vec3 opp(0, 0, 0);
                for (size_t k = i; k < j; ++k) opp += oldPos[m.tri[(size_t)edges[k].tri * 3 + (edges[k].corner + 2) % 3]];
                m.pos[ev] = (oldPos[a] + oldPos[b]) * 0.375f + opp * 0.125f;
            } else {
                m.pos[ev] = (oldPos[a] + oldPos[b]) * 0.5f;
            }
            nsum[a] += oldPos[b]; nsum[b] += oldPos[a]; valence[a]++; valence[b]++;
            if (cnt != 2) { bsum[a] += oldPos[b]; bsum[b] += oldPos[a]; bcount[a]++; bcount[b]++; }
        }
        for (size_t k = i; k < j; ++k) edgeVert[(size_t)edges[k].tri * 3 + edges[k].corner] = ev;
        i = j;
    }
    if (smooth) {
        for (size_t v = 0; v < nv; ++v) {
            float w = 1.0f - m.mask[v]; // masked vertices keep their position
            Vec3 target = oldPos[v];
            if (bcount[v] == 2) target = oldPos[v] * 0.75f + bsum[v] * 0.125f;
            else if (bcount[v] == 0 && valence[v] >= 3) {
                float n = (float)valence[v];
                float beta = valence[v] == 3 ? 3.0f / 16.0f : 3.0f / (8.0f * n);
                target = oldPos[v] * (1.0f - n * beta) + nsum[v] * beta;
            }
            m.pos[v] = lerp(oldPos[v], target, w);
        }
    }
    std::vector<uint32_t> newTri, newGroup;
    newTri.reserve(nt * 12);
    newGroup.reserve(nt * 4);
    for (size_t t = 0; t < nt; ++t) {
        uint32_t a = m.tri[t * 3], b = m.tri[t * 3 + 1], c = m.tri[t * 3 + 2];
        uint32_t ab = edgeVert[t * 3], bc = edgeVert[t * 3 + 1], ca = edgeVert[t * 3 + 2];
        uint32_t g = m.faceGroup[t];
        if (ab == UINT32_MAX || bc == UINT32_MAX || ca == UINT32_MAX) continue; // degenerate input triangle
        uint32_t add[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
        newTri.insert(newTri.end(), add, add + 12);
        for (int k = 0; k < 4; ++k) newGroup.push_back(g);
    }
    m.tri.swap(newTri);
    m.faceGroup.swap(newGroup);
    m.finalize();
}

} // namespace sf

// SculptForge 3D - mesh editing, masking, repair and analysis operations
#pragma once
#include "BVH.h"
#include "Mesh.h"

namespace sf {

// ---- basic editing -------------------------------------------------------------------------
// Merge vertices closer than eps, drop degenerate triangles and unused vertices. Returns merged count.
size_t weldVertices(Mesh& m, float eps);
void flipNormals(Mesh& m);
int countComponents(const Mesh& m);
std::vector<Mesh> separateComponents(const Mesh& m);
// Laplacian smoothing of all vertices. Masked vertices move proportionally less.
void smoothMesh(Mesh& m, int iterations, float factor, bool respectMask = true);
// Returns the mesh joined with its mirror image across the plane axis=0 (X), 1 (Y) or 2 (Z).
Mesh mirrorMesh(const Mesh& m, int axis, bool weldSeam);

// ---- holes / cutting / extraction ----------------------------------------------------------
// Triangulate open boundary loops. onlyVerts (optional, per-vertex flags) restricts filling to
// loops made entirely of flagged vertices. Returns number of loops filled.
size_t fillHoles(Mesh& m, const std::vector<uint8_t>* onlyVerts = nullptr);
// Remove everything on the positive side of the plane; optionally cap the cut. Returns new tri count.
size_t cutPlane(Mesh& m, Vec3 planePoint, Vec3 planeNormal, bool cap);
// Build a new mesh from the masked region (mask > 0.5). thickness > 0 produces a closed shell.
Mesh extractMasked(const Mesh& m, float thickness);

// ---- mask operations -----------------------------------------------------------------------
void maskClear(Mesh& m);
void maskInvert(Mesh& m);
void maskBlur(Mesh& m, int iterations);
void maskSharpen(Mesh& m, float amount);
void maskGrow(Mesh& m);
void maskShrink(Mesh& m);
// Mask concave regions (cavities); sensitivity scales the response.
void maskByCavity(Mesh& m, float sensitivity, bool invert);

// ---- analysis (3D printing validation) -----------------------------------------------------
struct MeshAnalysis {
    size_t vertices = 0, triangles = 0;
    size_t boundaryEdges = 0;      // edges used by exactly one triangle (open boundary)
    size_t nonManifoldEdges = 0;   // edges used by more than two triangles
    size_t inconsistentEdges = 0;  // edges whose two triangles have opposing winding
    size_t degenerateTriangles = 0;
    size_t looseVertices = 0;
    size_t holes = 0;              // number of boundary loops
    int components = 0;
    double volume = 0;             // signed; negative means inverted normals
    double surfaceArea = 0;
    AABB bounds;
    bool watertight = false;       // closed, manifold, consistently oriented
    bool invertedNormals = false;
    float minThickness = -1;       // sampled wall thickness estimate, -1 if not computed
    size_t thinSamples = 0;        // samples thinner than the threshold
    size_t thicknessSamples = 0;
};
// thinThreshold <= 0 skips the wall thickness sampling.
MeshAnalysis analyzeMesh(const Mesh& m, float thinThreshold = 0, int maxThicknessSamples = 4000);

// ---- helpers -------------------------------------------------------------------------------
struct EdgeRef {
    uint64_t key;   // (min << 32) | max
    uint32_t tri;   // triangle index
    uint8_t corner; // edge goes from corner to (corner+1)%3
    bool forward;   // true if tri traverses min -> max
};
// All triangle edges sorted by key (degenerate triangles skipped).
std::vector<EdgeRef> buildSortedEdges(const Mesh& m);

template <class F>
inline void forEachNeighbor(const Mesh& m, uint32_t v, F&& fn) {
    for (uint32_t i = m.vtStart[v]; i < m.vtStart[v + 1]; ++i) {
        const uint32_t* t = &m.tri[(size_t)m.vtList[i] * 3];
        for (int k = 0; k < 3; ++k)
            if (t[k] != v) fn(t[k]);
    }
}

} // namespace sf

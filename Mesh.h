// SculptForge 3D - indexed triangle mesh with per-vertex sculpt/paint attributes
#pragma once
#include "Math.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct Mesh {
    // Per-vertex data (all arrays always have vcount() entries)
    std::vector<Vec3> pos;
    std::vector<Vec3> nrm;
    std::vector<Vec3> col;      // base colour, linear RGB 0..1
    std::vector<float> rough;   // 0..1
    std::vector<float> metal;   // 0..1
    std::vector<float> mask;    // 0 = free, 1 = fully protected
    // Per-triangle data
    std::vector<uint32_t> tri;       // 3 indices per triangle
    std::vector<uint32_t> faceGroup; // one id per triangle

    // Derived adjacency: vertex -> incident triangles (CSR layout)
    std::vector<uint32_t> vtStart; // vcount()+1 entries
    std::vector<uint32_t> vtList;

    size_t vcount() const { return pos.size(); }
    size_t tcount() const { return tri.size() / 3; }

    void clear();
    // Resize every per-vertex array; new vertices get default attributes.
    void resizeVerts(size_t n);
    // Make attribute arrays consistent with pos/tri sizes (fills defaults).
    void ensureAttributes();
    uint32_t addVertex(Vec3 p);
    void addTri(uint32_t a, uint32_t b, uint32_t c, uint32_t group = 0);
    // Copy all attributes of vertex `src` blended between two/three vertices.
    void setVertexLerp(uint32_t dst, uint32_t a, uint32_t b, float t);

    void buildAdjacency();
    void computeNormals();                       // all vertices, area weighted
    void computeNormalFor(uint32_t v);           // needs adjacency
    Vec3 triNormalRaw(size_t t) const;           // unnormalised (2*area)
    AABB bounds() const;
    // After geometry edits that change topology: attributes + adjacency + normals.
    void finalize();

    // Structural validation: index range, sizes, finite values.
    bool validate(std::string* error = nullptr) const;
    void transform(const Mat4& m);
    // Append another mesh (vertices re-indexed). Face groups are offset to stay distinct.
    void append(const Mesh& o);
    // Remove vertices not referenced by any triangle and degenerate triangles.
    void compact(bool removeDegenerate = true);
    size_t memoryBytes() const;
};

} // namespace sf

// SculptForge 3D - triangle bounding volume hierarchy (raycast, closest point, sphere query)
#pragma once
#include "Mesh.h"

namespace sf {

struct RayHit {
    bool hit = false;
    float t = 0;
    uint32_t tri = 0;
    Vec3 point, normal; // geometric (face) normal, normalised
};

struct ClosestHit {
    bool found = false;
    float dist2 = 0;
    uint32_t tri = 0;
    Vec3 point;
    Vec3 bary; // barycentric weights of the closest point
};

class BVH {
public:
    void build(const Mesh& mesh);
    // Recompute node bounds after vertices moved (topology unchanged).
    void refit(const Mesh& mesh);
    // Refit only the leaves containing triangles around the given vertices (and their ancestors).
    // Needs current mesh adjacency. Cost is proportional to the edited region, not the mesh size.
    void refitVerts(const Mesh& mesh, const std::vector<uint32_t>& verts);
    bool empty() const { return nodes_.empty(); }

    RayHit raycast(const Mesh& mesh, Vec3 origin, Vec3 dir, bool frontFacesOnly = false,
                   float tMax = 1e30f) const;
    ClosestHit closestPoint(const Mesh& mesh, Vec3 p, float maxDist = 1e30f) const;
    // Triangles whose leaf bounds intersect the sphere (superset of exact hits).
    void sphereTris(Vec3 c, float r, std::vector<uint32_t>& out) const;
    // Unique vertices within radius r of c. `stamp` is scratch storage reused between calls.
    void sphereVerts(const Mesh& mesh, Vec3 c, float r, std::vector<uint32_t>& out) const;

private:
    struct Node {
        AABB box;
        uint32_t left = 0, right = 0; // children (internal) ; right==0 => leaf
        uint32_t first = 0, count = 0; // leaf triangle range into order_
    };
    std::vector<Node> nodes_;
    std::vector<uint32_t> order_, leafOfTri_;
    std::vector<uint8_t> dirty_;
    mutable std::vector<uint32_t> stamp_;
    mutable uint32_t stampGen_ = 0;
    uint32_t buildNode(const Mesh& mesh, std::vector<Vec3>& cent, uint32_t first, uint32_t count);
};

// Closest point on triangle abc to p; writes barycentric weights.
Vec3 closestPointOnTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c, Vec3* bary = nullptr);

} // namespace sf

// SculptForge 3D - topology engine: subdivision, decimation, dynamic topology, voxel remesh/booleans
#pragma once
#include "Mesh.h"
#include <functional>
#include <string>
#include <thread>

namespace sf {

// ---- subdivision ---------------------------------------------------------------------------
// Each triangle becomes four. smooth=true applies Loop subdivision rules (with boundary rules),
// smooth=false keeps the surface unchanged (linear midpoint split).
void subdivide(Mesh& m, bool smooth);

// ---- decimation (quadric error metric edge collapse) ---------------------------------------
struct DecimateOptions {
    bool preserveBoundary = true; // boundary edges get heavy constraint quadrics
    bool respectMask = false;     // vertices with mask > 0.5 are never collapsed
};
// Reduce to at most targetTriangles where possible. Returns the resulting triangle count.
size_t decimate(Mesh& m, size_t targetTriangles, const DecimateOptions& opt = {});

// ---- dynamic topology (local, used during strokes) -----------------------------------------
// Split every edge longer than maxLen that touches the sphere. Needs current adjacency; leaves
// the mesh with valid adjacency. Returns number of edges split.
size_t splitLongEdges(Mesh& m, Vec3 center, float radius, float maxLen);
// Collapse edges shorter than minLen inside the sphere. Returns number of collapses.
size_t collapseShortEdges(Mesh& m, Vec3 center, float radius, float minLen);

// ---- voxel remeshing and booleans ----------------------------------------------------------
enum class BoolOp : int { Union = 0, Difference = 1, Intersection = 2 };

struct RemeshOptions {
    float voxelSize = 0.05f;
    size_t maxCells = 100'000'000;             // memory guard (1 byte per cell + surface data)
    bool transferAttributes = true;            // colour / roughness / metalness / mask
    std::function<bool(float)> progress;       // return false to cancel
};
// Rebuild a watertight surface from the volume enclosed by the inputs (in a common space).
// Union merges all inputs, Difference subtracts inputs[1..] from inputs[0], Intersection keeps
// the common volume. Returns false (and leaves `out` untouched) on error or cancellation.
bool voxelRemesh(const std::vector<const Mesh*>& inputs, BoolOp op, const RemeshOptions& opt, Mesh& out,
                 std::string* error = nullptr);
// Rough triangle count a remesh at this voxel size will produce (2 triangles per voxel face).
size_t estimateRemeshTriangles(const Mesh& m, float voxelSize);

// ---- small parallel helper -----------------------------------------------------------------
template <class F>
void parallelFor(size_t count, F&& fn) {
    unsigned hw = std::thread::hardware_concurrency();
    size_t threads = std::min<size_t>(hw ? hw : 4, 16);
    if (count < 4096 || threads <= 1) { for (size_t i = 0; i < count; ++i) fn(i); return; }
    std::vector<std::thread> pool;
    size_t chunk = (count + threads - 1) / threads;
    for (size_t t = 0; t < threads; ++t) {
        size_t b = t * chunk, e = std::min(count, b + chunk);
        if (b >= e) break;
        pool.emplace_back([b, e, &fn] { for (size_t i = b; i < e; ++i) fn(i); });
    }
    for (auto& th : pool) th.join();
}

} // namespace sf

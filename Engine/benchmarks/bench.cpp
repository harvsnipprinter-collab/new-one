// SculptForge 3D engine benchmark (CPU side only: geometry, brushes, topology, files).
// Usage: sf_bench [output-directory] [max-triangles]
#include "sf/IO.h"
#include "sf/MeshOps.h"
#include "sf/Primitives.h"
#include "sf/Scene.h"
#include "sf/Topology.h"
#include "sf_api.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace sf;

static double now() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static double strokeMsPerDab(SFScene* s, uint32_t id, int type, float radius, int* dabsOut) {
    SFBrush b;
    sf_brush_default(&b);
    b.type = type; b.radius = radius; b.strength = 0.5f;
    float d[3] = {0, 0, -1}, o[3] = {-0.4f, 0, 5};
    double t0 = now();
    if (!sf_stroke_begin(s, id, &b, o, d, 1)) return -1;
    const int moves = 40; // one "touch event" per move, several interpolated dabs each
    for (int i = 1; i <= moves; ++i) {
        float oi[3] = {-0.4f + 0.8f * i / moves, 0.1f * std::sin(i * 0.3f), 5};
        sf_stroke_move(s, oi, d, 1);
    }
    sf_stroke_end(s);
    double ms = now() - t0;
    sf_undo(s);
    if (dabsOut) *dabsOut = moves;
    return ms / moves; // time per touch event, including BVH refit and normal updates
}

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "bench_output";
    size_t maxTris = argc > 2 ? (size_t)std::atoll(argv[2]) : 2'100'000;
    std::error_code ec;
    std::filesystem::create_directories(toPath(out), ec);
    const int divs[] = {29, 91, 204, 289, 408}; // 12*d^2 triangles: ~10k, 100k, 500k, 1M, 2M
    std::printf("| Triangles | Build prim (ms) | BVH build (ms) | Raycast (us) | Draw r=0.1 (ms/event) | Draw r=0.3 (ms/event) | Smooth r=0.3 (ms/event) | Move r=0.3 (ms/event) | Subdivide (ms) | Decimate 50%% (ms) | Voxel remesh (ms -> tris) | Save project (ms / MB) | STL export (ms) | Mesh RAM (MB) |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (int d : divs) {
        if ((size_t)12 * d * d > maxTris) break;
        double t0 = now();
        Mesh m = makeCube({2, 2, 2}, d, 1.0f); // rounded cube with full radius == sphere with even triangles
        double tPrim = now() - t0;
        size_t tris = m.tcount();
        t0 = now();
        BVH bvh;
        bvh.build(m);
        double tBvh = now() - t0;
        t0 = now();
        int hits = 0;
        const int rays = 20000;
        for (int i = 0; i < rays; ++i) {
            float a = i * 0.001f;
            hits += bvh.raycast(m, {std::cos(a) * 0.5f, std::sin(a * 1.7f) * 0.5f, 5}, {0, 0, -1}).hit;
        }
        double tRay = (now() - t0) * 1000.0 / rays;
        SFScene* s = sf_scene_create();
        sf_history_set_limits(s, 4, 4ull << 30);
        float p[5] = {2, 2, 2, (float)d, 1.0f};
        uint32_t id = sf_add_primitive(s, (int)PrimitiveType::RoundedCube, p, 5);
        double dSmall = strokeMsPerDab(s, id, (int)BrushType::Draw, 0.1f, nullptr);
        double dBig = strokeMsPerDab(s, id, (int)BrushType::Draw, 0.3f, nullptr);
        double dSmooth = strokeMsPerDab(s, id, (int)BrushType::Smooth, 0.3f, nullptr);
        double dMove = strokeMsPerDab(s, id, (int)BrushType::Move, 0.3f, nullptr);
        double tSub = -1, tDec = -1;
        if (tris <= 600'000) {
            Mesh c = m;
            t0 = now();
            subdivide(c, true);
            tSub = now() - t0;
        }
        {
            Mesh c = m;
            t0 = now();
            decimate(c, tris / 2);
            tDec = now() - t0;
        }
        RemeshOptions opt;
        opt.voxelSize = std::sqrt(4.0f * 3.14159f / (float)tris * 2.0f); // aims for a similar triangle count
        Mesh r;
        std::string err;
        t0 = now();
        bool ok = voxelRemesh({&m}, BoolOp::Union, opt, r, &err);
        double tRemesh = now() - t0;
        std::string proj = out + "/bench_" + std::to_string(tris) + ".sculptforge", stl = out + "/bench_" + std::to_string(tris) + ".stl";
        t0 = now();
        sf_save_project(s, proj.c_str());
        double tSave = now() - t0;
        double mb = (double)std::filesystem::file_size(toPath(proj), ec) / 1048576.0;
        t0 = now();
        sf_export(s, stl.c_str(), 0, nullptr, 0, 1.0f);
        double tStl = now() - t0;
        char sub[32], rem[64];
        if (tSub >= 0) std::snprintf(sub, sizeof(sub), "%.0f", tSub); else std::snprintf(sub, sizeof(sub), "skipped");
        if (ok) std::snprintf(rem, sizeof(rem), "%.0f -> %zu", tRemesh, r.tcount()); else std::snprintf(rem, sizeof(rem), "failed: %s", err.c_str());
        std::printf("| %zu | %.0f | %.0f | %.2f | %.2f | %.2f | %.2f | %.2f | %s | %.0f | %s | %.0f / %.1f | %.0f | %.0f |\n", tris, tPrim, tBvh, tRay,
                    dSmall, dBig, dSmooth, dMove, sub, tDec, rem, tSave, mb, tStl, (double)m.memoryBytes() / 1048576.0);
        std::fflush(stdout);
        sf_scene_destroy(s);
        std::filesystem::remove(toPath(proj), ec);
        std::filesystem::remove(toPath(stl), ec);
        (void)hits;
    }
    return 0;
}

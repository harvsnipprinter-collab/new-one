// Brush tests run through the same C API the iOS app uses.
#include "TestKit.h"
#include <cstring>

using namespace sf;

namespace {

struct Snap {
    std::vector<Vec3> pos, nrm, col;
    std::vector<float> rough, metal, mask;
    std::vector<uint32_t> tri, group;
    bool sameData(const Snap& o) const {
        return pos == o.pos && col == o.col && rough == o.rough && metal == o.metal && mask == o.mask && tri == o.tri &&
               group == o.group;
    }
};

Snap snap(SFScene* s, uint32_t id) {
    SFMeshView v;
    Snap r;
    if (!sf_object_mesh(s, id, &v)) return r;
    r.pos.resize(v.vertexCount); r.nrm.resize(v.vertexCount); r.col.resize(v.vertexCount);
    std::memcpy(r.pos.data(), v.positions, v.vertexCount * 12);
    std::memcpy(r.nrm.data(), v.normals, v.vertexCount * 12);
    std::memcpy(r.col.data(), v.colors, v.vertexCount * 12);
    r.rough.assign(v.roughness, v.roughness + v.vertexCount);
    r.metal.assign(v.metalness, v.metalness + v.vertexCount);
    r.mask.assign(v.mask, v.mask + v.vertexCount);
    r.tri.assign(v.indices, v.indices + v.triangleCount * 3);
    r.group.assign(v.faceGroups, v.faceGroups + v.triangleCount);
    return r;
}

Mesh toMesh(const Snap& s) {
    Mesh m;
    m.pos = s.pos; m.tri = s.tri;
    m.ensureAttributes();
    m.col = s.col; m.mask = s.mask; m.rough = s.rough; m.metal = s.metal; m.faceGroup = s.group;
    m.finalize();
    return m;
}

bool finite(const Snap& s) {
    for (auto& p : s.pos) if (!isFinite(p)) return false;
    for (auto& p : s.nrm) if (!isFinite(p)) return false;
    for (auto& p : s.col) if (!isFinite(p)) return false;
    for (float f : s.mask) if (!std::isfinite(f)) return false;
    for (uint32_t i : s.tri) if (i >= s.pos.size()) return false;
    return true;
}

// Stroke with parallel rays looking down -Z, through the listed (x, y) points.
int stroke(SFScene* s, uint32_t id, const SFBrush& b, const std::vector<Vec2>& pts, float pressure = 1.0f) {
    float d[3] = {0, 0, -1};
    float o[3] = {pts[0].r, pts[0].y, 5};
    if (!sf_stroke_begin(s, id, &b, o, d, pressure)) return -1;
    for (size_t i = 1; i < pts.size(); ++i) {
        float oi[3] = {pts[i].r, pts[i].y, 5};
        sf_stroke_move(s, oi, d, pressure);
    }
    return sf_stroke_end(s);
}

std::vector<Vec2> line(float x0, float x1, float y, int n) {
    std::vector<Vec2> p;
    for (int i = 0; i <= n; ++i) p.push_back({x0 + (x1 - x0) * i / n, y});
    return p;
}

uint32_t sphere(SFScene* s, int subdiv) {
    float p[2] = {1.0f, (float)subdiv};
    return sf_add_primitive(s, (int)PrimitiveType::Icosphere, p, 2);
}

SFBrush brush(BrushType t, float radius = 0.25f, float strength = 0.6f) {
    SFBrush b;
    sf_brush_default(&b);
    b.type = (int)t; b.radius = radius; b.strength = strength;
    return b;
}

size_t countChanged(const Snap& a, const Snap& b, BrushType t) {
    size_t n = 0;
    if (t == BrushType::FaceGroup) { for (size_t i = 0; i < a.group.size(); ++i) n += a.group[i] != b.group[i]; return n; }
    for (size_t i = 0; i < a.pos.size(); ++i)
        n += !(a.pos[i] == b.pos[i]) || !(a.col[i] == b.col[i]) || a.mask[i] != b.mask[i] || a.rough[i] != b.rough[i] ||
             a.metal[i] != b.metal[i];
    return n;
}

void eachBrush() {
    tk::section("Every brush: deterministic stroke, locality, validity, undo, redo");
    const int levels[] = {3, 5, 6}; // 1,280 / 20,480 / 81,920 triangles
    for (int t = 0; t < (int)BrushType::Count; ++t) {
        BrushType type = (BrushType)t;
        std::string name = brushName(type);
        bool allOk = true;
        std::string detail;
        for (int level : levels) {
            SFScene* s = sf_scene_create();
            uint32_t id = sphere(s, level);
            SFBrush b = brush(type);
            if (type == BrushType::Layer) b.layerHeight = 0.08f;
            if (type == BrushType::FaceGroup) b.faceGroup = 7;
            if (type == BrushType::MaterialPaint) { b.paintRough = 1; b.paintMetal = 1; b.paintMetalness = 1.0f; b.paintRoughness = 0.1f; }
            // brushes that edit existing mask / colour data need something to work on
            if (type == BrushType::MaskBlur || type == BrushType::MaskSharpen) {
                SFBrush mb = brush(BrushType::Mask, 0.2f, 0.5f);
                stroke(s, id, mb, line(-0.1f, 0.1f, 0, 4));
            }
            if (type == BrushType::Smudge) {
                SFBrush pb = brush(BrushType::Paint, 0.2f, 1.0f);
                pb.color[0] = 1; pb.color[1] = 0; pb.color[2] = 0;
                stroke(s, id, pb, line(-0.3f, -0.1f, 0, 4));
            }
            if (type == BrushType::Smooth || type == BrushType::Polish || type == BrushType::Flatten || type == BrushType::Scrape) {
                SFBrush db = brush(BrushType::Draw, 0.12f, 1.0f); // bumps to smooth away
                stroke(s, id, db, line(-0.1f, 0.1f, 0.05f, 3));
            }
            Snap before = snap(s, id);
            std::vector<Vec2> path = line(-0.2f, 0.2f, 0.0f, 8);
            int res = stroke(s, id, b, path);
            Snap after = snap(s, id);
            size_t changed = countChanged(before, after, type);
            bool ok = true;
            ok &= CHECK_MSG(res == 1, name + ": stroke should report a change");
            ok &= CHECK_MSG(changed > 0, name + ": stroke changed nothing");
            ok &= CHECK_MSG(finite(after), name + ": NaN or bad index after stroke");
            ok &= CHECK_MSG(after.tri == before.tri, name + ": topology must not change");
            // locality: nothing may change far away from the stroke
            float reach = (type == BrushType::Elastic ? 2.5f : 1.0f) * b.radius + 0.25f;
            size_t stray = 0;
            if (type != BrushType::FaceGroup)
                for (size_t i = 0; i < before.pos.size(); ++i) {
                    bool ch = !(before.pos[i] == after.pos[i]) || !(before.col[i] == after.col[i]) || before.mask[i] != after.mask[i];
                    if (!ch) continue;
                    Vec3 p = before.pos[i];
                    float dPath = length(Vec3(std::clamp(p.x, -0.2f, 0.2f), 0, 1) - p);
                    if (dPath > reach) ++stray;
                }
            ok &= CHECK_MSG(stray == 0, name + ": " + std::to_string(stray) + " vertices changed outside the brush area");
            ok &= CHECK_MSG(changed < before.pos.size() * 3 / 4 || level == 3, name + ": change is not localised");
            // brush-specific intent
            if (type == BrushType::Layer) {
                float mx = 0;
                for (size_t i = 0; i < before.pos.size(); ++i) mx = std::max(mx, length(after.pos[i] - before.pos[i]));
                ok &= CHECK_MSG(mx <= 0.0801f && mx > 0.04f, name + ": displacement must be capped by the layer height");
            }
            if (type == BrushType::Draw || type == BrushType::Clay || type == BrushType::ClayBuildup || type == BrushType::Inflate ||
                type == BrushType::Stamp) {
                double out = 0;
                for (size_t i = 0; i < before.pos.size(); ++i) out += length(after.pos[i]) - length(before.pos[i]);
                ok &= CHECK_MSG(out > 0, name + ": should add volume");
            }
            if (type == BrushType::Crease) {
                double out = 0;
                for (size_t i = 0; i < before.pos.size(); ++i) out += length(after.pos[i]) - length(before.pos[i]);
                ok &= CHECK_MSG(out < 0, name + ": should carve inward");
            }
            if (type == BrushType::Mask) {
                float mx = 0;
                for (float m : after.mask) mx = std::max(mx, m);
                ok &= CHECK_MSG(mx > 0.5f && after.pos == before.pos, name + ": should paint mask without moving vertices");
            }
            if (type == BrushType::Paint || type == BrushType::Smudge || type == BrushType::MaterialPaint)
                ok &= CHECK_MSG(after.pos == before.pos, name + ": must not move vertices");
            if (type == BrushType::MaterialPaint) {
                float mx = 0;
                for (float m : after.metal) mx = std::max(mx, m);
                SFObjectProps pr;
                sf_object_get_props(s, id, &pr);
                ok &= CHECK_MSG(mx > 0.3f && pr.usePainted == 1, name + ": metalness channel should be painted");
            }
            if (type == BrushType::FaceGroup) {
                size_t g7 = 0;
                for (uint32_t g : after.group) g7 += g == 7;
                ok &= CHECK_MSG(g7 == changed && g7 > 0, name + ": faces should get group 7");
            }
            // undo / redo must restore data exactly
            ok &= CHECK_MSG(sf_undo(s) == 1, name + ": undo failed");
            Snap undone = snap(s, id);
            ok &= CHECK_MSG(undone.sameData(before), name + ": undo did not restore the previous state exactly");
            float nerr = 0;
            for (size_t i = 0; i < undone.nrm.size(); ++i) nerr = std::max(nerr, length(undone.nrm[i] - before.nrm[i]));
            ok &= CHECK_MSG(nerr < 1e-4f, name + ": normals differ after undo");
            ok &= CHECK_MSG(sf_redo(s) == 1, name + ": redo failed");
            ok &= CHECK_MSG(snap(s, id).sameData(after), name + ": redo did not reproduce the stroke exactly");
            if (level == 5) detail = std::to_string(changed) + (type == BrushType::FaceGroup ? " faces" : " verts") + " changed at 20k tris";
            allOk &= ok;
            sf_scene_destroy(s);
        }
        if (allOk) tk::pass(name + " (3 resolutions): " + detail);
    }
}

double roughness(const Snap& s, Vec3 c, float r) { // mean distance from each vertex to its neighbour average
    Mesh m = toMesh(s);
    double sum = 0; int n = 0;
    for (uint32_t v = 0; v < m.vcount(); ++v) {
        if (length(m.pos[v] - c) > r) continue;
        Vec3 avg(0, 0, 0); int cnt = 0;
        forEachNeighbor(m, v, [&](uint32_t u) { avg += m.pos[u]; ++cnt; });
        if (!cnt) continue;
        sum += length(avg / (float)cnt - m.pos[v]); ++n;
    }
    return n ? sum / n : 0;
}

void behaviours() {
    tk::section("Brush behaviour: smoothing, symmetry, masks, pressure, stroke continuity");
    { // smoothing reduces surface roughness
        SFScene* s = sf_scene_create();
        uint32_t id = sphere(s, 5);
        SFBrush d = brush(BrushType::Draw, 0.06f, 1.0f);
        for (float y : {-0.1f, 0.0f, 0.1f}) stroke(s, id, d, line(-0.15f, 0.15f, y, 2));
        double r0 = roughness(snap(s, id), Vec3(0, 0, 1), 0.25f);
        SFBrush sm = brush(BrushType::Smooth, 0.3f, 1.0f);
        for (int i = 0; i < 3; ++i) stroke(s, id, sm, line(-0.2f, 0.2f, 0, 10));
        double r1 = roughness(snap(s, id), Vec3(0, 0, 1), 0.25f);
        CHECK_MSG(r1 < r0 * 0.5, "smooth should at least halve roughness: " + std::to_string(r0) + " -> " + std::to_string(r1));
        tk::pass("Smooth brush: roughness " + std::to_string(r0) + " -> " + std::to_string(r1));
        sf_scene_destroy(s);
    }
    { // X symmetry mirrors the deformation
        SFScene* s = sf_scene_create();
        uint32_t id = sphere(s, 5);
        Snap before = snap(s, id);
        SFBrush b = brush(BrushType::Draw, 0.2f, 1.0f);
        b.symX = 1;
        stroke(s, id, b, line(0.35f, 0.55f, 0.2f, 6));
        Snap after = snap(s, id);
        // icosphere is mirror symmetric in X: map each vertex to its mirror partner
        size_t moved = 0, mismatched = 0;
        float maxDisp = 0;
        for (size_t i = 0; i < before.pos.size(); ++i) {
            Vec3 d = after.pos[i] - before.pos[i];
            if (length(d) < 1e-6f) continue;
            ++moved;
            maxDisp = std::max(maxDisp, length(d));
            Vec3 mp{-before.pos[i].x, before.pos[i].y, before.pos[i].z};
            size_t j = 0; float best = 1e30f;
            for (size_t k = 0; k < before.pos.size(); ++k) { float dd = length2(before.pos[k] - mp); if (dd < best) { best = dd; j = k; } }
            Vec3 dm = after.pos[j] - before.pos[j];
            if (best > 1e-8f || length(Vec3(-d.x, d.y, d.z) - dm) > 1e-4f) ++mismatched;
        }
        size_t left = 0, right = 0;
        for (size_t i = 0; i < before.pos.size(); ++i)
            if (length(after.pos[i] - before.pos[i]) > 1e-6f) (before.pos[i].x > 0 ? right : left)++;
        CHECK(moved > 50 && maxDisp > 0.02f);
        CHECK_MSG(mismatched == 0, std::to_string(mismatched) + " vertices lack a mirrored counterpart");
        CHECK(left > 20 && right > 20);
        tk::pass("X symmetry: " + std::to_string(right) + " verts on +X and " + std::to_string(left) + " on -X moved as exact mirror images");
        // undo covers both sides
        sf_undo(s);
        CHECK(snap(s, id).sameData(before));
        sf_scene_destroy(s);
    }
    { // radial symmetry
        SFScene* s = sf_scene_create();
        float p[3] = {1.0f, 64, 32};
        uint32_t id = sf_add_primitive(s, (int)PrimitiveType::UVSphere, p, 3);
        Snap before = snap(s, id);
        SFBrush b = brush(BrushType::Draw, 0.2f, 1.0f);
        b.radialCount = 4; b.radialAxis = 1;
        stroke(s, id, b, line(-0.05f, 0.05f, 0.0f, 2));
        Snap after = snap(s, id);
        int quadrant[4] = {0, 0, 0, 0};
        for (size_t i = 0; i < before.pos.size(); ++i) {
            if (length(after.pos[i] - before.pos[i]) < 1e-3f) continue;
            Vec3 q = before.pos[i];
            if (q.z > 0.7f) quadrant[0]++; else if (q.x > 0.7f) quadrant[1]++; else if (q.z < -0.7f) quadrant[2]++; else if (q.x < -0.7f) quadrant[3]++;
        }
        CHECK(quadrant[0] > 5 && quadrant[0] == quadrant[1] && quadrant[1] == quadrant[2] && quadrant[2] == quadrant[3]);
        tk::pass("radial symmetry x4 deforms four identical regions (" + std::to_string(quadrant[0]) + " verts each)");
        sf_scene_destroy(s);
    }
    { // mask really protects
        SFScene* s = sf_scene_create();
        uint32_t id = sphere(s, 5);
        sf_op_mask(s, id, 7, 0); // mask everything
        Snap before = snap(s, id);
        for (BrushType t : {BrushType::Draw, BrushType::Smooth, BrushType::Move, BrushType::Paint, BrushType::Inflate, BrushType::SnakeHook}) {
            SFBrush b = brush(t, 0.3f, 1.0f);
            stroke(s, id, b, line(-0.2f, 0.3f, 0, 6));
        }
        Snap after = snap(s, id);
        CHECK_MSG(after.pos == before.pos && after.col == before.col, "fully masked mesh must not change");
        // half mask: only the unmasked half moves
        sf_op_mask(s, id, 0, 0);
        SFMeshView v;
        sf_object_mesh(s, id, &v);
        Snap half = snap(s, id);
        SFBrush mb = brush(BrushType::Mask, 0.6f, 1.0f);
        stroke(s, id, mb, line(-0.6f, -0.6f, 0, 1));
        stroke(s, id, mb, line(-0.6f, -0.55f, 0, 3));
        Snap masked = snap(s, id);
        SFBrush d = brush(BrushType::Draw, 0.5f, 1.0f);
        stroke(s, id, d, line(-0.3f, 0.3f, 0, 8));
        Snap res = snap(s, id);
        double movedMasked = 0, movedFree = 0;
        for (size_t i = 0; i < res.pos.size(); ++i) {
            float disp = length(res.pos[i] - masked.pos[i]);
            if (masked.mask[i] >= 1.0f) movedMasked = std::max(movedMasked, (double)disp);
            if (masked.mask[i] < 0.01f) movedFree = std::max(movedFree, (double)disp);
        }
        CHECK_MSG(movedMasked < 1e-6 && movedFree > 0.02, "masked moved " + std::to_string(movedMasked) + ", free moved " + std::to_string(movedFree));
        tk::pass("masked vertices resist Draw/Smooth/Move/Paint/Inflate/Snake Hook; unmasked ones still move");
        sf_scene_destroy(s);
        (void)half;
    }
    { // pressure scales strength
        double disp[2];
        int n = 0;
        for (float pr : {0.2f, 1.0f}) {
            SFScene* s = sf_scene_create();
            uint32_t id = sphere(s, 5);
            Snap before = snap(s, id);
            stroke(s, id, brush(BrushType::Draw, 0.25f, 1.0f), line(-0.1f, 0.1f, 0, 4), pr);
            Snap after = snap(s, id);
            double mx = 0;
            for (size_t i = 0; i < before.pos.size(); ++i) mx = std::max(mx, (double)length(after.pos[i] - before.pos[i]));
            disp[n++] = mx;
            sf_scene_destroy(s);
        }
        CHECK(disp[0] > 0 && disp[1] > disp[0] * 3);
        tk::pass("pressure 0.2 vs 1.0 -> displacement " + std::to_string(disp[0]) + " vs " + std::to_string(disp[1]));
    }
    { // a fast stroke (one big move) is interpolated: no gaps along the path
        SFScene* s = sf_scene_create();
        uint32_t id = sphere(s, 6);
        Snap before = snap(s, id);
        stroke(s, id, brush(BrushType::Draw, 0.1f, 1.0f), {{-0.6f, 0}, {0.6f, 0}});
        Snap after = snap(s, id);
        size_t onPath = 0, untouched = 0;
        for (size_t i = 0; i < before.pos.size(); ++i) {
            Vec3 p = before.pos[i];
            if (p.z < 0 || std::fabs(p.y) > 0.03f || std::fabs(p.x) > 0.55f) continue;
            ++onPath;
            if (length(after.pos[i] - p) < 1e-4f) ++untouched;
        }
        CHECK(onPath > 30);
        CHECK_MSG(untouched == 0, std::to_string(untouched) + " of " + std::to_string(onPath) + " path vertices were skipped");
        tk::pass("fast stroke interpolation: all " + std::to_string(onPath) + " vertices along the path were sculpted");
        sf_scene_destroy(s);
    }
    { // strokes follow the visible surface and do not punch through to the back
        SFScene* s = sf_scene_create();
        float p[4] = {2, 2, 0.1f, 24};
        uint32_t id = sf_add_primitive(s, (int)PrimitiveType::Cube, p, 4);
        Snap before = snap(s, id);
        stroke(s, id, brush(BrushType::Draw, 0.5f, 1.0f), line(-0.2f, 0.2f, 0, 6));
        Snap after = snap(s, id);
        size_t front = 0, back = 0;
        for (size_t i = 0; i < before.pos.size(); ++i) {
            if (length(after.pos[i] - before.pos[i]) < 1e-6f) continue;
            (before.pos[i].z > 0 ? front : back)++;
        }
        CHECK(front > 20 && back == 0);
        tk::pass("front-face filtering: " + std::to_string(front) + " front vertices moved, 0 back vertices");
        sf_scene_destroy(s);
    }
    { // grab brushes actually translate geometry by the drag
        SFScene* s = sf_scene_create();
        uint32_t id = sphere(s, 5);
        Snap before = snap(s, id);
        stroke(s, id, brush(BrushType::Move, 0.3f, 1.0f), {{0, 0}, {0.1f, 0.1f}, {0.3f, 0.2f}});
        Snap after = snap(s, id);
        Vec3 best(0, 0, 0);
        for (size_t i = 0; i < before.pos.size(); ++i) { Vec3 d = after.pos[i] - before.pos[i]; if (length(d) > length(best)) best = d; }
        CHECK_NEAR(best.x, 0.3, 0.02);
        CHECK_NEAR(best.y, 0.2, 0.02);
        CHECK_NEAR(best.z, 0.0, 0.01);
        Snap hookBefore = after;
        stroke(s, id, brush(BrushType::SnakeHook, 0.2f, 1.0f), {{-0.5f, -0.5f}, {-0.7f, -0.7f}, {-0.9f, -0.9f}, {-1.1f, -1.1f}});
        Snap hooked = snap(s, id);
        float reach = 0;
        for (auto& q : hooked.pos) reach = std::max(reach, -q.x);
        CHECK(reach > 1.05f); // geometry was pulled outside the original sphere
        tk::pass("Move follows the drag exactly (0.3, 0.2); Snake Hook pulls a tendril out to x = -" + std::to_string(reach));
        sf_scene_destroy(s);
        (void)hookBefore;
    }
}

void dyntopoStroke() {
    tk::section("Dynamic topology during a stroke");
    SFScene* s = sf_scene_create();
    uint32_t id = sphere(s, 3); // coarse: 1,280 triangles
    Snap before = snap(s, id);
    SFBrush b = brush(BrushType::Draw, 0.3f, 0.8f);
    b.dyntopo = 1; b.dyntopoMode = 0; b.dyntopoDetail = 0.04f;
    int r = stroke(s, id, b, line(-0.2f, 0.2f, 0, 8));
    Snap after = snap(s, id);
    CHECK(r == 1);
    CHECK_MSG(after.pos.size() > before.pos.size() * 2, "dyntopo should add detail under the brush");
    CHECK(finite(after));
    MeshAnalysis a = analyzeMesh(toMesh(after));
    CHECK_MSG(a.watertight && a.volume > 0, "mesh must stay closed and consistently oriented");
    size_t backBefore = 0, backAfter = 0;
    for (auto& p : before.pos) backBefore += p.z < -0.3f;
    for (auto& p : after.pos) backAfter += p.z < -0.3f;
    CHECK(backBefore == backAfter); // detail only where sculpted
    CHECK(sf_undo(s) == 1);
    CHECK_MSG(snap(s, id).sameData(before), "undo must restore the original topology");
    CHECK(sf_redo(s) == 1);
    CHECK(snap(s, id).sameData(after));
    tk::pass("stroke refined " + std::to_string(before.tri.size() / 3) + " -> " + std::to_string(after.tri.size() / 3) +
             " triangles locally, stayed watertight, undo/redo exact");
    // collapse mode reduces detail again
    SFBrush c = brush(BrushType::Smooth, 0.35f, 0.5f);
    c.dyntopo = 1; c.dyntopoMode = 1; c.dyntopoDetail = 0.25f;
    stroke(s, id, c, line(-0.2f, 0.2f, 0, 8));
    Snap reduced = snap(s, id);
    MeshAnalysis ra = analyzeMesh(toMesh(reduced));
    CHECK(reduced.tri.size() < after.tri.size() && ra.watertight && finite(reduced));
    tk::pass("collapse mode simplified to " + std::to_string(reduced.tri.size() / 3) + " triangles, still watertight");
    sf_scene_destroy(s);
}

void guards() {
    tk::section("Stroke guards and repeated history");
    SFScene* s = sf_scene_create();
    uint32_t id = sphere(s, 4);
    SFBrush b = brush(BrushType::Draw);
    float d[3] = {0, 0, -1}, miss[3] = {5, 5, 5};
    CHECK(sf_stroke_begin(s, id, &b, miss, d, 1) == 0 && sf_stroke_active(s) == 0); // ray misses
    CHECK(sf_stroke_begin(s, 999, &b, miss, d, 1) == 0);                           // unknown object
    SFObjectProps pr;
    sf_object_get_props(s, id, &pr);
    pr.locked = 1;
    sf_object_set_props(s, id, &pr);
    float o[3] = {0, 0, 5};
    CHECK(sf_stroke_begin(s, id, &b, o, d, 1) == 0); // locked
    pr.locked = 0;
    sf_object_set_props(s, id, &pr);
    SFBrush bad = b;
    bad.type = 999;
    CHECK(sf_stroke_begin(s, id, &bad, o, d, 1) == 0);
    Snap base = snap(s, id);
    std::vector<Snap> states;
    for (int i = 0; i < 6; ++i) {
        SFBrush bb = brush((BrushType)(i % 4), 0.2f, 0.7f);
        stroke(s, id, bb, line(-0.3f + 0.1f * i, 0.1f * i, 0.1f * i - 0.2f, 5));
        states.push_back(snap(s, id));
    }
    bool ok = true;
    for (int cycle = 0; cycle < 40; ++cycle) {
        for (int i = 5; i >= 0; --i) { sf_undo(s); ok &= snap(s, id).sameData(i == 0 ? base : states[i - 1]); }
        ok &= sf_can_undo(s) == 1; // the "add sphere" step remains
        for (int i = 0; i < 6; ++i) { sf_redo(s); ok &= snap(s, id).sameData(states[i]); }
        ok &= sf_can_redo(s) == 0;
    }
    CHECK_MSG(ok, "state mismatch during repeated undo/redo");
    tk::pass("misses, locked objects and bad brushes are rejected; 40 cycles of 6-step undo/redo stay exact");
    // scaled / moved objects: world-space radius and rays are converted to object space
    SFObjectProps tp;
    sf_object_get_props(s, id, &tp);
    tp.scale[0] = tp.scale[1] = tp.scale[2] = 3; tp.position[0] = 10;
    sf_object_set_props(s, id, &tp);
    Snap b0 = snap(s, id);
    float o2[3] = {10, 0, 20};
    SFBrush wb = brush(BrushType::Draw, 0.6f, 1.0f); // 0.6 world units == 0.2 object units
    CHECK(sf_stroke_begin(s, id, &wb, o2, d, 1) == 1);
    sf_stroke_end(s);
    Snap b1 = snap(s, id);
    sf_undo(s);
    uint32_t hitId = 0;
    float hp[3];
    CHECK(sf_pick(s, o2, d, &hitId, hp, nullptr) == 1 && hitId == id);
    Vec3 localHit{(hp[0] - 10) / 3, hp[1] / 3, hp[2] / 3}; // world -> object space
    sf_redo(s);
    float far = 0; size_t moved = 0;
    for (size_t i = 0; i < b0.pos.size(); ++i)
        if (!(b0.pos[i] == b1.pos[i])) { ++moved; far = std::max(far, length(b0.pos[i] - localHit)); }
    CHECK_MSG(moved > 0 && far <= 0.2001f, "moved " + std::to_string(moved) + " far " + std::to_string(far));
    tk::pass("strokes on a moved + 3x scaled object use the correct object-space radius");
    sf_scene_destroy(s);
}

} // namespace

void runBrushTests() {
    eachBrush();
    behaviours();
    dyntopoStroke();
    guards();
}

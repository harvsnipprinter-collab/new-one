// File, persistence and end-to-end acceptance tests (through the C API used by the app).
#include "TestKit.h"
#include <cstring>
#include <filesystem>
#include <functional>

using namespace sf;

namespace {

SFBrush brush(BrushType t, float radius, float strength) {
    SFBrush b;
    sf_brush_default(&b);
    b.type = (int)t; b.radius = radius; b.strength = strength;
    return b;
}

// Stroke given world-space ray origins; all rays point along `dir`.
int strokeRays(SFScene* s, uint32_t id, const SFBrush& b, const std::vector<Vec3>& origins, Vec3 dir) {
    float d[3] = {dir.x, dir.y, dir.z};
    float o[3] = {origins[0].x, origins[0].y, origins[0].z};
    if (!sf_stroke_begin(s, id, &b, o, d, 1.0f)) return -1;
    for (size_t i = 1; i < origins.size(); ++i) {
        float oi[3] = {origins[i].x, origins[i].y, origins[i].z};
        sf_stroke_move(s, oi, d, 1.0f);
    }
    return sf_stroke_end(s);
}
std::vector<Vec3> frontLine(Vec3 a, Vec3 b, int n) { // rays start in front (+Z) of the model
    std::vector<Vec3> r;
    for (int i = 0; i <= n; ++i) { Vec3 p = lerp(a, b, (float)i / n); r.push_back({p.x, p.y, 50}); }
    return r;
}

Mesh meshOf(SFScene* s, uint32_t id) {
    SFMeshView v;
    Mesh m;
    if (!sf_object_mesh(s, id, &v)) return m;
    m.pos.resize(v.vertexCount);
    std::memcpy(m.pos.data(), v.positions, v.vertexCount * 12);
    m.tri.assign(v.indices, v.indices + v.triangleCount * 3);
    m.ensureAttributes();
    std::memcpy(m.col.data(), v.colors, v.vertexCount * 12);
    m.mask.assign(v.mask, v.mask + v.vertexCount);
    m.rough.assign(v.roughness, v.roughness + v.vertexCount);
    m.metal.assign(v.metalness, v.metalness + v.vertexCount);
    m.faceGroup.assign(v.faceGroups, v.faceGroups + v.triangleCount);
    m.finalize();
    return m;
}

bool sameMesh(const Mesh& a, const Mesh& b) {
    return a.pos == b.pos && a.tri == b.tri && a.col == b.col && a.mask == b.mask && a.rough == b.rough && a.metal == b.metal &&
           a.faceGroup == b.faceGroup;
}

bool sameScenes(SFScene* a, SFScene* b, std::string& why) {
    if (sf_object_count(a) != sf_object_count(b)) { why = "object count"; return false; }
    for (int i = 0; i < sf_object_count(a); ++i) {
        uint32_t ia = sf_object_id_at(a, i), ib = sf_object_id_at(b, i);
        if (ia != ib) { why = "object ids/order"; return false; }
        if (std::string(sf_object_name(a, ia)) != sf_object_name(b, ib)) { why = "name"; return false; }
        SFObjectProps pa, pb;
        sf_object_get_props(a, ia, &pa); sf_object_get_props(b, ib, &pb);
        if (std::memcmp(&pa, &pb, sizeof(pa)) != 0) { why = std::string("properties of ") + sf_object_name(a, ia); return false; }
        if (!sameMesh(meshOf(a, ia), meshOf(b, ib))) { why = std::string("mesh data of ") + sf_object_name(a, ia); return false; }
    }
    if (std::string(sf_get_app_state(a)) != sf_get_app_state(b)) { why = "app state"; return false; }
    return true;
}

void setTransform(SFScene* s, uint32_t id, Vec3 pos, Vec3 scale, Quat rot = Quat()) {
    SFObjectProps p;
    sf_object_get_props(s, id, &p);
    p.position[0] = pos.x; p.position[1] = pos.y; p.position[2] = pos.z;
    p.scale[0] = scale.x; p.scale[1] = scale.y; p.scale[2] = scale.z;
    p.rotation[0] = rot.x; p.rotation[1] = rot.y; p.rotation[2] = rot.z; p.rotation[3] = rot.w;
    sf_object_set_props(s, id, &p);
}
void setMaterial(SFScene* s, uint32_t id, Vec3 color, float rough, float metal, float opacity = 1) {
    SFObjectProps p;
    sf_object_get_props(s, id, &p);
    p.baseColor[0] = color.x; p.baseColor[1] = color.y; p.baseColor[2] = color.z;
    p.roughness = rough; p.metallic = metal; p.opacity = opacity;
    sf_object_set_props(s, id, &p);
}
void setParent(SFScene* s, uint32_t id, uint32_t parent) { sf_object_set_parent(s, id, parent); }
uint32_t prim(SFScene* s, PrimitiveType t, std::initializer_list<float> params, const char* name) {
    std::vector<float> p(params);
    uint32_t id = sf_add_primitive(s, (int)t, p.data(), (int)p.size());
    if (id && name) sf_object_set_name(s, id, name);
    return id;
}

} // namespace

void runFileTests() {
    tk::section("Project save / load");
    SFScene* s = sf_scene_create();
    uint32_t head = prim(s, PrimitiveType::Icosphere, {1, 4}, "Head");
    uint32_t eye = prim(s, PrimitiveType::UVSphere, {0.2f, 24, 12}, "Eye \"left\" \xC3\xA9");
    uint32_t hat = prim(s, PrimitiveType::Cylinder, {0.6f, 0.5f, 32, 1}, "Hat");
    setTransform(s, eye, {0.4f, 0.2f, 0.9f}, {1, 1.2f, 1});
    setTransform(s, hat, {0, 1.1f, 0}, {1, 1, 1}, Quat::axisAngle({0, 0, 1}, 0.2f));
    setParent(s, eye, head); setParent(s, hat, head);
    setMaterial(s, eye, {0.02f, 0.02f, 0.02f}, 0.1f, 0.0f);
    setMaterial(s, hat, {0.9f, 0.7f, 0.1f}, 0.3f, 1.0f, 0.8f);
    SFBrush paint = brush(BrushType::Paint, 0.3f, 1.0f);
    paint.color[0] = 0.1f; paint.color[1] = 0.2f; paint.color[2] = 0.9f;
    strokeRays(s, head, paint, frontLine({-0.3f, 0, 0}, {0.3f, 0, 0}, 6), {0, 0, -1});
    strokeRays(s, head, brush(BrushType::Mask, 0.2f, 1.0f), frontLine({0, 0.4f, 0}, {0, 0.5f, 0}, 2), {0, 0, -1});
    strokeRays(s, head, brush(BrushType::FaceGroup, 0.2f, 1.0f), frontLine({0, -0.4f, 0}, {0, -0.5f, 0}, 2), {0, 0, -1});
    strokeRays(s, head, brush(BrushType::Draw, 0.2f, 1.0f), frontLine({0, -0.1f, 0}, {0, -0.1f, 0}, 1), {0, 0, -1});
    sf_set_app_state(s, "{\"camera\":{\"fov\":45},\"brushPresets\":[{\"name\":\"My Clay\",\"radius\":0.3}]}");
    std::string file = tk::path("roundtrip.sculptforge");
    CHECK_MSG(sf_save_project(s, file.c_str()) == 1, sf_last_error(s));
    SFScene* t = sf_scene_create();
    CHECK_MSG(sf_load_project(t, file.c_str()) == 1, sf_last_error(t));
    std::string why;
    CHECK_MSG(sameScenes(s, t, why), "difference in " + why);
    // new objects after a load must not collide with loaded ids
    uint32_t extra = prim(t, PrimitiveType::Cube, {1, 1, 1, 2}, "Extra");
    CHECK(extra != head && extra != eye && extra != hat && extra != 0);
    tk::pass("3 objects with transforms, hierarchy, materials, paint, mask, face groups, UI state: identical after reload");

    std::vector<uint8_t> good;
    readWholeFile(file, good);
    auto tryLoad = [&](const std::vector<uint8_t>& bytes, const char* name) {
        std::string p = tk::path(name);
        writeWholeFile(p, bytes);
        SFScene* x = sf_scene_create();
        prim(x, PrimitiveType::Cube, {1, 1, 1, 2}, "Keep me");
        int r = sf_load_project(x, p.c_str());
        std::string e = sf_last_error(x);
        bool intact = sf_object_count(x) == 1 && std::string(sf_object_name(x, sf_object_id_at(x, 0))) == "Keep me";
        sf_scene_destroy(x);
        return std::make_pair(r == 0 && intact, e);
    };
    std::vector<uint8_t> bad = good;
    bad[bad.size() / 2] ^= 0x40;
    auto r1 = tryLoad(bad, "corrupt_flip.sculptforge");
    CHECK_MSG(r1.first && r1.second.find("corrupted") != std::string::npos, r1.second);
    auto r2 = tryLoad(std::vector<uint8_t>(good.begin(), good.begin() + good.size() * 2 / 3), "corrupt_truncated.sculptforge");
    CHECK_MSG(r2.first && r2.second.find("truncated") != std::string::npos, r2.second);
    auto r3 = tryLoad(std::vector<uint8_t>(500, 0x55), "corrupt_garbage.sculptforge");
    CHECK(r3.first);
    auto r4 = tryLoad({}, "corrupt_empty.sculptforge");
    CHECK(r4.first);
    std::vector<uint8_t> future = good; // bump the version and fix the checksum: must be refused as too new
    uint32_t ver = 99;
    std::memcpy(future.data() + 8, &ver, 4);
    uint32_t crc = crc32(future.data(), future.size() - 12);
    std::memcpy(future.data() + future.size() - 12, &crc, 4);
    auto r5 = tryLoad(future, "future_version.sculptforge");
    CHECK_MSG(r5.first && r5.second.find("newer version") != std::string::npos, r5.second);
    tk::pass("bit-flip, truncation, garbage, empty file and future version are all rejected; open scene left untouched");

    { // a failed save must not destroy the previous good file
        Scene sc;
        Mesh m = makeIcosphere(1, 2);
        m.pos[3].x = std::numeric_limits<float>::quiet_NaN();
        sc.add(m, "broken");
        sc.objects[0]->mesh.pos[3].x = std::numeric_limits<float>::quiet_NaN();
        std::string err;
        CHECK(!saveProject(sc, file, &err) && err.find("refusing to save") != std::string::npos);
        std::vector<uint8_t> still;
        readWholeFile(file, still);
        CHECK(still == good);
        CHECK(!std::filesystem::exists(toPath(file + ".tmp")));
        tk::pass("saving an invalid scene is refused and the existing project file is unchanged");
    }

    tk::section("Import / export round trips");
    Mesh src = meshOf(s, head);
    MeshAnalysis sa = analyzeMesh(src);
    struct F { int fmt; const char* ext; bool colour; bool canImport; } formats[] = {
        {0, "stl", false, true}, {1, "obj", true, true}, {2, "ply", true, true}, {3, "glb", true, true}, {4, "3mf", false, false}};
    for (auto& f : formats) {
        std::string p = tk::path(std::string("head.") + f.ext);
        CHECK_MSG(sf_export(s, p.c_str(), f.fmt, &head, 1, 10.0f) == 1, sf_last_error(s));
        std::vector<uint8_t> bytes;
        CHECK(readWholeFile(p, bytes) && bytes.size() > 100);
        if (!f.canImport) { CHECK(bytes[0] == 'P' && bytes[1] == 'K'); tk::pass(std::string(f.ext) + ": written (" + std::to_string(bytes.size()) + " bytes), import not supported"); continue; }
        SFScene* x = sf_scene_create();
        int n = sf_import(x, p.c_str());
        CHECK_MSG(n == 1, std::string(f.ext) + ": " + sf_last_error(x));
        if (n == 1) {
            Mesh im = meshOf(x, sf_object_id_at(x, 0));
            im.transform(Scene().worldMatrix(0)); // identity
            SFObjectProps pr;
            sf_object_get_props(x, sf_object_id_at(x, 0), &pr);
            MeshAnalysis ia = analyzeMesh(im);
            CHECK_MSG(im.tcount() == src.tcount(), std::string(f.ext) + " triangle count");
            CHECK_MSG(ia.watertight, std::string(f.ext) + " should re-import watertight");
            CHECK_NEAR(ia.volume / (sa.volume * 1000.0), 1.0, 1e-3); // scale 10 -> volume x1000
            CHECK_NEAR(ia.bounds.size().x / (sa.bounds.size().x * 10.0), 1.0, 1e-3);
            if (f.colour) {
                size_t blue = 0, srcBlue = 0;
                for (auto& c : im.col) blue += c.z > 0.8f && c.x < 0.2f;
                for (auto& c : src.col) srcBlue += c.z > 0.8f && c.x < 0.2f;
                CHECK_MSG(blue == srcBlue && blue > 0, std::string(f.ext) + " vertex colours: " + std::to_string(blue) + " vs " + std::to_string(srcBlue));
            }
            tk::pass(std::string(f.ext) + ": " + std::to_string(bytes.size()) + " bytes, re-imported " + std::to_string(im.tcount()) +
                     " tris, volume and scale match" + (f.colour ? ", colours match" : ""));
        }
        sf_scene_destroy(x);
    }
    { // whole scene with hierarchy -> GLB keeps separate objects, world placement and materials
        std::string p = tk::path("scene.glb");
        CHECK(sf_export(s, p.c_str(), 3, nullptr, 0, 1.0f) == 1);
        SFScene* x = sf_scene_create();
        CHECK(sf_import(x, p.c_str()) == 3);
        bool found = false;
        for (int i = 0; i < sf_object_count(x); ++i) {
            uint32_t id = sf_object_id_at(x, i);
            if (std::string(sf_object_name(x, id)) != "Hat") continue;
            found = true;
            SFObjectProps pr;
            sf_object_get_props(x, id, &pr);
            CHECK_NEAR(pr.metallic, 1.0, 1e-4);
            CHECK_NEAR(pr.baseColor[0], 0.9, 1e-3);
            CHECK_NEAR(pr.opacity, 0.8, 1e-3);
            CHECK_NEAR(meshOf(x, id).bounds().center().y, 1.1, 0.02); // parent/child world transform applied
        }
        CHECK(found);
        sf_scene_destroy(x);
        tk::pass("multi-object GLB: 3 objects, names, PBR material and world transforms preserved");
    }
    { // error handling
        SFScene* x = sf_scene_create();
        CHECK(sf_import(x, tk::path("does_not_exist.stl").c_str()) == 0);
        writeWholeFile(tk::path("garbage.stl"), std::vector<uint8_t>(300, 'x'));
        CHECK(sf_import(x, tk::path("garbage.stl").c_str()) == 0);
        writeWholeFile(tk::path("garbage.glb"), std::vector<uint8_t>(300, 'x'));
        CHECK(sf_import(x, tk::path("garbage.glb").c_str()) == 0);
        writeWholeFile(tk::path("model.fbx"), std::vector<uint8_t>(300, 'x'));
        CHECK(sf_import(x, tk::path("model.fbx").c_str()) == 0 && std::string(sf_last_error(x)).find("unsupported") != std::string::npos);
        CHECK(sf_object_count(x) == 0);
        CHECK(sf_export(x, tk::path("empty.stl").c_str(), 0, nullptr, 0, 1) == 0); // empty scene
        CHECK(sf_export(s, tk::path("bad.stl").c_str(), 0, &head, 1, -1) == 0);    // bad scale
        CHECK(sf_export(s, tk::path("bad.xyz").c_str(), 9, &head, 1, 1) == 0);     // bad format
        sf_scene_destroy(x);
        tk::pass("missing, garbage and unsupported files, empty scenes and bad parameters fail with messages, no crash");
    }
    sf_scene_destroy(s);
    sf_scene_destroy(t);
}

void runAcceptanceTests() {
    tk::section("Acceptance tests (spec section 41)");
    { // 1 sculpting, 9 undo
        SFScene* s = sf_scene_create();
        uint32_t id = prim(s, PrimitiveType::Icosphere, {1, 5}, "Head");
        Mesh before = meshOf(s, id);
        SFBrush b = brush(BrushType::Draw, 0.22f, 1.0f);
        for (int i = 0; i < 4; ++i) strokeRays(s, id, b, frontLine({-0.03f, 0, 0}, {0.03f, 0, 0}, 4), {0, 0, -1});
        Mesh after = meshOf(s, id);
        float tip = after.bounds().mx.z;
        CHECK_MSG(tip > 1.15f, "nose tip z = " + std::to_string(tip));
        CHECK_NEAR(after.bounds().mn.z, -1.0, 1e-3);
        CHECK(analyzeMesh(after).watertight);
        tk::pass("[ACCEPT 1] sculpted a nose-like protrusion: front extends from z=1.00 to z=" + std::to_string(tip));
        for (int i = 0; i < 4; ++i) sf_undo(s);
        CHECK(sameMesh(meshOf(s, id), before));
        tk::pass("[ACCEPT 9] undo restores the exact previous geometry");
        sf_scene_destroy(s);
    }
    tk::pass("[ACCEPT 2] smoothing: covered by 'Smooth brush: roughness' check above");
    tk::pass("[ACCEPT 3] voxel remesh of stretched sphere: covered by remesh checks above");
    tk::pass("[ACCEPT 4] X symmetry: covered by symmetry check above");
    { // 5 multiple objects, 6 materials, 7 painting persistence, 8 subdivision, 10 save/load, 11 STL
        SFScene* s = sf_scene_create();
        uint32_t head = prim(s, PrimitiveType::Icosphere, {1, 4}, "Head");
        uint32_t eyeL = prim(s, PrimitiveType::UVSphere, {0.18f, 24, 12}, "Eye L");
        uint32_t eyeR = sf_object_duplicate(s, eyeL);
        sf_object_set_name(s, eyeR, "Eye R");
        uint32_t earL = prim(s, PrimitiveType::Cone, {0.3f, 0.6f, 24, 1}, "Ear L");
        uint32_t earR = sf_object_duplicate(s, earL);
        sf_object_set_name(s, earR, "Ear R");
        setTransform(s, eyeL, {-0.35f, 0.2f, 0.88f}, {1, 1, 1}); setTransform(s, eyeR, {0.35f, 0.2f, 0.88f}, {1, 1, 1});
        setTransform(s, earL, {-0.55f, 0.95f, 0}, {1, 1, 1}); setTransform(s, earR, {0.55f, 0.95f, 0}, {1, 1, 1});
        CHECK(sf_object_count(s) == 5 && head && eyeL && eyeR && earL && earR);
        SFMeshView a, b;
        sf_object_mesh(s, eyeL, &a); sf_object_mesh(s, eyeR, &b);
        CHECK(a.positions != b.positions); // independent geometry
        strokeRays(s, eyeL, brush(BrushType::Inflate, 0.2f, 1.0f), {{-0.35f, 0.2f, 50}}, {0, 0, -1});
        CHECK(!sameMesh(meshOf(s, eyeL), meshOf(s, eyeR)));
        tk::pass("[ACCEPT 5] head, two eyes and two ears are five independent mesh objects");
        setMaterial(s, head, {0.95f, 0.6f, 0.25f}, 0.8f, 0.0f);
        setMaterial(s, eyeL, {0.02f, 0.02f, 0.02f}, 0.05f, 0.0f); setMaterial(s, eyeR, {0.02f, 0.02f, 0.02f}, 0.05f, 0.0f);
        SFObjectProps ph, pe;
        sf_object_get_props(s, head, &ph); sf_object_get_props(s, eyeL, &pe);
        CHECK(ph.roughness != pe.roughness && ph.baseColor[0] != pe.baseColor[0]);
        tk::pass("[ACCEPT 6] head and eyes carry different materials");
        size_t t0 = meshOf(s, head).tcount();
        CHECK(sf_op_subdivide(s, head, 1) == 1);
        size_t t1 = meshOf(s, head).tcount();
        CHECK(t1 == t0 * 4);
        tk::pass("[ACCEPT 8] subdivision: " + std::to_string(t0) + " -> " + std::to_string(t1) + " triangles");
        SFBrush p = brush(BrushType::Paint, 0.15f, 1.0f);
        p.color[0] = 0.9f; p.color[1] = 0.05f; p.color[2] = 0.05f;
        strokeRays(s, head, p, frontLine({-0.2f, -0.3f, 0}, {0.2f, -0.3f, 0}, 6), {0, 0, -1});
        size_t redBefore = 0;
        for (auto& c : meshOf(s, head).col) redBefore += c.x > 0.8f && c.y < 0.2f;
        CHECK(redBefore > 20);
        std::string file = tk::path("acceptance.sculptforge");
        CHECK(sf_save_project(s, file.c_str()) == 1);
        SFScene* t = sf_scene_create(); // "close and reopen"
        CHECK(sf_load_project(t, file.c_str()) == 1);
        size_t redAfter = 0;
        for (auto& c : meshOf(t, head).col) redAfter += c.x > 0.8f && c.y < 0.2f;
        CHECK(redAfter == redBefore);
        tk::pass("[ACCEPT 7] painted mark (" + std::to_string(redBefore) + " red vertices) persists after save and reload");
        std::string why;
        CHECK_MSG(sameScenes(s, t, why), why);
        tk::pass("[ACCEPT 10] reopened scene: object count, transforms, materials and topology identical");
        std::string stl = tk::path("acceptance_head.stl");
        CHECK(sf_export(t, stl.c_str(), 0, &head, 1, 25.0f) == 1);
        SFScene* x = sf_scene_create();
        CHECK(sf_import(x, stl.c_str()) == 1);
        MeshAnalysis ia = analyzeMesh(meshOf(x, sf_object_id_at(x, 0)));
        CHECK(ia.triangles == t1 && ia.watertight);
        CHECK_NEAR(ia.bounds.size().y, 50.0, 0.5); // 2 units * 25 mm
        tk::pass("[ACCEPT 11] STL exported and re-read (engine reader; the Node validator re-parses it independently)");
        sf_scene_destroy(x); sf_scene_destroy(t); sf_scene_destroy(s);
    }
    std::printf("  ---- [ACCEPT 12] rendering: NOT RUN here (needs Metal; no GPU renderer on this machine)\n");
    std::printf("  ---- [ACCEPT 13] iOS launch: NOT RUN here (needs macOS + Xcode)\n");
    std::printf("  ---- [ACCEPT 14] touch input: NOT RUN here (needs a device or simulator)\n");

    tk::section("Acceptance 15: stylised cat character built through the app API");
    SFScene* s = sf_scene_create();
    const Vec3 fwd{0, 0, -1};
    // --- head ---
    uint32_t head = prim(s, PrimitiveType::Icosphere, {1, 5}, "Head");
    CHECK(sf_op_subdivide(s, head, 1) == 1); // 81,920 triangles
    setTransform(s, head, {0, 2.2f, 0}, {1.15f, 1, 1});
    SFBrush sym = brush(BrushType::Inflate, 0.35f, 0.9f);
    sym.symX = 1;
    for (int i = 0; i < 3; ++i) strokeRays(s, head, sym, frontLine({0.5f, 1.95f, 0}, {0.55f, 1.9f, 0}, 2), fwd); // cheeks (both sides)
    SFBrush clay = brush(BrushType::Clay, 0.3f, 1.0f);
    for (int i = 0; i < 4; ++i) strokeRays(s, head, clay, frontLine({-0.12f, 1.9f, 0}, {0.12f, 1.9f, 0}, 4), fwd);  // muzzle
    SFBrush nose = brush(BrushType::Draw, 0.1f, 1.0f);
    for (int i = 0; i < 3; ++i) strokeRays(s, head, nose, {{0, 2.02f, 50}}, fwd);                                    // nose
    SFBrush crease = brush(BrushType::Crease, 0.08f, 0.8f);
    crease.symX = 1;
    strokeRays(s, head, crease, frontLine({0.02f, 1.8f, 0}, {0.25f, 1.72f, 0}, 6), fwd);                              // mouth
    SFBrush smooth = brush(BrushType::Smooth, 0.25f, 0.5f);
    strokeRays(s, head, smooth, frontLine({-0.4f, 2.0f, 0}, {0.4f, 2.0f, 0}, 8), fwd);
    Mesh headMesh = meshOf(s, head);
    CHECK(headMesh.tcount() == 81920 && analyzeMesh(headMesh).watertight);
    CHECK(headMesh.bounds().mx.z > 1.1f); // muzzle + nose stick out
    // --- ears: sculpt one, mirror by duplication ---
    uint32_t earL = prim(s, PrimitiveType::Cone, {0.38f, 0.75f, 32, 1}, "Ear L");
    CHECK(sf_op_subdivide(s, earL, 0) == 1);
    setTransform(s, earL, {-0.62f, 3.05f, 0}, {1, 1, 0.5f}, Quat::axisAngle({0, 0, 1}, 0.35f));
    strokeRays(s, earL, brush(BrushType::Draw, 0.2f, 0.6f), frontLine({-0.62f, 2.95f, 0}, {-0.66f, 3.1f, 0}, 3), fwd);
    SFBrush inv = brush(BrushType::Draw, 0.16f, 0.8f);
    inv.invert = 1;
    strokeRays(s, earL, inv, frontLine({-0.62f, 2.95f, 0}, {-0.64f, 3.05f, 0}, 3), fwd); // inner ear cavity
    uint32_t earR = sf_object_duplicate(s, earL);
    sf_object_set_name(s, earR, "Ear R");
    setTransform(s, earR, {0.62f, 3.05f, 0}, {-1, 1, 0.5f}, Quat::axisAngle({0, 0, 1}, -0.35f)); // mirrored copy
    // --- eyes ---
    uint32_t eyeL = prim(s, PrimitiveType::UVSphere, {0.17f, 32, 16}, "Eye L");
    setTransform(s, eyeL, {-0.42f, 2.38f, 0.83f}, {1, 1.25f, 0.6f});
    uint32_t eyeR = sf_object_duplicate(s, eyeL);
    sf_object_set_name(s, eyeR, "Eye R");
    setTransform(s, eyeR, {0.42f, 2.38f, 0.83f}, {1, 1.25f, 0.6f});
    // --- body, limbs ---
    uint32_t body = prim(s, PrimitiveType::Capsule, {0.62f, 2.0f, 48}, "Body");
    CHECK(sf_op_subdivide(s, body, 1) == 1);
    setTransform(s, body, {0, 0.55f, 0}, {1, 1, 0.85f});
    SFBrush belly = brush(BrushType::Inflate, 0.45f, 0.6f);
    strokeRays(s, body, belly, frontLine({0, 0.3f, 0}, {0, 0.45f, 0}, 3), fwd);
    uint32_t armL = prim(s, PrimitiveType::Tube, {1.0f, 0.17f, 0.13f, 24}, "Arm L");
    setTransform(s, armL, {-0.7f, 0.85f, 0}, {1, 1, 1}, Quat::axisAngle({0, 0, 1}, -0.5f));
    uint32_t armR = sf_object_duplicate(s, armL);
    sf_object_set_name(s, armR, "Arm R");
    setTransform(s, armR, {0.7f, 0.85f, 0}, {1, 1, 1}, Quat::axisAngle({0, 0, 1}, 0.5f));
    uint32_t legL = prim(s, PrimitiveType::Capsule, {0.2f, 0.9f, 32}, "Leg L");
    setTransform(s, legL, {-0.3f, -0.75f, 0}, {1, 1, 1});
    uint32_t legR = sf_object_duplicate(s, legL);
    sf_object_set_name(s, legR, "Leg R");
    setTransform(s, legR, {0.3f, -0.75f, 0}, {1, 1, 1});
    // --- hat: crown + brim merged into one object ---
    uint32_t crown = prim(s, PrimitiveType::Cylinder, {0.55f, 0.6f, 48, 1}, "Hat crown");
    setTransform(s, crown, {0, 3.45f, 0}, {1, 1, 1});
    uint32_t brim = prim(s, PrimitiveType::Cylinder, {0.95f, 0.06f, 64, 1}, "Hat brim");
    setTransform(s, brim, {0, 3.15f, 0}, {1, 1, 1});
    uint32_t hatParts[2] = {crown, brim};
    uint32_t hat = sf_op_merge(s, hatParts, 2);
    CHECK(hat != 0);
    sf_object_set_name(s, hat, "Hat");
    // --- jacket: mask the upper body, extract a shell, sculpt wrinkles ---
    SFBrush mask = brush(BrushType::Mask, 0.75f, 1.0f);
    mask.frontFacesOnly = 0;
    for (int i = 0; i < 2; ++i) {
        strokeRays(s, body, mask, frontLine({-0.3f, 0.95f, 0}, {0.3f, 0.95f, 0}, 4), fwd);
        strokeRays(s, body, mask, {{-0.3f, 0.95f, -50}, {0.3f, 0.95f, -50}}, {0, 0, 1}); // from behind
    }
    uint32_t jacket = sf_op_extract_mask(s, body, 0.05f);
    CHECK_MSG(jacket != 0, sf_last_error(s));
    sf_object_set_name(s, jacket, "Jacket");
    CHECK(sf_op_mask(s, body, 0, 0) == 1);
    SFBrush wrinkle = brush(BrushType::Crease, 0.07f, 0.9f);
    strokeRays(s, jacket, wrinkle, frontLine({-0.35f, 0.9f, 0}, {-0.1f, 0.7f, 0}, 6), fwd);
    strokeRays(s, jacket, wrinkle, frontLine({0.35f, 0.95f, 0}, {0.12f, 0.75f, 0}, 6), fwd);
    // --- accessories ---
    uint32_t tie = prim(s, PrimitiveType::Cone, {0.12f, 0.5f, 16, 1}, "Tie");
    setTransform(s, tie, {0, 1.0f, 0.55f}, {1, -1, 0.4f});
    uint32_t button = prim(s, PrimitiveType::UVSphere, {0.05f, 16, 8}, "Button");
    setTransform(s, button, {0, 0.55f, 0.6f}, {1, 1, 0.5f});
    // --- hierarchy ---
    for (uint32_t c : {earL, earR, eyeL, eyeR, hat}) setParent(s, c, head);
    for (uint32_t c : {armL, armR, legL, legR, jacket, tie, button}) setParent(s, c, body);
    SFObjectProps cyc;
    sf_object_get_props(s, head, &cyc);
    cyc.parent = eyeL;
    CHECK(sf_object_set_props(s, head, &cyc) == 0 && sf_object_set_parent(s, head, eyeL) == 0); // cycles are refused
    // --- paint and materials ---
    float orange[3] = {0.95f, 0.55f, 0.15f}, navy[3] = {0.05f, 0.08f, 0.25f}, white[3] = {1, 1, 1};
    for (uint32_t o : {head, earL, earR, body, armL, armR, legL, legR}) CHECK(sf_op_fill(s, o, orange, 0, 0, 1) == 1);
    CHECK(sf_op_fill(s, jacket, navy, 0, 0, 1) == 1);
    SFBrush stripe = brush(BrushType::Paint, 0.09f, 1.0f);
    stripe.symX = 1;
    stripe.color[0] = 0.35f; stripe.color[1] = 0.15f; stripe.color[2] = 0.05f;
    strokeRays(s, head, stripe, frontLine({0.25f, 2.85f, 0}, {0.3f, 2.6f, 0}, 5), fwd); // forehead stripes
    SFBrush muzzlePaint = brush(BrushType::Paint, 0.3f, 1.0f);
    std::memcpy(muzzlePaint.color, white, sizeof(white));
    strokeRays(s, head, muzzlePaint, frontLine({-0.1f, 1.9f, 0}, {0.1f, 1.9f, 0}, 3), fwd);
    setMaterial(s, head, {1, 1, 1}, 0.85f, 0.0f);                       // matte fur
    for (uint32_t e : {eyeL, eyeR}) setMaterial(s, e, {0.02f, 0.02f, 0.02f}, 0.05f, 0.0f); // glossy eyes
    setMaterial(s, hat, {0.08f, 0.08f, 0.1f}, 0.7f, 0.0f);              // felt
    setMaterial(s, jacket, {1, 1, 1}, 0.75f, 0.0f);                     // fabric
    setMaterial(s, tie, {0.7f, 0.05f, 0.08f}, 0.4f, 0.0f);
    setMaterial(s, button, {1.0f, 0.78f, 0.34f}, 0.25f, 1.0f);          // gold
    CHECK(sf_object_count(s) == 14);
    // picking resolves the object in front
    uint32_t hit = 0;
    float o[3] = {0.42f, 2.38f, 50}, d[3] = {0, 0, -1}, pt[3];
    CHECK(sf_pick(s, o, d, &hit, pt, nullptr) == 1 && hit == eyeR);
    float o2[3] = {0, 3.5f, 50};
    CHECK(sf_pick(s, o2, d, &hit, pt, nullptr) == 1 && hit == hat);
    // moving the parent moves the children
    float before[16], after[16];
    sf_object_world_matrix(s, eyeL, before);
    setTransform(s, head, {0, 2.3f, 0.05f}, {1.15f, 1, 1});
    sf_object_world_matrix(s, eyeL, after);
    CHECK_NEAR(after[13] - before[13], 0.1, 1e-4);
    CHECK_NEAR(after[14] - before[14], 0.05, 1e-4);
    tk::pass("built 14 objects: sculpted head (81,920 tris), ears, eyes, body, limbs, merged hat, extracted jacket, tie, button");
    // --- save, reopen, compare ---
    sf_set_app_state(s, "{\"note\":\"cat character\"}");
    std::string file = tk::path("cat_character.sculptforge");
    double t0 = tk::nowMs();
    CHECK_MSG(sf_save_project(s, file.c_str()) == 1, sf_last_error(s));
    double saveMs = tk::nowMs() - t0;
    SFScene* r = sf_scene_create();
    CHECK_MSG(sf_load_project(r, file.c_str()) == 1, sf_last_error(r));
    std::string why;
    CHECK_MSG(sameScenes(s, r, why), why);
    tk::pass("saved (" + std::to_string((int)saveMs) + " ms, " + std::to_string(std::filesystem::file_size(toPath(file)) / 1024) +
             " KB) and reopened: all 14 objects, hierarchy, paint and materials identical");
    // --- exports for the independent validator ---
    for (auto& f : {std::make_pair(0, "cat_scene.stl"), std::make_pair(1, "cat_scene.obj"), std::make_pair(2, "cat_scene.ply"),
                    std::make_pair(3, "cat_scene.glb"), std::make_pair(4, "cat_scene.3mf")})
        CHECK_MSG(sf_export(r, tk::path(f.second).c_str(), f.first, nullptr, 0, 20.0f) == 1, sf_last_error(r));
    // --- print preparation: the raw assembly is NOT one watertight solid, and the analysis must say so ---
    SFAnalysis an;
    CHECK(sf_analyze(r, nullptr, 0, 20.0f, 0, &an) == 1);
    CHECK(an.components >= 14);
    CHECK_NEAR(an.boundsMax[1] - an.boundsMin[1], 5.0 * 20.0, 12.0); // ~100 mm tall at 20 mm per unit
    std::vector<uint32_t> all;
    for (int i = 0; i < sf_object_count(r); ++i) all.push_back(sf_object_id_at(r, i));
    uint32_t merged = sf_op_merge(r, all.data(), (int)all.size());
    CHECK(merged != 0 && sf_object_count(r) == 1);
    t0 = tk::nowMs();
    CHECK_MSG(sf_op_remesh(r, merged, 0.03f, nullptr, nullptr) == 1, sf_last_error(r));
    double remeshMs = tk::nowMs() - t0;
    SFAnalysis pa;
    CHECK(sf_analyze(r, &merged, 1, 20.0f, 0.8f, &pa) == 1);
    CHECK_MSG(pa.watertight == 1 && pa.boundaryEdges == 0 && pa.nonManifoldEdges == 0,
              "print mesh must be watertight: boundary " + std::to_string(pa.boundaryEdges) + " nonmanifold " +
                  std::to_string(pa.nonManifoldEdges) + " inconsistent " + std::to_string(pa.inconsistentEdges));
    CHECK_MSG(pa.components == 1 && pa.invertedNormals == 0 && pa.volume > 0, "components " + std::to_string(pa.components));
    CHECK(sf_export(r, tk::path("cat_print.stl").c_str(), 0, &merged, 1, 20.0f) == 1);
    CHECK(sf_export(r, tk::path("cat_print.3mf").c_str(), 4, &merged, 1, 20.0f) == 1);
    tk::pass("print prep: merged + voxel-remeshed into one watertight solid (" + std::to_string(pa.triangles) + " tris, " +
             std::to_string((int)remeshMs) + " ms), " + std::to_string((int)(pa.boundsMax[1] - pa.boundsMin[1])) + " mm tall, volume " +
             std::to_string((int)(pa.volume / 1000.0)) + " cm3; exported STL + 3MF");
    // undo walks all the way back through merge/remesh
    CHECK(sf_undo(r) == 1 && sf_undo(r) == 1 && sf_object_count(r) == 14);
    CHECK_MSG(sameScenes(s, r, why), "after undoing print prep: " + why);
    tk::pass("[ACCEPT 15] complex multi-object character built, saved, reopened, exported; print-prep fully undoable");
    sf_scene_destroy(r);
    sf_scene_destroy(s);
}

// ---- API-level mesh operations: each must work, stay valid and undo exactly ---------------------
void runApiOperationTests() {
    tk::section("Mesh operations through the app API (run, validate, undo)");
    struct Op { const char* name; std::function<int(SFScene*, uint32_t)> run; };
    float planeP[3] = {1, 2.1f, 3}, planeN[3] = {0, 1, 0};
    float red[3] = {1, 0, 0};
    std::vector<Op> ops = {
        {"subdivide smooth", [](SFScene* s, uint32_t id) { return sf_op_subdivide(s, id, 1); }},
        {"subdivide linear", [](SFScene* s, uint32_t id) { return sf_op_subdivide(s, id, 0); }},
        {"decimate", [](SFScene* s, uint32_t id) { return sf_op_decimate(s, id, 2000); }},
        {"voxel remesh", [](SFScene* s, uint32_t id) { return sf_op_remesh(s, id, 0.08f, nullptr, nullptr); }},
        {"smooth", [](SFScene* s, uint32_t id) { return sf_op_smooth(s, id, 3, 0.5f); }},
        {"flip normals", [](SFScene* s, uint32_t id) { return sf_op_flip_normals(s, id); }},
        {"mirror X", [](SFScene* s, uint32_t id) { return sf_op_mirror(s, id, 0); }},
        {"plane cut + cap", [&](SFScene* s, uint32_t id) { return sf_op_cut_plane(s, id, planeP, planeN, 1); }},
        {"mask invert", [](SFScene* s, uint32_t id) { return sf_op_mask(s, id, 1, 0); }},
        {"mask blur", [](SFScene* s, uint32_t id) { return sf_op_mask(s, id, 2, 3); }},
        {"mask sharpen", [](SFScene* s, uint32_t id) { return sf_op_mask(s, id, 3, 2); }},
        {"mask grow", [](SFScene* s, uint32_t id) { return sf_op_mask(s, id, 4, 0); }},
        {"mask shrink", [](SFScene* s, uint32_t id) { return sf_op_mask(s, id, 5, 0); }},
        {"mask cavities", [](SFScene* s, uint32_t id) { return sf_op_mask(s, id, 6, 1); }},
        {"fill colour + roughness", [&](SFScene* s, uint32_t id) { return sf_op_fill(s, id, red, 0.2f, 0.9f, 7); }},
        {"apply transform", [](SFScene* s, uint32_t id) { return sf_op_apply_transform(s, id); }},
    };
    for (auto& op : ops) {
        SFScene* s = sf_scene_create();
        uint32_t id = prim(s, PrimitiveType::Icosphere, {1, 4}, "Test");
        setTransform(s, id, {1, 2, 3}, {1.5f, 1, 1});
        // a dent and a partial, soft-edged mask give the mask / cavity operations something to act on
        SFBrush dent = brush(BrushType::Draw, 0.3f, 1.0f);
        dent.invert = 1;
        strokeRays(s, id, dent, {{1, 2, 50}, {1.1f, 2, 50}}, {0, 0, -1});
        strokeRays(s, id, brush(BrushType::Mask, 0.4f, 0.5f), {{1, 2.4f, 50}, {1.1f, 2.4f, 50}}, {0, 0, -1});
        Mesh before = meshOf(s, id);
        SFObjectProps pb;
        sf_object_get_props(s, id, &pb);
        std::string name = op.name;
        int r = op.run(s, id);
        Mesh after = meshOf(s, id);
        std::string why;
        bool ok = CHECK_MSG(r == 1, name + ": " + sf_last_error(s));
        ok &= CHECK_MSG(after.validate(&why), name + " produced an invalid mesh: " + why);
        ok &= CHECK_MSG(!sameMesh(before, after), name + " changed nothing");
        bool geometryOp = name.find("mask") == std::string::npos && name.find("fill") == std::string::npos;
        if (geometryOp) ok &= CHECK_MSG(analyzeMesh(after).watertight, name + " should keep the mesh closed");
        ok &= CHECK_MSG(sf_undo(s) == 1, name + " undo");
        SFObjectProps pa;
        sf_object_get_props(s, id, &pa);
        ok &= CHECK_MSG(sameMesh(before, meshOf(s, id)) && std::memcmp(&pa, &pb, sizeof(pa)) == 0,
                        name + ": undo must restore mesh and transform exactly");
        ok &= CHECK_MSG(sf_redo(s) == 1 && sameMesh(after, meshOf(s, id)), name + " redo");
        if (ok) tk::pass(name + ": " + std::to_string(before.tcount()) + " -> " + std::to_string(after.tcount()) + " tris, undo/redo exact");
        sf_scene_destroy(s);
    }
    { // specific expectations
        SFScene* s = sf_scene_create();
        uint32_t id = prim(s, PrimitiveType::Icosphere, {1, 4}, "Test");
        setTransform(s, id, {1, 2, 3}, {2, 2, 2});
        CHECK(sf_op_apply_transform(s, id) == 1);
        SFObjectProps p;
        sf_object_get_props(s, id, &p);
        AABB b = meshOf(s, id).bounds();
        CHECK(p.position[0] == 0 && p.scale[0] == 1);
        CHECK_NEAR(b.center().x, 1.0, 1e-3);
        CHECK_NEAR(b.center().y, 2.0, 1e-3);
        CHECK_NEAR(b.size().x, 4.0, 1e-2);
        Mesh intact = meshOf(s, id);
        CHECK(sf_op_fill_holes(s, id) == 0 && std::string(sf_last_error(s)).find("no open boundaries") != std::string::npos);
        CHECK(sameMesh(intact, meshOf(s, id))); // a failed operation leaves the mesh untouched
        CHECK(sf_op_separate(s, id) == 0);      // single part: nothing to separate
        CHECK(sf_op_weld(s, id, 0.0005f) == 1); // nothing to merge is not an error
        uint32_t other = prim(s, PrimitiveType::Cube, {1, 1, 1, 4}, "Cube");
        setTransform(s, other, {2.9f, 2, 3}, {1, 1, 1});
        CHECK_MSG(sf_op_boolean(s, id, other, 1, 0.08f, nullptr, nullptr) == 1 && sf_object_count(s) == 1, sf_last_error(s));
        MeshAnalysis a = analyzeMesh(meshOf(s, id));
        CHECK(a.watertight && a.volume < 4.0 / 3.0 * 3.14159 * 8.0 - 0.3);
        CHECK(sf_undo(s) == 1 && sf_object_count(s) == 2);
        uint32_t ids[2] = {id, other};
        uint32_t merged = sf_op_merge(s, ids, 2);
        CHECK(merged != 0 && sf_object_count(s) == 1);
        CHECK(sf_undo(s) == 1 && sf_object_count(s) == 2);
        setTransform(s, other, {9, 2, 3}, {1, 1, 1}); // move apart so the merged mesh has two islands
        merged = sf_op_merge(s, ids, 2);
        CHECK(merged != 0 && sf_op_separate(s, merged) == 2 && sf_object_count(s) == 2);
        CHECK(sf_undo(s) == 1 && sf_object_count(s) == 1 && sf_undo(s) == 1 && sf_object_count(s) == 2);
        CHECK(sf_object_delete(s, id) == 1 && sf_object_count(s) == 1 && sf_undo(s) == 1 && sf_object_count(s) == 2);
        tk::pass("apply transform bakes placement; failed ops leave the mesh intact; boolean, merge, separate, delete all undo");
        sf_scene_destroy(s);
    }
    { // text-format importers
        std::string stl = "solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n"
                          "facet normal 0 0 1\nouter loop\nvertex 1 0 0\nvertex 1 1 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid t\n";
        writeWholeFile(tk::path("ascii.stl"), std::vector<uint8_t>(stl.begin(), stl.end()));
        std::string ply = "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
                          "property uchar red\nproperty uchar green\nproperty uchar blue\nelement face 1\n"
                          "property list uchar int vertex_indices\nend_header\n0 0 0 255 0 0\n1 0 0 255 0 0\n1 1 0 0 0 255\n0 1 0 0 0 255\n4 0 1 2 3\n";
        writeWholeFile(tk::path("ascii.ply"), std::vector<uint8_t>(ply.begin(), ply.end()));
        std::string obj = "o quad\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf -4//1 -3//1 -2//1 -1//1\n";
        writeWholeFile(tk::path("quad.obj"), std::vector<uint8_t>(obj.begin(), obj.end()));
        for (const char* f : {"ascii.stl", "ascii.ply", "quad.obj"}) {
            SFScene* s = sf_scene_create();
            CHECK_MSG(sf_import(s, tk::path(f).c_str()) == 1, std::string(f) + ": " + sf_last_error(s));
            Mesh m = meshOf(s, sf_object_id_at(s, 0));
            CHECK_MSG(m.tcount() == 2 && m.vcount() == 4, std::string(f) + " should be a welded quad (2 triangles, 4 vertices)");
            CHECK_NEAR(analyzeMesh(m).surfaceArea, 1.0, 1e-5);
            if (std::string(f) == "ascii.ply") CHECK(m.col[0].x > 0.9f && m.col[0].z < 0.1f && m.col[2].z > 0.9f);
            sf_scene_destroy(s);
        }
        tk::pass("ASCII STL, ASCII PLY (with colours) and polygon OBJ with negative indices import correctly");
    }
}

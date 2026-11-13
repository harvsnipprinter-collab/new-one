// Masked transform regression tests: make sure protected vertices cannot move.
#include "TestKit.h"
#include <cstring>
#include <cmath>

void runMaskedTransformTests() {
    tk::section("Mask-aware gizmo transforms (Move, Rotate, Scale)");
    SFScene* s = sf_scene_create();
    float p[] = {1, 3};
    uint32_t id = sf_add_primitive(s, 1, p, 2);
    CHECK(id != 0);
    SFBrush b; sf_brush_default(&b);
    b.type = 18; b.radius = 0.7f; b.strength = 1.0f;
    float origin[] = {0, 0, 3}, dir[] = {0, 0, -1};
    // Several dabs saturate the centre of the protected mask to exactly 1.
    for (int pass = 0; pass < 5; ++pass) {
        CHECK(sf_stroke_begin(s, id, &b, origin, dir, 1.0f));
        CHECK(sf_stroke_end(s) >= 0);
    }
    SFMeshView view{}; CHECK(sf_object_mesh(s, id, &view));
    size_t count = view.vertexCount;
    std::vector<float> initial(view.positions, view.positions + count * 3);
    std::vector<float> masks(view.mask, view.mask + count);
    int frozen = 0, free = 0;
    for (float m : masks) { if (m >= 1.0f - 1e-6f) ++frozen; if (m <= 1e-6f) ++free; }
    CHECK_MSG(frozen > 0 && free > 0, "partial sculpt mask includes fully protected and free vertices");
    float pivot[3]{};
    CHECK(sf_mask_edit_pivot(s, id, pivot));
    float trans[3] = {0.3f, 0.25f, 0.2f}, axis[3] = {0, 1, 0}, scale[3] = {1, 1, 1};
    CHECK(sf_masked_transform_begin(s, id, pivot));
    CHECK(sf_masked_transform_update(s, trans, axis, 0, scale));
    CHECK(sf_masked_transform_end(s, 1));
    CHECK(sf_object_mesh(s, id, &view));
    int moved = 0;
    for (size_t i = 0; i < count; ++i) {
        bool changed = false;
        for (int j = 0; j < 3; ++j) {
            float before = initial[i*3+j], after = view.positions[i*3+j];
            if (masks[i] >= 1-1e-6f) CHECK_MSG(before == after, "masked vertex moved during translation");
            if (std::abs(before-after) > 1e-5f) changed = true;
        }
        if (changed) ++moved;
    }
    CHECK(moved > 0);
    CHECK(sf_undo(s));
    CHECK(sf_object_mesh(s, id, &view));
    for (size_t i = 0; i < count*3; ++i) CHECK_MSG(view.positions[i] == initial[i], "masked translate did not undo exactly");
    CHECK(sf_redo(s));
    CHECK(sf_object_mesh(s, id, &view));
    // Return to original using undo before starting additional operations.
    CHECK(sf_undo(s));
    float zero[3] = {0,0,0};
    CHECK(sf_masked_transform_begin(s, id, pivot));
    CHECK(sf_masked_transform_update(s, zero, axis, 0.45f, scale));
    CHECK(sf_masked_transform_end(s, 1));
    CHECK(sf_object_mesh(s, id, &view));
    for (size_t i = 0; i < count; ++i)
        if (masks[i] >= 1-1e-6f) for (int j=0;j<3;++j)
            CHECK_MSG(view.positions[i*3+j] == initial[i*3+j], "protected vertex rotated");
    CHECK(sf_undo(s));
    float bigger[3] = {1.3f, 1.0f, 1.1f};
    CHECK(sf_masked_transform_begin(s, id, pivot));
    CHECK(sf_masked_transform_update(s, zero, axis, 0, bigger));
    CHECK(sf_masked_transform_end(s, 1));
    CHECK(sf_object_mesh(s, id, &view));
    for (size_t i = 0; i < count; ++i)
        if (masks[i] >= 1-1e-6f) for (int j=0;j<3;++j)
            CHECK_MSG(view.positions[i*3+j] == initial[i*3+j], "protected vertex scaled");
    tk::pass("Move, Rotate and Scale respect protected vertices; undo restores original positions");
    sf_scene_destroy(s);
}

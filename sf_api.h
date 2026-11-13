// SculptForge 3D - C interface to the engine (imported directly by Swift through the bridging header)
//
// Conventions: functions returning int return 1 on success and 0 on failure unless stated otherwise;
// after a failure sf_last_error() describes the problem. Object ids are never 0.
#ifndef SF_API_H
#define SF_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SFScene SFScene;

typedef struct {
    float position[3];
    float rotation[4]; // quaternion x, y, z, w
    float scale[3];
    uint32_t parent;   // 0 = none
    int32_t visible, locked;
    float baseColor[3];
    float roughness, metallic, opacity, emission;
    int32_t usePainted;
} SFObjectProps;

typedef struct {
    const float* positions;  // 3 floats per vertex
    const float* normals;    // 3 floats per vertex
    const float* colors;     // 3 floats per vertex (linear RGB)
    const float* roughness;  // 1 float per vertex
    const float* metalness;  // 1 float per vertex
    const float* mask;       // 1 float per vertex
    const uint32_t* indices; // 3 per triangle
    const uint32_t* faceGroups;
    uint32_t vertexCount, triangleCount;
    uint64_t topologyVersion; // changes whenever buffers must be fully re-uploaded
} SFMeshView;

typedef struct {
    int32_t type;      // sf::BrushType
    float radius;      // world units
    float strength;
    int32_t falloff;   // sf::Falloff
    float spacing, smoothing;
    int32_t invert, frontFacesOnly, pressureStrength, pressureRadius;
    int32_t symX, symY, symZ, radialCount, radialAxis;
    float color[3];
    float paintRoughness, paintMetalness;
    int32_t paintColor, paintRough, paintMetal;
    float layerHeight;
    uint32_t faceGroup;
    int32_t dyntopo, dyntopoMode;
    float dyntopoDetail; // world units
} SFBrush;

typedef struct {
    uint64_t vertices, triangles, boundaryEdges, nonManifoldEdges, inconsistentEdges, degenerateTriangles,
        looseVertices, holes, thinSamples, thicknessSamples;
    int32_t components, watertight, invertedNormals;
    double volume, surfaceArea; // in output units (after unitScale)
    float boundsMin[3], boundsMax[3];
    float minThickness;         // -1 when not measured
} SFAnalysis;

typedef int (*SFProgress)(float fraction, void* user); // return 0 to cancel

// ---- lifecycle -----------------------------------------------------------------------------
SFScene* sf_scene_create(void);
void sf_scene_destroy(SFScene* s);
void sf_scene_clear(SFScene* s); // new empty project (clears history)
const char* sf_last_error(SFScene* s);
uint64_t sf_scene_memory_bytes(SFScene* s);
uint64_t sf_scene_triangle_count(SFScene* s);

// ---- objects -------------------------------------------------------------------------------
int32_t sf_object_count(SFScene* s);
uint32_t sf_object_id_at(SFScene* s, int32_t index);
uint32_t sf_add_primitive(SFScene* s, int32_t type, const float* params, int32_t paramCount);
const char* sf_primitive_name(int32_t type);
int32_t sf_primitive_count(void);
int sf_object_delete(SFScene* s, uint32_t id);
uint32_t sf_object_duplicate(SFScene* s, uint32_t id);
const char* sf_object_name(SFScene* s, uint32_t id);
int sf_object_set_name(SFScene* s, uint32_t id, const char* name);
int sf_object_get_props(SFScene* s, uint32_t id, SFObjectProps* out);
// Sets properties immediately. Wrap interactive edits in begin/end so they become one undo step.
int sf_object_set_props(SFScene* s, uint32_t id, const SFObjectProps* props);
// Re-parent while keeping the object where it is in the world (its local transform is recomputed). Undoable.
int sf_object_set_parent(SFScene* s, uint32_t id, uint32_t parent);
void sf_object_begin_edit(SFScene* s, uint32_t id);
void sf_object_end_edit(SFScene* s, uint32_t id);
int sf_object_world_matrix(SFScene* s, uint32_t id, float* out16);
int sf_object_move_index(SFScene* s, uint32_t id, int32_t newIndex);
int sf_object_mesh(SFScene* s, uint32_t id, SFMeshView* out);
// Nearest visible object hit by a world-space ray. Returns 1 on hit.
int sf_pick(SFScene* s, const float* origin, const float* dir, uint32_t* outId, float* outPoint, float* outNormal);

// ---- mask-aware interactive mesh transforms ------------------------------------------------
// A partial mask permits per-vertex deformation. Fully grey vertices stay fixed.
// When no partial mask exists, use the normal object property transform API instead.
// Returns 1 when there are both protected and editable vertices, 0 otherwise.
int sf_mask_edit_pivot(SFScene* s, uint32_t id, float* outWorld3);
int sf_masked_transform_begin(SFScene* s, uint32_t id, float* outWorldPivot3);
// All input values are expressed in world space, relative to the start of the drag.
// Each update recomputes from the original mesh; no cumulative floating-point drift.
int sf_masked_transform_update(SFScene* s, const float* translation3,
    const float* rotationAxis3, float angleRadians, const float* scale3);
// commit=1 records one undo item; commit=0 restores the starting mesh.
int sf_masked_transform_end(SFScene* s, int commit);

// ---- sculpt / paint strokes ----------------------------------------------------------------
void sf_brush_default(SFBrush* b);
const char* sf_brush_name(int32_t type);
int32_t sf_brush_count(void);
int sf_stroke_begin(SFScene* s, uint32_t id, const SFBrush* brush, const float* origin, const float* dir, float pressure);
void sf_stroke_move(SFScene* s, const float* origin, const float* dir, float pressure);
int sf_stroke_end(SFScene* s); // 1 if the stroke changed anything (and was recorded for undo)
int sf_stroke_active(SFScene* s);
// Vertex range changed since the last call (inclusive). Returns 0 if nothing is dirty.
int sf_stroke_take_dirty(SFScene* s, uint32_t* outMin, uint32_t* outMax);

// ---- history -------------------------------------------------------------------------------
int sf_undo(SFScene* s);
int sf_redo(SFScene* s);
int sf_can_undo(SFScene* s);
int sf_can_redo(SFScene* s);
void sf_history_set_limits(SFScene* s, uint32_t maxSteps, uint64_t maxBytes);
void sf_history_clear(SFScene* s);

// ---- mesh operations (all undoable) --------------------------------------------------------
int sf_op_subdivide(SFScene* s, uint32_t id, int smooth);
int sf_op_decimate(SFScene* s, uint32_t id, uint32_t targetTriangles);
int sf_op_remesh(SFScene* s, uint32_t id, float voxelSizeWorld, SFProgress progress, void* user);
uint64_t sf_estimate_remesh_triangles(SFScene* s, uint32_t id, float voxelSizeWorld);
int sf_op_smooth(SFScene* s, uint32_t id, int32_t iterations, float factor);
int sf_op_flip_normals(SFScene* s, uint32_t id);
int sf_op_fill_holes(SFScene* s, uint32_t id);
int sf_op_weld(SFScene* s, uint32_t id, float epsilonWorld);
int sf_op_mirror(SFScene* s, uint32_t id, int32_t axis);
int sf_op_cut_plane(SFScene* s, uint32_t id, const float* worldPoint, const float* worldNormal, int cap);
// maskOp: 0 clear, 1 invert, 2 blur, 3 sharpen, 4 grow, 5 shrink, 6 cavity, 7 fill (mask everything)
int sf_op_mask(SFScene* s, uint32_t id, int32_t maskOp, float amount);
// Fill unmasked vertices. channels bit 0 colour, bit 1 roughness, bit 2 metalness.
int sf_op_fill(SFScene* s, uint32_t id, const float* rgb, float roughness, float metalness, int32_t channels);
int sf_op_apply_transform(SFScene* s, uint32_t id);
uint32_t sf_op_extract_mask(SFScene* s, uint32_t id, float thicknessWorld); // new object id or 0
int32_t sf_op_separate(SFScene* s, uint32_t id);                           // number of resulting objects, 0 on failure
uint32_t sf_op_merge(SFScene* s, const uint32_t* ids, int32_t count);      // new object id or 0
// op: 0 union, 1 difference (a - b), 2 intersection. Result replaces a; b is removed.
int sf_op_boolean(SFScene* s, uint32_t a, uint32_t b, int32_t op, float voxelSizeWorld, SFProgress progress, void* user);

// ---- files ---------------------------------------------------------------------------------
int sf_save_project(SFScene* s, const char* path);
int sf_load_project(SFScene* s, const char* path); // on failure the current scene is left untouched
void sf_set_app_state(SFScene* s, const char* json);
const char* sf_get_app_state(SFScene* s);
// format: 0 STL, 1 OBJ, 2 PLY, 3 GLB, 4 3MF. ids == NULL exports every visible object.
int sf_export(SFScene* s, const char* path, int32_t format, const uint32_t* ids, int32_t count, float scale);
int32_t sf_import(SFScene* s, const char* path); // number of objects added, 0 on failure

// ---- 3D printing analysis ------------------------------------------------------------------
// ids == NULL analyses every visible object as one model. unitScale converts scene units to mm.
int sf_analyze(SFScene* s, const uint32_t* ids, int32_t count, float unitScale, float thinThresholdMm, SFAnalysis* out);

#ifdef __cplusplus
}
#endif
#endif

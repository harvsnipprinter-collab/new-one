#include "sf_api.h"
#include "sf/IO.h"
#include "sf/MeshOps.h"
#include "sf/Primitives.h"
#include "sf/Scene.h"
#include "sf/Topology.h"
#include <cstring>
#include <new>

using namespace sf;

struct SFScene {
    Scene scene;
    History history;
    std::string error;
    // active stroke
    std::unique_ptr<SculptStroke> stroke;
    uint32_t strokeObject = 0;
    Mat4 strokeInverse;
    std::shared_ptr<Mesh> strokeSnapshot;
    bool strokeSeeded = false;      // this stroke switched the object to painted material channels
    ObjectState strokePropsBefore;
    // interactive property edit
    uint32_t editObject = 0;
    ObjectState editBefore;
    uint32_t maskEditObject = 0;
    std::shared_ptr<Mesh> maskEditBefore;
    Mat4 maskEditWorld, maskEditWorldInverse;
    Vec3 maskEditPivot;
    bool maskEditChanged = false;
};

namespace {

Vec3 v3(const float* p) { return {p[0], p[1], p[2]}; }

int fail(SFScene* s, const std::string& msg) {
    if (s) s->error = msg;
    return 0;
}

Object* object(SFScene* s, uint32_t id) {
    if (!s) return nullptr;
    Object* o = s->scene.find(id);
    if (!o) s->error = "object not found";
    return o;
}

float uniformScale(const Mat4& m) {
    float d = std::fabs(m.det3());
    return d > 0 ? std::cbrt(d) : 1.0f;
}

Command addCommand(const Scene& sc, const Object& o) {
    Command c;
    c.kind = Command::ObjectAdd;
    c.objectId = o.st.id;
    c.after = o.st;
    c.meshAfter = std::make_shared<Mesh>(o.mesh);
    c.index = sc.indexOf(o.st.id);
    return c;
}

// Removes an object, returning commands that undo it (children re-parenting included).
void removeWithHistory(Scene& sc, uint32_t id, std::vector<Command>& out) {
    Object* o = sc.find(id);
    if (!o) return;
    for (auto& ch : sc.objects) {
        if (ch->st.parent != id) continue;
        Command p;
        p.kind = Command::Props;
        p.objectId = ch->st.id;
        p.before = ch->st;
        ch->st.parent = o->st.parent;
        p.after = ch->st;
        out.push_back(std::move(p));
    }
    Command c;
    c.kind = Command::ObjectRemove;
    c.objectId = id;
    c.before = o->st;
    c.meshBefore = std::make_shared<Mesh>(std::move(o->mesh));
    c.index = sc.indexOf(id);
    out.push_back(std::move(c));
    sc.remove(id);
}

// Run a mesh-modifying operation with validation, rollback and an undo record.
template <class F>
int meshOp(SFScene* s, uint32_t id, F&& fn) {
    Object* o = object(s, id);
    if (!o) return 0;
    if (s->stroke) return fail(s, "finish the current stroke first");
    if (o->st.locked) return fail(s, "object is locked");
    std::shared_ptr<Mesh> before;
    try {
        before = std::make_shared<Mesh>(o->mesh);
        std::string err;
        bool ok = fn(o->mesh, err);
        std::string why;
        if (ok && o->mesh.tcount() == 0) { ok = false; err = "operation removed all geometry"; }
        if (ok) { o->mesh.ensureAttributes(); if (!o->mesh.validate(&why)) { ok = false; err = "operation produced invalid geometry: " + why; } }
        if (!ok) {
            o->mesh = *before;
            o->meshChanged();
            return fail(s, err.empty() ? "operation failed" : err);
        }
        o->meshChanged();
        Command c;
        c.kind = Command::MeshReplace;
        c.objectId = id;
        c.meshBefore = before;
        c.meshAfter = std::make_shared<Mesh>(o->mesh);
        s->history.push(std::move(c));
        return 1;
    } catch (const std::bad_alloc&) {
        if (before) { o->mesh = *before; o->meshChanged(); }
        return fail(s, "not enough memory for this operation");
    } catch (const std::exception& e) {
        if (before) { o->mesh = *before; o->meshChanged(); }
        return fail(s, std::string("operation failed: ") + e.what());
    }
}

void toProps(const ObjectState& st, SFObjectProps* p) {
    p->position[0] = st.position.x; p->position[1] = st.position.y; p->position[2] = st.position.z;
    p->rotation[0] = st.rotation.x; p->rotation[1] = st.rotation.y; p->rotation[2] = st.rotation.z; p->rotation[3] = st.rotation.w;
    p->scale[0] = st.scale.x; p->scale[1] = st.scale.y; p->scale[2] = st.scale.z;
    p->parent = st.parent; p->visible = st.visible; p->locked = st.locked;
    p->baseColor[0] = st.mat.baseColor.x; p->baseColor[1] = st.mat.baseColor.y; p->baseColor[2] = st.mat.baseColor.z;
    p->roughness = st.mat.roughness; p->metallic = st.mat.metallic; p->opacity = st.mat.opacity;
    p->emission = st.mat.emission; p->usePainted = st.mat.usePainted;
}

std::vector<Object*> selection(SFScene* s, const uint32_t* ids, int32_t count) {
    std::vector<Object*> r;
    if (!ids || count <= 0) {
        for (auto& o : s->scene.objects) if (o->st.visible) r.push_back(o.get());
    } else {
        for (int32_t i = 0; i < count; ++i) if (Object* o = s->scene.find(ids[i])) r.push_back(o);
    }
    return r;
}

} // namespace

extern "C" {

SFScene* sf_scene_create(void) { return new (std::nothrow) SFScene(); }
void sf_scene_destroy(SFScene* s) { delete s; }
void sf_scene_clear(SFScene* s) {
    if (!s) return;
    s->stroke.reset();
    s->maskEditObject = 0;
    s->maskEditBefore.reset();
    s->scene = Scene();
    s->history.clear();
}
const char* sf_last_error(SFScene* s) { return s ? s->error.c_str() : "no scene"; }
uint64_t sf_scene_memory_bytes(SFScene* s) { return s ? s->scene.memoryBytes() + s->history.bytes() : 0; }
uint64_t sf_scene_triangle_count(SFScene* s) { return s ? s->scene.totalTriangles() : 0; }

int32_t sf_object_count(SFScene* s) { return s ? (int32_t)s->scene.objects.size() : 0; }
uint32_t sf_object_id_at(SFScene* s, int32_t i) {
    return (s && i >= 0 && i < (int32_t)s->scene.objects.size()) ? s->scene.objects[i]->st.id : 0;
}

uint32_t sf_add_primitive(SFScene* s, int32_t type, const float* params, int32_t count) {
    if (!s) return 0;
    try {
        if (type < 0 || type >= (int32_t)PrimitiveType::Count) { fail(s, "unknown primitive"); return 0; }
        Mesh m = makePrimitive((PrimitiveType)type, params, count);
        std::string why;
        if (m.tcount() == 0 || !m.validate(&why)) { fail(s, "primitive parameters produce no geometry"); return 0; }
        Object* o = s->scene.add(std::move(m), primitiveName((PrimitiveType)type));
        s->history.push(addCommand(s->scene, *o));
        return o->st.id;
    } catch (const std::exception&) { fail(s, "not enough memory"); return 0; }
}
const char* sf_primitive_name(int32_t type) { return primitiveName((PrimitiveType)type); }
int32_t sf_primitive_count(void) { return (int32_t)PrimitiveType::Count; }

int sf_object_delete(SFScene* s, uint32_t id) {
    if (!object(s, id)) return 0;
    if (s->stroke) return fail(s, "finish the current stroke first");
    Command g;
    g.kind = Command::Group;
    removeWithHistory(s->scene, id, g.children);
    s->history.push(std::move(g));
    return 1;
}

uint32_t sf_object_duplicate(SFScene* s, uint32_t id) {
    Object* o = object(s, id);
    if (!o) return 0;
    try {
        ObjectState st = o->st;
        st.id = s->scene.nextId++;
        st.name += " copy";
        Object* n = s->scene.insert(st, o->mesh, s->scene.indexOf(id) + 1);
        s->history.push(addCommand(s->scene, *n));
        return n->st.id;
    } catch (const std::exception&) { fail(s, "not enough memory"); return 0; }
}

const char* sf_object_name(SFScene* s, uint32_t id) {
    Object* o = s ? s->scene.find(id) : nullptr;
    return o ? o->st.name.c_str() : "";
}

int sf_object_set_name(SFScene* s, uint32_t id, const char* name) {
    Object* o = object(s, id);
    if (!o || !name) return 0;
    Command c;
    c.kind = Command::Props; c.objectId = id; c.before = o->st;
    o->st.name = name;
    c.after = o->st;
    s->history.push(std::move(c));
    return 1;
}

int sf_object_get_props(SFScene* s, uint32_t id, SFObjectProps* out) {
    Object* o = object(s, id);
    if (!o || !out) return 0;
    toProps(o->st, out);
    return 1;
}

int sf_object_set_props(SFScene* s, uint32_t id, const SFObjectProps* p) {
    Object* o = object(s, id);
    if (!o || !p) return 0;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(p->position[i]) || !std::isfinite(p->scale[i])) return fail(s, "invalid transform");
    Quat q{p->rotation[0], p->rotation[1], p->rotation[2], p->rotation[3]};
    float ql = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(ql > 1e-6f) || !std::isfinite(ql)) return fail(s, "invalid rotation");
    if (p->parent != 0 && (p->parent == id || !s->scene.find(p->parent) || s->scene.wouldCycle(id, p->parent)))
        return fail(s, "invalid parent");
    ObjectState& st = o->st;
    st.position = v3(p->position);
    st.rotation = {q.x / ql, q.y / ql, q.z / ql, q.w / ql};
    st.scale = v3(p->scale);
    for (int i = 0; i < 3; ++i) if (std::fabs(st.scale[i]) < 1e-6f) st.scale[i] = 1e-6f;
    st.parent = p->parent;
    st.visible = p->visible != 0; st.locked = p->locked != 0;
    st.mat.baseColor = v3(p->baseColor);
    st.mat.roughness = clamp01(p->roughness); st.mat.metallic = clamp01(p->metallic);
    st.mat.opacity = clamp01(p->opacity); st.mat.emission = std::max(0.0f, p->emission);
    st.mat.usePainted = p->usePainted != 0;
    return 1;
}

// Split an affine matrix into translation, rotation and (possibly mirrored) scale. Shear is dropped.
static void decompose(const Mat4& m, Vec3& t, Quat& q, Vec3& sc) {
    t = {m.m[12], m.m[13], m.m[14]};
    Vec3 c0{m.m[0], m.m[1], m.m[2]}, c1{m.m[4], m.m[5], m.m[6]}, c2{m.m[8], m.m[9], m.m[10]};
    sc = {length(c0), length(c1), length(c2)};
    for (int i = 0; i < 3; ++i) if (!(sc[i] > 1e-9f)) sc[i] = 1e-9f;
    if (m.det3() < 0) { sc.x = -sc.x; }
    c0 = normalize(c0 / sc.x);
    c1 = normalize(c1 - c0 * dot(c1, c0));
    c2 = cross(c0, c1);
    float r00 = c0.x, r10 = c0.y, r20 = c0.z, r01 = c1.x, r11 = c1.y, r21 = c1.z, r02 = c2.x, r12 = c2.y, r22 = c2.z;
    float tr = r00 + r11 + r22;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2;
        q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25f * s};
    } else if (r00 > r11 && r00 > r22) {
        float s = std::sqrt(1.0f + r00 - r11 - r22) * 2;
        q = {0.25f * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
        float s = std::sqrt(1.0f + r11 - r00 - r22) * 2;
        q = {(r01 + r10) / s, 0.25f * s, (r12 + r21) / s, (r02 - r20) / s};
    } else {
        float s = std::sqrt(1.0f + r22 - r00 - r11) * 2;
        q = {(r02 + r20) / s, (r12 + r21) / s, 0.25f * s, (r10 - r01) / s};
    }
}

int sf_object_set_parent(SFScene* s, uint32_t id, uint32_t parent) {
    Object* o = object(s, id);
    if (!o) return 0;
    if (parent != 0 && (parent == id || !s->scene.find(parent) || s->scene.wouldCycle(id, parent))) return fail(s, "invalid parent");
    Mat4 world = s->scene.worldMatrix(id);
    Mat4 local = parent ? s->scene.worldMatrix(parent).affineInverse() * world : world;
    Command c;
    c.kind = Command::Props; c.objectId = id; c.before = o->st;
    decompose(local, o->st.position, o->st.rotation, o->st.scale);
    o->st.parent = parent;
    c.after = o->st;
    s->history.push(std::move(c));
    return 1;
}

void sf_object_begin_edit(SFScene* s, uint32_t id) {
    Object* o = s ? s->scene.find(id) : nullptr;
    if (!o) return;
    s->editObject = id;
    s->editBefore = o->st;
}

void sf_object_end_edit(SFScene* s, uint32_t id) {
    Object* o = s ? s->scene.find(id) : nullptr;
    if (!o || s->editObject != id) return;
    s->editObject = 0;
    Command c;
    c.kind = Command::Props; c.objectId = id; c.before = s->editBefore; c.after = o->st;
    s->history.push(std::move(c));
}

int sf_object_world_matrix(SFScene* s, uint32_t id, float* out16) {
    if (!object(s, id) || !out16) return 0;
    Mat4 m = s->scene.worldMatrix(id);
    std::memcpy(out16, m.m, sizeof(float) * 16);
    return 1;
}

int sf_object_move_index(SFScene* s, uint32_t id, int32_t newIndex) {
    if (!object(s, id)) return 0;
    int i = s->scene.indexOf(id);
    auto& v = s->scene.objects;
    newIndex = std::clamp(newIndex, 0, (int32_t)v.size() - 1);
    auto o = std::move(v[i]);
    v.erase(v.begin() + i);
    v.insert(v.begin() + newIndex, std::move(o));
    return 1;
}

int sf_object_mesh(SFScene* s, uint32_t id, SFMeshView* out) {
    Object* o = object(s, id);
    if (!o || !out) return 0;
    const Mesh& m = o->mesh;
    out->positions = m.pos.empty() ? nullptr : &m.pos[0].x;
    out->normals = m.nrm.empty() ? nullptr : &m.nrm[0].x;
    out->colors = m.col.empty() ? nullptr : &m.col[0].x;
    out->roughness = m.rough.data(); out->metalness = m.metal.data(); out->mask = m.mask.data();
    out->indices = m.tri.data(); out->faceGroups = m.faceGroup.data();
    out->vertexCount = (uint32_t)m.vcount(); out->triangleCount = (uint32_t)m.tcount();
    out->topologyVersion = o->topologyVersion;
    return 1;
}

int sf_pick(SFScene* s, const float* origin, const float* dir, uint32_t* outId, float* outPoint, float* outNormal) {
    if (!s || !origin || !dir) return 0;
    Vec3 o = v3(origin), d = normalize(v3(dir));
    float best = 1e30f;
    uint32_t bestId = 0;
    Vec3 bp, bn;
    for (auto& obj : s->scene.objects) {
        if (!obj->st.visible || obj->mesh.tcount() == 0) continue;
        Mat4 w = s->scene.worldMatrix(obj->st.id), inv = w.affineInverse();
        Vec3 lo = inv.point(o), ld = inv.dir(d);
        float len = length(ld);
        if (!(len > 0)) continue;
        RayHit h = obj->bvh.raycast(obj->mesh, lo, ld / len);
        if (!h.hit) continue;
        Vec3 wp = w.point(h.point);
        float t = dot(wp - o, d);
        if (t < best) {
            best = t; bestId = obj->st.id; bp = wp;
            bn = normalize(w.dir(h.normal)); // adequate for uniform scale; used for cursor display only
        }
    }
    if (!bestId) return 0;
    if (outId) *outId = bestId;
    if (outPoint) { outPoint[0] = bp.x; outPoint[1] = bp.y; outPoint[2] = bp.z; }
    if (outNormal) { outNormal[0] = bn.x; outNormal[1] = bn.y; outNormal[2] = bn.z; }
    return 1;
}

// ---- mask-aware interactive transforms -------------------------------------------------------
int sf_mask_edit_pivot(SFScene* s, uint32_t id, float* outWorld3) {
    Object* o = object(s, id);
    if (!o || o->mesh.pos.empty()) return 0;
    float lo = 1.0f, hi = 0.0f, weights = 0.0f;
    Vec3 average(0);
    for (size_t i = 0; i < o->mesh.vcount(); ++i) {
        float mask = clamp01(o->mesh.mask[i]);
        lo = std::min(lo, mask); hi = std::max(hi, mask);
        float w = 1.0f - mask;
        average += o->mesh.pos[i] * w;
        weights += w;
    }
    if (hi < 0.001f || lo > 0.999f || weights < 1e-6f) return 0;
    Vec3 pivot = s->scene.worldMatrix(id).point(average / weights);
    if (outWorld3) { outWorld3[0] = pivot.x; outWorld3[1] = pivot.y; outWorld3[2] = pivot.z; }
    return 1;
}

int sf_masked_transform_begin(SFScene* s, uint32_t id, float* outWorldPivot3) {
    Object* o = object(s, id);
    if (!o || o->st.locked || s->stroke || s->maskEditObject) return 0;
    float xyz[3];
    if (!sf_mask_edit_pivot(s, id, xyz)) return 0;
    try { s->maskEditBefore = std::make_shared<Mesh>(o->mesh); }
    catch (const std::bad_alloc&) { return fail(s, "not enough memory for masked transform"); }
    s->maskEditWorld = s->scene.worldMatrix(id);
    s->maskEditWorldInverse = s->maskEditWorld.affineInverse();
    s->maskEditPivot = v3(xyz);
    s->maskEditObject = id;
    s->maskEditChanged = false;
    if (outWorldPivot3) std::memcpy(outWorldPivot3, xyz, sizeof(xyz));
    return 1;
}

int sf_masked_transform_update(SFScene* s, const float* trans3, const float* axis3,
                               float radians, const float* scale3) {
    if (!s || !s->maskEditObject || !s->maskEditBefore || !trans3 || !axis3 || !scale3) return 0;
    Object* o = object(s, s->maskEditObject);
    if (!o) return 0;
    Vec3 trans = v3(trans3), axis = v3(axis3), scale = v3(scale3);
    if (!isFinite(trans) || !isFinite(axis) || !isFinite(scale) || !std::isfinite(radians)
        || scale.x <= 0 || scale.y <= 0 || scale.z <= 0) return fail(s, "invalid masked transform");
    Quat rotation = Quat::axisAngle(length2(axis) > 1e-12f ? axis : Vec3(0, 1, 0), radians);
    const Mesh& before = *s->maskEditBefore;
    for (size_t i = 0; i < before.vcount(); ++i) {
        const float w = 1.0f - clamp01(before.mask[i]);
        if (w <= 0) { o->mesh.pos[i] = before.pos[i]; continue; }
        Vec3 world = s->maskEditWorld.point(before.pos[i]);
        Vec3 rel = world - s->maskEditPivot;
        Vec3 destWorld = s->maskEditPivot + rotation.rotate(rel * scale) + trans;
        // Blend complete target position, not the rotation angle: soft masks deform continuously.
        Vec3 destLocal = s->maskEditWorldInverse.point(lerp(world, destWorld, w));
        if (!isFinite(destLocal)) return fail(s, "invalid deformed vertex");
        o->mesh.pos[i] = destLocal;
    }
    o->mesh.computeNormals();
    o->bvh.refit(o->mesh);
    s->maskEditChanged = true;
    return 1;
}

int sf_masked_transform_end(SFScene* s, int commit) {
    if (!s || !s->maskEditObject || !s->maskEditBefore) return 0;
    Object* o = s->scene.find(s->maskEditObject);
    if (!o) { s->maskEditObject = 0; s->maskEditBefore.reset(); return 0; }
    if (!commit || !s->maskEditChanged) {
        o->mesh = *s->maskEditBefore;
        o->mesh.computeNormals();
        o->bvh.refit(o->mesh);
    } else {
        Command c;
        c.kind = Command::MeshReplace;
        c.objectId = s->maskEditObject;
        c.meshBefore = std::move(s->maskEditBefore);
        c.meshAfter = std::make_shared<Mesh>(o->mesh);
        s->history.push(std::move(c));
    }
    s->maskEditObject = 0;
    s->maskEditBefore.reset();
    s->maskEditChanged = false;
    return 1;
}

// ---- strokes -------------------------------------------------------------------------------
void sf_brush_default(SFBrush* b) {
    if (!b) return;
    BrushSettings d;
    std::memset(b, 0, sizeof(*b));
    b->type = (int32_t)d.type; b->radius = d.radius; b->strength = d.strength; b->falloff = (int32_t)d.falloff;
    b->spacing = d.spacing; b->smoothing = d.smoothing; b->frontFacesOnly = 1; b->pressureStrength = 1;
    b->radialCount = 1; b->radialAxis = 1;
    b->color[0] = d.color.x; b->color[1] = d.color.y; b->color[2] = d.color.z;
    b->paintRoughness = d.paintRoughness; b->paintMetalness = d.paintMetalness; b->paintColor = 1;
    b->layerHeight = d.layerHeight; b->faceGroup = 1; b->dyntopoDetail = d.dyntopoDetail;
}
const char* sf_brush_name(int32_t type) { return brushName((BrushType)type); }
int32_t sf_brush_count(void) { return (int32_t)BrushType::Count; }

int sf_stroke_begin(SFScene* s, uint32_t id, const SFBrush* b, const float* origin, const float* dir, float pressure) {
    Object* o = object(s, id);
    if (!o || !b || !origin || !dir) return 0;
    if (s->stroke) sf_stroke_end(s);
    if (o->st.locked) return fail(s, "object is locked");
    if (!o->st.visible) return fail(s, "object is hidden");
    if (b->type < 0 || b->type >= (int32_t)BrushType::Count) return fail(s, "unknown brush");
    try {
        Mat4 w = s->scene.worldMatrix(id);
        float us = uniformScale(w);
        BrushSettings bs;
        bs.type = (BrushType)b->type;
        bs.radius = b->radius / us;
        bs.strength = b->strength;
        bs.falloff = (Falloff)std::clamp(b->falloff, 0, 4);
        bs.spacing = b->spacing; bs.smoothing = b->smoothing;
        bs.invert = b->invert != 0; bs.frontFacesOnly = b->frontFacesOnly != 0;
        bs.pressureStrength = b->pressureStrength != 0; bs.pressureRadius = b->pressureRadius != 0;
        bs.symX = b->symX != 0; bs.symY = b->symY != 0; bs.symZ = b->symZ != 0;
        bs.radialCount = b->radialCount; bs.radialAxis = b->radialAxis;
        bs.color = v3(b->color);
        bs.paintRoughness = b->paintRoughness; bs.paintMetalness = b->paintMetalness;
        bs.paintColor = b->paintColor != 0; bs.paintRough = b->paintRough != 0; bs.paintMetal = b->paintMetal != 0;
        bs.layerHeight = b->layerHeight / us;
        bs.faceGroup = b->faceGroup;
        bs.dyntopo = b->dyntopo != 0;
        bs.dyntopoMode = (DynTopoMode)std::clamp(b->dyntopoMode, 0, 2);
        bs.dyntopoDetail = b->dyntopoDetail / us;
        // Painting roughness/metalness switches the object to its per-vertex channels, seeded from the
        // material. That touches every vertex, so such a stroke is recorded as a full snapshot.
        const bool seed = bs.type == BrushType::MaterialPaint && (bs.paintRough || bs.paintMetal) && !o->st.mat.usePainted;
        const ObjectState propsBefore = o->st;
        std::shared_ptr<Mesh> snapshot;
        if (seed) {
            snapshot = std::make_shared<Mesh>(o->mesh);
            std::fill(o->mesh.rough.begin(), o->mesh.rough.end(), o->st.mat.roughness);
            std::fill(o->mesh.metal.begin(), o->mesh.metal.end(), o->st.mat.metallic);
            o->st.mat.usePainted = true;
        }
        auto stroke = std::make_unique<SculptStroke>(o->mesh, o->bvh, bs);
        if (!snapshot && stroke->usesDynTopo()) snapshot = std::make_shared<Mesh>(o->mesh);
        s->strokeInverse = w.affineInverse();
        if (!stroke->begin(s->strokeInverse.point(v3(origin)), s->strokeInverse.dir(v3(dir)), pressure)) {
            if (seed) { o->mesh.rough = snapshot->rough; o->mesh.metal = snapshot->metal; o->st = propsBefore; }
            return 0;
        }
        s->stroke = std::move(stroke);
        s->strokeObject = id;
        s->strokeSnapshot = snapshot;
        s->strokeSeeded = seed;
        s->strokePropsBefore = propsBefore;
        return 1;
    } catch (const std::exception&) { return fail(s, "not enough memory"); }
}

void sf_stroke_move(SFScene* s, const float* origin, const float* dir, float pressure) {
    if (!s || !s->stroke || !origin || !dir) return;
    try {
        s->stroke->move(s->strokeInverse.point(v3(origin)), s->strokeInverse.dir(v3(dir)), pressure);
    } catch (const std::exception&) { s->error = "not enough memory"; }
}

int sf_stroke_end(SFScene* s) {
    if (!s || !s->stroke) return 0;
    std::unique_ptr<SculptStroke> stroke = std::move(s->stroke);
    Object* o = s->scene.find(s->strokeObject);
    std::shared_ptr<Mesh> snapshot = std::move(s->strokeSnapshot);
    if (!o) return 0;
    StrokeDiff diff;
    stroke->end(diff);
    bool changed = stroke->changedAnything();
    try {
        if (s->strokeSeeded) {
            s->strokeSeeded = false;
            if (changed && snapshot) { // mesh snapshot + material switch as one undo step
                Command g;
                g.kind = Command::Group;
                Command c;
                c.kind = Command::MeshReplace; c.objectId = o->st.id;
                c.meshBefore = snapshot;
                c.meshAfter = std::make_shared<Mesh>(o->mesh);
                g.children.push_back(std::move(c));
                Command p;
                p.kind = Command::Props; p.objectId = o->st.id; p.before = s->strokePropsBefore; p.after = o->st;
                g.children.push_back(std::move(p));
                s->history.push(std::move(g));
            } else if (snapshot) { // nothing painted: undo the seeding
                o->mesh.rough = snapshot->rough; o->mesh.metal = snapshot->metal; o->st = s->strokePropsBefore;
            }
        } else if (stroke->usesDynTopo()) {
            if (stroke->topologyChanged()) { o->mesh.ensureAttributes(); o->bvh.build(o->mesh); ++o->topologyVersion; }
            if (changed && snapshot) {
                Command c;
                c.kind = Command::MeshReplace; c.objectId = o->st.id;
                c.meshBefore = snapshot;
                c.meshAfter = std::make_shared<Mesh>(o->mesh);
                s->history.push(std::move(c));
            }
        } else if (!diff.empty() && changed) {
            Command c;
            c.kind = Command::Stroke; c.objectId = o->st.id;
            c.diff = std::move(diff);
            s->history.push(std::move(c));
        }
    } catch (const std::exception&) { s->error = "not enough memory to record undo"; }
    return changed ? 1 : 0;
}

int sf_stroke_active(SFScene* s) { return s && s->stroke ? 1 : 0; }

int sf_stroke_take_dirty(SFScene* s, uint32_t* outMin, uint32_t* outMax) {
    if (!s || !s->stroke) return 0;
    uint32_t a = s->stroke->dirtyMin(), b = s->stroke->dirtyMax();
    s->stroke->resetDirty();
    if (a > b) return 0;
    if (outMin) *outMin = a;
    if (outMax) *outMax = b;
    return 1;
}

// ---- history -------------------------------------------------------------------------------
int sf_undo(SFScene* s) {
    if (!s) return 0;
    if (s->stroke) sf_stroke_end(s);
    return s->history.undo(s->scene) ? 1 : 0;
}
int sf_redo(SFScene* s) {
    if (!s) return 0;
    if (s->stroke) sf_stroke_end(s);
    return s->history.redo(s->scene) ? 1 : 0;
}
int sf_can_undo(SFScene* s) { return s && s->history.canUndo(); }
int sf_can_redo(SFScene* s) { return s && s->history.canRedo(); }
void sf_history_clear(SFScene* s) { if (s) s->history.clear(); }
void sf_history_set_limits(SFScene* s, uint32_t maxSteps, uint64_t maxBytes) {
    if (s) s->history.setLimits(std::max<uint32_t>(1, maxSteps), (size_t)maxBytes);
}

// ---- mesh operations -----------------------------------------------------------------------
int sf_op_subdivide(SFScene* s, uint32_t id, int smooth) {
    return meshOp(s, id, [&](Mesh& m, std::string& err) {
        if (m.tcount() > 6'000'000) { err = "mesh is too dense to subdivide again"; return false; }
        subdivide(m, smooth != 0);
        return true;
    });
}

int sf_op_decimate(SFScene* s, uint32_t id, uint32_t target) {
    return meshOp(s, id, [&](Mesh& m, std::string&) { decimate(m, std::max<uint32_t>(target, 4)); return true; });
}

int sf_op_remesh(SFScene* s, uint32_t id, float voxelWorld, SFProgress progress, void* user) {
    Object* o = object(s, id);
    if (!o) return 0;
    float voxel = voxelWorld / uniformScale(s->scene.worldMatrix(id));
    return meshOp(s, id, [&](Mesh& m, std::string& err) {
        RemeshOptions opt;
        opt.voxelSize = voxel;
        if (progress) opt.progress = [=](float f) { return progress(f, user) != 0; };
        Mesh out;
        if (!voxelRemesh({&m}, BoolOp::Union, opt, out, &err)) return false;
        m = std::move(out);
        return true;
    });
}

uint64_t sf_estimate_remesh_triangles(SFScene* s, uint32_t id, float voxelWorld) {
    Object* o = object(s, id);
    if (!o) return 0;
    return estimateRemeshTriangles(o->mesh, voxelWorld / uniformScale(s->scene.worldMatrix(id)));
}

int sf_op_smooth(SFScene* s, uint32_t id, int32_t iterations, float factor) {
    return meshOp(s, id, [&](Mesh& m, std::string&) {
        smoothMesh(m, std::clamp(iterations, 1, 200), clamp01(factor), true);
        return true;
    });
}
int sf_op_flip_normals(SFScene* s, uint32_t id) {
    return meshOp(s, id, [&](Mesh& m, std::string&) { flipNormals(m); return true; });
}
int sf_op_fill_holes(SFScene* s, uint32_t id) {
    return meshOp(s, id, [&](Mesh& m, std::string& err) {
        if (fillHoles(m) == 0) { err = "no open boundaries found"; return false; }
        return true;
    });
}
int sf_op_weld(SFScene* s, uint32_t id, float epsWorld) {
    Object* o = object(s, id);
    if (!o) return 0;
    float eps = epsWorld / uniformScale(s->scene.worldMatrix(id));
    return meshOp(s, id, [&](Mesh& m, std::string&) { weldVertices(m, eps); return true; });
}
int sf_op_mirror(SFScene* s, uint32_t id, int32_t axis) {
    // Symmetrise: keep the positive half, cut at the object-space plane, mirror it across and weld the seam.
    return meshOp(s, id, [&](Mesh& m, std::string& err) {
        int ax = std::clamp(axis, 0, 2);
        Vec3 n(0, 0, 0);
        n[ax] = -1.0f; // cutPlane removes the side the normal points to: the negative half
        cutPlane(m, Vec3(0, 0, 0), n, false);
        if (m.tcount() == 0) { err = "nothing lies on the positive side of the mirror plane"; return false; }
        float snap = std::max(length(m.bounds().size()) * 1e-6f, 1e-9f);
        for (auto& p : m.pos) if (std::fabs(p[ax]) < snap) p[ax] = 0.0f; // seam vertices exactly on the plane
        m = mirrorMesh(m, ax, true);
        return true;
    });
}
int sf_op_cut_plane(SFScene* s, uint32_t id, const float* wp, const float* wn, int cap) {
    Object* o = object(s, id);
    if (!o || !wp || !wn) return 0;
    Mat4 w = s->scene.worldMatrix(id), inv = w.affineInverse();
    Vec3 p = inv.point(v3(wp));
    // normals transform with the inverse transpose: n_local = W^T * n_world
    Vec3 nw = v3(wn);
    Vec3 n{w.m[0] * nw.x + w.m[1] * nw.y + w.m[2] * nw.z, w.m[4] * nw.x + w.m[5] * nw.y + w.m[6] * nw.z,
           w.m[8] * nw.x + w.m[9] * nw.y + w.m[10] * nw.z};
    if (length2(n) == 0) return fail(s, "invalid cut plane");
    return meshOp(s, id, [&](Mesh& m, std::string&) { cutPlane(m, p, n, cap != 0); return true; });
}

int sf_op_mask(SFScene* s, uint32_t id, int32_t op, float amount) {
    return meshOp(s, id, [&](Mesh& m, std::string& err) {
        switch (op) {
        case 0: maskClear(m); break;
        case 1: maskInvert(m); break;
        case 2: maskBlur(m, std::clamp((int)amount, 1, 50)); break;
        case 3: maskSharpen(m, amount > 0 ? amount : 1.0f); break;
        case 4: maskGrow(m); break;
        case 5: maskShrink(m); break;
        case 6: maskByCavity(m, amount > 0 ? amount : 1.0f, false); break;
        case 7: std::fill(m.mask.begin(), m.mask.end(), 1.0f); break;
        default: err = "unknown mask operation"; return false;
        }
        return true;
    });
}

int sf_op_fill(SFScene* s, uint32_t id, const float* rgb, float roughness, float metalness, int32_t channels) {
    Object* o = object(s, id);
    if (!o || !rgb) return 0;
    // Filling roughness/metalness switches the object to its painted channels, seeded from the material.
    const bool seed = (channels & 6) && !o->st.mat.usePainted;
    const ObjectState before = o->st;
    Vec3 c = v3(rgb);
    int ok = meshOp(s, id, [&](Mesh& m, std::string&) {
        if (seed) {
            std::fill(m.rough.begin(), m.rough.end(), before.mat.roughness);
            std::fill(m.metal.begin(), m.metal.end(), before.mat.metallic);
        }
        for (size_t i = 0; i < m.vcount(); ++i) {
            float a = 1.0f - clamp01(m.mask[i]);
            if (channels & 1) m.col[i] = lerp(m.col[i], c, a);
            if (channels & 2) m.rough[i] = lerp(m.rough[i], clamp01(roughness), a);
            if (channels & 4) m.metal[i] = lerp(m.metal[i], clamp01(metalness), a);
        }
        return true;
    });
    if (ok && seed) { // fold the material switch into the same undo step as the fill
        o = s->scene.find(id);
        o->st.mat.usePainted = true;
        Command g;
        g.kind = Command::Group;
        Command last;
        if (s->history.takeLast(last)) g.children.push_back(std::move(last));
        Command p;
        p.kind = Command::Props; p.objectId = id; p.before = before; p.after = o->st;
        g.children.push_back(std::move(p));
        s->history.push(std::move(g));
    }
    return ok;
}

int sf_op_apply_transform(SFScene* s, uint32_t id) {
    Object* o = object(s, id);
    if (!o) return 0;
    Mat4 local = o->localMatrix();
    ObjectState before = o->st;
    if (!meshOp(s, id, [&](Mesh& m, std::string&) { m.transform(local); return true; })) return 0;
    o = s->scene.find(id);
    o->st.position = Vec3(0, 0, 0); o->st.rotation = Quat(); o->st.scale = Vec3(1, 1, 1);
    // fold the mesh change and the transform reset into one undo step
    Command g;
    g.kind = Command::Group;
    Command last;
    if (s->history.takeLast(last)) g.children.push_back(std::move(last));
    Command p;
    p.kind = Command::Props; p.objectId = id; p.before = before; p.after = o->st;
    g.children.push_back(std::move(p));
    s->history.push(std::move(g));
    return 1;
}

uint32_t sf_op_extract_mask(SFScene* s, uint32_t id, float thicknessWorld) {
    Object* o = object(s, id);
    if (!o) return 0;
    try {
        Mesh m = extractMasked(o->mesh, std::max(0.0f, thicknessWorld / uniformScale(s->scene.worldMatrix(id))));
        if (m.tcount() == 0) { fail(s, "nothing is masked"); return 0; }
        ObjectState st = o->st;
        st.id = s->scene.nextId++;
        st.name += " extract";
        Object* n = s->scene.insert(st, std::move(m), s->scene.indexOf(id) + 1);
        s->history.push(addCommand(s->scene, *n));
        return n->st.id;
    } catch (const std::exception&) { fail(s, "not enough memory"); return 0; }
}

int32_t sf_op_separate(SFScene* s, uint32_t id) {
    Object* o = object(s, id);
    if (!o) return 0;
    try {
        std::vector<Mesh> parts = separateComponents(o->mesh);
        if (parts.size() < 2) { fail(s, "object has only one connected part"); return 0; }
        Command g;
        g.kind = Command::Group;
        Command r;
        r.kind = Command::MeshReplace; r.objectId = id;
        r.meshBefore = std::make_shared<Mesh>(o->mesh);
        o->mesh = std::move(parts[0]);
        o->meshChanged();
        r.meshAfter = std::make_shared<Mesh>(o->mesh);
        g.children.push_back(std::move(r));
        ObjectState base = o->st;
        int at = s->scene.indexOf(id);
        for (size_t i = 1; i < parts.size(); ++i) {
            ObjectState st = base;
            st.id = s->scene.nextId++;
            st.name = base.name + " part " + std::to_string(i + 1);
            Object* n = s->scene.insert(st, std::move(parts[i]), at + (int)i);
            g.children.push_back(addCommand(s->scene, *n));
        }
        s->history.push(std::move(g));
        return (int32_t)parts.size();
    } catch (const std::exception&) { fail(s, "not enough memory"); return 0; }
}

uint32_t sf_op_merge(SFScene* s, const uint32_t* ids, int32_t count) {
    if (!s || !ids || count < 2) { fail(s, "select at least two objects to merge"); return 0; }
    try {
        Mesh all;
        ObjectState st;
        std::vector<uint32_t> valid;
        for (int32_t i = 0; i < count; ++i) {
            Object* o = s->scene.find(ids[i]);
            if (!o || std::find(valid.begin(), valid.end(), ids[i]) != valid.end()) continue;
            Mesh m = o->mesh;
            m.transform(s->scene.worldMatrix(ids[i]));
            if (valid.empty()) { st.mat = o->st.mat; st.name = o->st.name + " merged"; }
            all.append(m);
            valid.push_back(ids[i]);
        }
        if (valid.size() < 2) { fail(s, "select at least two objects to merge"); return 0; }
        Command g;
        g.kind = Command::Group;
        for (uint32_t id : valid) removeWithHistory(s->scene, id, g.children);
        st.id = s->scene.nextId++;
        Object* n = s->scene.insert(st, std::move(all), -1);
        g.children.push_back(addCommand(s->scene, *n));
        s->history.push(std::move(g));
        return n->st.id;
    } catch (const std::exception&) { fail(s, "not enough memory"); return 0; }
}

int sf_op_boolean(SFScene* s, uint32_t a, uint32_t b, int32_t op, float voxelWorld, SFProgress progress, void* user) {
    Object* oa = object(s, a);
    Object* ob = object(s, b);
    if (!oa || !ob) return 0;
    if (a == b) return fail(s, "boolean needs two different objects");
    if (op < 0 || op > 2) return fail(s, "unknown boolean operation");
    try {
        Mat4 wa = s->scene.worldMatrix(a);
        Mesh other = ob->mesh;
        other.transform(wa.affineInverse() * s->scene.worldMatrix(b));
        RemeshOptions opt;
        opt.voxelSize = voxelWorld / uniformScale(wa);
        if (progress) opt.progress = [=](float f) { return progress(f, user) != 0; };
        Mesh out;
        std::string err;
        if (!voxelRemesh({&oa->mesh, &other}, (BoolOp)op, opt, out, &err)) return fail(s, err);
        std::string why;
        if (!out.validate(&why)) return fail(s, "boolean produced invalid geometry: " + why);
        Command g;
        g.kind = Command::Group;
        Command r;
        r.kind = Command::MeshReplace; r.objectId = a;
        r.meshBefore = std::make_shared<Mesh>(std::move(oa->mesh));
        oa->mesh = std::move(out);
        oa->meshChanged();
        r.meshAfter = std::make_shared<Mesh>(oa->mesh);
        g.children.push_back(std::move(r));
        removeWithHistory(s->scene, b, g.children);
        s->history.push(std::move(g));
        return 1;
    } catch (const std::exception&) { return fail(s, "not enough memory for this boolean"); }
}

// ---- files ---------------------------------------------------------------------------------
int sf_save_project(SFScene* s, const char* path) {
    if (!s || !path) return 0;
    try {
        std::string err;
        if (!saveProject(s->scene, path, &err)) return fail(s, err);
        return 1;
    } catch (const std::exception& e) { return fail(s, std::string("save failed: ") + e.what()); }
}

int sf_load_project(SFScene* s, const char* path) {
    if (!s || !path) return 0;
    try {
        Scene loaded;
        std::string err;
        if (!loadProject(path, loaded, &err)) return fail(s, err);
        s->stroke.reset();
        s->scene = std::move(loaded);
        s->history.clear();
        return 1;
    } catch (const std::exception& e) { return fail(s, std::string("load failed: ") + e.what()); }
}

void sf_set_app_state(SFScene* s, const char* json) { if (s) s->scene.appState = json ? json : ""; }
const char* sf_get_app_state(SFScene* s) { return s ? s->scene.appState.c_str() : ""; }

int sf_export(SFScene* s, const char* path, int32_t format, const uint32_t* ids, int32_t count, float scale) {
    if (!s || !path) return 0;
    if (format < 0 || format > 4) return fail(s, "unknown export format");
    try {
        std::vector<ExportItem> items;
        for (Object* o : selection(s, ids, count)) {
            if (o->mesh.tcount() == 0) continue;
            items.push_back({&o->mesh, s->scene.worldMatrix(o->st.id), o->st.name, o->st.mat});
        }
        std::string err;
        if (!exportFile(items, (FileFormat)format, path, scale, &err)) return fail(s, err);
        return 1;
    } catch (const std::exception& e) { return fail(s, std::string("export failed: ") + e.what()); }
}

int32_t sf_import(SFScene* s, const char* path) {
    if (!s || !path) return 0;
    try {
        std::vector<ImportedObject> objs;
        std::string err;
        if (!importFile(path, objs, &err)) return fail(s, err);
        Command g;
        g.kind = Command::Group;
        for (auto& io : objs) {
            io.mesh.transform(io.transform); // bake file transforms into the geometry
            Object* o = s->scene.add(std::move(io.mesh), io.name);
            if (io.hasMaterial) o->st.mat = io.mat;
            g.children.push_back(addCommand(s->scene, *o));
        }
        int32_t n = (int32_t)g.children.size();
        s->history.push(std::move(g));
        return n;
    } catch (const std::exception& e) { return fail(s, std::string("import failed: ") + e.what()); }
}

int sf_analyze(SFScene* s, const uint32_t* ids, int32_t count, float unitScale, float thinMm, SFAnalysis* out) {
    if (!s || !out) return 0;
    try {
        Mesh all;
        Mat4 sc;
        sc.m[0] = sc.m[5] = sc.m[10] = unitScale > 0 ? unitScale : 1.0f;
        for (Object* o : selection(s, ids, count)) {
            Mesh m = o->mesh;
            m.transform(sc * s->scene.worldMatrix(o->st.id));
            all.append(m);
        }
        if (all.tcount() == 0) return fail(s, "nothing to analyse");
        MeshAnalysis a = analyzeMesh(all, thinMm);
        std::memset(out, 0, sizeof(*out));
        out->vertices = a.vertices; out->triangles = a.triangles; out->boundaryEdges = a.boundaryEdges;
        out->nonManifoldEdges = a.nonManifoldEdges; out->inconsistentEdges = a.inconsistentEdges;
        out->degenerateTriangles = a.degenerateTriangles; out->looseVertices = a.looseVertices; out->holes = a.holes;
        out->thinSamples = a.thinSamples; out->thicknessSamples = a.thicknessSamples;
        out->components = a.components; out->watertight = a.watertight; out->invertedNormals = a.invertedNormals;
        out->volume = a.volume; out->surfaceArea = a.surfaceArea;
        for (int i = 0; i < 3; ++i) { out->boundsMin[i] = a.bounds.mn[i]; out->boundsMax[i] = a.bounds.mx[i]; }
        out->minThickness = a.minThickness;
        return 1;
    } catch (const std::exception&) { return fail(s, "not enough memory for analysis"); }
}

} // extern "C"

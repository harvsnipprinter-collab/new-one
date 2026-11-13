// SculptForge 3D - scene graph, undo history and project persistence
#pragma once
#include "BVH.h"
#include "Mesh.h"
#include "Sculpt.h"
#include <memory>
#include <string>
#include <vector>

namespace sf {

struct Material {
    Vec3 baseColor{1, 1, 1}; // multiplies the painted vertex colour
    float roughness = 0.6f;
    float metallic = 0.0f;
    float opacity = 1.0f;
    float emission = 0.0f;
    bool usePainted = false; // true: roughness/metalness come from the per-vertex painted channels
};

// Everything about an object except its geometry.
struct ObjectState {
    uint32_t id = 0;
    std::string name;
    Vec3 position{0, 0, 0};
    Quat rotation;
    Vec3 scale{1, 1, 1};
    uint32_t parent = 0; // 0 = scene root
    bool visible = true, locked = false;
    Material mat;
};

struct Object {
    ObjectState st;
    Mesh mesh;
    BVH bvh;
    uint64_t topologyVersion = 1; // bumped whenever index/vertex counts may have changed
    Mat4 localMatrix() const { return Mat4::trs(st.position, st.rotation, st.scale); }
    void meshChanged() { mesh.finalize(); bvh.build(mesh); ++topologyVersion; }
};

struct Scene {
    std::vector<std::unique_ptr<Object>> objects;
    uint32_t nextId = 1;
    std::string appState; // opaque UI state (camera, lights, presets) stored with the project

    Object* find(uint32_t id);
    const Object* find(uint32_t id) const;
    int indexOf(uint32_t id) const;
    Mat4 worldMatrix(uint32_t id) const;
    Object* add(Mesh mesh, const std::string& name);
    // Insert a fully specified object (used by undo/load). Keeps its id.
    Object* insert(const ObjectState& st, Mesh mesh, int index);
    bool remove(uint32_t id); // children are re-parented to the removed object's parent
    bool wouldCycle(uint32_t child, uint32_t newParent) const;
    size_t totalTriangles() const;
    size_t memoryBytes() const;
};

// ---- undo / redo ---------------------------------------------------------------------------
struct Command {
    enum Kind { Stroke, MeshReplace, ObjectAdd, ObjectRemove, Props, Group } kind = Stroke;
    uint32_t objectId = 0;
    StrokeDiff diff;                               // Stroke
    std::shared_ptr<Mesh> meshBefore, meshAfter;   // MeshReplace / ObjectAdd / ObjectRemove (meshBefore)
    ObjectState before, after;                     // Props / ObjectAdd / ObjectRemove
    int index = -1;                                // list position for add/remove
    std::vector<Command> children;                 // Group
    size_t bytes() const;
};

class History {
public:
    void push(Command c);
    bool undo(Scene& s);
    bool redo(Scene& s);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void clear() { undo_.clear(); redo_.clear(); }
    // Remove and return the most recent command (used to fold several edits into one undo step).
    bool takeLast(Command& out) { if (undo_.empty()) return false; out = std::move(undo_.back()); undo_.pop_back(); return true; }
    void setLimits(size_t maxDepth, size_t maxBytes) { maxDepth_ = maxDepth; maxBytes_ = maxBytes; trim(); }
    size_t depth() const { return undo_.size(); }
    size_t bytes() const;

private:
    std::vector<Command> undo_, redo_;
    size_t maxDepth_ = 60, maxBytes_ = 768u << 20;
    void trim();
    static void apply(Scene& s, const Command& c, bool undo);
};

// ---- native project file (.sculptforge) ----------------------------------------------------
constexpr uint32_t kProjectVersion = 1;
// Atomic save: writes a temporary file, verifies it, then replaces the destination.
bool saveProject(const Scene& s, const std::string& path, std::string* error = nullptr);
// Loads into `out` only if the whole file parses and validates; otherwise `out` is untouched.
bool loadProject(const std::string& path, Scene& out, std::string* error = nullptr);

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0);

} // namespace sf

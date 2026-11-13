// SculptForge 3D - sculpt / paint / mask brush engine operating on real mesh data
#pragma once
#include "BVH.h"
#include "Mesh.h"

namespace sf {

enum class BrushType : int {
    Draw = 0, Clay, ClayBuildup, Inflate, Smooth, Flatten, Polish, Scrape, Crease, Pinch,
    Move, Elastic, SnakeHook, Nudge, Drag, Twist, Layer, Stamp,
    Mask, MaskBlur, MaskSharpen, FaceGroup, Paint, Smudge, MaterialPaint, Count
};
enum class Falloff : int { Smooth = 0, Sphere, Linear, Sharp, Constant };
enum class DynTopoMode : int { Subdivide = 0, Collapse = 1, Both = 2 };

struct BrushSettings {
    BrushType type = BrushType::Draw;
    float radius = 0.25f;       // object-space units
    float strength = 0.5f;      // 0..1
    Falloff falloff = Falloff::Smooth;
    float spacing = 0.15f;      // dab spacing as a fraction of the radius
    float smoothing = 0.0f;     // 0..0.95 lazy-stroke smoothing of the input
    bool invert = false;
    bool frontFacesOnly = true;
    bool pressureStrength = true;
    bool pressureRadius = false;
    bool symX = false, symY = false, symZ = false;
    int radialCount = 1;        // > 1 enables radial symmetry
    int radialAxis = 1;         // 0 X, 1 Y, 2 Z
    // painting
    Vec3 color{0.8f, 0.1f, 0.1f};
    float paintRoughness = 0.5f, paintMetalness = 0.0f;
    bool paintColor = true, paintRough = false, paintMetal = false;
    // misc
    float layerHeight = 0.05f;
    uint32_t faceGroup = 1;
    // dynamic topology
    bool dyntopo = false;
    DynTopoMode dyntopoMode = DynTopoMode::Subdivide;
    float dyntopoDetail = 0.03f; // target edge length in object units
};

const char* brushName(BrushType t);
// Brushes that grab a fixed region at stroke start instead of dabbing along the surface.
bool brushIsGrab(BrushType t);
// True when the brush moves vertices (as opposed to paint/mask only).
bool brushDeforms(BrushType t);
float falloffWeight(Falloff f, float normalisedDistance);

// Sparse before/after record of one stroke, used for undo/redo.
struct StrokeDiff {
    std::vector<uint32_t> verts;
    std::vector<Vec3> posBefore, posAfter, colBefore, colAfter;
    std::vector<float> roughBefore, roughAfter, metalBefore, metalAfter, maskBefore, maskAfter;
    std::vector<uint32_t> tris, groupBefore, groupAfter;
    bool empty() const { return verts.empty() && tris.empty(); }
    size_t bytes() const { return verts.size() * (4 + 24 * 2 + 12 * 2) + tris.size() * 12; }
    void apply(Mesh& m, bool undo) const; // restores values and recomputes normals
};

class SculptStroke {
public:
    SculptStroke(Mesh& mesh, BVH& bvh, const BrushSettings& settings);
    // Rays are in the mesh's local space. Returns false when the ray misses (no stroke started).
    bool begin(Vec3 rayOrigin, Vec3 rayDir, float pressure);
    void move(Vec3 rayOrigin, Vec3 rayDir, float pressure);
    // Finish; fills `diff` unless topology changed (then the caller must use a full snapshot).
    void end(StrokeDiff& diff);

    bool usesDynTopo() const { return dyn_; }
    bool topologyChanged() const { return topoChanged_; }
    bool changedAnything() const { return changed_; }
    // Vertex range modified since the last call to resetDirty().
    uint32_t dirtyMin() const { return dirtyMin_; }
    uint32_t dirtyMax() const { return dirtyMax_; }
    void resetDirty() { dirtyMin_ = UINT32_MAX; dirtyMax_ = 0; }
    Vec3 lastHitPoint() const { return lastPos_; }
    Vec3 lastHitNormal() const { return lastNormal_; }
    size_t dabCount() const { return dabs_; }

private:
    struct Dab { Vec3 center, normal, view, delta; float radius, strength; };
    struct GrabEntry { uint32_t v; float w; uint16_t sym; };

    Mesh& m_;
    BVH& bvh_;
    BrushSettings s_;
    std::vector<Mat4> sym_;
    std::vector<float> symDet_;
    bool dyn_ = false, topoChanged_ = false, changed_ = false, started_ = false;
    // first-touch originals
    std::vector<uint32_t> slot_, touched_;
    std::vector<Vec3> origPos_, origNrm_, origCol_;
    std::vector<float> origRough_, origMetal_, origMask_, layerAmt_;
    std::vector<uint32_t> triTouched_, triGroupBefore_;
    std::vector<uint8_t> triSeen_;
    // stroke state
    Vec3 lastPos_, lastNormal_, lastRayO_, lastRayD_, smoothO_, smoothD_;
    Vec3 grabOrigin_, grabPoint_, grabView_;
    std::vector<GrabEntry> grab_;
    Vec3 smudge_; bool smudgeValid_ = false;
    size_t dabs_ = 0;
    uint32_t dirtyMin_ = UINT32_MAX, dirtyMax_ = 0;
    // scratch
    std::vector<uint32_t> vs_, ring_, nstamp_, ts_, moved_;
    void refitMoved();
    std::vector<float> w_;
    std::vector<Vec3> tmp_;
    std::vector<float> tmpF_;
    uint32_t ngen_ = 0;

    uint32_t touch(uint32_t v);
    void buildSymmetry();
    void applyDab(const Dab& d);
    void applyOne(const Dab& d, float det);
    void applyGrab(Vec3 totalDelta);
    void applyHook(Vec3 from, Vec3 stepDelta, float radius);
    void gather(Vec3 c, float r, Vec3 view, bool useMask);
    void updateNormals(const std::vector<uint32_t>& verts);
    void runDynTopo(const Dab& d);
    Dab makeDab(const RayHit& h, Vec3 rayDir, float pressure, Vec3 delta) const;
};

} // namespace sf

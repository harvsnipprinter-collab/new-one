// SculptForge 3D - parametric primitive generation (all results are real welded triangle meshes)
#pragma once
#include "Mesh.h"

namespace sf {

enum class PrimitiveType : int {
    UVSphere = 0, Icosphere, Cube, RoundedCube, Cylinder, Cone, Torus, Plane, Disc, Capsule,
    Tube, Pyramid, Ring, Spiral, Prism, Count
};

struct Vec2 { float r, y; };

Mesh makeUVSphere(float radius, int segments, int rings);
Mesh makeIcosphere(float radius, int subdivisions);
Mesh makeCube(Vec3 size, int divisions, float cornerRadius = 0);
Mesh makeCylinder(float radius, float height, int sides, bool caps);
Mesh makeCone(float radius, float height, int sides, bool cap);
Mesh makeTorus(float majorRadius, float minorRadius, int majorSeg, int minorSeg);
Mesh makePlane(float width, float depth, int divisions);
Mesh makeDisc(float radius, int sides);
Mesh makeCapsule(float radius, float height, int sides);
// Revolve a profile (radius, y) listed bottom-to-top around the Y axis.
Mesh makeLathe(const std::vector<Vec2>& profile, int sides, bool closedProfile = false);
// Sweep a circle along a Catmull-Rom curve through the control points.
Mesh makeTube(const std::vector<Vec3>& controlPoints, float radiusStart, float radiusEnd, int sides,
              int samplesPerSpan, bool caps);
Mesh makeSpiral(float radius, float height, float turns, float thickness, int sides);

// Generic entry used by the UI. Parameter meaning per type is documented in Primitives.cpp.
Mesh makePrimitive(PrimitiveType type, const float* params, int paramCount);
const char* primitiveName(PrimitiveType type);

} // namespace sf

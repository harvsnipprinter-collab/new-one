#include "sf/Primitives.h"
#include "sf/MeshOps.h"
#include <map>

namespace sf {

static const float kPi = 3.14159265358979f;

static Mesh finish(Mesh m) {
    m.ensureAttributes();
    AABB b = m.bounds();
    float eps = std::max(length(b.size()) * 1e-6f, 1e-9f);
    weldVertices(m, eps); // merges seams/poles and drops degenerate triangles
    m.finalize();
    return m;
}

// Insert points so no profile segment is longer than maxLen.
static std::vector<Vec2> refineProfile(const std::vector<Vec2>& p, float maxLen, bool closed) {
    std::vector<Vec2> out;
    size_t n = p.size();
    size_t segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) {
        Vec2 a = p[i], b = p[(i + 1) % n];
        float len = std::hypot(b.r - a.r, b.y - a.y);
        int k = std::max(1, (int)std::ceil(len / std::max(maxLen, 1e-6f)));
        k = std::min(k, 512);
        for (int j = 0; j < k; ++j) {
            float t = (float)j / k;
            out.push_back({a.r + (b.r - a.r) * t, a.y + (b.y - a.y) * t});
        }
    }
    if (!closed) out.push_back(p.back());
    return out;
}

Mesh makeLathe(const std::vector<Vec2>& profile, int sides, bool closedProfile) {
    Mesh m;
    sides = std::clamp(sides, 3, 2048);
    size_t n = profile.size();
    if (n < 2) return m;
    m.resizeVerts(n * sides);
    for (size_t i = 0; i < n; ++i)
        for (int j = 0; j < sides; ++j) {
            float a = 2 * kPi * j / sides;
            float r = std::max(profile[i].r, 0.0f);
            m.pos[i * sides + j] = {r * std::cos(a), profile[i].y, r * std::sin(a)};
        }
    size_t rows = closedProfile ? n : n - 1;
    for (size_t i = 0; i < rows; ++i) {
        size_t i1 = (i + 1) % n;
        for (int j = 0; j < sides; ++j) {
            int j1 = (j + 1) % sides;
            uint32_t a = (uint32_t)(i * sides + j), b = (uint32_t)(i1 * sides + j);
            uint32_t c = (uint32_t)(i * sides + j1), d = (uint32_t)(i1 * sides + j1);
            m.addTri(a, b, c);
            m.addTri(b, d, c);
        }
    }
    return finish(std::move(m));
}

Mesh makeUVSphere(float radius, int segments, int rings) {
    rings = std::clamp(rings, 2, 2048);
    std::vector<Vec2> p;
    for (int i = 0; i <= rings; ++i) {
        float phi = -kPi / 2 + kPi * i / rings;
        float r = (i == 0 || i == rings) ? 0.0f : radius * std::cos(phi);
        p.push_back({r, radius * std::sin(phi)});
    }
    return makeLathe(p, segments);
}

Mesh makeIcosphere(float radius, int subdivisions) {
    subdivisions = std::clamp(subdivisions, 0, 8);
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<Vec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<uint32_t> f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                               3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (auto& p : v) p = normalize(p);
    for (int s = 0; s < subdivisions; ++s) {
        std::map<uint64_t, uint32_t> mid;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            uint64_t key = ((uint64_t)std::min(a, b) << 32) | std::max(a, b);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            v.push_back(normalize((v[a] + v[b]) * 0.5f));
            return mid[key] = (uint32_t)v.size() - 1;
        };
        std::vector<uint32_t> nf;
        nf.reserve(f.size() * 4);
        for (size_t i = 0; i < f.size(); i += 3) {
            uint32_t a = f[i], b = f[i + 1], c = f[i + 2];
            uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            uint32_t add[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            nf.insert(nf.end(), add, add + 12);
        }
        f.swap(nf);
    }
    Mesh m;
    m.resizeVerts(v.size());
    for (size_t i = 0; i < v.size(); ++i) m.pos[i] = v[i] * radius;
    m.tri = f;
    m.finalize();
    return m;
}

Mesh makeCube(Vec3 size, int div, float cornerRadius) {
    div = std::clamp(div, 1, 512);
    Mesh m;
    Vec3 h = size * 0.5f;
    // Each face: normal axis n, tangent axes u,v chosen so u x v = +n (outward winding).
    for (int face = 0; face < 6; ++face) {
        int n = face / 2;
        float sign = (face % 2 == 0) ? 1.0f : -1.0f;
        int u = (n + 1) % 3, v = (n + 2) % 3;
        if (sign < 0) std::swap(u, v);
        uint32_t base = (uint32_t)m.vcount();
        m.resizeVerts(base + (size_t)(div + 1) * (div + 1));
        for (int j = 0; j <= div; ++j)
            for (int i = 0; i <= div; ++i) {
                Vec3 p;
                p[n] = sign * h[n];
                p[u] = -h[u] + size[u] * i / div;
                p[v] = -h[v] + size[v] * j / div;
                m.pos[base + j * (div + 1) + i] = p;
            }
        for (int j = 0; j < div; ++j)
            for (int i = 0; i < div; ++i) {
                uint32_t a = base + j * (div + 1) + i, b = a + 1, c = a + (div + 1), d = c + 1;
                m.addTri(a, b, c, face);
                m.addTri(b, d, c, face);
            }
    }
    float rad = std::min({cornerRadius, h.x, h.y, h.z});
    if (rad > 0) {
        Vec3 inner = h - Vec3(rad);
        for (auto& p : m.pos) {
            Vec3 c{std::clamp(p.x, -inner.x, inner.x), std::clamp(p.y, -inner.y, inner.y),
                   std::clamp(p.z, -inner.z, inner.z)};
            Vec3 d = p - c;
            if (length2(d) > 0) p = c + normalize(d) * rad;
        }
    }
    return finish(std::move(m));
}

Mesh makeCylinder(float radius, float height, int sides, bool caps) {
    sides = std::clamp(sides, 3, 2048);
    float edge = 2 * kPi * radius / sides;
    std::vector<Vec2> p;
    if (caps) p.push_back({0, -height / 2});
    p.push_back({radius, -height / 2});
    p.push_back({radius, height / 2});
    if (caps) p.push_back({0, height / 2});
    return makeLathe(refineProfile(p, edge, false), sides);
}

Mesh makeCone(float radius, float height, int sides, bool cap) {
    sides = std::clamp(sides, 3, 2048);
    float edge = 2 * kPi * radius / sides;
    std::vector<Vec2> p;
    if (cap) p.push_back({0, -height / 2});
    p.push_back({radius, -height / 2});
    p.push_back({0, height / 2});
    return makeLathe(refineProfile(p, edge, false), sides);
}

Mesh makeTorus(float R, float r, int majorSeg, int minorSeg) {
    minorSeg = std::clamp(minorSeg, 3, 1024);
    std::vector<Vec2> p;
    for (int i = 0; i < minorSeg; ++i) {
        float a = 2 * kPi * i / minorSeg;
        p.push_back({R + r * std::cos(a), r * std::sin(a)});
    }
    return makeLathe(p, majorSeg, true);
}

Mesh makePlane(float width, float depth, int div) {
    div = std::clamp(div, 1, 2048);
    Mesh m;
    m.resizeVerts((size_t)(div + 1) * (div + 1));
    for (int j = 0; j <= div; ++j)
        for (int i = 0; i <= div; ++i)
            m.pos[j * (div + 1) + i] = {-width / 2 + width * i / div, 0, -depth / 2 + depth * j / div};
    for (int j = 0; j < div; ++j)
        for (int i = 0; i < div; ++i) {
            uint32_t a = j * (div + 1) + i, b = a + 1, c = a + (div + 1), d = c + 1;
            m.addTri(a, c, b); // +Y facing
            m.addTri(b, c, d);
        }
    m.finalize();
    return m;
}

Mesh makeDisc(float radius, int sides) {
    sides = std::clamp(sides, 3, 2048);
    float edge = 2 * kPi * radius / sides;
    return makeLathe(refineProfile({{radius, 0}, {0, 0}}, edge, false), sides); // faces +Y
}

Mesh makeCapsule(float radius, float height, int sides) {
    sides = std::clamp(sides, 3, 2048);
    int rings = std::max(2, sides / 4);
    float half = std::max(0.0f, height / 2 - radius);
    std::vector<Vec2> p;
    for (int i = 0; i <= rings; ++i) {
        float phi = -kPi / 2 + (kPi / 2) * i / rings;
        p.push_back({i == 0 ? 0.0f : radius * std::cos(phi), -half + radius * std::sin(phi)});
    }
    for (int i = 0; i <= rings; ++i) {
        float phi = (kPi / 2) * i / rings;
        p.push_back({i == rings ? 0.0f : radius * std::cos(phi), half + radius * std::sin(phi)});
    }
    return makeLathe(refineProfile(p, 2 * kPi * radius / sides, false), sides);
}

static Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
            (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

Mesh makeTube(const std::vector<Vec3>& cp, float r0, float r1, int sides, int samplesPerSpan, bool caps) {
    Mesh m;
    if (cp.size() < 2) return m;
    sides = std::clamp(sides, 3, 512);
    samplesPerSpan = std::clamp(samplesPerSpan, 1, 256);
    std::vector<Vec3> path;
    size_t n = cp.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        Vec3 p0 = cp[i == 0 ? 0 : i - 1], p1 = cp[i], p2 = cp[i + 1], p3 = cp[std::min(i + 2, n - 1)];
        for (int s = 0; s < samplesPerSpan; ++s) path.push_back(catmullRom(p0, p1, p2, p3, (float)s / samplesPerSpan));
    }
    path.push_back(cp.back());
    size_t rings = path.size();
    // Parallel-transport frames avoid twisting along the curve.
    Vec3 tPrev = normalize(path[1] - path[0]);
    Vec3 up = std::fabs(tPrev.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 nPrev = normalize(cross(cross(tPrev, up), tPrev));
    m.resizeVerts(rings * sides);
    for (size_t i = 0; i < rings; ++i) {
        Vec3 t = normalize((i + 1 < rings ? path[i + 1] : path[i]) - (i > 0 ? path[i - 1] : path[i]));
        if (length2(t) == 0) t = tPrev;
        Vec3 nn = nPrev - t * dot(nPrev, t);
        nn = length2(nn) > 1e-12f ? normalize(nn) : normalize(cross(t, std::fabs(t.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0)));
        Vec3 bn = cross(t, nn);
        float rad = lerp(r0, r1, rings > 1 ? (float)i / (rings - 1) : 0.0f);
        for (int j = 0; j < sides; ++j) {
            float a = 2 * kPi * j / sides;
            m.pos[i * sides + j] = path[i] + (nn * std::cos(a) + bn * std::sin(a)) * rad;
        }
        nPrev = nn; tPrev = t;
    }
    for (size_t i = 0; i + 1 < rings; ++i)
        for (int j = 0; j < sides; ++j) {
            int j1 = (j + 1) % sides;
            uint32_t a = (uint32_t)(i * sides + j), b = (uint32_t)((i + 1) * sides + j);
            uint32_t c = (uint32_t)(i * sides + j1), d = (uint32_t)((i + 1) * sides + j1);
            m.addTri(a, c, b);
            m.addTri(b, c, d);
        }
    if (caps) {
        uint32_t c0 = m.addVertex(path.front()), c1 = m.addVertex(path.back());
        uint32_t last = (uint32_t)((rings - 1) * sides);
        for (int j = 0; j < sides; ++j) {
            int j1 = (j + 1) % sides;
            m.addTri(c0, j1, j);
            m.addTri(c1, last + j, last + j1);
        }
    }
    return finish(std::move(m));
}

Mesh makeSpiral(float radius, float height, float turns, float thickness, int sides) {
    std::vector<Vec3> cp;
    int n = std::max(8, (int)(turns * 16));
    for (int i = 0; i <= n; ++i) {
        float t = (float)i / n, a = t * turns * 2 * kPi;
        cp.push_back({radius * std::cos(a), -height / 2 + height * t, radius * std::sin(a)});
    }
    return makeTube(cp, thickness, thickness, sides, 2, true);
}

// Parameter layout for makePrimitive (missing values use defaults):
//  UVSphere:    radius, segments, rings          Icosphere: radius, subdivisions
//  Cube:        sx, sy, sz, divisions            RoundedCube: sx, sy, sz, divisions, cornerRadius
//  Cylinder:    radius, height, sides, caps      Cone: radius, height, sides, cap
//  Torus/Ring:  major, minor, majorSeg, minorSeg Plane: width, depth, divisions
//  Disc:        radius, sides                    Capsule: radius, height, sides
//  Tube:        length, radiusStart, radiusEnd, sides   (straight; curves use makeTube directly)
//  Pyramid:     base radius, height, base sides  Spiral: radius, height, turns, thickness, sides
//  Prism:       radius, height, sides
Mesh makePrimitive(PrimitiveType type, const float* p, int count) {
    auto P = [&](int i, float def) { return (p && i < count && std::isfinite(p[i])) ? p[i] : def; };
    auto pos = [&](int i, float def) { return std::max(P(i, def), 1e-4f); };
    switch (type) {
    case PrimitiveType::UVSphere: return makeUVSphere(pos(0, 1), (int)P(1, 64), (int)P(2, 32));
    case PrimitiveType::Icosphere: return makeIcosphere(pos(0, 1), (int)P(1, 4));
    case PrimitiveType::Cube: return makeCube({pos(0, 2), pos(1, 2), pos(2, 2)}, (int)P(3, 16), 0);
    case PrimitiveType::RoundedCube:
        return makeCube({pos(0, 2), pos(1, 2), pos(2, 2)}, (int)P(3, 24), std::max(0.0f, P(4, 0.3f)));
    case PrimitiveType::Cylinder: return makeCylinder(pos(0, 1), pos(1, 2), (int)P(2, 48), P(3, 1) != 0);
    case PrimitiveType::Cone: return makeCone(pos(0, 1), pos(1, 2), (int)P(2, 48), P(3, 1) != 0);
    case PrimitiveType::Torus: return makeTorus(pos(0, 1), pos(1, 0.35f), (int)P(2, 64), (int)P(3, 24));
    case PrimitiveType::Ring: return makeTorus(pos(0, 1), pos(1, 0.08f), (int)P(2, 96), (int)P(3, 12));
    case PrimitiveType::Plane: return makePlane(pos(0, 2), pos(1, 2), (int)P(2, 32));
    case PrimitiveType::Disc: return makeDisc(pos(0, 1), (int)P(1, 48));
    case PrimitiveType::Capsule: return makeCapsule(pos(0, 0.5f), pos(1, 2), (int)P(2, 48));
    case PrimitiveType::Tube: {
        float len = pos(0, 2);
        return makeTube({{0, -len / 2, 0}, {0, 0, 0}, {0, len / 2, 0}}, pos(1, 0.25f), pos(2, 0.25f), (int)P(3, 24), 12, true);
    }
    case PrimitiveType::Pyramid: return makeCone(pos(0, 1), pos(1, 1.5f), (int)P(2, 4), true);
    case PrimitiveType::Spiral: return makeSpiral(pos(0, 1), pos(1, 2), pos(2, 3), pos(3, 0.12f), (int)P(4, 12));
    case PrimitiveType::Prism: return makeCylinder(pos(0, 1), pos(1, 2), (int)P(2, 6), true);
    default: return Mesh();
    }
}

const char* primitiveName(PrimitiveType t) {
    static const char* names[] = {"Sphere", "Icosphere", "Cube", "Rounded Cube", "Cylinder", "Cone", "Torus", "Plane",
                                  "Disc", "Capsule", "Tube", "Pyramid", "Ring", "Spiral", "Prism"};
    int i = (int)t;
    return (i >= 0 && i < (int)PrimitiveType::Count) ? names[i] : "Mesh";
}

} // namespace sf

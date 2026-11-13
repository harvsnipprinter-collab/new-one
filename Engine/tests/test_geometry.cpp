#include "TestKit.h"
#include <random>

using namespace sf;

static const double kPi = 3.14159265358979;

static bool valid(const Mesh& m) {
    std::string why;
    bool ok = m.validate(&why);
    if (!ok) std::printf("    invalid mesh: %s\n", why.c_str());
    return ok;
}

static void primitives() {
    tk::section("Primitives");
    struct Case { PrimitiveType t; bool closed; double volume; double tol; };
    const double r = 1, h = 2;
    Case cases[] = {
        {PrimitiveType::UVSphere, true, 4.0 / 3.0 * kPi, 0.02},
        {PrimitiveType::Icosphere, true, 4.0 / 3.0 * kPi, 0.02},
        {PrimitiveType::Cube, true, 8.0, 1e-4},
        {PrimitiveType::RoundedCube, true, 0, 0},
        {PrimitiveType::Cylinder, true, kPi * r * r * h, 0.01},
        {PrimitiveType::Cone, true, kPi * r * r * h / 3.0, 0.01},
        {PrimitiveType::Torus, true, 2 * kPi * kPi * 1.0 * 0.35 * 0.35, 0.03},
        {PrimitiveType::Plane, false, 0, 0},
        {PrimitiveType::Disc, false, 0, 0},
        {PrimitiveType::Capsule, true, kPi * 0.25 * 1.0 + 4.0 / 3.0 * kPi * 0.125, 0.02},
        {PrimitiveType::Tube, true, kPi * 0.0625 * 2.0, 0.03},
        {PrimitiveType::Pyramid, true, 0, 0},
        {PrimitiveType::Ring, true, 0, 0},
        {PrimitiveType::Spiral, true, 0, 0},
        {PrimitiveType::Prism, true, 0, 0},
    };
    for (const Case& c : cases) {
        Mesh m = makePrimitive(c.t, nullptr, 0);
        MeshAnalysis a = analyzeMesh(m);
        std::string name = primitiveName(c.t);
        bool ok = CHECK_MSG(m.tcount() > 0 && valid(m), name);
        ok &= CHECK_MSG(a.degenerateTriangles == 0 && a.looseVertices == 0, name + " has degenerate/loose geometry");
        if (c.closed) {
            ok &= CHECK_MSG(a.watertight, name + " should be watertight (boundary " + std::to_string(a.boundaryEdges) +
                                              ", nonmanifold " + std::to_string(a.nonManifoldEdges) + ")");
            ok &= CHECK_MSG(a.volume > 0, name + " volume should be positive (outward normals)");
            if (c.volume > 0) ok &= CHECK_NEAR(a.volume / c.volume, 1.0, c.tol);
        } else {
            ok &= CHECK_MSG(a.boundaryEdges > 0 && a.nonManifoldEdges == 0, name + " should be an open manifold sheet");
        }
        if (ok) tk::pass(name + ": " + std::to_string(m.tcount()) + " tris, volume " + std::to_string(a.volume));
    }
    // parameters really drive the geometry
    float p1[] = {2.0f, 32, 16}, p2[] = {2.0f, 64, 32};
    Mesh s1 = makePrimitive(PrimitiveType::UVSphere, p1, 3), s2 = makePrimitive(PrimitiveType::UVSphere, p2, 3);
    CHECK(s2.tcount() > s1.tcount() * 3);
    CHECK_NEAR(s1.bounds().size().y, 4.0, 1e-3);
    std::vector<Vec2> profile = {{0, 0}, {1, 0}, {0.6f, 1}, {0.9f, 2}, {0, 2}};
    Mesh lathe = makeLathe(profile, 48);
    CHECK(analyzeMesh(lathe).watertight && analyzeMesh(lathe).volume > 0);
    Mesh curve = makeTube({{0, 0, 0}, {1, 1, 0}, {2, 0, 1}, {3, 1, 0}}, 0.2f, 0.05f, 16, 10, true);
    CHECK(analyzeMesh(curve).watertight && analyzeMesh(curve).volume > 0);
    tk::pass("parameters, lathe and tapered curve tube");
}

static void bvh() {
    tk::section("BVH raycast / closest point vs brute force");
    Mesh m = makeIcosphere(1, 4);
    BVH b;
    b.build(m);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(-1, 1);
    int mismatches = 0, hits = 0;
    for (int i = 0; i < 2000; ++i) {
        Vec3 o{U(rng) * 3, U(rng) * 3, U(rng) * 3}, d = normalize(Vec3{U(rng), U(rng), U(rng)});
        RayHit h = b.raycast(m, o, d);
        float best = 1e30f;
        for (size_t t = 0; t < m.tcount(); ++t) { // brute force Moller-Trumbore
            Vec3 a = m.pos[m.tri[t * 3]], e1 = m.pos[m.tri[t * 3 + 1]] - a, e2 = m.pos[m.tri[t * 3 + 2]] - a;
            Vec3 pv = cross(d, e2);
            float det = dot(e1, pv);
            if (std::fabs(det) < 1e-20f) continue;
            Vec3 tv = o - a;
            float u = dot(tv, pv) / det;
            Vec3 qv = cross(tv, e1);
            float v = dot(d, qv) / det, tt = dot(e2, qv) / det;
            if (u >= 0 && v >= 0 && u + v <= 1 && tt > 1e-7f) best = std::min(best, tt);
        }
        bool bruteHit = best < 1e29f;
        if (bruteHit != h.hit || (h.hit && std::fabs(h.t - best) > 1e-4f)) ++mismatches;
        hits += h.hit;
    }
    CHECK(mismatches == 0);
    CHECK(hits > 100);
    int bad = 0;
    for (int i = 0; i < 500; ++i) {
        Vec3 p{U(rng) * 2, U(rng) * 2, U(rng) * 2};
        ClosestHit c = b.closestPoint(m, p);
        float best = 1e30f;
        for (size_t t = 0; t < m.tcount(); ++t)
            best = std::min(best, length2(closestPointOnTriangle(p, m.pos[m.tri[t * 3]], m.pos[m.tri[t * 3 + 1]], m.pos[m.tri[t * 3 + 2]]) - p));
        if (!c.found || std::fabs(c.dist2 - best) > 1e-5f) ++bad;
    }
    CHECK(bad == 0);
    std::vector<uint32_t> vs;
    b.sphereVerts(m, Vec3(0, 0, 1), 0.3f, vs);
    size_t brute = 0;
    for (auto& p : m.pos) brute += length2(p - Vec3(0, 0, 1)) <= 0.09f;
    CHECK(vs.size() == brute && brute > 0);
    tk::pass("2000 rays, 500 closest-point queries and a sphere query match brute force");
}

static void topology() {
    tk::section("Subdivision / decimation / repair");
    Mesh m = makeIcosphere(1, 3);
    size_t t0 = m.tcount();
    double v0 = analyzeMesh(m).volume;
    subdivide(m, false);
    CHECK(m.tcount() == t0 * 4);
    CHECK(valid(m) && analyzeMesh(m).watertight);
    CHECK_NEAR(analyzeMesh(m).volume, v0, 1e-4);
    Mesh cube = makeCube({2, 2, 2}, 2);
    double cv = analyzeMesh(cube).volume;
    subdivide(cube, true);
    MeshAnalysis ca = analyzeMesh(cube);
    CHECK(ca.watertight && ca.volume < cv && ca.volume > cv * 0.5); // Loop smoothing rounds the cube
    tk::pass("linear subdivide keeps shape (x4 triangles), Loop subdivide smooths");

    Mesh hi = makeIcosphere(1, 6); // 81,920 triangles
    double hv = analyzeMesh(hi).volume;
    for (uint32_t i = 0; i < hi.vcount(); ++i) hi.col[i] = hi.pos[i].y > 0 ? Vec3(1, 0, 0) : Vec3(0, 0, 1);
    size_t got = decimate(hi, 8000);
    MeshAnalysis da = analyzeMesh(hi);
    CHECK_MSG(got <= 8200 && got >= 4000, "decimate target, got " + std::to_string(got));
    CHECK(valid(hi) && da.watertight);
    CHECK_NEAR(da.volume / hv, 1.0, 0.02);
    int red = 0, blue = 0;
    for (auto& c : hi.col) { red += c.x > 0.9f && c.z < 0.1f; blue += c.z > 0.9f && c.x < 0.1f; }
    CHECK(red > (int)hi.vcount() / 3 && blue > (int)hi.vcount() / 3); // colours survive
    tk::pass("decimate 81,920 -> " + std::to_string(got) + " triangles, watertight, volume within 2%, colours kept");

    Mesh plane = makePlane(2, 2, 20);
    size_t pb = analyzeMesh(plane).boundaryEdges;
    decimate(plane, 200);
    MeshAnalysis pa = analyzeMesh(plane);
    CHECK(pa.nonManifoldEdges == 0 && pa.boundaryEdges > 0 && pa.boundaryEdges <= pb);
    CHECK_NEAR(pa.surfaceArea, 4.0, 0.05); // boundary preserved
    tk::pass("decimation preserves open boundaries");

    Mesh open = makeCylinder(1, 2, 32, false);
    CHECK(analyzeMesh(open).holes == 2);
    CHECK(fillHoles(open) == 2);
    MeshAnalysis fa = analyzeMesh(open);
    CHECK(fa.watertight && fa.volume > 0);
    CHECK_NEAR(fa.volume, kPi * 2, 0.08);
    tk::pass("fill holes closes an open cylinder");

    Mesh sph = makeIcosphere(1, 4);
    cutPlane(sph, Vec3(0, 0, 0), Vec3(0, 1, 0), true);
    MeshAnalysis ha = analyzeMesh(sph);
    CHECK(valid(sph) && ha.watertight);
    CHECK_NEAR(ha.volume, 2.0 / 3.0 * kPi, 0.05);
    CHECK(sph.bounds().mx.y < 1e-4f);
    Mesh sph2 = makeIcosphere(1, 4);
    cutPlane(sph2, Vec3(0, 0.3f, 0), Vec3(0.2f, 1, 0.1f), false);
    CHECK(analyzeMesh(sph2).holes == 1);
    tk::pass("plane cut with cap gives a watertight half sphere; without cap leaves one open loop");

    Mesh two = makeIcosphere(1, 2);
    Mesh other = makeCube({1, 1, 1}, 2);
    for (auto& p : other.pos) p += Vec3(5, 0, 0);
    two.append(other);
    CHECK(countComponents(two) == 2);
    auto parts = separateComponents(two);
    CHECK(parts.size() == 2 && parts[0].tcount() + parts[1].tcount() == two.tcount());
    Mesh half = makeIcosphere(1, 3);
    cutPlane(half, Vec3(0, 0, 0), Vec3(-1, 0, 0), false); // keep +X half
    Mesh mir = mirrorMesh(half, 0, true);
    MeshAnalysis ma = analyzeMesh(mir);
    CHECK(ma.watertight && ma.components == 1);
    CHECK_NEAR(ma.volume, analyzeMesh(makeIcosphere(1, 3)).volume, 0.02);
    Mesh flipped = makeIcosphere(1, 2);
    flipNormals(flipped);
    CHECK(analyzeMesh(flipped).invertedNormals);
    tk::pass("components, mirror + weld, flipped-normal detection");

    // defect detection used by the 3D-print panel
    Mesh nm;
    nm.resizeVerts(5);
    nm.pos = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}};
    nm.addTri(0, 1, 2); nm.addTri(1, 0, 3); nm.addTri(0, 1, 4);
    nm.finalize();
    MeshAnalysis na = analyzeMesh(nm);
    CHECK(na.nonManifoldEdges == 1 && !na.watertight);
    Mesh thin = makeCube({2, 2, 0.05f}, 8);
    MeshAnalysis ta = analyzeMesh(thin, 0.5f);
    CHECK(ta.thicknessSamples > 0 && ta.thinSamples > 0 && ta.minThickness < 0.06f && ta.minThickness > 0.04f);
    tk::pass("non-manifold edge and thin-wall detection");

    Mesh shellSrc = makeIcosphere(1, 4);
    for (uint32_t i = 0; i < shellSrc.vcount(); ++i) shellSrc.mask[i] = shellSrc.pos[i].y > 0.2f ? 1.0f : 0.0f;
    Mesh shell = extractMasked(shellSrc, 0.1f);
    MeshAnalysis sa = analyzeMesh(shell);
    CHECK(shell.tcount() > 0 && sa.watertight && sa.volume > 0);
    tk::pass("extract masked region as closed shell (" + std::to_string(shell.tcount()) + " tris)");

    Mesh dup = makeIcosphere(1, 2);
    size_t nv = dup.vcount();
    Mesh soup; // unshared vertices
    for (size_t t = 0; t < dup.tcount(); ++t)
        for (int k = 0; k < 3; ++k) { soup.addVertex(dup.pos[dup.tri[t * 3 + k]]); soup.tri.push_back((uint32_t)t * 3 + k); }
    soup.ensureAttributes();
    soup.pos.push_back(Vec3(9, 9, 9)); soup.ensureAttributes();     // loose vertex
    soup.addTri(0, 0, 1);                                            // degenerate triangle
    weldVertices(soup, 1e-5f);
    CHECK(soup.vcount() == nv && soup.tcount() == dup.tcount() && analyzeMesh(soup).watertight);
    tk::pass("weld removes duplicates, loose vertices and zero-area triangles");
}

static void remesh() {
    tk::section("Voxel remesh and booleans");
    Mesh s = makeIcosphere(1, 4);
    for (auto& p : s.pos) p.x *= 2.5f; // stretched sphere: long thin triangles
    for (uint32_t i = 0; i < s.vcount(); ++i) s.col[i] = s.pos[i].x > 0 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    s.finalize();
    double v0 = analyzeMesh(s).volume;
    RemeshOptions opt;
    opt.voxelSize = 0.05f;
    Mesh out;
    std::string err;
    double t0 = tk::nowMs();
    bool ok = voxelRemesh({&s}, BoolOp::Union, opt, out, &err);
    double ms = tk::nowMs() - t0;
    CHECK_MSG(ok, err);
    MeshAnalysis a = analyzeMesh(out);
    CHECK(valid(out));
    CHECK_MSG(a.watertight, "boundary " + std::to_string(a.boundaryEdges) + " nonmanifold " + std::to_string(a.nonManifoldEdges) +
                                " inconsistent " + std::to_string(a.inconsistentEdges));
    CHECK(out.tcount() != s.tcount());
    CHECK_NEAR(a.volume / v0, 1.0, 0.02);
    CHECK_NEAR(a.bounds.size().x, 5.0, 0.1);
    // uniform edge lengths
    float mx = 0;
    for (size_t t = 0; t < out.tcount(); ++t)
        for (int k = 0; k < 3; ++k) mx = std::max(mx, length(out.pos[out.tri[t * 3 + k]] - out.pos[out.tri[t * 3 + (k + 1) % 3]]));
    CHECK(mx < 0.05f * 3.0f);
    int red = 0, green = 0;
    for (uint32_t i = 0; i < out.vcount(); ++i) {
        red += out.pos[i].x > 0.2f && out.col[i].x > 0.8f;
        green += out.pos[i].x < -0.2f && out.col[i].y > 0.8f;
    }
    CHECK(red > (int)out.vcount() / 4 && green > (int)out.vcount() / 4);
    tk::pass("stretched sphere -> " + std::to_string(out.tcount()) + " tris in " + std::to_string((int)ms) +
             " ms, watertight, volume within 2%, colours transferred");

    // cancellation and memory guard
    RemeshOptions cancel = opt;
    cancel.progress = [](float f) { return f < 0.3f; };
    Mesh untouched = makeCube({1, 1, 1}, 1);
    size_t before = untouched.tcount();
    CHECK(!voxelRemesh({&s}, BoolOp::Union, cancel, untouched, &err) && untouched.tcount() == before && err == "cancelled");
    RemeshOptions tiny = opt;
    tiny.voxelSize = 1e-5f;
    CHECK(!voxelRemesh({&s}, BoolOp::Union, tiny, untouched, &err) && err.find("voxel size too small") != std::string::npos);
    tk::pass("cancellation leaves output untouched; too-small voxel is rejected with a suggestion");

    Mesh a1 = makeCube({2, 2, 2}, 4), b1 = makeCube({2, 2, 2}, 4);
    for (auto& p : b1.pos) p += Vec3(1, 1, 1);
    b1.finalize();
    RemeshOptions bo;
    bo.voxelSize = 0.04f;
    struct B { BoolOp op; double vol; const char* name; } ops[] = {
        {BoolOp::Union, 15.0, "union"}, {BoolOp::Difference, 7.0, "difference"}, {BoolOp::Intersection, 1.0, "intersection"}};
    for (auto& o : ops) {
        Mesh r;
        bool okb = voxelRemesh({&a1, &b1}, o.op, bo, r, &err);
        CHECK_MSG(okb, err);
        MeshAnalysis ra = analyzeMesh(r);
        CHECK_MSG(ra.watertight && ra.components == 1, o.name);
        CHECK_NEAR(ra.volume / o.vol, 1.0, 0.04);
        tk::pass(std::string("cube ") + o.name + ": volume " + std::to_string(ra.volume) + " (exact " + std::to_string(o.vol) + ")");
    }
    Mesh sp = makeIcosphere(1, 4), cyl = makeCylinder(0.4f, 4, 32, true);
    Mesh drilled;
    CHECK(voxelRemesh({&sp, &cyl}, BoolOp::Difference, bo, drilled, &err));
    MeshAnalysis dr = analyzeMesh(drilled);
    double exact = 4.0 / 3.0 * kPi * std::pow(1 - 0.16, 1.5); // sphere with a cylindrical hole (napkin ring)
    CHECK(dr.watertight);
    CHECK_NEAR(dr.volume / exact, 1.0, 0.04);
    tk::pass("sphere minus cylinder: volume " + std::to_string(dr.volume) + " (exact " + std::to_string(exact) + ")");
    Mesh far = makeCube({1, 1, 1}, 2);
    for (auto& p : far.pos) p += Vec3(10, 0, 0);
    Mesh none;
    CHECK(!voxelRemesh({&sp, &far}, BoolOp::Intersection, bo, none, &err) && err == "boolean result is empty");
    Mesh both;
    RemeshOptions coarse; coarse.voxelSize = 0.1f;
    CHECK(voxelRemesh({&sp, &far}, BoolOp::Union, coarse, both, &err) && analyzeMesh(both).components == 2);
    tk::pass("non-intersecting inputs: empty intersection reports an error, union keeps both parts");
}

static void dyntopo() {
    tk::section("Dynamic topology primitives");
    Mesh m = makeIcosphere(1, 3);
    size_t t0 = m.tcount();
    size_t n = splitLongEdges(m, Vec3(0, 0, 1), 0.4f, 0.05f);
    for (int i = 0; i < 4; ++i) n += splitLongEdges(m, Vec3(0, 0, 1), 0.4f, 0.05f);
    m.finalize();
    MeshAnalysis a = analyzeMesh(m);
    CHECK(n > 0 && m.tcount() > t0 && valid(m));
    CHECK_MSG(a.watertight, "split must keep the mesh closed");
    float longest = 0;
    for (size_t t = 0; t < m.tcount(); ++t)
        for (int k = 0; k < 3; ++k) {
            Vec3 p = m.pos[m.tri[t * 3 + k]], q = m.pos[m.tri[t * 3 + (k + 1) % 3]];
            if (length((p + q) * 0.5f - Vec3(0, 0, 1)) < 0.3f) longest = std::max(longest, length(p - q));
        }
    CHECK(longest <= 0.0501f);
    size_t t1 = m.tcount();
    size_t c = collapseShortEdges(m, Vec3(0, 0, 1), 0.4f, 0.12f);
    m.finalize();
    MeshAnalysis b = analyzeMesh(m);
    CHECK(c > 0 && m.tcount() < t1 && valid(m));
    CHECK_MSG(b.watertight, "collapse must keep the mesh closed");
    tk::pass("local refine: " + std::to_string(t0) + " -> " + std::to_string(t1) + " tris; local collapse -> " + std::to_string(m.tcount()));
}

void runGeometryTests() {
    primitives();
    bvh();
    topology();
    remesh();
    dyntopo();
}

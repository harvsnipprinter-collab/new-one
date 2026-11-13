// Minimal test harness shared by the engine test files.
#pragma once
#include "sf/IO.h"
#include "sf/MeshOps.h"
#include "sf/Primitives.h"
#include "sf/Scene.h"
#include "sf/Sculpt.h"
#include "sf/Topology.h"
#include "sf_api.h"
#include <chrono>
#include <cstdio>
#include <string>

namespace tk {

struct State {
    int passed = 0, failed = 0;
    std::string section;
    std::string outDir = "test_output";
};
inline State& state() { static State s; return s; }

inline void section(const char* name) {
    state().section = name;
    std::printf("\n== %s ==\n", name);
}

inline bool check(bool ok, const char* expr, const char* file, int line, const std::string& note = "") {
    if (ok) { ++state().passed; }
    else {
        ++state().failed;
        std::printf("  FAIL %s:%d  %s  %s\n", file, line, expr, note.c_str());
    }
    return ok;
}

inline void pass(const std::string& what) { std::printf("  ok   %s\n", what.c_str()); }

inline double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

inline std::string path(const std::string& file) { return state().outDir + "/" + file; }

} // namespace tk

#define CHECK(x) tk::check((x), #x, __FILE__, __LINE__)
#define CHECK_MSG(x, msg) tk::check((x), #x, __FILE__, __LINE__, (msg))
#define CHECK_NEAR(a, b, tol) \
    tk::check(std::fabs((double)(a) - (double)(b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__, \
              "got " + std::to_string((double)(a)) + " expected " + std::to_string((double)(b)))

void runGeometryTests();
void runBrushTests();
void runFileTests();
void runAcceptanceTests();
void runApiOperationTests();
void runMaskedTransformTests();

// SculptForge 3D engine test runner. Usage: sf_tests [output-directory]
#include "TestKit.h"
#include <filesystem>

int main(int argc, char** argv) {
    if (argc > 1) tk::state().outDir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(sf::toPath(tk::state().outDir), ec);
    std::printf("SculptForge 3D engine tests (output: %s)\n", tk::state().outDir.c_str());
    double t0 = tk::nowMs();
    runGeometryTests();
#ifndef SF_GEOMETRY_ONLY
    runBrushTests();
    runMaskedTransformTests();
    runFileTests();
    runApiOperationTests();
    runAcceptanceTests();
#endif
    std::printf("\n==== RESULT: %d checks passed, %d failed (%.1f s) ====\n", tk::state().passed, tk::state().failed,
                (tk::nowMs() - t0) / 1000.0);
    return tk::state().failed == 0 ? 0 : 1;
}

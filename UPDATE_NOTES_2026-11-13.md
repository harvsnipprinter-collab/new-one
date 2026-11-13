# SculptForge update — 13 November 2026

## Source changes

- Touch: two-finger tap undo, three-finger tap redo; retains camera gestures.
- Masking: bottom toolbar, visible overlay opacity control, partial-mask transforms.
- Transform: Move arrows, Rotate rings, Scale handles, with touch hit-testing.
- Smooth: bounded tangential relaxation to reduce unpredictable vertex movement.
- Mesh merging: Join operation clarifies that Voxel Remesh is required for seamless union.
- Reference images: PhotosPicker from iOS Photos, document picker from Files; drag, size, rotation, opacity, show/hide, lock; stored with project.
- Compiler portability: modified recursive GLB JSON structure for compatibility with Clang.

## Validation in this environment

- Clang C++20 engine build: PASS.
- C++ engine regression tests: PASS (3,539 checks, 0 failures).
- Swift source syntax parsing on Linux: PASS.
- iOS Xcode compile: NOT RUN (no macOS/Xcode available).
- iOS device interaction and Photos picker: NOT RUN (requires iOS device).

## Codemagic / Sideloadly

The included `codemagic.yaml` retains the Metal toolchain download step and an unsigned `iphoneos` archive/IPA packaging workflow. Commit the full project to GitHub, run the workflow, check for compiler errors, and download the IPA from artifacts if it builds. Sideloadly signs it at install time.

## Compatibility and limitations

Existing project files can open because reference-image state is an optional additive JSON field. New projects with embedded images will have larger file sizes. Reference images are screen-space overlays rather than textured 3D planes; currently they do not automatically orient to a front/side camera view.

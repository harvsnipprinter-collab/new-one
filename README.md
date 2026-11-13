# SculptForge 3D

An original 3D sculpting and painting app for iPhone and iPad: a portable C++20 geometry/sculpt engine
with a Swift + Metal iOS front end. No code or assets from any other sculpting app are used.

**Read [Documentation/FEATURE_STATUS.md](Documentation/FEATURE_STATUS.md) first.** It separates what is
implemented *and tested*, what is written but has never been compiled (the whole iOS layer, because this
project was authored on Windows), and what is not implemented at all.

| Part | Location | State |
|---|---|---|
| Geometry, sculpt, topology, history, file engine (C++20) | `Engine/` | Compiled and tested: 1283 automated checks pass |
| Independent export validator (Node.js) | `Tools/validate_exports.mjs` | Run: 24 of 24 files validate |
| iOS app (SwiftUI, UIKit touch input, Metal renderer) | `App/` | Written, **never compiled or run** |
| Xcode project definition (XcodeGen) | `project.yml` | Written, never generated |
| Cloud Mac build that produces the IPA | `.github/workflows/build-ios.yml` | Written, never executed |
| Sample output from the engine tests | `Samples/` | Real files produced by the engine |

## Quick start

* Get an IPA for Sideloadly: [Documentation/BUILD.md](Documentation/BUILD.md), "Build the IPA without a Mac".
* Build and test the engine on Windows, macOS or Linux:
  `bash Scripts/build_engine.sh` then `./build/sf_tests build/test_output`.
* How to use the app: [Documentation/USER_GUIDE.md](Documentation/USER_GUIDE.md).
* Test results: [Documentation/TEST_REPORT.md](Documentation/TEST_REPORT.md).

## Layout

```
App/               Swift app: Model (engine wrapper), Render (Metal), Input (touch/Pencil), UI (SwiftUI panels)
Engine/include     Public engine headers (sf/*.h) and the C API used by Swift (sf_api.h)
Engine/src         Engine implementation
Engine/tests       Test runner: geometry, every brush, files, acceptance scenarios
Engine/benchmarks  CPU benchmark (10k to 2M triangles)
Tools/             Independent file validator
Scripts/           build_engine.sh, make_ipa.sh
Documentation/     Build guide, user guide, feature status, test report, raw results
Samples/           cat_print.stl / .3mf (watertight, 100 mm tall), cat_scene.glb, cat_character.sculptforge
```

## Architecture

The engine has no platform dependencies and is split by responsibility:

* `Mesh`, `BVH`: indexed triangle mesh with per-vertex colour, roughness, metalness and mask; ray casts,
  closest-point and sphere queries; partial refit so stroke cost follows brush size, not mesh size.
* `Primitives`: 15 parametric shapes plus lathe and curve tube.
* `Sculpt`: 25 brushes, mirror and radial symmetry, stroke interpolation, sparse undo diffs.
* `Topology`: Loop subdivision, quadric-error decimation, dynamic topology (local split / collapse),
  voxel remesh and voxel booleans with a manifold guarantee.
* `MeshOps`: mask operations, weld, hole filling, plane cut with cap, shell extraction, print analysis.
* `Scene`: objects, hierarchy, materials, undo/redo with a memory budget.
* `Project`, `IO`: checksummed native format with atomic save; STL, OBJ, PLY, GLB, 3MF.

Swift talks to the engine only through the flat C API in `sf_api.h`. The renderer reads the engine's
vertex arrays directly and re-uploads only the vertex range a stroke touched.


## November 2026 touch, mask and reference-image update

- **References** (photo icon in the top toolbar): **Import from Photos** uses the native iOS photo picker. **Import from Files** accepts supported JPG/PNG/HEIC images. Images are shown above the 3D viewport. Drag an unlocked image to reposition it; use the bottom/side panel to change size, opacity, rotation, visibility or lock. Lock an image to pass touches through to the sculpt. Imported references are embedded in the native `.sculptforge` project file (optimised copies, not external links).
- **Mask toolbar**: select Mask to reveal accessible bottom actions, including Invert, Clear, Blur, Sharpen, Grow and Shrink. Grey geometry is protected; unmasked geometry can be transformed.
- **Masked Transform**: turn on Masked Transform and drag coloured X/Y/Z Move, Rotate or Scale handles; a partial mask keeps protected vertices fixed. Turn it off for whole-object transforms.
- **Two-finger tap** = Undo; **three-finger tap** = Redo; moving two fingers still pans/zooms.
- **Join/Remesh**: joining objects does not fuse intersecting surfaces. For two intersecting spheres, Join in Objects, then Voxel Remesh in Topology, then Smooth.

The portable C++ engine has automated tests (including mask-aware transformations), but **the SwiftUI/Metal iOS app must still be compiled and tested on a Mac/Xcode or Codemagic before installing**. A successful Xcode build is not equivalent to an on-device usability test.

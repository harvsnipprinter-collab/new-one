# Test report

Run on 2026-11-13 on a Windows 11 laptop (Intel Core 5 120U, 16 GB RAM), clang 23.1.4 (LLVM-MinGW),
`-O2 -Wall -Wextra -Werror`. Raw output is in `Documentation/results/`.

## What was run

| Suite | Command | Result |
|---|---|---|
| Engine tests | `./build/sf_tests build/test_output` | **1283 checks passed, 0 failed** (4.4 s) |
| Independent file validation | `node Tools/validate_exports.mjs build/test_output` | **24 of 24 files validated** |
| Benchmark | `./build/sf_bench build/bench_output` | Completed, table below |

## What was not run

* Nothing in `App/` (Swift, Metal shaders) has been compiled or executed.
* `project.yml`, the GitHub workflow and `Scripts/make_ipa.sh` have not been executed.
* No rendering test, no simulator or device launch, no touch test (acceptance tests 12, 13, 14).
* No GPU frame time, FPS or on-device memory measurements.
* The engine has only been compiled with one compiler on one OS. It uses only standard C++20.

## Engine test coverage

| Section | What is checked |
|---|---|
| Primitives | 15 shapes: valid, no degenerate or loose geometry; closed shapes watertight, outward facing, volume against the analytic value |
| BVH | 2000 ray casts and 500 closest-point queries against brute force |
| Topology | Subdivision counts and shape, decimation target / watertightness / volume / colour, boundary preservation, hole filling, plane cut with and without cap, mirror, component split, defect detection, shell extraction, weld |
| Voxel remesh and booleans | Watertight result, volume within 2%, uniform edge length, colour transfer, cancel, memory guard, union / difference / intersection volumes against exact values, empty and disjoint inputs |
| Dynamic topology | Local refinement and collapse keep the mesh closed |
| Every brush (25) at 3 resolutions | Stroke reports a change, geometry or attribute changed, nothing changed outside the brush area, no NaN, indices valid, undo restores bit-exact data, redo reproduces it; brush-specific intent for 12 of them |
| Brush behaviour | Smoothing halves roughness, X symmetry is an exact mirror, radial symmetry, masked vertices do not move, pressure scaling, fast-stroke interpolation leaves no gaps, back faces untouched, Move follows the drag, Snake Hook extends geometry |
| Stroke guards | Ray miss, unknown object, locked object, bad brush id; 40 cycles of 6-step undo / redo; scaled and moved objects |
| Project files | Round trip of 3 objects with hierarchy, materials, paint, mask, face groups and UI state; bit flip, truncation, garbage, empty file and future version rejected with the open scene untouched; invalid scene refused without damaging the existing file |
| Import / export | STL, OBJ, PLY, GLB round trips (triangle count, volume, scale, colours); 3MF written; multi-object GLB with materials and transforms; ASCII STL / PLY and polygon OBJ import; error handling |
| API operations | 16 mesh operations through the C API: succeed, stay valid, stay closed, undo and redo exactly |
| Acceptance | Specification tests 1 to 11 and 15 (see FEATURE_STATUS.md for the table) |

## Independent validation

`Tools/validate_exports.mjs` shares no code with the engine. It re-parses every exported file from the
format definitions and checks: STL size / count consistency and facet normals against winding; OBJ and
PLY index ranges; GLB header, chunk lengths, JSON, accessor bounds, POSITION min / max, unit normals,
material ranges; 3MF ZIP structure, CRC-32 of every entry, required parts, unit, index ranges; project
magic, end marker and CRC. It also rebuilds edge connectivity itself to confirm watertightness of the
files that must be solids, including `cat_print.stl` and `cat_print.3mf` (94,160 triangles, 46 x 101 x 45 mm).

It does **not** prove the files open in a particular slicer. `Samples/cat_print.stl` and
`Samples/cat_print.3mf` are there so you can open them in Bambu Studio yourself.

## Benchmark (CPU side only)

Sphere-like mesh with even triangles. "ms/event" is the time to process one touch-move event, including
interpolated dabs, normal updates and the BVH refit.

| Triangles | BVH build (ms) | Ray cast (µs) | Draw r=0.1 (ms/event) | Draw r=0.3 | Smooth r=0.3 | Move r=0.3 | Subdivide (ms) | Decimate 50% (ms) | Voxel remesh (ms, result) | Save (ms / MB) | STL export (ms) | Mesh RAM (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10,092 | 4 | 0.38 | 0.02 | 0.02 | 0.02 | 0.01 | 3 | 7 | 22, 15k tris | 30 / 0.3 | 5 | 1 |
| 99,372 | 24 | 0.29 | 0.08 | 0.09 | 0.11 | 0.11 | 27 | 103 | 170, 149k tris | 39 / 3.2 | 22 | 5 |
| 499,392 | 157 | 0.37 | 0.37 | 0.44 | 0.49 | 0.60 | 133 | 649 | 942, 749k tris | 85 / 16.2 | 79 | 26 |
| 1,002,252 | 302 | 0.40 | 2.83 | 1.11 | 1.05 | 1.44 | not run | 1644 | 2026, 1.5M tris | 170 / 32.5 | 166 | 52 |
| 1,997,568 | 653 | 0.63 | 4.77 | 2.38 | 2.45 | 3.23 | not run | 4087 | refused by the memory guard | 364 / 64.8 | 345 | 103 |

Notes:

* These are laptop CPU numbers, not iPhone or iPad numbers. Expect different absolute values on a device.
* Viewport FPS, GPU frame time and CPU load were not measured (no GPU build exists yet).
* Subdivision was not run above 600k triangles in the benchmark. The app refuses to subdivide above
  6M triangles.
* The voxel grid is capped at 100M cells, which is why the 2M case is refused at that voxel size.
* Dynamic topology rebuilds the BVH on every dab that changes topology, so it is much slower than the
  figures above on dense meshes. It was not benchmarked.

## Defects found by these tests and fixed

1. Voxel remesh produced non-manifold edges at sharp rims (two surface sheets in one voxel). Fixed with
   one vertex per sheet plus removal of ambiguous voxel faces; now verified manifold.
2. Re-parenting an object moved it. Added world-preserving re-parenting.
3. Stroke cost grew with total mesh size (25 to 45 ms per event at 1M triangles). Fixed with a partial
   BVH refit (now about 1 to 3 ms).
4. Mirror on a closed object produced two overlapping copies. It now keeps the positive half and mirrors it.
5. Fill with roughness / metalness was not fully undoable. The material switch is now part of the same
   undo step, and the Material brush records it too.

# Feature status

Status words used here:

* **Tested**: implemented in the C++ engine and covered by automated checks that were run and passed.
* **Written, not run**: Swift / Metal / build configuration that exists but has never been compiled or
  executed, because this project was produced on a Windows PC and iOS code only compiles on macOS.
  Compile errors on the first Mac build are possible.
* **Partial**: a real but reduced version of what the specification asks for.
* **Not implemented**: nothing exists for it. There are no placeholder buttons for these.

The app is **not production ready** and is **not feature-equivalent to commercial sculpting apps**.

## Summary

| Area | Status |
|---|---|
| C++ engine (geometry, brushes, topology, masks, scene, undo, files) | Tested (1283 checks, 0 failures) |
| Exported files re-parsed by an independent program | Tested (24 of 24) |
| Engine performance to 2M triangles (CPU, laptop) | Measured, see TEST_REPORT.md |
| iOS app: UI, touch / Pencil input, Metal renderer | Written, not run |
| Xcode project, GitHub build workflow, IPA packaging | Written, not run |
| Signed, installable IPA | **Not produced** (needs a Mac build plus your Apple ID signing) |

## Engine: tested

| Feature | Notes |
|---|---|
| Primitives | UV sphere, icosphere, cube, rounded cube, cylinder, cone, torus, plane, disc, capsule, tube, pyramid, ring, spiral, prism, lathe, tapered curve tube. Closed shapes verified watertight with correct volume |
| Brushes (25) | Draw, Clay, Clay Buildup, Inflate (Deflate = inverted), Smooth, Flatten, Polish, Scrape, Crease, Pinch, Move, Elastic, Snake Hook, Nudge, Drag, Twist, Layer, Stamp, Mask (Erase = inverted), Blur Mask, Sharpen Mask, Face Group, Paint, Smudge, Material Paint. Each tested at 1.3k / 20k / 82k triangles: changes geometry, stays local, no NaN, undo and redo exact |
| Brush settings | Radius, strength, 5 falloffs, spacing, lazy smoothing, invert, front-face filter, pressure to strength / radius |
| Stroke engine | Follows the visible surface via ray casts; fast strokes are interpolated with no gaps |
| Symmetry | X, Y, Z mirror in any combination and radial copies, in object space |
| Masking | Brush, clear, invert, blur, sharpen, grow, shrink, cavity, mask all; masked vertices verified not to move or take paint |
| Dynamic topology | Local edge split and collapse during a stroke; verified watertight; undo restores topology |
| Subdivision | Loop (smooth) and linear, x4 triangles, boundaries handled |
| Decimation | Quadric error metric with boundary preservation; colours carried over |
| Voxel remesh | Winding-number voxelisation + manifold surface nets; attributes transferred; progress, cancel, memory guard; output verified watertight |
| Booleans | Union, difference, intersection (voxel based); volumes within 4% of exact on cubes and sphere-minus-cylinder |
| Mesh repair | Weld, fill holes, flip normals, remove degenerate / loose geometry, separate parts, join, symmetrise (mirror the positive half) |
| Cutting | Plane cut with optional cap |
| Extraction | Masked region to a closed shell with thickness |
| Scene | Multiple objects, world-preserving parenting, cycle rejection, visibility, lock, reorder, duplicate, merge, per-object material |
| Undo / redo | Strokes (sparse diffs), mesh operations, object add / remove, properties, grouped steps; depth and memory limits |
| Project file | `.sculptforge`: versioned, CRC-checked, atomic save with read-back verification; corrupt, truncated and too-new files rejected without touching the open scene |
| Export | STL, OBJ (vertex colours), PLY (vertex colours), GLB (objects, transforms, PBR materials, vertex colours), 3MF |
| Import | STL (binary and ASCII), OBJ (polygons, negative indices, vertex colours), PLY (ASCII and binary little-endian), GLB (embedded buffer only) |
| Print analysis | Size in mm, volume, area, open edges, holes, non-manifold edges, inconsistent winding, inverted normals, parts, degenerate triangles, sampled wall thickness |

## iOS app: written, not run

All of this exists in `App/` and calls the tested engine, but none of it has been compiled.

| Feature | Notes |
|---|---|
| Metal renderer | PBR (GGX), up to 4 directional / point lights, hemisphere ambient, ACES tone mapping, exposure, 4x MSAA, transparency pass, background gradient, grid and axes, wireframe overlay, mask overlay, matcap-style / flat / face-orientation modes, off-screen image render |
| Camera | Orbit, pan, zoom, perspective and orthographic, field of view, six standard views, frame all / selected, bookmarks |
| Input | Finger and Apple Pencil strokes with pressure, coalesced samples, two-finger pan / zoom / twist, accidental-stroke cancel, tap to select, hover cursor, trackpad scroll, keyboard undo / redo / save |
| Panels | Brush (search, settings, symmetry, dynamic topology, presets), Objects, Topology, Mask, Paint, Material, Render, Files, 3D Print, Settings |
| Project management | New, open, save, save as, duplicate, rename, delete, thumbnails, autosave, crash recovery prompt, open `.sculptforge` from Files |
| Long operations | Run off the main thread with progress and cancel (remesh, boolean, print prep) |
| Left-handed layout, finger-sculpt toggle, pressure curve, history depth | Settings panel |

## Partial

| Requested | What exists |
|---|---|
| Transform gizmo with coloured handles, snapping, numeric rotation | Drag-to-move / rotate / scale in the view plane plus position and scale sliders. No drawn gizmo handles, no snapping, no numeric rotation entry |
| Robust exact mesh booleans | Voxel booleans only: resolution limited, sharp edges are rounded to the voxel size |
| Cut with box / lasso / polygon | Plane cut only, axis aligned in the UI |
| Voxel remesh at any density | Limited by a 100M-cell grid: roughly 1.5M output triangles on a sphere-like shape. Sharp-edge preservation is not implemented |
| Vertex paint tools | Paint, smudge, fill, material channels. No gradient or blur tool, no saved palettes (fixed 12-colour palette plus the system colour picker) |
| Material system | Base colour, roughness, metallic, opacity, emission. No normal, bump or displacement mapping, no textures |
| Matcap | Computed studio shading, not image-based matcaps |
| Primitive preview before confirming | Parameters are edited in the panel, then added; no live preview |
| Pencil support | Pressure and hover. No tilt handling, no custom palm-rejection settings |
| Performance techniques | BVH with partial refit, partial GPU uploads, background tasks, multithreaded remesh. No frustum culling, level of detail or GPU compute |
| Layer brush | Works within one stroke only (there is no layer system behind it) |
| GLB export | Per-vertex painted roughness / metalness is not written (object-level material values are) |

## Not implemented

* Brushes: Trim, Pose, Rotate, Insert Mesh, VDM. Brush alphas, alpha import, alpha rotation and tiling.
* Stroke modes other than freehand and single stamp (line, rectangle, ellipse, lasso, polygon, curve).
* Multiresolution sculpting (stored subdivision levels with detail propagation).
* Sculpt and paint layers.
* Component editing: vertex / edge / face selection, extrude, inset, bevel, bridge, and related tools.
* Lasso / rectangle / ellipse / gradient masking, mask by curvature, hide masked.
* UV mapping, unwrapping, textures of any kind, texture baking, reprojection between meshes.
* Text primitive, custom polygon / extruded curve editor, editable curve control points in the UI,
  lattice / bend / twist deformers, surface offset tool.
* Shadows, ambient occlusion, HDR environment maps, depth of field, fog, area lights, spotlights.
* Quad-dominant remeshing.
* USDZ, FBX and glTF-text (`.gltf`) support; 3MF import.
* Arrays, scattering, instances, reference images, measurements, object and surface snapping.
* Configurable shortcuts and gesture mapping, favourites toolbar, history replay.
* Xcode UI tests and on-device benchmarks (FPS, GPU frame time).

## Acceptance tests (specification section 41)

| # | Test | Result |
|---|---|---|
| 1 | Sculpt a nose-like protrusion on a sphere | **Pass** (engine) |
| 2 | Smooth brush reduces roughness | **Pass** (engine) |
| 3 | Voxel remesh of a stretched sphere | **Pass** (engine) |
| 4 | X symmetry gives matching deformation | **Pass** (engine, exact mirror) |
| 5 | Head, two eyes, two ears as independent objects | **Pass** (engine) |
| 6 | Different materials on head and eyes | **Pass** (engine data; not rendered) |
| 7 | Painted mark survives save and reload | **Pass** (engine) |
| 8 | Subdivision increases polygon count | **Pass** (engine) |
| 9 | Undo restores prior geometry | **Pass** (engine, bit-exact) |
| 10 | Save, close, reopen, compare | **Pass** (engine) |
| 11 | STL export checked by an independent parser | **Pass** (Node validator) |
| 12 | Render with two materials and a light | **Not run** (needs Metal) |
| 13 | Build and launch on iOS Simulator or device | **Not run** (needs macOS + Xcode) |
| 14 | Real touch interaction | **Not run** (needs a device or simulator) |
| 15 | Complex multi-object character | **Pass at engine level**: built through the same C API the app calls, not through the touch UI |

## What is needed for a signed IPA

1. A Mac build. Either the GitHub workflow in this project or Xcode on a Mac (see BUILD.md).
2. Fixing any Swift / Metal compile errors that first build reports.
3. Signing with an Apple ID. Sideloadly does this when it installs the unsigned IPA.

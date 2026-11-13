# SculptForge 3D user guide

This describes the app as written. The iOS layer has not been run yet (see FEATURE_STATUS.md), so read it
as intended behaviour; the engine functions behind every control are tested.

## Screen layout

* **Viewport** fills the screen.
* **Top bar**: undo, redo, Sculpt / Transform switch, one button per panel, project name and triangle
  count, save.
* **Left bar** (right side in left-handed mode): all brushes in Sculpt mode; Move / Rotate / Scale in
  Transform mode.
* **Bottom bar**: brush radius and intensity, Invert, X symmetry.
* **Panels** (one open at a time): Brush, Objects, Topology, Mask, Paint, Material, Render, Files,
  3D Print, Settings.

## Touch controls

| Gesture | Result |
|---|---|
| Apple Pencil, or one finger starting on the selected object | Brush stroke (Sculpt) or drag transform (Transform) |
| One finger starting on empty space | Orbit the camera |
| Tap another object | Select it |
| Two fingers | Pan, pinch to zoom, twist to turn the view |
| Second finger within 0.3 s of starting a stroke | The stroke is cancelled and removed |
| Trackpad scroll / mouse wheel | Zoom |
| Cmd-Z, Shift-Cmd-Z, Cmd-S | Undo, redo, save |

Pencil pressure drives intensity (and optionally radius). Fingers use a fixed medium pressure.
Turning off Settings > "Fingers can sculpt" makes fingers navigate only.

## A first sculpt

1. The app starts with a sphere of about 82,000 triangles. Pick **Draw** and stroke on it.
2. Turn on **Invert** to carve instead of add. **Smooth** relaxes the surface. **Move** and
   **Snake Hook** pull shapes out.
3. Turn on **Sym X** to work on both sides at once.
4. For more detail use Topology > **Subdivide**, or Brush > **Dynamic topology** to add triangles only
   where you sculpt.
5. For stretched or tangled triangles use Topology > **Voxel remesh**. The estimate shows roughly how
   many triangles you will get.

## Building a character from parts

* Objects > **Add primitive**: pick a shape, set its parameters, **Add to scene**. New objects appear
  where the camera is looking.
* Switch to **Transform** to place them. Objects > **Parent** attaches eyes, ears or a hat to the head so
  they follow it; an object keeps its place when you re-parent it.
* Topology > **Mirror X / Y / Z** makes an object symmetric: the positive half is kept, copied to the other
  side and welded.
  **Duplicate** makes an independent copy you can move to the other side.
* Clothing: paint a **Mask** on the body, then Mask > **Extract** with a thickness to get a shell you can
  sculpt and paint separately.
* Tick several objects and **Merge ticked** to join them; **Separate parts** splits them again.
* Topology > **Boolean** fuses, subtracts or intersects two objects. It is voxel based: the result is
  rebuilt at the chosen voxel size.

## Masking

The Mask brush darkens the areas it protects. Masked areas do not move when sculpted, smoothed or
grabbed and do not take paint or fills. Mask panel: clear, invert, blur, sharpen, grow, shrink, mask
cavities, mask all.

## Painting and materials

* Paint panel: choose a colour, then the **Paint**, **Smudge** or **Material** brush. **Fill** floods the
  unmasked surface.
* The Material brush can also paint roughness and metalness per vertex.
* Material panel: per-object base colour, roughness, metallic, opacity, emission, and presets (clay,
  plastic, gold, glass, skin, fabric and others).

## Rendering

Render panel: PBR / Matcap / Flat / Face-side shading, orthographic camera, field of view, standard
views, camera bookmarks, up to four lights with presets, ambient sky and ground colours, exposure,
background gradient, grid, wireframe. **Render and share** renders the current view up to 3840 px wide as
PNG (optionally transparent) or JPEG.

## Files

* **Save** writes a `.sculptforge` project to the app's Documents folder (visible in the Files app).
* A recovery copy is autosaved while you work, never during a stroke. After a crash the app offers to
  recover it.
* **Import**: STL, OBJ, PLY, GLB. **Export**: STL, OBJ, PLY, GLB, 3MF through the share sheet, which
  includes Save to Files.

## Preparing a 3D print

1. 3D Print > set **millimetres per scene unit**, or enter a target height and let the app compute it.
2. **Analyse** lists size, volume, open edges, non-manifold edges, flipped faces, disconnected parts and
   thin walls. A model that exports is not automatically printable: fix what the report lists.
3. Separate overlapping parts are not one solid. **Merge visible objects into one solid** joins
   everything and rebuilds one closed surface. Save first; undo restores the parts.
4. Export **STL** or **3MF** and open it in your slicer.

| Triangles | Build prim (ms) | BVH build (ms) | Raycast (us) | Draw r=0.1 (ms/event) | Draw r=0.3 (ms/event) | Smooth r=0.3 (ms/event) | Move r=0.3 (ms/event) | Subdivide (ms) | Decimate 50% (ms) | Voxel remesh (ms -> tris) | Save project (ms / MB) | STL export (ms) | Mesh RAM (MB) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10092 | 7 | 4 | 0.38 | 0.02 | 0.02 | 0.02 | 0.01 | 3 | 7 | 22 -> 15116 | 30 / 0.3 | 5 | 1 |
| 99372 | 53 | 24 | 0.29 | 0.08 | 0.09 | 0.11 | 0.11 | 27 | 103 | 170 -> 148960 | 39 / 3.2 | 22 | 5 |
| 499392 | 407 | 157 | 0.37 | 0.37 | 0.44 | 0.49 | 0.60 | 133 | 649 | 942 -> 749084 | 85 / 16.2 | 79 | 26 |
| 1002252 | 1392 | 302 | 0.40 | 2.83 | 1.11 | 1.05 | 1.44 | skipped | 1644 | 2026 -> 1503444 | 170 / 32.5 | 166 | 52 |
| 1997568 | 3421 | 653 | 0.63 | 4.77 | 2.38 | 2.45 | 3.23 | skipped | 4087 | failed: voxel size too small for available memory; use at least | 364 / 64.8 | 345 | 103 |

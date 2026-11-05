# Granny2 files (.skel skeletons, .anim animations) — decoded

All 547 `.skel`/`.anim` files (106 skeletons / 1,176 bones, 451 animations / 12,331 tracks) load with the native C++ reader
(`src/assets/gr2.cpp`, `build/vb_gr2_validate`). Files are standard **Granny 2.x `.gr2` containers** (exporter "Granny Standard Exporter, SDK
2.2.0.0", file version 6) — the same format the engine loads through `granny2.dll`. 

## Container
```
0x00  magic  b8 67 b0 ca f8 6d b1 0f 84 72 8c 7e 5e 19 00 1e
0x10  u32 header_size (0x160) , u32 header_format (0)
0x20  u32 version (6), total_size, crc32, sections_off (from 0x20), num_sections,
      root_type {u32 section, u32 offset}, root_object {u32 section, u32 offset}, u32 tag, u32 extra[4]
sections (44 B each, at 0x20 + sections_off):
      u32 compression (0 none, 1 Oodle0), data_offset, data_size, expanded_size, alignment,
      first16bit, first8bit, reloc_offset, num_relocs, mixed_marshall_offset, num_mixed
relocation (12 B): u32 offset_in_section, u32 target_section, u32 target_offset   (the u32 at that offset becomes a pointer)
```
Relocations are stored uncompressed at `reloc_offset`; section data is Oodle0-compressed (597 compressed sections in the corpus, the rest empty).
Pointers in section data are zero until resolved through the relocation table.

## Self-describing data
The root object's type is given as a **type descriptor array** (32-byte members: `u32 type, ptr name, ptr ref-type, u32 array_size, u32 extra[3], u32 ignored`):
1 Inline, 2 Reference, 3 ReferenceToArray (u32 count + ptr), 4 ArrayOfReferences, 5 VariantReference (type ptr + object ptr), 7 ReferenceToVariantArray
(type ptr + count + ptr), 8 String, 9 Transform (68 B: u32 flags, f32 pos[3], f32 quat[4] xyzw, f32 scaleShear[9]), 10 Real32, 11-20 integer types, 21 Real16.
`gr2.cpp` decodes any object generically from these descriptors (no per-struct code needed). File info root: ArtToolInfo (LightWave 7.5, right (1,0,0) / up
(0,1,0) / back (0,0,-1) = left-handed, UnitsPerMeter 1), ExporterInfo, Textures, Materials, Skeletons, VertexDatas, TriTopologies, Meshes, Models, TrackGroups, Animations.

* **Skeleton**: `Name`, `Bones[]` = `{Name, ParentIndex, Transform (local), InverseWorldTransform (4x4 f32), ...}`. Humans: 30 bones (`Male_Collision`, `Root`, `Pelvis`,
  `R Thigh`, ..., `Head`, `Ponytail_1..3`, `R/L Handle_1` weapon attach, `R/L Fingers_1`).
* **Animation**: `Name`, `Duration`, `TimeStep` (1/30 s), `TrackGroups[]` (one group; `TransformTracks[]` = `{Name (bone name), PositionCurve, OrientationCurve, ScaleShearCurve}`).
  Curve = `{Degree, Knots[], Controls[]}` as arrays of one-member `Real32` variants. **Every curve in the corpus is degree 0** (knots == controls in count; dim 3 position,
  4 quaternion, 9 scale/shear): keyframes at the knot times. `granny2.dll`'s degree-0 samplers just copy the control, so the original plays them as steps;
  `anim.cpp` interpolates linearly (quaternions normalised, shortest arc) for smoothness. Most tracks have 1 key (static), animated bones ~33 keys over the duration;
  tracks whose bone isn't in the skeleton (Weapon_n, Power_Armor, ...) are ignored.

## Matrix conventions (verified)
Matrices are **row-major, row-vector** (`p' = p * M`, translation in the last row). A bone's local matrix is `ScaleShear * Rot(q)^T` with translation in row 3;
`world = local * parentWorld`. Recomputing `inverse(world)` from the local transforms reproduces the stored `InverseWorldTransform` for every bone to 2e-6
(`gr2_parse.py`/numpy check). The quaternion is not conjugated.
Skin matrix for a bone = `InverseWorld * animatedWorld`; a vertex is `sum_i w_i * (v * skin[bone_i])`.

## Units: skeleton = mesh / 10
The skeleton's translations are 10x smaller than the G3D meshes' (a 6.05-tall `HumMNor` mesh vs a 0.55-tall skeleton; bone-weight centroids of the mesh vs bind
positions agree at ~10x, and the walk cycle looks right with `kSkeletonUnitScale = 10`). Without the scale, animated poses stretch into spikes.

## Binding meshes to skeletons
* G3D bone list (tag 0x0E) order differs from the `.skel`; **bind by bone name**. `Vertex::bone[4]/weight[4]` index the mesh's own `Mesh::bones` list.
  Converted meshes: after the bbox, `flag_skinned` then **4 parallel arrays of `nverts * (u32 bone, f32 weight)`** (bone 0xffffffff = unused); raw meshes: the per-vertex
  `nW` list (first 4 kept). All 856 skinned meshes have in-range indices and weights summing to 1.
* 511 skinned meshes bind fully by name (all Hum*, CR_*, WP_*, IT_*); 6 don't (doors "Bone01.." vs a 1-bone skeleton, `HumMSkl/HumFSkl` clavicle/finger/hip names); 288 tiles and
  48 props carry bone lists without any skeleton file (static).
* Skeleton file for a model: same stem (`CR_Cow.skel`); `HumMNor_BodVaultSuit` -> `HumMNor.skel`. Animations: `HumM_*`/`HumF_*` for humans (one set per gender, e.g.
  `HumM_Def_Una_Idle_01`, `HumM_Com_Una_Walk_01`), `<stem>_*` otherwise (`CR_Cow_Def_Una_Idle_01`, `WP_44Revolver_Single_*`, door `..._Open_In_01`).

## Oodle0 (section compression type 1)
Ported from `granny2.dll` (decompile of `FUN_1001d060` = `GrannyDecompressData` type 1, and helpers `1000e0c0/e130/e1c0/e6f0/e7f0/ec30/e690/ddc0/e020`,
`1001ca70/cbe0`) to `src/assets/oodle0.cpp`. **Verified byte-exact** against the real DLL code executed in a Unicorn x86 emulator
(`gr2_oodle0_emu.py`, `gr2_verify_oodle0.py`): 597/597 compressed sections identical.
* Input: 9 header dwords = 3 model-parameter sets `{p0,p1,p2}`, then an arithmetic-coded bit stream (bits taken LSB-first from little-endian dwords; the first
  31 bits, bit-reversed, form the initial code). Output is produced in 3 segments, ending at `stop0 = first16bit`, `stop1 = first8bit`, `stop2 = expanded_size` (the
  section header's fields), each segment with freshly initialised models.
* Decoder: 31-bit range coder (`lo/hi/code`, renormalise while the top bits agree, E3 underflow handling). Symbols come from adaptive frequency models with an
  escape symbol (index 0): frequencies in 16 cumulative buckets (`w[0..15]`), rescaled when the total exceeds 0x3fff, new symbols entered with frequency 2.
  Escaped values are read as uniform numbers.
* LZ layer: per token, a symbol from the length model chosen by the previous token's symbol (65 models; model sizes from `p2`'s four bytes): 0 = literal (literal model, `p1 & 0x1ff`
  alphabet; escape value uniform in `p0 & 0x1ff`), 1..0x3c = match length `sym+1`, 0x3d..0x40 = 128/192/256/512. A match then reads an offset-low symbol
  (alphabet `min(p0>>9, 4)`) and an offset-high symbol (alphabet `p1>>9`), distance `hi*4 + lo + 1`, bytes copied from the already-produced output.
Tools: `gr2_parse.py` (container), `gr2_tree.py` (generic decoder, Python), `gr2_oodle0_emu.py` (oracle; needs `pip install unicorn`), `tools/gr2_test.cpp`.

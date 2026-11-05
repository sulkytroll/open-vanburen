# G3D ("B3D 1.1") format -- reverse-engineering findings (2026-10-21)

**Status: solved.** `src/assets/g3d.cpp` parses 3912 of 3916 `.g3d` files, and
`verify_all.py` independently confirms 3912 are *strictly byte-exact* (parsing
consumes every byte up to the fixed 3-byte footer `0b 0b 01`, nothing left over).

| Category | Pass | Notes |
|---|---|---|
| Tiles    | 2928/2928 (100%) | raw path, 3-byte mesh header |
| Props    | 387/387 (100%)   | 298 converted ("Format B"), 89 raw (v3) |
| Items    | 131/131 (100%)   | |
| Effects  | 13/13 (100%)     | all raw (v3) |
| Critters | 453/457 (99.1%)  | see "Known failures" |
| **Total**| **3912/3916 (99.9%)** | session start (2026-10-19): 376/3916 (9.6%) |

How it was solved: the engine's own reader was decompiled with Ghidra
(`G3D_Mesh::ReadB3D` @0x4db810, `Gfx_G3D_Mesh::LoadMesh` @0x5267e0 in `F3.exe`)
and its literal `fread()` sequence was transcribed, then every hypothesis was
checked against real bytes in Python **before** being ported to C++
(`verify_readb3d.py`, `verify_rawb3d.py`, `verify_all.py`).

## File layout

```
"B3D 1.1 "                      8 bytes magic
version                         u8 == 0x03  ("v3" files)   or   u16 == 0x011c
f32 1.0, u8 4, f32 1024.0, u8 5, lpstr "LEFT_HANDED", u8 unk, i32 -1
tag stream ... mesh tag (0x0F, terminal)
footer 0b 0b 01
```
Tag stream starts at 0x25 (v3) or 0x27. `lpstr` = u16 length + bytes (used for
every name/enum). The old guess that byte 8 selects "Format A vs B" was wrong
as a *mesh* selector -- see below -- it only selects the header version.

### Outer tags (shared by all files)
| Tag | Layout |
|---|---|
| 0x07 material | lpstr name1, lpstr name2 (**the key mesh groups refer to**), 18 f32 (Ambient4 Diffuse4 Emissive4 Specular4 Shininess Alpha), lpstr blend ("OPAQUE"/"ALPHABLEND"/"ADD"), lpstr type ("SILENT"), then optional-properties trailer (below) |
| 0x08 texture  | u8, lpstr name, lpstr filename, u32 w, u32 h |
| 0x0A node     | u8, lpstr name |
| 0x0E bone     | lpstr name, u16 parent, u8 flags (**bitmask**: bit0 = position 3xf32, bit1 = rotation axis+angle 4xf32) |
| 0x0F mesh     | terminal; see below |

Material trailer: `u32 mask`, then **f64 values** per set bit (values per bit 0..5 =
1,1,2,2,3,4), then `u32` (observed 0 or 2). Samples: mask 2 -> 1 double,
6 -> 3, 24 -> 5 (animated textures / UV transforms / blend params). Decoded for the selection discs: bit 4 (3 doubles) = texture rotation (centre u, centre v, speed; `Disc_select_01` -0.05, `Disc_select_02` +0.2); bit 3 (2 doubles, `1.0, 0.0` in `Disc_select_01`) and bits 0 / 1 (`Cone_Laser_01`, mask 3) are not decoded yet.

### Mesh tag -- selected by a flag byte (the engine's "converted" flag)
* **v3 files** (header version 0x03, i.e. all Effects + 89 Props + 2 Items):
  header `u8, u8(flag==0)`, raw path.
* **other files**: header `u8, u8, u8(flag)`; `flag==1` -> converted path,
  `flag==0` -> raw path. **Every Tile is raw**: they were shipped without
  the engine's offline "re-convert" step (the engine even prints
  "has not been re-converted!" for this case), so they carry the larger,
  un-baked encoding.

#### Raw path (flag==0)
```
u32 nverts
nverts * vertex:
    f32 pos[3], f32 norm[3], f32 color[4]                  (40 bytes; engine widens to f64 in memory)
    u32 nUV;  nUV * (f32 u, f32 v)                         [channel 0 = texture UV, channel 1 = lightmap UV]
    u32 nW;   nW  * (u32 bone, f32 weight)
u32 ngroups
ngroups * {
    u32 group_index
    lpstr matkey                    -- matches an outer material's name2
    u32 ntex; ntex * lpstr          -- texture filename(s); 0 => untextured
    u32 unk (0)
    u8  marker  (0xC9 == primitive type 0, triangle list; engine: byte - 0xC9)
    u32 kk                          -- index count (multiple of 3)
    kk * u32 index                  -- 0-based
    kk * u8  flag                   -- always 1
}
```
(The old "Format A" 56-byte vertex / `u8 kk + 3 pad` description was this same
layout with nUV=1, nW=0 -- it is not a separate format.)

#### Converted path (flag==1), "Format B"
```
u32 nverts, u32 stride, byte vertex_blob[nverts*stride]
    vertex (stride 44 seen): f32 pos[3], f32 norm[3], u8 rgba[4], u32 nUV, nUV*2 f32, ...
f32 bbox_min[3], f32 bbox_max[3]
u8  flag_skinned
    if set: u32 must_be_4, then 4 * byte[nverts*8]      (4 parallel per-vertex bone index/weight arrays)
u32 ngroups
ngroups * {
    lpstr name1, lpstr name2
    f32 ambient[4], diffuse[4], f3[4], f4[4], shininess   (17 floats; alpha = diffuse[3])
    lpstr blend, lpstr type
    60 bytes: 8 optional render-state fields (flag byte + floats; always fully present)
    u32 n1; n1 * lpstr          -- texture file ref(s) (e.g. "GD_ammo.tga")
    u32 n2; n2 * lpstr          -- name tokens ("1","-","Default")
}
ngroups * lpstr texname            -- read AFTER all group bodies
u32 indexcount[ngroups]
u32 trianglecount[ngroups]         -- == indexcount/3
ngroups * u16 indices[indexcount]  -- 0-based
[skinned meshes only: u32, u8, then lpstr* until exactly 3 bytes remain]
footer 0b 0b 01
```
Note: `n1`/`n2` were first mistaken for "count100"/"boneWeightCount" with 28-byte
records; 0x1c is just `sizeof(std::string)` in the engine's memory -- the file
stores plain length-prefixed strings.

## Bugs fixed along the way (all verified against real files)
1. **Bone flags** is a bitmask, not an enum (position vec3 was being dropped for flags 1/3, desyncing every later tag).
2. **Material trailer** is variable (mask + doubles); previously assumed fixed 8 bytes.
3. **Format B group `Face::mat`/materials**: group materials were appended after the outer material list so indices pointed at the wrong entries; `Mesh::materials` is now exactly one entry per group (outer material colours resolved by key for raw meshes; `Material::texture_filename` filled per group).

## Known failures (4 files)
* `Critters/CR_Sheep`, `CR_Badger`, `CR_Bear`: header version **0x0135** (newer exporter).
  Tags are re-numbered (material = `0x0f`, texture = `0x10`, node = `0x18`...), the
  material has no `type` string and a 5-byte trailer, and there is one extra header
  byte. The parser rejects them ("unsupported header version"). Would need the
  engine's version-dependent tag table from `ReadB3D`; low value (3 files).
* `Critters/CR_Monsta`: not B3D at all -- a text file starting `# G3D Fi...` (placeholder).

## Human critter texture atlas
`Critters/Hum[MF](Fat|Nor|Str|Wir)_<Part>*.g3d` meshes are UV-mapped into ONE shared 512x512 character atlas, but each part's `.tga` holds only its own
sub-rectangle (e.g. `BodVaultsuit_default_LG.tga` is 256x256 = the atlas' bottom-left quadrant; the mesh UVs only span u 0-0.5, v 0.5-1). Sampling the file
with the raw UVs gives black patches and wrong islands; the correct coordinate is `((u-u0)/su, (v-v0)/sv)` with `su,sv = texsize/512`. Verified by overlaying
the UV wireframe on the textures (islands/holes line up exactly after the remap). Part origins observed (u0,v0): Bod (0,.5) 256px, Bac (.5,.5) 128, Fee (.75,.5) 128,
Han (.75,.25) 128, Hea (.75,0) 128, Sho (.5,0) 128, Hai (.5,.25) 64, Van (.625,.25) 64, Eye (.625,.375) 64, Pon (.5,.375) 64, Bea (.5,.375) 32, Mus (.5,.4375) 32.
Implemented in `vb::humanAtlasRect` (Body fixed at (0,.5); the others use the size-aligned cell containing the UV minimum -- small parts are a heuristic).
The base-body rule covers only `Hum[MF](Fat|Nor|Str|Wir)`; the skeleton monsters `HumMSkl`/`HumFSkl` use `CR_Skeleton_default_LG.tga` whole (UVs span 0-1), so they are not remapped.
Non-human critters, Items, Props and Tiles use their UVs as-is. Meshes with a second material (e.g. prison suit "UpperArms" -> `Skin_HumM_default_LG.tga`)
get the same remap using the first material's texture size -- looks right, not independently verified. `vb_model`: `A` toggles the remap, `-noatlas`.

Hair / beard / stubble transparency (2026-10-28): the file-level material tag (0x07) starts with the material's *shader name* (`BASE_2X` for opaque body / clothing / full beards, `BASE_VERTEX_ALPHA_2X` for hair, stubble and similar) before its key (`Hair_Bald`); `Material::shader` now keeps it (copied to the group materials by key). With `BASE_VERTEX_ALPHA_2X` the transparency is the **vertex alpha doubled** (`HumMNor_HaiBald` 0.05 .. 0.50, `BeaStubble` up to 0.70, `MusStubble` up to 0.55; the textures themselves are opaque dark brown): the bald "hair" is a faint cap and the stubble a light shadow. The model shader multiplies the alpha by `min(1, 2 * vertex alpha)` for those groups and `drawActor` blends them (depth write off). Before this every human drew a solid hair cap and a solid black beard.

## Open questions / next steps
* **Y/Z handedness -- decided in M2.** Raw-path positions are Y/Z-swapped by the parser (Z-up export -> Y-up), converted-path positions
  are kept in file order (already Y-up). Both render upright. The engine is Direct3D (left-handed), so `vb_model` uses a left-handed
  view (same as `vb_map`) and no further conversion: the converted "STOP" sign reads correctly. Raw-path (Tile) chirality is not
  independently confirmed (no text-bearing tile checked); compare against octree geometry if a mirrored tile ever shows up.
* **Converted vertex layout (stride 44 in every shipped file): `f32 pos[3], f32 nrm[3], u8 rgba[4], f32 uv0[2], f32 uv1[2]` -- there is
  NO UV-count word.** (The parser used to read a count at +28, which made all converted-mesh UVs zero; fixed in M2 by rendering textures.
  The structural 3912/3916 verification could not catch this.)
* Vertex `color` is 4 floats (raw) vs 4 bytes (converted); the 8 optional render-state
  fields and `trianglecount` are read but unused.
* Skinning data (per-vertex weights, `flag_skinned` blocks, bone tags) is skipped, not
  exposed in `Mesh` -- needed later for animation (SKEL/ANIM lumps).
* M2 done: `vb_model` renders G3D meshes with per-material textures. Next: placing them in maps (entity templates), skeleton/animation.

## Tooling notes
* The `rea` MCP connector's Ghidra provider needs `GHIDRA_INSTALL_DIR` (Ghidra 12.1.4 at
  `~/ghidra`); the already-running MCP server lacked it. Workaround:
  `research/ghidra/rea.sh <cmd> ...` runs the `rea` CLI with the variable set. Each invocation
  re-runs Ghidra auto-analysis (~2 min, no caching), so batch lookups and run them in
  the background (`research/ghidra/rea_batch*.sh`). Raw outputs are in `rea_out/` (set `REA_OUT`)
  (`decomp_readb3d_1.c`, `decomp_loadmesh_idx.c`, `decomp_FUN_*.c`,
  `function_readb3d_full.json` = ReadB3D assembly).
* Decompiler pitfall that cost time: Ghidra dropped stack arguments at the call sites in
  `ReadB3D`, so the per-vertex reader looked like it did no file I/O. The assembly showed
  `PUSH ESI` (the FILE*) before `CALL 0x4c9e00` -- always check the assembly when a
  decompiled call has suspiciously few arguments.
* Verification scripts: `verify_all.py` (strict, whole corpus), `verify_readb3d.py`
  (converted path + outer tags), `verify_rawb3d.py` (raw path, single file), and
  `run_corpus.py` (drives the C++ `vb_g3d_validate`).

## Texture alpha only means transparency for non-OPAQUE materials
Many textures carry an alpha channel that is NOT transparency (e.g. `PS_Tech.tga`, used by all `DS_Tech_Props`, is ~0 everywhere: alpha there is a
spec/glow side-channel). Alpha-testing those made the props vanish. The material's blend string (`OPAQUE`, ALPHATEST/ALPHABLEND/ADD ...) is now kept in
`Material::blend_name`; renderers must ignore texture alpha when it is `OPAQUE` (as the octree materials already do in `vb_map`).


## Raw-path scale and axes (fixed 2026-10-21)
`LoadMesh` (`FUN_005267e0`) takes a **scale argument** (via the scene loader `FUN_00527fd0`, `param_4`, multiplied into every position) and a swap flag. Raw-path meshes
(all Tiles, and the older **v3** props/effects) are stored at **1/10 of world scale**, so the parser multiplies their positions by 10 (`kRawScale`):
* 10x10 tiles are 1.00 wide in the file (-> 10 units, matching the map's 10-unit grid); Vault `Door01` is 0.62 x 0.78 vs a measured 6.0 x 7.5 opening in the octree;
  `DS_Mesa_Console` 0.59 tall (-> 5.9, a human-height console); skeletons are 1/10 too (`kSkeletonUnitScale`). Converted meshes already carry world scale.
* Axes: non-v3 raw meshes (Tiles) are **Z-up** (Y/Z swapped by the loader, as the engine does for flag 0); **v3** meshes are **Y-up** (no swap) -- swapping them laid
  cars and consoles on their sides. Normals follow the same axis mapping.
* Before this fix every v3 prop in the maps (all Vault doors, the Mesa console, the Vault cog door `DS_Mesa_VEntrance`, vehicles) was 10x too small and wrongly oriented.

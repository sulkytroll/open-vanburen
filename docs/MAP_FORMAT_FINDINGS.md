# Map formats -- reverse-engineering findings (2026-10-21)

A map ("level") is three lumps sharing a name, plus baked lightmap textures, all in the `Maps` group
(one map, `Default_StartMap`, is in `Engine`):

| Lump | Type | Role | Status |
|---|---|---|---|
| `NN_Name.map` | 1300 MAP | entities, triggers, sounds, effects, waypoints, nav graph, map settings | **38/38 exact** |
| `Name.tre`    | 400 TRE  | baked static level geometry: materials, textures, vertices, octree of triangle lists | **39/39 exact** |
| `Name.col`    | 1700 COL | collision/walk grid (RLE) | **39/39 exact** |
| `Name_N.tga`  | 200 TGA  | baked lightmaps (referenced from the octree as `Name_N.ctx`) | plain TGA |

Strict verification: `python3 verify_maps.py` (every chunk framed exactly, every decoder consumes its payload
exactly, octree traversal ends exactly at EOF, triangles == FLGS entries, indices in range).
Ground truth came from the engine: `Map::Create` (0x57b8a0) -> map-file reader (0x574f60) -> per-chunk virtual
handlers; `OCTREE` loader (0x5b0ad0) and node reader (0x5af800); `Octree2Gfx::ConstructRenderData`.

## `.map` -- EMAP chunk stream

Whole file = chunks, each `tag(4) u32 version u32 total_length` (length includes the 12-byte header; for `EMAP`
the length equals its absolute end offset because it starts at 0). Chunks are length-prefixed so unknown or
partially understood ones can be skipped. Strings are `u16 length + bytes` (same as G3D).
The engine reads the stream in a loop and dispatches on the tag through the `Map` vtable
(slot -> handler): EMAP 3c, EMEP 40, EMTR 44, EMEN 48, EME2 4c, 2MWT 50, ECAM 54, EMFG 58, EMNO 5c, EMNP 60,
EPTH 64, EMSD 68, EMEF 6c. (`EMEN`, `EMFG` are legacy tags not present in the shipped maps.)

| Tag | Payload |
|---|---|
| `EMAP` v5 (first) | `u32 id`; 4 strings (heightmap-ish `.8`, `.rle`, ambient-map texture, 4th always empty); `u8[3]` ambient RGB; `u8` flag; `u8[4]` light RGBA; `u8` flag; `u8` fog enable; `u8[3]` fog RGB; `f32[3]` fog params (250, 500, 1.0 in samples). Fields are version-gated (v>=1..5), all present in v5. |
| `EMEP` | entry point: `u8 id`; 6 x `vec3` party positions; 6 x `f32` angles. (One legacy test map has a 17-byte `id + 4 f32` variant.) |
| `ECAM` | 4 x `f32` camera parameters (defaults `0, 1000, 1000, 0`) |
| `EMNP` | nav-point graph: `u32 n`; n x { `u8 flag`, `vec3 pos`, `f32[3]` (0), `u8[5]` neighbor indices, `0xFF` = none } |
| `EMNO` | map note/icon: `vec3 pos`, string icon (`AreaMapIcon01.dds`), 4 bytes |
| `EPTH` | waypoint path: string name, `u32 n`, n x { `vec3 pos`, `vec3` } |
| `EMSD` | sound emitter: string name, `vec3 pos`, string `.psf` sound, `u8`, `u8` |
| `EMEF` | effect emitter: string name, `vec3 pos`, `vec3 rot`, `u8 n`, n x string effect file (`.veg`), `u8` flag |
| `EMTR` | trigger polygon: `u32 type`, `u32 n`, n x `vec3`; the trigger object then reads its own data from the **next chunk**: type 0 -> `ETTR`, 1 -> `ESTR`, 6 -> `EBTR` |
| `ETTR` v1 | map transition: string destination map, `u8` entry point id, `u8` |
| `ESTR` | script trigger: string `.amx` script, `u32` |
| `EBTR` | boundary trigger: `u32 id`, `u8[3]` RGB (editor colour) |
| `EME2` | placed entity: string entity file (`.DOR/.CRT/.ITM/...`), `vec3 pos`, `vec3 rot` (radians), `u8 flag`, then a nested `EEOV` chunk |
| `EEOV` v1/v2 | per-instance override: string instance name, `u8[3]` flags, `u32`, `u32`, string display name, string script `.amx`, `u32 n` + n x (string, `u32`) script variables, `u8` flag, `f32` (1.0), string skin texture, `u8 m` + m x string inventory item files; v>=1: `u8`, `u8`; v>=2: `u32 k` + k x string (extra items) |
| `2MWT` | water: string name (`EFF_Water`), 140 bytes of parameters (`i32 -1`, floats, flags), `u32 n`, n x { `vec3 pos`, string texture, `vec2 uv` } |

All positions are world units, X/Z horizontal, Y up (matching the octree).

## `.tre` -- octree ("8TRE")

`'8TRE' u32 size`, then chunks `tag(4) u32 length(excl. header) payload`:

```
HEAD  16B : u32 version(2), u32 flags (4 = static lights, 8 = static lighting grid), u32 tree depth, u32 ?
[LGTD { INFO u32 n; n * LGTR(64B) }]                 only if flags&4 -- static lights, DECODED (see "Static lights and light visibility grid")
LVLD  { INFO 16B: u32 w, u32 h, f32 sx, f32 sy ;  LVL0: light-visibility grid }     if flags&8 -- DECODED (below)
MATD  { INFO u32 n ; n * MATR(196B) }
TXTD  { u32 first_lightmap_index, u32 total ; (total-1) * TXTR(zstring) }     texture index 0 = none, 1.. = TXTR order
VTXD  { u32 nbuf ; nbuf * VTXB { u32 nverts ; XYZ f32x3[n], NRML f32x3[n], DIFU u32[n] (colour), TXUV f32x2[n], LMUV f32x2[n] } }
TREE  { preorder NODE/LEAF chunks }
```
* `MATR` (196 B): `u32 shader class` (0 NO_TEXTURE, 2 BASE_2X, 28 SK_BASE_LM_2X = base texture x lightmap, indexes the
  engine's 0x21-entry material table), name[80], RGBA bytes (127,127,127,255), blend string (`OPAQUE`...), surface
  string (`SILENT`/`SAND`/`CONCRETE` -- footstep material), then 12 bytes (floats/flags).
* `TREE`: `NODE` chunk = `INFO` (64 B: `f32 min[3], max[3]`, `u32 vertex_buffer_id`, `u32 flags`, `u32 child_present[8]`);
  the children follow as **sibling chunks in slot order** (only slots whose flag is non-zero exist). `LEAF` chunk =
  `INFO` (32 B: bounds, buffer id, flags) + `IDXD { IDXS (u16 stream), FLGS (u8 per triangle) }`. **Every LEAF is
  followed by 4 padding bytes** (copies of the last FLGS byte) that the TREE length does not count; the file ends
  right after the last leaf's padding.
* `IDXS` stream, all u16: `[0x8000|material, c1, { [0x4000|t, c2, { [layer, n, n * vertex_index, pad word if n is odd] }* ] }* ]`
  where `c1`/`c2` are word counts of the contents. `material` indexes `MATR`; `layer` = primary diffuse texture slot
  (1-based into TXTD, 0 = none); `t` = **1-based lightmap number** (0 = unlit), i.e. texture slot
  `first_lightmap_index + t - 1` (max `t` equals the lightmap count in every map; confirmed visually -- the baked shadows line
  up, `Name_N.ctx` -> `Name_N.tga` in `Maps`). Triangles are plain lists of 3 indices into the leaf's vertex
  buffer. Total triangles == number of FLGS bytes (checked on all 39 files).
* Leaf quirk: the LEAF length never counts the 4 padding bytes, and the contained IDXD length sometimes does (it can
  overshoot the leaf end); the leaf length is authoritative, read sibling chunks by offset up to it, then skip 4.
* Coordinates: vertices lie inside the root node bounds; Y is up, floor near 0.

## `.col` -- collision grid

```
u32 width, u32 height
height * row:  (u32 run, u8 value)* until the runs of the row sum to width
```
No trailing bytes (39/39 files, `col_parse.py`). `value` is a small per-cell type code (the engine does not name
it in code; observed values 47 (0x2f, dominant -- empty/blocked), 1, 2, 0, 5, 9, 44, 27, 38, ...; in indoor maps the
playable floor is mostly 5, in the Mesa test maps 1). Indoors, value 5 is walkable floor and furniture/walls show as distinct values (the overlay in `vb_map -col` lines up exactly with the rooms). Row order: **file row 0 is the minimum-Z edge** (row index = Z cell).
(The engine builds its lists in reverse row order into its own grid space; measured against the octree geometry, the
non-void cells overlap the level footprint best with row = Z cell in 5 of 6 maps tested.)

The grid covers the octree root's XZ extent at **2 cells per world unit**, origin at the root min corner:
`width = 2*(max_x - min_x)`, `height = 2*(max_z - min_z)` (e.g. Vault 384x334 for a 192x167 area). This holds for
31 of 39 maps; 8 test maps (`aarontemp`, `aarontemp2`, `Canyon_Random_01`, `ScottE_Test1/2`, `Test_City_Building01/02`,
`Test_Int_Vault1_5x5`) ship a stale grid of a different size.

## Putting it together (what a map loader needs)
1. Parse `.map` chunks -> entity placements (`EME2`: file, pos, rot), emitters, triggers, entry points, nav graph, fog/light.
2. Parse `.tre` -> `VTXB` arrays (XYZ/NRML/DIFU/TXUV/LMUV), `TXTD` names, `MATR` materials, then walk `TREE`; each leaf
   yields `(material, lightmap-slot, texture, triangle list)` batches over its vertex buffer.
3. Textures: `TXTR` names (`.tga`/`.dds` from `Tiles`/`Props` etc.); `Name_N.ctx` lightmaps are the `Name_N.tga` files in `Maps`.
4. `.col` -> walk grid (2 cells/unit, row index = Z cell from the min-Z edge).
Tools: `map_chunks.py`, `map_decode.py`, `tre_parse.py`, `col_parse.py`, `verify_maps.py` (Python reference);
C++ port in `src/assets/{tre,col,map}.{h,cpp}` (`vb_map_validate` loads all 116 files), viewer `vb_map` (renders all 39 levels).

## Rendering notes (from the working `vb_map` viewer)
* Vertex colour `DIFU` is ARGB with 0x7f7f7f = neutral; the `_2X` shaders double it. Final colour = diffuse x (vertex colour x2) x (lightmap x2).
* UVs are D3D-style (v = 0 at the top row of the image), no flip needed; lightmap UVs are per-vertex `LMUV`, lightmaps clamp.
* Blend strings: `OPAQUE` solid; anything else (`ALPHABLEND`, `ADD`) is drawn after the opaque batches with depth writes off.
* Winding is unknown (the viewer disables culling); X/Z horizontal, Y up, no handedness flip was needed to look right.
* TGA gotcha: pixels are BGR(A), and 16-bit TGA is A1R5G5B5 (the old loader had both wrong; fixed in `tga.cpp`).

## Open questions
* `FLGS` per-triangle flag values (0x00/0x01/0x10...): floor/walkable/etc., used by `RenderFloorCB` ("Floor Triangle Indices").
* HEAD flag bit value 2 (set on some maps, no visible effect on the data), LGTR `aux` word (0..3), `HEAD` 4th word, `ECAM` field meanings, the 8 `EMEP` angles, `EMNP` extra floats.
* `EEOV` semantics (gameplay: script variables, inventory) are decoded structurally, not interpreted.


## Static lights and light visibility grid (LGTD / LVLD) -- DECODED, verified

Verified by `python3 lgt_parse.py` (strict: every grid byte consumed, parities flip, ids in range; 37/37 octrees with a grid) and
cross-checked against the C++ loader (`vb_map_validate`: 197 lights, 1,070,456 non-empty cells, identical). Engine reference:
octree loader 0x5b0ad0 (reads both) and `OCTREE::GetLightVisibility` at 0x5afce0.

**LGTR (64 B) -- one static light** (`Tre::lights`):

| word | meaning |
|---|---|
| 0 u32 | type: 0 = point (166), 1 = spot (31) |
| 1 u32 | `aux` 0..3, meaning unknown (engine adds 1 to it after loading) |
| 2-4 f32 | colour RGB 0..1 |
| 5-7 f32 | position x,y,z |
| 8-10 f32 | direction (unit vector for spots; NaN = unused for points) |
| 11 f32 | intensity / multiplier (1.0, 2.0, 0.2-0.55) |
| 12 f32 | range |
| 13-15 f32 | falloff (1.0), spot inner and outer cone angle in radians (0.436 / 0.524 = 25 / 30 deg); 1,0,0 for points |

**LVLD -- which static lights can reach each cell.** Grid `w x h` cells, `sx` x `sy` world units per cell (always 0.5), covering exactly
the octree root bounds (`w*sx = maxX-minX`, `h*sy = maxZ-minZ`, verified on all 37). The light lists are *visibility* (line of sight), not
distance-limited: ~60% of cells list lights farther than 1.5x their range. Used by the engine to pick which lights affect dynamic objects
(characters, props), since the world itself is lit by the baked lightmaps.
* Cell for a world position: `col = int((x - minX)/sx + 0.5)`, `row = int((maxZ - z)/sy + 0.5)` (rows count down from max Z), `cell = row*w + col`.
* `LVL0` payload = `ceil(w*h/32)` pairs of `(u32 offset, u32 mask)` followed by a u16 stream. Pair `cell>>5` covers 32 cells; mask bit
  `31-(cell&31)` set means that cell has a list. `offset` is a byte offset from the start of the LVL0 payload to the block's first list;
  the lists of a block follow in cell order (blocks themselves are contiguous in the stream).
* A list is a run of u16 with equal bit 15; the next list in the stream has bit 15 flipped; light id = `v & 0x7fff` (index into LGTD).
  Empty-set cells (mask bit clear) have no entry at all.
* 15 octrees have a grid but 0 lights (empty grid, all masks 0); 2 have lights but no grid (flags 4 only).
* Reader: `TreLightGrid::visibleLights(x, z)` in `src/assets/tre.cpp`. The viewer (`M` markers) draws each light (cross + range ring, spots get
  a direction line) and shows "lights here" for the camera position in the HUD.

## Handedness
The engine is Direct3D, i.e. a **left-handed** world (Y up, looking down +Z puts +X on the right). `vb_map` therefore builds its view
matrix left-handed (determinant -1; `lookAt` in `tools/vb_map/main.cpp`); the vertex data is used unchanged. Camera yaw 0 now looks
down +Z (`-cam x y z yaw pitch` follows this). Octree triangle winding is consequently reversed relative to GL's default (culling stays off).
The same applies to G3D meshes (M2): the Y/Z "handedness" question is answered by this -- keep the data as-is and render with the left-handed view.


## Entity templates (.critter .door .item .weapon .armor .ammo .use .con) -- DECODED, loader `src/assets/entity.cpp`
A placed `EME2` entity names a template, e.g. `Critters00_TMBobby.CRT` -> `_CRT/Critters00_TMBobby.critter` (`.DOR/.door .ITM/.item .WEA/.weapon .ARM/.armor
.AMO/.ammo .CON/.con .USE/.use`). Files are a flat chunk stream `tag(4) u32 version, u32 length` where **length includes the 12-byte header**:
* `EEN2` v0: `lpstr model` (the **G3D base name**: `DS_Vault13_Doors{1}Door01`, `GD_WP_223Autoloader`, `HumMNor`, `CR_Cow`; empty for 3 player/NPC templates),
  0-2 `lpstr` icon names (`..._INV.dds`, `..._ACT.dds`), flag bytes, then a nested `EEOV` chunk (same layout as the map's per-instance EEOV: script, default skin texture, ...).
* `GENT` (generic entity), then one kind-specific chunk: `GOBJ`+`GDOR` (door), `GITM` (item/armor), `GCRE` (critter), `GIAR` (armor), weapon/ammo chunks.
* **GCRE** (critters) contains length-prefixed strings: portrait `port_*.dds`, hair name (`HaiMohawk`) + hair texture (`HaiMohawk_blonde_LG.dds`), and worn armor file
  (`ArmorLeather_Outfit.ARM`). **GITM** of an armor lists the part-mesh suffixes it shows (`BodLeatherOutfit HanLeatherOutfit FeeLeatherOutfit ShoLeatherOutfit`).
* All 285 templates parse with every chunk consumed; every model name resolves to a G3D except 3 templates without a model.
* **Human assembly** (`EntityLibrary::parts`): base body `HumMNor` (skin texture = instance EEOV skin, else template default; `<No Text...>` = placeholder, ignored) +
  `HumMNor_<Hair>` (hair texture) + one `HumMNor_<Part>` per armor part. All parts skin to `HumMNor.skel`; UV remap per `G3D_FORMAT_FINDINGS.md` (character atlas).
  **The bare body's material groups that the worn parts cover must not be drawn** (else skin pokes through the clothes): `vb::coveredGroups` hides a body group
  (`Chest Feet ForeArms Hands Head Legs Shins UpperArms Waist`) when >= 80% of its vertices lie within 0.15 units of the worn parts' vertices (leather/vault: Chest, Waist,
  UpperArms, ForeArms, Legs, Shins 100%; prison suit: ForeArms only 44% so they stay bare; Fee* -> Feet 88%; Han* -> Hands 100%). Derived from geometry, not from a file field.
* Rotation: the entity yaw is in `rot[0]` (`rot[1]`,`rot[2]` ~0); D3D-style rotation about Y; models are used in file units, positioned at `pos` (same world as the octree).
* The maps place only 175 entities in total (Junktown 35, Spelunking 37, Vault 21, ...); the bulk of the scenery is baked into the octrees.
* Placement: entity `y` is not always the floor. Doors/props/items use their stored y, but characters and creatures are stored at y=0 while the floor can be lower (Junktown: -1.0), so
  `vb_map` drops skinned characters onto the highest octree surface under (x,z) that is at most 2 units above the stored y (the engine snaps them to the floor; the exact engine rule is not decoded).

## `NO_TEXTURE` octree batches -- resolved: the original looks the same
Octree batches whose material shader is `NO_TEXTURE` (0) have no texture layer, no lightmap, all-zero UVs and per-vertex colours (Junktown's roof gas tanks, the blocky
machine near (122,0,76)). `vb_map` draws them as vertex colour x2 x 0.75 with no lighting. A screenshot of the real tech demo (taken by the user) shows that machine with the
same soft grey concrete body (a 256x256 swatch stretched over large faces) and flat pale panels, so this is how the game itself renders it -- not a parsing or texturing bug.
(Drawing them with factor 1.0 instead of 0.75 saturates to white, so 0.75 is closer; the exact engine factor is unverified.) The static lights / light grid (LGTD/LVLD) may still
affect dynamic objects, but nothing suggests they change this geometry.
Debug helpers: `VB_NOTEX=1` hides these batches; `VB_DEBUG_ENTS=1` prints every placed entity with its model and bounds.

## Collision grid values (seen by rendering Junktown's .col as an image)
The `.col` grid is a surface-type map at 0.5 units/cell: 1 = dirt/open ground, 9 = pavement/concrete, 44 = footprints of props and vehicles (round/rect blobs), 47 = impassable (building walls drawn as thin
outlines, cliffs, outside the map); 5, 12, 16, 23, 27, 31, 34, 38, 42 are rarer floor types (interior floors, ledges, ...). `vanburen` treats 44 and 47 as blocked, everything else walkable.
Entity templates: a critter's `GCRE` chunk lists its worn parts in order (name, optionally followed by that part's texture), e.g. `Default_PC`: HaiBald MusStubble BeaStubble BodPrisonSuit FeePrisonSuit
BacPrisonSuit + `BacPrisonSuit_13_LG.dds` (Vault 13 back piece) VanPipBoyClosed; `EntityTemplate::wear` holds them.
The new-game screenshot (user-provided) is Junktown: the party's entry point 1 is (95.1, 0, 273.9) angle 3.14 and Cpl. Armstrong stands at (84, 289); the original camera looks toward +Z (yaw 0), tilt ~39 degrees (ECAM
(4.8, 46.2, 65.1) on the start map gives a ~39 degree elevation and ~73 distance), and everything outside a lit radius around the party is black (fog of war / unexplored).
Fog of war (vanburen, `F` cycles tech-demo / remembered / off), done in the world/model fragment shaders (`gm::kFogGLSL`): visibility = max(1 - smoothstep(27, 34, ground distance to the player), floor).
The tech demo's behaviour (user's in-game screenshots): the whole static map is shown at reduced brightness with the light disc following the player, while people and objects (entities) vanish outside the disc (floor 0.4 is my estimate from the screenshot; the radii were fitted by eye
to the new-game screen). The 'remembered' style (black unexplored ground, explored ground at 0.5 through an R8 mask at 0.5 units/texel, actors hidden outside the disc) is my own design.

Fog of war and non-world draws (user observation from the tech demo): fire, smoke (particle effects) and NPC dialogue text stay fully visible inside the dimmed/fogged areas, while people and
placed objects vanish outside the lit disc. Whether that is deliberate or an artifact of the unpolished demo is unknown. Plan: when effects (EMEF emitters + `.vfx`) and dialogue/bark text are
implemented, draw them without fog, with a single switch to apply fog to them instead.

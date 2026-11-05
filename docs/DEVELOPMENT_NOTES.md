# Van Buren — Native Linux Port

Native, compilable, **single-player (no networking)** reimplementation of the
Van Buren tech demo that reuses the original assets. Built in C++17 with CMake,
targeting SDL2 / GLEW / OpenGL 4.6 (RTX 4060 Ti).

Reference-only prior art (cloned, not used as a build base):
- `openvb` — C++ reimplementation skeleton (VS2003-targeted, only 3 .cpp files). Good architecture reference.
- `bsimser/Van-Buren-Explorer` — MIT C# tool; source of the `.grp`/`.rht` format spec.

**Project root:** `vanburen/` (this folder).
**Build:** `cmake -S . -B build && cmake --build build -j4`
**Run:** `DISPLAY=:0 ./build/vb_render ...`

---

## Asset formats (VERIFIED — all 7044 assets extracted)

### `.grp` archive
```
int32 vMajor, vMinor, nFiles
nFiles * ( int32 offset, int32 length )
raw data
```
All little-endian.

### `resource.rht` catalog
```
int32 Major, Minor, NumEntries, OffsetToGroupNames, OffsetToResourceNames
NumEntries * ( int32 Version, Index, Type, NameOff, GroupNameOff )
[ OffsetToGroupNames : OffsetToResourceNames ]  -> group-name pool (null-terminated)
[ OffsetToResourceNames : EOF ]                 -> resource-name pool (null-terminated)
```
**Join:** RHT group name == `<group>.grp` filename stem (files live in `<demo>/data/`);
entry `Index` → the lump at that index in the group archive.

### LumpType codes
| 100 G3D | 200 TGA | 300 SMA | 400 TRE | 600 SKEL | 700 ANIM |
|---|---|---|---|---|---|
| 800 INI | 1100 WAV | 1200 SCR | 1300 MAP | 1400 CRITTER | 1500 VFX |
| 1600 GUI | 1700 COL | 1800 ITEM | 2400 WEAPON | 2500 ARMOR | 2600 DOOR |
| 2700 USE | 3500 AMMO | 3600 CON | 3700 TXT | | |

- **G3D** = Granny2 meshes (header `B3D 1.1`)
- **TGA** = standard Targa (8/16/24/32-bit, RLE)
- **WAV** = RIFF PCM

### Extracted assets
`assets/` (the output of `vb_extract`) — 559 MB:
G3D 3916, TGA 1653, WAV 113, MAP 38, ANIM 448, CRITTER 108, SKEL 99, TRE 39, VFX 119, WEAPON 51, ARMOR 22, DOOR 13, AMMO 20, CON 48, USE 18, COL 39, GUI 25, INI 125, SCR 83, ITEM 5, TXT 1, + sound-config types.

---

## Milestones

1. **DONE** — `vb_extract` (`tools/vb_extract`): native extractor. Reads `resource.rht` + all `.grp`, writes every asset out with correct name/type. Verified: 7044 files, valid RIFF/TGA/B3D signatures.
2. **DONE** — `vb_render` (`tools/vb_render`): SDL2/GLEW window, loads a TGA, draws it as a textured quad. Verified on RTX 4060 Ti (OpenGL 4.6) — center of frame shows game art.
3. **M1 DONE, M2 NEXT** — G3D parser (`src/assets/g3d.cpp`) parses 3912/3916 shipped meshes, strictly byte-exact (see `G3D_FORMAT_FINDINGS.md`). Next: wire it into the 3D renderer `vb_model` (`tools/vb_model`, perspective camera + textured triangles, validated against a generated cube).
4. Then — MAP loading → render the actual test level.
5. Then — gameplay: player movement, critters, doors, UI, audio (needs OpenAL).

---

## G3D (Granny "B3D 1.1") format — SOLVED

Full layout, derivation and corpus results are in **`G3D_FORMAT_FINDINGS.md`** (authoritative).
Summary: tag stream (material 0x07 / texture 0x08 / node 0x0A / bone 0x0E / mesh 0x0F), all names
are u16-length-prefixed strings. The mesh tag has a *raw* encoding (variable-length vertices,
u32 indices — every Tile, Effect and "v3" file) and a *converted* encoding (vertex blob + bbox +
per-group embedded materials + u16 indices — most Items/Props/Critters). Ground truth came from
decompiling `G3D_Mesh::ReadB3D` / `Gfx_G3D_Mesh::LoadMesh` in `F3.exe` with Ghidra.

**Corpus status (2026-10-21): 3912/3916 pass** (Tiles 2928/2928, Props 387/387, Items 131/131,
Effects 13/13, Critters 453/457). Failures: `CR_Sheep/Badger/Bear` (header version 0x135, a newer
exporter with re-numbered tags) and `CR_Monsta` (a text placeholder, not B3D).

**Verification tools:** `verify_all.py` (strict byte-exact over the whole corpus — a file passes only
if every byte up to the `0b 0b 01` footer is accounted for), `verify_readb3d.py`, `verify_rawb3d.py`,
`run_corpus.py` (drives the C++ `vb_g3d_validate`). Pattern that worked: verify a hypothesis in
Python against real bytes first, then port to C++.

**Model viewer (M2):** `./run_models.sh` (or `DISPLAY=:0 ./build/vb_model`) opens a **model selector** listing all 3,916 `.g3d` files (`Critters/`, `Items/`, `Props/`,
`Tiles/`, `Effects/`): Up/Down/PgUp/PgDn/wheel/click, type to filter (`items/`, `revolver`), Enter to open. `./run_models.sh GD_WP_50Revolver` opens one directly
(name substring or path). In the viewer: left-drag orbit, wheel zoom, Space auto-spin, R reset, `[`/`]` previous/next in the folder, T textures, W wireframe, C culling,
Y swap Y/Z, A character-atlas UV remap, N normals, H HUD, F1 help, Tab/Esc back to the selector. Options: `-list [substr]`, `-shot out.bmp`, `-yaw/-pitch`, `-assets DIR`. Skinning/bone data is still parsed past but not exposed in `Mesh`.

## MAP / TRE / COL formats — SOLVED, with C++ loaders and a level viewer

A map = `.map` (EMAP chunk stream: entities, triggers, emitters, nav graph, fog), `.tre` (baked octree level geometry:
materials, textures, vertex arrays, triangle batches) and `.col` (RLE collision grid). All **38 maps / 39 octrees /
39 collision grids verify strictly** (`python3 verify_maps.py`) and load in C++ (`build/vb_map_validate`).
Full layouts, engine function addresses and open questions: **`MAP_FORMAT_FINDINGS.md`**.

**Viewer app:** `./run_maps.sh` (or `DISPLAY=:0 ./build/vb_map`) opens a **map selector** listing all 39 levels — Up/Down/PgUp/PgDn,
mouse wheel/click, type to filter, Enter to open. `./run_maps.sh 03_Tutorial_Junktown` opens a map directly. In a level: WASD (ground plane only) + Space/Ctrl
fly (Shift = fast, mouse wheel = speed), right-mouse or arrow keys look, **Tab/Esc** back to the selector, **[ / ]** previous/next map,
M markers (entities/triggers/nav graph/static lights + "lights here" readout), C collision overlay, L lightmaps, T textures, F wireframe, H HUD, F1 help.
Headless screenshot: `-shot out.bmp [-cam x y z yawdeg pitchdeg] [-markers] [-col]`; `-list` prints the map names; `-assets DIR` overrides the asset path.
Loaders: `src/assets/tre.cpp`, `map.cpp`, `col.cpp`; Python references: `map_chunks.py`, `map_decode.py`, `tre_parse.py`, `col_parse.py`.

## The game prototype — `vanburen` (M4 started)
`./run_game.sh [mapname]` (default `03_Tutorial_Junktown`: the party arrives at entry point 1 with Cpl. Armstrong nearby -- matches the tech demo's new-game screen) is the start of the
engine, forked from the map viewer. **Camera** (parameters from the game's own `Engine/sys.ini`: fixed distance 350, 38.5 degree tilt, zoom = field of view 6-15 degrees, pan 96 units/s): WASD pan on the ground, Q/E rotate 90 degrees around the camera target (smooth), mouse wheel zoom (FOV),
Space re-centres on the player. **Player**: the `Default_PC` character (bald, stubble, prison suit, Vault 13 back piece, Pip-Boy); left-click the ground to walk there (A* over the collision grid
at 0.5-unit cells: values 44 = props/vehicles and 47 = walls/void block, one cell of clearance, string-pulled path, walk cycle at 5.4 units/s, stands on the octree floor). Debug: C collision
overlay, M markers, T/L, X wireframe, `[` `]` switch maps. **F cycles the fog of war**: (1, default) the tech demo's style -- the whole map at 40% brightness with a lit disc (about 20-26 units, soft edge) that follows the player; people, creatures and placed objects vanish outside the disc;
(2) 'remembered' -- black beyond the disc, ground you have walked past stays at 50%, actors likewise hidden outside the disc; (0) off. `-fog 0|1|2` / `-nofog` choose the start mode. Test hooks: `-shot out.bmp -click px py -simtime sec -yawq n -zoom d`. Shared code: `tools/common/level.h` (level + entity renderer, picking),
`tools/common/nav.h` (grid + A*); `vb_map` uses the same renderer. Not yet: characters colliding with each other, weapon models and combat effects, sound.

**The world runs the game's own scripts** (`src/game/world.{h,cpp}`, GL-free; details in `GAME_DATA_FINDINGS.md` "Script runtime"): every placed entity gets its `.scr` (OnCreate/OnUpdate/OnMessage/OnOpen/...), the ~110 engine natives the scripts call are implemented
(positions, teams/squads/attitudes, find/match entities, action queues, messages, floating text, journal, globals, doors, triggers), so Cpl. Armstrong gives his scripted intro monologue and follows you, insurgents patrol the map's waypoints, dogs wander, the first-combat trigger
wakes the hidden insurgents, containers list their contents, and the vault-door console opens the door, walks the protagonist in by script (A_MoveToPosition on the player) and the map-transition trigger loads `04_Tutorial_Vault`, where Frank opens a dialogue by himself.
**Click** a person/door/container to walk up and use it; people with a `.sma` dialogue open the conversation window (answer with 1-9 or by clicking; Esc ends it). The mouse cursor is the game's own (`Engine/DefaultCursor.tga`, `Interface/*Cursor.tga`) and changes with what is under it: talk (people with a dialogue), use (doors, containers), pickup (items; clicking picks them up), attack (hostile critters; combat itself is not implemented), wait (while a script drives the player), the stone arrow over the dialogue/journal windows. `-mouse px py` places it for screenshots. The **bottom bar** is the game's own `Actionbar.gui` layout (see `GAME_DATA_FINDINGS.md` "GUI layouts"): weapon slots, party strip with health / fatigue bars, HEALTH / FATIGUE / EVASION counters, action points, six buttons (Inventory I, Character K, Pip-Boy = journal J, Options = Esc menu, End Turn (live only in combat; this was Sneak, which does nothing in the tech demo), Use Skill is a stub) and the message screen on the left (container contents, pickups, "locked" ...). `build/vb_gui_dump -tree Actionbar.gui` prints a layout. **Inventory (I or the bar button)**: the game's own `Inventory.gui` window; drag an item onto a head / body / hand frame to equip it, drag it back to the backpack to put it away, or double-click / right-click; the selected item's description is shown bottom right, and equipped weapons appear in the bar's two weapon slots. Containers and corpses open a `Container.gui` window (click an item to take it, Take All, Done / Esc). Debug flags: `-takeall` (loot whatever `-use NAME` opens), `-equipall`, `-select SLOT`. **Combat** is turn based: it starts with the first attack. Clicking an enemy only walks up to them; to attack, click one of the two **weapon buttons** on the action bar (it is drawn pressed and the cursor becomes the attack crosshair), then click the target (the player walks into range first). An empty gun's button says RELOAD and reloads when clicked. Everything costs action points (shown by the pips and the AP box; the top line says whose turn it is); the **End Turn** button (the former Sneak button, grayed out outside combat) or **Enter** ends your turn. Floating numbers: damage red, health gained green (`+25`), "miss" gray. Equipped weapons are drawn in the right hand (the weapon's own model on the skeleton's `R Handle_1` bone; the held model is the `WP_x` model (not the ground-drop `GD_WP_x`), attached with an identity offset; minigun / flamethrower / rail gun / arraygun / shotgun use the two-handed animation set (`_Def_2HH_Idle` out of combat, `_Com_2HH_*` in combat); critters use the first weapon in their inventory; bodies drop theirs). Effects are the game's own (`Effects/*.vfx`, see `VFX_FORMAT_FINDINGS.md`): each weapon's muzzle flash, tracers, ejected brass, smoke, laser beams and flames, blood / laser / electrical hit effects, the weapon's recoil animation, footsteps, and the map's fires / steam / dust. The protagonist starts with a test kit: machine pistol, .44 revolver, minigun, laser pistol, combat knife, flamethrower, ammo for each, and three first aid kits (equip them in the inventory). Dead people with an inventory are containers: click the body to take their things. At 0 hit points the world fades to gray and blurry over 3 seconds; press **Enter** to restart. **J** journal, **Z** speech text shown/hidden in the fog (shown by default, as in the tech demo).
Headless check of the scripts: `build/vb_world_sim [map] -t seconds [-goto x z] [-use ENTITY] [-talk NAME] [-alltalk]` (`VB_TRACE=<instance|*>` prints every native call). Game test hooks: `-talk NAME`, `-use ENTITY`, `-at x z`.

## Entities and animation — DONE
`vb_map` places the entities (`E` toggles): doors, props, items, weapons and creatures via the entity template files, and humans assembled from body + hair + armor
parts, with idle animations (`P` toggles). `vb_model` plays any animation on a skinned model (`,`/`.` choose, `P` pause; `-anim substr -time t`). Skeletons/animations are native
Granny2 files decoded by `src/assets/gr2.cpp` (+ `oodle0.cpp`, a port of Granny's Oodle0 decompressor verified byte-exact against the original DLL): `GR2_FORMAT_FINDINGS.md`.

## Scripts, strings and game data (decoded and running in `vanburen`)
* **Scripts** (`.scr` entity behaviour, `.sma` dialogue) are Pawn/"Small" bytecode: `AMX_FORMAT_FINDINGS.md`, parser/disassembler `amx_parse.py`, C++ VM `src/assets/amx.{h,cpp}`
  (`build/vb_amx_test` runs every public of all 83 scripts: 978/991 finish cleanly with stubbed natives). The 109 natives the scripts use are implemented in `src/game/world.cpp`; all maps and all 36 dialogue scripts run without VM errors (`vb_world_sim`).
* **Strings** `English.stf` (3281 strings, 1-based ids): `src/assets/stf.{h,cpp}`; engine settings `Engine/sys.ini` (camera, start map, clock). See `GAME_DATA_FINDINGS.md`.

## Environment / gotchas

- Graphics dev pkgs (SDL2, GLEW, OpenGL) installed by user. **OpenAL NOT installed** (needed for the audio milestone).
- No sudo available (no `SUDO_PASSWORD` in `.env`). Ask the user to install any missing package.
- CMake targets: `SDL2::SDL2`, `GLEW::GLEW`, `OpenGL::GL` (NOT `GL::GL` / `SDL2::SDL`).
- Vision backend (local llama-server) may 500 — verify renders by reading framebuffer pixels, not `vision_analyze`.

## Layout

```
vanburen/
├── CMakeLists.txt
├── src/assets/
│   ├── format.h          # LumpType, RhtEntry
│   ├── rht.h/.cpp        # resource.rht catalog
│   ├── grp.h/.cpp        # .grp archive
│   ├── tga.h/.cpp        # TGA loader
│   ├── g3d.h/.cpp        # G3D (Granny B3D 1.1) mesh parser
│   ├── entity.h/.cpp     # entity templates + model assembly;  gr2.h/.cpp + oodle0.cpp: Granny2 skeletons/animations;  anim.h/.cpp: posing
├── tools/
│   ├── vb_extract/main.cpp   # asset extractor
│   ├── vb_render/main.cpp    # 2D texture renderer
│   ├── vb_model/main.cpp     # G3D model viewer + selector
│   ├── common/ui.h           # shared GL shader helpers + bitmap-font text renderer
│   └── vb_g3d_validate/main.cpp # G3D validation tool [NEW]
├── verify_all.py / verify_readb3d.py / verify_rawb3d.py  # byte-exact G3D verifiers
├── tools/vb_map/main.cpp           # level viewer; vb_map_validate loads every map/octree/collision file
├── tools/rea.sh, rea_batch*.sh     # drive the `rea` (Ghidra) CLI; outputs in build/rea_out/
├── G3D_FORMAT_FINDINGS.md          # authoritative G3D format notes
└── granny2.dll / granny2_analysis.gts # Ghidra analysis artifacts
```

## Source files

- Demo: the Fallout 3 Van Buren tech demo folder (F3.exe, data/*.grp, resource.rht, Fonts/, Movies/)
- Prior art: `openvb` and `bsimser/Van-Buren-Explorer` (see the top of this file)

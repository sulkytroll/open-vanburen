# Van Buren Port — Implementation Plan

## Current state (2026-10-21)
- ✅ `vb_extract` — extracts all 7,044 assets
- ✅ `vb_render` — 2D TGA renderer (verified on RTX 4060 Ti)
- ✅ `vb_model` — G3D model viewer + selector (M2 done; `./run_models.sh`)
- ✅ `g3d.h` + `g3d.cpp` — **M1 complete**: 3912/3916 shipped meshes parse, strictly byte-exact
  (Tiles, Props, Items, Effects 100%; Critters 453/457). See `G3D_FORMAT_FINDINGS.md`.
- ✅ `vb_g3d_validate` + `run_corpus.py` — corpus validator (99.9%)

---

## M1: G3D Parser — DONE
Ground-truthed against the engine's own reader (`ReadB3D`/`LoadMesh` decompiled with Ghidra) and
verified with `verify_all.py`. Two mesh encodings (raw / converted) behind one outer tag stream.

Known leftovers (documented, low value): `CR_Sheep/Badger/Bear` use header version 0x135 with
re-numbered tags (3 files); `CR_Monsta` is a text placeholder.

Deferred decisions: Y/Z handedness convention (decide visually in M2); skinning/bone data is parsed
past but not yet exposed in `Mesh` (needed for SKEL/ANIM later).

---

## M2: Wire G3D → `vb_model` — DONE (2026-10-21)
`vb_model <name|file.g3d>` renders real meshes (orbit camera, per-material textures, browse with [ ], T/W/C/Y/N toggles, `-shot`, `-list`).
Fixed a latent parser bug on the way (converted-mesh UVs were all zero). Original goals below.

**Goal:** `vb_model <model.g3d>` renders real game meshes.

- Replace the `.msh` loader with G3D loading.
- Resolve textures from `Material::texture_filename` (per group; filled by `parseG3D`) against the extracted assets directory.
- Keep the perspective camera, textured triangle shader, and screenshot feature.
- Auto-center and scale models to fit the view frustum.

*Deliverable:* `vb_model` renders 10+ diverse meshes (weapons, critters, props) without crashing.

---

## M3: MAP Loader → Level Rendering — loaders + static rendering DONE
**Goal:** Load and render a Van Buren map.

**Done (2026-10-21):** `.map`/`.tre`/`.col` formats solved and verified; C++ loaders (`src/assets/tre.cpp`, `map.cpp`, `col.cpp`,
`vb_map_validate` loads all 116 files); `vb_map` viewer renders all 39 levels with textures and baked lightmaps and overlays
entities/emitters/triggers/nav graph/collision. (Also fixed the TGA loader's BGR / A1R5G5B5 bugs.)

**Remaining:**
- Place `EME2` entities as real models: entity file (`.DOR/.CRT/.ITM`...) -> its model (G3D) via the entity template lumps; needs M2 (G3D in the renderer) and the ITEM/DOOR/CRITTER lump formats.
- Use the collision grid + `FLGS` triangle flags for walking/picking (value meanings still unknown).
- Frustum culling by octree node, emitters (`.veg` effects, `.psf` sounds), triggers/scripts (`.amx`).
- Open: `FLGS` meaning, COL cell values, `ECAM`, handedness/winding of octree triangles (viewer just disables culling).

*Deliverable:* a playable test level (movement, doors, critters) — see M4.

---

## M4: Core Gameplay Systems
**Goal:** Playable prototype.

| Subtask | Status |
|---|---|
| Player movement (click-to-walk, camera pan/rotate/zoom, fog of war) | ✅ `vanburen` (2026-10-21): A* over the COL grid; 44/47 cells block; NPC collision, doors, running still to do |
| Critter spawning / AI | ✅ (2026-10-21) the original `.scr` scripts run in `src/game/world.cpp` (natives, action queues, messages, perception message, teams); turn-based combat (2026-10-23): turn order, action points, weapon buttons attack / reload, enemies and Armstrong fight through the engine's turn AI, death fades to gray; formulas are placeholders, no weapon models / effects / sound |
| Doors (LumpType 2600) | ✅ click to open/close via the script's OnOpen/OnClose, open animation, collision cells open with the door; locked doors; vault-door console sequence |
| Dialogue / journal / triggers / map transitions | ✅ `.sma` dialogue window, quest journal (J), script zones (OnEnter/OnLeave), map transitions with entry points |
| UI/HUD | ✅ action bar, cursors, dialogue, journal; inventory window (equip / unequip by drag, double-click, right-click), container window, held weapons on the bar (2026-10-22). Not yet: PipBoy, Character, Options windows from their .gui, trading, drop to world, weight / weapon stats |
| Audio (OpenAL) | ❌ (OpenAL not installed on box) |

**Order:** movement → doors → critters → UI → audio. Each builds on the prior.

---

## M5: Polish
- Multiple weapon pickups
- Health/ammo HUD
- Sound effects from WAV assets (LumpType 1100)
- Ambient audio via OpenAL
- Loading screen transition from splash to level

---

## Dependencies & Risks

- **G3D** is no longer a blocker (M1 done). Remaining risks there: Y/Z handedness choice and exposing skinning data for animation.
- **MAP format** is now decoded (see `MAP_FORMAT_FINDINGS.md`); remaining work is the C++ port and rendering. Next unknown formats: CRITTER/SKEL/ANIM, GUI, SCR.
- **OpenAL** needs to be installed for audio. No sudo available — user must install manually.
- **CRITTER format** (LumpType 1400) is proprietary; AI/animation data structure is unknown.

---

## Open items (known issues, deliberately deferred)
Fixed on 2026-10-22 (former items 1-5, kept here for the record):
* **Closed door clicked from afar** now walks up to it and opens it. `NavGrid::findPathWithin` finds the nearest (by walking distance) reachable cell from which the target is within reach, so targets standing on blocked cells (closed doors, crates) work; `reachOf` gives the reach per kind (door 6, item 3, others 5). "You can't reach that." now only appears when nothing near the target is reachable (and not while a dialogue interrupts the walk).
* **Fog visibility rule**: people, creatures and loose items are drawn (and pickable) only inside the lit disc; doors, containers and props are scenery: drawn dimly like the map (tech-demo style) or where explored (remembered style). `fogHides()` / `entVisible()` in `tools/vanburen/main.cpp`; `LevelRenderer::setActorFog` sets the shader per model. Unverified against the tech demo: whether it also hides props such as barrels.
* **Cursor and picking respect the fog**: `pickEntity` skips entities `entVisible` says are not visible, so an unseen enemy no longer turns the cursor red or takes clicks. (Speech text in the fog is still the separate Z switch.)
* **Collision with placed objects**: props and containers block their footprint in the walk grid at map load (bounding box shrunk by 0.25, skipping flat things under 2 units tall, pieces over 14 units and slivers under 0.4); the cells are released when the entity is destroyed. Characters are solid for the protagonist: the player is pushed out of any living character closer than 1.5 units. Not done: characters blocking each other (NPCs still walk through one another and through the player; they steer by their scripts), and NPC path planning around the player. Debug: `VB_DEBUG_ENTS=1` prints how many props were blocked and whether the spawn still reaches every door / container / person; `VB_NOPROPBLOCK=1` disables it; `VB_DEBUG_PROPS=1` lists every prop's size.
* **Equipped armor changes the player model**: `EntityLibrary::assemblePlayer(file, bodyArmor, headArmor)`; body armor replaces the template's outfit and back piece, a helmet (or an armor with a head part) replaces the hair. Unequipping restores the prison suit and hair. The model is rebuilt when the equipment changes.

Still open:
6. **Helper items (Security Kit, Tool Kit) and the hand slots** (FIXED 2026-10-25, verified by the user in the tech demo). The kits now go in the hand slots and the action button shows their icon with USE ON; kit button or Use Skill button -> yellow crosshair -> click a target -> small skill menu from the target's own script ("Security - Unlock", "Mechanics - Repair", Cancel). Details in `GAME_DATA_FINDINGS.md` ("Skills"). Open: AP costs / skill rolls of skills, the Medic kit (First Aid Kit in a hand is not required), the message id renumbering guess.
7. **Empty containers** (CUT 2026-10-28: the tech demo does not open empty containers, so the current "It is empty." is correct).
   Likely approach: always open the Container window for a container (not for a dead critter with nothing on it, probably), and let items go in by dragging from the inventory window onto the container's slots (or by a click-to-store). `Entity::cargo` already holds a container's items, so storing is `bag -> cargo` with the same stacking rules; the window has 10 slots (`Container.gui`), so decide what happens with more than 10 items (scrolling or refusing). Check the tech demo for: whether it does this at all, whether the inventory window opens alongside the container, and whether items put in persist when the player leaves the map (cargo is rebuilt from the map on every load today, so it would not).

Fixed on 2026-10-24 (former items 8-13):
* **Open windows take the click** (8): with the inventory, character, options or journal window open, a click that is not on the window or the bottom bar is swallowed (the container window already was modal), so the world never reacts. Walk-by-click works again once the window is closed.
* **Weapon effects follow the barrel** (9): the weapon skeletons keep the barrel on the muzzle bone's Z axis (X is the slide's up), while the effect files are authored with X along the barrel, Y up and Z to the side. `heldAnchor` in `tools/vanburen/main.cpp` now remaps the bone frame (effect X = bone Z, Y = bone X, Z = bone Y). The flame now streams along the barrel and the laser beam runs forward. Debug: `VB_DEBUG_MUZZLE=1` prints each weapon's muzzle axes. Still odd: the victim's `FX_HitLaser` cone (rotation 180/90/0 on a plain yaw anchor) is a tall vertical line; the original probably anchors hit effects on the surface normal.
* **Combat feedback follows the animation** (10): `World::tryAttack` still rolls the hit and spends the AP at once, but queues an `Impact`; damage, the hit reaction, the floating numbers, the log line, death and the script callbacks land when the attack animation reaches its release / damage cue (fists 0.40 s, melee 0.666, one-handed gun 0.533, two-handed gun 0.733), in `World::updateImpacts`. Also fixed on the way: the victim of an attack always joins the combat (a neutral animal was left out, so the fight ended at once).
* **Footprints** (11, 13; revised 2026-10-24 after a second report: they appeared inside the fog and did not match the feet): each print now goes under the foot that is planted (the lower of `L Foot` / `R Foot`) when the walk cue fires, with that foot's left / right picture, and a print is hidden once it lies outside the lit disc (`visibleAt`). Earlier fix: `Rotatable` footprint sprites are laid flat with the toes along the walker's facing (the file's own Rotation only fits the engine's quad layout), and each of the decal's corners now follows the ground height (a 3 x 3 grid) so a slope no longer cuts it off in jagged pieces. `VB_FPDEBUG=1` draws them big and magenta. If glitches remain at footprints, they may come from the dust puff or the fog edge: report them again.
* **Speech in the fog** (12): speech and floating numbers of anyone outside the lit disc are hidden by default (Z shows them); in combat they stay visible for hostile creatures and the player's own side. The speaker's current position decides, not where the text started.
* **Frame rate** (found on the way): `groundHeight` scanned every triangle for every colliding particle; it now uses a grid (`VB_PROF=1` prints per-phase timings).

Still open:
14. **Hit-laser orientation** (needs verification, raised 2026-10-24). The victim's `FX_HitLaser_01` shows a tall thin vertical red line in the engine. Its `cone1` model has Rotation 180 / 90 / 0 on a plain yaw anchor at the victim's chest. The original probably anchors hit effects on the surface normal or along the shot direction, so the cone may be meant to lie along the incoming beam (or be a small spark). Verify against the tech demo (if possible) before changing; compare `FX_HitFire_01` / `FX_HitBlood*` for the same frame convention. Investigation 2026-10-28: `Cone_Light.g3d` is a cone 51.5 long (along its Y axis, from y = -46.5 to 5) and 9 wide; scaled (0.02, 0.1, 0.02) it is a 5-unit long thin cone, and Rotation (180, 90, 0) (pitch 180, yaw 90) keeps its axis vertical, so in any frame whose Y is up it is a vertical beam. The effect has `TargetBodyPart = true`: the engine anchors it on the victim's body part (a bone frame, roughly up along the body), not on the shot direction, so the vertical beam may be exactly what the demo shows (an upward energy flash). Tried: anchoring with X along the shot (the weapon-effect convention): the cone stays vertical, the other hit effects are isotropic (their velocities are symmetric), so it was reverted. Needs a reference: what does the hit look like in the demo (a screenshot of a laser hit would settle it)?
15. **Footprint timing** (CUT 2026-10-28, not important).
16. **Footprint surfaces** (CUT 2026-10-28, not important).
17. **Starting the tech demo directly in the Vault** (FIXED 2026-10-24). `../run_f3_vault.sh` starts a patched copy of the demo (folder `Fallout 3 Van Buren Tech Demo Vault`, original untouched) where Start New Game opens the Vault. Cause of the cold-start crash: the client's set-current-map handler `FUN_00546140` dereferences the `end()` of its map table when the name is missing; the copy's F3.exe ignores that case, and `Engine.grp` has `Start map` changed. Details: `GAME_DATA_FINDINGS.md` ("Vault launcher"). Open: side effects of the guard later in the Vault are unchecked.
18. **Perception message rule** (raised 2026-10-25, to revisit). The port sends message id 1 ("someone is in view") to enemies of the protagonist every 0.5 s and to every other critter within 60 units only once, when the protagonist comes into range (re-armed beyond 70). This is a guess; it fixed Frank's walk back to his door (the handler of neutral NPCs clears their actions). Find the engine's real rule (line of sight? view cone? per-script perception range, `GetPrimaryAttribute` perception) and whether neutrals should be re-notified.
19. **Light field refinements** (raised 2026-10-25 after the field-of-visibility change). Compare against the demo: terrain / cliff heights as occluders (only the collision grid is used), (props no longer block light, per the user), the light point's height (4 units) and edge softness, the update cost (3 ms per cell moved), map light sources (lamps, console lights) and whether the explored memory should keep the shape of the field.

- Building roofs fade out when the player walks in (done, see GAME_DATA_FINDINGS.md "Building roofs"). Open: the demo's real fade time and whether the near (front) walls of the lower storey also fade, `BTM_STORY_INT` interior geometry (probably shown only from inside), back-face culling.
20. **Red outline on the acting enemy in combat** (DONE 2026-10-28 as planned: hull pass in `vanburen`, `outline` / `flatCol` uniforms of the model shader, 0.05 units, red 0.9 / 0.05 / 0.05, culling the back faces of the pushed-out copy because the models' winding is the opposite of GL's default; colour and thickness are guesses, `VB_STARTCOMBAT=1` starts a fight with the nearest enemy for testing). Original note: In the demo, during combat the enemy whose turn it is gets a thin red outline (silhouette edge, about 1 px, slightly glowing). Planned: after drawing the actor, redraw it as a back-face "inflated hull" (vertices pushed out along their normals, front faces culled, flat red, depth test LEQUAL), keyed on `world.currentActor()` when it is a hostile critter; fall back to a screen-space edge pass on an actor-ID mask if the hull shows seams. Check the demo's exact colour and whether other states (targeted, hovered enemy) also use it.
- The "remembered" fog of war (fog mode 2) is disabled for now: F toggles tech-demo fog / off only, `-fog 2` falls back to 1. The code is intact; `VB_FOG_REMEMBERED=1` re-enables the mode (2026-10-28).

21. **Audio** (started 2026-10-28, OpenAL Soft). Done: preset / wav parsing, 3D playback, map sound emitters, effect sounds (footsteps, shots, fires, vents, generator ...), script `AttachSound`. Not heard by the author yet (only verified through the OpenAL wave backend): check volumes and the sequencing / offset guesses. UI sounds (first pass): action bar buttons (`f3_button_down_01` on press, `f3_button_up_01` on release), inventory `InventoryPickUp` (drag start) / `InventoryPutDown` (equip, unequip, swap), `CombatSequenceStart` / `End`. Message-box sounds (`f3_messagebox_open/close`) play when the F1 help or the Esc options / exit prompt opens / closes (the port has no real MessageBox window). Dialogue windows: `CombatSequenceStart` when a conversation starts, `f3_button_down_01` when a reply is chosen (user's choice). To do: toggle, `LevelUp` (no level-up in the world yet), the `ButtonIn/Out` and `btnclick` files, the voice sets for hits / deaths (script variables name them: check how the `g_Sound...` values map and whether `flag` means looping), the weapon sounds that are named only by the executable, door sounds (`SlidingDoorOpen/Close`, `DS_Mesa_VEntrance_Open`), combat sequence start / end, level up, explosion, music beyond the first pass (done: the one shipped piece `MA1` streams on every map; to do: which piece belongs to which map is not in the data, only MA1 ships; no fades/ducking) and group volumes / ducking (`.sndlst`).
22. **Main menu** DONE first pass (2026-10-28): 3D scene with the original camera, rolling-in train, seven buttons (the user's own four-button picture `data/menu_buttons.png`; only Start New Game -> Junktown and Exit Game work, Object Browser DONE 2026-11-05 (every .g3d of the game in a filterable list: type to filter, click or Up/Down to choose, drag to orbit, wheel zoom, the animation list below the model list picks a skeleton animation (default: rest pose; Left/Right also cycle), the wheel scrolls whichever list it is over without dragging the highlight back into view, Space spins, Esc returns to the main menu, which is hidden while the browser is open; the model is normalised to a unit sphere and drawn with the game's own model path; test hooks `-menuclick 2`, `VB_OBJFILTER`, `VB_OBJPICK`, `VB_OBJYAW/PITCH/DIST/ANIM`; the real mouse / keyboard path is untested); Map Browser DONE 2026-11-05, reworked the same day: like the Object Browser the menu is hidden and the maps are listed on the left; the chosen map is shown alone (no player, no audio, no fog of war, no scripts) with the game's own camera (`GameCamera`: WASD pan, Shift twice as fast, Q / E rotate, wheel zoom through a wider fov range 6 - 60 than in play, it starts zoomed out to fit the map; left-drag turns / tilts the view, right-drag pans so the ground follows the cursor), O or the check box under the list shows / hides the map's objects (people, creatures, doors, containers, props; `VB_MAPOBJ=0` starts with them hidden), Up / Down choose, Esc back to the main menu; "Loading ..." is drawn for a frame before the (synchronous) load; test hooks `-menuclick 1`, `VB_MAPPICK=name`, `VB_MAPCAM="x z yaw pitch fov"`), opens when no map is named. To do: Play Tutorial, Load Saved Game, Play With a Friend, Game Options, Play Movies;  lighting match; the train's hand-over from Walk to Idle; character creation (`[CharGen]` in sys.ini).
23. **Single binary on the packaged game (branch `vfs`, started 2026-11-05)**. Goal: `vanburen` is dropped into the tech demo folder (next to `F3.exe`) and reads the original `data/*.grp` + `resource.rht` + `Override/` directly; no `vb_extract` step. Done: `src/assets/vfs.*` (mmap of the archives, 35 ms mount; paths stay `<root>/<Group>/<name>.<ext>`; `Override/` first; ordinary files fall through, e.g. `English.stf`, `Fonts/`; `addEmbedded` serves files compiled into the binary: `tools/vanburen/embedded.cpp` `.incbin`s `data/menu_buttons.tga`), every loader and directory scan goes through it, `vanburen` finds the game folder from `-game DIR`, `$VB_GAME_DIR`, the executable's folder, then the current folder (`-assets DIR -game DIR` still reads an extracted copy, for comparisons). Verified against the fresh download (`../F3_Demo`, byte-identical to the 2003 files): `vb_vfs_test <game> <extracted>` 7044 / 7044 identical; all 39 maps load with identical logs and screenshots in both modes (the only pixel differences: the fps counter, and uninitialised action-point digits on test maps that have no player). Start New Game starts the map named by `Engine/sys.ini` "Start map" (the Vault demo folder, whose only difference from the standard one is that value and so `Engine.grp`, starts in the Vault: verified). Not done: `vb_model`, `vb_map`, `vb_world_sim` and the other tools still default to the extracted copy; static linking of SDL2 / GLEW / FreeType; (the Object Browser is done).
24. **Packaging and CI (2026-11-05)**. `scripts/package_appimage.sh` (linuxdeploy + its appimage plugin, `ALLOW_DOWNLOAD=1` fetches them into `build-appimage/tools/`; 8 MB, 44 bundled libraries: SDL2, GLEW, OpenAL Soft and their dependencies; FreeType, libGL, libc and the display / sound servers come from the system, FreeType because linuxdeploy's exclude list treats it as one) and `scripts/package_tarball.sh` write into `dist/`; `.github/workflows/build.yml` builds every program on Ubuntu 22.04 (old glibc on purpose), packages both, smoke-tests the AppImage (it must print "tech demo folder not found" without a display, which proves every bundled library loads) and on a `v*` tag attaches them to a GitHub Release. Verified locally (glibc 2.39): the AppImage placed next to `F3.exe` and started from another folder finds the demo through `$APPIMAGE`, renders the menu and plays audio through the bundled OpenAL. Not yet verified: the workflow itself on GitHub (GCC 11 on the runner), older distributions, Wayland-only systems.

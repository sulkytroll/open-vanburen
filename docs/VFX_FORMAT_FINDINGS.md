# Effect files (`Effects/*.vfx`, "VEG V1.1") — format notes

Status: **fully parsed**. `vfx_parse.py` reads all 119 shipped files and consumes every byte (`python3 vfx_parse.py ../assets/Effects/*.vfx`; `-v` prints every property, `-names` the property inventory, `-catalog` one line per file). **The engine plays them** (`src/assets/vfx.{h,cpp}` parser, `tools/common/vfxrt.h` runtime, hooks in `tools/vanburen/main.cpp`): particle systems (emitters, forces, colour / alpha / scale ramps, trails, ground collision, triggered effects), models (muzzle flashes, blood / laser cones), sprites (billboards and flat ground decals), lightning and swing trails, for attacks (the weapon's own effect file at the animation's `RL` cue, a hit effect on the victim), held-weapon effects (the flamethrower's pilot flame), footsteps (`FL` / `FR` cues) and the map's effect emitters (`EMEF`: fires, steam, dust). **Not played yet:** sounds (no audio), lights (no dynamic lighting), rain / snow / wind (weather), model effects (recolouring), the gr2 model animations and `InitialVelocityTarget`.
The files are *exports of the engine's effect editor*: self-describing property bags (every value carries its name and type name), so the layout below is certain; the **meaning** of the fields is inferred from their names, the values and the places that reference the files (marked "guess" where it is not certain).

## File layout
```
"VEG V1.1"  u32 effectCount  12 bytes (zero in every file)
effectCount x {  "VFX V1.0"  u64 0  u32 n  n x property  }
u32 m (always 3)  m x property            // group level: Loop (bool), MinStartTime, MaxStartTime (float)
property = u16 nameLen, name, u16 typeLen, type string, i32 -1, u32 valueSize, value bytes
```
"VEG" is the group ("visual effect group": one file = several simultaneous effects), "VFX" one effect. The engine and the data call the files `.veg` (templates, maps, executable), they ship as `.vfx`.

## Value types (by type string)
| type | value bytes |
|---|---|
| `float`, `int` | 4 bytes |
| `bool` | 1 byte |
| `std::string`, `enum X`, `VFX_Target`, `VFX_SnapBone` | `u16 length` + ASCII (enums are stored as their names, e.g. `VFXEFFECTTYPE_MODEL`; an empty `SnapBone` = none) |
| `VFX_Vector`, `VFX_Rotation` | 3 floats (rotation in degrees, guess) |
| `VFX_Color` | 3 bytes R, G, B (unused slots hold 0000ff / ffffff) |
| `VFX_Byte` | 1 byte (alpha 0..255) |
| `VFX_TextureUVs` | 4 floats `u0 v0 u1 v1`: the rectangle of the texture atlas (`FO3_FX.tga`) used |
| `VFX_Resource` | two strings: kind (`tga` texture, `b3d` model, `skl`, `gr2` animation, `wav` sound, ...) and file name (may be empty). `b3d` names are G3D models (`Xsprite_muzzleflash.b3d` = `Effects/Xsprite_muzzleflash.g3d`) |
| `VFX_TriggeredEffect` | 4 bytes: index of another effect in the same file to start (`ffffffff` = none) (guess) |

## Fields every effect has
`Name`, `EffectType`, `Delay` (s after the group starts), `Duration` (s; **-1 = lasts until removed**, ambient effects), `Target` (`VFXTARGET_SELF` the entity / weapon the effect was started on, `_TARGET` the thing hit, `_NONE`, `_EFFECT 0` another effect of the group (trail end points)), `Socket` (`GFX_BONE_ID`: `BONE_NONE`, `BONE_ROOT`, `BONE_WEAPON_1`, `BONE_R_FINGER_1`, `BONE_R_FOREARM`), `SnapBone` (a bone of the target's skeleton: **`FX_Muzzle`** (70 uses), **`FX_Brass`** (14), `Weapon_1` (8), `Bone01..04`), `TargetBodyPart` (aim at a body part of the target: hit effects), `HideIfNoTarget`, `Offset` (vector, relative to the bone), `Rotation`, `RotateWithTarget`, `TriggeredEffect` (the effect is only started by another effect).
Effect counts over the 119 files: particle system 150, sound 64, model 42, sprite 33, collision 21, lightning 11, trail 10, light 9, rain 4, snow 1, wind 1, model effect 1.

## Per type (extra fields, in file order)
* **Particle system** (180 properties): bounding box (`BoundingBoxType` automatic / optimized + `_OptimizedAABBMin/Max`), emitter (`EmitterShape` box / sphere, `Min/MaxDimensions`, `Acceleration1/2` + `RotateAccel1/2WithTarget`, `UseEmitterLifetime`, `EmitterLifetime`, `AutoAdd`, `Min/MaxAddRate` (particles per second), `Min/MaxParticlesPerAdd`, `UseMaxParticles`, `MaxParticles`), look (`Texture`, `TextureUVs`, `Model` (a mesh instead of a sprite), `RenderPhase` `RP_BLEND_ADD` additive / `RP_TRANSLUSCENT` alpha blend / `RP_OPAQUE` / `RP_BLEND_REVSUBTRACT`, `Modulate2x`, `Min/MaxParticleAlpha`), per-particle randoms (`Min/MaxParticleLifetime`, `Size`, `Mass`, `Resistance` (drag), `Min/MaxInitialVelocity` vectors + `Min/MaxInitialVelocityScale`, `InitialVelocityTarget/Socket/SnapBone` (shoot toward another point), `Min/MaxExplosion`, `Min/MaxVibration`, rotation: `RotatableParticles`, `Min/MaxParticleRotation`, `Min/MaxParticleSpin`, `FaceVelocityVector`), `ParticleDeathEffect` (triggered effect when a particle dies), a colour ramp (`NumColorsUsed` <= 8, `Color1..8` with `ColorTime1..8`), and eight "adjust" groups that animate a value over the particle's / system's lifetime: colour, particle alpha, system alpha, particle scale, system scale, `Min/MaxDim X/Y/Z` of the emitter. Each is `Adjust<X>` (bool), `Adjust<X>OverLifetime`, start / final value, `<X>Waveform` (`VFXWAVE_LINEAR`, `_SINE`, `_SAW`, `_SQUARE`), `<X>Freq`, `<X>Phase` (guess: value = lerp(start, final, wave(t * freq + phase)), over the lifetime when `OverLifetime`). Then trails (`ParticleTrailType` none / line, `TrailInterval`, `TrailLifetime`, `TrailRenderPhase`, begin / end colour, alpha and scale, `RibbonTexture`, `RibbonTextureUVs`, `RibbonOrigin`, `RibbonEndPoint`, `RibbonAlignment`) and collision (`PerformCollision`, `FaceNormal`, `CollisionEffect` = triggered effect on impact; brass casings bounce, blood sticks).
* **Model** (56): `Model` (G3D), `Skeleton`, `Animation` (gr2), `LoopCount`, `ScaleX/Y/Z`, `UseShadowVolume`, `IgnoreLighting`, `UseRenderPhase`/`RenderPhase`, and optional animated `Color`, `Emissive`, `Alpha`, `Scale` (same adjust pattern with `Final...`). Muzzle flashes and the flame cone are model effects (`Xsprite_muzzleflash`, `Cone_Muzzleflash_01/02`, `Cone_Blood`, `Cone_Laser_01`, `Cone_Light`, `Disc_select_01/02`, ...).
* **Sprite** (44): `Texture`, `TextureUVs`, `Width`, `Height`, `Rotatable`, `RenderPhase`, `_DisableZBuffer`, animated colour / alpha / scale (`InitialScaleX/Y` -> `FinalScaleX/Y`). Blinking lights, the selection ring.
* **Sound** (17): `PresetFile` (resource, `.psf` sound preset, see the map's `EMSD` emitters), `WAVFile`, `Looping`, `FireAndForget`. 64 of the 150 effect blocks are sounds.
* **Light** (53): `LightType` (`VFXLIGHTTYPE_POINT` only), `Direction`, `Diffuse` + `DiffusePercent`, `Specular`, `Ambient`, `Range`, `Falloff`, `Attenuation0/1/2`, `Theta`, `Phi`, `Priority`, `CastShadow`, animated diffuse / specular / ambient / range.
* **Lightning** (76): bolts between the effect and `EndPointTarget/Socket/SnapBone/Offset`: lifetime, `RepeatCount`/delay, segment density (`Min/MaxSegmentsPerFoot`, `AdditionalSegments`), `Jaggedness`, `Meander`, `AlwaysStrikeTarget`, width (`Min/MaxWidth`, `WidthStyle`, `TipWidthScale`), branching (`BranchProbability`, angles, `MaxBranches`, `MaxBranchLevels`, scales), outer / inner colour and alpha (animated), `InnerGlow`.
* **Trail** (30): a ribbon from the effect to `EndPointTarget...` (`VFXTARGET_EFFECT 0` = another effect of the group): two colour / alpha / width-scale stops, `SegmentLifetime`, `Alignment`, texture.
* **Collision** (19): a force field affecting the group's particles: `CollisionShape` sphere / box, `CollisionType` repel (5) / attract (16), `Dimensions`, `Radius`, `Power` (explosions push smoke away, fires draw it in).
* **Rain** (31), **Snow** (41), **Wind** (31): weather around the camera (`demo_rain` / `demo_snow` / `demo_wind`, the `FX_WeatherRain*`): drops per second, velocities, `EmitterSize`, `GroundYOffset`, `OffsetFromCamera`, `AffectedByWind`; snow flakes sway (`XSin` / `ZSin` amplitude / frequency / phase); wind direction, velocity and gusts.
* **Model effect** (42): recolours the entity's own model while it runs (`ApplyToAttachments`, colour / emissive / alpha animated); used by `FX_State_FloatingEyeBot_Broken`.

## Where effects are referenced
* **Weapons**: every attack mode block (`GWAM`, inside `GIWP` of the `.weapon` template) ends with `u32 mode-name string id` + `u16 length` + the effect file, e.g. `FX_WP_9mmMachinePistol_Single_01.veg` for the single shot, `..._Burst_01.veg` for the burst; knives `..._Thrust_01` / `..._Slash_01`; the flamethrower names `FX_FlamerConeLarge_01.veg` and (in the item's own `EEOV` block) `FX_WP_Flamethrower_Idle_01.veg`, a looping effect that runs while the weapon is held (pilot flame). The mode-name string ids: 330 SINGLE, 349 BURST, 325 / 584 SWEEP, 585 THRUST, 350 MELEE (and 2878 for the revolver's second mode). The first `GWAM` words, now clearer: word 0 attack kind (2 single shot, 1 burst, 6 slash, 8 thrust, 9 throw), word 1 damage type (0 normal, 2 electrical, 4 melee, 5 energy / fire), word 2 shots per burst (1, 6, 9, 12), word 5 a to-hit modifier (-15 for bursts), word 6 damage, words 9 / 10 minimum / maximum melee damage (4-8 thrust, 6-10 slash, 3-5 shiv), word 12 a 16.16 number (15 single, 18 / 20 burst, 8-12 melee: not the range; maybe an AP cost), word 15 the mode-name string id. Weapon groups typically hold the muzzle flash (model effect, `SnapBone FX_Muzzle`, 0.1 s), brass ejection (particle, `FX_Brass`, gravity -10), tracers (particle with acceleration 200 along the barrel, 0.2 s), a smoke puff and the gun sound.
* **Impacts**: the executable chooses the hit effect by name in one function (`F3.exe` 0x45c2xx..0x45c5xx, which also picks the hit-reaction animation and the hit sound): by the hit's damage class (field `+0xa0`, 0..5) and a second value (`+0xa4`): class 0 (probably bullets) -> `FX_HitBloodHighVelocity_01` when `+0xa4 == 5`, else `FX_HitRicochetMetal_01` when the target is not flesh (`+0x9c == 0`), else `FX_HitBloodStandard_01`; class 2 -> `FX_HitElectrical_01`; classes 1, 3, 4 -> `FX_HitBloodStandard_01`; class 5 by `+0xa4`: 1 `FX_HitFire_01`, 2 `FX_HitBloodStandard_01`, 3 `FX_HitLaser_01`. (The meaning of the classes is a guess from the effect names.) Hit effects use `Target = SELF` with `TargetBodyPart` = true: they are started on the *victim*.
* **Footsteps**: the animation cue tables (`Critters/*.ini`, `[ANIM_CUES] Count / CueN / TimeN` in ms) fire `FL` / `FR` (left / right foot) while walking; the executable builds `FX_FootL_Hum_<Surface>_Base_01` / `FX_FootR_...` (surfaces Asphalt, Carpet, Metal, Sand, Wood; also `FloatingEyeBot`). Other cue names seen: `RL` (weapon attacks: probably "release", when the shot / blow starts its effects: 1H gun 533 ms, 2HH gun 733 ms, 1H slash 533 ms, punch 266 ms), `DM` (damage is applied: 1H slash 666 ms, punch 400 ms, 2HH gun 733 ms), `LF`, `FF`, `BF`, `PU`, `UP`, `EP` (feet / pickup / unequip / equip, guesses).
* **Maps**: `EMEF` emitters name effect files (`FX_GroundBlowingDust_01.veg`, `FX_AirVentMist_01.veg`, fires, generators, console lights) and scripts attach them with `AttachVEG`.
* Weather demo files are referenced by name in the executable.

## How the runtime uses it
Lengths, velocities and accelerations in an effect file are multiplied by 10 (like the skeleton bones; model `Scale*` is not), `Offset` is in the anchor bone's space, `RotateWithTarget` rotates emitted velocities with the anchor, particle size `x ScaleAdjust`, `Adjust*` values follow `wave(t * Freq + Phase)` with LINEAR = x, SINE = 0.5 - 0.5 cos(2 pi x), SAW = frac(x), SQUARE = on / off over the particle's (or effect's) life. Particle `Size` is the full width of the quad (half-extent = size / 2, like a sprite's `Width`; using the whole size made the fires and smoke twice too large). Draw order: opaque, reverse-subtract decals, alpha-blended (smoke), then additive (flames), so smoke never hides the fire; the tech demo's burning car shows flames and almost no smoke with this. Ribbon width of a particle trail is a guess (0.35 x the ribbon origin / end distance). `Rotatable` sprites are flat decals on the ground; the others face the camera. Gun groups start at the attack animation's `RL` cue, melee groups at the start of the swing; a hit effect starts at the `RL` / `DM` cue: `FX_HitBloodStandard` (flesh; `HighVelocity` for two-handed guns), `RicochetMetal` (robots), `Electrical`, `Fire` / `Laser` for energy damage. Surfaces for footsteps are a guess (Sand outdoors, Asphalt on cell value 9, Metal in the vault). Debug: `VB_DEBUG_FX=1`, `VB_NOAMBIENT=1`, `VB_FORCEHIT=1` (the player always hits), `VB_FXFLIPV=1` (flip the atlas rows).

## Units (guess)
Offsets, sizes and velocities are small numbers (muzzle offset 0.02, brass size 0.01, tracer acceleration 200): they are probably in the skeleton's own unit (metres), which the port scales by 10 (`kSkeletonUnitScale`) for bone positions; the same factor probably applies to effect distances. Time is in seconds.

## Open questions
Exact blending of `Adjust*` values (linear vs wave over lifetime), whether `Mass` / `Resistance` mean drag, the damage-class and `+0xa4` meanings of the hit table, which cue triggers which effect group, how `Delay` interacts with `MinStartTime` / `MaxStartTime`, and the `.psf` sound preset format.

## Catalogue (`vfx_parse.py -catalog`; `t=delay+duration`, `-1` = until removed; target / bone; resource)
```
FX_AirVentMist_01.vfx: particlesystem "mist1" t=0+-1 self [FO3_FX.tga] | sound "sound" t=0+-1 self
FX_BrassEjectMulti_01.vfx: particlesystem "BrassEjectMulti" t=0+1 target [FO3_FX.tga]
FX_BrassEjectSingle_01.vfx: particlesystem "BrassEjectSingle" t=0+0.5 target [FO3_FX.tga]
FX_ComputerBlinkRed_01.vfx: sprite "blinking light" t=0+-1 target [FO3_FX.tga]
FX_ElectricalTechGeneratorActive_01.vfx: light "light" t=0+-1 self | lightning "electricity1" t=0+-1 self | lightning "electricity2" t=0+-1 self | sound "sound" t=0+-1 self
FX_ElectricalTechGeneratorSmoking_01.vfx: particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke2" t=0+-1 self [FO3_FX.tga] | sound "sound" t=0+-1 self
FX_ExplosionGreenVaporLarge_01.vfx: particlesystem "explode1" t=0+1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+3.1 self [FO3_FX.tga] | particlesystem "smoke2" t=0+3.1 self [FO3_FX.tga] | particlesystem "smoke3" t=0+3.1 self [FO3_FX.tga] | collision "repel" t=0+-1 self
FX_ExplosionGreenVaporMedium_01.vfx: particlesystem "explode1" t=0+0.6 self [FO3_FX.tga] | particlesystem "smoke1" t=0+3.1 self [FO3_FX.tga] | particlesystem "smoke2" t=0+3.1 self [FO3_FX.tga] | particlesystem "smoke3" t=0+3.1 self [FO3_FX.tga]
FX_ExplosionNormalExtraLarge_01.vfx: particlesystem "explode1" t=0+1 self [FO3_FX.tga] | particlesystem "boom1" t=0+0.6 self [FO3_FX.tga] | particlesystem "boom2" t=0.1+0.6 self [FO3_FX.tga] | particlesystem "boom3" t=0.2+0.6 self [FO3_FX.tga] | particlesystem "boom4" t=0.3+0.6 self [FO3_FX.tga] | particlesystem "boom5" t=0.4+0.6 self [FO3_FX.tga] | collision "repel1" t=0+-1 self | particlesystem "smoke2" t=0+3.5 self [FO3_FX.tga] | sound "sound" t=0+-1 self [Explosion.wav]
FX_ExplosionNormalLarge_01.vfx: particlesystem "explode1" t=0+1 self [FO3_FX.tga] | particlesystem "boom1" t=0+0.6 self [FO3_FX.tga] | particlesystem "boom2" t=0.1+0.6 self [FO3_FX.tga] | particlesystem "boom3" t=0.2+0.6 self [FO3_FX.tga] | particlesystem "boom4" t=0.3+0.6 self [FO3_FX.tga] | particlesystem "boom5" t=0.4+0.6 self [FO3_FX.tga] | collision "repel1" t=0+-1 self | particlesystem "smoke2" t=0+3.5 self [FO3_FX.tga] | sound "explosion_sound" t=0+-1 self [Explosion.wav]
FX_ExplosionNormalMedium_01.vfx: particlesystem "explode1" t=0+1 self [FO3_FX.tga] | particlesystem "boom1" t=0+0.6 self [FO3_FX.tga] | particlesystem "smoke2" t=0+3.5 self [FO3_FX.tga] | sound "sound" t=0+-1 self [Explosion.wav]
FX_ExplosionNormalSmall_01.vfx: particlesystem "explode1" t=0+1 self [FO3_FX.tga] | particlesystem "boom1" t=0+0.6 self [FO3_FX.tga] | particlesystem "smoke2" t=0+2.5 self [FO3_FX.tga] | sound "sound" t=0+-1 self [Explosion.wav]
FX_ExplosionPlasmaLarge_01.vfx: particlesystem "explode1" t=0+1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+1.1 self [FO3_FX.tga] | particlesystem "smoke2" t=0+1.1 self [FO3_FX.tga] | particlesystem "smoke3" t=0+1.1 self [FO3_FX.tga] | collision "repel" t=0+-1 self
FX_ExplosionPlasmaMedium_01.vfx: particlesystem "explode1" t=0+0.6 self [FO3_FX.tga] | particlesystem "smoke1" t=0+1.1 self [FO3_FX.tga] | particlesystem "smoke2" t=0+1 self [FO3_FX.tga] | particlesystem "smoke3" t=0+1 target [FO3_FX.tga] | collision "repel" t=0+-1 target
FX_FireExtraLarge_01.vfx (loops): sprite "burn_mark" t=0+-1 self [FO3_FX.tga] | sprite "heat_mark" t=0+-1 self [FO3_FX.tga] | particlesystem "fire1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | light "light" t=0+-1 self
FX_FireLarge_01.vfx (loops): sprite "burn_mark" t=0+-1 self [FO3_FX.tga] | particlesystem "fire1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | light "light" t=0+-1 self
FX_FireMedium_01.vfx (loops): sprite "burn_mark" t=0+-1 self [FO3_FX.tga] | particlesystem "fire1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | light "light" t=0+-1 self
FX_FireOilBarrel_01.vfx: particlesystem "fire1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | light "light" t=0+-1 self | sound "sound" t=0+-1 self
FX_FireOilBarrel_02.vfx: particlesystem "fire1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | light "light" t=0+-1 self | sound "sound" t=0+-1 self
FX_FireSmall_01.vfx (loops): sprite "burn_mark" t=0+-1 self [FO3_FX.tga] | particlesystem "fire1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | light "light" t=0+-1 self
FX_FireTorch_01.vfx (loops): particlesystem "fire1" t=0+-1 self [FO3_FX.tga] | particlesystem "smoke1" t=0+-1 self [FO3_FX.tga] | light "light" t=0+-1 self
FX_FlamerConeLarge_01.vfx: particlesystem "fire" t=0+3.5 self/FX_Muzzle [FO3_FX.tga] | particlesystem "smoke" t=0+3.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+-1 self [AttackFlamer.wav]
FX_FootF_FloatingEyeBot_Asphalt_Base_01.vfx: particlesystem "dust1" t=0+-1 self [FO3_FX.tga]
FX_FootF_FloatingEyeBot_Carpet_Base_01.vfx: particlesystem "dust1" t=0+-1 self [FO3_FX.tga]
FX_FootF_FloatingEyeBot_Metal_Base_01.vfx: particlesystem "dust1" t=0+-1 self [FO3_FX.tga]
FX_FootF_FloatingEyeBot_Sand_Base_01.vfx: particlesystem "dust1" t=0+-1 self [FO3_FX.tga]
FX_FootF_FloatingEyeBot_Wood_Base_01.vfx: particlesystem "dust1" t=0+-1 self [FO3_FX.tga]
FX_FootH_FloatingEyeBot_Asphalt_Base_01.vfx: particlesystem "dust1" t=0+0.7 self [FO3_FX.tga]
FX_FootH_FloatingEyeBot_Carpet_Base_01.vfx: particlesystem "dust1" t=0+0.7 self [FO3_FX.tga]
FX_FootH_FloatingEyeBot_Sand_Base_01.vfx: particlesystem "dust1" t=0+1.1 self [FO3_FX.tga]
FX_FootH_FloatingEyeBot_Wood_Base_01.vfx: particlesystem "dust1" t=0+0.7 self [FO3_FX.tga]
FX_FootL_Hum_Asphalt_Base_01.vfx: sprite "Footprint (Left)" t=0+1 self [footprint_L.tga] | sound "sound" t=0+-1 self
FX_FootL_Hum_Carpet_Base_01.vfx: sprite "Footprint (Left)" t=0+3 self [footprint_L.tga] | sound "sound" t=0+-1 self
FX_FootL_Hum_Metal_Base_01.vfx: sprite "Footprint (Left)" t=0+1 self [footprint_L.tga] | sound "sound" t=0+-1 self
FX_FootL_Hum_Sand_Base_01.vfx: sprite "Footprint (Left)" t=0+5 self [footprint_L.tga] | particlesystem "dust" t=0+0.6 self [FO3_FX.tga] | sound "sound" t=0+-1 self
FX_FootL_Hum_Wood_Base_01.vfx: sprite "Footprint (Left)" t=0.1+2 self [footprint_L.tga] | sound "sound" t=0+-1 self
FX_FootR_Hum_Asphalt_Base_01.vfx: sprite "Footprint (Right)" t=0+1 self [footprint_R.tga] | sound "sound" t=0+-1 self
FX_FootR_Hum_Carpet_Base_01.vfx: sprite "Footprint (Right)" t=0+3 self [footprint_R.tga] | sound "sound" t=0+-1 self
FX_FootR_Hum_Metal_Base_01.vfx: sprite "Footprint (Right)" t=0+1 self [footprint_R.tga] | sound "sound" t=0+-1 self
FX_FootR_Hum_Sand_Base_01.vfx: sprite "Footprint (Left)" t=0+5 self [footprint_R.tga] | particlesystem "dust" t=0+0.6 self [FO3_FX.tga] | sound "sound" t=0+-1 self
FX_FootR_Hum_Wood_Base_01.vfx: sprite "Footprint (Right)" t=0.1+2 self [footprint_R.tga] | sound "sound" t=0+-1 self
FX_GlassShatterLarge_01.vfx: particlesystem "glass1" t=0+1.1 self/snap (2) [FO3_FX.tga]
FX_GlassShatterMedium_01.vfx: particlesystem "glass1" t=0+1.1 self/snap (2) [FO3_FX.tga] | sound "sound" t=0+-1 self
FX_GlassShatterSmall_01.vfx: particlesystem "glass1" t=0+1.1 self/snap (2) [FO3_FX.tga]
FX_GroundBlowingDeadLeaves_01.vfx (loops): particlesystem "leaves1" t=0+6 self [FO3_FX.tga] | collision "attract1" t=0+-1 self | collision "attract2" t=0+-1 self | collision "attract3" t=0+-1 self
FX_GroundBlowingDust_01.vfx: particlesystem "dust1" t=0+6 self [FO3_FX.tga] | collision "attract1" t=0+-1 self | collision "attract2" t=0+-1 self | collision "attract3" t=0+-1 self | sound "sound" t=1.5+-1 self
FX_GroundBlowingDust_nosound_01.vfx: particlesystem "dust1" t=0+6 self [FO3_FX.tga] | collision "attract1" t=0+-1 self | collision "attract2" t=0+-1 self | collision "attract3" t=0+-1 self
FX_GroundBlowingTrash_01.vfx (loops): particlesystem "trash1" t=0+6 self [FO3_FX.tga] | particlesystem "trash2" t=0+6 self [FO3_FX.tga] | particlesystem "trash3" t=0+6 self [FO3_FX.tga] | particlesystem "trash4" t=0+6 self [FO3_FX.tga] | collision "attract1" t=0+-1 self | collision "attract2" t=0+-1 self | collision "attract3" t=0+-1 self
FX_GroundSwirlieDeadLeaves_01.vfx (loops): particlesystem "leaves1" t=0+4 self [FO3_FX.tga] | collision "attract1" t=0+-1 self | collision "attract2" t=0+-1 self | collision "attract3" t=0+-1 self | collision "attract4" t=0+-1 self
FX_HitBloodHighVelocity_01.vfx: sprite "trail" t=0+0.1 self [FO3_FX.tga] | model "cone1" t=0.1+0.2 self [Cone_Blood.b3d] | model "cone2" t=0.1+0.2 self [Cone_Blood.b3d] | model "cone3" t=0.1+0.2 self [Cone_Blood.b3d] | particlesystem "bloodspray_1" t=0+1 self [FO3_FX.tga] | particlesystem "bloodspray_2" t=0+1 self [FO3_FX.tga]
FX_HitBloodStandard_01.vfx: model "cone1" t=0.1+0.2 self [Cone_Blood.b3d] | particlesystem "bloodspray_1" t=0+1 self [FO3_FX.tga] | particlesystem "bloodspray_2" t=0+1 self [FO3_FX.tga]
FX_HitElectrical_01.vfx: sprite "mark1" t=0.1+0.5 self [FO3_FX.tga] | sprite "mark2" t=0.1+0.5 self [FO3_FX.tga] | particlesystem "smoke1" t=0+1 self [FO3_FX.tga] | lightning "spark" t=0.2+1 self
FX_HitFire_01.vfx: sprite "mark1" t=0.1+0.5 self [FO3_FX.tga] | sprite "mark2" t=0.1+0.5 self [FO3_FX.tga] | particlesystem "smoke1" t=0+1.1 self [FO3_FX.tga]
FX_HitLaser_01.vfx: sprite "mark1" t=0.1+0.5 self [FO3_FX.tga] | sprite "mark2" t=0.1+0.5 self [FO3_FX.tga] | model "cone1" t=0+0.2 self [Cone_Light.b3d] | particlesystem "smoke1" t=0+1 self [FO3_FX.tga]
FX_HitRicochetMetal_01.vfx: particlesystem "armor" t=0+1.5 self [FO3_FX.tga]
FX_LaserBeam_01.vfx: sprite "laserarray_FlashSingle_01" t=0+0.1 self/FX_Muzzle [FO3_FX.tga] | sprite "laserarray_FlashSingle_02" t=0+0.1 self/FX_Muzzle [FO3_FX.tga] | model "laserarray_TracersSingle" t=0.1+0.1 self/FX_Muzzle [Cone_Laser_01.b3d] | sound "sound" t=0+1 self [AttackLaser.wav]
FX_MesaConsoleLight_01.vfx: model "cone" t=0+-1 self [Cone_Light.b3d] | light "light" t=0+-1 self
FX_MuzzleFlashLargeMulti_01.vfx: model "Minigun_FlashLargeMulti" t=0+1 self/FX_Muzzle [Xsprite_muzzleflash.b3d]
FX_MuzzleFlashLargeSingle_01.vfx: model "Minigun_FlashLargeMulti" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d]
FX_MuzzleFlashMediumMulti_01.vfx: model "Machinegun_FlashMediumMulti" t=0+0.5 self/FX_Muzzle [Xsprite_muzzleflash.b3d]
FX_MuzzleFlashMediumSingle_01.vfx: model "Machinegun_FlashMediumSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d]
FX_MuzzleFlashSmallMulti_01.vfx: model "Pistol_FlashSmallMulti" t=0+0.5 self/FX_Muzzle [Xsprite_muzzleflash.b3d]
FX_MuzzleFlashSmallSingle_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d]
FX_MuzzleSmoke_01.vfx: particlesystem "MuzzleSmoke" t=0+2.5 self/FX_Muzzle [FO3_FX.tga]
FX_Selection_Critter_Medium_01.vfx (loops): modeleffect "highlight" t=0+-1 self | model "select1" t=0+-1 self [Disc_select_01.b3d] | model "select2" t=0+-1 self [Disc_select_02.b3d]
FX_SlideProjectorOn_01.vfx: model "light" t=0+-1 self [Cone_Light.b3d]
FX_SmokeBlackExtraLarge_01.vfx (loops): particlesystem "smoke1" t=0+-1 self [FO3_FX.tga]
FX_SmokeBlackLarge_01.vfx (loops): particlesystem "smoke1" t=0+-1 self [FO3_FX.tga]
FX_SmokeBlackMedium_01.vfx (loops): particlesystem "smoke1" t=0+-1 self [FO3_FX.tga]
FX_SmokeBlackSmall_01.vfx (loops): particlesystem "smoke1" t=0+-1 self [FO3_FX.tga]
FX_SparkShowerLarge_01.vfx: particlesystem "sparks1" t=0+-1 self [FO3_FX.tga] | particlesystem "sparks2" t=0+-1 self [FO3_FX.tga]
FX_SparkShowerMedium_01.vfx: particlesystem "sparks1" t=0+-1 self [FO3_FX.tga] | particlesystem "sparks2" t=0+-1 self [FO3_FX.tga]
FX_SparkShowerSmall_01.vfx: particlesystem "sparks1" t=0+-1 self [FO3_FX.tga] | particlesystem "sparks2" t=0+-1 self [FO3_FX.tga]
FX_State_FloatingEyeBot_Broken_01.vfx: lightning "lightning" t=0+-1 self | particlesystem "sparks2" t=0+-1 self [FO3_FX.tga] | sound "sound" t=0+-1 self
FX_TracersMulti_01.vfx: particlesystem "Minigun_TracersMulti" t=0+1.1 self/FX_Muzzle [FO3_FX.tga]
FX_TracersSingle_01.vfx: particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga]
FX_WP_223Autoloader_GunSilencer_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackLowPistol.wav]
FX_WP_223Autoloader_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackHighPistol.wav]
FX_WP_223Minigun_Burst_01.vfx: model "Minigun_FlashLargeMulti" t=0+1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Minigun_TracersMulti" t=0+1.1 self/FX_Muzzle [FO3_FX.tga] | particlesystem "Minigun_BrassEjectMulti" t=0+1.5 self/FX_Brass [FO3_FX.tga] | sound "Sound" t=0+1.5 self [AttackMinigun.wav]
FX_WP_223Minigun_Sweep_01.vfx: model "Minigun_FlashLargeMulti" t=0+1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Minigun_TracersMulti" t=0+1.1 self/FX_Muzzle [FO3_FX.tga] | particlesystem "Minigun_BrassEjectMulti" t=0+1.5 self/FX_Brass [FO3_FX.tga] | sound "Sound" t=0+1.5 self [AttackMinigun.wav]
FX_WP_22Autoloader_GunSilencer_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackLowPistol.wav]
FX_WP_22Autoloader_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackHighPistol.wav]
FX_WP_44Revolver_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackHighPistol.wav]
FX_WP_45Autoloader_GunSilencer_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackLowPistol.wav]
FX_WP_45Autoloader_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackHighPistol.wav]
FX_WP_45Revolver_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackHighPistol.wav]
FX_WP_50Revolver_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackHighPistol.wav]
FX_WP_9mmAutoloader_GunSilencer_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackLowPistol.wav]
FX_WP_9mmAutoloader_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackHighPistol.wav]
FX_WP_9mmMachinePistol_Burst_01.vfx: model "Pistol_FlashSmallMulti" t=0+0.5 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectMulti" t=0+0.9 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersMulti" t=0+0.6 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=1.2+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackSubmachinegun.wav]
FX_WP_9mmMachinePistol_GunSilencer_Burst_01.vfx: model "Pistol_FlashSmallMulti" t=0+1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectMulti" t=0+1.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersMulti" t=0+1.1 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=1.5+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackLowPistol.wav] | sound "sound" t=0.1+1 self [AttackLowPistol.wav] | sound "sound" t=0.2+1 self [AttackLowPistol.wav] | sound "sound" t=0.3+1 self [AttackLowPistol.wav] | sound "sound" t=0.4+1 self [AttackLowPistol.wav] | sound "sound" t=0.5+1 self [AttackLowPistol.wav] | sound "sound" t=0.6+1 self [AttackLowPistol.wav] | sound "sound" t=0.7+1 self [AttackLowPistol.wav] | sound "sound" t=0.8+1 self [AttackLowPistol.wav] | sound "sound" t=0.9+1 self [AttackLowPistol.wav] | sound "sound" t=1+1 self [AttackLowPistol.wav]
FX_WP_9mmMachinePistol_GunSilencer_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+-1 self [AttackLowPistol.wav]
FX_WP_9mmMachinePistol_Single_01.vfx: model "Pistol_FlashSmallSingle" t=0+0.1 self/FX_Muzzle [Xsprite_muzzleflash.b3d] | particlesystem "Pistol_BrassEjectSingle" t=0+0.5 self/FX_Brass [FO3_FX.tga] | particlesystem "Pistol_TracersSingle" t=0+0.2 self/FX_Muzzle [FO3_FX.tga] | particlesystem "MuzzleSmoke" t=0.4+2.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+-1 self [AttackHighPistol.wav]
FX_WP_BaseballBat_Slash_01.vfx: trail "trail" t=0+0.5 self | sound "sound" t=0+0.4 self [AttackHeavySwing_2.wav]
FX_WP_CattleProd_Idle_01.vfx: lightning "spark" t=0+-1 self/Weapon_1
FX_WP_CattleProd_Thrust_01.vfx: lightning "spark" t=0.5+0.4 self/Weapon_1 | sprite "mark1" t=0.5+0.5 self/Weapon_1 [FO3_FX.tga] | sprite "mark2" t=0.5+0.5 self/Weapon_1 [FO3_FX.tga]
FX_WP_CombatKnife_Slash_01.vfx: trail "trail" t=0.2+0.5 self | sound "sound" t=0+0.5 self [AttackSwing_1.wav]
FX_WP_CombatKnife_Thrust_01.vfx: trail "trail" t=0.2+0.5 self | sound "sound" t=0+0.5 self [AttackSwing_1.wav]
FX_WP_Flamethrower_Idle_01.vfx: model "pilot_outter" t=0+-1 self/FX_Muzzle [Cone_Light.b3d] | model "pilot_inner" t=0+-1 self/FX_Muzzle [Cone_Light.b3d]
FX_WP_Flamethrower_Sweep_01.vfx: particlesystem "fire" t=0+3.5 self/FX_Muzzle [FO3_FX.tga] | particlesystem "smoke" t=0+3.5 self/FX_Muzzle [FO3_FX.tga] | sound "sound" t=0+1 self [AttackFlamer.wav]
FX_WP_FloatingEyeBot_Laser_Single_01.vfx: sprite "laserarray_FlashSingle_01" t=0+0.1 self [FO3_FX.tga] | sprite "laserarray_FlashSingle_02" t=0+0.1 self [FO3_FX.tga] | model "laserarray_TracersSingle" t=0.1+0.1 self [Cone_Laser_01.b3d] | sound "sound" t=0+0.5 self [AttackLaser.wav]
FX_WP_FloatingEyeBot_Shock_Idle_01.vfx: lightning "spark" t=0+-1 self
FX_WP_FloatingEyeBot_Tranq_Single_01.vfx: model "Tranq_FlashSingle" t=0+0.1 self [Cone_Muzzleflash_01.b3d] | model "Tranq_TracersSingle" t=0+0.1 self [Cone_Laser_01.b3d]
FX_WP_Hatchet_Slash_01.vfx: trail "trail" t=0.2+0.5 self | sound "sound" t=0+0.5 self [AttackSwing_2.wav]
FX_WP_Hatchet_Throw_01.vfx: trail "trail" t=0.2+0.5 self | sound "sound" t=0+0.5 self [AttackSwing_2.wav]
FX_WP_LaserPistol_NoRecoil_01.vfx: sprite "laserarray_FlashSingle_01" t=0+0.1 self/FX_Muzzle [FO3_FX.tga] | sprite "laserarray_FlashSingle_02" t=0+0.1 self/FX_Muzzle [FO3_FX.tga] | model "laserarray_TracersSingle" t=0.1+0.1 self/FX_Muzzle [Cone_Laser_01.b3d] | sound "sound" t=0+1 self [AttackLaser.wav]
FX_WP_LaserSaw_Idle_01.vfx: model "beam" t=0+-1 self/Weapon_1 [Xsprite_muzzleflash.b3d]
FX_WP_LaserSaw_Slash_01.vfx: model "beam" t=0+-1 self/Weapon_1 [Xsprite_muzzleflash.b3d] | trail "trail" t=0.2+1 effect 0 | sound "sound" t=0+0.5 self [AttackSwing_2.wav]
FX_WP_Machete_Slash_01.vfx: trail "trail" t=0+0.5 self | sound "sound" t=0+0.4 self [AttackHeavySwing_1.wav]
FX_WP_Machete_Thrust_01.vfx: trail "trail" t=0+0.5 self | sound "sound" t=0+0.4 self [AttackHeavySwing_1.wav]
FX_WP_PowerFists_Punch_01.vfx: trail "trail" t=0.2+0.2 self | sprite "lens_flare1" t=0.2+0.5 self [FO3_FX.tga] | sprite "lens_flare2" t=0.2+0.5 self [FO3_FX.tga] | sound "sound" t=0.2+0.4 self [AttackHeavySwing_2.wav]
FX_WP_Shiv_Thrust_01.vfx: trail "trail" t=0.2+0.5 self | sound "sound" t=0+0.4 self [AttackSwing_1.wav]
FX_WeatherRainAcid_01.vfx: rain "rain1" t=0+-1 self
FX_WeatherRainNormal_01.vfx: rain "rain1" t=0+-1 self
_F3_Generator.vfx (loops): model "Generator Model" t=0+-1 self [DS_Tech{5}Generator.b3d] | lightning "Lightning Bone 1" t=0+4.7 effect 0/Bone01 | lightning "Lightning Bone 2" t=0+4.7 effect 0/Bone02 | lightning "Lightning Bone 3" t=0+4.7 effect 0/Bone03 | lightning "Lightning Bone 4" t=0+4.7 effect 0/Bone04
_F3_Life_Support.vfx: particlesystem "Smoke" t=0+-1 self [fo3_FX.tga]
demo_rain.vfx: rain "Fast Rain" t=0+-1 self | rain "Slow Rain" t=0+-1 self
demo_snow.vfx: snow "Snow" t=0+-1 self [particle_fx.tga]
demo_wind.vfx: wind "Wind" t=0+-1 self
```

## Debug switches (vanburen)
`VB_PROF=1` per-phase frame timings (and a line for every frame over 12 ms); `VB_DEBUG_FX=1` effect counts / sprite and footstep traces; `VB_LIST_FX=1` the map's effect emitters; `VB_NOAMBIENT=1` no map emitters; `VB_FORCEHIT=1` the player always hits; `VB_FXFLIPV=1` flip atlas rows; `VB_FPDEBUG=1` footprints drawn big and magenta; `VB_PSIZE=<k>` scale particle size; `VB_NOTRANS=1` skip alpha-blended particles (smoke); `VB_DEBUG_MUZZLE=1` print weapon muzzle axes; `VB_HANDS=<a.WEA>,<b.WEA>` equip those weapons in the hands (test hook); `VB_DEBUG_BONES=1` list the player skeleton.

## Findings of 2026-10-24
* **Weapon bone frame**: the weapon skeletons have the barrel on the muzzle bone's Z axis and the slide's up on X, while effect files use X along the barrel, Y up, Z sideways. `heldAnchor` remaps (effect X = bone Z, Y = bone X, Z = bone Y).
* **Footprints** (`Rotatable` sprites named footprint): toes along the walker's facing (the file's Rotation does not apply); each corner follows the ground height; the print goes under the planted (lower) foot at the `FL` / `FR` cue; the sprite's `Duration` and linear alpha fade are played as authored; hidden outside the lit disc. Per-surface durations: Sand 5 s, Wood 2 s, Carpet 3 s (alpha 100), Asphalt / Metal 1 s. The surface mapping is a guess (open item 16).
* **Particles**: `Size` is the full quad width; draw order opaque, reverse-subtract, alpha-blended, additive.
* **Hit effects**: `FX_HitLaser_01`'s cone renders as a tall vertical line (open item 14).


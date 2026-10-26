# Three-engine investigation — 26 October 2026

This is investigation 44, **not an APK release**. Baseline is the verified
`android-touch` commit **a4577e913961000e8a0b24c2a11abb3186067550** (APK 900043).
Changes are isolated on **research/three-engine-fidelity-44**. No engine
replacement, protected/development-branch merge, new APK or replacement of the
stable Drive download is part of this investigation.

Accepted Bunker, building Force Fire, Citadel wall, normal shell, touch,
multi-touch, importer and save functionality is preserved. Nuclear presentation
remains unverified. This report extends FIDELITY-AUDIT43 rather than replacing
the existing tracker or treating every old hypothesis as a new bug.

## Source pins and investigation boundaries

| Project | Inspected revision | Architecture | License read |
|---|---|---|---|
| [RA2R](https://github.com/SolidFaker/RA2R/tree/7c9d47c37d5e7dde134a69abf63ca1424cc62e32) | 7c9d47c37d5e7dde134a69abf63ca1424cc62e32 | C++20/SDL3; asset/tools pipeline, software voxel raster, deterministic sim | MIT |
| [VERA20K](https://github.com/YuriPlanet/vera20k/tree/8be601598438b05dd62874cf837bb29a7685a8dc) | 8be601598438b05dd62874cf837bb29a7685a8dc | Rust; GPU-free core, application/presentation and GPU consumers | GPLv3 |
| [RA2YR ReSource](https://github.com/Ritanlisa/RA2YR_ReSource/tree/b01d3cf93db69aca9b5cb5054ba04e4358963828) | b01d3cf93db69aca9b5cb5054ba04e4358963828 | Reconstructed C++20 Win32/DirectDraw program and x86 hook tools | GPLv3 plus declared EA Section 7 terms |
| Omar upstream | a8cfdccfef8592c13a6517c3fb76d834b1534027 | Existing C++17/SDL2 foundation | Existing 0BSD baseline retained |
| Gagarin | 945a29e262dbb364bbe41605c42fc5acde5a2918 | Address/byte-matching reconstruction | GPLv3 research only |

Complete Git tree inventories were fetched without truncation: RA2R 859 files,
VERA20K 5,789, ReSource 724. Recent ten-commit histories, root build files,
licenses and selected documentation were read. The first targeted source pass
read 149 files; subsequent reads covered native fixture metadata, lift,
factory, receiver, surface and validation sources. This is focused source-level
research, not a claim that every line or dependency was audited.

RA2R's recent history includes game-speed alignment, macOS, aircraft/naval work
and special weapons; VERA's includes copyright/license changes and Ares ammo
work; ReSource's recent history emphasizes type inference and class database
recovery. A recent commit or named class does not certify gameplay parity.

No external engine test suite, foreign oracle, hook DLL or retail executable
was executed. Their checked-in tests, observed fixtures and scope declarations
were inspected; published native observations remain externally produced
evidence. Our own fork tests and benchmark **were** executed below.

Local gamemd.exe was read only: 5,286,208 bytes, PE version resource `1.11`,
SHA-256 `3e81a61775d2745d1dabe397325ef663cd994ffc194da4e998e3bf5d2d308600`.
That resource string alone does not authenticate a particular v1.001 oracle
image. No unknown binary was assumed address-compatible or redistributed.

Drive `ra2yrlogs` was refreshed: newest diagnostic remains build 900042,
`1qTO7B54f8Uiih_vRGhmxvoJj-aGZArWJ`; no new 900043 session or MP4 was found.
Its complete analysis is in audit43. No new device symptom is inferred here.

## What the validation evidence actually establishes

### RA2R: asset/renderer implementation, not a retail-executable oracle

The actual readers cover MIX/SHP/VXL/HVA/TMP/palettes; tests include synthetic
fixtures, codecs, animation metadata, deterministic raster output and optional
retail assets. Asset cases may skip without installation files. Test source
existence and CI configuration are not newly executed passes.

Relevant paths: `engine/src/assets/{mix_file,shp_file,vxl_file,hva_file}.cpp`,
`engine/src/render/{voxel_raster,terrain_tile,map_scene,object_layer}.cpp`,
`engine/src/cache/cache_manager.cpp`, `tests/test_{assets,render,fixtures,
anim_frames,engine_modules}.cpp`.

Its actual cache manager uses source CRC plus schema, temporary-file writes
and rename. Map RGBA caching uses an entry-count LRU; the design document's
atlases/mmap/streaming proposals are not proof those paths are all implemented.
The renderer has per-pixel depth inside composed voxel parts; map scene painter
ordering is a different layer. Do not call it a complete world depth-buffer
replacement for our bridge/terrain ordering.

### VERA20K: unusually useful bounded native observations

`tools/native_oracle.md`, individual sidecars and consumer tests distinguish
original instruction execution, substituted platform services, synthetic object
priors, receiver boundaries and wider Rust integration. This is stronger than
an undocumented implementation, but a bounded component comparison is not a
complete original match, battle, save or Android performance test.

Examples inspected:

* `tools/spatial_oracle/disk_laser.{md,meta.json,json}`: original ring setup,
  FLH/Jumpjet queries and Disk constructor/Fire/AI/expiry. Damage and sound end
  at service boundaries; full scenario and save/swizzle are excluded.
* `gattling_stage.{meta.json,json}`: 68 histories of original stage/accessor
  routines. Unit/building caller and audio-playback controls are separate.
* `tools/procedural_drawing_oracle/magnetic_wave.meta.json`: selected TELE/MTNK
  readers, Wave construction/update and packed framebuffer distortion; legal
  firing/lift, whole scenario and live GPU composition have separate owners.
* `magnetron_aircraft_lift.{md,meta.json}`: bounded hit/Imbue/Jumpjet dispatch;
  landing and effect seams are declared, rather than claiming a whole flight.
* `src/sim/world/factory_infantry_output_tests.rs`: joins native local goldens
  to Rust production; it explicitly does not compare whole producer/HouseAI,
  absolute native frames, all Scatter choices or complete RNG streams.

### ReSource: architecture, recovered layouts and a narrow comparison pipeline

Its modeled chain is Abstract -> Object -> Mission -> Radio -> Techno -> Foot;
Building is on the Techno branch, and mobile Unit/Infantry/Aircraft behavior
builds on Foot. Prototype/type classes, COM multiple-inheritance interfaces,
32-bit vtable offsets, House ownership and Team/Trigger/Script objects are
separate concerns. This is useful for locating original method responsibilities,
not a requirement to replace our World-owned unit/building vectors with that
class layout. Pointer width, calling conventions and binary vtables cannot be
transplanted into ARM64. CMake explicitly excludes `_generated/` stub/decompile
material; the volume of that material is not compiled behavioral coverage.

The checked-in `signals.json` contains **19,133 records with a completed field;
20 are completed=true and none are done=true** in this snapshot. These flags
are metadata, not runtime success receipts. They also do not count every
handwritten implementation. The README's counts, coverage document and hook
review differ; report them as stale/self-reported, not interchangeable totals.

In the inspected `src/render/surface.cpp`, 28 REVERSE markers include four
Inject candidates (bounded pixel/dashed-line/ellipse operations) and many None
markers. None is not independently sufficient evidence of a passed comparison.
The referenced `injectFunctionTest/functions.json`, generated `gen/` output and
comparisonResult.log are not present in the tracked tree at this pin. The
current generator instead consumes **signals.json** and local decompile/map
artifacts. We did not fabricate active-hook results from README instructions.

Actual limitations found: `src/system/factory.cpp` explicitly defers the
factory implementation and contains stub persistence/checksum methods;
`src/render/voxel.cpp` has unimplemented creation paths; Techno contains many
constant/empty methods, with a simplified Fire body using a fixed base
firepower. These are not safe replacements for our existing combat/factory.
Large IDA listings and named methods are useful leads, not validated C++ bodies.

The hook design is valuable: pinned calling conventions, thread/depth gates,
original-versus-rebuilt runs, input/output comparisons and rollback. However,
`shadow_txn.cpp` protects a configured **.data range**, not arbitrary complete
heap/OS state; non-owner writes can unprotect without backup. Its safety and
coverage must be checked per routine. It is not an ARM64 engine plugin.

## Subsystem comparison matrix

Legend: **I** actual code inspected; **T** test code inspected, not run here;
**N** published bounded original-executable observations/consumer; **D** layout,
documentation, stub or unverified reconstruction. None implies whole-game parity.

| Subsystem | RA2R | VERA20K | ReSource | Current Android fork / conclusion |
|---|---|---|---|---|
| MIX/SHP/TMP/palette | I/T | Asset pipeline + native raster controls | MIX/surface work, other paths partial | Existing readers/tests; no parser transplant |
| VXL/HVA/orientation | I/T software cubes | Pose/raster/lighting oracle families | Voxel creation stubs | Existing normal tables/HVA/raster; compare nonidentity transforms first |
| Terrain/cliffs/depth | I/T tile depth + map ordering | TMP, packed Z/A, slope controls | DSurface/layout leads | Preserve layered bridges; avoid global renderer replacement |
| Shadows/animation | I/T frame plans | N local shadows/animation plus GPU consumers | Surface/body/layout fragments | Existing ShadowIndex, HVA/SHP/slot machinery; object-specific gaps |
| Cache/performance | I cache/LRU; portions of design still plans | New GPU/core split; workload claims bounded | DirectDraw/Win32 foundation | Existing bounded caches; independent lighting optimization measured |
| Flying Disc | Broad special-weapon sim is not Disc native proof | N DiskLaser lifecycle | Techno fields, no verified full owner | Current static visual fallback and immediate generic hit need coordinated replacement |
| Chaos Drone | No verified native owner identified | Psychedelic receiver implementation | Warhead/hierarchy leads | Psychedelic unhandled; major gameplay gap, not just gas art |
| Magnetron | No verified native lift owner identified | N lift/drop and Wave families | Layout/method leads | Native fork suppresses lift damage but lacks lift/Wave rendering |
| Gatling | General weapons not original-stage proof | N state, caller and audio families | Fields/stubs ≠ stage parity | Stage currently derived from charge; sound loop43 remains partial fidelity |
| Production/exits | I/T broad simulation | N local exits + wider integration | Factory deferred; GetExitCoords alone insufficient | Existing exits use documented deterministic fallback; retain until controlled case |
| AI/pathfinding | Flow-field/grid approach | Mission/path/native local observations | Foot/House/Team recovery partial | Different algorithms not drop-ins; diagnose exact mission/occupancy state |
| Superweapons | Broad special-effect tests | N launch/impact slices + effect owners | Super state/layout, partial bodies | Missing grant/effect families confirmed in audit43; nuclear visuals unverified |

## Priority findings and smallest correct development steps

### 1. Flying Disc attack is a retained sequence, not one static rim ray

**Expected original evidence:** VERA's Disk corpus executes initialization
**0x4A6CF0**, constructor **0x4A7A30**, Fire **0x4A71A0**, AI **0x4A7340** and
Detach **0x4A7900**. It observes sixteen integer ring positions, including two
non-regular authored angles, opposing ring progress, countdown-separated
emissions and a final target discharge. FLH and target are queried during
emissions. Damage and report are recorded at the final discharge boundary.
This supersedes the earlier nearest-rim-only interpretation used for build42.

**Fork:** `disk_laser.hpp`, `pres_fx.inc::add_beam/draw_beams` and
`pres_game.inc::fire_fx` create a static ellipse/ray; ordinary combat applies
the hit separately/immediately. The root is **confirmed architectural
incompleteness**, not missing Disk art. The old visual change was an approximation.

**Fix:** implement an independent saved Disk attack controller with delayed
discharge, cancellation/range/crash/expiry, current FLH/target queries, ring
births and charge/report timing together. Do not add a cosmetic rotating ring
while leaving damage out of sync. Keep DrainWeapon separate. No GPL state
machine or original table was copied here. **Risk:** simulation timing/save/RNG;
high difficulty. **Tests:** native component facts with declared boundaries,
ground/air/building, moving firer/victim, target/source death, Stop, range loss,
save/load during charge and normal shell regression. No full Disc fix shipped.

Sources: [native control scope](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/tools/spatial_oracle/disk_laser.md),
[consumer](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/src/sim/world/disk_laser.rs).
Gagarin's matched BeginDraining **0x70FD70** is relevant to the separate drain
ability, not proof of this laser lifecycle. ReSource's drain fields are not a
validated implementation of either complete ability.

### 2. Chaos Drone needs Psychedelic receiver behavior and a self-centered origin

Owned retail rules specify ChaosAttack AreaFire, PsychGasCreate Psychedelic
and CDGAS. The effect is not an ordinary 600-HP cannon shot. VERA's
`world_receiver.rs` traces the original **0x701D71..0x701DB4** transition through
team removal, target clearing and Hunt; its receiver owns timer/immunity state.

**Fork/root confirmed:** no native Psychedelic parser/receiver owner was found;
AreaFire is handled for particular infantry deploy cases rather than a general
Chaos attack owner. `pres_game.inc` generic instant impacts use the target.
Thus flag handling is absent; the exact resulting live damage/origin in a
phone Chaos session was not newly measured. Do not present a predicted instant
kill as a newly observed device bug.

**Fix:** independent berserk state and expiry, original immunity/hostility/order
rules and self-centered area release; then CDGAS ownership, height and audio.
Preserve actual damage pipelines for ordinary warheads. **Risk:** targeting,
AI/team membership and save compatibility. **Tests:** immune/nonimmune,
friends/enemies, overlapping clouds, deployment/movement, repeat hit, timer
expiry, source death and save; determine original order acceptance explicitly.
High impact, high difficulty; no partial gas-only gameplay claim.

Source: [receiver](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/src/sim/combat/world_receiver.rs).
No validated complete Chaos owner was established in RA2R, ReSource or the
reviewed Gagarin functions; absence of a filename is not a whole-repository
proof that no related generic routine exists.

### 3. Magnetron lift and magnetic rendering have separate missing owners

**Evidence:** native lift controls execute hit slice **0x46963A..0x469AA4**,
ImbueLocomotor **0x710000** and Jumpjet/Foot destination dispatch. Magnetic Wave
controls reach constructor **0x75E950**, Update **0x762AF0** and DrawIt
**0x75F9F0** with original packed-buffer distortion/depth behavior.

**Fork/root confirmed:** magnetic primary damage is deliberately suppressed in
`combat.cpp`; `weapon_resolve.cpp` recognizes IsLocomotor. `pres_fx.inc`
does not parse IsMagBeam and has no native magnetic-wave renderer. The 5000
weapon value must not be made ordinary hit damage. A purple straight laser
would not reproduce the measured wave or transport gameplay.

**Fix:** independent saved lift/hold/drop and victim movement restrictions,
then retained wave source/target/phase plus depth-aware distortion. Gagarin's
matched ReleaseLocomotor **0x70FEE0** is a release reference, not whole lift
parity. ReSource's layouts cannot fill the missing behavior by themselves.
**Tests:** vehicle/aircraft/building/infantry legality, bridge/water impacts,
source/target expiry, falling ownership/kill credit and native boundary controls;
separate GPU/image comparisons. High impact/difficulty; no speculative patch.

Sources: [lift scope](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/tools/spatial_oracle/magnetron_aircraft_lift.md),
[wave scope](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/tools/procedural_drawing_oracle/magnetic_wave.meta.json).

### 4. Gatling stage transitions cannot be inferred from charge alone

**Original component evidence:** **0x70DE70** tests the pre-increment value and
advances a retained stage at most once; **0x70E000** decreases a retained stage.
Stage, value and report latch are distinct. Native reports start/release with
the stage owner rather than generic per-shot dispatch. Stock audio controllers
have attack/body/decay service behavior; a few synthetic loop tests do not prove
the complete playback/RNG pipeline.

**Fork:** `gattling.hpp::stage` derives a stage directly by crossing thresholds;
step clamps charge. Building state retains charge only. `weapon_sound_loop.hpp`
and `pres_game.inc` drive report loops from shot events. These are **confirmed
structural differences**, including threshold/hysteresis and startup/decay
timing. The build43 cell-order/low-power correction remains valid and preserved.

**Fix:** retain original-compatible stage state with charge and independent
report ownership; establish all unit/building caller intervals before changing
cadence. Requires saved-state review and deterministic tests. Do not merely
replace >= with >: the same value can belong to different retained states.
**Tests:** native histories, rookie/elite, increase/decay, target swap, AA,
power/Stop, load/restart and audio limits; preserve working tank SFX. High
confidence but medium/high integration risk; no stage rewrite in this patch.

Sources: [stage owner](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/src/sim/combat/gattling.rs),
[scope](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/tools/spatial_oracle/gattling_stage.meta.json),
[audio controls](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/tools/input_oracle/gattling_loop.md).
Gagarin **0x70E000** is byte-matched; it does not certify our caller scheduling.

### 5. Nuclear presentation must follow silo, carrier, impact animation and lighting

VERA separates Super launch **0x6CC390**, silo Mission_Missile **0x44C980**,
NukeMaker **0x46B310**, waiting for NUKEBALL and impact/lighting. Its documented
residuals include no-silo tails, missing-art test fallback and palette priority
under concurrent effects. ReSource's Super fields/stubs are not drop-in effects.

**Fork:** `world_sw.cpp`, `pres_game.inc` and `pres_effects.inc` have timed
strikes, effects and the recent ascent trailer, not a verified full original
silo/Bullet/NUKEBALL pipeline. **Root:** parity unverified; no new live nuclear
failure was supplied. **Fix:** trace those four owners, then replace only a
demonstrably wrong transition. **Tests:** readiness/doors/ascent/descent/wait/
impact/light/sound, save/Stop/target loss and overlapping storm/flash. Preserve
working missiles and ordinary shells. High complexity; no new nuclear claim.

Source: [nuke owner and residuals](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/src/sim/superweapon/nuke.rs).

### 6. Factory exit coordinates are only one part of production

VERA's paid-infantry test joins native ExitObject **0x443C60**, Infantry
**0x51DFF0**, Idle **0x51CBA0**, Walk **0x75AC80**, Scatter and radio/per-cell
transitions. It explicitly separates component results from whole House AI
and complete RNG/absolute tick equivalence. ReSource's GetExitCoords computes a
foundation-relative position, while FactoryClass itself is deferred.

**Fork:** `factory.cpp::exit_cell` and `World::launch_unit` already have
ground/naval/domain and occupied-exit logic. ExitCoord sub-cell lean is a
documented dropped detail and south-first ring search is a deterministic
fallback rather than native RNG ordering. **Root:** known approximation, not
a freshly reproduced universal factory bug. **Fix:** reproduce one stock exit
with original foundation/ExitCoord/Walk/radio facts before changing coordinates.
**Tests:** infantry/vehicle/air/navy, blocked door, rally/no rally, multiple paid
outputs, save/load and factory death. Medium/high; retain existing exits.

Sources: [native/Rust boundary](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/src/sim/world/factory_infantry_output_tests.rs),
[ReSource factory](https://github.com/Ritanlisa/RA2YR_ReSource/blob/b01d3cf93db69aca9b5cb5054ba04e4358963828/src/system/factory.cpp).

### 7. Terrain, HVA and layering: do not import another renderer's assumptions

RA2R supplies useful TMP bounds, depth-plane, frame/palette and shared voxel-Z
code. Its HVA reader transposes the 3x3 portion while this fork's `src/hva.cpp`
keeps row-major matrices based on its existing original-loader research. Some
RA2R format notes also contain older translation-scale assumptions later
qualified in the source. This disagreement is **unresolved**, not permission
to transpose every working tank or replace ramp matrices.

**Fork:** existing VXL normal-mode tables, selected HVA frames, ShadowIndex,
terrain transforms, layered depth and interpolation are present. **Fix path:**
nonidentity HVA original-query comparisons, per-pixel tile/object depth tests,
then exact ramp/anchor corrections where observed. Gagarin matrix/helper names
are not complete movement validation. **Tests:** mixed normal modes, nonidentity
HVA sections, all headings/ramp families, under/over bridges and occlusion.
Original visual correctness is not established by byte-identical output alone.

Sources: [RA2R reader](https://github.com/SolidFaker/RA2R/blob/7c9d47c37d5e7dde134a69abf63ca1424cc62e32/engine/src/assets/hva_file.cpp),
[renderer](https://github.com/SolidFaker/RA2R/blob/7c9d47c37d5e7dde134a69abf63ca1424cc62e32/engine/src/render/voxel_raster.cpp).

### 8. AI, targeting, pathfinding and other Yuri abilities

RA2R's flow-field/grid simulation is an alternative design, not the same
original mission/locomotor/cost policy. ReSource Foot/House/Team structures and
decompile listings are leads; placeholder methods and inferred names must be
checked against original instruction behavior. VERA's local original controls
can guide specific mission, receiver and exit tests, but do not certify all AI.

Our fork already has fixed mission ticks, targeting/guard restrictions,
navigation/traffic recovery, mind control, slave follow/replacement, MCV and
production tests. The remaining drain, superweapon, berserk and magnetic
owners identified above must interact with these systems rather than duplicate
them. **Root:** no new general deadlock/AI expansion failure reproduced in this
investigation. **Small action:** retain exact command/mission/occupancy traces,
build seeded failing maps and fix a state transition at a time. **Acceptance:**
1/10/100 units, all locomotor domains, stable seeds, blocked exits, control/
capture/expiry and equal replay/save state. No broad AI/pathfinder transplant.

## Independent implementation: reuse voxel normal lighting exactly

**Confirmed cost:** our raster calls `light_level` for every present voxel,
although its result depends only on normal index, section normal mode, call's
light vectors and VPL level count. Many dense voxels share a normal.

**Small change:** a stack-local 256-entry, lazily filled integer lookup per
section. The first encounter runs the existing calculation unchanged; later
encounters reuse it. No shared mutable cache, heap allocation, transform,
rounding, depth tie, palette, shader, simulation or save change. This was
independently written against our source; no external implementation was copied.
It is a cold-raster CPU optimization, not a correction to unproven visual parity.

**Reproducible tests:** 20 frozen pixel hashes from baseline a4577e9, both
normal modes, all byte normal indices, two nontrivial HVA frames and flat/
edge/corner/diagonal/interpolated slopes. All hashes matched. Existing broader
raster and deterministic tests also ran. The generated fixture contains no
retail content. See `tests/test_voxel_raster.cpp` and
`tools/voxel_lighting_bench.cpp` (argument `corpus` prints hashes/anchors).

**Measured final benchmark:** Windows host, GCC `-O2 -ffp-contract=off`, nine
samples of 180 dense synthetic uncached raster calls per process; four
alternating-order process pairs. Median of process medians **181.7405 ms ->
168.9415 ms**, **7.04% less time**. An earlier pass measured 6.91%. This does
not establish Android FPS, GPU time, thermals, whole-frame or sparse-model speed.
Warm cached sprites do not pay the measured raster cost at all.

Example current build of the standalone benchmark:

```text
g++ -std=c++17 -O2 -ffp-contract=off -Iengine -Iinclude tools/voxel_lighting_bench.cpp engine/voxel_raster.cpp -o voxel-bench.exe
voxel-bench.exe
voxel-bench.exe corpus
```

For A/B use the SAME benchmark source/compiler on baseline a4577e9 and this
branch; interleave processes. Do not compare timings from different fixtures.

## Licensing and architecture decision

* Retain the native C++17/SDL2 Android foundation, touch/input, importer and
  current saved-world/lockstep ownership. Do not merge the three engines.
* RA2R's MIT root license is compatible with considering specific files after
  provenance review and retained notice. Its third-party CMake names ImGui MIT
  and GoogleTest BSD-3; SDL3 and optional FFmpeg have separate licensing. Root
  MIT does not automatically relicense dependencies or external tables/data.
* VERA20K GPLv3 and ReSource GPLv3/EA terms are research only here. No code,
  fixture table, hook implementation or translated control flow was imported.
  Direct integration would require a separate compatibility/obligations review
  and the user's approval, as requested. No such approval was sought or needed
  for our independent optimization.
* ReSource's extra terms address trademark/affiliation, notices, origins and
  liability; they do not grant rights to distribute RA2 retail assets or exe.
  Do not infer executable equivalence or code provenance from its project name.
* A future original-comparison harness should pin exe/asset fingerprints,
  entry/exit PCs, supplied priors, substituted services, object/global/heap
  writes, all RNG streams and acceptance bounds. Export behavioral observations,
  not retail bytes. A Win32 hook remains a separate research tool, not something
  to load into this ARM64 app.

## Verification and delivery status

**Executed:** full native suite **1,276 cases / 277,617 assertions passed**, zero
failures; one optional absent retail landmark fixture excluded. Other optional
oracle/data tests may early-return; no complete campaign-oracle claim. New
frozen-corpus case: 20 assertions passed. Host A/B benchmark above executed.
The changed renderer compiled and linked into ARM64 `libmain.so` using existing
NDK/SDL2 configuration (`test44-android-native.log`).

**Not executed:** foreign builds/tests/oracles, original game play/hook injection,
new original disassembly, phone/emulator, GPU captures or thermal testing.
No full APK was packaged or uploaded in this research-only phase; the native
compile is compatibility evidence, not an installable or device-tested release.
No version-code/save-schema change. Stable APK **900043** remains the official
Drive artifact. Implementation and research commits are recorded in the living
tracker after push to the separate branch; android-touch is not merged.

Next priority is a complete independently specified Disc controller, then
Chaos/Psychedelic receiver semantics and Magnetron lift/wave owners, using
bounded native observations plus original-game repros. Establish those saved
state contracts before visual polish or a renderer/engine rewrite.

# Engine development audit

**Reviewed:** 2026-10-29  
**Scope:** the current RA2AndroidPort worktree: `engine/`, `src/`, `include/`,
`android/`, `server/`, `tests/`, `tools/`, and the existing compatibility and
playtest notes. This is a source and documentation audit, not a claim that all
game modes were run with retail assets.

## Executive summary

This is a native C++17/SDL2 reimplementation, not a wrapper around the Windows
game. Desktop and Android builds share the engine and asset libraries.
Simulation, asset parsing, campaign scripting, touch input, saves, mod profiles
and lockstep networking already exist. The remaining work is compatibility
completion and runtime validation, especially around Yuri's Revenge, campaign
edge cases, rendering fidelity, and engine extensions used by mods.

The current worktree has extensive uncommitted edits. This audit preserves
them; findings describe the inspected source as it exists now. The Android
debug APK builds, but compilation alone does not establish gameplay or visual
correctness.

## Subsystem audit

Status labels describe implementation evidence, not a blanket pass of the
feature: **implemented** means a source path exists; **covered** means relevant
automated tests exist; **runtime open** means real asset/device or original-game
comparison is still required.

| # | Subsystem and current implementation | Working / covered | Missing, bugs, or limits | Key source and tests | Priority |
|---|---|---|---|---|---|
| 1 | **Engine architecture:** one C++17 engine, asset library, desktop executable, and Android native target. CMake source lists are shared with the Android JNI CMake project. | Core simulation and asset code have broad native tests. APK compiles from shared sources. | The game is a clean-room reimplementation; successful build is not proof of original-game parity. Current source tree is dirty, so changes need careful review and regression checks. | `CMakeLists.txt`, `cmake/ra2_sources.cmake`, `engine/main.cpp`, `android/app/jni/CMakeLists.txt` | P0 validation |
| 2 | **Rendering / SDL2:** isometric terrain, SHP, VXL/HVA, palette, theater, building, projectile, unit, bridge and presentation layers are implemented. | Raster, projection, SHP, voxel, theater, bridge and rendering-stability tests exist. | Bridge tile ownership/deck facts and draw ordering still have reported overlap/flicker risks. Fog, theater and effects need map/device replay; automated geometry tests cannot prove the full image is correct. | `engine/iso_renderer.cpp`, `engine/world_depth.cpp`, `engine/world_depth.inc`, `engine/pres_*.inc`, `src/art.cpp`; `tests/test_*bridge*`, `tests/test_voxel_raster.cpp`, `tests/render_stability/` | P0 rendering |
| 3 | **Android / JNI / lifecycle:** SDLActivity hosts the native engine; Java handles setup/import, audio focus, network multicast lock, orientation and autosave handoff. | Java touch-layout tests and Android debug builds exist. Device diagnostics and crash reports can be exported. | Build output does not verify lifecycle, GPU, sound, touch comfort or thermal behavior. Current build has a known C++ missing-`override` warning in a map-trigger adapter. | `android/app/src/main/java/org/ra2/engine/RA2Activity.java`, `SetupActivity.java`, `engine/audio_sdl.cpp`, `engine/platform_os.cpp`, `engine/app_robust.inc` | P1 runtime |
| 4 | **Touch controls / gestures:** native input supports selection, commands, command dock, control groups, route ordering, and configurable layout; Android launcher exposes touch controls. | `tests/test_touch_gestures.cpp`, `tests/test_touch_bar_layout.cpp`, Java `TouchLayoutRegression.java`, and native touch-panel harness exist. | Full-device accessibility and gesture conflict checks remain necessary. Do not replace the established mapping without device evidence. | `engine/input.cpp`, `engine/touch_gestures.cpp`, `engine/touch_ui.inc`, `engine/touch_bar.inc`, `android/app/src/main/java/org/ra2/engine/TouchLayout.java` | P2 |
| 5 | **Movement / pathfinding / targeting:** deterministic A* routes over terrain and bridge layers; occupancy, movement costs, vehicle locomotion, group movement, attack targeting and force-fire are native systems. | `tests/test_pathfind.cpp`, `test_occupancy.cpp`, `test_locomotion.cpp`, `test_group_move.cpp`, combat and bridge tests cover component behavior. | Congestion and traffic deadlocks need reproduced match traces. The supplied `0.9.0+gv0.9.0-150-g7c3dba0-dirty` diagnostic sampled no Magnetron activity and reported `blocked_over_3s=0`; it is not proof of global pathfinding health. Bridge-cell pathing and bridge rendering need to be diagnosed separately. | `engine/pathfind.cpp`, `engine/world_loco.cpp`, `engine/world.cpp`, `engine/occupancy.cpp`, `engine/group_move.cpp`; `tests/test_pathfind.cpp`, `tests/test_movement.cpp` | P0 with reproduction |
| 6 | **Combat / weapons / effects / death:** data-driven weapon slots, projectile simulation, armor/warhead damage, veterancy, firing effects, AnimList, particles, beams, death and superweapon presentation exist. | Many combat, projectile, veterancy, superweapon and presentation mechanics have unit tests and reverse-engineering notes. The latest source audit confirmed the renderer did not parse `IsMagBeam`; this pass adds a magnetic-wave presentation path using `MagnaBeamColor`. | Magnetron vehicle lift/pull/drop and its full locomotor ownership lifecycle remain absent; its beam is a separate presentation effect. All weapon/effect/death combinations are not validated against original assets. This pass has not yet been exercised in a live match. | `engine/combat.cpp`, `engine/weapon_resolve.cpp`, `engine/weapon_unit.cpp`, `engine/projectile.cpp`, `engine/pres_fx.inc`, `engine/pres_effects.inc`, `engine/pres_game.inc`; `tests/test_combat.cpp`, `test_projectile.cpp`, `test_superweapon.cpp` | P0 |
| 7 | **Buildings / production / economy / harvesting:** placement, construction, queues, MCV deployment, power, ore and miner state are implemented. Yuri slave managers/workers have dedicated logic. | Tests cover production, MCV, buildings, economy, harvesting, ore and slave mechanics. | Economy and miner code paths are complex; reports need actor-specific diagnostics to distinguish path blockage, ore search, docking and manager ownership. No broad “all miners fixed” claim is supported without play traces. | `engine/building.cpp`, `engine/production.cpp`, `engine/ore.cpp`, `engine/harvest.cpp`, `engine/realplay_economy.inc`, `engine/realplay_scene.inc`; `tests/test_production.cpp`, `test_harvest.cpp`, `test_ore.cpp`, `test_mcv_deploy.cpp` | P1 |
| 8 | **Factions / tech trees / Yuri mechanics:** faction rosters, prerequisites, production, Yuri-specific units/buildings, mind control, psychic effects, Grinder and Bio Reactor mechanics are represented in engine code. | Yuri menu, art lookup, core behavior, abilities and production tests exist. | Remaining gaps include Magnetron locomotion/effect parity beyond the new visual wave, exact Yuri superweapon presentation and campaign/AI coverage. The root `README.md` is stale: it says Yuri is unplayable while newer fidelity notes/source cover Yuri systems. | `engine/roster*.cpp`, `engine/world_yuri.cpp`, `engine/world_drain.cpp`, `engine/abilities.cpp`, `engine/skirmish.cpp`; `tests/test_yuri_core.cpp`, `test_yuri_menu.cpp`, `test_chaos.cpp`, `test_capture.cpp` | P0 |
| 9 | **Campaign loading / scripting:** campaign catalogs, mission loading, map triggers, teams, scripts, objectives, cinematics and save state have dedicated systems. | Campaign and map-trigger tests and `scripts/play/` mission scenarios exist; Allied/Soviet mission behaviors have multiple documented play checks. | Yuri campaign availability and mission selection remain incomplete according to the current fidelity notes. Not every shipped mission/map trigger has an end-to-end regression; missing objective reports require the exact mission/map and diagnostic trace. | `engine/campaign.cpp`, `engine/campaign_runtime.inc`, `engine/campaign_rules.inc`, `engine/map_trigger.cpp`, `engine/campaign_scenes.inc`; `tests/test_campaign.cpp`, `tests/test_map_trigger.cpp`, `scripts/play/` | P0 |
| 10 | **AI / difficulty:** skirmish AI, AI triggers, task forces, teams, base planning and rules-based AI INI parsing are implemented. | `tests/test_ai_base.cpp`, `test_ai_team.cpp`, `test_ai_trigger.cpp`, `test_aimd.cpp` cover component behavior. | Human-match behavior, faction breadth, campaign AI and long-run production/attack patterns require seeded soak traces. A source feature or deterministic demo does not establish competent AI. | `engine/ai_base.cpp`, `engine/ai_team.cpp`, `engine/ai_trigger.cpp`, `engine/aimd_parse.cpp`, `engine/ai_skirmish.inc`; matching tests and `docs/gap/PLAYTEST-STATUS.md` | P1 |
| 11 | **Multiplayer / synchronization:** LAN discovery, direct connection, relay server, lockstep packet/state synchronization and game compatibility checks exist. | Network wire, event, lockstep, lobby and multiplayer tests exist; server code is present. | Same-build/content compatibility is required. Android-to-PC interoperability is intentionally gated; multiplayer runtime and desync behavior need real peer tests. Do not promise cross-platform equivalence from unit tests alone. | `engine/net_*`, `engine/platform_os.*`, `server/`, `tests/test_net_*.cpp`, `tests/test_mp.cpp`, `docs/SERVER.md` | P1 |
| 12 | **Asset loaders / maps / theaters:** MIX, INI, CSF, SHP, VXL/HVA, PAL, TMP, audio, map packs and theater loaders are separate reusable libraries. | Parsers have broad fixture tests; map catalog and import paths exist. | Retail and community files vary; supported format does not imply support for every map trigger, theater rendering path or mod-specific extension. Tests without external assets do not validate every shipped file. | `src/mix.cpp`, `src/ini.cpp`, `src/map.cpp`, `src/art.cpp`, `src/vxl.cpp`, `src/hva.cpp`, `src/theater.cpp`, `include/ra2assets/`; parser tests | P1 |
| 13 | **INI / MIX / modding:** profiles are opt-in overlays with scanning, reporting, Android import/selection, file precedence and profile fingerprints. A command-line `ra2modcheck` audits profiles. | `tests/test_mod_profile.cpp`; example data-only profile; mod diagnostics and compatibility documentation exist. | Full support is only for implemented keys and the documented overlay rules. Ares/Phobos/Syringe DLL logic cannot load as Windows plugins on Android. Mental Omega and Rise of the East are not certified. | `src/mod_profile.cpp`, `src/asset_fs.cpp`, `src/rules.cpp`, `tools/ra2modcheck.cpp`, Android `ModManager.java`, `docs/MOD-PROFILES.md`, `docs/MODDING-COMPATIBILITY.md` | P1 |
| 14 | **Save/load / autosave:** simulation state has versioned serialization; Android/native autosave handling and recovery paths exist. | `tests/test_save_states.cpp`, `test_saveload.cpp`; save-format compatibility documented in release notes. | Saves are version-sensitive and multiplayer peers need compatible engine/content. Campaign and mod-profile transitions require targeted continuation checks; source-level serialization does not establish crash-free device recovery. | `engine/save*`, `engine/game_scene_lifecycle.inc`, `engine/app_robust.inc`, `android/app/src/main/java/org/ra2/engine/RA2Activity.java`; save tests | P1 |
| 15 | **Tests / known failures:** extensive native component tests, script harnesses, render harness, Android Java tests and diagnostic reports are present. | CMake registers tests across assets, gameplay, UI, campaign, pathfinding, networking and saves. Latest Android debug assembly succeeded during this audit. | This audit did not run the native test suite. Old status documents record different revisions and historical failures; consult current sources and rerun relevant tests before treating a row as current. APK compilation is not runtime verification. | `tests/`, `scripts/play/`, `tests/render_stability/`, `CMakeLists.txt`, `docs/gap/PLAYTEST-STATUS.md` | P0 gates |

## Five highest-priority problems

1. **Bridge cell ownership, depth and flicker:** reproduce the reported bridge
   overlap on a named map and compare loaded bridge facts, unit deck state and
   draw order. Do not apply a blanket depth offset.
2. **Yuri feature completeness:** Magnetron lift/pull/drop is a confirmed
   missing simulation owner; the new `IsMagBeam` renderer fixes only its
   missing attack wave. The Psychic Dominator/Genetic Mutator, Grinder and
   garrison paths need rule- and mission-specific verification.
3. **Campaign coverage:** Yuri's Revenge campaign catalog/loading is incomplete;
   add reproducible mission-level checks for scenario selection, trigger
   events, objectives and progression rather than mission-specific hardcodes.
4. **Combat and presentation parity:** validate actual weapon projectile,
   superweapon, impact, particle-chain and death-animation behavior against
   owned retail data. Existing data parsers and tests do not cover all visual
   outcomes.
5. **Path traffic and mod compatibility:** gather a reproducible congested-map
   trace, then fix the demonstrated route/collision cause. In parallel, make
   the mod compatibility report explicit about unsupported keys and extension
   dependencies; current profile support is data-overlay support, not arbitrary
   engine-logic support.

## Recommended development roadmap

1. **Reproducible acceptance cases:** capture exact map, theater, unit orders,
   assets/profile identity and diagnostics for bridge flicker, Magnetron attack
   and lift, path congestion, and reported campaign failures.
2. **Close focused vanilla gaps:** fix one root mechanic at a time and add a
   regression test at the simulation or presentation seam. First milestone:
   bridge ownership/rendering case with a synthetic map fixture and a real-map
   device replay; preserve the current pathing and touch behavior unless that
   reproduction implicates them.
3. **Yuri and campaign progression:** implement Magnetron lifecycle as saved
   deterministic simulation state; verify each Yuri campaign map and trigger
   progression with harnessed scenarios.
4. **Effect and death audit:** compare a curated unit/superweapon matrix using
   original game assets, including hit timing, target type, palette, depth,
   particle chains and InfDeath variants.
5. **Mod compatibility increments:** publish key-level support reports,
   test overlay precedence and save/network fingerprints, then add high-value
   data-only mod features. Keep Ares/Phobos DLL-backed logic explicitly
   unsupported until separately implemented.
6. **Multiplayer and Android acceptance:** run same-content Android peer
   sessions, Android/PC sessions where enabled, long matches, resume/background
   cycles, and device performance captures.

## Immediate milestone and selected improvement

**Milestone:** accurately present the vanilla Magnetron primary attack while
preserving its separate simulation semantics. Current evidence: `TELE` uses an
`IsMagBeam=yes` primary weapon and `MagnaBeamColor`; vanilla references describe
a colored wave along the path with direction depending on vehicle targeting.
The source previously parsed `IsLaser`, `IsRadBeam` and `IsLine` but not
`IsMagBeam`.

**Implemented in this audit pass:** added `IsMagBeam` parsing, a magnetic wave
render kind, `AudioVisual.MagnaBeamColor` resolution, and vehicle-target path
orientation. The wave shares the existing presentation beam clock and remains
cosmetic; it does not implement unit lift, pull, damage timing or drop. Build
success is verified, but visual parity requires an asset-backed live match.

## Audit caveats and evidence

- `README.md` has stale contradictory Yuri status text. The newer source,
  `docs/YURI-FIDELITY50.md`, `docs/MOD-PROFILES.md`, `docs/MODDING-COMPATIBILITY.md`,
  `docs/VERA20K-EFFECTS48.md` and tests provide more specific subsystem
  evidence; this audit does not infer full feature parity from any one document.
- The supplied Android report `RA2 game diagnostics
  0.9.0+gv0.9.0-150-g7c3dba0-dirty` was an Urban skirmish with no Magnetron
  activity and no prolonged-block counter. It cannot diagnose the reported
  Magnetron or prove pathfinding is generally correct.
- The focused `IsMagBeam` INI regression passed (1 case, 2 assertions). A full
  native run during an intermediate test edit reported 13 failed cases; one
  save-layout failure came from a temporary `WeaponSlot` field that was
  removed before the final test build. The post-revert full run terminated
  abnormally before its summary; its partial output still showed failures in
  map parsing, `BuildingTypeDef`/`World` save-field invariants, settings
  serialization, unusable local MIX fixtures and an enemy/alliance truth-table
  case. These results need triage and are not attributed to the magnetic-wave
  change. Live-device visual/gameplay tests were not run. The
  Android debug APK assembled successfully; the compiler still reports the
  existing `waypoint_cell` missing-`override` warning.
- Post-revert filtered suites passed: pathfinding (1 case/9 assertions),
  campaign (27/178), bridges (27/493), and the magnetic-weapon INI regression
  (1/2). Asset-dependent cases explicitly skipped because the retail install
  and maps were unavailable. These filtered passes do not override the full
  suite failure.
- External vanilla behavior references used for this focused improvement:
  [IsMagBeam (ModEnc)](https://modenc2.markjfox.net/IsMagBeam) and
  [VERA20K upstream project](https://github.com/YuriPlanet/vera20k). These are
  comparison sources, not proof that this fork matches their implementations.

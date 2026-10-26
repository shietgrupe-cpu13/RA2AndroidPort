# VERA20K investigation and local integration — 26 October 2026

This extends `THREE-ENGINE-INVESTIGATION.md` and `GAMEPLAY-INVESTIGATION45.md`.
It does not replace their history or imply the remaining fidelity work is done.
The investigation initially permitted local work only. The user's subsequent
explicit instruction authorizes commits/push and APK delivery exclusively to
Google Drive after validation. No main/development-branch merge, history rewrite
or GitHub APK release is authorized by that delivery instruction.

## Revisions and evidence limits

| Repository | Current inspected revision | Notes |
|---|---|---|
| Android fork, default `android-touch` | `a4577e913961000e8a0b24c2a11abb3186067550` | Default branch is older than the delivered build. |
| Android fork, delivered `fix/yuri-gameplay-45` | `857fbfeff16302aecc04e96076bb7288922db196` | Local development starts here, retaining the tested Disc drain and guard changes. |
| Omar upstream | `a8cfdccfef8592c13a6517c3fb76d834b1534027` | Unchanged from investigation44; C++17/SDL2, 0BSD. |
| VERA20K | `1cca505b7da69b019cdb7f24d083ce9f8d654d86` | GPLv3; latest merge adds opt-in single-player WebAssembly. |
| Gagarin | `f03baaf2fdc42e6a95cbd52698348fc209624272` | GPL reconstruction; latest commit concerns TagClass destructor ownership. |

The GitHub commit endpoints and complete recursive VERA/Gagarin tree inventories
were fetched. VERA's comparison from the previously inspected `8be6015` contains
two commits and extensive platform/application changes. It also modifies path
batching, frame pacing and rendering support, so the merge title alone is not
evidence that every runtime path is unchanged. Relevant current files were read.

VERA's README explicitly calls campaigns incomplete, AI partial, and its
30-client/20,000-unit validation functional rather than proof of real-time speed.
No foreign test suite, native oracle or original game executable was run here.
Checked-in VERA tests are inspected evidence, not tests executed by this project.

Gagarin's current `reccmp-project.yml` still requires original executable SHA256
`1cdd1180e49024fbda8ad568caac2e86e856063ff67ab38f62b7d2c7bb84298c`.
The user's read-only local executable, previously hashed as
`3e81a61775d2745d1dabe397325ef663cd994ffc194da4e998e3bf5d2d308600`, differs.
Therefore native-address comparisons are published research, not a locally rerun
v1.001 oracle. The latest large match manifest could not be read through the
connector; no newly measured current match percentages are claimed.

## Architecture and transferable ideas

Paths below are pinned to the VERA revision above. Its two crates separate
GPU-free core state from the application/rendering consumer. Our existing
`World`, commands, saved state and presentation layers remain the foundation.

| Area | Actual VERA owner / evidence | Current fork | Decision |
|---|---|---|---|
| Determinism | `docs/index.md`, `SimRuntime::advance_frame`; `src/sim/replay_determinism_tests.rs::determinism_repeatability_same_inputs` | `World::step`, command log, `save_world`, sync hashes and lockstep tests | Reuse the methodology: fixed inputs, seed, phase and save continuation. Do not replace existing arithmetic with Rust types. |
| Scheduling | `src/app/match_runtime/frame_pacer.rs`: app owns wall-clock pacing; sim does not read it | `main.cpp` logic accumulator and fixed-step World | Preserve the boundary. A fixture's tick interval is not proof of one universal retail game speed. |
| Pathfinding | `src/sim/movement/path_batch.rs`; `src/sim/pathfinding/{core,zone_incremental,zone_hierarchy}.rs` | `pathfind.cpp`, occupancy layers, `world_loco.cpp`, `World::order_unit_move` | Batched searches explicitly delay/reorder work and are documented as a proposed sim-scale deviation. Do not import as a vanilla bug fix. |
| Assets/memory | `src/assets/asset_manager.rs`: ordered archive registration, shared payloads, lazy loose-file snapshots and bounded reads | `MixFile::open_path/open_nested`, `AssetFS`, lazy shared archive views | Existing system already avoids whole-archive copies. Audit source precedence and payload limits individually instead of rebuilding it. |
| Rendering/cache | `src/render/sprite_atlas.rs`: palette-index rectangles shared across contexts; growth pages; `terrain_draw.rs`: ordered destination/depth edits | `TextureCache`, `VoxelAnimationCache`, raster/facing cache, layered object rendering | Indexed atlases are a useful candidate, not a drop-in wgpu port. Memory, depth, palette and device draw-call measurements must precede adoption. |
| Interpolation | `docs/render-interpolation-prototype.md`, previous/current presentation snapshots and opt-in A/B | Existing read-only unit/projectile/transition interpolation | Compare discontinuity handling and capture behavior; do not interpolate simulation or change path arrival to smooth a frame. |
| Large battles | README gives bounded functional scale validation | No equivalent controlled Android benchmark in this session | No claim that either engine is faster on a POCO X7 Pro. |
| Mods/INI | `docs/ini-roots.md`: complete rules/art/AI triplets, frozen source identity, conservative ambiguity rejection | Current retail roots and partial tag support | Root selection alone does not make Mental Omega playable. Sound/UI/archive and Ares/Phobos tags need their own capability audit. |

One independent CPU lookup optimization was measured before retention. With512
units,256 active statuses and10,000 hostility queries, five Windows Debug-build
samples measured398.168–400.924ms before, and2.1673–2.3225ms after. The median
changed from399.340ms to2.1807ms (about99.45% less time for this microbenchmark).
The correction replaces repeated active-status scans with a checked current-World
index and direct status lookup. Hostility counts and serialized state stay equal.
This is **not** an Android, release-build, frame-rate or thermal measurement.
The earlier raster benchmark remains historical evidence. Proposed atlas/path
batching work was not enabled without its own workload measurements.

## Gameplay crosswalk and priorities

Evidence grades: original instruction/control provenance is stronger than an
unverified implementation, but each native corpus has declared service seams.
Neither a function name nor a unit test count certifies a complete original game.

| Priority / issue | VERA / original reference | Fork owner and conclusion | Minimal next correction / verification |
|---|---|---|---|
| P0 Chaos gas | `src/sim/combat/{damage/receive,receiver_health,world_receiver}.rs`; Techno receiver `0x701D6B..0x701DBA`, countdown `0x6F9F0D..0x6F9F31` | Generic `combat.cpp::damage_area` previously treated control Damage as HP; local new `world_chaos.cpp` owns status | Implemented and refined below. Whole-PC frame ordering and rendered tint remain acceptance items. |
| P0 Disc normal attacks | `src/sim/world/disk_laser.rs`; `tools/spatial_oracle/disk_laser.md`: ctor `4A7A30`, Fire `4A71A0`, AI `4A7340`, Detach `4A7900` | `disk_laser.hpp`, `pres_fx.inc::add_beam/draw_beams` still use an immediate static approximation | Need a saved charge/discharge owner, current FLH/target queries and final-hit/audio timing together. No cosmetic-only completion claim. |
| P0 Disc drain | Gagarin `match/movement/locomotion_begin_drain_methods.h`, BeginDraining `70FD70`; VERA's DiskLaser is a separate feature | Existing `world_drain.cpp` already implements building-specific links and wallet/power behavior | Preserve; compare multi-source cleanup, hover footprint, first transfer and audible DISKRAY against PC. Do not duplicate or remove the controller. |
| P0 Magnetron | `src/sim/locomotor_lift.rs::{locomotor_detonation,imbue_locomotor,release_locomotor}`; `tools/spatial_oracle/magnetron_aircraft_lift.md`; Gagarin `techno_class_release_locomotor_methods.h` (`70FEE0`) | `weapon_resolve.cpp` recognizes the primary locomotor role; `combat.cpp` safely suppresses nominal5000 HP; no full lift/wave owner | Saved exclusive links, eligible ground/landed actors, movement lock, pull/release/drop/collision/kill credit first; separate depth-sensitive wave rendering. Do not substitute an ordinary beam or damage. |
| P1 Brute/melee | Original guard/threat owners and VERA acquisition/receiver separation | Existing45 guard-home fix and bounded ACQUIRE diagnostics | The latest log's sampled enemy was unseen and ~45cells away, not a reproduced nearby-target refusal. Require a visible legal target witness before expanding aggression. |
| P1 Mind control | VERA `src/rules/mind_control_rules.rs`, `tools/spatial_oracle/mind_control_tags.md`; Gagarin drain/lift helpers are not CaptureManager | `world_yuri.cpp::{prepare_skirmish_mind_control,resolve_skirmish_mind_control,release_unit_mind_control}`, campaign remains `CampaignRuntime` | Audit eligibility, capacity, owner transitions, event callbacks and release independently. Deployed controller retains existing victims; do not reintroduce release-on-deploy. No new mind-control fix claimed. |
| P1 AI | `src/sim/ai_base_building.rs::update_production_choices`, `ai_unit_choice.rs::choose/harvester_choice`; original chooser/production owners | `World::step_ai_houses/step_ai_teams`, existing prerequisite and recovery logic | VERA documents a distinct 8-frame chooser and mode machine; our base pass has broader fallback scheduling. Changing a clock alone is unsafe. Reproduce specific production and RNG decisions first. DEBUG-AI remains separate requested work. |
| P1 Movement/production | VERA locomotor/path hierarchy and factory output native controls | `factory.cpp::exit_cell` explicitly drops subcell ExitCoord and uses a deterministic ring fallback; `world_mcv.cpp` already has accepted deployment fixes | Compare factory-door source/offset and occupancy as one transaction; no new global pathfinding or accepted Bunker rewrite. |
| P1 Map/depth | `src/render/terrain_draw.rs` and terrain/native controls; Gagarin `map_class_update_pathfinding2_methods.cpp` (`586990`) refreshes dirty subzones, not whole A* | Supported theater rendering and bridge layers exist; DESERT/NEWURBAN/LUNAR intentionally filtered | Compare entry/middle/exit, deck and ground depth separately. Do not merely unfilter unsupported maps or treat zone refresh as a complete planner. |
| P2 Gatling | `src/sim/combat/gattling.rs`: independent stage/value/report latch, increase `70DE70`, update `70E000` | `gattling.hpp` derives stage from charge; `weapon_sound_loop.hpp` supplies a partial report owner | Retained stage and exact call phase needed; normal derived-threshold tests alone are insufficient. Requires saved-state migration and audio attack/body/decay controls. Tank audio accepted; Cannon audibility still unverified. |
| P2 Nuclear | `src/sim/superweapon/nuke.rs::{launch,impact}`: launch, building missile mission and falling warhead are separate; NUKEBALL delays impact | `world_sw.cpp` and `pres_game.inc` already have separate events and diagnostics | Reproduce ready/launch/ascent/descent/impact/recharge against PC; no nuclear visual fix or device acceptance claimed. |

VERA source links are formed from
`https://github.com/YuriPlanet/vera20k/blob/1cca505b7da69b019cdb7f24d083ce9f8d654d86/`
plus the exact paths above. Gagarin links use
`https://github.com/r-melvin/gagarin/blob/f03baaf2fdc42e6a95cbd52698348fc209624272/`.
These are references, not permission to copy their GPL implementation.

## Changes integrated locally

### Chaos receiver, targeting, presentation and save state

The original user-owned rules give CAOS an AreaFire Psychedelic weapon. Its
Damage is a duration, not an ordinary hit. The independent implementation uses
the existing area collector for recipients, layers and Verses; it does not add a
persistent damaging gas volume just because CDGAS remains visible.

`chaos.hpp` and `world_chaos.cpp` add a saved status owner. Deploy/direct attack/
ground fire release from the source; expiry, immunity, re-exposure, source death,
human command admission and ownership-preserving hostility are covered by tests.
The normal HP/projectile path is unchanged for weapons outside this policy.
`command.cpp` blocks human orders during the status, leaving the existing scripted
and computer command paths separate. `combat.cpp` uses the status during target
choice without changing global house alliances. Affected workers must not have
their Hunt interrupted by mobile-manager follow orders.

The initial unfinished patch incorrectly used the recipient's AoE distance for
duration and retained the longer old timer. Current VERA receiver research states
the original uses the damage kernel at literal distance zero and writes the new
timer. Re-exposure does not repeat the first-hit team/target/Hunt transition.
New focused tests reproduced the old behavior as failures; the correction is
independently written against those behavioral facts, not a translation of the
Rust implementation.

`pres_game.inc::fire_fx` emits CDGAS at the source's ground anchor. Existing
Flat=yes handling supplies the ground draw pass. `pres_state.inc` uses the
retail-selected ColorAdd for visible status feedback; exact original packed
RGB565 composition and audible gas timing were not captured on a phone.
`report_probe.hpp` adds bounded BERSERK entries rather than per-frame tracing.

Save format57 appends status state, validates links/timers, includes gameplay
state in deterministic serialization, and reads55/56. Earlier APKs cannot read
new57 saves. Default unconfigured worlds retain their older base byte sequence.
Original full active-object ordering, signed/nonpositive custom-warhead values,
all arbitrary mod weapon combinations and every campaign/team callback are not
certified by these focused tests. The default behavior is scoped to the existing
AreaFire Psychedelic policy; this is not full mod compatibility.

### Yuri skirmish loading palette

The newest screenshot `1J7Vga1QT-dZLaUCiutwWVOO63gV9EQpA` shows corrupted bright
colours and white panels. Root cause is confirmed: build45 paired LS800YURI.SHP
with classic MPLS.PAL. The expansion's correct palette is **MPYLS.PAL**, present
in `ra2md.mix/loadmd.mix` with the image. A private local decode using that pair
visually restores the purple art and black panel. No retail image is bundled.

`faction_art.hpp::loading_country_palette`, `skirmish_load.inc::load`, and
`test_yuri_menu.cpp` now pair every playable country explicitly. Tests verify all
eleven aliases' image/palette availability and the Yuri panel colour. This is a
palette correction, not complete replication of the PC loading-screen layout.
The earlier audit41 claim that classic MPLS was Yuri's correct palette is
superseded by this screenshot and decoded asset evidence; history is retained.

## Verification, licensing and remaining gates

Validation logs and final local commit/APK identities are recorded in the local
delivery manifest after completion. Preliminary loading checks passed3 cases /
52 assertions. The new native-contract tests failed2 cases before the duration/
refresh correction, then passed7 cases /63 assertions. Source-death/command-return
coverage was added afterward; final results must be read from the final run.
An earlier full run passed1292 cases /277858 assertions but predates those final
refinements and is not presented as final-source verification.

One unavailable real-install landmark fixture is excluded consistently with prior
builds. Some other external-oracle tests early-return when optional corpora are
absent; suite success does not mean they executed original-game comparisons.
No physical POCO, emulator, GPU, thermal, full retail battle or VERA suite run
has occurred. Existing shell, Force Fire, Bunker, walls, touch and save tests must
remain green. Nuclear and exact Disc/gas presentation still need gameplay review.

Original Omar engine license/credits, Bryle13 changes and dependency notices are
retained. No VERA/Gagarin code, lookup table, Rust implementation, hook binary,
retail asset or signing credential is added to the application or tracked source.
VERA's GPL license is respected as research-only for this permissive integration.
No Android FPS gain or complete gameplay parity is claimed. Verified delivery
identities and results are recorded separately when those actions complete.

Next highest-risk work: Disc retained discharge; Magnetron lift/drop and waves;
original Gatling stage/audio owner; DEBUG-AI; measured traffic and theater support.
Each needs its own saved-state/phase specification and reproducer, rather than a
large unverified engine replacement or a direct GPL port.

## Final native verification for the packaged source

After the worker, migration, receiver and lookup corrections, the final suite
passed **1296 cases /277885 assertions**, with the one previously unavailable
landmark fixture excluded. Log: `build-tools/test46-final-suite.log`.
Focused Chaos suite: **10 cases /81 assertions** passed, including the benchmark's
state-invariance checks. Logs retain the pre-correction failures and the before/
after timing samples; no failures were relabeled as successes.

Local functional commits are `9cbdd89` (loading palettes), `05f5f94` (Chaos status)
and `481faa1` (lookup optimization). This report/build-number commit follows them.
The delivery manifest records the final full source SHA and any actual ARM64
build, signature/checksum, push and Drive verification. Those actions must not be
inferred from passing native tests alone.

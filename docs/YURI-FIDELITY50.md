# Build 900050: launch geometry, area damage, navigation and map importing

2026-10-27. Native C++17 fork; baseline `76c08f69ea6150a43a6d08ac2daf9e620f2d3950`.
This is an implementation checkpoint, not a claim that every requested mechanic
or map pack now works. Final commit/APK receipts belong in the existing Drive tracker.

## Implemented

* Missile spawns use the launcher's weapon slot FLH in body coordinates. Even
  missiles apply `SecondSpawnOffset` before lateral mirroring. Retail BSUB uses
  secondary `(0,0,-40)` and second offset `(-70,0,0)`; DRED uses primary FLH.
  The launcher weapon's own `DecloakToFire` governs the retained surface hold,
  instead of treating the Boomer torpedo's flag as the cruise launcher's flag.
  Existing CMisl RaiseRate and normal shell/projectile solvers are retained.
  Profiles are registered from the complete rules registry, independently of the
  local player's faction, and saved/hashed. Visual parity still needs a phone test.
* Nuke and Weather Storm impacts now reuse the existing `damage_area` combat
  owner: native ring membership, per-foundation-cell hits, armor/falloff and
  invulnerability. Normal vehicle projectiles are unchanged. Regression: a
  centered blast on a 3x3, 1000-HP concrete silo with stock NUKE values used to
  leave 952 HP; it now leaves 616 HP (48 + 4*43 + 4*41 damage). The 600 base
  damage and 8% concrete multiplier were not increased. Radiation remains separate.
* Z / MORE -> ROUTE Z captures up to 64 movement picks, shows numbered nodes,
  and commits on a second press/tap. This is a toggle affordance; keyboard
  release-to-commit and queued attack/enter/capture actions are not implemented.
  Committed movement uses existing formation, A*, collision and movement-domain
  handlers. Remaining legs are simulation state, not a renderer timer. Stop,
  replacement commands, death, boarding and loss of command authority cancel it.
  Uncommitted plans are local UI state and are discarded on load/cancel.
* Launcher -> Touch controls & hotkeys selects primary dock actions and its
  initial collapsed state. MORE and control groups remain accessible. Existing
  target sizes, attack move, force fire, selection, production and pinch remain.
* Yuri's RADARY uses its actual RADARYURI.PAL; shared Soviet chrome keeps its
  own SIDEBAR.PAL. Palette correction covers closed, boot/open and closing frames.
  This does not recolor the entire sidebar or replace imported retail artwork.
* Main-menu click/combo sounds initialize even when optional theme music fails.
  Sound mute/focus/settings still apply; physical-device audio is unverified.
* The SAF importer recognizes loose .yrm/.map/.mpr files and stores them beneath
  the app's `ra2/maps` folder, leaving original files untouched. The catalog,
  previews, loading, save map token and multiplayer content digest use the same
  source-aware reader. Only safe leaf filenames and files <=16 MiB are accepted.
  Packed terrain, dimensions, starts, theater and unsupported scripting are checked.
  Supported INI overrides use the existing campaign rules merger; vehicle roster
  and spawned vehicle art/stat loading receive those scenario rules rather than
  silently reopening unmodified base rules. This also avoids repeated base-rules
  parsing in that path; no FPS/loading-speed gain is claimed without a benchmark.
* Format5 decompression now bounds cumulative output to 16 MiB before the next
  chunk is decoded/allocated. A regression exercises a small compressed expansion
  exceeding this bound. Retail map format terrain is below 3 MiB; overlays are 256 KiB.

## Map compatibility limits

The Zoom3000 publisher describes 200+ maps, not a verified 300-map count. Its RAR
could not be downloaded: ordinary ModDB access returned 403 and old mirror links
were expired. No bypass was used and the archive has not been audited.

Extract a legally obtained pack yourself, then use the existing folder importer.
The game scans `maps/` and reports rejected maps in diagnostics. Import copies
documents; eligibility is decided by the engine. No RAR extraction or retail map
assets are bundled in the APK. There is no 300-entry catalog cap.

This first implementation deliberately refuses nonempty trigger/team/script/tube
sections and map-specific disabled AI-trigger filtering. DESERT/NEWURBAN/LUNAR
remain filtered pending rendering validation. A source-level eligibility probe
accepted 6/91 retail Battle maps converted to loose files; 85 required map
scripting and were rejected. That is a format regression sample, NOT a Zoom3000
compatibility percentage. The normal archived retail catalog remains unchanged.
INI support means keys already implemented by this engine; arbitrary mod logics,
new theaters, Ares/Phobos DLLs and Mental Omega are not certified.

## Verification

Native suite: **1333 cases / 280151 assertions passed**, one unavailable landmark
case excluded. Optional RA2_TEST_MAP, ecache01 and XCC oracle fixtures were absent;
their guarded tests can return without executing those external comparisons.
Focused checks cover original and legacy pointer schemes, route ordering/cancel,
save/load continuation, launch coordinates/surface hold, palette bytes, map
validation/catalog reads, storm damage and all three engineer factions repairing
a synthetic destroyed high span without capturing its hut.

The silo regression was observed failing at **952 HP** before the correction and
passing at **616 HP** afterward. The production touch-panel harness passed
capability, disabled-action, capture, slide-off, group, menu and ROUTE routing.
The updated `ctl-touch-w6.play` script is supplied but was not executed in a
desktop/emulator/phone renderer. Android C++ syntax passed with one pre-existing
missing-override warning. Final APK build/signature/upload checks are recorded
separately after completion. No POCO, emulator, GPU, sound-output or thermal claim.

Save format **60**, with an append-only tail for routes/launch profiles; reads
55-59. Keep backups and use identical builds on network peers. Existing DEBUG-AI,
Disc, Chaos, Bunker, building Force Fire, Citadel walls, MCV and shell behavior
remain covered by the existing regression suite.

## Remaining investigation, not implemented fixes

* Full high-bridge span/ramp/cell ownership and depth rendering. The loader in
  `world_depth.inc::depth_set_terrain` currently constructs high-deck facts only
  from stored BRIDGE1/2/BRIDGEB1/B2 overlay cells. A retail probe of Yuri's Plot
  found six stored anchors, not a complete deck grid. Original bridge flag writers
  populate additional cells and bridge/ramp tile state. This is a confirmed source
  discrepancy and a suspected cause of the reported overlap/funneling; no GPU or
  in-game reference replay established the full cause. Do not widen every road,
  disable occupancy, or change only a drawing offset. Engineer role handling itself
  already accepts every faction and passed a new end-to-end command regression.
* SpyPlaneSpecial, ForceShieldSpecial, GeneticConverterSpecial and
  PsychicDominatorSpecial remain explicitly unsupported by `rp_sw_type_of` and
  the effect dispatcher. Adding their enums/buttons alone would not implement
  aircraft passes, building-only shields/blackout, mutation receiver/anim spawn,
  or staged permanent capture/lighting. They still need saved controllers and tests.
* Rocket payload building damage still uses the older `an_area_damage` owner;
  its unification is separate from the now-correct nuke/storm path. Exact missile
  hull presentation, all rotations, interception and impacts need phone acceptance.
* Magnetron lift/wave, Disc exact authored charge-ring parity, normal-AI expansion,
  traffic deadlocks, full map scripting and broader mod profiles remain open.

## Evidence and licensing

* Local owned v1.001 rules/art: BSUB/CruiseLauncher, NAMISL/NUKE/NukePayload;
  SIDE C02MD filenames and palette bytes; no retail data distributed.
* [Ares CellSpread documentation](https://ares-developers.github.io/Ares-docs/new/warheads/cellspread.html)
  documents original multiple foundation hits and the extension's optional hit cap.
* [SecondSpawnOffset](https://modenc.renegadeprojects.com/SecondSpawnOffset) and
  [DecloakToFire](https://modenc.renegadeprojects.com/DecloakToFire).
* Existing fork `docs/re/combat.md` DamageArea findings; `combat.cpp::damage_area`.
* VERA20K pin `086b5f58632bbf58a6c784ccd1b7559ccaf14db4`:
  `src/sim/superweapon/{nuke,force_shield,genetic_converter,psychic_dominator,spy_plane}.rs`,
  `src/map/bridge_facts.rs`, `src/map/resolved_terrain/bridge_flags.rs`,
  `src/sim/bridge_state/ordinary_repair.rs`, presentation instances/bridges.rs.
  Its nuke notes retain presentation residuals; extension features are not assumed
  to be original mechanics merely because their code exists.
* Gagarin pin `2df2acf7564e778368b100ec5d5b78c9a42ffac6`:
  `match/map/cell_class_bridge_tile_queries.cpp` (native tile-window queries),
  `match/map/bridge_span_collapse_family_methods.cpp` (**PARTIAL**, reported best
  73.7%/59.6% instruction match; not a fully verified replacement engine).
  Local read-only disassembly also confirmed the bridge direction/state writer
  at 0x47E040; no generated retail code/opcodes were added to this repository.
* redalert2-recomp pin `d379c7d1a9aed55d28fdf13a9b7b828d730014fc`: authored MIT
  tooling/docs are distinct from generated/lifted EA code excluded by NOTICE.
  No setup or reconstructed game code was executed or imported.

All corrections are independent work in the existing permissive C++ architecture.
VERA20K/Gagarin GPL source was reference material, not copied or translated.
Omar El Arab's 0BSD copyright/license, dependency notices and Bryle13 fork credits
are retained. No EA retail assets, signing secrets or DLLs belong in commits/APK.

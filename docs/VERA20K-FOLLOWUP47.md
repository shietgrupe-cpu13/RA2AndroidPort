# Build 900047: Yuri presentation and an economy-only test opponent

26 October 2026. Extends VERA20K-INTEGRATION46.md and the living tracker;
earlier findings are not deleted or relabelled as current device verification.

## Evidence and scope

Source baseline: fork research/vera20k-integration-46,
58260cc55495833e801871ed76803320c91d2fd0. Remote android-touch is still
a4577e913961000e8a0b24c2a11abb3186067550. This batch uses a separate branch,
fix/yuri-debug-ai-47; it does not merge either branch or rewrite history.

Latest references checked: VERA20K
98bd8e6097f6dc13dc7ad83d9b61b4bff5aaa005 (2026-10-26 11:10:34 UTC),
Gagarin f03baaf2fdc42e6a95cbd52698348fc209624272, and Omar upstream
a8cfdccfef8592c13a6517c3fb76d834b1534027. References are GPL research only;
no Rust/C++ implementation, literal ring table or original assets are copied.

Drive instructions and tracker were fetched again. Latest accessible diagnostic
is still build900045 / 857fbfe, uploaded04:00:31 UTC. The latest screenshot
and Disc reference MP4s are the same files inspected for46. No900046 diagnostic,
new Bunker capture or sidebar screenshot appeared in the current folder listing.
The user's new observation is evidence of a symptom, not a captured reproduction.
The older diagnostics identify Android16 on Xiaomi2412DPC0AG; they cannot prove
this patch's phone performance or behavior. The900045 report remains description-
blank and is not evidence of a new crash in46.

## Implemented corrections

### Yuri sidebar (confirmed asset-resolution defect)

The old loader required a complete sidec03.mix or sidec03md.mix, otherwise
selected Allied chrome/style. Neither complete archive exists in the inspected
retail installation. Its ra2md.mix/localmd.mix/sidec02md.mix has7 entries,
including radary.shp, but no top.shp or sidebar.pal. Retail Yuri inherits Soviet
chrome and overlays its own pieces; this is not evidence that all its controls
should receive newly invented purple artwork.

main.cpp now retains Yuri style, uses ra2.mix/sidec02.mix as its stock base,
and resolves Yuri overrides through sidebar_assets.hpp. Complete custom third-
sidebar archives remain a supported earlier path. The regression decodes all
ten required base SHPs, the Yuri radar and the768-byte base palette. Existing
sidebar geometry tests still apply; exact phone composition/touch positions need
user verification. Other factions' asset selection is unchanged.

### Chaos Drone vent (requested presentation correction)

46 explicitly emitted at ground height, and CDGAS's Flat=true put it underneath
the Drone. The imported CAOS hull has one section with top bound20.871 model
units. artmd has no authored PrimaryFireFLH on CAOS. No height is invented in
the game rules: presentation registers each imported hull's bounds and projects
its upper central point through the existing slope matrix and voxel basis.
gas_emitter.hpp owns only the screen offset. pres_game.inc applies it to the
gas; a per-instance object-layer override prevents the Flat type from hiding
that vent emission. Ordinary Flat effects retain their existing draw pass.

This does not change AoE origin, damage/status, recipients, duration, ownership,
orders, RNG or saved simulation. Facing rotation cannot move an axial centre
point; terrain tilt can. Exact native emission anchor, HVA-deformed custom
models and phone appearance are not certified by a hull-bound calculation.
The retail cloud art still controls its frame timing. No global projectile
offset or lighting change is made.

### DEBUG-AI (explicit testing feature, not vanilla AI)

Skirmish opponent menu now has DEBUG-AI, persisted as an engine-local slot type7.
It is independent of Easy/Medium/Brutal and uses neutral Normal table indices.
The explicit mode is saved through the existing difficulty field. Save format58
rejects older readers while retaining the unchanged57 field layout and55/56/57
read support. Old APKs cannot read new58 saves.

debug_ai.hpp sanitizes only Debug configurations: no defense/wall/naval/SW plan,
no trigger/team creation, no combat-unit roster, no offensive SW firing, and no
base-response orders. It retains base construction, repairs, economy, harvester
and MCV recovery. Starting combat forces are omitted only for this mode.
Ground unit and building combat gates keep economic actors from firing back;
they remain valid targets. Economy units include armed War Miners in their
economic role, not an extra army. Optional stationary-target presets are not
implemented in this batch.

Yuri can be explicitly selected for DEBUG-AI. A confirmed two-side-only helper
previously treated Yuri as Allied when resolving country and AIBasePlanningSide.
The helper now honors ThirdSide/YuriCountry/planning side2, as the original
YAREFN rules require. SMIN/PCV are appended to the shared asset roster; previous
indices remain unchanged. Existing slave management remains responsible for
YAREFN workers. Original Normal/Easy/Brutal policies are not globally changed.
This is not a claim of complete Yuri combat AI or of original expansion parity.

### Disabled legacy ore timer (confirmed hang)

An economy regression with zero growth interval stalled. Isolated tests showed
OreField::step's legacy seconds loop repeatedly subtracting zero. The guard
refuses nonpositive/nonfinite intervals before timer/RNG mutation. Positive
intervals and the original frame-based owner retain their prior paths. This is
an API/input robustness fix, not proof that the user's ordinary match froze for
this reason. The initial timed-out fixture and compilation-error logs are kept.

## VERA comparison: remaining work and smallest safe next steps

All VERA links below use this prefix plus the exact path:
https://github.com/YuriPlanet/vera20k/blob/98bd8e6097f6dc13dc7ad83d9b61b4bff5aaa005/
Gagarin links use:
https://github.com/r-melvin/gagarin/blob/f03baaf2fdc42e6a95cbd52698348fc209624272/

| Priority | Verified reference / fork owner | Result and next regression |
|---|---|---|
| P0 Disc laser | VERA src/sim/world/disk_laser.rs; tools/spatial_oracle/disk_laser.md; native ctor4A7A30/Fire4A71A0/AI4A7340/Detach4A7900. Fork disk_laser.hpp, combat.cpp, pres_fx.inc | Current VERA retains a charging shot and applies final discharge damage/audio; the fork still has an immediate/static approximation. A saved shot owner, cancellation, current source/target attachment, final-hit timing and scene restore must be changed together. Not implemented here. Test ground/air/force fire, source/target death, moving aim and mid-charge saves. Preserve world_drain.cpp building interactions. |
| P0 Magnetron | VERA src/sim/locomotor_lift.rs; original ImbueLocomotor710000/ReleaseLocomotor70FEE0; Gagarin match/movement/techno_class_release_locomotor_methods.h. Fork weapon_resolve.cpp/combat.cpp | Suppressing nominal5000 lift damage is not a completed lift implementation. Need exclusive source/victim links, destination refusal, climb/hold/drop, release callbacks, landing/water/bridge/collision and kill credit. Wave geometry is separate. Not implemented here; no ordinary damaging substitute. |
| P1 Bunker smoothness | Fork building_transition.hpp, world_depth.cpp, unit_presentation.hpp and main.cpp draw ordering. VERA docs/render-interpolation-prototype.md | Existing docking/alignment/open/close/exit machinery already exists. No newer Bunker witness is available to prove a particular remaining defect. Preserve accepted behavior. Capture entry/exit at low speed and both render rates; compare coordinates, pose continuity and front/back hull occlusion. Do not equate VERA's optional interpolation prototype with full PC parity. |
| P1 AI difficulty/expansion | VERA src/sim/ai_base_building.rs; fork ai_base.cpp, ai_skirmish.inc, World::step_ai_houses/step_ai_teams | Original difficulty tables, repairs and expansion already have owners. DEBUG-AI and Yuri side selection are added; no blanket stronger-AI rewrite. Remaining scheduler/chooser/RNG differences need per-faction seeded construction/production traces. A clock change alone is insufficient. |
| P1 projectiles/rockets | VERA src/app/presentation/instances/projectiles.rs and its flight/render/voxel tests; fork projectile_presentation.hpp, pres_game.inc, world_an.cpp and pres_effects.inc | VERA retains display admission, raw lepton Z/bridge state, body/shadow and shared fractional XY sampling. RocketLocomotion actors and Bullet actors must remain distinct. Compare launcher FLH/burst/turret transform, ascent/trails, occlusion and impact. Existing normal tank shells are preserved, not rewritten. |
| P2 lighting | VERA src/app/presentation/lighting.rs and anim_lighting_tests.rs; fork light_sources.inc, pres_state.inc, voxel_raster.cpp and pres_fx.inc | VERA separates cell intensity, palette conversion, class extra light, global events and effect tint. A projectile is not automatically a dynamic light source. Validate retail Normalized/AltPalette/UseNormalLight/Extra*Light and scenario RGB before adding lights. No blanket glow/bloom, GPU speed or lighting-parity claim. |
| P2 Ares/Phobos | VERA docs/phobos-sidebar-gdi.md | This inspected feature is an optional Phobos sidebar-position extension with documented radar residuals. It is not proof all Ares/Phobos behavior is implemented or vanilla. Keep extension gates separate from stock rules. |

The original GetFLH weapon transform, slope matrix and burst flip already have
fork references in docs/re/voxel-render.md section6. Reuse them for specialized
effects instead of adjusting all shell origins. Native VERA component tests use
supplied state/service bounds; they are not a complete battle or Android result.
Gagarin function identities remain partial references, not newly certified match
percentages. No new original PC executable/hook capture was run in this batch.

## Verification and delivery record

Focused new coverage passed8 cases/117 assertions, including base construction,
harvester production, zero military production, no attacks, settings conversion,
save continuation, original sidebar assets, gas slope projection and the timer
guard. Final full native suite passed1304 cases/278003 assertions. The first full
run found a stale test that rejected the now-valid format58; the assertion now
checks rejection of the next unknown version and explicitly accepts57.
Build/signing/Drive results are recorded separately after completion.
One unavailable landmark fixture remains excluded; optional foreign oracle corpora
may early-return. No emulator, phone, GPU, audio listening or thermal test was run.
The synthetic economy test is not a complete retail three-faction AI playtest;
Yuri slave integration and exact vent/depth appearance need phone acceptance.

Credits, Omar's0BSD and dependency notices remain unchanged. Bryle13 attribution
is retained. No retail files, GPL code/tables, credentials or external DLLs are
added to tracked application source. APK delivery stays Google Drive only.

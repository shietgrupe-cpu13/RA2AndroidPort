# Deployment, status artwork and movement presentation — 900049

Research date2026-10-27 (client date). Baseline900048/source
ba9f15a6aa798719189386b1f40f5d0ed789aca0. Branch `fix/yuri-fidelity-49`.
This batch independently corrects verified defects. It does not complete
Magnetron lifting or certify every original PC animation.

## Context and newest diagnostics

Current Drive instruction folder, RA2YRINST.txt, Unified Instructions and living
tracker were consulted before implementation. Unified statements that Chaos and
DEBUG-AI are missing are historical: later user acceptance and actual900047/48
source supersede them. Existing tracker history stays intact. Source delivery
uses a separate VERA investigation branch, retaining android-touch unchanged.

Fresh source pins: VERA20K086b5f58632bbf58a6c784ccd1b7559ccaf14db4;
Gagarin2df2acf7564e778368b100ec5d5b78c9a42ffac6;
Omar upstreama8cfdccfef8592c13a6517c3fb76d834b1534027. Fork remote default
android-touch stilla4577e913961000e8a0b24c2a11abb3186067550; the newer actual
delivered development branch is fix/yuri-effects-48 at ba9f15a above.

New diagnostics:

* Drive1evQYihtqxceGWD4u56jedDasV1j2Z1yi,246314bytes,
  SHA2563f5f49117b537caf30d99268fd4c48921c0b3ffaeaf08ef995d62451432e4003,
  uploaded2026-10-26T17:03:35Z. App900048, Xiaomi2412DPC0AG, Android16.
  Current2535lines: zero ASSERT/FATAL/ASSET_MISS; seven traffic-block reports,
  six snapshots. Conscript contention reaches3.87s, later summaries recover to
  zero blocked>3s. No clean shutdown recorded yet; this alone is not a crash.
  Description blank and current map field blank, so don't invent a map identity.
  Previous session has three Gatling muzzle emissions and enabled loop handles
  at stages1/2; the rendered animation/audio still needs device acceptance.
* Drive1-Vm4JPkq8ozby_N--xWHikrvODqGmk68,76325bytes,
  SHA256f8830637e71eed2266fa07c37cc6ed696995a0b8a53ede08a7942b4b278afcd9,
  uploaded2026-10-26T17:01:24Z. App900048, INFINIX X6837, Android13.
  Current configuration is isleofoades.map with normal AI. Its rotated previous
  log has227 block reports,938 recoveries,136 snapshots and816 total unit
  records near the end. It lacks a session/version header in that retained
  tail; do not attribute all its battle history to a particular build/device.
  It ends with NORMAL EXIT. Recovery count is not proof all traffic is correct.

Xiaomi current CPU windows: median scene-render60.86Hz/2.095ms window average,
max118.782ms; world-step59.91Hz/2.167ms,max5.278ms. Infinix rotated battle tail:
scene59.5Hz/4.29ms,max85.079ms; world59.99Hz/5.071ms,max29.19ms. These are CPU
log windows, not GPU timing or thermal measurements. No measured speedup is
claimed. No new video was uploaded after the earlier Disc references.

## R-067 — Deployed GI anti-air legality (confirmed source defect)

Retail GI Para is a ground weapon; Guardian GI's deployed missile retains its
own AA capability. Deployment selects a slot, not permission to ignore its
projectile AA/AG or warhead Verses. VERA
[combat_weapon.rs](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/combat/combat_weapon.rs)
separates deployed selection from
[fire_error.rs](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/combat/fire_error.rs)
legality. Original Infantry SelectWeapon521610 is an existing fork research
reference; no new Gagarin byte-match claim is made.

engine/weapon_unit.cpp::unit_weapon_for_unit_base previously returned the
deployed slot unconditionally. It now validates that slot against target
altitude/armor. Building targeting applies the same ground/Verses gate.
Tests cover GI air rejection, legal ground fire, Guardian-style AA retention
and zero-Verses refusal. No global anti-air disable or Guardian GI nerf.

## R-068 — Deployed Desolator primary-fire separation

Retail DESO has a targeted RadBeamWeapon and self-area RadEruptionWeapon,
RadLevel500/AreaFire=yes. VERA
[deployment](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/deploy.rs)
and [deployed guard oracle](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/world/techno_ai/deployed_guard_oracle_tests.rs)
distinguish the self-area mission from ordinary targeting; the oracle is a
bounded component test, not a complete retail battle.

The fork already freezes deployed radiation actors in world_abilities.cpp.
The new report doesn't log enough deployment state to prove why the phone
appeared to target-fire. A synthetic boundary fixture reproduced ordinary
primary fire when the derived frozen cache was stale. New
weapon_unit.hpp::deployment_owns_fire guards selection and combat.cpp's
ordinary owner using the actual deployed radiation/self-area policy. It does
not disable radiation eruptions in world_abilities.cpp. Tests refuse ordinary
unit/building/primary fire during that state and permit primary fire again
after undeploy. Phone root cause remains unconfirmed; don't call the synthetic
cache case proof of the user's exact scene. DEPLOY_FIRE diagnostics expose it.

## R-069 — Attached bomb countdown and repair wrench (confirmed presentation gap)

Read-only retail archive inspection: ra2.mix/cache.mix/BOMBCURS.SHP has13frames,
WRENCH.SHP7frames and MOUSEPAL.PAL768bytes. conquer.mix/BOMB.SHP has30frames
and is not the clock strip. VERA
[ui_overlays.rs DrawExtras](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/app/presentation/ui_overlays.rs)
(6F5190) consumes the clock and repair toggle before health/rank overlays;
[bomb.rs](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/bomb.rs)
documents GetClockFrame438A00, configured fuse and flicker plus visibility.

engine/hud_pips.inc loaded the wrong SHP/palette, used a hardcoded451-frame
denominator and player0 visibility, and drew no building repair wrench.
It now loads the clock/wrench with MousePal, queries configured IvanTimedDelay
and IvanIconFlickerRate, uses the current local viewer, native building draw
anchor, and being_repaired toggle. techno_status.hpp contains read-only frame
queries. It never advances a fuse, repairs HP or spends credits. Hidden enemy
bombs require planter ownership or a local BombSight detector; this immediate
render query does not reproduce the native retained46-call visibility refresh.
Detector range currently samples units; exotic modded building detectors and
all original integer geometry boundaries remain unverified.

Important distinction: Tanya/SEAL C4 is NOT an attached Ivan BombClass timer.
VERA [c4_plant.rs](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/c4_plant.rs)
and fork c4_applied/c4_frame use a separate building demolition timer. The
vanilla clock path reads an attached bomb. No invented universal C4 countdown
is advertised; a requested optional mobile C4 indicator is separate future UX.
Tests cover configurable fuse/flicker, invalid inputs and wrench frame cycling.
Wrench timing uses the normal14-frame normalization; all speed-option-specific
normalization and GPU positioning still need original/phone comparison.

## R-059 follow-up — Boomer vertical missile launch (confirmed missing arm)

VERA [rocket_movement.rs](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/movement/rocket_movement.rs)
separates CMisl raise6624C8 from V3/DMisl tilt and powered climb. During its
launch interval CMisl rises RaiseRate leptons per frame. Existing fork
RocketBlock parsed RaiseRate but never used it. engine/rocket.cpp::rocket_step
now accepts the kind-specific raise policy; world_an.cpp passes it for CMisl.
Other rockets retain their original tilt path. pres_effects.inc adds the
previously absent CMisl pre-climb V3TAKOFF puffs at24-frame intervals; existing
powered launch/trail paths remain. No new gameplay fields or save layout.

Test: same launch block, CMisl rises at configured3leptons/frame while the
Dreadnought path remains at deck height, then enters climb. Native phase6 is
represented by the existing preflight phase plus saved type policy, not a
copied VERA runtime. Exact original timer epilogue, FLH/second-spawn offsets,
launcher-height fallback40 and native spawn-manager ownership remain partial.
Thus this is a verified raise correction, not complete launch fidelity.

Retail read-only HVA: DRED1frame, DREDWO1frame, BSUB1frame. These do not provide
the imagined animated opening hull. Dreadnought already had missile pitch,
launch puff, trail and no-spawn alternate model paths; the report doesn't
identify a failed missile phase. New LAUNCHER/MISSILE probes expose readiness,
target, kind, phase, altitude, pitch and owner. Dreadnought's remaining user
symptom is unverified, not claimed fixed by Boomer's correction.

## R-005/R-038 follow-up — Bunker approach and turret movement

Confirmed render discontinuity: bunker_platform_offset has one-cell support,
but the render host admitted the tank only after crossing the foundation's
half-cell edge. At that edge an already nonzero offset abruptly appeared.
building_transition.hpp::bunker_render_passage now owns the complete support.
Offset uses smooth endpoint easing while retaining the accepted0.5-cell final
placement. main.cpp classification consumes that predicate. No collision,
path speed, occupancy, attack or docking footprint changes. Tests cover eight
directions, both sides of the old boundary and smooth endpoints. Phone all-
direction entry/exit and front hull occlusion remain acceptance requirements.

PresTurrets previously displayed fixed-tick def_turret_facing directly while
mobile units used display interpolation. unit_presentation.hpp now has an
unsaved/unhashed BuildingPresentation history captured per logic tick and
sampled by pres_turret.inc. It follows the shortest wrapped turn and cuts
history on ownership/type/position/lifecycle changes. Building combat's ROT,
fire admission and damage remain unchanged. This improves visual turn pacing;
it does not certify native idle scanning, retained Gatling stage or HVA parity.
VERA's [turret-facing tests](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/combat/combat_turret_facing_tests.rs)
are simulation evidence, not a substitute for Android video acceptance.

CORRECTION TO900048 RESEARCH: retail AGGattling explicitly has `Burst = 2`
(including spaces around the key). The earlier statement that all stock stages
had Burst1 was wrong. New900048 logs also show burst2 at the muzzle. The event-
specific muzzle proxy correction therefore benefits stock weapons, not only
custom profiles. No phone INI divergence is established by this observation.
This correction preserves historical records rather than rewriting them.

## Still unresolved: Magnetron and broader traffic/AI

Magnetron primary lifting/holding/pulling/dropping and its retained magnetic
screen wave are not implemented by this batch. The fork suppresses nominal
lift damage but lacks the exclusive locomotor handoff and release lifecycle.
VERA locomotor_lift.rs/wave_magnetic_tests.rs and Gagarin ReleaseLocomotor70FEE0
remain research references. A cosmetic purple beam cannot certify the missing
mechanic. No fake vehicle lift, damaging substitute or copied GPL code ships.
Normal-AI expansion and crowd deadlocks need seeded reproductions and owner
traces; this batch adds diagnostic context instead of changing scheduler/RNG.
The snapshot self-owner is filtered before reporting a blocker. Existing
detour/yield rules and accepted Bunker/Force Fire/walls/MCV are preserved.

## Mod capability audit

Current src/asset_fs.cpp discovers expand/ecache/elocal artwork archives.
main.cpp::read_ra2md_file still reads INIs from ra2md.mix and explicit nested
fallbacks, not a general loose-mod profile. Parsing a tag is not implementing
its behavior. Windows Ares/Phobos DLLs cannot load into this native ARM64 engine.
No broad Mental Omega compatibility or arbitrary extension support is claimed.
Next safe milestone: separate read-only mod overlay, one resolver for rules/art/
sound/maps, content fingerprint in saves/network, unsupported-tag report,
reversible enable/disable, and small data-only pack acceptance first. This
batch changes no archive precedence or importer and copies no mod assets.

## Validation and delivery

Focused tests and final suite/build results are appended to the same tracker
after completion, including failures. One absent optional landmark fixture is
excluded; foreign native-oracle corpora may early-return. Android frontend
syntax checks pass with the existing waypoint_cell override warning. No real
phone/emulator, speaker listening, GPU, native-PC hook or thermal capture here.
Save schema remains59; maintain backups and matching multiplayer builds.

Omar0BSD, Bryle13 credits and dependency notices remain. All implementation is
independent C++17 from verified behavior; no GPL source/tables, retail assets,
DLLs or credentials are committed. New installable ARM64 APK must be delivered
to Google Drive only after validation, with checksum and exact source commit.

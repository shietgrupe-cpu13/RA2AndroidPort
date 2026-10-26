# Yuri effects and traffic follow-up — build 900048

Investigation date: 2026-10-26. Baseline: build900047, source
986846d87fc74f269777030c7b4a3779c82ba87d. Development branch:
`fix/yuri-effects-48`. This is an incremental correction, not a claim that all
remaining Yuri mechanics or original PC rendering have been implemented.

## Evidence and reproducibility

The instruction folder, Unified Instructions and existing living research
tracker were read before implementation. Previous tracker history is retained.
Latest report inspected: Drive file178T1Fbaj4-IrZI-MJWX1m_BBhmCiKm9c,
uploaded2026-10-26T12:45:33.678Z, ZIP282846bytes,
SHA25644e6eda3db1884400551461e39c2ecae02ab9ae9ea09af0805ac72ba3b84e064.
It identifies900047/986846d, Android16, map xbreak.map, seed0, human
YuriCountry versus Africans DEBUG-AI. Description is blank. Current engine log
has4545lines and exits normally at frame9498, code0; no current ASSERT, FATAL,
ASSET_MISS or TRAFFIC_BLOCK. All current blocked_over_3s counters are zero.

YAGGUN39 requests AGGattling/AGGattling2 loop reports with enabled mixer handles
at frames5430/5621, then stops at5797. This confirms report-event admission and
handles, not that the speaker audibly played the right sound. Stage3 is not
witnessed. DISK combat and TELE lifting are not reproduced by this report.
The older rotated engine log contains19 traffic lines, including CMIN34 blocked
8.93/18.93s by enemy DRON92 at66,64. It has no matching current session header;
do not attribute it to900047. Older process-exit traces are not a new crash.

User clarified that lag means units stop/get stuck behind others. Existing
world_loco.cpp already has parked-friendly scatter, moving-block delay,
stationary-obstacle detours, bridge-plane reservations and stale-claim repair.
No latest-log reproduction proves which of these fails in the reported scene.
No new Bunker MP4 was available in this session; existing accepted behavior and
previous recordings remain the visual references.

Source pins inspected:

* VERA20K086b5f58632bbf58a6c784ccd1b7559ccaf14db4.
* Gagarin7a9e8ee4031e29e1c2e54a3730f0ac1c96b98957.
* Omar upstreama8cfdccfef8592c13a6517c3fb76d834b1534027.

## Source-level findings and implemented changes

### P0 — Disc retained charge/discharge (confirmed missing owner)

[VERA disk controller](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/world/disk_laser.rs)
and its [native component oracle](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/tools/spatial_oracle/disk_laser.md)
identify ctor4A7A30, Fire4A71A0, AI4A7340 and Detach4A7900. Their behavioral
contract retains the original target/weapon, emits eight charging phases,
waits between emissions, then discharges once. Birth during object AI does not
advance the retained controller in the same pass. Current source/target
coordinates are queried while the birth heading remains retained. Range,
source/target expiry and crashing cancel the pending shot. Retail DiskLaser
Report is at discharge; AudioVisual DiskLaserChargeUp starts charging.

The fork previously damaged/reported immediately and drew a static beam.
Independent C++ implementation: `World::queue_disc_laser/step_disc_lasers` in
engine/world_disk.cpp, declarations in world.hpp, intercepts in combat.cpp,
fixed-tick owner in world.cpp, presentation in pres_game.inc and main.cpp wiring.
The first phase is next tick; the final event is the17th controller visit.
Damage uses the captured combat profile exactly once. Source orders changing
target do not retarget an already fired charge. Position/elevation and final
aim update as actors move; source death/crash, target death and range cancel.
Existing Disc building-drain behavior remains in world_drain.cpp.

Save59 retains the shot profile, heading, phase and wait. It reads55–58 through
the existing extension migrations; absent older tails clear pending shots while
retaining newly configured type policy. Disk events are saved but excluded from
the gameplay hash. Old APKs cannot read59 saves. CMake source lists include the
new owner for native tests and Android.

Presentation emits opposed charge segments and a final beam, using the current
FLH and target plus the retained weapon for impact. Sound requests attach to
the current ring endpoint. The ring is an independently calculated regular
circle: the irregular native authored16-point table was NOT copied. Exact
packed pixel positions, promotion mid-charge and every native range boundary
remain unverified. Existing weapon_in_range/foundation allowances are reused;
this is not a claim of exhaustive native-oracle parity. Gagarin's
[TU066](https://github.com/r-melvin/gagarin/blob/7a9e8ee4031e29e1c2e54a3730f0ac1c96b98957/match/combat/tu066_disk_laser_class.cpp)
is still structural/partial, not a complete independent combat confirmation.

Regression: phase count/timing, one final hit, moving original target after
retargeting, ground force fire, aircraft, buildings, death/crash/range cancel,
captured damage, save continuation, invalid refs, old-tail migration and normal
weapon bypass. No phone audio/visual acceptance is claimed.

### P1 — Gatling Cannon versus Tank effects (confirmed omitted impact)

[VERA Gatling owner](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/combat/gattling.rs)
separates retained stage/value and report latches (increase70DE70/decay70E000).
Retail YTNK/YAGGUN ground stages share AGGattling/2/3. Stage muzzle animations
are directional MGUN, GUNFIRE, VTMUZZLE; GattWH uses PIFFPIFF. A read-only retail
asset decode found all8MGUN files6frames, GUNFIRE4, VTMUZZLE11, PIFFPIFF12;
YAGGUN HVA has2frames and YTNK1. No invented rotating-barrel asset is added.

Confirmed fork defect: unit fire_fx invokes instant-shot warhead impact;
defense_fire_tick did not. pres_game.inc now invokes shared impact for staged
instant building weapons only, including unit/building/cell aims. Flying
projectiles retain their own eventual impact, avoiding duplicate explosions.
engine/defense_muzzle.hpp resolves the event's staged/AA/elite profile and
passes actual Burst to the existing FLH projection. Stock Burst is1 for all
these ground stages: the burst correction helps custom profiles but is NOT the
cause of the stock missing effect. Existing loop audio is retained.
Bounded MUZZLE diagnostics report the first event per building/weapon, capped128
keys per scene, including resolved anim, spawn result and projected offset.
No MIX-per-lookup tracing or per-frame stderr flushing is introduced.

Regression: staged profile/burst selection and flying-projectile impact gate.
Remaining: original retained stage/latch parity, stage3 witness and actual
speaker/phone comparison of muzzle and impact against YTNK. Do not claim the
log proves complete turret animation/audio fidelity.

### P1 — Bunker render-pose continuity (confirmed classification mismatch)

Existing simulation docking/alignment/hull and physical exit remain intact.
main.cpp previously classified bunker containment/front lip using fixed Unit
fx/fy while draw_one_unit used interpolated UnitPresentation/BunkerMotion.
Classification now samples the same render pose. One collection owns physical
passage and legacy visual exit so a tank matching both is not emitted twice.
Rear/tank/front ordering remains explicit; no path teleport or timer rewrite.
tests/test_yuri_core.cpp samples interpolation fractions/classification and
checks save bytes unchanged. Expected improvement is fewer threshold pops or
double draws. GPU/video acceptance of every direction remains outstanding.
VERA's [interpolation prototype](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/docs/render-interpolation-prototype.md)
is a comparison reference, not proof its full renderer matches PC.

### P1 — Traffic and normal AI (diagnostics, not a claimed gameplay fix)

[VERA scatter](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/movement/scatter.rs)
and [candidate selection](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/movement/scatter_cell.rs)
distinguish force/no-kidding, mission/locomotor admission, candidate occupancy,
surface and elevation. Native class offsets differ between inspected original
binaries; equal-looking names are not proof of identical class state.
Fork world_loco.cpp/on_blocked, world_w7.cpp/incoming_scatters and
unit_orders.cpp/order_scatter already implement a subset of this policy.
Do not force an attacking enemy, frozen/deployed actor or inaccessible terrain
out of the way as a generic deadlock cure.

report_probe.hpp now samples waits >=1s (still capped24 unit samples) and records
blocker identity, allegiance, movement/target/frozen/deployed state and the
mover's delay/blockage/tries. Snapshot occupancy is explicitly on the CURRENT
plane, not asserted to be the landing plane at a bridge entrance.
blocked_over_3s keeps its original meaning. Probe tests verify blocker context
and byte-identical saved state. No gameplay RNG or movement order is altered.

[VERA AI chooser](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/ai_base_building.rs)
has a retained original chooser/owner cadence; the fork ai_base.cpp static plan
and recovery fallback are not equivalent merely because both build refineries.
Bounded AI_BASE reports mode, credits, plan/pending/recovery and next-pass time.
The current report's DEBUG-AI is intentionally passive and cannot demonstrate
normal difficulty/expansion defects. No scheduler, difficulty or RNG rewrite
is shipped without per-faction seeded traces.

### P0 unresolved — Magnetron lifting and effects

[VERA lift](https://github.com/YuriPlanet/vera20k/blob/086b5f58632bbf58a6c784ccd1b7559ccaf14db4/src/sim/locomotor_lift.rs)
and Gagarin's [ReleaseLocomotor](https://github.com/r-melvin/gagarin/blob/7a9e8ee4031e29e1c2e54a3730f0ac1c96b98957/match/movement/techno_class_release_locomotor_methods.h)
(70FEE0, plus Imbue710000) require exclusive source/victim links, locomotor
handoff, command refusal, climb/hold/pull/drop, cancellation callbacks,
landing/water/bridge/collision and ownership/kill credit. The fork's magnetic
guard suppressing nominal5000HP damage is not a completed lift. This remains
unimplemented here. A purple beam or linear teleport would conceal missing
simulation and risk accepted movement/saves; it is not presented as a fix.

## Performance evidence and validation limits

Current report32 CPU windows: scene-render median61.14Hz, median window average
1.886ms, maximum108.385ms. World-step median60Hz, median average0.4545ms,
maximum2.87ms. A cold-cache/loading explanation is a hypothesis, not a confirmed
root cause. No Perfetto/GPU/thermal capture was supplied or recorded. No FPS,
memory or battery improvement is claimed. New shot state uses a copied combat
profile; large-battle allocation cost still needs measurement.

Focused gameplay/presentation suite:11cases/163assertions passed. Final full
native suite:1315cases/278171assertions passed after the read-only probe change.
Android build/signature and delivery are recorded in the
living tracker after verification. One unavailable landmark is excluded;
optional foreign-oracle corpora may early-return and are not claimed executed.
Android main.cpp syntax check passed before the probe refinement; full ARM64
compile remains required. No emulator/physical phone/GPU/audio listening test.

Exploratory failures are retained locally: initial native run ended
-1073741819 without output during dependency/layout refinement; a rebuilt
target passed after a test fixture's unrelated actor was moved away from the
legitimate area-hit location. Do not assert a proven crash root cause or a
gameplay damage change. Malformed saved-ref rejection is an intentional test.

## Compatibility, licensing and next acceptance

Normal vehicle projectile fire_fx remains unchanged. Retest shells, force
fire, Disc drains, Chaos, Bunkers, walls, MCV, importer and touch/pinch after
install. DEBUG-AI remains available. All multiplayer participants need the same
build; keep older saves backed up before writing59. No mod compatibility is
newly promised; DiskLaser support is currently configured from primary weapons.

All implementation is independent C++17. No GPL source, translated algorithms,
authored VERA lookup table, proprietary Chrono code, retail assets or DLLs are
added. Omar0BSD, dependency notices and Bryle13 credits remain retained.
Unofficial/EA non-affiliation and ownership confirmation remain in the launcher.
Source delivery is a separate development branch; APK is Google Drive only.

Next: capture the reported stopped units with a precise map/scene and new
TRAFFIC_SNAPSHOT; accept Disc phase/audio/force fire/drain interruption on the
phone; compare Cannon stage1–3 with Tank effects; record all Bunker directions;
implement Magnetron only with saved lifecycle/landing regressions; gather
normal Allied/Soviet/Yuri expansion traces before replacing the chooser.

# Latest Yuri gameplay investigation — 26 October 2026

Baseline: `454bc2791d277d667aee66c530a2fa06ba8fe9f7` (APK 900044).
Isolated development branch: `fix/yuri-gameplay-45`. This record supplements
THREE-ENGINE-INVESTIGATION.md and the existing Drive tracker; it does not
replace earlier observations or turn reconstructed functions into whole-game
verification. No GPL source, fixture tables or translated implementations were
imported. Original engine and dependency notices remain intact.

## New diagnostic evidence

Drive archive `1Krx77DwIOdYKg21YWbjUejN_xVmCQEcC`, 315,003 bytes, uploaded
2026-10-26 02:30:34 UTC. Archive SHA-256:
`b9e3e38b07abc6af7f9a76c3897375f6054b74ed4c1c3537c3211884b87c363c`.
Report creation 02:30:29 UTC, version code900044, version ending454bc27.
Device Xiaomi2412DPC0AG, Android16/API36, ARM64. Description is explicitly
absent. Map bldfeud.map/mapsmd03, seed0, human YuriCountry vs easy Africans.

The two engine files are **rotation halves of the same version/session**:
previous starts at frame0 and ends11400; current starts11400 and ends18479.
Do not call the previous file a different APK. The current final line is
`[290282083ms frame=18479] ENGINE NORMAL EXIT code=0`.

Current: 3,100 lines, 10 TRAFFIC_BLOCK events, no assertion/fatal diagnostic.
Previous: 8,561 lines, 41 TRAFFIC_BLOCK events. No evidence here establishes
new pathfinding regression from the raster-only900044 change.
Current active performance samples: 25, fps59.4–61.5, logic28.8–60.2/s,
maximum recorded render2.76ms and worst17.1ms. Earlier half includes startup
and resumption (logic rates are not all steady-state), render maximum3.61ms,
worst43.5ms. These are CPU/logged call statistics, not GPU captures, battery
temperature or controlled A/B performance results.

Stationary Gatling building20/YAGGUN emitted all three loop reports with
nonzero handles: frame5155 AGGattling/Loop1/2743; frame5346
AGGattling2/Loop2/2744; frame5551 AGGattling3/Loop3/2789. There are subsequent
start/stop witnesses. This establishes the event-to-mixer-handle path in this
session, **not** that sound was audible on the phone.

Optional expand01.mix and ra2.csf are absent; GATOW has an unresolved main
image. Map dressing reports missing-art=0. Do not infer missing Disc/Chaos
mechanics from these unrelated optional asset messages.

Special units occur in the new logs, including DISK55, TELE45 and Brutes27–29.
At frame11400 the Brutes have no targets, but bounded snapshots omit nearby
eligible enemies, mission/scan state and fog/weapon filters. This cannot prove
that they refused a legal target. TELE has both cell force-fire orders
(frame12481) and object attack orders in the earlier half. An attack command
does not prove the absent magnetic locomotor or wave was executed.

The latest process-exit reason2/status9 is not itself a native-crash trace.
RA2Activity.onDestroy intentionally killProcess()s the native process after
SDL teardown on normal finish. The exported .trace protobufs are dated
October22 and23, before900044; do not attribute those SIGSEGVs to this session.

## Media review and original-binary boundary

Read both newest folder MP4s and both screenshots, using decoded contact
sheets and original files locally. VID_20261026_001732.mp4 is5.75s and shows
a Disc near an Allied base; VID_20261026_001219.mp4 is12.48s and shows Disc
lasers and ring/body effects. These clips were uploaded October25 16:12–16:17
UTC, before900044 was built. They are reference footage, **not a new900044
visual acceptance run**, and their provenance does not authenticate executable
v1.001. The screenshots are likewise older and show the previously reported
Yuri wall/sidebar situation. No new build900044 MP4 was present at discovery.

Local gamemd.exe SHA-256 is
`3e81a61775d2745d1dabe397325ef663cd994ffc194da4e998e3bf5d2d308600`.
Gagarin reccmp-project.yml pins original v1.001 to
`1cdd1180e49024fbda8ad568caac2e86e856063ff67ab38f62b7d2c7bb84298c`.
Therefore **no direct matching-original execution claim** is made here. Local
gamemd-spawn.exe also differs. Retail files are read-only and never bundled.

## A — Floating Disc: confirmed drain dispatch gap, staged correction

Original owned rules define DiskDrain DrainWeapon=yes, InvisibleVertical,
CellRangefinding, FireOnce and FireWhileMoving=no; amount30/delay30 frames,
DrainAnimationType=DISKRAY. Drainable/ResourceDestination/Power are distinct
building predicates. Power drains affect the owner's grid; refinery drains
transfer funds; other drainable defenses are independently disabled. A normal
target still uses DiskLaser. An attack is not a1HP drain bullet.

Evidence crosswalk:

* [Gagarin BeginDraining](https://github.com/r-melvin/gagarin/blob/945a29e262dbb364bbe41605c42fc5acde5a2918/match/movement/locomotion_begin_drain_methods.cpp):
  0x70FD70, manifest matched100%; occupying-cell validation, source/target
  links, animation attachment. IsDraining0x70FEB0 and IsBeingDrained0x70FEC0
  are matched predicates. This does not establish every periodic/cleanup path.
* [DrainWeapon observed behavior](https://modenc.renegadeprojects.com/DrainWeapon)
  and [Drainable](https://modenc.renegadeprojects.com/Drainable) corroborate
  owned rules and the source/target relationship. Exact first-transfer phase
  remains a native-comparison acceptance item.
* VERA20K8be6015 disk_laser oracle0x4A6CF0/0x4A7A30/0x4A71A0/0x4A7340
  describes the **separate** retained ring/laser lifecycle, not drain economy.
* RA2R7c9d47c and ReSourceb01d3cf did not establish a verified complete drain
  implementation in the inspected source. ReSource fields alone are not a
  working controller. The detailed evidence grades remain in investigation44.

Fork root: weapon_unit.cpp's generic armor-based primary selection, followed
by combat.cpp normal damage, has no DrainWeapon controller or saved links.
This is confirmed source behavior; lack of a visible beam is not the root.

Independent correction: world_drain.cpp configures rules-driven source/target
policies, routes explicit enemy drain orders through the existing jumpjet
integrator to foundation overlap, and owns saved links and per-source transfer
timers. Ordinary laser targets and ground Force Fire keep their existing
damage/projectile pipelines. The controller does not modify normal shell art.

Queries immediately revalidate live sources, ownership and targets. Multiple
Discs do not overwrite manual power or spy timers. Capture/sale ends former
drain orders without turning them into accidental friendly force-fire shots.
Wallet updates use the shared human/AI/campaign adapter and production pool.
Transfers are capped by available funds. Drained defenses are gated directly;
grid shutdown feeds the existing blackout query and production/power paths.
Presentation attaches the imported DISKRAY animation and its sound report to
the active source, with loop cleanup. Pixel-accurate anchoring, first-transfer
phase and audible timing still require matching-PC and phone acceptance.

Save format56 appends this state after the unchanged55 base World sequence.
The reader accepts55 containers and base worlds; new56 worlds are not promised
readable in old APKs. Links are validated on load; rules, links and timers are
included in deterministic hashes. No save bytes are rewritten in place.

Status: **partial** for the full request. Drain gameplay controller implemented
for verification; full DiskLaser ring charging/final-hit timing remains
unresolved. Do not advertise complete Floating Disc fidelity.

## B — Chaos Drone: receiver and exposure semantics remain unresolved

Owned rules: ChaosAttack Damage600, ROF30, Range3, AreaFire=yes,
PsychGasCreate CellSpread3/Psychedelic=yes/AnimListCDGAS. CAOS disables passive
acquisition and retaliation. CDGAS art is Flat=true, Rate450, with no Damage
or particle-exposure settings. A persistent independently damaging gas volume
must not be invented solely from its lingering appearance.

[VERA receiver](https://github.com/YuriPlanet/vera20k/blob/8be601598438b05dd62874cf837bb29a7685a8dc/src/sim/combat/world_receiver.rs)
records0x701D71..0x701DB4: transition out of team, clear target, queue Hunt;
receiver owns the timer and immunities. That is a bounded original observation,
not an end-to-end Chaos release test. RA2R/ReSource have not supplied a verified
replacement at the inspected pins. Gagarin's drain helpers do not implement it.

Fork: no Psychedelic receiver owner, duration/status, ownership-preserving
berserk targeting or complete general AreaFire dispatch exists. Generic
fire_fx places instant impacts at the victim and combat applies ordinary
damage. These are confirmed source gaps; exact gas emission alignment in a
new phone video is not measured here.

Next smallest complete correction: specify self-centered fire behavior against
the original; saved berserk timer/transition and original ownership; immunity,
allied exposure, re-exposure/expiry and team/mission rules; affected tint; then
correct CDGAS origin/layer. Own-source exclusion vs allied exposure must be
resolved from original observations, not contradictory community prose.
[Psychedelic](https://modenc.renegadeprojects.com/Psychedelic) and
[BerserkFriendly](https://modenc.renegadeprojects.com/BerserkFriendly) are
secondary cross-checks only. BerserkFriendly describes victim targeting
exclusion, not permission to damage or convert the Drone's ownership.

Required acceptance: one/multiple releases, entry/exit relative to actual
native exposure rules, immune units, same-owner/allies/enemies, source death,
save/load at expiry, original mission and player command precedence. No
speculative600HP damage change or approximate permanent Hunt was shipped.

## C — Magnetron: preserve control-value suppression

Fork confirmed paths: weapon_resolve.cpp reads primary IsLocomotor into
UnitTypeW7::magnetic; weapon_unit.cpp forces the valid secondary against
buildings; combat.cpp suppresses primary control Damage5000 against units.
No magnetic-lift locomotor or authentic IsMagBeam wave owner exists. The
secondary building damage alone is not feature completion.

VERA spatial oracle original hit slice0x46963A..0x469AA4, Imbue0x710000 and
Foot/Jumpjet transitions establish separate lift responsibility; wave ctor
0x75E950, update0x762AF0, draw0x75F9F0 describe depth-sensitive distortion.
Gagarin ReleaseLocomotor0x70FEE0 is a relevant helper, not the whole controller.
RA2R/ReSource simplified effects are not evidence for a purple-laser substitute.
See investigation44 pinned links and reconstruction status for these slices.

Status **unresolved**. Need saved lift/hold/pull/release state with exclusive
target control, bridge/ground/water/landing and collision eligibility; source
death/capture and invalid-target release; then separately implement wave depth
sampling. Keep5000 out of ordinary damage. Ground force-fire legality is an
original-PC repro gate, not an assumption. Tests must cover each locomotor
handoff and repeat/save determinism before enabling lift in gameplay.

## D — Brute: reproduced default-guard anchor defect

Owned BRUTE: Punch1.4, Smash1.1, GuardRange2, DefaultToGuardArea=yes.
Sight8 is not automatically the threat radius. Existing recovered scan logic
uses guard range/Area Guard and respects fog, legal targets and missions.

Root confirmed: world_abilities.cpp::w7_step_deploy_timers sets area_guard
before world_w7.cpp::enter_idle_mode, but omitted tg.home_x/home_y. The latter
then returns because area_guard is already set. Existing combat.cpp pursuit
leash and return checks consequently skip the missing anchor.

Correction initializes the anchor on this transition and repairs only unset
anchors from older saves when actually idle. It does not reset anchors during
pursuit or globally increase aggression. Source-grounded behavior references:
original EnterIdle0x51CB2A/0x737BB6 and MissionAreaGuard0x4D41A0, already
documented in the fork. No independently rerun original hook test here.

Before fix: focused anchor test failed5 assertions; basic visible-target melee
test already passed. After fix: anchor, save/load continuation, visible/hidden
acquisition, Move precedence and CAOS/TELE passive-disabled tests pass in the
pre-Disc suite. This does **not** establish that all user-observed Brute idle
cases are resolved. Diagnostics now add bounded ACQUIRE/ACQUIRE_CANDIDATE
records for mission/home/scan/passive flags, visibility/disguise/legal filters
and selected slot, without mutating saved state or consuming RNG.

## Verification and outstanding acceptance

Pre-Disc guard/diagnostics suite:1280 cases/277646 assertions passed; one
unavailable ecache retail-landmark fixture excluded as in prior builds.
First unrestricted run also exposed that known absent fixture and a test's
incorrect assumption that a returning Area Guard stayed at its manually moved
coordinate; fixed the test to use its actual idle position. No engine failure
was hidden as a pass.

Disc-focused suite:5 cases/113 assertions passed, including full jumpjet
approach, mid-transfer save continuation and bad-link refusal. Combined source
suite:1285 cases/277763 assertions passed, one unavailable retail-landmark
fixture excluded. ARM64 engine library compiled and linked. Subsequent
presentation cleanup/packaging results are recorded in the living tracker after
completion; no phone or pixel-perfect drain animation claim follows from it.

No direct PC oracle execution, new original disassembly, phone installation,
GPU profiling or thermal test was performed here. Existing normal projectiles,
Tank Bunker, Force Fire, walls and touch code are preserved; retain their
regression tests. Do not claim new visuals/device behavior from compilation.

# Wave 7 / 10: Yuri-faction units, mind control and psychic effects — gap analysis

**Tree:** master `b3a509f`, using `build/ra2engine.exe` as built (no rebuild).

**Scope:**
- Infantry: INIT, VIRUS, BRUTE, YURI, YURIPR, SLAV, YENGINEER.
- Vehicles: LTNK, YTNK, TELE, MIND, CAOS, SMIN, PCV, YHVR.
- Air and naval: DISK, and BSUB (which `06-naval.md` left out of scope).
- Effects: the Psychic Tower and Gattling Cannon, mind control in general, the Genetic Mutator and the Psychic Dominator.

**Method:**
- Data: `rulesmd.ini` and `artmd.ini` (YR), and RA2's `rules.ini` (`ra2.mix/local.mix`) for comparison. I also scanned all 25 campaign maps in `cmaps` (the 24 missions, with SOV09 in both its t and u versions) and all 161 `multimd.mix` maps listed in `missionsmd.pkt`.
- Code: `campaign_runtime.inc` (poll_mind_control), `combat.cpp`, `world_abilities.cpp`, `weapon_resolve.cpp`, `realplay_*.inc`, `gf_start.inc`, `mp/mp_model.cpp`, `ai_trigger.cpp`.
- Ghidra: retail YR gamemd.exe. The project copy has been deleted. Raw dumps are in `D:\ra2-re\w7-yuri-out`; use `keymap2.txt` for INI offsets, because `keymap.txt` is off by one.
- Play: 6 `--play` runs, with scripts in `10-evidence/`.

**Time base:** logic frames, unless a value is labelled as a driver frame.

**Data-set caveat (U-1 / R-193).** The 24 missions were authored for RA2's `game.exe` and `rules.ini`. The clone runs them on YR `rulesmd.ini` with the map overlaid. Where RA2 and YR semantics differ, the YR gamemd behaviour is used as "original" and the RA2 behaviour is noted.

## 1. Summary

**Reachability.** Only the **Yuri Clone (YURI)** is reachable in shipped content. Every other Yuri-faction unit and structure is unreachable:
- Skirmish forbids the Yuri country.
- The YR co-op maps (C1A01MD..C4W01MD, `GameMode=cooperative`) are filtered out of the map list.
- No Allied/Soviet mission places or creates any other Yuri unit.

| Content | Where it appears | Reachable? | Loads / spawns / draws in the clone |
|---|---|---|---|
| **YURI** (shown as "Psi-Corps Trooper" via the map UIName) | **SOV03**: player gets 4 and 6 by paradrop (TeamTypes `4/6 Psi-Corp Team`). **SOV09**: the player-controlled `Africans` get 1-3 by paradrop. **SOV11**: enemy BadGuy2 has 4 placed, and BG2/BG3 AITriggers build teams (up to "Yuri Clump" = 10). **SOV10**: an unused team (no trigger). **SOV12**: buildable in RA2 (see W7-10-08) [CONFIRMED-DATA, map scan] | **YES (major)** | Yes: drawn, paradropped, captures [CONFIRMED-PLAY s3/s9/s11] |
| Mind control in general (also PTROOP's MindControl weapon) | as YURI. PTROOP needs `RequiresStolenThirdTech` (a Yuri lab), so it is unreachable in skirmish. In SOV10 a sleeping Civie1 PTROOP is placed [CONFIRMED-DATA] | YES (through YURI) | campaign runtime only (`campaign_runtime.inc:1606-1690`) |
| NAPSIS Psychic Sensor | SOV11 (BadGuy1 and BadGuy3 own it, capturable); skirmish XYuriPlot (Neutral) | yes (as a tech building) | yes; PsychicDetectionRadius and SensorArray are read (`abil_w5_rules.cpp:74-75`) |
| NAPSYB / NAPSYA Psychic Beacon / Amplifier | ALL03, ALL04, ALL08, SOV08; skirmish XEB1 (Neutral) | yes, but the conversions are trigger-scripted (no mind-control weapon) | yes (wave-2/4 scripted) |
| INIT, VIRUS, BRUTE, YURIPR, SLAV, YENGINEER, LTNK, YTNK, TELE, MIND, CAOS, SMIN, PCV, YHVR, DISK, BSUB | only in co-op maps C1A0x/C2S0x/C3Y0x/C4W01MD, which are filtered out. XMP29U2/XMP29MW contain a leftover TaskForce "2 Yuri" that no TeamType uses. XMP17DU has a false hit inside a base64 blob [CONFIRMED-DATA] | **NO** | Infantry spawn and draw. **Vehicles spawn invisible** (W7-10-10). No crash with all types alive for 1000+ frames [CONFIRMED-PLAY y1, s9b] |
| YAPSYT Psychic Tower, YAGGUN Gattling Cannon, YAGNTC Genetic Mutator, YAPPET Psychic Dominator | Prerequisite YACNST. They appear only in co-op maps and in RushHr's `[YuriCountry]` AI base-node list (used only by a Yuri house) | NO | not modelled (SW enum entries only) |
| Yuri side in the skirmish and MP lobby | `gf_lobby_countries()` omits YuriCountry (`gf_start.inc:382`). Out-of-range or stale indices become Random (`skirmish_lobby.cpp:200`, `mp_model.cpp:292`). The 45 aimd AITriggers that use Yuri TaskForces are all Side=3 and are filtered (`ai_trigger.cpp:147`) | n/a | OK (W7-10-12 covers network interop) |

**Totals:** 13 gaps.
- 2 MAJOR, both reachable: W7-10-01 (SOV11) and W7-10-02 (SOV09, needs a decision).
- 6 MINOR, reachable: 03, 04, 05, 06, 08, 09.
- 1 MINOR, partial: 11.
- 4 MINOR, not reachable: 07, 10, 12, 13.

**Top items:**
1. **W7-10-01.** Units that enemy (AI) Yuris capture just stand and guard. The original runs `DecideUnitFate` (TeamType MindControlDecision, otherwise a random pick among Add To Team / Grinder / Bio Reactor / Hunt, falling back to **Hunt**). In SOV11 the player's mind-controlled tanks should turn and hunt the player [CONFIRMED-RE 0x4722E0, CONFIRMED-PLAY s11].
2. **W7-10-02.** Capacity is hard-coded to 1. The original's capacity is the primary weapon's **Damage**, and `InfiniteMindControl` lifts the cap. SOV09 overrides `[MindControl] Damage=10`, so under YR semantics each SOV09 Yuri can hold 10 victims; RA2's game.exe had no capacity at all. This needs an owner decision [CONFIRMED-RE 0x4716F0 / 0x471BB0].
3. **W7-10-05.** Mind control has no presentation: no MINDANIM ring on the victim, no purple link line, no YuriMindControlSound, no MindClearedSound [CONFIRMED-PLAY, code].
4. **W7-10-06.** The Yuri deploy is modelled as a GI gun post. The original fires PsiWave at its **own cell** at once (AreaFire) and stands up again after UndeployDelay=150 [CONFIRMED-RE 0x51F3F0].
5. **W7-10-03 / 04.** The capture rules are looser than the original's:
   - A Yuri can steal a victim that another controller already holds, and the release bookkeeping then goes wrong.
   - The capture happens on range, with no shot and no ROF.

## 2. Per-unit table

| Unit | Original special mechanic (rules / RE) | Clone | Verdict |
|---|---|---|---|
| **YURI** | MindControl (Damage 1 = capacity 1; SOV09 override 10; elite MindControlE Range 14). Capture at bullet detonation. Victim → controller house, Guard; human-owned victim leaves its team; AI victim → DecideUnitFate. Ring anim, link line, sounds. Deploy → PsiWave at own cell, undeploy after 150. ImmuneToPsionics, TypeImmune, DetectDisguise, Sight 12 | Capture, release on controller death, ImmuneToPsionics refusal and swap-on-order all work [CONFIRMED-PLAY s9/s11]. Gaps 01-06, 08, 09 | PARTIAL |
| YURIPR | SuperMindControl: ControllerBuilding warhead (100% vs buildings, so it captures buildings unless they are ImmuneToPsionics, which defaults to yes for BuildingTypes); SuperPsiWave deploy | units only, capacity 1 | NOT REACHABLE (11) |
| INIT Initiate | PsychicJab; Occupier with UCPsychicJab | generic weapon and occupy | NOT REACHABLE, OK? |
| VIRUS | Virusgun, Virus warhead (InfDeath=8, poison-cloud death) | generic damage; no poison cloud | NOT REACHABLE (11) |
| BRUTE | Punch/Smash melee (Smashing: Rocker, Wall). Unnatural, ImmuneToPsionics, DefaultToGuardArea | generic short-range weapon | NOT REACHABLE (11) |
| SLAV / SMIN | SlaveManager: 5 slaves, regen 500, reload 25; slaves go to the killer's house when the miner dies (0x6B0170); SMIN deploys into YAREFN | no SlaveManager (0 refs to `Enslaves`) | NOT REACHABLE (11) |
| YENGINEER | Engineer, DefuseKit | generic engineer | NOT REACHABLE |
| LTNK | ATGUN plain tank | generic; **invisible** (10) | NOT REACHABLE |
| YTNK / YAGGUN | IsGattling: stage at +0x140, value at +0x144; RateUp 0x70D2A0 / RateDown 0x70D430; weapon = stage*2 + AA | slot 0/1 only (0 refs to `IsGattling`) | NOT REACHABLE (11) |
| TELE | MagneticBeam IsMagBeam → warhead IsLocomotor attaches a Jumpjet loco (lift) (0x4690A0); MagneShake vs buildings | generic damage 5000 (would one-shot vehicles) | NOT REACHABLE (11) |
| MIND | MultipleMindControlTank Damage 3, InfiniteMindControl; overload damage per OverloadCount/Damage/Frames via C4Warhead (0x471970) | capacity 1, no overload | NOT REACHABLE (11) |
| CAOS | ChaosAttack PsychGasCreate, Psychedelic → berserk for warhead damage frames (0x70116C) | generic damage | NOT REACHABLE (11) |
| PCV | DeploysInto YACNST | spawns and draws; deploy not tested | NOT REACHABLE |
| YHVR | hover transport, Passengers 12 | invisible (10) | NOT REACHABLE |
| DISK | DiskLaser; DiskDrain DrainWeapon (power, or money every 30 frames, 30 credits; 0x70F170 / 0x6F9697) | no drain (0 refs to `DrainWeapon`) | NOT REACHABLE (11) |
| BSUB | Underwater; BoomerTorpedo; CruiseLauncher Spawns CMISL x2 | generic spawner path (not exercised) | NOT REACHABLE |
| YAPSYT | MultipleMindControlTower Damage 3 (capacity 3) | not modelled | NOT REACHABLE |
| YAGNTC / YAPPET | GeneticConverter (MutateWarhead full damage to infantry → InfantryMutate anim → Brute, 0x6CBA10 / 0x517DB5); Dominator (DamageArea, then permanent capture within DominatorCaptureRange, 0x53ADB0) | Both powers are registered; Dominator captures psionic-eligible enemies in its area, and Genetic Converter replaces enemy infantry with Brutes. Exact damage, capture range, and presentation remain incomplete. | PARTIAL (11) |

## 3. Gaps

### W7-10-01: Victims of AI controllers stand still; the original sends them to Hunt (or into a team)
- **Severity / effort:** MAJOR (SOV11) / S
- **Evidence:**
  - [CONFIRMED-PLAY s11] Enemy BadGuy2 Yuris captured a player HTNK and an E2 at campaign frame ~1 (`[camp] mind control unit=87 HTNK house 0 -> 2`). After 600 frames both were still at their capture cells (80,77) and (77,75) with full HP, next to the player's units.
  - [CONFIRMED-code] `campaign_runtime.inc:1677-1690`: after `set_unit_owner` (Stop + Guard), only a *human* new owner removes the victim from its team. An AI owner gets nothing more.
- **Original** [CONFIRMED-RE Capture 0x471C80 → DecideUnitFate 0x4722E0]:
  - The victim always leaves its team (0x6E9DD0).
  - For a computer owner, the fate comes from the capturer's TeamType `MindControlDecision`. If that is unset, it is a random roll weighted by money, power and health over: 1 Add To Team, 2 Grinder, 3 Bio Reactor, 4 Hunt, 5 Do Nothing.
  - When an option fails, the fallback is mission 0xF **Hunt** ("AICapture: Naw, let's put him in hunt").
  - Without a Grinder or Bio Reactor, a captured unit therefore joins the capturer's team or hunts the player.
- **Fix:**
  - In `poll_mind_control`, always erase the victim from its old team.
  - For a computer new owner, read the controller team's `MindControlDecision`. If it is 1, add the victim to the controller's team. If it is unset or 4, set Hunt.
  - Grinder and Bio Reactor are Yuri-only, so they fall through to Hunt.

### W7-10-02: Capacity is fixed at 1; the original uses the weapon's Damage (SOV09 sets 10)
- **Severity / effort:** MAJOR for SOV09 balance / S (needs a decision first)
- **Evidence:**
  - [CONFIRMED-RE 0x6F34A0 → ctor 0x4716F0] A CaptureManager is created only when the *primary* weapon's warhead has MindControl. It gets `Max = Weapon.Damage` and `Infinite = Weapon.InfiniteMindControl`.
  - [CONFIRMED-RE CanCapture 0x471BB0] `!Infinite && Count >= Max && Max != 1` refuses the capture. With Max == 1, Capture frees the old victim first.
  - [CONFIRMED-DATA] rulesmd `[MindControl] Damage=1`. The SOV09 map overrides it with `Damage=10 Range=8 Report=YuriMindControl`. RA2 rules.ini also has Damage=10, but RA2's game.exe had no capacity (one victim).
  - [CONFIRMED-code] `campaign_runtime.inc:1645-1656` allows one victim per controller.
- **Original (YR gamemd on this data):** each SOV09 Yuri may hold up to 10 victims. Once full, it cannot take an 11th; there is no swap when Max > 1.
- **Clone:** one victim, swapped on an explicit order.
- **Fix:**
  - **Owner decision (U-1 family).** YR-faithful means capacity = Damage, plus `InfiniteMindControl`, plus the Max == 1 swap rule. The alternative is to keep RA2's 1 for the RA2 missions.
  - If YR-faithful: replace `mc_victim` with a vector, and block auto-acquire when full (CannotControlAnyMore 0x4721D0 gates passive acquire 0x708650 and retaliation 0x707C40).
  - Either way, update R-036.

### W7-10-03: A victim held by one controller can be stolen by another, and the release then goes wrong
- **Severity / effort:** MINOR (SOV11 has player and enemy Yuris only if W7-10-08 is fixed) / S
- **Evidence:**
  - [CONFIRMED-RE 0x471BB0 + 0x70F980] CanCapture refuses when the target has `MindControlledBy` (+0x2C0) set or is permanently controlled (+0x2C4). A controlled unit **cannot be re-captured** until it is freed.
  - The other refusals:
    - same house (allies are *not* refused);
    - ImmuneToPsionics (BuildingType defaults this to 1, so there are no buildings);
    - a unit inside a tank bunker;
    - +0x2CC set;
    - iron-curtained;
    - the capacity rule.
  - There is no aircraft or in-air check in CanCapture. The Verses/projectile path keeps the Yuri off aircraft.
  - [CONFIRMED-code] `campaign_runtime.inc:1666-1672` checks only `is_enemy`, not aircraft, `psionic_target_ok` and range. A unit already held by controller A can be captured by B. A's `mc_victim` still points at it, so when A dies `mc_release` returns it to `mc_orig` even though B holds it.
  - Iron Curtain targets are not excluded either.
- **Fix:**
  - Add `vt.mc_controller` and refuse capture while it is >= 0.
  - Refuse iron-curtained targets.
  - Release only when `vt.mc_controller == ctl`.

### W7-10-04: The capture happens on range, without the shot or ROF
- **Severity / effort:** MINOR / S
- **Evidence:**
  - [CONFIRMED-RE 0x4690A0 @0x4692C0] The capture happens at bullet detonation, through the normal fire path, so ROF (200, i.e. 13 s), facing and the Inviso projectile apply.
  - Tag events 6 and 44 are raised first (0x6E4910). YuriMindControlSound plays only when the old or new owner is the local player.
  - GetFireError 0x6FB5D0 returns 5 (illegal) when CanCapture fails, and also against 0% Verses (Controller: wood/steel/concrete 0%).
  - [CONFIRMED-code] `poll_mind_control` captures whenever `unit_in_weapon_range` holds, before `World::step`, whatever the controller's `rearm_frames`. `try_fire` returns early for `w_mind_control` (`combat.cpp:145`).
  - The result: a Yuri with capacity 1 swaps victims instantly on an order, and the first capture of a spawned target happened before the Yuri's first shot.
- **Fix:**
  - Capture from `try_fire` (an event that the runtime consumes) instead of from the range poll.
  - Keep `w_mind_control` so the shot does no damage.

### W7-10-05: Mind control has no visible or audible feedback
- **Severity / effort:** MINOR (UX: the player cannot tell which units are controlled or by whom) / M
- **Evidence:**
  - [CONFIRMED-PLAY s9, crop `10-evidence/s9-capture-crop.png`] The captured HTNK shows only the new house colour: no ring, no link line.
  - [CONFIRMED-code] Nothing in `pres_*`, `hud_*` or `iso_renderer` refers to mind control. The `mc_*` fields live only in `campaign_runtime.inc`.
  - `try_fire` returns before any projectile or detonation, so the warhead `AnimList=YURICNTL` never plays.
- **Original** [CONFIRMED-RE 0x471C80 / 0x471F20 / 0x472090 / 0x472570]:
  - **Ring:** a `ControlledAnimationType` (MINDANIM) anim is attached over the victim, at its z plus `MindControlRingOffset`, and removed on release.
  - **Link line:** drawn from the controller's FLH to the victim in the house colour for `MindControlAttackLineFrames`=20, and again while the controller or the victim is selected.
  - **Sounds:** `YuriMindControlSound` on capture (local-player victim); the victim type's `MindClearedSound`, else the Rules one ("MindCleared"), on release.
- **Fix:**
  - Expose controller/victim pairs to the presentation layer.
  - Draw the MINDANIM loop and the timed or selected link line.
  - Play the two sounds.
  - Spawn YURICNTL at the victim on capture.

### W7-10-06: The Yuri deploy is a GI gun post; the original fires one PsiWave at its own cell
- **Severity / effort:** MINOR (SOV03/09/11) / S
- **Evidence:**
  - [CONFIRMED-PLAY s9] A SOV09 Yuri deployed with no enemy near fired nothing: `fx_anims` unchanged, no RING1, `sel_rof_cool=0`.
  - [CONFIRMED-code] `world_abilities.cpp:61-79` and `weapon_resolve.cpp:127-139`: a DeployFire infantry becomes an immobile post that fires slot 1 at acquired targets within weapon range (PsiWave Range=1).
- **Original** [CONFIRMED-RE Infantry Mission_Unload 0x51F3F0, Guard 0x521050, SelectWeapon 0x521610]:
  - On deploy, an AreaFire deploy weapon (any type except "DESO") does `SetTarget(own cell)`, so PsiWave (CellSpread 3, 250 damage, Verses 0% vs vehicles and buildings) fires at once around the Yuri. Then the mission becomes Guard with delay = UndeployDelay (150).
  - After that, Guard undeploys him.
  - SelectWeapon returns DeployFireWeapon only while deployed, and slot 0 otherwise. The Yuri never switches to PsiWave by himself.
- **Fix:** for a DeployFire unit whose deploy weapon is AreaFire and UndeployDelay >= 0:
  - fire once at its own cell;
  - stand up after UndeployDelay frames;
  - do not hold a post.

### W7-10-07: PsychicDamage and TypeImmune are not read (harmless today)
- **Severity / effort:** MINOR / S. Not reachable in effect.
- **Evidence** [CONFIRMED-RE ReceiveDamage 0x700DA0]:
  - **PsychicDamage:** a PsychicDamage warhead does 0 damage only against types with ImmuneToPsionicWeapons (+0xD36). The exe looks up the misspelled key "ImmuneToPsionicWeaponss" (0x8426DC), so the flag is effectively set only by the BuildingType ctor default, i.e. on buildings.
  - **TypeImmune:** damage is 0 when the attacker's and victim's types are the same and they have the same owner.
  - **Clone:** neither is read. PsiPulse already has 0% Verses vs wood/steel/concrete, and the clone's damage matches.
- **Fix:** none required. Note it in docs/re.

### W7-10-08: The SOV12 player cannot build the Yuri (YR prerequisites on an RA2 mission)
- **Severity / effort:** MINOR (an R-193 instance) / S
- **Evidence:**
  - [CONFIRMED-DATA] RA2 `rules.ini [YURI] Prerequisite=NAHAND,NATECH TechLevel=10 Owner=Russians,...`.
  - SOV12's player house has TechLevel=10 and no `[YURI]` override, so in RA2 the Yuri is buildable after a Battle Lab.
  - YR `rulesmd [YURI] Prerequisite=YABRCK,NAPSIS`.
  - [CONFIRMED-PLAY s12] With NAHAND, NARADR, NATECH and power placed, the cameo list has IVAN, SHK, DOG and others, but **no YURI** (`load_player_unit_roster` full-parses the YR prerequisites).
  - SOV10 bans it explicitly (`TechLevel=-1`). SOV11 lifts it to TechLevel 10, which is above the player's TL 9.
- **Fix:** under U-1, use the RA2 prerequisite for RA2-campaign rosters (for example a campaign override table for `YURI`). Otherwise record this as an accepted R-193 difference.

### W7-10-09: Releases on controller limbo and transport entry are missing
- **Severity / effort:** MINOR / S
- **Evidence:**
  - [CONFIRMED-RE] FreeAll 0x472070 is called on:
    - the controller's death (0x7015A6);
    - FootClass UnInit (0x4DE4E0);
    - TechnoClass vt+0x170 (0x70F800);
    - a Temporal hit on the controller (0x71A1C8);
    - a Disc drain of a controller building (0x70F1BD).
  - A victim entering a building or transport is freed first (0x51A0C2/0x51A220, 0x739216/0x739674) [SUSPECTED].
  - SetOwningHouse 0x700940 does **not** free: a controller whose owner changes keeps its victims. The clone matches this.
  - [CONFIRMED-code] The clone releases only when `!live(u)` (`campaign_runtime.inc:1632`). A Yuri that boards an IFV (IFVMode=8) or a transport keeps its victim, and so does a victim that boards one.
- **Fix:** release on the controller's `cargo_of >= 0` or Temporal erase, and when the victim boards.

### W7-10-10: Yuri-faction vehicles created at runtime are invisible (not reachable)
- **Severity / effort:** MINOR, not reachable / S
- **Evidence:**
  - [CONFIRMED-PLAY y1, s9b] The skirmish cheat path and the campaign `camp_spawn` path both log `[render] no SHP loaded for image=LTNK/YTNK/TELE/MIND/CAOS/SMIN/YHVR(/DISK)… skipping`.
  - The units exist (they show in `units(6,…)`) but are not drawn. Infantry and PCV draw.
- **Cause:** voxel and art loading is driven by the roster and the map's preload list, and these types are in neither.
- **Fix:** load art on demand when a new type is created (a trigger/team create of any unlisted vehicle would hit the same path).

### W7-10-11: Yuri special mechanics are only partially modelled
- **Severity / effort:** MINOR, not reachable / L in total
- **Evidence:** Several unit mechanics remain unimplemented (`IsGattling`, `Enslaves`, `IsLocomotor`, `IsMagBeam`, `Psychedelic`, `DrainWeapon`, `InfiniteMindControl`). Psychic Dominator and Genetic Converter now have basic skirmish effects, but their full rules-driven damage, range, and presentation are not yet matched.
- **Original, for whenever the scope grows** (each [CONFIRMED-RE], see §2):
  - **Gattling stages:** 0x70D2A0 / 0x70D430 / 0x6F2890.
  - **SlaveManager:** 0x6AE830; release to the killer's house at 0x6B0170.
  - **Magnetic lift:** 0x4690A0 → 0x70F400.
  - **Berserk:** 0x70116C.
  - **Disc drain:** 0x70F170 / 0x6F9697.
  - **Mastermind overload:** 0x471970.
  - **Mutator:** 0x6CBA10 / 0x517DB5.
  - **Dominator:** 0x53ADB0, permanent capture and PermaControlledAnimationType.
- **Fix:** out of scope (U-5). Keep this list in REMAINING as a non-goal.

### W7-10-12: An original-client peer that picks Yuri is silently remapped (network interop, not reachable yet)
- **Severity / effort:** MINOR (relevant once ORIGINAL-compatible networking lands) / S
- **Evidence:**
  - [CONFIRMED-code] `mp_model.cpp:292` maps a country outside `gf_lobby_countries()` (9 entries; YuriCountry would be index 9) to `rng.range(0, nc-1)`.
  - The YR client's lobby offers Yuri, so the two sides would disagree on the house.
- **Fix:** in the networking work, refuse or kick a Yuri pick in the lobby handshake rather than remapping it (cross-ref docs/gap/net).

### W7-10-13: Capturing a Psychic Sensor grants no Psychic Reveal
- **Severity / effort:** MINOR (SOV11 enemy NAPSIS; skirmish XYuriPlot Neutral NAPSIS) / S
- **Evidence:**
  - [CONFIRMED-DATA] `[NAPSIS] Capturable=yes SuperWeapon=PsychicRevealSpecial`.
  - [CONFIRMED-code] `realplay_economy.inc:76-86` grants only the classic RA2 SW types.
- **Original** [SUSPECTED]: a building's SuperWeapon= is granted to its owner, so the capturer gets Psychic Reveal.
- **Fix:** this is the same owner decision as W2-AN-19 / W6-04-19 (YR SW keys). Record it there.

## 4. Verified OK

- **Lobby and AI:**
  - The skirmish and MP lobbies cannot pick Yuri (`gf_start.inc:382`). A stale RA2MD.INI index becomes Random (`skirmish_lobby.cpp:200`).
  - All 45 aimd AITriggers whose TaskForces contain Yuri units are Side=3 and are filtered (`ai_trigger.cpp:147`) [CONFIRMED-DATA script `ait.py`].
- **Skirmish maps:**
  - No standard-mode skirmish map places Yuri units or structures.
  - The RushHr Yuri base nodes belong to its `[YuriCountry]` house only.
  - The co-op C*MD maps are excluded (`load_yr_map_catalog(…, "standard")`) [CONFIRMED-DATA].
- **No crash:** with all 16 Yuri-faction types alive in skirmish (y1, 337 frames) and in campaign (s9b, 1017 frames), fighting [CONFIRMED-PLAY].
- **Yuri arrivals:** the YURI paradrops arrive and draw in SOV03 (8 with the cheat-forced trigger) and SOV09 (2) [CONFIRMED-PLAY s3/s9].
- **Capture and release:**
  - Capture changes the owner and stops the victim (Guard), matching vt+0x3D0 = 0x70EC80 [CONFIRMED-RE].
  - A *human* new owner pulls the victim out of its team, matching 0x6E9DD0.
  - The victim returns to its original house when the controller dies: s9 shows E1 and HTNK back to house 1 after `camp_killu 6 YURI` [CONFIRMED-PLAY]. FreeUnit restores the capture-time owner [CONFIRMED-RE 0x471F20].
  - A controller that changes owner keeps its victims, as in the original (SetOwningHouse does not free) [CONFIRMED-RE].
- **Capacity-1 behaviour:** a full Yuri does not auto-acquire a new victim (CannotControlAnyMore), but an explicit order swaps it (the Max == 1 branch of Capture) [CONFIRMED-RE vs `campaign_runtime.inc:1650`].
- **ImmuneToPsionics:** honoured both for capture and for auto-acquire, with map overrides merged (W6-02-12). It matches CanCapture's +0xD35 check.
- **Events:** the capture raises events 6 and 44 with the controller's house, matching 0x6E4910 before the capture.
- **Enemy Yuris** (SOV11 BadGuy2) capture the player's units [CONFIRMED-PLAY s11]. What happens to those units afterwards is W7-10-01.
- **Save/load:** the mind-control state is saved (`save_fields.hpp:1384`, `u_mc_victim` / `u_mc_orig`).
- **PsiPulse:** damage vs ImmuneToPsionics infantry (dogs) is faithful. PsychicDamage does not block ImmuneToPsionics [CONFIRMED-RE 0x700DA0].

## 5. Evidence

- **Play scripts** (`10-evidence/`):
  - `y1.play`: skirmish, all types.
  - `s9.play`: SOV09 deploy, capture, release.
  - `s9b.play`: campaign-path spawns.
  - `s11.play`: SOV11 enemy Yuris.
  - `s12.play`: SOV12 roster.
  - `s3.play`: SOV03 Psi-Corps drop.
  - `s9-capture-crop.png`.
- **Run outputs:** in the session scratch dir `w7-10/play/out-*`.
- **RE:** dumps in `D:\ra2-re\w7-yuri-out`. Scripts in `D:\ra2-re\w7-yuri-scripts`.
- **REMAINING updates suggested:**
  - R-036: move from "guessed" to RE-backed (W7-10-02/03/04/09).
  - R-193: add W7-10-08.
  - U-5: add the W7-10-11 list.

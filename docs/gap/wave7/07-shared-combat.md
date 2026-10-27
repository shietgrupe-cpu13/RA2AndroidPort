# Wave 7 / 07 — Shared combat behaviour (all units)

Audit of master `b3a509f`. READ-ONLY: no engine edits and no commits.

**Tags:** [CONFIRMED-RE addr], [CONFIRMED-DATA], [CONFIRMED-PLAY], [SUSPECTED]. Clone citations are file:line on `b3a509f`.

**Method.** Three opus RE sub-audits ran Ghidra on retail gamemd.exe (YR 1.001). Each used its own project copy, and **all copies are deleted**.

| Slice | Topics | Outputs |
|---|---|---|
| A | crush, scatter, prone | `D:\ra2-re\w7-shc-A-out` (b1–b18), helper `w7-shc-A-scripts\multi.py` |
| B | acquisition, missions, retaliation, leash, attack-move | `D:\ra2-re\w7-shc-B-out` (b1–b8, d1–d3, s2–s6, am/) |
| C | veterancy, ROF, weapon selection, friendly fire, force-fire | `D:\ra2-re\w7-shc-C-out` (d*, sel/) |

**Other sources:**
- the rulesmd data (`D:\ra2-re\out\shroud_evidence\rulesmd.ini`);
- the RE docs;
- the clone code;
- the play run `07-evidence/ret.play`.

**Cross-references, not duplicated:**
- W7-03-01 (crush alliance);
- W7-03-03 / W7-06-05 (OpportunityFire);
- W7-03-04 (Mirage blink);
- W7-03-09 (AutoCrush IQ);
- W7-02-09 (computer infantry never scatter when hit).

## Summary

The damage pipeline is now faithful: DamageArea, Modify_Damage, Verses, the floors, the ring set, own/allied splash, the ROF jitter and the Burst timing. So are:
- the threat radius;
- the plain-Guard stale-target rule (R-212);
- weapon selection (SelectWeapon, found at FUN_006F2890);
- the fear and prone rules;
- the arithmetic and arming of veterancy.

The **behaviour layer above the damage pipeline is not faithful.**

**1. No mission model.** The clone has one implicit idle-guard state. The Mission Control keys (Retaliate, NoThreat, Paralyzed, Scatter, Rate) are never read. Computer and dog-type units never idle in Area Guard. Area Guard's home/return state machine is missing. Attack-move (YR mission 29) and Aggressive teams are missing.

**2. Retaliation is a different rule.** In the clone it is "idle and not moving, any mission". The original gates it by:
- mission;
- human vs computer (PlayerReturnFire=no);
- ammo;
- FireError;
- Verses > 1%;
- a threat comparison.

It then *overrides the mission with Mission_Attack*, with no leash, and resumes the old order afterwards.

**3. Target choice is nearest, not threat-scored,** and the 1% Verses "no passive acquire" rule is missing. GIs therefore auto-target tanks.

**4. ToProtect and the building call-for-help are missing.**

**5. Force-fire on own or allied units is impossible** or silently dropped.

**6. Crush and scatter:**
- crushers squash allied infantry (W7-03-01);
- the squash happens at reservation time with no warning, so infantry never get the chance to flee;
- the friendly hold-up scatter is too narrow;
- building placement over own units is refused rather than scattering them;
- the slow-projectile `Incoming` scatter is missing.

**7. Veterancy residue:**
- the ROF jitter is not scaled by VeteranROF;
- the house handicap is lost on a weapon-slot switch;
- kill credit for spawned aircraft, open-topped passengers and buildings is routed wrongly.

**8. The fv-a walk-back claim is resolved.** R-212's "finish the walk, no walk back" is CONFIRMED-RE for plain Guard. It is wrong for Area Guard, which does walk home (W7-07-06). The remaining anchor-leash walk-back (`kGuardStray`) is wrong for plain Guard (W7-07-07).

---

## Gap list

### A. Missions, acquisition, retaliation

#### W7-07-01 Retaliation gate and effect (MAJOR)

**Original.** [CONFIRMED-RE CanRetaliateToAttacker 0x707C40; ReceiveDamage tail 0x701F00..0x701FCC; FootClass::Override_Mission 0x4D8E60]

CanRetaliateToAttacker returns true only when all of these hold:
- the victim type has CanRetaliate (+0xD9A);
- the current mission has Retaliate=yes (Mission Control +8). Retaliate=no is set for Sleep, Harmless, Retreat, Enter, Eaten, Capture, Harvest, Hunt, Unload, Construction and Selling [CONFIRMED-DATA rulesmd ~L30430];
- the attacker is not allied (0x4F98B0) and is not disguised as the victim's house;
- the victim has ammo and is armed;
- a human victim has no TarCom;
- GetFireError is not 5 or 6;
- **human owner:** PlayerReturnFire=no (Rules+0x17EC) requires a building, or a unit in Guard, Area Guard or Patrol. A human unit under Move or Attack does not retaliate;
- the victim is not in a Suicide team;
- **computer owner with a TarCom:** it switches only if threat(attacker) ≥ threat(TarCom) (FUN_0070C190);
- the victim weapon's Verses vs the attacker armor is > 0.01.

The effect:
- A human unit that is out of range skips retaliation beyond `(Sight+0.5)*256` leptons.
- Otherwise it calls `Override_Mission(Attack, attacker)`, which saves the Destination, chases with no leash, and resumes the old mission afterwards.
- So **computer units retaliate while moving, then resume.**

**Clone.**
- `combat.cpp:262` (`deliver_hit_from`) and `:714` (`damage_area`) retaliate iff `target==-1 && !moving()`, in any mission.
- They never switch targets.
- They set a leashed auto target and re-plant the guard anchor (`:271`, `:719`).
- `deliver_hit_from` has no alliance gate.
- `damage_area` uses `is_enemy`, so a neutral shooter is never retaliated against; the original uses `!IsAlliedWith`.
- [CONFIRMED-PLAY] `ret.play` R1: a moving Conscript was hit (125→113) and kept `utgt=-1`. That is right for a human, wrong for a computer unit.
- [CONFIRMED-PLAY] R2: an idle Conscript hit by a Rhino from 6 cells retaliated (`utgt=5`), walked 2 cells, then stood at the leash and kept taking hits (→81).

**Fix.**
1. Implement `can_retaliate()` per the list above, using `are_allied`.
2. On success: `target=attacker`, `attack_order=true` (no leash), save the path goal and mission, and restore them afterwards.
3. Depends on W7-07-02.

#### W7-07-02 No mission model; the Mission Control keys are unread (MAJOR, foundation)

**Original.** [CONFIRMED-DATA] the Mission Control sections define, per mission:
- NoThreat at +4;
- Paralyzed at +7;
- Retaliate at +8;
- Scatter at +9;
- Rate / AARate.

`FUN_005B3430` returns `&MissionControl[CurrentMission]`. [CONFIRMED-RE 0x6F9BC7] TechnoClass::AI scans only in Move, Harvest and Guard (with FUN_00708710 permission).

**Clone.**
- There is no mission field. `area_guard` is a bool (`target_w6.hpp:30`).
- Campaign Sleep/Harmless use `set_dormant` (`campaign_runtime.inc:1333`).
- 0 readers of Retaliate, NoThreat, Paralyzed or the mission Scatter flag.

**Fix.** Add `Unit::mission` and the parsed table. W7-07-01, 05, 06, 07, 10, 11, 12 and 22 depend on it.

#### W7-07-03 Target choice is nearest; the original maximises a threat score (MAJOR)

**Original.** [CONFIRMED-RE 0x6F8320 Greatest_Threat, 0x6F71D0 Evaluate_Object, 0x70C190 (w7-shc-B-out/d1.txt), 0x6F6ED0]

Units and buildings are scored in one pass, and the maximum is kept. The scan exits early at radius/4 or radius/2 once something is found. The score:

```
v = TgtEff*Verses[tgtWH vs my armor] (negated if the target is not targeting me)
  + SpecialThreat*target.SpecialThreatValue
  + (EnemyHouseThreatBonus if target.house == my Enemy)
  + MyEff*Verses[myWH vs target armor]
  + Strength*target.HealthRatio
  + Distance*max(0, dist_cells - myRange_cells)
  + 100000
```

- The coefficients are the house's per-type set, else the Rules "Dumb" set (+0x1068..0x1088).
- The result is ftol'd, gets a health tweak and ×0.5 per allied object near the target (spread warheads), and has a minimum of 1.
- This overlaps W7-01-03 (ThreatPosed / SpecialThreatValue unread).

**Clone.**
- `combat.cpp:1186-1197` picks the nearest unit, with ties going to the lowest index.
- Buildings are considered only when no unit is found (`:1203-1245`).

**Fix.** Port `threat_value()` and the Evaluate_Object adjustments, with one max pass in ring order.

#### W7-07-04 The 1% Verses rule: no passive acquire, no retaliation (MAJOR)

**Original.**
- [CONFIRMED-RE 0x6F7270..0x6F727C] Evaluate_Object rejects a target with Verses ≤ 0.02.
- [CONFIRMED-RE 0x707F2x] CanRetaliate rejects one with Verses ≤ 0.01.
- [CONFIRMED-DATA rulesmd ~L26381] "A 1% means no retaliate, no passive aquire". This covers HollowPoint (GI vs vehicles and buildings), SANoBuilding and others.

**Clone.** `weapon.cpp:6-11` `verses_allows` blocks only 0%.

**Fix.** Keep 0% for force-fire. Add `> 2%` to the acquisition scans and `> 1%` to `can_retaliate`.

#### W7-07-05 Default idle mission should be Area Guard for computer, DefaultToGuardArea and GUARD_AREA units (MAJOR)

**Original.** [CONFIRMED-RE UnitClass::Enter_Idle_Mode 0x737990 @0x737B90..0x737BD0]

An armed idle unit, not in a team and not mind-controlled, enters Area Guard if any of these holds:
- house IQ ≥ [IQ] GuardArea=2 (Rules+0x1440; skirmish AI IQ 5, humans 0);
- it has veteran ability GUARD_AREA;
- its type has `DefaultToGuardArea` (+0xD39). That covers ADOG, DOG, YADOG, YDOG, DNOA, DNOB, DRON, SQD and CAOS [CONFIRMED-DATA].

**Clone.** `area_guard` is set only by map `Mission=Area Guard` (`campaign_runtime.inc:1309`) and the player's G command (`unit_orders.cpp:84`). DefaultToGuardArea has 0 readers. This subsumes the W7-01-03 row and W7-06-15.

**Fix.** Apply the rule on every idle transition (arrival, factory exit, target end).

#### W7-07-06 Area Guard leash and return-home state machine; R-212 is wrong for area guard (MAJOR)

**Original.** [CONFIRMED-RE Mission_Area_Guard FUN_004D41A0]

- **Leash:** `(FUN_007072E0(2)>>8)+6`, where FUN_007072E0(2) is 2×(GuardRange, else weapon range), clamped to 0x700..0x1000 leptons. That is **13–22 cells**.
- **On engage:** home = current cell (+0x5DC). Archive = the queued NavCom, else the Destination, else home.
- **Engaged:** the target is dropped when its path/zone distance from home (FUN_0042D0F0) reaches the leash.
- **Lost target:** rescan from home; if nothing is found, **walk back** to the nearer of home or archive, scanning on the way, then go to archive.

**Clone.**
- `combat.cpp:1093-1118` applies R-212 ("finish the walk, re-anchor at the path end") to every auto target.
- `:1323-1342` uses a `max(range,acq)+2`-cell anchor leash on both self and target.

**Fix.** For area-guard units, implement home/archive, the target-from-home leash and the rescan-or-return. Keep R-212 for plain Guard.

#### W7-07-07 Plain-Guard chase: no anchor, per-tick range drop (MINOR–MAJOR)

**Original.** [CONFIRMED-RE 0x6F9B8A..0x6F9C1E, TargetSomethingNearby 0x708CA0, Approach_Target 0x4D55B0 @0x4D5636..0x4D5679]

- At each targeting tick, a scan-acquired target is checked with `GetFireError(..,checkRange=1)`. A result of 5 (ILLEGAL) or 8 (RANGE) drops it, and the unit rescans from its **current** cell.
- Approach only if CanApproachTarget (+0xD33), else `SetTarCom(0)`.
- **There is no anchor and no walk back.**
- This confirms R-212 for plain Guard: removing fv-a's walk-back was correct.

**Clone.** `combat.cpp:26` `kGuardStray=2` and `:1323-1342`: an anchor leash plus a walk back to `guard_x/y`. This is RE-FALLBACK, and it is wrong.

**Fix.** Remove the anchor leash and walk-back for plain Guard. Drop out-of-range auto targets at each targeting tick and rescan from the current cell. Keep the W2-SK-25 approach.

#### W7-07-08 Scan cadence (MINOR; timing and determinism)

**Original.** [CONFIRMED-RE 0x708CA0 / 0x6F9B8A / 0x4D41A0 tail]
- Guard rescans every `NormalTargetingDelay 27 + Random(0,2)` frames (Rules+0xE08).
- Area Guard rescans every 36 + Random(0,2) (Rules+0xE04).
- A held, in-range target is never re-evaluated.

**Clone.** `combat.cpp:1129` scans every frame while idle. Reactions are up to 27–36 frames faster, and whole groups lock onto the same target on the same frame.

**Fix.** Add a per-unit targeting timer with a sync-RNG roll.

#### W7-07-09 ToProtect and building call-for-help are missing (MAJOR for the skirmish AI)

**Original.** [CONFIRMED-RE 0x701C43 → FUN_00707500; BuildingClass::ReceiveDamage 0x44223C]

**Trigger:** a computer techno with ToProtect=yes (+0xC96), or any computer building, is hit by a non-allied infantry or vehicle. A cooldown applies (+0x650). In the campaign, an armed victim does not call.

**Response:** up to 6 armed, recruitable, reachable units of the same owner that are not in a team (or are in a base-defence team) are sent at the attacker. They are chosen by value, within a budget of `attacker value × Rules+0xB18`.

**Clone.** `git grep ToProtect engine` finds 0 hits.

**Fix.** Add `call_for_help(victim, attacker)` on unit and building damage for computer houses.

#### W7-07-10 Harmless and Selling (NoThreat) objects are still auto-targeted (MINOR, campaign)

**Original.** [CONFIRMED-RE 0x6F732F..0x6F733B] Evaluate_Object rejects a target whose mission has NoThreat.

**Clone.** `world_w6.cpp:22-34` `legal_threat_unit` has no such test. The ALL09/ALL12 prisoners and Harmless civilians are shot.

**Fix.** Add a `no_threat` flag while Harmless or Selling, and reject it in the `legal_threat_*` functions.

#### W7-07-11 Sticky mission (MINOR)

**Original.** [CONFIRMED-RE 0x4D5636..0x4D5660] A Sticky unit drops an out-of-range target and never approaches. [CONFIRMED-DATA] `[Sticky] Paralyzed=yes Scatter=no`. How Sticky acquires was not traced [SUSPECTED].

**Clone.** A map `Mission=Sticky` unit behaves as Guard (approaches and scatters).

**Fix.** With `mission==Sticky`: no approach, no leash moves, no scatter.

#### W7-07-12 Attack-move is missing; Aggressive teams do not fight on the move (MAJOR / MINOR)

**Original.** [CONFIRMED-RE FUN_00730CC0, FUN_00730BC0, FootClass FUN_004DEFF0 / FUN_004DF230 / FUN_004DF3C0]

YR 1.001 has Attack Move:
- mission 29;
- the AttackMoveNav and AttackMoveTar cursors;
- a command-bar button;
- a modifier-key mode [SUSPECTED Ctrl+Shift].

The order is refused by PreventAttackMove (+0x6C8). The unit scans while moving, engages, then resumes the move.

[CONFIRMED-RE FUN_00708710 head] AI team members whose TeamType has Aggressive=yes (and not Suicide) also scan while moving. [CONFIRMED-DATA aimd.ini] 94 TeamTypes have Aggressive=yes and 32 have Suicide=yes.

**Clone.** Mission 29 and its input/order path are implemented. The attack-move scan was limited to the ordinary acquisition radius, and every moving unit stopped at the next cell even when it could fire while driving.

**Fix.** Attack-move scans visible units out to the mover's sight/acquisition radius and keeps the target while it remains visible within that radius, allowing it to approach before resuming the destination. Infantry stop and engage; ground vehicles and `OpportunityFire=yes` types keep their route and can fire while moving, subject to their normal weapon-facing gate. `PreventAttackMove` remains honored.

#### W7-07-13 Force-fire on own or allied units is impossible or silently dropped (MAJOR)

**Original.**
- [CONFIRMED-RE What_Action FUN_006FF380] With the force-fire key down, any object other than self gives ACTION_ATTACK, whatever the alliance.
- [CONFIRMED-RE GetFireError FUN_006FB5D0] It has no house test, so the unit fires and keeps the target.

**Clone.**
- `input_original.cpp:153-156`: Ctrl gives Attack only on non-controllable units.
- `world.cpp:1400` and `:1464` reject `can_command(issuer, tgt.house)`.
- An allied or neutral unit target passes input, but `valid_target` (`combat.cpp:61-66`, which requires `is_enemy`) clears it in step (d) (`:1093`) on the next tick.
- Allied buildings work: that path only checks alive.

**Fix.**
- Ctrl over any object not in the selection gives Attack.
- Drop the `can_command` guard for force-fire.
- Step (d) should use an explicit-order validity check that skips `is_enemy` while `attack_order` is set.
- Auto-acquire stays enemy-only.

### B. Veterancy and damage

#### W7-07-14 Veteran ROF jitter order; garrison ROF (MINOR–MAJOR)

**Original.** [CONFIRMED-RE FUN_006FC460 0x6FC55E..0x6FC60C]
- `r = ftol(ROF*houseROF + Random(0,2))`, then, if the unit has the ROF ability, `r = ftol(r*VeteranROF)`.
- Example: a veteran GI (ROF 20) gets {12,12,13}.

**Clone.**
- `world_veterancy.cpp:62` pre-multiplies 0.6 into `w_rof`, and `combat.cpp:203-206` adds the jitter afterwards. The veteran GI gets {12,13,14}: about 5% slower.
- Garrisons (`world_depth.cpp:272-275`) compute `ftol(ROF/n/OccupyROFMult)+jitter` and skip house ROF. The original is `ftol((ROF*house+R)/n/1.2)`. At ROF 20 and n 5 that is a constant 3 vs 3..5, so garrisons fire about 33% slower in the clone.

**Fix.** Store the un-multiplied base and a flag. Compute jitter → ability multiply → occupant division, with ftol at each step.

#### W7-07-15 `activate_weapon_slot` drops the house FirePower/ROF handicap (MINOR–MAJOR)

**Original.** [CONFIRMED-RE 0x6FC460, 0x6FD800] The house multipliers apply to every shot.

**Clone.** `weapon_unit.cpp:85-89` uses house = 1.0. Any slot switch loses the difficulty or handicap until the next rank refresh. Affected:
- Flak Trooper;
- Guardian GI;
- Apocalypse AA;
- aircraft attacks (`world_an.cpp:468/479`);
- OpenTransportWeapon (`world_w5.cpp:234`);
- `world_veterancy.cpp:79`.

**Fix.** Pass the house multipliers through.

#### W7-07-16 Kill-credit routing (MINOR)

**Original.** [CONFIRMED-RE FUN_007021D0 0x70232D..0x70247A] Credit is decided in this order:
1. An open-topped passenger credits the Trainable transport.
2. Otherwise a Trainable killer is credited itself.
3. Otherwise a MissileSpawn killer credits its SpawnOwner.
4. Otherwise a garrison credits the firing occupant.

The value is 0 only for allied victims (`IsAlliedWith`), so neutrals score [CONFIRMED-RE 0x7022FC].

**Clone.**
- `world_veterancy.cpp:140` credits the spawner for every spawned unit. This is wrong for Hornets and ASW planes, which are Trainable and should earn their own credit.
- Passenger kills credit the passenger (`world_w5.cpp:265`).
- Garrison splash kills get no credit.
- `world_veterancy.cpp:146` uses `is_enemy`, so neutral kills score 0.
- Buildings are never credited and have no rank (relevant to `[YAREFN] ElitePrimary`).

**Fix.** Apply the original order, use `are_allied`, and add building veterancy.

#### W7-07-17 Residual non-DamageArea damage paths (MINOR; residue of R-060)

**Original.** [CONFIRMED-RE FUN_00489190, combat.md §8] Every detonation goes through DamageArea.

**Clone.** `apply_spread_damage` (`combat.cpp:330-373`) still skips the firer's own house (`:343`), uses linear CB-2 falloff, ignores buildings and stamps no killer. Its callers are `world_an.cpp:472`, `world_depth.cpp:298` and `building_combat.cpp:462` (inert in stock). Passenger, defence and garrison point shots and shrapnel use the single-target `deliver_hit_from`.

**Fix.** Route every detonation through `damage_area` with its source set.

#### W7-07-18 Psychedelic (berserk) is missing (MINOR, Yuri content)

**Original.** [CONFIRMED-RE FUN_006FD210] `if (Techno+0x298) RearmTimer /= 2`. GetFireError and What_Action read the same flag (attack anything, no player control) [SUSPECTED name Berzerk]. [CONFIRMED-DATA] The Chaos Drone's PsychGasCreate warhead has `Psychedelic=yes`.

**Clone.** No consumer. **Fix:** a berserk timer that applies ×2 ROF, any-target acquisition and loss of control.

#### W7-07-19 Aircraft chevron offset (COSMETIC)

**Original.** [CONFIRMED-RE V13] +(5,2) for infantry, +(10,6) for everything else.

**Clone.** `main.cpp:3683` chooses by `is_vehicle`, so aircraft get the infantry offset.

**Fix.** Test `is_infantry` instead.

### C. Crush, scatter, prone

New anchors from slice A [CONFIRMED-RE]:
- Scatter = vtbl+0x174 (Infantry FUN_0051CEA0, Unit FUN_00742880).
- CanEnterCell = vtbl+0x1AC (Unit FUN_0073DFA0; MOVE enum 0 OK, 6 TEMP, 7 NO).
- **CellClass::Incoming FUN_00481580.**
- The **squash site UnitClass::Crush(cell, warn) 0x7404B0** (vtbl+0x534).
- `[CombatDamage] Incoming=` at Rules+0x16C0; CloseEnough at Rules+0x1718.

Incoming scatters an occupant iff any of these holds:
- (an Elite unit is in the cell and `!nokidding`);
- `nokidding`;
- PlayerScatter;
- the occupant has the SCATTER ability;
- `[IQ] Scatter (2) ≤ owner IQ`.

Humans have IQ 0, so by IQ alone only computer units answer.

#### W7-07-20 Crushers squash allied infantry; allied units never scatter (MAJOR; = W7-03-01)

**Original.** [CONFIRMED-RE 0x5F64C0 IsCrushable, 0x4F98B0 IsAlliedWith]

**Clone.** `world.cpp:5670` uses `v.house != u.house`.

**Fix.** Use `!are_allied` (not `is_enemy`: neutrals stay crushable). The scatter half is W7-07-22.

#### W7-07-21 The crush happens without warning; infantry never get a chance to flee (MINOR, balance)

**Original.** [CONFIRMED-RE 0x4B3D85, 0x740564-0x74058A, 0x481580, 0x739FB9]
- Before the tank enters a cell, Drive calls `Crush(cell,1)`, which calls `Incoming(Empty, forced=1, nokidding=0)`. Computer infantry always scatter; human infantry scatter only with SCATTER, PlayerScatter, or an Elite unit in the cell.
- The squash itself (`Crush(cell,0)`) runs on **arrival** (Per_Cell_Process FUN_00739AC0). It hits every crushable object in the cell, plays the **victim's** CrushSound, credits the kill (FUN_007021D0), and removes the victim with no death anim.

**Clone.** `world.cpp:5662-5680` kills at step-reservation time, with no warning.

**Fix.**
- On reservation, `order_scatter` the eligible occupants.
- Squash on arrival (`world.cpp` ~5745) only those still present.

#### W7-07-22 The friendly hold-up scatter is too narrow; the CloseEnough stop is missing (MINOR)

**Original.** [CONFIRMED-RE 0x73DFA0; Drive 0x4B2550 @0x4B2CE0 / 0x4B319D / 0x4B385A / 0x4B4357; Walk 0x75A6B1]

CanEnterCell returns MOVE_TEMP for an **allied**, stationary occupant. That can be any Foot, vehicles included, and its target does not matter. On MOVE_TEMP:
- if the mover is within CloseEnough (2.25 cells) of its goal, it **stops** (`Assign_Destination(0)`);
- otherwise it calls `Incoming(Empty,1,1)`, and every occupant scatters.

The Walk locomotor does the same for infantry movers.

**Clone.** `world.cpp:5757-5770` handles:
- vehicle movers only;
- sub-cell infantry occupants only;
- `house == u.house` only;
- occupants with `target<0` only;
- no CloseEnough stop.

**Fix.**
- Use `are_allied`.
- Include vehicle occupants and infantry movers, and drop the target condition.
- Stop the move within 2.25 cells of the goal.

#### W7-07-23 Building placement over own or allied units is refused instead of scattering them (MINOR, UX)

**Original.** [CONFIRMED-RE FUN_0045EE60, caller 0x443BC0 @0x445170]

Allied Foot occupants give result 1 ("pending"). Idle ones get `Incoming(Empty,1,1)`, and the placement is queued (house +0x5700, FUN_0042F300) and retried until the cells clear.

The same function is used by MCV deploy (0x738556). The factory door does the same ("Weapons factory clearing %s from door", FUN_00449490). Whether the cursor shows those cells green is [SUSPECTED].

**Clone.** `placement_check` (`building.hpp:215-227`, `building.cpp` `occ.is_free`) treats any unit as blocking, so the click fails.

**Fix.** Treat allied ground units as pending: scatter them, then retry the placement each tick.

#### W7-07-24 Damage-scatter details (MINOR; complements W7-02-09)

**Original.** [CONFIRMED-RE 0x700DA0 @0x701ED5-0x702196] Damage scatter uses `Scatter(Empty,1,0)` in two branches.

**Retaliate branch** (0x701FD2): a Foot with no Target and no Destination scatters if:
- PlayerScatter; or
- veteran with VeteranAbilities SCATTER; or
- elite with Veteran or Elite SCATTER.

There is **no human gate**.

**Non-retaliate branch** (0x702089) additionally requires the mission's Scatter flag, a stationary unit, and no Target or Destination. Then a computer owner always scatters; a human owner needs PlayerScatter or SCATTER.

**Clone.** `world_w5.cpp:176-192` (veteran SCATTER: an idle ranked unit that loses HP steps aside) has no mission gate. Sleep, Sticky, Harvest, Unload and Construction units still step aside. There is also no computer branch (W7-02-09).

**Fix.** Add the mission Scatter flag to the non-retaliate path, alongside W7-02-09's computer branch.

#### W7-07-25 The player Scatter command (X) moves deployed infantry; the direction is not threat-based (MINOR)

**Original.** [CONFIRMED-RE EVENT_SCATTER 0x4C77D1; InfantryClass::Scatter 0x51CEB3-0x51CF18]
- `Scatter(Empty,1,0)`. A human infantry in a Deploy or Deployed sequence ignores it. Moving units do not scatter.
- Both Scatters go to `MapClass::NearByLocation`. The 8-way fallback starts at the threat-away direction ± random (infantry `Random(0,4)-2`, unit `Random(0,2)-1`).

**Clone.** `unit_orders.cpp:87-108` uses a uniform random start in 0..7, is never threat-directed, and undeploys a deployed GI or Guardian GI through `order_unit_move` (`world.cpp:1211`).

**Fix.** Skip deployed human infantry, and add a threat-coordinate overload of `order_scatter`.

#### W7-07-26 Slow-projectile `Incoming` threat scatter is missing (MINOR)

**Original.** [CONFIRMED-RE 0x5203E0 @0x5207A0-0x5207F7] After an infantry shot with weapon +0xA8 < `[CombatDamage] Incoming=10`, it calls `Incoming(shooter coord,1,0)` on the target cell. [SUSPECTED] +0xA8 is Speed: Grenade, FireballLauncher, RadEruption, PsiWave, GrandCannon and Bomb qualify.

**Clone.** `Incoming` has 0 readers.

**Fix.** Read the key, and scatter the eligible occupants of the target cell away from the shooter.

#### W7-07-27 Fraidycat panic scatter is wrongly limited to computer houses (MINOR)

**Original.** [CONFIRMED-RE FUN_0051FDE0 tail 0x51FF0C] The panic scatter has no human gate. Only the idle-roll scatter (0x51CC21) is computer-only.

**Clone.** `inf_anim.cpp:181-183` adds `!c.human`.

**Fix.** Drop `!c.human` at line 182 only.

#### W7-07-28 Crush presentation (COSMETIC)

**Original.** [CONFIRMED-RE 0x7406DC-0x740720] Only the victim's CrushSound plays: InfantrySquish, or TankCrush when a vehicle is omni-crushed. The victim is removed outright, with no death sequence, anim or explosion.

**Clone.**
- `pres_game.inc:1056-1064` also plays the crusher's CrushSound (TankCrush on every squish).
- It detects a crush by an adjacency heuristic.
- The normal death plays.
- An omni-crushed vehicle gets no squish.

**Fix.** Set an explicit `crushed` flag at the squash site, play only the victim's sound, and vanish the victim.

#### W7-07-29 Iron-Curtained units can be crushed (MINOR, edge case)

**Original.** [CONFIRMED-RE 0x5F651A / 0x5F6567 → vtbl+0x160 FUN_0041BF70] IsCrushable is false while the Iron Curtain timer runs.

**Clone.** `combat.cpp:557-566` `can_crush` has no such test.

**Fix.** `if (v.an.iron_left_frames > 0) return false;`.

### D. Cloak, Sensors and disguise

- **Cloak:** Evaluate_Object (0x6F71D0 / 0x6F7E90) has no explicit cloak test. Cloak is probably rejected through GetFireError==ILLEGAL [SUSPECTED; not traced]. The clone gates on `unit_hidden_from` / `seen_by_mask` (`combat.cpp:66`, `1183`). No gap is filed until the GetFireError cloak branch is traced.
- **Disguise:** [CONFIRMED-RE Evaluate_Object, slice B] A scanner without DetectDisguise (+0xD31 [SUSPECTED name]) rejects a disguised target unless that target's fake-blink timer (+0x1EC) is running. During a blink, a **computer** scanner accepts it if `Random(0,99) <= Rules+0xE14[difficulty]` (DisabledDisguiseDetectionPercent). Humans never accept it. This feeds W7-03-04.

---

## Verified OK
- **Damage pipeline** (DamageArea, Modify_Damage): linear falloff, truncation, MaxDamage 10000, building floor 1, ring set, own/allied splash, AffectsAllies, source skipped [CONFIRMED-RE 0x489090 / 0x489190 / 0x700DA0; `combat.cpp:456-789`].
- **ROF and Burst timing:** `ROF + Random(0,2)` for rookies, buildings and defences; intra-burst `Random(3,5)`; burst index order; no first-shot delay [CONFIRMED-RE 0x6FC460 / 0x6FD210; `combat.cpp:203-220`].
- **Verses:** 0% is ILLEGAL (`slot_can_target`). The defended-hit floor is 1 [CONFIRMED-RE 0x6FB5D0 / 0x700DA0].
- **Weapon selection:** SelectWeapon is now located at **FUN_006F2890** (weapons.md §2 had it as "not located"). Its effective choice matches the clone's `choose_weapon` + `an_naval_slot` for every stock two-weapon type and armor, ground and air. This was checked with `w7-shc-C`'s `sel.py`; the literal rule differs only when both weapons are AA, and no stock type is affected.
- **Threat radius:** FUN_007072E0(0) and (1), with area guard ×2 capped at 0x1000 (W6-02-11).
- **Plain-Guard stale target** (R-212): finish the walk, no walk back. An owner change means Stop [CONFIRMED-RE 0x4D55B0 / 0x706C50 / 0x4D9870 / 0x737990 / 0x700940].
- **Moving units without OpportunityFire** (and outside Aggressive teams) do not auto-acquire [CONFIRMED-RE 0x6F9BC7 / 0x708710].
- **Retaliation exceptions:** no retaliation against a disguised attacker. CanRetaliate=no is read. Sleep and Harmless units do not retaliate. Sleep units stay targetable.
- **Approach (W2-SK-25):** the close-in matches Approach_Target with CanApproachTarget; the Mirage is excluded.
- **Building targets:** humans pick only armed buildings, computers any [CONFIRMED-RE 0x6F7ADB].
- **Veterancy** is armed in skirmish, campaign and load (`main.cpp:5579`, `6249`, `7118`). The arithmetic matches V1–V11:
  - VeteranRatio 3 and VeteranCap 2 (cap on the unrounded double);
  - victim value ×2 / ×3, DontScore, Trainable;
  - ability gating, with elite inheriting the veteran abilities;
  - ElitePrimary / EliteSecondary;
  - SELF_HEAL every 14 frames, +1 HP;
  - STRONGER applied before Verses.
- **Promotion feedback:** the UpgradeVeteran / UpgradeElite sounds and the EVA line play for the local house. The elite flash lasts 150 frames (`pres_game.inc:70-71`, `169-176`; `world_veterancy.cpp:185`).
- **Produced veterans and team VeteranLevel** are handled (`world.cpp:1061`, `campaign_runtime.inc:1268`).
- **Crush gates:** Crusher or the CRUSHER ability, OmniCrusher / OmniCrushResistant, the building exclusion, all three sub-cell spots crushed, DeployedCrushable (the timing during the deploy anim aside), and no stock vehicle with Crushable=yes [CONFIRMED-RE 0x5F64C0 / 0x7404B0 / 0x520874; CONFIRMED-DATA].
- **Crush steering:** Crush 1.8 in 3-D, PlayerAutoCrush / AutoCrush [CONFIRMED-RE 0x742720] (the IQ gate is W7-03-09). Crush kills are credited to the crusher.
- **Fear and prone (FUN_0051FDE0):** fear decays unless Fearless; Down at > 49 (skipped for human units with a destination or a moving loco, never for Fraidycat); Up at < 50. The idle-roll Fraidycat scatter is computer-only (`inf_anim.cpp:129-183`).
- **ScatterCommandClass FUN_007300B0:** the ownership test, event 7 and the UI sound match `group_scatter` (apart from W7-07-25).
- **Ground force-fire** (Ctrl on a cell) exists, and an explicit attack pursues.

## Prioritized fix list
| # | Gaps | Effort |
|---|---|---|
| 1 | W7-07-20 + W7-07-22 alliance crush and hold-up scatter (with W7-03-01) | S |
| 2 | W7-07-13 force-fire legality | S |
| 3 | W7-07-04 1% Verses rule | S |
| 4 | W7-07-02 mission enum and table → W7-07-01 retaliation, -10 NoThreat, -11 Sticky, -24 scatter gate | M |
| 5 | W7-07-05 / -06 / -07 / -08 Area Guard default and state machine, Guard chase, scan cadence | M |
| 6 | W7-07-03 threat scoring | M |
| 7 | W7-07-09 call-for-help | M |
| 8 | W7-07-12 attack-move + Aggressive teams (shared with W7-03-03) | M |
| 9 | W7-07-21 / -23 / -26 crush warning, placement scatter, Incoming | S–M |
| 10 | W7-07-14 / -15 / -16 / -17 veterancy and damage residue | S–M each |
| 11 | W7-07-18 / -19 / -25 / -27 / -28 / -29 | S each |

**Evidence:** `07-evidence/ret.play`; the Ghidra outputs listed under Method.

# Wave 6 / 04: Unit, structure and superweapon fidelity matrix (Allied + Soviet)

**Audit tree:** branch `w6-units` @ `e2b569e` (master after the P1/P3/P4 mission merges), Release build `build-rel/`.
**Scope:**
- every buildable Allied and Soviet infantry, vehicle, aircraft, ship, structure, defence, wall and superweapon under the YR `rulesmd.ini` (U-1, decided);
- country specials;
- the YR additions for these two sides.
Yuri-faction content is out of scope. In YR the Psychic Sensor, Cloning Vats, Psychic Beacon and Dominator, Yuri Tank Bunker and Citadel Wall are Yuri-only (`Prerequisite=YACNST/YATECH/...`); this was confirmed in play (`cameo=0` for both sides). YR has no separate "Black Hawk": `[SHAD]` is the Night Hawk, which was audited instead.

**Method.** Four opus sub-audits (A Allied ground, B Soviet ground, C air and naval, D structures and superweapons) worked from:
- **The spec:** `rulesmd.ini` / `artmd.ini`.
- **The RE docs:** `docs/re/*.md`.
- **Ghidra:** private copies `D:\ra2-re\w6-units-{C,D}`, used where behaviour is code-driven.
- **Play runs:** 20 `--play` runs, one heavy run at a time through `04-evidence/run-play.ps1` (a global mutex), using real clicks (`click unit 0:X`, then `click unit 1:Y`) wherever possible.

Their full per-unit write-ups are kept as appendices in `04-parts/A.md` .. `D.md`, with gap IDs remapped to the numbering below. A gap reported by two parts has one ID here.

**Evidence:**
- scripts: `docs/gap/wave6/04-evidence/scripts/{A,B,C,D}-*.play`;
- screenshots: `04-evidence/*.png`;
- step logs: `04-evidence/C-logs/`, `04-evidence/A-*-result.txt`;
- full logs: `play-out/w6-*` (not committed).

**Time base trap.** The skirmish lobby's default Game Speed 1 runs **4 logic frames per `--play` driver frame**. Every timing below was converted to logic frames before it was compared with a rules value. `slider:speed=2` gives a 1:1 ratio.

**Legend:**
- `OK` = measured in play.
- `OK?` = checked by code and rules reading only.
- `WRONG` / `MISSING` = a gap, with its ID.
- `N/A` = does not apply.

## Summary

**38 gap rows. None is a BLOCKER:**
- 21 MAJOR, one of which (W6-04-21) is a DATA-LIMIT re-check;
- 16 MINOR;
- 1 UNCONFIRMED.

**New or still open:**
- 31 rows are NEW.
- 7 rows are still open under an existing ID: P4-5, R-063 (residue), R-201, R-090 ×2, R-191 and W2-AN-26.

What a player notices first:

1. **Burst weapons fire half (or a quarter) of their shots** (W6-04-01): Apocalypse, IFV, every elite tank, Boris's MiGs. Nothing in the engine reads `Burst=`.
2. **Soviet base-busting does not work** (W6-04-03): Crazy Ivan bombs, Terrorists, Demo Trucks and death weapons hit a building once instead of once per foundation cell. An Ivan leaves a Battle Lab at 50/500.
3. **Signature YR/RA2 mechanics silently missing:**
   - Prism Tank beam split and elite Tesla bounce (W6-04-02);
   - Aegis anti-air (W6-04-06);
   - Dolphin vs Squid (W6-04-09);
   - Battle Fortress crush and its passenger weapon (W6-04-14 / 15);
   - Siege Chopper deploy (W6-04-17).
4. **Buildable-roster holes:**
   - Terror Drone (W6-04-10);
   - Robot Tank and Robot Control Center (W6-04-18);
   - Siege Chopper (W6-04-17);
   - Battle Bunker and Industrial Plant (W6-04-21);
   - the YR Spy Plane and Force Shield superweapons (W6-04-19 / 20, a scope decision).
5. **Walls** (W6-04-04 / 05):
   - Built walls are chipped by rifles but shrug off nukes.
   - Map wall overlays cannot be shot at all (P4-5).
6. **Naval C4** (W6-04-07): Tanya and SEALs swim but cannot reach a ship to plant C4.

**Confirmed fixed or working on this tree** (details in §3):
- **R-049** (the Giant Squid never fires) **did not reproduce**: squids of both houses grab ships in play. REMAINING should move R-049 to DONE for the grab; the eject half is W6-04-09.
- **R-047** (Mirage tree draw) was not re-confirmed visually: the screenshot was under shroud.
- **Boris airstrike** (R-046) works.
- **Harrier / Black Eagle rearm** works at ReloadRate.
- **Kirov:** bombing, crash and self-heal work.
- **Destroyer + Osprey** kill a submerged Typhoon.
- **Dog vs infantry** works when the target is reachable.
- **Tanya** swims.
- **Superweapons:** the Iron Curtain, Chronosphere, Nuke and Lightning Storm charge times, durations and damage all match rules.
- **IFV modes:** all seven passenger weapon swaps are correct.
- **Desolator** deploy and radiation decay work.

## 1. Gap entries

The columns used per entry are: what the player experiences, evidence, original behaviour, severity, effort, fix location. The full narrative is in the cited appendix section.

### MAJOR

#### W6-04-01: `Burst=` is ignored by every weapon (NEW; parts A, B, C)

| Field | Details |
|---|---|
| **Player** | Every Burst>1 weapon fires one shot per ROF cycle. That is 50 rulesmd weapons, including:<ul><li>Apocalypse 120mmx (2, elite 4) and its AA tusks;</li><li>IFV HoverMissile (2, elite 4);</li><li>elite Grizzly 105mmE, elite Rhino 120mmE, elite Tank Destroyer SABOTE;</li><li>elite Flak Trooper and Flak Track;</li><li>Boris's MiG Maverick3 (2);</li><li>Aegis MedusaE, Destroyer 155mmE, Typhoon SubTorpedoE;</li><li>SCHP 160mmE.</li></ul>DPS is ½ to ¼ of the original. In play an Apocalypse lost a duel to one Rhino. |
| **Evidence** | <ul><li>`git grep '"Burst"' engine` finds 0 readers.</li><li>`combat.cpp:103` `try_fire` sets `rof_cool = rearm_secs(w_rof)` on every shot.</li><li>`B-soviet2.play` block B: the Apocalypse took a Radar 1000 → 900, and the next drop came about 80 frames later; a volley would give 1000 → 800.</li></ul> |
| **Original** | <ul><li>`TechnoClass::Fire` FUN_006FD210 advances `CurrentBurstIndex` (+0x3B8) modulo `Burst` (+0x9C).</li><li>`GetROF` FUN_006FC460 returns Random(3,5) between shots of a burst (or the type's per-burst delay at +0xE44), and the full ROF only after the last shot.</li><li>Sources: docs/re/combat.md §5, veterancy.md §6.1.</li></ul> |
| **Severity / effort** | MAJOR / M |
| **Fix** | Add `burst` to `WeaponSlot`/`UnitCombat`, elite slots included. Add `Unit::burst_index`. Change `combat.cpp` `try_fire`/`rearm_secs`, `building_combat.cpp` (defence weapons) and `world_an.cpp` (aircraft Ammo: Maverick3 is Ammo=1, Burst=2). |

#### W6-04-02: Shrapnel keys are read from the weapon section instead of the projectile, so the Prism Tank never splits and elite Tesla units never bounce (NEW; regression of W2-SK-22; parts A, B)

| Field | Details |
|---|---|
| **Player** | <ul><li>A Prism Tank shot into a group hurts only the unit it hits.</li><li>An elite Tesla Trooper or Tesla Tank bolt does not arc on to 2 more targets.</li></ul> |
| **Evidence** | <ul><li>`main.cpp:854-855` reads `section_string(primary, "ShrapnelWeapon")`.</li><li>In rulesmd, `[Comet]` has no such key; it sits on `[LargeCometP] ShrapnelWeapon=CometFragment ShrapnelCount=5`, `[Electricbounce]` (TeslaFragment ×2) and `[SuperCometP]`. Verified by the lead.</li><li>`A-probe.play` P4: 4 adjacent Conscripts summed 625 → 525, exactly the Comet's 100, with no fragments.</li></ul> |
| **Original** | `ShrapnelWeapon`/`ShrapnelCount` are BulletType keys (projectile.md, rules). |
| **Severity / effort** | MAJOR / S |
| **Fix** | `resolve_combat_unit` (`main.cpp`): read the keys from `rules.weapon_projectile(w)` per slot, elite slots included, and swap them in `refresh_vet_stats`. |

#### W6-04-03: Ability blasts bypass the faithful DamageArea, so buildings take a fraction of the damage (NEW; R-060 fixed only the weapon path; part B)

| Field | Details |
|---|---|
| **Player** | <ul><li>A Crazy Ivan bomb leaves a Battle Lab at 50/500 HP.</li><li>A Demolition Truck leaves one at 291/500.</li><li>The original destroys it in both cases: 1323 damage, computed with the clone's own faithful port.</li><li>Terrorists, death weapons (Apocalypse Explodes, Demo Truck) and the Desolator eruption go through the same path.</li></ul> |
| **Evidence** | <ul><li>`world_abilities.cpp:236` `ability_area_damage`: 2-D linear falloff, one hit per building at its nearest cell. It has no Iron Curtain check, no AffectsAllies, no veterancy armour and no ProneDamage.</li><li>The faithful path is `combat.cpp:505 damage_area` (FUN_00489190, one hit per foundation cell in the rings).</li><li>`B-soviet2.play` F and `B-soviet3.play` E; screenshot `04-evidence/B-dtruck.jpg`.</li></ul> |
| **Original** | BombClass::Detonate 0x4386A0 calls DamageArea; FireDeathWeapon detonates a bullet. Both run through FUN_00489190 (abilities.md §E/§F). |
| **Severity / effort** | MAJOR / S-M |
| **Fix** | Route `ability_area_damage` through `damage_area` with a synthetic shooter (ability warhead Verses, CellSpread, PercentAtMax). Keep the eruption's RadSite creation. |

#### W6-04-04: Built walls take damage from any warhead and ignore Wall / WallAbsoluteDestroyer (NEW; part D)

| Field | Details |
|---|---|
| **Player** | <ul><li>A GI's rifle chips an enemy wall (300 → 294).</li><li>A Nuke only scratches walls; in the original it erases every wall in its blast rings.</li></ul> |
| **Evidence** | <ul><li>`D-defences.play` L66.</li><li>Walls are 1×1 HP buildings (R-195).</li><li>The `wall` warhead field (`weapon_resolve.cpp:35`) is read only by the bridge code.</li><li>Affected paths: `combat.cpp` damage_area / deliver_building_hit, `world_sw.cpp` an_area_damage, `world_abilities.cpp` ability_area_damage.</li></ul> |
| **Original** | <ul><li>FUN_00489190 @0x489581..0x4895BD, per wall cell:<ul><li>`WallAbsoluteDestroyer` (WH+0x145) destroys the wall;</li><li>else `Wall` (+0x144) calls `Reduce_Wall(dmg)`;</li><li>else `Wood` (+0x147) on wood walls calls `Reduce_Wall(dmg)`;</li><li>otherwise nothing happens.</li></ul></li><li>`Reduce_Wall` FUN_00480BC0 has no HP. It advances a damage stage with probability dmg/Strength.</li><li>Ghidra output: `D:\ra2-re\w6-units-D-out\d1.txt`.</li></ul> |
| **Severity / effort** | MAJOR / M |
| **Fix** | Gate building damage on a wall type in the four damage paths above. Stage model per FUN_00480BC0. |

#### W6-04-05: Map wall overlays cannot be damaged by any weapon (STILL OPEN, P4-5; part D)

| Field | Details |
|---|---|
| **Player** | Force-firing sandbags or concrete or a prison fence on a campaign map does nothing. Only crushing removes one (P4-4). |
| **Evidence** | <ul><li>Code: `world.hpp:821-846` and `world.cpp:695-726` hold crush state only, and no combat path touches overlays.</li><li>Not measured: the stock skirmish map has no wall overlays near the start. A campaign repro (e.g. ALL05 silo walls) is the next step.</li></ul> |
| **Original** | As W6-04-04 (FUN_00489190 → FUN_00480BC0). |
| **Severity / effort** | MAJOR / M |
| **Fix** | Overlay damage stage (OverlayData high nibble), called from `damage_area` with the W6-04-04 gate. Do it together with W6-04-04. |

#### W6-04-06: Ships ignore `OmniFire=yes`; the Aegis turns its ROT-1 hull before firing and loses to a lone Kirov (NEW; part C)

| Field | Details |
|---|---|
| **Player** | <ul><li>The Aegis acquires a Kirov at 11.9 cells but fires its first Medusa at about 4.5 cells. The Kirov sinks it and keeps 1706 of its 2000 HP.</li><li>Carrier, Dreadnought and Destroyer launchers also wait for the hull to turn.</li></ul> |
| **Evidence** | <ul><li>`play-out/w6-C-naval2/play.log` (sampled table in the C appendix); screenshot `04-evidence/C-aegis-kirov.jpg`.</li><li>`Unit::omni_fire` is set only for jumpjets (`main.cpp:928`, inside `is_aircraft`) and for Fly/Rocket types (`w2air_main.inc:72`). The aim gate is at `combat.cpp:796/907/1104`.</li><li>`WeaponSlot::omni_fire` (`weapon_resolve.cpp:34`) is parsed but never used.</li></ul> |
| **Original** | `OmniFire` = WeaponType+0x12B bypasses the turn-to-face gate (aircraft.md §4). Medusa, ASWLauncher, HornetLauncher, DredLauncher and BlackHawkCannon all have OmniFire=yes. |
| **Severity / effort** | MAJOR / S |
| **Fix** | Set `u.omni_fire` from the active slot for every unit (`activate_weapon_slot` / `resolve_combat_unit`). |

#### W6-04-07: Tanya and SEALs never reach a ship to plant C4 (NEW; R-001 swim is fixed; part A)

| Field | Details |
|---|---|
| **Player** | <ul><li>Tanya on the water, ordered onto a Destroyer 3.6 cells away, stays "moving" in place for about 450 frames and is shot down.</li><li>A SEAL on the shore never enters the water to reach a ship.</li><li>A SEAL adjacent to a Sea Scorpion does C4 it in one hit, so the weapon works and the approach does not.</li></ul> |
| **Evidence** | <ul><li>`A-water2.play` (`04-evidence/A-A-water2-result.txt`): DEST at 600 HP throughout, `sel_target=3`, `moving=1`.</li><li>`world.cpp:4626-4637`: non-naval approaches call `assign_formation(..., passable_)`, the land grid. The naval branch at 4602 uses `grid_for(movement_domain)`.</li></ul> |
| **Original** | The approach uses the unit's own MovementZone, AmphibiousDestroyer (naval.md §1/§4). |
| **Severity / effort** | MAJOR (ALL09 / SOV09 SEALs, any naval C4) / S |
| **Fix** | `World::step` approach dispatch: pass `grid_for(u.movement_domain)` for Amphibious units. |

#### W6-04-08: Transport capacity counts bodies; the original sums passenger `Size` (NEW; the RE corrects transport-cargo.md §1; part C)

| Field | Details |
|---|---|
| **Player** | <ul><li>The Amphibious Transport (Passengers=12) loads 5 Grizzlies, and would load 12.</li><li>The original takes 4 Grizzlies (Size 3) or 2 Apocalypses (Size 6).</li></ul> |
| **Evidence** | <ul><li>`C-naval1` N2: `cargo_total(0)=5`.</li><li>`cargo.cpp:42` `return cargo.size() < passengers_max` (lead-verified).</li></ul> |
| **Original** | <ul><li>`UnitClass::ReceiveMessage` FUN_00736500 (cases 0xE/0xF) refuses when `Passengers < GetTotalSize() + Size` (+0x5E0, +0x380; `GetTotalSize` FUN_00473390 sums Size).</li><li>`docs/re/transport-cargo.md` §1 read only the FootClass base FUN_004190B0, which UnitClass overrides.</li><li>Garrisons (FUN_0043C250) really are a count.</li></ul> |
| **Severity / effort** | MAJOR / S |
| **Fix** | `cargo.cpp` `can_load`: a size sum for unit transports, the count for buildings. Correct transport-cargo.md §1. |

#### W6-04-09: A Sonic hit (Dolphin) or a healing hit does not free a host from its parasite (NEW; R-049 eject half; part C)

| Field | Details |
|---|---|
| **Player** | <ul><li>A ship held by an enemy Giant Squid cannot be saved: the Dolphin cannot target the limboed Squid, and a Dolphin shot or a Naval Yard repair on the ship does not free it.</li><li>The same applies to a Terror Drone inside a tank under repair.</li></ul> |
| **Evidence** | <ul><li>`C-naval2` R3: `parasited=1` from f=588 to 835, DEST 585 → 458.</li><li>`click unit 1:SQD` → "no unit".</li><li>`git grep '"Sonic"' engine` finds 0 hits.</li><li>`abilities.cpp:148` has no heal eject.</li><li>Screenshot `04-evidence/C-dolphin-squid.jpg`.</li></ul> |
| **Original** | <ul><li>`FootClass::ReceiveDamage` FUN_004D7250: when the warhead has `Sonic` (+0x14B, loader 0x75C3B0) and the host has a parasite (+0x694), it calls `ParasiteClass::ExitUnit` FUN_00629F40.</li><li>A negative-damage (heal) hit also ejects (abilities.md §B3).</li></ul> |
| **Severity / effort** | MAJOR / S |
| **Fix** | Parse `Sonic=` into the warhead profile. On a Sonic hit or a heal of a host with `parasite_on_me >= 0`, run the existing exit code (`world_abilities.cpp` ~585-608). |

#### W6-04-10: The Terror Drone cannot be built (NEW; part B)

| Field | Details |
|---|---|
| **Player** | No Terror Drone cameo with a War Factory up. One of the four basic Soviet vehicles is missing. |
| **Evidence** | <ul><li>`B-soviet.play` L47: `cameo(DRON)=0` while V3, HTK and APOC show.</li><li>`artmd [DRON] Voxel=no`. `load_unit_roster_table` (`main.cpp` ~1934) uses the SHP-vehicle branch only for the naval locomotor (DLPH/SQD).</li><li>DRON goes to the voxel loader, which fails, and `if (!va.ok) continue;` drops it.</li><li>The AI list (`ai_skirmish.inc:29`) probably loses it too (inferred).</li><li>A cheat-spawned drone works fully.</li></ul> |
| **Original** | `[DRON] TechLevel=4 Prerequisite=NAWEAP`. |
| **Severity / effort** | MAJOR / S |
| **Fix** | Take the SHP-vehicle branch for any `!art.voxel(image)` vehicle; check it renders. |

#### W6-04-11: Ivan bombs (and other ability events) have no presentation (NEW; part B)

| Field | Details |
|---|---|
| **Player** | <ul><li>No flickering bomb icon on a bombed object.</li><li>No `CrazyIvanAttack` attach sound and no `CrazyIvanBombTick`.</li><li>The player cannot tell which of his units carry a bomb, so Engineer defusing is guesswork.</li></ul> |
| **Evidence** | <ul><li>`pres_audio_events.inc:141` consumes only `AbilityEvent::Deploy`. BombPlant, BombBlast, Infect, Suicide, Eruption and Teleport are dropped (`main.cpp:6303`).</li><li>Screenshot `04-evidence/B-ivan-bomb.jpg`.</li></ul> |
| **Original** | `[AudioVisual] BombAttachSound / BombTickingSound`, `IvanIconFlickerRate=8` (abilities.md §E). The icon SHP and its offset are [RE-FALLBACK]. |
| **Severity / effort** | MAJOR / S-M |
| **Fix** | `pres_audio_events.inc on_world_events` (sounds); draw the bomb icon in pres_game for `bomb_frame >= 0`. |

#### W6-04-12: The Nuclear Reactor's death blast is twice as strong and leaves no fallout (NEW; part D)

| Field | Details |
|---|---|
| **Player** | A destroyed Nuclear Reactor wrecks the owner's own adjacent buildings at double strength, and no radiation is left behind. |
| **Evidence** | <ul><li>`D-defences2.play` L160: an adjacent NAPOWR went 750 → 488; the original would leave about 619.</li><li>L161: `rad_total` stayed 0.</li><li>`main.cpp:1240` never reads `DeathWeaponDamageModifier`.</li><li>`world_abilities.cpp:516-519` uses the raw damage and makes no rad site.</li></ul> |
| **Original** | <ul><li>FireDeathWeapon FUN_0070CAC0 @0x70CB1B: `Damage × DeathWeaponDamageModifier` (type+0xD1C), then a real bullet detonates (FUN_004690A0).</li><li>That bullet is NukePayload: RadLevel 500 plus NUKEANIM.</li></ul> |
| **Severity / effort** | MAJOR / S |
| **Fix** | Read the modifier into `BldTypeW5`. In the death branch, scale the damage and call `rad_.detonate` when `rad_level > 0`. Also route the blast through W6-04-03's fix. |

#### W6-04-13: Base defences never attack enemy structures (NEW, needs RE; part D)

| Field | Details |
|---|---|
| **Player** | <ul><li>French Grand Cannon "creep" is impossible.</li><li>Prism Towers and Tesla Coils ignore enemy buildings, including garrisoned civilian buildings.</li></ul> |
| **Evidence** | <ul><li>`building_combat.cpp:189-215` `engageable` scans `units_` only.</li><li>defenses.md §5: "Defenses do not target buildings".</li></ul> |
| **Original** | [RE-FALLBACK: community knowledge] `Greatest_Threat` / `Target_Something_Nearby` for buildings has not been decompiled (defenses.md §3). |
| **Severity / effort** | MAJOR / M |
| **Fix** | RE the building threat scan first. Then add enemy buildings to the `step_building_combat` acquisition (distance to centre + (w+h)·64, combat.md §9). |

#### W6-04-14: The Battle Fortress cannot crush vehicles (`OmniCrusher`) (NEW; part A)

| Field | Details |
|---|---|
| **Player** | The Battle Fortress drives around enemy tanks instead of over them. |
| **Evidence** | <ul><li>`git grep OmniCrusher engine` finds 0 hits.</li><li>`weapon_resolve.cpp:117` makes every vehicle uncrushable.</li><li>`OmniCrushResistant` (BFRT, MCVs, SMIN) is not read either.</li></ul> |
| **Original** | [RE-FALLBACK, rules semantics] An OmniCrusher crushes any non-OmniCrushResistant ground unit (MovementZone=CrusherAll). |
| **Severity / effort** | MAJOR / M |
| **Fix** | The crush tests in `world.cpp` (crush path, W5-B steering) and `weapon_resolve.cpp`. |

#### W6-04-15: Battle Fortress passengers fire their primary, not `OpenTransportWeapon` (NEW; part A)

| Field | Details |
|---|---|
| **Player** | A GI in a Battle Fortress fires the M60 (15) instead of Para (25), about 2.2× less DPS. |
| **Evidence** | <ul><li>`A-probe2.play` Q5: Conscript HP steps of −12, never −24.</li><li>`world_w5.cpp:220-260` uses `p.w_damage/w_range/w_rof`.</li><li>`OpenTransportWeapon` has 0 readers.</li></ul> |
| **Original** | [RE-FALLBACK, rules key semantics; abilities-w5.md §6] |
| **Severity / effort** | MAJOR / S |
| **Fix** | world_w5 OpenTopped pass: use the passenger's slot `OpenTransportWeapon` when it is ≥ 0. |

#### W6-04-16: The IFV's `AirRangeBonus=4` is not applied (NEW; part A)

| Field | Details |
|---|---|
| **Player** | The IFV, the Allied mobile AA, engages aircraft at 6 cells instead of 10. |
| **Evidence** | `git grep AirRangeBonus engine` finds 0 hits; `[FV] AirRangeBonus=4`. |
| **Original** | [RE-FALLBACK] TechnoType AirRangeBonus is added to the range against airborne targets. |
| **Severity / effort** | MAJOR / S |
| **Fix** | Read it in `resolve_combat_unit` and add it in `weapon_in_range` when the target is airborne. |

#### W6-04-17: Siege Chopper: unbuildable, and its deploy-to-artillery mode does not exist (STILL OPEN R-201 + NEW deploy; parts B, C)

| Field | Details |
|---|---|
| **Player** | <ul><li>There is no SCHP cameo: `B-soviet.play` L45 `cameo(SCHP)=0` with NAWEAP and NATECH up.</li><li>A cheat-spawned SCHP ignores `d`: it stays at altitude 500 with BlackHawkCannon and never fires its 160mm (Range 12).</li></ul> |
| **Evidence** | <ul><li>`C-air.play` A5: `sel_altitude=500`, `sel_damage=35`, `weapon_range(SCHP)=6`; screenshot `04-evidence/C-schp-no-deploy.jpg`.</li><li>`IsSimpleDeployer` and `DeployToLand` have 0 readers.</li><li>`weapon_resolve.cpp:100` binds DeployFire only with `Deployer=yes`.</li></ul> |
| **Original** | [RE-FALLBACK, rules semantics] Land, SCHPDEPL, image SCHD, fire the Secondary from the ground, lift off on undeploy. |
| **Severity / effort** | MAJOR (YR Soviet unit) / M |
| **Fix** | <ul><li>Art: re-run the R-201 census. GAWEAT, listed as "art absent", now has a cameo, so the census is stale.</li><li>Deploy: in the `world_abilities.cpp` deploy dispatch, a simple-deployer jumpjet lands, swaps to UnloadingClass and activates slot 1.</li></ul> |

#### W6-04-18: The Robot Control Center is missing from the building roster and `PoweredUnit` is not modelled, so the Robot Tank cannot be built (NEW; parts A, D)

| Field | Details |
|---|---|
| **Player** | <ul><li>No GAROBO cameo, and so no Robot Tank, a YR Allied unit.</li><li>Robots never deactivate when the RCC is lost or unpowered: a cheat-spawned ROBO moves without one.</li></ul> |
| **Evidence** | <ul><li>`D-defences.play` L39 `cameo(GAROBO)=0`.</li><li>GAROBO is absent from `kRoster` (`main.cpp:2305-2338`), and the "art absent" comment at `main.cpp:2068` is unverified.</li><li>`A-probe2.play` Q4: the ROBO moved 32 → 38.</li><li>`PoweredUnit` / `PowersUnit` have 0 readers.</li></ul> |
| **Original** | `[ROBO] Prerequisite=GAWEAP,GAROBO PoweredUnit=yes`, `[GAROBO] PowersUnit=ROBO`. Deactivation is [RE-FALLBACK]. |
| **Severity / effort** | MAJOR / M |
| **Fix** | Append GAROBO to `kRoster` (append-only) and check its art. Add a per-house "powered PowersUnit building alive" freeze in `World::step`. |

#### W6-04-19: The YR Spy Plane (Soviet Radar Tower) is missing (NEW; part D; **scope decision**)

| Field | Details |
|---|---|
| **Player** | A Soviet player with a Radar Tower has no Spy Plane button: `D-superweapons.play` L54 `sw_count(0)=0`. |
| **Evidence** | <ul><li>`rp_supported_sw_type` admits the seven classic types and implemented Yuri powers, but not SpyPlane.</li><li>`main.cpp` does not register NARADR for the AI SW list.</li></ul> |
| **Original** | YR `[NARADR] SuperWeapon=SpyPlaneSpecial` (RechargeTime 4), Type index 8 (superweapon.md §4). |
| **Severity / effort** | MAJOR under U-1 (the engine plays YR rules) / M |
| **Fix** | If YR Spy Plane support is in scope: add `SpyPlane` and a `fire_superweapon_at` branch (an SPYP fly-over that reveals along its path; RE the reveal radius). |

#### W6-04-20: The YR Force Shield (both Battle Labs) is missing (NEW; part D; **scope decision**)

| Field | Details |
|---|---|
| **Player** | A Battle Lab grants no Force Shield: `D-defences.play` L51 `sw_count(0)=1` (AmerParaDrop only). |
| **Evidence** | The same filter as W6-04-19; `world_sw.cpp:260` `default: return false`. |
| **Original** | YR `[GATECH]/[NATECH] SuperWeapon=ForceShieldSpecial`: RechargeTime 5, `ForceShieldRadius=4`, `ForceShieldDuration=500`, then `ForceShieldBlackoutDuration=1000`. |
| **Severity / effort** | MAJOR under U-1 / M |
| **Fix** | Reuse the Iron Curtain building flag inside radius 4, plus the spy-blackout path. RE `ObjectClass::IronCurtain(..., ForceShield)` to see whether units are covered. Same decision as W6-04-19. |

#### W6-04-21: The Battle Bunker (NABNKR) and Industrial Plant (NAINDP) are still unbuildable (STILL OPEN, R-201 / R-066; part D)

| Field | Details |
|---|---|
| **Player** | Soviets lose their YR garrison bunker (also in the AI's `SovietBaseDefenses`) and the 25% vehicle discount. |
| **Evidence** | `D-superweapons.play` L43/L44: `cameo=0` for both. |
| **Severity / effort** | MAJOR in play; DATA-LIMIT per R-201 until the art census is re-run (see W6-04-17). |

### MINOR

#### W6-04-22: Base defences use 2-D range with no MinimumRange and no elevation bonus (STILL OPEN, residue of R-063, which skipped `building_combat.cpp`; part D)

| Field | Details |
|---|---|
| **Player** | <ul><li>A Conscript hugging a Grand Cannon dies; the original cannot fire inside `MinimumRange=3`.</li><li>Hill defences get no elevation bonus.</li><li>AA defences measure aircraft in 2-D, which gives the Patriot about 12 cells horizontal reach instead of about 11.6 (Kirov) or about 10.5 (Harrier).</li></ul> |
| **Evidence** | <ul><li>`D-defences2.play` L56-76.</li><li>`building_combat.cpp:184-188` uses `std::hypot`.</li></ul> |
| **Original** | FUN_006F6750 (3-D distance, MinimumRange +0xB8) plus the FUN_006F6490 elevation bonus (combat.md §9). |
| **Severity / effort** | MINOR (MAJOR for France) / S |
| **Fix** | `step_building_combat`: call `weapon_in_range_from` with the building centre and z. |

#### W6-04-23: The Crazy Ivan cannot bomb own or allied objects, and Ctrl force-fire refuses own objects (NEW; part B)

| Field | Details |
|---|---|
| **Player** | "Bomb your own tank and drive it into the base" is impossible. |
| **Evidence** | <ul><li>`B-soviet.play` T3b: `bombs=0` after a plain click and after a Ctrl click.</li><li>`input_original.cpp:134-136`: Ctrl attack requires `!controllable(house)`.</li><li>`input_original.cpp:155-170`: an own unit resolves to Select.</li></ul> |
| **Original** | `[IVAN] AttackCursorOnFriendlies=yes`, and Ctrl force-fire on own units is standard. The cursor path was not traced [RE-FALLBACK]. |
| **Severity / effort** | MINOR / S |
| **Fix** | `Input::classify` plus the `order_attack` / `valid_target` acceptance of explicit friendly targets. |

#### W6-04-24: The Desolator rad beam (`IsRadBeam`) and Boris's Flare line (`IsLine`) are not drawn (NEW; part B)

| Field | Details |
|---|---|
| **Evidence** | `pres_fx.inc:170 fill_beam` reads only IsElectricBolt and IsLaser. |
| **Severity / effort** | MINOR / S |
| **Fix** | Add beam kinds in `fill_beam` and the beam drawer. The colour and duration are [RE-FALLBACK]. |

#### W6-04-25: Cloaked naval units surface on every shot; `DecloakToFire=no` is ignored (NEW, PROBABLE; part C)

| Field | Details |
|---|---|
| **Player** | A Typhoon, Dolphin or Squid pops up for about 18 ticks after each shot. |
| **Evidence** | <ul><li>`C-naval3` S2: `submerged(0)` 2 → 1 at f=460.</li><li>`world_an.cpp:131` re-arms `surface_left` on every `fire_flash`.</li></ul> |
| **Original** | `DecloakToFire` (WeaponType+0x133) is read only by GetFireError FUN_006FB5D0 @0x6FBF4A. Whether `Fire` itself uncloaks was not traced. |
| **Severity / effort** | MINOR / S |
| **Fix** | Parse it per slot and gate `world_an.cpp:131` on it. |

#### W6-04-26: Spawned aircraft that dock back on their carrier count as units lost (NEW; part C)

| Field | Details |
|---|---|
| **Evidence** | <ul><li>`C-naval1`: `units_lost(0)` 2 → 13 with no real losses.</li><li>`world_an.cpp:543-551` docks a spawn without `deployed_away`.</li><li>`gf_scene.inc:176-183` counts it as lost.</li></ul> |
| **Severity / effort** | MINOR (the score screen is wrong) / S |
| **Fix** | Set `deployed_away = true` on dock. |

#### W6-04-27: Unit and building sounds that never play (NEW; complements the FIXED R-130; parts A, C, D)

| Field | Details |
|---|---|
| **Missing sounds** | <ul><li>`ChronoInSound`/`ChronoOutSound` for the Chrono Legionnaire and Chrono Commando.</li><li>`EnterWaterSound`/`LeaveWaterSound` for Tanya and SEALs.</li><li>`CreateSound` (TanyaPrimeCreated, ChronoCommandoCreated, KirovCreated).</li><li>Robot Tank `ActivateSound`/`DeactivateSound`.</li><li>Per-type `EnterTransportSound` (IFV IFVTransform, BFRT).</li><li>Aircraft `MoveSound` loops (Kirov, Rocketeer, Harrier).</li><li>`AuxSound1/2` takeoff and landing.</li><li>`ImpactLandSound` (crash impact).</li><li>Powered defences' `WorkingSound`/`NotWorkingSound`.</li><li>`HasRadialIndicator` (Gap Generator circle).</li></ul> |
| **Evidence** | Each key has 0 readers (`git grep`). `pres_audio_events.inc:140` handles only the Deploy ability event. |
| **Severity / effort** | MINOR / M |
| **Fix** | `pres_audio_events.inc`: create, teleport, water, flight-state and crash-impact edges, and the `def_offline` edge. A radial draw in the selection overlay. |

#### W6-04-28: An IFV with a passenger ignores veterancy (NEW; part A)

| Field | Details |
|---|---|
| **Player** | A veteran or elite IFV with a passenger does base damage and ROF, and never uses `EliteWeapon<k>`. |
| **Evidence** | <ul><li>`world_abilities.cpp:296-303` copies the raw damage.</li><li>`world_veterancy.cpp:109-112`.</li><li>An empty elite IFV is correct (88/30).</li></ul> |
| **Severity / effort** | MINOR / S |
| **Fix** | Route the swap through `fire_damage` / `rof_frames`, and build an elite IFV table. |

#### W6-04-29: `DeployedCrushable=no` (Guardian GI) is not read (NEW; part A)

A deployed Guardian GI can be run over.

| Field | Details |
|---|---|
| **Severity / effort** | MINOR / S |
| **Fix** | The `world.cpp` crush test. |

#### W6-04-30: Mirage `CanApproachTarget=no` is not read (NEW; part A)

The Mirage chases targets instead of holding.

| Field | Details |
|---|---|
| **Severity / effort** | MINOR / S |
| **Fix** | The `combat.cpp:1109` approach branch. |

#### W6-04-31: Mirage elite-weapon quirk and disguise delay are unverified (NEW, RE; part A)

- `[MGTK] EliteSecondary=MirageGunE` with no Secondary: the engine never uses it (`weapon_unit.cpp:31-36`).
- The 15-frame "still" delay (`world.cpp:2271`) is [RE-FALLBACK].

| Field | Details |
|---|---|
| **Severity / effort** | MINOR / S, after RE of FUN_007456F0 and SelectWeapon |

#### W6-04-32: A SEAL against a vehicle on land uses the MP5 (1%) while Tanya plants C4 (STILL OPEN, R-191 SelectWeapon RE-FALLBACK; part A)

| Field | Details |
|---|---|
| **Evidence** | `A-probe2.play` Q1/Q2; `choose_weapon` prefers a primary with Verses > 0. |
| **Severity / effort** | MINOR / S, after RE |

#### W6-04-33: C4 on ships is an instant 2500-damage hit with no plant event, and the `ship_c4` probe is dead (NEW, low; part A)

| Field | Details |
|---|---|
| **Evidence** | `abil_w5.hpp:147`; abilities.md:313 lists it as cut. The outcome is right. |
| **Severity / effort** | MINOR / S |

#### W6-04-34: The Chrono Miner teleports back to the ore field after unloading (NEW, RE needed; part A)

| Field | Details |
|---|---|
| **Evidence** | `harvest.cpp:616-638`; economy.md §3 flags it as [RE-FALLBACK]. |
| **Original** | Common knowledge: it drives to the ore and teleports only home. Confirm in FUN_0073C560. |
| **Severity / effort** | MINOR (Allied income is too high) / S |

#### W6-04-35: Harness: `click unit` on an airborne unit clicks its shadow (NEW, harness only; part C)

| Field | Details |
|---|---|
| **Effect** | `scripts/play/air.play` §3 "Rocketeer obeys Stop" (W2-AN-07) passes vacuously. Players are not affected (`Input::unit_under` raises the hit box). |
| **Evidence** | `game_scene_play.inc:49-52` |
| **Severity / effort** | MINOR / S |
| **Fix** | Subtract `altitude_to_screen_dy` for aircraft in the locator. |

#### W6-04-36: Fliers' speed uses the ground `GameSpeedBias` 1.6 (STILL OPEN, W2-AN-26 / R-090; part C)

| Field | Details |
|---|---|
| **Measured** | Kirov 1.125 cells/s, against about 0.70 derived in aircraft.md §5. R-082 found no reader of the house speed handicap. |
| **Severity / effort** | MINOR / S, after tracing `GetMaxSpeed` (vtbl +0x538) |

#### W6-04-37: Kirov crash damage is still guessed (STILL OPEN, R-090; part C)

| Field | Details |
|---|---|
| **Measured** | The own War Factory went 1000 → 541, from `max(10, Strength/4)` (`world_an.cpp:625-633`). |
| **Severity / effort** | MINOR / S, RE |

### UNCONFIRMED

#### W6-04-38: A dog sent at a GI about 10 cells away stalled out of bite range (part A; probable test artifact)

- In `A-dog.play` D2 an Allied dog stopped 2.24 cells from a GI (bite range 1.5) and was shot down.
- Agent B reproduced a similar stall on the same map (`hillbtwn`). It traced it to a GI spawned **inside a fenced yard**, unreachable on foot (`04-evidence/B-dog-fenced.jpg`).
- In open ground, dogs killed GIs in 19-26 frames in 3 geometries.
- The A06 GI was spawned at a similar offset from the ConYard, so treat this as not a gap unless a run on open ground reproduces it.

## 2. Observations not filed

- **Patriot vs Kirov:** 70-73 per hit, against the expected 75 (SAMWH 100% vs medium). The cause is unattributed; possibly a country armor multiplier in `vet_receive`. This belongs to the combat-core owner.
- **Night Hawk:** only 2 of 5-6 selected GIs boarded in both runs. Terrain near the spawn point is suspected.
- **Carrier:** only one Hornet is ever airborne at a time. Whether the original launches all three at once was not RE'd.
- **Sea Scorpion:** kept its Destroyer target during a single Harrier pass. The pass was too short to judge.
- **AI Typhoon:** never attacked a Destroyer 6.7 cells away (its Sight is 4). Sight-gated acquisition is consistent with the original.
- **Not measured:**
  - Guardian GI vs aircraft (cheat-spawned enemy aircraft sit on the ground);
  - Dreadnought MinimumRange 8;
  - Squid Culling at 25%;
  - map wall overlays (W6-04-05).

## 3. Verified working in play on e2b569e

- **Allied infantry:**
  - GI deploy: Para 25 / range 5 / ROF 15, kills 3.5× faster.
  - Guardian GI missile: 40 vs a Rhino.
  - Sniper: range 14, ROF 150, elite ROF 36; one-shots a GI at 7 cells. British only.
  - Dog: one bite on reachable infantry; refuses tanks.
  - Chrono Legionnaire: erases a GI in 160 frames (spec 157); the teleport move works.
  - Tanya: swims (`04-evidence/A-tanya-swim.jpg`), and C4s a Rhino on land as in YR.
  - SEAL: C4s an adjacent Sea Scorpion.
  - Own and enemy Mirage disguise.
- **Allied vehicles:**
  - IFV: all seven passenger modes resolve the correct weapon; an elite empty IFV gets 88 / ROF 30.
  - Tank Destroyer: kills a Rhino, 2% vs infantry; German only.
  - Battle Fortress: passengers fire out.
- **Soviet infantry and vehicles:**
  - Country gates: Iraq gets the Desolator; Russia, Libya and Cuba specials are correctly absent for other countries.
  - Tesla Trooper: 50 per bolt.
  - Ivan timer: 452 frames (451 expected).
  - Terror Drone: infection works; a GI dies in 12 frames.
  - Desolator: deploy kills; the field decays about 20% in 450 frames.
  - Apocalypse SelfHealing: +1 HP per 14 frames.
  - Tesla Tank: kills a Grizzly in 3 bolts.
  - **Boris airstrike:** 2 MiGs destroy a Radar.
  - Demo Truck: suicide plus radiation.
  - SMCV: deploys.
- **Air:**
  - Korea: Black Eagle present, no Harrier.
  - **Harrier / Black Eagle:** strike, RTB, dock, rearm in 272 ticks (ReloadRate 270).
  - **Rocketeer:** kills Conscripts.
  - **Kirov:** bombs every 52 ticks; per-cell building splash; falls when killed; self-heals.
- **Naval:**
  - **Destroyer + Osprey:** sonar finds a submerged Typhoon at once; the Ospreys kill it.
  - **Carrier:** Hornets hit a land building.
  - **Dreadnought:** a 2-missile volley hits a Radar.
  - **Typhoon:** torpedoes every 120 ticks.
  - **Giant Squid:** grabs ships (R-049 grab no longer reproduces).
  - **Aegis:** kills a Kirov once it is facing it.
- **Structures:**
  - Sidebar census for both sides is correct, including GTGCAN France-only and GAAIRC/AMRADR for America.
  - Nuclear Reactor: +2000 power.
  - Sentry Gun: damage, ProneDamage and ROF match.
  - Patriot: kills a Kirov.
  - Wall: a Grizzly levels a wall.
- **Superweapons:**
  - **Iron Curtain:** charge about 4480 (4500); kills infantry, protects vehicles and buildings; lasts 752 (750).
  - **Chronosphere:** charge about 6284 (6300); a tank warped into water dies.
  - **Nuke:** charge 8988 (9000); GAPOWR left at exactly 390; fallout.
  - **Lightning Storm:** first bolt at about 256 (250); ends at about 432 (430).
  - **AmerParaDrop:** granted.

## 4. Prioritized fix list (one fix agent)

Ordered by player impact per effort. Every item is data/code work with the RE already cited, except where marked. Suggested order and grouping, so the diffs stay local:

| # | IDs | Files | Effort | Why first |
|---|---|---|---|---|
| 1 | W6-04-01 Burst | `combat.cpp`, `unit_combat.hpp`, `weapon_resolve.cpp`, `building_combat.cpp`, `world_an.cpp` | M | Halves the DPS of a dozen core units; RE complete |
| 2 | W6-04-02 shrapnel section | `main.cpp` `resolve_combat_unit` | S | One-line class of bug; Prism Tank and elite Tesla |
| 3 | W6-04-06 OmniFire for all units | `main.cpp` / `weapon_resolve.cpp` | S | Aegis/Carrier/Dreadnought are useless without it |
| 4 | W6-04-03 + W6-04-12 ability blasts and reactor death through `damage_area`; DeathWeaponDamageModifier + rad | `world_abilities.cpp`, `main.cpp` | S-M | Soviet base-busting; reactor self-destruction |
| 5 | W6-04-08 transport Size sum | `cargo.cpp`, `docs/re/transport-cargo.md` | S | RE done this wave |
| 6 | W6-04-07 amphibious attack approach | `world.cpp` approach dispatch | S | Naval C4, SEAL missions |
| 7 | W6-04-09 Sonic / heal parasite eject | warhead profile, unit damage paths | S | Dolphin's purpose; RE done |
| 8 | W6-04-10 Terror Drone SHP vehicle | `main.cpp` roster loader | S | Missing basic Soviet unit |
| 9 | W6-04-15, W6-04-16, W6-04-29, W6-04-30 small rules keys | `world_w5.cpp`, `main.cpp`, `combat.cpp`, `world.cpp` | S each | Cheap fidelity wins |
| 10 | W6-04-04 + W6-04-05 wall damage model | `combat.cpp`, `world_sw.cpp`, `world_abilities.cpp`, overlay grid | M | P4-5 and the built-wall bug in one model |
| 11 | W6-04-22 defence range via `weapon_in_range_from` | `building_combat.cpp` | S | Grand Cannon dead zone |
| 12 | W6-04-11 bomb icon + sounds, W6-04-24 beams | `pres_audio_events.inc`, `pres_game`, `pres_fx.inc` | S-M | Visibility of Soviet specials |
| 13 | W6-04-14 OmniCrusher | crush path | M | YR Battle Fortress identity |
| 14 | W6-04-25, W6-04-26, W6-04-28, W6-04-35 | `world_an.cpp`, `world_abilities.cpp`, `game_scene_play.inc` | S each | Correctness and score-screen polish |

**Deferred: needs a decision or RE first, not for this agent:**
- **W6-04-19 / 20 Spy Plane, Force Shield:** owner decision. W2-AN-19 "classic SWs" conflicts with U-1 "YR rules".
- **W6-04-17 / 18 / 21:** re-run the R-201 art census first (the GAWEAT "absent" note is stale), then implement SCHP deploy, GAROBO + PoweredUnit and NABNKR.
- **W6-04-13 defences vs structures:** RE `Greatest_Threat` first.
- **W6-04-27 sound batch:** an audio agent.
- **W6-04-31 / 32 / 34 / 36 / 37:** go to the R-090 / R-191 RE-calibration batch.

**Doc updates for the REMAINING owner:**
- R-049: the grab now works; the remaining half is W6-04-09.
- R-063: building path not done (W6-04-22).
- P4-5: becomes W6-04-05.
- `transport-cargo.md` §1 is wrong (W6-04-08).
- `air.play` W2-AN-07 passes vacuously (W6-04-35).

## 5. Fidelity matrix (all units, structures and superweapons)

The Part column gives the appendix with the details: A = Allied ground, B = Soviet ground, C = air and naval, D = structures, walls and superweapons. Structures have no Move or Vet column, so those cells are N/A.

| Part | Unit | Move | Weapon(s)+ROF | Range (3-D/min) | Warhead Verses/splash | Projectile | Special/deploy | Vet/elite | Garrison/passengers | Sight/cloak/detect | Sounds |
|---|---|---|---|---|---|---|---|---|---|---|---|
| A | E1 GI | OK? Speed 4, foot, Crushable | OK. M60 15/20; deployed Para 25/15 measured | OK. 4, deployed 5 measured | OK? SA/SSA | OK? InvisibleLow | OK. Deploy measured: kills a Conscript in 88 frames deployed vs 304 undeployed. The deploy itself is instant, with no deploy-sequence delay | OK? M60E/ParaE | OK? Occupier UCPara/UCElitePara (R-053 open) | OK? Sight 5 | OK? Deploy/Undeploy sound (R-130) |
| A | GGI Guardian GI | OK? Speed 3 | OK. Deployed MissileLauncher 40/40 measured vs a Rhino (−40 per hit) | OK. 8 / min 1 | OK. GUARDWH 100% vs heavy | OK? AAHeatSeeker2 AA+AG; undeployed M60 has no AA | WRONG: DeployedCrushable=no not read (W6-04-29) | OK? MissileLauncherE | N/A (Occupier=no) | OK? 6 | OK? |
| A | ENGINEER | OK? | N/A (DefuseKit, R-046) | N/A | N/A | N/A | OK? Capture, defuse and IFV repair (R-048) | N/A (Trainable=no) | IFVMode 1 → RepairBullet (R-048 play) | OK? 4 | OK? |
| A | ADOG Allied dog | OK? Speed 8 | OK. Bite kills a Conscript at 3 cells; refuses a Rhino (0%) | WRONG: pursuit stalls at 2.2 cells (W6-04-38) | OK? ParasiteDog 100% vs inf, 0% vs vehicles | MISSING: jump not drawn (R-054) | OK? DetectDisguise (R-148 radius guessed) | OK? | IFVMode 0 → HoverMissile (OK) | OK? 9, DetectDisguise | OK? |
| A | SPY | OK? | OK? MakeupKit disguise (W2-SK-22 play) | N/A | N/A | N/A | OK? Infiltrate dispatch per abilities-w5 §1 (R-042) | N/A | IFVMode 2 → CRM60 | OK? PermaDisguise | OK? |
| A | TANY Tanya | OK. Swims (amphibious, measured); Crushable=no | OK. Pistols 125/5; C4 Sapper vs Rhino on land measured (kill in ~68 frames) | OK. 6 / C4 1.5 | OK? HollowPoint2 | OK? | WRONG: never reaches a ship to plant C4 (W6-04-07) | OK? DoublePistolsE; SelfHealing | IFVMode 4 → CRMP5 125/7/10 (OK) | OK? 8 | MISSING: EnterWater/LeaveWater/Create sounds (W6-04-27) |
| A | SNIPE Sniper (UK) | OK? Speed 4 | OK. AWP one-shots a GI at 7 cells (auto-acquired, GI dead within 2 driver frames); ROF 150 | OK. 14 measured | OK? HollowPoint 200% vs none | OK? | N/A | OK. Elite ROF 36 = AWPE 60 × VeteranROF 0.6 | N/A (not an Occupier in rulesmd); IFV → AWPE 125/14/60 (OK) | OK? 8 (< range, needs a spotter) | OK? |
| A | GHOST Navy SEAL | OK. Swims (amphibious code path) | OK? MP5 125/10 | OK. 6 / C4 1.5 | OK? | OK? | PARTIAL: C4'd a Sea Scorpion in one hit when it came adjacent, but cannot path onto water to reach a Destroyer (W6-04-07); vs a Rhino on land it uses MP5 at 1% (W6-04-32) | OK? MP5E | IFVMode 4 → CRMP5 | OK? 8 | MISSING: EnterWater/LeaveWater (W6-04-27) |
| A | CLEG Chrono Legionnaire | OK. Teleport move measured | OK. NeutronRifle erase: GI in 160 frames (spec 1250/8 = 157) | OK. 5 | OK? Temporal | N/A | OK. Teleport plus erase | OK? NeutronRifleE 16 | IFVMode 10 → CRNeutronRifle 5/6/120 (OK) | OK? 8 | MISSING: ChronoIn/OutSound (W6-04-27) |
| A | CCOMAND Chrono Commando | OK? Teleporter (bound via its IFVMode) | OK? ChronoMP5; FakeC4 as a normal 5000 hit | OK? | OK? | OK? | OK? Stolen-tech gate (R-042) | OK? | IFVMode 4 | OK? | MISSING: Create/ChronoIn/Out (W6-04-27) |
| A | MTNK Grizzly | OK? Speed 7, Crusher | OK? 105mm 65/60 | OK? 5 | OK? AP .3/.5 | OK? Cannon | N/A | WRONG: elite 105mmE Burst=2 fires once (W6-04-01) | N/A | OK? 8 | OK? |
| A | FV IFV | OK? Speed 10, Crusher=no | WRONG: HoverMissile Burst 2 (elite 4) fires once (W6-04-01) | WRONG: AirRangeBonus=4 missing (W6-04-16) | OK? HE .5/.5 | OK? | OK. Modes measured: empty 25/6/50, GI CRM60 25/6/15, GGI 30/8/40, Sniper 125/14/60, Tanya 125/7/10, CLEG 5/6/120, dog 25/6/50 | OK. Elite empty 88/ROF 30. Passenger modes ignore FIREPOWER (W6-04-28) | OK. Passengers=1 | OK? 8 | WRONG: IFVTransform per-type sound (W6-04-27) |
| A | SREF Prism Tank | OK? Speed 4, Crusher | WRONG: Comet hits the target only; no fragments (W6-04-02) | OK. 10 | OK? CometWH | WRONG: ShrapnelWeapon is on the projectile (W6-04-02) | N/A | WRONG: SuperCometFragment unused (W6-04-02) | N/A | OK? 8 | OK? |
| A | MGTK Mirage | OK? Speed 7 | OK? MirageGun 100/70 | OK? 7 | OK? | OK? | OK. Disguises when still, own and enemy measured. Enemy tree draw not visible in the shot (shroud): R-047 not re-measured. CanApproachTarget missing (W6-04-30) | ? MirageGunE (EliteSecondary quirk, W6-04-31) | N/A | OK? 9 | OK? |
| A | TNKD Tank Destroyer (Germany) | OK? Speed 5, Turret=no | OK. SABOT kills a Rhino; Conscript takes 3 per shot (150 × 2%) | OK. 5 | OK. UltraAP 2% inf / 100% heavy | OK? | N/A | WRONG: SABOTE Burst=2 (W6-04-01) | N/A | OK? 8 | OK? |
| A | BFRT Battle Fortress | WRONG: OmniCrusher missing (W6-04-14) | OK? 20mmRapid 30/20 | OK? 5.5 | OK? HARVWH | OK? | WRONG: passengers fire primary, not OpenTransportWeapon (W6-04-15) | OK? | OK. 5 passengers fire out (passenger_shots=3), +2 range, ×1.2 damage | OK? 6 | WRONG: EnterTransport per-type (W6-04-27) |
| A | ROBO Robot Tank | OK? Hover amphibious (code) | OK? Robogun | OK? | OK? | OK? | WRONG: PoweredUnit not modelled; it moves with no Control Center (W6-04-18). Not buildable on this install (GAROBO art absent) | OK? | N/A | OK? 6 | MISSING: Activate/Deactivate (W6-04-27) |
| A | CMIN Chrono Miner | OK? Drives (the teleporter profile is not bound for CMIN) | N/A | N/A | N/A | N/A | WRONG?: warps back to the field after unloading (W6-04-34) | N/A | N/A | OK? 4 | OK? ChronoMinerTeleport |
| A | AMCV | OK? | N/A | N/A | N/A | N/A | OK? Deploys to GACNST (gF) | N/A | N/A | OK? 6 | OK? PlaceBuilding |
| B | E2 Conscript | OK? Speed 4 Walk, crushable | OK? M1Carbine 15/25 | OK? 4 (R-063 3-D) | OK? SA | OK? InvisibleLow | N/A | OK? elite M1CarbineE (range 5) | OK? Occupier, UCM1Carbine | OK? 5 | OK? (R-130) |
| B | SENGINEER Soviet Engineer | OK? Speed 4 | OK? DefuseKit (BombDisarm) | OK? 1.5 | OK? | OK? | OK? capture/repair/defuse (R-046 FIXED; image ENGINEER drives the gestures) | N/A Trainable=no | N/A | OK? 4 | OK? |
| B | DOG Attack Dog | OK Speed 8 | OK BadTeeth ParasiteDog: GI killed 19-26 frames after the click in 3 geometries | OK 1.5 | OK infantry only (vehicle Verses 0%) | MISSING leap not drawn (R-054 STILL OPEN) | OK DetectDisguise | OK? | N/A | OK? Sight 9 | OK? |
| B | FLAKT Flak Trooper | OK? Speed 4 | OK? FlakGuyGun AG + FlakGuyAAGun AA (choose_weapon) | OK? 5 / AA 8 | OK? FlakTWH 1-cell, FlakGuyWH | OK? FlakProj scatter (R-057 PARTIAL) | N/A | WRONG elite Burst=2 not fired (W6-04-01) | N/A | OK? | OK? |
| B | SHK Tesla Trooper | OK? Speed 4, Crushable=no honoured (weapon_resolve.cpp:125) | OK ElectricBolt 50 per shot, Grizzly 250→200 | OK 3 | OK Shock heavy 100% | OK? bolt drawn (IsElectricBolt) | OK? coil charge (R-045 FIXED) | WRONG elite bounce missing (W6-04-02) | N/A | OK? 6 | OK? |
| B | IVAN Crazy Ivan | OK? Speed 4 | OK bomb plant; blast 452 frames after the click (timer 451, §E) | OK 1.5 | WRONG building blast far too weak (W6-04-03) | N/A | MISSING bomb on own/allied objects (W6-04-23); defuse OK? (R-046) | OK? IvanDamage is used, not the elite Damage (§E) | N/A | OK? 6 | MISSING attach/tick sounds, bomb icon (W6-04-11) |
| B | CIVAN Chrono Ivan (stolen tech) | OK? Teleport loco (§C1) | OK? IvanBomber | OK? | WRONG as IVAN (W6-04-03) | N/A | OK? stolen-tech gate (R-042 FIXED) | OK? | N/A | OK? 8 | as IVAN (W6-04-11) |
| B | BORIS Boris | OK? Speed 5, Crushable=no | OK? AKM 65/20; Flare designates buildings | OK? 7 / Flare 12 | OK? BORISWH | MISSING Flare `IsLine` laser not drawn (W6-04-24) | OK airstrike: called at once from 10 cells, 2 BPLN, enemy Radar 1000→0 | WRONG MiG `Maverick3 Burst=2` fires 1 (W6-04-01) | N/A | OK? 9 | OK? |
| B | TERROR Terrorist (Cuba) | OK? Speed 6 | OK? Suicide TerrorBomb | OK? 1.5 | WRONG building blast (W6-04-03) | N/A | OK? death weapon (§F) | N/A | N/A | OK? 9 | OK? |
| B | DESO Desolator (Iraq) | OK? Speed 4, Crushable=no | OK? RadBeam 125/50 | OK? 6 (elite 8) | OK? RadBeamWarhead | MISSING `IsRadBeam` beam not drawn (W6-04-24) | OK deploy kills the adjacent GIs; after undeploy the field decays 261198→208959 in 450 frames (accumulating site, §A1-A3); glow drawn | OK? SelfHealing | N/A | OK? 6 | OK? DeploySound |
| B | HTNK Rhino | OK? Speed 6, Crusher | OK? 120mm 90/65 | OK? 5.75 | OK? AP .3 | OK? Cannon (R-PJ fixed) | N/A | WRONG elite 120mmE Burst=2 (W6-04-01) | N/A | OK? 8 | OK? |
| B | HTK Flak Track | OK? Speed 8, Crusher | OK? FlakTrackGun + AA gun | OK? 5 / AA 10 | OK? | OK? | N/A | WRONG elite Burst=2 (W6-04-01) | OK? 5 passengers, SizeLimit 2 (not OpenTopped in YR, so they cannot fire out) | OK? 8 | OK? |
| B | APOC Apocalypse | OK? Speed 4, Crusher | WRONG one 100-dmg shell per ROF instead of a 2-shell volley (W6-04-01) | OK? 5.75 / tusk 8 | OK? ApocAP | OK? | OK SelfHealing: +10 HP in 140 frames (500→510, +1/14 frames, V11) | WRONG 120mmxE Burst=4 (W6-04-01) | N/A | OK? 6 | OK? |
| B | V3 V3 Launcher | OK? Speed 4 | OK? spawner rocket 200 (elite 400) | OK? 18 / min 5 holds (combat.cpp:1068) | OK? V3WH | OK? RocketLoco (W4-PJ-04), interceptable (W4-PJ-10) | N/A | OK? V3EliteWarhead | N/A | OK? 7 | OK? |
| B | DTRUCK Demolition Truck (Libya) | OK? Speed 5 | OK suicide on arrival (71 frames from 5 cells) | OK 1 | WRONG Battle Lab only 500→291 (W6-04-03) | N/A | OK radiation left (rad_total 0→7572) | N/A | N/A | OK? 5 | OK? |
| B | TTNK Tesla Tank (Russia) | OK? Speed 6, Crusher | OK TankBolt kills a Grizzly in 3 bolts | OK? 4 (elite 6) | OK? Electric | OK? bolt | N/A | WRONG elite bounce (W6-04-02) | N/A | OK? 8 | OK? |
| B | DRON Terror Drone | MISSING not buildable (W6-04-10) | OK (cheat-spawned) Grizzly infected at once, 300 HP chewed, drone pops out; a GI dies in 12 frames | OK 1.83 | OK Parasite | MISSING leap (R-054) | OK? Service Depot kills the drone (world.cpp:2240) | N/A | N/A | OK? 4 | OK? |
| B | HARV War Miner | OK? Speed 4, Crusher | OK? 20mmRapid | OK? 5.5 | OK? HARVWH | OK? | OK? harvest (R-190 RE-FALLBACK) | OK? SelfHealing | N/A | OK? 4 | OK? |
| B | SMCV | OK? Speed 4 | N/A | N/A | N/A | N/A | OK? deploys into NACNST (play: real deploy each run) | N/A | N/A | OK? | OK? |
| B | SAPC Amphibious Transport | OK? Hover + Amphibious zone (R-001) | N/A | N/A | N/A | N/A | N/A | N/A | OK? 12 passengers, SizeLimit 6 (body-count capacity per transport-cargo.md §1, not re-verified) | OK? 6 | OK? |
| B | SCHP Siege Chopper | MISSING not buildable (R-201 STILL OPEN: voxel) | flight: agent C | – | – | – | MISSING deploy-to-artillery 160mm (W6-04-17) | WRONG 160mmE Burst=2 (W6-04-01) | N/A | – | – |
| B | YURI / PSIC | N/A: Yuri needs YABRCK; PSIC does not exist in rulesmd (the Psi-Corps Trooper is PTROOP, Allied stolen tech) | | | | | | | | | |
| C | JUMPJET Rocketeer | OK. Jumpjet, lifts off on exit; flight 7.1 cps by code (JumpjetSpeed 30, bias; W6-04-36) | OK. 20mm 25/30. First Conscript killed 41 frames after the order; all 3 killed | OK? 5 | OK? SSA | OK? Inviso AA+AG | OK? Stop clears the flight goal (C1 inconclusive: it had already arrived); falls on death (Crashable) | OK? 20mmE ROF 5 | N/A | OK? 8 | MISSING: RocketeerMoveLoop, ImpactLandSound (W6-04-27) |
| C | ORCA Harrier | OK. Fly, 3.28 cps; built onto an AMRADR pad | OK. Maverick 150. HYD 400→250 in one pass; fires at 6 cells, then RTB | OK? 6 (elite 9) | OK. ORCAAP 100% vs heavy | OK? homing DRAGON (W4-PJ-01 fixed) | OK. Ammo 1 → RTB → rearm at 270 ticks (BEAG run). Attack-run geometry RE-FALLBACK (R-090) | OK? MaverickE 300 | N/A | OK? 8 | MISSING: AuxSound1/2 takeoff and landing, MoveLoop (W6-04-27) |
| C | BEAG Black Eagle (Korea) | OK. Korea: `cameo(BEAG)=1`, `cameo(ORCA)=0` | OK. Maverick2 200: GATECH 500→300 in one pass | OK? 6 (elite 9) | OK. ORCAAP | OK? homing | OK. Order f=248 → hit f=268 → docked f=274 → ammo back f=342 (68 frames = 272 ticks) | OK? | N/A | OK? 8 | MISSING (W6-04-27) |
| C | SHAD Night Hawk | OK? Jumpjet, lands to load (W2-AN-08) | OK? BlackHawkCannon 35/40 OmniFire | OK? 6 | OK? SA | OK? Inviso | OK? lands, then loads | OK? | OK? Group boarding: 2 of 5-6 GIs boarded in both runs. The rest stood still 2-3 cells away (terrain is suspected; not confirmed, see §3) | MISSING: RadarInvisible=yes unread (minor) | MISSING (W6-04-27) |
| C | SCHP Siege Chopper (YR) | OK. Flies (2.8 cps) | WRONG: only BlackHawkCannon is ever used. Secondary 160mm needs deploy (W6-04-17) | OK? 6 | OK? SA | OK? | **MISSING:** `d` does nothing; it stays at altitude 500 and never becomes SCHD artillery (W6-04-17) | OK? | N/A | OK? 7 | MISSING (W6-04-27) |
| C | ZEP Kirov | OK. Jumpjet 1.125 cps measured. RE says ~0.70 (W6-04-36, W2-AN-26 open) | OK. BlimpBomb 250, ROF 50: bomb interval 13 frames = 52 ticks | OK. 1.5 | OK. BlimpHE CellSpread 2. Refinery 1000→231 by one bomb (per-cell building splash, combat.md §8), dead on the 2nd | OK? Vertical ZBOMB voxel | OK. Crashable: falls 750 lepts in 15 frames (JumpjetCrash 12). Crash on own GAWEAP 1000→541 (−459, Strength/4 RE-FALLBACK, R-090) | OK? SelfHealing seen (+3) | N/A | OK? 8 | MISSING: KirovCreated (CreateSound), KirovMoveLoop, KirovCrash ImpactLandSound (W6-04-27) |
| C | ASW Osprey (spawned) | OK. From the Destroyer; kills a submerged Typhoon 600 HP in 837 frames | OK. ASWBomb 50 | OK? 3 | OK? APSplash | OK? DepthCharge homing | OK. Destroyer `Range=-2` launcher; docks and rearms (SpawnReloadRate 150) | N/A | N/A | OK? 2 | WRONG: every docked return counts as a unit lost (W6-04-26) |
| C | HORNET (spawned) | OK. From the Carrier | OK. HornetBomb 40: NATECH 500→460 | OK? 5 | OK? ORCAAP | OK? NormalBomb | PARTIAL: only one Hornet airborne at a time (`spawned_out` 1 over 150 frames). Not RE'd (§3) | OK? HornetBombE | N/A | OK? 2 | WRONG: lost-unit count (W6-04-26) |
| C | DMISL (spawned) | OK. Rocket loco (W4-PJ-04) | OK. NARADR 1000→612 from one 2-missile volley | OK? 25 / min 8 | OK? | OK. Pause, tilt, climb | OK. Interceptable (W4-PJ-10) | N/A | N/A | N/A | OK? DontScore |
| C | DEST Destroyer | OK. Water (W2-AN-09) | OK. 155mm 60/110 + ASWLauncher | OK? 8 | OK? ARTYHE | OK? Ballistic | OK. Sensors 8 sees a submerged sub at once (`sub_detected_by0=1` after 0 frames); ASW Osprey hunts it | OK? 155mmE Burst 2 (W6-04-01-type burst issue may apply) | N/A | OK. Sonar | OK? |
| C | AEGIS Aegis Cruiser | OK? Water, ROT 1 | **WRONG:** Medusa 100/15 fires only after the hull slowly turns. OmniFire ignored (W6-04-06) | WRONG: effective range ~4.5 cells instead of 12 (W6-04-06) | OK? SAMWH (0% vs ships/buildings) | OK? Homing MEDUSA | MISSING: RadialFireSegments / DistributedFire (minor) | OK? MedusaE Burst 2 | N/A | OK? 8 | OK? |
| C | CARRIER Aircraft Carrier | OK? Water | OK. HornetLauncher spawner. Attacks a land building (Tech Centre) | OK? 25 | N/A (Special) | N/A | OK. CanPassiveAquire=no honoured. Hull-turn delay before launch (W6-04-06) | OK? | N/A | OK? 7 | OK? |
| C | DLPH Dolphin | OK. SHP ship, submerges | PARTIAL: SonicZap 4 dmg plus ambient. IsSonic wave and AmbientDamage are not modelled | OK? 6 | MISSING: `Sonic=yes` warhead eject of a parasite (W6-04-09) | MISSING: sonic wave | **MISSING:** cannot free a ship held by a Squid (W6-04-09) | OK? | N/A | WRONG: surfaces on fire despite DecloakToFire=no (W6-04-25) | OK? |
| C | LCRF Amphibious Transport (Allied) | OK. Amphibious hover (W2-AN-10, naval.play) | N/A | N/A | N/A | N/A | OK. Beaches, unloads on land only | OK? | **WRONG:** took 5 Grizzlies (`cargo_total=5`); the original caps at 4 (size sum 12) (W6-04-08) | OK? 6 | OK? |
| C | SAPC Armored Transport (Soviet) | OK? as LCRF; Crusher=yes | N/A | N/A | N/A | N/A | OK? | N/A | WRONG: same capacity rule (W6-04-08) | OK? 6 | OK? |
| C | SUB Typhoon | OK? Water; submerged | OK. SubTorpedo 100/120: HYD 370→270, torpedo every ~30 frames = 120 ticks | OK? 7 | OK? APSplash | OK? Level torpedo SUBT | WRONG: surfaces for CloakDelay after every shot (`submerged(0)` 2→1 at f=460) (W6-04-25) | OK? SubTorpedoE Burst 2 | N/A | OK. Hidden unless sonar; Sensors 7 | OK? |
| C | HYD Sea Scorpion | OK? Water | OK? FlakTrackGun AG 25/25 + FlakWeapon AA 40/20 | OK? 5 / 12 | OK? FlakTWH / FlakWH | OK? | Not observed: it did not engage the Harrier's 1-pass strike (it kept its Destroyer target, `utgt=2`) | OK? | N/A | OK? 8 | OK? |
| C | SQD Giant Squid | OK. SHP ship | OK. **Grabs in play now:** mine auto-grabbed an enemy DEST (`parasited=1`, 498→279 over 120 frames), and an AI squid grabbed my DEST. R-049's "never fires" was not reproduced | OK. 1.5 | OK? ParasitePlus paralyse | MISSING: leap not drawn (R-054) | PARTIAL: Culling at 25% not observed. Eject by Sonic or heal is missing (W6-04-09) | OK? | N/A | WRONG: surfaces on fire (W6-04-25) | OK? |
| C | DRED Dreadnought | OK? Water | OK. DredLauncher 2×DMISL vs a radar 81 frames after the order | OK? 25 / min 8 (not measured) | OK? DMisl block | OK. RocketLocomotion | OK. CanPassiveAquire=no. Hull-turn before launch (W6-04-06) | OK? elite warhead | N/A | OK? 7 | OK? |
| C | GAYARD / NAYARD | — | — | — | — | — | OK? UnitRepair is free [RE-FALLBACK] (R-090). A healing hit does not eject a Squid (W6-04-09) | — | — | — | — |
| C | GAAIRC / AMRADR pads | — | — | — | — | — | OK. 4 pads; Harrier and Black Eagle dock and rearm | — | — | — | — |
| D | GACNST / NACNST Construction Yards | N/A | N/A | N/A | N/A | N/A | OK (MCV deploy, Factory=Building) | N/A | N/A | OK? Sight 8 | OK? |
| D | GAPOWR Power Plant (+200) / NAPOWR Tesla Reactor (+150) | N/A | N/A | N/A | N/A | N/A | OK (`power_out` 2300 = 2000 + 2x150 measured) | N/A | N/A | OK? | OK? |
| D | NANRCT Nuclear Reactor (+2000) | N/A | DeathWeapon NukePayload | N/A | **WRONG**: death blast at full 600, not x0.5 (W6-04-12) | N/A | **MISSING**: no RadLevel 500 fallout on death (W6-04-12) | N/A | N/A | OK? | OK? |
| D | GAREFN / NAREFN Refineries | N/A | N/A | N/A | N/A | N/A | OK? (Soylent 300) | N/A | N/A | OK? | OK? |
| D | GAPILE / NAHAND Barracks | N/A | N/A | N/A | N/A | N/A | OK? (spy: veteran infantry) | N/A | N/A | OK? | OK? |
| D | GAWEAP / NAWEAP War Factories | N/A | N/A | N/A | N/A | N/A | OK? (spy: veteran vehicles) | N/A | N/A | OK? | OK? |
| D | GAYARD / NAYARD Naval Yards | N/A | N/A | N/A | N/A | N/A | OK? (UnitRepair; naval.play) | N/A | N/A | OK? | OK? |
| D | GAAIRC Airforce Command / AMRADR (Americans) | N/A | N/A | N/A | N/A | N/A | OK (GAAIRC forbidden to Americans, AMRADR present; AmerParaDrop granted, sw_count 1) | N/A | N/A | OK? Radar | OK? |
| D | NARADR Soviet Radar | N/A | N/A | N/A | N/A | N/A | **MISSING**: YR SpyPlaneSpecial not granted, sw_count 0 (W6-04-19) | N/A | N/A | OK? Radar | N/A |
| D | GADEPT / NADEPT Service Depots | N/A | N/A | N/A | N/A | N/A | OK? (UnitRepair) | N/A | N/A | OK? | OK? |
| D | GATECH / NATECH Battle Labs | N/A | N/A | N/A | N/A | N/A | **MISSING**: YR ForceShieldSpecial not granted (W6-04-20); stolen tech OK? (R-042) | N/A | N/A | OK? | N/A |
| D | GAOREP Ore Purifier | N/A | N/A | N/A | N/A | N/A | OK? (OrePurifier read, `world.cpp:4661`) | N/A | N/A | OK? | OK? |
| D | NAINDP Industrial Plant | N/A | N/A | N/A | N/A | N/A | **MISSING**: cameo 0 (R-201); FactoryPlant 0.75 not applied (R-066) | N/A | N/A | – | – |
| D | GASPYSAT Spy Satellite Uplink | N/A | N/A | N/A | N/A | N/A | OK? (whole-map reveal while powered; play abil-gap) | N/A | N/A | OK? | – |
| D | GAROBO Robot Control Center | N/A | N/A | N/A | N/A | N/A | **MISSING**: not in the building roster, so the Robot Tank cannot be built (W6-04-18) | N/A | N/A | – | – |
| D | GAGAP Gap Generator | N/A | N/A | N/A | N/A | N/A | OK? (radius 10, powered; R-044 play abil-gap) | N/A | N/A | OK? | MISSING radial indicator (W6-04-27) |
| D | GAPILL Pillbox | N/A | OK? Vulcan2 50, ROF 26 | **WRONG**: 2-D, no elevation bonus (W6-04-22) | OK? SA, ProneDamage 70% | OK? InvisibleLow | – | N/A | OK (not garrisonable) | OK? Sight 7 | OK? PillboxAttack |
| D | NALASR Sentry Gun | N/A | OK: Vulcan 50, hits every 24-36 logic frames | **WRONG**: 2-D (W6-04-22) | OK: 40 vs flak, then 28 prone (125>85>57>29>1>dead) | OK? | OK (Power 0: never offline) | N/A | N/A | OK? | OK? SentryGunAttack |
| D | NASAM Patriot Missile | N/A | OK: RedEye2, one hit about every 60 logic frames vs a Kirov | **WRONG**: 2-D vs air (the Kirov is at 750 leptons) (W6-04-22) | OK? SAMWH (70-73 per hit measured vs 75, see §3) | OK? AAHeatSeeker homing | OK? AA only (LandTargeting=1) | N/A | N/A | OK? Sight 10 | OK? PatriotAttack |
| D | NAFLAK Flak Cannon | N/A | OK? FlakWeapon 40, ROF 20 | **WRONG**: 2-D (W6-04-22) | OK? FlakWH 1.0 spread | OK? FlakScatter (R-057) | OK? AA only | N/A | N/A | OK? | OK? |
| D | ATESLA Prism Tower | N/A | OK? PrismShot 120, ROF 45 | **WRONG**: 2-D (W6-04-22) | OK? | OK? laser | OK? chain (R-043, play abil-prism-chain) | N/A | N/A | OK? | OK? |
| D | TESLA Tesla Coil | N/A | OK? CoilBolt 200 / OPCoilBolt 300 | **WRONG**: 2-D (W6-04-22) | OK? Electric | OK? bolt | OK? Overpowerable (R-045, play abil-tesla-charge) | N/A | N/A | OK? | OK? |
| D | GTGCAN Grand Cannon (France) | N/A | OK? 150, ROF 120 | **WRONG**: MinimumRange 3 ignored, killed a Conscript 1.5 cells out (W6-04-22) | OK? GrandCannonWH 2 spread | OK? arcing ball | OK (RequiredHouses: Americans cannot build) | N/A | N/A | OK? Sight 10 | OK? TurretRotateSound |
| D | NABNKR Battle Bunker | N/A | occupants' weapons | – | – | – | **MISSING**: cameo 0 (R-201) | N/A | **MISSING** (R-201; garrison combat R-053) | – | – |
| D | GAWALL / NAWALL Walls | N/A | N/A | N/A | **WRONG**: any warhead damages a built wall; a GI took 6 HP off (W6-04-04) | N/A | OK (Grizzly AP levelled one) | N/A | N/A | N/A | OK? |
| D | Map wall overlays | N/A | N/A | N/A | **MISSING**: no warhead damages them (P4-5, W6-04-05) | N/A | OK? crushable (P4-4) | N/A | N/A | N/A | N/A |
| D | NAMISL Nuke (RechargeTime 10) | N/A | OK: charge 8988 logic frames (9000) | OK? (Range 7 cosmetic) | OK: GAPOWR 750 left at 390 (600 x wood 60%); fallout OK (rad_total 57787); walls **WRONG** (W6-04-04) | ~400-frame flight (documented deviation) | OK | N/A | N/A | N/A | OK? |
| D | NAIRON Iron Curtain (5) | N/A | OK: charge about 4480 (4500) | 3x3 | OK: 2 Conscripts killed, the tank and a building protected, about 752 frames (750) | N/A | OK | N/A | N/A | N/A | OK? |
| D | GACSPH Chronosphere (7) | N/A | OK: charge about 6284 (6300) | 3x3 | OK: a Rhino warped onto water died | N/A | OK? (infantry chrono-killed) | N/A | N/A | N/A | OK? |
| D | GAWEAT Weather Control (10) | N/A | OK: charge 9000 | 10x10 scatter | OK: first bolt about 256 frames (Deferment 250), storm ends about 432 (250+180), target GAGAP destroyed | N/A | OK | N/A | N/A | N/A | OK? |
| D | Paradrops (AMRADR AmerParaDrop, CAAIRP ParaDrop) | N/A | – | – | – | – | OK? (sw.play; R-056 FIXED) | N/A | – | – | OK? |
| D | SpyPlaneSpecial (NARADR), ForceShieldSpecial (Battle Labs) | N/A | – | – | – | – | **MISSING** (W6-04-19, W6-04-20) | N/A | – | – | – |

## Fix status (wave-6 fix agent #4, branch `w6fix-units`)

| ID | Status | Commit | RE / basis |
|---|---|---|---|
| W6-04-01 | FIXED | `afaccad` | TechnoClass::Fire FUN_006FD210 (index++, GetROF, index %= Burst), GetROF FUN_006FC460 (Random(3,5) inside a burst), AircraftClass::Mission_Attack FUN_00417FE0 case 4 (Burst shots per ammo point) |
| W6-04-02 | FIXED | `8433c78` | BulletType ShrapnelWeapon/ShrapnelCount (rulesmd projectile sections); fragment targeting stays W2-SK-22 RE-FALLBACK |
| W6-04-03 | FIXED | `392cf1f` | BombClass::Detonate 0x4386A0 / FireDeathWeapon FUN_0070CAC0 -> DamageArea FUN_00489190 |
| W6-04-04 | FIXED | `fba6ff5` | DamageArea @0x489581..0x4895BD + Reduce_Wall FUN_00480BC0 |
| W6-04-05 | FIXED | `fba6ff5` | as W6-04-04 (map overlays: per-cell stage, destroyed walls open) |
| W6-04-06 | FIXED | `355a063` | WeaponType OmniFire +0x12B |
| W6-04-07 | FIXED | `2fe84c9` | naval.md §1/§4 (approach on the unit's own zone) |
| W6-04-08 | FIXED | `4ccc77d` | UnitClass::ReceiveMessage FUN_00736500; FUN_004190B0 is AircraftClass's |
| W6-04-09 | FIXED | `b03b13d` | FootClass::ReceiveDamage FUN_004D7250 (Sonic +0x14B, heal -> ExitUnit FUN_00629F40) |
| W6-04-10 | FIXED | `a3ca80d` | rules [DRON]; the campaign/cheat infantry-path registration |
| W6-04-11 | FIXED (icon) | `f795dc5` | IvanIconFlickerRate=8; icon conquer.mix bomb.shp [RE-FALLBACK]; sounds by #5 |
| W6-04-12 | FIXED | `392cf1f` | FireDeathWeapon FUN_0070CAC0 @0x70CB1B (Damage x DeathWeaponDamageModifier), RadSite on detonation |
| W6-04-13 | FIXED | `ba1b4b3` | BuildingClass::Greatest_Threat FUN_00445E60, GetThreatFlags FUN_007718B0 (AG = 0xB8 incl. BUILDINGS), FUN_006F8320 @0x6F8410, Evaluate_Object R-064 rule |
| W6-04-14 | FIXED | `7348982` | rules semantics [RE-FALLBACK] |
| W6-04-15 | FIXED | `7348982` | rules OpenTransportWeapon= |
| W6-04-16 | FIXED | `7348982` | FUN_006F6750 adds AirRangeBonus vs an airborne target (combat.md §9) |
| W6-04-17 | PARTIAL | `afc11ee` | buildable (art present: localmd.mix schp/schd; no turret voxel -> hull only); deploy-to-artillery OPEN |
| W6-04-18 | FIXED | `afc11ee` | GAROBO rostered (YR art); PoweredUnit stop [RE-FALLBACK] |
| W6-04-19 / 20 | OPEN | – | user decision |
| W6-04-21 | PARTIAL | `afc11ee` | NABNKR / NAINDP buildable (YR art); NAINDP UnitsCostBonus not applied (R-066) |
| W6-04-22 | FIXED | `1aefcca` | FUN_006F6750 / FUN_006F6490 for building shooters |
| W6-04-24 | FIXED | `f795dc5` | IsRadBeam / IsLine [colours RE-FALLBACK] |
| W6-04-25 | FIXED | `b885a04` | DecloakToFire +0x133 |
| W6-04-26 | FIXED | `b885a04` | deployed_away on dock |
| W6-04-27 | FIXED by #5 | `e24699f` | – |
| W6-04-28 | FIXED | `b885a04` | veterancy.md V5/V7/V10 |
| W6-04-29 / 30 | FIXED | `7348982` | rules keys |
| W6-04-35 | FIXED | `b885a04` | harness |
| W6-04-23, 31..34, 36..38 | OPEN | – | not in this fix list |

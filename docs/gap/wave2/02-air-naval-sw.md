# Wave-2 gap analysis 02: aircraft, naval and superweapons in real skirmish play

Audit of master `dfff02a` (worktree `D:\ra2-wt\w2-air`, branch `w2-air`, own build dir;
no engine source modified, nothing committed). The original is RA2 Allied/Soviet with no Yuri.
The clone reads YR `rulesmd.ini` filtered to the two classic sides. Where classic `rules.ini`
(`ra2.mix/local.mix`) differs, the difference is called out below.

Wave-1 items already fixed and re-verified (not re-reported): G-24 water grid in skirmish and
Naval Yard placement on water (the Destroyer launches onto water), G-25 SW granted by its
building and revoked with it, G-29 the `IsPowered` pause, G-44 the AA weapon gate (Flak Track
hits a Kirov), and G-66 the AI SW autofire code path.

## How this was exercised

All runs were done one at a time from PowerShell with `RA2_INSTALL_DIR=D:\Red Alert 2`.
The scratch scripts and their outputs are in the session scratchpad, not in the repo.

| Run | What | Result |
|---|---|---|
| `w2-air.play` (Allied, hillbtwn) | GAPOWR, GAREFN, GAPILE and AMRADR built through the real sidebar, then the sidebar was dumped; Rocketeer built; ORCA, ZEP, SHAD and E1 cheat-spawned and ordered | `cameo(ORCA)=0`, `cameo(BEAG)=0`, `cameo(JUMPJET)=1`; Rocketeer trained in 94 frames. ORCA, ZEP and SHAD were not rendered: a cheat-spawned type outside the human roster has no art (`[render] no SHP loaded for image=FALC/ZEP`), so this is a probe artefact. |
| `w2-kirov.play` (Soviet) | ZEP built from a real War Factory, takeoff, attack order on an enemy HTNK, `S` | The Kirov leaves the factory grounded, then takes off on its gather move. It flies about 1.1 cells/s, crossed 64 cells and killed an HTNK in the AI base (`units_lost(1)` 1 to 2). Shadow and raised sprite are OK (screenshots `k*.png`, `katt*.png`). |
| `w2-sov*.play` (Soviet, real build) | Tech to NATECH, then NAIRON, then NAMISL, then the Kirov | After NAIRON was built, `sw(0)=0` (the Iron Curtain grants nothing). After NAMISL, `sw(0)=1`. **The first render after the silo exists throws `Texture dimensions can't be 0`, and the play driver exits with code 3.** |
| `w2-swbld.play` (Allied) | Instant NAIRON, GACSPH, GAWEAT and NAMISL, with a screenshot after each | NAIRON, GACSPH and GAWEAT render. **NAMISL crashes the renderer** (also seen with superweapons off, so the SW slot is not the cause). |
| `w2-nuke.play` (Soviet, no rendering) | NAMISL plus 3 NAPOWR placed instantly; wait for READY; `click sw`; target an enemy HARV | READY after 2,221 driver frames (game speed 1 runs about 4 logic ticks per frame, which is about 9,000 ticks: correct). Firing changes `hp_lost(1)` from 0 to 920 and `hp_lost(2)` from 0 to 15 within 3 frames: an instant strike with no flight. No HARV died (600 x 100% against Strength 1000). |
| `w2-naval.play` (Allied, xbayopigs.map) | GAYARD placed on water by the real sidebar; DEST built; 4 move orders in 4 directions; LCRF built; enemy SUB and DRED cheat-spawned | Yard and Destroyer OK. **The DEST never moved: `sel_cx/cy` stayed 175,189 for all 4 orders.** The cursor read `Move` over land (no water path) and `NoMove` over water. LCRF "launched" (`units(0,LCRF)=1`). The enemy SUB and DRED killed 6 shore infantry in 900 frames, and the surfaced SUB died to the Destroyer's 155mm. |
| `w2-swoff.play` (Soviet, Superweapons unchecked) | Refinery built, sidebar dumped | `tab_ids="NAPOWR,NAREFN,NAHAND,NAYARD"` with `cameo(NARADR)=0`: **the radar is gone**, so NATECH, Tesla Coil, Flak Track and the rest of the radar tech tree become unreachable. |
| `--ai-soak --minutes 30 --diff brutal` (Soviet AI, xmp31s2) | Passive human | Over 30 minutes the AI built 29 buildings: no NATECH, NAMISL, NAYARD or air units. `sw_ever_ready=0 sw_shots=0`. From minute 21 the AI sat at 174 credits. |
| `--probe-realplay` | Existing G-24 naval probe | **Stale and crashing.** It was written for the pre-G-23 pre-built base, so its G-20/G-22/G-26 checks FAIL, and it aborts on `stl_vector.h:1253 __n < this->size()` before reaching its naval section. |

Ghidra (private copy `D:\ra2-re\w2-air`, two short read-only headless passes over YR `gamemd.exe`,
scripts `D:\ra2-re\scripts\w2air_sw1.py` and `w2air_sw2.py`):

- **SW EVA call sites, via xrefs to the EVA strings.**
  - `EVA_NuclearSiloDetected`, `IronCurtainDetected`, `ChronosphereDetected` and `WeatherDeviceReady` are referenced only from `FUN_00445ee0` (building placement/unlimbo).
  - `EVA_NuclearMissileLaunched`, `IronCurtainActivated`, `ChronosphereActivated` and `LightningStormCreated` come from the fire dispatcher `FUN_006cba10`.
  - The `*Ready` strings come from `FUN_006cb320`, `FUN_006cb700` and `FUN_006cba10`.
- **Who hears the "detected" EVA.** In `FUN_00445ee0` the warning is played only when the placing house is not the player and not allied to the player (`FUN_0050b510()==0 && FUN_004f9870(PlayerPtr)==0`). It fires only for SW types 0, 1, 2, 3, 7 and 9: Nuke, Iron Curtain, Lightning Storm, Chronosphere, Psychic Dominator and Genetic Mutator. The SW index is read from `BuildingType+0x16F0`.
- **Iron Curtain.** Dispatcher case 1 walks a 3x3 cell offset table (`DAT_00b0afa8..0xb0afcc`, 9 dx,dy pairs) around the target and calls the object vtable `+0x154` on every object in those cells.
- **Chrono Warp.** Case 4 walks the same 3x3 table around the source. An object passing the `TechnoType+0xD97` / `+0xCD4` tests is teleported. Any other object gets `ReceiveDamage` (vtable `+0x16C`) for its full Strength, with the warhead at `Rules+0xFA8`. This is the chrono-kill branch.

Classic RA2 ground truth used below, from `rules.ini` extracted from `ra2.mix/local.mix`:
- **`[SuperWeaponTypes]`**: MultiSpecial, IronCurtainSpecial, LightningStormSpecial, ChronoSphereSpecial, ChronoWarpSpecial, ParaDropSpecial, AmericanParaDropSpecial. Classic RA2 has no SpyPlane or ForceShield superweapon.
- **SW-granting buildings**: NAMISL, NAIRON, GACSPH and GAWEAT; AMRADR grants AmerParaDrop; CAAIRP (Tech Airport) grants ParaDrop. **NARADR has no `SuperWeapon=` in classic RA2** (YR adds `SpyPlaneSpecial`).
- **`[General]` superweapon values**: `IronCurtainDuration=750`, `Lightning*` (Deferment 250, Damage 250, Duration 180, HitDelay 10, ScatterDelay 5, CellSpread 10, Separation 3), `Chrono*` (Delay 60, DistanceFactor 48, MinimumDelay 16), `AmerParaDropInf=E1 Num=8`, `AllyParaDropInf=E1 Num=6`, `SovParaDropInf=E2 Num=9`, `ParadropRadius=1024`.
- **`[General]` aircraft and naval values**: `ReloadRate=.3`, `FlightLevel=1500`, `CloakDelay=.02`, `ShipSinkingWeight=3.0`.

## 1. Matrix

Key: Y = works in real play; P = partial; N = no; G = gate/demo only; n/a = not in classic RA2.

### Aircraft

| Unit (rules id) | Buildable? | Moves? | Attacks? | Special behaviour? | Notes |
|---|---|---|---|---|---|
| Harrier (`ORCA`, Fly loco, Ammo=1, Dock=GAAIRC/AMRADR) | **N**: `[AircraftTypes]` skipped (`with_aircraft=false`, `realplay_scene.inc:96`, `realplay_economy.inc:120`); verified `cameo(ORCA)=0` with AMRADR up | **N/wrong**: Fly CLSID is not `is_aircraft` (`main.cpp:797-800`), so a spawned ORCA is a ground vehicle at Speed 14 | ground-only, as a tank would | **N**: no pad, takeoff, `Ammo--`, return to base or 270-frame rearm in World; `roster2::Sortie` is run only by `--demo-roster2` (`main.cpp:13757-13825`) | W2-AN-01/02 |
| Black Eagle (`BEAG`) | **N** (same) | **N** | **N** | **N** | Alliance plus all Allies in classic `Owner=` |
| Kirov (`ZEP`, Jumpjet, BalloonHover) | **Y** (`cameo(ZEP)=1` after NATECH) | **Y**: straight-line cruise at about 1.1 cells/s, raised sprite plus shadow | **Y**: bombs from cruise, 2-cell BlimpHE splash, killed an HTNK | **P**: exits the factory grounded (a ground target until its gather move); `S` does not stop it (`unit_orders.cpp:23-26` leaves `has_flight_goal`); **no crash on death** (`Crashable=yes` ignored) | W2-AN-05/06/07 |
| Rocketeer (`JUMPJET`, infantry Jumpjet) | **Y** (built from the barracks after AMRADR) | **Y** (flight branch) | **Y** (20mm, AA+AG) | **P**: same exit-grounded issue; no crash/fall on death (`Crashable=yes`) | — |
| Night Hawk (`SHAD`, Jumpjet, Passengers=5, Landable) | **Y** (vehicle, GAWEAP, voxel loaded) | **P**: flies, but **never lands** (no code sets `FlightState::Descending`; `world.cpp:1309-1349`) | **Y** (BlackHawkCannon) | **N**: infantry can board an airborne Night Hawk and it unloads in the air (`world.cpp:1190-1200` has no altitude test); no land-to-load/unload | W2-AN-08 |
| Spy Plane (`SPYP`) | n/a | — | — | YR-only SW (`NARADR SuperWeapon=SpyPlaneSpecial` is YR) | W2-AN-19 |
| Paradrop plane (`PDPLANE`) | n/a (SW-spawned) | **N** | — | No ParaDrop or AmerParaDrop SW | W2-AN-17 |
| Hornet / ASW Osprey / Dread missile (`HORNET`/`ASW`/`DMISL`, Spawned=yes) | n/a (spawned) | **N** | **N** | No spawner system at all | W2-AN-12 |
| Anti-air: Flak Track, Flak Trooper, IFV, GGI, APOC, Patriot (NASAM), Flak Cannon (NAFLAK), Aegis | Y | — | **Y** (G-44 fixed; `probe-combat` covers the HTK and NASAM rows) | Aegis Medusa is AA-only (correct) | Instant projectile for AA (projectile.md deviation) |

### Naval

| Unit | Buildable? | Moves? | Attacks? | Special behaviour? | Notes |
|---|---|---|---|---|---|
| Destroyer (`DEST`) | **Y** (`cameo(DEST)=1`, launched onto water) | **N in practice**: a human click on water is `NoMove` because `Input::classify` tests the LAND grid (`input_original.cpp:59-64,181`); a click on land paths nowhere. Verified: 4 orders, 0 moves | **P**: 155mm hits a *submerged* sub (no cloak) | **N**: ASW Osprey spawner (`Secondary=ASWLauncher Spawns=ASW`) missing; `Sensors=yes SensorsSight=8` sub detection missing | W2-AN-09/12/13 |
| Aegis Cruiser (`AEGIS`) | **P** (GAYARD+RADAR; voxel loaded) | same as DEST (**N**) | **Y** AA only (Medusa `AG=no`) | — | — |
| Aircraft Carrier (`CARRIER`) | **P** (GAYARD+TECH) | **N** (as DEST) | **Wrong**: `HornetLauncher` (`Spawner=yes`) resolves as a direct weapon: Damage 1, Range 25, every 150 frames | **N**: no Hornets | W2-AN-12 |
| Dolphin (`DLPH`) | **N**: dropped by the art guard (`[unit] id=DLPH` resolves, but no `[vehicle] image=DLPH` voxel line; roster2.md "Cut: SQD, DLPH") | — | — | Sonic weapon, Underwater, Cloakable, Sensors | W2-AN-15 |
| Dreadnought (`DRED`) | **P** (NAYARD+NATECH) | **N** (as DEST) | **Wrong**: `DredLauncher Spawner=yes Range=25 MinimumRange=8` fires as an instant 50-damage, 2-shot burst at up to 25 cells (no missiles, no interception, no minimum range); the cheat-spawned DRED and SUB killed 6 shore GIs | **N**: no V3-style missiles; the `CanPassiveAquire=no` guard rule is not read | W2-AN-12 |
| Typhoon Sub (`SUB`) | **P** (NAYARD) | **N** (as DEST) | **Wrong**: Torpedo `Level=yes` plus `LandTargeting=1 NavalTargeting=5` is ignored, so the sub torpedoes land targets | **N**: no submerge/cloak (`Underwater/Cloakable` unread; no `{4A582741}` second loco); every unit can see and shoot it | W2-AN-13/14 |
| Giant Squid (`SQD`) | **N** (art-dropped like DLPH) | — | — | grab/punch (ParasitePlus) | W2-AN-15 |
| Sea Scorpion (`HYD`) | **P** (NAYARD+NARADR) | **N** (as DEST) | **Y** (FlakTrackGun AG; secondary FlakWeapon AA) | — | — |
| Amphibious Transport (`LCRF` Allied, `SAPC` Soviet; Hover loco, `MovementZone=Amphibious`) | **Y** from the yard (`units(0,LCRF)=1`) | **Wrong**: Hover CLSID is not `is_naval`, so the unit is Land-domain and launches through the land exit ring (`world.cpp:1023-1035`); it can never enter water | — | **N**: no beach crossing, no load at shore / unload on beach | W2-AN-10 |
| Naval Yard (`GAYARD`/`NAYARD`) | **Y** (water placement) | — | — | **N**: `UnitRepair=yes` dock repair missing (`grep -i UnitRepair engine/` finds nothing); `NumberImpassableRows=3` not modelled | W2-AN-11 |
| Naval AI | **N**: the AI roster has no ships (`ai_skirmish.inc:15-37`), the AI plan has no yard, and no naval TaskForces run; the soak confirmed no NAYARD | — | — | — | W2-AN-16 |
| Ship death | — | — | — | **N**: `ShipSinkingWeight=3.0` sinking is missing; ships explode like tanks | W2-AN-25 |

### Superweapons and support powers (classic RA2 set)

| SW (building) | Granted? | Charge / power gate | Targeting UI | Effect | EVA | AI use | Notes |
|---|---|---|---|---|---|---|---|
| Nuclear Missile (`NAMISL`, MultiSpecial / RechargeTime=10) | **Y** (`sw(0)=1` after the silo) | **Y**: 9,000 ticks; `IsPowered` pause (G-29) | **Y**: `click sw` arms, a map click fires; nuke cursor | **P**: instant damage at click (no NukeCarrier climb, no NKMSLDN fall); enemies only (no friendly fire); never hits airborne units (`combat.cpp:247`); no RadLevel=500 fallout; building splash OK | **P**: "ready" and "launched" for the human only; no enemy "Nuclear silo detected" or "missile launched" warning | Code path exists (G-66) but the AI never reaches NATECH/NAMISL (soak: `sw_shots=0`) | **Rendering a silo crashes the game** (W2-AN-03) |
| Iron Curtain (`NAIRON`) | **N**: `Type=IronCurtain` maps to `None` (`realplay_economy.inc:185-195`); verified `sw(0)=0` with NAIRON built | — | cursor art exists (`pres_game.inc:237`) but is unreachable | **N** (750-frame invulnerability, 3x3 cells) | ready line only (dead code) | **N**: the AI still pays 2,500 for the building | W2-AN-04 |
| Chronosphere + Chrono Warp (`GACSPH`) | **N** (`None`) | — | **N**: no two-click source/destination | **N** (3x3 teleport; chrono-kill for ineligible objects; water deaths) | **N** | **N** | W2-AN-04 |
| Weather Control / Lightning Storm (`GAWEAT`) | **N** (`None`) | — | **N** | **N** (250-frame warning, 180-frame storm, bolts every 10/5 frames, 10x10 scatter) | **N** | **N** | W2-AN-04 |
| American Paratroopers (`AMRADR`, 4 min) | **N** (`None`) | — | **N** | **N** (PDPLANE drops 8 E1) | — | — | W2-AN-17 |
| Paradrop (`CAAIRP` Tech Airport, captured) | **N**: CAAIRP is not in the building roster | — | — | **N** (Allied 6 E1 / Soviet 9 E2) | — | — | W2-AN-17 |
| More than one SW per house | **N**: one `HouseSuperweapon` per house (`world.hpp:1592-1601`); `rp_sync_superweapons` takes the FIRST granting building (`realplay_economy.inc:201-223`); a single NukeIcon sidebar slot (`main.cpp:3740-3761`) | — | — | — | — | — | W2-AN-04 |
| Lobby "Super Weapons" off | **Wrong**: hides every building with `SuperWeapon=`, so the YR `rulesmd` NARADR (SpyPlane) and AMRADR (AmerParaDrop) vanish. Verified `cameo(NARADR)=0` | — | — | — | — | — | W2-AN-18 |

## 2. Gap table

Severity: BLOCKER = crashes or makes a basic loop impossible; MAJOR = a headline mechanic is
missing or wrong; MINOR = fidelity. Effort: S < 1 day, M 1-3 days, L > 3 days.

| ID | Issue | Original (cite) | Clone (file:line / play evidence) | Status | Severity | Fix sketch | Effort | Depends on |
|---|---|---|---|---|---|---|---|---|
| W2-AN-01 | Harrier and Black Eagle cannot be built | `[ORCA]/[BEAG]` in `[AircraftTypes]`, `Prerequisite=RADAR`, built at GAAIRC/AMRADR (`roster2.md §C`) | `with_aircraft=false` (`realplay_scene.inc:96`, filter at `realplay_economy.inc:120`); `cameo(ORCA)=0` with AMRADR built | MISSING | MAJOR | Flip to `true` once W2-AN-02 lands; keep the Aircraft queue/`air_ft_` already wired (`realplay_scene.inc:112,247`) | S | W2-AN-02 |
| W2-AN-02 | No fixed-wing flight model in World | FlyLocomotion `{4A582746}` (`aircraft.md` Deferred), Ammo, dock pads `GetCoords+DockingOffset[k]`, `Ammo--` per pass, return to base at 0, 270-frame `ReloadRate` reload (`roster2.md` TL;DR 9-11, `FUN_00417FE0`, `FUN_0044B780`) | `is_aircraft` only for the Jumpjet CLSID (`main.cpp:797-800`), so ORCA is a ground vehicle; `Sortie`/`step_sortie` (`roster2.cpp`) is driven only by `--demo-roster2` (`main.cpp:13757-13825`) | GATE-ONLY | MAJOR | Add `fly_loco` to `UnitCombat`; a `World::step_fixed_wing` that owns a `Sortie` per aircraft (pad claim via `first_free_pad`, takeoff to `FlightLevel`, attack pass, return to base, land, reload); spawn ORCA docked on a free pad in `launch_unit` when the factory is Helipad; an attack order sets `Sortie.target`; idle = docked | L | — |
| W2-AN-03 | **Rendering a Nuclear Missile Silo crashes the game** | silo renders with its `SuperAnim*` overlays | `w2-swbld.play`: NAIRON, GACSPH and GAWEAT render, then NAMISL throws `Texture dimensions can't be 0` (`texture_cache.cpp:50-53`), driver exit 3; same with superweapons off (`w2-silo-off.play`), and in the real-build run at the first render after placement (`w2-sov3.play` f=3371) | BROKEN | BLOCKER | Guard 0x0 frames in `TextureCache::get` (skip the draw; `main.cpp:3565` already guards one path); find the empty frame in `ngmisl.shp` or `NAMISL_E..H` and skip it at load. Any AI silo on screen would also crash (the AI catalog includes NAMISL) | S | — |
| W2-AN-04 | Iron Curtain, Chronosphere/Chrono Warp and Lightning Storm do nothing; one SW per house | 7 classic SW types; per-house instance per SW type (`superweapon.md §1`, `FUN_004f53b0`); dispatcher cases 1/2/3/4 (`FUN_006cba10`; this audit: case 1 = 3x3 IronCurtain call vtable `+0x154`, case 4 = 3x3 chrono teleport, ineligible objects take their full Strength via the `Rules+0xFA8` warhead) | `rp_superweapon_table` maps only `MULTIMISSILE` (`realplay_economy.inc:191`); `fire_superweapon` refuses non-Nuke (`world.cpp:1443`); one `HouseSuperweapon` (`world.hpp:1592-1601`); first-building-wins (`realplay_economy.inc:204-211`); NukeIcon-only slot (`main.cpp:3740-3761`, loader `main.cpp:2203-2217`); NAIRON built gives `sw(0)=0` | MISSING | MAJOR | `sw_houses_[h]` becomes a vector keyed by SW type; one sidebar cameo per granted SW (SidebarImage from rules); effects: IronCurtain = 3x3 invulnerable for `IronCurtainDuration=750` (Unit/Building `iron_until`, `ReceiveDamage` early-out, red tint); ChronoSphere/Warp = PreClick source then PostClick destination, move 3x3 objects, kill infantry and land units dropped on water; Lightning Storm = cloud plus 250-frame deferment, then 180 frames of `IonWH` 250-damage bolts (direct every 10 frames, scatter every 5 within 10x10, separation 3) | L | W2-AN-03 |
| W2-AN-05 | Nuke is instant and invisible to enemies | NukeCarrier up-leg to `DetonationAltitude 20000`, then NukePayload down (`superweapon.md §2`), several seconds of flight; `EVA_NuclearMissileLaunched` from the dispatcher (this audit, `FUN_006cba10@6cd208`) | Damage applied in the click frame (`world.cpp:1437-1511`; `w2-nuke.play` `hp_lost(1)` 0 to 920 within 3 frames); the takeoff anim and NUKEANIM play together only for the human's launch (`pres_game.inc:128,368-390`); an AI launch has no visuals or sound | WRONG | MAJOR | Model the two legs as a timed SW projectile (reuse P2 `step_projectiles` with a Vertical spec: NKMSLUP from the silo, NKMSLDN onto the cell); detonate on arrival; drive visuals from a World event, not from the human click | M | — |
| W2-AN-06 | Nuke: no friendly fire, no airborne victims, no fallout | `FUN_00489190` hits every object in radius, own included (`superweapon.md` Deviations); `[NukePayload] RadLevel=500` | enemies-only (`world.cpp:1466,1492`); airborne skipped (`combat.cpp:247`); no RadSite | WRONG | MINOR | Remove the house filter for SW splash; let the down-leg hit the air layer within its altitude; reuse P4's RadSite (`abilities.hpp`) with level 500 at the cell | S | W2-AN-05 |
| W2-AN-07 | Jumpjet aircraft do not crash, ignore Stop, exit grounded | `Crashable=yes` (ZEP/JUMPJET/SHAD): falls with `JumpjetCrash` and explodes on impact; the Kirov crash damages the ground (`aircraft.md` Deferred, Draw_Matrix crash tilt); Stop halts a hovering unit; jumpjets lift off on exit | no `Crashable` read (grep); dies in place at altitude; `order_stop` returns before clearing `has_flight_goal` (`unit_orders.cpp:23-26`); `launch_unit` spawns Grounded (`kirov_out.png`: Kirov sitting on the road until its gather move) | PARTIAL | MINOR | On death with altitude > 0 and Crashable: a `Crashing` state that descends at `JumpjetCrash`, then a ground explosion (unit Explosion/DeathWeapon) at the impact cell; Stop clears the flight goal; `launch_unit` puts jumpjets in TakingOff | M | — |
| W2-AN-08 | Night Hawk never lands; boarding and unloading in mid-air | non-BalloonHover jumpjets land at the destination (`BalloonHover` read at `0x714135`, `aircraft.md §2`); passengers enter and leave a landed transport | `FlightState::Descending` is never assigned (grep: only read at `world.cpp:827,1322`, `flight.cpp:47`); `balloon_hover` copied but unused (`world.cpp:603`); boarding tests only Chebyshev adjacency (`world.cpp:1190-1200`) | MISSING | MAJOR | Arrived and `!balloon_hover` sets Descending, then Grounded (re-claim the occupancy cell); `can_load` and the unload require Grounded; an enter order on an airborne transport makes it land at the boarder's cell | M | — |
| W2-AN-09 | **Ships cannot be ordered to move** | ship follows a FootClass path over the Float grid (`naval.md §1`); move cursor over water | `Input::classify` uses `cell_walkable`, which reads `world_.passable()` (LAND) for every selection (`input_original.cpp:59-64,134,181`); the cursor uses the same grid (`pres_game.inc:281-284`). `w2-naval.play`: the DEST stayed at 175,189 through 4 orders (cursor Move over land and NoMove over water) | BROKEN | BLOCKER | `cell_walkable` takes the selection's domains: walkable if any selected unit can enter (water grid for `is_naval`, always for `is_aircraft`, both for amphibious); same in `probe_cursor`; group move over mixed domains splits per domain | S | — |
| W2-AN-10 | Amphibious transports are land-only | LCRF/SAPC `SpeedType=Hover`, `MovementZone=Amphibious`, Hover loco `{4A582742}`: `[Water]` and `[Beach]` passable (`naval.md §2` table); load on the shore, beach and unload | not `is_naval` (`main.cpp:834-839`), so Land domain; launched through the land ring (`world.cpp:1023-1035`); `MovementDomain` has no Amphibious (`unit_combat.hpp`) | WRONG | MAJOR | Add `MovementDomain::Amphibious` = land OR water grid (with Beach); `launch_unit` puts it on water; A* over the union; cargo board/unload already exists, so restrict unload to non-water cells | M | W2-AN-09 |
| W2-AN-11 | Naval Yard does not repair ships; yard footprint fully blocked | `[GAYARD]/[NAYARD] UnitRepair=yes` (`rules.ini`), dock repair like the service depot; `NumberImpassableRows=3` bay (`roster2.md §B`) | no UnitRepair anywhere (grep); `block_water_footprint` blocks all 16 cells (`realplay_scene.inc:38-39`) | MISSING | MINOR | Shared `UnitRepair` dock service (shares the depot fix, if another wave-2 area owns GADEPT): a damaged ship ordered onto the yard dock cell heals at `RepairRate`/`RepairStep` for credits | M | — |
| W2-AN-12 | No spawner weapons: Carrier, Dreadnought and Destroyer ASW are wrong | `Spawner=yes` weapons launch `Spawns=` aircraft/missiles (`HORNET` x3, `DMISL` x2, `ASW` x1); missiles can be shot down; `MinimumRange=8` for DRED | grep finds no `Spawner`/`MinimumRange`; resolved as direct-fire: CARRIER 1 damage at 25 cells, DRED 50x2 instant at 25 cells (killed shore infantry in `w2-naval.play`) | WRONG | MAJOR | Interim: zero out `Spawner=yes` weapons (no fire) plus honour `MinimumRange`; full: a `SpawnManager` per unit (regen, launch, return), with HORNET/ASW on the W2-AN-02 fixed-wing state and DMISL as a homing projectile with Strength 50 that AA can target | L | W2-AN-02 |
| W2-AN-13 | Subs and Dolphins never submerge; no sonar | `Underwater=yes Cloakable=yes`, second loco `{4A582741}`, `CloakDelay=.02`; only `Sensors=yes` units (DEST/SUB/DLPH, SensorsSight) reveal them (`naval.md` Deferred, `FUN_00740780`) | `Underwater/Cloakable/Sensors` unread (grep); the SUB is visible to all and died to the DEST's cannon | MISSING | MAJOR | `Unit::cloaked` set while idle/moving and cleared for `CloakDelay` after firing; `valid_target`/visibility skip a cloaked enemy unless an enemy Sensors unit is within SensorsSight; draw with the cloak shimmer for the owner only | M | — |
| W2-AN-14 | Naval targeting rules ignored | `NavalTargeting`/`LandTargeting`/`CanPassiveAquire` (`naval.md §4`, `FUN_00711510`); `Torpedo Level=yes` stays on water | unread (grep `NavalTargeting` finds only a comment, `world.cpp:3584`); the SUB torpedoes land units | MISSING | MINOR | Parse the three keys into `UnitCombat`; `valid_target` filters by the victim domain (land vs naval) per key; a Level projectile cannot target land | S | — |
| W2-AN-15 | Dolphin and Giant Squid absent | `[DLPH]`/`[SQD]` are buildable in classic RA2 (`Prerequisite=GAYARD,GATECH` / `NAYARD,NATECH`) | dropped by the art guard (no `[vehicle] image=DLPH`; roster2.md "Cut: SQD, DLPH (no voxel art)") | MISSING | MINOR | Resolve their art (open RE question below); SQD needs the P4 parasite grab on ships | M | W2-AN-13 |
| W2-AN-16 | AI fields no aircraft and no navy | stock aimd TaskForces include Kirov/Harrier/Dreadnought/Sub teams; the AI builds GAAIRC/NAYARD (`04-ai.md` row 17) | AI roster has no ZEP/JUMPJET/SHAD/ships (`ai_skirmish.inc:15-37`); `ai_idle_units` and `ai_quarry_exists` skip `is_aircraft` (`world.cpp:2700,2718`); soak (30 min, Brutal): no NATECH, NAYARD or air | MISSING | MAJOR | Add ZEP/SHAD/JUMPJET and the ships to the AI roster; let teams recruit jumpjets (they path like ground for the script layer); NAYARD in the plan when the map has water; naval TaskForces gated on the yard | M | W2-AN-09, W2-AN-02 |
| W2-AN-17 | No Paradrop / American Paratroopers | AMRADR `AmericanParaDropSpecial` (4 min, 8 E1), CAAIRP `ParaDropSpecial` (6 E1 / 9 E2) via PDPLANE from the map edge, `ParadropRadius=1024` | mapped to `None` (`realplay_economy.inc:185-195`); CAAIRP not in the building roster (`main.cpp:1939-1941`) | MISSING | MAJOR | After W2-AN-04's multi-SW: a scripted plane (straight line from the nearest edge, no AI) that spawns the infantry with a parachute fall at the target, then exits; add CAAIRP as a capturable tech building granting ParaDrop | M | W2-AN-04 |
| W2-AN-18 | **Lobby "Super Weapons" off removes the radar** | the option disables superweapons with `DisableableFromShell=yes` (Nuke, Curtain, Chrono, Storm); classic NARADR has no SW, and AmerParaDrop is `DisableableFromShell=no` | `rp_is_lobby_sw_building` hides any building with `SuperWeapon=` except `[AI] BuildTech` (`realplay_economy.inc:75-80`, used at `realplay_scene.inc:144`); YR `rulesmd` NARADR=SpyPlane and AMRADR=AmerParaDrop, so both vanish. `w2-swoff.play`: `cameo(NARADR)=0` | WRONG | BLOCKER (with SW off) | Gate on the SW type's `DisableableFromShell=yes`, not on key presence; also ignore YR-only SW keys on classic buildings (NARADR SpyPlane, GATECH/NATECH ForceShield) | S | — |
| W2-AN-19 | Unsupported YR-only SW data leaks in through `rulesmd` | classic `rules.ini` `[SuperWeaponTypes]` has 7 entries; NARADR has no SW | the supported-type filter excludes SpyPlane and ForceShield while explicitly admitting the implemented Yuri powers | FIXED | MINOR | Keep unsupported types out of the roster and AI SW catalog; add YR types only with working effects | S | — |
| W2-AN-20 | No enemy SW warnings | `FUN_00445ee0` plays `EVA_NuclearSiloDetected`/`IronCurtainDetected`/`ChronosphereDetected`/`WeatherDeviceReady` when a non-player, non-allied house places SW type 0/1/2/3 (this audit); the dispatcher plays `*Activated`/`MissileLaunched`/`LightningStormCreated` on fire | `superweapon_eva` only tracks house 0's own ready/launch (`pres_game.inc:649-663`) | MISSING | MINOR | World event on SW-building placement and on fire (house, type); PresGame plays "detected" for enemies and "launched/activated" for any house; a minimap/radar event at the strike cell | S | W2-AN-04 |
| W2-AN-21 | AI never actually uses superweapons | AI builds NAIRON/NAMISL (`ai-base.md`), autofires when IQ allows (`FUN_004fd320`, G-66) | code exists (`world.cpp:2595-2636`) but the AI never reaches tech: soak 30 min Brutal gives `sw_ever_ready=0`, credits stuck at 174 from minute 21; the fire path handles the Nuke only | PARTIAL | MAJOR | Mostly AI economy (another area); here: per-type AI targeting for IronCurtain (own attack team), Chrono (team into the enemy base), Storm (as nuke); verify with `--ai-soak --minutes 60` | M | W2-AN-04 |
| W2-AN-22 | `--probe-realplay` is stale and aborts | — | pre-G-23 assumptions: G-20/G-22/G-26 rows FAIL; aborts on `stl_vector.h:1253 __n < this->size()` (Building index) before its G-24 naval section | BROKEN | MINOR (harness) | Deploy the MCV first (as `--ai-soak` does), bounds-check the building lookup; keep it as the naval regression check | S | — |
| W2-AN-23 | SW sidebar slot is NukeIcon-only, one slot | each granted SW shows its `SidebarImage` cameo (IRCRICON/CHROICON/BOLTICON/APARICON/PARAICON) with its own clock | `draw_sw_slot` blits `sb_nukeicon` for any type (`main.cpp:3740-3761`) | WRONG | MINOR | Load each SW's SidebarImage from cameo.mix; one slot per granted SW | S | W2-AN-04 |
| W2-AN-24 | Aircraft altitude ignored by the cursor hit test | the cursor tracks the raised sprite | `probe_cursor` uses the ground point (`pres_game.inc:247-250`), while `Input::unit_under` raises it (`input.cpp:129-130`): the cursor shows Select/Attack over the shadow, not over the Kirov | WRONG | MINOR | Apply `altitude_to_screen_dy` in `probe_cursor` | S | — |
| W2-AN-25 | Ships do not sink | `ShipSinkingWeight=3.0`: surface ships of that weight sink instead of exploding | no sinking path (grep) | MISSING | MINOR | Naval death: a sink animation (pitch plus descend) instead of the vehicle Explosion | S | — |
| W2-AN-26 | Jumpjet/air speed scaling unverified | `aircraft.md §5`: JumpjetSpeed 5 = 12 leptons/frame; whether GameSpeedBias 1.6 (ground) or an air bias applies to jumpjets is not hunted | `speed_to_cps` applies the ground `GameSpeedBias` to flight speed (`main.cpp:813`); measured about 1.1 cells/s for the Kirov | PARTIAL | MINOR | RE question Q3; switch to the air bias if one exists | S | Q3 |

## 3. Prioritized fix plan

1. **Stop the crash and unblock the navy (all S).**
   - W2-AN-03: skip 0x0 textures; find the empty NAMISL frame. Files: `engine/texture_cache.cpp`, the building draw in `engine/main.cpp`.
   - W2-AN-09: domain-aware `cell_walkable` and cursor. Files: `engine/input_original.cpp:59-64,134,181`, `engine/pres_game.inc:276-284`.
   - W2-AN-18 and W2-AN-19: `DisableableFromShell` gate plus a classic-SW whitelist. Files: `engine/realplay_economy.inc:72-80,185-195`, `engine/realplay_scene.inc:144`.
   - W2-AN-22: fix `--probe-realplay` so the naval rows run again.
2. **Make naval combat sane (S/M).**
   - Interim spawner neutralisation plus `MinimumRange` (W2-AN-12 first half). Files: `engine/weapon_resolve.cpp`, `engine/combat.cpp`.
   - Naval targeting keys (W2-AN-14). Files: `engine/unit_combat.hpp`, `engine/combat.cpp:60-73`, `engine/main.cpp:822-839`.
   - Sub cloak and sonar (W2-AN-13). Files: `engine/world.cpp` (step_combat), `engine/visibility.cpp`, the draw in `main.cpp`.
   - Amphibious domain (W2-AN-10). Files: `engine/world.hpp` (MovementDomain/`grid_for`), `world.cpp:840-848,1001-1035`.
3. **Superweapon framework v2 (L).**
   - Per-house per-type SW vector, sidebar cameos per type (W2-AN-04, W2-AN-23).
   - Timed nuke flight with fallout (W2-AN-05, W2-AN-06).
   - Effects: Iron Curtain, then Chronosphere/Warp, then Lightning Storm.
   - EVA events (W2-AN-20).
   - Hotspots: `engine/superweapon.hpp`, `engine/world.cpp:1419-1539`, `world.hpp:921-957,1586-1603`, `engine/realplay_economy.inc:170-223`, `engine/pres_game.inc:368-390,649-663`, `main.cpp:2203-2217,3740-3761`, `input.cpp:648-690`.
4. **Fixed-wing aircraft (L).**
   - W2-AN-02: put `roster2::Sortie` into World, add the Fly loco flag, spawn on a GAAIRC/AMRADR pad, then flip `with_aircraft` (W2-AN-01).
   - Then spawners on that state (W2-AN-12 second half: Hornet/ASW) and Paradrop planes (W2-AN-17).
   - Hotspots: `engine/roster2.{hpp,cpp}`, `world.cpp:792-833` (order_unit_move), `world.cpp:1309-1349` (step_flight), `realplay_scene.inc:96`, `main.cpp:787-820`.
5. **Jumpjet polish (M).**
   - Night Hawk landing and grounded-only load/unload (W2-AN-08).
   - Crash on death, Stop, takeoff on exit (W2-AN-07); cursor altitude (W2-AN-24).
   - Files: `world.cpp:1173-1229,1309-1349`, `unit_orders.cpp:17-26`, `pres_game.inc:247`.
6. **AI air and naval (M)** after 1-4 (W2-AN-16, W2-AN-21).
   - Files: `engine/ai_skirmish.inc:15-37`, `world.cpp:2595-2752`, the AI catalog in `main.cpp:2296-2311`.
7. **Fidelity tail (S/M).**
   - Yard repair (W2-AN-11), ship sinking (W2-AN-25), Dolphin and Squid art (W2-AN-15), air speed bias (W2-AN-26).

## 4. Open RE questions

1. **NAMISL empty frame.** Which frame or anim of `ngmisl.shp` / `NAMISL_E..H` is 0x0, and how does the original skip it? Probably `ShapeFileStruct` frames with w=0 are simply not drawn: confirm in the SHP blitter.
2. **Chrono Warp eligibility flags.** What are `TechnoType+0xD97` and `+0xCD4` in the case-4 eligibility test (teleport vs `ReceiveDamage(Strength)` with `Rules+0xFA8`)? Candidates: `Organic`/infantry, `Naval`, and the water-landing check. The answer pins which units the Chronosphere kills (`FUN_006cba10` around `0x6cbe..0x6cc248`).
3. **Jumpjet speed bias.** Do jumpjets use `GroundspeedBias` (GameSpeedBias x [Difficulty] Groundspeed) or an `Airspeed` bias? Trace `GetMaxSpeed` (obj vtbl `+0x538`) for a Jumpjet `UnitClass`.
4. **Iron Curtain call.** What is vtable `+0x154` called on each object in the 3x3 area (IronCurtain(duration, house, force))? Does it skip infantry (RA2: the curtain kills infantry)? Does `(flags & 4) && obj+0x27C` exclude in-limbo objects?
5. **Lightning Storm sequence.** Pin `FUN_00539be0` (start), `FUN_00539e10` (pre-fire hook) and the bolt scheduler against `[General] Lightning*`: is the direct-hit cadence `LightningHitDelay` relative to the cloud, and what does `LightningSeparation` mean in cells?
6. **Fixed-wing locomotor.** FlyLocomotionClass `{4A582746}` (registered in `FUN_006BB020`, `DAT_007E8A80`): climb/descent rate, cruise speed source, the attack-run geometry (overfly vs stand-off), and whether `FlightLevel=1500` is the cruise Z (`roster2.md` uses a 100 lep/frame RE-FALLBACK).
7. **Naval Yard repair.** The UnitRepair dock service: the cadence and cost per step for ships (`FUN_0044B780` UnitRepair branch vs ReloadRate), and which dock cell ships use (`DockingOffset0=384,-128,0`).
8. **Sub cloak.** Submerge/cloak loop `{4A582741}` (`FUN_00740780`): the CloakDelay timer reset on fire, sonar reveal radius (SensorsSight cells vs leptons), and whether surfaced subs are targetable by everything.
9. **Dolphin and Squid art.** Classic RA2 ships `dlph`/`sqd` art; find which MIX and name the loader misses (voxel or SHP) on this install.
10. **SW sidebar placement.** In classic RA2, are SW cameos inserted into a specific strip (tab) and in which order relative to the building cameos? This affects the UI only.

## 5. Fix status (wave-2 fix, branch `w2fix-air`)

Acceptance is real play: `scripts/play/naval.play`, `air.play`, `sw.play` (all PASS),
the fixed `--probe-realplay` (0 fails) and `--ai-soak --map xbayopigs.map --cash 50000
--diff brutal --minutes 30`. Save format v10.

| ID | Status | Fix | Evidence |
|---|---|---|---|
| W2-AN-01 | FIXED | `with_aircraft=true`; ORCA/BEAG in the Aircraft queue (AMRADR/GAAIRC) | `air.play` L37-L44 `cameo(ORCA)=1`, built onto a pad |
| W2-AN-02 | FIXED | FlyLocomotion CLSID -> `AnType::fixed_wing`; `World::an_step_fixed_wing` sortie (pad, takeoff to FlightLevel, attack pass, Ammo--, RTB, land, +1 ammo per `ReloadRate` 270 frames); AirportBound crash with no airfield | `air.play` L47-L58 (airborne, tank hit, ammo 0, docked, ammo 1) |
| W2-AN-03 | FIXED | `TextureCache::get/get_rgba` return nullptr for 0-dim frames | auditor `w2-swbld.play` PASS (exit 0), `sw.play` renders NAMISL |
| W2-AN-04 | FIXED | one SW record per type per house (`sw_houses_[h]` list), Iron Curtain (3x3, 750 frames, organic killed [RE-FALLBACK]), Chronosphere two-pick + chrono kill (Organic && !Teleporter, RE'd `TechnoType+0xD97/+0xCD4`), Lightning Storm (deferment 250, 180 frames of bolts) | `sw.play` (iron/chrono/storm sections) |
| W2-AN-05 | FIXED | nuke = `SwStrike`: 200 frames up (GiantNukeUp, Acceleration 1, DetonationAltitude 20000) + 200 frames down (RE: `FUN_006cba10` spawns the down missile 20000 lept above the target at -100 lept/frame, `DAT_007eca88=-100.0`); FX/EVA from the World event feed (AI launches show too) | `sw.play` L80-L86 (launched, still in flight at +10, detonated later) |
| W2-AN-06 | FIXED | blast hits every house and airborne units; `[NukePayload] RadLevel=500` RadSite | `sw.play` L86-L87 (`hp_lost(1)`, `rad_total>0`); unit tests updated |
| W2-AN-07 | FIXED | jumpjets lift off on exit; Stop drops the cruise goal; Crashable / fixed-wing deaths fall and explode on impact [RE-FALLBACK impact damage = Strength/4] | `air.play` sections 3-4 |
| W2-AN-08 | FIXED | non-BalloonHover Landable jumpjets land at the goal (nearest free cell); board/unload only when grounded (unload order lands first) | `air.play` section 5 |
| W2-AN-09 | FIXED | `World::selection_can_enter` (per movement domain) for clicks and the cursor; per-domain group formation | `naval.play` section 1; `--probe-realplay` W2-AN-09 rows |
| W2-AN-10 | FIXED | `MovementDomain::Amphibious` (land OR water), yard launch, beaching, unload only on land | `naval.play` section 3 |
| W2-AN-11 | FIXED [RE-FALLBACK cost] | UnitRepair yard heals a stopped ship within 1 cell, `RepairStep` per `RepairRate`; not charged | `naval.play` section 4 |
| W2-AN-12 | FIXED | Spawner weapons: Carrier Hornets, Destroyer ASW, Dreadnought/V3 missiles (regen/reload, volley, DMisl pause+tilt), `MinimumRange`, `CanPassiveAquire=no`; ASWLauncher `Range=-2` = infinite | `naval.play` sections 2 and 5 |
| W2-AN-13 | FIXED [RE-FALLBACK shimmer] | submerged = Underwater+Cloakable unless it fired within CloakDelay (18 frames); Sensors/SensorsSight sonar mask; hidden subs untargetable, undrawn, ignored by defenses | `naval.play` section 2 |
| W2-AN-14 | FIXED | `an_naval_slot`: the classic NavalTargeting 0..6 / LandTargeting table (rules.ini comment block) | unit + play coverage (sub never targets land; ASW secondary vs submerged) |
| W2-AN-15 | FIXED | RE: artmd `[DLPH]`/`[SQD]` are `Voxel=no` SHP ships (WalkFrames/FiringFrames) -- loaded from conquer.mix; SQD's ParasitePlus grab not modelled | `naval.play` section 6 |
| W2-AN-16 | FIXED | AI: `[AI] BuildNavalYard` + `AINavalYardAdjacency=20` water placement (node dropped when no water), ships + Kirov/Night Hawk/Rocketeer in the AI roster, ships launch at the yard, jumpjets recruit | soak: NAYARD built, 4 ships + 2 Kirovs |
| W2-AN-17 | PARTIAL | American paratroopers (PDPLANE run, 8 E1 dropped within ParadropRadius); generic ParaDrop effect works but CAAIRP (Tech Airport) is not in the building roster | `sw.play` paradrop section |
| W2-AN-18 | FIXED | lobby filter = classic SW with `DisableableFromShell=yes` | auditor `w2-swoff.play` PASS (`cameo(NARADR)=1`) |
| W2-AN-19 | FIXED | supported SW filter (`rp_supported_sw_type`); Yuri powers added with effects | SpyPlane/ForceShield remain excluded |
| W2-AN-20 | FIXED | SW event feed: enemy grant -> *Detected/WeatherDeviceReady; Launched/Activated for every house; own Ready | `sw.play` events |
| W2-AN-21 | FIXED | AI fires every charged type (nuke/storm/paradrop at the best enemy target; Iron Curtain on its densest vehicle 3x3; Chronosphere that cluster into the enemy base) [RE-FALLBACK pickers]; tech/SW/yard planned after the refineries | soak: NATECH/NAMISL/NAIRON, 3 launches |
| W2-AN-22 | FIXED | `--probe-realplay` deploys the MCV, grows the old bases, checks a Destroyer sails | 0 fails |
| W2-AN-23 | FIXED | per-type SidebarImage cameos, one slot each (2-wide grid in the radar area) | `sw.play` `sw_slots.png` |
| W2-AN-24 | FIXED | cursor hit test raises airborne units | code (pres_game.inc) |
| W2-AN-25 | FIXED | ShipSinkingWeight ships slide under instead of exploding | `naval.play` `sunk(1)>=1` |
| W2-AN-26 | OPEN [RE-FALLBACK] | jumpjet/air speed keeps the ground GameSpeedBias (Q3 not traced) | -- |

RE answers: Q1 (0-dim frames are skipped), Q2 (0xD97 = Organic, 0xCD4 = Teleporter, by a
byte scan of the LoadFromINI stores), Q9 (Voxel=no SHP ships). Q3/Q4/Q5/Q6/Q7/Q8/Q10
remain [RE-FALLBACK] as marked in the code.

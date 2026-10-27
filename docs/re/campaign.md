# Campaign missions (RA2 Allied + Soviet) — reverse-engineering notes

(Method: `docs/re/workflow.md`; YR 1.001 `gamemd.exe` (SHA1 `7BE4E550…B6C7`),
image base `0x00400000`. Hunt scripts `p10_c1.py` … `p10_c14.py` +
`p10_common.py` under `D:\ra2-re\scripts\` (off-repo), decompiles/listings under
`D:\ra2-re\p10out\` against a **private copy** of the analysed project
(`D:\ra2-re\p10proj`). Nothing under `D:\ra2-re` is committed. Every function
below was located from **content**: the INI key/section strings the loaders
consume (`"MAPSELMD.INI"` `0x82F380`, `"NextScenario"` `0x83D194`,
`"BATTLEMD.INI"` `0x8251A8`, `"MISSIONMD.INI"` `0x83871C`, `"LSLoadBriefing"`
`0x83CC10`, `"TXT_SCENARIO_WON"` `0x823C6C`, the object-section names reached
through the pointer table at `0x7E2E88`/`0x7F4C38`/`0x7E9EF8`/`0x7E1248`),
debug source paths (`"D:\Ra2 Addon\MapSel.CPP"`, `Scenario.CPP`, `House.CPP`)
and then caller walks. YR's `gamemd.exe` runs its own campaign through exactly
this code with the `*MD.INI` file names; RA2 `game.exe` carries the same strings
without the `MD` suffix (`BATTLE.INI`, `MISSION.INI`, `MAPSEL.INI` — checked by
string scan), which is why the RA2 data files are consumed with the YR logic.)

Confidence labels as in `map-triggers.md`: **CONFIRMED** (decompile/disassembly +
real data agree), **PROBABLE** (structure confirmed, a name taken from YRpp /
ModEnc), **[RE-FALLBACK]** (not determined; what we do instead).

---

## §1 The campaign data on this install (CONFIRMED — extracted + parsed)

| file | where | role |
|---|---|---|
| `battle.ini` | `ra2.mix/local.mix` | `[Battles]` list → per-battle `Scenario=` (first map), `Description=` (CSF), `CD=`, `FinalMovie=`, `DebugOnly=` |
| `mission.ini` | `ra2.mix/local.mix` | per-map section `[ALL01T.MAP]`: `Briefing=` / `UIName=` / `LSLoadMessage=` / `LSLoadBriefing=` (CSF labels), `LS640/800BkgdName=` (loading-screen SHP), `LS*BriefLocX/Y` |
| `mapsel.ini` | `ra2.mix/local.mix` | progression: per-side stage list (`[GDI]` = Allied, `[Nod]` = Soviet) + one section per stage (`Scenario=`, numbered next-stage keys) |
| `rules.ini` | `expand01.mix` (patch, wins) / `ra2.mix/local.mix` | **the RA2 `[Countries]` / `[BuildingTypes]` index spaces** the maps were authored against (§6) |
| `all01t…all12s.map` | `Maps01.mix` | Allied 1–12 (+ `trn01t/trn02t`, `e31/e32`) |
| `sov01t…sov12s.map` | `MAPS02.MIX` | Soviet 1–12 |
| campaign movies | `MOVIES01.MIX` / `MOVIES02.MIX` | **both 4-byte stubs on this install** — no Bink/VQA campaign movie is present (§8) |

`battle.ini [Battles]`: `1=TUT1 2=ALL1 3=SOV1` are the shipping campaigns (the
rest are `DebugOnly=yes` jump-in entries). `[ALL1] Scenario=ALL01t.MAP,
Description=DESC:ALL1`; `[SOV1] Scenario=SOV01t.MAP`. The YR data
(`battlemd.ini`/`missionmd.ini`/`mapselmd.ini` in `ra2md.mix/localmd.mix`) has
the same shape for the `*md.map` missions; P10 scope is the RA2 set.

Theaters (map `[Map] Theater=`): Allied 1 `all01t` = **URBAN**, Soviet 1 `sov01t`
= **URBAN** (the `t/s/u` suffix is *not* the theater); the 24 missions are
URBAN ×9, SNOW ×8, TEMPERATE ×7 — all three are in the engine's theater chain.

## §2 Located functions (this build)

| Function | Role | How found |
|---|---|---|
| `FUN_0046cd30` / `FUN_0046cbf0` | `[Battles]` list reader / per-battle ReadINI (`Scenario`, `FinalMovie`, `Description`, `DebugOnly`, `CD`) | only users of `"Battles"`/`"FinalMovie"`/`"DebugOnly"` |
| `FUN_0052c8d0` | loads `BATTLEMD.INI` + every `BATTLEMD*.INI` | `"BATTLEMD*.INI"` user |
| `FUN_00686560` | scenario INI load: `MISSIONMD.INI [<map>]` briefing/LS keys, `[Basic] Player=` | `"MISSIONMD.INI"`, `"LSLoadBriefing"` user |
| `FUN_00689890` | scenario `[Basic]`/`[Header]` ReadINI | only reader of `"NextScenario"`+`"EndOfGame"`+`"SkipScore"` (`FUN_0068a770` is the writer) |
| `FUN_005ce910` | MapSel loader: `MAPSELMD.INI`, `[<SideName>]` stages `1..100`, `Anims`/`Sounds` | `"MAPSELMD.INI"` user; logs `"MapSelect: Failed to create stage"` |
| `FUN_005cf300` | stage ReadINI (`Scenario`, `Description`, `VoiceOver`, … , numbered next-stage keys) | stage ctor called from `FUN_005ce910` |
| `FUN_005cf180` / `FUN_005cf230` / `FUN_005cf0d0` / `FUN_005cf250` | stage find by **scenario file name** / by index / by stage name; stage → number | callees of the two selectors |
| `FUN_005ad740` → `FUN_005adb30` → `FUN_005adc50` | **map-select screen** path (`SkipMapSelect=no`): locate the current stage by scenario name, show its next-stage choices, take the chosen stage's `Scenario=` | `"MapSelect: Stage = %s"` / `"Selected %s"` |
| `FUN_005ad850` → `FUN_005ad950` | **automatic** path (`SkipMapSelect=yes`): current stage by stored stage number, first valid next stage | `"MapSelect: Side = %s"` |
| `FUN_00686140` | Scenario name setter (Scenario `+0x125C`) | callee of both selectors |
| `FUN_004fc800` / `FUN_004fc9f0` | `HouseClass::Flag_To_Win` / `Flag_To_Lose` | `"TXT_SCENARIO_WON"` / `"TXT_SCENARIO_LOST"` users |
| `FUN_004f8350` | `HouseClass::AI` — the BorrowedTime expiry → PlayerWins / PlayerLoses flags | reads `+0x1F7`/`+0x298`; writes `DAT_00A82CB1` / `DAT_00A8DC38` |
| `FUN_0055cc60` | main-loop game-over dispatch → `Do_Win` / `Do_Lose` | only reader of both flags; caller `FUN_0048cbd0` (main loop) |
| `FUN_006850b0` | **`Do_Win`** (campaign): score, PostScore/PreMapSelect, EndOfGame, next scenario | caller `FUN_0055cc60`; reads `[Basic]` fields `+0x34AE/+0x34AF/+0x34AC/+0x34B0` |
| `FUN_00685800` | `Do_Lose` | `FUN_0055cc60` |
| `FUN_0044f810` | `[Structures]` reader | user of the `"Structures"` pointer `0x7E2E88` |
| `FUN_00742080` | `[Units]` reader | user of the `"Units"` pointer `0x7F4C38` |
| `FUN_0051f810` | `[Infantry]` reader | user of the `"Infantry"` pointer `0x7E9EF8` |
| `FUN_0041af80` | `[Aircraft]` **writer** (format string `"%s,%s,%d,%d,%d,%d,%s,%s,%d,%d,%d,%d"`) | user of the `"Aircraft"` pointer `0x7E1248` |
| `FUN_006e0010` / `FUN_006e00d0` / `FUN_006e15c0` / `FUN_006dfa00` | TAction ChangeHouse(14) / AllChangeHouse(36) / DestroyAttachedObject(32) / Apply100Damage(63) | `TActionClass::Execute` (`FUN_006dce20`) case targets |
| `FUN_004f9990` / `FUN_004f9db0` | `HouseClass::MakeAlly` / `MakeEnemy` | Execute cases `0x25` / `0x26` |
| jump table `0x6E94E4` (65 entries, `JMP [EAX*4+0x6E94E4]` at `0x6E89CB`, `CMP EAX,0x40`) | **TeamClass script-action executor** (§7) | found by searching for the code address of the `Flag_To_Win` call site `0x6E8E39` |

## §3 Campaign start + progression (CONFIRMED)

1. **Start.** The battle list (`battle.ini [Battles]`) gives the first map of
   each campaign: `ALL1 → ALL01t.MAP`, `SOV1 → SOV01t.MAP`.
2. **Briefing/loading data** comes from `mission.ini [<MAPNAME>]`, the section
   name being the scenario file name (`FUN_00686560`, `+0x497` is the section):
   `Briefing` (CSF label → `+0x714`), `UIName` (`+0x13BA`), `LSLoadMessage`,
   `LSLoadBriefing` (`+0x364B`), `LS640/800BriefLocX/Y`, `LS640/800BkgdName`,
   (YR only: `LS800BkgdPal`). The map's own `[Basic] Briefing=` carries the same
   label and is the fallback.
3. **Player house.** `[Basic] Player=` (default `"Americans"`), resolved as a
   house name, then the player's **Side** = `HouseType.SideIndex`
   (`FUN_00686560` lines writing `Scenario+0xD2E*4`). For the RA2 maps it is
   `Player House` → `Country=Player` → `[Player] Side=GDI` (Allied) / `Nod`
   (Soviet).
4. **Mission end** (§5) runs `Do_Win` (`FUN_006850b0`) in campaign mode
   (`Session.GameMode == 0`):
   - `SkipScore` (`+0x34AE`) == no → the score screen;
   - `PostScore` / `PreMapSelect` movies when set (`!= -1`);
   - if the campaign is not continuing (`FUN_0049f6b0` false) **or**
     `OneTimeOnly` (`+0x34AF`) → back to the menu;
   - `EndOfGame` (`+0x34AC`) → the campaign's `FinalMovie` then credits → menu;
   - else **next scenario** via MapSel: `SkipMapSelect=no` → `FUN_005ad740`
     (the select screen), `yes` → `FUN_005ad850` (automatic). Then
     `++Scenario+0x1254` (missions-played counter) and the next scenario starts.
5. **Next scenario (MapSel).** The side's section is the **side name**
   (`Sides[HouseType(player).SideIndex].Name` → `GDI` / `Nod`); its numbered keys
   `1..100` list the stages (`FUN_005ce910`). The current stage is found by
   **matching its `Scenario=` against the current scenario file name**
   (`FUN_005cf180`, case-insensitive string compare); the stage's numbered keys
   (`2=ALL02` — "index color is the # above, that's how it chooses the next
   map") name the next stage(s); the chosen stage's `Scenario=` becomes the new
   scenario name (`FUN_00686140`). Every RA2 stage lists exactly **one** next
   stage, so the choice is forced (the select screen is a one-click formality):
   `ALL01→ALL02→…→ALL12`, `Sov01→…→Sov12`. **The map's `[Basic]
   NextScenario=`/`AltNextScenario=` are read (`+0x4`, `+0x108`) but are TS
   leftovers (`GDI2A.map`/`GDI9C.MAP` on every RA2 map) and are not what picks
   the next mission.**
6. `EndOfGame=yes` on `all12s` / `sov12s` (and `e32`) ends each campaign.

## §4 Pre-placed objects (CONFIRMED field order from the readers)

Common head `OWNER,TYPE,HEALTH,X,Y` (`OWNER` = a `[Houses]` name; HEALTH in
256ths). Cell packing: NewINIFormat ≥ 4 → separate `X,Y` fields (else
`v%128, v/128`). Strength = `ftol(TypeStrength × HEALTH / 256)`; if it is
`> Strength−3` it snaps to full `Strength`; floor 1 (buildings: HEALTH clamped
to 256 first). An object whose `Unlimbo` fails is deleted (never placed
elsewhere).

| section | fields after the head (reader) |
|---|---|
| `[Infantry]` (`FUN_0051f810`) | `SubCell, Mission, Facing, Tag, Veterancy, Group, OnBridge, AutocreateNo, AutocreateYes` |
| `[Units]` (`FUN_00742080`) | `Facing, Mission, Tag, Veterancy, Group, OnBridge, FollowsIndex, AutocreateNo, AutocreateYes` |
| `[Structures]` (`FUN_0044f810`) | `Facing, Tag, AISellable, AIRebuildable, (unused), Powered, (upgrade count), Upgrade1..3, AIRepairable, Nominal` |
| `[Aircraft]` (writer `FUN_0041af80`) | `Facing, Mission, Tag, Veterancy, Group, AutocreateNo, AutocreateYes` (12 fields) |

`Tag` = a TagType id or `None`; a non-`None` tag is resolved with
`FUN_006E53E0` (find TagType) → `FUN_006E4810` (**`TagClass::GetInstance`** —
one shared TagClass per TagType) → `FUN_005F5360` (attach, which increments the
tag's InstanceCount). In multiplayer modes `[Structures]` of the *player* house
are skipped; in campaign (`GameMode == 0`) everything is placed. Mission:
campaign objects get their INI mission (`vtbl+0x1E8`).

## §5 Win / lose → mission end (CONFIRMED)

- `Flag_To_Win(house)`: only once per house (`+0x1F7` IsWinner, `+0x1F6`,
  `+0x1F8` IsLoser guards); sets IsWinner; **BorrowedTime** timer
  `{start = Frame, left = ftol(Rules[+0x14C8] × 900.0)}` — `Rules+0x14C8` is
  `[General] SavourDelay` (`.1` min on this install → **90 frames**). For the
  player house in campaign: the end message (`TXT_SCENARIO_WON` "Mission
  Accomplished") is posted, the theme stops.
- `Flag_To_Lose`: symmetric (`+0x1F8`, `TXT_SCENARIO_LOST`).
- `HouseClass::AI` (`FUN_004f8350`): once the house's BorrowedTime has run out
  (and, campaign, the house is the human player), clears the flag and sets
  `PlayerWins` (`DAT_00A82CB1`) / `PlayerLoses` (`DAT_00A8DC38`); the main loop
  (`FUN_0055cc60`) then runs `Do_Win` / `Do_Lose`.
- Trigger actions: `1 Win` / `2 Lose` are player-relative (map-triggers.md §5)
  and land in `Flag_To_Win/Lose`; script actions `7` / `23` (§7) call them on
  the player house directly.

## §6 Index spaces for RA2 maps (CONFIRMED by data)

Trigger house values index HouseTypes = **rules `[Countries]` then the map's
new countries appended** (map-triggers.md §6). The RA2 maps were authored
against **RA2 `rules.ini`**, whose `[Countries]` has 13 entries
(`0=Americans … 8=Russians, 9=GDI, 10=Nod, 11=Neutral, 12=Special`) — YR's
`rulesmd.ini` inserts `9=YuriCountry`, shifting everything after by one.
Decisive check on `all01t`: map `[Countries] 0=Player 1=BadGuy1 …` → under
RA2 rules `Player=13`, and the victory trigger's action is `1 Win, Value=13`;
the failure trigger is `2 Lose, Value=13`; under the YR list 13 would be
`Neutral`. **Campaign mode therefore builds the trigger index spaces from the
RA2 `rules.ini` lists** (patch `expand01.mix` copy first), not `rulesmd.ini`.

## §7 Team script actions (CONFIRMED anchors, ModEnc names)

The executor dispatches `action` directly through the 65-entry table at
`0x6E94E4` (`action > 0x40` → no-op). Anchors read from the handlers:

| # | handler evidence | name |
|---:|---|---|
| 0 | `FUN_006ec5f0` | Attack (quarry type = arg) |
| 1 | `FUN_006ebf00` | Attack waypoint |
| 3 | `FUN_006ebd30` | **Move to waypoint** (arg = waypoint index) |
| 4 | splits arg `x = arg % 128, y = arg / 128` → `FUN_005653d0` | Move to cell |
| 5 | timer set + `FUN_006eadd0` | Guard area (timer) |
| 6 | `arg − 2` → `FUN_00690fa0` (script position) | Jump to line (arg is **1-based**: the cursor is set to arg−2 and then advanced) |
| 7 | `Flag_To_Win(Player)` | Player wins |
| 8 | `FUN_006ee670` | Unload |
| 11 | `FUN_006ecd40` | Do mission (arg = mission) |
| 12 | `PUSH 1` → set global (`FUN_00689070`) | Set global |
| 16 | `FUN_006ec240` | Patrol to waypoint |
| 23 | `Flag_To_Lose(Player)` | Player loses |
| 38 | `PUSH 0` → `FUN_00689070` | Clear global |
| 39 / 40 | `PUSH 1` / `PUSH 0` → set local (`FUN_00689310`) | Set / clear local |
| 46 / 47 | `FUN_006ed870` / `FUN_006ed950` | Attack enemy building / move to enemy building |

These match the ModEnc ScriptActions numbering (`20` Change house, `21`
Scatter, `37` Delete team members, `49` Success, `53/54` gather at enemy/base…)
at every anchor read. **Note:** `ai-team.md` §3 maps the stock-`aimd.ini`
actions differently (e.g. "9 = move to waypoint"); that table is AI-2's and is
gate-locked — campaign map teams use the anchors above through their own
interpreter, AI-2 is not touched (flagged for a future AI-team re-hunt).

Census over the 24 RA2 missions (action: uses): 0:957, 16:699, 3:632, 5:338,
46:331, 49:329, 11:175, 6:148, 8:126, 39:80, 37:60, 14:59, 1:49, 20:42, 42:42,
53:40, 54:39, 21:28, 47:26, 50:24, 9:22, 19:15, 55:14, others ≤ 6.

## §8 Movies, audio

`Intro=`/`Brief=`/`Win=`/`Lose=` name movies (`A01_f00e`, `S01_f00e`, …).
`MOVIES01.MIX`/`MOVIES02.MIX`/`MOVMD03.MIX` are 4-byte stubs on this install,
so **no campaign movie can be played** (the Bink decoder is there; the data is
not). Briefing *speech* in RA2 is part of those movies; there is no separate
briefing audio file in `mission.ini`.

## §9 What we take as fall-back

1. **Score screen** — the score tally layout is not recovered; a mission-result
   screen (won/lost, time, units/buildings killed/lost from the engine's own
   counters) stands in. [RE-FALLBACK]
2. **Map-select screen** — the stage choice is forced on every RA2 stage
   (§3.5), so the next mission is taken directly (no globe/VQA screen).
3. **TeamClass delivery** — W4-FIX-SEQ recovered `Reinforcement`
   (`FUN_0065d320` -> `FUN_0065d770` create, `FUN_0065da50` place) [CONFIRMED]:
   * the members are created and added to the team; VeteranLevel 0/2/3 sets
     -0.25 / 1.0 / 2.0 veterancy (`FUN_0074ee80/eeb0/eed0`);
   * `Full=yes` (TeamType +0xA5): every non-transport member goes INSIDE the
     last-created transport (`FUN_004732d0`, no capacity test). A transport is
     a member type with Passengers>0 (+0x5E0) that is a UnitClass, or an
     AircraftClass when the TaskForce holds an AircraftType with Passengers
     (`FUN_006ee9d0`); when any TaskForce type is Naval (+0xCCE) only Naval
     types count;
   * `Droppod=yes` (+0xB0): the AircraftType named at 0x838700 (PDPLANE) is
     created for the house, unlimboed flying at the TransportWaypoint
     (`UseTransportOrigin` +0xB1, `FUN_006f0e40`) or on the house Edge
     (+0x1E0 -> `FUN_004aa350`), takes every member aboard and flies Mission
     ParadropApproach (0x1A) to the reinforcement waypoint;
   * otherwise the objects are unlimboed at the waypoint (8-neighbour retries);
     an all-infantry group next to a building exits from it (KickOutUnit).
   The clone follows this; RE-FALLBACKs: the edge cell (straight off the drop
   point on screen instead of a random edge cell), ParachuteMaxFallRate fall.
   The jumps follow the plane's paradrop missions (W8-05-07, see
   aircraft.md §Paradrop missions).
   `CreateTeam` recruits the house's idle on-map units of the TaskForce types;
   a computer house's AI production also trains the missing ones
   [RE-FALLBACK, RE-W4-SQ-2].
4. **Script actions** — W4-FIX-SEQ: 8 Unload (`FUN_006ee670`) orders every
   member transport to unload and completes when all are empty; its arg
   decides who stays in the team (0 all, 1 transports, 2 unloaded units, 3
   none; TransportsReturnOnUnload +0xF4 sends transports home); 14 Load
   (`FUN_006ec760`) boards the members into the first non-full transport; 43
   WaitLoaded (@0x6E9192) waits until every member's NumPassengers reaches its
   Passengers; 46/47/58 use the BuildingTypes arg + selector
   (`FUN_006ee130`). Iron curtain, chrono, talk bubbles stay skipped.
   [CONFIRMED / RE-FALLBACK where noted in the code]
5. **Unit stats** come from `rulesmd.ini` (the engine's single rules source);
   the RA2 `rules.ini` is used only for the trigger index spaces (§6).
   [DEVIATION]
6. **Apply100Damage** (63) — five `C4Warhead` explosions of 100 around the
   waypoint (`FUN_006dfa00`): applied as 100 damage to every object on the
   waypoint cell [DEVIATION: no splash]; then the bridge destroy dispatch
   `FUN_00586bb0` on the waypoint cell up to 4 times (no roll) [CONFIRMED,
   W4-FIX-SEQ: low section step or the whole high span].

7. **Attack-waypoint bombardments** (script action 1) — Allied 1's
   dreadnoughts open on the Statue of Liberty (waypoint 0, `CANEWY04`, the
   tagged "critical structure" whose death enables `Mission Failed`). In the
   original the missiles are intercepted by the Patriot sites (`NASAM`) placed
   in their path; the engine models no missile interception, and executing the
   order literally fails the mission ~15 s in. The team holds position instead.
   [DEVIATION]
8. **Alliances** — RA2 `Allies=` lists are one-directional
   (HouseClass::IsAlliedWith reads the house's own bits). Team 0 = the player +
   PlayerControl houses + houses *mutually* allied with it (allied both ways);
   a civilian-side house the player lists as an ally is a neutral
   non-combatant. W4-FIX-SEQ: every other pair uses the house's OWN list
   (one-sided matrix), so enemy houses that do not list each other fight
   (SOV09's Russians vs Americans). MakeAlly/MakeEnemy set the pair both ways.
   [PROBABLE]
9. **House credits** — `[<House>] Credits=` × 100 (the C&C `HouseClass::Read_INI`
   lineage; not traced in this build). [PROBABLE]
10. **Loading-screen palette** — `ra2.mix/load.mix/ldscrna.pal` (Allied) /
    `ldscrns.pal` (Soviet), the `LDSCRN%c.PAL` names in `game.exe` next to
    `LDSCRN.PAL`; the selector was not traced. [PROBABLE]
11. **Destroyed counters (events 15/16)** count the trigger house's own
    *losses*. [RE-FALLBACK]
12. **Object lines** — facings/sub-cells/veterancy are not applied (units
    spawn facing south on a free cell near their INI cell) [CUT];
    Mission=Sleep/Harmless (W4-FIX-SEQ) keep the object from acquiring or
    retaliating until a Wakeup action (81/82/83).

## §10 Confidence summary

| claim | evidence | confidence |
|---|---|---|
| battle/mission/mapsel roles + keys | loaders `FUN_0046cbf0`, `FUN_00686560`, `FUN_005ce910/005cf300` + extracted files | **CONFIRMED** |
| next mission = MapSel stage chain matched by scenario name, not `NextScenario` | `FUN_005adb30/005cf180`, data (`NextScenario=GDI2A.map` everywhere) | **CONFIRMED** |
| object field orders | the three readers + the aircraft writer | **CONFIRMED** |
| strength rule (`> Strength−3` snaps to full, floor 1) | readers | **CONFIRMED** |
| BorrowedTime = SavourDelay × 900 frames → PlayerWins/Loses → Do_Win/Do_Lose | `FUN_004fc800`, `FUN_004f8350`, `FUN_0055cc60` | **CONFIRMED** (`Rules+0x14C8` = SavourDelay: PROBABLE by value/name) |
| RA2 `[Countries]` index space | `all01t` `Win 13` / `Lose 13` | **CONFIRMED** |
| ChangeHouse = every techno whose tag holds the firing trigger | `FUN_006e0010` (`FUN_006e48f0(trigger)`) | **CONFIRMED** |
| MakeAlly/MakeEnemy are mutual | Execute `0x25/0x26` call both directions | **CONFIRMED** |
| script action numbering | jump table `0x6E94E4` anchors | **CONFIRMED** at the anchors, PROBABLE (ModEnc) elsewhere |
| team delivery, score layout | — | **[RE-FALLBACK]** |

## §11 Portable campaign progress (engine behavior)

After a won campaign mission with a next mission, the engine stores the next
map, campaign side, and difficulty in the device-local `Saved Games/CAMPAIGN.PROGRESS`
file. Android's launcher can import or export this small file through the
Storage Access Framework. The startup resume page uses it if present; choosing
to forget campaign progress removes it. Interrupted in-mission world state
remains separate in `AUTOSAVE.SAV` and is not included in this transfer.

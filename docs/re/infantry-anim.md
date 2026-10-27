# Infantry animation sequences & facing — original-game calibration

Recovered 2026-07-10, after live play exposed two fidelity bugs in the Combat
Phase's **empirically eyeballed** animation table (engine/world.cpp Task 7).
This doc replaces that guesswork as the calibration authority for infantry
frame selection. Method: original game DATA (artmd.ini extracted from the
install's `ra2md.mix/localmd.mix` via `ra2extract --raw`) + structure
cross-check against the YRpp reference headers (read off-repo at
`D:\ra2-re\reference\YRpp`; prose conclusions only, no GPL code copied) +
direct inspection of the SHP art itself where the data file is the wrong
authority (facing block order).

## 1. Sequence data model (HIGH)

Infantry art sections name a sequence section: `[GI] Sequence=GISequence`,
`[CONS] Sequence=ConSequence` (artmd.ini). Each sequence entry is

```
Name=StartFrame,FrameCount,FacingMultiplier[,Facing/Sound extras]
```

confirmed structurally by YRpp `SubSequenceStruct { StartFrame; CountFrames;
FacingMultiplier; Facing; ...sounds }` (InfantryTypeClass.h). Frame selection:

```
frame = StartFrame + facing_index * FacingMultiplier + phase   (phase < FrameCount)
```

`FacingMultiplier=0` ⇒ facing-independent (deaths, idles). Extra trailing
fields exist (e.g. `Idle1=56,15,0,S` — the `S` labels the fixed facing that
anim renders at) and MUST be tolerated by a parser reading the 3 ints.

## 2. Ground truth for this slice's units (HIGH — read from the real artmd.ini)

`[GISequence]` and `[ConSequence]` are IDENTICAL for every entry we consume:

| Entry  | Value      | Frames covered                      |
|--------|-----------|--------------------------------------|
| Ready  | `0,1,1`   | 0..7 (1 frame × 8 facings)           |
| Walk   | `8,6,6`   | 8..55 (6 × 8)                        |
| FireUp | `164,6,6` | 164..211 (6 × 8)                     |
| Die1   | `134,15,0`| 134..148 (15, no facing)             |
| Die2   | `149,15,0`| 149..163 (15, no facing)             |
| Prone  | `86,1,6` / Crawl `86,6,6` / Idle2 `71,15,0,E` | (context for §3) |

## 3. The two Task-7 defects this data exposes

- **CONS fire was hardcoded 158..163** — those are the LAST SIX FRAMES OF
  DIE2 (149..163). Every Conscript shot played half a death animation
  ("soldier keeps falling before dying" — user bug report, confirmed).
  Real FireUp is 164, same as GI.
- **Die was hardcoded 80..87 for both units** — frames 80..85 are the tail
  of Idle2 (71..85) and 86..87 the head of the Prone/Crawl block. Real
  deaths are Die1=134..148 (15 frames). At the game's 15 Hz anim rate that
  is exactly 1.0 s — independently validating combat.cpp's kDieAnimSecs=1.0.
- GI FireUp=164..169 happened to be correct (luck, not method).

Lesson recorded as standing policy: original data first, no eyeball-derived
frame tables.

## 4. Facing block order (HIGH — from the art itself)

The art, not the ini, defines which block index faces which way. Dumped the
first frame of each of GI FireUp's 8 blocks (164+6k, k=0..7) and read the
muzzle-flash direction (unambiguous in all 8; see scratch contact sheet,
reproducible via `ra2extract ... <frame>`):

| k | frame | flash points | screen dir |
|---|-------|--------------|------------|
| 0 | 164   | straight up (back view)   | N  |
| 1 | 170   | up-left      | NW |
| 2 | 176   | left         | W  |
| 3 | 182   | down-left    | SW |
| 4 | 188   | down (frontal view)       | S  |
| 5 | 194   | down-right   | SE |
| 6 | 200   | right        | E  |
| 7 | 206   | up-right     | NE |

**Blocks run COUNTERCLOCKWISE on screen from North.** Corroboration: the
artmd-labeled `Idle1=56,15,0,S` frame (56) is frontal exactly like block
k=4; YRpp's `SequenceFacing {N=0, NE=1, E=2 ...}` enum is a CLOCKWISE label
ordering used for those per-entry labels — it is NOT the frame block order.
A clockwise assumption would mirror-flip every aim except N/S.

## 5. Mapping our cell axes to facing k (HIGH — pure geometry given §4)

Our iso projection: screen (sx,sy) ∝ (x−y, x+y). So a cell-space direction
(dx,dy) has screen angle `atan2(dx+dy, dx−dy)` (y-down), and

```
k = round( ((−90° − screen_angle) mod 360°) / 45° ) mod 8
```

Worked 8-way table (cell dir → k): (0,−1)→7 NE, (1,−1)→6 E, (1,0)→5 SE,
(1,1)→4 S, (0,1)→3 SW, (−1,1)→2 W, (−1,0)→1 NW, (−1,−1)→0 N.
Explicit ground force-fire uses this same cell-space direction for infantry aim
facing, including when the selected cell is behind the unit. Combat also commits
that facing before emitting the shot, so the fire pose and its effects use the
aim direction on the firing tick.

## 6. Divergences / not modelled (documented, deliberate)

- `[GI] FireUp=2` / `[CONS] FireUp=6` (art flag, distinct from the sequence
  entry): the frame within the fire anim at which the projectile actually
  launches. Our sim applies damage at anim start (≤0.4 s cosmetic skew). Low.
- Prone behavior (Down/Crawl/FireProne when under fire) not modelled — in
  the original, infantry taking fire DO drop prone deliberately; ours stay
  standing. Carry-forward candidate, do not confuse with the §3 bug.
- Per-facing spawn direction in the original is not RE'd; we spawn facing
  S (k=4, frontal) as a cosmetic choice matching the game's placed-infantry
  look. Low.
- Walk anim rate: we keep kWalkFps=9.0 (Phase 2 tuning); artmd has no
  WalkRate= for these units and the engine default rate wasn't RE'd. Low.

## 7. Attack-approach behavior (structure MEDIUM, knobs HIGH)

Companion finding for the same bug batch (single-file attackers): the
original never paths an attacker to the target's own (occupied) cell.
`FootClass::ApproachTarget` (YRpp FootClass.h vtable; Ares hooks
UnitClass's override at 0x741613) computes a DESTINATION NEAR the target
and `Mission_Attack` re-checks weapon range every frame while moving,
stopping the unit as soon as it is in range — the destination is a firing
position, never the victim's cell. Tuning knobs confirmed to exist:
`TechnoTypeClass::CanApproachTarget`, `CanRecalcApproachTarget`,
`RulesClass::ApproachTargetResetMultiplier`. The original additionally
packs up to 3 infantry per cell via sub-cell `CellClass::OccupationFlags`
— our Phase-A 1-unit-per-cell model is kept (user-approved simplification),
with fan-out achieved by assigning each attacker its own free firing cell.
Full decompile of ApproachTarget's destination choice NOT done (content-
anchored hunt deferred); our ring-assignment is a behavioral equivalent,
flagged MEDIUM.

**Update (w7r2, CONFIRMED-RE FUN_004D55B0 @0x4D5EE0..0x4D67B2, decompile in
D:\ra2-re\last-out\at.txt):** the destination is chosen on rings round the
target's centre. The start radius is the weapon range (less 0x80 past
0x1FF; a building target adds (w+h)*0x40), stepped inward by 0x100 while it
exceeds 204.8 leptons (DAT_007E8238). On each ring the 25 offsets of
DAT_008214EC (0, ±1..6, ±8, ±16, ±24, ±32, ±48, ±64, in 1/256 turn) are tried
about the facing from the target to the unit; the point is snapped to its
cell centre and must pass the in-range test FUN_006F6750 (3-D, the building
range rule) and be free (FUN_00577E90 / FUN_004833B0, else its 8 neighbours,
table DAT_0089E5F8). A path-length check FUN_0042D0F0 (<= Chebyshev + 8)
follows. With no candidate it falls back to NearbyLocation. The clone
(combat.cpp, W7 rules only) follows this for building targets, without the
path-length check.

---

# Wave 3 (W3-B): the full DoType machine — recovered from gamemd.exe

Ghidra on a private copy of the `p4proj` gamemd.exe project (`D:\ra2-re\w3fix-b`,
scripts `disp.py`/`fnat.py`/`dis.py`/`dw.py`/`xref.py`/`callctx.py`, outputs under
`D:\ra2-re\w3fix-b-out\`). Implemented in `engine/inf_anim.{hpp,cpp}`; armed only in
the live game (`World::enable_infantry_anims`), so gates/tests keep the §2 tables.

## 8. Sequence table and DoControls (HIGH)

The art `Sequence=` section is read as 42 entries in YRpp `Sequence` order (Ready,
Guard, Prone, Walk, FireUp, Down, Crawl, Up, FireProne, Idle1, Idle2, Die1..Die5,
Tread, Swim, WetIdle1/2, WetDie1/2, WetAttack, Hover, Fly, Tumble, FireFly, Deploy,
Deployed, DeployedFire, DeployedIdle, Undeploy, Cheer, Paradrop, AirDeathStart/
Falling/Finish, Panic, Shovel, Carry, SecondaryFire, SecondaryProne); each entry is
0x24 bytes at type+0xE3C. The InfantryClass vtable is at 0x7EA058 (ctor
0x517BBD), PlayAnim = vtbl+0x558 = `FUN_0051d4c0`.

Per-sequence hard-coded controls, dumped from `0x7E9F7C` (4 bytes each:
Interrupt, IsMobile, RandomStart, Rate). Rate = game frames per stage:
Ready/Guard 0 (static), Prone 6, Walk 3, FireUp 1, Down 1, Crawl 1, Up 1,
FireProne 1, Idle1/2 3, Die1-5 1, Tread 3, Swim 1, WetIdle 3, WetDie 1,
WetAttack 1, Hover 2, Fly/Tumble/FireFly 1, Deploy..Undeploy 1, Cheer 3,
Paradrop 1, AirDeathStart 3, AirDeathFalling 1, AirDeathFinish 3, Panic 4,
Shovel 6, Carry 3, Secondary* 1. Not interruptible: Down, Up, Die*, WetDie*,
Deploy, Undeploy, Cheer, AirDeath*. Idle/WetIdle/Hover/Cheer rates go through the
game-speed normaliser `FUN_005faad0` [the clone runs them at the logic rate].
This replaces the §6 `kWalkFps = 9` guess: Walk advances every 3 frames (5 fps).

## 9. PlayAnim substitutions (`FUN_0051d4c0`, HIGH)

- Amphibious soldiers on a water/beach cell use the wet set: Walk/Crawl -> Swim,
  Ready/Prone -> Tread, Idle1/2 -> WetIdle1/2, Die1/2 -> WetDie1/2,
  FireUp/FireProne -> WetAttack (with Enter/LeaveWaterSound on the switch).
- Walk -> Panic when fear (+0x6D4) > 199.
- Down is refused unless art `Crawls=yes` (read from the ART section, `0x5243DE`).
  Down sets the prone flag (+0x6DB); Up and Deploy clear it.
- A running sequence whose Interrupt byte is 0 is only replaced when forced.

## 10. A finished sequence (`FUN_00520810`, HIGH)

Runs when stage >= FrameCount: Die1-5 -> corpse (type `DeathAnims` indexed by
InfDeath, else for humans a random `[General] DeadBodies`, unless NotHuman
(type+0xEAD) and no own DeadBodies) and UnInit; WetDie1/2 and AirDeathFinish ->
UnInit; Deploy -> Deployed; Undeploy -> Ready; AirDeathStart -> AirDeathFalling;
otherwise: moving -> Walk / Crawl, deploy group -> Deployed, prone -> Prone, else
Ready. A finished sequence with a facing label (e.g. `Idle1=56,15,0,S`) turns the
soldier to that facing (SequenceFacing N=0..NW=7, clockwise). Per-frame sequence
sounds play when `stage % count == frame`.

## 11. Fear, prone and ProneDamage (HIGH)

`InfantryClass::ReceiveDamage` (`FUN_00517db5`): a prone soldier takes
`ftol(damage * Warhead.ProneDamage)` (warhead+0xF8; rulesmd `ProneDamage=50%`
etc.), min 1. After the hit: with an attacker and fear <= 99 -> fear = 100
(Fraidycat 300, Fearless unchanged); otherwise (no attacker, or already afraid)
+50, +25 above ConditionRed, +12 above ConditionYellow, cap 300; Fearless never.
Per frame (`FUN_0051fde0`): fear-- (not Fearless); standing and fear > 49 (and not
a player-controlled soldier with a destination/moving, not Fraidycat, not in the
deploy group) -> PlayAnim(Down); prone and fear < 50 -> Up; a computer-owned
Fraidycat with fear > 50 standing still Scatters. A player move order makes a
prone soldier stand (`FUN_0051a830`). No locomotor reads the prone flag: crawling
keeps the walking speed.

## 12. Idle actions (`FUN_0051cb80`, gate `FUN_00521400`, HIGH)

Called from the guard missions when there is no target, if: not moving, not prone,
not firing, sequence in {Ready, Guard, Tread}. Re-arms the timer to
`Random over [IdleActionFrequency*450, IdleActionFrequency*1800]` frames
(rules [AudioVisual] `IdleActionFrequency=.15` -> 67..270 frames), then rolls
Random(0,10): 1,2,7 -> Idle2 (+ a VoiceComment 50 % for player units); 3,4,5 ->
Idle1; 6,9,10 -> turn to a random facing; 8 -> turn + Scatter for a
computer-owned Fraidycat; 0 -> nothing. A Fraidycat with fear > 50 scatters
instead.

## 13. Death by InfDeath (`FUN_00517db5` switch, HIGH; 5 MEDIUM)

warhead+0x120 `InfDeath`: 1 Die1; 2 Die2; 3 `InfantryExplode` anim; 4
`FlamingInfantry`; 5 `AnimTypes[1]` (= ELECTRO, the second `[Animations]` entry);
6 `InfantryHeadPop`; 7 `InfantryNuked`; 8 `InfantryVirus` (+ sound); 9 Die2 (YR
mutate path); 10 `InfantryBrute`; 0 / absent -> the body is removed with no anim.
NotHuman -> Die1 always. A type `DeathAnims=` list overrides (indexed by InfDeath).
The [AudioVisual] keys are rules+0x9C..0xB4 (Wake 0x94 is the drowning anim).

## 14. Walk stage of vehicles (`FUN_004da440`, HIGH)

FootClass::AI increments WalkedFramesSoFar (+0x538) every `WalkRate` frames
(TechnoType +0x294, default 1) while the locomotor moves, every `IdleRate` frames
(+0x298, default 0 = never) standing. SHP units (`FUN_0073b520`) show
`stage % WalkFrames` when moving or IdleRate != 0; voxels (`FUN_0073a3a0`) show HVA
frame `stage % HVA frames`; a Harvester=yes unit that is unloading draws its
`UnloadingClass` (`0x73C1CC`).

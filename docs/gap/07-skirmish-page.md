# Gap analysis 07 — the Skirmish setup page (dialog `0x102`)

Read-only audit of **one screen**: Main Menu → Single Player → Skirmish. Clone at
master `25bd5b0` (`SkirmishSetupScene`, `engine/main.cpp:7034-7562`) against the
original **Red Alert 2 1.006 `game.exe`** (the target: RA2-classic, no Yuri) with
the **YR 1.001 `gamemd.exe`** page noted wherever it differs.

Evidence tags: **[CONFIRMED]** read from the binary/resources/data,
**[ANCHORED]** strongly implied by code+assets but not traced to the pixel,
**[RE-FALLBACK]** a design choice where the original was not recovered.

## 0. Summary

The clone's lobby is a **placeholder UI**, not a reproduction. Everything around it
(main menu, Single Player page, Options page) is drawn by the faithful shell
renderer (`draw_shell`, `draw_options_page`) on the 640x480 logical canvas. The lobby
alone:

- draws the **in-game options panel** `sidec01.mix/bkgdlg.shp` (Allied eagle, blue)
  stretched over the window, with **rect-font** labels and **blue boxes**, in raw
  **1280x720 device coordinates** (it does not use `ShellXform`);
- uses a **different control model**: one "AI PLAYERS" count plus one shared
  "DIFFICULTY" row. The original has 8 fixed player rows, each with its own
  None/Easy/Medium/Brutal dropdown;
- has **no dropdowns, flags, colour swatches, map preview, Choose Map dialog, name
  edit, Game Speed slider, MCV Repacks or Build Off Ally**, and no tooltips, sounds
  or persistence;
- adds things the RA2 skirmish page does not have: a **Tech Level** row and a
  **Team** column (Team is YR-only).

The **data path behind it mostly works**. Map catalog, starting cash, unit count,
crates, superweapons, short game, Allied/Soviet country, MCV start with random start
spread and per-slot AI difficulty all reach `launch_skirmish`. So the fix is mostly a
**re-skin plus a widget model on top of the existing shell code**. There are a few
real behaviour gaps: persistence, exclusive colours, Random, Game Speed and MCV
Repacks.

Rough distance: visual fidelity ~10% (only the menu music carries over); control set
~45%; gameplay consumption ~70%.

### Evidence files (outside the repo — legal; regenerate with the commands in §7)

| What | Path |
|---|---|
| **Clone lobby screenshot** (`--menu-shot`, worktree build of `25bd5b0`) | `D:\ra2-re\out\gapsk_shots\clone_lobby_1280x720.png` |
| Clone Options page (the faithful shell look the lobby should share) | `D:\ra2-re\out\gapsk_shots\clone_options_1280x720.png` |
| **Original layout reconstruction** (dialog-0x102 template rects drawn over the real shell art + real flag/checkbox/trackbar PCX; NOT a retail screenshot) | `D:\ra2-re\out\gapsk_shots\ORIG_0x102_reconstruction_640.png` (and `_2x.png`) |
| SDTP frame 0 (main menu) vs frame 1 (large empty monitor = preview well), SDWRNTMP, SDMPBTN | `D:\ra2-re\out\gapsk_shots\sidebar_tops.png`, `sdtp_f1.png` |
| Flags / skins (raw PCX from `ra2.mix/local.mix`) | `D:\ra2-re\out\gapsk_shots\{usai,japi,frai,geri,gbri,djbi,arbi,lati,rusi,rani}.pcx`, `cue_i/cce_i/trakgrip/trofl/trofm/trofr.pcx` |
| All RT_DIALOG dumps (`game.exe` 97, `gamemd.exe` 98 templates) | `D:\ra2-re\out\gapsk_shots\dlg_game_exe.txt`, `dlg_gamemd_exe.txt` |
| CSF strings used by the page (ra2.csf / ra2md.csf) | `D:\ra2-re\out\gapsk_shots\csf_ra2.txt`, `csf_md.txt` |
| `mpmodes.ini` / `mpmodesmd.ini` | `D:\ra2-re\out\gapsk_shots\mpmodes*.ini` |
| Ghidra decompiles, `game.exe` | `D:\ra2-re\out\gapsk_ra2\*.c` (+ `_disasm.txt` with the CSF key at each fetch) |
| Ghidra decompiles, `gamemd.exe` | `D:\ra2-re\out\gapsk_md\*.c` |
| Private Ghidra project copy | `D:\ra2-re\gap-skirmish` (scripts `D:\ra2-re\scripts\gapsk_dump.py`, `gapsk_dump2.py`) |

### Key code addresses (`game.exe`, RA2 1.006)

| Address | Role |
|---|---|
| `0x00683BF0` | creates dialog `0x102` (`mainmenu_true.md` §3) |
| `0x00683D10` | DLGPROC: `0x497` → init; `WM_COMMAND` → `0x6829F0`; `0x4E9` → per-item tooltips |
| `0x00683FC0` | init (msg `0x497`): fills the combos and restores the saved session |
| `0x006829F0` | `WM_COMMAND`: slot changes, Choose Map, **Start + Back commit path**, validation |
| `0x006836F0` / `0x00683920` | show+enable / close+hide player rows when the map's max player count changes |
| `0x004D1560` | country combo fill (Random + every `Multiplay=yes` country, by `UIName`) |
| `0x004D2080` | colour combo fill (`???` + the colours not owned by another row) |
| `0x004D2480` / `0x004D2700` | set a colour / handle colour `CBN_SELCHANGE`: ownership table `0x008660E0` |
| `0x004D1EA0` | colour names (`GUI:*Abbrev`) |
| `0x004D1120` | flag picture by country index |
| `0x005E22B0` | tooltip dispatcher, (dialog, control) → `STT:*` |
| `0x005C6960` | runs the Choose Map dialog `0x6B` (proc `LAB_005C69E0`) |
| `0x0066F3A0` / `0x00670380` | read / write `RA2.INI [MultiPlayer]` (+ the `[Skirmish]` session block via `0x0066F1F0`) |

The same functions in `gamemd.exe` (YR): init `0x006ADD70`, `WM_COMMAND` `0x006AC570`, DLGPROC `0x006ADA80`, tooltips `0x00603850`.

---

## 1. Original LAYOUT SPEC — dialog `0x102` (RA2-classic, `game.exe`)

**Template.** DLGTEMPLATEEX, 426x295 DLU, font "MS Sans Serif" 8 pt, 51 controls
[CONFIRMED `dlg_game_exe.txt`]. The template has **no movie control `0x71A`** and
**no mini-monitor `0x71C`**.

**Pixel conversion.** px = MapDialogRect with base units 6x13: `x*6/4, y*13/8`. That
gives the **640x480 shell canvas**, the same canvas and conversion used by
`options.md` §1 and `mainmenu_true.md` §1.

**At 800x600 and other sizes.** The clone already renders every shell page on that
640x480 logical canvas and scales it with `ShellXform`:

- `s = min(W/640, H/480)`, centred.
- At the clone's 1280x720 logical window: s=1.5, offset (160,0).
- At 800x600: s=1.25, offset (0,0).

The Skirmish page keeps this uniform, centred fit on canvases at or narrower
than 4:3. On wider landscape canvases it expands horizontally to fill the
available width while retaining the vertical fit, with the pointer transform
using the same independent X/Y scales. Its map-list header opens a dropdown
with All, Custom, and Official maps; this is a view filter only and does not
change the selected map unless the player picks another entry. The search field
below the map list filters the visible display names case-insensitively while
retaining the selected map and preview; Enter or Escape leaves text entry, and
Backspace edits the query. The existing per-row AI dropdowns continue to select
None, Easy, Medium, or Brutal opponents.

### Reference sources and licensing

The layout is an original implementation informed by the CnCNet Yuri's Revenge
launcher; it does not copy its source code, logos, or artwork. The source-code
licenses checked for the references are:

- [CnCNet XNA client](https://github.com/CnCNet/xna-cncnet-client): repository
  includes a GPL-3.0 license.
- [VERA20K](https://github.com/YuriPlanet/vera20k): renderer repository includes
  `LICENSE-GPL` (GPL-3.0).
- [redalert2-recomp](https://github.com/sp00nznet/redalert2-recomp): its own
  repository code is MIT-licensed ([LICENSE](https://github.com/sp00nznet/redalert2-recomp/blob/main/LICENSE));
  its [notice](https://github.com/sp00nznet/redalert2-recomp/blob/main/NOTICE.md)
  explicitly excludes EA game assets and lifted code from that grant. It is a
  static recompilation of the shipping binaries, not a clean-room engine like
  this project. Its [presenter](https://github.com/sp00nznet/redalert2-recomp/blob/main/docs/presenter.md),
  [high-resolution handling](https://github.com/sp00nznet/redalert2-recomp/blob/main/docs/hires.md),
  and [HD voxel renderer](https://github.com/sp00nznet/redalert2-recomp/blob/main/docs/voxels.md)
  are useful design references; no code or assets were copied.
- [CnCNet Yuri's Revenge client package](https://github.com/CnCNet/cncnet-yr-client-package):
  no root license was confirmed during review. Do not infer that bundled game
  assets, launcher artwork, or trademarks are covered by the XNA client's
  source-code license.

Those references do not establish rights to use Electronic Arts game assets or
CnCNet branding. This project may read assets from the user's local game
installation; it does not bundle or reproduce the reference artwork.

RA2-classic's own runtime relayout at 800x600 is not recovered (open question Q1).
The **YR template is natively 800x600** (533x369 DLU); its rects are in §1.6.

### 1.1 Chrome (page frame) [CONFIRMED assets / ANCHORED placement]

| Layer | Asset | Palette | Rect (640x480) | Note |
|---|---|---|---|---|
| Screen backdrop | `ra2.mix/neutral.mix/mnscrns.shp` f0 | `cache.mix/shell.pal` | (0,0,472,448) | Static, as on Options `0xD5`: no `0x71A`, so no Bink movie |
| Bottom bar | `neutral.mix/lwscrns.shp` | shell.pal | (0,448,472,32) | Holds the status/tooltip line `0x695` |
| Sidebar top | `neutral.mix/sdtp.shp` **frame 1** (empty large monitor) | shell.pal | (472,0,168,199) | The map preview `0x468` (486,37,144,112) fills frame 1's monitor hole. There is no `0x71C`, so there is no WARNING animation [ANCHORED: frame 1's hole matches the preview rect exactly; see `sidebar_tops.png`] |
| Button grid | `sdbtnbkgd.shp` (shell2.pal) x6 rows at y=199+42k; empty rows get `sdbtnanm.shp` f10 | shell2 / `neutral.mix/sdbtnanm.pal` | x=472 / x=484 | Same as `mainmenu_true.md` §7.2 |
| Buttons | `sdbtnanm.shp` f2 normal / f3 hover / f4 pressed, 156x42 | sdbtnanm.pal | rows below | Label gold 12metfnt, pressed offset (+2,+5) |
| Sidebar bottom | `sdbtm.shp` | shell.pal | (472,415,168,65) | |
| Text | 12metfnt; controls yellow `(255,255,0)`, lines red `(255,0,0)`, disabled `(159,0,0)` | — | — | Colours from `FUN_005ED180` (`options.md` §4) |

### 1.2 Sidebar controls

| id | Type | Template px (x,y,w,h) | Clone shell grid | CSF label → text | Tooltip | Behaviour |
|---|---|---|---|---|---|---|
| `0x694` | STATIC header | (477,1,162,16) | header | `GUI:SkirmishGame` "Skirmish Game" | — | gold shell header |
| `0x468` | STATIC `SS_BLACKFRAME` | (486,37,144,112) | — | — | `STT:SkirmishMapThumbnail` "Displays a satellite image of the chosen battlefield." | Map `[PreviewPack]` (base64 → Format5 **LZO** → 24-bit RGB, `maps.md` §4.6); start markers are **baked into the image** (red). Repainted on `WM_PAINT` (`0x683D10`, msg 0xF) |
| `0x6EC` | STATIC centred | (490,167,135,16) | — | default `GUI:None` | `STT:SkirmishLabelGameType` | Game-mode name, e.g. `GUI:Battle` "Battle" |
| `0x5A8` | STATIC `SS_CENTER` | (490,188,135,32) | — | default `GUI:None` | `STT:SkirmishLabelScenario` | Map display name (2 lines) |
| `0x617` | BUTTON owner-draw | (477,242,162,37) | **row 1** → (484,241,156,42) | `GUI:StartGame` "Start Game" | `STT:SkirmishButtonStartGame` | §1.5 Start |
| `0x5AA` | BUTTON owner-draw, `BS_MULTILINE` | (477,286,162,37) | **row 2** → (484,283,156,42) | `GUI:ChooseMap` "**Customize Battle**" | `STT:SkirmishButtonChooseMap` "Use this to select a battlefield." | Hides `0x102` and runs dialog `0x6B` (§1.7) |
| `0x5C0` | BUTTON owner-draw | (477,417,162,37) | **row 5** → (484,409,156,42) | `GUI:Back` "Back" | `STT:SkirmishButtonBack` "Return to previous menu." | Commits and saves the settings too (§1.5) |
| — | (rows 0, 3, 4) | — | empty sockets (sdbtnanm f10) | — | — | — |
| `0x695` | STATIC | (15,458,454,19) | — | `GUI:Blank` | — | Tooltip/status line |

### 1.3 Player rows (8 fixed rows; `y` = 17,43,69,95,121,147,173,199 px; pitch 16 DLU = 26 px)

| Column | Row 0 (human) | Rows 1-7 ids | Type / px | Items (data value) | Tooltip | Behaviour [CONFIRMED `0x683FC0`/`0x6829F0`/`0x4D*`] |
|---|---|---|---|---|---|---|
| Name / player | `0x6A0` EDIT (52,17,150,19) | — | — | text, `EM_LIMITTEXT` **19**, seeded from `[MultiPlayer] Handle` (`FUN_006FA180`) | `STT:SkirmishEditPlayer` "Enter your name here." | Saved on Start/Back (20-byte buffer) |
| AI selector | — | `0x50B,0x50E,0x516,0x51A,0x51B,0x51C,0x51D` | COMBOBOX (52,y,150,{120 dropped}) | `GUI:None` "None" (−1), `GUI:AIEasy` "Easy Enemy" (2), `GUI:AINormal` "Medium Enemy" (1), `GUI:AIHard` "**Brutal Enemy**" (0) | control `STT:SkirmishComboAIPlayer` "Add an opponent."; items `STT:PlayerNone/DumbAI/SmartAI/GeniusAI` | "None" sets the row's country and colour to −2 and **disables** them (`EnableWindow(0)`); any AI level re-enables them |
| Flag | `0x6DA` | `0x6DB..0x6E1` | STATIC `SS_GRAYRECT` (214,y,48,19) | `local.mix` PCX 47x23 (magenta = key): `rani` (−2 Random), `usai` 0 America, `japi` 1 Korea, `frai` 2 France, `geri` 3 Germany, `gbri` 4 Great Britain, `djbi` 5 Libya, `arbi` 6 Iraq, `lati` 7 Cuba, `rusi` 8 Russia, `obsi` (−3 observer) | `STT:SkirmishPictureFlag` | Updated on every country change (`FUN_004D1120`) |
| Country | `0x6A1` | `0x510,0x513,0x51E,0x514,0x51F,0x520,0x521` | COMBOBOX (270,y,117,120), 7 visible items (msg `0x4DE,7`) | `GUI:RandomEx` "Random (???)" (−2), then every country with `Multiplay=yes` in rules order, labelled by `UIName` (`Name:Americans` "America" … `Name:Russians` "Russia") | `STT:SkirmishComboCountry` "Select a nationality." | `CBN_SELCHANGE` → flag refresh. The human row is always enabled. Observer (−3) appears only in network games |
| Colour | `0x6A2` | `0x522..0x528` | COMBOBOX + `WS_VSCROLL` (396,y,52,118), 9 visible (msg `0x4DE,9`) | `GUI:RandomAsSymbols` "???" (−2), then **only the colours not owned by another row**, in the order `GUI:GoldAbbrev` GLD (221,226,13), `RedAbbrev` RED (255,25,25), `BlueAbbrev` BLU (42,116,226), `GreenAbbrev` GRN (62,209,46), `OrangeAbbrev` ORA (255,160,25), `SkyBlueAbbrev` SKY (50,215,230), `PurpleAbbrev` PUR (149,40,189), `PinkAbbrev` PNK (255,154,235). Each item carries its swatch colour (msg `0x498`). Grey OBS (96,96,96) is observer only | `STT:SkirmishComboColor` "Select a color." | **Colours are exclusive.** Picking one releases the row's old colour, claims the new one in table `0x8660E0` and rebuilds all 8 colour combos (`FUN_004D2700`/`FUN_004D2080`), so **duplicate colours cannot happen** |

RGB triples are [CONFIRMED] from the dword table at `game.exe` VA `0x7E6D94`.

**Row visibility.** Rows ≥ the map's max player count (`FUN_005C6600`) are set to
"None" and **hidden** (all four controls `ShowWindow(0)`, `FUN_00683920`). Rows below
it are shown (`FUN_006836F0`). The visible row count therefore equals the map's
player count, human included.

**Team and Start columns.** None in RA2-classic. Start positions are assigned at
launch by the random spread (`mcv-deploy.md` §6, `FUN_006880F0`).

### 1.4 Game options (left column = checkboxes, right = labelled sliders)

| id | Type | px | CSF label | Tooltip | Range / default | INI key (`[Skirmish]`) |
|---|---|---|---|---|---|---|
| `0x54E` | checkbox (`cue_i`/`cce_i` 18x18, label at +26) | (52,235,150,16) | `GUI:ShortGame` "Short Game" | `STT:SkirmishCBoxShortGame` "Players win when all enemy buildings are destroyed." | rules `ShortGame=yes` | `ShortGame` |
| `0x693` | checkbox | (52,263,150,16) | `GUI:MCVRepacks` "MCV Repacks" | `STT:SkirmishCBoxRedeploys` "Players have the ability to move their ConYard after it's deployed." | rules `MCVRedeploys=yes` | `MCVRepacks` |
| `0x696` | checkbox | (52,290,150,16) | `GUI:CratesAppear` "Crates Appear" | `STT:SkirmishCBoxCrates` "Random Power-Up Crates will appear." | rules `Crates=yes` | `CratesAppear` |
| `0x69A` | checkbox | (52,320,154,16) | `GUI:SuperWeaponsAllowed` "Superweapons" | `STT:SkirmishCBoxSWAllowed` "Super Weapons can be built." | yes | `SuperWeaponsAllowed` |
| `0x69D` | checkbox, **visible + `WS_DISABLED`** in RA2 (style `0x58000003`) | (219,318,249,17) | `GUI:BuildOffAlly` "Build Off Ally ConYards" | `STT:SkirmishCBoxBuildOffAlly` | yes (greyed) | `BuildOffAlly` |
| `0x699` | label | (219,235,90,16) | `GUI:GameSpeed` "Game Speed" | — | — | — |
| `0x529` | trackbar (`trakgrip`, readout `trof*` 50 px; usable track = 127−50−13 = 64) | (321,235,127,21) | — | `STT:SkirmishSliderSpeed` "Controls unit speed and build times." | range **0..6**, **pos = 6 − GameSpeed** | `GameSpeed` (also the session speed `DAT_00A40B18`) |
| `0x69B` | label | (219,263,90,16) | `GUI:Credits` "Credits" | — | — | — |
| `0x511` | trackbar | (321,263,127,21) | — | `STT:SkirmishSliderCredits` | range **MinMoney..MaxMoney** (5000..10000), step **MoneyIncrement** 100 (msg `0x4AB`), pos = Credits | `Credits` |
| `0x69C` | label | (219,290,90,16) | `GUI:UnitCount` "Unit Count" | — | — | — |
| `0x50C` | trackbar | (321,290,127,21) | — | `STT:SkirmishSliderUnit` | range **MinUnitCount..MaxUnitCount** (0..10), pos = UnitCount | `UnitCount` |

**Not on the page (RA2 or YR):** Tech Level, Bridges Destroyable, Multi Engineer,
Shroud, Fog, Ally Change and AI-difficulty slider. `STT:SkirmishSliderTech` and
`STT:SkirmishSliderAIDifficulty` exist in the CSF but no control uses them. The
skirmish TechLevel is the rules value (10); the rest come from
`[MultiplayerDialogSettings]` (`skirmish.md` §1).

**Trackbar readouts.** The init sends no `0x4AC`, so the default **numeric readout is
on** for all three sliders [ANCHORED on `options.md` §2].

### 1.5 Start / Back / validation / persistence [CONFIRMED `0x6829F0`, `RA2.INI`]

**Start (`0x617`).**
1. Disables the button.
2. Counts the AI rows whose value is in {0,1,2}.
3. If **map max < AI+1**: message box `TXT_SCENARIO_TOO_SMALL` "This map has a %d
   player max. The max includes human and computer players." with `TXT_OK`, then
   re-enables Start.
4. If **AI+1 < 2**: `TXT_NEED_AT_LEAST_TWO_PLAYERS` "You need at least two players
   to start the game!".
5. Runs the session's scenario check (vtable +0x14); on failure, a message box
   (`Skirmish.cpp` line 0x34B).
6. Copies name, country, colour and per-slot `{type, country, colour}` into the
   session (`DAT_00A3D400`, stride 12). Type: None=1, Brutal=4, Medium=5, Easy=6.
7. Reads `GameSpeed = 6 − pos`, Credits, UnitCount and the five checkboxes, then
   launches.

**Back (`0x5C0`).** Shares the commit path but skips validation. **The lobby state is
saved on Back as well.**

**Persistence.** `RA2.INI` (YR: `RA2MD.INI`), real values from this install:

```ini
[MultiPlayer]   Handle=54,61,6e,79,61,   ; hex bytes of the name ("Tanya")
                Color=6  Side=Alliance  ColorEx=-1  SideEx=-1  GameMode=1
[Skirmish]      GameMode=1  ScenIndex=2  GameSpeed=1  Credits=10000  UnitCount=10
                ShortGame=yes  SuperWeaponsAllowed=yes  BuildOffAlly=yes  MCVRepacks=no  CratesAppear=yes
                Slot01=6,4,1        ; AI row 1: type,country,color  (6=Easy, country 4=British, colour 1=Red)
                Slot02..Slot07=1,-2,-2   ; None, Random, Random
```

The human's colour and country persist in `[MultiPlayer] Color` / `Side` as the
country name. The colour index is the **lobby order** (Gold, Red, Blue, Green,
Orange, Sky, Purple, Pink). On first run, defaults come from
`[MultiplayerDialogSettings]` with `AIPlayers=0`, so every AI row starts as "None".

**Map change.** Choose Map may change the max player count:

- Rows beyond the new count are closed and hidden silently.
- Rows gained are shown.
- A random map reloads `RandMap.img`.

**Keyboard / sound / transition.**

- Esc = dialog Cancel = Back [ANCHORED, G-01].
- Button and checkbox clicks play `GUIMainButtonSound` / `GUICheckboxSound` =
  `MenuClick`; combo open/close plays `MenuACBOpen` / `MenuACBClose`; trackbars tick
  (rulesmd `[AudioVisual]`).
- Navigation between shell pages plays `GUIMoveOutSound` / `GUIMoveInSound`
  (`MenuSlideOut` / `MenuSlideIn`); the same sounds play when entering or
  leaving Skirmish setup. Their stereo BAG samples are downmixed per frame for
  the mono SFX mixer, preserving their original pitch and configured volume.
- Page enter/leave runs the staggered SDBTNANM row power-on (frames 5-10 in, 11-16
  out, 30 ms/frame, `FUN_005E5150`; `mainmenu_true.md` §7.2).
- The shell theme `grinder` keeps looping.

### 1.6 YR (`gamemd.exe`) differences — dialog `0x102` is natively **800x600** (533x369 DLU, 72 controls)

- Sidebar column at x=637:
  - header (637,1,162,16)
  - Start (637,242,162,37), Customize Battle (637,286,162,37), Back (637,562,162,37)
  - preview (643,37,144,112), game type (648,167,135,16), map name (648,188,135,32)
  - status line (3,576,615,19)
- **Column headers**: `GUI:Players` (58,34), `GUI:Side` (286,34), `GUI:Color` (424,34),
  `GUI:StartPosition` "Start" (487,34), `GUI:Team` (544,34).
- Rows y = 58 + 26k:
  - name edit (57,58,150,22)
  - AI combos x=58 w150
  - flag (225,y,48,19)
  - side/country (286,y,117)
  - colour (423,y,43)
  - **Start combos `0x6A3..0x6A8,0x6AA,0x6AB`** (486,y,37)
  - **Team combos `0x76D..0x774`** (546,y,37)
- Options:
  - Short Game (72,286), MCV Repacks (72,313), Crates (72,341), Superweapons (72,370)
  - Build Off Ally **enabled** at (301,368,249,17)
  - Slider labels at x=301; sliders (403,286/313/341,127,21)
- YR adds `GUI:StartSelection` "Select a start location." (ra2md.csf only), the
  `Name:YuriCountry` side and the `teamgame` mode (`mpmodesmd.ini` id 9).
- RA2-classic has **no** Team/Start columns and no clickable start selection.

### 1.7 Choose Map = "Customize Battle", dialog `0x6B` [CONFIRMED template; proc behaviour ANCHORED]

| id | Type | RA2 px (640x480) | Label / behaviour |
|---|---|---|---|
| `0x694` | header | (477,1,162,16) | `GUI:ChooseMap` "Customize Battle" |
| — | STATIC | (34,32,385,19) | `GUI:SelectEngagement` "Select Engagement" |
| — / `0x6EB` | label + LISTBOX | (30,97,195,16) / (30,126,195,260) | `GUI:GameType` "Game Type". In skirmish only the `[Battle]` modes of `mpmodes.ini` allow AI: RA2 = Battle (id 1); YR also Team Game (id 9) |
| — / `0x553` | label + LISTBOX (scroll) | (252,97,195,16) / (252,126,195,260) | `GUI:GameMap` "Game Map": maps whose filter matches the mode (`standard` for Battle, `maps.md` §2) |
| `0x468` | preview | (486,37,144,112) | Same preview well |
| `0x6C5` | button row 0 | (477,198,162,37) | `GUI:UseMap` "Use Map" |
| `0x583` | button row 1 | (477,242,162,37) | `GUI:CreateRandomMap` "Random Map" (`RandMap.img` generator) |
| `0x5C0` | button row 5 | (477,417,162,37) | `GUI:Cancel` "Cancel" (result 2 restores the old map) |
| `0x695` | status | (15,458,454,19) | — |

---

## 2. Clone state (what is there now)

- Scene: `SkirmishSetupScene`, `engine/main.cpp:7041-7252`; `update` `:7257`;
  `render` `:7265-7373`; `handle` `:7482-7562`.
- Entry: `ShellScene` `ACT_SKIRMISH` `:7459`.
- Screenshot: `D:\ra2-re\out\gapsk_shots\clone_lobby_1280x720.png`.
- Geometry is raw device px at 1280x720:
  - option rows `global_rect` `:7119` (60,150+46i,520,38)
  - slot cells `:7120-7129`
  - map stepper `:7134` (640,96,600,40)
  - Start/Back `:7130-7131`
- Panel art: `art_.dlg` = `sidec01.mix/bkgdlg.shp` + `uibkgd.pal` (`:6134`), drawn at
  (20,20,1240,680) (`:7274-7279`).
- Text: rect font `draw_text`, uppercase; title "SKIRMISH SETUP" in 12metfnt at
  (60,56) (`:7299`).
- Config: `default_skirmish_config` (`engine/skirmish.cpp:6-14`) gives Allied
  "Americans" colour 0 team 0, plus a Soviet "Russians" AI Normal colour 1 team 1,
  plus a Neutral civilian. Crates false, cash = `mp_money()`, unit_count 10,
  tech 10.
- Consumption by `launch_skirmish` (`main.cpp:12314-12439`), real-play branch
  `:12360-12394`:

  | Field | Consumer |
  |---|---|
  | map | `cfg.map_file` (`:7509-7511`) |
  | starts | `gf_pick_starts` random spread |
  | per-house force + MCV + unit_count + tech filter | `gf_spawn_start_force` (`:12380`) |
  | AI difficulty | `make_ai_config(s.diff)` (`:12383`; G-63 index fix in `ai_base.cpp:19`) |
  | cash | AI wallet, and the human via `take_income(0)` (`main.cpp:4853`, G-21 fixed) |
  | crates | `enable_p5_features` (`main.cpp:4348`) |
  | superweapons | `realplay_scene.inc:123` |
  | short game | `configure_skirmish_houses` (`skirmish.cpp:24`) |
  | tech cap | `gf_scene.inc:22` |
  | colour | `World::house_palette` → `pal_for_color` (`world.cpp:323-379`) |
  | game speed | the **[Options]** setting (`main.cpp:5065-5070`), not the lobby |
  | MCV redeploys | rules (`gf_start.inc:255-260`), not the lobby |

---

## 3. Gap table

Status: WORKS / PARTIAL / WRONG / MISSING / EXTRA (clone has something the RA2
page lacks). Effort: S ≤ ½ day, M 1-3 days, L > 3 days.

| ID | Element | Original (cite) | Clone (file:line, shot) | Status | Sev | Fix sketch | Effort |
|---|---|---|---|---|---|---|---|
| SK-01 | Page chrome | Shell page: MNSCRNS + LWSCRNS + SDTP **f1** + SDBTNBKGD grid + SDBTNANM buttons + SDBTM (§1.1) | `bkgdlg.shp` in-game options panel stretched to 1240x680 (`main.cpp:7271-7279`); shot `clone_lobby_1280x720.png` | WRONG | MAJOR | Render via `draw_shell` with a "sidebar top frame 1, no WARNING monitor, no Bink" variant (new params). Drop `art_.dlg` here | S |
| SK-02 | Coordinate space / scaling | 640x480 template px → `ShellXform` (§1) | Raw 1280x720 device rects (`:7119-7134`), separate from `ShellXform` | WRONG | MAJOR | All rects in logical 640x480 from §1 tables; hit-test via `xform().hit()` (same pattern as `ShellScene::to_device`) | S |
| SK-03 | Sidebar buttons | Start Game row 1, **Customize Battle** row 2, Back row 5, empty sockets 0/3/4 (§1.2) | START/BACK blue boxes bottom-left (`:7359-7372`); no Choose Map | WRONG | MAJOR | `ShellButton` list for this page; reuse hover/press frames 3/4 | S |
| SK-04 | Header | `GUI:SkirmishGame` "Skirmish Game" in the sidebar header (477,1) | "SKIRMISH SETUP" at (60,56) (`:7299`) | WRONG | MINOR | `header()` of the page | S |
| SK-05 | Fonts / colours | 12metfnt; yellow text, red lines, dark red disabled (`options.md` §4) | Uppercase rect font, blue boxes (`:7286-7294`) | WRONG | MAJOR | Reuse the `draw_options_page` text/frame lambdas (factor them out, `:6537-6581`) | S |
| SK-06 | Player name | EDIT `0x6A0`, 19 chars, from `[MultiPlayer] Handle` (hex), saved on Start/Back | "P0 YOU" static (`:7342`) | MISSING | MINOR | Shell edit widget (SDL text input; Android soft keyboard); store in settings | M |
| SK-07 | Player-row model | 8 fixed rows; rows ≥ map max hidden; per-row AI combo None/Easy/Medium/Brutal (§1.3) | Global "AI PLAYERS" 1..7 + rows rebuilt (`:7168-7205`) | WRONG | MAJOR | Lobby state = 8 rows `{type,country,color}` (mirrors the INI); visible = `max_players`; build `SkirmishConfig.slots` from the non-None rows at Start | M |
| SK-08 | AI difficulty labels / scope | Per row: "Easy Enemy" / "Medium Enemy" / "Brutal Enemy" (`GUI:AI*`) | One global row EASY/NORMAL/HARD (`menu_diff_name` `:6675`; `set_difficulty` `:7160`) | WRONG | MAJOR | Per-row item → `AiDifficulty` (Easy=2→Easy, Medium=1→Normal, Brutal=0→Hard); the G-63 index fix already holds | S |
| SK-09 | Country dropdown | Random (???) + 9 Multiplay countries by `UIName`, per row incl. human; 7 visible | Left click toggles Allied/Soviet (America/Russia), right click cycles within the side (`:7488-7551`); label uppercased, truncated to 11 (`gf_start.inc:419-425`); no Random | PARTIAL | MAJOR | Dropdown over `gf_lobby_countries()` (`gf_start.inc:386`) + Random. Side = country `Side=` (`gf_side_of_country`) | S |
| SK-10 | Flag picture | `0x6DA..0x6E1`: `usai/japi/frai/geri/gbri/djbi/arbi/lati/rusi/rani.pcx` (`FUN_004D1120`) | none | MISSING | MINOR | Load the 10 PCX in `load_menu_art`, magenta-keyed, blit at (214,y) | S |
| SK-11 | Colour dropdown | ??? + free colours only (exclusive), swatch + `GUI:*Abbrev`, lobby order Gold/Red/Blue/Green/Orange/Sky/Purple/Pink (`0x4D2080/0x4D2700`, RGB `0x7E6D94`) | "COL n" cycles 0..7, duplicates allowed (`:7553-7556`); `color_ix` is the `[Colors]` order (Gold, DarkRed, Orange, Magenta, Purple, DarkBlue, DarkSky, DarkGreen; `world.cpp:330-339`) | WRONG | MAJOR | Exclusive ownership table; map lobby idx → clone `color_ix`: {0→0 Gold, 1→1 DarkRed, 2→5 DarkBlue, 3→7 DarkGreen, 4→2 Orange, 5→6 DarkSky, 6→4 Purple, 7→3 Magenta} [ANCHORED by RGB/name] | S |
| SK-12 | Random resolution | Country −2 / colour −2 resolved at launch (session code; exact rule = Q2) | No Random option at all | MISSING | MINOR | At Start: random country among Multiplay countries; random colour among unowned (seeded with the existing `GfRng`) | S |
| SK-13 | Team column | **None in RA2-classic** (FFA); YR has Team `0x76D..` (§1.6) | TEAM -1..7 cycle per row (`:7557-7558`); default human team 0 / AI team 1 (`skirmish.cpp:9-10`) | EXTRA | MINOR | Classic target: hide the column, every slot `alliance_team=-1` (FFA). Optional YR mode later | S |
| SK-14 | Start positions | RA2: none on the page; random spread at launch (`FUN_006880F0`). YR: Start combo + click on preview | `gf_pick_starts` (`main.cpp:12365`) | WORKS (classic) | — | — | — |
| SK-15 | None / closed rows | "None" disables the row's country+colour (−2), rows beyond map max hidden (`0x683920/0x6836F0`) | No such state | MISSING | MAJOR | Part of SK-07 | (SK-07) |
| SK-16 | Map preview | `0x468` (486,37,144,112) in SDTP f1 monitor; `[PreviewPack]` LZO RGB with baked red start markers (`maps.md` §4.6) | none; map shown as text in a stepper (`:7300-7324`) | MISSING | MAJOR | Decode with the existing `base64_decode` + `decompress_mappack(LZO)`; aspect-fit into the well; cache per map | M |
| SK-17 | Game type + map name labels | `0x6EC` "Battle", `0x5A8` map name centred under the preview | "A HILL BETWEEN 2 · 1 OF 91 2 P" stepper row (`:7303-7314`) | PARTIAL | MINOR | Draw `GUI:Battle` + `MapEntry::display_name` in the two sidebar rects | S |
| SK-18 | Choose Map | "Customize Battle" dialog `0x6B`: Game Type list, Game Map list (scrolling), preview, Use Map / Random Map / Cancel (§1.7) | ±1 / ±10 stepper inside the lobby (`:7516-7525`) | MISSING | MAJOR | New page (a `ShellScene` page id or a sub-mode of the lobby) with two skinned listboxes + preview; Use Map writes `map_ix_`, Cancel restores | M |
| SK-19 | Random Map | `RandMap.img` generator (`0x583`) | — | MISSING | MINOR | Out of scope: render the button greyed | S |
| SK-20 | Game Speed slider | `0x529`, 0..6, pos = 6−speed, readout; sets the session speed | Omitted (`:7039`); the game reads `[Options] GameSpeed` (`main.cpp:5065-5070`) | MISSING | MAJOR | Slider → `cfg.game_speed` (new field) used by `GameScene` instead of `g_user_settings->game_speed` for skirmish; also persist `[Skirmish] GameSpeed` (Q7: also `[Options]`?) | S |
| SK-21 | Credits slider | `0x511` 5000..10000 step 100, readout | ± halves of a text row (`:7210-7212`); consumed correctly | PARTIAL (widget) | MINOR | Trackbar widget; range/step from `[MultiplayerDialogSettings]` (not hardcoded) | S |
| SK-22 | Unit Count slider | `0x50C` 0..10 | ± text row (`:7214`); consumed (`main.cpp:12381`) | PARTIAL (widget) | MINOR | Trackbar widget | S |
| SK-23 | Tech Level | **Not on the page**; rules `TechLevel=10` | Extra row (`:7215`), consumed by `gf_tech_cap` | EXTRA | MINOR | Remove the row; `cfg.tech_level = rules [MultiplayerDialogSettings] TechLevel` | S |
| SK-24 | Short Game | checkbox `0x54E` | ON/OFF text row (`:7218`); consumed (`skirmish.cpp:24`) | PARTIAL (widget) | MINOR | Checkbox widget | S |
| SK-25 | MCV Repacks | checkbox `0x693` → session `MCVRedeploys` | Missing; rules value always (`gf_start.inc:255-260`) | MISSING | MINOR | Checkbox → `cfg.mcv_redeploys` → `world.set_mcv_redeploys` | S |
| SK-26 | Crates Appear | checkbox `0x696`, default from INI / rules `Crates=yes` | Row default **OFF** (`skirmish.hpp:43`); consumed (`main.cpp:4348`) | PARTIAL | MINOR | Checkbox; default from INI/rules (gates keep their explicit `crates=false`) | S |
| SK-27 | Superweapons | checkbox `0x69A`: SW **cannot be built** when off | Row; off → no SW grant table (`realplay_scene.inc:123`); SW buildings still buildable? | PARTIAL | MINOR | Also filter `SuperWeapon=` buildings out of the sidebar when off | S |
| SK-28 | Build Off Ally ConYards | `0x69D` visible but **disabled** in RA2 (enabled in YR) | none | MISSING | MINOR | Draw disabled (dark red); persist the INI value untouched | S |
| SK-29 | Checkbox / slider skins | `cue_i/cce_i`, `trakgrip`, `trofl/m/r` (`options.md` §4) | Options page has them (`main.cpp:6606-6656`); lobby uses text rows | WRONG | MAJOR | Factor `draw_trackbar` / `draw_checkbox` / `trackbar_pos_from_x` out of `draw_options_page` / `options_handle` (`:6996-7032`) | S |
| SK-30 | Dropdown widget | Combobox skin `0x5F4BA0` (red frame + text), dropped list (7 / 9 visible, scrolling, colour swatches), `MenuACBOpen/Close` | Click-to-cycle cells | MISSING | MAJOR | One `ShellDropdown` (closed face, open list as a modal overlay owning input, wheel + arrow scroll, Esc closes) used by AI/country/colour | M |
| SK-31 | Tooltips | Status line `0x695` shows `STT:Skirmish*` / per-item `STT:Player*` (`0x5E22B0`) | none | MISSING | MINOR | Hover → `gf_csf(key)` into (15,458,454,19); shell-wide | S |
| SK-32 | UI sounds | `MenuClick`, `MenuACBOpen/Close`, slider tick (rulesmd `GUI*Sound`) | none in any shell scene (`grep MenuClick engine/` → 0) | MISSING | MINOR | Play through the menu mixer on click / open / close; shell-wide | S |
| SK-33 | Page transition | SDBTNANM staggered row power-on/off 30 ms (`FUN_005E5150`) + SDWRNTMP | Instant swap | MISSING | MINOR | Shell-wide transition state in `draw_shell` | M |
| SK-34 | Shell music | `grinder` loops across shell pages | `SkirmishSetupScene::update` (`:7257-7263`) | WORKS | — | — | — |
| SK-35 | Esc / Back | Esc = Cancel = Back; **Back commits + saves** | Esc → main menu (`:7484-7487`); Back → `MainMenuScene` (`:7527-7530`). Returns to **Main Menu**, not Single Player; state discarded | PARTIAL | MINOR | Back/Esc → `ShellScene(PAGE_SINGLE)` (`mainmenu_true.md` §3: the Skirmish page's parent) [ANCHORED]; save the lobby INI | S |
| SK-36 | Validation | Too many players → `TXT_SCENARIO_TOO_SMALL`; fewer than 2 → `TXT_NEED_AT_LEAST_TWO_PLAYERS` (msg box + `TXT_OK`) | AI count silently clamped to 1..min(7,max−1) (`:7171-7175`) | PARTIAL | MINOR | With the row model, Start validates and shows `GfConfirm` (`gf_confirm_`, already used for Exit) | S |
| SK-37 | Persistence | `RA2.INI [Skirmish]` (GameMode, ScenIndex, GameSpeed, Credits, UnitCount, 5 checkboxes, Slot01..07=type,country,color) + `[MultiPlayer] Handle/Color/Side`, written on Start **and** Back | None: every visit starts from `default_skirmish_config` (`:7045`) | MISSING | MAJOR | Extend `ra2engine::Settings` (it already preserves foreign sections, `settings.hpp:25`) with a `SkirmishPrefs` parse/write; map ScenIndex ↔ catalog index (or store the file name in an extra key) | M |
| SK-38 | First-run defaults | `[MultiplayerDialogSettings]`: AIPlayers=0 (all "None"), Money 10000, UnitCount 10, GameSpeed 1, Crates yes, ShortGame yes, MCVRedeploys yes | 1 Soviet AI Normal, crates off | PARTIAL | MINOR | Seed from rules. Keep `default_skirmish_config` for the gates; the lobby uses its own seeding | S |
| SK-39 | Yuri side | Not in RA2 | Side cycle Allied/Soviet only; 9 countries (`gf_start.inc:386-391`) | WORKS | — | — | — |
| SK-40 | Game mode rules override | Battle applies `MPBattle.ini` (RA2) / `MPBattleMD.ini` (YR) over rules at launch (`mpmodes.ini`) | Not applied (`grep -i mpbattle engine/` → 0) | MISSING | MINOR (page) / MAJOR (gameplay) | Load the mode INI over rulesmd before `launch_skirmish` (separate gameplay task) | M |
| SK-41 | Hover / pressed states | Owner-draw hover/press on buttons; combo hover | No hover in the lobby | MISSING | MINOR | Comes with SK-03 / SK-30 | (S) |
| SK-42 | Test harness targets | — | `play_probe` names `start`, `back`, `map:±`, `opt:<ROW>:±`, `slot<N>:side/color/team` (`:7070-7110`) used by `scripts/play/sk-*.play`, `shell-smoke.play` | — | RISK | Keep the old names as aliases (or migrate the scripts in the same change); add `button:Start Game`, `combo:ai<N>`, `combo:country<N>`, `combo:color<N>`, `slider:credits`, `check:crates` | S |

---

## 4. Prioritized fix plan (single implementer, in order)

Main regions touched:

| Region | Location |
|---|---|
| `draw_shell` | `main.cpp:6297-6467` |
| Options widgets | `:6469-6657`, `options_handle` `:6996-7032` |
| Lobby class | `:7034-7373` |
| Lobby `handle` | `:7482-7562` |
| `run_menu_shot` | `:7601-7643` |
| `load_menu_art` | `:6040-6230` |
| Skirmish config | `engine/skirmish.hpp`, `skirmish.cpp` |
| Settings | `engine/settings.{hpp,cpp}` |
| Country helpers | `engine/gf_start.inc:386-425` |
| Colour mapping | `engine/world.cpp:323-379` |

1. **Lobby state model** (SK-07, 08, 11, 12, 13, 15, 23, 38).
   - Add `LobbyState { name; rows[8]{type: None/Easy/Medium/Brutal, country: -2|idx, color: -2|lobbyIdx}; game_speed; credits; unit_count; short, mcv, crates, sw, build_off_ally; map_ix }`.
   - Add a pure `LobbyState → SkirmishConfig` builder: Random resolution, lobby→`color_ix` table, FFA teams, `tech_level` from rules, `mcv_redeploys` and `game_speed` fields.
   - Host-testable; leave `default_skirmish_config` and the gates untouched.
2. **Persistence** (SK-37, 35). Parse/write `[Skirmish]` and `[MultiPlayer] Handle/Color/Side` in `Settings`, using the exact key spellings in §1.5. Load on entering the page; save on Start and Back. Back/Esc returns to `PAGE_SINGLE`.
3. **Re-host on the shell** (SK-01..05, 41).
   - Give `draw_shell` a page-variant argument: SDTP frame, monitor on/off, Bink on/off.
   - Lobby `render` = `draw_shell` (buttons Start Game r1 / Customize Battle r2 / Back r5, header "Skirmish Game"), then the page controls drawn in logical px via `ShellXform`.
   - Delete the device-px geometry.
4. **Shared shell widgets** (SK-29, 30, 06).
   - Factor trackbar and checkbox drawing and hit-testing out of the Options page.
   - Add `ShellDropdown` (closed face per §1.3 rects; open list overlay with scroll and item swatches; selection callback; Esc closes) and `ShellEdit` (19-char limit).
   - Options page keeps working: re-run its `--menu-shot`.
5. **Page controls per §1.3/§1.4** (SK-09, 10, 11, 20-28).
   - 8 rows: edit + 7 AI dropdowns, flags, country, colour.
   - Exclusive colour rebuild; None-row disable; hide rows ≥ map max.
   - 3 trackbars with readouts; 5 checkboxes, Build Off Ally disabled.
   - Load the flag PCX in `load_menu_art`.
6. **Preview + labels** (SK-16, 17). `[PreviewPack]` decode → texture cache keyed by map file → aspect-fit in (486,37,144,112); "Battle" at (490,167,135,16); map name at (490,188,135,32).
7. **Customize Battle page** (SK-18, 19). Dialog `0x6B` layout per §1.7: Game Type list (Battle), Game Map list from `load_yr_map_catalog(... "standard")`, preview, Use Map / Random Map (greyed) / Cancel. On return, apply the row-visibility rule.
8. **Validation** (SK-36). Start → the two message boxes via `GfConfirm` + `TXT_OK`; the button re-enables.
9. **Consumption** (SK-20, 25, 27). In `GameScene`:
   - Use `cfg.game_speed` for skirmish instead of `[Options]`.
   - `set_mcv_redeploys(cfg.mcv_redeploys)` (`gf_start.inc:255`).
   - Superweapons off also hides `SuperWeapon=` buildings.
   - SK-40 (MPBattleMD.ini) is a separate gameplay item.
10. **Polish, shell-wide** (SK-31, 32, 33). Tooltips on `0x695`, `MenuClick` / `MenuACB*` sounds, staggered page transition.
11. **Harness** (SK-42).
    - Keep the old `play_probe` target names as aliases and add the new ones.
    - Run `shell-smoke.play`, `sk-a.play`, `sk-s.play` and `--menu-shot` (`_setup.png`) once at the end, per the scoped-test rule.
    - Update `scripts/play/README.md` §lobby.

---

## 5. Open RE questions

- **Q1.** RA2-classic at **800x600**: how the shell relayouts `0x102` (WM_INITDIALOG
  skinners `FUN_005ECF40` / `FUN_005ED180` / `LAB_005ECB00`, not traced). Also which
  SDTP frame and chrome sit under `0x5A8` (488..623 x 188..220), which overlaps button
  row 0's SDBTNBKGD slice.
- **Q2.** Exact **Random** resolution at launch: country −2 → which pool (Multiplay
  only? side-balanced?) and colour −2 → lowest free or random free. Look in the
  session start after `FUN_00672A30` / `FUN_00672AE0` / `FUN_00672BB0`.
- **Q3.** Combobox skin `0x5F4BA0`: arrow glyph/art, dropped-list frame and scrollbar
  art, swatch drawing for msg `0x498`, item height. The visible-item counts (7 / 9)
  are CONFIRMED.
- **Q4.** Preview drawing: scaling mode (stretch vs aspect-fit, filtering) into
  144x112. Whether RA2-classic overlays anything besides the baked markers (YR
  numbered start markers).
- **Q5.** Dialog `0x6B` proc (`LAB_005C69E0`):
  - Game Type list contents in skirmish: Battle only, or also Free For All (also
    `randomMapsAllowed=true`)?
  - Map list item format and order.
  - Any player-count filter.
  - The Random Map generator UI.
- **Q6.** Session vtable `+0x14` scenario check and its message (line 0x34B,
  `FUN_007703F0`).
- **Q7.** Does the lobby Game Speed also write `[Options] GameSpeed`
  (`DAT_00A40B18`)? This install has `[Options] GameSpeed=1` equal to
  `[Skirmish] GameSpeed=1`.
- **Q8.** `ScenIndex` semantics: an index into which ordered list (mode-filtered
  catalog order)? Needed to round-trip with the clone's catalog.
- **Q9.** Trackbar msg `0x4AB` (Credits step): does the readout show the raw value
  ("10000") or a formatted one?
- **Q10.** The parent page after Back: the Single Player page `0x100`, via the shell
  state machine `FUN_005135F0` [ANCHORED]; confirm.

## 6. Out of scope / notes

- Lobby colour → in-game remap already flows through `house_palette`; only the index
  mapping (SK-11) is wrong.
- Country bonuses are tracked under RE-E6 in `GAP-ANALYSIS.md`.
- YR Team/Start columns would be an optional "YR mode" switch. The data
  (`alliance_team`, `use_map_starts`) already exists in `SkirmishConfig`.

## 8. Fix status (branch `skirmish-page`)

Implemented per §4 (steps 1-9, 11; step 10 partly). Evidence:
`D:\ra2-re\out\gapsk_shots\fixed\` (`lobby_menu_shot_1280x720.png` = `--menu-shot`
`_setup.png`; `lobby_first_run.png`, `lobby_ai_dropdown.png`, `lobby_configured.png`,
`customize_battle.png`, `lobby_game_start.png` from `--play`), and
`scripts/play/sk-lobby.play` (PASS: every page choice asserted in the game probe).
`sk-a`, `sk-s`, `sk-end`, `sk-a-lose`, `shell-smoke` PASS with migrated lobby steps.

| ID | Status | Note |
|---|---|---|
| SK-01 | FIXED | `draw_shell(..., sdtp_frame=1)`, monitor/Bink off |
| SK-02 | FIXED | all rects = §1 template px through `ShellXform` |
| SK-03 | FIXED | Start Game r1 / Customize Battle r2 / Back r5, sockets 0/3/4 |
| SK-04 | FIXED | header `GUI:SkirmishGame` |
| SK-05 | FIXED | shared `ShellPainter` (12metfnt yellow / red lines / dark red disabled) |
| SK-06 | FIXED | name edit, 19 chars, `[MultiPlayer] Handle` hex bytes; SDL text input (Android keyboard). [RE-FALLBACK] empty Handle shows "Player" |
| SK-07 | FIXED | 8 fixed rows, visible = map max |
| SK-08 | FIXED | per-row None/Easy/Medium/Brutal; `AiDifficulty = 2 - item data` (Brutal -> Difficult -> class 0) |
| SK-09 | FIXED | Random (???) + 9 countries by `Name:*`, 7 visible, scroll strip |
| SK-10 | FIXED | `usai..rusi/rani` PCX, magenta key (`obsi.pcx` absent on this install; unused in skirmish) |
| SK-11 | FIXED | ??? + free colours only, swatches, lobby -> house colour table |
| SK-12 | FIXED | Q2 resolved from `FUN_00672BB0`: country uniform over Multiplay countries, colour uniform 0..7 re-rolled while owned; human first, then AI rows |
| SK-13 | FIXED | no Team column; every slot FFA (`alliance_team = -1`) |
| SK-14 | WORKS | unchanged (no Start column in RA2-classic; the task's "start position dropdown" is YR-only and was not added) |
| SK-15 | FIXED | None disables country/colour (-2), rows past max closed + hidden |
| SK-16 | FIXED | `[PreviewPack]` LZO RGB, aspect-fit [RE-FALLBACK Q4] |
| SK-17 | FIXED | `GUI:Battle` + map name (2 lines) |
| SK-18 | FIXED | Customize Battle page (Game Type: Battle; Game Map list; Use Map / Cancel) |
| SK-19 | FIXED (greyed) | Random Map button disabled: no RandMap generator |
| SK-20 | FIXED | slider pos = 6 - speed; `cfg.game_speed` drives the tick rate; also `[Options] GameSpeed` (Q7 CONFIRMED: `0x6829F0` writes `DAT_00A40B18` = OptionsClass+0) |
| SK-21 | FIXED | trackbar Min/MaxMoney step MoneyIncrement from rules; readout = value |
| SK-22 | FIXED | trackbar Min/MaxUnitCount |
| SK-23 | FIXED | row removed; `tech_level` = rules `[MultiplayerDialogSettings] TechLevel` |
| SK-24 | FIXED | checkbox |
| SK-25 | FIXED | checkbox -> `World::set_mcv_redeploys` (sold ConYard undeploys) |
| SK-26 | FIXED | checkbox, default rules `Crates=yes` |
| SK-27 | FIXED | off also removes `SuperWeapon=` structures from the sidebar and the AI plan. [RE-FALLBACK: `[AI] BuildTech` tech centres stay] |
| SK-28 | FIXED | shown greyed (lamp on, dark red label), persisted untouched |
| SK-29 | FIXED | trackbar/checkbox factored out of the Options page |
| SK-30 | FIXED | `ShellList` dropdown (7/9 rows, scroll strip, swatches, Esc/Enter/Up/Down). [RE-FALLBACK Q3: arrow/list/scroll art drawn as red primitives] |
| SK-31 | FIXED (page) | `STT:*` on the status line incl. per-item `STT:Player*`; honours `[Options] ToolTips`. Other shell pages still have none |
| SK-32 | FIXED (page) | `MenuClick` / `MenuACBOpen` / `MenuACBClose` from soundmd + audio.bag on the menu mixer; slider tick not added |
| SK-33 | OPEN | staggered SDBTNANM page transition not implemented (shell-wide, M) |
| SK-34 | WORKS | grinder keeps looping (now `playing()`-checked so clicks don't stall it) |
| SK-35 | FIXED | Back/Esc commit + save, return to Single Player (Q10 per mainmenu_true.md) |
| SK-36 | FIXED | both messages via `GfConfirm` + `TXT_OK`; the too-small case is unreachable through the UI (rows past max are closed), covered by unit tests |
| SK-37 | FIXED | load on entry, save on Start and Back; isolated under `--play` (`RA2_PREF_DIR` now also set in the CRT environment on Windows -- it silently fell back to `%APPDATA%` before) |
| SK-38 | FIXED | first run per `FUN_0066F1F0(...,1,6)`: Slot01 = Easy / Random / Random, other rows None (supersedes the "AIPlayers=0" note above); human America/Gold [RE-FALLBACK] |
| SK-39 | WORKS | no Yuri country |
| SK-40 | CLOSED (no-op) | `MPBattleMD.ini` holds only `[MultiplayerDialogSettings] AlliesAllowed=yes`; nothing to layer for Battle |
| SK-41 | FIXED | button hover/press; open combo face highlighted |
| SK-42 | FIXED | scripts migrated; `start`/`back` aliases kept; new targets/keys in `scripts/play/README.md` |

Open RE: Q1 (800x600 relayout), Q3, Q4, Q5 (Random Map UI), Q6, Q8 (`ScenIndex` is an
index into the global scenario list `DAT_00A3D8DC`; the clone stores its catalog index
plus `ScenFile=`), Q9.

## 7. Reproduce

```bash
# dialog templates (both exes)
python D:/ra2-re/scripts/options_dump_dialogs.py "D:/Red Alert 2/game.exe" 102 6B
# Ghidra (private project copy)
JAVA_HOME="C:/Program Files/Java/jdk-21.0.12" .../support/analyzeHeadless.bat D:/ra2-re/gap-skirmish ra2 \
  -process game.exe -noanalysis -readOnly -scriptPath D:/ra2-re/scripts -postScript gapsk_dump.py
# clone screenshot (separate worktree build; never the main build dir)
D:\ra2-wt\gap-skirmish\build\ra2engine.exe --menu-shot <dir>\menu.png   # -> menu_setup.png
```

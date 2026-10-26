RA2 Engine - phone install
==========================

Android fork modifications: Bryle13. Original engine: Omar El Arab (0BSD).
EA has not endorsed and does not support this product.
On first launcher use, read Terms & game files and explicitly confirm your
rights to use your own game files. Decline exits. Terms and full licenses can
be reopened in the launcher. Confirmation is not proof of purchase or legal
clearance. Diagnostics remain local until you choose Share game report.
Current test builds use Google Drive delivery; do not redistribute retail assets.

Build 900042: correct Yuri Citadel wall asset, black-edged enemy-colour dashed
sensor intent lines, combat idle/secondary animation correction, Floating Disc
rim laser/air-target height and nuclear ascent trail. Full Disc drain abilities
and Mental Omega gameplay are NOT implemented. See FIDELITY-AUDIT42.md and
TOUCH-TEST42.txt for evidence, limitations and the read-only mod audit.
Save schema remains 55; use identical builds on every network peer.

Build 900041: Tank Bunker platform/composite depth correction, explicit defense
FIRE/STOP orders, authentic Yuri flag/loading art, deployed-miner role alert,
and defense-projectile muzzle alignment. See TOUCH-TEST41.txt and
FIDELITY-AUDIT41.md for verification steps and unresolved cases, including
charged Prism explicit orders and specialist FX. Save schema 55 requires a
fresh match; retain old saves and use identical network builds. Advanced
RA2.INI [Audio] MinerRoleAlerts=no restores the prior deployed-building alert.

1. Copy RA2-Engine.apk to the phone and open it to install
   (allow "Install unknown apps" when asked). To update, install the new
   APK over the old one - your game files and settings are kept. NOTE:
   saved games require a compatible engine save format. Builds 900027,
   900028 and 900029 use format 53. Earlier format 52 saves cannot load in
   these builds. Keep backups and finish older-format matches before updating.

2. Copy the whole RA2 folder (every .mix and .csf file from your Red Alert 2
   install, about 1 GB) into the phone's Download folder, so you have
   Download/RA2/ra2.mix etc. Include thememd.mix (the Yuri's Revenge music).

3. Open RA2 Engine -> "Import game files". The picker opens at
   Download/RA2. IMPORTANT: select the RA2 folder itself (Android does not
   allow picking Download directly), tap USE THIS FOLDER, then ALLOW.
   Copying takes about 30 seconds - keep the screen open until it ends -
   then the game starts. Afterwards you can delete Download/RA2.

   To add or replace files later (campaign maps, music, patches such as
   ecache01.mix, or your own movie files - MOVIES01.MIX, MOVMD03.MIX or
   *.bik), put them in Download/RA2 and import again: long-press the app
   icon -> "Import game files", or in the game Options -> Game Files.
   Saves and settings are kept; there is no need to clear the app data.

Skirmish teams
--------------
Each active player row has a Team selector: matching numbers 1-4 are
allies; - means no team. Choose at least one enemy. Team choices are
saved with launcher settings. Yuri AI remains unavailable in this preview.

Touch controls
--------------
- Tap: select, or give an order to the selected units.
- Tap a building anywhere on its picture (also a tower's top) to pick it.
- With two or more units selected, tapping one of them moves the group
  there; tap a unit that is not selected to select it.
- Double tap a unit: select every unit of that type on screen (never deploys).
- Tap the only selected unit, then wait: deploy / unload it (or DEPLOY).
- Drag with one or two fingers: scroll the map.
- Spread or pinch two fingers on the battlefield to zoom in or out. The
  battlefield zooms while the sidebar and command panel keep their size.
  Zoom is bounded (0.75x to 1.75x) and lasts this game
  session. Lift both fingers before tapping an order. A pinch never becomes
  a deselect or movement tap. Sidebar and command-panel gestures keep their
  existing meaning. A screen resize resets zoom to the selected resolution.
- Launcher theme opens eight saved appearance choices. The game artwork and
  faction interface remain independent of this launcher preference.
- Main-menu Quit returns to the portrait launcher. No phone-home step is
  intended. Please report any remaining home-screen flash on your device.
- With MCV Redeploy enabled in skirmish, select an eligible Construction Yard
  and press UNDEPLOY. Blocked exits or a disabled option prevent this action.
- Automatic deployment retries are quiet. Player-issued deployment failures
  retain their normal feedback; this does not mute combat or urgent alerts.
- Hold (a ring fills), then drag: draw a selection box. Or turn on BOX in
  the command column: then a one-finger drag draws the box at once.
- Hold, then lift: cancel the armed mode (sell / repair / placement /
  superweapon). It never deselects.
- Hold a cameo: its name and price show above your finger; the build keeps
  going. While it builds, a small menu opens: PAUSE / RESUME, CANCEL, ALL.
- Sell / repair: tap a building to mark it, tap it again to do it.
- Superweapon: tap to see the target area, tap the same spot again to fire.
- Tap the radar: look there. Use an ordinary ground tap for movement.
- Two-finger tap: cancel a mode, else deselect everything.
- Three-finger tap: show / hide the frame-rate overlay.
- Arrow (bottom left): expand or collapse the command panel. STOP, GUARD,
  DEPLOY, SCATTER, FIRE, ATK MOVE, +SELECT and teams 1-5 are on the first
  page. MORE opens TYPE, HOME, EVT, NEXT, PREV, FOLW, teams 6-0,
  V1-V3 (hold = save the view, tap = jump back), BOX and BRF (briefing).
  Team taps select; holds assign. ATK MOVE arms the next ground tap to
  engage enemies along the route. The panel hides while chat is open.
- Sell / repair / placement modes show a banner with an X to cancel.
- Back button (or the Options button on the sidebar): in-game options
  menu. On the mission results page Back =
  Continue (or Main Menu after a lost mission); on a loading screen it
  returns to the main menu.

If the game is closed
---------------------
If Android closes the game in the background, or restarts it (for example
after switching to gesture navigation or changing the theme), the game was
saved automatically: the next start shows RESUME GAME? with the mission
name. MAIN MENU asks before it throws that game away. If you won a mission
and the game was closed on the results page or the next loading screen,
the next start offers CONTINUE CAMPAIGN?.

Playing with friends (network)
------------------------------
- Same Wi-Fi: one player taps Network -> Host Game; the others tap
  Network, then the game in the list (or Join by IP and type the address
  the host's lobby shows, e.g. 192.168.1.20:50020). Choose country and
  colour, tap Ready; the host taps Start Game.
- Over the internet: someone runs the small server (docs/SERVER.md). Tap
  Internet, type the server address, Connect. Host a room or join one from
  the list or with its room code.
- In the game: the Chat button (top right) opens the chat line and the
  keyboard; Send sends it to everyone. The options menu does not pause a
  network game, and network games cannot be saved.
- If you switch to another app, the others see "paused the game" and wait
  90 seconds. Come back within that time and the game continues. If the
  game is closed or the network changes, you cannot rejoin that match yet.
- Phones and Windows PCs cannot play together yet (the game would go out
  of sync): the host refuses a player on the other platform unless it
  ticks "Cross-platform (exp.)". Phone with phone and PC with PC work.
- Everyone needs the same game files (the same RA2 / Yuri's Revenge
  files). If they differ the game stops at once with "Scenarios do not
  match". "Out of sync" means the games stopped agreeing; a report
  (DESYNC_*.SAV/.TXT) is saved in Saved Games -- please send it.

About
-----
Options -> About shows the version, the licence and the third-party
notices.


Reporting performance
---------------------
Three-finger tap in a game shows FPS (frames per second), LOGIC (15/s =
full speed) and the milliseconds per frame. Please send a screenshot of it
in a busy moment, with the phone model. With a PC: see docs/INSTALL.md
section 5 (adb shell setprop debug.ra2.perf 1, then adb logcat).

Build 900036: Game main menu in the Android launcher selects Original Yuri's Revenge (default) or independently drawn Command Center (experimental). Submenus and game controls are retained. This does not connect to CnCNet servers. With MCV Redeploy enabled, select your Construction Yard and tap legal ground to pack and move; the UNDEPLOY command remains available. Reverse buildup completes before movement.

Build 900039: Tank Bunker evacuation lowers the hull before normal pathfinding
from the center; blocked exits keep the tank inside. Entry no longer shifts
half a cell after arrival. MCV packing suppresses Structure Sold speech.
Blocked friendly infantry can yield, and miners plan legal near-ore deploy
sites. Display-only unit position/facing/height interpolation and slope shadow
corrections preserve fixed-tick gameplay. See TOUCH-TEST39.txt for phone checks.
Save format 54: retain backups; old format-53 saves are not converted. Start a
fresh skirmish. Every network participant must use the same engine build.

Build 900040: fixes remote stale occupancy claims and reselects blocked firing
positions. Tank Bunker movement/drawing now share the art-aligned docking cell
instead of the southeast foundation cell. Skirmish has a uniform UI transform,
bounded flags/badges, compact Team/Start controls and separated slider labels.
Native validation: 1259 cases / 276943 assertions passed; 12 focused cases /
261 assertions passed. Save schema stays 54. See TOUCH-TEST40.txt and
ENGINE-REFERENCES40.md for phone checks, research links and remaining limits.
Automated validation: 1255 cases / 276810 assertions passed, zero failures;
one optional absent-retail landmark fixture excluded. No phone/GPU/thermal
measurement or universal bridge/animation parity is claimed.
Build 900043: see docs/TOUCH-TEST43.txt for the stationary Gatling audio test.
The correction covers cell Force Fire loop lifetime and low-power stopping;
ordinary turret silence still needs phone verification with WEAPON_AUDIO logs.
Existing bunker, building Force Fire, wall and touch behavior is preserved.

Build 900047: DEBUG-AI is available in the skirmish opponent dropdown. Choose
its country explicitly (including Yuri) to test an economy/base without attacks,
military production, default defenses or offensive superweapons. Economic War
Miners remain valid targets but do not fire back in this testing mode.
Verify Yuri's sidebar/radar, Chaos Drone upper-centre gas on flat ground/slopes,
and DEBUG-AI economy plus save/load. Save58 reads55/56/57; older APKs cannot
read new58 saves. Disc discharge, Magnetron lifting and further Bunker polish
remain research items. No physical-phone testing is claimed by the build.

Build900048: Disc combat retains eight charging emissions and a final discharge
instead of immediate damage/report. Test moving/air/ground targets, Force Fire,
death/range cancellation and save/load mid-charge. The visual ring remains an
independent regular-circle approximation; building drains are retained.
Gatling Cannon instant shots now invoke their warhead impact animation; compare
stage1/2/3 muzzle and impact with the Tank. Capture sound and MUZZLE diagnostics.
Bunker occlusion now follows its interpolated render pose and avoids duplicate
passage draws. Test all entry/exit directions. Stopped units >=1s have bounded
TRAFFIC_SNAPSHOT context; share a report while the obstruction is still visible.
DEBUG-AI is retained. Normal-AI expansion and Magnetron lift/drop are not claimed
fixed. Save59 reads55/56/57/58; older APKs cannot read59. Keep save backups.
See VERA20K-EFFECTS48.md for evidence, limitations and remaining acceptance.

Build900049: deployed GI obeys its ground-only weapon; Guardian GI retains AA.
Test Desolator deployment: radiation continues, targeted primary fire stops
until undeploy. Check Ivan bomb clock and repair wrench using imported art.
Tanya C4 is a distinct demolition timer, not the vanilla Ivan bomb clock.
Check Boomer missile vertical raise before powered launch, plus Dreadnought/V3
regressions. Report LAUNCHER/MISSILE/DEPLOY_FIRE diagnostics for failures.
Bunker approach offset and turret display turns are smoother without changes
to docking, collision, ROT or combat timing. Test all entry/exit directions.
Magnetron lift/wave, full missile-launch fidelity and general mod support remain
open; see YURI-FIDELITY49.md. Save59 layout unchanged. Back up older saves.

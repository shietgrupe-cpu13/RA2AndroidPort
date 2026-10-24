RA2 Engine - phone install
==========================

1. Copy RA2-Engine.apk to the phone and open it to install
   (allow "Install unknown apps" when asked). To update, install the new
   APK over the old one - your game files and settings are kept. NOTE:
   saved games do not carry over between engine versions yet; after an
   update the old saves are listed as "Older version" and cannot be
   loaded. Finish a mission before you update.

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

Touch controls
--------------
- Tap: select, or give an order to the selected units.
- Tap a building anywhere on its picture (also a tower's top) to pick it.
- With two or more units selected, tapping one of them moves the group
  there; tap a unit that is not selected to select it.
- Double tap a unit: select every unit of that type on screen (never deploys).
- Tap the only selected unit, then wait: deploy / unload it (or DPLY).
- Drag with one or two fingers: scroll the map.
- Hold (a ring fills), then drag: draw a selection box. Or turn on BOX in
  the command column: then a one-finger drag draws the box at once.
- Hold, then lift: cancel the armed mode (sell / repair / placement /
  superweapon). It never deselects.
- Hold a cameo: its name and price show above your finger; the build keeps
  going. While it builds, a small menu opens: PAUSE / RESUME, CANCEL, ALL.
- Sell / repair: tap a building to mark it, tap it again to do it.
- Superweapon: tap to see the target area, tap the same spot again to fire.
- Tap the radar: look there (arm MOVE first to send the units there).
- Two-finger tap: cancel a mode, else deselect everything.
- Three-finger tap: show / hide the frame-rate overlay.
- CMD (left edge): the command column - +SEL, FIRE, MOVE, STOP, GRD, SCAT,
  DPLY, BOX, BRF (mission briefing), teams 1-5 (tap = select, hold =
  create), MORE: TYPE, HOME, EVT, NEXT, PREV, FOLW, teams 6-0,
  V1-V3 (hold = save the view, tap = jump back).
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

Touch controls
--------------
The command panel starts collapsed. Tap its small arrow at the lower-left
to show the commands; tap the arrow again to fold it away. The panel stays
in the state you chose while playing. Its buttons retain their full touch
targets, and the multiplayer chat line moves above the expanded panel.

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

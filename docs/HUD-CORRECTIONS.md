# HUD corrections: cameos, free touch mapping and native health bars

This pass changes only the sidebar/viewport layout, Android control editor,
touch HUD routing/drawing, and health-bar display. It builds on the previous
vehicle fixes without changing their gameplay logic.

## Sidebar

On Android the sidebar uses a fixed 480-unit reference height for its screen
size. World resolution overrides change the battlefield resolution; they no
longer change the relative cameo size or the number of rows that fit at a
given sidebar percentage. Explicit cameo counts remain limited by available
space, in even two-column rows.

A shortened strip now ends below its scroll buttons. It neither paints nor
captures the rest of that column. The battlefield renders and accepts input
beneath the short strip, and the unused addon tail is suppressed. Initial
layout, live changes, and resize/resume all use the same viewport rule.

## Free-screen controls

Launcher > Touch controls defaults to **Free screen controls**. This mode has
no large selection card, dock background, or folding arrow. Existing saved
positions are retained. Choose **Free screen layout** in the preset list to
start with Fire and Attack Move near the top center and Deploy near the
bottom center.

Open **Edit positions in landscape**, select and drag a button anywhere on
the full screen, then use **Size** for that button. **Tools** hides the editor
chrome so it does not shrink the editing area. **Hide** and **Restore** retain
positions and sizes. **Use layout** updates the draft; the outer **Save**
commits the layout for the next game launch. Cancel discards draft edits.

Runtime and preview both use compact 64x36 dp base buttons, the global size,
and the individual size. Coordinates span the remaining screen travel after
button dimensions are deducted, so controls fit at all four edges. Button
sizes and text do not inherit the world resolution or the old dock's large
density floor. Classic dock remains available by disabling free-screen mode.

## Health bars

The launcher display modes now gate the live native PIPBRD/PIPS rendering,
not only debug/fallback bars. Unit modes are Off, selected/damaged/hovered,
and Always; building modes are Off, selected/damaged/repairing, and Always.
The ordinary visibility/death/cargo checks still apply. Off retains the
native unit selection outline without health pips. Cargo, group, and other
native selected-unit status pips are unchanged.

The extra rectangular building bar is no longer drawn alongside native bars.
Buildings use the original diagonal PIPS row. Save launcher display settings
and relaunch the game to apply them.

## Verification

- 35 focused native cases / 1,818 assertions passed, covering the sidebar
  reference height, short-strip hit area/viewport, size/count geometry,
  free-button screen edges and physical sizing, settings, and health modes.
- Java migration, presets, corner placement, individual size persistence,
  hide/restore, cancellation and serialization checks passed.
- Production touch routing harness passed classic and custom layouts,
  all-hidden controls, and free-screen commands outside the old dock area.
- Android C++ frontend compilation passed. The pre-existing waypoint missing
  override warning remains. Android Java editor compilation passed.
- No Android device was connected; phone visual/gesture playtesting was not
  possible. Test/build/audit logs are retained in
  android/app/build/reports/campaign-fixes.

The debug APK build succeeded. v1/v2 signatures, ZIP CRC, ZIP alignment and
AArch64 ELF 16 KB LOAD alignment checks pass. No retail assets are bundled.
SHA-256: `3fb7a9d862e0581f9f1caedbd0d4a0a843d8c9dbbbc28868bf546880d471fd08`.

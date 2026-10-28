# Campaign, display, bridge and Yuri fixes

2026-10-28. These are targeted fixes to the existing Android engine.

## Changes

- Added Allied and Soviet Yuri's Revenge campaign entries. Loading, mission
  names and progression select `battlemd.ini`, `missionmd.ini` and
  `mapselmd.ini` for MD missions. Campaign saves already carry the map name,
  so the campaign family survives resume without a save format change.
- YR campaigns use the YR country/type registries and animation index lists.
  Original RA2 campaigns retain their classic roster restrictions. Campaign
  voxel units now resolve combat from the effective mission rules rather
  than reloading stock rules behind the mission overlay.
- Psychic Towers can acquire/release victims in campaigns through the same
  implementation as skirmish. Campaign unit-controller bookkeeping remains
  with the campaign runtime.
- Added persistent **Override display settings (live)** and **Widescreen:
  use full display** controls under the in-game **Game Controls** page, with
  corresponding main-menu options. The controls page leaves the battlefield
  visible and shows the live canvas dimensions. Resizing preserves the camera
  center and updates sidebar geometry, the input viewport and SDL's canvas.
  Turning the override off returns to the existing resolution/launcher policy.
- Bridge riders sort after the artwork that covers their structural lane,
  including cells displaced from a bridge sprite's anchor. Ground units
  beneath the bridge retain the ground layer.
- Instant force-fire selects the intended bridge layer before collapse
  clears occupants. A shot at the deck no longer selects a ship underneath;
  actual collapse still kills occupants on both layers.
- A deployed slave miner retains a reachable ore field within its workers'
  scan, instead of packing simply because ore lies beyond two cells. Worker
  positions, rather than the blocked building foundation, establish reachability.
- Countdown labels resolve `ra2md.csf` from the active profile, loose install
  file, or `langmd.mix`, then fall back to RA2 strings. Missing labels fall
  back to the configured readable superweapon name rather than `NAME:YAPPET`.

## Verification

- ARM64 Android debug APK built successfully through the offline Gradle build.
- Android C++ frontend compilation passed. The existing `waypoint_cell`
  missing-`override` warning remains.
- Focused native tests with the full local game installation: **106 cases,
  1,448 assertions, all passed**. Coverage includes both YR seven-mission
  chains, parsing all 14 mission maps, bridge lane ordering, layer selection
  and collapse, Psychic Tower capture/save/release in both session types,
  miner relocation, and display settings persistence.
- Broad native run before the final registry/animation refinement: **1,360
  of 1,362 cases passed**. The failures are the existing AssetFS expectation
  that the optional `ecache01.mix` is installed, and an unchanged test that
  expects default neutral house 2 to be hostile. The engine already excludes
  neutral houses in the original HEAD implementation. These tests were not
  weakened to make this run green.
- APK signature, ZIP integrity, native AArch64 identity and 16 KB ELF segment
  alignment were checked. No retail game assets are bundled.
- Native test/build logs and APK audit are retained under
  `android/app/build/reports/campaign-fixes/` locally.

## Limits requiring gameplay work

This does **not** certify complete skirmish/campaign parity or completion of
every YR mission. No phone visual test or full campaign playthrough was run.
The synthetic bridge regression checks layer selection and destruction;
it does not establish original-game broken-ramp artwork or falling animation.

An audit of all 14 YR maps also found actions outside the current action
execution switch: 106, 118-122, 127-130, 132, 135-137 and 139 occur in those
maps. Presentation requests are handled separately, so a switch census alone
is not a complete compatibility verdict. The listed non-dispatched actions
need original-game behavior analysis and implementation before complete YR
campaign support can be claimed. Menu exposure and correct registries fix
the launch path; they do not supply missing scenario logic.

See [the source research](MOD-COMPATIBILITY-RESEARCH.md) for the recommended
original-game comparison and extension-compatibility workflow.

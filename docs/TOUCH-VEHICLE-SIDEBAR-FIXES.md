# Touch controls, vehicle presentation and live sidebar settings

## Player controls

The Android launcher uses sensor landscape. Open its Touch controls settings,
then Edit positions in landscape. Drag a control, Hide it, or use Restore to
bring it back at its retained position. Use layout updates the draft; Save in
the outer dialog commits it for the next game launch. Cancel discards the draft.
Classic, two-thumb and Yuri presets are available. Existing layouts migrate;
control groups and More can now be hidden too. An all-hidden layout is retained.
Floating controls capture their own gestures even outside the dock.

The Yuri preset includes Psi Wave, Slave Miner deploy/pack, Psychic Reveal,
Psychic Dominator and Genetic Mutator. Superweapon shortcuts use the normal
readiness and targeting flow. Mind control, Magnetron and Disc drain remain
ordinary selected-unit targeting actions.

During a match, open Options > Game Controls. Sidebar size cycles through
60%, 75%, 90%, 100%, and 125%. Cameo boxes cycles Auto, 6, 8, 10, 12, ... 60.
The two-column strip is limited to the rows that fit at the chosen size and
resolution; additional products remain reachable by scrolling. Both options
apply immediately and persist in RA2.INI [Video] SidebarScale/SidebarCells.
Radar, tabs, repair/sell, production badges, and options use scaled geometry.

## Vehicle corrections

- Siege Chopper recognizes IsSimpleDeployer + DeployFire. Deployment reserves
  a legal landing cell, lands, suppresses artillery during descent, and keeps
  the chopper grounded while attacking. Move or undeploy takes off. A blocked
  landing cell refuses deployment. Deployed units draw UnloadingClass SCHD
  with the original type's turret metadata. DeployingAnim SCHPDEPL replaces
  the body while playing, with house palette, shadow and animation sound.
  Grounded artillery FLH uses the turret facing; airborne fire uses the body.
- IFV named TurretWeapon/Index mapping reads canonical uppercase INI keys.
  Passenger modes now select the corresponding turret artwork, including the
  stock engineer (turret 2), machine gun (turret 1) and advanced (turret 3).
  The stock hull remains the same, as defined by the game's assets.
- V3, Dreadnought and Boomer rocket launch effects track each launch lifecycle,
  clearing state while docked, tilting or dead so reused slots can animate
  again. Smoke and trails obey visibility. Existing rocket pitch rendering
  and launcher empty-model selection are retained.

## Validation

- 59 focused native cases / 2,036 assertions passed with the full local retail
  installation, including deployment/fire/takeoff, blocked landings, save/load,
  IFV mode mapping, missile slot reuse, sidebar sizing/scroll/hit geometry and
  settings persistence. Real VXL/HVA/SHP assets for these units were decoded.
- Java layout migration, hide/restore, cancel, presets and serialization pass.
- Native touch harness passes floating-button capture, groups, disabled actions,
  menu suppression, Yuri targeting and the all-hidden layout.
- Android ARM64 C++ frontend compiles; the pre-existing waypoint override
  warning remains. Android Java compiles in the APK build.
- Final broad suite: 1,369 / 1,371 cases passed (282,433 assertions).
  The same two existing failures concern absent optional ecache01.mix and the
  old neutral-house hostility expectation. See full-native-current.log for
  details; these tests were not weakened.
- Android debug APK built successfully; v1/v2 signatures, ZIP CRC, zipalign
  and AArch64 ELF 16 KB LOAD alignment pass. No retail game assets are bundled.
  SHA-256: `6a6733fe035437443c07a064865dec022d123cba7a1fd92a1944cd7f09d74c3b`.
- Build and test logs and APK audit are under android/app/build/reports/campaign-fixes.

No Android device is connected, so this does not claim phone visual or touch
playtesting. Complete campaign support remains subject to the missing mission
logic described in CAMPAIGN-DISPLAY-FIXES.md.

## References

Implementation uses the retail rulesmd/artmd and local engine code. The Phobos
project's own [simple-deployer documentation](https://phobos.readthedocs.io/en/latest/Fixed-or-Improved-Logics.html#issimpledeployer-customizations)
was consulted for deployment behavior; no Phobos code was copied. Android's
[activity orientation documentation](https://developer.android.com/guide/topics/manifest/activity-element#screen)
and [touch-target guidance](https://support.google.com/accessibility/android/answer/7101858?hl=en)
informed the launcher/editor. [RetroTouch](https://github.com/Andiweli/RetroTouch)
was a control-editor reference; no code was copied.

# High bridge structural cells — 2026-10-27

## Problem and correction

`depth_set_terrain` previously enabled only the cells containing BRIDGE1/2 or
BRIDGEB1/B2 overlays. These are artwork anchors, not the complete structural
cross-section. Movement therefore lacked the neighboring deck cells. The unit
renderer independently checked the same sparse artwork-anchor set, so simply
fixing passability would still leave outer-lane units at the wrong visual height.

The loader now resolves each anchor's four structural cells, clipped to real
map cells, using the existing per-cell underlying land classification. The
renderer uses those simulation bridge cells and the unit's own deck layer.
Ships and swimmers can retain the separate ground/water layer. Airborne units
and passengers are excluded from bridge elevation; landed aircraft retain it.
Low bridges, movement collision/occupancy and original artwork are unchanged.

## Reference and evidence

User-requested reference: [redalert2-recomp](https://github.com/sp00nznet/redalert2-recomp),
inspected at `c97b695b41c9e74c66c9d14d898f68f1ef8c449b`.
Its [testing documentation](https://github.com/sp00nznet/redalert2-recomp/blob/c97b695b41c9e74c66c9d14d898f68f1ef8c449b/docs/testing.md)
describes paired original/recompiled runs with recordings. The public repository
does not supply the generated game implementation. Its
[NOTICE](https://github.com/sp00nznet/redalert2-recomp/blob/c97b695b41c9e74c66c9d14d898f68f1ef8c449b/NOTICE.md)
distinguishes its MIT tooling from generated retail code. No generated code,
game assets or reference implementation was copied into this project.

Geometry was independently checked against the local owned gamemd.exe v1.001:

* `OverlayClass::Mark`, 0x5FC5EE–0x5FC631: concrete/wood first orientation
  invokes the bridge setter with direction 0; the second with direction 6.
* Bridge setters 0x47E040 and 0x47E470: structural flag 0x100 is written to
  the anchor, first and second forward cells, and one opposite cell. The third
  forward cell and extra direction-6 cell receive side flags, not structural
  membership, and must not be opened as additional lanes.
* The local Yuri's Plot map probe finds six stored anchors. This demonstrates
  that overlay count cannot be used as the structural cell count; it does not
  prove that gaps between anchors should be filled.

Implementation is independent C++ in the existing permissive architecture.

## Verification and limits

New asset-independent regressions cover all four overlay identities, native
cell coordinates, unrelated-overlay rejection, separate water/deck grids,
outer-lane unit elevation, airborne/naval/passenger exclusions, collapse and
engineer-hut repair. Android frontend syntax verification passes (the existing
missing `override` warning remains).

Focused bridge run: **3 cases / 256 assertions passed**. Full native run:
**1344 cases / 280439 assertions passed**, excluding the unavailable landmark
fixture. The initial restricted run failed five cases involving temporary
save/log writes and local network sockets; rerunning with those permissions
passed. Optional external-oracle fixtures can still skip internally, so this
is not an original-game visual acceptance result.

This fixes confirmed structural-cell ownership and the corresponding unit-height
gate. Full ramp tile/state reconstruction, pre-destroyed span gap reconstruction,
per-section native collapse timing and artwork occlusion parity remain separate
work. A phone replay of tanks crossing in both directions alongside a ship
underneath is still required to accept the visible result. No phone/GPU or
original/recompiled visual comparison was performed in this change.

Save layout remains 60. Existing saves retain their serialized geometry; start a
new match to exercise the corrected map loader. Network peers must use matching
builds because new matches now have corrected simulation bridge cells.

# Yuri rendering follow-up (touch test 7)

Phone report: Lasher shows the wrong vehicle model; Slave Miner artwork is
incorrect or incomplete; radar has black pixels in explored areas.

## Changes

- `src/asset_fs.cpp`, `include/ra2assets/asset_fs.hpp`: optional expansion-first
  lookup with classic fallback. Normal lookups and RA2-only lookups retain
  their previous order.
- `engine/main.cpp`: vehicle types whose Owner includes YuriCountry opt into
  expansion-first lookup for hull, turret and barrel VXL/HVA files. Every
  successfully resolved part logs its rules type, image name and source archive
  under `[vehicle-art]`. This addresses duplicate-name shadowing; it does not
  prove that duplicate files caused the tester's particular wrong model.
- Vehicle loads that reuse a parsed scenario `Rules` object now read `Owner`
  from that same object when selecting YR art. They no longer consult an empty
  temporary INI and accidentally choose classic artwork for Yuri units such as
  the Lasher.
- `engine/pres_radar.inc`: unexplored cells leave the cleared radar image alone.
  Previously their black rectangles could overwrite explored cells sharing the
  same pixel when the map was reduced to radar size. Unexplored-only pixels
  remain black; this does not reveal the map.

## Still unresolved

The deployed Slave Miner is a building with an idle-animation layer. The
current loader does attempt that layer and freezes powered animations rather
than hiding them on low power. No proven correction for its missing body has
been identified without the tester's actual game files/runtime logs. Slave
workers and mining income remain unsupported in this Yuri preview.

## Verification

Synthetic MIX tests exercise classic/YR duplicate resolution, RA2-only
restriction, classic fallback, missing files and source labels. No retail
artwork is used. The targeted run passed 6 cases / 46 assertions (including
existing asset search order tests; retail-install and harness cases excluded).
Android main translation-unit syntax check passed with two existing warnings.
Radar change has source review only; no rendered radar or phone test was run.

## Phone checks

Start a fresh Yuri skirmish. Compare a Lasher and a mobile Slave Miner against
the previous build, then deploy the miner. Check the radar on an explored map
boundary. If the vehicle remains wrong, collect Android logs containing
`[vehicle-art]`; these identify which exact archives and model names won.

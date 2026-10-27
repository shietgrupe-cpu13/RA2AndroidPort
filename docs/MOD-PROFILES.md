# Initial native mod profiles

2026-10-27. This milestone adds an opt-in data overlay, compatibility inspection,
Android folder import/selection, and a command-line audit tool. It does not
decompile a Windows mod or make Mental Omega playable.

## Use

On Android, open **Mods & compatibility** in the launcher. Import the folder
containing the profile's `mod.ini`, read the compatibility report, and enable
the profile. Quit an already running game before launching a different profile.
Select **Base game** to disable it. Imported files live under
`Android/data/org.ra2.engine/files/ra2/mods/<import-id>/`; original/base files
are never replaced. An interrupted import stays unselected.

An original, asset-free example is supplied in
[`examples/mods/small-stat-demo`](../examples/mods/small-stat-demo). Import that
folder, not the parent `mods` folder. It changes the existing GI's cost to 75
and strength to 150 while retaining its other base rules.

```
small-stat-demo/
  mod.ini
  rulesmd.ini
  artmd.ini             # optional
  aimd.ini              # optional, experimental outside verified keys
  assets/               # optional loose SHP/VXL/HVA/PAL resources
  maps/                 # optional loose .map/.mpr/.yrm files
  expand99.mix          # optional data archive
```

`mod.ini` uses the engine's existing INI format:

```ini
[Mod]
Id=small-stat-demo
Name=Small stat demo
Requires=base
```

`Id` permits lowercase ASCII letters, digits, hyphens and underscores, up to
48 characters. Every profile must explicitly declare `Requires=base`. Ares,
Phobos and other engine requirements are blocked. Do not relabel an extension
mod as a base profile to bypass the report.

Desktop: `ra2engine --mod <folder>`. Native audit (no game startup):
`ra2modcheck <folder>`. CMake builds `ra2modcheck` with the existing pure C++
asset library. Both audit and activation use the same scanner.

## Resolution and isolation

* Loose files at the profile root, `assets/` or `INI/` precede archive members.
  Supplying the same loose basename in multiple supported locations is blocked.
* Root MIX files use descending normalized filename priority. Within an archive,
  its own entries precede the known localmd/cachemd/conqmd/cameomd nested archives.
  This is an explicit initial profile convention, not universal mod load-order
  emulation. Existing base archive precedence is untouched.
* Standard rules/art/AI/sound/EVA INIs are patches over the corresponding base
  INI. Existing keys are replaced in their original positions; new keys/sections
  are appended. Unchanged registry order and duplicate entries are retained.
* SHP/VXL/HVA/PAL artwork uses the same overlay resolver, including direct
  `AssetFS::find` callers. Loose resources use lazy offset reads, with no
  repacking or eager loading of every asset.
* Profile maps enter the existing validator/catalog as `modmaps/<filename>`.
  Their source remains distinct from base `maps/` files in saves and network
  map reads. Disabled or missing profiles fail closed when resolving those maps.
* Stable FNV-1a content fingerprints cover sorted relative names and actual file
  bytes. Save/preferences folders are isolated by profile ID **and** fingerprint.
  Multiplayer rules checks include the fingerprint as well as effective INIs.
  FNV is a content-mismatch check, not an authenticity signature. Keep profile
  files unchanged while a game is running.

## Compatibility report

**SUPPORTED** means the scanner found only the initial verified data subset;
it is not a promise of full gameplay parity. **PARTIAL** lists unverified keys
and resource limitations; Android requires choosing **Enable experimental mod**.
**BLOCKED** prevents activation. Missing manifests, root Windows DLL/Syringe
components, extension-tag families, configuration includes, renamed game INIs,
malformed archives, ambiguous names and linked paths are blockers.

Unknown keys are labelled **unverified**, not automatically called unimplemented.
Numeric registry IDs are not counted as capability names. Dotted extension keys
are grouped into families so a large custom foundation cannot hide other findings.
This is a conservative scanner, not an exhaustive Ares/Phobos capability database.

The last Android inspection report is included in diagnostic ZIPs as
`mod-inspection.txt`; this is the last inspected folder, which can differ from
the selected profile. Runtime logs identify the profile actually activated.
Each active profile also keeps `mod-report.txt` in its isolated preference folder.

Limits: inspection/import use 16,384 entries and depth 12; Android imports cap
individual files at 2 GiB and total copied bytes at 8 GiB. Individual resolved
resources cap at 64 MiB and inspected config content at 8 MiB. No DLL or EXE is
executed. Invalid imports are not auto-activated and partial copies are removed.

## Mental Omega basis

Read-only inspection of the user-provided `D:\Games\Mental Omega\mod` found
1,959 files and seven opened archive views. Its readme identifies Mental Omega
3.3.6, credits Speeder/the Mentalmeisters, and identifies Ares/CnCNet dependencies.
No original files were modified, redistributed, or copied into this repository.

The scanner found `rulesmo.ini`, `artmo.ini`, `aimo.ini`, `soundmo.ini` and
`evamo.ini` inside its archive search. It also identified Ares/Syringe components
and extension families including custom foundations/palettes, AttachEffect,
EMP, conversions, additional superweapon logic and PrismForwarding. These are
requirements to investigate, not implementations supplied by this milestone.

The current result is **BLOCKED**. Changing only the manifest or renaming INIs
does not satisfy these dependencies. Next work should build an explicit MO data
adapter, resolve its complete config/content graph, and implement/test required
behaviors individually against owned reference data before enabling gameplay.

Custom audio banks/loose audio, string tables, additional theater templates,
archived mod map catalogs and full mod campaign packaging remain incomplete.
Loose maps still face the existing scripting/theater restrictions. No general
Mental Omega, Ares, Phobos, or arbitrary-mod compatibility is claimed.

## Verification

* Full native suite: **1,352 cases / 280,522 assertions passed**; one unavailable
  landmark fixture excluded. Optional external-oracle tests may skip internally.
* Final focused loader suite: **8 cases / 83 assertions passed**. This includes
  a real accepted map traveling through the mod catalog, rule-stat overrides,
  loose/archive/base resource precedence, reversible activation, content hashes,
  unsupported requirements and preservation of base registry ordering.
* ARM64 `libmain.so` built and linked; the new scanner JNI export is present.
  All Android Java sources compile against API 35 with Java 17.
* The supplied sample reports SUPPORTED with fingerprint `c3b43a1ee9ce2bb2`.
  The Mental Omega folder reports BLOCKED, as expected.

No APK was packaged or installed for this milestone, and the new Android
selection/import interaction has not been exercised on a phone. Native tests
are not a substitute for mod gameplay or physical-device acceptance.

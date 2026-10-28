# References for RA2/YR behavior and mod compatibility

Reviewed 2026-10-28. Recommendations below concern this C++17/SDL Android
engine, whose current native mod profiles accept a limited data overlay.

## Recommendation

Use **redalert2-recomp for original-game comparisons**, **Phobos documentation
for modern extension requirements**, and **Ares documentation for the older
extension contract**. YRpp is a useful map of original engine structures.
No single repository makes existing Windows extension mods run on this engine.

| Source | Best use here | Integration limit |
|---|---|---|
| [sp00nznet/redalert2-recomp](https://github.com/sp00nznet/redalert2-recomp) | Compare original gameplay, UI sizing, and scripted test outcomes | A different engine architecture, built from the user's original binaries; replacing this engine would be a separate port |
| [Phobos](https://github.com/Phobos-developers/Phobos) and its [feature documentation](https://phobos.readthedocs.io/en/latest/) | Identify extension tags, defaults, interactions, and test cases required by modern mods | A DLL injected into the original executable; features need native implementations here |
| [Ares documentation](https://ares-developers.github.io/Ares-docs/) | Define compatibility requirements for Ares-era mods: prerequisites, superweapons, armor, scripts, and other extended behavior | Ares targets Yuri's Revenge 1.001 through Syringe; loading its INIs alone cannot supply its logic |
| [Phobos YRpp](https://github.com/Phobos-developers/YRpp) | Cross-check class fields, layers, state names, and original function addresses | Headers describing the original binary, rather than a portable engine implementation |
| [CnCNet client-mod-base](https://github.com/CnCNet/client-mod-base) | Understand real mod folder layouts, launcher configuration, and packaging expectations | A client/developer package, not the game simulation |
| [CnCNet YR client package](https://github.com/CnCNet/cncnet-yr-client-package) | Integration examples for the official online package | Launcher and deployment behavior must be distinguished from engine capabilities |

These rankings are engineering judgments about relevance to this project,
not independent certification of another project's stability.

## What recompilation gives us

The recomp project translates the shipping RA2 1.006 and YR 1.001 binaries
into C. This makes it useful for behavior comparisons against the original,
while our engine independently implements gameplay in C++.
Its [testing guide](https://github.com/sp00nznet/redalert2-recomp/blob/main/docs/testing.md)
documents scripted runs and comparison with its original-machine-code mode.
That comparison method is valuable for reproducing bridge, ore, and campaign
issues with the same assets and orders.

Its [widescreen guide](https://github.com/sp00nznet/redalert2-recomp/blob/main/docs/hires.md)
distinguishes the battlefield resolution from menu presentation. This is a
useful reference for keeping a larger tactical view separate from scaled UI.
The current change follows that principle: live canvas resizing recomputes the
sidebar and input viewport while preserving the camera center.

Crucially, its own [mod guide](https://github.com/sp00nznet/redalert2-recomp/blob/main/mods/README.md)
says Ares/Phobos DLL mods do not work in the recomp. Its data-folder overlay
and isolated settings/saves are useful design references, but do not supply
the missing extension behaviors. The project advertises desktop platforms;
its documented [Windows host](https://github.com/sp00nznet/redalert2-recomp/blob/main/docs/host.md)
also illustrates how different its runtime is from this Android/SDL engine.

## Why importing a mod is not enough

This project's `ModProfile` scanner and [existing profile guide](MOD-PROFILES.md)
already separate supported data, unverified data, and blocked executable
extensions. It supports rules/art/AI/sound/EVA overlays and asset/map lookup,
with profile fingerprints and isolated save directories. It deliberately
does not claim Mental Omega or arbitrary Ares/Phobos compatibility.

[Ares](https://ares-developers.github.io/Ares-docs/) and
[Phobos](https://phobos.readthedocs.io/en/latest/) add executable behavior to
`gamemd.exe`. An ARM64 native build cannot acquire those features merely by
finding the DLL or accepting an extension key. The same key needs a parser,
simulation behavior, rendering where applicable, serialization, and network
determinism in this engine. This is an architectural inference from their
documented injection model and our implementation.

## Practical implementation sequence

1. **Finish base YR parity.** Run both seven-mission campaign chains with full
   map archives; check scripted state changes, mind control, special weapons,
   bridge collapse/repair, and harvesting in saved and resumed games.
2. **Improve data fidelity.** Audit registry additions, INI precedence, custom
   art/CSF/sound, theater assets, and map rules. Test tiny original fixtures
   that isolate one requirement before testing a large mod.
3. **Build a versioned capability inventory.** For each extension tag, record
   parsing support, execution support, saving, multiplayer checks, and an
   executable regression case. Report unsupported tags with their file,
   section, and required extension. Unknown keys must not imply compatibility.
4. **Choose one concrete mod/version.** Inventory its actual requirements and
   implement the smallest complete feature set. Typical investigation areas
   include custom armor/verses, alternate prerequisites, additional weapons,
   attach effects, custom superweapons, projectiles, shields, and AI scripts;
   these are candidates to audit, not claims about a particular mod's needs.
5. **Certify scenarios, then expand.** Verify startup, build/train/deploy,
   economy, combat, AI, mission outcomes, save/load, and deterministic replay.
   Retain blockers until those capabilities actually exist.

## Source reuse

The recomp repository's own code has an
[MIT license](https://github.com/sp00nznet/redalert2-recomp/blob/main/LICENSE).
Phobos provides a [GPLv3 license](https://github.com/Phobos-developers/Phobos/blob/develop/LICENSE.md).
This repository currently has a BSD Zero Clause license. Do not import source
under an assumption that these licenses are interchangeable. This change
uses references and locally owned game data; it does not copy extension code
or bundle original game data/generated recomp output.

YRpp's [CellClass](https://github.com/Phobos-developers/YRpp/blob/phobos-dev/CellClass.h)
describes the ground/alternate bridge content distinction and bridge levels;
its [SlaveManagerClass](https://github.com/Phobos-developers/YRpp/blob/phobos-dev/SlaveManagerClass.h)
describes separate manager and worker states. These help frame tests without
treating the headers as complete implementations of those behaviors.

# Save compatibility follow-up

Verified by source inspection on 2026-10-29 after the bridge renderer build.

## Newly introduced rules fields

`engine/building.hpp` adds `BuildingTypeDef::grinding` and `bio_reactor`.
`engine/save_fields.hpp::fields(BuildingTypeDef&)` does not visit either field.
The aggregate coverage test reports 60 members versus 58 visited members.
This is an unresolved serialization coverage gap, not evidence of a harmless
test expectation change.

`engine/main.cpp` explicitly refreshes building types after its interactive
load succeeds. This repairs the rule flags for that path. It does not prove
that every caller of `save::load_world` performs the same rebind.
`save::load_world` reads into the supplied World directly; it does not itself
refresh type definitions from the installed rules.

`World::unit_soylent_` is another newly added rules-derived map. It is neither
visited by SaveAccess nor explicitly exempted by the source coverage test.
Because load_world reads in place, an already initialized World retains the
map; a separately initialized or empty host may have different values.
The corresponding World size pin is short by 48 bytes on this native build.

## Required next correction

Choose and enforce one policy across all load callers: versioned persistence
of these fields, or mandatory reinstallation of rule-derived state. Check
the multiplayer rules digest covers the new gameplay flags and refunds.
Do not merely relax aggregate coverage or size assertions. Add a round-trip
test loading into a distinct World and verifying Grinder refund and Bio
Reactor occupancy behavior, plus old-save migration coverage if the binary
layout changes.

## Current validation boundary

The focused bridge, naval availability, and independent production queue
tests passed. The complete native suite reported 13 failing cases, including
these save coverage failures, map/settings expectations, retail MIX fixture
errors, and multihouse behavior. A successful Android debug build does not
resolve these failures or prove runtime visual parity.

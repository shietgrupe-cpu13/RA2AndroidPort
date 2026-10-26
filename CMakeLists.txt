cmake_minimum_required(VERSION 3.20)
project(ra2assets LANGUAGES CXX)

# W2-GA-P2: an unset build type used to mean NO optimisation (-O0): terrain
# draw, IMA decode and voxel rasterisation ran 2.5-3.5x slower. Single-config
# generators now default to RelWithDebInfo (-O2 -g); pass
# -DCMAKE_BUILD_TYPE=Release for a shipping build (-O3, no debug info) or
# Debug to get -O0 back. The sim is integer/SSE2 double math with no
# -ffast-math, so every gate hash is identical across these (re-verified).
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Debug / RelWithDebInfo / Release / MinSizeRel" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Library + engine source lists live in cmake/ra2_sources.cmake, a single
# source of truth shared with the Android jni build (android/app/jni) so the
# two builds cannot drift. It defines RA2_LIB_SOURCES and RA2_ENGINE_SOURCES.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ra2_sources.cmake)

# Library sources, shared between the static `ra2assets` target and the
# optional `ra2assets_so` shared target below so both actually contain the
# real library code (not just the placeholder).
set(RA2_SOURCES ${RA2_LIB_SOURCES})

add_library(ra2assets STATIC)
target_include_directories(ra2assets PUBLIC include)
target_sources(ra2assets PRIVATE ${RA2_SOURCES})

# NOTE: src/mix_keyderiv.cpp previously needed per-file
# `-fno-strict-aliasing;-fsigned-char` because the old verbatim XCC port did
# `*(word*)` limb type-punning and relied on plain `char` being signed. The
# clean-room reimplementation uses only uint8_t byte handling and no
# type-punning, so those flags are no longer required and were removed.

option(RA2_BUILD_SHARED "Build ra2assets as a shared lib (for Android .so check)" OFF)
if (RA2_BUILD_SHARED)
    set_target_properties(ra2assets PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Built from the same RA2_SOURCES as the static `ra2assets` target (not a
    # thin wrapper that links the static archive) so the resulting
    # libra2assets.so actually contains the library's real compiled code —
    # this is what the Android ELF/portability check below inspects.
    add_library(ra2assets_so SHARED ${RA2_SOURCES})
    target_include_directories(ra2assets_so PUBLIC include)
    set_target_properties(ra2assets_so PROPERTIES OUTPUT_NAME ra2assets)
endif()

enable_testing()
add_executable(ra2assets_tests
    tests/test_main.cpp
)
target_sources(ra2assets_tests PRIVATE tests/test_blowfish.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_mix.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_pal.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_shp.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_pcx.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_oracle.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_lcw.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_mappack.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_lzo1x.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_ini.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_rules.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_roster.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_art.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_theater_name.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_asset_fs.cpp tests/test_yuri_art_lookup.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_sounds.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_map.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_pkt.cpp)
# maplist.cpp is pure C++17 (no SDL): the pkt -> existence -> mode -> CSF map
# catalog. Reached only by the skirmish setup UI and --demo-maps, never by a
# gate path (maps.md §7 rule 5).
target_sources(ra2assets_tests PRIVATE tests/test_maplist.cpp engine/maplist.cpp)
# G0 --play (agent G): the script language (ra2play) half of engine/play_script.cpp.
target_sources(ra2assets_tests PRIVATE tests/test_play_lang.cpp)
target_include_directories(ra2assets_tests PRIVATE engine)
target_sources(ra2assets_tests PRIVATE tests/test_theater.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_land_type.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_audio.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_wav.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_vxl.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_vpl.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_csf.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_fnt.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_ufnt.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_bink.cpp)
# W5-E2 (R-134): Bink Audio vs ffmpeg (tests/data fixtures)
target_sources(ra2assets_tests PRIVATE tests/test_bink_audio.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_bink_async.cpp)   # W5-E2 R-173 (engine/bink_async.hpp)
target_compile_definitions(ra2assets_tests PRIVATE RA2_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_sources(ra2assets_tests PRIVATE tests/test_fileio.cpp)
# iso_project.cpp is pure C++17 (no SDL) and lives under engine/ so it can be
# compiled into BOTH this test binary and the ra2engine executable below
# from a single source of truth (see engine/iso_project.hpp for rationale).
target_sources(ra2assets_tests PRIVATE tests/test_projection.cpp engine/iso_project.cpp)
# pathfind.cpp is pure C++17 (no SDL) and lives under engine/ so it can be
# compiled into BOTH this test binary and the ra2engine executable below
# from a single source of truth (same pattern as iso_project.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_pathfind.cpp tests/test_bridge_cliff.cpp engine/pathfind.cpp)
# Net L0: the original FastMath / _ftol port + its emulator oracle + the sim libm ban.
target_sources(ra2assets_tests PRIVATE tests/test_orig_math.cpp engine/orig_math.cpp tests/test_net_l0.cpp)
# world.cpp is pure C++17 (no SDL) -- its passability-grid builder + coordinate
# reconciliation are unit-tested here (test_world.cpp) alongside the projection
# it must agree with. Compiled from the same single source of truth as the
# ra2engine executable below (same pattern as iso_project.cpp / pathfind.cpp).
target_sources(ra2assets_tests PRIVATE tests/test_world.cpp engine/world.cpp engine/world_yuri.cpp tests/test_yuri_core.cpp
                                       engine/world_loco.cpp engine/locomotion.cpp tests/test_locomotion.cpp  # W7 locomotion
                                       engine/world_econ.cpp  # L2 per-house economy
                                       tests/test_house_econ.cpp
                                       engine/world_veterancy.cpp  # P5 World members
                                       tests/test_world_veterancy.cpp)
# input.cpp is SDL-free (only InputEvent, screen_to_cell, World -- see
# engine/input.hpp) so it compiles into this test binary too (Task 12 review
# Finding 3: Input previously had zero test coverage); it calls
# terrain_screen_bounds, which is defined in engine/world.cpp (already a
# source above) rather than engine/iso_renderer.cpp so this stays SDL-free.
target_sources(ra2assets_tests PRIVATE tests/test_input.cpp engine/input.cpp engine/input_original.cpp
    tests/test_input_original.cpp tests/test_input_touch_w6.cpp)
# G-04: KeyboardMD.ini hotkey table.
target_sources(ra2assets_tests PRIVATE tests/test_hotkeys.cpp engine/hotkeys.cpp)
# G-70/G-71: touch gesture layer.
target_sources(ra2assets_tests PRIVATE tests/test_touch_gestures.cpp tests/test_touch_bar_layout.cpp tests/test_yuri_menu.cpp engine/touch_gestures.cpp)
# occupancy.cpp is a pure C++17 cell-ownership grid (no SDL) used by the
# multi-unit movement/collision logic; compiled into both this test binary and
# ra2engine from one source of truth (same pattern as pathfind.cpp/world.cpp).
target_sources(ra2assets_tests PRIVATE tests/test_occupancy.cpp engine/occupancy.cpp)
# group_move.cpp is pure C++17 (no SDL): formation target assignment for a
# group move order. Compiled into both binaries from one source of truth.
target_sources(ra2assets_tests PRIVATE tests/test_group_move.cpp engine/group_move.cpp)
# test_movement.cpp exercises the pure step_units collision stepper defined in
# engine/world.cpp (already a source above) against hand-built grids/units --
# no MapFile needed, so the head-on/boxed-in/funnel cases are deterministic.
target_sources(ra2assets_tests PRIVATE tests/test_movement.cpp)
# test_vehicle.cpp exercises the vehicle locomotion additions (ROT turn-in-
# place, per-unit speed, turret slew, aim-gated fire) in engine/world.cpp and
# engine/combat.cpp (both already sources above -- no new source needed).
target_sources(ra2assets_tests PRIVATE tests/test_vehicle.cpp)
# combat.cpp is pure C++17 (no SDL): armor_index/damage_vs/step_combat, the
# targeting/ROF/damage/death/pursuit-request core (docs/re/combat.md).
# Compiled into both this test binary and ra2engine from one source of truth
# (same pattern as pathfind.cpp/world.cpp/occupancy.cpp/group_move.cpp).
target_sources(ra2assets_tests PRIVATE tests/test_combat.cpp engine/combat.cpp)
# Gap fix C: weapon slots / per-target pick / slot resolution.
target_sources(ra2assets_tests PRIVATE engine/weapon.cpp engine/weapon_unit.cpp
                                       engine/weapon_resolve.cpp engine/building_combat.cpp
                                       engine/unit_orders.cpp engine/command.cpp tests/test_command.cpp
                                       tests/test_gap_combat.cpp)
# W5-B: combat and movement core (DamageArea, sub-cells, land speed, crush).
target_sources(ra2assets_tests PRIVATE tests/test_w5_combat.cpp)
# Wave-6 #4: units and weapons (docs/gap/wave6/04-units.md).
target_sources(ra2assets_tests PRIVATE tests/test_w6_units.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_w7_units.cpp)   # wave-7 fix (units)
target_sources(ra2assets_tests PRIVATE tests/test_w8_place.cpp)   # w8fix-place: placement / overlays / exits
target_sources(ra2assets_tests PRIVATE tests/test_w7_airnav.cpp)   # wave-7 air/naval
target_sources(ra2assets_tests PRIVATE tests/test_w8_misc.cpp)   # wave-8 fix (w8fix-misc)
# G16 world depth (agent G): bridges (pure) + the World garrison/bridge members.
target_sources(ra2assets_tests PRIVATE engine/inf_anim.cpp tests/test_inf_anim.cpp)   # W3-B
target_sources(ra2assets_tests PRIVATE engine/bridges.cpp engine/world_depth.cpp
                                       tests/test_bridges.cpp tests/test_high_bridges.cpp
                                       tests/test_w8_bridges.cpp)   # wave-8 bridges
# building.cpp is pure C++17 (no SDL): BuildingTypeDef/Building struct defs,
# foundation_cells row-major enumeration, placement_check validity (in-grid,
# passable, unoccupied, no overlap, adjacency), building_frame/buildup_frame
# for SHP frame selection, step_buildings state machine (Buildup->Idle->Dead->
# tombstone), and kill_building marking + die-anim timing (docs/re/building-
# render.md). Compiled into both this test binary and ra2engine from one source
# of truth (same pattern as combat.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_building.cpp engine/building.cpp)
# audio_mixer.cpp is pure C++17 (no SDL/asset deps): the software mixer --
# SoundBank storage, keyed voice registry, Limit/interrupt/priority pool
# rules, linear attenuation + GLOBAL floor, and the EVA FIFO queue
# (docs/re/audio-playback.md). Compiled into both this test binary and
# ra2engine from one source of truth (same pattern as combat.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_audio_mixer.cpp engine/audio_mixer.cpp engine/music_stream.cpp)
# voxel_raster.cpp is the pure C++17 voxel rasterizer (no SDL): VXL/HVA model +
# yaw + voxels.vpl lighting + palette -> RGBA sprite (docs/re/voxel-render.md).
# Compiled into both this test binary and ra2engine from one source of truth
# (same pattern as combat.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_voxel_raster.cpp engine/voxel_raster.cpp)
# facing_cache.cpp is the pure C++17 load-time voxel facing cache (no SDL): it
# rasterizes each of kVehicleFacings quantized yaws once at load (build_facing_set)
# so the draw path can blit cached sprites (docs/re/voxel-render.md §2). Compiled
# into both this test binary and ra2engine from one source of truth (same pattern
# as voxel_raster.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_facing_cache.cpp engine/facing_cache.cpp)
# production.cpp is pure C++17 (no SDL): construction economy — the single
# BuildingType queue, credits, and power (docs/re/construction.md §§7-10).
# Compiled into both this test binary and ra2engine from one source of truth
# (same pattern as combat.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_production.cpp engine/production.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_world_power_w5.cpp)   # W5-E2 power-down
# sidebar.cpp is pure C++17 (no SDL): RA2 sidebar geometry + hit-testing
# (docs/re/construction.md §11). Compiled into both this test binary and ra2engine
# from one source of truth (same pattern as combat.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_sidebar.cpp engine/sidebar.cpp)
# ore.cpp is pure C++17 (no SDL): per-cell tiberium kind/density/variant from
# the map's overlay, harvester depletion, and the growth/spread timer
# (docs/re/economy.md §§4-5). Compiled into both this test binary and ra2engine
# from one source of truth (same pattern as combat.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_ore.cpp engine/ore.cpp)
# harvest.cpp is pure C++17 (no SDL): the harvester mission FSM -- seek ore/
# gulp/return/dock/unload, CMIN chrono warp (docs/re/economy.md §§1-3).
# Compiled into both this test binary and ra2engine from one source of truth
# (same pattern as ore.cpp/combat.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_harvest.cpp engine/harvest.cpp)
# factory.cpp is pure C++17 (no SDL): factory exit-cell (8-ring walk to the
# first free door cell + ExitCoord), primary-factory selection, and set-primary
# (docs/re/production.md §1). Compiled into both this test binary and ra2engine
# from one source of truth (same pattern as harvest.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_factory.cpp engine/factory.cpp)
# cargo.cpp is pure C++17 (no SDL): CB-1 transport/cargo -- the COUNT
# capacity can_load predicate, LIFO next_eject, the facing-scatter eject_cell
# picker, and the pip-only cargo_total_size metric
# (docs/re/transport-cargo.md). Compiled into both this test binary and
# ra2engine from one source of truth (same pattern as factory.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_cargo.cpp engine/cargo.cpp)
# flight.cpp is pure C++17 (no SDL): CB-2 Aircraft -- the step_altitude
# climb/descend integrator, the straight-line cruise_step, the shortest-arc
# heading_toward, altitude_to_screen_dy, and advance_flight_state
# (docs/re/aircraft.md). Compiled into both this test binary and ra2engine
# from one source of truth (same pattern as cargo.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_flight.cpp engine/flight.cpp)
# ai_base.cpp is pure C++17 (no SDL): the AI-1 base-planning brain --
# difficulty mods, urgency ordering (next_build), placement scan
# (find_build_location) over a read-only BaseView (docs/re/ai-base.md).
# Compiled into both this test binary and ra2engine from one source of truth
# (same pattern as factory.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_ai_base.cpp engine/ai_base.cpp)
# aimd_parse.cpp is pure C++17 (no SDL): the AI-2 aimd.ini parser --
# TaskForce/ScriptType/TeamType tables via the existing ra2assets::Ini
# ordered-section walk (docs/re/ai-team.md §1). Compiled into both this test
# binary and ra2engine from one source of truth (same pattern as ai_base.cpp).
target_sources(ra2assets_tests PRIVATE tests/test_aimd.cpp engine/aimd_parse.cpp)
# ai_team.cpp is pure C++17 (no SDL): the AI-2 offensive-team brain -- lifecycle
# (Recruiting/Gathering/Executing/Disbanding) + the ScriptType interpreter over
# a read-only TeamView (docs/re/ai-team.md §2/§3). Compiled into both this test
# binary and ra2engine from one source of truth (same pattern as aimd_parse.cpp).
target_sources(ra2assets_tests PRIVATE tests/test_ai_team.cpp engine/ai_team.cpp)
# ai_trigger.cpp is pure C++17 (no SDL): the AI-3 AITriggerTypes weighted
# trigger engine -- condition eval + enable filters + weighted-random selection
# + adaptive weight dynamics over a read-only TriggerView (docs/re/ai-trigger.md).
# Compiled into both this test binary and ra2engine from one source of truth
# (same pattern as ai_team.cpp above).
target_sources(ra2assets_tests PRIVATE tests/test_ai_trigger.cpp engine/ai_trigger.cpp)
# superweapon.cpp is pure C++17 (no SDL/World/RNG): the CB-4 SuperweaponType enum
# (recovered [SuperWeaponTypes] roster) + the per-house recharge-timer lifecycle
# (tick/is_ready/on_fired/charge_fraction) + the id<->Type mapping helpers
# (docs/re/superweapon.md §1/§4). Header-inline lifecycle; the .cpp holds only the
# enum<->id map, exercised by test_superweapon.cpp.
target_sources(ra2assets_tests PRIVATE tests/test_superweapon.cpp engine/superweapon.cpp)
# Engineer/Spy Capture phase [RE: capture.md]: pure capture arithmetic
# (cash-steal/blackout/veteran/startup). capture.cpp is header-inline (one anchor
# symbol); the World mutation entry points live in engine/world.cpp (already on
# test_world's line), so capture.cpp is NOT added to test_world's line.
target_sources(ra2assets_tests PRIVATE tests/test_capture.cpp engine/capture.cpp)
# Shroud/Fog phase [RE: shroud.md]: pure near-circular reveal math
# (engine/visibility.cpp) exercised by test_visibility.cpp; the World-layer
# visibility grids + step_visibility live in engine/world.cpp (already on
# test_world's line).
target_sources(ra2assets_tests PRIVATE tests/test_visibility.cpp engine/visibility.cpp)
# Repair & Sell phase [RE: repair-sell.md]: pure repair/sell arithmetic + the
# refusal predicates (14-frame global cadence, the two-integer-division
# stepCost, the FLAT refund, the survivor divisors, the mirrored buildup
# frame). World-free -- it sees only Building/BuildingTypeDef, like
# production.cpp. The World mutation entry points live in engine/world.cpp
# (already on test_world's line), so world.cpp is NOT added here.
target_sources(ra2assets_tests PRIVATE tests/test_repair_sell.cpp engine/repair_sell.cpp)
# Projectile flight phase (P2) [RE: projectile.md]: the pure BulletClass core --
# launch solver (Fire), ballistic/vertical integrators (Update), scatter, and the
# deterministic projectile RNG. World-free; the collision/detonation pass lives in
# engine/world.cpp (already on test_world's line).
target_sources(ra2assets_tests PRIVATE tests/test_projectile.cpp engine/projectile.cpp
                                       tests/test_rocket.cpp engine/rocket.cpp)
# P9 Map triggers [RE: map-triggers.md]: the pure map-trigger parser + runtime
# (TagClass/TriggerClass/TEvent/TAction semantics over an abstract TriggerHost).
# World-free; compiled into both this test binary and ra2engine.
target_sources(ra2assets_tests PRIVATE tests/test_map_trigger.cpp engine/map_trigger.cpp)
# P10 Campaign missions [RE: campaign.md]: control files, [Basic], object
# records, BorrowedTime mission end, map-team script interpreter. World-free.
target_sources(ra2assets_tests PRIVATE tests/test_campaign.cpp engine/campaign.cpp)
# P5 Veterancy + Crates [RE: veterancy.md]: the pure veterancy formulas and the
# crate RNG/powerup math. engine/world.cpp (already on test_world's line) calls
# into both, so they are compiled into the test binary here once.
target_sources(ra2assets_tests PRIVATE tests/test_veterancy.cpp engine/veterancy.cpp
                                       tests/test_crate.cpp engine/crate.cpp)
# P6 Roster breadth + remaining producers [RE: roster2.md]: the PURE wall
# (mask/frame/line-fill/3x time), water-bound naval yard (placement/exit/
# category split) and airfield (pad coords, 270-frame rearm, sortie) mechanics.
# World-free; factory.cpp/production.cpp are already on earlier test lines.
target_sources(ra2assets_tests PRIVATE tests/test_roster2.cpp engine/roster2.cpp)
# P4 Unit special abilities [RE: abilities.md]: the pure ability arithmetic +
# the radiation field (engine/abilities.cpp, World-free) and the World-side
# ability pass + orders (engine/world_abilities.cpp, a World member TU split out
# of world.cpp), exercised by test_abilities.cpp.
target_sources(ra2assets_tests PRIVATE engine/world_chaos.cpp tests/test_chaos.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_abilities.cpp tests/test_abilities_world.cpp engine/abilities.cpp engine/world_abilities.cpp engine/world_w5.cpp engine/world_w6.cpp engine/world_w7.cpp engine/world_drain.cpp tests/test_w6_missions.cpp tests/test_w7_combat.cpp engine/abil_w5_rules.cpp tests/test_abil_w5.cpp engine/world_mcv.cpp tests/test_mcv_deploy.cpp
                 engine/world_sw.cpp engine/world_an.cpp engine/ai_unit.cpp tests/test_ai_w7.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_w7r2.cpp)   # w7r2: wave-7 mission-script restore fixes
target_sources(ra2assets_tests PRIVATE tests/test_w8_layers.cpp)   # w8fix-layer: deck / ground layers

# Skirmish Shell: the PURE config->World core (SkirmishConfig/configure_skirmish_
# houses/default_skirmish_config). engine/world.cpp is already on test_world's
# line above, so only the new module + its test are added here.
target_sources(ra2assets_tests PRIVATE tests/test_skirmish.cpp engine/skirmish.cpp)
# Skirmish page (dialog 0x102): the pure lobby model + RA2.INI persistence +
# LobbyState -> SkirmishConfig converter (docs/gap/07-skirmish-page.md).
target_sources(ra2assets_tests PRIVATE tests/test_skirmish_lobby.cpp engine/skirmish_lobby.cpp)
# Multiplayer shell (engine/mp/, SDL-free): options string, 0x1236 framing,
# lobby state machines over memory + loopback UDP, LAN beacons, JSON.
target_sources(ra2assets_tests PRIVATE tests/test_mp.cpp engine/mp/mp_socket.cpp engine/mp/mini_json.cpp
               engine/mp/lobby_wire.cpp engine/mp/mp_net.cpp engine/mp/mp_model.cpp engine/mp/mp_lobby.cpp
               engine/mp/lan_discovery.cpp engine/mp/server_link.cpp)
if (WIN32)
  target_link_libraries(ra2assets_tests PRIVATE ws2_32)
endif()
# rectfont.cpp is pure C++17 (no SDL): the 5x7 rect-font glyph table + glyph_rows
# extending the kDigits technique to A-Z/0-9/space/basic punctuation for menu
# labels (Task 5, [RE: skirmish.md §UI]). Its coverage cases live in
# test_skirmish.cpp above; draw_text (the SDL consumer) lives in engine/main.cpp.
target_sources(ra2assets_tests PRIVATE engine/rectfont.cpp)
# P8 Options + persistence: the PURE settings layer (RA2.INI IniDoc round-trip,
# OptionsClass defaults/clamps/save list, slider/trackbar math, the recovered
# edge-scroll table) [RE: docs/re/options.md]. SDL-free; main.cpp owns the UI.
target_sources(ra2assets_tests PRIVATE tests/test_settings.cpp engine/settings.cpp)
# G0 --play harness: the pure script parser / assertion evaluator / play_hash.
target_sources(ra2assets_tests PRIVATE tests/test_play_script.cpp engine/play_script.cpp)
# P7 Save/Load [RE: docs/re/saveload.md]: the archive core, field visitors,
# file container + slots. SDL-free.
target_sources(ra2assets_tests PRIVATE tests/test_saveload.cpp engine/savegame.cpp)
target_sources(ra2assets_tests PRIVATE tests/test_save_states.cpp)   # W8-06-01 save states + W8-05-07 paradrop
# The saveload source-scan tripwire reads engine/*.hpp to prove every World /
# MapTriggerSystem data member is visited by the serializer.
target_compile_definitions(ra2assets_tests PRIVATE RA2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# engine/net (original-format networking) [RE: docs/re/net-protocol.md].
target_sources(ra2assets_tests PRIVATE ${RA2_NET_SOURCES}
    tests/test_net_wire.cpp tests/test_net_event.cpp tests/test_net_lockstep.cpp
    tests/test_net_play.cpp engine/net_frame_crc.cpp tests/test_w9_speed.cpp   # NET-PLAY: two real Worlds in lockstep (+ W9 frame CRC)
    tests/test_w9_match.cpp)   # W9-05: diplomacy / victory / DESTRUCT / BuildOffAlly / beacons / UI tripwire
target_include_directories(ra2assets_tests PRIVATE third_party include engine)
target_link_libraries(ra2assets_tests PRIVATE ra2assets)
if (WIN32)
  target_link_libraries(ra2assets_tests PRIVATE ws2_32)
endif()
# Two-process LAN test driver: two peers + a tiny deterministic model.
# command_bridge.cpp needs the engine's Command/World headers; the stub-model
# driver uses only the SDL-free transport, codec and lockstep layers.
set(RA2_NETPEER_SOURCES ${RA2_NET_SOURCES})
list(FILTER RA2_NETPEER_SOURCES EXCLUDE REGEX "(command_bridge|net_game)\\.cpp$")
add_executable(ra2netpeer tools/ra2netpeer.cpp ${RA2_NETPEER_SOURCES})
target_include_directories(ra2netpeer PRIVATE engine)
if (WIN32)
  target_link_libraries(ra2netpeer PRIVATE ws2_32 winmm)
  if (MINGW)
    target_link_options(ra2netpeer PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()
add_test(NAME ra2assets_tests COMMAND ra2assets_tests)

add_executable(ra2extract tools/ra2extract.cpp)
target_include_directories(ra2extract PRIVATE third_party)
target_link_libraries(ra2extract PRIVATE ra2assets)
# W3-FIX-A: the art-resolution census (developer tool, not built by default).
add_executable(art_census EXCLUDE_FROM_ALL tools/art_census.cpp)
# simdiff harness, L0 rung (docs/re/simdiff-harness.md): GameRandom draw stream.
add_executable(simdiff_l0 EXCLUDE_FROM_ALL tools/simdiff/l0_clone.cpp engine/crate.cpp)
target_link_libraries(art_census PRIVATE ra2assets)

option(RA2_BUILD_ENGINE "Build the SDL2 engine/app (needs SDL2)" ON)
if (RA2_BUILD_ENGINE)
  include(FetchContent)
  # W6-06-14: pinned release tarball + hash (cmake/sdl2_pin.cmake).
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl2_pin.cmake)
  FetchContent_Declare(SDL2
    URL ${RA2_SDL2_URL}
    URL_HASH SHA256=${RA2_SDL2_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  # Net L0: RA2_ANDROID_CLI (with the NDK toolchain) builds ra2engine as a plain
  # Android EXECUTABLE running the desktop command line (--demo-*, --play,
  # --ai-soak) under SDL's dummy video/audio drivers -- the cross-platform
  # determinism gate (tools/xplat_gate.ps1, docs/DEVELOPMENT.md "Networking L0").
  option(RA2_ANDROID_CLI "Android: build the CLI gate executable (static SDL)" OFF)
  if (ANDROID AND RA2_ANDROID_CLI)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  else()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
  endif()
  # W2-AR-30: SDL's internal SDL_assert()s are compiled OUT of optimised
  # builds (level 1 = SDL_assert_release only); a Debug build keeps them
  # (level 2) so bugs like W2-AR-01 stay visible. Either way the engine
  # installs a non-blocking assertion handler (logs, never waits on stdin).
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SDL_ASSERTIONS "enabled" CACHE STRING "SDL internal assertions (W2-AR-30)")
  else()
    set(SDL_ASSERTIONS "release" CACHE STRING "SDL internal assertions (W2-AR-30)")
  endif()
  FetchContent_MakeAvailable(SDL2)
  # Engine source list comes from cmake/ra2_sources.cmake (shared with the
  # Android jni build so the two cannot drift).
  # R-182: the engine is compiled ONCE into an object library and linked
  # twice: ra2engine (console subsystem -- the developer / CI binary every
  # --play, --demo-* and gate run uses) and, on Windows, ra2game (WIN32
  # subsystem: a double-click opens no console window; the log goes to
  # %APPDATA%\ra2engine\RA2\ra2engine.log). Same objects, same behaviour.
  add_library(ra2engine_obj OBJECT ${RA2_ENGINE_SOURCES})
  # audio_sdl.cpp is main.cpp's SDL audio-sink companion (Task 6 of the Audio
  # Playback phase, task-6-brief.md Step 1/Files). Added directly to this
  # target (and to android/app/jni/CMakeLists.txt) rather than into the
  # shared RA2_ENGINE_SOURCES list in cmake/ra2_sources.cmake, per the task
  # brief -- it is not part of ra2assets_tests either way.
  target_sources(ra2engine_obj PRIVATE engine/audio_sdl.cpp)
  # W2-AR-23 version string; Q3: no FMA contraction, so float paths match
  # the Android (clang, arm64) build bit-for-bit.
  target_compile_definitions(ra2engine_obj PRIVATE RA2_VERSION="${RA2_VERSION}")
  target_compile_options(ra2engine_obj PRIVATE -ffp-contract=off)
  # third_party for the vendored stb_image_write.h (screenshot PNG output);
  # STB_IMAGE_WRITE_IMPLEMENTATION is defined in exactly one TU (engine/main.cpp).
  target_include_directories(ra2engine_obj PRIVATE include engine ${RA2_GENERATED_DIR})   # W6-06-16 notices
  # W2-AR-25: warnings on for the engine; vendored headers are SYSTEM so
  # their own warnings (stb) stay out of the count.
  target_include_directories(ra2engine_obj SYSTEM PRIVATE third_party)
  target_compile_options(ra2engine_obj PRIVATE -Wall -Wextra)
  # Usage requirements (include dirs, defines) of ra2assets + SDL2 for the objects.
  if (ANDROID AND RA2_ANDROID_CLI)
    target_link_libraries(ra2engine_obj PRIVATE ra2assets SDL2::SDL2-static)
  else()
    target_link_libraries(ra2engine_obj PRIVATE ra2assets SDL2::SDL2)
  endif()
  add_executable(ra2engine $<TARGET_OBJECTS:ra2engine_obj>)
  # NOTE: link order deviates from the brief's literal listing (SDL2 then
  # SDL2main) to (SDL2main then SDL2). Under MinGW/GNU ld, static archives
  # are resolved in a single left-to-right pass: SDL2main.a's WinMain is
  # pulled in via `-Wl,--undefined=WinMain` and internally references SDL_*
  # symbols (SDL_Init, SDL_free, ...) that live in SDL2's import lib. If
  # SDL2main.a is placed after SDL2's import lib on the command line, GNU ld
  # has already finished scanning the SDL2 import lib by the time it
  # discovers those undefined refs and does not revisit it, so the link
  # fails ("undefined reference to SDL_ShowSimpleMessageBox" etc). Listing
  # SDL2main before SDL2 (the standard `-lSDL2main -lSDL2` convention used
  # by every MinGW SDL2 tutorial) fixes this; MSVC's linker is not
  # order-sensitive this way, which is why the brief's literal order can
  # look fine there. No SDL2 version/flags changed.
  if (ANDROID AND RA2_ANDROID_CLI)
    target_compile_definitions(ra2engine_obj PRIVATE RA2_ANDROID_CLI=1)
    target_link_libraries(ra2engine PRIVATE ra2assets SDL2::SDL2-static log android)
  else()
    target_link_libraries(ra2engine PRIVATE ra2assets SDL2::SDL2main SDL2::SDL2)
  endif()
  if (WIN32)
    # W2-AR-22: registry install-path lookup + the BROWSE folder picker
    # (engine/platform_os.cpp).
    target_link_libraries(ra2engine PRIVATE advapi32 shell32 ole32 winmm ws2_32)
    # R-182: the player's double-click binary (no console window).
    add_executable(ra2game WIN32 $<TARGET_OBJECTS:ra2engine_obj>)
    target_link_libraries(ra2game PRIVATE ra2assets SDL2::SDL2main SDL2::SDL2 advapi32 shell32 ole32 winmm ws2_32)
    # W6-06-02: a VERSIONINFO resource (version from VERSION + git).
    if (NOT RA2_VERSION_BASE)
      file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RA2_VERSION_BASE)
      string(STRIP "${RA2_VERSION_BASE}" RA2_VERSION_BASE)
    endif()
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ra2v "${RA2_VERSION_BASE}")
    set(RA2_VER_MAJOR ${CMAKE_MATCH_1})
    set(RA2_VER_MINOR ${CMAKE_MATCH_2})
    set(RA2_VER_PATCH ${CMAKE_MATCH_3})
    enable_language(RC)
    configure_file(cmake/ra2engine.rc.in ${CMAKE_CURRENT_BINARY_DIR}/ra2engine.rc @ONLY)
    target_sources(ra2engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ra2engine.rc)
    target_sources(ra2game PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ra2engine.rc)
    # W6-06-02: a MinGW build needs libstdc++-6 / libgcc_s_seh-1 /
    # libwinpthread-1 at run time; link them statically so ra2engine.exe /
    # ra2game.exe run from a clean folder with only SDL2.dll beside them.
    # (-static only changes how -l libraries are searched; SDL2's import
    # library is passed by full path, so SDL2.dll stays a DLL.)
    if (MINGW)
      foreach (_t ra2engine ra2game)
        target_link_options(${_t} PRIVATE -static -static-libgcc -static-libstdc++)
      endforeach()
    endif()
  endif()
  # SDL_SHARED=ON builds SDL2.dll; copy it next to ra2engine.exe post-build
  # so the exe can find it at runtime (Windows has no rpath).
  if (NOT (ANDROID AND RA2_ANDROID_CLI))
    add_custom_command(TARGET ra2engine POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:ra2engine>)
  endif()
endif()

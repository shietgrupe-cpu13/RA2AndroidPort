// W4-SQ-10: destructible high bridges on a real World (the deck carries ground
// units while intact, a break takes the whole span, a hut repairs it).
#include "doctest.h"
#include "asset_fixture.hpp"
#include "harvest.hpp"   // OreValues (set_economy)
#include "world.hpp"
#include "bridges.hpp"
#include "ground_presentation.hpp"

using namespace ra2engine;

TEST_CASE("high bridges: resolved outer lanes retain separate ground and deck, collapse and repair") {
    for (int overlay : {0x18, 0x19, 0xED, 0xEE}) {
        CAPTURE(overlay);
        auto map = ra2assets::MapFile::parse("[Map]\nSize=0,0,32,32\nLocalSize=0,0,32,32\n");
        auto& cells = const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
            cells.push_back({x,y,0,0,0});
        World w(map);
        const int W = w.grid_w();
        const std::size_t n = static_cast<std::size_t>(W) * w.grid_h();
        std::vector<ra2assets::LandType> land(n, ra2assets::LandType::Water);
        w.set_land_terrain(land);
        std::vector<std::uint8_t> hb(n, 0);
        int xs[4], ys[4];
        REQUIRE(high_bridge_section_cells(overlay, 25, 25, xs, ys));
        for (int k = 0; k < 4; ++k) hb[ys[k] * W + xs[k]] = 1;
        w.set_high_bridges(hb);
        World::LandSpeedRules rules;
        w.arm_land_speed(std::vector<std::uint8_t>(n, static_cast<std::uint8_t>(ra2assets::LandType::Water)),
                         std::vector<std::uint8_t>(n, 0), rules);
        for (int k = 0; k < 4; ++k) {
            CHECK(w.high_bridge_cell(xs[k], ys[k]));
            CHECK(w.walkable(xs[k], ys[k]));
            CHECK(w.grid_for(MovementDomain::Water)[ys[k] * W + xs[k]] != 0);
            CHECK((*w.deck_grid(MovementDomain::Land))[ys[k] * W + xs[k]] == 2);
        }
        const bool north = overlay == 0x18 || overlay == 0xED;
        CHECK_FALSE(w.high_bridge_cell(north ? 25 : 22, north ? 22 : 25));
        CHECK_FALSE(w.walkable(north ? 25 : 22, north ? 22 : 25));
        Unit& u = w.add_unit(xs[2], ys[2], "GI");
        w.sync_deck_layer(u);
        CHECK(u.on_deck == 1);
        CHECK(unit_on_high_bridge(w, u));
        u.on_deck = 0; CHECK_FALSE(unit_on_high_bridge(w, u));
        u.on_deck = 1; u.is_naval = true; CHECK_FALSE(unit_on_high_bridge(w, u));
        u.is_naval = false; u.is_aircraft = true; u.altitude = 1;
        CHECK_FALSE(unit_on_high_bridge(w, u));
        u.altitude = 0; CHECK(unit_on_high_bridge(w, u));   // landed jumpjet
        u.is_aircraft = false; u.cargo_of = 0; CHECK_FALSE(unit_on_high_bridge(w, u));
        u.cargo_of = -1;
        CHECK(w.force_bridge_break(xs[2], ys[2]) == 4);
        CHECK_FALSE(unit_on_high_bridge(w, u));
        CHECK(u.hp <= 0);
        for (int k = 0; k < 4; ++k) {
            CHECK(w.high_bridge_destroyed(xs[k], ys[k]));
            CHECK_FALSE(w.walkable(xs[k], ys[k]));
        }
        const int hut = w.add_building(0, 27, 27, 1, 1, 0, 100, 0, 0.0, true);
        REQUIRE(hut >= 0);
        CHECK(w.repair_high_bridge_near(hut) == 4);
        for (int k = 0; k < 4; ++k) CHECK(w.walkable(xs[k], ys[k]));
    }
}

TEST_CASE("high bridges: deck passable while intact, broken by the trigger call, repaired by a hut") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    const int W = w.grid_w();
    REQUIRE(W > 40);
    // A 5-cell deck over "water" (1) along y = 20, x = 10..14, with a land
    // ramp cell (2) at each end.
    std::vector<std::uint8_t> hb(static_cast<std::size_t>(W) * w.grid_h(), 0);
    hb[20 * W + 9] = 2;
    for (int x = 10; x <= 14; ++x) hb[20 * W + x] = 1;
    hb[20 * W + 15] = 2;
    w.set_high_bridges(hb);
    for (int x = 10; x <= 14; ++x) CHECK(w.walkable(x, 20));
    CHECK(w.high_bridge_cell(12, 20));
    CHECK_FALSE(w.high_bridge_destroyed(12, 20));
    CHECK(w.bridge_cells_destroyed() == 0);

    // Apply100Damage's FUN_00586BB0 call: the whole span goes.
    CHECK(w.force_bridge_break(12, 20) == 7);
    for (int x = 10; x <= 14; ++x) CHECK_FALSE(w.walkable(x, 20));
    CHECK(w.high_bridge_destroyed(9, 20));
    CHECK(w.bridge_cells_destroyed() == 7);
    CHECK(w.force_bridge_break(12, 20) == 0);   // nothing left to break

    // A hut within reach of the span restores it.
    const int hut = w.add_building(0, 16, 21, 1, 1, 0, 100, 0, 0.0, true);
    REQUIRE(hut >= 0);
    CHECK(w.repair_high_bridge_near(hut) == 7);
    for (int x = 10; x <= 14; ++x) CHECK(w.walkable(x, 20));
    CHECK(w.bridge_cells_destroyed() == 0);
}

// R-210 bridges and cliffs: the World side -- per-domain deck grids, the deck
// layer a unit stands on, and the Gate=yes cells that open for allies only.
TEST_CASE("R-210 World: deck grids per domain, the deck layer, allied gates") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    const int W = w.grid_w();
    const std::size_t n = static_cast<std::size_t>(W) * w.grid_h();
    std::vector<std::uint8_t> hb(n, 0);
    hb[20 * W + 9] = 2;
    for (int x = 10; x <= 14; ++x) hb[20 * W + x] = 1;
    hb[20 * W + 15] = 2;
    w.set_high_bridges(hb);
    CHECK_FALSE(w.terrain_armed());   // no heights: the 2-D search
    World::LandSpeedRules r;
    w.arm_land_speed(std::vector<std::uint8_t>(n, 0), std::vector<std::uint8_t>(n, 0), r);
    CHECK(w.terrain_armed());

    const std::vector<std::uint8_t>* dl = w.deck_grid(MovementDomain::Land);
    const std::vector<std::uint8_t>* da = w.deck_grid(MovementDomain::Amphibious);
    REQUIRE(dl);
    REQUIRE(da);
    CHECK((*dl)[20 * W + 12] == 2);   // over water: deck only for a land mover
    CHECK((*dl)[20 * W + 9] == 1);    // over land: deck and ground
    CHECK((*da)[20 * W + 12] == 1);   // an amphibious mover may swim under it
    CHECK(w.deck_grid(MovementDomain::Water) == nullptr);

    Unit& u = w.add_unit(12, 20, "GI");
    w.sync_deck_layer(u);
    CHECK(u.on_deck == 1);
    PathTerrain t;
    std::vector<int> gates;
    REQUIRE(w.terrain_for(u, t, gates) != nullptr);
    CHECK(t.start_layer == 1);
    CHECK(t.deck == dl);

    // The span breaks: the deck grids empty out and the unit on it dies.
    CHECK(w.force_bridge_break(12, 20) == 7);
    CHECK((*w.deck_grid(MovementDomain::Land))[20 * W + 12] == 0);
    CHECK(u.hp <= 0);

    // Gate=yes: closed for movement, open to the owner and its allies.
    std::vector<BuildingTypeDef> types(1);
    types[0].id = "GAGATE_A";
    types[0].fw = 3;
    types[0].w6.gate = true;
    w.set_economy(types, {}, OreValues{}, 1000.0, 0.0, 0.0);
    const int gb = w.add_building(0, 30, 30, 3, 1, 0, 100, 0, 0.0, true);
    REQUIRE(gb >= 0);
    CHECK_FALSE(w.walkable(31, 30));
    for (int h = 0; h < 2; ++h) w.set_house(h, {World::Side::Allied, h + 1, "", h, true});
    std::vector<int> open;
    w.open_gates_for(0, open);
    CHECK(open.size() == 3);
    w.open_gates_for(1, open);
    CHECK(open.empty());
    w.set_ally_view(0, 1, true);   // the OWNER's view decides (FUN_004F9870)
    w.open_gates_for(1, open);
    CHECK(open.size() == 3);
}

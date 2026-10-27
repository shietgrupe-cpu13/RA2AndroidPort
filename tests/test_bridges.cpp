// G16 (agent G): low-bridge overlay classes and the section state machine
// recovered from gamemd.exe (docs/re/world-depth.md §B).
#include "doctest.h"
#include "bridges.hpp"

#include <map>

using namespace ra2engine;

TEST_CASE("high bridges: anchor owns four structural cells in the native direction") {
    for (int overlay : {0x18, 0x19, 0xED, 0xEE}) {
        CAPTURE(overlay);
        int xs[4], ys[4];
        REQUIRE(high_bridge_section_cells(overlay, 20, 30, xs, ys));
        const bool north = overlay == 0x18 || overlay == 0xED;
        CHECK(xs[0] == 20); CHECK(ys[0] == 30);
        CHECK(xs[1] == (north ? 20 : 19)); CHECK(ys[1] == (north ? 29 : 30));
        CHECK(xs[2] == (north ? 20 : 18)); CHECK(ys[2] == (north ? 28 : 30));
        CHECK(xs[3] == (north ? 20 : 21)); CHECK(ys[3] == (north ? 31 : 30));
        // Side flags on the third forward cell do not grant a structural deck.
        for (int i = 0; i < 4; ++i)
            CHECK_FALSE((xs[i] == (north ? 20 : 17) && ys[i] == (north ? 27 : 30)));
    }
    int xs[4] = {}, ys[4] = {};
    for (int overlay : {-1, 0, 0x4A, 0x65, 0xCD, 255})
        CHECK_FALSE(high_bridge_section_cells(overlay, 20, 30, xs, ys));
}

TEST_CASE("bridges: runtime overlay classes (FUN_00586BB0 dispatch)") {
    CHECK(bridge_class_of_overlay(0x4A) == BridgeClass::LowWood);       // LOBRDG01
    CHECK(bridge_class_of_overlay(0x65) == BridgeClass::LowWood);       // LOBRDG28 (destroyed)
    CHECK(bridge_class_of_overlay(0x66) == BridgeClass::None);
    CHECK(bridge_class_of_overlay(0xCD) == BridgeClass::LowConcrete);   // LOBRDB01
    CHECK(bridge_class_of_overlay(0x18) == BridgeClass::HighHead);      // BRIDGE1
    CHECK(bridge_class_of_overlay(0xED) == BridgeClass::HighHead);      // BRIDGEB1
    CHECK(low_bridge_destroyed(100));
    CHECK(low_bridge_destroyed(101));
    CHECK(low_bridge_destroyed(231));
    CHECK_FALSE(low_bridge_destroyed(0x50));
}

TEST_CASE("bridges: section destroy steps (FUN_0057B720 / FUN_0057BCE0)") {
    CHECK(low_bridge_axis(0x4A) == LowAxis::ColumnY);
    CHECK(low_bridge_axis(0x53) == LowAxis::RowX);
    CHECK(low_bridge_next_stage(0x4A) == 0x50);   // intact -> damaged
    CHECK(low_bridge_next_stage(0x50) == 100);    // damaged -> destroyed
    CHECK(low_bridge_next_stage(100) == 100);     // destroyed stays
    CHECK(low_bridge_next_stage(0x5C) == 0x5D);   // end piece, once
    CHECK(low_bridge_next_stage(0x53) == 0x59);
    CHECK(low_bridge_next_stage(0x59) == 0x65);
    CHECK(low_bridge_next_stage(0x62) == 99);
    CHECK(low_bridge_next_stage(0x4A + 131) == 0x50 + 131);   // concrete mirror
}

TEST_CASE("bridges: cross-section centring (FUN_0057B4D0)") {
    // A ColumnY bridge 3 cells wide in y (rows 10..12) along x.
    std::map<std::pair<int, int>, int> m;
    for (int x = 0; x < 6; ++x)
        for (int y = 10; y <= 12; ++y) m[{x, y}] = 0x4A;
    auto at = [&](int x, int y) {
        auto it = m.find({x, y});
        return it == m.end() ? -1 : it->second;
    };
    int cx = 0, cy = 0;
    low_bridge_section_centre(3, 10, at, cx, cy);   // top row -> centre one below
    CHECK(cx == 3);
    CHECK(cy == 11);
    low_bridge_section_centre(3, 12, at, cx, cy);   // bottom row -> centre one above
    CHECK(cy == 11);
    low_bridge_section_centre(3, 11, at, cx, cy);
    CHECK(cy == 11);
    int xs[3], ys[3];
    low_bridge_section_cells(3, 11, LowAxis::ColumnY, xs, ys);
    CHECK(ys[0] == 10);
    CHECK(ys[2] == 12);
    CHECK(xs[1] == 3);
}

TEST_CASE("bridges: OverlayTypes runtime order skips the ini numbering gaps") {
    const std::vector<std::pair<std::string, std::string>> sec = {
        {"1", "a"}, {"2", "b ;c"}, {"42", "lobrdg01"}};
    const std::vector<std::string> n = overlay_names_in_order(sec);
    REQUIRE(n.size() == 3);
    CHECK(n[1] == "B");
    CHECK(n[2] == "LOBRDG01");
}

TEST_CASE("bridges: repair maps every damaged/destroyed piece back to an intact one") {
    CHECK(low_bridge_repaired(100) == 0x4A);
    CHECK(low_bridge_repaired(0x51) == 0x4A);
    CHECK(low_bridge_repaired(0x65) == 0x53);
    CHECK(low_bridge_repaired(0x5A) == 0x53);
    CHECK(low_bridge_repaired(99) == 0x62);
    CHECK(low_bridge_repaired(0x5D) == 0x5C);
    CHECK(low_bridge_repaired(0x53) == 0x53);       // intact stays
    CHECK(low_bridge_repaired(0x65 + 131) == 0x53 + 131);
    for (int i = 0x4A; i < 0x66; ++i) {             // repair(next(i)) is never destroyed
        CHECK_FALSE(low_bridge_destroyed(low_bridge_repaired(low_bridge_next_stage(i))));
    }
}

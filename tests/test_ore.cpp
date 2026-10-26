#include "doctest.h"
#include "../engine/ore.hpp"
#include <limits>
using namespace ra2engine;

TEST_CASE("ore: disabled and invalid legacy intervals do not hang or consume RNG") {
 OreField f(4,4);std::vector<std::uint8_t> pass(16,1);OreRng rng;
 const auto before=rng.s;
 for(double interval:{0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
  f.step(1.0/15,interval,0.1,0.1,pass,rng);CHECK(rng.s==before);
 }
}

TEST_CASE("ore: overlay classification [RE-GATE pre-hunt key-1 table]") {
    // NOTE: three literals below (123->121, 168->166, variant index 110->108)
    // are corrected from the brief's draft by +/-2 vs. the pre-hunt numbers:
    // with base=102/127/147 and a strict 20-wide (12 flat + 8 slope) block
    // per economy.md §5, base+20 is the first INVALID index (last valid is
    // base+19), and TIB07 (0-indexed variant 6) sits at base+6. The original
    // literals (123, 168, 110) were 2 past those correct boundaries -- an
    // arithmetic slip in the pre-hunt draft. Keeping the drafted literals
    // would require shifting the ore bases to 104/129/149, which would
    // misclassify the empirically-confirmed real-map overlay index 102
    // (Task 2's dump) as None -- a real functional regression. Corrected here
    // to match the RESOLVED calibration (economy.md §5) verbatim ranges.
    CHECK(overlay_ore_kind(27) == OreKind::Gems);
    CHECK(overlay_ore_kind(38) == OreKind::Gems);
    CHECK(overlay_ore_kind(104) == OreKind::Ore);
    CHECK(overlay_ore_kind(121) == OreKind::Ore);   // last valid index of TIB01's block
    CHECK(overlay_ore_kind(129) == OreKind::Ore);
    CHECK(overlay_ore_kind(166) == OreKind::Ore);   // last valid index of TIB3_01's block
    CHECK(overlay_ore_kind(26) == OreKind::None);
    CHECK(overlay_ore_kind(39) == OreKind::None);
    CHECK(overlay_ore_kind(-1) == OreKind::None);
    CHECK(overlay_ore_variant(108) == 6);   // TIB07 (102 + 6)
    CHECK(overlay_ore_variant(27) == 0);    // GEM01
}

TEST_CASE("ore: take_bale depletes and empties") {
    OreField f(8, 8);
    f.set(3, 3, OreKind::Ore, 2, 0);
    CHECK(f.take_bale(3, 3) == OreKind::Ore);
    CHECK(f.density_at(3, 3) == 1);
    CHECK(f.take_bale(3, 3) == OreKind::Ore);
    CHECK(f.kind_at(3, 3) == OreKind::None);
    CHECK(f.take_bale(3, 3) == OreKind::None);
}

TEST_CASE("ore: growth fires exactly on the interval and is deterministic") {
    auto build = [] {
        OreField f(4, 4);
        f.set(1, 1, OreKind::Ore, 3, 0);
        return f;
    };
    std::vector<std::uint8_t> pass(16, 1);
    OreField a = build(), b = build();
    OreRng ra, rb;
    a.step(299.9, 300.0, 0.06, 0.0, pass, ra);
    CHECK(a.density_at(1, 1) == 3);              // not yet
    a.step(0.2, 300.0, 0.06, 0.0, pass, ra);
    CHECK(a.density_at(1, 1) == 4);              // 1 candidate -> n = max(1,0) = 1
    b.step(300.1, 300.0, 0.06, 0.0, pass, rb);
    CHECK(b.density_at(1, 1) == 4);              // same seed, same result
}

TEST_CASE("ore: spread only into passable empty neighbors") {
    OreField f(3, 3);
    f.set(1, 1, OreKind::Ore, kMaxOreDensity, 4);
    std::vector<std::uint8_t> pass(9, 0);
    pass[static_cast<size_t>(2) + 1 * 3] = 1;    // only (2,1) passable
    OreRng r;
    f.step(300.1, 300.0, 0.0, 0.06, pass, r);
    CHECK(f.kind_at(2, 1) == OreKind::Ore);
    CHECK(f.density_at(2, 1) == 1);
    CHECK(f.variant_at(2, 1) == 4);
    int ore_cells = 0;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if (f.kind_at(x, y) == OreKind::Ore) ++ore_cells;
    CHECK(ore_cells == 2);                        // source + exactly one seed
}

TEST_CASE("ore: gems never grow") {
    OreField f(4, 4);
    f.set(1, 1, OreKind::Gems, 3, 0);
    std::vector<std::uint8_t> pass(16, 1);
    OreRng r;
    // Gems pass growth_pct/spread_pct = 0 at the call site (per-kind rates);
    // OreField::step's growth candidate list must ALSO exclude Gems when
    // growth_pct applies to Ore only. [RE-GATE: economy.md §4 decides whether
    // rates are per-kind; pre-hunt: step() takes ORE rates and skips Gems.]
    f.step(300.1, 300.0, 0.06, 0.06, pass, r);
    CHECK(f.density_at(1, 1) == 3);
}

// W2-SK-01 [RE: gamemd TerrainClass::AI 0x71B9D0 / FUN_00483690]: an ore mine
// animates, then spawns ONE ore cell at OverlayData 3 on an empty neighbour.
TEST_CASE("ore: mine animation end spawns one neighbour cell at density 3") {
    OreField f(10, 10);
    std::vector<std::uint8_t> pass(100, 1);
    pass[5 * 10 + 5] = 0;                 // the mine itself is impassable
    f.add_mine(5, 5, /*half_frames=*/2, /*rate=*/1, /*prob=*/1.0);
    f.add_mine(5, 5, 2, 1, 1.0);          // duplicate cell ignored
    REQUIRE(f.mines().size() == 1);
    OreRng rng;
    CHECK(f.step_mines(1, pass, rng) == 0);   // stage 1 of 2
    CHECK(f.step_mines(1, pass, rng) == 1);   // stage 2 == half -> spawn
    int n = 0;
    for (int y = 4; y <= 6; ++y)
        for (int x = 4; x <= 6; ++x)
            if (f.has_ore(x, y)) {
                ++n;
                CHECK(f.density_at(x, y) == kMineSpawnDensity);
                CHECK(f.kind_at(x, y) == OreKind::Ore);
            }
    CHECK(n == 1);
    CHECK_FALSE(f.has_ore(5, 5));            // never on the mine cell
    // Run long: the 8-ring fills, then the mine stops adding cells.
    for (int i = 0; i < 200; ++i) f.step_mines(1, pass, rng);
    n = 0;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) n += f.has_ore(x, y) ? 1 : 0;
    CHECK(n == 8);
    CHECK(f.mines()[0].spawned == 8);
}

TEST_CASE("ore: mine with zero probability never animates") {
    OreField f(6, 6);
    std::vector<std::uint8_t> pass(36, 1);
    f.add_mine(2, 2, 2, 1, 0.0);
    OreRng rng;
    CHECK(f.step_mines(500, pass, rng) == 0);
}

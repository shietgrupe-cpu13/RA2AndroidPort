#include "doctest.h"
#include "../engine/harvest.hpp"
#include "../engine/diagnostics.hpp"
#include "ra2assets/asset_trace.hpp"
#include "ra2assets/mix.hpp"
#include <fstream>
#include <iterator>
#include <sstream>
using namespace ra2engine;

namespace {
struct Rig {
    std::vector<Unit> units;
    std::vector<Building> buildings;
    std::vector<BuildingTypeDef> types;   // [0] = NAREFN 4x3
    std::vector<int> ref_types{0};
    OreField ore{20, 20};
    std::vector<std::uint8_t> pass = std::vector<std::uint8_t>(400, 1);
    Occupancy occ{20, 20};  // Task 6 review fix: chrono warps reconcile this
    OreValues values;
    double credits[2] = {0, 0};
    std::vector<HarvestMove> moves;
    std::vector<HarvestEvent> events;
    Rig() {
        BuildingTypeDef d; d.id = "NAREFN"; d.fw = 4; d.fh = 3; types.push_back(d);
        Building b; b.type = 0; b.cx = 8; b.cy = 8; b.fw = 4; b.fh = 3;
        b.state = Building::State::Idle; buildings.push_back(b);
    }
    Unit& harv(int x, int y, bool chrono = false, int storage = 40) {
        Unit u; u.cell_x = x; u.cell_y = y; u.fx = x; u.fy = y;
        u.is_harvester = true; u.chrono_miner = chrono; u.storage_bales = storage;
        u.harv = HarvState::SeekOre;
        // Claim the spawn cell like World::add_unit does (the warp's
        // occupancy reconcile below is asserted against these claims). NOTE
        // the two dock-queue tests spawn two units on one cell -- the later
        // claim wins, matching that fixture's deliberate invariant bend.
        occ.set(x, y, (int)units.size());
        units.push_back(u); return units.back();
    }
    void step(double dt) {
        moves.clear(); events.clear();
        step_harvest(units, buildings, types, ref_types, ore, pass, 20, 20, occ, dt,
                     values, credits, moves, events);
    }
    // Number of cells currently claimed by unit `idx` (invariant checks).
    int claims_of(int idx) const {
        int n = 0;
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x)
                if (occ.at(x, y) == idx) ++n;
        return n;
    }
};

int count_events(const std::vector<HarvestEvent>& evs, HarvestEvent::Kind k, int unit) {
    int n = 0;
    for (const auto& e : evs) if (e.kind == k && e.unit == unit) ++n;
    return n;
}
}  // namespace

// Dock cell for the Rig's default NAREFN (cx=8, cy=8, fw=4, fh=3) is
// (cx + fw/2, cy + fh) = (10, 11) [dock_cell_x/y, harvest.hpp].

TEST_CASE("harvest: seek picks the nearest ore cell and requests a move") {
    Rig r;
    r.ore.set(3, 3, OreKind::Ore, 5, 0);
    r.ore.set(15, 15, OreKind::Ore, 5, 0);  // Chebyshev 13 from (2,2) -- farther
    r.harv(2, 2);                            // Chebyshev 1 from (3,3) -- nearest

    r.step(0.1);

    REQUIRE(r.units.size() == 1);
    CHECK(r.units[0].harv_cell_x == 3);
    CHECK(r.units[0].harv_cell_y == 3);
    REQUIRE(r.moves.size() == 1);
    CHECK(r.moves[0].unit == 0);
    CHECK(r.moves[0].gx == 3);
    CHECK(r.moves[0].gy == 3);
    CHECK(r.units[0].harv == HarvState::MoveToOre);
}

TEST_CASE("slave harvest: workers reserve separate ore cells near their manager") {
    Rig r;
    r.types[0].slave_count=2;
    r.ore.set(2,3,OreKind::Ore,5,0);
    r.ore.set(7,7,OreKind::Ore,5,0);
    r.ore.set(6,7,OreKind::Ore,5,0);
    for(int i=0;i<2;++i){auto& worker=r.harv(2+i,2);worker.w7.slave_manager=0;}
    r.step(0.1);
    REQUIRE(r.moves.size()==2);
    CHECK_FALSE((r.moves[0].gx==r.moves[1].gx && r.moves[0].gy==r.moves[1].gy));
    CHECK(r.moves[0].gy==7);CHECK(r.moves[1].gy==7);
    CHECK(r.units[0].bales_ore==0);
}

TEST_CASE("slave harvest: different manager edges unload without the truck dock queue") {
    Rig r;r.types[0].slave_count=2;
    for(auto c:{Cellxy{7,8},Cellxy{12,10}}){
        auto& worker=r.harv(c.x,c.y);worker.w7.slave_manager=0;
        worker.harv=HarvState::ReturnToRef;worker.bales_ore=1;
    }
    r.step(0.1);
    CHECK(r.units[0].harv==HarvState::Unloading);
    CHECK(r.units[1].harv==HarvState::Unloading);
    r.step(1.0);
    CHECK(r.credits[0]==50);
    CHECK(r.units[0].bales_ore==0);CHECK(r.units[1].bales_ore==0);
}

TEST_CASE("slave harvest and reload use fractional SLAV rules rates") {
    Rig r;
    auto& profile=r.types[0].slave_combat;
    profile.image="SLAV";
    profile.harvest_rate_micros=50000;
    profile.slave_reload_rate_micros=50000;
    r.ore.set(2,2,OreKind::Ore,5,0);
    auto& worker=r.harv(2,2);
    worker.w7.slave_manager=0;
    worker.harv=HarvState::Harvesting;
    r.step(0.05);
    CHECK(r.units[0].bales_ore==1);

    worker.harv=HarvState::Unloading;
    worker.harv_timer=0;
    worker.bales_ore=1;
    r.step(0.05);
    CHECK(r.units[0].bales_ore==0);
    CHECK(r.credits[0]==25);
}

TEST_CASE("harvest: gulps fill to exactly Storage then return") {
    Rig r;
    // Dense ore under the miner AND all 8 ring cells so 40 gulps are
    // available without a wide relocate (own cell provides 11, the dig
    // then walks the fixed N,NE,E,SE,S,SW,W,NW ring for the remaining 29).
    r.ore.set(5, 5, OreKind::Ore, kMaxOreDensity, 0);
    static const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (int d = 0; d < 8; ++d) r.ore.set(5 + dx[d], 5 + dy[d], OreKind::Ore, kMaxOreDensity, 0);
    r.harv(5, 5);  // storage defaults to 40 (HARV)

    r.step(0.01);  // SeekOre: own cell is nearest ore (distance 0) -> MoveToOre
    REQUIRE(r.units[0].harv == HarvState::MoveToOre);
    REQUIRE(r.units[0].harv_cell_x == 5);
    REQUIRE(r.units[0].harv_cell_y == 5);

    r.step(0.01);  // already at harv_cell, not moving -> arrived -> Harvesting
    REQUIRE(r.units[0].harv == HarvState::Harvesting);

    for (int gulp = 1; gulp <= 40; ++gulp) {
        r.step(kGulpSecs);
        if (gulp == 1) {
            CHECK(r.units[0].bales_ore == 1);
            CHECK(r.ore.density_at(5, 5) == kMaxOreDensity - 1);
        }
        if (gulp == 11) {
            // own cell exhausted exactly at gulp 11 (started at density 11)
            CHECK(r.units[0].bales_ore == 11);
            CHECK(r.ore.kind_at(5, 5) == OreKind::None);
        }
        if (gulp == 12) {
            // ring cell N (5,4) starts feeding
            CHECK(r.units[0].bales_ore == 12);
            CHECK(r.ore.density_at(5, 4) == kMaxOreDensity - 1);
        }
        if (gulp < 40) {
            CHECK(r.units[0].harv == HarvState::Harvesting);
        }
    }
    CHECK(r.units[0].bales_ore + r.units[0].bales_gems == 40);
    CHECK(r.units[0].harv == HarvState::ReturnToRef);
}

TEST_CASE("harvest: unload drains bales into house credits at the recovered rate") {
    Rig r;
    r.harv(2, 2);
    r.units[0].harv = HarvState::Unloading;
    r.units[0].dock_refinery = 0;
    r.units[0].bales_ore = 2;
    r.units[0].bales_gems = 1;
    r.units[0].harv_timer = 0.0;

    r.step(kUnloadSecsPerBale);  // drains 1 ore bale
    CHECK(r.units[0].bales_ore == 1);
    CHECK(r.credits[0] == 25);
    CHECK(r.units[0].harv == HarvState::Unloading);

    r.step(kUnloadSecsPerBale);  // drains the 2nd ore bale (ore before gems)
    CHECK(r.units[0].bales_ore == 0);
    CHECK(r.credits[0] == 50);
    CHECK(r.units[0].harv == HarvState::Unloading);

    r.step(kUnloadSecsPerBale);  // drains the last (gems) bale -> empty
    CHECK(r.units[0].bales_gems == 0);
    CHECK(r.credits[0] == 2 * 25 + 1 * 50);
    CHECK(r.units[0].harv == HarvState::SeekOre);
    CHECK(r.units[0].dock_refinery == -1);
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadEnd, 0) == 1);
}

TEST_CASE("harvest: one dock - second miner queues until the first finishes") {
    Rig r;
    r.harv(10, 11);  // unit 0, at the dock cell
    r.harv(10, 11);  // unit 1, same dock cell
    for (int i = 0; i < 2; ++i) {
        r.units[i].harv = HarvState::WaitDock;
        // WaitDock queues at the refinery STAMPED by ReturnToRef; the test
        // stamps it directly, mirroring how a unit really arrives here.
        r.units[i].dock_refinery = 0;
    }
    r.units[0].bales_ore = 5;
    r.units[1].bales_ore = 3;

    r.step(0.01);  // ascending order: unit 0 claims the free dock first
    REQUIRE(r.units[0].harv == HarvState::Unloading);
    REQUIRE(r.units[0].dock_refinery == 0);
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadStart, 0) == 1);
    // unit 1 sees the dock already taken by an Unloading unit this same tick
    CHECK(r.units[1].harv == HarvState::WaitDock);
    CHECK(r.units[1].dock_refinery == 0);  // still stamped-queued, NOT unloading
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadStart, 1) == 0);

    // Drain unit 0's 5 bales one per tick; unit 1 must stay queued throughout.
    for (int i = 0; i < 4; ++i) {
        r.step(kUnloadSecsPerBale);
        CHECK(r.units[1].harv == HarvState::WaitDock);
    }
    REQUIRE(r.units[0].bales_ore == 1);

    r.step(kUnloadSecsPerBale);  // unit 0's last bale drains -> frees the dock,
                                 // and unit 1 (processed right after, same tick)
                                 // claims it immediately.
    CHECK(r.units[0].bales_ore == 0);
    CHECK(r.units[0].harv == HarvState::SeekOre);
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadEnd, 0) == 1);
    CHECK(r.units[1].harv == HarvState::Unloading);
    CHECK(r.units[1].dock_refinery == 0);
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadStart, 1) == 1);
}

TEST_CASE("harvest: stamped dock target - far refinery going Idle mid-wait poaches nobody") {
    Rig r;
    // Second refinery (index 1) in the far corner, dock cell (2, 3); still
    // under construction, so not pickable when the miners queue.
    {
        Building b2;
        b2.type = 0; b2.cx = 0; b2.cy = 0; b2.fw = 4; b2.fh = 3;
        b2.state = Building::State::Buildup;
        r.buildings.push_back(b2);
    }
    r.harv(10, 11);  // unit 0: at refinery 0's dock (10, 11)
    r.harv(5, 6);    // unit 1: queue spot Chebyshev 3 from B's dock, 5 from A's
    for (int i = 0; i < 2; ++i) {
        r.units[i].harv = HarvState::WaitDock;
        r.units[i].dock_refinery = 0;  // both queued AT refinery 0 (stamped)
        r.units[i].bales_ore = 3;
    }

    r.step(0.01);  // unit 0 claims refinery 0's dock; unit 1 queues behind it
    REQUIRE(r.units[0].harv == HarvState::Unloading);
    REQUIRE(r.units[1].harv == HarvState::WaitDock);

    r.buildings[1].state = Building::State::Idle;  // far refinery opens mid-wait

    r.step(0.01);
    // The queued miner must NOT re-derive "nearest = refinery 1" (it IS
    // nearer to unit 1's cell) and start a second unload -- the stamp holds.
    CHECK(r.units[1].harv == HarvState::WaitDock);
    CHECK(r.units[1].dock_refinery == 0);
    int unloading = 0;
    for (const auto& u : r.units)
        if (u.harv == HarvState::Unloading) ++unloading;
    CHECK(unloading == 1);  // exactly one Unloading per refinery at any tick

    // Drain unit 0's 3 bales; unit 1 stays queued at refinery 0 throughout.
    for (int i = 0; i < 3; ++i) {
        CHECK(r.units[1].harv == HarvState::WaitDock);
        CHECK(r.units[1].dock_refinery == 0);
        r.step(kUnloadSecsPerBale);
    }
    CHECK(r.units[0].harv == HarvState::SeekOre);       // finished + freed dock
    CHECK(r.units[1].harv == HarvState::Unloading);     // took over the SAME dock
    CHECK(r.units[1].dock_refinery == 0);               // ...at refinery 0, not 1
}

TEST_CASE("harvest: CMIN full cycle - load, warp home, unload, drive back (W7-11-13)") {
    Rig r;
    r.ore.set(2, 2, OreKind::Ore, kMaxOreDensity, 0);  // 11 bales under the miner
    r.ore.set(2, 1, OreKind::Ore, 9, 0);               // + 9 in the N ring cell = 20
    r.harv(2, 2, /*chrono=*/true, /*storage=*/20);

    r.step(0.01);  // SeekOre: own cell is the nearest ore (distance 0)
    REQUIRE(r.units[0].harv == HarvState::MoveToOre);
    r.step(0.01);  // already at harv_cell, not moving -> Harvesting
    REQUIRE(r.units[0].harv == HarvState::Harvesting);

    for (int g = 0; g < 20; ++g) r.step(kGulpSecs);
    REQUIRE(r.units[0].bales_ore == 20);
    REQUIRE(r.units[0].harv == HarvState::ReturnToRef);
    REQUIRE(r.units[0].harv_cell_x == 2);
    REQUIRE(r.units[0].harv_cell_y == 2);

    // Outbound warp: ChronoOut on the first transit tick...
    r.step(kChronoWarpSecs / 2);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 0) == 1);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoIn, 0) == 0);
    CHECK(r.units[0].harv == HarvState::ReturnToRef);   // still in transit
    // ...ChronoIn a full kChronoWarpSecs later, landing ON the dock cell.
    r.step(kChronoWarpSecs / 2);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 0) == 0);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoIn, 0) == 1);
    CHECK(r.units[0].cell_x == 10);
    CHECK(r.units[0].cell_y == 11);
    REQUIRE(r.units[0].harv == HarvState::WaitDock);

    r.step(0.01);  // dock free -> Unloading
    REQUIRE(r.units[0].harv == HarvState::Unloading);
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadStart, 0) == 1);

    for (int i = 0; i < 19; ++i) r.step(kUnloadSecsPerBale);
    REQUIRE(r.units[0].bales_ore == 1);
    CHECK(r.units[0].harv == HarvState::Unloading);
    CHECK(r.credits[0] == 19 * 25);

    r.step(kUnloadSecsPerBale);  // last bale -> UnloadEnd
    CHECK(r.units[0].bales_ore == 0);
    CHECK(r.credits[0] == 20 * 25);
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadEnd, 0) == 1);
    // W7-11-13 [RE: UnitClass::SetDestination 0x740780]: no warp back -- the
    // Chrono Miner piggybacks a Drive locomotor and drives to the ore.
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 0) == 0);
    CHECK(r.units[0].harv == HarvState::SeekOre);
    CHECK(r.units[0].dock_refinery == -1);
    CHECK(r.units[0].cell_x == 10);                     // leaves the dock on its tracks
    CHECK(r.units[0].cell_y == 11);
}

TEST_CASE("harvest: CMIN warps home, HARV drives") {
    Rig r;
    r.harv(1, 1, /*chrono=*/true, /*storage=*/20);   // unit 0: CMIN, far from refinery
    r.harv(1, 2, /*chrono=*/false, /*storage=*/40);  // unit 1: HARV, far from refinery
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].bales_ore = 20;  // full
    r.units[1].harv = HarvState::ReturnToRef;
    r.units[1].bales_ore = 40;  // full

    r.step(kChronoWarpSecs);  // one full warp interval

    // CMIN: no move request, warped straight to the dock cell (10, 11).
    CHECK(r.units[0].cell_x == 10);
    CHECK(r.units[0].cell_y == 11);
    CHECK(r.units[0].harv == HarvState::WaitDock);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 0) == 1);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoIn, 0) == 1);
    for (const auto& m : r.moves) CHECK(m.unit != 0);  // no HarvestMove for unit 0

    // HARV: no chrono events, hasn't arrived. W7-04-23 [RE: 0x73DB0E,
    // HarvesterTooFarDistance=5]: farther than 5 cells it does not radio
    // (bind) the refinery yet -- it drives toward the dock and decides again.
    bool harv_move_found = false;
    for (const auto& m : r.moves) {
        if (m.unit == 1) {
            harv_move_found = true;
            CHECK_FALSE((m.gx == 12 && m.gy == 9));
        }
    }
    CHECK(harv_move_found);
    CHECK(r.units[1].dock_refinery == -1);
    CHECK(r.units[1].harv == HarvState::ReturnToRef);
    CHECK(r.units[1].cell_x == 1);
    CHECK(r.units[1].cell_y == 2);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 1) == 0);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoIn, 1) == 0);
}

TEST_CASE("harvest: determinism - two identical rigs stay identical") {
    auto build = []() {
        Rig r;
        r.harv(2, 2);
        r.harv(2, 3, /*chrono=*/true, /*storage=*/20);
        r.harv(18, 18);
        r.ore.set(3, 3, OreKind::Ore, 6, 0);
        r.ore.set(3, 2, OreKind::Gems, 4, 0);
        r.ore.set(17, 17, OreKind::Ore, 8, 1);
        r.ore.set(10, 10, OreKind::Ore, kMaxOreDensity, 0);
        return r;
    };
    Rig a = build();
    Rig b = build();

    for (int i = 0; i < 500; ++i) {
        double dt = 0.15 + 0.05 * (i % 5);  // deterministic, non-uniform cadence
        a.step(dt);
        b.step(dt);
    }

    REQUIRE(a.units.size() == b.units.size());
    for (size_t i = 0; i < a.units.size(); ++i) {
        const Unit& ua = a.units[i];
        const Unit& ub = b.units[i];
        CHECK(ua.cell_x == ub.cell_x);
        CHECK(ua.cell_y == ub.cell_y);
        CHECK(ua.fx == ub.fx);
        CHECK(ua.fy == ub.fy);
        CHECK(ua.bales_ore == ub.bales_ore);
        CHECK(ua.bales_gems == ub.bales_gems);
        CHECK((int)ua.harv == (int)ub.harv);
        CHECK(ua.dock_refinery == ub.dock_refinery);
        CHECK(ua.harv_timer == ub.harv_timer);
    }
    CHECK(a.credits[0] == b.credits[0]);
    CHECK(a.credits[1] == b.credits[1]);
}

// ---- Task 6 review fix: chrono-warp occupancy reconcile ----
// Invariant: after any warp, an alive unit holds exactly ONE occupancy
// claim, on its current cell, and a warp never lands on another unit's cell.

TEST_CASE("harvest: CMIN warp releases the origin cell and claims exactly the landing cell "
          "(both legs)") {
    Rig r;
    r.ore.set(2, 2, OreKind::Ore, kMaxOreDensity, 0);
    r.harv(2, 2, /*chrono=*/true, /*storage=*/20);   // claims (2,2)
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].harv_cell_x = 2;
    r.units[0].harv_cell_y = 2;
    r.units[0].has_harv_cell = true;  // stamped directly (bypasses SeekOre)
    r.units[0].bales_ore = 20;                        // full -> outbound warp
    REQUIRE(r.occ.at(2, 2) == 0);

    // Outbound leg: warp to the dock cell (10, 11).
    r.step(kChronoWarpSecs);
    REQUIRE(r.units[0].cell_x == 10);
    REQUIRE(r.units[0].cell_y == 11);
    CHECK(r.occ.at(2, 2) == Occupancy::kEmpty);       // (a) origin released...
    CHECK(r.occ.can_enter(2, 2, /*unit*/ 99));        // ...another unit could be pathed onto it
    CHECK(r.occ.at(10, 11) == 0);                     // landing claimed
    CHECK(r.claims_of(0) == 1);                       // (b) exactly one claim

    // Drive through WaitDock -> Unloading -> drain all 20 bales.
    r.step(0.01);
    REQUIRE(r.units[0].harv == HarvState::Unloading);
    for (int i = 0; i < 20; ++i) r.step(kUnloadSecsPerBale);
    REQUIRE(r.units[0].bales_ore == 0);
    REQUIRE(r.units[0].dock_refinery == -1);
    // W7-11-13: no return warp -- SeekOre at the dock; it drives back.
    REQUIRE(r.units[0].harv == HarvState::SeekOre);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 0) == 0);
    CHECK(r.claims_of(0) >= 1);
}

TEST_CASE("harvest: warp onto a cell occupied by another unit deflects (no overlap)") {
    Rig r;
    r.harv(2, 2, /*chrono=*/true, /*storage=*/20);    // unit 0: CMIN, claims (2,2)
    r.harv(10, 11, /*chrono=*/false, /*storage=*/40);  // unit 1: parked ON the dock cell
    r.units[1].harv = HarvState::None;                 // idle blocker, never moves
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].bales_ore = 20;
    REQUIRE(r.occ.at(10, 11) == 1);                    // blocker owns the dock cell

    r.step(kChronoWarpSecs);

    // Deflected to the first free ring cell around the dock in the recovered
    // N,NE,E,SE,S,SW,W,NW order: N = (10, 10).
    CHECK(r.units[0].cell_x == 10);
    CHECK(r.units[0].cell_y == 10);
    const bool overlap = r.units[0].cell_x == r.units[1].cell_x &&
                         r.units[0].cell_y == r.units[1].cell_y;
    CHECK_FALSE(overlap);                              // (c) no overlap
    CHECK(r.occ.at(10, 10) == 0);                      // lander's ONE claim
    CHECK(r.occ.at(10, 11) == 1);                      // blocker's claim untouched
    CHECK(r.occ.at(2, 2) == Occupancy::kEmpty);        // origin released
    CHECK(r.claims_of(0) == 1);
    CHECK(r.claims_of(1) == 1);
}

TEST_CASE("harvest: warp releases a mid-move next-cell reservation too") {
    Rig r;
    r.harv(2, 2, /*chrono=*/true, /*storage=*/20);
    // Simulate a mid-move CMIN: it also holds a reservation on the next path
    // cell (the step_units reserve-before-enter protocol).
    r.occ.set(3, 2, 0);
    REQUIRE(r.claims_of(0) == 2);
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].bales_ore = 20;

    r.step(kChronoWarpSecs);

    CHECK(r.units[0].cell_x == 10);
    CHECK(r.units[0].cell_y == 11);
    CHECK(r.occ.at(2, 2) == Occupancy::kEmpty);
    CHECK(r.occ.at(3, 2) == Occupancy::kEmpty);        // stale reservation swept
    CHECK(r.claims_of(0) == 1);                        // exactly the landing cell
}

// ---- Review Important-1: ReturnToRef emits a HarvestMove ONCE per leg ----

TEST_CASE("harvest: a driving HARV gets exactly one HarvestMove per return leg") {
    Rig r;
    r.harv(1, 1, /*chrono=*/false, /*storage=*/40);   // unit 0: HARV, far from the dock
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].bales_ore = 40;                         // full -> heads home

    int total_moves = 0;

    // First tick: not moving yet -> exactly one HarvestMove requested (the
    // dock cell for the Rig's NAREFN is (10, 11), see the file-header note).
    r.step(0.1);
    REQUIRE(r.moves.size() == 1);
    CHECK(r.moves[0].unit == 0);
    CHECK(r.moves[0].gx == 10);
    CHECK(r.moves[0].gy == 11);
    total_moves += (int)r.moves.size();

    // World::order_unit_move would now have set the unit's path toward the
    // dock cell; step_harvest itself never touches Unit::path, so this test
    // (which drives ONLY step_harvest, not step_units/order_unit_move -- see
    // the Rig fixture) mocks that hand-off the same way World::step does:
    // the unit is "moving" with its path's last cell == the stamped target.
    r.units[0].path = {{1, 1}, {10, 11}};
    r.units[0].path_idx = 1;

    // Many more ticks while still driving toward the SAME cell: NO further
    // HarvestMove, ever -- step_units (not step_harvest) now owns the drive,
    // including its own re-path/give-up handling on collision.
    for (int i = 0; i < 50; ++i) {
        r.step(0.1);
        total_moves += (int)r.moves.size();
        CHECK(r.moves.empty());
        CHECK(r.units[0].harv == HarvState::ReturnToRef);  // still driving
    }
    CHECK(total_moves == 1);  // exactly one HarvestMove for the whole leg

    // Arrival: step_units would have advanced cell_x/y onto the dock cell
    // and cleared the path.
    r.units[0].cell_x = 10;
    r.units[0].cell_y = 11;
    r.units[0].path.clear();
    r.units[0].path_idx = 0;

    r.step(0.1);
    CHECK(r.moves.empty());                      // arrival needs no new move
    CHECK(r.units[0].harv == HarvState::WaitDock);
}

TEST_CASE("harvest: a HARV that stops without arriving re-requests exactly one HarvestMove") {
    // Mirrors step_units' blocked give-up: the path was abandoned mid-route
    // (collision give-up), so the unit is stationary again without having
    // reached the dock. ReturnToRef must notice and re-issue exactly one
    // fresh HarvestMove -- not zero (stuck forever) and not one per tick.
    Rig r;
    r.harv(1, 1, /*chrono=*/false, /*storage=*/40);
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].bales_ore = 40;

    r.step(0.1);
    REQUIRE(r.moves.size() == 1);

    r.units[0].path = {{1, 1}, {10, 11}};
    r.units[0].path_idx = 1;
    r.step(0.1);
    CHECK(r.moves.empty());  // driving -> quiet

    // step_units gives up: path cleared, unit stationary short of the dock.
    r.units[0].path.clear();
    r.units[0].path_idx = 0;
    r.units[0].cell_x = 5;
    r.units[0].cell_y = 5;  // stopped partway, not at the dock

    r.step(0.1);
    REQUIRE(r.moves.size() == 1);  // re-requested exactly once
    CHECK(r.moves[0].gx == 10);
    CHECK(r.moves[0].gy == 11);
    CHECK(r.units[0].harv == HarvState::ReturnToRef);
}

// ---- Review Important-3: empty CMIN + manual return skips the warp-back ----

TEST_CASE("harvest: CMIN manual return with an empty, never-stamped hold skips the warp-back") {
    Rig r;
    r.harv(5, 6, /*chrono=*/true, /*storage=*/20);  // fresh spawn -- never found ore
    REQUIRE_FALSE(r.units[0].has_harv_cell);
    REQUIRE(r.units[0].bales_ore == 0);
    REQUIRE(r.units[0].bales_gems == 0);
    // Mirrors World::order_return(): jump straight to ReturnToRef, bypassing
    // SeekOre entirely (a manual return order never stamps harv_cell).
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].harv_timer = 0.0;

    // Outbound chrono warp to the dock (10, 11) -- unaffected by the fix;
    // an empty-handed manual return still legitimately drives to the dock.
    r.step(kChronoWarpSecs);
    CHECK(r.units[0].cell_x == 10);
    CHECK(r.units[0].cell_y == 11);
    REQUIRE(r.units[0].harv == HarvState::WaitDock);

    r.step(0.01);  // dock free -> Unloading
    REQUIRE(r.units[0].harv == HarvState::Unloading);
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadStart, 0) == 1);

    r.step(0.01);  // no bales to drain -> UnloadEnd fires immediately.
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadEnd, 0) == 1);
    // The bug (pre-fix): this would ALSO fire ChronoOut and start a timed
    // warp toward harv_cell_x/y's (0,0) default. Fixed: no warp at all.
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 0) == 0);
    CHECK(r.units[0].harv == HarvState::SeekOre);  // straight back to seeking, in place
    CHECK(r.units[0].cell_x == 10);                // still at the dock -- no warp-back
    CHECK(r.units[0].cell_y == 11);

    // Confirm no delayed ChronoIn/teleport ever follows.
    r.step(kChronoWarpSecs * 2);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoIn, 0) == 0);
    CHECK(r.units[0].cell_x == 10);
    CHECK(r.units[0].cell_y == 11);
}

TEST_CASE("harvest: CMIN manual return with a real stamped harv_cell drives back (W7-11-13)") {
    // Contrast case: has_harv_cell == true (found ore earlier) -> the normal
    // timed warp-back still fires. Guards against the fix being over-broad.
    Rig r;
    r.ore.set(2, 2, OreKind::Ore, kMaxOreDensity, 0);
    r.harv(2, 2, /*chrono=*/true, /*storage=*/20);
    r.step(0.01);  // SeekOre finds (2,2) -> stamps has_harv_cell, MoveToOre
    REQUIRE(r.units[0].has_harv_cell);
    r.step(0.01);  // arrives, not moving -> Harvesting
    REQUIRE(r.units[0].harv == HarvState::Harvesting);

    // Manual return mid-harvest, before any bales are collected.
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].harv_timer = 0.0;

    r.step(kChronoWarpSecs);
    REQUIRE(r.units[0].harv == HarvState::WaitDock);
    r.step(0.01);
    REQUIRE(r.units[0].harv == HarvState::Unloading);

    r.step(0.01);  // no bales -> UnloadEnd; W7-11-13: no warp back, it drives
    CHECK(count_events(r.events, HarvestEvent::Kind::UnloadEnd, 0) == 1);
    CHECK(count_events(r.events, HarvestEvent::Kind::ChronoOut, 0) == 0);
    CHECK(r.units[0].harv == HarvState::SeekOre);
    CHECK(r.units[0].dock_refinery == -1);
}

// ---- Review Important-2: FreeUnit grand-opening edge detector coverage ----
// (main.cpp's spawn_free_units_on_open, extracted into the pure
// free_unit_grand_openings so the Buildup->Idle edge detection that drives a
// refinery's grand-opening miner spawn -- previously exercised only by hand
// through the interactive loop -- gets direct automated coverage.)

TEST_CASE("harvest: free_unit_grand_openings fires once per Buildup->Idle edge, "
          "FreeUnit types only") {
    std::vector<Building> bs;
    Building refA; refA.type = 0; refA.state = Building::State::Buildup;
    bs.push_back(refA);                                  // type 0: ships a FreeUnit
    Building other; other.type = 1; other.state = Building::State::Buildup;
    bs.push_back(other);                                 // type 1: does NOT
    std::vector<bool> free_unit_of_type = {true, false};
    std::vector<int> prev_states;

    // Frame 1: still Buildup -> no signal yet, but both get seeded.
    auto opened = free_unit_grand_openings(bs, free_unit_of_type, prev_states);
    CHECK(opened.empty());

    // Both buildings complete buildup on the SAME tick.
    bs[0].state = Building::State::Idle;
    bs[1].state = Building::State::Idle;
    opened = free_unit_grand_openings(bs, free_unit_of_type, prev_states);
    REQUIRE(opened.size() == 1);          // none for the non-FreeUnit type
    CHECK(opened[0] == 0);                // exactly one signal, for the FreeUnit type

    // Subsequent ticks: no more signals, even though both stay Idle.
    for (int i = 0; i < 5; ++i) {
        opened = free_unit_grand_openings(bs, free_unit_of_type, prev_states);
        CHECK(opened.empty());
    }

    // A second refinery (FreeUnit type) is added later and completes buildup
    // on its own tick -- it must fire exactly once too.
    Building refB; refB.type = 0; refB.state = Building::State::Buildup;
    bs.push_back(refB);
    opened = free_unit_grand_openings(bs, free_unit_of_type, prev_states);
    CHECK(opened.empty());                // still Buildup -- only seeded

    bs[2].state = Building::State::Idle;
    opened = free_unit_grand_openings(bs, free_unit_of_type, prev_states);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0] == 2);

    // And it doesn't fire again afterward either.
    opened = free_unit_grand_openings(bs, free_unit_of_type, prev_states);
    CHECK(opened.empty());
}

TEST_CASE("harvest: free_unit_grand_openings ignores an out-of-range building type") {
    std::vector<Building> bs;
    Building b; b.type = 5; b.state = Building::State::Buildup;  // no such roster slot
    bs.push_back(b);
    std::vector<bool> free_unit_of_type = {true};  // roster only has 1 entry
    std::vector<int> prev_states;

    free_unit_grand_openings(bs, free_unit_of_type, prev_states);
    bs[0].state = Building::State::Idle;
    auto opened = free_unit_grand_openings(bs, free_unit_of_type, prev_states);
    CHECK(opened.empty());
}

TEST_CASE("harvest: Ore Purifier bonus + house 2+ paid to its own pool (gap G-27) [RE: economy.md §2]") {
    Rig r;
    r.harv(2, 2);
    r.units[0].house = 2;                 // a third skirmish house
    r.units[0].harv = HarvState::Unloading;
    r.units[0].dock_refinery = 0;
    r.units[0].bales_ore = 1;
    r.units[0].harv_timer = 0.0;
    double credits[3] = {0, 0, 0};
    const int purifiers[3] = {0, 0, 2};   // house 2 owns two purifiers
    r.moves.clear(); r.events.clear();
    step_harvest(r.units, r.buildings, r.types, r.ref_types, r.ore, r.pass, 20, 20, r.occ,
                 kUnloadSecsPerBale, r.values, credits, r.moves, r.events, 3, purifiers, 0.25);
    CHECK(credits[0] == 0);               // no longer misrouted to house 0
    CHECK(credits[2] == doctest::Approx(25 + 2 * 0.25 * 25));   // 25 + 12.5
}

// W6-03-03 [RE: FUN_00522a80 -- n = own purifiers + Rules AIVirtualPurifiers
// [house+0x184] for a computer house outside a campaign; rulesmd 4,2,0 h,m,e]:
// at PurifierBonus .25 a Brutal AI (4 virtual) earns 2x, a Medium AI (2) 1.5x.
TEST_CASE("harvest: AIVirtualPurifiers -- Brutal 2x, Medium 1.5x, Easy 1x the human unload (W6-03-03)") {
    const int virt[3] = {4, 2, 0};   // hard, medium, easy
    const double want[3] = {2.0, 1.5, 1.0};
    for (int k = 0; k < 3; ++k) {
        Rig r;
        r.harv(2, 2);
        r.units[0].house = 1;
        r.units[0].harv = HarvState::Unloading;
        r.units[0].dock_refinery = 0;
        r.units[0].bales_ore = 1;
        r.units[0].harv_timer = 0.0;
        double credits[2] = {0, 0};
        const int purifiers[2] = {0, virt[k]};
        r.moves.clear(); r.events.clear();
        step_harvest(r.units, r.buildings, r.types, r.ref_types, r.ore, r.pass, 20, 20, r.occ,
                     kUnloadSecsPerBale, r.values, credits, r.moves, r.events, 2, purifiers, 0.25);
        CHECK(credits[1] == doctest::Approx(25.0 * want[k]));
    }
}

TEST_CASE("harvest: a War Miner whose dock cell is blocked drives to the QueueingCell and unloads (NEW-3)") {
    Rig r;
    // Rig refinery NAREFN at (8,8) 4x3 -> dock cell (10,11); block it.
    r.pass[(size_t)11 * 20 + 10] = 0;
    r.harv(2, 2);
    r.units[0].harv = HarvState::ReturnToRef;
    r.units[0].bales_ore = 40;
    r.step(0.1);
    REQUIRE(r.moves.size() == 1);
    CHECK(r.moves[0].gx == 12);   // QueueingCell = origin + (fw, 1)
    CHECK(r.moves[0].gy == 9);
    // Arrive there -> docks.
    r.units[0].cell_x = 12; r.units[0].cell_y = 9; r.units[0].fx = 12; r.units[0].fy = 9;
    r.units[0].path.clear();
    r.step(0.1);
    CHECK(r.units[0].harv == HarvState::WaitDock);
}

// W2-SK-04: the ore search is zone (reachability) and occupant filtered.
TEST_CASE("harvest: reachable-ore search skips walled-off and occupied cells") {
    OreField ore(20, 20);
    std::vector<std::uint8_t> pass(400, 1);
    Occupancy occ(20, 20);
    for (int y = 0; y < 20; ++y) pass[y * 20 + 10] = 0;   // a wall at x=10
    ore.set(11, 5, OreKind::Ore, 5, 0);                     // nearest, but across the wall
    ore.set(2, 5, OreKind::Ore, 5, 0);                      // reachable, farther
    int ox = -1, oy = -1;
    REQUIRE(find_reachable_ore(ore, pass, 20, 20, occ, 0, 8, 5, 48, -1, -1, ox, oy));
    CHECK(ox == 2);
    CHECK(oy == 5);
    occ.set(2, 5, 7);   // another unit sits on it
    CHECK_FALSE(find_reachable_ore(ore, pass, 20, 20, occ, 0, 8, 5, 48, -1, -1, ox, oy));
    occ.clear(2, 5);
    CHECK_FALSE(find_reachable_ore(ore, pass, 20, 20, occ, 0, 8, 5, 48, 2, 5, ox, oy));   // skip cell
}

// W2-SK-03: an empty miner with no ore never idles on the dock area.
TEST_CASE("harvest: empty miner with no ore leaves the refinery dock") {
    Rig r;
    // Rig refinery at (8,8) 4x3 -> dock cell (10, 11).
    Unit& u = r.harv(10, 11);
    (void)u;
    r.step(0.1);
    REQUIRE(r.moves.size() == 1);
    const HarvestMove m = r.moves[0];
    CHECK(std::max(std::abs(m.gx - 10), std::abs(m.gy - 11)) >= 2);
}

// RA1 Mission_Harvest fallback: a part-loaded miner with no ore left banks it.
TEST_CASE("harvest: part-loaded miner with no ore in reach returns to the refinery") {
    Rig r;
    Unit& u = r.harv(2, 2);
    u.bales_ore = 5;
    r.step(0.1);
    CHECK(r.units[0].harv == HarvState::ReturnToRef);
}


TEST_CASE("diagnostics: bounded rotation, named asset trace, snapshots and miner warp") {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/"ra2-diagnostics-test12";
    fs::create_directories(root);
    const auto read=[&](const char* name){std::ifstream in(root/name,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    struct Cleanup { ~Cleanup(){ra2assets::asset_trace_callback().store(nullptr);diag::stop();} } cleanup;
    REQUIRE(diag::start(root.u8string(),128));
    diag::write(std::string(100,'a').data(),100);
    diag::write(std::string(100,'b').data(),100);
    CHECK(read("engine-previous.log")==std::string(100,'a'));
    CHECK(read("engine-current.log")==std::string(100,'b')); // flushed before close
    diag::snapshot("skirmish.txt",std::string(70000,'s'));
    CHECK(fs::file_size(root/"skirmish.txt")==65536);
    diag::snapshot("../not-allowed.txt","no");CHECK_FALSE(fs::exists(root.parent_path()/"not-allowed.txt"));
    REQUIRE(diag::start(root.u8string()));
    ra2assets::asset_trace_callback().store(diag::asset);
    std::vector<uint8_t> buf;
    auto put32=[&](uint32_t v){for(int i=0;i<4;++i)buf.push_back(static_cast<uint8_t>(v>>(8*i)));};
    put32(0);buf.push_back(1);buf.push_back(0);put32(1);
    put32(ra2assets::mix_id("probe.ini"));put32(0);put32(1);buf.push_back('x');
    auto mix=ra2assets::MixFile::open(std::move(buf));
    REQUIRE(mix.find("probe.ini")!=nullptr);REQUIRE(mix.find("probe.ini")!=nullptr);
    auto assets=read("assets-current.log");
    CHECK(assets.find("file-hit probe.ini")!=std::string::npos);
    CHECK(assets.find("file-hit probe.ini",assets.find("file-hit probe.ini")+1)==std::string::npos);
    for(int i=0;i<9000;++i)diag::asset("test",std::to_string(i),"synthetic",1);
    CHECK(diag::state().seen.size()==8192);
    CHECK(fs::file_size(root/"assets-current.log")<=1024*1024);
    Rig logged,plain;
    for(auto* r:{&logged,&plain}){r->harv(1,1,true,20);r->units[0].harv=HarvState::ReturnToRef;r->units[0].bales_ore=20;}
    logged.step(kChronoWarpSecs);
    const auto log=read("engine-current.log");
    CHECK(log.find("WARP begin")!=std::string::npos);CHECK(log.find("WARP complete")!=std::string::npos);
    diag::stop();plain.step(kChronoWarpSecs);
    CHECK(logged.units[0].cell_x==plain.units[0].cell_x);CHECK(logged.units[0].cell_y==plain.units[0].cell_y);
    CHECK(logged.units[0].harv==plain.units[0].harv);CHECK(logged.units[0].dock_refinery==plain.units[0].dock_refinery);
    CHECK(logged.events.size()==plain.events.size());CHECK(logged.claims_of(0)==plain.claims_of(0));
}

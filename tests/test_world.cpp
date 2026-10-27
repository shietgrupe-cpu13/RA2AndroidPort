#include "doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <tuple>
#include <vector>

#include "ai_team.hpp"        // AI-2 offensive-teams: AiTeamState/TeamInstance/opcodes
#include "aimd_parse.hpp"     // AI-2: AimdData/TaskForce/ScriptType/TeamType
#include "building.hpp"
#include "cargo.hpp"          // CB-1: eject_cell -- the eject-cell witness
#include "factory.hpp"        // Production phase: exit_cell/primary_factory/set_primary
#include "harvest.hpp"        // Economy phase: HarvState/HarvestEvent/OreValues/kUnloadSecsPerBale
#include "iso_project.hpp"   // cell_to_screen / screen_to_cell
#include "iso_renderer.hpp"  // pal_for_house
#include "capture.hpp"        // Capture phase: kNeutralHouse
#include "occupancy.hpp"
#include "pathfind.hpp"
#include "production.hpp"     // Production phase: ProductionQueues cancel/reconcile_refund
#include "ra2assets/map.hpp"
#include "ra2assets/mix.hpp"
#include "ra2assets/pal.hpp"  // Palette/Rgba (pal_for_house tests)
#include "unit_type.hpp"      // Production phase: UnitTypeDef (refund test)
#include "selection.hpp"
#include "world.hpp"
#include "asset_fixture.hpp"  // shared load-once real map (mapsmd03.mix)

using ra2assets::MapCell;
using ra2engine::build_passability;

namespace {

MapCell cell(int x, int y) {
    MapCell c;
    c.x = x;
    c.y = y;
    c.tile = 0;
    return c;
}

int gidx(int w, int x, int y) { return y * w + x; }

// Scan outward from (x0, y0) for the first offset (skipping skip_dx/skip_dy)
// that find_path -- the same algorithm order_move_selected uses -- reaches
// with a real (>1 cell) route. Pure probing: no World state is touched.
constexpr int kProbeDirs[8][2] = {{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                  {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
bool find_reachable(const ra2engine::World& w, int x0, int y0, int skip_dx, int skip_dy,
                    int& ox, int& oy) {
    for (int r = 4; r <= 40; r += 2)
        for (const auto& d : kProbeDirs) {
            const int dx = d[0] * r, dy = d[1] * r;
            if (dx == skip_dx && dy == skip_dy) continue;
            auto p = ra2engine::find_path(w.passable(), w.grid_w(), w.grid_h(),
                                          {x0, y0}, {x0 + dx, y0 + dy});
            if (p.size() > 1) { ox = x0 + dx; oy = y0 + dy; return true; }
        }
    return false;
}

// Count of walkable Chebyshev-ring-1 (immediate 8-neighbor) cells around
// (x, y). Task 3: an attack_order target is no longer approached directly --
// World::step now resolves each attacker a DISTINCT free firing cell near the
// target via assign_formation, so (unlike the old direct-pursuit code) the
// terrain immediately around a test's chosen target now matters: a spot
// boxed in by cliffs/water/other terrain leaves assign_formation nothing
// close to hand out, and the pursuer can only reach whatever free cell the
// ring search finds -- possibly far outside weapon range. Tests placing a
// pursuit target below use this to reject overly-enclosed candidates.
int open_neighbor_count(const ra2engine::World& w, int x, int y) {
    static const int nb[8][2] = {{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                 {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int n = 0;
    for (const auto& o : nb)
        if (w.walkable(x + o[0], y + o[1])) ++n;
    return n;
}

// Like find_reachable's kProbeDirs ring scan, but ALSO requires at least
// `min_open` free (open_neighbor_count) immediate neighbors around the
// candidate before accepting it -- see open_neighbor_count above. Returns the
// found cell's (ox, oy) and its Euclidean distance from (x0, y0) in odist.
bool find_reachable_open(const ra2engine::World& w, int x0, int y0, int start_r, int min_open,
                         int& ox, int& oy, double& odist) {
    for (int r = start_r; r <= 60; r += 2)
        for (const auto& d : kProbeDirs) {
            const int dx = d[0] * r, dy = d[1] * r;
            const int tx = x0 + dx, ty = y0 + dy;
            if (open_neighbor_count(w, tx, ty) < min_open) continue;
            auto p = ra2engine::find_path(w.passable(), w.grid_w(), w.grid_h(), {x0, y0}, {tx, ty});
            if (p.size() > 1) {
                ox = tx;
                oy = ty;
                odist = std::sqrt(double(dx * dx + dy * dy));
                return true;
            }
        }
    return false;
}

}  // namespace

TEST_CASE("build_passability: empty cells -> empty grid, zero dims") {
    std::vector<MapCell> cells;
    int gw = -1, gh = -1;
    auto grid = build_passability(cells, gw, gh);
    CHECK(gw == 0);
    CHECK(gh == 0);
    CHECK(grid.empty());
}

TEST_CASE("build_passability: grid sized to max+1, marks present cells passable") {
    // Cells at (1,2), (5,7), (3,3). Max x = 5, max y = 7 -> grid 6x8.
    std::vector<MapCell> cells = {cell(1, 2), cell(5, 7), cell(3, 3)};
    int gw = 0, gh = 0;
    auto grid = build_passability(cells, gw, gh);

    REQUIRE(gw == 6);
    REQUIRE(gh == 8);
    REQUIRE(grid.size() == static_cast<size_t>(gw) * gh);

    // Present cells are passable...
    CHECK(grid[gidx(gw, 1, 2)] == 1);
    CHECK(grid[gidx(gw, 5, 7)] == 1);
    CHECK(grid[gidx(gw, 3, 3)] == 1);
    // ...every other coordinate is impassable (off-map / no record).
    CHECK(grid[gidx(gw, 0, 0)] == 0);
    CHECK(grid[gidx(gw, 2, 2)] == 0);
    CHECK(grid[gidx(gw, 4, 7)] == 0);

    // Exactly the three materialized cells are passable.
    int passable_count = 0;
    for (auto v : grid) passable_count += (v != 0);
    CHECK(passable_count == 3);
}

// The subtle coordinate reconciliation this task depends on: MapCell.x/y, the
// passability grid index, and iso_project's cell_to_screen/screen_to_cell all
// live in the SAME raw iso space. So projecting a materialized cell to screen
// and inverting must return that same cell, and that cell must be passable in
// the grid built from the same cells -- i.e. a click that lands on a mapped
// tile resolves to an in-grid, walkable cell the unit can be pathed to.
TEST_CASE("coordinate reconciliation: cell -> screen -> cell round-trips into the passable grid") {
    std::vector<MapCell> cells = {cell(10, 12), cell(40, 55), cell(110, 112), cell(2, 200)};
    int gw = 0, gh = 0;
    auto grid = build_passability(cells, gw, gh);

    for (const auto& c : cells) {
        const ra2engine::ScreenPt p = ra2engine::cell_to_screen(c.x, c.y, 0, 60, 30, 15);
        int rx = -1, ry = -1;
        ra2engine::screen_to_cell(p.x, p.y, 60, 30, rx, ry);
        CHECK(rx == c.x);
        CHECK(ry == c.y);
        REQUIRE(rx >= 0);
        REQUIRE(ry >= 0);
        REQUIRE(rx < gw);
        REQUIRE(ry < gh);
        CHECK(grid[gidx(gw, rx, ry)] == 1);  // the inverted cell is walkable
    }
}

// ---- Multi-unit World integration (guarded real map) ----
namespace {
bool load_real_map_w(ra2assets::MapFile& out) {
    // Borrow the shared, load-once mapsmd03.mix map (asset_fixture) instead of
    // re-slurping + re-parsing the same archive in every integration case.
    // Same ok/false contract as before (archive present AND map entry found);
    // the parsed map is copied into `out` so callers keep their own instance.
    if (!ra2fixture::mapsmd03_available()) return false;
    out = ra2fixture::mapsmd03_map();
    return true;
}

// GI (house 0, M60) / Conscript (house 1, M1Carbine) combat stats -- ground
// truth from docs/re/combat.md / the Combat Phase's extracted rulesmd values
// (both Strength=125, SA warhead), same numbers test_combat.cpp's
// make_soldier helper uses.
ra2engine::UnitCombat gi_stats() {
    ra2engine::UnitCombat c;
    c.strength = 125;
    c.armor = "none";
    c.image = "GI";
    c.damage = 15;
    c.rof = 20;
    c.range = 4.0;
    c.verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};  // kSAVerses
    return c;
}
ra2engine::UnitCombat cons_stats() {
    ra2engine::UnitCombat c;
    c.strength = 125;
    c.armor = "flak";
    c.image = "CONS";
    c.damage = 15;
    c.rof = 25;
    c.range = 4.0;
    c.verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};  // kSAVerses
    return c;
}

// Spawn `n` combat units of `house` at walkable+unoccupied cells scanning a
// small square from (base_x, base_y) -- same walkable()+unit_at_cell()<0
// scan the Input-test spawn helper (tests/test_input.cpp's `spawn`) and this
// file's own "group move" test use, just via add_combat_unit instead of
// add_unit. Returns the placed units' indices (fewer than n if the area is
// too crowded/blocked).
std::vector<int> spawn_combat_squad(ra2engine::World& world, int base_x, int base_y, int house,
                                    const ra2engine::UnitCombat& stats, int n) {
    std::vector<int> idx;
    for (int dy = 0; dy < 8 && static_cast<int>(idx.size()) < n; ++dy)
        for (int dx = 0; dx < 8 && static_cast<int>(idx.size()) < n; ++dx) {
            const int x = base_x + dx, y = base_y + dy;
            if (world.walkable(x, y) && world.unit_at_cell(x, y) < 0) {
                world.add_combat_unit(x, y, house, stats);
                idx.push_back(static_cast<int>(world.units().size()) - 1);
            }
        }
    return idx;
}

// Spawn two 3-unit squads (GI house 0 / Conscript house 1) INTERLEAVED at
// walkable+unoccupied cells scanning outward from (base_x, base_y): the
// (placed % 2) alternation means every two spatially-adjacent placed units
// are opposite factions, so every unit's nearest enemy is close -- unlike a
// two-separate-blocks layout (each squad scanned from its own offset base),
// which leaves the far-end unit of EACH block as nobody's "nearest enemy"
// (it's the single farthest point from the other block): in a 120s
// full-battle run against the real map this produced two mutually-idle,
// undamaged, just-out-of-weapon-range survivors that never engaged --
// confirmed by instrumenting a run before landing on this interleaved
// layout. Interleaving keeps every unit within a cell or two of an opposing
// unit throughout the fight, so a unit whose original target dies is very
// likely to have another hostile already within auto-acquire range.
struct InterleavedSquads {
    std::vector<int> gi, cons;
};
InterleavedSquads spawn_interleaved_squads(ra2engine::World& world, int base_x, int base_y,
                                           const ra2engine::UnitCombat& gi_c,
                                           const ra2engine::UnitCombat& cons_c, int n_each) {
    InterleavedSquads sq;
    int placed = 0;
    for (int dy = 0; dy < 8 && placed < 2 * n_each; ++dy)
        for (int dx = 0; dx < 8 && placed < 2 * n_each; ++dx) {
            const int x = base_x + dx, y = base_y + dy;
            if (!world.walkable(x, y) || world.unit_at_cell(x, y) >= 0) continue;
            const bool is_gi = (placed % 2) == 0;
            world.add_combat_unit(x, y, is_gi ? 0 : 1, is_gi ? gi_c : cons_c);
            const int idx = static_cast<int>(world.units().size()) - 1;
            (is_gi ? sq.gi : sq.cons).push_back(idx);
            ++placed;
        }
    return sq;
}
}  // namespace

TEST_CASE("World: add_unit places units, occupancy and unit_at_cell agree") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    ra2engine::Unit& u0 = world.add_unit(c->x, c->y, "GI", 0);
    CHECK(world.units().size() == 1);
    CHECK(world.unit_at_cell(c->x, c->y) == 0);
    CHECK(world.occupancy().at(c->x, c->y) == 0);
    (void)u0;
}

TEST_CASE("World: group move sends selected units to distinct cells, never overlapping") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // Spawn a small cluster around the start cell (skip any non-walkable slot).
    int placed = 0;
    for (int dy = 0; dy < 3 && placed < 4; ++dy)
        for (int dx = 0; dx < 3 && placed < 4; ++dx)
            if (world.walkable(c->x + dx, c->y + dy) &&
                world.unit_at_cell(c->x + dx, c->y + dy) < 0) {
                world.add_unit(c->x + dx, c->y + dy, "GI", 0);
                ++placed;
            }
    REQUIRE(placed >= 2);
    for (std::size_t i = 0; i < world.units().size(); ++i) tsel_.set(static_cast<int>(i));

    // Order them to a reachable cell ~12 south-east and run the sim.
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), c->x + 12, c->y + 12);
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 20.0; t += dt) {
        world.step(dt);
        // no-overlap invariant every frame
        const auto& us = world.units();
        for (size_t i = 0; i < us.size(); ++i)
            for (size_t j = i + 1; j < us.size(); ++j)
                CHECK_FALSE((us[i].cell_x == us[j].cell_x && us[i].cell_y == us[j].cell_y));
    }
    // Distinct final cells.
    const auto& us = world.units();
    for (size_t i = 0; i < us.size(); ++i)
        for (size_t j = i + 1; j < us.size(); ++j)
            CHECK_FALSE((us[i].cell_x == us[j].cell_x && us[i].cell_y == us[j].cell_y));
}

TEST_CASE("unit_draw_order: sorts back-to-front by fx+fy") {
    std::vector<ra2engine::Unit> us(3);
    us[0].fx = 5; us[0].fy = 5;   // depth 10 (middle)
    us[1].fx = 1; us[1].fy = 1;   // depth 2  (farthest -> first)
    us[2].fx = 9; us[2].fy = 9;   // depth 18 (nearest -> last)
    auto order = ra2engine::unit_draw_order(us);
    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 0);
    CHECK(order[2] == 2);
}

TEST_CASE("World: order_move_selected mid-walk retarget keeps fx/fy (no backward snap)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.set_move_speed(3.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_unit(c->x, c->y, "GI", 0);
    tsel_.set(0);

    int t1x = 0, t1y = 0;
    REQUIRE(find_reachable(world, c->x, c->y, 999999, 999999, t1x, t1y));
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), t1x, t1y);
    REQUIRE(world.units()[0].moving());

    // Step partway: 0.05s at 3 cells/sec = 0.15 cells -- genuinely mid-segment.
    world.step(0.05);
    REQUIRE(world.units()[0].moving());
    const double fx0 = world.units()[0].fx, fy0 = world.units()[0].fy;
    REQUIRE((fx0 != (double)world.units()[0].cell_x || fy0 != (double)world.units()[0].cell_y));

    int t2x = 0, t2y = 0;
    REQUIRE(find_reachable(world, world.units()[0].cell_x, world.units()[0].cell_y,
                           t1x - world.units()[0].cell_x, t1y - world.units()[0].cell_y,
                           t2x, t2y));
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), t2x, t2y);

    // The retarget must NOT snap fx/fy backward to the committed cell.
    CHECK(world.units()[0].fx == fx0);
    CHECK(world.units()[0].fy == fy0);
    CHECK(world.units()[0].moving());
    REQUIRE_FALSE(world.units()[0].path.empty());
    CHECK(world.units()[0].path.back().x == t2x);   // formation slot for a lone
    CHECK(world.units()[0].path.back().y == t2y);   // unit is the goal itself
}

TEST_CASE("World: unreachable order mid-walk cancels and snaps fx/fy cleanly") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.set_move_speed(3.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_unit(c->x, c->y, "GI", 0);
    tsel_.set(0);

    int t1x = 0, t1y = 0;
    REQUIRE(find_reachable(world, c->x, c->y, 999999, 999999, t1x, t1y));
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), t1x, t1y);
    world.step(0.05);
    REQUIRE(world.units()[0].moving());
    REQUIRE((world.units()[0].fx != (double)world.units()[0].cell_x ||
             world.units()[0].fy != (double)world.units()[0].cell_y));

    // A goal FAR off-grid: the formation ring search finds no free walkable
    // cell, the unit keeps its own cell as target, pathing to self cancels ->
    // fx/fy must snap onto the committed cell, not strand mid-segment.
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), world.grid_w() + 1000, world.grid_h() + 1000);
    CHECK_FALSE(world.units()[0].moving());
    CHECK(world.units()[0].path.empty());
    CHECK(world.units()[0].fx == (double)world.units()[0].cell_x);
    CHECK(world.units()[0].fy == (double)world.units()[0].cell_y);
}

// ---- Combat integration (Task 5: add_combat_unit / order_attack / step chaining / tombstones) ----

TEST_CASE("World: two squads fight to elimination deterministically (real map)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }

    const ra2engine::UnitCombat gi = gi_stats();
    const ra2engine::UnitCombat cons = cons_stats();
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // Runs the full scenario on a fresh World (same underlying map): spawn 3
    // GIs (house 0) and 3 Conscripts (house 1) interleaved (see
    // spawn_interleaved_squads -- close-range combat, ~1-3 cells apart), each
    // GI explicitly ordered (via order_attack, selecting one unit at a time)
    // to attack its own nearest hostile, each Conscript given an explicit
    // attack on its own nearest GI directly (hostiles have no selection path
    // -- order_attack is a house-0-only command), then simulates at 60Hz to a
    // 120s cap, checking per-frame invariants throughout. Returns the final
    // (hp, cell_x, cell_y, alive, dying) tuple per unit for the bit-
    // determinism comparison across two independent runs below.
    auto run_battle = [&](ra2engine::World& world) {
        world.set_move_speed(3.0);

        InterleavedSquads sq = spawn_interleaved_squads(world, c->x, c->y, gi, cons, 3);
        std::vector<int>& gi_idx = sq.gi;
        std::vector<int>& cons_idx = sq.cons;
        REQUIRE(gi_idx.size() == 3);
        REQUIRE(cons_idx.size() == 3);

        // Each GI attacks its OWN nearest hostile: select just that unit so
        // order_attack's "every selected unit" only ever touches it.
        for (int gi_i : gi_idx) {
            tsel_.clear();
            tsel_.set(gi_i);
            int nearest = -1;
            double best_d2 = 0.0;
            for (int cons_i : cons_idx) {
                const double dx = world.units()[gi_i].cell_x - world.units()[cons_i].cell_x;
                const double dy = world.units()[gi_i].cell_y - world.units()[cons_i].cell_y;
                const double d2 = dx * dx + dy * dy;
                if (nearest == -1 || d2 < best_d2) {
                    nearest = cons_i;
                    best_d2 = d2;
                }
            }
            REQUIRE(nearest != -1);
            world.group_attack(0, tsel_.ids(), nearest);
        }
        tsel_.clear();

        // Conscripts (hostile) have no selection path -- set target/
        // attack_order directly on each, aimed at its own nearest GI.
        for (int cons_i : cons_idx) {
            int nearest = -1;
            double best_d2 = 0.0;
            for (int gi_i : gi_idx) {
                const double dx = world.units()[cons_i].cell_x - world.units()[gi_i].cell_x;
                const double dy = world.units()[cons_i].cell_y - world.units()[gi_i].cell_y;
                const double d2 = dx * dx + dy * dy;
                if (nearest == -1 || d2 < best_d2) {
                    nearest = gi_i;
                    best_d2 = d2;
                }
            }
            REQUIRE(nearest != -1);
            world.units()[cons_i].target = nearest;
            world.units()[cons_i].attack_order = true;
        }

        const double dt = 1.0 / 60.0;
        double t = 0.0;
        bool ended = false;
        for (; t < 120.0; t += dt) {
            world.step(dt);
            const auto& us = world.units();

            // No two ALIVE-and-not-dying units ever share a cell (a dying
            // unit's occupancy was already released at the moment it started
            // dying, so it MAY coincide with a living unit's cell -- excluded
            // from this invariant by design).
            for (std::size_t i = 0; i < us.size(); ++i) {
                if (!us[i].alive || us[i].die_time >= 0.0) continue;
                for (std::size_t j = i + 1; j < us.size(); ++j) {
                    if (!us[j].alive || us[j].die_time >= 0.0) continue;
                    CHECK_FALSE((us[i].cell_x == us[j].cell_x && us[i].cell_y == us[j].cell_y));
                }
            }
            // No target ever dangles: -1, or a valid index into a unit that
            // is alive. (A target may point at a unit that is DYING but still
            // alive==true for one frame -- validation clears it on the NEXT
            // step_combat call -- so "alive", not "not dying", is the correct
            // per-frame invariant here.)
            for (const auto& u : us) {
                CHECK((u.target == -1 || (u.target >= 0 &&
                                          u.target < static_cast<int>(us.size()) &&
                                          us[u.target].alive)));
            }

            int gi_alive = 0, cons_alive = 0;
            for (int gi_i : gi_idx)
                if (us[gi_i].alive && us[gi_i].die_time < 0.0) ++gi_alive;
            for (int cons_i : cons_idx)
                if (us[cons_i].alive && us[cons_i].die_time < 0.0) ++cons_alive;
            if (gi_alive == 0 || cons_alive == 0) {
                ended = true;
                break;
            }
        }
        // Gap fix C (G-50): with the original's ROF Random(0,2) jitter the
        // kill order can leave a GI and a Conscript idle 5 cells apart (range
        // 4): a legitimate standoff (idle units only engage within weapon
        // range), so "resolved" = one side eliminated OR every surviving
        // pair is out of range and idle.
        if (!ended) {
            const auto& us = world.units();
            for (int gi_i : gi_idx)
                for (int cons_i : cons_idx) {
                    const auto& a = us[gi_i];
                    const auto& b = us[cons_i];
                    if (!a.alive || a.die_time >= 0.0 || !b.alive || b.die_time >= 0.0) continue;
                    const double d = std::hypot(a.fx - b.fx, a.fy - b.fy);
                    CHECK(d > a.w_range);
                    CHECK(a.target == -1);
                    CHECK(b.target == -1);
                }
        }

        std::vector<std::tuple<int, int, int, bool, bool>> final_state;
        for (const auto& u : world.units())
            final_state.emplace_back(u.hp, u.cell_x, u.cell_y, u.alive, u.die_time >= 0.0);
        return final_state;
    };

    ra2engine::World world1(map);
    const auto final1 = run_battle(world1);

    ra2engine::World world2(map);
    const auto final2 = run_battle(world2);

    // Bit determinism: an identical run from a fresh World reaches the exact
    // same final state.
    REQUIRE(final1.size() == final2.size());
    CHECK(final1 == final2);
}

TEST_CASE("World: order_attack sets target+attack_order on selected units only") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    std::vector<int> gi_idx = spawn_combat_squad(world, c->x, c->y, 0, gi_stats(), 3);
    std::vector<int> cons_idx = spawn_combat_squad(world, c->x + 3, c->y, 1, cons_stats(), 1);
    REQUIRE(gi_idx.size() == 3);
    REQUIRE(cons_idx.size() == 1);
    const int hostile = cons_idx[0];

    // Select only the first two GIs.
    tsel_.set(gi_idx[0]);
    tsel_.set(gi_idx[1]);
    tsel_.set(gi_idx[2], false);

    world.group_attack(0, tsel_.ids(), hostile);

    CHECK(world.units()[gi_idx[0]].target == hostile);
    CHECK(world.units()[gi_idx[0]].attack_order == true);
    CHECK(world.units()[gi_idx[1]].target == hostile);
    CHECK(world.units()[gi_idx[1]].attack_order == true);
    // Unselected GI untouched.
    CHECK(world.units()[gi_idx[2]].target == -1);
    CHECK(world.units()[gi_idx[2]].attack_order == false);

    tsel_.clear();
    tsel_.set(gi_idx[2]);

    // No-op: out-of-range target index.
    world.group_attack(0, tsel_.ids(), static_cast<int>(world.units().size()) + 5);
    CHECK(world.units()[gi_idx[2]].target == -1);
    CHECK(world.units()[gi_idx[2]].attack_order == false);

    // No-op: target is house 0 (friendly) -- can't order an attack on it.
    world.group_attack(0, tsel_.ids(), gi_idx[0]);
    CHECK(world.units()[gi_idx[2]].target == -1);

    // No-op: dead target.
    world.units()[hostile].alive = false;
    world.group_attack(0, tsel_.ids(), hostile);
    CHECK(world.units()[gi_idx[2]].target == -1);
    world.units()[hostile].alive = true;  // restore

    // No-op: dying target.
    world.units()[hostile].die_time = 0.2;
    world.group_attack(0, tsel_.ids(), hostile);
    CHECK(world.units()[gi_idx[2]].target == -1);
    world.units()[hostile].die_time = -1.0;  // restore

    // A selected but dying unit does not get a target set either.
    world.units()[gi_idx[2]].die_time = 0.1;
    world.group_attack(0, tsel_.ids(), hostile);
    CHECK(world.units()[gi_idx[2]].target == -1);
}

TEST_CASE("World: dead units are unpickable and non-occupying") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(3.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    std::vector<int> gi_idx = spawn_combat_squad(world, c->x, c->y, 0, gi_stats(), 1);
    std::vector<int> cons_idx = spawn_combat_squad(world, c->x + 3, c->y, 1, cons_stats(), 1);
    REQUIRE(gi_idx.size() == 1);
    REQUIRE(cons_idx.size() == 1);
    const int victim = cons_idx[0];
    world.units()[victim].w_damage = 0;  // disarm: isolate the GI's attack (no retaliation kill-race)

    tsel_.set(gi_idx[0]);
    world.group_attack(0, tsel_.ids(), victim);

    const int vx = world.units()[victim].cell_x;
    const int vy = world.units()[victim].cell_y;
    REQUIRE(world.unit_at_cell(vx, vy) == victim);  // alive: pickable, occupies its cell

    const double dt = 1.0 / 60.0;
    double t = 0.0;
    while (world.units()[victim].die_time < 0.0 && t < 30.0) {
        world.step(dt);
        t += dt;
    }
    REQUIRE(world.units()[victim].die_time >= 0.0);
    REQUIRE(world.units()[victim].alive);  // dying, not yet tombstoned

    // Dying: unpickable and non-occupying, but still rendered (die anim).
    CHECK(world.unit_at_cell(vx, vy) != victim);
    CHECK(world.occupancy().at(vx, vy) == ra2engine::Occupancy::kEmpty);
    {
        const auto order = ra2engine::unit_draw_order(world.units());
        bool found = false;
        for (int idx : order)
            if (idx == victim) found = true;
        CHECK(found);
    }

    // Run the death animation out to tombstone (alive = false).
    while (world.units()[victim].alive && t < 32.0) {
        world.step(dt);
        t += dt;
    }
    REQUIRE_FALSE(world.units()[victim].alive);

    CHECK(world.unit_at_cell(vx, vy) != victim);
    CHECK(world.occupancy().at(vx, vy) == ra2engine::Occupancy::kEmpty);
    {
        const auto order = ra2engine::unit_draw_order(world.units());
        bool found = false;
        for (int idx : order)
            if (idx == victim) found = true;
        CHECK_FALSE(found);  // tombstoned: excluded from draw order
    }
}

TEST_CASE("World: attack order halts the pursuer at weapon range (no melee overshoot)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // GI (house 0) at the start cell; a hostile Conscript ~6 cells away (well
    // beyond the 4-cell weapon range) at a reachable, unoccupied cell.
    ra2engine::UnitCombat gi{125, "none", "GI", 15, 20, 4.0,
        {100,80,80,50,25,25,75,50,25,100,100}};
    ra2engine::UnitCombat co{125, "flak", "CONS", 15, 25, 4.0,
        {100,80,80,50,25,25,75,50,25,100,100}};
    world.add_combat_unit(c->x, c->y, 0, gi);
    // find_reachable's nearest-first scan (starting at radius 4) can land
    // exactly on the 4-cell weapon range boundary rather than beyond it (on
    // this map, the only radius-4 reachable offsets from the start cell are
    // the cardinal (+-4,0) -- confirmed via debug trace, and every other
    // radius-4 direction is blocked by terrain). Rather than rely on
    // find_reachable's shared radius-4 start, scan directly here starting at
    // radius 6, which guarantees a strictly-out-of-range distance (>4)
    // whether the hit is cardinal (d=6) or diagonal (d~8.49), so the pursue
    // transition this test exercises is actually triggered. ALSO requires
    // open ground around the candidate (open_neighbor_count) -- Task 3's
    // approach logic resolves a firing position NEAR the target via
    // assign_formation rather than pathing directly to it, so a target
    // boxed in by terrain would leave the GI no in-range cell to walk to at
    // all (confirmed via debug trace: a boxed-in candidate this scan used to
    // accept left the GI stuck 4.12 cells out -- just outside range).
    int ex = 0, ey = 0;
    double d0 = 0.0;
    bool found_far = find_reachable_open(world, c->x, c->y, 6, 4, ex, ey, d0);
    REQUIRE(found_far);
    REQUIRE(d0 > 4.0);
    world.add_combat_unit(ex, ey, 1, co);   // hostile, index 1

    // Select the GI and order the attack; the Conscript does NOT move or fire
    // (leave it idle: it will auto-acquire/return-fire, which is fine).
    tsel_.set(0);
    world.group_attack(0, tsel_.ids(), 1);
    REQUIRE(world.units()[0].attack_order);

    const int hp0 = world.units()[1].hp;
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 20.0; t += dt) world.step(dt);

    // The GI closed to within weapon range and STOPPED there (did not march to
    // melee contact) and has fired at least once.
    const ra2engine::Unit& g = world.units()[0];
    const ra2engine::Unit& e = world.units()[1];
    const double df = std::sqrt((e.fx - g.fx) * (e.fx - g.fx) + (e.fy - g.fy) * (e.fy - g.fy));
    CHECK(df <= 4.0 + 1e-6);        // reached effective range
    CHECK(df >= 2.0);               // held at range -- did NOT close to melee (~1 cell)
    CHECK_FALSE(g.moving());        // holding, not still walking
    CHECK(e.hp < hp0);              // fired on the target while holding
}

TEST_CASE("combat: five attackers fan out and ALL fire (no single-file queue)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    // Regression for the reported bug: a group attack order used to send
    // EVERY attacker to the target's own occupied cell (combat.cpp's old §f
    // attack_order branch emitted {i, v.cell_x, v.cell_y} for every unit), so
    // they queued single-file behind whoever got in range first and never
    // fanned out to distinct firing positions -- only ONE attacker ever fired
    // (the others sat blocked, out of weapon range, forever). docs/re/
    // infantry-anim.md §7: the original approaches until in weapon range and
    // never paths to the victim's own cell; World::step now batch-assigns
    // each attacker its own free firing cell via assign_formation.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // Tough, disarmed victim (house 1) far enough away (> the 4-cell weapon
    // range) that every attacker must actually approach it. Huge hp so the
    // victim survives the whole 12s window; damage=0 so no attacker dies
    // mid-test (a death would vacate a cell and could mask the single-file
    // symptom by letting the next queued attacker shuffle forward).
    ra2engine::UnitCombat victim_stats = cons_stats();
    victim_stats.strength = 100000;
    victim_stats.damage = 0;

    // Needs at least 5 open (walkable) immediate neighbors around the victim
    // cell -- see open_neighbor_count -- so assign_formation actually has 5
    // distinct, in-range firing positions to hand out (a candidate near a
    // map edge/cliff/water could otherwise leave it no choice but to hand out
    // cells beyond weapon range, which isn't the bug this test targets).
    int vx = 0, vy = 0;
    double dv = 0.0;
    bool found_far = find_reachable_open(world, c->x, c->y, 8, 5, vx, vy, dv);
    REQUIRE(found_far);
    REQUIRE(dv > 4.0);
    world.add_combat_unit(vx, vy, 1, victim_stats);
    const int victim_idx = static_cast<int>(world.units().size()) - 1;

    // Five house-0 attackers CLUSTERED tightly near the start cell -- the
    // "single-file-prone" formation from the user's bug report (spawn_combat_
    // squad packs them into adjacent free cells scanning outward from base).
    std::vector<int> attackers = spawn_combat_squad(world, c->x, c->y, 0, gi_stats(), 5);
    REQUIRE(attackers.size() == 5);

    tsel_.clear();
    for (int a : attackers) tsel_.set(a);
    world.group_attack(0, tsel_.ids(), victim_idx);
    for (int a : attackers) REQUIRE(world.units()[a].attack_order);

    bool has_fired[5] = {false, false, false, false, false};
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 12.0; t += dt) {
        world.step(dt);
        for (std::size_t k = 0; k < attackers.size(); ++k)
            if (world.units()[attackers[k]].fire_flash > 0.0) has_fired[k] = true;
    }

    for (std::size_t k = 0; k < attackers.size(); ++k) CHECK(has_fired[k]);

    // Every attacker ended within weapon range of the victim.
    const ra2engine::Unit& v = world.units()[victim_idx];
    for (int a : attackers) {
        const ra2engine::Unit& u = world.units()[a];
        const double dx = v.fx - u.fx, dy = v.fy - u.fy;
        CHECK(std::sqrt(dx * dx + dy * dy) <= u.w_range + 1e-6);
    }
}

// ---- Move-cancels-engagement regression (freeze seam: hold-at-range pass
// clearing the path of a moving unit that still carried an auto-acquired or
// retaliation target) ----

namespace {
// Scan a short list of close offsets from (bx, by) for the first walkable,
// unoccupied cell -- used by the two tests below to park a Conscript within
// weapon range (4.0 cells) of a GI's start cell via a direct add_combat_unit
// call (not the squad-scan helpers above, which aren't guaranteed to land
// close). Ordered nearest-ish first; the wide spread of directions/radii
// makes it very unlikely every candidate is blocked on this map (the same
// neighborhood other tests in this file already pack up to 6 units into).
bool find_close_free_cell(const ra2engine::World& w, int bx, int by, int& ox, int& oy) {
    struct Off { int dx, dy; };
    static const Off offs[] = {{3, 0},  {0, 3},  {-3, 0}, {0, -3}, {2, 2},  {2, -2},
                               {-2, 2}, {-2, -2}, {2, 0},  {0, 2},  {-2, 0}, {0, -2},
                               {1, 1},  {1, -1},  {-1, 1}, {-1, -1}, {1, 0}, {0, 1},
                               {-1, 0}, {0, -1}};
    for (const auto& o : offs) {
        const int x = bx + o.dx, y = by + o.dy;
        if (w.walkable(x, y) && w.unit_at_cell(x, y) < 0) { ox = x; oy = y; return true; }
    }
    return false;
}
}  // namespace

TEST_CASE("World: a group-move order cancels engagement and is never frozen by the hold-pass") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    // Regression for the freeze seam this task fixes: a GI holding an
    // AUTO-ACQUIRED target (idle, in weapon range) that is then given a group
    // move order used to have its path repeatedly cleared by the hold-at-range
    // pass every single frame (the pass only checked "has a live target still
    // in range", not "is this an explicit attack order") -- frozen in place,
    // never reaching the ordered cell. order_move_selected must now cancel
    // the engagement outright, so the hold-at-range pass never touches it.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    world.add_combat_unit(c->x, c->y, 0, gi_stats());  // GI, index 0

    // Conscript (idle, house 1) within weapon range (4.0 cells) of the GI's
    // start cell.
    int conx = -1, cony = -1;
    REQUIRE(find_close_free_cell(world, c->x, c->y, conx, cony));
    const double d_init =
        std::sqrt(double((conx - c->x) * (conx - c->x) + (cony - c->y) * (cony - c->y)));
    REQUIRE(d_init <= 4.0 + 1e-6);  // in range
    world.add_combat_unit(conx, cony, 1, cons_stats());  // Conscript, index 1

    // One step: both units are idle and in range of each other -> the GI
    // auto-acquires the Conscript.
    world.step(1.0 / 60.0);
    REQUIRE(world.units()[0].target == 1);

    // A goal at least 5 cells away (well beyond weapon range). Reuses this
    // file's far-goal probe pattern (kProbeDirs at growing radius, confirmed
    // reachable via find_path) rather than the shared find_reachable helper:
    // the earlier "attack order halts pursuer" test found find_reachable's
    // radius-4 start can land exactly on this map's cardinal offsets from
    // this same start cell (distance == weapon range, not "well beyond" it).
    // Starting the scan at radius 6 guarantees a strictly ->=5-cell goal
    // regardless of which directions this map allows.
    int gx = 0, gy = 0;
    double dgoal = 0.0;
    bool found_far = false;
    for (int r = 6; r <= 40 && !found_far; r += 2) {
        for (const auto& d : kProbeDirs) {
            const int dx = d[0] * r, dy = d[1] * r;
            const int tx = c->x + dx, ty = c->y + dy;
            auto p = ra2engine::find_path(world.passable(), world.grid_w(), world.grid_h(),
                                          {c->x, c->y}, {tx, ty});
            if (p.size() > 1) {
                gx = tx;
                gy = ty;
                dgoal = std::sqrt(double(dx * dx + dy * dy));
                found_far = true;
                break;
            }
        }
    }
    REQUIRE(found_far);
    REQUIRE(dgoal >= 5.0);

    tsel_.clear();
    tsel_.set(0);
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), gx, gy);

    // The move order must cancel engagement immediately -- load-bearing fix.
    CHECK(world.units()[0].target == -1);
    CHECK_FALSE(world.units()[0].attack_order);

    // Simulate: pre-fix, the hold-at-range pass would clear this path every
    // frame (the target was still in range at the moment of the order) and
    // the GI would never move at all. Post-fix it walks unimpeded to the
    // ordered cell.
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 30.0 && world.units()[0].moving(); t += dt) world.step(dt);

    REQUIRE_FALSE(world.units()[0].moving());
    REQUIRE(world.units()[0].cell_x == gx);
    REQUIRE(world.units()[0].cell_y == gy);
}

TEST_CASE("World: moving units do not auto-acquire or return fire (idle-only engagement)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    // Spec wording: "IDLE/guarding units auto-acquire hostiles in range and
    // return fire when hit" -- a unit executing a player move order is
    // neither idle nor guarding. GI ordered to walk away from an in-range
    // idle Conscript: the Conscript (idle) DOES auto-acquire and fires on the
    // GI (whose start cell is already within the Conscript's range), but the
    // GI must never acquire a target or retaliate while moving() is true --
    // it takes damage but keeps walking -- and must still arrive.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    world.add_combat_unit(c->x, c->y, 0, gi_stats());  // GI, index 0

    // Conscript (idle, house 1) within weapon range of the GI's start cell --
    // the GI's own starting position is already in range, so the Conscript
    // can acquire and fire the instant combat runs, regardless of which
    // direction the GI is then ordered to walk.
    int conx = -1, cony = -1;
    REQUIRE(find_close_free_cell(world, c->x, c->y, conx, cony));
    const double d_init =
        std::sqrt(double((conx - c->x) * (conx - c->x) + (cony - c->y) * (cony - c->y)));
    REQUIRE(d_init <= 4.0 + 1e-6);  // in range
    world.add_combat_unit(conx, cony, 1, cons_stats());  // Conscript, index 1

    // A far goal (direction is irrelevant to the engagement window -- the
    // Conscript is already in range of the GI's own start cell before it has
    // moved anywhere).
    int gx = 0, gy = 0;
    bool found_far = false;
    for (int r = 8; r <= 40 && !found_far; r += 2) {
        for (const auto& d : kProbeDirs) {
            const int dx = d[0] * r, dy = d[1] * r;
            const int tx = c->x + dx, ty = c->y + dy;
            auto p = ra2engine::find_path(world.passable(), world.grid_w(), world.grid_h(),
                                          {c->x, c->y}, {tx, ty});
            if (p.size() > 1) {
                gx = tx;
                gy = ty;
                found_far = true;
                break;
            }
        }
    }
    REQUIRE(found_far);

    tsel_.set(0);
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), gx, gy);
    REQUIRE(world.units()[0].moving());
    REQUIRE(world.units()[0].target == -1);

    const double dt = 1.0 / 60.0;
    double t = 0.0;
    // W6-02-11: an idle unit now scans its whole threat radius (range + 1),
    // so on ARRIVAL the GI may acquire the chasing Conscript and close in --
    // the property under test is the walk itself, which ends at the goal.
    while (world.units()[0].moving() && t < 30.0 &&
           !(world.units()[0].cell_x == gx && world.units()[0].cell_y == gy)) {
        CHECK(world.units()[0].target == -1);
        world.step(dt);
        t += dt;
    }

    REQUIRE(world.units()[0].cell_x == gx);
    REQUIRE(world.units()[0].cell_y == gy);
    CHECK(world.units()[0].hp < 125);  // it was shot en route
}

// ---- pal_for_house (Task 7: house palette remap) ----

namespace {
// Hand-built unittem.pal stand-in: index 5 is a sentinel OUTSIDE the 16..31
// remap ramp (verifies non-ramp entries are left alone); index 20 is the
// task brief's own worked example -- (60,90,150), brightness
// (60+90+150)/3 == 100 -> house-0 remap (100*2/5, 100*3/5, 100) == (40,60,100)
// -- with a non-255 alpha (200) to verify alpha is preserved, not
// clobbered; indices 16 and 31 are the ramp's inclusive boundaries.
// Uses Palette's mutable operator[] (added this task alongside the existing
// const overload -- pal_for_house needs write access to build a derived
// palette from a copy, which the const-only accessor didn't allow).
ra2assets::Palette make_test_pal() {
    uint8_t raw[768] = {0};
    ra2assets::Palette pal = ra2assets::Palette::load(raw, 768);  // all zero, alpha 255
    pal[5] = ra2assets::Rgba{11, 22, 33, 255};     // sentinel, OUTSIDE the ramp
    pal[16] = ra2assets::Rgba{200, 10, 10, 255};   // ramp lower boundary
    pal[20] = ra2assets::Rgba{60, 90, 150, 200};   // brief's worked example
    pal[31] = ra2assets::Rgba{90, 5, 5, 255};      // ramp upper boundary
    pal[32] = ra2assets::Rgba{7, 8, 9, 255};        // sentinel, just OUTSIDE the ramp
    return pal;
}
}  // namespace

TEST_CASE("pal_for_house: house 1 (Soviet) returns the palette unchanged (identity)") {
    const ra2assets::Palette src = make_test_pal();
    const ra2assets::Palette out = ra2engine::pal_for_house(src, 1);
    for (int i = 0; i < 256; ++i) {
        CHECK(out[i].r == src[i].r);
        CHECK(out[i].g == src[i].g);
        CHECK(out[i].b == src[i].b);
        CHECK(out[i].a == src[i].a);
    }
}

TEST_CASE("pal_for_house: house 0 (Allied) rescales ramp entries onto a blue ramp by original brightness") {
    const ra2assets::Palette src = make_test_pal();
    const ra2assets::Palette out = ra2engine::pal_for_house(src, 0);

    // Brief's worked example: entry 20 = (60,90,150) -> brightness 100 ->
    // (40, 60, 100), alpha (200) preserved verbatim.
    CHECK(out[20].r == 40);
    CHECK(out[20].g == 60);
    CHECK(out[20].b == 100);
    CHECK(out[20].a == 200);

    // Ramp boundary 16 = (200,10,10) -> brightness (200+10+10)/3 == 73 (int
    // division) -> (73*2/5, 73*3/5, 73) == (29, 43, 73).
    CHECK(out[16].r == 29);
    CHECK(out[16].g == 43);
    CHECK(out[16].b == 73);
    CHECK(out[16].a == 255);
}

TEST_CASE("pal_for_house: indices outside 16..31 are untouched by either house") {
    const ra2assets::Palette src = make_test_pal();
    const ra2assets::Palette out0 = ra2engine::pal_for_house(src, 0);
    const ra2assets::Palette out1 = ra2engine::pal_for_house(src, 1);
    for (const ra2assets::Palette* out : {&out0, &out1}) {
        CHECK((*out)[5].r == src[5].r);
        CHECK((*out)[5].g == src[5].g);
        CHECK((*out)[5].b == src[5].b);
        CHECK((*out)[5].a == src[5].a);
        CHECK((*out)[32].r == src[32].r);
        CHECK((*out)[32].g == src[32].g);
        CHECK((*out)[32].b == src[32].b);
    }
}

// ---- Fire/die anim-override pass (Task 7 introduced; Task 2 recalibrated
// the frame values + added facing -- docs/re/infantry-anim.md) ----

TEST_CASE("World::step anim-override: fire_flash > 0 selects the unit's fire frame range and visits all frames during flash window") {
    // No real map needed -- this exercises Unit::frame selection only, not
    // terrain/pathing, so a default-constructed (cell-less) MapFile is fine
    // (World tolerates an empty map: build_passability(empty) -> 0x0 grid,
    // Occupancy(0,0) no-ops all accesses -- see occupancy.hpp).
    ra2assets::MapFile map;
    ra2engine::World world(map);
    ra2engine::Unit& u = world.add_combat_unit(0, 0, 0, gi_stats());  // image "GI"
    const ra2engine::UnitAnimSeq& seq = ra2engine::default_anim();

    // Mid-flash sanity check: frame selection is within range, at whatever
    // facing the unit currently has (no target set -> stays the spawn
    // default, but the assertion is written facing-relative on purpose).
    u.fire_flash = 0.3;
    world.step(1.0 / 60.0);
    const int base = seq.fire.first + u.facing * seq.fire.fmult;
    CHECK(u.frame >= base);
    CHECK(u.frame < base + seq.fire.count);

    // Exhaustiveness check: simulate a full flash window from fire_flash = 0.4
    // (kFireFlashSecs) down to 0, collecting all visited frames. Facing stays
    // fixed throughout (no target -> the aim-facing pass never touches it),
    // proving all 6 fire frames are reachable at that one fixed facing.
    ra2engine::World world2(map);
    ra2engine::Unit& u2 = world2.add_combat_unit(0, 0, 0, gi_stats());
    const ra2engine::UnitAnimSeq& seq2 = ra2engine::default_anim();

    std::set<int> visited_frames;
    u2.fire_flash = 0.4;  // start of flash window
    const double dt = 1.0 / 60.0;

    while (u2.fire_flash > 0.0) {
        world2.step(dt);
        // u2.fire_flash is decremented by step_combat during step()
        // Record the frame after each step while still in flash window.
        if (u2.fire_flash >= 0.0) {
            visited_frames.insert(u2.frame);
        }
    }

    // After fire_flash reaches 0, verify all fire frames were visited.
    REQUIRE(seq2.fire.count > 0);
    for (int i = 0; i < seq2.fire.count; ++i) {
        const int expected_frame = ra2engine::seq_frame(seq2.fire, u2.facing, i);
        CHECK(visited_frames.count(expected_frame) > 0);
    }
}

TEST_CASE("World::step anim-override: die_time selects the die range and reaches the last frame before tombstone") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    ra2engine::Unit& u = world.add_combat_unit(0, 0, 1, cons_stats());  // image "CONS"
    const ra2engine::UnitAnimSeq& seq = ra2engine::default_anim();

    // Early in the death animation: not yet at the end.
    u.die_time = 0.05;
    world.step(1.0 / 60.0);
    REQUIRE(u.alive);  // still dying, not yet tombstoned
    CHECK(u.frame >= seq.die.first);
    CHECK(u.frame < seq.die.first + seq.die.count);

    // Late in the death animation, but still short of combat.cpp's
    // kDieAnimSecs (1.0s) -- default_anim's die entry (134,15,0) is EXACTLY
    // 15 frames at kDieAnimFps (15Hz, world.cpp) == kDieAnimSecs (1.0s), so
    // the WHOLE die range (134..148) is reachable before the unit tombstones
    // (docs/re/infantry-anim.md §2/§3): this reaches the LAST die frame (148)
    // while still alive, proving frame 148 is actually displayed (the Task 7
    // bug this replaces never reached its analogous last frame at all).
    u.die_time = 0.95;
    world.step(1.0 / 60.0);
    REQUIRE(u.alive);  // still dying at this die_time (< kDieAnimSecs)
    CHECK(u.frame == seq.die.first + seq.die.count - 1);
    CHECK(u.frame == 148);
}

// ---- Task 2: facing + artmd-calibrated animation table (docs/re/infantry-anim.md) ----

TEST_CASE("facing: 8-way cell-direction table (docs/re/infantry-anim.md §5)") {
    using ra2engine::facing_from_delta;
    CHECK(facing_from_delta(0, -1) == 7);   // screen NE
    CHECK(facing_from_delta(1, -1) == 6);   // E
    CHECK(facing_from_delta(1, 0) == 5);    // SE
    CHECK(facing_from_delta(1, 1) == 4);    // S
    CHECK(facing_from_delta(0, 1) == 3);    // SW
    CHECK(facing_from_delta(-1, 1) == 2);   // W
    CHECK(facing_from_delta(-1, 0) == 1);   // NW
    CHECK(facing_from_delta(-1, -1) == 0);  // N
    CHECK(facing_from_delta(0, 0) == -1);   // no direction -> keep current
    CHECK(facing_from_delta(5.0, 5.0) == 4);      // continuous, same ray as (1,1)
    CHECK(facing_from_delta(0.1, -0.1) == 6);     // continuous E ray
}

TEST_CASE("facing: seq_frame formula and real default table") {
    using namespace ra2engine;
    const UnitAnimSeq& a = default_anim();
    CHECK(a.stand.first == 0);   CHECK(a.stand.count == 1);  CHECK(a.stand.fmult == 1);
    CHECK(a.walk.first == 8);    CHECK(a.walk.count == 6);   CHECK(a.walk.fmult == 6);
    CHECK(a.fire.first == 164);  CHECK(a.fire.count == 6);   CHECK(a.fire.fmult == 6);
    CHECK(a.die.first == 134);   CHECK(a.die.count == 15);   CHECK(a.die.fmult == 0);
    CHECK(seq_frame(a.stand, 3, 0) == 3);          // stand facing SW -> frame 3
    CHECK(seq_frame(a.fire, 6, 2) == 164 + 36 + 2); // fire E phase 2 -> 202
    CHECK(seq_frame(a.die, 6, 4) == 138);           // die ignores facing (fmult 0)
    CHECK(seq_frame(a.fire, 0, 99) == 169);         // phase clamps to count-1
    AnimTable t;
    UnitAnimSeq custom = a; custom.fire.first = 500;
    t["ZZZ"] = custom;
    CHECK(anim_for_image(t, "ZZZ").fire.first == 500);
    CHECK(anim_for_image(t, "GI").fire.first == 164);  // miss -> default
}

TEST_CASE("Unit: spawns facing S (facing == 4, docs/re/infantry-anim.md §6)") {
    ra2engine::Unit u;
    CHECK(u.facing == 4);
}

TEST_CASE("World::step: walking unit faces its segment (docs/re/infantry-anim.md §5)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(3.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // Find a walkable start cell whose east neighbor is ALSO walkable and
    // unoccupied (pick_start_cell's centroid cell alone isn't guaranteed to
    // have a walkable east neighbor at a map edge/obstacle).
    int sx = -1, sy = -1;
    for (int dy = 0; dy < 8 && sx < 0; ++dy)
        for (int dx = 0; dx < 8 && sx < 0; ++dx) {
            const int x = c->x + dx, y = c->y + dy;
            if (world.walkable(x, y) && world.walkable(x + 1, y) &&
                world.unit_at_cell(x, y) < 0 && world.unit_at_cell(x + 1, y) < 0) {
                sx = x;
                sy = y;
            }
        }
    REQUIRE(sx >= 0);

    world.add_unit(sx, sy, "GI", 0);
    REQUIRE(world.units()[0].facing == 4);  // spawn facing S
    tsel_.set(0);
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), sx + 1, sy);  // one cell east
    REQUIRE(world.units()[0].moving());

    world.step(1.0 / 60.0);  // small dt at 3 cells/sec -> still mid-segment
    CHECK(world.units()[0].facing == 5);  // faces the segment: delta (1,0) -> SE
    CHECK(world.units()[0].frame >= 8 + 5 * 6);
    CHECK(world.units()[0].frame < 8 + 5 * 6 + 6);
}

TEST_CASE("World::step anim-override: firing unit faces its target (docs/re/infantry-anim.md §5)") {
    // No real map needed (same minimal fixture as the fire/die override tests
    // above): two stationary combat units, attacker due WEST of victim so
    // the attacker's delta-to-victim is (1,0) -> facing 5 (SE).
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.add_combat_unit(0, 0, 0, gi_stats());    // index 0: attacker (GI, house 0)
    world.add_combat_unit(1, 0, 1, cons_stats());  // index 1: victim (CONS, house 1), due east

    // Idle proximity: step_combat auto-acquires the nearest hostile in range
    // and fires immediately (rearm_frames starts at 0) -- no explicit order needed.
    world.step(1.0 / 60.0);

    const ra2engine::Unit& attacker = world.units()[0];
    REQUIRE(attacker.target == 1);
    CHECK(attacker.facing == 5);  // faces the victim even between/during shots
    REQUIRE(attacker.fire_flash > 0.0);
    const ra2engine::UnitAnimSeq& seq = ra2engine::default_anim();
    const int base = seq.fire.first + attacker.facing * seq.fire.fmult;
    CHECK(attacker.frame >= base);
    CHECK(attacker.frame < base + seq.fire.count);
}

// ---- Buildings & Construction Task 4: World integration (blocking, attack) ----
// Real-map fixture (same load_real_map_w/pick_start_cell precedent as every
// other World integration test in this file) -- MapFile has no public
// cell-injecting constructor, so an "in-memory" fixture would mean hand-
// synthesizing IsoMapPack5 LZO1X bytes, which test_lzo1x.cpp documents as
// impractical; the real-map-or-skip pattern is this file's established way
// of getting real walkable terrain into a World.

TEST_CASE("world: add_building blocks passability, death restores it") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World w(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // A 2x2 walkable block with a 5th walkable cell 2 cells east of its
    // origin (outside the foundation, to prove only the foundation itself
    // gets blocked).
    int bx = -1, by = -1;
    for (int dy = 0; dy < 10 && bx < 0; ++dy)
        for (int dx = 0; dx < 10 && bx < 0; ++dx) {
            const int x = c->x + dx, y = c->y + dy;
            if (w.walkable(x, y) && w.walkable(x + 1, y) && w.walkable(x, y + 1) &&
                w.walkable(x + 1, y + 1) && w.walkable(x + 2, y)) {
                bx = x;
                by = y;
            }
        }
    REQUIRE(bx >= 0);
    REQUIRE(w.walkable(bx, by));

    const int bi = w.add_building(/*type*/ 0, bx, by, /*fw*/ 2, /*fh*/ 2, /*house*/ 1,
                                  /*strength*/ 100, /*armor_ix*/ 0, /*buildup_total*/ 0.0,
                                  /*instant*/ true);
    CHECK_FALSE(w.walkable(bx, by));
    CHECK_FALSE(w.walkable(bx + 1, by + 1));
    CHECK(w.walkable(bx + 2, by));
    CHECK(w.building_at_cell(bx + 1, by) == bi);
    CHECK(w.building_at_cell(bx + 2, by) == -1);

    ra2engine::kill_building(w.buildings()[bi]);
    w.step(0.001);  // death detected -> passability restored
    CHECK(w.walkable(bx, by));
    CHECK(w.building_at_cell(bx, by) == -1);

    w.step(ra2engine::kBuildingDieSecs + 0.1);
    CHECK_FALSE(w.buildings()[bi].alive);  // tombstoned, never erased
}

TEST_CASE("world: AI-house power OUTPUT is zeroed by blackout at the AI production "
          "site [RE: capture.md §B2]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World w(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // A walkable 2x2 for the enemy power plant.
    int bx = -1, by = -1;
    for (int dy = 0; dy < 12 && bx < 0; ++dy)
        for (int dx = 0; dx < 12 && bx < 0; ++dx) {
            const int x = c->x + dx, y = c->y + dy;
            if (w.walkable(x, y) && w.walkable(x + 1, y) && w.walkable(x, y + 1) &&
                w.walkable(x + 1, y + 1)) {
                bx = x;
                by = y;
            }
        }
    REQUIRE(bx >= 0);

    // Roster with one +200-output power plant type -- exactly the shape of the
    // roster the AI site passes to power_totals(buildings(), roster, h).
    std::vector<ra2engine::BuildingTypeDef> roster(1);
    roster[0].power = 200;

    // Enemy/AI house 1 owns the plant (instant -> Idle -> counted by power_totals).
    const int bi = w.add_building(/*type*/ 0, bx, by, /*fw*/ 2, /*fh*/ 2, /*house*/ 1,
                                  /*strength*/ 100, /*armor_ix*/ 0, /*buildup_total*/ 0.0,
                                  /*instant*/ true);
    REQUIRE(bi >= 0);

    // Timer 0 -> the AI site's apply_blackout is IDENTITY: full output.
    CHECK_FALSE(w.house_blacked_out(1));
    const ra2engine::PowerTotals normal = ra2engine::apply_blackout(
        ra2engine::power_totals(w.buildings(), roster, 1), w.house_blacked_out(1));
    CHECK(normal.output == 200);

    // A house-0 spy infiltrates the enemy plant -> blackout_frames[1] armed.
    REQUIRE(w.infiltrate_building(bi, /*spy_house*/ 0,
                                  ra2engine::World::InfiltratePurpose::PowerPlant,
                                  /*steal_pct*/ 0.5, /*blackout*/ 1000));
    CHECK(w.blackout_frames(1) == 1000);
    CHECK(w.house_blacked_out(1));

    // Now the AI site's apply_blackout forces the ENEMY's power OUTPUT to 0
    // (its production + defenses go offline); drain is unaffected.
    const ra2engine::PowerTotals blacked = ra2engine::apply_blackout(
        ra2engine::power_totals(w.buildings(), roster, 1), w.house_blacked_out(1));
    CHECK(blacked.output == 0);
}

TEST_CASE("world: R3 an attack on a building stops at weapon range (not at its foundation)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    // [RE: infantry-anim.md §7 -- Mission_Attack stops the approach as soon as
    // the target is in range; FUN_006F6750 measures a building to its centre
    // with range + (w+h)*64.] Before R3 only unit targets held at range.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World w(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // A straight walkable run of 12 cells east of a start cell.
    int sx = -1, sy = -1;
    for (int dy = -20; dy <= 20 && sx < 0; ++dy)
        for (int dx = -20; dx <= 20 && sx < 0; ++dx) {
            bool ok = true;
            for (int k = 0; k <= 12 && ok; ++k) ok = w.walkable(c->x + dx + k, c->y + dy);
            if (ok) { sx = c->x + dx; sy = c->y + dy; }
        }
    REQUIRE(sx >= 0);
    ra2engine::UnitCombat gi = gi_stats();
    gi.rof = 2000;   // keep the building alive: the test is about where the unit stops
    w.add_combat_unit(sx, sy, 0, gi);
    const int bi = w.add_building(0, sx + 11, sy, 1, 1, /*house*/ 1, 1000, 0, 0.0, true);
    REQUIRE(bi >= 0);
    tsel_.set(0);
    w.group_attack_building(0, tsel_.ids(), bi);
    REQUIRE(w.units()[0].target_building == bi);
    for (int f = 0; f < 600; ++f) w.step(1.0 / 15.0);
    const ra2engine::Unit& u = w.units()[0];
    CHECK(u.target_building == bi);
    CHECK(u.path.empty());
    // Range 4 + (1+1)*64/256 = 4.5 cells to the centre: it halts ~4 cells
    // short, not on the adjacent cell (sx + 10).
    CHECK(sx + 11 - u.cell_x >= 4);
    CHECK(sx + 11 - u.cell_x <= 5);
}

TEST_CASE("world: order_attack_building sets exclusive target") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World w(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    w.add_combat_unit(c->x, c->y, 0, gi_stats());  // house-0 combat unit, index 0
    tsel_.set(0);

    // Friendly (house 0) building: ordering an attack on it must be a no-op.
    int fx = -1, fy = -1;
    REQUIRE(find_close_free_cell(w, c->x, c->y, fx, fy));
    const int bi_friendly = w.add_building(0, fx, fy, 1, 1, /*house*/ 0, 100, 0, 0.0, true);
    w.group_attack_building(0, tsel_.ids(), bi_friendly);
    CHECK(w.units()[0].target_building == -1);

    // Hostile (house 1) building elsewhere.
    int ex = -1, ey = -1;
    REQUIRE(find_close_free_cell(w, c->x + 3, c->y + 3, ex, ey));
    const int bi_enemy = w.add_building(0, ex, ey, 1, 1, /*house*/ 1, 100, 0, 0.0, true);
    w.group_attack_building(0, tsel_.ids(), bi_enemy);
    CHECK(w.units()[0].target_building == bi_enemy);
    CHECK(w.units()[0].target == -1);
    CHECK(w.units()[0].attack_order);

    // A subsequent order_attack(<enemy unit>) clears target_building (the two
    // target kinds are mutually exclusive).
    int hx = -1, hy = -1;
    REQUIRE(find_close_free_cell(w, c->x - 3, c->y - 3, hx, hy));
    w.add_combat_unit(hx, hy, 1, cons_stats());  // hostile unit, index 1
    w.group_attack(0, tsel_.ids(), 1);
    CHECK(w.units()[0].target_building == -1);
    CHECK(w.units()[0].target == 1);
    CHECK(w.units()[0].attack_order);
}

// ---- Economy phase Task 6: World integration (ore ownership, harvest step,
// income, orders) -- same real-map-or-skip fixture as every other World
// integration test in this file (see the comment above "world: add_building
// blocks passability..." for why: MapFile has no cell-injecting constructor).

TEST_CASE("World: add_harvester sets harvester fields + occupancy") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    ra2engine::Unit& u = world.add_harvester(c->x, c->y, 0, gi_stats(), /*chrono*/ false,
                                             /*storage*/ 40);
    CHECK(world.units().size() == 1);
    CHECK(u.is_harvester);
    CHECK_FALSE(u.chrono_miner);
    CHECK(u.storage_bales == 40);
    CHECK(u.harv == ra2engine::HarvState::SeekOre);   // starts hunting immediately
    CHECK(world.unit_at_cell(c->x, c->y) == 0);
    CHECK(world.occupancy().at(c->x, c->y) == 0);

    // CMIN variant: chrono flag + a different storage figure both carry through.
    int cx = -1, cy = -1;
    REQUIRE(find_close_free_cell(world, c->x, c->y, cx, cy));
    ra2engine::Unit& cmin = world.add_harvester(cx, cy, 1, cons_stats(), /*chrono*/ true,
                                                /*storage*/ 20);
    CHECK(cmin.chrono_miner);
    CHECK(cmin.storage_bales == 20);
    CHECK(cmin.house == 1);
}

TEST_CASE("World: order_harvest paths a selected harvester to ore and it gulps on arrival") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    int ox = -1, oy = -1;
    REQUIRE(find_close_free_cell(world, c->x, c->y, ox, oy));
    world.ore().set(ox, oy, ra2engine::OreKind::Ore, ra2engine::kMaxOreDensity, 0);

    ra2engine::Unit& u =
        world.add_harvester(c->x, c->y, 0, gi_stats(), /*chrono*/ false, /*storage*/ 40);
    tsel_.set(static_cast<int>(world.units().size()) - 1);
    world.group_harvest(0, tsel_.ids(), ox, oy);
    CHECK(u.harv == ra2engine::HarvState::MoveToOre);
    CHECK(u.harv_cell_x == ox);
    CHECK(u.harv_cell_y == oy);

    // step_harvest only runs once economy is configured -- growth/spread
    // parked at a huge interval so this short run never fires them (keeps
    // the test focused purely on the seek->arrive->gulp path).
    std::vector<ra2engine::BuildingTypeDef> types(1);
    types[0].id = "NAREFN";
    world.set_economy(types, {0}, ra2engine::OreValues{}, /*growth_interval*/ 1000.0,
                      /*growth_pct*/ 0.0, /*spread_pct*/ 0.0);

    bool gulped = false;
    for (int i = 0; i < 400 && !gulped; ++i) {
        world.step(0.1);
        if (u.bales_ore > 0.0 || world.ore().density_at(ox, oy) < ra2engine::kMaxOreDensity)
            gulped = true;
    }
    CHECK(gulped);
}

TEST_CASE("World: take_income drains harvest income once, then reads zero") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // Find a 4x3 refinery-shaped footprint (NAREFN) with its dock cell
    // (origin + (fw/2, fh), per dock_cell_x/y) also walkable.
    int bx = -1, by = -1;
    for (int dy = 0; dy < 16 && bx < 0; ++dy)
        for (int dx = 0; dx < 16 && bx < 0; ++dx) {
            const int x = c->x + dx, y = c->y + dy;
            bool ok = true;
            for (int fy = 0; fy < 3 && ok; ++fy)
                for (int fx = 0; fx < 4 && ok; ++fx)
                    if (!world.walkable(x + fx, y + fy)) ok = false;
            if (ok && world.walkable(x + 2, y + 3)) { bx = x; by = y; }
        }
    REQUIRE(bx >= 0);

    const int ref_idx = world.add_building(/*type*/ 0, bx, by, /*fw*/ 4, /*fh*/ 3, /*house*/ 0,
                                           /*strength*/ 1000, /*armor_ix*/ 0,
                                           /*buildup_total*/ 0.0, /*instant*/ true);
    const int dock_x = bx + 2, dock_y = by + 3;   // dock_cell_x/y, harvest.hpp

    ra2engine::Unit& u = world.add_harvester(dock_x, dock_y, 0, gi_stats(), /*chrono*/ false,
                                             /*storage*/ 40);
    // Drive the mini-cycle directly from Unloading (drives the same
    // per-bale drain step_harvest's own unload test exercises) rather than a
    // full seek/gulp/return walk -- this test is about take_income's
    // drain-once-then-zero contract, not the whole FSM.
    u.harv = ra2engine::HarvState::Unloading;
    u.dock_refinery = ref_idx;
    u.bales_ore = 2;    // 2 * OreValues::ore (25) = 50 credits

    std::vector<ra2engine::BuildingTypeDef> types(1);
    types[0].id = "NAREFN";
    world.set_economy(types, {0}, ra2engine::OreValues{}, /*growth_interval*/ 1000.0,
                      /*growth_pct*/ 0.0, /*spread_pct*/ 0.0);

    for (int i = 0; i < 50 && (u.bales_ore > 0.0 || u.bales_gems > 0.0); ++i)
        world.step(ra2engine::kUnloadSecsPerBale);
    CHECK(u.bales_ore == 0.0);

    const double income = world.take_income(0);
    CHECK(income == doctest::Approx(50.0));
    CHECK(world.take_income(0) == doctest::Approx(0.0));   // drained once -> zero after
    CHECK(world.take_income(1) == doctest::Approx(0.0));   // house 1 untouched
}

TEST_CASE("World: step() without set_economy runs zero ore/harvest activity "
         "(byte-identity gate)") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // Even with an ore cell manually placed and a harvester mid-mission,
    // set_economy was never called -> types_ is empty -> step() must not
    // touch ore growth/spread OR the harvester's mission state at all.
    world.ore().set(c->x, c->y, ra2engine::OreKind::Ore, ra2engine::kMaxOreDensity, 0);
    ra2engine::Unit& u =
        world.add_harvester(c->x, c->y, 0, gi_stats(), /*chrono*/ false, /*storage*/ 40);
    u.harv = ra2engine::HarvState::Harvesting;
    u.harv_timer = 0.0;

    for (int i = 0; i < 50; ++i) world.step(1.0);

    CHECK(world.ore().timer() == 0.0);
    CHECK(world.ore().density_at(c->x, c->y) == ra2engine::kMaxOreDensity);   // never gulped
    CHECK(u.bales_ore == 0.0);
    CHECK(u.harv == ra2engine::HarvState::Harvesting);   // FSM frozen, never advanced
    CHECK(world.take_income(0) == doctest::Approx(0.0));
}

// --- Task 5: World factory launch / rally / primary integration ---

TEST_CASE("World: launch_unit spawns at the exit when free and routes to rally") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // Place a 1x1 "factory" building on a walkable cell; its exit is (cx+0, cy+1).
    const int fi = world.add_building(/*type=*/0, c->x, c->y, /*fw=*/1, /*fh=*/1, /*house=*/0,
                                      /*strength=*/100, /*armor_ix=*/0, /*buildup=*/0.0,
                                      /*instant=*/true);
    ra2engine::BuildingTypeDef fdef; fdef.id = "GAWEAP"; fdef.fw = 1; fdef.fh = 1;
    auto free_cell = [&](int x, int y) {
        return world.walkable(x, y) && world.unit_at_cell(x, y) < 0;
    };
    const ra2engine::Cellxy ex = ra2engine::exit_cell(world.buildings()[fi], fdef, free_cell);
    REQUIRE(ex.x >= 0);                               // ring found a free door cell
    REQUIRE(world.walkable(ex.x, ex.y));
    // Set a rally a few cells away, then launch a GI.
    world.set_rally(fi, ex.x + 4, ex.y);
    ra2engine::UnitCombat gi; gi.image = "GI"; gi.strength = 100; gi.armor = "none";
    const int ui = world.launch_unit(fi, fdef, gi);
    REQUIRE(ui >= 0);
    CHECK(world.units()[ui].cell_x == ex.x);
    CHECK(world.units()[ui].cell_y == ex.y);
    CHECK(world.units()[ui].house == 0);
    CHECK(world.units()[ui].moving());               // has a path toward the rally
}

TEST_CASE("World: launch_unit ring-falls-back past an occupied door cell, HOLDs when boxed in") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    const int fi = world.add_building(0, c->x, c->y, 1, 1, 0, 100, 0, 0.0, true);
    ra2engine::BuildingTypeDef fdef; fdef.id = "GAWEAP"; fdef.fw = 1; fdef.fh = 1;
    auto free_cell = [&](int x, int y) {
        return world.walkable(x, y) && world.unit_at_cell(x, y) < 0;
    };
    const ra2engine::Cellxy ex = ra2engine::exit_cell(world.buildings()[fi], fdef, free_cell);
    REQUIRE(ex.x >= 0);
    // Occupy the door cell: the 8-ring walk falls back to the NEXT free cell
    // [RE-GATE APPLIED: production.md §1.3 — first-free-in-ring, not a fixed cell]:
    ra2engine::UnitCombat blocker; blocker.image = "GI"; blocker.strength = 100; blocker.armor = "none";
    world.add_combat_unit(ex.x, ex.y, 0, blocker);
    const int ui = world.launch_unit(fi, fdef, blocker);
    REQUIRE(ui >= 0);                                 // spawned on a ring cell...
    CHECK((world.units()[ui].cell_x != ex.x || world.units()[ui].cell_y != ex.y));  // ...not the door
    // Now box the factory in: occupy EVERY remaining free ring cell.
    while (true) {
        const ra2engine::Cellxy g = ra2engine::exit_cell(world.buildings()[fi], fdef, free_cell);
        if (g.x < 0) break;
        world.add_combat_unit(g.x, g.y, 0, blocker);
    }
    const int before = static_cast<int>(world.units().size());
    CHECK(world.launch_unit(fi, fdef, blocker) == -1);        // fully blocked -> HOLD
    CHECK(static_cast<int>(world.units().size()) == before);  // nothing spawned
}

TEST_CASE("World: launch_unit gathers to the default cell when no rally is set") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    const int fi = world.add_building(0, c->x, c->y, 1, 1, 0, 100, 0, 0.0, true);
    ra2engine::BuildingTypeDef fdef; fdef.id = "GAWEAP"; fdef.fw = 1; fdef.fh = 1;
    ra2engine::UnitCombat gi; gi.image = "GI"; gi.strength = 100; gi.armor = "none";
    const int ui = world.launch_unit(fi, fdef, gi);   // no set_rally called
    REQUIRE(ui >= 0);
    CHECK_FALSE(world.buildings()[fi].has_rally);
    // Default destination = the exit-adjacent gather cell — the nearest free
    // cell from the building's own coordinate [RE-GATE APPLIED: production.md
    // §1.4, FUN_00500020] — NOT the map center. The unit spawns at the first
    // free ring cell (south door for a 1x1) and best-effort moves one ring
    // cell over to clear the door; the key contract is the spawn cell.
    CHECK(world.units()[ui].cell_x == world.buildings()[fi].cx + fdef.fw / 2);
    CHECK(world.units()[ui].cell_y == world.buildings()[fi].cy + fdef.fh);
}

TEST_CASE("World: set_primary routes primary_factory, factory_category_lost detects a wipe") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    const int a = world.add_building(3, c->x, c->y, 1, 1, 0, 100, 0, 0.0, true);
    const int b = world.add_building(3, c->x + 2, c->y, 1, 1, 0, 100, 0, 0.0, true);
    std::vector<int> ftypes = {3};
    CHECK(ra2engine::primary_factory(world.buildings(), ftypes, 0) == a);  // first-built default
    world.set_primary(b, ftypes);
    CHECK(ra2engine::primary_factory(world.buildings(), ftypes, 0) == b);
    CHECK_FALSE(world.factory_category_lost(ftypes, 0));
    ra2engine::kill_building(world.buildings()[a]);
    ra2engine::kill_building(world.buildings()[b]);
    CHECK(world.factory_category_lost(ftypes, 0));    // no alive Idle factory left
}

// Carry-forward from the Task 3 review: the ProductionQueues cancel +
// reconcile_refund path (the sink for THIS task's factory-category-lost
// cancel) had NO test coverage. Drive a real cancel and assert the exact
// credit total lands back in the shared pool via reconcile_refund. This is
// the refund path a caller invokes when factory_category_lost() returns true.
TEST_CASE("World/production: factory-lost cancel refunds spend into the shared pool exactly") {
    std::vector<ra2engine::UnitTypeDef> u(1);
    u[0].id = "MTNK"; u[0].cost = 2000; u[0].tech_level = 2; u[0].is_vehicle = true;

    ra2engine::ProductionQueues q;
    q.set_credits(5000.0);
    // Build a MTNK in the Vehicle queue; tick a while so real credits are spent.
    q.start_unit(ra2engine::QueueCat::Vehicle, 0, u, /*build_speed=*/0.7);
    std::vector<ra2engine::BuildingTypeDef> btypes;  // unused by unit tick
    const double speedups[4] = {1.0, 1.0, 1.0, 1.0};
    for (int i = 0; i < 30; ++i) q.tick(1.0, 1.0, speedups, btypes);
    // Credits were drawn down but the build is not yet ready/placed.
    CHECK(q.credits() < 5000.0);
    CHECK_FALSE(q.ready(ra2engine::QueueCat::Vehicle));

    // factory_category_lost -> cancel that queue + reconcile the shared pool.
    q.cancel(ra2engine::QueueCat::Vehicle);
    q.reconcile_refund();
    // Cancel refunds ALL spend, so the pool returns to exactly the start value.
    CHECK(q.credits() == doctest::Approx(5000.0));
    CHECK(q.producing(ra2engine::QueueCat::Vehicle) == -1);  // slot cleared
}

// --- AI-1 base-planning phase [RE: ai-base.md] ------------------------------

TEST_CASE("World: step_ai_houses firewall - no AI house means a byte-identical step") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    CHECK_FALSE(world.ai_enabled(1));
    CHECK(world.ai_state(1) == nullptr);
    const std::size_t nb = world.buildings().size();
    const std::size_t nu = world.units().size();
    for (int i = 0; i < 60; ++i) world.step(1.0 / 15.0);
    // No AI flagged -> the planner never runs: nothing new appears.
    CHECK(world.buildings().size() == nb);
    CHECK(world.units().size() == nu);
}

// W6-03-04 [RE: FUN_005045b0 / FUN_004fd320 / FUN_004f9990]: the Enemy is
// the living, non-allied house with the highest anger; the AIHateDelays seed
// adds 1 toward the nearest house; MakeAlly clears the anger toward the ally.
TEST_CASE("World: W6-03-04 AI enemy is the arg-max of anger (seed, damage, alliance)") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    for (int h = 0; h < 3; ++h) {
        ra2engine::World::HouseRec r;
        r.side = h == 1 ? ra2engine::World::Side::Soviet : ra2engine::World::Side::Allied;
        r.color_ix = h;
        world.set_house(h, r);
    }
    ra2engine::AiHouseConfig cfg;   // no catalog: the AI plans nothing
    cfg.hate_delay_frames = 30;
    // House 1 (AI) at the start cell; house 0 near, house 2 far.
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    world.add_building(0, c->x + 4, c->y, 1, 1, /*house=*/0, 100, 0, 0.0, true);
    world.add_building(0, c->x + 20, c->y + 20, 1, 1, /*house=*/2, 100, 0, 0.0, true);
    world.enable_ai_house(1, cfg);
    CHECK(world.ai_enemy_house(1) == -1);
    // The hate-delay seed: within the first AI ticks the nearest house gets 1.
    for (int i = 0; i < 240 && world.ai_enemy_house(1) < 0; ++i) world.step(1.0 / 15.0);
    CHECK(world.ai_enemy_house(1) == 0);
    CHECK(world.ai_anger(1, 0) >= 1);
    // Damage anger toward house 2 outweighs the seed -> the Enemy switches.
    world.ai_add_anger(1, 2, 50);
    CHECK(world.ai_enemy_house(1) == 2);
    // Self is never the Enemy, whatever its anger.
    world.ai_add_anger(1, 1, 1000);
    CHECK(world.ai_enemy_house(1) == 2);
    // Allying with house 2 clears that anger (MakeAlly) -> back to house 0.
    world.set_pair_alliance(1, 2, true);
    CHECK(world.ai_anger(1, 2) == 0);
    CHECK(world.ai_enemy_house(1) == 0);
    // Breaking the alliance (MakeEnemy) adds 1 -- below house 0's anger.
    world.set_pair_alliance(1, 2, false);
    CHECK(world.ai_anger(1, 2) == 1);
    CHECK(world.ai_enemy_house(1) == 0);
}

TEST_CASE("World: an AI house builds out through the EXISTING queue+placement machinery") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // Hand roster: [0]=conyard 1x1, [1]=power 1x1 (test values; ids Soviet-shaped).
    ra2engine::AiHouseConfig cfg;
    cfg.roster.resize(2);
    cfg.roster[0].id = "NACNST"; cfg.roster[0].conyard = true;
    cfg.roster[0].cost = 3000; cfg.roster[0].tech_level = 1;
    cfg.roster[0].strength = 1000; cfg.roster[0].adjacent = 2;
    cfg.roster[1].id = "NAPOWR"; cfg.roster[1].cost = 600;
    cfg.roster[1].tech_level = 1; cfg.roster[1].strength = 750;
    cfg.roster[1].power = 150; cfg.roster[1].adjacent = 2;
    cfg.buildup_totals = {0.0, 0.0};
    cfg.catalog.power = 1;  // only the power plant is in the build set ->
                            // build_base_plan yields exactly ONE node
                            // (catalog gaps skipped) [RE: ai-base.md §2]
    ra2engine::DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = ra2engine::kDifficultyCost[i];
        g[i].build_time = ra2engine::kDifficultyBuildTime[i];
        g[i].build_delay = ra2engine::kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = ra2engine::kDifficultyBuildSlowdown[i];
    }
    cfg.difficulty = ra2engine::AiDifficulty::Normal;
    cfg.mods = ra2engine::difficulty_mods(ra2engine::AiDifficulty::Normal, g);
    cfg.build_speed = 0.1;      // fast builds for the test (54 frames for 600c)
    cfg.start_credits = 10000;
    // Seed the ConYard, then flag the house.
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    world.enable_ai_house(1, cfg);
    CHECK(world.ai_enabled(1));
    REQUIRE(world.ai_state(1) != nullptr);
    CHECK(world.ai_credits(1) == doctest::Approx(10000.0));
    // Step: the plan walk reaches its Power node -> the AI starts + places
    // NAPOWR via the EXISTING ProductionQueues + placement_check +
    // add_building path (the ~7 s tick + BuildDelay cadence paces the
    // attempts [RE: ai-base.md §1] — 3000 frames covers dozens of passes).
    const std::size_t nb = world.buildings().size();
    for (int i = 0; i < 3000 && world.buildings().size() < nb + 1; ++i)
        world.step(1.0 / 15.0);
    REQUIRE(world.buildings().size() >= nb + 1);
    const ra2engine::Building& nb1 = world.buildings()[nb];
    CHECK(nb1.type == 1);
    CHECK(nb1.house == 1);
    // Placement is nearest-first from the ConYard. NOTE the AI BYPASSES the
    // Adjacent= proximity check [RE-GATE APPLIED: ai-base.md §3]; locality
    // comes from the Chebyshev ring scan + the AIBaseSpacing(1) gap.
    CHECK(std::max(std::abs(nb1.cx - c->x), std::abs(nb1.cy - c->y)) <= 4);
    // The planned-base list recorded the node [RE: ai-base.md §3].
    REQUIRE(world.ai_state(1)->plan.size() >= 1);
    CHECK(world.ai_state(1)->plan[0].type == 1);
    CHECK(world.ai_state(1)->plan[0].building == (int)nb);
    // Credits were charged through the queue economy (600 x cost_mul).
    CHECK(world.ai_credits(1) <
          10000.0 - 0.5 * 600.0 * ra2engine::kDifficultyCost[1]);
    // Plan satisfied + no refinery/war factory in this catalog -> economy
    // expansion has nothing to add [RE: ai-base.md §2 — expansion appends
    // refineries/war factories only]: no further growth.
    const std::size_t done = world.buildings().size();
    for (int i = 0; i < 600; ++i) world.step(1.0 / 15.0);
    CHECK(world.buildings().size() == done);
}

TEST_CASE("World: AI rebuild re-queues the destroyed planned node at its cell") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    ra2engine::AiHouseConfig cfg;  // same tiny 2-type cfg as the case above
    cfg.roster.resize(2);
    cfg.roster[0].id = "NACNST"; cfg.roster[0].conyard = true;
    cfg.roster[0].cost = 3000; cfg.roster[0].tech_level = 1;
    cfg.roster[0].strength = 1000; cfg.roster[0].adjacent = 2;
    cfg.roster[1].id = "NAPOWR"; cfg.roster[1].cost = 600;
    cfg.roster[1].tech_level = 1; cfg.roster[1].strength = 750;
    cfg.roster[1].power = 150; cfg.roster[1].adjacent = 2;
    cfg.buildup_totals = {0.0, 0.0};
    cfg.catalog.power = 1;  // one-entry catalog -> the plan has exactly ONE
                            // node, so the rebuild target is unambiguous
    ra2engine::DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = ra2engine::kDifficultyCost[i];
        g[i].build_time = ra2engine::kDifficultyBuildTime[i];
        g[i].build_delay = ra2engine::kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = ra2engine::kDifficultyBuildSlowdown[i];
    }
    cfg.difficulty = ra2engine::AiDifficulty::Normal;
    cfg.mods = ra2engine::difficulty_mods(ra2engine::AiDifficulty::Normal, g);
    cfg.build_speed = 0.1;
    cfg.start_credits = 10000;
    world.add_building(0, c->x, c->y, 1, 1, 1, 1000, 0, 0.0, true);
    world.enable_ai_house(1, cfg);
    const std::size_t nb = world.buildings().size();
    for (int i = 0; i < 3000 && world.buildings().size() < nb + 1; ++i)
        world.step(1.0 / 15.0);
    REQUIRE(world.buildings().size() == nb + 1);
    const int node_cx = world.ai_state(1)->plan[0].cx;
    const int node_cy = world.ai_state(1)->plan[0].cy;
    // Kill the placed power plant; the planner re-queues THAT node.
    ra2engine::kill_building(world.buildings()[nb]);
    for (int i = 0; i < 4000 && !(world.ai_state(1)->plan[0].building > (int)nb); ++i)
        world.step(1.0 / 15.0);
    REQUIRE(world.ai_state(1)->plan.size() == 1);       // NOT a new expansion node
    const ra2engine::PlannedNode& n = world.ai_state(1)->plan[0];
    CHECK(n.type == 1);
    CHECK_FALSE(n.dead);
    REQUIRE(n.building > (int)nb);                      // a NEW building index
    CHECK(world.buildings()[n.building].type == 1);
    CHECK(world.buildings()[n.building].alive);
    // Rebuilt at the exact stored cell [RE-GATE APPLIED: ai-base.md §5]
    // (the dead building's rubble releases its cells, so the cell is free):
    CHECK(world.buildings()[n.building].cx == node_cx);
    CHECK(world.buildings()[n.building].cy == node_cy);
}

TEST_CASE("World: AI can_place rejects a partially-off-grid or partially-occupied foundation") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // Contract #1: the AI's placement validity (placement_check with
    // bypass_adjacency=true, over the FULL foundation) must reject a location
    // unless the ENTIRE foundation is on-grid, buildable, and unoccupied
    // [RE-GATE APPLIED: ai-base.md §3]. A 2x2 foundation is used so a single
    // off-grid or occupied cell makes the whole placement fail.
    const int fw = 2, fh = 2;
    // A fully-on-grid, empty interior cell passes (baseline sanity).
    CHECK(ra2engine::placement_check(c->x, c->y, fw, fh, /*house=*/1, /*adjacent=*/2,
                                     world.passable(), world.grid_w(), world.grid_h(),
                                     world.occupancy(), world.buildings(),
                                     /*bypass_adjacency=*/true).ok);
    // Partial-off-grid: origin at the far bottom-right corner so the +1 cells
    // of the 2x2 foundation fall off the grid -> at least one cell off-grid ->
    // the whole placement fails (NOT ok).
    CHECK_FALSE(ra2engine::placement_check(world.grid_w() - 1, world.grid_h() - 1, fw, fh,
                                           /*house=*/1, /*adjacent=*/2, world.passable(),
                                           world.grid_w(), world.grid_h(), world.occupancy(),
                                           world.buildings(), /*bypass_adjacency=*/true).ok);
    // Partial-occupied: drop a 1x1 building on ONE cell of the 2x2 footprint;
    // the overlap must make the full-foundation placement fail.
    world.add_building(0, c->x + 1, c->y + 1, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    CHECK_FALSE(ra2engine::placement_check(c->x, c->y, fw, fh, /*house=*/1, /*adjacent=*/2,
                                           world.passable(), world.grid_w(), world.grid_h(),
                                           world.occupancy(), world.buildings(),
                                           /*bypass_adjacency=*/true).ok);

    // AIBaseSpacing(1) gap through the bound closure [RE-GATE APPLIED:
    // ai-base.md §3]: the AI's can_place probes the foundation EXPANDED by
    // +2*spacing (origin - s, size + 2*s), so a building placed one cell away
    // from a candidate must BLOCK that candidate even though the candidate's
    // RAW foundation is itself free. This exercises the spacing gap, not just
    // the raw footprint (review Minor #1). Uses a fresh 1x1 candidate clear of
    // the buildings placed above.
    const int s = ra2engine::kAiBaseSpacing;  // == 1
    const int gx = c->x + 3, gy = c->y;       // candidate origin (1x1)
    auto raw_ok = [&](int x, int y) {
        return ra2engine::placement_check(x, y, 1, 1, /*house=*/1, /*adjacent=*/2,
                                          world.passable(), world.grid_w(), world.grid_h(),
                                          world.occupancy(), world.buildings(),
                                          /*bypass_adjacency=*/true).ok;
    };
    auto spaced_ok = [&](int x, int y) {  // exactly what ai_place_ready binds
        return ra2engine::placement_check(x - s, y - s, 1 + 2 * s, 1 + 2 * s,
                                          /*house=*/1, /*adjacent=*/2,
                                          world.passable(), world.grid_w(), world.grid_h(),
                                          world.occupancy(), world.buildings(),
                                          /*bypass_adjacency=*/true).ok;
    };
    // Baseline: candidate is buildable and its spacing halo is clear.
    REQUIRE(raw_ok(gx, gy));
    REQUIRE(spaced_ok(gx, gy));
    // Drop a 1x1 building exactly one cell east of the candidate — OUTSIDE the
    // raw 1x1 footprint but INSIDE the +2*spacing expanded foundation.
    world.add_building(0, gx + 1, gy, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    CHECK(raw_ok(gx, gy));          // raw footprint still free -> raw check accepts
    CHECK_FALSE(spaced_ok(gx, gy)); // but the 1-cell gap now blocks the candidate
}

TEST_CASE("World: a power-preemption insert never corrupts a pending queue's node "
          "(stable node identity)") {
    // REGRESSION [fix wave 1]: step_ai_houses stored, per queue, the RAW plan
    // index of the node in production (rec.state.pending[q]). next_build can
    // INSERT a power-preemption node at the first-unsatisfied index; an insert
    // at an index <= a stored pending index shifts the real node by one, but
    // the stored raw index was NOT updated -> when that queue completed,
    // ai_place_ready placed the WRONG (shifted) node: wrong type, wrong cell.
    // The fix gives every PlannedNode a stable id and stores/resolves pending
    // by id. This test reproduces the shift: a defense is mid-production
    // (pending[Defense] -> the defense node) when an earlier radar node dies
    // with power headroom tight, so next_build inserts a Power node AHEAD of
    // it. The in-production defense must still land at ITS OWN node (defense
    // type, its own stored cell), NOT the shifted radar node.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);

    // Roster: [0]conyard [1]power(+50) [2]radar(-40 drainer) [3]defense
    // [4]power2(+100, NOT in the catalog -> the AI never manages it; it only
    // supplies output so radar's ORIGINAL build clears the preemption gate,
    // then we kill it to make the radar REBUILD tight).
    ra2engine::AiHouseConfig cfg;
    cfg.roster.resize(5);
    auto& R = cfg.roster;
    R[0].id = "NACNST"; R[0].conyard = true; R[0].cost = 3000; R[0].tech_level = 1;
    R[0].strength = 1000; R[0].adjacent = 2;
    R[1].id = "NAPOWR"; R[1].cost = 600; R[1].tech_level = 1; R[1].strength = 750;
    R[1].power = 50; R[1].adjacent = 2;
    R[2].id = "NARADR"; R[2].cost = 1000; R[2].tech_level = 1; R[2].strength = 750;
    R[2].power = -40; R[2].adjacent = 2;   // drainer -> a preemption target when dead
    R[3].id = "NALASR"; R[3].cost = 5000; R[3].tech_level = 1; R[3].strength = 1000;
    R[3].power = 0; R[3].adjacent = 2;     // slow (5000c) so it stays in production
    R[4].id = "NAPOW2"; R[4].cost = 600; R[4].tech_level = 1; R[4].strength = 750;
    R[4].power = 100; R[4].adjacent = 2;   // extra output (not a catalog entry)
    cfg.buildup_totals = {0.0, 0.0, 0.0, 0.0, 0.0};
    cfg.catalog.conyard = 0; cfg.catalog.power = 1; cfg.catalog.radar = 2;
    cfg.catalog.defenses = {3};            // one defense type
    cfg.ai.defense_limit = 1;              // -> exactly ONE defense node in the plan
    ra2engine::DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = ra2engine::kDifficultyCost[i];
        g[i].build_time = ra2engine::kDifficultyBuildTime[i];
        g[i].build_delay = ra2engine::kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = ra2engine::kDifficultyBuildSlowdown[i];
    }
    cfg.difficulty = ra2engine::AiDifficulty::Normal;
    cfg.mods = ra2engine::difficulty_mods(ra2engine::AiDifficulty::Normal, g);
    cfg.build_speed = 0.1;
    cfg.start_credits = 100000;

    // Seed the ConYard and the extra (un-managed) power source, then flag AI.
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    const int power2_idx =
        world.add_building(4, c->x + 3, c->y, 1, 1, /*house=*/1, 750, 0, 0.0, true);
    REQUIRE(power2_idx >= 0);
    world.enable_ai_house(1, cfg);

    const int kRadar = 2, kDefense = 3;
    auto count_type = [&](int type) {
        int n = 0;
        for (const auto& b : world.buildings())
            if (b.alive && b.state != ra2engine::Building::State::Dead &&
                b.house == 1 && b.type == type) ++n;
        return n;
    };
    auto find_alive = [&](int type) {
        for (int i = 0; i < (int)world.buildings().size(); ++i) {
            const auto& b = world.buildings()[i];
            if (b.alive && b.state != ra2engine::Building::State::Dead &&
                b.house == 1 && b.type == type) return i;
        }
        return -1;
    };

    // Phase 1: let the AI build its power node, then the radar (output 150 from
    // power2+power keeps radar's build under the preemption gate).
    int steps = 0;
    for (; steps < 8000 && count_type(kRadar) < 1; ++steps) world.step(1.0 / 15.0);
    REQUIRE(count_type(kRadar) == 1);

    // Phase 2: step until the AI starts the (slow) defense in the Defense queue.
    for (; steps < 12000 && world.ai_producing(1, 1) != kDefense; ++steps)
        world.step(1.0 / 15.0);
    REQUIRE(world.ai_producing(1, 1) == kDefense);

    // Record the defense node's OWN stored cell (chosen at start, before any
    // insert) — the place the defense MUST land.
    const ra2engine::AiBaseState* st = world.ai_state(1);
    REQUIRE(st != nullptr);
    int dcx = -1, dcy = -1;
    for (const ra2engine::PlannedNode& n : st->plan)
        if (n.type == kDefense) { dcx = n.cx; dcy = n.cy; break; }
    REQUIRE(dcx != 0);  // a real cell was chosen (not the unset (0,0) sentinel)

    // Kill the extra power source (drops output 150 -> 50, plan power node still
    // satisfied) AND the radar (its node goes dead). Now rebuilding radar would
    // push drain past output - PowerSurplus(50), so the NEXT next_build pass
    // INSERTS a Power node ahead of the radar node — shifting the pending
    // defense node by one.
    ra2engine::kill_building(world.buildings()[power2_idx]);
    const int radar_idx = find_alive(kRadar);
    REQUIRE(radar_idx >= 0);
    ra2engine::kill_building(world.buildings()[radar_idx]);

    // Phase 3: run long enough for the insert to fire AND the defense to finish.
    for (int i = 0; i < 3000 && world.ai_producing(1, 1) == kDefense; ++i)
        world.step(1.0 / 15.0);
    REQUIRE(world.ai_producing(1, 1) != kDefense);   // defense completed + placed

    // The defense must have been placed at ITS OWN node: a building of the
    // DEFENSE type, at the defense node's stored cell. Before the fix the
    // Defense queue placed the shifted RADAR node instead (wrong type/cell),
    // so no defense-type building exists here.
    const int def_idx = find_alive(kDefense);
    REQUIRE(def_idx >= 0);                            // FAILS before the fix
    CHECK(world.buildings()[def_idx].cx == dcx);
    CHECK(world.buildings()[def_idx].cy == dcy);
}

TEST_CASE("World: a PRE-BUILT building is synced into its plan node and rebuilt "
          "IN PLACE when destroyed") {
    // REGRESSION [fix wave — final review IMPORTANT-1]: ai-base.md §3 —
    // FUN_0050a3e0 builds the base plan AND walks the house's already-placed
    // buildings, recording each live building's cell + World index into its
    // matching plan node. Our engine skipped that sync, so a node covering a
    // PRE-BUILT building (spawn_base, the seeded demo ConYard) stayed
    // cx=cy=0/building=-1. When such a pre-built building was destroyed,
    // node-death detection (n.building==-1) never fired, the node looked FRESH
    // (cx==0&&cy==0) and the AI rebuilt at a NEW nearest-ring cell instead of
    // IN PLACE at the retained cell — breaking the §3/§5 rebuild-in-place
    // contract on the exact Goal scenario (the pre-built enemy base rebuilding
    // what it loses). This test FAILS before the sync (node never linked;
    // rebuild lands at the wrong cell) and PASSES after.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // [0]=conyard 1x1, [1]=power 1x1; BOTH in the catalog so the plan has a
    // node for each (so the pre-built NAPOWR has a node to be synced into).
    ra2engine::AiHouseConfig cfg;
    cfg.roster.resize(2);
    cfg.roster[0].id = "NACNST"; cfg.roster[0].conyard = true;
    cfg.roster[0].cost = 3000; cfg.roster[0].tech_level = 1;
    cfg.roster[0].strength = 1000; cfg.roster[0].adjacent = 2;
    cfg.roster[1].id = "NAPOWR"; cfg.roster[1].cost = 600;
    cfg.roster[1].tech_level = 1; cfg.roster[1].strength = 750;
    cfg.roster[1].power = 150; cfg.roster[1].adjacent = 2;
    cfg.buildup_totals = {0.0, 0.0};
    cfg.catalog.conyard = 0;
    cfg.catalog.power = 1;
    ra2engine::DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = ra2engine::kDifficultyCost[i];
        g[i].build_time = ra2engine::kDifficultyBuildTime[i];
        g[i].build_delay = ra2engine::kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = ra2engine::kDifficultyBuildSlowdown[i];
    }
    cfg.difficulty = ra2engine::AiDifficulty::Normal;
    cfg.mods = ra2engine::difficulty_mods(ra2engine::AiDifficulty::Normal, g);
    cfg.build_speed = 0.1;
    cfg.start_credits = 10000;

    // Pre-build BOTH the ConYard and a NAPOWR at KNOWN cells (like spawn_base),
    // BEFORE flagging the house. The NAPOWR sits far (+6,+6) from the ConYard
    // so a FRESH nearest-ring rebuild (the pre-fix bug) would land somewhere
    // else entirely — making the stored-cell check discriminating.
    const int cy_idx = world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    const int napowr_cx = c->x + 6, napowr_cy = c->y + 6;
    const int pw_idx =
        world.add_building(1, napowr_cx, napowr_cy, 1, 1, /*house=*/1, 750, 0, 0.0, true);
    REQUIRE(cy_idx == 0);
    REQUIRE(pw_idx == 1);
    world.enable_ai_house(1, cfg);

    // One tick: the plan builds + the pre-built buildings sync into their nodes.
    world.step(1.0 / 15.0);
    const ra2engine::AiBaseState* st = world.ai_state(1);
    REQUIRE(st != nullptr);
    REQUIRE(st->plan.size() == 2);        // conyard node + power node
    const ra2engine::PlannedNode& pnode = st->plan[1];
    REQUIRE(pnode.type == 1);             // the power node
    // The sync recorded the pre-built NAPOWR's cell + World index into its node.
    CHECK(pnode.building == pw_idx);      // FAILS before the fix (stays -1)
    CHECK(pnode.cx == napowr_cx);         // FAILS before the fix (stays 0)
    CHECK(pnode.cy == napowr_cy);         // FAILS before the fix (stays 0)

    // Destroy the pre-built NAPOWR. Because its node is now LINKED, node-death
    // detection flips the node dead and the AI rebuilds it at the RETAINED cell.
    ra2engine::kill_building(world.buildings()[pw_idx]);
    for (int i = 0; i < 5000 && !(world.ai_state(1)->plan[1].building > pw_idx); ++i)
        world.step(1.0 / 15.0);
    const ra2engine::PlannedNode& rebuilt = world.ai_state(1)->plan[1];
    REQUIRE(rebuilt.building > pw_idx);   // a NEW building index (rebuilt)
    CHECK_FALSE(rebuilt.dead);
    CHECK(world.buildings()[rebuilt.building].type == 1);
    CHECK(world.buildings()[rebuilt.building].alive);
    // Rebuilt IN PLACE at the pre-built cell — NOT a fresh nearest-ring cell
    // (the pre-fix bug placed it near the ConYard center instead).
    CHECK(world.buildings()[rebuilt.building].cx == napowr_cx);
    CHECK(world.buildings()[rebuilt.building].cy == napowr_cy);
}

// ---- AI-2 offensive-teams phase [RE: ai-team.md] : step_ai_teams integration ----

namespace {
// Tiny AI-2 world config: buildings [0]=NACNST conyard, [1]=NAHAND barracks;
// unit roster [0] = one infantry type (image "AITEST"). aimd: ONE autocreate
// TeamType needing 1 x type0, script = attack-anything then success.
ra2engine::AiHouseConfig ai2_cfg(bool teams_enabled, bool unreachable_move = false) {
    ra2engine::AiHouseConfig cfg;
    cfg.roster.resize(2);
    cfg.roster[0].id = "NACNST"; cfg.roster[0].conyard = true;
    cfg.roster[0].cost = 3000; cfg.roster[0].tech_level = 1;
    cfg.roster[0].strength = 1000; cfg.roster[0].adjacent = 2;
    cfg.roster[1].id = "NAHAND"; cfg.roster[1].cost = 500;
    cfg.roster[1].tech_level = 1; cfg.roster[1].strength = 500;
    cfg.roster[1].adjacent = 2;
    cfg.buildup_totals = {0.0, 0.0};
    // Empty AiCatalog -> the AI-1 base brain plans nothing (all -1 skipped);
    // this test exercises ONLY the team brain.
    ra2engine::DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = ra2engine::kDifficultyCost[i];
        g[i].build_time = ra2engine::kDifficultyBuildTime[i];
        g[i].build_delay = ra2engine::kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = ra2engine::kDifficultyBuildSlowdown[i];
    }
    cfg.mods = ra2engine::difficulty_mods(ra2engine::AiDifficulty::Normal, g);
    cfg.build_speed = 0.05;    // 200c infantry -> 9 frames -> fast test builds
    cfg.start_credits = 10000;
    if (!teams_enabled) return cfg;
    cfg.team.enabled = true;
    cfg.team.house_name = "Russians";
    cfg.team.inf_factory_types = {1};   // NAHAND
    cfg.team.veh_factory_types = {};
    // Fast test clocks (the REAL cadences are the Task 3 constants
    // [RE: ai-team.md §4]): TeamDelays check pass every 1 s, AutocreateTime spawn
    // every ~1 s, so an autocreate team lands quickly in the short test window.
    cfg.team.rules.team_delay_frames = 15;         // 1 s management check pass
    cfg.team.rules.autocreate_time_min = 1.0 / 60.0;  // ~1 s spawn cadence
    ra2engine::UnitTypeDef u;
    u.id = "E2TEST";
    u.cost = 200;
    u.tech_level = 1;
    u.is_vehicle = false;
    u.prerequisite = {"NAHAND"};
    u.combat.strength = 100;
    u.combat.image = "AITEST";
    cfg.team.unit_roster.push_back(u);
    ra2engine::TaskForce tf;
    tf.id = "TF1";
    tf.members = {{1, 0}};              // 1 x type0
    cfg.team.aimd.taskforces.push_back(tf);
    ra2engine::ScriptType sc;
    sc.id = "SC1";
    if (unreachable_move) {
        // Move to a mapped waypoint that is OFF-MAP: every snap candidate is out
        // of bounds so the snap fails, order_unit_move cancels, and the member
        // can never satisfy the arrival predicate. The generalized backstop must
        // re-issue then advance the pc so the team disbands (never wedges) — the
        // FIX-1 wedge-freedom guarantee exercised end-to-end through step_ai_teams.
        cfg.team.waypoints = {ra2engine::Cellxy{-100, -100}};
        sc.actions = {{ra2engine::kOpMoveToWaypoint, 0},
                      {ra2engine::kOpDisband, 0}};
    } else {
        sc.actions = {{ra2engine::kOpAttackQuarry, ra2engine::kQuarryAnything},
                      {ra2engine::kOpDisband, 0}};
    }
    cfg.team.aimd.scripts.push_back(sc);
    ra2engine::TeamType tt;
    tt.id = "TT1";
    tt.house = "Russians";
    tt.taskforce = 0;
    tt.script = 0;
    tt.priority = 7;
    tt.max = 1;
    tt.autocreate = true;
    cfg.team.aimd.teams.push_back(tt);
    return cfg;
}
}  // namespace

TEST_CASE("World: step_ai_teams firewall - team.enabled=false means no teams ever") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    world.enable_ai_house(1, ai2_cfg(/*teams_enabled=*/false));
    const std::size_t nu = world.units().size();
    for (int i = 0; i < 300; ++i) world.step(1.0 / 15.0);
    REQUIRE(world.ai_team_state(1) != nullptr);
    CHECK(world.ai_team_state(1)->teams.empty());        // firewall held
    CHECK(world.ai_team_state(1)->teams_created == 0);
    CHECK(world.units().size() == nu);                   // nothing produced
    CHECK(world.ai_unit_producing(1, 0) == -1);
}

TEST_CASE("World: a team recruits an IDLE unit first and attacks the player") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    // An IDLE house-1 combat unit of the roster type (image match) + a
    // house-0 target unit placed WELL OUTSIDE auto-acquire reach (20 cells,
    // weapon range 4) so the AI unit stays genuinely idle/recruitable until
    // the TEAM issues the attack order — the recruit-idle premise under test.
    ra2engine::UnitCombat uc;
    uc.strength = 100;
    uc.image = "AITEST";
    uc.damage = 10; uc.rof = 15; uc.range = 4.0;
    uc.verses = std::vector<int>(11, 100);
    world.add_combat_unit(c->x + 3, c->y + 2, /*house=*/1, uc);
    ra2engine::UnitCombat pc0 = uc;
    pc0.image = "PLTEST";
    world.add_combat_unit(c->x + 20, c->y + 2, /*house=*/0, pc0);
    world.enable_ai_house(1, ai2_cfg(/*teams_enabled=*/true));
    const std::size_t nu = world.units().size();
    // Step: autocreate fires (1 s test clock), recruit-idle claims the
    // standing unit (NO production needed), the team gathers then executes
    // attack-anything at the house-0 unit.
    bool attacked = false;
    for (int i = 0; i < 1200 && !attacked; ++i) {
        world.step(1.0 / 15.0);
        const ra2engine::Unit& u = world.units()[0];   // the AI unit
        attacked = u.attack_order && (u.target >= 0 || u.target_building >= 0);
    }
    REQUIRE(world.ai_team_state(1) != nullptr);
    CHECK(world.ai_team_state(1)->teams_created == 1);   // Max=1 respected
    CHECK(world.units().size() == nu);                   // recruit-idle, NOT produce
    CHECK(world.ai_unit_producing(1, 0) == -1);          // Infantry queue never ran
    REQUIRE(attacked);
    CHECK(world.units()[0].target == 1);                 // the house-0 unit
}

TEST_CASE("World: a team produces its shortfall through the EXISTING Infantry queue") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    world.add_building(1, c->x + 3, c->y, 1, 1, /*house=*/1, 500, 0, 0.0, true);  // NAHAND
    world.enable_ai_house(1, ai2_cfg(/*teams_enabled=*/true));
    const std::size_t nu = world.units().size();
    const double credits0 = world.ai_credits(1);
    // No idle unit exists -> next_produce_type drives the Infantry queue
    // (tab2) -> launch_unit spawns at the barracks exit -> the unit is
    // claimed into the recruiting team [RE: ai-team.md §2/§6].
    for (int i = 0; i < 1200 && world.units().size() < nu + 1; ++i)
        world.step(1.0 / 15.0);
    REQUIRE(world.units().size() == nu + 1);
    const ra2engine::Unit& made = world.units()[nu];
    CHECK(made.house == 1);
    CHECK(made.image == "AITEST");
    CHECK(world.ai_credits(1) < credits0);               // charged through the queue
    REQUIRE(world.ai_team_state(1) != nullptr);
    REQUIRE(world.ai_team_state(1)->teams.size() >= 1);
    const ra2engine::TeamInstance& t = world.ai_team_state(1)->teams[0];
    REQUIRE(t.members.size() == 1);
    CHECK(t.members[0] == (int)nu);                      // claimed on launch
    CHECK(t.member_types[0] == 0);
    // The single-slot queue held ONE unit at a time (Max=1 team of 1):
    for (int i = 0; i < 300; ++i) world.step(1.0 / 15.0);
    CHECK(world.units().size() == nu + 1);               // no runaway production
}

TEST_CASE("World: an Executing move whose ideal cell is unreachable does NOT wedge") {
    // [FIX: Executing-move wedge] Before the fix, a move opcode whose target's
    // CloseEnough neighborhood is blocked/unreachable ordered members onto an
    // unreachable cell; order_unit_move cancelled, arrival vs the ideal was
    // false forever, move_issued stayed true, and the team wedged in Executing
    // PERMANENTLY holding its Max/TotalAITeamCap slot — the AI eventually stops
    // attacking. With the generalized backstop the team re-issues then advances
    // past the dead move, disbands, and FREES its cap slot so a fresh team spawns.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);  // AI conyard
    // An idle recruitable AI unit (fills the team with no production wait) and a
    // far house-0 unit so the enemy-threat centroid (gather-ring centre) is
    // well-defined — the gather succeeds and the team reaches Executing, exactly
    // as in the recruit-idle test above.
    ra2engine::UnitCombat uc;
    uc.strength = 100; uc.image = "AITEST";
    uc.damage = 10; uc.rof = 15; uc.range = 4.0;
    uc.verses = std::vector<int>(11, 100);
    world.add_combat_unit(c->x + 3, c->y + 2, /*house=*/1, uc);
    ra2engine::UnitCombat pc0 = uc; pc0.image = "PLTEST";
    world.add_combat_unit(c->x + 20, c->y + 2, /*house=*/0, pc0);
    world.enable_ai_house(1, ai2_cfg(/*teams_enabled=*/true, /*unreachable_move=*/true));

    // Observe the lifecycle: the team must actually REACH Executing (non-vacuous
    // — the move opcode is genuinely exercised) AND the cap slot must free (a
    // second team is created once the first disbands past the dead move). Under
    // the bug the first team wedges in Executing forever -> teams_created pegs at 1.
    bool saw_executing = false;
    int max_created = 0;
    for (int i = 0; i < 4000; ++i) {
        world.step(1.0 / 15.0);
        const ra2engine::AiTeamState* st = world.ai_team_state(1);
        REQUIRE(st != nullptr);
        max_created = std::max(max_created, st->teams_created);
        for (const ra2engine::TeamInstance& t : st->teams)
            if (t.state == ra2engine::TeamState::Executing) saw_executing = true;
        if (saw_executing && max_created >= 2) break;
    }
    CHECK(saw_executing);           // the Executing move opcode was reached
    CHECK(max_created >= 2);        // team disbanded past the dead move + cap freed
    // Max=1 held throughout: teams recycle (create -> wedge-free disband ->
    // create) rather than accumulating a leaked wedged instance.
    CHECK((int)world.ai_team_state(1)->teams.size() <= 1);
}

// ---- AI-3 AITriggerTypes phase [RE: ai-trigger.md] : trigger-eval step ----

namespace {
// AI-3 trigger cfg: ai2_cfg(true) with the autocreate path SILENCED
// (autocreate=false on every fixture TeamType — a TEST-ISOLATION device so
// only the trigger path can create; ALL 163 stock TeamTypes are
// Autocreate=yes and the trigger pool == the autocreate pool [RE-GATE
// APPLIED: ai-trigger.md §6]) + ONE TeamType TT2 reachable ONLY through the
// trigger path + ONE trigger keyed "enemy owns >= 1 of building roster type
// 0 (NACNST)" with Team2 = <none>. Script overridden to bare disband so the
// outcome lands fast (kOpDisband sets completed_success -> RegisterSuccess
// [RE: ai-trigger.md §4]). Fast test clocks carry over from ai2_cfg.
ra2engine::AiHouseConfig ai3_cfg(bool triggers_enabled, int total_team_cap = 30) {
    ra2engine::AiHouseConfig cfg = ai2_cfg(/*teams_enabled=*/true);
    cfg.team.aimd.teams[0].autocreate = false;   // silence AI-2 autocreate
    cfg.team.aimd.scripts[0].actions =
        {ra2engine::ScriptAction{ra2engine::kOpDisband, 0}};
    ra2engine::TeamType tt;
    tt.id = "TT2";
    tt.house = "Russians";
    tt.taskforce = 0;     // reuse TF1 (1 x type0)
    tt.script = 0;        // reuse SC1 (now bare disband)
    tt.priority = 5;
    tt.max = 1;
    tt.autocreate = false;                        // NON-autocreate: trigger-only
    cfg.team.aimd.teams.push_back(tt);
    ra2engine::AITriggerType tr;
    tr.id = "TRG1";
    tr.name = "player conyard seen";
    tr.team1 = 1;                                 // TT2
    tr.team2 = -1;
    tr.owner_house = "<all>";
    tr.tech_level = 0;
    tr.condition_type = ra2engine::kCondEnemyOwns;
    tr.condition_object =
        ra2engine::TriggerObject{ra2engine::TechnoKind::Building, 0};
    tr.comparator = ra2engine::TriggerComparator{1, ra2engine::kCompGreaterEq};
    tr.starting_weight = 50.0; tr.min_weight = 10.0; tr.max_weight = 90.0;
    tr.is_for_skirmish = true;
    tr.side = 0;
    tr.base_defense = false;
    tr.enabled[0] = tr.enabled[1] = tr.enabled[2] = true;
    tr.is_enabled = true;    // [AITriggerTypesEnable] default [RE: §5]
    cfg.team.aimd.triggers.push_back(tr);
    cfg.team.triggers_enabled = triggers_enabled;
    cfg.team.rules.total_team_cap = total_team_cap;
    return cfg;
}

// AI-3 non-Normal difficulty fixture [RE-GATE STOP: the 2-difficulty edge].
// Drives the house at AiDifficulty::Difficult (engine enum 2, 0 = EASIEST) with
// TWO triggers that differ ONLY in their per-difficulty enable slot, sharing
// one always-satisfiable enemy-owns condition and equal weight so the enable
// slot alone decides eligibility:
//   TRG_HARD (index 0) -> team TT2, enabled = {easy=0, normal=0, hard=1}
//   TRG_EASY (index 1) -> team TT3, enabled = {easy=1, normal=0, hard=0}
// The World converts the enum to the pure engine's 0 = HARDEST convention, so a
// Difficult AI consults the HARD flag: TRG_HARD is eligible/fires, TRG_EASY is
// NOT. Under the pre-fix non-inverted fill the two SWAP (Easy fires, Hard is
// excluded), so this pins the world<->engine difficulty-convention seam.
ra2engine::AiHouseConfig ai3_cfg_difficult() {
    ra2engine::AiHouseConfig cfg = ai3_cfg(/*triggers_enabled=*/true);
    cfg.difficulty = ra2engine::AiDifficulty::Difficult;   // enum 2 (0 = EASIEST)
    ra2engine::DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = ra2engine::kDifficultyCost[i];
        g[i].build_time = ra2engine::kDifficultyBuildTime[i];
        g[i].build_delay = ra2engine::kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = ra2engine::kDifficultyBuildSlowdown[i];
    }
    cfg.mods = ra2engine::difficulty_mods(ra2engine::AiDifficulty::Difficult, g);
    // Second trigger-only team TT3 (teams index 2), reachable ONLY via TRG_EASY.
    ra2engine::TeamType tt3;
    tt3.id = "TT3"; tt3.house = "Russians";
    tt3.taskforce = 0; tt3.script = 0; tt3.priority = 5; tt3.max = 1;
    tt3.autocreate = false;
    cfg.team.aimd.teams.push_back(tt3);   // index 2
    // Rebuild the trigger pool as exactly the two enable-slot variants off the
    // ai3_cfg TRG1 template (same condition/comparator/weights/side/tech).
    const ra2engine::AITriggerType base = cfg.team.aimd.triggers[0];
    cfg.team.aimd.triggers.clear();
    ra2engine::AITriggerType hard = base;
    hard.id = "TRG_HARD"; hard.team1 = 1;   // TT2
    hard.enabled[0] = false; hard.enabled[1] = false; hard.enabled[2] = true;
    ra2engine::AITriggerType easy = base;
    easy.id = "TRG_EASY"; easy.team1 = 2;   // TT3
    easy.enabled[0] = true; easy.enabled[1] = false; easy.enabled[2] = false;
    cfg.team.aimd.triggers.push_back(hard);   // index 0
    cfg.team.aimd.triggers.push_back(easy);   // index 1
    return cfg;
}
}  // namespace

TEST_CASE("World: a trigger fires, creates its team via the trigger path, and the disband outcome adjusts its weight") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // AI base (house 1) + the PLAYER conyard (house 0) that makes the
    // enemy-owns condition TRUE, well outside auto-acquire reach.
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    // [G-60] the TaskForce must be buildable (FUN_0041ff00): stand a NAHAND.
    world.add_building(1, c->x - 3, c->y, 1, 1, /*house=*/1, 500, 0, 0.0, true);
    world.add_building(0, c->x + 20, c->y + 2, 1, 1, /*house=*/0, 1000, 0, 0.0, true);
    // One IDLE house-1 combat unit so recruit-idle fills TT2 instantly.
    ra2engine::UnitCombat uc;
    uc.strength = 100;
    uc.image = "AITEST";
    uc.damage = 10; uc.rof = 15; uc.range = 4.0;
    uc.verses = std::vector<int>(11, 100);
    world.add_combat_unit(c->x + 3, c->y + 2, /*house=*/1, uc);
    world.enable_ai_house(1, ai3_cfg(/*triggers_enabled=*/true));
    REQUIRE(world.ai_trigger_state(1) != nullptr);
    REQUIRE(world.ai_trigger_state(1)->weights.size() == 1);
    CHECK(world.ai_trigger_state(1)->weights[0] == 50.0);   // starting_weight
    // Step until the created team disbands and reports its outcome: fire on
    // the first 1 s check pass -> recruit the idle unit -> gather -> execute
    // bare disband -> Disbanding -> weight adjusted.
    for (int i = 0; i < 4500 && world.ai_trigger_state(1)->adjustments < 1; ++i)
        world.step(1.0 / 15.0);
    const ra2engine::AiTriggerState* ts = world.ai_trigger_state(1);
    REQUIRE(ts != nullptr);
    CHECK(ts->triggers_fired == 1);              // fired once, then Max=1-capped
    CHECK(ts->last_fired == 0);
    REQUIRE(world.ai_team_state(1) != nullptr);
    CHECK(world.ai_team_state(1)->teams_created == 1);   // the TRIGGER create —
                                                 // autocreate silenced; Team2
                                                 // is <none> -> ONE create
    REQUIRE(ts->adjustments == 1);
    CHECK(ts->last_adjusted == 0);               // Team1-identity scan found
                                                 // TRG1 (Team1 == TT2)
                                                 // [RE-GATE APPLIED:
                                                 // ai-trigger.md §6]
    CHECK(ts->last_success);                     // the bare-disband script ran
                                                 // to kOpDisband -> the team's
                                                 // completed_success flag; NOT
                                                 // a survivors rule [RE-GATE
                                                 // APPLIED: ai-trigger.md §4]
    CHECK(ts->weights[0] == 50.0 + ra2engine::kAiTriggerSuccessWeightDelta);
    CHECK(ts->last_old_weight == 50.0);          // first outcome: E=0 -> no
                                                 // track-record term -> 50+20
    CHECK(ts->last_new_weight ==
          50.0 + ra2engine::kAiTriggerSuccessWeightDelta);
    CHECK(ts->times_completed[0] == 1);          // C incremented on success
    CHECK(ts->times_executed[0] == 1);           // E incremented every outcome
}

TEST_CASE("World: trigger firewall - triggers_enabled=false means no trigger ever fires") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    world.add_building(0, c->x + 20, c->y + 2, 1, 1, /*house=*/0, 1000, 0, 0.0, true);
    world.enable_ai_house(1, ai3_cfg(/*triggers_enabled=*/false));
    for (int i = 0; i < 600; ++i) world.step(1.0 / 15.0);
    const ra2engine::AiTriggerState* ts = world.ai_trigger_state(1);
    REQUIRE(ts != nullptr);
    CHECK(ts->triggers_fired == 0);              // firewall held
    CHECK(ts->adjustments == 0);
    CHECK(ts->weights[0] == 50.0);               // weight untouched
    CHECK(world.ai_team_state(1)->teams_created == 0);  // autocreate silenced,
                                                 // trigger path fenced off
}

TEST_CASE("World: a trigger create is skipped at TotalAITeamCap") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    world.add_building(0, c->x + 20, c->y + 2, 1, 1, /*house=*/0, 1000, 0, 0.0, true);
    world.enable_ai_house(1, ai3_cfg(/*triggers_enabled=*/true, /*total_team_cap=*/0));
    for (int i = 0; i < 600; ++i) world.step(1.0 / 15.0);
    const ra2engine::AiTriggerState* ts = world.ai_trigger_state(1);
    REQUIRE(ts != nullptr);
    CHECK(ts->triggers_fired == 0);              // at cap -> create skipped
    CHECK(world.ai_team_state(1)->teams_created == 0);
}

// The world<->engine difficulty-convention seam [RE-GATE STOP: non-inverted
// difficulty]. A Difficult AI (engine enum 2) MUST consult the HARD enable
// slot, not Easy: the World converts the enum (0 = EASIEST) to the pure
// engine's 0 = HARDEST convention, which trigger_filters_pass re-inverts as
// enabled[2-difficulty]. The Hard-slot-only trigger fires; the Easy-slot-only
// trigger is INELIGIBLE and never fires. Under the pre-fix non-inverted fill
// these SWAP, so this fails without the world.cpp `2 - (int)difficulty` fix.
TEST_CASE("World: a Difficult AI consults the HARD enable slot, not Easy [RE-GATE: 2-difficulty edge]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    // [G-60] the TaskForce must be buildable (FUN_0041ff00): stand a NAHAND.
    world.add_building(1, c->x - 3, c->y, 1, 1, /*house=*/1, 500, 0, 0.0, true);
    world.add_building(0, c->x + 20, c->y + 2, 1, 1, /*house=*/0, 1000, 0, 0.0, true);
    // One IDLE house-1 unit so the fired team fills instantly and runs to disband.
    ra2engine::UnitCombat uc;
    uc.strength = 100; uc.image = "AITEST";
    uc.damage = 10; uc.rof = 15; uc.range = 4.0;
    uc.verses = std::vector<int>(11, 100);
    world.add_combat_unit(c->x + 3, c->y + 2, /*house=*/1, uc);
    world.enable_ai_house(1, ai3_cfg_difficult());
    const ra2engine::AiTriggerState* ts0 = world.ai_trigger_state(1);
    REQUIRE(ts0 != nullptr);
    REQUIRE(ts0->weights.size() == 2);
    const int kHard = 0, kEasy = 1;
    CHECK(ts0->weights[kHard] == 50.0);
    CHECK(ts0->weights[kEasy] == 50.0);
    for (int i = 0; i < 4500 && world.ai_trigger_state(1)->triggers_fired < 1; ++i)
        world.step(1.0 / 15.0);
    const ra2engine::AiTriggerState* ts = world.ai_trigger_state(1);
    REQUIRE(ts != nullptr);
    CHECK(ts->triggers_fired >= 1);
    CHECK(ts->last_fired == kHard);              // the Hard-slot trigger fired
    // The Easy-slot trigger is INELIGIBLE for a Difficult AI: TT3 is never
    // created, so its weight is never adjusted (adjustment runs only at the
    // disband of a team whose Team1 == the trigger's team) — it stays at start.
    CHECK(ts->weights[kEasy] == 50.0);
}

// World-level FAILURE attribution end-to-end [RE: ai-trigger.md §4/§6]. The
// trigger fires + creates its team; the recruited member is WIPED before the
// script reaches kOpDisband, so completed_success stays FALSE and the disband
// scan takes the RegisterFailure path. Success attribution is covered above;
// this pins the failure branch through the World glue. Weight math (E=0, first
// outcome, no track term): 50 + (-50) = 0, CLAMPED to min_weight 10.
TEST_CASE("World: a trigger whose team is WIPED before it completes takes the FAILURE weight path [RE: ai-trigger.md 4/6]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    // [G-60] the TaskForce must be buildable (FUN_0041ff00): stand a NAHAND.
    world.add_building(1, c->x - 3, c->y, 1, 1, /*house=*/1, 500, 0, 0.0, true);
    world.add_building(0, c->x + 20, c->y + 2, 1, 1, /*house=*/0, 1000, 0, 0.0, true);
    ra2engine::UnitCombat uc;
    uc.strength = 100; uc.image = "AITEST";
    uc.damage = 10; uc.rof = 15; uc.range = 4.0;
    uc.verses = std::vector<int>(11, 100);
    world.add_combat_unit(c->x + 3, c->y + 2, /*house=*/1, uc);
    world.enable_ai_house(1, ai3_cfg(/*triggers_enabled=*/true));
    REQUIRE(world.ai_trigger_state(1) != nullptr);
    REQUIRE(world.ai_trigger_state(1)->weights.size() == 1);
    CHECK(world.ai_trigger_state(1)->weights[0] == 50.0);
    // Fire the trigger + recruit the idle unit into the team (Gathering is
    // movement-bound, so this lands well before the bare-disband op).
    int member = -1;
    for (int i = 0; i < 4500 && member < 0; ++i) {
        world.step(1.0 / 15.0);
        const ra2engine::AiTeamState* tst = world.ai_team_state(1);
        if (tst)
            for (const ra2engine::TeamInstance& t : tst->teams)
                if (!t.members.empty()) { member = t.members[0]; break; }
    }
    REQUIRE(member >= 0);
    REQUIRE(world.ai_trigger_state(1)->adjustments == 0);   // not yet disbanded
    world.units()[member].alive = false;                    // WIPE mid-script
    for (int i = 0; i < 4500 && world.ai_trigger_state(1)->adjustments < 1; ++i)
        world.step(1.0 / 15.0);
    const ra2engine::AiTriggerState* ts = world.ai_trigger_state(1);
    REQUIRE(ts != nullptr);
    REQUIRE(ts->adjustments == 1);
    CHECK(ts->last_adjusted == 0);
    CHECK_FALSE(ts->last_success);               // wipe -> completed_success FALSE
    CHECK(ts->times_executed[0] == 1);           // E incremented every outcome
    CHECK(ts->times_completed[0] == 0);          // C NOT incremented on failure
    CHECK(ts->weights[0] == 10.0);               // 50 - 50 = 0 -> clamp to min 10
    CHECK(ts->last_old_weight == 50.0);
    CHECK(ts->last_new_weight == 10.0);
}

// Direct step_team coverage the Task-3 review flagged, now that the World
// consumes the flag [RE: ai-trigger.md §4]: completed_success is set TRUE only
// when the script runs to kOpDisband (the original's team+0x84 success flag),
// and stays FALSE when the team reaches Disbanding via a member wipe. This
// pins the exact bit the AI-3 disband attribution keys success-vs-failure on.
TEST_CASE("step_team: completed_success TRUE after 49 (success, script continues), FALSE on member wipe [RE: ai-trigger.md 4]") {
    ra2engine::AimdData aimd;
    ra2engine::TaskForce tf; tf.id = "TF1"; tf.members = {{1, 0}};  // 1 x type0
    aimd.taskforces.push_back(tf);
    ra2engine::ScriptType sc; sc.id = "SC1";
    sc.actions = {ra2engine::ScriptAction{ra2engine::kOpSuccess, 0}};  // bare 49 (success)
    aimd.scripts.push_back(sc);
    ra2engine::TeamType tt; tt.id = "TT1"; tt.taskforce = 0; tt.script = 0;
    tt.priority = 5; tt.max = 1; tt.full = false;
    aimd.teams.push_back(tt);

    // --- success path: script runs to kOpDisband ---
    {
        ra2engine::TeamInstance t; t.teamtype = 0;
        t.members = {0}; t.member_types = {0};
        ra2engine::TeamView v;
        v.members.push_back(ra2engine::TeamMemberView{0, true, 5, 5, true});
        v.base_x = 5; v.base_y = 5; v.enemy_x = 5; v.enemy_y = 5;
        std::uint32_t rng = 0x2545F491u;
        ra2engine::step_team(t, v, aimd, rng, 1.0);            // Recruiting -> Gathering
        REQUIRE(t.state == ra2engine::TeamState::Gathering);
        // Stand the member ON the realized gather cell so gather completes.
        v.members[0].cx = t.gather_x; v.members[0].cy = t.gather_y;
        ra2engine::step_team(t, v, aimd, rng, 1.0);            // Gathering: emit move
        ra2engine::step_team(t, v, aimd, rng, 1.0);            // Gathering: within -> Executing
        REQUIRE(t.state == ra2engine::TeamState::Executing);
        CHECK_FALSE(t.completed_success);                       // not yet
        // G-61 [c14.log idx 49]: 49 sets team+0x80/+0x84 (success) and the
        // script CONTINUES -- the team disbands only when it runs off the end.
        ra2engine::step_team(t, v, aimd, rng, 1.0);            // Executing: 49 (success)
        CHECK(t.completed_success);                             // success flag set
        CHECK(t.state == ra2engine::TeamState::Executing);      // not a disband
        CHECK(t.script_pc == 1);
        ra2engine::step_team(t, v, aimd, rng, 1.0);            // ran off the end
        CHECK(t.state == ra2engine::TeamState::Disbanding);
        CHECK(t.completed_success);                             // kept for AI-3 attribution
    }

    // --- wipe path: all members die before the script completes ---
    {
        ra2engine::TeamInstance t; t.teamtype = 0;
        t.members = {0}; t.member_types = {0};
        ra2engine::TeamView v;
        v.members.push_back(ra2engine::TeamMemberView{0, true, 5, 5, true});
        v.base_x = 5; v.base_y = 5; v.enemy_x = 5; v.enemy_y = 5;
        std::uint32_t rng = 0x2545F491u;
        ra2engine::step_team(t, v, aimd, rng, 1.0);            // Recruiting -> Gathering
        REQUIRE(t.state == ra2engine::TeamState::Gathering);
        v.members[0].alive = false;                             // wiped
        ra2engine::step_team(t, v, aimd, rng, 1.0);            // alive==0 -> Disbanding
        CHECK(t.state == ra2engine::TeamState::Disbanding);
        CHECK_FALSE(t.completed_success);                       // wipe -> failure witness
    }
}

// ---- CB-1 Transports/Cargo (guarded real map) [RE: transport-cargo.md] ----
namespace {
// HTK-shaped transport stats scaled for tests: a real vehicle profile with a
// cargo pool. Task 4 resolves real HTK numbers from rulesmd; tests hand-pick
// small pools to hit boundaries fast.
ra2engine::UnitCombat transport_stats(int passengers, int size_limit) {
    ra2engine::UnitCombat c;
    c.strength = 200;
    c.armor = "light";
    c.image = "HTK";
    c.is_vehicle = true;
    c.rot = 5;
    c.speed_cps = 4.0;
    c.passengers = passengers;
    c.size_limit = size_limit;
    c.size = 3;
    return c;
}
// First free walkable cell scanning east from (x0, y).
bool free_cell_east(ra2engine::World& w, int x0, int y, int& outx) {
    for (int dx = 0; dx < 12; ++dx)
        if (w.walkable(x0 + dx, y) && w.unit_at_cell(x0 + dx, y) < 0) {
            outx = x0 + dx;
            return true;
        }
    return false;
}
// True iff NO occupancy cell anywhere is owned by unit `idx`.
bool owns_no_cell(const ra2engine::World& w, int idx) {
    const ra2engine::Occupancy& occ = w.occupancy();
    for (int y = 0; y < occ.height(); ++y)
        for (int x = 0; x < occ.width(); ++x)
            if (occ.at(x, y) == idx) return false;
    return true;
}
}  // namespace

TEST_CASE("World: cargo boarding takes the passenger off-map and releases ALL occupancy") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int tx = -1;
    REQUIRE(free_cell_east(world, c->x, c->y, tx));
    world.add_combat_unit(tx, c->y, 0, transport_stats(2, 2));   // unit 0: transport
    int gx = -1;
    REQUIRE(free_cell_east(world, tx + 3, c->y, gx));
    world.add_combat_unit(gx, c->y, 0, gi_stats());              // unit 1: passenger
    tsel_.set(1);
    world.group_enter_transport(0, tsel_.ids(), 0);
    CHECK(world.units()[1].enter_target == 0);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 300 && world.units()[1].cargo_of < 0; ++f) world.step(dt);
    REQUIRE(world.units()[1].cargo_of == 0);                     // boarded
    REQUIRE(world.units()[0].cargo.size() == 1);
    CHECK(world.units()[0].cargo[0] == 1);
    CHECK(world.units()[1].enter_target == -1);
    tsel_.prune(world);                                          // NET L1: the UI drops it
    CHECK_FALSE(tsel_.has(1));                                   // force-cleared

    CHECK(owns_no_cell(world, 1));      // ALL occupancy released (incl. any
                                        // mid-hop reservation) -- spec §9
    // Off-map: unpickable, undrawn, untargetable.
    CHECK(world.unit_at_cell(world.units()[1].cell_x, world.units()[1].cell_y) != 1);
    for (int i : ra2engine::unit_draw_order(world.units())) CHECK(i != 1);
    int ex = -1;
    REQUIRE(free_cell_east(world, tx + 1, c->y + 2, ex));
    world.add_combat_unit(ex, c->y + 2, 1, cons_stats());        // unit 2: enemy
    world.step(dt);                                              // auto-acquire pass
    CHECK(world.units()[2].target != 1);                         // never the carried unit
    // One-unit-per-cell audit stays green among on-map units.
    for (std::size_t a = 0; a < world.units().size(); ++a)
        for (std::size_t b = a + 1; b < world.units().size(); ++b) {
            const ra2engine::Unit& ua = world.units()[a];
            const ra2engine::Unit& ub = world.units()[b];
            if (!ua.alive || ua.cargo_of >= 0 || !ub.alive || ub.cargo_of >= 0) continue;
            CHECK((ua.cell_x != ub.cell_x || ua.cell_y != ub.cell_y));
        }
}

TEST_CASE("World: cargo eject uses the facing-scatter eject_cell, stamps coords/facing, LIFO one per frame") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int tx = -1;
    REQUIRE(free_cell_east(world, c->x, c->y, tx));
    world.add_combat_unit(tx, c->y, 0, transport_stats(3, 2));   // unit 0: Passengers=3
    const double dt = 1.0 / 15.0;
    for (int k = 0; k < 2; ++k) {                                // units 1, 2
        int gx = -1;
        REQUIRE(free_cell_east(world, tx + 2 + k, c->y + 1, gx));
        world.add_combat_unit(gx, c->y + 1, 0, gi_stats());
    }
    tsel_.set(1);
    tsel_.set(2);
    world.group_enter_transport(0, tsel_.ids(), 0);
    for (int f = 0; f < 300 && world.units()[0].cargo.size() < 2; ++f) world.step(dt);
    REQUIRE(world.units()[0].cargo.size() == 2);
    const std::vector<int> board_order = world.units()[0].cargo;   // boarding order;
                                                                   // LIFO ejects the
                                                                   // BACK first
    (void)world.take_cargo_events();                               // drain the boards
    tsel_.clear();
    tsel_.set(0);
    world.group_unload(0, tsel_.ids());
    CHECK(world.units()[0].unload_pending);
    // The eject cell must be EXACTLY the pure picker's answer [RE-GATE
    // APPLIED: transport-cargo.md §4 -- facing-scatter, NOT a ring walk].
    auto free_cell = [&](int x, int y) {
        return world.walkable(x, y) && world.occupancy().is_free(x, y);
    };
    const int ttx = world.units()[0].cell_x, tty = world.units()[0].cell_y;
    const ra2engine::Cellxy expect = ra2engine::eject_cell(
        ttx, tty, world.units()[0].body_facing, world.units()[0].eject_counter, free_cell);
    REQUIRE(expect.x >= 0);
    world.step(dt);                                                // one eject per frame
    std::vector<ra2engine::CargoEvent> ev = world.take_cargo_events();
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ra2engine::CargoEvent::Kind::Eject);
    CHECK(ev[0].unit == board_order[1]);                           // LIFO: the LAST
                                                                   // boarded leaves
                                                                   // FIRST [RE-GATE
                                                                   // APPLIED: transport-
                                                                   // cargo.md §6]
    CHECK(ev[0].x == expect.x);                                    // the scatter cell
    CHECK(ev[0].y == expect.y);
    const ra2engine::Unit& p0 = world.units()[board_order[1]];
    CHECK(p0.cargo_of == -1);
    CHECK(p0.cell_x == expect.x);                                  // coords stamped
    CHECK(p0.cell_y == expect.y);
    CHECK(p0.fx == doctest::Approx((double)expect.x));
    CHECK(world.occupancy().at(expect.x, expect.y) == board_order[1]);  // claimed
    CHECK(p0.guard_x == expect.x);                                 // guard anchor
    CHECK(p0.facing >= 0);
    CHECK(p0.facing < 8);
    CHECK(world.units()[0].eject_counter == 1);                    // side alternator
                                                                   // bumped [§4]
    CHECK(world.units()[0].cargo.size() == 1);                     // ONE per frame
    world.step(dt);
    ev = world.take_cargo_events();
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].unit == board_order[0]);                           // LIFO: the FIRST
                                                                   // boarded leaves LAST
    CHECK(world.units()[0].cargo.empty());
    CHECK_FALSE(world.units()[0].unload_pending);                  // cleared when empty
}

TEST_CASE("World: cargo boxed-in unload HOLDs, then resumes when a neighbouring cell frees") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int tx = -1;
    REQUIRE(free_cell_east(world, c->x, c->y, tx));
    const int ty = c->y;
    world.add_combat_unit(tx, ty, 0, transport_stats(2, 2));     // unit 0
    int gx = -1;
    REQUIRE(free_cell_east(world, tx + 2, ty, gx));
    world.add_combat_unit(gx, ty, 0, gi_stats());                // unit 1
    tsel_.set(1);
    world.group_enter_transport(0, tsel_.ids(), 0);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 300 && world.units()[1].cargo_of < 0; ++f) world.step(dt);
    REQUIRE(world.units()[1].cargo_of == 0);
    (void)world.take_cargo_events();  // drain the board event so the HOLD check
                                      // below sees ONLY unload-pass activity
    // Box the transport in: fill EVERY free walkable cell at Chebyshev
    // r <= 2 (eject_cell's whole reach -- the r=1 primary/fallback plus the
    // r=2 fallback ring) with plain units. Track one OUTER (r == 2) blocker
    // to release later -- inner blockers stay put.
    int mover = -1;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int x = tx + dx, y = ty + dy;
            if (!world.walkable(x, y) || world.unit_at_cell(x, y) >= 0) continue;
            world.add_unit(x, y, "GI");
            const int bi = static_cast<int>(world.units().size()) - 1;
            if (mover < 0 && std::max(std::abs(dx), std::abs(dy)) == 2) mover = bi;
        }
    REQUIRE(mover >= 0);
    tsel_.clear();
    tsel_.set(0);
    world.group_unload(0, tsel_.ids());
    for (int f = 0; f < 5; ++f) world.step(dt);
    CHECK(world.units()[0].cargo.size() == 1);                   // HOLD: still aboard
    CHECK(world.units()[0].unload_pending);                      // order persists
    CHECK(world.take_cargo_events().empty());                    // no eject happened
    // Free one outer (r=2 fallback) cell: walk the tracked blocker away.
    tsel_.clear();
    tsel_.set(mover);
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), tx + 8, ty + 8);
    for (int f = 0; f < 400 && world.units()[0].cargo.size() == 1; ++f) world.step(dt);
    CHECK(world.units()[0].cargo.empty());                       // resumed + ejected
    CHECK(world.units()[1].cargo_of == -1);
    CHECK_FALSE(world.units()[0].unload_pending);
}

TEST_CASE("World: cargo transport death tombstones its cargo; enter orders cancel cleanly") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int tx = -1;
    REQUIRE(free_cell_east(world, c->x, c->y, tx));
    world.add_combat_unit(tx, c->y, 0, transport_stats(2, 2));   // unit 0
    int gx = -1;
    REQUIRE(free_cell_east(world, tx + 2, c->y, gx));
    world.add_combat_unit(gx, c->y, 0, gi_stats());              // unit 1: boards
    int hx = -1;
    REQUIRE(free_cell_east(world, tx + 2, c->y + 3, hx));
    world.add_combat_unit(hx, c->y + 3, 0, gi_stats());          // unit 2: en route
    tsel_.set(1);
    world.group_enter_transport(0, tsel_.ids(), 0);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 300 && world.units()[1].cargo_of < 0; ++f) world.step(dt);
    REQUIRE(world.units()[1].cargo_of == 0);
    tsel_.clear();
    tsel_.set(2);
    world.group_enter_transport(0, tsel_.ids(), 0);
    REQUIRE(world.units()[2].enter_target == 0);
    // Kill the transport the way combat does (die_time flips >= 0).
    world.units()[0].die_time = 0.0;
    world.step(dt);
    // W7-04-02 [RE: UnitClass::ReceiveDamage FUN_00736D60 KILLED branch]: the
    // passenger escapes ALIVE beside the dead transport.
    CHECK(world.units()[1].alive);
    CHECK(world.units()[1].cargo_of == -1);
    CHECK(std::abs(world.units()[1].cell_x - world.units()[0].cell_x) <= 1);
    CHECK(std::abs(world.units()[1].cell_y - world.units()[0].cell_y) <= 1);
    CHECK(world.units()[0].cargo.empty());
    CHECK(world.units()[2].enter_target == -1);                  // cancelled cleanly
    CHECK(world.units()[2].alive);                               // ...and unharmed
}

TEST_CASE("World: cargo capacity refusal at boarding; a new order supersedes enter/unload") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int tx = -1;
    REQUIRE(free_cell_east(world, c->x, c->y, tx));
    world.add_combat_unit(tx, c->y, 0, transport_stats(1, 2));   // unit 0: ONE slot
    for (int k = 0; k < 2; ++k) {                                // units 1, 2
        int gx = -1;
        REQUIRE(free_cell_east(world, tx + 2 + k, c->y + 1, gx));
        world.add_combat_unit(gx, c->y + 1, 0, gi_stats());
    }
    tsel_.set(1);
    tsel_.set(2);
    world.group_enter_transport(0, tsel_.ids(), 0);                              // oversubscribed
    const double dt = 1.0 / 15.0;
    int boards = 0, refusals = 0;
    for (int f = 0; f < 400 && boards + refusals < 2; ++f) {
        world.step(dt);
        for (const ra2engine::CargoEvent& e : world.take_cargo_events()) {
            if (e.kind == ra2engine::CargoEvent::Kind::Board) ++boards;
            if (e.kind == ra2engine::CargoEvent::Kind::Refuse) ++refusals;
        }
    }
    CHECK(boards == 1);                                          // exactly one fits
    CHECK(refusals == 1);                                        // one REFUSED at full
    CHECK(world.units()[0].cargo.size() == 1);
    // A new move order on the transport supersedes a pending unload.
    tsel_.clear();
    tsel_.set(0);
    world.group_unload(0, tsel_.ids());
    CHECK(world.units()[0].unload_pending);
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), tx + 6, c->y + 6);
    CHECK_FALSE(world.units()[0].unload_pending);                // superseded
    // A new move order on an en-route passenger supersedes its enter order.
    int wx = -1;
    REQUIRE(free_cell_east(world, tx + 2, c->y + 4, wx));
    world.add_combat_unit(wx, c->y + 4, 0, gi_stats());          // unit 3
    int vx = -1;
    REQUIRE(free_cell_east(world, wx + 1, c->y + 4, vx));
    world.add_combat_unit(vx, c->y + 4, 0, transport_stats(2, 2));  // unit 4
    tsel_.clear();
    tsel_.set(3);
    world.group_enter_transport(0, tsel_.ids(), 4);
    REQUIRE(world.units()[3].enter_target == 4);
    world.group_move(ra2engine::kAnyHouse, tsel_.ids(), tx, c->y + 6);                     // unit 3 still selected
    CHECK(world.units()[3].enter_target == -1);                  // superseded
}

// [CB-1 off-map invariant] A carried unit is off-map/invisible to census --
// ai_fill_trigger_view's own-unit count must exclude cargo_of>=0 units the
// same way it already excludes corpses (!alive / die_time>=0). Regression
// for the widening: mark the ONLY roster-type-0 unit as carried (cargo_of/
// cargo set directly -- order_enter_transport/order_unload_selected are
// house-0-only player orders and can't drive an AI-house [house=1] unit),
// key a trigger on kCondOwnerOwns unit-count >= 1, and confirm it never
// fires while the unit is carried, then DOES fire once un-carried (the same
// unit, now genuinely on-map) -- proving the harness actually exercises the
// census path rather than being vacuously green.
TEST_CASE("World: ai_fill_trigger_view excludes a carried unit from the own-unit census [CB-1]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);  // AI conyard
    // [G-60] the TaskForce must be buildable (FUN_0041ff00): stand a NAHAND.
    world.add_building(1, c->x - 3, c->y, 1, 1, /*house=*/1, 500, 0, 0.0, true);
    int tx = -1;
    REQUIRE(free_cell_east(world, c->x + 3, c->y, tx));
    world.add_combat_unit(tx, c->y, /*house=*/1, transport_stats(2, 2));  // unit 0: transport
    int gx = -1;
    REQUIRE(free_cell_east(world, tx + 3, c->y, gx));
    ra2engine::UnitCombat uc;
    uc.strength = 100;
    uc.image = "AITEST";                 // matches ai2_cfg's unit_roster[0]
    uc.damage = 10; uc.rof = 15; uc.range = 4.0;
    uc.verses = std::vector<int>(11, 100);
    world.add_combat_unit(gx, c->y, /*house=*/1, uc);   // unit 1: the ONLY
                                                        // roster-type-0 unit
    // Mark unit 1 carried directly -- exercises the exact fields the census
    // fix guards, without needing the (house-0-only) player boarding orders.
    world.units()[1].cargo_of = 0;
    world.units()[0].cargo.push_back(1);
    ra2engine::AiHouseConfig cfg = ai3_cfg(/*triggers_enabled=*/true);
    // Rekey TRG1 off kCondEnemyOwns/building onto kCondOwnerOwns/unit-type-0
    // (>= 1 of the roster's OWN unit type) -- the exact slice this fix guards.
    cfg.team.aimd.triggers[0].condition_type = ra2engine::kCondOwnerOwns;
    cfg.team.aimd.triggers[0].condition_object =
        ra2engine::TriggerObject{ra2engine::TechnoKind::Unit, 0};
    cfg.team.aimd.triggers[0].comparator =
        ra2engine::TriggerComparator{1, ra2engine::kCompGreaterEq};
    world.enable_ai_house(1, cfg);
    REQUIRE(world.ai_trigger_state(1) != nullptr);
    const double dt = 1.0 / 15.0;
    for (int i = 0; i < 900; ++i) world.step(dt);   // many 1 s check passes
    CHECK(world.ai_trigger_state(1)->triggers_fired == 0);   // carried -> NOT
                                                              // counted -> never
                                                              // eligible
    // Control: un-carry the SAME unit and confirm the trigger now fires --
    // proving the census (and this test) is live, not toothless.
    world.units()[1].cargo_of = -1;
    world.units()[0].cargo.clear();
    for (int i = 0; i < 900 && world.ai_trigger_state(1)->triggers_fired < 1; ++i)
        world.step(dt);
    CHECK(world.ai_trigger_state(1)->triggers_fired == 1);   // now counted
}

// ---- CB-2 Aircraft/Flight: World integration [RE: aircraft.md] ----
namespace {
// A minimal Jumpjet-shaped combat stat block for the flight World tests
// (hand-built -- no rulesmd/install needed) [RE-GATE APPLIED: aircraft.md §5].
ra2engine::UnitCombat zep_stats() {
    ra2engine::UnitCombat c = gi_stats();       // start from a valid infantry block
    c.image = "ZEP";
    c.is_vehicle = true;
    c.is_aircraft = true;
    c.cruise_altitude = 750.0;
    c.climb_rate = 90.0;                          // 6 leptons/frame * 15 = 90/sec
    c.descend_rate = 180.0;                       // 12 * 15
    c.flight_speed = 0.7;                         // ~cells/sec (test-only value)
    c.flight_turn = 30.0 * 256.0;                 // test-only u16/sec
    c.balloon_hover = true;
    c.omni_fire = true;
    return c;
}
}  // namespace

TEST_CASE("World: aircraft takeoff releases its ground cell; airborne holds none") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    ra2engine::Unit& z = world.add_combat_unit(20, 20, /*house=*/0, zep_stats());
    (void)z;
    const int ti = 0;
    // Grounded: it occupies its spawn cell (add_unit set it).
    CHECK(world.occupancy().at(20, 20) == ti);
    // Order it to a distant cell -> takeoff releases the ground cell immediately.
    world.order_unit_move(ti, 20, 30);
    CHECK(world.occupancy().at(20, 20) < 0);     // released on takeoff
    // Step until airborne+cruising; it must hold NO occ_ cell anywhere.
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 400 && world.units()[ti].flight_state != ra2engine::FlightState::Cruising; ++f)
        world.step(dt);
    CHECK(world.units()[ti].flight_state == ra2engine::FlightState::Cruising);
    CHECK(world.units()[ti].altitude == doctest::Approx(750.0));
    int held = 0;
    const ra2engine::Occupancy& occ = world.occupancy();
    for (int y = 0; y < occ.height(); ++y)
        for (int x = 0; x < occ.width(); ++x)
            if (occ.at(x, y) == ti) ++held;
    CHECK(held == 0);                            // airborne: reserves no cell
}

TEST_CASE("World: a ground unit may occupy the cell beneath an airborne aircraft") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.add_combat_unit(20, 20, /*house=*/0, zep_stats());   // idx 0
    world.order_unit_move(0, 20, 30);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 200; ++f) world.step(dt);
    // The aircraft is now airborne over some cell; a ground GI can claim it.
    ra2engine::Unit& g = world.add_combat_unit(20, 20, /*house=*/0, gi_stats());  // idx 1
    world.step(dt);
    CHECK(world.occupancy().at(g.cell_x, g.cell_y) == 1);      // ground unit holds it
}

TEST_CASE("World: ordering an airborne aircraft to attack claims no ground cell") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    // CB-2 [airborne-no-cell invariant]: commit_attack_target must NOT re-assert
    // ground occupancy for an airborne aircraft. Before the guard was added, the
    // attack-commit re-claimed the aircraft's ground-projection cell, which
    // nothing ever released -> a permanently stranded cell.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.add_combat_unit(20, 20, /*house=*/0, zep_stats());          // idx 0: aircraft
    ra2engine::Unit& enemy = world.add_combat_unit(30, 10, /*house=*/1, gi_stats());  // idx 1
    (void)enemy;
    // Take off and reach cruise -> airborne, holds no cell.
    world.order_unit_move(0, 20, 30);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 400 && world.units()[0].flight_state != ra2engine::FlightState::Cruising; ++f)
        world.step(dt);
    CHECK(world.units()[0].flight_state == ra2engine::FlightState::Cruising);
    // Its current ground-projection cell (must remain free after the attack order).
    const int cx = world.units()[0].cell_x;
    const int cy = world.units()[0].cell_y;
    // Order the airborne aircraft to attack the ground enemy.
    tsel_.set(0);
    world.group_attack(0, tsel_.ids(), 1);
    CHECK(world.units()[0].attack_order);            // order took effect
    // Airborne-occupancy audit: the aircraft holds NO cell anywhere.
    int held = 0;
    const ra2engine::Occupancy& occ = world.occupancy();
    for (int y = 0; y < occ.height(); ++y)
        for (int x = 0; x < occ.width(); ++x)
            if (occ.at(x, y) == 0) ++held;
    CHECK(held == 0);                                 // invariant: airborne holds none
    CHECK(occ.at(cx, cy) != 0);                       // ground-projection cell not re-claimed
    // And the cell is genuinely free: a ground unit may now occupy it.
    ra2engine::Unit& g = world.add_combat_unit(cx, cy, /*house=*/0, gi_stats());  // idx 2
    world.step(dt);
    CHECK(world.occupancy().at(g.cell_x, g.cell_y) == 2);
}

TEST_CASE("World: step_units never runs ground movement on an airborne aircraft") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.add_combat_unit(20, 20, /*house=*/0, zep_stats());
    world.order_unit_move(0, 20, 30);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 400; ++f) world.step(dt);
    // It reached its goal via step_flight (straight-line), never via A*: path
    // is empty the whole time and it arrived by fx/fy, not by walking cells.
    CHECK(world.units()[0].path.empty());
    CHECK(world.units()[0].cell_y > 20);         // it moved toward y=30
}

TEST_CASE("World: ground units do NOT auto-acquire an aircraft") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    world.add_combat_unit(20, 20, /*house=*/0, zep_stats());       // aircraft, house 0
    world.order_unit_move(0, 20, 21);                              // take off, hover near
    ra2engine::Unit& enemy = world.add_combat_unit(20, 22, /*house=*/1, gi_stats());
    (void)enemy;
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 200; ++f) world.step(dt);
    CHECK(world.units()[1].target == -1);        // enemy GI never targets the aircraft
}

TEST_CASE("World: an aircraft flies to a ground target and kills it (air-to-ground)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    ra2engine::UnitCombat z = zep_stats();
    z.damage = 250;                              // BlimpBomb Damage
    z.rof = 50;                                  // BlimpBomb ROF
    z.range = 1.5;                               // BlimpBomb Range (cells)
    z.strength = 2000;
    world.add_combat_unit(20, 20, /*house=*/0, z);          // idx 0 aircraft
    ra2engine::UnitCombat tgt = cons_stats();
    tgt.strength = 200;                          // dies in one 250-dmg hit
    ra2engine::Unit& victim = world.add_combat_unit(20, 30, /*house=*/1, tgt);  // idx 1 ground
    victim.hp = victim.max_hp = 200;
    // Take off + order the attack.
    world.order_unit_move(0, 20, 20);            // takeoff in place
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 200 && world.units()[0].flight_state != ra2engine::FlightState::Cruising; ++f)
        world.step(dt);
    tsel_.set(0);
    world.group_attack(0, tsel_.ids(), 1);                       // commit_attack_target for the aircraft
    bool died = false;
    for (int f = 0; f < 3000 && !died; ++f) {
        world.step(dt);
        if (!world.units()[1].alive || world.units()[1].die_time >= 0.0) died = true;
    }
    CHECK(died);                                 // the ground target took a lethal air hit
    CHECK(world.units()[0].alive);              // the aircraft survived (no ground AA)
}

TEST_CASE("World: one BlimpHE bomb 2-cell splash kills BOTH adjacent ground targets") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    // D5 [RE-GATE APPLIED: aircraft.md §4 -> combat.md §7]: CellSpread=2 is a
    // REAL ~512-lepton splash. One bomb on target A must also kill target B one
    // cell away (linear 100%->PercentAtMax falloff over round(CellSpread)=2).
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    ra2engine::UnitCombat z = zep_stats();
    z.damage = 250;                              // BlimpBomb Damage
    z.rof = 50;                                  // BlimpBomb ROF
    z.range = 1.5;                               // BlimpBomb Range (cells)
    z.strength = 2000;
    z.cell_spread = 2.0;                         // BlimpHE CellSpread=2
    z.percent_at_max = 0.5;                      // BlimpHE PercentAtMax=.5
    world.add_combat_unit(20, 20, /*house=*/0, z);            // idx 0 aircraft
    ra2engine::UnitCombat tgt = cons_stats();
    ra2engine::Unit& a = world.add_combat_unit(20, 30, /*house=*/1, tgt);  // idx 1 (center)
    ra2engine::Unit& b = world.add_combat_unit(21, 30, /*house=*/1, tgt);  // idx 2 (1 cell away)
    a.hp = a.max_hp = 100;                       // center 250; edge falloff 250*.75=187
    b.hp = b.max_hp = 100;                        // both die from the ONE splash
    world.order_unit_move(0, 20, 20);            // takeoff in place
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 200 && world.units()[0].flight_state != ra2engine::FlightState::Cruising; ++f)
        world.step(dt);
    tsel_.set(0);
    world.group_attack(0, tsel_.ids(), 1);                       // attack target A only
    auto dead = [&](int i) {
        return !world.units()[i].alive || world.units()[i].die_time >= 0.0;
    };
    for (int f = 0; f < 3000 && !(dead(1) && dead(2)); ++f) world.step(dt);
    CHECK(dead(1));                              // center target killed
    CHECK(dead(2));                              // adjacent target killed by the SAME splash
    CHECK(world.units()[0].alive);              // aircraft survived (no ground AA)
}

TEST_CASE("water passability: Water cells are 1, Clear/Beach/Rock are 0") {
    using ra2assets::LandType;
    // A 3x2 grid: row 0 = Clear, Water, Beach; row 1 = Rock, Water, Clear.
    std::vector<LandType> lt = {
        LandType::Clear, LandType::Water, LandType::Beach,
        LandType::Rock,  LandType::Water, LandType::Clear,
    };
    // All cells materialized (Land grid all-1): water = LandType==Water.
    std::vector<std::uint8_t> pass(6, 1);
    std::vector<std::uint8_t> w = ra2engine::build_water_passability(lt, pass, 3, 2);
    REQUIRE(w.size() == 6u);
    CHECK(w[0] == 0);   // Clear
    CHECK(w[1] == 1);   // Water
    CHECK(w[2] == 0);   // Beach (0% for plain Float)
    CHECK(w[3] == 0);   // Rock
    CHECK(w[4] == 1);   // Water
    CHECK(w[5] == 0);   // Clear
}

TEST_CASE("water passability: a Water cell that is NOT materialized stays 0") {
    // [RE-GATE APPLIED: naval.md §1] the intersection with the Land grid -- a
    // ship never paths on a non-materialized water cell.
    using ra2assets::LandType;
    std::vector<LandType> lt = { LandType::Water, LandType::Water };
    std::vector<std::uint8_t> pass = { 1, 0 };   // cell 1 not materialized
    std::vector<std::uint8_t> w = ra2engine::build_water_passability(lt, pass, 2, 1);
    REQUIRE(w.size() == 2u);
    CHECK(w[0] == 1);   // Water AND materialized
    CHECK(w[1] == 0);   // Water but NOT materialized -> blocked
}

TEST_CASE("water passability: an empty/short LandType array yields an all-0 grid") {
    std::vector<std::uint8_t> pass(12, 1);
    std::vector<std::uint8_t> w = ra2engine::build_water_passability({}, pass, 4, 3);
    CHECK(w.size() == 12u);
    for (std::uint8_t v : w) CHECK(v == 0);   // default-empty water grid (spec §8)
}

TEST_CASE("water passability: build_passability output is byte-identical to today") {
    // Regression pin: adding the water grid must not perturb the Land grid.
    std::vector<ra2assets::MapCell> cells;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) { ra2assets::MapCell c; c.x = x; c.y = y; cells.push_back(c); }
    int gw = 0, gh = 0;
    std::vector<std::uint8_t> land = ra2engine::build_passability(cells, gw, gh);
    CHECK(gw == 4);
    CHECK(gh == 4);
    for (std::uint8_t v : land) CHECK(v == 1);   // all 16 materialized -> walkable, unchanged
}

// ===========================================================================
// CB-3 Naval / Water integration [RE-GATE APPLIED: naval.md §1/§3]
// ===========================================================================
namespace {
// Zero-arg World builder for the naval cases: binds a World to the shared,
// load-once mapsmd03 fixture map. That map is a long-lived function-local
// static (asset_fixture.hpp), so the returned World's NON-owning map pointer
// stays valid after the return. Overloads the bool load_real_map_w(MapFile&)
// helper above by arity (both in this TU's anonymous namespace). Callers MUST
// gate on ra2fixture::mapsmd03_available() first (same as every other
// real-map case in this file) -- an absent install means an empty-grid World
// (grid_w()==grid_h()==0), and these cases hand-paint fixed cell indices
// (e.g. lt[50*gw+60]) into a gw*gh-sized vector, which would be an
// out-of-bounds WRITE (not a clean CHECK failure) against a zero-sized grid.
ra2engine::World load_real_map_w() {
    return ra2engine::World(ra2fixture::mapsmd03_map());
}

// A minimal naval combat-stat block for the World naval tests (hand-built --
// no rulesmd/install needed) [RE-GATE APPLIED: naval.md §5].
ra2engine::UnitCombat hyd_stats() {
    ra2engine::UnitCombat c;
    c.image = "HYD";
    c.strength = 400;
    c.armor = "heavy";
    c.is_vehicle = true;
    c.is_naval = true;
    c.movement_domain = ra2engine::MovementDomain::Water;
    c.speed_cps = 3.0;
    c.rot = 5;  // a ship is a voxel vehicle -- it must be able to turn to face
                // its heading before it rolls (step_units's vehicle turn gate),
                // so a nonzero ROT is required for it to move at all.
    return c;
}
}  // namespace

TEST_CASE("World: a naval unit paths across water via A* on water_passable_") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world = load_real_map_w();
    // Paint a horizontal water corridor across materialized interior rows. The
    // real mapsmd03 grid is a 213x213 rotated diamond: at row y the materialized
    // span is x in [104-y, 102+y] (empirically verified), so rows 50/51 x=60..90
    // sit solidly inside it -- the set_water_terrain intersection (water =
    // Water-land-type AND passable_) keeps them [RE-GATE APPLIED: naval.md §1].
    using ra2assets::LandType;
    const int gw = world.grid_w(), gh = world.grid_h();
    std::vector<LandType> lt(static_cast<size_t>(gw) * gh, LandType::Clear);
    for (int x = 60; x <= 90; ++x) {
        lt[static_cast<size_t>(50) * gw + x] = LandType::Water;
        lt[static_cast<size_t>(51) * gw + x] = LandType::Water;
    }
    world.set_water_terrain(lt);
    ra2engine::Unit& s = world.add_combat_unit(60, 50, /*house=*/0, hyd_stats());
    (void)s;
    world.order_unit_move(0, 90, 50);            // order across the water corridor
    CHECK(world.units()[0].path.size() > 1);     // a real water path was found
    for (const ra2engine::Cellxy& c : world.units()[0].path) {
        const std::size_t i = static_cast<std::size_t>(c.y) * gw + c.x;
        CHECK(lt[i] == LandType::Water);         // every path cell is Water
    }
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 4000 && world.units()[0].moving(); ++f) world.step(dt);
    CHECK(world.units()[0].cell_x == 90);
    CHECK(world.units()[0].cell_y == 50);        // arrived across the water
}

TEST_CASE("World: a naval unit ordered onto land is BLOCKED (empty/stay-put path)") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world = load_real_map_w();
    using ra2assets::LandType;
    const int gw = world.grid_w(), gh = world.grid_h();
    std::vector<LandType> lt(static_cast<size_t>(gw) * gh, LandType::Clear);
    lt[static_cast<size_t>(50) * gw + 60] = LandType::Water;   // one materialized water cell
    world.set_water_terrain(lt);
    world.add_combat_unit(60, 50, /*house=*/0, hyd_stats());
    world.order_unit_move(0, 70, 50);            // (70,50) is Clear/land -> unreachable
    CHECK(world.units()[0].path.empty());        // no water path -> stay put
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 60; ++f) world.step(dt);
    CHECK(world.units()[0].cell_x == 60);        // did not move onto land
    CHECK(world.units()[0].cell_y == 50);
}

TEST_CASE("World: a ground unit's A* is byte-identical with and without a naval unit present") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    // The parameterization must not perturb ground pathing.
    auto ground_path = [](bool with_naval) {
        ra2engine::World world = load_real_map_w();
        if (with_naval) {
            using ra2assets::LandType;
            const int gw = world.grid_w(), gh = world.grid_h();
            std::vector<LandType> lt(static_cast<size_t>(gw) * gh, LandType::Clear);
            lt[static_cast<size_t>(50) * gw + 60] = LandType::Water;
            world.set_water_terrain(lt);
            world.add_combat_unit(60, 50, /*house=*/0, hyd_stats());   // idx 0 naval
        }
        // Ground unit at a materialized interior cell so its A* yields a REAL
        // (non-empty) path -- the byte-identity check is only meaningful on a
        // non-trivial path.
        ra2engine::Unit& g = world.add_combat_unit(90, 50, /*house=*/0, gi_stats());  // ground
        const int gi_idx = with_naval ? 1 : 0;
        (void)g;
        world.order_unit_move(gi_idx, 94, 54);
        return world.units()[gi_idx].path;
    };
    const std::vector<ra2engine::Cellxy> a = ground_path(false);
    const std::vector<ra2engine::Cellxy> b = ground_path(true);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].x == b[i].x);
        CHECK(a[i].y == b[i].y);                 // identical ground path either way
    }
}

TEST_CASE("World: a naval unit approaches over water and kills a naval target (FlakTrackGun)") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world = load_real_map_w();
    using ra2assets::LandType;
    const int gw = world.grid_w(), gh = world.grid_h();
    // Water corridor along a MATERIALIZED interior row: the mapsmd03 diamond's
    // passable span at row 50 is x in [54,152], so a corridor at x=56..96 sits
    // solidly inside it (set_water_terrain keeps only Water-land-type AND
    // passable_ cells) [RE-GATE APPLIED: naval.md §1; matches the Task-4 naval
    // path tests' rows 50/51, x=60..90 region].
    std::vector<LandType> lt(static_cast<size_t>(gw) * gh, LandType::Clear);
    for (int x = 56; x <= 96; ++x) lt[static_cast<size_t>(50) * gw + x] = LandType::Water;
    world.set_water_terrain(lt);

    ra2engine::UnitCombat atk = hyd_stats();
    atk.damage = 25;                             // FlakTrackGun Damage
    atk.rof = 25;                                // FlakTrackGun ROF
    atk.range = 5.0;                             // FlakTrackGun Range (cells)
    atk.verses = {100, 100, 100, 100, 100};      // full damage vs the target armor
    world.add_combat_unit(60, 50, /*house=*/0, atk);          // idx 0 attacker (naval)
    ra2engine::UnitCombat tgt = hyd_stats();
    tgt.strength = 100;                          // dies in a few 25-dmg hits
    ra2engine::Unit& victim = world.add_combat_unit(92, 50, /*house=*/1, tgt);  // idx 1 (naval)
    victim.hp = victim.max_hp = 100;

    tsel_.clear();
    tsel_.set(0);
    world.group_attack(0, tsel_.ids(), 1);                       // attack the hostile ship
    const double dt = 1.0 / 15.0;
    bool died = false;
    for (int f = 0; f < 6000 && !died; ++f) {
        world.step(dt);
        if (!world.units()[1].alive || world.units()[1].die_time >= 0.0) died = true;
    }
    CHECK(died);                                 // approached over water + fired to a kill
    CHECK(world.units()[0].alive);              // attacker survived
    // The attacker moved along the water corridor toward the target.
    CHECK(world.units()[0].cell_x > 60);
    CHECK(world.units()[0].cell_y == 50);        // stayed on the water row
}

// --- CB-4 Superweapons: World registry (Task 3) [RE: superweapon.md] ---------

TEST_CASE("World SW: grant_superweapon materializes the record and opens sw_any_") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    CHECK_FALSE(world.sw_ready(0));
    CHECK(world.sw_charge_fraction(0) == doctest::Approx(0.0));
    CHECK(world.sw_state(0) == nullptr);          // no SW granted yet
    std::array<int, 11> verses;
    verses.fill(100);
    world.grant_superweapon(0, ra2engine::SuperweaponType::Nuke, /*recharge_secs=*/10.0,
                            /*dmg=*/600, /*cell_spread=*/10.0, /*pmax=*/0.02, verses);
    REQUIRE(world.sw_state(0) != nullptr);
    CHECK(world.sw_state(0)->type == ra2engine::SuperweaponType::Nuke);
    CHECK_FALSE(world.sw_ready(0));               // not charged yet
}

TEST_CASE("World SW: step_superweapon charges the granted house to READY at recharge_secs") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    std::array<int, 11> verses; verses.fill(100);
    world.grant_superweapon(0, ra2engine::SuperweaponType::Nuke, 10.0, 600, 10.0, 0.02, verses);
    const double dt = 1.0 / 15.0;
    int frames = 0;
    for (; frames < 100000 && !world.sw_ready(0); ++frames) world.step(dt);
    CHECK(world.sw_ready(0));
    CHECK(world.sw_charge_fraction(0) == doctest::Approx(1.0));
    // ~10s / (1/15 s) = 150 frames; allow the exact clamp frame.
    CHECK(frames >= 150);
    CHECK(frames <= 152);
}

TEST_CASE("World SW: an ungranted house stays inert and a no-SW World is a no-op") {
    // step_superweapon must not touch an ungranted house, and a World that never
    // grants a SW steps exactly as before (sw_any_ false -> strict no-op, §8).
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    std::array<int, 11> verses; verses.fill(100);
    world.grant_superweapon(0, ra2engine::SuperweaponType::Nuke, 5.0, 600, 10.0, 0.02, verses);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 30; ++f) world.step(dt);
    CHECK(world.sw_state(1) == nullptr);          // house 1 was never granted
    CHECK_FALSE(world.sw_ready(1));
    ra2engine::World bare(map);                     // never grants a SW
    for (int f = 0; f < 30; ++f) bare.step(dt);    // step_superweapon early-returns
    CHECK(bare.sw_state(0) == nullptr);
}

// --- CB-4 Superweapons: the fired EFFECT (Task 4) [RE: superweapon.md] --------

namespace {
// A minimal target combat-stat block (hand-built -- no weapon, so the charge-up
// step()s never let these units fight each other before the nuke lands).
ra2engine::UnitCombat sw_target_stats() {
    ra2engine::UnitCombat c;
    c.image = "E1";
    c.strength = 100;
    c.armor = "none";
    return c;
}
// The [NUKE] verses (uniform 100%) as a full 11-slot array -- damage_vs identity,
// keeps the test's armor math simple.
std::array<int, 11> nuke_verses_arr() {
    std::array<int, 11> v; v.fill(100);
    return v;
}
}  // namespace

TEST_CASE("World SW: fire_superweapon is refused before READY, succeeds after, resets") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.grant_superweapon(0, ra2engine::SuperweaponType::Nuke, 5.0, 600, 10.0, 0.02,
                            nuke_verses_arr());
    world.add_combat_unit(20, 20, /*house=*/1, sw_target_stats());   // an enemy at the cell
    CHECK_FALSE(world.fire_superweapon(0, 20, 20));   // not charged -> refused
    CHECK(world.units()[0].hp == world.units()[0].max_hp);   // undamaged
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 200 && !world.sw_ready(0); ++f) world.step(dt);
    REQUIRE(world.sw_ready(0));
    CHECK(world.fire_superweapon(0, 20, 20));          // charged -> fires
    CHECK_FALSE(world.sw_ready(0));                    // timer reset, ready cleared
    CHECK(world.sw_charge_fraction(0) == doctest::Approx(0.0));
}

TEST_CASE("World SW: the detonation kills enemies in radius, spares the firing house, "
          "leaves out-of-radius units untouched") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.grant_superweapon(0, ra2engine::SuperweaponType::Nuke, 1.0, 600, 10.0, 0.02,
                            nuke_verses_arr());
    world.add_combat_unit(20, 20, /*house=*/1, sw_target_stats());   // idx 0: near enemy
    world.add_combat_unit(21, 20, /*house=*/0, sw_target_stats());   // idx 1: own (firing house)
    world.add_combat_unit(20, 90, /*house=*/1, sw_target_stats());   // idx 2: far enemy
    const int own_hp0 = world.units()[1].hp, far_hp0 = world.units()[2].hp;
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 30 && !world.sw_ready(0); ++f) world.step(dt);
    REQUIRE(world.sw_ready(0));
    REQUIRE(world.fire_superweapon(0, 20, 20));         // launch AT the enemy cluster cell
    // W2-AN-05: the missile climbs 200 frames and falls 200 frames first.
    CHECK(world.units()[0].hp > 0);
    for (int f = 0; f < 399; ++f) world.step(dt);
    CHECK(world.units()[0].hp > 0);                     // still in flight at frame 399
    world.step(dt);
    CHECK(world.units()[0].hp <= 0);                    // near enemy dead (600 dmg >> 100 hp)
    // W2-AN-06 [RE: superweapon.md §5 -- FUN_00489190 has no house filter]:
    // the firing house's own unit in the blast dies too.
    CHECK(world.units()[1].hp < own_hp0);
    CHECK(world.units()[2].hp == far_hp0);              // 70 cells away -> outside CellSpread=10
}

TEST_CASE("World SW: the detonation falloff -- edge damage < center damage") {
    // Center takes full; a unit near the radius edge takes ~PercentAtMax*Damage.
    // With CellSpread=10 the radius is round(10)=10 cells; a unit 9 cells out is
    // near the edge. Use high hp so neither dies and we can compare damage taken.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.grant_superweapon(0, ra2engine::SuperweaponType::Nuke, 1.0, 600, 10.0, 0.02,
                            nuke_verses_arr());
    ra2engine::UnitCombat tough = sw_target_stats();
    tough.strength = 100000;                            // survive to measure damage
    world.add_combat_unit(20, 20, /*house=*/1, tough);  // idx 0: center
    world.add_combat_unit(29, 20, /*house=*/1, tough);  // idx 1: 9 cells out (near edge)
    const int c_hp0 = world.units()[0].hp, e_hp0 = world.units()[1].hp;
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 30 && !world.sw_ready(0); ++f) world.step(dt);
    REQUIRE(world.fire_superweapon(0, 20, 20));
    for (int f = 0; f < 400; ++f) world.step(dt);       // W2-AN-05 flight time
    const int c_dmg = c_hp0 - world.units()[0].hp;
    const int e_dmg = e_hp0 - world.units()[1].hp;
    CHECK(c_dmg > 0);
    CHECK(e_dmg > 0);
    CHECK(e_dmg < c_dmg);                               // linear falloff toward the edge
}

TEST_CASE("World SW: the detonation splashes BUILDINGS -- in-radius damaged/destroyed, "
          "own spared, out-of-radius untouched [RE-GATE APPLIED superweapon.md §5.3]") {
    // The nuke levels structures in the blast (the SW-only building loop in
    // fire_superweapon). Enemy building at the target cell is destroyed; an own
    // building in-radius is spared (enemies-only); a far enemy building is
    // untouched. armor "none" + Verses 100% -> full 600 damage at the center.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.grant_superweapon(0, ra2engine::SuperweaponType::Nuke, 1.0, 600, 10.0, 0.02,
                            nuke_verses_arr());
    // add_building(type, cx, cy, fw, fh, house, strength, armor_ix, buildup_total, instant)
    const int near_enemy_b = world.add_building(0, 20, 20, 1, 1, /*house=*/1, /*strength=*/100,
                                                /*armor_ix=*/0, 0.0, /*instant=*/true);
    const int own_b        = world.add_building(0, 22, 20, 1, 1, /*house=*/0, 100, 0, 0.0, true);
    const int far_enemy_b  = world.add_building(0, 20, 60, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    const int own_hp0 = world.buildings()[own_b].hp;
    const int far_hp0 = world.buildings()[far_enemy_b].hp;
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 30 && !world.sw_ready(0); ++f) world.step(dt);
    REQUIRE(world.sw_ready(0));
    REQUIRE(world.fire_superweapon(0, 20, 20));         // launch AT the enemy building
    for (int f = 0; f < 400; ++f) world.step(dt);       // W2-AN-05 flight time
    CHECK(world.buildings()[near_enemy_b].hp <= 0);     // 600 dmg >> 100 hp -> destroyed
    CHECK(world.buildings()[near_enemy_b].state == ra2engine::Building::State::Dead);
    CHECK(world.buildings()[own_b].hp < own_hp0);       // W2-AN-06: own structures are hit too
    CHECK(world.buildings()[far_enemy_b].hp == far_hp0);  // 40 cells away -> outside CellSpread=10
    CHECK(world.buildings()[far_enemy_b].state != ra2engine::Building::State::Dead);
}

TEST_CASE("World capture: capture_building flips ownership + resets primary [RE: capture.md §A3]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    // add_building(type, cx, cy, fw, fh, house, strength, armor_ix, buildup_total, instant)
    const int bi = world.add_building(0, 20, 20, 1, 1, /*house=*/1, /*strength=*/100,
                                      /*armor_ix=*/0, 0.0, /*instant=*/true);
    REQUIRE(bi >= 0);
    world.buildings()[bi].primary = true;
    CHECK(world.buildings()[bi].house == 1);
    CHECK(world.capture_building(bi, /*new_house=*/0, /*startup=*/0));
    CHECK(world.buildings()[bi].house == 0);       // ownership flipped
    CHECK_FALSE(world.buildings()[bi].primary);    // primary reset
    CHECK_FALSE(world.capture_building(bi, 0, 0)); // no-op: already owned
}

TEST_CASE("World capture: Yuri Prime transfers a hostile building") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    const int prime = static_cast<int>(world.units().size());
    world.add_combat_unit(10, 10, 0, gi_stats());
    world.units()[static_cast<std::size_t>(prime)].tg.t.rules_id = "YURIPR";
    const int bi = world.add_building(0, 14, 10, 1, 1, 1, 100, 0, 0.0, true);

    REQUIRE(bi >= 0);
    CHECK(world.capture_building_with_unit(prime, bi, 0));
    CHECK(world.buildings()[static_cast<std::size_t>(bi)].house == 0);
    CHECK_FALSE(world.capture_building_with_unit(prime, bi, 0));
}

TEST_CASE("World capture: Yuri Prime's building weapon captures without damaging") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    auto stats = gi_stats();
    stats.damage = 0;
    stats.range = 5.0;
    stats.rof = 1;
    stats.verses = {0};
    const int prime = static_cast<int>(world.units().size());
    world.add_combat_unit(10, 10, 0, stats);
    world.units()[static_cast<std::size_t>(prime)].tg.t.rules_id = "YURIPR";
    const int bi = world.add_building(0, 12, 10, 1, 1, 1, 100, 0, 0.0, true);
    REQUIRE(bi >= 0);
    const int hp = world.buildings()[static_cast<std::size_t>(bi)].hp;

    world.group_attack_building(0, {prime}, bi);
    for (int frame = 0; frame < 5 && world.buildings()[static_cast<std::size_t>(bi)].house != 0; ++frame)
        world.step(1.0 / 15.0);

    CHECK(world.buildings()[static_cast<std::size_t>(bi)].house == 0);
    CHECK(world.buildings()[static_cast<std::size_t>(bi)].hp == hp);
}

TEST_CASE("World capture: AI sells engineer captures but keeps Unsellable buildings") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    std::vector<ra2engine::BuildingTypeDef> roster(3);
    roster[0].id = "GACNST";
    roster[0].cost = 3000;
    roster[0].strength = 1000;
    roster[0].crewed = true;
    roster[2].id = "CAOILD";
    roster[2].unsellable = true;
    world.set_building_types(std::move(roster));
    world.enable_ai_house(0, ra2engine::AiHouseConfig{});
    const int sellable = world.add_building(0, 5, 5, 5, 4, 1, 1000, 0, 3.6, true);
    const int unsellable = world.add_building(2, 20, 5, 2, 2, 1, 1000, 0, 0.0, true);

    REQUIRE(sellable >= 0);
    REQUIRE(unsellable >= 0);
    CHECK(world.capture_building(sellable, 0, 0));
    CHECK(world.building_selling(sellable));
    CHECK(world.capture_building(unsellable, 0, 0));
    CHECK_FALSE(world.building_selling(unsellable));
    CHECK(world.buildings()[static_cast<std::size_t>(unsellable)].house == 0);
}

TEST_CASE("World capture: repair_building heals an own damaged building [RE: capture.md §A5]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    // add_building(type, cx, cy, fw, fh, house, strength, armor_ix, buildup_total, instant)
    const int bi = world.add_building(0, 20, 20, 1, 1, /*house=*/0, /*strength=*/100,
                                      /*armor_ix=*/0, 0.0, /*instant=*/true);
    REQUIRE(bi >= 0);
    world.buildings()[bi].hp = world.buildings()[bi].max_hp / 2;   // damaged
    CHECK(world.repair_building(bi, /*by_house=*/0));
    CHECK(world.buildings()[bi].hp == world.buildings()[bi].max_hp);   // full
    CHECK_FALSE(world.repair_building(bi, 0));         // already full -> no action
    CHECK_FALSE(world.repair_building(bi, 1));         // not owned by house 1
}

TEST_CASE("World engineer capture: a DISTANT order defers -- capture happens on ARRIVAL, "
          "not the frame the order is issued [RE: capture.md §A5]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // Find an engineer cell with a clear walkable corridor `dist` cells east
    // (the engineer's approach path + the 1x1 enemy building foundation).
    const int dist = 6;
    int ex = -1, ey = -1;
    for (int dy = 0; dy < 12 && ex < 0; ++dy)
        for (int dx = 0; dx < 12 && ex < 0; ++dx) {
            const int x = c->x + dx, y = c->y + dy;
            bool ok = true;
            for (int k = 0; k <= dist; ++k)
                if (!world.walkable(x + k, y)) { ok = false; break; }
            if (ok) { ex = x; ey = y; }
        }
    REQUIRE(ex >= 0);
    ra2engine::UnitCombat eng = gi_stats();
    eng.image = "ENGINEER";
    world.add_combat_unit(ex, ey, /*house=*/0, eng);            // unit 0: engineer
    const int bx = ex + dist, by = ey;
    const int bi = world.add_building(0, bx, by, 1, 1, /*house=*/1, /*strength=*/100,
                                      /*armor_ix=*/0, 0.0, /*instant=*/true);   // enemy building
    REQUIRE(bi >= 0);
    world.set_track_entered(true);   // W2-CA-01 campaign opt-in
    world.order_engineer_capture(0, bi);
    // Order recorded; NOTHING happens on the same frame the order is issued.
    CHECK(world.units()[0].capture_target == bi);
    CHECK(world.units()[0].alive);                              // engineer NOT consumed yet
    CHECK(world.buildings()[bi].house == 1);                    // still enemy-owned
    // One frame in: the engineer is still far away -> no capture.
    const double dt = 1.0 / 15.0;
    world.step(dt);
    CHECK(world.buildings()[bi].house == 1);
    CHECK(world.units()[0].alive);
    // Step to arrival: adjacency triggers the capture + consumes the engineer.
    for (int f = 0; f < 400 && world.units()[0].alive; ++f) world.step(dt);
    CHECK(world.buildings()[bi].house == 0);                    // captured -> house 0
    CHECK_FALSE(world.units()[0].alive);                        // engineer consumed on arrival
    CHECK(world.units()[0].capture_target == -1);               // intent cleared
    // W2-CA-01: exactly one EnteredBy record (capture, consumed engineer).
    const auto ev = world.take_entered_events();
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == ra2engine::World::EnteredEvent::Capture);
    CHECK(ev[0].unit == 0);
    CHECK(ev[0].building == bi);
    CHECK(ev[0].house == 0);
    CHECK(ev[0].consumed);
    CHECK(world.take_entered_events().empty());
}

TEST_CASE("World spy infiltrate: a DISTANT order defers -- the effect (cash steal) happens on "
          "ARRIVAL, not the frame the order is issued [RE: capture.md §B]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // Find a spy cell with a clear walkable corridor `dist` cells east (the
    // spy's approach path + the 1x1 enemy refinery foundation).
    const int dist = 6;
    int sx = -1, sy = -1;
    for (int dy = 0; dy < 12 && sx < 0; ++dy)
        for (int dx = 0; dx < 12 && sx < 0; ++dx) {
            const int x = c->x + dx, y = c->y + dy;
            bool ok = true;
            for (int k = 0; k <= dist; ++k)
                if (!world.walkable(x + k, y)) { ok = false; break; }
            if (ok) { sx = x; sy = y; }
        }
    REQUIRE(sx >= 0);
    ra2engine::UnitCombat spy = gi_stats();
    spy.image = "SPY";
    world.add_combat_unit(sx, sy, /*house=*/0, spy);           // unit 0: spy
    const int bx = sx + dist, by = sy;
    const int refn = world.add_building(0, bx, by, 1, 1, /*house=*/1, /*strength=*/100,
                                        /*armor_ix=*/0, 0.0, /*instant=*/true);   // enemy refinery
    REQUIRE(refn >= 0);
    world.grant_credits_for_test(0, 0.0);                      // spy house 0
    world.grant_credits_for_test(1, 1000.0);                   // victim house 1
    using P = ra2engine::World::InfiltratePurpose;
    world.order_spy_infiltrate(0, refn, P::Refinery, 0.5, 1000);
    // Order recorded; NOTHING happens on the same frame the order is issued.
    CHECK(world.units()[0].infiltrate_target == refn);
    CHECK(world.units()[0].alive);                             // spy NOT consumed yet
    CHECK(world.house_credits(1) == doctest::Approx(1000.0));  // victim untouched
    CHECK(world.house_credits(0) == doctest::Approx(0.0));     // spy untouched
    // One frame in: the spy is still far away -> no infiltration.
    const double dt = 1.0 / 15.0;
    world.step(dt);
    CHECK(world.house_credits(1) == doctest::Approx(1000.0));
    CHECK(world.house_credits(0) == doctest::Approx(0.0));
    CHECK(world.units()[0].alive);
    // Step to arrival: adjacency triggers the infiltration + consumes the spy.
    for (int f = 0; f < 400 && world.units()[0].alive; ++f) world.step(dt);
    CHECK(world.house_credits(1) == doctest::Approx(500.0));   // victim halved on arrival
    CHECK(world.house_credits(0) == doctest::Approx(500.0));   // spy gained on arrival
    CHECK_FALSE(world.units()[0].alive);                       // spy consumed on arrival
    CHECK(world.units()[0].infiltrate_target == -1);           // intent cleared
}

TEST_CASE("World capture: neutral oil derrick pays its owner ONLY after capture [RE: capture.md §D1]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    // Place a neutral (house 2) income building; register its 20/100-frame tick.
    // add_building(type, cx, cy, fw, fh, house, strength, armor_ix, buildup_total, instant)
    const int oil = world.add_building(0, 20, 20, 1, 1, /*house=*/ra2engine::kNeutralHouse,
                                       /*strength=*/100, /*armor_ix=*/0, 0.0, /*instant=*/true);
    REQUIRE(oil >= 0);
    world.register_tech_income(oil, /*amount=*/20, /*delay=*/100);
    const double dt = 1.0 / 15.0;
    for (int i = 0; i < 300; ++i) world.step(dt);              // neutral pays nobody
    CHECK(world.house_credits(0) == doctest::Approx(0.0));
    // Capture it (from neutral) -> startup 1000 to house 0, then income accrues.
    CHECK(world.capture_building(oil, /*new_house=*/0, /*startup=*/1000));
    CHECK(world.house_credits(0) == doctest::Approx(1000.0));  // startup granted once
    for (int i = 0; i < 100; ++i) world.step(dt);              // one delay period
    CHECK(world.house_credits(0) == doctest::Approx(1020.0));  // +20 amount
}

TEST_CASE("World infiltrate: refinery cash steal moves 0.5x victim credits [RE: capture.md §B3]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    // add_building(type, cx, cy, fw, fh, house, strength, armor_ix, buildup_total, instant)
    const int refn = world.add_building(0, 20, 20, 1, 1, /*house=*/1, /*strength=*/100,
                                        /*armor_ix=*/0, 0.0, /*instant=*/true);   // enemy refinery
    REQUIRE(refn >= 0);
    world.grant_credits_for_test(0, 0.0);                 // spy house 0
    world.grant_credits_for_test(1, 1000.0);              // victim house 1
    using P = ra2engine::World::InfiltratePurpose;
    CHECK(world.infiltrate_building(refn, /*spy_house=*/0, P::Refinery, 0.5, 1000));
    CHECK(world.house_credits(1) == doctest::Approx(500.0));   // victim halved
    CHECK(world.house_credits(0) == doctest::Approx(500.0));   // spy gained
    CHECK_FALSE(world.infiltrate_building(refn, /*spy_house=*/1, P::Refinery, 0.5, 1000)); // self-spy
    CHECK(world.house_credits(1) == doctest::Approx(500.0));   // self-spy left credits untouched
}

TEST_CASE("World infiltrate: power plant arms the blackout timer [RE: capture.md §B2]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    const int pow = world.add_building(0, 20, 20, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    REQUIRE(pow >= 0);
    using P = ra2engine::World::InfiltratePurpose;
    CHECK_FALSE(world.house_blacked_out(1));
    CHECK(world.infiltrate_building(pow, /*spy_house=*/0, P::PowerPlant, 0.5, 1000));
    CHECK(world.house_blacked_out(1));
    CHECK(world.blackout_frames(1) == 1000);
    // Decays in step (100 frames = ~6.67 s).
    const double dt = 1.0 / 15.0;
    for (int i = 0; i < 100; ++i) world.step(dt);
    CHECK(world.blackout_frames(1) == 900);
}

TEST_CASE("World infiltrate: factory sets the produced-veteran flag for the spy house "
          "[RE: capture.md §B5]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    const int fac = world.add_building(0, 20, 20, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    REQUIRE(fac >= 0);
    using P = ra2engine::World::InfiltratePurpose;
    CHECK_FALSE(world.produced_veteran(0));
    CHECK(world.infiltrate_building(fac, /*spy_house=*/0, P::Factory, 0.5, 1000));
    CHECK(world.produced_veteran(0));            // spy house's future units veteran
    CHECK_FALSE(world.produced_veteran(1));      // victim unaffected
}

TEST_CASE("World infiltrate: superweapon building resets the victim SW recharge "
          "[RE: capture.md §B4]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    const int swb = world.add_building(0, 20, 20, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    REQUIRE(swb >= 0);
    std::array<int, 11> verses; verses.fill(100);
    world.grant_superweapon(1, ra2engine::SuperweaponType::Nuke, /*recharge_secs=*/10.0,
                            /*dmg=*/600, /*cell_spread=*/10.0, /*pmax=*/0.02, verses);
    const double dt = 1.0 / 15.0;
    for (int i = 0; i < 75; ++i) world.step(dt);              // ~5 s of a 10 s recharge
    CHECK(world.sw_charge_fraction(1) > 0.0);                 // partially charged
    using P = ra2engine::World::InfiltratePurpose;
    CHECK(world.infiltrate_building(swb, /*spy_house=*/0, P::Superweapon, 0.5, 1000));
    CHECK(world.sw_charge_fraction(1) == doctest::Approx(0.0));   // recharge reset
    CHECK_FALSE(world.sw_ready(1));
}

TEST_CASE("World infiltrate: radar/lab is inert and invalid targets no-op [RE: capture.md §B6]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    const int rl = world.add_building(0, 20, 20, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    REQUIRE(rl >= 0);
    using P = ra2engine::World::InfiltratePurpose;
    CHECK(world.infiltrate_building(rl, /*spy_house=*/0, P::RadarLab, 0.5, 1000));  // recognized, no state change
    CHECK_FALSE(world.house_blacked_out(1));
    CHECK(world.house_credits(0) == doctest::Approx(0.0));
    // Invalid index / self-spy no-op returns false.
    CHECK_FALSE(world.infiltrate_building(-1, 0, P::Refinery, 0.5, 1000));
    CHECK_FALSE(world.infiltrate_building(999, 0, P::Refinery, 0.5, 1000));
}

TEST_CASE("World veteran: rank-1 spawn scales firepower + HP; rank 0 identical [RE: capture.md §B5]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* sc = ra2engine::pick_start_cell(map);
    REQUIRE(sc != nullptr);
    ra2engine::UnitCombat c;
    c.image = "E1"; c.strength = 100; c.armor = "none"; c.damage = 40;
    ra2engine::Unit& base = world.add_combat_unit(sc->x, sc->y, /*house=*/0, c);
    CHECK(base.w_damage == 40);
    CHECK(base.max_hp == 100);
    CHECK(base.hp == 100);
    CHECK(base.veteran_rank == 0);
    c.veteran_rank = 1;
    ra2engine::Unit& vet = world.add_combat_unit(sc->x + 1, sc->y, /*house=*/0, c);
    CHECK(vet.veteran_rank == 1);
    CHECK(vet.w_damage == 44);    // 40 * 1.1 (VeteranCombat), round to nearest
    CHECK(vet.max_hp == 150);     // 100 * 1.5 (VeteranArmor)
    CHECK(vet.hp == 150);
}

TEST_CASE("World veteran: a spied factory makes produced units veteran [RE: capture.md §B5]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* sc = ra2engine::pick_start_cell(map);
    REQUIRE(sc != nullptr);
    // House 0's own launchable factory.
    const int fac = world.add_building(/*type=*/0, sc->x, sc->y, /*fw=*/1, /*fh=*/1, /*house=*/0,
                                       /*strength=*/100, /*armor_ix=*/0, /*buildup=*/0.0,
                                       /*instant=*/true);
    REQUIRE(fac >= 0);
    // Enemy (house 1) factory that house 0's spy infiltrates -> produced_veteran_[0].
    const int enemy_fac = world.add_building(0, 40, 40, 1, 1, /*house=*/1, 100, 0, 0.0, true);
    REQUIRE(enemy_fac >= 0);
    using P = ra2engine::World::InfiltratePurpose;
    CHECK(world.infiltrate_building(enemy_fac, /*spy_house=*/0, P::Factory, 0.5, 1000));
    CHECK(world.produced_veteran(0));
    ra2engine::BuildingTypeDef fdef; fdef.id = "GAWEAP"; fdef.fw = fdef.fh = 1;
    ra2engine::UnitCombat c;
    c.image = "E1"; c.strength = 100; c.armor = "none"; c.damage = 40;
    const int ui = world.launch_unit(fac, fdef, c);
    REQUIRE(ui >= 0);
    CHECK(world.units()[ui].veteran_rank == 1);   // produced as a veteran
    CHECK(world.units()[ui].w_damage == 44);      // 40 * 1.1 (VeteranCombat)
    CHECK(world.units()[ui].max_hp == 150);       // 100 * 1.5 (VeteranArmor)
}

TEST_CASE("World disguise: a dog reveals a disguised enemy spy; non-detector does not "
          "[RE: capture.md §C4]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    ra2engine::UnitCombat spyc = gi_stats();
    spyc.image = "SPY"; spyc.strength = 100; spyc.armor = "flak";
    world.add_combat_unit(10, 10, /*house=*/0, spyc);
    const int spy_i = (int)world.units().size() - 1;
    world.set_disguise(spy_i, "E2", /*as_house=*/1);
    CHECK(world.units()[spy_i].disguised);
    // A non-detector enemy nearby does NOT reveal.
    ra2engine::UnitCombat gic = gi_stats();
    gic.image = "E1"; gic.strength = 100; gic.armor = "none";
    world.add_combat_unit(11, 10, /*house=*/1, gic);
    const double dt = 1.0 / 15.0;
    world.step(dt);
    CHECK(world.units()[spy_i].disguised);             // still disguised
    // A DOG (detect_disguise) within range reveals.
    ra2engine::UnitCombat dogc = gi_stats();
    dogc.image = "DOG"; dogc.strength = 100; dogc.armor = "none";
    world.add_combat_unit(12, 10, /*house=*/1, dogc);
    world.units().back().detect_disguise = true;
    world.step(dt);
    // W7-01-02 [RE: 0x6F71D0]: no radius, no flag clear -- the dog simply
    // ignores the disguise in its own scan; it still fools the house it copies.
    CHECK(world.units()[spy_i].disguised);
    CHECK(world.disguise_fools(1, world.units()[spy_i]));
}

TEST_CASE("World disguise: spy STAYS disguised on attack; only a DetectDisguise dog reveals it "
          "[RE: capture.md §C4 REVERSAL]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    ra2engine::UnitCombat spyc = gi_stats();
    spyc.image = "SPY"; spyc.strength = 100; spyc.armor = "flak";
    spyc.damage = 10; spyc.range = 5.0; spyc.rof = 10;
    world.add_combat_unit(10, 10, /*house=*/0, spyc);
    const int spy_i = (int)world.units().size() - 1;
    world.set_disguise(spy_i, "E2", 1);                   // sets capture_any_
    ra2engine::UnitCombat foe = gi_stats();
    foe.image = "E1"; foe.strength = 100; foe.armor = "none";
    world.add_combat_unit(12, 10, /*house=*/1, foe);
    ra2engine::Selection tsel_;
    tsel_.set(spy_i);
    world.group_attack(0, tsel_.ids(), (int)world.units().size() - 1);    // spy attacks the foe
    world.step(1.0 / 15.0);
    CHECK(world.units()[spy_i].disguised);                // PermaDisguise: attacking does NOT reveal
    // A DetectDisguise dog within range reveals it (the ONLY defeat).
    ra2engine::UnitCombat dogc = gi_stats();
    dogc.image = "DOG"; dogc.strength = 100; dogc.armor = "none";
    ra2engine::Unit& dog = world.add_combat_unit(11, 10, /*house=*/1, dogc);
    dog.detect_disguise = true;
    world.step(1.0 / 15.0);
    CHECK(world.units()[spy_i].disguised);                // W7-01-02: the dog does not clear it
}

TEST_CASE("World disguise: an idle enemy unit does NOT auto-acquire a disguised spy, "
          "then DOES once revealed [RE: capture.md §Design 6]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    // House-0 spy with NO weapon (range 0) so it never fires -- isolates the
    // ENEMY's auto-acquire path as the only way the enemy could target it.
    ra2engine::UnitCombat spyc = gi_stats();
    spyc.image = "SPY"; spyc.strength = 100; spyc.armor = "flak";
    spyc.damage = 0; spyc.range = 0.0;
    world.add_combat_unit(10, 10, /*house=*/0, spyc);
    const int spy_i = (int)world.units().size() - 1;
    world.set_disguise(spy_i, "E2", /*as_house=*/1);
    CHECK(world.units()[spy_i].disguised);
    // Idle enemy (house 1) combat unit ADJACENT to the spy (range 4.0 covers it).
    ra2engine::UnitCombat foe = gi_stats();
    foe.image = "E1"; foe.strength = 100; foe.armor = "none";
    world.add_combat_unit(11, 10, /*house=*/1, foe);
    const int foe_i = (int)world.units().size() - 1;
    const double dt = 1.0 / 15.0;
    world.step(dt);
    // Disguised -> treated as friendly -> NOT auto-acquired (no target set).
    CHECK(world.units()[foe_i].target == -1);
    // Reveal (as a dog's DetectDisguise would) -> now a valid target again.
    world.clear_disguise(spy_i);
    CHECK_FALSE(world.units()[spy_i].disguised);
    world.step(dt);
    CHECK(world.units()[foe_i].target == spy_i);          // auto-acquired once revealed
}

TEST_CASE("World shroud: add_combat_unit copies sight; seen_by_mask defaults 0xFF [RE: shroud.md §2]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    ra2engine::UnitCombat c;
    c.image = "E1"; c.strength = 100; c.armor = "none"; c.sight = 5;
    ra2engine::Unit& u = world.add_combat_unit(5, 5, /*house=*/0, c);
    CHECK(u.sight == 5);
    CHECK(u.seen_by_mask == 0xFFFFFFFFu);   // INERT default = seen by all houses (fv7c: 32-bit)
}

TEST_CASE("World shroud: step_visibility reveals around a unit; sticky fog; inert when off [RE: shroud.md §3/§4]") {
    // NOTE (deviation from brief): the brief calls a `make_test_world()` helper
    // for a hand-built >=12x12 map, but MapFile has no way to inject cells
    // (cells_ is private, only fillable via MapFile::parse from a real
    // [IsoMapPack5], and no LZO ENCODER exists in-tree to synthesize one). So
    // this uses the SAME real-map fixture every other World integration test in
    // this file uses; the visibility math itself is host-covered by
    // test_visibility.cpp. The full suite (Step 6) / gates run with
    // RA2_INSTALL_DIR set, so this executes rather than skips.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    REQUIRE(world.grid_w() > 12);
    REQUIRE(world.grid_h() > 12);
    ra2engine::UnitCombat c;
    c.image = "E1"; c.strength = 100; c.armor = "none"; c.sight = 5;
    world.add_combat_unit(6, 6, /*house=*/0, c);
    // Shroud OFF: accessors report all-visible (inert).
    CHECK(world.visible_at(0, 0, 0));
    CHECK(world.explored_at(0, 0, 0));
    // Enable + tick.
    world.enable_shroud(true);
    world.step_visibility();
    CHECK(world.visible_at(0, 6, 6));                     // source cell visible
    CHECK(world.explored_at(0, 6, 6));
    CHECK_FALSE(world.visible_at(0, 0, 0));               // far corner shrouded
    CHECK_FALSE(world.explored_at(0, 0, 0));
    CHECK(world.units()[0].seen_by_mask == 0x01);         // seen only by house 0
    // Move the unit far away, tick again: old cell stays EXPLORED (fog) but not
    // VISIBLE (fog no-regrow §4).
    const int ox = 6, oy = 6;
    world.units()[0].cell_x = 1; world.units()[0].cell_y = 1;  // teleport for the test
    world.step_visibility();
    CHECK(world.explored_at(0, ox, oy));                  // still explored (sticky)
    CHECK_FALSE(world.visible_at(0, ox, oy));             // no longer visible
    CHECK(world.visible_at(0, 1, 1));                     // new location visible
}

TEST_CASE("World shroud: fogged enemy is not auto-acquired; visible enemy is [RE: shroud.md §6]") {
    // Drives the ACTUAL step_combat auto-acquire scan (via world.step): the new
    // targeting clause skips a candidate whose cell is not seen_by_mask-visible
    // to the scanning house. The load-bearing assertion is scanner.target: it
    // FAILS before the clause exists (a fogged, in-range foe would be acquired).
    // Uses the same real-map fixture the other World shroud tests use; the gates
    // and full suite run with RA2_INSTALL_DIR set, so this executes.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    REQUIRE(world.grid_w() > 16);
    REQUIRE(world.grid_h() > 8);
    // Scanner: house 0, SMALL sight (3) but LARGE weapon range (6.0) -> there is
    // a band that is in weapon range yet outside its own sight (fogged).
    ra2engine::UnitCombat sc;
    sc.image = "E1"; sc.strength = 200; sc.armor = "none"; sc.sight = 3;
    sc.damage = 40; sc.rof = 20; sc.range = 6.0; sc.verses = {100};
    world.add_combat_unit(6, 6, /*house=*/0, sc);
    const int scan_i = (int)world.units().size() - 1;
    // Enemy at dx=5: in weapon range (5<=6) but OUTSIDE sight (25>9) -> fogged
    // for house 0 (only the scanner reveals for house 0).
    ra2engine::UnitCombat en;
    en.image = "E2"; en.strength = 100; en.armor = "none"; en.sight = 3;
    world.add_combat_unit(11, 6, /*house=*/1, en);
    const int foe_i = (int)world.units().size() - 1;
    world.enable_shroud(true);
    const double dt = 1.0 / 15.0;
    world.step(dt);                                       // step_visibility + step_combat
    CHECK_FALSE(world.visible_at(0, 11, 6));              // foe cell fogged for house 0
    CHECK((world.units()[foe_i].seen_by_mask & 0x01) == 0);
    CHECK(world.units()[scan_i].target == -1);           // clause: fogged foe NOT acquired
    // Move the foe INTO sight+range (dx=3: in range 3<=6, in sight 9<=9).
    world.units()[foe_i].cell_x = 9; world.units()[foe_i].cell_y = 6;
    world.units()[foe_i].fx = 9;    world.units()[foe_i].fy = 6;   // fx/fy are cell-space
    world.step(dt);
    CHECK(world.visible_at(0, 9, 6));                     // now visible to house 0
    CHECK((world.units()[foe_i].seen_by_mask & 0x01) != 0);
    CHECK(world.units()[scan_i].target == foe_i);        // visible foe IS auto-acquired
}

TEST_CASE("World shroud: house index >= 8 auto-acquires a seen enemy (32-bit seen_by_mask) [RE: FUN_004F98B0]") {
    // fv7c (13b #1): seen_by_mask was uint8_t and only stamped for houses < 8,
    // so `mask & (1u << 10)` was always 0 and ALL04's PlayerAlt2 (house 10)
    // never auto-acquired a unit. The original's house masks are 32-bit.
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    REQUIRE(world.grid_w() > 16);
    for (int h = 0; h <= 10; ++h)
        world.set_house(h, {ra2engine::World::Side::Allied, h & 7, "", h, true});
    REQUIRE(world.house_count() == 11);
    ra2engine::UnitCombat sc;
    sc.image = "E1"; sc.strength = 200; sc.armor = "none"; sc.sight = 5;
    sc.damage = 40; sc.rof = 20; sc.range = 6.0; sc.verses = {100};
    world.add_combat_unit(6, 6, /*house=*/10, sc);
    const int scan_i = (int)world.units().size() - 1;
    ra2engine::UnitCombat en;
    en.image = "E2"; en.strength = 100; en.armor = "none"; en.sight = 3;
    world.add_combat_unit(9, 6, /*house=*/1, en);
    const int foe_i = (int)world.units().size() - 1;
    world.enable_shroud(true);
    world.step(1.0 / 15.0);
    CHECK((world.units()[foe_i].seen_by_mask & (1u << 10)) != 0);   // stamped for house 10
    CHECK(world.units()[foe_i].seen_by_house(10));
    CHECK(world.units()[scan_i].target == foe_i);   // house 10 acquires the visible foe
}

TEST_CASE("World shroud: reshroud_house clears the victim's explored map [RE: shroud.md §7B]") {
    // NOTE (deviation from brief): the brief calls a hand-built `make_test_world()`
    // helper; MapFile cannot inject cells, so this reuses the real-map fixture
    // every other World shroud test uses (executes with RA2_INSTALL_DIR set).
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    REQUIRE(world.grid_w() > 8);
    REQUIRE(world.grid_h() > 8);
    ra2engine::UnitCombat c;
    c.image = "E2"; c.strength = 100; c.armor = "none"; c.sight = 5;
    world.add_combat_unit(6, 6, /*house=*/1, c);           // enemy scout
    world.enable_shroud(true);
    world.step_visibility();
    CHECK(world.explored_at(1, 6, 6));                     // enemy explored its start
    world.reshroud_house(1);                               // spy sabotages enemy radar
    CHECK_FALSE(world.explored_at(1, 6, 6));               // victim map wiped
    CHECK_FALSE(world.visible_at(1, 6, 6));
}

TEST_CASE("World shroud: SpySat building reveals the whole map + protects vs sabotage [RE: shroud.md §7A/§7B]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    REQUIRE(world.grid_w() > 8);
    REQUIRE(world.grid_h() > 8);
    world.enable_shroud(true);
    // House 0 owns a SpySat building (grants_map_reveal=true).
    const int b = world.add_building(/*type=*/0, /*cx=*/2, /*cy=*/2, /*fw=*/1, /*fh=*/1,
                                     /*house=*/0, /*strength=*/100, /*armor_ix=*/0,
                                     /*buildup_total=*/0.0, /*instant=*/true,
                                     /*sight=*/4, /*grants_map_reveal=*/true);
    REQUIRE(b >= 0);
    world.step_visibility();
    CHECK(world.explored_at(0, 0, 0));                     // whole map revealed
    CHECK(world.explored_at(0, world.grid_w() - 1, world.grid_h() - 1));
    world.reshroud_house(0);                               // sabotage SUPPRESSED (latched)
    CHECK(world.explored_at(0, 0, 0));                     // still revealed
}

// --- Multi-house / Alliances phase, Task 2 [RE: multihouse.md §1] -----------
// NOTE (deviation from brief): the brief calls a `make_test_world()` helper, but
// no such helper exists in-tree and MapFile cannot inject cells. HouseRec and its
// accessors are map-independent (pure table + derivation), so these use a
// default-constructed MapFile -> host-only, no RA2_INSTALL_DIR needed.
TEST_CASE("World multihouse: HouseRec defaults reproduce today's 0/1/2 world [RE: multihouse.md §1]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    CHECK(world.house_count() == 3);
    // Defaults (houses_configured_ == false -> today's derivation):
    CHECK(world.house_side(0) == ra2engine::World::Side::Allied);
    CHECK(world.house_side(1) == ra2engine::World::Side::Soviet);   // the hashed spawn branch
    CHECK(world.house_side(2) == ra2engine::World::Side::Allied);   // !=1 -> Allied
    CHECK(world.house_combatant(0));
    CHECK(world.house_combatant(1));
    CHECK_FALSE(world.houses_configured());
    // alliance_team sentinel resolves to own id (FFA) before configure:
    CHECK(world.house_team(0) == 0);
    CHECK(world.house_team(1) == 1);
    CHECK(world.house_team(2) == 2);
}

TEST_CASE("World multihouse: set_house opens the firewall + grows the table [RE: multihouse.md §1/D1]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    // Configure houses 0+3 onto team 7 (FFA otherwise), house 3 is new (grows).
    world.set_house(0, {ra2engine::World::Side::Allied, 5, "Americans", 7, true});
    world.set_house(3, {ra2engine::World::Side::Soviet, 1, "Russians", 7, true});
    CHECK(world.houses_configured());
    CHECK(world.house_count() == 4);
    CHECK(world.house_side(3) == ra2engine::World::Side::Soviet);
    CHECK(world.house_team(0) == 7);
    CHECK(world.house_team(3) == 7);          // shared team
    CHECK(world.house_side(0) == ra2engine::World::Side::Allied);
}

// --- Multi-house / Alliances phase, Task 3 [RE: multihouse.md §D2] ----------
// The six per-house [3] arrays are now std::vector sized house_count() (default
// 3). Parity at indices 0/1/2 + out-of-range reject; growth via set_house.
// NOTE (deviation from brief): uses a default-constructed MapFile (no
// make_test_world() helper in-tree), matching the Task 2 cases above.
TEST_CASE("World multihouse: six per-house arrays vectorized, parity at 0/1/2 [RE: multihouse.md §D2]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    CHECK(world.house_count() == 3);
    // house_credits read/write parity at 0/1/2 + out-of-range reject.
    world.grant_credits_for_test(0, 100.0);
    world.grant_credits_for_test(1, 250.0);
    world.grant_credits_for_test(2, 7.0);
    CHECK(world.house_credits(0) == doctest::Approx(100.0));
    CHECK(world.house_credits(1) == doctest::Approx(250.0));
    CHECK(world.house_credits(2) == doctest::Approx(7.0));
    CHECK(world.house_credits(3) == doctest::Approx(0.0));   // out of range -> 0
    CHECK(world.house_credits(-1) == doctest::Approx(0.0));
    // take_income zeros only the taken index; neutral has no income path.
    CHECK(world.take_income(0) == doctest::Approx(100.0));
    CHECK(world.house_credits(0) == doctest::Approx(0.0));
    CHECK(world.take_income(3) == doctest::Approx(0.0));     // >= house_count() -> 0
}

TEST_CASE("World multihouse: per-house arrays grow with set_house [RE: multihouse.md §D2]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_house(4, {ra2engine::World::Side::Soviet, 1, "Russians", 4, true});
    CHECK(world.house_count() == 5);
    world.grant_credits_for_test(4, 42.0);                   // index 4 now valid
    CHECK(world.house_credits(4) == doctest::Approx(42.0));
    CHECK(world.take_income(4) == doctest::Approx(42.0));    // widened bound
}

// --- Multi-house / Alliances phase, Task 4 [RE: multihouse.md §3/§4] ---------
// is_enemy/are_allied truth table. NOTE (deviation from brief): the brief calls a
// `make_test_world()` helper; no such helper exists in-tree and the predicates are
// map-independent (pure HouseRec table logic), so this uses a default-constructed
// MapFile (host-only, no RA2_INSTALL_DIR needed), matching the Task 2/3 cases.
TEST_CASE("World multihouse: is_enemy/are_allied truth table [RE: multihouse.md §3/§4]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    // (a) Default (houses_configured_ == false): is_enemy == a!=b, are_allied == a==b.
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            CHECK(world.is_enemy(a, b)   == (a != b));   // the byte-identity contract
            CHECK(world.are_allied(a, b) == (a == b));
        }
    // (b) Configured FFA: every house its own team.
    world.set_house(0, {ra2engine::World::Side::Allied, 5, "", 0, true});
    world.set_house(1, {ra2engine::World::Side::Soviet, 1, "", 1, true});
    world.set_house(2, {ra2engine::World::Side::Neutral, 3, "", 2, false});
    CHECK(world.is_enemy(0, 1));                          // cross-team combatants
    CHECK(world.is_enemy(1, 0));
    CHECK_FALSE(world.is_enemy(0, 0));                    // self never enemy
    CHECK_FALSE(world.are_allied(0, 1));
    // (c) Neutral / non-combatant: never enemy of anyone, both directions.
    CHECK_FALSE(world.is_enemy(0, 2));
    CHECK_FALSE(world.is_enemy(2, 0));
    CHECK_FALSE(world.is_enemy(1, 2));
    CHECK_FALSE(world.are_allied(0, 2));                  // not allied to a combatant
    CHECK(world.are_allied(2, 2));                        // self-ally still holds
    // (d) Configured team: 0+3 allied vs 1.
    world.set_house(3, {ra2engine::World::Side::Soviet, 6, "", 0, true});   // team 0 with house 0
    world.set_house(0, {ra2engine::World::Side::Allied, 5, "", 0, true});   // ensure team 0
    CHECK(world.are_allied(0, 3));                        // teammates
    CHECK_FALSE(world.is_enemy(0, 3));                    // allies never fight
    CHECK(world.is_enemy(0, 1));                          // cross-team enemy
    CHECK(world.is_enemy(3, 1));
}

// --- Multi-house / Alliances phase, Task 5 [RE: multihouse.md §3] ------------
// AllyReveal: allied houses share revealed shroud. NOTE (deviation from brief):
// the brief calls a hand-built `make_test_world()` helper; no such helper exists
// in-tree and MapFile cannot inject cells, so these use the SAME real-map fixture
// every other World shroud test uses (executes with RA2_INSTALL_DIR set).
TEST_CASE("World multihouse: AllyReveal shares explored/visible between allies [RE: multihouse.md §3]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    REQUIRE(world.grid_w() > 16);
    REQUIRE(world.grid_h() > 16);
    // Houses 0 and 1 allied (team 0); house 1 has no vision source of its own here.
    world.set_house(0, {ra2engine::World::Side::Allied, 5, "", 0, true});
    world.set_house(1, {ra2engine::World::Side::Allied, 6, "", 0, true});   // ally of 0
    world.set_house(2, {ra2engine::World::Side::Soviet, 1, "", 2, true});   // enemy
    world.enable_shroud(true);
    world.enable_ally_reveal(true);
    ra2engine::UnitCombat c; c.image = "E1"; c.strength = 100; c.armor = "none"; c.sight = 5;
    world.add_combat_unit(8, 8, /*house=*/0, c);              // only house 0 has a scout
    world.step_visibility();
    CHECK(world.visible_at(0, 8, 8));                         // house 0 sees its scout
    CHECK(world.visible_at(1, 8, 8));                         // ally house 1 SHARES it
    CHECK(world.explored_at(1, 8, 8));
    CHECK_FALSE(world.visible_at(2, 8, 8));                   // enemy does NOT share
}

// P5 [RE: FUN_00566fd0]: the share is ONE hop from the source's own sight --
// house 2 allied with 1 and 1 allied with 0 does not hand 2's sight to 0, even
// over many frames; and the source's own Allies view decides (one-sided).
TEST_CASE("World multihouse: AllyReveal is one hop, from the source's own view [RE: FUN_00566fd0]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    for (int h = 0; h < 4; ++h) world.set_house(h, {ra2engine::World::Side::Allied, h + 1, "", h, true});
    world.set_ally_view(0, 1, true); world.set_ally_view(1, 0, true);   // 0 <-> 1
    world.set_ally_view(1, 2, true); world.set_ally_view(2, 1, true);   // 1 <-> 2 (not 0)
    world.set_ally_view(3, 0, true);                                     // 3 -> 0 only
    world.enable_shroud(true);
    world.enable_ally_reveal(true);
    ra2engine::UnitCombat c; c.image = "E1"; c.strength = 100; c.armor = "none"; c.sight = 3;
    world.add_combat_unit(8, 8, /*house=*/2, c);
    world.add_combat_unit(30, 30, /*house=*/3, c);
    for (int k = 0; k < 4; ++k) world.step_visibility();
    CHECK(world.explored_at(1, 8, 8));          // direct ally of the source
    CHECK_FALSE(world.explored_at(0, 8, 8));    // no chain 2 -> 1 -> 0
    CHECK(world.explored_at(0, 30, 30));        // 3 counts 0 as an ally: 0 sees it
    CHECK_FALSE(world.explored_at(1, 30, 30));
}

TEST_CASE("World multihouse: AllyReveal inert when unconfigured (shroud gate contract) [RE: multihouse.md §3]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    ra2engine::World world(map);
    REQUIRE(world.grid_w() > 16);
    REQUIRE(world.grid_h() > 16);
    world.enable_shroud(true);                                // shroud on, alliances NOT configured
    ra2engine::UnitCombat c; c.image = "E1"; c.strength = 100; c.armor = "none"; c.sight = 5;
    world.add_combat_unit(8, 8, /*house=*/0, c);
    world.step_visibility();
    CHECK(world.visible_at(0, 8, 8));
    CHECK_FALSE(world.visible_at(1, 8, 8));                   // no sharing (houses_configured_ false)
}

// --- Multi-house / Alliances phase, Task 6 [RE: multihouse.md §5] ------------
// Last-team-standing victory/defeat. ShortGame=yes -> a combatant house is
// defeated when its last BUILDING is destroyed; a whole alliance-team wins
// together; neutral/non-combatant houses never win or lose. NOTE (deviation
// from brief): the brief calls a `make_test_world()` helper; none exists
// in-tree. add_building/add_combat_unit are map-independent here (foundation
// cells + occupancy clamp to grid bounds), so this uses a default-constructed
// MapFile like the Task 2/3/4 cases, and kills a building via
// ra2engine::kill_building (the helper every other building-death test uses)
// instead of the brief's buildings_mut().
TEST_CASE("World multihouse: last-team-standing victory (ShortGame) [RE: multihouse.md §5]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    // Teams: 0+2 (team 0) vs 1 (team 1); house 3 neutral non-combatant.
    world.set_house(0, {ra2engine::World::Side::Allied,  5, "", 0, true});
    world.set_house(1, {ra2engine::World::Side::Soviet,  1, "", 1, true});
    world.set_house(2, {ra2engine::World::Side::Allied,  6, "", 0, true});
    world.set_house(3, {ra2engine::World::Side::Neutral, 3, "", 3, false});
    world.enable_victory(true, /*short_game=*/true);
    // Give each combatant a building + the neutral one.
    world.add_building(0, 2,  2,  1, 1, /*house=*/0, 100, 0, 0.0, true);
    world.add_building(0, 20, 2,  1, 1, /*house=*/1, 100, 0, 0.0, true);
    world.add_building(0, 2,  20, 1, 1, /*house=*/2, 100, 0, 0.0, true);
    world.add_building(0, 20, 20, 1, 1, /*house=*/3, 100, 0, 0.0, true);
    world.step_victory();
    CHECK_FALSE(world.house_defeated(0));
    CHECK_FALSE(world.house_defeated(1));
    CHECK(world.winning_team() == -1);             // 2 combatant teams alive
    CHECK_FALSE(world.house_defeated(3));           // neutral never defeated
    // Destroy house 1's only building -> team 1 defeated -> team 0 wins.
    for (auto& b : world.buildings())
        if (b.house == 1) ra2engine::kill_building(b);
    world.step_victory();
    CHECK(world.house_defeated(1));
    CHECK_FALSE(world.house_defeated(0));
    CHECK_FALSE(world.house_defeated(2));
    CHECK(world.winning_team() == 0);              // last team standing = team 0
}

// ShortGame=no: a house is defeated only with zero buildings AND zero units;
// a neutral house never wins even as the sole "survivor".
TEST_CASE("World multihouse: ShortGame=no needs zero units too; neutral never wins [RE: multihouse.md §5]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_house(0, {ra2engine::World::Side::Allied,  5, "", 0, true});
    world.set_house(1, {ra2engine::World::Side::Soviet,  1, "", 1, true});
    world.set_house(2, {ra2engine::World::Side::Neutral, 3, "", 2, false});
    world.enable_victory(true, /*short_game=*/false);  // full game: buildings AND units
    // House 0 keeps a building; house 1 has NO building but one surviving unit.
    world.add_building(0, 2, 2, 1, 1, /*house=*/0, 100, 0, 0.0, true);
    ra2engine::UnitCombat uc; uc.image = "E1"; uc.strength = 100; uc.armor = "none";
    world.add_combat_unit(10, 10, /*house=*/1, uc);
    world.step_victory();
    CHECK_FALSE(world.house_defeated(1));   // ShortGame=no: a loose unit keeps house 1 alive
    CHECK(world.winning_team() == -1);      // two combatant teams still stand
    // Kill house 1's last unit -> zero buildings AND zero units -> defeated.
    for (auto& u : world.units())
        if (u.house == 1) u.alive = false;
    world.step_victory();
    CHECK(world.house_defeated(1));
    CHECK(world.winning_team() == 0);       // team 0 sole combatant survivor
    CHECK_FALSE(world.house_defeated(2));   // neutral (no buildings/units) never defeated
}

// --- Multi-house / Alliances phase, Task 7 [RE: multihouse.md §1/§2/D6] ------
// Per-house SIDE (drives spawn_base ConYard/base + unit TYPE) and COLOR (drives
// the render palette). NOTE (deviation from brief): the brief calls a
// `make_test_world()` helper; none exists in-tree, and house_side/house_color
// are map-independent (pure HouseRec table logic), so this uses a
// default-constructed MapFile (host-only, no RA2_INSTALL_DIR), matching Tasks
// 2-6. The actual spawn-TYPE byte-identity is proven empirically by the base/
// capture/econ/ai-* gates (Step 5); this locks the accessor defaults + colors.
TEST_CASE("World multihouse: house_side drives spawn side; defaults reproduce today [RE: multihouse.md §1/D6]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    // Default (firewall off): house 1 -> Soviet, else Allied (the hashed branch).
    // Soviet -> NACNST/NA* base + E2 unit; Allied -> GACNST + E1 (main.cpp routes
    // through house_side): these defaults keep the spawned Building{type} identical.
    CHECK(world.house_side(0) == ra2engine::World::Side::Allied);
    CHECK(world.house_side(1) == ra2engine::World::Side::Soviet);
    CHECK(world.house_side(2) == ra2engine::World::Side::Allied);
    // Configured: house 0 can be Soviet, house 1 Allied (fully table-driven).
    world.set_house(0, {ra2engine::World::Side::Soviet, 1, "Russians", 0, true});
    world.set_house(1, {ra2engine::World::Side::Allied, 5, "Americans", 1, true});
    CHECK(world.house_side(0) == ra2engine::World::Side::Soviet);
    CHECK(world.house_side(1) == ra2engine::World::Side::Allied);
}

// pal_for_color: the 8 MP-color remap (HSV->RGB spread across the reserved
// house-color band 16..31, like pal_for_house). Render-only, NOT hashed.
TEST_CASE("pal_for_color: 8 MP colors are distinct + only the 16..31 band moves [RE: multihouse.md §2]") {
    const ra2assets::Palette src = make_test_pal();
    // Each of the 8 valid color indices produces a remapped palette; the ramp
    // entry 20 (brightness 100 -> a mid Value) must differ pairwise across colors.
    std::array<ra2assets::Rgba, 8> band{};
    for (int c = 0; c < 8; ++c) {
        const ra2assets::Palette out = ra2engine::pal_for_color(src, c);
        band[c] = out[20];
        // Non-ramp indices are untouched (mirrors pal_for_house's band-only rule).
        CHECK(out[5].r == src[5].r);
        CHECK(out[5].g == src[5].g);
        CHECK(out[5].b == src[5].b);
        CHECK(out[32].r == src[32].r);
        CHECK(out[20].a == src[20].a);   // alpha preserved
    }
    for (int a = 0; a < 8; ++a)
        for (int b = a + 1; b < 8; ++b)
            CHECK((band[a].r != band[b].r || band[a].g != band[b].g ||
                   band[a].b != band[b].b));   // 8 pairwise-distinct colors
    // Out-of-range color index -> palette returned unchanged.
    const ra2assets::Palette none = ra2engine::pal_for_color(src, 8);
    for (int i = 0; i < 256; ++i) {
        CHECK(none[i].r == src[i].r);
        CHECK(none[i].g == src[i].g);
        CHECK(none[i].b == src[i].b);
    }
}

// --- Repair & Sell phase, Task 4 [RE: repair-sell.md §A2/§A3/§A4/§C1] -------
// These cases run on a DEFAULT MapFile (0x0 grid): add_building's foundation
// write, step_units/step_combat and OreField::step are all bounds-guarded on an
// empty grid, and the repair tick itself is pure Building/BuildingTypeDef
// arithmetic -- so no RA2_INSTALL_DIR is needed and nothing can silently skip.
namespace {

// The roster the repair cases price against. Indices are load-bearing:
//   0 GACNST-like  Cost 3000 / Strength 1000 -> stepCost 3   [RE: §D9 measured]
//   1 GAPOWR-like  Cost  800 / Strength  750 -> stepCost 1   [RE: §D0.1]
//   2 ClickRepairable=no (the §A4 type opt-out)
std::vector<ra2engine::BuildingTypeDef> repair_roster() {
    std::vector<ra2engine::BuildingTypeDef> t(3);
    t[0].id = "GACNST"; t[0].cost = 3000; t[0].strength = 1000;
    t[1].id = "GAPOWR"; t[1].cost = 800;  t[1].strength = 750;
    t[2].id = "NOCLICK"; t[2].cost = 1000; t[2].strength = 1000;
    t[2].click_repairable = false;
    return t;
}

// set_economy is what populates World::types_ -- repair needs Cost/Strength to
// price a step, so a World that never called it refuses every wrench click.
// Growth parked at 1000 s so the short runs below never tick ore.
void wire_repair_economy(ra2engine::World& world) {
    world.set_economy(repair_roster(), {}, ra2engine::OreValues{}, /*growth_interval*/ 1000.0,
                      /*growth_pct*/ 0.0, /*spread_pct*/ 0.0);
}

}  // namespace

TEST_CASE("world: step_repair_sell is a strict no-op with the firewall closed [RE: repair-sell.md]") {
    // A stock world nobody ever ordered to repair must be untouched by the new
    // pass -- this is the byte-identity guarantee in test form.
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    const int b = world.add_building(/*type*/ 0, 5, 5, /*fw*/ 5, /*fh*/ 4, /*house*/ 0,
                                     /*strength*/ 1000, /*armor_ix*/ 0, /*buildup*/ 0.0,
                                     /*instant*/ true);
    world.buildings()[b].hp = 500;
    world.grant_credits_for_test(0, 10000.0);

    const int before_hp = world.buildings()[b].hp;
    const double before_credits = world.house_credits(0);
    for (int k = 0; k < 100; ++k) world.step(1.0 / 15.0);
    CHECK(world.buildings()[b].hp == before_hp);
    CHECK(world.house_credits(0) == doctest::Approx(before_credits));
    CHECK_FALSE(world.building_repairing(b));
    CHECK(world.repair_frame() == 100);        // the counter still advanced
}

// fv7c [RE: FUN_00450620 via BuildingClass::Update 0x43FAA0; repair-sell.md
// §A7]: a computer house's building is repaired only when the house IQ >=
// [IQ] RepairSell, money >= [AI] CreditReserve, the RepairDelay latch is clear
// and the building is AIRepairable ([Structures] field 16) or captured.
TEST_CASE("fv7c: AI building repair -- IQ, CreditReserve and AIRepairable gates (FUN_00450620)") {
    struct Case { int iq; double credits; bool repairable; bool captured; bool expect; int type = 0; };
    const Case cases[] = {
        {5, 10000.0, true, false, true},    // repaired
        {5, 10000.0, false, false, false},  // not AIRepairable (ALL04's Russian2 NAYARD)
        {5, 10000.0, false, true, true},    // captured counts
        {0, 10000.0, true, false, false},   // IQ below RepairSell (1)
        {5, 50.0, true, false, false},      // below CreditReserve (100)
        {5, 10000.0, true, false, true, 2}, // ClickRepairable=no: Repair(1) skips the cursor gate (NAPSYA)
    };
    for (const Case& c : cases) {
        ra2assets::MapFile map;
        ra2engine::World world(map);
        wire_repair_economy(world);
        ra2engine::World::CombatW7Rules r;
        r.armed = true;
        world.arm_combat_w7(r);
        world.set_house_iq(1, c.iq);
        world.set_house_repair_delay(1, 10);
        const int b = world.add_building(c.type, 5, 5, 5, 4, /*house*/ 1, 1000, 0, 0.0, true);
        world.buildings()[b].hp = 500;
        world.buildings()[b].ai_repairable = c.repairable;
        world.buildings()[b].has_been_captured = c.captured;
        world.grant_credits_for_test(1, c.credits);
        for (int k = 0; k < 15; ++k) world.step(1.0 / 15.0);
        INFO("iq=" << c.iq << " credits=" << c.credits << " repairable=" << c.repairable);
        CHECK(world.building_repairing(b) == c.expect);
    }
}

TEST_CASE("world: set_repair refuses per the recovered gates [RE: repair-sell.md §A4/§C1]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    const int e = world.add_building(0, 20, 5, 5, 4, /*house*/ 1, 1000, 0, 0.0, true);
    const int f = world.add_building(0, 35, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    const int u = world.add_building(0, 50, 5, 5, 4, /*house*/ 0, 1000, 0, /*buildup*/ 5.0,
                                     /*instant*/ false);
    const int nc = world.add_building(2, 65, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    world.buildings()[b].hp = 500;
    world.buildings()[e].hp = 500;
    world.buildings()[u].hp = 500;
    world.buildings()[nc].hp = 500;            // f stays at full health

    CHECK_FALSE(world.set_repair(-1, 0, 1));                 // bad index
    CHECK_FALSE(world.set_repair(999, 0, 1));                // bad index
    CHECK_FALSE(world.set_repair(e, 0, 1));                  // NOT your building
    CHECK_FALSE(world.set_repair(f, 0, 1));                  // nothing to repair
    CHECK_FALSE(world.set_repair(u, 0, 1));                  // under construction
    CHECK_FALSE(world.set_repair(nc, 0, 1));                 // ClickRepairable=no
    CHECK(world.set_repair(b, 0, 1));                        // arms
    CHECK(world.building_repairing(b));
    CHECK_FALSE(world.set_repair(b, 0, 1));                  // already on -> no change
    CHECK(world.toggle_repair(b, 0));                        // -1 toggles OFF
    CHECK_FALSE(world.building_repairing(b));
    CHECK(world.toggle_repair(b, 0));                        // -1 toggles ON
    CHECK(world.building_repairing(b));
    CHECK(world.set_repair(b, 0, 0));                        // 0 turns OFF
    CHECK_FALSE(world.building_repairing(b));
    CHECK_FALSE(world.set_repair(b, 0, 0));                  // already off -> no change
    // [§B2] a building already running the sell mission cannot be armed.
    world.buildings()[b].selling = true;
    CHECK_FALSE(world.set_repair(b, 0, 1));
    world.buildings()[b].selling = false;
    // A Dead building refuses both directions.
    world.buildings()[b].state = ra2engine::Building::State::Dead;
    CHECK_FALSE(world.set_repair(b, 0, 1));
    CHECK_FALSE(world.set_repair(b, 0, 0));
}

TEST_CASE("world: the repair tick is a 14-frame GLOBAL phase, +8 HP, charged [RE: §A2]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    world.buildings()[b].hp = 500;
    world.grant_credits_for_test(0, 10000.0);
    REQUIRE(world.set_repair(b, 0, 1));

    const int step_cost = ra2engine::repair_step_cost(3000, 1000, 8, 0.15);   // 3
    CHECK(step_cost == 3);
    std::vector<std::uint64_t> hit_frames;
    int prev_hp = world.buildings()[b].hp;
    double prev_credits = world.house_credits(0);
    for (int k = 0; k < 60; ++k) {
        const std::uint64_t f = world.repair_frame();
        world.step(1.0 / 15.0);
        if (world.buildings()[b].hp != prev_hp) {
            hit_frames.push_back(f);
            CHECK(world.buildings()[b].hp == prev_hp + 8);           // +8 flat
            CHECK(world.house_credits(0) == doctest::Approx(prev_credits - step_cost));
            prev_hp = world.buildings()[b].hp;
            prev_credits = world.house_credits(0);
        } else {
            CHECK(world.house_credits(0) == doctest::Approx(prev_credits));  // no idle drain
        }
    }
    REQUIRE(hit_frames.size() >= 4);
    for (std::size_t i = 1; i < hit_frames.size(); ++i)
        CHECK(hit_frames[i] - hit_frames[i - 1] == 14);               // NOT 15
    for (std::uint64_t f : hit_frames) CHECK(f % 14 == 0);            // an ABSOLUTE phase
}

TEST_CASE("world: repairing buildings step TOGETHER on the global phase [RE: §A2]") {
    // Two buildings armed on DIFFERENT frames still share one cadence: the
    // phase is `Frame % interval`, not a per-building timer.
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    const int b0 = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    const int b1 = world.add_building(1, 20, 5, 3, 3, /*house*/ 0, 750, 0, 0.0, true);
    world.buildings()[b0].hp = 500;
    world.buildings()[b1].hp = 300;
    world.grant_credits_for_test(0, 10000.0);

    REQUIRE(world.set_repair(b0, 0, 1));
    for (int k = 0; k < 5; ++k) world.step(1.0 / 15.0);   // frames 0..4 (0 fires for b0)
    REQUIRE(world.set_repair(b1, 0, 1));                  // armed mid-interval
    const int hp0 = world.buildings()[b0].hp;
    const int hp1 = world.buildings()[b1].hp;
    // Frames 5..13 are off-phase for BOTH; frame 14 steps BOTH at once.
    for (int k = 5; k < 14; ++k) world.step(1.0 / 15.0);
    CHECK(world.buildings()[b0].hp == hp0);
    CHECK(world.buildings()[b1].hp == hp1);
    const double credits_before = world.house_credits(0);
    world.step(1.0 / 15.0);                                // frame 14
    CHECK(world.buildings()[b0].hp == hp0 + 8);
    CHECK(world.buildings()[b1].hp == hp1 + 8);
    // GACNST 3000/1000 -> 3, GAPOWR 800/750 -> 1 [RE: §D9 measured table].
    CHECK(world.house_credits(0) == doctest::Approx(credits_before - 4.0));
}

TEST_CASE("world: repair auto-stops at full health [RE: §A2]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    world.buildings()[b].hp = world.buildings()[b].max_hp - 4;   // < one 8-HP step
    world.grant_credits_for_test(0, 10000.0);
    REQUIRE(world.set_repair(b, 0, 1));
    for (int k = 0; k < 30; ++k) world.step(1.0 / 15.0);
    CHECK(world.buildings()[b].hp == world.buildings()[b].max_hp);    // clamped, never over
    CHECK_FALSE(world.building_repairing(b));                        // cleared itself
    CHECK(world.house_credits(0) == doctest::Approx(10000.0 - 3.0));  // charged exactly once
}

TEST_CASE("world: out of credits STOPS repair silently, with no partial step [RE: §A3]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    world.buildings()[b].hp = 500;
    world.grant_credits_for_test(0, 2.0);                            // less than one step
    REQUIRE(world.set_repair(b, 0, 1));
    const int hp0 = world.buildings()[b].hp;
    for (int k = 0; k < 30; ++k) world.step(1.0 / 15.0);
    CHECK(world.buildings()[b].hp == hp0);                           // NO partial repair
    CHECK(world.house_credits(0) == doctest::Approx(2.0));           // and NO free repair
    CHECK_FALSE(world.building_repairing(b));                        // the flag was cleared
    // Refilling does NOT resume by itself -- the player must click again (§A3).
    world.grant_credits_for_test(0, 10000.0);
    for (int k = 0; k < 30; ++k) world.step(1.0 / 15.0);
    CHECK(world.buildings()[b].hp == hp0);
}

TEST_CASE("world: the engineer path clears the wrench flag (off-ramp 5) [RE: §A4/§A6]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    world.buildings()[b].hp = 500;
    world.grant_credits_for_test(0, 10000.0);
    REQUIRE(world.set_repair(b, 0, 1));
    CHECK(world.repair_building(b, 0));            // instant, free, separate path
    CHECK(world.buildings()[b].hp == world.buildings()[b].max_hp);
    CHECK_FALSE(world.building_repairing(b));
}

TEST_CASE("world: the repair tick reads the INJECTED rules, not RepairSellRules defaults "
          "[RE: repair-sell.md §A1/§D9]") {
    // THE SILENT-FAILURE GUARD. This install's rulesmd.ini values happen to EQUAL
    // the RepairSellRules struct defaults, so a World that ignored
    // set_repair_sell_rules entirely would still look correct here and be wrong
    // on any install whose [General] differs. Every number below is DELIBERATELY
    // NOT a default, and each of the three the tick consumes moves an OBSERVABLE:
    //   repair_rate    0.035 -> trunc(31.5) = 31-frame cadence (default: 14)
    //   repair_step    20    -> +20 HP per step                (default: +8)
    //   repair_percent 0.50  -> stepCost trunc((3000/(1000/20))*0.5) = 30 (default: 3)
    // Delete the wire-up and this case fails on all three.
    ra2assets::MapFile map;
    ra2engine::World world(map);
    wire_repair_economy(world);
    ra2engine::RepairSellRules r;
    r.repair_rate = 0.035;
    r.repair_step = 20;
    r.repair_percent = 0.50;
    world.set_repair_sell_rules(r);
    CHECK(world.repair_sell_rules().repair_step == 20);
    CHECK(world.repair_sell_rules().repair_rate == doctest::Approx(0.035));
    CHECK(ra2engine::repair_interval_frames(0.035) == 31);
    CHECK(ra2engine::repair_step_cost(3000, 1000, 20, 0.50) == 30);

    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    world.buildings()[b].hp = 100;
    world.grant_credits_for_test(0, 10000.0);
    REQUIRE(world.set_repair(b, 0, 1));

    std::vector<std::uint64_t> hit_frames;
    int prev_hp = world.buildings()[b].hp;
    double prev_credits = world.house_credits(0);
    for (int k = 0; k < 70; ++k) {
        const std::uint64_t f = world.repair_frame();
        world.step(1.0 / 15.0);
        if (world.buildings()[b].hp != prev_hp) {
            hit_frames.push_back(f);
            CHECK(world.buildings()[b].hp == prev_hp + 20);      // injected RepairStep
            CHECK(world.house_credits(0) == doctest::Approx(prev_credits - 30.0));
            prev_hp = world.buildings()[b].hp;
            prev_credits = world.house_credits(0);
        }
    }
    // The DEFAULT cadence (14) would have fired at 0/14/28/42/56 for +8 and -3.
    REQUIRE(hit_frames.size() == 3);
    CHECK(hit_frames[0] == 0);
    CHECK(hit_frames[1] == 31);
    CHECK(hit_frames[2] == 62);
    CHECK(world.buildings()[b].hp == 160);
    CHECK(world.house_credits(0) == doctest::Approx(10000.0 - 90.0));
}

TEST_CASE("world: set_building_types arms repair lookups WITHOUT the economy [RE: repair-sell.md §A4]") {
    // The interactive launcher never calls set_economy (types_ stays empty --
    // the economy firewall), so repair/sell must price against the lookup-only
    // roster instead. Before set_building_types: every wrench click refused.
    ra2assets::MapFile map;
    ra2engine::World world(map);
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 0.0, true);
    world.buildings()[b].hp = 500;
    world.grant_credits_for_test(0, 10000.0);
    CHECK_FALSE(world.set_repair(b, 0, 1));          // no roster -> refused
    world.set_building_types(repair_roster());
    CHECK(world.btypes().size() == 3);
    REQUIRE(world.set_repair(b, 0, 1));
    world.step(1.0 / 15.0);                          // frame 0 is on-phase
    CHECK(world.buildings()[b].hp == 508);
    CHECK(world.house_credits(0) == doctest::Approx(10000.0 - 3.0));
}

// --- Repair & Sell phase, Task 5: the sell mission [RE: repair-sell.md §B1/§B2/§B3/§B5] ---
namespace {

// The roster the sell cases price against, values from the §D9 measured table:
//   0 GACNST  Cost 3000, Crewed=yes          -> refund 1500, survA 3 / survS 5
//   1 NAREFN  Cost 2000, Soylent=300, Crewed -> refund 300 (NOT 1000), surv 1
//   2 CAOILD  Unsellable=yes                 -> refused
//   3 GAPILL  Cost 500, Crewed=no            -> refund 250, 0 survivors
std::vector<ra2engine::BuildingTypeDef> sell_roster() {
    std::vector<ra2engine::BuildingTypeDef> t(4);
    t[0].id = "GACNST"; t[0].cost = 3000; t[0].strength = 1000; t[0].crewed = true;
    t[1].id = "NAREFN"; t[1].cost = 2000; t[1].strength = 1000; t[1].crewed = true;
    t[1].soylent = 300;
    t[2].id = "CAOILD"; t[2].cost = 0; t[2].strength = 1000; t[2].unsellable = true;
    t[3].id = "GAPILL"; t[3].cost = 500; t[3].strength = 400; t[3].crewed = false;
    return t;
}

// Step until the mission on b ends (or a 200-frame cap); returns frames run.
int run_sell(ra2engine::World& world, int b) {
    int frames = 0;
    while (world.building_selling(b) && frames < 200) {
        world.step(1.0 / 15.0);
        ++frames;
    }
    return frames;
}

}  // namespace

TEST_CASE("world: begin_sell refuses per the recovered gates [RE: repair-sell.md §B5/§C1]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_building_types(sell_roster());
    const int b   = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 3.6, true);
    const int e   = world.add_building(0, 20, 5, 5, 4, /*house*/ 1, 1000, 0, 3.6, true);
    const int u   = world.add_building(0, 35, 5, 5, 4, /*house*/ 0, 1000, 0, 3.6, /*instant*/ false);
    const int oil = world.add_building(2, 50, 5, 2, 2, /*house*/ 0, 1000, 0, 3.6, true);
    const int cy  = world.add_building(0, 65, 5, 5, 4, /*house*/ 0, 1000, 0, 3.6, true);

    CHECK_FALSE(world.begin_sell(-1, 0));                    // bad index
    CHECK_FALSE(world.begin_sell(999, 0));                   // bad index
    CHECK_FALSE(world.begin_sell(e, 0));                     // NOT your building (house 1)
    CHECK_FALSE(world.begin_sell(u, 0));                     // under construction
    CHECK_FALSE(world.begin_sell(oil, 0));                   // Unsellable=yes [§D0.3 CAOILD]
    CHECK_FALSE(world.begin_sell(b, 0, /*c4_applied=*/true));
    CHECK_FALSE(world.building_selling(b));
    CHECK(world.begin_sell(b, 0));                           // starts the mission
    CHECK(world.building_selling(b));
    CHECK_FALSE(world.begin_sell(b, 0));                     // already selling
    // CONFIRMED BY ABSENCE (§B5): a full-health Construction Yard sells -- there
    // is NO last-ConYard rule.
    CHECK(world.begin_sell(cy, 0));
}

TEST_CASE("world: selling cancels repair first [RE: repair-sell.md §B2 entry]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_building_types(sell_roster());
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 3.6, true);
    world.buildings()[b].hp = 500;
    world.grant_credits_for_test(0, 10000.0);
    REQUIRE(world.set_repair(b, 0, 1));
    REQUIRE(world.begin_sell(b, 0));
    CHECK_FALSE(world.building_repairing(b));
    CHECK_FALSE(world.set_repair(b, 0, 1));                  // and cannot be re-armed
}

TEST_CASE("world: the sell mission pays ONLY on completion, FLAT [RE: repair-sell.md §B1/§B2]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_building_types(sell_roster());
    const int b = world.add_building(0, 5, 5, 5, 4, /*house*/ 0, 1000, 0, 3.6, true);
    world.buildings()[b].hp = 1;                             // nearly dead
    world.grant_credits_for_test(0, 1000.0);
    const double c0 = world.house_credits(0);
    REQUIRE(world.begin_sell(b, 0));
    int frames = 0;
    while (world.building_selling(b) && frames < 200) {
        world.step(1.0 / 15.0);
        ++frames;
        if (world.building_selling(b))
            CHECK(world.house_credits(0) == doctest::Approx(c0));   // NOTHING until the end
    }
    CHECK(frames > 1);                                              // a MISSION, not instant
    CHECK(frames >= 54);                                            // the buildup duration,
    CHECK(frames <= 55);                                            //   3.6 s at 15 fps
    // FLAT refund: 1 HP of 1000 refunds exactly what full health would.
    CHECK(world.house_credits(0) == doctest::Approx(c0 + 1500.0));
    CHECK(world.buildings()[b].state == ra2engine::Building::State::Dead);
    CHECK(world.buildings()[b].sold);
    CHECK_FALSE(world.buildings()[b].selling);
    CHECK_FALSE(world.buildings()[b].being_repaired);
}

TEST_CASE("world: the Soylent override wins over RefundPercent [RE: repair-sell.md §D0.2]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_building_types(sell_roster());
    const int r = world.add_building(1, 5, 5, 4, 3, /*house*/ 0, 1000, 0, 2.0, true);
    const int p = world.add_building(3, 20, 5, 1, 1, /*house*/ 0, 400, 0, 2.0, true);
    REQUIRE(world.begin_sell(r, 0));
    REQUIRE(world.begin_sell(p, 0));
    run_sell(world, r);
    run_sell(world, p);
    CHECK(world.house_credits(0) == doctest::Approx(300.0 + 250.0));  // NOT 1000 + 250
}

TEST_CASE("world: an AI owner gets the UNSCALED refund [RE: repair-sell.md §B1]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_building_types(sell_roster());
    world.enable_ai_house(1, ra2engine::AiHouseConfig{});
    REQUIRE(world.ai_enabled(1));
    const int b1 = world.add_building(0, 5, 5, 5, 4, /*house*/ 1, 1000, 0, 3.6, true);
    // step_ai_houses drains house_credits_[1] into the AI's own wallet every
    // frame (rec.credits += take_income), so read both.
    const double c0 = world.house_credits(1) + world.ai_credits(1);
    REQUIRE(world.begin_sell(b1, 1));
    run_sell(world, b1);
    world.step(1.0 / 15.0);                                         // let the AI drain it
    CHECK(world.house_credits(1) + world.ai_credits(1) ==
          doctest::Approx(c0 + 3000.0));                            // NOT 1500
}

TEST_CASE("world: a type with no buildup art sells INSTANTLY [RE: repair-sell.md §B5 item 6]") {
    ra2assets::MapFile map;
    ra2engine::World world(map);
    world.set_building_types(sell_roster());
    const int w = world.add_building(3, 5, 5, 1, 1, /*house*/ 0, 400, 0, /*buildup*/ 0.0, true);
    const double c0 = world.house_credits(0);
    CHECK(world.begin_sell(w, 0));
    CHECK_FALSE(world.building_selling(w));                  // completed inside begin_sell
    CHECK(world.buildings()[w].state == ra2engine::Building::State::Dead);
    CHECK(world.buildings()[w].sold);
    CHECK(world.house_credits(0) == doctest::Approx(c0 + 250.0));
}

TEST_CASE("world: sell survivors spawn on the freed foundation [RE: repair-sell.md §B3]") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    const ra2engine::UnitCombat crew = gi_stats();

    SUBCASE("Allied GACNST -> clamp(1500/500,1,5) = 3, on the footprint") {
        ra2engine::World world(map);
        world.set_building_types(sell_roster());
        world.set_survivor_crew(static_cast<int>(ra2engine::World::Side::Allied), crew);
        const int b = world.add_building(0, c->x, c->y, 3, 3, /*house*/ 0, 1000, 0, 1.0, true);
        CHECK_FALSE(world.walkable(c->x, c->y));
        const int u0 = static_cast<int>(world.units().size());
        REQUIRE(world.begin_sell(b, 0));
        run_sell(world, b);
        REQUIRE(static_cast<int>(world.units().size()) == u0 + 3);
        for (int i = u0; i < u0 + 3; ++i) {
            const ra2engine::Unit& s = world.units()[i];
            CHECK(s.house == 0);
            CHECK(s.cell_x >= c->x);
            CHECK(s.cell_x < c->x + 3);
            CHECK(s.cell_y >= c->y);
            CHECK(s.cell_y < c->y + 3);
        }
        CHECK(world.buildings()[b].blocking_released);
    }
    SUBCASE("Soviet side -> clamp(1500/250,1,5) = 5") {
        ra2engine::World world(map);
        world.set_building_types(sell_roster());
        world.set_survivor_crew(static_cast<int>(ra2engine::World::Side::Soviet), crew);
        REQUIRE(world.house_side(1) == ra2engine::World::Side::Soviet);  // default derivation
        const int b = world.add_building(0, c->x, c->y, 3, 3, /*house*/ 1, 1000, 0, 1.0, true);
        const int u0 = static_cast<int>(world.units().size());
        REQUIRE(world.begin_sell(b, 1));
        run_sell(world, b);
        CHECK(static_cast<int>(world.units().size()) == u0 + 5);
    }
    SUBCASE("no registered crew -> no survivors") {
        ra2engine::World world(map);
        world.set_building_types(sell_roster());
        const int b = world.add_building(0, c->x, c->y, 3, 3, /*house*/ 0, 1000, 0, 1.0, true);
        const int u0 = static_cast<int>(world.units().size());
        REQUIRE(world.begin_sell(b, 0));
        run_sell(world, b);
        CHECK(static_cast<int>(world.units().size()) == u0);
    }
    SUBCASE("Crewed=no -> no survivors") {
        ra2engine::World world(map);
        world.set_building_types(sell_roster());
        world.set_survivor_crew(static_cast<int>(ra2engine::World::Side::Allied), crew);
        const int b = world.add_building(3, c->x, c->y, 1, 1, /*house*/ 0, 400, 0, 1.0, true);
        const int u0 = static_cast<int>(world.units().size());
        REQUIRE(world.begin_sell(b, 0));
        run_sell(world, b);
        CHECK(static_cast<int>(world.units().size()) == u0);
    }
}

TEST_CASE("World: G-66 a charged AI superweapon fires at the enemy's highest-value target") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(map);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    ra2engine::AiHouseConfig cfg;
    cfg.roster.resize(2);
    cfg.roster[0].id = "NACNST"; cfg.roster[0].conyard = true;
    cfg.roster[0].cost = 3000; cfg.roster[0].tech_level = 1; cfg.roster[0].strength = 1000;
    cfg.roster[1].id = "GAPOWR"; cfg.roster[1].cost = 800; cfg.roster[1].tech_level = 1;
    cfg.roster[1].strength = 750; cfg.roster[1].power = 200;
    cfg.buildup_totals = {0.0, 0.0};
    cfg.team.building_class = {0, ra2engine::kAiBldPower};
    cfg.start_credits = 0;
    // AI house 1 ConYard; enemy house 0: a power plant (value 100) and a ConYard
    // (value 100 too) -- ties broken by the seeded draw; a lone unit scores 2.
    world.add_building(0, c->x, c->y, 1, 1, /*house=*/1, 1000, 0, 0.0, true);
    const int pw = world.add_building(1, c->x + 12, c->y, 1, 1, /*house=*/0, 750, 0, 0.0, true);
    world.enable_ai_house(1, cfg);
    // W6-03-05 [RE: FUN_0050ca10]: the picker needs an Enemy (house+0x5600);
    // this synthetic config never arms the hate delay, so seed the anger.
    world.ai_add_anger(1, 0, 1);
    REQUIRE(world.ai_enemy_house(1) == 0);
    std::array<int, 11> verses; verses.fill(100);
    world.grant_superweapon(1, ra2engine::SuperweaponType::Nuke, 2.0, 600, 3.0, 0.02, verses);
    const int hp0 = world.buildings()[pw].hp;
    bool fired = false;
    for (int i = 0; i < 400 && !fired; ++i) {
        const bool ready = world.sw_ready(1);
        world.step(1.0 / 15.0);
        if (ready && !world.sw_ready(1)) fired = true;
    }
    CHECK(fired);
    for (int i = 0; i < 410; ++i) world.step(1.0 / 15.0);   // W2-AN-05: 200 up + 200 down
    CHECK(world.buildings()[pw].hp < hp0);   // the nuke landed on the power plant
}

TEST_CASE("World W2-CA-10: MakeAlly/MakeEnemy are pairwise (FUN_004f9990/FUN_004f9db0)") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    for (int h = 0; h < 4; ++h) {
        ra2engine::World::HouseRec r;
        r.alliance_team = h < 1 ? 0 : 1;   // player alone on team 0; three enemies on team 1
        world.set_house(h, r);
    }
    CHECK(world.are_allied(1, 3));
    CHECK(world.is_enemy(0, 1));
    // An enemy house allies the player: only that pair changes.
    world.set_pair_alliance(1, 0, true);
    CHECK(world.are_allied(0, 1));
    CHECK(world.is_enemy(0, 2));      // the player is NOT dragged onto team 1
    CHECK(world.is_enemy(0, 3));
    CHECK(world.are_allied(1, 2));    // house 1 keeps its old friends
    world.set_pair_alliance(1, 2, false);
    CHECK(world.is_enemy(1, 2));
    CHECK(world.is_enemy(2, 1));
}

// W5-PRES R-103 [RE: ColorScheme ramp 0x68BDB0 + HSV2RGB 0x517260]: Gold
// 43,239,255, shade 0 -> S' = trunc(SIN(50deg)*239) = 183, V' = trunc(COS(pi/16)*255)
// = 250 -> sector 1 (q, V, p) = (248, 250, 70); index 31 is the darkest.
TEST_CASE("pal_for_hsv: the original ColorScheme ramp (Gold shade 0, monotone darkening)") {
    ra2assets::Palette src{};
    for (int i = 0; i < 256; ++i) src[i] = ra2assets::Rgba{10, 20, 30, 255};
    const ra2assets::Palette out = ra2engine::pal_for_hsv(src, 43, 239, 255);
    CHECK(out[16].r == 248);
    CHECK(out[16].g == 250);
    CHECK(out[16].b == 70);
    for (int i = 17; i <= 31; ++i) CHECK(out[i].g <= out[i - 1].g);
    CHECK(out[31].g < 40);
    CHECK(out[15].r == 10);   // outside the band untouched
    CHECK(out[32].r == 10);
}

// W9-02-02 [RE: Basic_Path FUN_004D3840 path failure -> abandon]: an engineer
// whose target has no reachable approach cell gives up instead of re-pathing
// every frame forever.
TEST_CASE("W9-02-02: an engineer that cannot reach its target abandons the capture") {
    ra2assets::MapFile map;
    if (!load_real_map_w(map)) { MESSAGE("skipped: no install"); return; }
    ra2engine::World world(map);
    world.set_move_speed(4.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // A walkable block: target building ringed by 8 blocked cells, the
    // engineer 5 cells away.
    int ox = -1, oy = -1;
    for (int dy = 0; dy < 20 && ox < 0; ++dy)
        for (int dx = 0; dx < 20 && ox < 0; ++dx) {
            bool ok = true;
            for (int y = 0; y < 5 && ok; ++y)
                for (int x = 0; x < 9 && ok; ++x) ok = world.walkable(c->x + dx + x, c->y + dy + y);
            if (ok) { ox = c->x + dx; oy = c->y + dy; }
        }
    REQUIRE(ox >= 0);
    const int bx = ox + 2, by = oy + 2;
    const int bi = world.add_building(0, bx, by, 1, 1, /*house=*/1, 100, 0, 0.0, /*instant=*/true);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            if (x || y) world.block_cell(bx + x, by + y);
    ra2engine::UnitCombat eng = gi_stats();
    eng.image = "ENGINEER";
    world.add_combat_unit(ox + 7, oy + 2, /*house=*/0, eng);
    world.order_engineer_capture(0, bi);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 150; ++f) world.step(dt);
    CHECK(world.units()[0].capture_target == -1);   // abandoned
    CHECK_FALSE(world.units()[0].moving());
    CHECK(world.buildings()[bi].house == 1);
}

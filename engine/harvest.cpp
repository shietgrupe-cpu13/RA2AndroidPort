#include "harvest.hpp"
#include "diagnostics.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <climits>
#include <cmath>

namespace ra2engine {

namespace {

// 8-ring / warp-fallback neighbor order: N, NE, E, SE, S, SW, W, NW
// [RE-GATE APPLIED: economy.md §1 -- CONFIRMED that an 8-neighbour ring dig
// via an offset table (DAT_0089E5F8) exists and is walked neighbour-by-
// neighbour; the table's own dx,dy VALUES were not decompiled here, so this
// specific N,NE,E,SE,S,SW,W,NW sequence is [RE-FALLBACK adjudicated], not
// itself CONFIRMED -- review Minor-5: the banner previously overclaimed the
// exact order, not just the mechanism (the Harvesting-case comment below
// already split this correctly).]
constexpr int kRingDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kRingDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

const UnitCombat* slave_profile(const Unit& worker, const std::vector<Unit>& units,
                                const std::vector<Building>& buildings,
                                const std::vector<BuildingTypeDef>& types) {
    int type = -1;
    const int manager = worker.w7.slave_manager;
    if (manager >= 0 && manager < static_cast<int>(buildings.size())) {
        type = buildings[static_cast<std::size_t>(manager)].type;
    } else if (manager <= -2) {
        const int mobile = -2 - manager;
        if (mobile >= 0 && mobile < static_cast<int>(units.size()))
            type = units[static_cast<std::size_t>(mobile)].deploys_into;
    }
    if (type < 0 || type >= static_cast<int>(types.size())) return nullptr;
    const UnitCombat& profile = types[static_cast<std::size_t>(type)].slave_combat;
    return profile.image.empty() ? nullptr : &profile;
}

bool is_passable(const std::vector<std::uint8_t>& passable, int gw, int gh, int x, int y) {
    if (x < 0 || y < 0 || x >= gw || y >= gh) return false;
    return passable[(size_t)y * (size_t)gw + (size_t)x] != 0;
}

// [RE-GATE APPLIED §1: FootClass::MoveToTiberium — the wide scan's exact
// radius/tie-break was not isolated (economy.md §1); [RE-FALLBACK
// adjudicated] to this deterministic nearest-cell rule: Chebyshev distance,
// ties broken by scan order (y ascending, then x ascending), which is
// equivalent to "smaller y, then smaller x" since only STRICTLY smaller
// distances replace the running best.]
[[maybe_unused]] bool find_nearest_ore(const OreField& ore, int cx, int cy, int radius, int& out_x, int& out_y) {
    bool found = false;
    int best_dist = INT_MAX;
    int y0 = std::max(0, cy - radius), y1 = std::min(ore.h() - 1, cy + radius);
    int x0 = std::max(0, cx - radius), x1 = std::min(ore.w() - 1, cx + radius);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!ore.has_ore(x, y)) continue;
            int dist = std::max(std::abs(x - cx), std::abs(y - cy));
            if (dist > radius) continue;
            if (!found || dist < best_dist) {
                found = true;
                best_dist = dist;
                out_x = x;
                out_y = y;
            }
        }
    }
    return found;
}

// Widening-ring deflect bound for a blocked warp target (Chebyshev radius).
// 8 rings = up to a 17x17 neighborhood -- far more candidates than a dock
// pileup can plausibly exhaust; beyond it the warp aborts in place (the
// unit's own just-released cell is always re-claimable, see teleport_unit).
constexpr int kWarpDeflectRadius = 8;
// W7-04-23 [CONFIRMED-DATA rulesmd [General] HarvesterTooFarDistance=5;
// RE 0x73DB0E]: cells beyond which a returning miner does not radio its refinery.
constexpr int kHarvesterTooFarDistance = 5;

// Teleport `u` (unit index `self`) to (tx, ty), reconciling occupancy
// (Task 6 review fix -- the pre-fix code only checked terrain passability
// and left `occ` untouched, so the pre-warp cell stayed claimed forever and
// the landing cell was never claimed):
//   1. release EVERY claim `self` holds (current cell + any mid-move
//      next-cell reservation -- the same release_all_of sweep World's order
//      flows use);
//   2. land on the first cell that is terrain-passable AND free in `occ`:
//      the target itself, else the radius-1 8-ring in the recovered
//      N,NE,E,SE,S,SW,W,NW order [RE-GATE §1: DAT_0089E5F8] (preserves the
//      pre-fix landing choice whenever the target/ring is open), else
//      widening Chebyshev rings walked in scan order (y asc, x asc -- the
//      same deterministic tie-break find_nearest_ore uses) out to
//      kWarpDeflectRadius;
//   3. if EVERY candidate is blocked, the warp aborts in place: the unit
//      stays on its current cell (guaranteed re-claimable -- step 1 just
//      released it and nobody else can have claimed it within this call);
//   4. claim exactly the landing cell.
// Post-invariant either way: `self` holds exactly ONE claim, on its current
// cell, and never overlaps another unit's claim. Clears path (movement
// stays in step_units).
void teleport_unit(Unit& u, int self, int tx, int ty,
                   const std::vector<std::uint8_t>& passable, int gw, int gh, Occupancy& occ) {
    diag::note("WARP begin unit=%d image=%s from=%d,%d requested=%d,%d hp=%d state=%d dock=%d ore=%.1f gems=%.1f",self,u.image.c_str(),u.cell_x,u.cell_y,tx,ty,u.hp,static_cast<int>(u.harv),u.dock_refinery,u.bales_ore,u.bales_gems);
    occ.release_all_of(self, /*keep_x*/ -1, /*keep_y*/ -1);  // off-grid keep = release ALL
    auto ok = [&](int x, int y) {
        return is_passable(passable, gw, gh, x, y) && occ.is_free(x, y);
    };
    int fx = u.cell_x, fy = u.cell_y;  // abort-in-place fallback
    bool found = false;
    if (ok(tx, ty)) {
        fx = tx;
        fy = ty;
        found = true;
    }
    for (int d = 0; d < 8 && !found; ++d) {
        int nx = tx + kRingDx[d], ny = ty + kRingDy[d];
        if (ok(nx, ny)) {
            fx = nx;
            fy = ny;
            found = true;
        }
    }
    for (int r = 2; r <= kWarpDeflectRadius && !found; ++r) {
        for (int y = ty - r; y <= ty + r && !found; ++y)
            for (int x = tx - r; x <= tx + r && !found; ++x) {
                if (std::max(std::abs(x - tx), std::abs(y - ty)) != r) continue;  // perimeter only
                if (ok(x, y)) {
                    fx = x;
                    fy = y;
                    found = true;
                }
            }
    }
    diag::note("WARP destination unit=%d resolved=%d,%d found=%d",self,fx,fy,found);
    u.cell_x = fx;
    u.cell_y = fy;
    u.fx = (double)fx;
    u.fy = (double)fy;
    u.path.clear();
    u.path_idx = 0;
    occ.set(fx, fy, self);
    diag::note("WARP complete unit=%d placed=%d,%d",self,u.cell_x,u.cell_y);
}

// NEW-3 (gap owner A): the cell a DRIVING miner heads for. The dock cell
// itself when it is passable; else the art QueueingCell (artmd [NAREFN]/
// [GAREFN] QueueingCell=4,1: "a harvester will aim for this cell if it wasn't
// allowed to reserve the docking cell") = foundation origin + (fw, 1); else
// the first passable cell of the ring hugging the foundation (scan order)
// [RE-FALLBACK: the pad/queue geometry beyond QueueingCell was not hunted].
// Without this a refinery whose south-centre cell is blocked (a building
// placed there, a cliff) froze every War Miner in ReturnToRef forever.
Cellxy dock_approach_cell(const Building& b, const std::vector<std::uint8_t>& passable, int gw,
                          int gh) {
    const int dx = dock_cell_x(b), dy = dock_cell_y(b);
    if (is_passable(passable, gw, gh, dx, dy)) return {dx, dy};
    if (is_passable(passable, gw, gh, b.cx + b.fw, b.cy + 1)) return {b.cx + b.fw, b.cy + 1};
    for (int y = b.cy - 1; y <= b.cy + b.fh; ++y)
        for (int x = b.cx - 1; x <= b.cx + b.fw; ++x) {
            const bool edge = y == b.cy - 1 || y == b.cy + b.fh || x == b.cx - 1 || x == b.cx + b.fw;
            if (edge && is_passable(passable, gw, gh, x, y)) return {x, y};
        }
    return {dx, dy};
}

// W2-SK-03: true when (x, y) is (or touches) the dock / dock-approach cell of
// any live refinery -- a miner with nothing to do must not sit there.
bool on_dock_area(const std::vector<Building>& buildings, const std::vector<int>& refinery_types,
                  const std::vector<std::uint8_t>& passable, int gw, int gh, int x, int y) {
    for (const Building& b : buildings) {
        if (!b.alive || b.state == Building::State::Dead) continue;
        if (std::find(refinery_types.begin(), refinery_types.end(), b.type) == refinery_types.end())
            continue;
        const Cellxy a = dock_approach_cell(b, passable, gw, gh);
        if (std::max(std::abs(a.x - x), std::abs(a.y - y)) <= 1) return true;
        if (std::max(std::abs(dock_cell_x(b) - x), std::abs(dock_cell_y(b) - y)) <= 1) return true;
    }
    return false;
}

// W2-SK-03 [RE-FALLBACK: an idle original miner waits beside its refinery,
// never on the pad]: the nearest free passable cell (rings 2..8 around the
// miner, scan order) that is not part of any refinery's dock area.
bool parking_cell(const std::vector<Building>& buildings, const std::vector<int>& refinery_types,
                  const std::vector<std::uint8_t>& passable, int gw, int gh, const Occupancy& occ,
                  int cx, int cy, int& out_x, int& out_y) {
    for (int r = 2; r <= 8; ++r)
        for (int y = cy - r; y <= cy + r; ++y)
            for (int x = cx - r; x <= cx + r; ++x) {
                if (std::max(std::abs(x - cx), std::abs(y - cy)) != r) continue;
                if (!is_passable(passable, gw, gh, x, y) || !occ.is_free(x, y)) continue;
                if (on_dock_area(buildings, refinery_types, passable, gw, gh, x, y)) continue;
                out_x = x;
                out_y = y;
                return true;
            }
    return false;
}

}  // namespace

bool find_reachable_ore(const OreField& ore, const std::vector<std::uint8_t>& passable, int gw,
                        int gh, const Occupancy& occ, int self, int cx, int cy, int radius,
                        int skip_x, int skip_y, int& out_x, int& out_y,
                        const std::vector<Cellxy>* reserved, int prefer_x, int prefer_y) {
    const int x0 = std::max(0, cx - radius), x1 = std::min(std::min(ore.w(), gw) - 1, cx + radius);
    const int y0 = std::max(0, cy - radius), y1 = std::min(std::min(ore.h(), gh) - 1, cy + radius);
    if (x0 > x1 || y0 > y1) return false;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    // Zone flood (4-connected == reachable without corner cutting) inside the
    // scan box, seeded from the miner's own cell.
    std::vector<std::uint8_t> reach((std::size_t)bw * bh, 0);
    std::vector<int> stack;
    if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) {
        reach[(std::size_t)(cy - y0) * bw + (cx - x0)] = 1;
        stack.push_back((cy - y0) * bw + (cx - x0));
    }
    static const int ndx[4] = {1, -1, 0, 0}, ndy[4] = {0, 0, 1, -1};
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = x0 + i % bw, y = y0 + i / bw;
        for (int d = 0; d < 4; ++d) {
            const int nx = x + ndx[d], ny = y + ndy[d];
            if (nx < x0 || nx > x1 || ny < y0 || ny > y1) continue;
            const int j = (ny - y0) * bw + (nx - x0);
            if (reach[(std::size_t)j] || !is_passable(passable, gw, gh, nx, ny)) continue;
            reach[(std::size_t)j] = 1;
            stack.push_back(j);
        }
    }
    bool found = false;
    int best = INT_MAX;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!reach[(std::size_t)(y - y0) * bw + (x - x0)] || !ore.has_ore(x, y)) continue;
            if (x == skip_x && y == skip_y) continue;
            if (reserved && std::any_of(reserved->begin(),reserved->end(),[&](const Cellxy& c){return c.x==x && c.y==y;})) continue;
            const int o = occ.at(x, y);
            if (o != Occupancy::kEmpty && o != self) continue;   // Cell_Techno() != NULL
            const int dist = std::max(std::abs(x - (prefer_x>=0 ? prefer_x : cx)), std::abs(y - (prefer_y>=0 ? prefer_y : cy)));
            if (prefer_x>=0 && prefer_y>=0 && dist>radius) continue;
            if (dist < best) {
                best = dist;
                out_x = x;
                out_y = y;
                found = true;
            }
        }
    return found;
}

int nearest_refinery(const std::vector<Building>& buildings,
                     const std::vector<BuildingTypeDef>& types,
                     const std::vector<int>& refinery_types, int house, int x, int y) {
    int best = -1;
    int best_dist = INT_MAX;
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive || b.state != Building::State::Idle) continue;
        if (b.house != house) continue;
        if (b.type >= 0 && b.type < static_cast<int>(types.size()) && types[b.type].slave_count > 0) continue;
        bool is_ref = false;
        for (int rt : refinery_types) {
            if (b.type == rt) { is_ref = true; break; }
        }
        if (!is_ref) continue;
        int dx = std::abs(dock_cell_x(b) - x);
        int dy = std::abs(dock_cell_y(b) - y);
        int dist = std::max(dx, dy);
        if (dist < best_dist) {
            best_dist = dist;
            best = (int)i;
        }
    }
    return best;
}

std::vector<std::size_t> free_unit_grand_openings(
    const std::vector<Building>& buildings, const std::vector<bool>& free_unit_of_type,
    std::vector<int>& prev_states) {
    prev_states.resize(buildings.size(), -1);
    std::vector<std::size_t> opened;
    for (std::size_t i = 0; i < buildings.size(); ++i) {
        const int cur = static_cast<int>(buildings[i].state);
        const int prev = prev_states[i];
        prev_states[i] = cur;
        if (prev != static_cast<int>(Building::State::Buildup) ||
            cur != static_cast<int>(Building::State::Idle))
            continue;
        const int t = buildings[i].type;
        if (t < 0 || t >= (int)free_unit_of_type.size() || !free_unit_of_type[t]) continue;
        opened.push_back(i);
    }
    return opened;
}

void step_harvest(std::vector<Unit>& units, std::vector<Building>& buildings,
                  const std::vector<BuildingTypeDef>& types,
                  const std::vector<int>& refinery_types, OreField& ore,
                  const std::vector<std::uint8_t>& passable, int gw, int gh, Occupancy& occ,
                  double dt, const OreValues& values, double house_credits[2],
                  std::vector<HarvestMove>& moves, std::vector<HarvestEvent>& events,
                  int n_houses, const int* purifiers, double purifier_bonus,
                  const HarvestScan& scan) {
    for (int idx = 0; idx < (int)units.size(); ++idx) {
        Unit& u = units[idx];
        const UnitCombat* slave = slave_profile(u, units, buildings, types);
        const double harvest_secs = slave ? std::max(1, slave->harvest_rate_micros) / 1000000.0 : kGulpSecs;
        const double reload_secs = slave ? std::max(1, slave->slave_reload_rate_micros) / 1000000.0
                                         : kUnloadSecsPerBale;
        if (!u.is_harvester || !u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 ||
            u.is_aircraft)
            continue;  // carried harvester: mission FSM parks (CB-1); CB-2: an
                       // aircraft is never a harvester (data-unreachable, guarded)
        if (u.w7.slave_manager >= 0) {
            const int manager = u.w7.slave_manager;
            if (manager >= static_cast<int>(buildings.size()) || !buildings[manager].alive ||
                buildings[manager].state != Building::State::Idle || buildings[manager].house != u.house) continue;
            u.dock_refinery = manager;
        }

        // Auto-resume after a manual move order [RE-GATE APPLIED §1 PROBABLE:
        // "was harvesting" flag restores the mission once the ordered move
        // completes]. While the move is in flight, mission logic is entirely
        // skipped; on arrival the mission restarts at SeekOre (a fresh rescan
        // picks up wherever the miner ends up, load intact).
        if (u.manual_hold) {
            if (!u.moving()) {
                u.manual_hold = false;
                u.harv = HarvState::SeekOre;
                u.harv_timer = 0.0;
            }
            continue;
        }

        switch (u.harv) {
            case HarvState::None:
                break;

            case HarvState::SeekOre: {
                // Scan immediately when due (harv_timer <= 0 -- true the
                // instant the mission enters this state, since every
                // transition into SeekOre resets harv_timer to 0), otherwise
                // wait out the rescan cadence [RE-FALLBACK: economy.md §1].
                u.harv_timer -= dt;
                if (u.harv_timer > 0.0) break;
                u.harv_timer = kRescanSecs;
                int ox = 0, oy = 0;
                // W2-SK-02/04: TiberiumShortScan first, then TiberiumLongScan
                // (RA1 Mission_Harvest FINDINGSPOT order); zone- and occupant-
                // filtered; the last cell a move to failed is skipped once.
                std::vector<Cellxy> reserved;
                const bool slave=u.w7.slave_manager>=0;
                int center_x=-1,center_y=-1;
                if (slave) {
                    const auto& manager=buildings[u.w7.slave_manager];
                    center_x=manager.cx+(manager.fw-1)/2;
                    center_y=manager.cy+(manager.fh-1)/2;
                    for (int other=0;other<static_cast<int>(units.size());++other) {
                        const auto& v=units[other];
                        if (other!=idx && v.alive && v.die_time<0 && v.w7.slave_manager==u.w7.slave_manager &&
                            (v.harv==HarvState::MoveToOre || v.harv==HarvState::Harvesting))
                            reserved.push_back({v.harv_cell_x,v.harv_cell_y});
                    }
                }
                // Vanilla SlaveMinerSlaveScan default (14). Ordinary miners
                // retain their existing short/long scans and selection order.
                const bool got = slave ? find_reachable_ore(ore,passable,gw,gh,occ,idx,u.cell_x,u.cell_y,
                    14,u.harv_skip_x,u.harv_skip_y,ox,oy,&reserved,center_x,center_y) :
                    find_reachable_ore(ore, passable, gw, gh, occ, idx, u.cell_x, u.cell_y,
                                       scan.short_scan, u.harv_skip_x, u.harv_skip_y, ox, oy) ||
                    find_reachable_ore(ore, passable, gw, gh, occ, idx, u.cell_x, u.cell_y,
                                       scan.long_scan, u.harv_skip_x, u.harv_skip_y, ox, oy);
                u.harv_skip_x = u.harv_skip_y = -1;
                if (got) {
                    if (u.w7.slave_manager >= 0)
                        diag::note("SLAVE ore-target unit=%d manager=%d from=%d,%d target=%d,%d",idx,u.w7.slave_manager,u.cell_x,u.cell_y,ox,oy);
                    u.harv_cell_x = ox;
                    u.harv_cell_y = oy;
                    u.has_harv_cell = true;  // real stamp -- see world.hpp
                    moves.push_back({idx, ox, oy});
                    u.harv = HarvState::MoveToOre;
                    break;
                }
                // Nothing in reach. A part-loaded miner banks what it has
                // [RE-FALLBACK: RA1 Mission_Harvest -- no tiberium found and
                // Tiberium_Load() > 0 -> return to the refinery].
                if (u.bales_ore + u.bales_gems > 0.0) {
                    u.harv = HarvState::ReturnToRef;
                    u.harv_timer = 0.0;
                    break;
                }
                // W2-SK-03: an empty miner never idles on a refinery's dock
                // area (it froze every full miner queued behind it).
                if (!u.moving() && on_dock_area(buildings, refinery_types, passable, gw, gh,
                                                u.cell_x, u.cell_y)) {
                    int px = 0, py = 0;
                    if (parking_cell(buildings, refinery_types, passable, gw, gh, occ, u.cell_x,
                                     u.cell_y, px, py))
                        moves.push_back({idx, px, py});
                }
                break;
            }

            case HarvState::MoveToOre: {
                bool at_cell = (u.cell_x == u.harv_cell_x && u.cell_y == u.harv_cell_y);
                if (at_cell && !u.moving()) {
                    u.harv = HarvState::Harvesting;
                    u.harv_timer = 0.0;
                    break;
                }
                if (!ore.has_ore(u.harv_cell_x, u.harv_cell_y)) {
                    // Target emptied en route (another harvester got there
                    // first) -- rescan rather than arrive at a dead cell.
                    u.harv = HarvState::SeekOre;
                    u.harv_timer = 0.0;
                    break;
                }
                if (!at_cell && !u.moving()) {
                    // Stopped without arriving: the path was abandoned
                    // (unreachable / collision give-up in step_units).
                    // W2-SK-04: the next scan skips that cell (no livelock on
                    // one unreachable target); a miner that is ON ore digs.
                    u.harv_skip_x = u.harv_cell_x;
                    u.harv_skip_y = u.harv_cell_y;
                    if (ore.has_ore(u.cell_x, u.cell_y)) {
                        u.harv_cell_x = u.cell_x;
                        u.harv_cell_y = u.cell_y;
                        u.harv = HarvState::Harvesting;
                    } else {
                        u.harv = HarvState::SeekOre;
                    }
                    u.harv_timer = 0.0;
                }
                break;
            }

            case HarvState::Harvesting: {
                // [RE-GATE APPLIED §1 CONFIRMED: 8-ring dig in DAT_0089E5F8
                // offset-table order, one density step per timed gulp banked
                // into a per-type store. [RE-FALLBACK] the gulp cadence
                // itself; and [RE-FALLBACK adjudicated] trying the miner's
                // OWN cell before the ring -- the doc confirms only the
                // 8-neighbour ring walk, not an own-cell-first step.]
                u.harv_timer += dt;
                while (u.harv_timer >= harvest_secs && u.harv == HarvState::Harvesting) {
                    u.harv_timer -= harvest_secs;
                    int tx = u.cell_x, ty = u.cell_y;
                    OreKind k = OreKind::None;
                    if (ore.has_ore(tx, ty)) {
                        k = ore.take_bale(tx, ty);
                    } else {
                        for (int d = 0; d < 8 && k == OreKind::None; ++d) {
                            int nx = tx + kRingDx[d], ny = ty + kRingDy[d];
                            if (ore.has_ore(nx, ny)) k = ore.take_bale(nx, ny);
                        }
                    }
                    if (k == OreKind::None) {
                        // Own cell AND ring exhausted -> relocate.
                        u.harv = HarvState::SeekOre;
                        u.harv_timer = 0.0;
                        break;
                    }
                    if (k == OreKind::Ore) {
                        u.bales_ore += 1.0;
                    } else {
                        u.bales_gems += 1.0;
                    }
                    if (u.bales_ore + u.bales_gems >= (double)u.storage_bales) {
                        u.harv = HarvState::ReturnToRef;
                        u.harv_timer = 0.0;
                        break;
                    }
                }
                break;
            }

            case HarvState::ReturnToRef: {
                // Stamp the target refinery ONCE, the moment this state picks
                // it (review fix: re-deriving `nearest` every tick made the
                // WaitDock busy check evaluate against a moving target in a
                // multi-refinery house). The stamp persists through WaitDock/
                // Unloading; repick ONLY when the stamped building dies
                // [economy.md §2 -- the radio handshake binds a miner to the
                // refinery that answered it].
                int ref = u.dock_refinery;
                bool fresh_pick = false;
                if (u.w7.slave_manager >= 0) ref = u.w7.slave_manager;
                if (u.w7.slave_manager>=0) {
                    const auto& manager=buildings[ref]; // validated at function entry
                    const int dx=std::max({manager.cx-u.cell_x,0,u.cell_x-(manager.cx+manager.fw-1)});
                    const int dy=std::max({manager.cy-u.cell_y,0,u.cell_y-(manager.cy+manager.fh-1)});
                    if (std::max(dx,dy)<=1 && !u.moving()) {
                        u.harv=HarvState::Unloading;u.harv_timer=0;
                        events.push_back({HarvestEvent::Kind::UnloadStart,idx});
                    } else if (!u.moving()) {
                        int bx=-1,by=-1,best=INT_MAX;
                        for (int y=manager.cy-1;y<=manager.cy+manager.fh;++y)
                            for(int x=manager.cx-1;x<=manager.cx+manager.fw;++x) {
                                if (x>=manager.cx && x<manager.cx+manager.fw && y>=manager.cy && y<manager.cy+manager.fh) continue;
                                if (!is_passable(passable,gw,gh,x,y) || (!occ.is_free(x,y) && occ.at(x,y)!=idx)) continue;
                                const bool claimed=std::any_of(units.begin(),units.end(),[&](const Unit& v){
                                    return &v!=&u && v.alive && v.w7.slave_manager==ref && v.moving() && v.path.back().x==x && v.path.back().y==y;
                                });
                                const bool requested=std::any_of(moves.begin(),moves.end(),[&](const HarvestMove& m){return m.gx==x && m.gy==y;});
                                if (claimed || requested) continue;
                                const int distance=std::max(std::abs(x-u.cell_x),std::abs(y-u.cell_y));
                                if(distance<best){best=distance;bx=x;by=y;}
                            }
                        if(bx>=0)moves.push_back({idx,bx,by});
                    }
                    break; // slaves do not use the refinery's single truck dock
                }
                if (ref < 0 || ref >= (int)buildings.size() ||
                    !buildings[(size_t)ref].alive ||
                    buildings[(size_t)ref].state != Building::State::Idle) {
                    ref = nearest_refinery(buildings, types, refinery_types, u.house, u.cell_x,
                                           u.cell_y);
                    u.dock_refinery = ref;
                    fresh_pick = true;
                }
                // W7-04-23 [RE: UnitClass harvest mission 0x73DB0E,
                // [General] HarvesterTooFarDistance=5]: a returning miner
                // radios (binds) the refinery only within 5 cells; farther
                // away it drives toward it (FUN_0056D750) and decides again
                // -- a distant miner neither holds the dock nor queues.
                if (fresh_pick && ref >= 0 && !u.chrono_miner) {
                    const Building& rb = buildings[(size_t)ref];
                    const int ddx = u.cell_x < rb.cx ? rb.cx - u.cell_x : (u.cell_x >= rb.cx + rb.fw ? u.cell_x - (rb.cx + rb.fw - 1) : 0);
                    const int ddy = u.cell_y < rb.cy ? rb.cy - u.cell_y : (u.cell_y >= rb.cy + rb.fh ? u.cell_y - (rb.cy + rb.fh - 1) : 0);
                    if (std::max(ddx, ddy) > kHarvesterTooFarDistance) {
                        u.dock_refinery = -1;
                        const Cellxy ac = dock_approach_cell(rb, passable, gw, gh);
                        if (!u.moving() || u.path.back().x != ac.x || u.path.back().y != ac.y)
                            moves.push_back({idx, ac.x, ac.y});
                        break;
                    }
                }
                if (ref < 0) {
                    // No alive refinery: park the (still loaded) miner back
                    // in SeekOre, retried every rescan, per economy.md §1/§2.
                    u.dock_refinery = -1;
                    u.harv = HarvState::SeekOre;
                    u.harv_timer = 0.0;
                    break;
                }
                int dcx = dock_cell_x(buildings[(size_t)ref]);
                int dcy = dock_cell_y(buildings[(size_t)ref]);
                if (!u.chrono_miner) {   // NEW-3: drive to a reachable dock approach
                    const Cellxy ac = dock_approach_cell(buildings[(size_t)ref], passable, gw, gh);
                    dcx = ac.x;
                    dcy = ac.y;
                }
                if (u.chrono_miner) {
                    // [RE-GATE APPLIED §3 PROBABLE: teleport locomotor warps
                    // on going full; [RE-FALLBACK] exact timing ->
                    // kChronoWarpSecs.] harv_timer == 0.0 exactly on the
                    // first tick of this warp (every transition INTO
                    // ReturnToRef resets it), so ChronoOut fires once.
                    if (u.harv_timer == 0.0) {
                        events.push_back({HarvestEvent::Kind::ChronoOut, idx});
                    }
                    u.harv_timer += dt;
                    if (u.harv_timer >= kChronoWarpSecs) {
                        teleport_unit(u, idx, dcx, dcy, passable, gw, gh, occ);
                        events.push_back({HarvestEvent::Kind::ChronoIn, idx});
                        u.harv = HarvState::WaitDock;
                        u.harv_timer = 0.0;
                    }
                } else {
                    bool at_dock = (u.cell_x == dcx && u.cell_y == dcy && !u.moving());
                    // W2-SK-03 dock queue [RE-FALLBACK: artmd QueueingCell=4,1
                    // "a harvester will aim for this cell if it wasn't allowed
                    // to reserve the docking cell"]: while the dock is taken
                    // (a unit on it, or another miner docked/unloading here),
                    // or another returning miner of this refinery is nearer
                    // the dock, wait at the queueing cell instead of crowding
                    // the pad -- the crowd sealed the empty miner leaving it.
                    bool busy = false;
                    const int on_dock = occ.at(dcx, dcy);
                    if (on_dock != Occupancy::kEmpty && on_dock != idx) busy = true;
                    const int my_d = std::max(std::abs(u.cell_x - dcx), std::abs(u.cell_y - dcy));
                    for (std::size_t j = 0; j < units.size() && !busy; ++j) {
                        if ((int)j == idx) continue;
                        const Unit& o = units[j];
                        if (!o.alive || !o.is_harvester || o.dock_refinery != ref) continue;
                        if (o.harv == HarvState::WaitDock || o.harv == HarvState::Unloading) busy = true;
                        else if (o.harv == HarvState::ReturnToRef && !o.chrono_miner) {
                            const int od = std::max(std::abs(o.cell_x - dcx), std::abs(o.cell_y - dcy));
                            if (od < my_d || (od == my_d && (int)j < idx)) busy = true;
                        }
                    }
                    if (at_dock) {
                        u.harv = HarvState::WaitDock;
                        u.harv_timer = 0.0;
                    } else if (busy && !at_dock) {
                        const Building& rb = buildings[(size_t)ref];
                        const int qx = rb.cx + rb.fw, qy = rb.cy + 1;   // QueueingCell=4,1
                        const int qd = std::max(std::abs(u.cell_x - qx), std::abs(u.cell_y - qy));
                        const bool near_dock = my_d <= 2;
                        if (u.moving() && !near_dock) {
                            // keep driving; re-evaluated every tick
                            const Cellxy t = u.path.back();
                            if (t.x == dcx && t.y == dcy && my_d <= 4) {   // don't close on a busy pad
                                u.path.resize(u.path_idx < u.path.size() ? u.path_idx + 1 : u.path.size());
                            }
                        } else if (qd > 2 || near_dock) {
                            int px = qx, py = qy;
                            if (!is_passable(passable, gw, gh, qx, qy) || !occ.is_free(qx, qy) ||
                                on_dock_area(buildings, refinery_types, passable, gw, gh, qx, qy))
                                parking_cell(buildings, refinery_types, passable, gw, gh, occ, qx, qy, px, py);
                            if (!u.moving() || u.path.back().x != px || u.path.back().y != py)
                                moves.push_back({idx, px, py});
                        }
                    } else if (!u.moving() || u.path.back().x != dcx ||
                               u.path.back().y != dcy) {
                        // Review Important-1: emit the HarvestMove ONLY when
                        // the unit isn't already pathing to this exact dock
                        // cell (step_units owns the drive and its re-path /
                        // give-up handling once the move is issued).
                        moves.push_back({idx, dcx, dcy});
                    }
                }
                break;
            }

            case HarvState::WaitDock: {
                // Queue at the refinery STAMPED by ReturnToRef (review fix:
                // never re-derive `nearest` here -- another refinery going
                // Idle mid-wait must not poach a queued miner). Repick (via
                // ReturnToRef) only if the stamped building died.
                int ref = u.dock_refinery;
                if (ref < 0 || ref >= (int)buildings.size() ||
                    !buildings[(size_t)ref].alive ||
                    buildings[(size_t)ref].state == Building::State::Dead) {
                    u.dock_refinery = -1;
                    u.harv = HarvState::ReturnToRef;
                    u.harv_timer = 0.0;
                    break;
                }
                // [RE-GATE APPLIED §2: the radio handshake refuses "dock
                // busy"; [RE-FALLBACK adjudicated] to deterministic
                // lowest-index FIFO -- an emergent property of processing
                // units ascending: a lower-index unit that claims the dock
                // THIS tick is already Unloading by the time a higher-index
                // unit checks, below.]
                bool busy = false;
                for (size_t j = 0; j < units.size(); ++j) {
                    if ((int)j == idx) continue;
                    if (units[j].dock_refinery == ref && units[j].harv == HarvState::Unloading) {
                        busy = true;
                        break;
                    }
                }
                if (!busy) {
                    events.push_back({HarvestEvent::Kind::UnloadStart, idx});
                    u.harv = HarvState::Unloading;
                    u.harv_timer = 0.0;
                }
                break;
            }

            case HarvState::Unloading: {
                // CMIN post-unload warp-back transit: `dock_refinery == -1`
                // with an empty load marks the return warp in flight (set
                // below when the last bale drains, ONLY when has_harv_cell --
                // review Important-3 -- so `harv` never re-enters this state
                // with dock_refinery == -1 unless a real warp is in flight).
                // Must be checked BEFORE the refinery-death repick, which
                // keys off the same -1.
                if (u.chrono_miner && u.dock_refinery < 0) {
                    u.harv_timer += dt;
                    if (u.harv_timer >= kChronoWarpSecs) {
                        teleport_unit(u, idx, u.harv_cell_x, u.harv_cell_y, passable, gw, gh, occ);
                        events.push_back({HarvestEvent::Kind::ChronoIn, idx});
                        u.harv = HarvState::SeekOre;
                        u.harv_timer = 0.0;
                    }
                    break;
                }
                if (u.dock_refinery < 0 || u.dock_refinery >= (int)buildings.size() ||
                    !buildings[(size_t)u.dock_refinery].alive ||
                    buildings[(size_t)u.dock_refinery].state == Building::State::Dead) {
                    // Refinery died mid-unload -> repick.
                    u.dock_refinery = -1;
                    u.harv = HarvState::ReturnToRef;
                    u.harv_timer = 0.0;
                    break;
                }
                // [RE-GATE APPLIED §2 CONFIRMED: per-tick chunked transfer,
                // first-non-empty type slot first (ore before gems here,
                // matching the rig's fixed 2-slot ore/gems store); income =
                // bales * Value; no PurifierBonus this phase.]
                u.harv_timer += dt;
                while (u.harv_timer >= reload_secs &&
                       (u.bales_ore > 0.0 || u.bales_gems > 0.0)) {
                    u.harv_timer -= reload_secs;
                    int house = (u.house >= 0 && u.house < n_houses) ? u.house : 0;
                    double v;
                    if (u.bales_ore > 0.0) {
                        u.bales_ore -= 1.0;
                        v = (double)values.ore;
                    } else {
                        u.bales_gems -= 1.0;
                        v = (double)values.gems;
                    }
                    house_credits[house] += v;
                    // [RE: economy.md §2 CONFIRMED] Ore Purifier bonus: a
                    // separate add of count x PurifierBonus x value.
                    if (purifiers && purifiers[house] > 0)
                        house_credits[house] += purifiers[house] * purifier_bonus * v;
                }
                if (u.bales_ore <= 0.0 && u.bales_gems <= 0.0) {
                    events.push_back({HarvestEvent::Kind::UnloadEnd, idx});
                    u.dock_refinery = -1;
                    // W7-11-13 [RE: UnitClass::SetDestination 0x740780]: the
                    // Chrono Miner warps only when docking at a DockUnload
                    // refinery; leaving it, it piggybacks a Drive locomotor
                    // and DRIVES back to the ore like a HARV (the old timed
                    // warp-back is gone; the transit branch at the top of this
                    // case only finishes one restored from an older save).
                    u.harv = HarvState::SeekOre;
                    u.harv_timer = 0.0;
                }
                break;
            }
        }
    }
}

}  // namespace ra2engine

// G16 world depth (agent G): garrisonable buildings (G-48) and bridges (G-49).
// World members kept in their own TU (the building_combat.cpp precedent);
// world.cpp only calls step_depth and the engineer-at-hut hook.
// Every behaviour is cited to docs/re/world-depth.md (gamemd.exe anchors).
#include <algorithm>
#include <cmath>

#include "bridges.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "combat.hpp"       // deliver_hit_from, deliver_unit_hit, deliver_building_hit, rearm_frames_for
#include "group_move.hpp"   // nearest_free_cells
#include "veterancy.hpp"    // is_elite
#include "world.hpp"
#include "diagnostics.hpp"
#include "weapon_unit.hpp"
#include "slave_passage.hpp"
#include "building_transition.hpp"

namespace ra2engine {

namespace {
constexpr double kFireFlashSecs = 0.4;
// High-bridge deck cells begin beyond the ramp overlays and can sit several
// cells from the repair hut. Use the same generous scan for the engineer's
// Enter eligibility and the actual repair walk, so the hut cannot accept an
// engineer for a bridge it then fails to find (or reject a bridge it could fix).
constexpr int kHighBridgeRepairScan = 8;
int cheb_to_foundation(const Building& b, int x, int y) {
    int best = -1;
    for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
        const int d = std::max(std::abs(x - c.x), std::abs(y - c.y));
        if (best < 0 || d < best) best = d;
    }
    return best;
}
// == world.cpp land_type_blocks_ground. W8-01-06 [DATA: rulesmd [Beach]
// Foot=Track=Wheel=0%]: Beach blocks the ground SpeedTypes too (the fv7c load
// path already did), so a low section destroyed at runtime over a Beach tile is
// not walkable -- the runtime path agrees with the load path.
bool blocks_ground(ra2assets::LandType t) {
    return t == ra2assets::LandType::Water || t == ra2assets::LandType::Rock ||
           t == ra2assets::LandType::Wall || t == ra2assets::LandType::Beach;
}
bool airborne(const Unit& u) { return u.is_aircraft && u.altitude > 0.0; }
// The objects a bridge cell's collapse can reach: on the map (not carried,
// garrisoned or under a parachute), alive, not in flight.
bool collapse_candidate(const Unit& u) {
    return u.alive && u.die_time < 0.0 && u.cargo_of < 0 && !airborne(u);
}
// A JumpJet type (TechnoType +0xD94, FUN_00711510) or the jumpjet locomotor.
bool jumpjet_type(const Unit& u) {
    return u.loco.t.kind == static_cast<int>(LocoKind::Jumpjet) || (u.is_aircraft && !u.an.t.fixed_wing);
}
void reset_bunker_drive(Unit& u) {
    const auto type=u.loco.t;
    u.loco=UnitLoco{};u.loco.t=type;
    u.wait_time=0;
}
}  // namespace

int World::neutral_house() const {
    for (int h = 0; h < house_count(); ++h)
        if (!house_combatant(h)) return h;
    return -1;
}

// ---------------------------------------------------------------- garrisons

// BuildingClass::CanBeOccupiedBy (FUN_00457CE0): CanBeOccupied, not mid-build
// / mid-sell, an Occupier=yes infantry, the building owned by the
// infantry's house OR by a neutral (MultiplayPassive) house, and room left
// (OccupantCount != MaxNumberOccupants). The Assaulter (+0xEB5) branch is a
// YR-only infantry flag no stock RA2 unit uses -- not modelled.
bool World::can_occupy(int ui, int bi) const {
    if (ui < 0 || ui >= static_cast<int>(units_.size())) return false;
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    const Unit& u = units_[ui];
    const Building& b = buildings_[bi];
    if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.is_aircraft || u.is_naval) return false;
    if (!b.alive || b.state != Building::State::Idle || b.selling) return false;
    if (b.type < 0 || b.type >= static_cast<int>(btypes().size())) return false;
    const BuildingTypeDef& t = btypes()[b.type];
    if (!t.can_be_occupied && !t.grinding) return false;
    if (t.grinding) {
        // Grinder pads accept the owner's infantry and ground vehicles, without
        // applying civilian garrison ownership, Occupier, or occupant limits.
        if (b.house != u.house || u.is_naval) return false;
        return true;
    }
    if (t.bio_reactor)
        return u.is_infantry && b.house == u.house &&
               static_cast<int>(b.occupants.size()) < t.max_occupants;
    if (t.tank_bunker) {
        if (!u.is_vehicle || !u.w7.t.bunkerable || b.house != u.house) return false;
        if(b.bunker_exit_left>=0 || std::any_of(units_.begin(),units_.end(),[&](const Unit& v){return v.alive && v.bunker_exit_host==bi;}))return false;
    } else if (!u.occupier || u.is_vehicle) return false;
    const bool owner_ok = b.house == u.house || !house_combatant(b.house) || passive_house(b.house);
    // W7-08-09 [RE: CanBeOccupiedBy FUN_00457CE0]: no entry at or below
    // ConditionRed (Rules+0x1708).
    if (garrison_red(b)) return false;
    return owner_ok && static_cast<int>(b.occupants.size()) < t.max_occupants;
}

// W7-08-09 [RE: FUN_004581F0 / FUN_00457CE0]: health ratio <= ConditionRed.
bool World::garrison_red(const Building& b) const {
    return b.max_hp > 0 && static_cast<double>(b.hp) / static_cast<double>(b.max_hp) <= land_rules_.condition_red;
}

bool World::campaign_can_occupy(int bi) const {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    const Building& b = buildings_[bi];
    if (!b.alive || b.state != Building::State::Idle || b.selling) return false;
    if (b.type < 0 || b.type >= static_cast<int>(btypes().size())) return false;
    const BuildingTypeDef& t = btypes()[b.type];
    if (garrison_red(b)) return false;   // W7-08-09
    return t.can_be_occupied && static_cast<int>(b.occupants.size()) < t.max_occupants;
}

bool World::campaign_occupy_now(int ui, int bi) {
    if (!can_occupy(ui, bi)) return false;
    garrison_any_ = true;
    occupy_now(ui, bi);
    return true;
}

void World::order_occupy(int ui, int bi) {
    if (!can_occupy(ui, bi)) return;
    garrison_any_ = true;
    Unit& u = units_[ui];
    u.target = -1;
    u.target_building = -1;
    u.attack_order = false;
    u.enter_target = -1;
    u.capture_target = -1;
    u.infiltrate_target = -1;
    u.attack_cell_x = u.attack_cell_y = -1;
    u.occupy_target = bi;
    const Building& b = buildings_[bi];
    if(btypes()[b.type].tank_bunker) {
        const auto dock=bunker_docking_cell(b.cx,b.cy,b.fw,b.fh);
        order_unit_move(ui,dock[0],dock[1]);
        return;
    }
    if (cheb_to_foundation(b, u.cell_x, u.cell_y) <= 1) return;   // enters next step
    int nx = b.cx, ny = b.cy, best = -1;
    for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
        const int d = std::max(std::abs(u.cell_x - c.x), std::abs(u.cell_y - c.y));
        if (best < 0 || d < best) { best = d; nx = c.x; ny = c.y; }
    }
    // The nearest free cells around that foundation cell; the first one the
    // unit can actually path to (a cell in a walled-off pocket is skipped).
    const std::vector<Cellxy> free = nearest_free_cells({nx, ny}, 8, passable_, grid_w_, grid_h_, occ_);
    for (const Cellxy& g : free) {
        order_unit_move(ui, g.x, g.y);
        if (units_[ui].moving()) break;
    }
}

int World::group_occupy_count(int issuer, const std::vector<int>& ids, int bi) const {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return 0;
    int n = 0;
    for (int i : ids) {
        if (i < 0 || i >= static_cast<int>(units_.size())) continue;
        const Unit& u = units_[static_cast<std::size_t>(i)];
        if (u.house != issuer || u.cargo_of >= 0) continue;
        if (!can_occupy(i, bi)) continue;
        // Room check against the orders already given this click.
        const Building& b = buildings_[bi];
        if (static_cast<int>(b.occupants.size()) + n >= btypes()[b.type].max_occupants) break;
        ++n;
    }
    return n;
}

int World::group_occupy(int issuer, const std::vector<int>& ids, int bi) {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return 0;
    int n = 0;
    for (int i : ids) {
        if (i < 0 || i >= static_cast<int>(units_.size())) continue;
        const Unit& u = units_[static_cast<std::size_t>(i)];
        if (u.house != issuer || u.cargo_of >= 0) continue;
        if (!can_occupy(i, bi)) continue;
        // Room check against the orders already given this click.
        const Building& b = buildings_[bi];
        if (static_cast<int>(b.occupants.size()) + n >= btypes()[b.type].max_occupants) break;
        order_occupy(i, bi);
        ++n;
    }
    return n;
}

// InfantryClass::Occupy (FUN_00522640): the infantry leaves the map
// (Limbo) and is appended to Occupants; the first occupant raises the
// "structure garrisoned" EVA for the owner (FUN_0050B510 player check).
void World::occupy_now(int ui, int bi) {
    Unit& u = units_[ui];
    Building& b = buildings_[bi];
    const BuildingTypeDef& type = btypes()[b.type];
    if (type.grinding) {
        const auto soylent = unit_soylent_.find(u.tg.t.rules_id);
        const int unit_soylent = soylent == unit_soylent_.end() ? 0 : soylent->second;
        const int refund = sell_refund(u.vet.cost, unit_soylent, repair_rules_.refund_percent,
                                       !ai_enabled(u.house));
        if (refund > 0) deposit_credits(u.house, refund);
        consume_unit(ui);
        diag::note("GRINDER consumed building=%d unit=%d house=%d refund=%d", bi, ui, u.house, refund);
        return;
    }
    if(btypes()[b.type].tank_bunker)reset_bunker_drive(u);
    occ_.release_all_of(ui, -1, -1);
    u.path.clear();
    u.path_idx = 0;
    u.target = -1;
    u.target_building = -1;
    u.occupy_target = -1;
    u.cargo_of = kGarrisonCargo;
    u.garrison_of = bi;
    b.occupants.push_back(ui);
    garrison_events_.push_back({GarrisonEvent::Entered, bi, u.house});
    note_entered(EnteredEvent::Occupy, ui, bi, u.house, false);   // W2-CA-01
    if (b.occupants.size() == 1) garrison_events_.push_back({GarrisonEvent::Garrisoned, bi, u.house});
    sync_garrison_owner(bi);
    diag::note("GARRISON entered building=%d unit=%d house=%d owner=%d occupants=%zu",bi,ui,u.house,b.house,b.occupants.size());
}

// FUN_004581F0 (called each tick from FUN_0043FAA0 for CanBeOccupied types):
// empty and not neutral -> back to the neutral house ("structure abandoned"
// EVA for the old owner); occupied and neutral -> the FIRST occupant's house.
void World::sync_garrison_owner(int bi) {
    Building& b = buildings_[bi];
    if (b.type >= 0 && b.type < static_cast<int>(btypes().size())) {
        const auto& type=btypes()[b.type];
        // Constructed bunkers retain their owner when empty. Only civilian
        // urban-combat garrisons adopt/abandon ownership with their occupants.
        if (type.tank_bunker || type.bio_reactor || type.id=="NABNKR") return;
    }
    const int neutral = neutral_house();
    if (b.occupants.empty()) {
        if (neutral >= 0 && b.house != neutral) {
            garrison_events_.push_back({GarrisonEvent::Abandoned, bi, b.house});
            b.house = neutral;
            b.garrison_target = -1;
        }
    } else {
        const int occupant_house = units_[b.occupants.front()].house;
        if (b.house != occupant_house) {
            b.house = occupant_house;
        }
    }
}

// BuildingClass::UnloadOccupants (FUN_00457DD0), W8-06-07 / W8-02-04
// [RE: disasm 0x457DD0..0x458180]: FiringOccupantIndex resets; the exit is
// ONE cell on the ring around the foundation (origin (x0,y0), size w x h),
// walked in this order, each tested with the FIRST occupant's
// IsCellOccupied (vtbl+0x1AC -> InfantryClass FUN_0051BD70: land type +
// the FootClass height gate FUN_004D9B70 + room in the cell):
//   1. (x0+w, y)   y = y0+h down to y0-1     (the right column, bottom up)
//   2. (x, y0+h)   x = x0+w-1 down to x0-1   (the bottom row, right to left)
//   3. (x, y0-1)   x = x0 .. x0+w            (the top row, left to right)
//   4. (x0-1, y)   y = y0 .. y0+h            (the left column, top down)
// No enterable cell: KillOccupants FUN_004585B0(0) UnInits every occupant
// (no death), unless the caller passed the second flag (the sell path), in
// which case they unlimbo on the foundation's last cell (x0+w-1, y0+h-1).
// Every occupant is Unlimboed (forced, DAT_00A8D714++) at that one cell's
// centre and scattered; the clone fills the cell's infantry spots and
// spreads the rest to the nearest enterable cells around it (same gate)
// [RE-FALLBACK: the forced stacking / scatter].
void World::unload_occupants(int bi, bool place_inside) {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return;
    Building& b = buildings_[bi];
    b.firing_occupant = 0;
    if (b.occupants.empty()) return;
    diag::note("GARRISON unload-request building=%d house=%d occupants=%zu inside=%d",bi,b.house,b.occupants.size(),place_inside);
    const int x0 = b.cx, y0 = b.cy, w = b.fw, h = b.fh;
    const int lead = b.occupants.front();
    const bool physical_exit=btypes()[b.type].tank_bunker && b.alive &&
        b.state==Building::State::Idle && !b.selling && !place_inside && units_[lead].house==b.house;
    if(physical_exit && b.bunker_exit_left!=0) {
        if(b.bunker_exit_left<0)b.bunker_exit_left=18; // retail reverse hull: 9 frames at delay 2
        return;
    }
    PathTerrain pterr;
    std::vector<int> pgates;
    const PathTerrain* ptp = terrain_for(units_[static_cast<std::size_t>(lead)], pterr, pgates);
    // Can `ui` stand on (x,y), reached from (fx,fy) on layer fl? Returns the
    // layer (0 ground / 1 deck) or -1.
    auto enter = [&](int ui, int fx, int fy, int fl, int x, int y) -> int {
        if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return -1;
        const Unit& u = units_[static_cast<std::size_t>(ui)];
        if (!unit_can_enter_cell(u, x, y) || building_at_cell(x, y) >= 0) return -1;
        if (!occ_.can_enter(x, y, ui)) return -1;
        if (!ptp || !ptp->level) return 0;
        return path_step_layer(*ptp, grid_w_, fx, fy, fl, x, y);
    };
    std::vector<Cellxy> ring;
    for (int y = y0 + h; y >= y0 - 1; --y) ring.push_back({x0 + w, y});
    for (int x = x0 + w - 1; x >= x0 - 1; --x) ring.push_back({x, y0 + h});
    for (int x = x0; x <= x0 + w; ++x) ring.push_back({x, y0 - 1});
    for (int y = y0; y <= y0 + h; ++y) ring.push_back({x0 - 1, y});
    Cellxy exit{-1, -1};
    int exit_layer = 0;
    for (const Cellxy& c : ring) {
        const int l = enter(lead, x0, y0, 0, c.x, c.y);
        if (l >= 0) {
            if(physical_exit) {
                Unit probe=units_[lead];probe.cargo_of=-1;probe.garrison_of=-1;probe.bunker_exit_host=bi;
                PathTerrain route_terrain;std::vector<int> openings;
                const auto* terrain=terrain_for(probe,route_terrain,openings);
                const auto dock=bunker_docking_cell(b.cx,b.cy,b.fw,b.fh);
                const auto route=find_path_bounded_t(grid_for(probe.movement_domain),grid_w_,grid_h_,
                    {dock[0],dock[1]},c,2048,nullptr,terrain);
                if(route.size()<2)continue;
            }
            exit = c; exit_layer = l; break;
        }
    }
    const int house = b.house;
    // A blocked player unload keeps a tank safely inside its bunker.
    if (exit.x < 0 && b.alive && b.state == Building::State::Idle && !b.selling &&
        b.type >= 0 && btypes()[b.type].tank_bunker) return;
    std::vector<int> occ_list = b.occupants;
    b.occupants.clear();
    if (exit.x < 0 && !place_inside) {
        // KillOccupants(0): UnInit, no death anim, no kill credit.
        for (int ui : occ_list) {
            Unit& u = units_[static_cast<std::size_t>(ui)];
            u.garrison_of = -1;
            u.cargo_of = -1;
            u.cell_x = x0 + w - 1;
            u.cell_y = y0 + h - 1;
            u.fx = u.cell_x;
            u.fy = u.cell_y;
            ability_erase_unit(ui);
        }
        garrison_events_.push_back({GarrisonEvent::Unloaded, bi, house});
        sync_garrison_owner(bi);
        return;
    }
    const bool inside = exit.x < 0;
    if (inside) exit = Cellxy{x0 + w - 1, y0 + h - 1};
    auto put = [&](int ui, Cellxy c, int layer) {
        Unit& u = units_[static_cast<std::size_t>(ui)];
        if(btypes()[b.type].tank_bunker)reset_bunker_drive(u);
        u.cell_x = c.x;
        u.cell_y = c.y;
        u.fx = c.x;
        u.fy = c.y;
        u.guard_x = c.x;
        u.guard_y = c.y;
        u.on_deck = static_cast<std::uint8_t>(layer == 1 ? 1 : 0);
        if (map_) if (const ra2assets::MapCell* mc = map_->at(c.x, c.y)) u.level = mc->level;
        occ_.set(c.x, c.y, ui);
    };
    if(physical_exit && exit.x>=0) {
        const int ui=lead;
        b.occupants.clear();b.bunker_exit_left=-1;
        auto& u=units_[ui];u.garrison_of=-1;u.cargo_of=-1;u.bunker_exit_host=bi;
        const auto dock=bunker_docking_cell(b.cx,b.cy,b.fw,b.fh);
        put(ui,{dock[0],dock[1]},0);
        order_unit_move(ui,exit.x,exit.y);
        garrison_events_.push_back({GarrisonEvent::Unloaded,bi,house});
        return;
    }
    // The unlimbo loop runs from the LAST occupant down (0x458088: index
    // count-1 .. 0).
    for (auto it = occ_list.rbegin(); it != occ_list.rend(); ++it) {
        const int ui = *it;
        Unit& u = units_[static_cast<std::size_t>(ui)];
        u.garrison_of = -1;
        u.cargo_of = -1;
        if (inside || occ_.can_enter(exit.x, exit.y, ui)) { put(ui, exit, exit_layer); continue; }
        // The exit cell is full: the nearest enterable cell around it.
        Cellxy got{-1, -1};
        int got_layer = 0;
        for (int r = 1; r <= 3 && got.x < 0; ++r)
            for (int dy = -r; dy <= r && got.x < 0; ++dy)
                for (int dx = -r; dx <= r && got.x < 0; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int x = exit.x + dx, y = exit.y + dy;
                    if (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h) continue;   // the foundation
                    const int l = enter(ui, exit.x, exit.y, exit_layer, x, y);
                    if (l >= 0) { got = Cellxy{x, y}; got_layer = l; }
                }
        if (got.x < 0) {   // a failed Unlimbo deletes the unit (vtbl+0xF8)
            u.cell_x = exit.x;
            u.cell_y = exit.y;
            u.fx = exit.x;
            u.fy = exit.y;
            ability_erase_unit(ui);
            continue;
        }
        put(ui, got, got_layer);
    }
    garrison_events_.push_back({GarrisonEvent::Unloaded, bi, house});
    b.bunker_exit_left=-1;
    sync_garrison_owner(bi);
    diag::note("GARRISON unloaded building=%d owner=%d count=%zu exit=%d,%d",bi,b.house,occ_list.size(),exit.x,exit.y);
}

Cellxy World::nearest_exit_from_host(const Unit& u, int hx, int hy, int hlayer, int* layer) const {
    PathTerrain pterr;
    std::vector<int> pgates;
    const PathTerrain* ptp = terrain_for(u, pterr, pgates);
    const std::vector<std::uint8_t>& grid = grid_for(u.movement_domain);
    auto ok = [&](int x, int y, int& l) {
        if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return false;
        const std::size_t i = static_cast<std::size_t>(y) * grid_w_ + x;
        if (i >= grid.size() || !grid[i] || !occ_.is_free(x, y)) return false;
        l = 0;
        if (!ptp || !ptp->level) return true;
        l = path_step_layer(*ptp, grid_w_, hx, hy, hlayer, x, y);
        return l >= 0;
    };
    int l = 0;
    if (ok(hx, hy, l)) { if (layer) *layer = l; return {hx, hy}; }
    for (int r = 1; r <= grid_w_ + grid_h_; ++r) {
        for (int x = hx - r; x <= hx + r; ++x) {
            if (ok(x, hy - r, l)) { if (layer) *layer = l; return {x, hy - r}; }
            if (ok(x, hy + r, l)) { if (layer) *layer = l; return {x, hy + r}; }
        }
        for (int y = hy - r + 1; y <= hy + r - 1; ++y) {
            if (ok(hx - r, y, l)) { if (layer) *layer = l; return {hx - r, y}; }
            if (ok(hx + r, y, l)) { if (layer) *layer = l; return {hx + r, y}; }
        }
    }
    return {-1, -1};
}

// W7-02-10 / W7-01-38(a): the player gives an own firing garrison a target
// (unit or structure) like a defence [RE: SUSPECTED, BuildingClass
// What_Action for CanOccupyFire not traced]. Returns false if refused.
bool World::garrison_attack(int issuer, int bi, int unit_target, int bld_target) {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size()) || !garrison_firing(bi)) return false;
    Building& b = buildings_[static_cast<std::size_t>(bi)];
    if (!b.alive || !can_command(issuer, b.house)) return false;
    if (unit_target >= 0 && unit_target < static_cast<int>(units_.size())) {
        const Unit& v = units_[static_cast<std::size_t>(unit_target)];
        if (!v.alive || v.die_time >= 0.0 || v.cargo_of >= 0 || are_allied(b.house, v.house)) return false;
        b.garrison_target = unit_target;
        b.w7.garrison_target_building = -1;
        return true;
    }
    if (bld_target >= 0 && bld_target < static_cast<int>(buildings_.size()) && bld_target != bi) {
        const Building& o = buildings_[static_cast<std::size_t>(bld_target)];
        if (!o.alive || o.state == Building::State::Dead || are_allied(b.house, o.house)) return false;
        b.garrison_target = -1;
        b.w7.garrison_target_building = bld_target;
        return true;
    }
    return false;
}

bool World::garrison_firing(int bi) const {   // vtable +0x400 = FUN_00458DC0
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    const Building& b = buildings_[bi];
    if (b.type < 0 || b.type >= static_cast<int>(btypes().size())) return false;
    const BuildingTypeDef& t = btypes()[b.type];
    return t.can_be_occupied && t.can_occupy_fire && !b.occupants.empty();
}

// TechnoClass::GetWeaponRange (FUN_006F6750) for a garrison: the weapon's own
// Range is replaced by (vtable +0x404 = FUN_00458DF0: min(foundation W, H) / 2)
// + OccupyWeaponRange.
double World::garrison_range(int bi) const {
    const Building& b = buildings_[bi];
    if (btypes()[b.type].tank_bunker && !b.occupants.empty()) {
        const Unit& u=units_[b.occupants.front()];
        return u.occupy_w.range+btypes()[b.type].bunker_range;
    }
    return std::min(b.fw, b.fh) / 2 + depth_rules_.occupy_weapon_range;
}

std::vector<World::GarrisonEvent> World::take_garrison_events() {
    std::vector<GarrisonEvent> out;
    out.swap(garrison_events_);
    return out;
}

void World::step_garrison_combat(double dt) {
    const int nb = static_cast<int>(buildings_.size());
    const int nu = static_cast<int>(units_.size());
    for (int bi = 0; bi < nb; ++bi) {
        Building& b = buildings_[bi];
        if (!b.alive || b.state != Building::State::Idle || !garrison_firing(bi) || b.attack_order_kind) continue;
        if (b.garrison_rearm_frames > 0) --b.garrison_rearm_frames;   // Net L0: whole frames
        b.def_fire_flash = std::max(0.0, b.def_fire_flash - dt);
        if (b.firing_occupant < 0 || b.firing_occupant >= static_cast<int>(b.occupants.size()))
            b.firing_occupant = 0;
        // BuildingClass::GetWeapon (FUN_004526E0): the FiringOccupantIndex
        // occupant's OccupyWeapon (EliteOccupyWeapon when that occupant is elite).
        Unit& occ = units_[b.occupants[b.firing_occupant]];
        const bool elite = occ.vet.recv ? is_elite(occ.vet.veterancy) : occ.veteran_rank >= 2;
        WeaponSlot w = (elite && occ.occupy_w_elite.valid) ? occ.occupy_w_elite : occ.occupy_w;
        const BuildingTypeDef& type=btypes()[b.type];
        if (type.tank_bunker && occ.house!=b.house) continue;
        if(type.tank_bunker && occ.gattling.enabled()) {
            occ.gattling.step(b.garrison_target>=0 || b.w7.garrison_target_building>=0,elite);
            w=occ.gattling.weapon(0,elite);
        }
        auto bunker_slot = [&](const Unit& target) {
            const int slot=unit_weapon_for_unit(occ,target);
            if(occ.gattling.enabled() && slot>=0) return occ.gattling.weapon(slot,elite);
            return slot==1 ? secondary_view(occ) : slot==0 ?
                ((elite && occ.occupy_w_elite.valid) ? occ.occupy_w_elite : occ.occupy_w) : WeaponSlot{};
        };
        if (!w.valid) continue;
        const double range = garrison_range(bi);
        const double cx = b.cx + (b.fw - 1) / 2.0, cy = b.cy + (b.fh - 1) / 2.0;
        auto engageable = [&](int j) {
            if (j < 0 || j >= nu) return false;
            const Unit& v = units_[j];
            if (!v.alive || v.die_time >= 0.0 || !unit_combat_exposed(v)) return false;
            if (!is_enemy(b.house, v.house) || disguise_fools(b.house, v)) return false;   // W7-01-02
            if (!v.seen_by_house(b.house)) return false;   // fv7c: 32-bit house mask
            if (!slot_can_target(type.tank_bunker ? bunker_slot(v) : w, unit_airborne(v), v.armor_ix)) return false;
            return om::hypot(v.fx - cx, v.fy - cy) <= range + 0.5;
        };
        // W7-02-10: a structure target (an order, or the defence rule's
        // armed / occupied enemy building when no unit is in range).
        const int nb_all = static_cast<int>(buildings_.size());
        auto bld_engageable = [&](int j, bool ordered) {
            if (j < 0 || j >= nb_all || j == bi) return false;
            const Building& o = buildings_[static_cast<std::size_t>(j)];
            if (!o.alive || o.state == Building::State::Dead || !is_enemy(b.house, o.house)) return false;
            if (o.type < 0 || o.type >= static_cast<int>(btypes().size())) return false;
            const BuildingTypeDef& ot = btypes()[static_cast<std::size_t>(o.type)];
            if (ot.wall || o.iron_left_frames > 0) return false;
            if (!ordered) {
                if (ot.insignificant) return false;
                const bool armed = ot.armed || (ot.can_occupy_fire && !o.occupants.empty());
                if (!armed && !(house_is_computer(b.house) && ai_threat_any_building())) return false;
                if (!object_visible_at(b.house, o.cx, o.cy)) return false;
            }
            if (!slot_can_target(w, false, o.armor_ix)) return false;
            double best = -1.0;
            for (const Cellxy& c : foundation_cells(o.cx, o.cy, o.fw, o.fh)) {
                const double d = om::hypot(c.x - cx, c.y - cy);
                if (best < 0.0 || d < best) best = d;
            }
            return best >= 0.0 && best <= range + 0.5;
        };
        if (b.garrison_target >= 0 && b.w7.garrison_target_building >= 0) b.w7.garrison_target_building = -1;
        if (!engageable(b.garrison_target)) {
            b.garrison_target = -1;
            if (!bld_engageable(b.w7.garrison_target_building, true)) {
                b.w7.garrison_target_building = -1;
                double best = 0.0;
                for (int j = 0; j < nu; ++j) {
                    if (!engageable(j)) continue;
                    const double d = om::hypot(units_[j].fx - cx, units_[j].fy - cy);
                    if (b.garrison_target < 0 || d < best) { b.garrison_target = j; best = d; }
                }
                if (b.garrison_target < 0)
                    for (int j = 0; j < nb_all; ++j) {
                        if (!bld_engageable(j, false)) continue;
                        const Building& o = buildings_[static_cast<std::size_t>(j)];
                        const double d = om::hypot(o.cx + (o.fw - 1) / 2.0 - cx, o.cy + (o.fh - 1) / 2.0 - cy);
                        if (b.w7.garrison_target_building < 0 || d < best) { b.w7.garrison_target_building = j; best = d; }
                    }
            }
        }
        const int tbld = b.garrison_target < 0 ? b.w7.garrison_target_building : -1;
        if (type.tank_bunker && (b.garrison_target>=0 || tbld>=0)) {
            const double tx=tbld>=0 ? buildings_[tbld].cx+(buildings_[tbld].fw-1)/2.0 : units_[b.garrison_target].fx;
            const double ty=tbld>=0 ? buildings_[tbld].cy+(buildings_[tbld].fh-1)/2.0 : units_[b.garrison_target].fy;
            units_[b.occupants.front()].turret_facing=facing_u16_from_delta(tx-cx,ty-cy);
        }
        if ((b.garrison_target < 0 && tbld < 0) || b.garrison_rearm_frames > 0.0) continue;
        if (type.tank_bunker && b.garrison_target >= 0) w=bunker_slot(units_[b.garrison_target]);
        const double rof_mult=type.tank_bunker ? type.bunker_rof : depth_rules_.occupy_rof_mult;
        const double damage_mult=type.tank_bunker ? type.bunker_damage : depth_rules_.occupy_damage_mult;
        // TechnoClass::GetROF (FUN_006FC460): ROF / OccupantCount, then
        // ftol(ROF / OccupyROFMultiplier) when the multiplier is > 1.0.
        // W7-02-10 [RE: GetROF FUN_006FC460 @0x6FC570/0x6FC594]: the
        // veterancy is the BUILDING's own (never veteran), the 0..2 jitter is
        // rearm_frames_for's Random(0,2).
        int rof = w.rof / static_cast<int>(b.occupants.size());
        if (rof_mult > 1.0) rof = ftol(rof / rof_mult);
        if (w7_rules_.armed) {
            // W7-07-14 [RE: FUN_006FC460]: ftol((ROF * houseROF + Random(0,2))
            // / occupants / OccupyROFMultiplier) -- the jitter and the house
            // ROF before the divisions (ROF 20, 5 occupants: a constant 3).
            double t = w.rof * house_rof_mul(b.house) + combat_rng_.random(0, 2);
            t /= static_cast<double>(b.occupants.size());
            if (rof_mult > 1.0) t /= rof_mult;
            b.garrison_rearm_frames = ftol(t);
        } else {
            b.garrison_rearm_frames = rearm_frames_for(rof, &combat_rng_);
        }
        b.def_fire_flash = kFireFlashSecs;
        // TechnoClass::Fire (FUN_006FD210): damage x OccupyDamageMultiplier.
        Unit shooter;
        shooter.house = b.house;
        shooter.fx = cx;
        shooter.fy = cy;
        shooter.cell_x = static_cast<int>(om::lround(cx));
        shooter.cell_y = static_cast<int>(om::lround(cy));
        shooter.w_damage = ftol(w.damage * damage_mult);
        shooter.w_rof = rof;
        shooter.w_range = range;
        shooter.w_verses = w.verses;
        shooter.w_cell_spread = w.cell_spread;
        shooter.w_percent_at_max = w.percent_at_max;
        shooter.w_inf_death = w.inf_death;          // W3-B
        shooter.w_prone_damage = w.prone_damage;
        shooter.alive = false;
        if (tbld >= 0) {   // W7-02-10: a garrison shot at a structure
            fire_events_.push_back({-1, bi, -1, tbld, 0, w.id});
            ++garrison_shots_;
            const Building& o = buildings_[static_cast<std::size_t>(tbld)];
            int ax = 0, ay = 0, az = 0;
            building_aim_lept(o, cell_level(o.cx, o.cy), ax, ay, az);
            damage_area(*this, units_, buildings_, occ_, shooter, -1, bi, ax, ay, az);
            b.firing_occupant = (b.firing_occupant + 1) % static_cast<int>(b.occupants.size());
            continue;
        }
        fire_events_.push_back({-1, bi, b.garrison_target, -1, 0, w.id});
        ++garrison_shots_;
        // UC weapons fly invisible projectiles: delivered as instant hits
        // [RE-FALLBACK: garrison shots are not routed through step_projectiles].
        if (w.cell_spread >= 1.0)
            apply_spread_damage(units_, occ_, shooter, units_[b.garrison_target]);
        else
            deliver_hit_from(units_, occ_, shooter, -1, bi, b.garrison_target);
        {   // Wave-5 (item 8): the firing occupant earns the kill (garrisoned
            // infantry gain rank in the original) [RE-FALLBACK: the credit
            // routing through BuildingClass::Fire was not traced].
            Unit& v = units_[b.garrison_target];
            const int fo = b.occupants[static_cast<std::size_t>(b.firing_occupant) % b.occupants.size()];
            if (v.die_time >= 0.0 && v.vet.killed_by < 0) v.vet.killed_by = fo;
        }
        // FiringOccupantIndex = (index + 1) % OccupantCount after every shot
        // (TechnoClass::Fire @0x6FE525).
        b.firing_occupant = (b.firing_occupant + 1) % static_cast<int>(b.occupants.size());
    }
}

// ------------------------------------------------------------------ bridges

void World::set_bridges(std::vector<int> overlay, std::vector<ra2assets::LandType> tile_land) {
    bridge_ovl_.assign(static_cast<std::size_t>(grid_w_) * grid_h_, -1);
    for (std::size_t i = 0; i < overlay.size() && i < bridge_ovl_.size(); ++i)
        if (is_low_bridge_overlay(overlay[i])) bridge_ovl_[i] = static_cast<std::int16_t>(overlay[i]);
    bridge_tile_land_.assign(bridge_ovl_.size(), static_cast<std::uint8_t>(ra2assets::LandType::Clear));
    for (std::size_t i = 0; i < tile_land.size() && i < bridge_tile_land_.size(); ++i)
        bridge_tile_land_[i] = static_cast<std::uint8_t>(tile_land[i]);
    ++bridge_version_;
}

int World::bridge_overlay_at(int x, int y) const {
    if (bridge_ovl_.empty() || x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return -1;
    return bridge_ovl_[static_cast<std::size_t>(y) * grid_w_ + x];
}

// Destroyed low-bridge cells right now (maps may ship a bridge already blown,
// e.g. May Day's river crossings, to be repaired at the hut).
int World::bridge_cells_destroyed() const {
    int n = 0;
    for (int v : bridge_ovl_)
        if (v >= 0 && low_bridge_destroyed(v)) ++n;
    for (std::uint8_t v : hbridge_)
        if ((v & 3) && (v & 4)) ++n;   // W4-SQ-10: broken high-bridge cells
    return n;
}

// CellClass::Recalc_Attributes analogue for one bridge cell: an intact /
// damaged low section is Land=Road with NoUseTileLandType=true (ground
// passable, no ships); a destroyed one (LOBRDG27/28, NoUseTileLandType=false)
// falls back to the tile's land type (water under the river span). The
// LandType the rest of the engine reads follows bridge_ovl_ (speed_land_at,
// W8-01-04); the passability grids are rewritten here.
void World::set_bridge_cell(int x, int y, int idx) {
    const std::size_t i = static_cast<std::size_t>(y) * grid_w_ + x;
    const bool was_destroyed = low_bridge_destroyed(bridge_ovl_[i]);
    bridge_ovl_[i] = static_cast<std::int16_t>(idx);
    const ra2assets::LandType lt = low_bridge_destroyed(idx)
                                       ? static_cast<ra2assets::LandType>(bridge_tile_land_[i])
                                       : ra2assets::LandType::Road;
    if (i < passable_.size()) passable_[i] = blocks_ground(lt) ? 0 : 1;
    if (i < water_passable_.size()) water_passable_[i] = (lt == ra2assets::LandType::Water) ? 1 : 0;
    // fv7c: Beach is the amphibious/hover-only grid (W8-01-06: a section
    // broken over Beach is swimmable, not walkable).
    if (i < beach_.size()) beach_[i] = (lt == ra2assets::LandType::Beach) ? 1 : 0;
    ++bridge_version_;
    if (was_destroyed == low_bridge_destroyed(idx)) return;   // a damage stage: same land
    low_bridge_cell_kill(x, y, low_bridge_destroyed(idx));
}

// W8-01-03 / W8-03-07 [RE: FUN_00487920, called on the three rewritten cells
// after FUN_0047D1C0 by the section rewrites FUN_0057B720 / FUN_0057BCE0 / ...]:
// every object in the cell's (ground) list that can no longer enter it
// (vt+0x1AC CanEnterCell == Move::No, for a unit FUN_0073DFA0) takes lethal
// C4Warhead damage with ignoreDefenses=1 and preventPassengerEscape=1; a
// landed AircraftType (WhatAmI 2) dies too, and in the destroy pass so does a
// JumpJet type (+0xD94). So a Tanya / SEAL / Yuri Prime / Robot Tank on a
// section collapsing into Water or Beach lives; a GI or a tank dies. Then the
// 5x5 around the cell: a mover whose locomotor is heading into the cell
// (vt+0xA0) and that cannot enter it dies the same way. [PROBABLE: an object
// in flight is not in the cell list; the clone skips airborne units. The
// repair rewrite is taken as the non-destroy pass (param 0).]
void World::low_bridge_cell_kill(int x, int y, bool destroy) {
    const int nu = static_cast<int>(units_.size());
    for (int j = 0; j < nu; ++j) {
        const Unit& u = units_[static_cast<std::size_t>(j)];
        if (!collapse_candidate(u) || u.cell_x != x || u.cell_y != y) continue;
        const bool kill = !unit_can_enter_cell(u, x, y) || (u.is_aircraft && u.an.t.fixed_wing) ||
                          (destroy && jumpjet_type(u));
        if (kill) bridge_kill_unit(j);
    }
    for (int j = 0; j < nu; ++j) {
        const Unit& u = units_[static_cast<std::size_t>(j)];
        if (!collapse_candidate(u) || u.is_aircraft || !u.moving()) continue;
        if (u.cell_x == x && u.cell_y == y) continue;
        if (std::abs(u.cell_x - x) > 2 || std::abs(u.cell_y - y) > 2) continue;
        std::size_t pi = u.path_idx;
        while (pi < u.path.size() && u.path[pi].x == u.cell_x && u.path[pi].y == u.cell_y) ++pi;
        if (pi >= u.path.size() || u.path[pi].x != x || u.path[pi].y != y) continue;
        if (!unit_can_enter_cell(u, x, y)) bridge_kill_unit(j);
    }
}

// The collapse kill [RE: FUN_0047DC80 / FUN_00487920 / FUN_005F3680 ->
// ReceiveDamage(&Health, 0, Rules+0xFA8 C4Warhead, 0, ignoreDefenses=1,
// preventPassengerEscape=1, 0)]: lethal through the Iron Curtain, and a
// transport's passengers die with it (W8-03-06; KillPassengers, not the
// W7-04-02 escape of step_cargo).
void World::bridge_kill_unit(int ui) {
    if (ui < 0 || ui >= static_cast<int>(units_.size())) return;
    Unit& t = units_[static_cast<std::size_t>(ui)];
    if (!t.alive || t.die_time >= 0.0) return;
    for (int p : t.cargo) {
        if (p < 0 || p >= static_cast<int>(units_.size())) continue;
        Unit& u = units_[static_cast<std::size_t>(p)];
        u.cargo_of = -1;
        u.alive = false;   // tombstone directly (no die anim), as step_cargo's kill-all
        u.die_time = -1.0;
        u.hp = 0;
        u.cell_x = t.cell_x;
        u.cell_y = t.cell_y;
        u.fx = t.cell_x;
        u.fy = t.cell_y;
    }
    t.cargo.clear();
    t.unload_pending = false;
    ability_kill_unit(ui);
}

// MapClass::DamageArea's bridge part (FUN_00489190): an explosion in a low
// bridge cell with a Wall=yes warhead breaks the section when
// Random(1, BridgeStrength) < damage (the IonCannonWarhead always does); the
// section rewrite is FUN_0057B720 / FUN_0057BCE0 over the 3-cell
// cross-section FUN_0057B4D0 centres. BridgeDestruction=no disables it.
int World::bridge_explosion(int cx, int cy, int damage, bool wall_warhead, int z_abs) {
    if ((bridge_ovl_.empty() && hbridge_.empty()) || !wall_warhead || !depth_rules_.armed ||
        !depth_rules_.bridge_destruction)
        return 0;
    const int idx = bridge_overlay_at(cx, cy);
    if (!is_low_bridge_overlay(idx) || low_bridge_destroyed(idx)) {
        // W4-SQ-10 [RE: FUN_00489190 @0x489DCB..0x489F1E concrete, @0x489FB5..
        // 0x48A0C6 wood]: the same Wall= warhead + Random(1, BridgeStrength) <
        // damage roll breaks the high span (FUN_00586BB0).
        if (!high_bridge_cell(cx, cy) || high_bridge_destroyed(cx, cy)) return 0;
        // W8-04-03 / W8-05-05 / W8-03-09 [RE: 0x489E87 / 0x48A012]: on a cell
        // with the bridge flag (+0x140 & 0x100) the roll needs the explosion Z
        // in ((Level-2)*104 + BridgeHeight, (Level+1)*104 + BridgeHeight]
        // (Level +0x11B, DAT_0089D7E0 = 104, DAT_0089D7D4 = BridgeHeight 416):
        // a shell at a ship under the span, or an air burst more than a level
        // above the deck, never rolls.
        const int lv = cell_level(cx, cy);
        if (!(z_abs > (lv - 2) * 104 + 416 && z_abs <= (lv + 1) * 104 + 416)) return 0;
        if (!(bridge_rng_.random(1, std::max(1, depth_rules_.bridge_strength)) < damage)) return 0;
        return break_high_bridge(cx, cy);
    }
    if (!(bridge_rng_.random(1, std::max(1, depth_rules_.bridge_strength)) < damage)) return 0;
    auto at = [&](int x, int y) { return bridge_overlay_at(x, y); };
    int sx = cx, sy = cy;
    low_bridge_section_centre(cx, cy, at, sx, sy);
    const LowAxis ax = low_bridge_axis(bridge_overlay_at(sx, sy));
    if (ax == LowAxis::None) return 0;
    int xs[3], ys[3];
    low_bridge_section_cells(sx, sy, ax, xs, ys);
    int changed = 0;
    for (int k = 0; k < 3; ++k) {
        const int o = bridge_overlay_at(xs[k], ys[k]);
        if (!is_low_bridge_overlay(o)) continue;
        const int nx = low_bridge_next_stage(o);
        if (nx == o) continue;
        set_bridge_cell(xs[k], ys[k], nx);
        ++changed;
    }
    return changed;
}

// An engineer entering a BridgeRepairHut (InfantryClass Mission_Enter,
// FUN_00519449: engineer +0xEC3 && type +0x16B6): RepairBridgeSound, then the
// bridge found within +-2 cells is restored (FUN_00573070 / FUN_0056FB80).
// The clone restores every damaged/destroyed low section of the bridge run
// connected to a low-bridge cell within 2 cells of the hut's foundation to an
// intact piece (low_bridge_repaired) [RE-FALLBACK: the per-section restore
// walk of FUN_0057EE70 is not traced]. Returns the number of cells restored.
int World::deck_z(int x, int y) const {
    const int lv = cell_level(x, y);
    return (high_bridge_cell(x, y) && !high_bridge_destroyed(x, y)) ? (lv + 4) * 104 : lv * 104;
}

// W8-05-04 / W8-06-04 [RE: DamageArea FUN_00489190 @0x489DCB: the bridge roll
// sits in the area applicator itself, so every DamageArea caller rolls it --
// rockets FUN_00662A70 @0x662D07, the death weapon FUN_0070CAC0 -> bullet
// FUN_004690A0 @0x4699A0, the Lightning Storm bolt FUN_0053A030]: the blast's
// own cell, a Wall=yes warhead (+0x144).
int World::bridge_area_roll(double cx, double cy, int damage, int wall_flags, int z_abs) {
    if (bridge_ovl_.empty() && hbridge_.empty()) return 0;
    const int x = static_cast<int>(om::lround(cx)), y = static_cast<int>(om::lround(cy));
    if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return 0;
    return bridge_explosion(x, y, damage, (wall_flags & kW6Wall) != 0, z_abs);
}

int World::repair_bridge_near(int bi) {
    if (bridge_ovl_.empty()) return repair_high_bridge_near(bi);   // W4-SQ-10
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return 0;
    const Building& b = buildings_[bi];
    std::vector<Cellxy> stack;
    std::vector<std::uint8_t> seen(bridge_ovl_.size(), 0);
    for (int y = b.cy - 2; y < b.cy + b.fh + 2; ++y)
        for (int x = b.cx - 2; x < b.cx + b.fw + 2; ++x)
            if (is_low_bridge_overlay(bridge_overlay_at(x, y))) stack.push_back({x, y});
    int restored = 0;
    while (!stack.empty()) {
        const Cellxy c = stack.back();
        stack.pop_back();
        const std::size_t i = static_cast<std::size_t>(c.y) * grid_w_ + c.x;
        if (seen[i]) continue;
        seen[i] = 1;
        const int fixed = low_bridge_repaired(bridge_ovl_[i]);
        if (fixed != bridge_ovl_[i]) {
            set_bridge_cell(c.x, c.y, fixed);
            ++restored;
        }
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = c.x + dx, y = c.y + dy;
                if ((dx || dy) && is_low_bridge_overlay(bridge_overlay_at(x, y))) stack.push_back({x, y});
            }
    }
    if (restored == 0) restored = repair_high_bridge_near(bi);   // W4-SQ-10
    return restored;
}

// ------------------------------------------------------------- high bridges
// W4-SQ-10. A high bridge is its deck overlays (BRIDGE1/2, BRIDGEB1/B2) over
// the river plus the Ovrps/Ovrpsb ramp tiles at its ends (temperat.ini
// BridgeSet 19 / WoodBridgeSet 80). The destroy functions (FUN_005766D0
// concrete, FUN_00570FC0 wood) walk the span both ways, swap the ramp tiles
// for broken ends, take the deck away and kill what stands on it
// (FUN_0047DC80 on each section). [RE-FALLBACK: the clone takes the whole
// connected deck at once; the ramp tiles keep their art; the deck overlays
// stop drawing.]
void World::set_high_bridges(std::vector<std::uint8_t> cells) {
    hbridge_ = std::move(cells);
    hbridge_.resize(static_cast<std::size_t>(grid_w_) * grid_h_, 0);
    for (std::size_t i = 0; i < hbridge_.size() && i < passable_.size(); ++i)
        if ((hbridge_[i] & 3) == 1) passable_[i] = 1;   // the deck carries ground units (water grid kept)
    ++bridge_version_;
}

// W8-01-01/02, W8-02-01, W8-03-05, W8-06-02/03 [RE: FUN_0047DC80, called on
// each cross-section cell by the destroy walks FUN_005766D0 / FUN_00570FC0]:
// 1) every object in the cell's ground list (+0xE4: ships, swimmers, hover
//    and ground units UNDER the span) takes ReceiveDamage(Health, C4Warhead,
//    ignoreDefenses=1, preventPassengerEscape=1);
// 2) every object in the deck list (+0xE8) gets vt+0xEC = FUN_005F3970
//    (IsFallingDown +0x8D, die-on-landing +0x8F, OnBridge cleared) and on
//    touchdown FUN_005F3680 deals the same lethal hit.
// So every object of the cell dies, on the deck or under it, whatever its
// layer: the clone kills them all at once [RE-FALLBACK: no fall animation].
// Layer-agnostic by design (the deck/ground occupancy split lives elsewhere):
// every unit whose cell is the deck cell is iterated, not the cell's single
// occupancy owner -- up to three infantry share a cell (W5-B sub-cells).
void World::high_bridge_cell_kill(int x, int y) {
    const int nu = static_cast<int>(units_.size());
    for (int j = 0; j < nu; ++j) {
        const Unit& u = units_[static_cast<std::size_t>(j)];
        if (collapse_candidate(u) && u.cell_x == x && u.cell_y == y) bridge_kill_unit(j);
    }
}

bool World::high_bridge_cell(int x, int y) const {
    if (hbridge_.empty() || x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return false;
    return (hbridge_[static_cast<std::size_t>(y) * grid_w_ + x] & 3) != 0;
}

bool World::high_bridge_destroyed(int x, int y) const {
    if (!high_bridge_cell(x, y)) return false;
    return (hbridge_[static_cast<std::size_t>(y) * grid_w_ + x] & 4) != 0;
}

int World::break_high_bridge(int x, int y) {
    if (hbridge_.empty()) return 0;
    int sx = -1, sy = -1;
    for (int r = 0; r <= 1 && sx < 0; ++r)
        for (int dy = -r; dy <= r && sx < 0; ++dy)
            for (int dx = -r; dx <= r && sx < 0; ++dx)
                if (high_bridge_cell(x + dx, y + dy) && !high_bridge_destroyed(x + dx, y + dy)) {
                    sx = x + dx;
                    sy = y + dy;
                }
    if (sx < 0) return 0;
    std::vector<Cellxy> stack{{sx, sy}};
    int n = 0;
    while (!stack.empty()) {
        const Cellxy c = stack.back();
        stack.pop_back();
        const std::size_t i = static_cast<std::size_t>(c.y) * grid_w_ + c.x;
        if (hbridge_[i] & 4) continue;
        hbridge_[i] |= 4;
        ++n;
        if ((hbridge_[i] & 3) == 1) passable_[i] = 0;
        high_bridge_cell_kill(c.x, c.y);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx || dy) && high_bridge_cell(c.x + dx, c.y + dy) && !high_bridge_destroyed(c.x + dx, c.y + dy))
                    stack.push_back({c.x + dx, c.y + dy});
    }
    ++bridge_version_;
    return n;
}

// W7-08-19 [RE: InfantryClass::What_Action 0x51E17C L60-63 -> FUN_00586E40]:
// over a BridgeRepairHut the engineer gets Enter only when a bridge within the
// hut's scan needs repair (else NoEnter): the read-only twin of the two
// repair walks above (same cells, same connected flood).
bool World::bridge_needs_repair_near(int bi) const {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    const Building& b = buildings_[bi];
    if (!bridge_ovl_.empty()) {
        std::vector<Cellxy> stack;
        std::vector<std::uint8_t> seen(bridge_ovl_.size(), 0);
        for (int y = b.cy - 2; y < b.cy + b.fh + 2; ++y)
            for (int x = b.cx - 2; x < b.cx + b.fw + 2; ++x)
                if (is_low_bridge_overlay(bridge_overlay_at(x, y))) stack.push_back({x, y});
        while (!stack.empty()) {
            const Cellxy c = stack.back();
            stack.pop_back();
            const std::size_t i = static_cast<std::size_t>(c.y) * grid_w_ + c.x;
            if (seen[i]) continue;
            seen[i] = 1;
            if (low_bridge_repaired(bridge_ovl_[i]) != bridge_ovl_[i]) return true;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = c.x + dx, y = c.y + dy;
                    if ((dx || dy) && is_low_bridge_overlay(bridge_overlay_at(x, y))) stack.push_back({x, y});
                }
        }
    }
    if (hbridge_.empty()) return false;
    for (int y = b.cy - kHighBridgeRepairScan; y < b.cy + b.fh + kHighBridgeRepairScan; ++y)
        for (int x = b.cx - kHighBridgeRepairScan; x < b.cx + b.fw + kHighBridgeRepairScan; ++x)
            if (high_bridge_destroyed(x, y)) return true;
    return false;
}

int World::repair_high_bridge_near(int bi) {
    if (hbridge_.empty() || bi < 0 || bi >= static_cast<int>(buildings_.size())) return 0;
    const Building& b = buildings_[bi];
    std::vector<Cellxy> stack;
    // The deck starts beyond the ramp cells, so scan farther than the hut's
    // low-bridge overlay check. Keep this radius in sync with the Enter test.
    for (int y = b.cy - kHighBridgeRepairScan; y < b.cy + b.fh + kHighBridgeRepairScan; ++y)
        for (int x = b.cx - kHighBridgeRepairScan; x < b.cx + b.fw + kHighBridgeRepairScan; ++x)
            if (high_bridge_destroyed(x, y)) stack.push_back({x, y});
    int n = 0;
    while (!stack.empty()) {
        const Cellxy c = stack.back();
        stack.pop_back();
        const std::size_t i = static_cast<std::size_t>(c.y) * grid_w_ + c.x;
        if (!(hbridge_[i] & 4)) continue;
        hbridge_[i] = static_cast<std::uint8_t>(hbridge_[i] & 3);
        ++n;
        if ((hbridge_[i] & 3) == 1) passable_[i] = 1;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx || dy) && high_bridge_destroyed(c.x + dx, c.y + dy)) stack.push_back({c.x + dx, c.y + dy});
    }
    if (n) ++bridge_version_;
    return n;
}

int World::force_bridge_break(int x, int y) {
    const int idx = bridge_overlay_at(x, y);
    if (is_low_bridge_overlay(idx) && !low_bridge_destroyed(idx)) {
        auto at = [&](int ax, int ay) { return bridge_overlay_at(ax, ay); };
        int sx = x, sy = y;
        low_bridge_section_centre(x, y, at, sx, sy);
        const LowAxis ax = low_bridge_axis(bridge_overlay_at(sx, sy));
        if (ax == LowAxis::None) return 0;
        int xs[3], ys[3];
        low_bridge_section_cells(sx, sy, ax, xs, ys);
        int changed = 0;
        for (int k = 0; k < 3; ++k) {
            const int o = bridge_overlay_at(xs[k], ys[k]);
            if (!is_low_bridge_overlay(o)) continue;
            const int nx = low_bridge_next_stage(o);
            if (nx == o) continue;
            set_bridge_cell(xs[k], ys[k], nx);
            ++changed;
        }
        return changed;
    }
    return break_high_bridge(x, y);
}

void World::order_attack_cell(int ui, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= grid_w_ || cy >= grid_h_) return;
    if (ui < 0 || ui >= static_cast<int>(units_.size())) return;
    Unit& u = units_[ui];
    if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.w_damage <= 0) return;
    order_stop(ui); // replace an earlier movement or attack-move mission
    u.target = -1;
    u.target_building = -1;
    u.occupy_target = -1;
    u.attack_order = true;
    u.attack_cell_x = cx;
    u.attack_cell_y = cy;
}

int World::group_attack_cell(int issuer, const std::vector<int>& ids, int cx, int cy) {
    // R3: every selected unit the player commands -- house 0 and the
    // PlayerControl houses (W2-CA-02), as order_attack / order_move do since
    // W5-A. The original issues the order for each CurrentObjects entry the
    // player owns; there is no Owner == PlayerPtr test (see order_stop_selected).
    int n = 0;
    for (int i : ids)
        if (i >= 0 && i < static_cast<int>(units_.size()) && can_command(issuer, units_[i].house) &&
            units_[i].cargo_of < 0) {
            order_attack_cell(i, cx, cy);
            ++n;
        }
    return n;

}

// ------------------------------------------------------------------ the pass

void World::step_depth(double dt) {
    // Ground force-fire uses the same flight solver and muzzle as object fire.
    // Only instant weapons retain the immediate cell-hit path.
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[i];
        if (!u.cell_shot) continue;
        u.cell_shot = false;
        const int x = u.attack_cell_x, y = u.attack_cell_y;
        if (projectiles_enabled_ && u.w_proj.flies()) {
            launch_projectile(i,-1,-1,x,y);
            continue;
        }
        const bool wall = u.w_active == 1 ? u.w2.wall : u.p_wall;
        // [RE-FALLBACK: a ground-fire shell at a bridge cell bursts at the
        // cell's bridge coordinates (GetCoordsWithBridge): the deck height]
        // Resolve the intended layer before a collapse clears its occupants.
        // Force-fire at bridge coordinates must not hit a ship underneath.
        const int victim = occ_.at(x, y, deck_intact(x,y) ? 1 : 0);
        bridge_explosion(x, y, u.w_damage, wall, deck_z(x, y));
        if (victim >= 0 && victim != i && victim < static_cast<int>(units_.size()))
            deliver_unit_hit(units_, occ_, i, victim);
        const int bb = building_at_cell(x, y);
        // W6-04-04/05: a wall (built or a map overlay) takes Reduce_Wall per
        // the warhead's wall keys, not an HP hit.
        if (bb >= 0 && !w6_wall_building_hit(*this, units_, buildings_, bb, u.w6.wall_flags, u.w_damage))
            deliver_building_hit(units_, buildings_, i, bb);
        w6_overlay_wall_hit(x, y, u.w6.wall_flags, u.w_damage);
    }
    if (!garrison_any_) return;
    for(int ui=0;ui<int(units_.size());++ui) {
        auto& u=units_[ui];const int host=u.bunker_exit_host;
        if(host<0)continue;
        if(!u.alive || u.die_time>=0 || u.cargo_of>=0 || host>=int(buildings_.size()) ||
           !buildings_[host].alive || u.house!=buildings_[host].house) {u.bunker_exit_host=-1;continue;}
        const auto& b=buildings_[host];
        if(u.fx<b.cx-0.5 || u.fy<b.cy-0.5 || u.fx>b.cx+b.fw-0.5 || u.fy>b.cy+b.fh-0.5)
            u.bunker_exit_host=-1;
    }
    for(int bi=0;bi<int(buildings_.size());++bi) {
        auto& b=buildings_[bi];
        if(b.occupants.empty())b.bunker_exit_left=-1;
        if(b.bunker_exit_left>0)--b.bunker_exit_left;
        if(b.bunker_exit_left==0 && !b.occupants.empty())unload_occupants(bi);
    }
    // Occupy arrivals (the step_capture analogue).
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[i];
        if (u.occupy_target < 0) continue;
        const int bi = u.occupy_target;
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || !can_occupy(i, bi)) {
            u.occupy_target = -1;
            continue;
        }
        // Drive reserves the next cell before the hull reaches it. Do not
        // consume a bunker approach on that reservation: finish the track
        // first, otherwise entry jumps from a still-moving hull position.
        const bool bunker=btypes()[buildings_[bi].type].tank_bunker;
        if(bunker) {
            const auto& b=buildings_[bi];
            const auto dock=bunker_docking_cell(b.cx,b.cy,b.fw,b.fh);
            if(u.cell_x==dock[0] && u.cell_y==dock[1] && !u.moving()) {
                const double desired=bunker_horizontal_facing(u.body_facing);
                u.body_facing=turn_toward(u.body_facing,desired,(u.rot_u16_per_sec>0?u.rot_u16_per_sec:16384)*dt);
                if(std::abs(std::remainder(u.body_facing-desired,65536.0))<1)occupy_now(i,bi);
            } else if(!u.moving())order_occupy(i,bi);
            continue;
        }
        if (cheb_to_foundation(buildings_[bi], u.cell_x, u.cell_y) <= 1 &&
            (!bunker || (!u.moving() && std::abs(u.fx-u.cell_x)<0.01 && std::abs(u.fy-u.cell_y)<0.01))) {
            occupy_now(i, bi);
        } else if (!u.moving()) {
            order_occupy(i, bi);   // re-path adjacent
        }
    }
    for (int bi = 0; bi < static_cast<int>(buildings_.size()); ++bi) {
        Building& b = buildings_[bi];
        if (b.type < 0 || b.type >= static_cast<int>(btypes().size()) ||
            !btypes()[b.type].can_be_occupied)
            continue;
        // Dead occupants leave the list (they cannot be hurt inside, but a
        // trigger/ability may still erase them).
        b.occupants.erase(std::remove_if(b.occupants.begin(), b.occupants.end(),
                                         [&](int ui) { return !units_[ui].alive; }),
                          b.occupants.end());
        if (btypes()[b.type].tank_bunker && !b.occupants.empty() &&
            units_[b.occupants.front()].house!=b.house) {
            unload_occupants(bi);continue;
        }
        // The building died or is being sold: its occupants walk out
        // (FUN_004421B0 / FUN_00449B70 both call UnloadOccupants).
        // W8-06-07 [RE: FUN_00449B70 @0x44A4FC passes the place-inside flag;
        // the death path FUN_004421B0 @0x4425BB does not].
        if (!b.occupants.empty() && (!b.alive || b.state == Building::State::Dead || b.selling)) {
            unload_occupants(bi, /*place_inside=*/b.selling && b.alive && b.state != Building::State::Dead);
            continue;
        }
        // W7-08-09 [RE: the damage-state update FUN_0043FAA0 @0x44012F ->
        // FUN_004581F0 -> UnloadOccupants FUN_00457DD0]: at health ratio <=
        // ConditionRed civilian occupants walk out alive. The Soviet Battle
        // Bunker retains its defenders; damage alone is not an unload order.
        if (!b.occupants.empty() && !btypes()[b.type].bio_reactor && btypes()[b.type].id!="NABNKR" && garrison_red(b)) {
            unload_occupants(bi);
            continue;
        }
        if (b.alive && b.state != Building::State::Dead) sync_garrison_owner(bi);
    }
    step_garrison_combat(dt);
}

// ------------------------------------------------------- R-210 terrain
// [RE: FootClass height gate FUN_004D9B70; CellClass::IsClearToMove
// FUN_004833B0 (a bridge cell's deck at Level + 4 skips the land-type test,
// the ground under it keeps it); InfantryClass FUN_0051BD70 / UnitClass
// FUN_0073DFA0 IsCellOccupied: a Gate=yes building (+0x16B7) opens
// (Move::ClosedGate) when its owner IsAlliedWith (FUN_004F9870) the mover].
const std::vector<std::uint8_t>* World::deck_grid(MovementDomain d) const {
    if (hbridge_.empty() || d == MovementDomain::Water) return nullptr;
    const std::size_t n = static_cast<std::size_t>(grid_w_) * grid_h_;
    if (deck_ver_ != bridge_version_ || deck_land_.size() != n || deck_amph_.size() != n) {
        deck_land_.assign(n, 0);
        deck_amph_.assign(n, 0);
        for (std::size_t i = 0; i < n && i < hbridge_.size(); ++i) {
            const std::uint8_t v = hbridge_[i];
            if ((v & 3) == 0 || (v & 4)) continue;   // no deck / broken span
            // Over water / cliff (1) the ground is not a land mover's: deck
            // only; over land (2) both the deck and the ground under it.
            deck_land_[i] = (v & 3) == 1 ? 2 : 1;
            deck_amph_[i] = 1;   // an amphibious mover swims under or walks over
        }
        deck_ver_ = bridge_version_;
    }
    return d == MovementDomain::Amphibious ? &deck_amph_ : &deck_land_;
}

void World::open_gates_for(int h, std::vector<int>& out) const {
    out.clear();
    const std::vector<BuildingTypeDef>& bt = btypes();
    for (const Building& b : buildings_) {
        if (!b.alive || b.type < 0 || b.type >= static_cast<int>(bt.size())) continue;
        if (b.state == Building::State::Dead || !bt[static_cast<std::size_t>(b.type)].w6.gate) continue;
        if (!are_allied(b.house, h)) continue;   // the gate owner's view (FUN_004F9870)
        for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh))
            if (c.x >= 0 && c.y >= 0 && c.x < grid_w_ && c.y < grid_h_)
                out.push_back(c.y * grid_w_ + c.x);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

const PathTerrain* World::terrain_for(const Unit& u, PathTerrain& out, std::vector<int>& gates) const {
    out = PathTerrain{};
    gates.clear();
    if (u.movement_domain != MovementDomain::Water) open_gates_for(u.house, gates);
    append_slave_passage(u,buildings_,btypes(),grid_w_,grid_h_,gates);
    if (!gates.empty()) out.open_cells = &gates;
    if (!terrain_armed()) return gates.empty() ? nullptr : &out;
    out.level = &level_grid_;
    out.slope = ramp_.size() == level_grid_.size() ? &ramp_ : nullptr;
    out.deck = deck_grid(u.movement_domain);
    out.version = bridge_version_;
    out.start_layer = out.deck ? u.on_deck : 0;
    return &out;
}

void World::sync_deck_layer(Unit& u) const {
    if (!terrain_armed()) return;
    const std::vector<std::uint8_t>* dk = deck_grid(u.movement_domain);
    const int x = u.cell_x, y = u.cell_y;
    if (!dk || x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) { u.on_deck = 0; return; }
    const std::uint8_t v = (*dk)[static_cast<std::size_t>(y) * grid_w_ + x];
    if (v == 0) u.on_deck = 0;
    else if (v == 2) u.on_deck = 1;
}

}  // namespace ra2engine

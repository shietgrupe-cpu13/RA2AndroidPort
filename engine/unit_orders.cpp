// Gap fix C -- the World side of the Stop / Guard / Scatter orders (gap owner
// B binds the S / G / X keys and Android buttons to these). [RE: FootClass
// Mission_Stop / Mission_Guard / Scatter are the standard Westwood missions;
// the clone's model:] stop = finish the cell being entered, drop every order;
// guard = stop + guard post at the current cell (idle units auto-acquire and
// are leashed to it -- combat.cpp); scatter = step to a free adjacent cell.
#include <cmath>
#include <algorithm>

#include "world.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

namespace ra2engine {

void World::clear_waypoints(int issuer,const std::vector<int>& ids) {
    for(int i:ids)if(i>=0 && i<static_cast<int>(units_.size()) && can_command(issuer,units_[i].house))
        waypoint_routes_.erase(i);
}
void World::queue_waypoint(int issuer,const std::vector<int>& ids,int x,int y) {
    if(x<0 || y<0 || x>=grid_w_ || y>=grid_h_)return;
    std::vector<int> starters;
    for(int i:ids) {
        if(i<0 || i>=static_cast<int>(units_.size()))continue;
        const Unit& u=units_[i];
        if(!u.alive || u.die_time>=0 || u.cargo_of>=0 || !can_command(issuer,u.house) ||
           (u.is_aircraft && u.an.t.fixed_wing) || (u.deployed && u.deploy_fire))continue;
        auto it=waypoint_routes_.find(i);
        if(it==waypoint_routes_.end())starters.push_back(i);
        else if(it->second.house==issuer && it->second.points.size()<64)it->second.points.push_back({x,y});
    }
    // Preserve normal movement-domain formation assignment for the first leg.
    if(!starters.empty())group_move(issuer,starters,x,y);
    for(int i:starters) {
        const Unit& u=units_[i];
        const Cellxy goal=u.has_flight_goal ? Cellxy{u.flight_goal_x,u.flight_goal_y} :
            u.path.empty() ? Cellxy{u.cell_x,u.cell_y} : u.path.back();
        waypoint_routes_[i]={issuer,{goal}};
    }
}
void World::step_waypoints() {
    for(auto it=waypoint_routes_.begin();it!=waypoint_routes_.end();) {
        const int i=it->first;
        auto& r=it->second;
        if(i<0 || i>=static_cast<int>(units_.size()) || !units_[i].alive || units_[i].die_time>=0 ||
           units_[i].cargo_of>=0 || !can_command(r.house,units_[i].house) || r.points.empty()) {
            it=waypoint_routes_.erase(it);continue;
        }
        const Unit& u=units_[i];
        if(u.moving() || u.has_flight_goal || u.frozen || u.cell_x!=r.points.front().x || u.cell_y!=r.points.front().y) {
            ++it;continue;
        }
        r.points.erase(r.points.begin());
        if(r.points.empty()) {it=waypoint_routes_.erase(it);continue;}
        const auto next=r.points.front();
        group_move(r.house,{i},next.x,next.y);
        const Unit& moved=units_[i];
        r.points.front()=moved.has_flight_goal ? Cellxy{moved.flight_goal_x,moved.flight_goal_y} :
            moved.path.empty()?Cellxy{moved.cell_x,moved.cell_y}:moved.path.back();
        ++it;
    }
}

namespace {
bool orderable(const Unit& u) { return u.alive && u.die_time < 0.0 && u.cargo_of < 0; }
}  // namespace

void World::order_stop(int idx) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& u = units_[idx];
    if (!orderable(u)) return;
    waypoint_routes_.erase(idx);
    if (u.deploys_into>=0 && u.deploys_into<static_cast<int>(btypes().size()) && btypes()[u.deploys_into].slave_count>0) {
        u.manual_hold=true;
        u.deploy_mission=0;
    }
    u.attack_cell_x = u.attack_cell_y = -1;   // G16: Stop ends a ground fire order
    u.occupy_target = -1;
    if (u.is_aircraft) {
        // W2-AN-07: Stop halts a hovering jumpjet where it is (the cruise goal
        // is dropped; a landable Night Hawk then descends); a fixed-wing
        // aircraft abandons its sortie and returns to its airfield.
        u.target = -1; u.target_building = -1; u.attack_order = false;
        u.has_flight_goal = false;
        u.an.attack_unit = u.an.attack_building = -1;
        u.an.attack_x = u.an.attack_y = -1;
        if (u.an.t.fixed_wing && u.an.sortie != 0) u.an.sortie = 3;   // RTB
        return;
    }
    // A unit standing on its cell stops right there; one between cells finishes
    // entering the cell already reserved, then stands (no backward snap).
    if (u.moving()) {
        const bool on_cell = std::abs(u.fx - u.cell_x) < 1e-6 && std::abs(u.fy - u.cell_y) < 1e-6;
        if (on_cell) {
            occ_.release_all_of(idx, u.cell_x, u.cell_y);
            occ_.set(u.cell_x, u.cell_y, idx);
            u.path.clear();
            u.path_idx = 0;
        } else {
            u.path.resize(u.path_idx + 1);
        }
    }
    u.target = -1;
    u.target_building = -1;
    u.attack_order = false;
    u.enter_target = -1;
    u.capture_target = -1;
    u.infiltrate_target = -1;
    u.unload_pending = false;
    u.has_flight_goal = false;
    const Cellxy post = u.moving() ? u.path.back() : Cellxy{u.cell_x, u.cell_y};
    u.guard_x = post.x;
    u.guard_y = post.y;
    u.tg.area_guard = false;   // W6-02-11: Stop leaves Area_Guard
    u.tg.mission = -1;         // w7fix-combat: Stop replaces the mission
    u.tg.overridden = false;
    u.tg.opportunity = false;
    u.tg.amove_x = u.tg.amove_y = -1;
    u.tg.home_x = u.tg.home_y = -1;
    // W7-01-32 / W7-02-08 / W7-04-17 [RE: Enter_Idle_Mode 0x51CB2A / 0x737BB6]:
    // a DefaultToGuardArea type (dogs, Terror Drone) idles in Area_Guard.
    if (u.w7.t.default_to_guard_area && !u.is_aircraft) {
        u.tg.area_guard = true;
        u.tg.home_x = post.x;   // W7-07-06: the post it guards
        u.tg.home_y = post.y;
    }
    enter_idle_mode(idx);      // W7-07-05: then Enter_Idle_Mode (computer IQ, GUARD_AREA)
}

// [RE: TechnoClass::SetOwningHouse FUN_00700940 (FootClass FUN_004DBDE0 /
// UnitClass FUN_007451D0 wrap it): on a real owner change it calls
// SetTarget(0) (vtbl+0x3C8), SetDestination(0) (+0x480), QueueMission(Guard)
// (+0x1E8, 5) and Enter_Idle_Mode (+0x484) -- the unit drops its target and
// its move and guards the spot it is on. Nothing of the old owner's orders
// survives.] The clone's form: the new house, then Stop (which also re-plants
// the guard anchor here, so the leash never pulls the unit back to a post or
// an approach the previous owner set).
void World::set_unit_owner(int idx, int house) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return;
    if (units_[idx].house == house) return;   // FUN_00700940: same owner -> no-op
    release_unit_mind_control(idx);
    const int old_house=units_[idx].house;
    if (old_house>=0 && old_house<static_cast<int>(ai_houses_.size())) {
        for (auto& team:ai_houses_[old_house].team_state.teams)
            for (int k=static_cast<int>(team.members.size())-1;k>=0;--k)
                if (team.members[k]==idx) {
                    team.members.erase(team.members.begin()+k);
                    if (k<static_cast<int>(team.member_types.size()))
                        team.member_types.erase(team.member_types.begin()+k);
                }
    }
    units_[idx].tg.team_aggressive=false;
    units_[idx].tg.team_suicide=false;
    units_[idx].house = house;
    // W7-09-09 [RE: SetOwningHouse 0x700940 -> Assign_Mission(Guard)]: the
    // new owner's unit leaves Sleep / Harmless (order_stop clears the mission).
    order_stop(idx);
}

void World::order_guard(int idx) {
    // Guard = stop and hold this post (the guard anchor step_combat leashes
    // auto-acquired engagements to).
    order_stop(idx);
    // W6-02-11 [RE: GuardObject FUN_0072FE30 = MegaMission(Area_Guard)]: the
    // doubled area-guard threat radius.
    if (idx >= 0 && idx < static_cast<int>(units_.size()) && orderable(units_[idx]) && !units_[idx].is_aircraft) {
        units_[idx].tg.area_guard = true;
        units_[idx].tg.home_x = units_[idx].cell_x;   // W7-07-06: guard this spot
        units_[idx].tg.home_y = units_[idx].cell_y;
    }
}

// fv7c (13a #3) [RE: UnitClass::Scatter FUN_00742880 -- the directional
// branch accepts a neighbour only when the unit's own vtbl+0x1AC
// (IsCellOccupied, with the height gate FUN_004D9B70) passes; NearbyLocation
// takes the unit's own SpeedType / MovementZone]: the unit's movement domain
// (a ship scatters to water -- it used to test the LAND grid, so a ship never
// scattered) and the one-step height gate.
bool World::scatter_cell_ok(const Unit& u, int x, int y) const { return scatter_cell_layer(u, x, y) >= 0; }

// w8fix-layer: the layer (0 ground, 1 deck) a scatter step into (x,y) lands
// on, -1 when the cell is refused.
int World::scatter_cell_layer(const Unit& u, int x, int y) const {
    if (!unit_can_enter_cell(u, x, y)) return -1;
    PathTerrain t;
    std::vector<int> gates;
    const PathTerrain* tp = terrain_for(u, t, gates);
    if (tp && tp->level) return path_step_layer(*tp, grid_w_, u.cell_x, u.cell_y, u.on_deck, x, y);
    return u.on_deck ? 1 : 0;
}

void World::order_scatter(int idx, bool cell_push) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& u = units_[idx];
    if (!orderable(u) || u.frozen || u.is_aircraft) return;
    // W7-01-19 [RE: ScatterObject 0x4C6BD0 -> TechnoClass::Scatter 0x51CEA0]:
    // X is ignored for a human's deployed unit and for a unit on the move.
    if (u.deployed && !house_is_computer(u.house)) return;
    // w7r2 [RE: UnitClass::Scatter FUN_00742880 @0x74294D..0x742978]: a unit
    // with a Destination returns only when nokidding is 0. The blocked mover's
    // CellClass::ScatterContent (Drive Move 6, 0x4B2CD6..0x4B2CE0) passes the
    // empty coord with force = nokidding = 1, so a vehicle HELD UP on its own
    // route is moved aside too (its destination replaced by a nearby cell,
    // 0x742A9B NearbyLocation -> 0x742EA0 SetDestination). Without it two
    // vehicles meeting head-on in a one-cell lane waited on each other forever
    // (SOV07's harvesters at (74,61)/(75,61)).
    if (u.moving() && !(cell_push && u.wait_time > 0.0)) return;
    static const int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    const int start = combat_rng_.random(0, 7);   // deterministic sync-RNG pick
    for (int k = 0; k < 8; ++k) {
        const int d = (start + k) & 7;
        const int x = u.cell_x + kDx[d], y = u.cell_y + kDy[d];
        // W5-B R-061: with sub-cells an infantry may scatter into a cell that
        // still has a free spot.
        // w8fix-layer [RE: FUN_00742880 -> IsCellOccupied on the mover's own
        // layer]: a free spot on the plane of the layer the step lands on.
        const int lay = scatter_cell_layer(u, x, y);
        const bool taken = occ_.subcells() ? !occ_.can_enter(x, y, idx, lay < 0 ? Occupancy::kAnyLayer : lay)
                                           : unit_at_cell(x, y) >= 0;
        if (lay < 0 || taken || building_at_cell(x, y) >= 0) continue;   // fv7c
        u.target = -1;
        u.target_building = -1;
        u.attack_order = false;
        order_unit_move(idx, x, y);
        return;
    }
}

// R3 [RE: gamemd StopCommandClass::Execute FUN_0072ff70 (event 6),
// GuardCommandClass FUN_0072fe30 (mission 0xB), ScatterCommandClass
// FUN_007300b0 (event 7)]: each walks CurrentObjects (DAT_00a8dc24/30) and
// sends the order for every selected object the player may command
// (vtbl+0xA0); there is no Owner == PlayerPtr test. Selection itself already
// admits the PlayerControl houses (W2-CA-02, house+0x1ED), so a mind-
// controlled or map-PlayerControl unit (SOV09's Africans) obeys S / G / X
// like house 0's. The clone tested house == 0 only.
// NET L1: `ids` are the command's Whom list (the issuer's selection,
// ascending); can_command is the apply-time ownership check.
void World::group_stop(int issuer, const std::vector<int>& ids) {
    for (int i : ids)
        if (i >= 0 && i < static_cast<int>(units_.size()) && can_command(issuer, units_[i].house)) order_stop(i);
}
void World::group_guard(int issuer, const std::vector<int>& ids) {
    for (int i : ids)
        if (i >= 0 && i < static_cast<int>(units_.size()) && can_command(issuer, units_[i].house)) order_guard(i);
}
void World::group_scatter(int issuer, const std::vector<int>& ids) {
    for (int i : ids) {
        if (i < 0 || i >= static_cast<int>(units_.size()) || !can_command(issuer, units_[i].house)) continue;
        // W7-07-25 [RE: EVENT_SCATTER 0x4C77D1, InfantryClass::Scatter
        // 0x51CEB3..0x51CF18]: a human infantry in its Deploy / Deployed
        // sequence ignores it, and a moving unit does not scatter.
        const Unit& u = units_[static_cast<std::size_t>(i)];
        if (w7_rules_.armed && ((u.deployed && !u.is_vehicle && human_commanded(u.house)) || u.moving())) continue;
        order_scatter(i);
    }
}


}  // namespace ra2engine

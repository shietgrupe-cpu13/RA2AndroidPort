// W7 locomotion (docs/gap/wave7/11-locomotion.md): the per-frame ground
// movement pass (step_units) and its helpers, split out of world.cpp.
//
// One pass per logic frame moves every ground unit along its A* path with the
// locomotor its type names [RE: TechnoType Locomotor= CLSID]:
//   * Drive / Ship -- the TRACK model [RE: DriveLocomotionClass Start_Of_Move
//     FUN_004B2550 / While_Moving FUN_004B0E50; ShipLocomotionClass
//     FUN_006A1310 / FUN_0069FC80, same tables]: the unit turns in place only
//     until its facing is exactly the next cell's 8-way direction, then drives a
//     TurnTrack entry chosen by looking one step ahead -- a 45 / 90 degree bend
//     is one curved two-cell track, driven without stopping -- one track point
//     per 7 speed units (GetCurrentSpeed leptons/frame). The points are ~11.1
//     leptons apart (23 per straight cell, 31 per diagonal), which is where the
//     vehicles' ~x1.6 lepton speed comes from: there is no GameSpeedBias.
//   * Walk -- raw leptons per frame along a straight line (no bias), x2/3 when
//     crawling [RE: WalkLocomotionClass FUN_00759CE0, FUN_00521AB0].
//   * Hover -- raw leptons x a 0..1 fraction ramped by HoverAcceleration /
//     HoverBrake, moving along the CURRENT facing (drift) [RE: FUN_00514130,
//     FUN_00515CF0].
// Blocked next cells run the original's Can_Enter-result state machine
// (re-path every PathDelay, path around moving traffic after
// BlockagePathDelay, stop within CloseEnough, scatter parked friendlies) --
// units never make a unit abandon its order [RE: FUN_004B2550 Move 2 / 6,
// Basic_Path FUN_004D3840].

#include "world.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "bridges.hpp"     // W8-03-03: low_bridge_destroyed
#include "combat.hpp"      // can_crush
#include "locomotion.hpp"
#include "pathfind.hpp"
#include "diagnostics.hpp"
#include "slave_passage.hpp"

namespace ra2engine {

namespace {

constexpr double kWalkFps = 9.0;      // == world.cpp's walk-cycle frame rate
constexpr int kRepathBudget = 4000;   // == world.cpp (W2-SK-16)
constexpr int kTerrainCheckBudget = 20000;   // == world.cpp kNearestPathBudget
constexpr double kTrackUnit = 7.0;    // [RE: FUN_004B0E50] speed units per track point
constexpr double kStraightUnits = 161.0;   // 23 points x 7 per straight cell

// Logic frames covered by a step of dt seconds (15 per second; an exact whole
// number for the game's 1/15 s tick).
double frames_of(double dt) {
    const double f = dt * 15.0;
    const double r = std::floor(f + 0.5);
    return std::fabs(f - r) < 1e-6 ? r : f;
}

// A unit under way -- it has a path and is not held up waiting for a cell
// (the original's Can_Enter returns MovingBlock only for an occupant whose
// locomotor is moving; a held-up unit reads as a parked one, Temp).
bool under_way(const Unit& v) { return v.moving() && v.wait_time <= 0.0; }

bool live_on_map(const Unit& v) {
    return v.alive && v.die_time < 0.0 && v.cargo_of < 0 &&
           !(v.is_aircraft && v.flight_state != FlightState::Grounded);
}

// The next path index whose cell differs from the unit's own.
std::size_t next_path_index(const Unit& u) {
    std::size_t pi = u.path_idx;
    while (pi < u.path.size() && u.path[pi].x == u.cell_x && u.path[pi].y == u.cell_y) ++pi;
    return pi;
}

// [RE: FUN_004297B0 MovingBlock] 1 when the chain of moving occupants (up to
// 10, each followed to its next path cell) ends in a free cell, else 4.
int moving_block_cost(const std::vector<Unit>& units, const Occupancy& occ, int o) {
    int cur = o;
    for (int k = 0; k < 10; ++k) {
        if (cur < 0 || cur >= static_cast<int>(units.size())) return 4;
        const Unit& v = units[static_cast<std::size_t>(cur)];
        if (!under_way(v)) return 4;
        const std::size_t pi = next_path_index(v);
        if (pi >= v.path.size()) return 4;
        const Cellxy n = v.path[pi];
        const int nxt = occ.at(n.x, n.y);
        if (nxt == Occupancy::kEmpty || nxt == cur) return 1;
        cur = nxt;
    }
    return 4;
}

}  // namespace

LocoKind unit_loco_kind(const Unit& u) {
    if (u.loco.t.kind >= 0 && u.loco.t.kind <= static_cast<int>(LocoKind::Other)) {
        const LocoKind k = static_cast<LocoKind>(u.loco.t.kind);
        // A Teleporter that walks a path drives a piggybacked Drive locomotor
        // [RE: UnitClass::SetDestination 0x740780]; any other non-ground
        // locomotor reaching step_units moves as its body class does.
        if (k == LocoKind::Drive || k == LocoKind::Walk || k == LocoKind::Hover || k == LocoKind::Ship) return k;
    }
    if (u.is_naval) return LocoKind::Ship;
    if (u.is_vehicle) return LocoKind::Drive;
    return LocoKind::Walk;
}

bool loco_is_track(const Unit& u) {
    const LocoKind k = unit_loco_kind(u);
    return k == LocoKind::Drive || k == LocoKind::Ship;
}

// W9-02-06 / W9-02-10: the grid is a long-lived scratch (occupancy_cost_
// scratch below). A call resets only the cells the previous call raised --
// instead of allocating and filling w*h cells per blocked path -- when `out`
// is the same buffer of the same size; any other vector is assigned in full.
namespace {
struct OccCostStamp {
    const std::uint16_t* buf = nullptr;
    std::size_t n = 0;
    std::vector<std::uint32_t> raised;
};
OccCostStamp& occ_cost_stamp() {
    static thread_local OccCostStamp s;
    return s;
}
}  // namespace

std::vector<std::uint16_t>& occupancy_cost_scratch() {
    static thread_local std::vector<std::uint16_t> v;
    return v;
}

void build_occupancy_cost(const std::vector<Unit>& units, const Occupancy& occ, int w, int h, int self,
                          bool blockage_mode, const World* world, std::vector<std::uint16_t>& out) {
    const std::size_t n = static_cast<std::size_t>(w > 0 ? w : 0) * static_cast<std::size_t>(h > 0 ? h : 0);
    OccCostStamp& st = occ_cost_stamp();
    if (st.buf == out.data() && st.n == n && out.size() == n) {
        for (const std::uint32_t k : st.raised) out[k] = 1;   // undo the last call's stamps
    } else {
        out.assign(n, 1);
    }
    st.raised.clear();
    st.buf = out.data();
    st.n = n;
    if (self < 0 || self >= static_cast<int>(units.size())) return;
    const Unit& me = units[static_cast<std::size_t>(self)];
    const bool me_inf = occ.is_infantry(self) && occ.subcells();
    // w8fix-layer [RE: the deck and ground content chains +0xE8 / +0xE4]:
    // the layer the mover would stand on in a deck cell -- deck-only cells
    // (over water / a cliff) the deck, a deck over land or an amphibious
    // mover its current layer; a ship always the water.
    const std::vector<std::uint8_t>* dk =
        (world && !me.is_naval && me.movement_domain != MovementDomain::Water) ? world->deck_grid(me.movement_domain)
                                                                                : nullptr;
    for (std::size_t o = 0; o < units.size(); ++o) {
        if (static_cast<int>(o) == self) continue;
        const Unit& v = units[o];
        if (!live_on_map(v)) continue;
        if(slave_mobile_pair(me,v,int(o)) || slave_mobile_pair(v,me,self))continue;
        const int x = v.cell_x, y = v.cell_y;
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        int my_layer = 0;
        if (dk && static_cast<std::size_t>(y) * w + x < dk->size()) {
            const std::uint8_t d = (*dk)[static_cast<std::size_t>(y) * w + x];
            my_layer = d == 2 ? 1 : d == 1 ? (me.on_deck ? 1 : 0) : 0;
        }
        if ((v.on_deck ? 1 : 0) != my_layer) continue;   // the other layer never blocks
        // An infantry mover shares a cell with a free sub-cell spot.
        if (me_inf && occ.can_enter(x, y, self, my_layer)) continue;
        int c;
        if (me.enter_target == static_cast<int>(o)) c = 1;   // W7-11-12: the transport's own cell is OK
        // Crushers commit through enemy crushable infantry, including units
        // already moving; do not make the ore miners route around them.
        else if (me.crusher && me.is_vehicle && can_crush(me, v) &&
                 !(world ? world->are_allied(me.house, v.house) : me.house == v.house)) c = 1;
        else if (under_way(v)) c = blockage_mode ? 1000 : moving_block_cost(units, occ, static_cast<int>(o));
        else if (world ? world->are_allied(me.house, v.house) : me.house == v.house) c = 8;   // Temp
        else c = 20;   // Destroyable
        std::uint16_t& cell = out[static_cast<std::size_t>(y) * w + x];
        if (c > cell) {
            if (cell == 1) st.raised.push_back(static_cast<std::uint32_t>(static_cast<std::size_t>(y) * w + x));
            cell = static_cast<std::uint16_t>(c);
        }
    }
}

bool path_meets_units(const std::vector<Cellxy>& path, const Occupancy& occ, int self) {
    for (std::size_t i = 1; i < path.size(); ++i) {
        const int o = occ.at(path[i].x, path[i].y);
        if (o != Occupancy::kEmpty && o != self) return true;
    }
    return false;
}

double land_speed_mult(const MoveSpeedCtx& c, const Unit& u, int w, int h, int nx, int ny) {
    double m = 1.0;
    // Only the Drive/Ship locomotors read the GroundType table for speed
    // (FUN_004B2550 / FUN_006A1310); Walk and Hover do not.
    if (c.armed && c.land && (u.is_vehicle || u.is_naval) && !u.is_aircraft && u.land_speed_loco && nx >= 0 &&
        ny >= 0 && nx < w && ny < h) {
        const std::size_t i = static_cast<std::size_t>(ny) * w + nx;
        if (i < c.land->size()) {
            int land = (*c.land)[i];
            if (c.ore && c.ramp && i < c.ramp->size() && c.ore->has_ore(nx, ny) && (*c.ramp)[i] < 5) land = 5;
            // W8-03-03 [RE: FUN_0047D1C0]: an intact low-bridge cell is Road
            // (World::speed_land_at); vehicles cross at the Road row's speed.
            if (c.bridge_ovl && i < c.bridge_ovl->size() && (*c.bridge_ovl)[i] >= 0 &&
                !low_bridge_destroyed((*c.bridge_ovl)[i]))
                land = 1;
            // A high bridge has a Road deck and an independent water/ground
            // plane below it. Read the entered plane, not the river's speed.
            bool next_deck=false;
            if(c.world && c.world->deck_intact(nx,ny) && c.level && i<c.level->size()) {
                const int cx=u.cell_x,cy=u.cell_y;
                if(cx>=0 && cy>=0 && cx<w && cy<h) {
                    const std::size_t ci=static_cast<std::size_t>(cy)*w+cx;
                    if(ci<c.level->size())next_deck=((*c.level)[ci]+(u.on_deck?4:0)==(*c.level)[i]+4);
                }
            }
            if(next_deck)land=1;
            if (land < 0 || land > 11) land = 0;
            int st = u.speed_type;
            if (st < 0 || st > 7) st = u.is_naval ? 5 : 1;   // default: Track (Drive), Float (ship)
            m = c.cost[land][st];
            if (m > 1.0) m = 1.0;
            // Slope (UnitClass only): the next cell higher -> *Uphill, lower ->
            // *Downhill; SpeedType Track uses the Tracked pair, every other
            // type the Wheeled pair.
            if (c.level && !u.is_naval) {
                const std::size_t ci = static_cast<std::size_t>(u.cell_y) * w + u.cell_x;
                if (u.cell_x >= 0 && u.cell_y >= 0 && u.cell_x < w && u.cell_y < h && ci < c.level->size()) {
                    const int cur = (*c.level)[ci]+(u.on_deck?4:0), nxt = (*c.level)[i]+(next_deck?4:0);
                    const int k = st == 1 ? 0 : 1;
                    if (cur < nxt) m *= c.up[k];
                    else if (nxt < cur) m *= c.down[k];
                }
            }
            if (m == 0.0) m = 0.5;
        }
    }
    // W7-11-06 [RE: FUN_004B2550 / FUN_006A1310: Health ratio <= Rules+0x1700
    // ConditionYellow -> x0.75 (DAT_007E6FC0)]: Drive and Ship only.
    if (loco_is_track(u) && u.max_hp > 0 && static_cast<double>(u.hp) / u.max_hp <= c.loco.condition_yellow)
        m *= 0.75;
    return m;
}

namespace {

enum class Reserve { Ok, Blocked, Cliff, Stop };

struct StepEnv {
    std::vector<Unit>& units;
    const std::vector<std::uint8_t>& passable;
    const std::vector<std::uint8_t>& water_passable;
    int w, h;
    Occupancy& occ;
    double dt, fr, move_cps;
    const AnimTable& anims;
    const std::vector<std::uint8_t>* amphib_passable;
    const MoveSpeedCtx& sc;
    bool have_speed;
    const std::vector<std::uint8_t>* wall_leak;
    const std::vector<std::uint8_t>* crush_passable;
    const std::vector<std::uint8_t>* crush_leak;
    const StepTerrain* terrain;
    mutable std::vector<int> passage_cells;

    const PathTerrain* terrain_of_unit(const Unit& u, PathTerrain& t) const {
        t = !terrain ? PathTerrain{} : u.movement_domain == MovementDomain::Water ? terrain->water
            : u.movement_domain == MovementDomain::Amphibious ? terrain->amph : terrain->land;
        t.start_layer = t.deck ? u.on_deck : 0;
        t.open_cells = nullptr;
        if (terrain && terrain->gates && u.movement_domain != MovementDomain::Water && u.house >= 0 &&
            u.house < static_cast<int>(terrain->gates->size()) && !(*terrain->gates)[(std::size_t)u.house].empty())
            t.open_cells = &(*terrain->gates)[(std::size_t)u.house];
        if(sc.world && (u.occupy_target>=0 || u.bunker_exit_host>=0 || slave_passage_building(u,sc.world->buildings(),sc.world->btypes()))) {
            passage_cells=t.open_cells?*t.open_cells:std::vector<int>{};
            append_slave_passage(u,sc.world->buildings(),sc.world->btypes(),w,h,passage_cells);
            t.open_cells=&passage_cells;
        }
        return (t.level || t.open_cells) ? &t : nullptr;
    }
    // w7fix-combat: the alliance view IsAlliedWith (null World: same house).
    bool allied(int a, int b) const { return sc.world ? sc.world->allied_with(a, b) : a == b; }
    bool w7crush() const { return sc.world && sc.world->combat_w7().armed && occ.subcells(); }
    int base_speed(const Unit& u) const { return leptons_per_frame(u.speed_cps > 0.0 ? u.speed_cps : move_cps); }
};

// The unit stands: path done (or stopped short) at its cell.
void arrive(StepEnv& e, Unit& u, int self) {
    const UnitAnimSeq& seq = anim_for_image(e.anims, u.image);
    u.fx = u.cell_x;
    u.fy = u.cell_y;
    u.path.clear();
    u.path_idx = 0;
    u.anim_time = 0.0;
    u.frame = seq_frame(seq.stand, u.facing, 0);
    u.wait_time = 0.0;
    u.loco.trk = -1;
    u.loco.accum = 0.0;
    u.loco.path_delay = 0.0;
    u.loco.blockage = -1.0;
    u.loco.temp_tries = 0;
    e.occ.mark_layer(self, u.on_deck);
    e.occ.release_all_of(self,u.cell_x,u.cell_y);
    e.occ.set(u.cell_x, u.cell_y, self, u.on_deck);   // w8fix-layer: its own layer's plane
}

// Mark `tgt` (a neighbour of `from`) for unit u before entering it: the
// crusher squash, the R-210 height gate and the occupancy reservation (a
// sub-cell spot for infantry). `layer` gets the deck layer the unit lands on.
Reserve reserve_cell(StepEnv& e, Unit& u, int self, Cellxy from, int from_layer, Cellxy tgt, int& layer) {
    // W7-11-12 [RE: InfantryClass 0x51BD70 / UnitClass 0x73DFA0 Can_Enter:
    // the NavCom transport's own cell is OK under Mission Enter]: a passenger
    // walks INTO the transport's cell -- it stops there and step_cargo
    // boards it.
    if (u.enter_target >= 0 && u.enter_target < static_cast<int>(e.units.size())) {
        const Unit& t = e.units[static_cast<std::size_t>(u.enter_target)];
        if (t.cell_x == tgt.x && t.cell_y == tgt.y) return Reserve::Stop;
    }
    // Gap fix C (G-47): a Crusher vehicle driving into a cell held by enemy
    // Crushable infantry squashes it. w7fix-combat W7-03-01 / W7-09-04 [RE:
    // ObjectClass::IsCrushable 0x5F64C0 -> IsAlliedWith 0x4F98B0]: never an
    // ally of the crusher's owner. W7-07-21 [RE: Drive 0x4B3D85 ->
    // UnitClass::Crush(cell, 1) 0x7404B0 -> CellClass::Incoming FUN_00481580;
    // the squash Crush(cell, 0) runs on ARRIVAL, FUN_00739AC0]: with the W7
    // rules the infantry is warned when the crusher commits to the cell and
    // squashed only if still there when it arrives (squash_on_arrival).
    const bool w7crush = e.w7crush();
    // R-210 [RE: FUN_004D9B70]: the height gate on every step -- the layer
    // the unit lands on (-1: a cliff, refused after the crush below).
    layer = from_layer;
    if (e.terrain) {
        PathTerrain t;
        if (const PathTerrain* tp = e.terrain_of_unit(u, t); tp && tp->level) {
            const int dx = tgt.x - from.x, dy = tgt.y - from.y;
            layer = (dx < -1 || dx > 1 || dy < -1 || dy > 1)
                        ? -1
                        : path_step_layer(*tp, e.w, from.x, from.y, from_layer, tgt.x, tgt.y);
        }
    }
    // w8fix-layer W8-01-05 / W8-02-02 / W8-03-04 [RE: IsClearToMove
    // FUN_004833B0 reads the occupation word of the mover's layer (+0x128 on
    // the deck, +0x124 below); UnitClass::Crush 0x7404B0 walks the AltObject
    // chain +0xE8 for a crusher on the bridge, else FirstObject +0xE4]: every
    // occupancy test below sees only the plane of the layer entered.
    const int L = layer < 0 ? (from_layer > 0 ? 1 : 0) : layer;
    if (u.crusher && u.is_vehicle) {
        int victims[4] = {e.occ.at(tgt.x, tgt.y, L), -1, -1, -1};
        if (e.occ.subcells())
            for (int s = Occupancy::kFirstSpot; s < Occupancy::kFirstSpot + Occupancy::kSpots; ++s)
                victims[s - 1] = e.occ.spot_unit(tgt.x, tgt.y, s, L);
        const int cell_ix = tgt.y * e.w + tgt.x;
        bool warn = false, elite = false;
        for (int o : victims) {
            if (o < 0 || o == self || o >= static_cast<int>(e.units.size())) continue;
            Unit& v = e.units[static_cast<std::size_t>(o)];
            elite = elite || is_elite(v.vet.veterancy);
            if (w7crush && e.occ.is_infantry(o) && can_crush(u, v) && !e.allied(u.house, v.house) && v.alive &&
                v.die_time < 0.0 && v.cargo_of < 0) {
                warn = true;   // squashed on arrival
                continue;
            }
            if (can_crush(u, v) && !e.allied(u.house, v.house) && v.alive && v.die_time < 0.0 &&
                v.cargo_of < 0) {   // W6-04-14/29
                v.hp = 0;
                v.die_time = 0.0;
                v.vet.killed_by = self;
                v.target = -1;
                v.target_building = -1;
                v.path.clear();
                v.path_idx = 0;
                e.occ.release_all_of(o, -1, -1);
            }
        }
        if (warn && u.tg.crush_warn != cell_ix) {
            u.tg.crush_warn = cell_ix;
            for (int o : victims) {
                if (o < 0 || o == self || o >= static_cast<int>(e.units.size())) continue;
                const Unit& v = e.units[static_cast<std::size_t>(o)];
                if (!v.alive || v.die_time >= 0.0 || v.moving() || v.is_vehicle) continue;
                if (e.sc.scatter_out && e.sc.world->incoming_scatters(v, elite, /*nokidding=*/false))
                    e.sc.scatter_out->push_back(o);
            }
        }
    }
    if (layer < 0) return Reserve::Cliff;
    if (e.occ.is_infantry(self)) {
        // W5-B R-061 [RE: FindInfantrySubposition FUN_00481090]: a spot in the
        // next cell -- the quadrant of the current in-cell position, else the
        // spot preference row; no free spot (or a vehicle there) blocks.
        if (e.occ.spot_of(tgt.x, tgt.y, self, L) < 0) {
            int cur = e.occ.spot_of(from.x, from.y, self);
            if (cur < 0) cur = 0;
            const int owner=e.occ.at(tgt.x,tgt.y,L);
            const int allowed=owner>=0 && owner<int(e.units.size()) && slave_mobile_pair(u,e.units[owner],owner)?owner:-1;
            const int sp = e.occ.free_spot(tgt.x, tgt.y, cur, self & 3, L,allowed);
            if (sp < 0) return Reserve::Blocked;
            e.occ.set_spot(tgt.x, tgt.y, self, sp, L);
        }
    } else if (e.occ.at(tgt.x, tgt.y, L) != self || !e.occ.can_enter(tgt.x, tgt.y, self, L)) {
        // W7-07-21 (w7fix-combat): a cell held only by infantry this crusher
        // may squash is entered (the squash happens on arrival).
        bool crush_only = false;
        // The manager may move across its own workers too. Keep their spots
        // intact: other vehicles still see those infantry as blockers.
        if(e.occ.subcells()) {
            const int owner=e.occ.at(tgt.x,tgt.y,L);
            bool only_workers=owner==self || owner<0 ||
                (owner<int(e.units.size()) && slave_mobile_pair(e.units[owner],u,self));
            for(int s=Occupancy::kFirstSpot;s<Occupancy::kFirstSpot+Occupancy::kSpots;++s) {
                const int worker=e.occ.spot_unit(tgt.x,tgt.y,s,L);
                if(worker>=0 && worker!=self && (worker>=int(e.units.size()) || !slave_mobile_pair(e.units[worker],u,self)))only_workers=false;
            }
            crush_only=only_workers;
        }
        if (!crush_only && w7crush && u.crusher && u.is_vehicle && !e.occ.can_enter(tgt.x, tgt.y, self, L)) {
            crush_only = true;
            for (int s = Occupancy::kFirstSpot; s < Occupancy::kFirstSpot + Occupancy::kSpots; ++s) {
                const int o = e.occ.spot_unit(tgt.x, tgt.y, s, L);
                if (o < 0 || o == self) continue;
                if (o >= static_cast<int>(e.units.size())) { crush_only = false; break; }
                const Unit& v = e.units[static_cast<std::size_t>(o)];
                if (!(can_crush(u, v) && !e.allied(u.house, v.house) && v.alive && v.die_time < 0.0)) crush_only = false;
            }
            const int own = e.occ.at(tgt.x, tgt.y, L);   // a vehicle owner (not us) still blocks
            if (own >= 0 && own != self && (own >= static_cast<int>(e.units.size()) || !e.occ.is_infantry(own)))
                crush_only = false;
        }
        if (!crush_only && !e.occ.can_enter(tgt.x, tgt.y, self, L)) return Reserve::Blocked;
        e.occ.set(tgt.x, tgt.y, self, L);   // reserve (now owns current + next)
        if (e.have_speed) u.speed_mult = land_speed_mult(e.sc, u, e.w, e.h, tgt.x, tgt.y);   // W5-B R-062
    }
    return Reserve::Ok;
}

// W7-07-21 (w7fix-combat) [RE: Per_Cell_Process FUN_00739AC0 -> Crush(cell, 0)
// 0x7404B0]: the squash on arriving in a cell -- every crushable non-allied
// infantry still there dies (kill credited to the crusher).
void squash_on_arrival(StepEnv& e, Unit& u, int self) {
    if (!e.w7crush() || !u.crusher || !u.is_vehicle) return;
    for (int s = Occupancy::kFirstSpot; s < Occupancy::kFirstSpot + Occupancy::kSpots; ++s) {
        const int o = e.occ.spot_unit(u.cell_x, u.cell_y, s, u.on_deck);   // w8fix-layer: its own layer
        if (o < 0 || o == self || o >= static_cast<int>(e.units.size())) continue;
        Unit& v = e.units[static_cast<std::size_t>(o)];
        if (!(can_crush(u, v) && !e.allied(u.house, v.house) && v.alive && v.die_time < 0.0 && v.cargo_of < 0))
            continue;
        v.hp = 0;
        v.die_time = 0.0;
        v.vet.killed_by = self;
        v.target = -1;
        v.target_building = -1;
        v.path.clear();
        v.path_idx = 0;
        e.occ.release_all_of(o, -1, -1);
    }
    u.tg.crush_warn = -1;
}

// A re-path from the unit's cell to its goal. mode 0: terrain only (the
// no-route test), 1: unit costs (Temp 8, moving 1/4, enemy 20), 2: the
// blockage mode -- moving units cost 1000 [RE: Basic_Path FUN_004D3840 mode
// 1/2, cost table DAT_0081770C]. Adopts the new path when one is found.
bool repath(StepEnv& e, Unit& u, int self, int mode) {
    if (u.path.empty()) return false;
    const Cellxy goal = u.path.back();
    const bool crush = e.crush_passable && u.crusher && u.is_vehicle && u.movement_domain == MovementDomain::Land;
    const std::vector<std::uint8_t>& base =
        u.movement_domain == MovementDomain::Water ? e.water_passable
        : (u.movement_domain == MovementDomain::Amphibious && e.amphib_passable) ? *e.amphib_passable
        : crush ? *e.crush_passable : e.passable;
    std::vector<std::uint8_t> strict;
    if (mode==3) {
        strict=base;
        // A cost of 8/20 can keep selecting an immovable blocker forever
        // when the real detour costs more. After a sustained stall, test an
        // actual route around stationary units on the mover's own layer.
        for (int i=0;i<static_cast<int>(e.units.size());++i) {
            const auto& v=e.units[i];
            if (i==self || !live_on_map(v) || slave_mobile_pair(u,v,i) || slave_mobile_pair(v,u,self) ||
                (under_way(v) && v.wait_time<3.0) || v.on_deck!=u.on_deck ||
                (can_crush(u,v) && !e.allied(u.house,v.house))) continue;
            if (v.cell_x>=0 && v.cell_y>=0 && v.cell_x<e.w && v.cell_y<e.h)
                strict[static_cast<std::size_t>(v.cell_y)*e.w+v.cell_x]=0;
        }
    }
    const auto& g=mode==3 ? strict : base;
    std::vector<std::uint16_t>& cost = occupancy_cost_scratch();   // W9-02-06: reused, reset incrementally
    if (mode > 0) build_occupancy_cost(e.units, e.occ, e.w, e.h, self, mode == 2, e.sc.world, cost);
    PathTerrain rt;
    std::vector<Cellxy> np = find_path_bounded_t(
        g, e.w, e.h, {u.cell_x, u.cell_y}, goal, mode == 0 ? kTerrainCheckBudget : kRepathBudget,
        u.movement_domain == MovementDomain::Water ? nullptr : crush ? e.crush_leak : e.wall_leak,
        e.terrain_of_unit(u, rt), mode > 0 ? &cost : nullptr);
    if (np.size() > 1) {
        u.path = std::move(np);
        u.path_idx = 1;
        if (mode==3) diag::note("TRAFFIC_RECOVERY unit=%d wait_s=%.2f goal=%d,%d path=%zu image=%s house=%d mission=%d cell=%d,%d deck=%d manager=%d next=%d,%d blocker=%d",
            self,u.wait_time,goal.x,goal.y,u.path.size(),u.image.c_str(),u.house,u.tg.mission,u.cell_x,u.cell_y,u.on_deck,u.w7.slave_manager,
            u.path[1].x,u.path[1].y,e.occ.at(u.path[1].x,u.path[1].y,u.on_deck));
        return true;
    }
    return mode == 0 && np.size() == 1;   // already at the goal: a route trivially exists
}

// The next cell `bc` cannot be entered this frame: the original's
// Can_Enter-result state machine [RE: Drive FUN_004B2550, Walk FUN_00759CE0,
// Hover FUN_00514D90, Ship FUN_006A1310, Basic_Path FUN_004D3840].
void on_blocked(StepEnv& e, Unit& u, int self, Cellxy bc, bool cliff, int layer) {
    // w8fix-layer: the blocker is looked up on the plane of the layer the
    // step would land on (a ship under a deck never blocks deck traffic).
    const int LB = layer < 0 ? (u.on_deck ? 1 : 0) : layer;
    const UnitAnimSeq& seq = anim_for_image(e.anims, u.image);
    const LocoRules& lr = e.sc.loco;
    UnitLoco& L = u.loco;
    u.wait_time += e.dt;
    u.frame = seq_frame(seq.stand, u.facing, 0);   // stand while waiting
    L.accum = 0.0;   // [RE: FUN_004B0E50] not driving -> SpeedAccum 0
    L.hover = 0.0;   // [RE: FUN_00514130] SetSpeedPercentage(0)
    L.path_delay -= e.fr;
    static const bool trace = std::getenv("RA2_LOCO_TRACE") != nullptr;   // diagnostics
    if (trace && L.path_delay <= 0.0)
        std::fprintf(stderr, "[loco] blocked u=%d %s at (%d,%d) next (%d,%d) occ=%d cliff=%d goal=(%d,%d) wait=%.1f\n", self,
                     u.image.c_str(), u.cell_x, u.cell_y, bc.x, bc.y, e.occ.at(bc.x, bc.y, LB), cliff ? 1 : 0,
                     u.path.empty() ? -1 : u.path.back().x, u.path.empty() ? -1 : u.path.back().y, u.wait_time);
    if (trace && L.path_delay <= 0.0) {
        const int oo = e.occ.at(bc.x, bc.y, LB);
        if (oo >= 0 && oo < static_cast<int>(e.units.size()))
            std::fprintf(stderr, "[loco]   occupant %s house=%d moving=%d wait=%.1f\n", e.units[(std::size_t)oo].image.c_str(),
                         e.units[(std::size_t)oo].house, e.units[(std::size_t)oo].moving() ? 1 : 0,
                         e.units[(std::size_t)oo].wait_time);
    }
    // The blocking occupant (none -> terrain: a cliff, a closed cell).
    int o = Occupancy::kEmpty;
    if (!cliff) {
        o = e.occ.at(bc.x, bc.y, LB);
        if (o == self) o = Occupancy::kEmpty;
        if (o == Occupancy::kEmpty && e.occ.subcells())
            for (int s = Occupancy::kFirstSpot; s < Occupancy::kFirstSpot + Occupancy::kSpots; ++s) {
                const int v = e.occ.spot_unit(bc.x, bc.y, s, LB);
                if (v != Occupancy::kEmpty && v != self) { o = v; break; }
            }
        if (o >= static_cast<int>(e.units.size())) o = Occupancy::kEmpty;
    }
    const Cellxy goal = u.path.empty() ? Cellxy{u.cell_x, u.cell_y} : u.path.back();
    if(o>=0) {
        const auto& owner=e.units[o];
        // A completed mover must not keep a remote cell reserved. Repair an
        // orphan left by an older save/mission before treating it as traffic.
        if(!owner.moving() && owner.loco.trk<0 &&
           std::abs(owner.fx-owner.cell_x)<1e-9 && std::abs(owner.fy-owner.cell_y)<1e-9 &&
           (owner.cell_x!=bc.x || owner.cell_y!=bc.y)) {
            e.occ.release(bc.x,bc.y,o);
            diag::note("TRAFFIC_STALE_RELEASE unit=%d cell=%d,%d owner=%d owner_cell=%d,%d",self,bc.x,bc.y,o,owner.cell_x,owner.cell_y);
            return;
        }
    }
    if(diag::state().enabled.load() && o>=0 && u.wait_time>=3.0 && (u.wait_time-e.dt<3.0 || diag::state().frame.load()%150==0))
        diag::note("TRAFFIC_BLOCK unit=%d image=%s wait_s=%.2f cell=%d,%d next=%d,%d goal=%d,%d layer=%d blocker=%d blocker_image=%s blocker_moving=%d blocker_wait=%.2f allied=%d",
            self,u.image.c_str(),u.wait_time,u.cell_x,u.cell_y,bc.x,bc.y,goal.x,goal.y,LB,o,
            e.units[o].image.c_str(),e.units[o].moving(),e.units[o].wait_time,e.allied(u.house,e.units[o].house));
    if (o>=0 && (!e.sc.scatter_out || !e.allied(u.house,e.units[o].house)) && u.wait_time>=3.0 && L.path_delay<=0.0 && repath(e,u,self,3)) {
        L.path_delay=lr.path_delay_frames;
        return;
    }
    auto close_enough = [&]() {
        // [RE: FUN_004B2550 Move 6: dist(destination) < CloseEnough and the
        // height differs by under 2 levels -> stop, mission complete].
        const double dx = (goal.x - u.cell_x) * 256.0, dy = (goal.y - u.cell_y) * 256.0;
        if (om::sqrt(dx * dx + dy * dy) >= static_cast<double>(lr.close_enough)) return false;
        if (e.have_speed && e.sc.level) {
            const auto lv = [&](int x, int y) {
                const std::size_t i = static_cast<std::size_t>(y) * e.w + x;
                return (x >= 0 && y >= 0 && x < e.w && y < e.h && i < e.sc.level->size()) ? (*e.sc.level)[i] : 0;
            };
            const int d = lv(goal.x, goal.y) - lv(u.cell_x, u.cell_y);
            if (d >= 2 || d <= -2) return false;
        }
        return true;
    };
    if (o == Occupancy::kEmpty) {
        // No unit: terrain (a cliff, a building placed on the route). Re-plan on
        // the PathDelay cadence; with no terrain route at all the move is
        // abandoned [RE: FUN_004D3840 path failure].
        if (L.path_delay > 0.0) return;
        L.path_delay = lr.path_delay_frames;
        if (!repath(e, u, self, 0)) arrive(e, u, self);   // no route: the order ends
        return;
    }
    const Unit& blk = e.units[static_cast<std::size_t>(o)];
    if (under_way(blk) && live_on_map(blk)) {
        // Move 2 MovingBlock: wait; re-plan when PathDelay expires, in the
        // blockage mode (around moving units) once BlockagePathDelay ran out.
        if (L.blockage < 0.0) L.blockage = lr.blockage_path_delay;
        else L.blockage -= e.fr;
        if (L.path_delay > 0.0) return;
        L.path_delay = lr.path_delay_frames;
        repath(e, u, self, L.blockage <= 0.0 ? 2 : 1);
        return;
    }
    const bool friendly = e.allied(u.house, blk.house);
    if (friendly && L.temp_tries == 0) {
        // Move 6 Temp, first try: clear the path and re-plan at once.
        L.temp_tries = 1;
        L.path_delay = lr.path_delay_frames;
        repath(e, u, self, 1);
        return;
    }
    if (L.path_delay > 0.0) return;
    L.path_delay = lr.path_delay_frames;
    // (w7fix-combat: the stop ends a plain move; an attack approach keeps
    // re-pathing -- the attack mission would re-issue the same approach every
    // frame and livelock against the stop)
    const bool plain_move = u.target < 0 && u.target_building < 0 && u.attack_cell_x < 0 && !u.crushing;
    if (plain_move && close_enough()) {
        arrive(e, u, self);   // close enough: the move is done
        return;
    }
    if (friendly) {
        // W7-11-09 [RE: CellClass::ScatterContent FUN_00481580(force=1)]: every
        // parked occupant of the cell, vehicle or infantry, is told to scatter
        // (World::order_scatter -- scatter rules: w7fix-combat); the mover waits
        // and retries.
        if (e.sc.scatter_out) {
            int occs[4] = {e.occ.at(bc.x, bc.y, LB), -1, -1, -1};
            if (e.occ.subcells())
                for (int s = Occupancy::kFirstSpot; s < Occupancy::kFirstSpot + Occupancy::kSpots; ++s)
                    occs[s - 1] = e.occ.spot_unit(bc.x, bc.y, s, LB);
            for (int v : occs) {
                if (v < 0 || v == self || v >= static_cast<int>(e.units.size())) continue;
                const Unit& ov = e.units[static_cast<std::size_t>(v)];
                // (a unit busy with a target keeps it: Scatter only moves an
                // idle one -- w7fix-combat; IQ / PlayerScatter gate in
                // World::incoming_scatters when the W7 rules are armed)
                // (w7r2 [RE: FUN_00742880]: a vehicle held up on its own
                // route -- not under way -- is pushed too; order_scatter)
                const bool parked = !ov.moving() || (!under_way(ov) && (ov.is_vehicle || v>self));
                if (live_on_map(ov) && parked && e.allied(u.house, ov.house) && ov.target < 0 &&
                    ov.target_building < 0 && ov.attack_cell_x < 0 && ov.enter_target < 0 &&
                    (!e.sc.world || !e.sc.world->combat_w7().armed ||
                     e.sc.world->incoming_scatters(ov, false, /*nokidding=*/true)))
                    e.sc.scatter_out->push_back(v);
            }
        }
        ++L.temp_tries;
        // Queue the friendly scatter first; keep strict recovery available if
        // there is no space to yield. Neither action should starve the other.
        if(u.wait_time<3.0 || !repath(e,u,self,3))repath(e,u,self,1);
        return;
    }
    // An enemy in the way (Destroyable): re-plan on the PathDelay cadence.
    repath(e, u, self, 1);
}

// Walk (and the generic straight-segment mover): raw leptons along straight
// lines between cell centres [RE: WalkLocomotionClass FUN_00759CE0]. A
// vehicle here (a track unit resuming off-centre after its path was replaced)
// keeps the hull turn-then-move gate.
void step_walk(StepEnv& e, Unit& u, int self, double cells_per_frame) {
    const UnitAnimSeq& seq = anim_for_image(e.anims, u.image);
    if (u.is_vehicle) {
        const std::size_t pi = next_path_index(u);
        if (pi < u.path.size()) {
            const double seg_dx = u.path[pi].x - u.fx;
            const double seg_dy = u.path[pi].y - u.fy;
            if (seg_dx != 0.0 || seg_dy != 0.0) {
                const double want = facing_u16_from_delta(seg_dx, seg_dy);
                u.body_facing = turn_toward(u.body_facing, want, u.rot_u16_per_sec * e.dt);
                double d = std::fmod(want - u.body_facing, 65536.0);
                if (d > 32768.0) d -= 65536.0;
                if (d < -32768.0) d += 65536.0;
                if (std::abs(d) > kAimToleranceU16) return;   // still turning in place
            }
        }
    }
    double budget = cells_per_frame * e.fr;
    while (budget > 0.0 && u.path_idx < u.path.size()) {
        const Cellxy tgt = u.path[u.path_idx];
        if (tgt.x == u.cell_x && tgt.y == u.cell_y && std::fabs(u.fx - tgt.x) < 1e-9 &&
            std::fabs(u.fy - tgt.y) < 1e-9) {   // already here (e.g. path[0])
            ++u.path_idx;
            continue;
        }
        if (!u.is_vehicle) {
            const int f = facing_from_delta(tgt.x - u.cell_x, tgt.y - u.cell_y);
            if (f >= 0) u.facing = f;
        }
        int layer = u.on_deck;
        if (tgt.x != u.cell_x || tgt.y != u.cell_y) {
            const Reserve r = reserve_cell(e, u, self, {u.cell_x, u.cell_y}, u.on_deck, tgt, layer);
            if (r == Reserve::Stop) { arrive(e, u, self); return; }
            if (r != Reserve::Ok) { on_blocked(e, u, self, tgt, r == Reserve::Cliff, layer); return; }
        }
        const double dx = tgt.x - u.fx;
        const double dy = tgt.y - u.fy;
        const double dist = om::sqrt(dx * dx + dy * dy);
        if (dist <= 1e-9 || budget >= dist) {
            if (tgt.x != u.cell_x || tgt.y != u.cell_y) e.occ.release(u.cell_x, u.cell_y, self);   // leave the old cell
            u.fx = tgt.x;
            u.fy = tgt.y;
            u.cell_x = tgt.x;
            u.cell_y = tgt.y;
            if (e.terrain) u.on_deck = static_cast<std::uint8_t>(layer > 0 ? 1 : 0);   // R-210
            squash_on_arrival(e, u, self);   // W7-07-21 (on the layer arrived on)
            budget -= (dist <= 1e-9 ? 0.0 : dist);
            ++u.path_idx;
            u.wait_time = 0.0;
            u.loco.blockage = -1.0;
            u.loco.temp_tries = 0;
        } else {
            const double t = budget / dist;
            u.fx += dx * t;
            u.fy += dy * t;
            budget = 0.0;
        }
    }
    if (u.path_idx >= u.path.size()) {
        arrive(e, u, self);
        return;
    }
    u.anim_time += e.dt;
    const int phase = om::ftol(u.anim_time * kWalkFps) % seq.walk.count;
    u.frame = seq_frame(seq.walk, u.facing, phase);
}

// Drop an in-progress track whose cells no longer match the path (another
// system replaced it): keep the current cell, release the rest.
void abort_track(StepEnv& e, Unit& u, int self) {
    u.loco.trk = -1;
    u.loco.accum = 0.0;
    e.occ.release_all_of(self, u.cell_x, u.cell_y);
    e.occ.set(u.cell_x, u.cell_y, self, u.on_deck);
}

// [RE: Drive FUN_004B0E50 lines 101-188 / Ship FUN_0069FC80] the
// SpeedPercentage ramp toward the desired value (Drive+0x50).
void update_speed_pct(StepEnv& e, Unit& u, int base) {
    UnitLoco& L = u.loco;
    if (!L.t.accelerates) {
        L.pct = L.pct_desired;
        return;
    }
    double d = 1e9;
    if (!u.path.empty()) {
        const double dx = (u.path.back().x - u.fx) * 256.0, dy = (u.path.back().y - u.fy) * 256.0;
        d = om::sqrt(dx * dx + dy * dy);
    }
    const double decel = static_cast<double>(base) * L.t.decel_factor * e.fr;
    if (d < static_cast<double>(L.t.slowdown_distance)) {
        // Approaching the destination: brake to a floor of 0.3.
        L.pct = std::max(0.3, std::min(L.pct, L.pct_desired) - decel);
    } else if (L.pct < L.pct_desired) {
        L.pct = std::min(L.pct + L.t.accel_factor * e.fr, L.pct_desired);
    } else if (L.pct > L.pct_desired) {
        L.pct = std::max(L.pct - decel, L.pct_desired);
    }
}

// Drive / Ship: the track model (see the file comment).
void step_track(StepEnv& e, Unit& u, int self) {
    const UnitAnimSeq& seq = anim_for_image(e.anims, u.image);
    UnitLoco& L = u.loco;
    // A track in progress must still match the path (the next cells).
    if (L.trk >= 0) {
        bool ok = u.path_idx < u.path.size();
        if (ok) {
            const Cellxy n = u.path[u.path_idx];
            if (L.trk_two && !L.trk_in_mid)
                ok = n.x == L.trk_mx && n.y == L.trk_my && u.path_idx + 1 < u.path.size() &&
                     u.path[u.path_idx + 1].x == L.trk_hx && u.path[u.path_idx + 1].y == L.trk_hy;
            else
                ok = n.x == L.trk_hx && n.y == L.trk_hy;
        }
        if (!ok) abort_track(e, u, self);
    }
    const int base = e.base_speed(u);
    if (L.trk < 0 && (std::fabs(u.fx - u.cell_x) > 1e-9 || std::fabs(u.fy - u.cell_y) > 1e-9)) {
        // Off the cell centre without a track (a path replaced mid-track):
        // finish into the cell grid with a straight segment at the straight
        // track's rate (161 speed units per cell).
        update_speed_pct(e, u, base);
        step_walk(e, u, self, static_cast<double>(apply_speed_pct(base, L.pct)) / kStraightUnits);
        return;
    }
    // The speed is taken after Start_Of_Move set the desired percentage for
    // the cell being entered [RE: Process FUN_004B0410 -> FUN_004B2550, then
    // While_Moving FUN_004B0E50].
    bool speed_added = false;
    for (int guard = 0; guard < 8; ++guard) {
        if (L.trk < 0) {
            // [RE: Start_Of_Move FUN_004B2550]
            const std::size_t pi = next_path_index(u);
            if (pi >= u.path.size()) {
                arrive(e, u, self);
                return;
            }
            u.path_idx = pi;
            const Cellxy t0 = u.path[pi];
            const int d0 = dir8_of_step(t0.x - u.cell_x, t0.y - u.cell_y);
            if (d0 < 0) {   // not a neighbour (a teleport-style path): straight segment
                L.accum = 0.0;
                step_walk(e, u, self, static_cast<double>(apply_speed_pct(base, L.pct)) / kStraightUnits);
                return;
            }
            // Turn in place until the facing is EXACTLY the 8-way direction.
            const double want = static_cast<double>(d0) * 8192.0;
            if (u.body_facing != want) {
                if (u.rot_u16_per_sec <= 0.0) {
                    u.body_facing = want;   // a hand-built unit with no ROT turns at once
                } else {
                    u.body_facing = turn_toward(u.body_facing, want, u.rot_u16_per_sec * e.dt);
                    L.accum = 0.0;
                    u.frame = seq_frame(seq.stand, u.facing, 0);
                    return;
                }
            }
            int layer0 = u.on_deck;
            const Reserve r0 = reserve_cell(e, u, self, {u.cell_x, u.cell_y}, u.on_deck, t0, layer0);
            if (r0 == Reserve::Stop) { arrive(e, u, self); return; }
            if (r0 != Reserve::Ok) { on_blocked(e, u, self, t0, r0 == Reserve::Cliff, layer0); return; }
            // Look one step ahead: a 45 / 90 degree bend is one curved track
            // over both cells (TurnTrack flag 8) when the second cell is free.
            bool two = false;
            int layer1 = layer0;
            const TurnTrack* tt = &turn_track(d0, d0);
            if (pi + 1 < u.path.size()) {
                const Cellxy t1 = u.path[pi + 1];
                const int d1 = dir8_of_step(t1.x - t0.x, t1.y - t0.y);
                if (d1 >= 0 && d1 != d0) {
                    const TurnTrack& c = turn_track(d0, d1);
                    if ((c.flags & 8) && c.track > 0) {
                        const double sm = u.speed_mult;
                        const Reserve r1 = reserve_cell(e, u, self, t0, layer0, t1, layer1);
                        u.speed_mult = sm;   // the speed is the first cell's
                        if (r1 == Reserve::Ok) {
                            two = true;
                            tt = &c;
                            L.trk_hx = t1.x;
                            L.trk_hy = t1.y;
                        }
                    }
                }
            }
            if (!two) {
                L.trk_hx = t0.x;
                L.trk_hy = t0.y;
            }
            L.trk = tt->track;
            L.trk_flags = tt->flags & 7;
            L.trk_face = static_cast<int>(tt->face) << 8;
            L.trk_idx = 0;
            L.trk_two = two;
            L.trk_in_mid = false;
            L.trk_mx = t0.x;
            L.trk_my = t0.y;
            L.trk_layer_mid = static_cast<std::int8_t>(layer0);
            L.trk_layer_head = static_cast<std::int8_t>(two ? layer1 : layer0);
            L.pct_desired = e.have_speed ? u.speed_mult : 1.0;
            if (!e.have_speed && loco_is_track(u) && u.max_hp > 0 &&
                static_cast<double>(u.hp) / u.max_hp <= e.sc.loco.condition_yellow)
                L.pct_desired *= 0.75;   // W7-11-06 without a speed context
            u.wait_time = 0.0;
            L.blockage = -1.0;
            L.temp_tries = 0;
        }
        if (!speed_added) {
            speed_added = true;
            update_speed_pct(e, u, base);
            L.accum += static_cast<double>(apply_speed_pct(base, L.pct)) * e.fr;
        }
        // [RE: While_Moving FUN_004B0E50] one track point per 7 speed units.
        const RawTrack& rt = raw_track(L.trk);
        while (L.accum > kTrackUnit && L.trk_idx < rt.n) {
            L.accum -= kTrackUnit;
            int px = 0, py = 0, pf = 0;
            track_point(L.trk, L.trk_idx, L.trk_flags, px, py, pf);
            u.fx = L.trk_hx + px / 256.0;
            u.fy = L.trk_hy + py / 256.0;
            u.body_facing = pf;
            if (L.trk_two && !L.trk_in_mid && L.trk_idx >= rt.cell) {
                // The RawTrack CellIndex: the unit is now in the middle cell.
                e.occ.release(u.cell_x, u.cell_y, self);
                u.cell_x = L.trk_mx;
                u.cell_y = L.trk_my;
                if (e.terrain) u.on_deck = static_cast<std::uint8_t>(L.trk_layer_mid > 0 ? 1 : 0);
                squash_on_arrival(e, u, self);   // W7-07-21 (on the layer arrived on)
                ++u.path_idx;
                L.trk_in_mid = true;
            }
            ++L.trk_idx;
        }
        if (L.trk_idx < rt.n) break;   // mid-track: done for this frame
        // The track's end: on the head-to cell centre, facing the end facing.
        if (u.cell_x != L.trk_hx || u.cell_y != L.trk_hy) e.occ.release(u.cell_x, u.cell_y, self);
        if (L.trk_two && !L.trk_in_mid) {   // (CellIndex past the end: never in the tables)
            e.occ.release(L.trk_mx, L.trk_my, self);
            ++u.path_idx;
        }
        u.fx = L.trk_hx;
        u.fy = L.trk_hy;
        u.cell_x = L.trk_hx;
        u.cell_y = L.trk_hy;
        u.body_facing = L.trk_face;
        if (e.terrain) u.on_deck = static_cast<std::uint8_t>(L.trk_layer_head > 0 ? 1 : 0);
        squash_on_arrival(e, u, self);   // W7-07-21 (on the layer arrived on)
        e.occ.mark_layer(self, u.on_deck);
        e.occ.set(u.cell_x, u.cell_y, self, u.on_deck);
        ++u.path_idx;
        L.trk = -1;
        if (u.path_idx >= u.path.size()) {
            arrive(e, u, self);
            return;
        }
        if (L.accum <= kTrackUnit) break;
    }
    u.anim_time += e.dt;
    const int phase = om::ftol(u.anim_time * kWalkFps) % seq.walk.count;
    u.frame = seq_frame(seq.walk, u.facing, phase);
}

// Hover [RE: HoverLocomotionClass Process FUN_00514130, speed FUN_00515CF0].
void step_hover(StepEnv& e, Unit& u, int self) {
    const UnitAnimSeq& seq = anim_for_image(e.anims, u.image);
    const LocoRules& lr = e.sc.loco;
    UnitLoco& L = u.loco;
    const std::size_t pi = next_path_index(u);
    if (pi >= u.path.size()) {
        arrive(e, u, self);
        return;
    }
    u.path_idx = pi;
    const Cellxy t = u.path[pi];
    int layer = u.on_deck;
    {
        const int ddx = t.x - u.cell_x, ddy = t.y - u.cell_y;
        if (ddx < -1 || ddx > 1 || ddy < -1 || ddy > 1) {   // not a neighbour: straight segment
            step_walk(e, u, self, static_cast<double>(apply_speed_pct(e.base_speed(u), 1.0)) / 256.0);
            return;
        }
    }
    const Reserve r = reserve_cell(e, u, self, {u.cell_x, u.cell_y}, u.on_deck, t, layer);
    if (r == Reserve::Stop) { arrive(e, u, self); return; }
    if (r != Reserve::Ok) { on_blocked(e, u, self, t, r == Reserve::Cliff, layer); return; }
    const double dx = t.x - u.fx, dy = t.y - u.fy;
    const double dist_head = om::sqrt(dx * dx + dy * dy) * 256.0;
    double err = 0.0;
    if (dist_head > 1e-9) {
        // [RE: Hover Link FUN_00513AD0: the facing turns at ROT x 2]
        const double want = facing_u16_from_delta(dx, dy);
        u.body_facing = turn_toward(u.body_facing, want, 2.0 * u.rot_u16_per_sec * e.dt);
        err = std::fmod(want - u.body_facing, 65536.0);
        if (err > 32768.0) err -= 65536.0;
        if (err < -32768.0) err += 65536.0;
    }
    const double gx = (u.path.back().x - u.fx) * 256.0, gy = (u.path.back().y - u.fy) * 256.0;
    const int dist_goal = om::ftol(om::sqrt(gx * gx + gy * gy));
    bool straight = false;   // Path[0] == Path[1]: the next two steps share a direction
    if (pi + 1 < u.path.size())
        straight = dir8_of_step(t.x - u.cell_x, t.y - u.cell_y) ==
                   dir8_of_step(u.path[pi + 1].x - t.x, u.path[pi + 1].y - t.y);
    const double target = hover_target_fraction(om::ftol(err), dist_goal, straight, lr.hover_boost);
    for (int k = 0; k < static_cast<int>(e.fr + 0.5) || k == 0; ++k)
        L.hover = hover_ramp(L.hover, target, lr.hover_accel, lr.hover_brake);
    const double step = static_cast<double>(apply_speed_pct(e.base_speed(u), L.hover)) * e.fr;
    if (dist_head <= step) {
        // Within this frame's step of the head-to point: arrive on it.
        e.occ.release(u.cell_x, u.cell_y, self);
        u.fx = t.x;
        u.fy = t.y;
        u.cell_x = t.x;
        u.cell_y = t.y;
        if (e.terrain) u.on_deck = static_cast<std::uint8_t>(layer > 0 ? 1 : 0);
        squash_on_arrival(e, u, self);   // W7-07-21 (on the layer arrived on)
        u.path_idx = pi + 1;
        u.wait_time = 0.0;
        L.blockage = -1.0;
        L.temp_tries = 0;
        if (u.path_idx >= u.path.size()) {
            arrive(e, u, self);
            return;
        }
        // [RE: FUN_00514130] reaching the head-to point only switches to the
        // next one -- the frame's full step is still taken along the facing,
        // provided the next cell can be marked.
        const double rest = step - dist_head;
        const std::size_t pn = next_path_index(u);
        int nl = u.on_deck;
        if (rest > 0.0 && pn < u.path.size() &&
            reserve_cell(e, u, self, {u.cell_x, u.cell_y}, u.on_deck, u.path[pn], nl) == Reserve::Ok) {
            constexpr double kPi = 3.14159265358979323846;
            const double a = u.body_facing * (2.0 * kPi / 65536.0);
            u.fx += om::sin(a) * rest / 256.0;
            u.fy -= om::cos(a) * rest / 256.0;
        }
    } else if (step > 0.0) {
        // Along the CURRENT facing (drift on a shallow bend).
        constexpr double kPi = 3.14159265358979323846;
        const double a = u.body_facing * (2.0 * kPi / 65536.0);
        u.fx += om::sin(a) * step / 256.0;
        u.fy -= om::cos(a) * step / 256.0;
    }
    u.anim_time += e.dt;
    const int phase = om::ftol(u.anim_time * kWalkFps) % seq.walk.count;
    u.frame = seq_frame(seq.walk, u.facing, phase);
}

}  // namespace

void step_units(std::vector<Unit>& units, const std::vector<std::uint8_t>& passable,
                const std::vector<std::uint8_t>& water_passable, int w, int h,
                Occupancy& occ, double dt, double move_cps, const AnimTable& anims,
                const std::vector<std::uint8_t>* amphib_passable, const MoveSpeedCtx* speed,
                const std::vector<std::uint8_t>* wall_leak, const std::vector<std::uint8_t>* crush_passable,
                const std::vector<std::uint8_t>* crush_leak, const StepTerrain* terrain) {
    static const MoveSpeedCtx kNoSpeed{};
    StepEnv e{units, passable, water_passable, w, h, occ, dt, frames_of(dt), move_cps, anims,
              amphib_passable, speed ? *speed : kNoSpeed, speed != nullptr, wall_leak, crush_passable,
              crush_leak, terrain};
    for (std::size_t i = 0; i < units.size(); ++i) {
        Unit& u = units[i];
        const int self = static_cast<int>(i);
        // Dead/dying, carried and airborne units never move here (combat
        // released a dying unit's cells; CB-2 aircraft fly in step_flight).
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.is_aircraft) continue;
        occ.mark_layer(self, u.on_deck);   // w8fix-layer: the default plane of its claims
        const LocoKind kind = unit_loco_kind(u);
        const bool track = kind == LocoKind::Drive || kind == LocoKind::Ship;
        if (!u.moving() && track && u.loco.trk >= 0 &&
            (std::fabs(u.fx - u.cell_x) > 1e-9 || std::fabs(u.fy - u.cell_y) > 1e-9)) {
            // A stopped track unit finishes the track it is on [RE: Stop_Moving
            // FUN_004AFD10 only lowers the desired speed; the track runs out]
            // -- unless the stop snapped it onto its cell. The cells ahead are
            // re-reserved (a stop may have released them); if one was taken
            // meanwhile the unit backs onto its own cell centre instead.
            UnitLoco& L = u.loco;
            Cellxy ahead[2];
            int al[2] = {0, 0};   // w8fix-layer: the layer of each cell ahead
            int n = 0;
            if (L.trk_two && !L.trk_in_mid) { al[n] = L.trk_layer_mid > 0; ahead[n++] = {L.trk_mx, L.trk_my}; }
            al[n] = L.trk_layer_head > 0;
            ahead[n++] = {L.trk_hx, L.trk_hy};
            bool ok = true;
            for (int k = 0; k < n && ok; ++k)
                if (occ.at(ahead[k].x, ahead[k].y, al[k]) != self && !occ.can_enter(ahead[k].x, ahead[k].y, self, al[k]))
                    ok = false;
            u.path.clear();
            u.path.push_back({u.cell_x, u.cell_y});
            if (ok) {
                for (int k = 0; k < n; ++k) {
                    occ.set(ahead[k].x, ahead[k].y, self, al[k]);
                    u.path.push_back(ahead[k]);
                }
                u.path_idx = 1;
                L.pct_desired = std::min(L.pct_desired, 0.3);
            } else {
                occ.release_all_of(self, u.cell_x, u.cell_y);
                occ.set(u.cell_x, u.cell_y, self, u.on_deck);
                L.trk = -1;
                u.path_idx = 0;
            }
        }
        if (!u.moving()) {
            const UnitAnimSeq& seq = anim_for_image(anims, u.image);
            occ.release_all_of(self,u.cell_x,u.cell_y);
            occ.set(u.cell_x, u.cell_y, self, u.on_deck);   // idle units own their cell (own layer)
            u.fx = u.cell_x;
            u.fy = u.cell_y;
            u.frame = seq_frame(seq.stand, u.facing, 0);
            u.wait_time = 0.0;
            u.loco.trk = -1;
            u.loco.accum = 0.0;
            u.loco.hover = 0.0;
            u.loco.path_delay = 0.0;
            u.loco.blockage = -1.0;
            u.loco.temp_tries = 0;
            continue;
        }
        // W7-06-16 (w7fix-airnav) [RE: FUN_006B7400 != 0 -> the move step
        // returns "hold": ShipLoco FUN_006A1310 @0x6A13E6, DriveLoco
        // FUN_004B2550 @0x4B264C]: a Dreadnought / V3 with its own rockets on
        // the deck stays where it is.
        if (u.an.launch_hold) continue;
        if (track) {
            step_track(e, u, self);
        } else if (kind == LocoKind::Hover) {
            step_hover(e, u, self);
        } else {
            // Walk: raw leptons per frame [RE: FUN_00759CE0 SetSpeedPercentage
            // 1.0 + GetCurrentSpeed], crawling x2/3 [RE: FUN_00521AB0].
            int s = e.base_speed(u);
            if (u.ia.prone) {
                bool crawls = true;
                if (e.sc.inf_tables) {
                    const auto it = e.sc.inf_tables->find(u.image);
                    if (it != e.sc.inf_tables->end()) crawls = it->second.crawls;
                }
                s = infantry_speed(s, true, crawls);
            }
            step_walk(e, u, self, static_cast<double>(s) / 256.0);
        }
    }
}

}  // namespace ra2engine

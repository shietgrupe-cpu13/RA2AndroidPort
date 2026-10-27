// NET L1: the command layer (see command.hpp). The apply of each event type
// reproduces exactly what the pre-L1 UI call did to the World, with the
// "current selection" replaced by the command's explicit Whom list.
#include "command.hpp"
#include "diagnostics.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace ra2engine {

const char* event_name(EventType t) {
    switch (t) {
        case EventType::Empty: return "EMPTY";
        case EventType::PowerOn: return "POWERON";
        case EventType::PowerOff: return "POWEROFF";
        case EventType::Ally: return "ALLY";
        case EventType::MegaMission: return "MEGAMISSION";
        case EventType::MegaMissionF: return "MEGAMISSION_F";
        case EventType::Idle: return "IDLE";
        case EventType::Scatter: return "SCATTER";
        case EventType::Destruct: return "DESTRUCT";
        case EventType::Deploy: return "DEPLOY";
        case EventType::Detonate: return "DETONATE";
        case EventType::Place: return "PLACE";
        case EventType::Options: return "OPTIONS";
        case EventType::GameSpeed: return "GAMESPEED";
        case EventType::Produce: return "PRODUCE";
        case EventType::Suspend: return "SUSPEND";
        case EventType::Abandon: return "ABANDON";
        case EventType::Primary: return "PRIMARY";
        case EventType::SpecialPlace: return "SPECIAL_PLACE";
        case EventType::Exit: return "EXIT";
        case EventType::Animation: return "ANIMATION";
        case EventType::Repair: return "REPAIR";
        case EventType::Sell: return "SELL";
        case EventType::SellCell: return "SELLCELL";
        case EventType::Special: return "SPECIAL";
        case EventType::FrameSync: return "FRAMESYNC";
        case EventType::Message: return "MESSAGE";
        case EventType::ResponseTime: return "RESPONSE_TIME";
        case EventType::FrameInfo: return "FRAMEINFO";
        case EventType::SaveGame: return "SAVEGAME";
        case EventType::Archive: return "ARCHIVE";
        case EventType::AddPlayer: return "ADDPLAYER";
        case EventType::Timing: return "TIMING";
        case EventType::ProcessTime: return "PROCESS_TIME";
        case EventType::PageUser: return "PAGEUSER";
        case EventType::RemovePlayer: return "REMOVEPLAYER";
        case EventType::LatencyFudge: return "LATENCYFUDGE";
        case EventType::MegaFrameInfo: return "MEGAFRAMEINFO";
        case EventType::PacketTiming: return "PACKETTIMING";
        case EventType::AboutToExit: return "ABOUTTOEXIT";
        case EventType::FallbackHost: return "FALLBACKHOST";
        case EventType::AddressChange: return "ADDRESSCHANGE";
        case EventType::PlanConnect: return "PLANCONNECT";
        case EventType::PlanCommit: return "PLANCOMMIT";
        case EventType::PlanNodeDelete: return "PLANNODEDELETE";
        case EventType::AllCheer: return "ALLCHEER";
        case EventType::AbandonAll: return "ABANDON_ALL";
        case EventType::SelectNotify: return "SELECT_NOTIFY";
    }
    return "?";
}

Target Target::unit(const World& w, int idx) {
    Rtti r = Rtti::Infantry;
    if (idx >= 0 && idx < static_cast<int>(w.units().size())) {
        const Unit& u = w.units()[static_cast<std::size_t>(idx)];
        r = u.is_aircraft ? Rtti::Aircraft : u.is_vehicle ? Rtti::Unit : Rtti::Infantry;
    }
    return {r, idx};
}

std::vector<int> Command::whom_ids() const {
    std::vector<int> v;
    v.reserve(whom.size());
    for (const Target& t : whom) v.push_back(t.id);
    return v;
}

bool Command::operator==(const Command& o) const {
    return type == o.type && house == o.house && frame == o.frame && seq == o.seq && whom == o.whom &&
           mission == o.mission && target == o.target && dest == o.dest && rtti_type == o.rtti_type &&
           heap_id == o.heap_id && is_naval == o.is_naval && cx == o.cx && cy == o.cy && cx2 == o.cx2 &&
           cy2 == o.cy2 && value == o.value && ext == o.ext;
}

Command make_mega(const World& w, const std::vector<int>& ids, Mission m, Target target, Target dest) {
    Command c;
    c.type = EventType::MegaMission;
    for (int i : ids) c.whom.push_back(Target::unit(w, i));
    c.mission = m;
    c.target = target;
    c.dest = dest;
    return c;
}

Command make_whom(const World& w, EventType t, const std::vector<int>& unit_ids) {
    Command c;
    c.type = t;
    for (int i : unit_ids) c.whom.push_back(Target::unit(w, i));
    return c;
}

Command make_building_event(EventType t, int building) {
    Command c;
    c.type = t;
    c.whom.push_back(Target::building(building));
    return c;
}

Command make_type_event(EventType t, Rtti rtti, int heap_id, bool naval) {
    Command c;
    c.type = t;
    c.rtti_type = static_cast<std::int32_t>(rtti);
    c.heap_id = heap_id;
    c.is_naval = naval ? 1 : 0;
    return c;
}

// --- queue -------------------------------------------------------------------

namespace {
bool key_less(const Command& a, const Command& b) {
    if (a.frame != b.frame) return a.frame < b.frame;
    if (a.house != b.house) return a.house < b.house;
    return a.seq < b.seq;
}
}  // namespace

void CommandQueue::push(Command c) {
    // Insert after every element that is not greater (stable for equal keys).
    auto it = std::upper_bound(q_.begin(), q_.end(), c, key_less);
    q_.insert(it, std::move(c));
}

std::vector<Command> CommandQueue::take_due(std::uint32_t frame) {
    std::vector<Command> out;
    std::size_t n = 0;
    while (n < q_.size() && q_[n].frame <= frame) ++n;
    out.assign(std::make_move_iterator(q_.begin()), std::make_move_iterator(q_.begin() + static_cast<long>(n)));
    q_.erase(q_.begin(), q_.begin() + static_cast<long>(n));
    return out;
}

std::uint32_t CommandScheduler::schedule(Command c) {
    c.house = static_cast<std::int8_t>(house_);
    c.frame = now_ + delay_;
    c.seq = seq_++;
    const std::uint32_t f = c.frame;
    q_->push(std::move(c));
    return f;
}

// --- rules -------------------------------------------------------------------

bool OrderRules::is_refinery(int t) const {
    return std::find(refinery_types.begin(), refinery_types.end(), t) != refinery_types.end();
}

World::InfiltratePurpose OrderRules::purpose_of(int t) const {
    return (t >= 0 && t < static_cast<int>(infiltrate_purposes.size())) ? infiltrate_purposes[static_cast<std::size_t>(t)]
                                                                         : World::InfiltratePurpose::None;
}

std::vector<int> OrderRules::primary_group(int t) const {
    // [RE: production.md §1.2] set_primary clears only the factory's sibling
    // group (the pre-L1 Input::commit_factory_click resolution).
    if (std::find(inf_factory_types.begin(), inf_factory_types.end(), t) != inf_factory_types.end())
        return inf_factory_types;
    if (std::find(veh_factory_types.begin(), veh_factory_types.end(), t) != veh_factory_types.end())
        return veh_factory_types;
    return {t};
}

// --- apply -------------------------------------------------------------------

namespace {

bool unit_ok(const World& w, int i) { return i >= 0 && i < static_cast<int>(w.units().size()); }
bool bld_ok(const World& w, int b) { return b >= 0 && b < static_cast<int>(w.buildings().size()); }
bool bld_live(const World& w, int b) {
    return bld_ok(w, b) && w.buildings()[static_cast<std::size_t>(b)].alive &&
           w.buildings()[static_cast<std::size_t>(b)].state != Building::State::Dead;
}

// Whom ids restricted to unit references (ascending, as the UI built them).
std::vector<int> unit_whom(const World& w,const Command& c) {
    std::vector<int> v;
    for (const Target& t : c.whom)
        if (t.is_unit() && unit_ok(w,t.id) &&
            (c.house==kAnyHouse || (w.units()[t.id].w7.slave_manager==-1 &&
             (w.house_is_computer(c.house) || !w.unit_berserk(t.id))))) v.push_back(t.id);
    return v;
}

const OrderRules& rules_of(CommandHost* host) {
    static const OrderRules kNone;
    const OrderRules* r = host ? host->order_rules() : nullptr;
    return r ? *r : kNone;
}

void apply_mega(World& w, const Command& c, CommandHost* host) {
    const int H = c.house;
    const std::vector<int> ids = unit_whom(w,c);
    // W4-SQ: a player order takes the units out of their script team.
    w.mark_player_orders(ids);
    if(c.mission!=Mission::QMove)w.clear_waypoints(H,ids);
    switch (c.mission) {
        case Mission::QMove:
            if(c.dest.is_cell())w.queue_waypoint(H,ids,c.dest.cell_x(),c.dest.cell_y());
            return;
        case Mission::Move:
            if (c.dest.is_cell()) w.group_move(H, ids, c.dest.cell_x(), c.dest.cell_y());
            if(c.dest.is_cell())for(const auto& t:c.whom) {
                if(!t.is_building() || !bld_ok(w,t.id) || w.buildings()[t.id].house!=H)continue;
                if(w.can_pack_conyard(t.id)) {
                    w.order_pack_conyard_move(t.id,H,c.dest.cell_x(),c.dest.cell_y());
                    continue;
                }
                if(!w.can_pack_slave_miner(t.id))continue;
                const int mobile=static_cast<int>(w.units().size());
                if(w.order_pack_slave_miner(t.id,H))w.order_unit_move(mobile,c.dest.cell_x(),c.dest.cell_y());
            }
            return;
        case Mission::Attack:
            // W7-02-10: an own firing garrison in the Whom takes the target.
            for (const Target& t : c.whom)
                if (t.is_building() && bld_ok(w, t.id))
                    (void)w.order_building_attack(H,t.id,c.target.is_unit()?c.target.id:-1,
                        c.target.is_building()?c.target.id:-1,c.target.is_cell()?c.target.cell_x():-1,
                        c.target.is_cell()?c.target.cell_y():-1,
                        c.dest.is_cell() && c.dest.cell_x()==-1 && c.dest.cell_y()==-1);
            if (c.target.is_unit()) w.group_attack(H, ids, c.target.id);
            else if (c.target.is_building()) w.group_attack_building(H, ids, c.target.id);
            else if (c.target.is_cell()) w.group_attack_cell(H, ids, c.target.cell_x(), c.target.cell_y());
            return;
        case Mission::Harvest:
            if (c.target.is_cell()) w.group_harvest(H, ids, c.target.cell_x(), c.target.cell_y());
            return;
        case Mission::Enter:
            if (c.target.is_unit()) {
                w.group_enter_transport(H, ids, c.target.id);
            } else if (c.target.is_building() && bld_ok(w, c.target.id)) {
                const Building& b = w.buildings()[static_cast<std::size_t>(c.target.id)];
                // Dock: harvesters entering their own refinery return to it;
                // anything else entering a building garrisons it.
                if (b.alive && b.house == H && rules_of(host).is_refinery(b.type)) w.group_return(H, ids);
                // W7-08-01 / W7-04-06 [RE: Mission_Repair 0x44B780]: damaged
                // vehicles entering an own UnitRepair building dock for service.
                else if (w.depot_wants(H, ids, c.target.id)) (void)w.group_depot(H, ids, c.target.id);
                else w.group_occupy(H, ids, c.target.id);
            }
            return;
        case Mission::Capture: {
            // Engineers: defuse an Ivan bomb (unit or building), else capture an
            // enemy / neutral building or repair an own damaged one. W7-01-04
            // [RE: Mouse_Left_Release 0x4AE660 -> Active_Click_With on every
            // selected object]: each engineer of the Whom acts, not a lone one.
            for (int eng : ids) {
                if (!unit_ok(w, eng)) continue;
                const Unit& e = w.units()[static_cast<std::size_t>(eng)];
                if (!w.can_command(H, e.house) || !w.unit_is_engineer(e)) continue;
                if (c.target.is_unit()) {
                    (void)w.order_defuse(eng, c.target.id, -1);
                    continue;
                }
                if (!c.target.is_building() || !bld_live(w, c.target.id)) return;
                const int bi = c.target.id;
                const World::EngineerAct act = w.engineer_action(eng, bi);
                if (act == World::EngineerAct::Defuse) {
                    (void)w.order_defuse(eng, -1, bi);
                    continue;
                }
                if (act != World::EngineerAct::None) w.order_engineer_capture(eng, bi);
            }
            return;
        }
        case Mission::Sabotage: {
            // Spies infiltrate an enemy / neutral Spyable building (W7-01-04:
            // every spy of the Whom; W7-01-14: never an allied or non-Spyable one).
            if (!c.target.is_building() || !bld_live(w, c.target.id)) return;
            const Building& tb = w.buildings()[static_cast<std::size_t>(c.target.id)];
            const OrderRules& r = rules_of(host);
            for (int spy : ids) {
                if (!unit_ok(w, spy)) continue;
                const Unit& s = w.units()[static_cast<std::size_t>(spy)];
                if (!w.can_command(H, s.house) || !w.unit_is_spy(s) || !w.spy_can_infiltrate(spy, c.target.id)) continue;
                w.order_spy_infiltrate(spy, c.target.id, r.purpose_of(tb.type), r.spy_steal_pct, r.spy_blackout_frames);
            }
            return;
        }
        case Mission::AreaGuard:
        case Mission::Guard:
            w.group_guard(H, ids);
            return;
        case Mission::AttackMove:   // W7-07-12 [RE: YR mission 29]
            if (c.dest.is_cell()) w.group_attack_move(H, ids, c.dest.cell_x(), c.dest.cell_y());
            else if (c.target.is_cell()) w.group_attack_move(H, ids, c.target.cell_x(), c.target.cell_y());
            return;
        case Mission::Stop:
            w.group_stop(H, ids);
            for(const auto& t:c.whom) if(t.is_building())w.stop_building_attack(H,t.id);
            return;
        default:
            return;   // missions the clone's UI never issues
    }
}

void apply_deploy(World& w, const Command& c) {
    const int H = c.house;
    // DeployObject [RE: G16 world-depth.md §A5]: a selected own garrison
    // unloads; then every selected transport unloads, then every selected
    // unit deploys (the pre-L1 deploy_selected order).
    for (const Target& t : c.whom)
        if (t.is_building() && bld_ok(w, t.id)) {
            const Building& b = w.buildings()[static_cast<std::size_t>(t.id)];
            if (b.house == H) {
                if (!b.occupants.empty()) w.unload_occupants(t.id);
                else if(w.can_pack_conyard(t.id)) (void)w.order_pack_conyard(t.id,H);
                else (void)w.order_pack_slave_miner(t.id, H);
            }
        }
    const std::vector<int> ids = unit_whom(w,c);
    w.clear_waypoints(H,ids);
    w.group_unload(H, ids);
    for (int i : ids)
        if (unit_ok(w, i) && w.can_command(H, w.units()[static_cast<std::size_t>(i)].house)) (void)w.order_deploy(i);
}

// The single building of a building event, owned by the issuer, or -1.
int own_building(const World& w, const Command& c) {
    if (c.whom.empty() || !c.whom[0].is_building() || !bld_ok(w, c.whom[0].id)) return -1;
    const Building& b = w.buildings()[static_cast<std::size_t>(c.whom[0].id)];
    if (!b.alive || b.house != c.house) return -1;
    return c.whom[0].id;
}

}  // namespace

bool valid_net_command(const World& w, const Command& c) {
    if (c.house < 0 || c.house >= w.house_count()) return false;
    auto on_map = [&w](int x, int y) { return x >= 0 && y >= 0 && x < w.grid_w() && y < w.grid_h(); };
    auto ok = [&](const Target& t) {
        if (t.none()) return true;
        if (t.is_unit()) return unit_ok(w, t.id);
        if (t.is_building()) return bld_ok(w, t.id);
        if (t.is_cell()) return on_map(t.cell_x(), t.cell_y());
        return true;   // type / house references: resolved (and checked) by the apply
    };
    for (const Target& t : c.whom)
        if (!ok(t)) return false;
    if (!ok(c.target) || !ok(c.dest)) return false;
    switch (c.type) {
        case EventType::Place:
        case EventType::SellCell:
            return on_map(c.cx, c.cy);
        case EventType::SpecialPlace:
            return on_map(c.cx, c.cy) && (c.cx2 < 0 || on_map(c.cx2, c.cy2));
        default:
            return true;
    }
}

void apply_command(World& w, const Command& c, CommandHost* host) {
    const int H = c.house;
    if(c.type!=EventType::SelectNotify) {
        diag::note("COMMAND begin frame=%u seq=%u house=%d event=%s mission=%d actors=%zu target=%d:%d dest=%d:%d type=%d:%d cell=%d,%d value=%d",
            c.frame,c.seq,H,event_name(c.type),static_cast<int>(c.mission),c.whom.size(),static_cast<int>(c.target.rtti),c.target.id,
            static_cast<int>(c.dest.rtti),c.dest.id,c.rtti_type,c.heap_id,c.cx,c.cy,c.value);
        if(diag::state().enabled.load())for(const auto& actor:c.whom) {
            if(actor.is_unit() && actor.id>=0 && actor.id<static_cast<int>(w.units().size())) {
                const auto& u=w.units()[actor.id];diag::note(" actor unit=%d rules=%s image=%s cell=%d,%d hp=%d harv=%d dock=%d",actor.id,u.tg.t.rules_id.c_str(),u.image.c_str(),u.cell_x,u.cell_y,u.hp,static_cast<int>(u.harv),u.dock_refinery);
            } else if(actor.is_building())diag::note(" actor building=%d",actor.id);
        }
    }
    struct Done {const Command& c;~Done(){if(c.type!=EventType::SelectNotify)diag::note("COMMAND end frame=%u seq=%u event=%s",c.frame,c.seq,event_name(c.type));}} done{c};
    switch (c.type) {
        case EventType::MegaMission:
            apply_mega(w, c, host);
            return;
        case EventType::Idle: {
            const std::vector<int> ids = unit_whom(w,c);
            w.mark_player_orders(ids);
            w.group_stop(H, ids);
            for(const auto& t:c.whom)if(t.is_building())w.stop_building_attack(H,t.id);
            return;
        }
        case EventType::Scatter: {
            const std::vector<int> ids = unit_whom(w,c);
            w.mark_player_orders(ids);
            w.clear_waypoints(H,ids);
            w.group_scatter(H, ids);
            return;
        }
        case EventType::Deploy:
            apply_deploy(w, c);
            return;
        case EventType::PowerOn:
        case EventType::PowerOff: {
            const int b = own_building(w, c);
            if (b >= 0) (void)w.set_building_power(b, c.type == EventType::PowerOn ? 1 : 0);
            return;
        }
        case EventType::Repair:
            if (!c.whom.empty() && c.whom[0].is_building()) (void)w.toggle_repair(c.whom[0].id, H);
            return;
        case EventType::Sell:
            if (!c.whom.empty() && c.whom[0].is_building()) (void)w.begin_sell(c.whom[0].id, H);
            return;
        case EventType::SellCell: {
            const int b = w.building_at_cell(c.cx, c.cy);
            if (b >= 0) (void)w.begin_sell(b, H);
            return;
        }
        case EventType::Primary: {
            const int b = own_building(w, c);
            if (b < 0) return;
            w.set_primary(b, rules_of(host).primary_group(w.buildings()[static_cast<std::size_t>(b)].type));
            return;
        }
        case EventType::Archive: {
            const int b = own_building(w, c);
            if (b >= 0 && c.target.is_cell()) w.set_rally(b, c.target.cell_x(), c.target.cell_y());
            return;
        }
        case EventType::Place:
            if (host) host->place(H, c.rtti_type, c.heap_id, c.cx, c.cy);
            return;
        case EventType::Produce:
            if (host) host->produce(H, c.rtti_type, c.heap_id, c.is_naval != 0);
            return;
        case EventType::Suspend:
            if (host) host->suspend(H, c.rtti_type, c.heap_id, c.is_naval != 0);
            return;
        case EventType::Abandon:
            if (host) host->abandon(H, c.rtti_type, c.heap_id, c.is_naval != 0);
            return;
        case EventType::AbandonAll:
            if (host) host->abandon_all(H, c.rtti_type, c.heap_id, c.is_naval != 0);
            return;
        case EventType::SpecialPlace:
            (void)w.fire_superweapon_at(H, static_cast<SuperweaponType>(c.value), c.cx, c.cy, c.cx2, c.cy2);
            return;
        case EventType::Ally: {
            // W9-05-03 [RE: EventClass::Execute FUN_004C6BD0 case 3: the
            // sender's IsAlliedWith(HouseID) ? MakeEnemy FUN_004F9DB0 :
            // MakeAlly FUN_004F9990 -- ONE-WAY, no longer the symmetric pair
            // toggle]. The sender-side gates (ToggleAlliance FUN_007309C0:
            // AlliesAllowed / AllyChangeAllowed, a human target) are
            // re-checked here too (hardening: every peer has the same rules).
            const int other = c.value;
            if (other < 0 || other >= w.house_count() || other == H) return;
            if (!w.allies_allowed() || !w.ally_change_allowed()) return;
            const World::AllyChange ch = w.toggle_alliance(H, other);
            if (host && ch != World::AllyChange::None) host->diplomacy(H, other, ch);
            return;
        }
        case EventType::Destruct:
            // W9-05-06 [RE: EVENT_DESTRUCT -> HouseClass FUN_004FC7A0 sets
            // ToDie (+0x1F6) unless already won / lost; HouseClass::AI then
            // blows the house up and MPlayer_Defeated runs]. [RE-FALLBACK:
            // the clone resigns the house at once (resign_house).]
            if (H >= 0 && H < w.house_count() && w.victory_enabled() && !w.house_defeated(H) &&
                w.house_outcome(H) == 0) {
                w.resign_house(H);
                if (host) host->destructed(H);
            }
            return;
        case EventType::AllCheer:
            w.make_house_cheer(H);
            return;
        case EventType::SelectNotify:
            if (host) host->selection_notify(H, std::vector<int>(c.ext.begin(), c.ext.end()));
            return;
        default:
            return;   // session / transport events: not sim actions
    }
}

// --- text form ---------------------------------------------------------------

std::string command_to_line(const Command& c) {
    std::ostringstream o;
    o << "C " << c.frame << ' ' << static_cast<int>(c.house) << ' ' << c.seq << ' ' << static_cast<int>(c.type) << ' '
      << static_cast<int>(c.mission) << ' ' << static_cast<int>(c.target.rtti) << ' ' << c.target.id << ' '
      << static_cast<int>(c.dest.rtti) << ' ' << c.dest.id << ' ' << c.rtti_type << ' ' << c.heap_id << ' '
      << c.is_naval << ' ' << c.cx << ' ' << c.cy << ' ' << c.cx2 << ' ' << c.cy2 << ' ' << c.value << ' '
      << c.whom.size();
    for (const Target& t : c.whom) o << ' ' << static_cast<int>(t.rtti) << ' ' << t.id;
    o << ' ' << c.ext.size();
    for (std::int32_t v : c.ext) o << ' ' << v;
    return o.str();
}

bool command_from_line(const std::string& line, Command& out) {
    std::istringstream in(line);
    char tag = 0;
    in >> tag;
    if (tag != 'C') return false;
    Command c;
    long long frame = 0, seq = 0;
    int house = 0, type = 0, mission = 0, tr = 0, dr = 0, cx = 0, cy = 0, cx2 = 0, cy2 = 0;
    std::size_t nw = 0, ne = 0;
    in >> frame >> house >> seq >> type >> mission >> tr >> c.target.id >> dr >> c.dest.id >> c.rtti_type >>
        c.heap_id >> c.is_naval >> cx >> cy >> cx2 >> cy2 >> c.value >> nw;
    if (!in || nw > 1000000) return false;
    c.frame = static_cast<std::uint32_t>(frame);
    c.house = static_cast<std::int8_t>(house);
    c.seq = static_cast<std::uint32_t>(seq);
    c.type = static_cast<EventType>(type);
    c.mission = static_cast<Mission>(mission);
    c.target.rtti = static_cast<Rtti>(tr);
    c.dest.rtti = static_cast<Rtti>(dr);
    c.cx = static_cast<std::int16_t>(cx);
    c.cy = static_cast<std::int16_t>(cy);
    c.cx2 = static_cast<std::int16_t>(cx2);
    c.cy2 = static_cast<std::int16_t>(cy2);
    for (std::size_t k = 0; k < nw; ++k) {
        int r = 0;
        Target t;
        in >> r >> t.id;
        t.rtti = static_cast<Rtti>(r);
        c.whom.push_back(t);
    }
    in >> ne;
    if (!in || ne > 1000000) return false;
    for (std::size_t k = 0; k < ne; ++k) {
        std::int32_t v = 0;
        in >> v;
        c.ext.push_back(v);
    }
    if (!in) return false;
    out = std::move(c);
    return true;
}

}  // namespace ra2engine

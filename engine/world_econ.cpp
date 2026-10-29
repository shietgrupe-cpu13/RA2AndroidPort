// L2: the per-house economy inside the World -- wallets, the six FactoryClass
// production queues, hold / abandon / queue-behind, ready-to-place structures,
// the unit exit handoff, primary factory / rally, and the per-house outcome.
//
// [RE: HouseClass / FactoryClass -- docs/re/production.md §1.1-§1.5,
// construction.md §7-§10, economy.md §6a; the network events PRODUCE 0x0E
// (HouseClass::BeginProduction FUN_004FA170), SUSPEND 0x0F (FUN_004FA730 ->
// FactoryClass::Suspend FUN_004C9D80), ABANDON 0x10 / ABANDON_ALL 0x2E
// (FUN_004FA830 -> FactoryClass::Abandon FUN_004C9F10, next queued object via
// FUN_004CA4C0), PLACE 0x0B (FUN_004FAF00), PRIMARY 0x11, REPAIR 0x15,
// SELL 0x16, executed in EventClass::Execute FUN_004C6BD0 (switch @0x4C6BF7);
// docs/gap/net/01-original-protocol.md §3.2.]
//
// This is the former GameScene code (prod_tick, hold_begin/hold_end,
// refill_unit_queues, rp_launch_ready_units, place_building, cameo_click's
// state changes) moved into the sim, parameterised by house, so a human house
// and a computer house keep their money and queues in the same HouseEcon and
// several human houses can play in one World. The math is unchanged
// (ProductionQueues / production.cpp, factory.cpp, roster2.cpp).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "orig_math.hpp"   // Net L0: om::ipow / om::ftol
#include "factory.hpp"
#include "roster2.hpp"
#include "capture.hpp"   // apply_blackout
#include "world.hpp"
#include "savegame.hpp"   // W9-05 econ_rules_digest

namespace ra2engine {

namespace {
bool held_bit(std::uint8_t m, int c) { return ((m >> c) & 1u) != 0; }
bool is_unit_cat(QueueCat c) {
    return c == QueueCat::Infantry || c == QueueCat::Vehicle || c == QueueCat::Naval ||
           c == QueueCat::Aircraft;
}
template <class T> T at_or(const std::vector<T>& v, int i, T def) {
    return i >= 0 && i < static_cast<int>(v.size()) ? v[static_cast<std::size_t>(i)] : def;
}
}  // namespace

HouseEcon& World::econ_slot(int house) {
    if (static_cast<int>(econ_.size()) <= house) econ_.resize(static_cast<std::size_t>(house) + 1);
    return econ_[static_cast<std::size_t>(house)];
}

bool World::econ_active(int house) const {
    return house >= 0 && house < static_cast<int>(econ_.size()) &&
           (econ_[static_cast<std::size_t>(house)].player || ai_enabled(house));
}

double World::player_wallet(int house) const {
    return player_economy(house) ? econ_[static_cast<std::size_t>(house)].credits : 0.0;
}

void World::enable_player_economy(int house, double start_credits) {
    if (house < 0) return;
    HouseEcon& e = econ_slot(house);
    e = HouseEcon{};
    e.player = true;
    e.credits = start_credits;
    e.queues.set_credits(e.credits);
    set_human(house, true);
}

void World::set_player_econ_rules(int house, PlayerEconRules rules) {
    if (house < 0) return;
    const std::size_t n = static_cast<std::size_t>(house) + 1;
    if (econ_rules_.size() < n) {
        econ_rules_.resize(n);
        econ_rules_set_.resize(n, 0);
    }
    econ_rules_[static_cast<std::size_t>(house)] = std::move(rules);
    econ_rules_set_[static_cast<std::size_t>(house)] = 1;
    // A just-loaded house sees its stolen tech at once (not only from the
    // next step), so a command applied before that step judges the same.
    apply_stolen_tech_gate(econ_rules_[static_cast<std::size_t>(house)].units, house);
    // W9-05: the sync hash covers what each peer installed (a digest; the
    // later stolen-tech gating is a function of hashed state).
    if (econ_rules_digest_.size() < n) econ_rules_digest_.resize(n, 0);
    econ_rules_digest_[static_cast<std::size_t>(house)] = save::econ_rules_digest(econ_rules_[static_cast<std::size_t>(house)]);
    if (const char* e = std::getenv("RA2_NET_ECON_DUMP"); e && *e == '1')   // W9-05 diagnostic: per-field digests (a desync names the field)
        for (const auto& fd : save::econ_rules_field_digests(econ_rules_[static_cast<std::size_t>(house)]))
            std::fprintf(stderr, "[econdump] house %d %s %016llx\n", house, fd.first.c_str(),
                         static_cast<unsigned long long>(fd.second));
}

const PlayerEconRules* World::player_econ_rules(int house) const {
    if (house < 0 || house >= static_cast<int>(econ_rules_set_.size()) ||
        !econ_rules_set_[static_cast<std::size_t>(house)])
        return nullptr;
    return &econ_rules_[static_cast<std::size_t>(house)];
}

bool World::player_economy(int house) const {
    return house >= 0 && house < static_cast<int>(econ_.size()) &&
           econ_[static_cast<std::size_t>(house)].player && !ai_enabled(house);
}

const HouseEcon* World::econ(int house) const {
    return econ_active(house) ? &econ_[static_cast<std::size_t>(house)] : nullptr;
}

double World::credits(int house) const {
    if (econ_active(house)) return econ_[static_cast<std::size_t>(house)].credits;
    return house_credits(house);
}

void World::set_wallet(int house, double amount) {
    if (house < 0) return;
    if (econ_active(house)) {
        econ_[static_cast<std::size_t>(house)].credits = amount;
        econ_[static_cast<std::size_t>(house)].queues.set_credits(amount);
    } else if (house < house_count()) {
        house_credits_[static_cast<std::size_t>(house)] = amount;
    }
}

// MultipleFactory build-SPEED multiplier for one category [RE-GATE APPLIED:
// production.md §1.2 -- each EXTRA same-class alive Idle owned factory
// multiplies build TIME by MultipleFactory (0.8), FUN_006F3D00; the tick
// SPEED multiplier is mf^-(n-1)]. n <= 1 -> 1.0. (Was main.cpp
// factory_speedup, house 0.)
double World::econ_factory_speedup(int house, const std::vector<int>& ftypes, double mf) const {
    int n = 0;
    for (const Building& b : buildings_) {
        if (!b.alive || b.state != Building::State::Idle || b.house != house) continue;
        for (int t : ftypes)
            if (b.type == t) { ++n; break; }
    }
    if (n <= 1 || mf <= 0.0) return 1.0;
    return om::ipow(1.0 / mf, n - 1);   // Net L0: deterministic, no libm pow
}

// An own alive Idle ConYard (BuildingType factory) [RE: construction.md §10].
bool World::econ_conyard_alive(int house) const {
    const std::vector<BuildingTypeDef>& bt = btypes();
    for (const Building& b : buildings_) {
        if (!b.alive || b.state != Building::State::Idle || b.house != house) continue;
        if (b.type >= 0 && b.type < static_cast<int>(bt.size()) && bt[static_cast<std::size_t>(b.type)].conyard)
            return true;
    }
    return false;
}

void World::step_player_economies(double dt) {
    for (int h = 0; h < static_cast<int>(econ_.size()); ++h) {
        if (!player_economy(h) || !player_econ_rules(h)) continue;
        step_player_econ(h, dt);
    }
}

// One house's production tick (was GameScene's per-tick glue: hold_begin,
// prod_tick, hold_end, the wallet read-back + income drain, the exit handoff
// and refill_unit_queues -- same order, same math).
void World::step_player_econ(int house, double dt) {
    HouseEcon& e = econ_[static_cast<std::size_t>(house)];
    PlayerEconRules& r = econ_rules_[static_cast<std::size_t>(house)];
    ProductionQueues& q = e.queues;
    // wave-5 (R-042): spy-stolen tech unlocks its units for this house.
    apply_stolen_tech_gate(r.units, house);
    q.set_credits(e.credits);
    // G-07 hold: a suspended queue does not advance -- ProductionQueues::tick
    // early-outs on a READY slot, so mark held slots ready for the tick only
    // [RE: FactoryClass::AI skips a suspended factory, IsSuspended +0x70].
    std::uint8_t marked = 0;
    for (int c = 0; c < kQueueCats; ++c) {
        if (!held_bit(e.held, c)) continue;
        ProductionState& ps = q.state(static_cast<QueueCat>(c));
        if (ps.slot.type >= 0 && !ps.slot.ready) {
            ps.slot.ready = true;
            marked |= static_cast<std::uint8_t>(1u << c);
        }
    }
    // [RE-GATE APPLIED: production.md §1.1/§1.2/§1.5] power x MultipleFactory
    // per category; Structure+Defense share the ONE ConYard factory-count slot.
    // A spied power plant forces OUTPUT to 0 while the blackout runs
    // [RE: capture.md §B2].
    const PowerTotals pt = apply_blackout(power_totals(buildings_, btypes(), house),
                                          house_blacked_out(house));
    const double pmul = build_speed_multiplier(pt, r.low_power_mod);
    const double mf = r.multiple_factory;
    const double bsp = econ_factory_speedup(house, r.factory_types[0], mf);
    const double sp[kQueueCats] = {
        bsp, bsp,
        econ_factory_speedup(house, r.factory_types[static_cast<int>(QueueCat::Infantry)], mf),
        econ_factory_speedup(house, r.factory_types[static_cast<int>(QueueCat::Vehicle)], mf),
        econ_factory_speedup(house, r.factory_types[static_cast<int>(QueueCat::Naval)], mf),
        econ_factory_speedup(house, r.factory_types[static_cast<int>(QueueCat::Aircraft)], mf)};
    q.tick_all(dt, pmul, sp, btypes());
    // ConYard lost cancels BOTH building queues through the broadcast cancel,
    // Structure first so each refund accumulates [RE: production.md §1.1].
    if (!econ_conyard_alive(house)) {
        if (q.producing(QueueCat::Structure) != -1) q.cancel(QueueCat::Structure);
        if (q.producing(QueueCat::Defense) != -1) q.cancel(QueueCat::Defense);
    }
    // Factory lost cancels + refunds the matching unit queue.
    for (QueueCat c : {QueueCat::Infantry, QueueCat::Vehicle, QueueCat::Naval, QueueCat::Aircraft})
        if (q.producing(c) != -1 && factory_category_lost(r.factory_types[static_cast<int>(c)], house))
            q.cancel(c);
    q.reconcile_refund();
    // hold end: unmark, and a slot that emptied (a cancel) drops its hold.
    for (int c = 0; c < kQueueCats; ++c) {
        ProductionState& ps = q.state(static_cast<QueueCat>(c));
        if (((marked >> c) & 1u) && ps.slot.type >= 0) ps.slot.ready = false;
        if (held_bit(e.held, c) && ps.slot.type < 0) e.held &= static_cast<std::uint8_t>(~(1u << c));
    }
    e.credits = q.structure().credits;
    e.credits += take_income(house);   // harvest / sell / tech income lands in the wallet
    econ_launch_ready_units(house);
    econ_refill(house);
}

// Exit handoff: each READY unit queue launches from its category's primary
// factory [RE: production.md §1.3, KickOutUnit FUN_00443BC0]; a HARV/CMIN
// becomes a working miner [RE: economy.md §1].
void World::econ_launch_ready_units(int house) {
    HouseEcon& e = econ_[static_cast<std::size_t>(house)];
    const PlayerEconRules& r = econ_rules_[static_cast<std::size_t>(house)];
    ProductionQueues& q = e.queues;
    for (QueueCat cat : {QueueCat::Infantry, QueueCat::Vehicle, QueueCat::Naval, QueueCat::Aircraft}) {
        if (!q.ready(cat)) continue;
        const int fi = primary_factory(buildings_, r.factory_types[static_cast<int>(cat)], house);
        if (fi < 0) continue;
        const int uslot = q.producing(cat);
        if (uslot < 0 || uslot >= static_cast<int>(r.units.size())) {
            q.placed(cat);
            continue;
        }
        const UnitTypeDef& ud = r.units[static_cast<std::size_t>(uslot)];
        const int ft = buildings_[static_cast<std::size_t>(fi)].type;
        if (ft < 0 || ft >= static_cast<int>(btypes().size())) continue;
        const BuildingTypeDef fdef = btypes()[static_cast<std::size_t>(ft)];
        const int uidx = launch_unit(fi, fdef, ud.combat);
        if (uidx >= 0) {
            if (ud.harvester) make_harvester(uidx, ud.chrono_miner, ud.storage > 0 ? ud.storage : 20);
            // The launched unit's art sequences (an image with none keeps the
            // table's entry -- default_anim() when never registered).
            if (at_or<char>(r.unit_has_anim, uslot, 0))
                set_anim(ud.combat.image, r.unit_anims[static_cast<std::size_t>(uslot)]);
            q.placed(cat);
            econ_launches_.push_back({house, uidx});
        }
    }
}

// The next queued object starts as the factory frees [RE: FUN_004CA4C0].
// only_cat >= 0 refills just that category (the abandon path).
void World::econ_refill(int house, int only_cat) {
    HouseEcon& e = econ_[static_cast<std::size_t>(house)];
    const PlayerEconRules* r = player_econ_rules(house);
    if (!r) return;
    for (int c = 0; c < kQueueCats; ++c) {
        if (only_cat >= 0 && c != only_cat) continue;
        std::vector<int>& bl = e.backlog[static_cast<std::size_t>(c)];
        const QueueCat cat = static_cast<QueueCat>(c);
        if (bl.empty() || e.queues.producing(cat) != -1) continue;
        const int type = bl.front();
        bl.erase(bl.begin());
        if (can_start_unit(e.queues.state(cat), type, r->units, buildings_, btypes(), house, r->groups,
                           !factory_category_lost(r->factory_types[static_cast<std::size_t>(c)], house), &units_)) {
            e.queues.set_credits(e.credits);
            e.queues.start_unit(cat, type, r->units, r->build_speed);
        }
    }
}

bool World::econ_can_build(int house, QueueCat cat, int type) const {
    const PlayerEconRules* r = player_econ_rules(house);
    if (!r || !player_economy(house)) return false;
    ProductionState probe;
    if (is_unit_cat(cat)) {
        if (type < 0 || type >= static_cast<int>(r->units.size())) return false;
        // BuildLimit= (Tanya, Boris: 1): none while the limit is on the field
        // [RE-FALLBACK: counted by the unit's art image, as the sidebar does].
        const int lim = at_or<int>(r->unit_build_limit, type, 0);
        if (lim > 0) {
            int alive = 0;
            const std::string& img = r->units[static_cast<std::size_t>(type)].combat.image;
            for (const Unit& u : units_) alive += u.alive && u.house == house && u.image == img;
            if (alive >= lim) return false;
        }
        return can_start_unit(probe, type, r->units, buildings_, btypes(), house, r->groups,
                              !factory_category_lost(r->factory_types[static_cast<int>(cat)], house), &units_);
    }
    if (type < 0 || type >= static_cast<int>(btypes().size())) return false;
    if ((cat == QueueCat::Structure || cat == QueueCat::Defense) &&
        at_or<char>(r->water_bound, type, 0)) {
        const auto& water = grid_for(MovementDomain::Water);
        if (std::none_of(water.begin(), water.end(), [](std::uint8_t cell) { return cell != 0; })) return false;
    }
    if (!r->bld_allowed.empty() && !at_or<char>(r->bld_allowed, type, 0)) return false;
    return can_start(probe, type, btypes(), buildings_, house, r->groups, econ_conyard_alive(house), &units_);
}

int World::econ_queued_count(int house, QueueCat cat, int type) const {
    const HouseEcon* e = econ(house);
    if (!e) return 0;
    int n = e->queues.producing(cat) == type ? 1 : 0;
    for (int t : e->backlog[static_cast<std::size_t>(cat)]) n += (t == type);
    return n;
}

bool World::econ_held(int house, QueueCat cat) const {
    const HouseEcon* e = econ(house);
    return e && held_bit(e->held, static_cast<int>(cat));
}

ProdResult World::econ_produce(int house, QueueCat cat, int type) {
    if (!player_economy(house) || !player_econ_rules(house)) return ProdResult::Refused;
    HouseEcon& e = econ_[static_cast<std::size_t>(house)];
    const PlayerEconRules& r = econ_rules_[static_cast<std::size_t>(house)];
    const int ci = static_cast<int>(cat);
    ProductionState& ps = e.queues.state(cat);
    const bool held = held_bit(e.held, ci);
    if (is_unit_cat(cat)) {
        if (type < 0 || type >= static_cast<int>(r.units.size())) return ProdResult::Refused;
        // The suspended current object of this type restarts (FUN_004C9DC0).
        if (ps.slot.type == type && held) {
            e.held &= static_cast<std::uint8_t>(~(1u << ci));
            return ProdResult::Resumed;
        }
        // CanBuild (vtbl+0x94) against an idle slot: prereqs, tech, factory.
        if (!econ_can_build(house, cat, type)) return ProdResult::Refused;
        if (ps.slot.type == -1) {
            e.queues.set_credits(e.credits);
            e.queues.start_unit(cat, type, r.units, r.build_speed);
            return ProdResult::Started;
        }
        // Busy: queue one more [RE: FactoryClass::Set FUN_004C9B90 refuses when
        // the queued count +0x50 >= MaximumQueuedObjects Rules+0xF0].
        std::vector<int>& bl = e.backlog[static_cast<std::size_t>(ci)];
        if (static_cast<int>(bl.size()) >= r.max_queued) return ProdResult::Refused;
        bl.push_back(type);
        return ProdResult::Queued;
    }
    // ---- buildings (one at a time) ----
    if (cat != QueueCat::Structure && cat != QueueCat::Defense) return ProdResult::Refused;
    if (type < 0 || type >= static_cast<int>(btypes().size())) return ProdResult::Refused;
    if (ps.slot.type == type) {
        if (held && !ps.slot.ready) {   // resume
            e.held &= static_cast<std::uint8_t>(~(1u << ci));
            return ProdResult::Resumed;
        }
        return ProdResult::Refused;     // running / done: no effect
    }
    // [FUN_004FA170: a BuildingType request on a factory whose object is
    // running (not suspended) is refused -> the strip says UnableToComply.]
    if (ps.slot.type >= 0 && (!held || ps.slot.ready)) return ProdResult::Busy;
    if (!econ_can_build(house, cat, type)) return ProdResult::Refused;
    // FactoryClass::Set for a BuildingType abandons the (suspended) current
    // object first (FUN_004C9F10, full refund).
    if (ps.slot.type >= 0) {
        e.queues.set_credits(e.credits);
        e.queues.cancel(cat);
        e.queues.reconcile_refund();
        e.credits = e.queues.structure().credits;
        e.held &= static_cast<std::uint8_t>(~(1u << ci));
    }
    e.queues.set_credits(e.credits);
    ra2engine::start(ps, type, btypes(), r.build_speed);
    // A wall builds WallBuildSpeedCoefficient (rulesmd 3.0) times slower
    // [RE: roster2.md TL;DR 5; TechnoClass::Time_To_Build FUN_006F3D00 wall
    // branch] (G-30).
    if (r.wall_rules && at_or<char>(r.wall, type, 0)) {
        const int wf = wall_build_frames(btypes()[static_cast<std::size_t>(type)].cost, r.build_speed,
                                         r.wall_coefficient);
        ps.slot.total_frames = std::max(1, wf);
        ps.slot.step_ticks = std::clamp(ps.slot.total_frames / kProductionSteps, 1, 255);
    }
    return ProdResult::Started;
}

ProdResult World::econ_suspend(int house, QueueCat cat) {
    if (!player_economy(house)) return ProdResult::Refused;
    HouseEcon& e = econ_[static_cast<std::size_t>(house)];
    const int ci = static_cast<int>(cat);
    const ProductionState& ps = e.queues.state(cat);
    // [FUN_004FA730 -> FUN_004C9D80 sets IsSuspended once.] A finished object
    // is not suspended (the strip never sends it; a READY slot does not tick).
    if (ps.slot.type < 0 || ps.slot.ready || held_bit(e.held, ci)) return ProdResult::Refused;
    e.held |= static_cast<std::uint8_t>(1u << ci);
    return ProdResult::Suspended;
}

ProdResult World::econ_abandon(int house, QueueCat cat, int type, bool all) {
    if (!player_economy(house)) return ProdResult::Refused;
    HouseEcon& e = econ_[static_cast<std::size_t>(house)];
    const int ci = static_cast<int>(cat);
    std::vector<int>& bl = e.backlog[static_cast<std::size_t>(ci)];
    bool dequeued = false;
    if (all) {
        const std::size_t n0 = bl.size();
        bl.erase(std::remove(bl.begin(), bl.end(), type), bl.end());
        dequeued = bl.size() != n0;
    } else {
        // One queued copy goes first (the latest one queued).
        const auto last = std::find(bl.rbegin(), bl.rend(), type);
        if (last != bl.rend()) {
            bl.erase(std::next(last).base());
            return ProdResult::Dequeued;
        }
    }
    if (e.queues.producing(cat) != type) return dequeued ? ProdResult::Dequeued : ProdResult::Refused;
    // FactoryClass::Abandon: refund everything spent, then broadcast the pool.
    e.queues.set_credits(e.credits);
    e.queues.cancel(cat);
    e.queues.reconcile_refund();
    e.credits = e.queues.structure().credits;
    e.held &= static_cast<std::uint8_t>(~(1u << ci));
    // The next queued object starts at once (FUN_004FA830 -> FUN_004CA4C0).
    econ_refill(house, ci);
    return ProdResult::Abandoned;
}

PlacementCheck World::econ_placement(int house, int type, int cx, int cy) const {
    const PlayerEconRules* r = player_econ_rules(house);
    const std::vector<BuildingTypeDef>& bt = btypes();
    if (type < 0 || type >= static_cast<int>(bt.size())) return {};
    const BuildingTypeDef& d = bt[static_cast<std::size_t>(type)];
    const bool water = r && at_or<char>(r->water_bound, type, 0);
    const std::vector<std::uint8_t>& grid = water ? grid_for(MovementDomain::Water) : passable();
    if (water && grid.empty()) return {};
    // BaseNormal=no neighbours do not extend the base (G-28).
    std::vector<char> base_normal;
    if (r && !r->base_normal.empty()) {
        base_normal = r->base_normal;
        if (base_normal.size() < bt.size()) base_normal.resize(bt.size(), 1);
    }
    // W8-03-01 [RE: FUN_0047C530]: a land building also needs a buildable
    // cell (no overlay / slope / bridge, Buildable land); a wall may stand on
    // ore. WaterBound keeps its water-grid test.
    const bool wall = r && at_or<char>(r->wall, type, 0);
    // W9-05-09 [RE: FUN_004A8DC0 @0x4A8F0A]: BuildOffAlly -- an allied
    // building (owner.IsAlliedWith(placer)) of an EligibileForAllyBuilding type.
    std::vector<char> allied_owner;
    AllyBuild ally;
    if (r && r->build_off_ally && !r->ally_eligible.empty()) {
        allied_owner.assign(static_cast<std::size_t>(house_count()), 0);
        for (int h = 0; h < house_count(); ++h)
            allied_owner[static_cast<std::size_t>(h)] = h != house && allied_with(h, house) ? 1 : 0;
        ally.allied_owner = &allied_owner;
        ally.eligible_type = &r->ally_eligible;
    }
    PlacementCheck res = placement_check(cx, cy, d.fw, d.fh, house, d.adjacent, grid, grid_w(), grid_h(),
                                         occupancy(), buildings_, /*bypass_adjacency=*/false,
                                         base_normal.empty() ? nullptr : &base_normal,
                                         water ? nullptr : build_mask(wall),
                                         ally.allied_owner ? &ally : nullptr);
    // W4-MC-04 [RE: FUN_00577e90]: never build outside [Map] LocalSize.
    if (playfield_.valid)
        for (int y = cy; y < cy + d.fh; ++y)
            for (int x = cx; x < cx + d.fw; ++x)
                if (!cell_in_playfield(x, y)) {
                    res.ok = false;
                    const std::size_t k = static_cast<std::size_t>((y - cy) * d.fw + (x - cx));
                    if (k < res.cell_ok.size()) res.cell_ok[k] = false;
                }
    return res;
}

bool World::econ_place_building(int house, int type, int cx, int cy) {
    if (!econ_placement(house, type, cx, cy).ok) return false;
    const PlayerEconRules* r = player_econ_rules(house);
    auto add = [&](int x, int y) {
        const BuildingTypeDef d = btypes()[static_cast<std::size_t>(type)];
        add_building(type, x, y, d.fw, d.fh, house, d.strength, d.armor_ix,
                     r ? at_or<double>(r->buildup_total, type, 0.0) : 0.0, /*instant=*/false, d.sight,
                     d.grants_map_reveal);
        if (r && at_or<char>(r->water_bound, type, 0))
            block_water_footprint(x, y, d.fw, d.fh);   // ships path around the yard
    };
    add(cx, cy);
    // A wall closes the line to the next own wall within GuardRange for free
    // (G-30, roster2.md TL;DR 4).
    if (r && r->wall_rules && at_or<char>(r->wall, type, 0)) {
        auto own_wall = [&](int x, int y) {
            const int bi = building_at_cell(x, y);
            return bi >= 0 && buildings_[static_cast<std::size_t>(bi)].type == type &&
                   buildings_[static_cast<std::size_t>(bi)].house == house;
        };
        auto can_place = [&](int x, int y) {
            const PlacementCheck c = placement_check(x, y, 1, 1, house, 0, passable(), grid_w(), grid_h(),
                                                     occupancy(), buildings_, /*bypass_adjacency=*/true,
                                                     nullptr, build_mask(/*wall=*/true));
            return c.ok;
        };
        for (const Cellxy& c : wall_line_fill(cx, cy, at_or<int>(r->wall_fill_range, type, 0), own_wall, can_place))
            add(c.x, c.y);
    }
    return true;
}

ProdResult World::econ_place(int house, QueueCat cat, int cx, int cy) {
    if (!player_economy(house)) return ProdResult::Refused;
    if (cat != QueueCat::Structure && cat != QueueCat::Defense) return ProdResult::Refused;
    HouseEcon& e = econ_[static_cast<std::size_t>(house)];
    // [FUN_004FAF00: the factory's object must be done (FUN_004CA050).]
    if (!e.queues.ready(cat)) return ProdResult::Refused;
    const int type = e.queues.producing(cat);
    if (type < 0 || !econ_place_building(house, type, cx, cy)) return ProdResult::Refused;
    e.queues.placed(cat);
    return ProdResult::Placed;
}

bool World::econ_set_primary(int house, int b_idx) {
    if (b_idx < 0 || b_idx >= static_cast<int>(buildings_.size())) return false;
    const Building& b = buildings_[static_cast<std::size_t>(b_idx)];
    if (!b.alive || b.house != house) return false;
    const PlayerEconRules* r = player_econ_rules(house);
    if (!r) return false;
    // The category whose factory list holds this type (unit factories only --
    // TogglePrimaryFactory gates on the producer Factory= type, FUN_00448171).
    for (QueueCat c : {QueueCat::Infantry, QueueCat::Vehicle, QueueCat::Naval, QueueCat::Aircraft}) {
        const std::vector<int>& ft = r->factory_types[static_cast<int>(c)];
        if (std::find(ft.begin(), ft.end(), b.type) == ft.end()) continue;
        set_primary(b_idx, ft);
        return true;
    }
    return false;
}

bool World::econ_set_rally(int house, int b_idx, int x, int y) {
    if (b_idx < 0 || b_idx >= static_cast<int>(buildings_.size())) return false;
    if (buildings_[static_cast<std::size_t>(b_idx)].house != house) return false;
    set_rally(b_idx, x, y);
    return true;
}

bool World::econ_power(int house, int b_idx, int control) {
    if (b_idx < 0 || b_idx >= static_cast<int>(buildings_.size())) return false;
    if (buildings_[static_cast<std::size_t>(b_idx)].house != house) return false;
    return set_building_power(b_idx, control);
}

// Per-house outcome + the win-time cheer, inside the sim (was GameScene's
// gf_update outcome latch and its victory_cheered_ world mutation, house 0).
// [RE: HouseClass::Flag_To_Win FUN_004FC800 / Flag_To_Lose FUN_004FC9F0 set
// once per house (+0x1F7 IsWinner / +0x1F8 IsLoser guards); MP defeat
// FUN_004FBED0; docs/re/mcv-deploy.md §5, campaign.md §3.]
// [RE-FALLBACK: the original's win-time cheer call site was not traced; every
// human house of the winning team cheers once.]
void World::step_house_outcomes() {
    if (!victory_enabled_) return;
    const int n = house_count();
    if (static_cast<int>(house_outcome_.size()) < n) house_outcome_.resize(static_cast<std::size_t>(n), 0);
    if (static_cast<int>(victory_cheered_.size()) < n) victory_cheered_.resize(static_cast<std::size_t>(n), 0);
    // W9-05-04 [RE: FUN_004FBED0 tail]: once the game is over every living
    // combatant house wins -- allies of different lobby teams together.
    for (int h = 0; h < n; ++h) {
        int& o = house_outcome_[static_cast<std::size_t>(h)];
        const bool winner = game_over() && house_combatant(h) && !house_defeated(h) && !passive_house(h);
        if (o == 0) {
            if (house_defeated(h)) o = -1;
            else if (winner) o = 1;
        }
        char& ch = victory_cheered_[static_cast<std::size_t>(h)];
        if (!ch && is_human(h) && winner) {
            make_house_cheer(h);
            ch = 1;
        }
    }
}

void World::resign_house(int h) {
    const int n = house_count();
    if (h < 0 || h >= n) return;
    if (static_cast<int>(house_defeated_.size()) < n) house_defeated_.resize(static_cast<std::size_t>(n), 0);
    house_defeated_[static_cast<std::size_t>(h)] = 1;
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        const Unit& u = units_[static_cast<std::size_t>(i)];
        if (u.alive && u.die_time < 0.0 && u.house == h) ability_kill_unit(i);
    }
    for (Building& b : buildings_)
        if (b.alive && b.state != Building::State::Dead && b.house == h) {
            b.hp = 0;
            kill_building(b);
        }
}

}  // namespace ra2engine

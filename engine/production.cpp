#include "production.hpp"
#include "world.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include <algorithm>
#include <cmath>

namespace ra2engine {

int build_frames(int cost, double build_speed) {
    // [RE: construction.md §7, FUN_00711280] Binary operation order:
    // ftol(Cost * BuildSpeed * 0.9). Do NOT algebraically rearrange —
    // /1000*900 rounds differently under IEEE 754 (503 vs 504 for GAPOWR:
    // 800*0.7 = exactly 560.0, *0.9 = exactly 504.0).
    return om::ftol((cost * build_speed * 0.9));
}

PowerTotals power_totals(const std::vector<Building>& buildings,
                         const std::vector<BuildingTypeDef>& types, int house) {
    PowerTotals p;
    for (const Building& b : buildings) {
        if (!b.alive || b.house != house || b.state != Building::State::Idle) continue;
        if (b.type < 0 || b.type >= (int)types.size()) continue;
        int pw = types[b.type].power;
        if (types[b.type].bio_reactor) pw += static_cast<int>(b.occupants.size()) * 100;
        // [RE: BuildingClass::Power_Output FUN_0044E7B0 @0x44E85F-0x44E86A,
        // construction.md §8] a positive PowerBonus is scaled by the health
        // ratio -- ftol(Health / Type.Strength * PowerBonus) -- so a damaged
        // power plant delivers less (drain does not scale, FUN_0044E880).
        // Building::max_hp carries Type.Strength (the map's health % lowers hp).
        const int st = b.max_hp;
        // W7-01-11 [RE: TemporalClass::Fire 0x71A1A0 -> 0x4521B0 HasPower=0]: a
        // plant being erased delivers nothing (LetGo 0x452200 restores it).
        if (pw > 0 && b.w7.warp_unpowered) continue;
        if (pw > 0 && st > 0) p.output += om::ftol(((double)b.hp / (double)st * (double)pw));
        else if (pw >= 0) p.output += pw;
        else if (!b.power_off) p.drain -= pw;   // W5-E2: a powered-down building draws nothing
    }
    return p;
}

double build_speed_multiplier(const PowerTotals& p, double low_power_penalty_modifier) {
    if (p.drain <= p.output) return 1.0;
    // [RE-GATE: construction.md §8] f = output/drain; 1 - mod*(1-f), then
    // floor .5 and (low power only) cap .8.
    double f = (double)p.output / p.drain;
    double s = 1.0 - low_power_penalty_modifier * (1.0 - f);
    s = std::max(s, kMinLowPowerSpeed);
    s = std::min(s, kMaxLowPowerSpeed);
    return s;
}

bool prerequisites_met(const std::vector<std::string>& tokens,
                       const std::vector<Building>& buildings,
                       const std::vector<BuildingTypeDef>& types, int house,
                       const std::vector<std::pair<std::string, std::vector<std::string>>>& groups, const std::vector<Unit>* units) {
    auto have = [&](const std::string& id) {
        for (const Building& b : buildings) {
            // W5-B R-065 (G-31, RE-E1): the original's Prerequisite check reads
            // HouseClass::ActiveBuildingTypes -- objects "currently owned and
            // on the map", gained at Unlimbo (placement) -- so a structure
            // counts from the moment it is placed, build-up included. RE'd:
            // TechnoClass::Unlimbo (FUN_006F61D0 @0x6F62BF) calls
            // HouseClass::RegisterGain (FUN_005028A0), which bumps the
            // per-type counters -- at placement, not at the grand opening.
            if (!b.alive || b.house != house || b.state == Building::State::Dead) continue;
            if (b.type >= 0 && b.type < (int)types.size() && types[b.type].id == id) return true;
        }
        return false;
    };
    for (const std::string& tok : tokens) {
        bool ok = have(tok);
        if (!ok) {
            for (const auto& g : groups) {
                if (g.first != tok) continue;
                for (const std::string& id : g.second)
                    if (have(id)) { ok = true; break; }
                break;
            }
        }
        // YR allows one configured vehicle type to satisfy PROC, even mobile.
        if (!ok && tok == "PROC" && units) {
            for (const auto& g : groups) if (g.first == "PROC_ALTERNATE" && g.second.size() == 1 && !g.second.front().empty())
                for (const Unit& u : *units)
                    if (u.alive && u.die_time < 0 && u.house == house && u.cargo_of < 0 &&
                        u.tg.t.rules_id == g.second.front()) { ok = true; break; }
        }
        if (!ok) return false;
    }
    return true;
}

bool can_start(const ProductionState& ps, int type,
               const std::vector<BuildingTypeDef>& types,
               const std::vector<Building>& buildings, int house,
               const std::vector<std::pair<std::string, std::vector<std::string>>>& groups,
               bool conyard_alive, const std::vector<Unit>* units) {
    if (type < 0 || type >= (int)types.size()) return false;
    if (!conyard_alive || ps.slot.type != -1) return false;
    if (types[type].tech_level <= 0) return false;       // -1 = never buildable
    return prerequisites_met(types[type].prerequisite, buildings, types, house, groups, units);
}

// [RE-GATE: production.md §1.5] Scalar entry point shared by ALL categories.
// Reproduces the old BuildingType start() math EXACTLY (same operations, same
// order, after ps.slot = QueueSlot{}) — see start(ps,type,types,...) below.
void start(ProductionState& ps, int cost, int charge, double build_speed, double time_mult) {
    ps.slot = QueueSlot{};
    ps.slot.type = 0;   // caller (wrapper / queue) overwrites with the real roster index
    // W7-04-03 [RE: Time_To_Build FUN_006F3D00 @0x6F3D4E]: t = ftol(t x
    // BuildTimeMultiplier) after the base (and the country multiplier, 1.0).
    // A multiplier of exactly 1.0 keeps the old value bit-for-bit.
    int t = build_frames(cost, build_speed);
    if (time_mult != 1.0) t = om::ftol(static_cast<double>(t) * time_mult);
    ps.slot.total_frames = std::max(1, t);
    // [RE-GATE: construction.md §10 FUN_004C9DC0] frames per production step.
    ps.slot.step_ticks = std::clamp(ps.slot.total_frames / kProductionSteps, 1, 255);
    // [RE-GATE: economy.md §6a] charge basis: the caller's FreeUnit-subtracted
    // amount when set (> 0), else raw cost — resolved once here so tick()
    // never has to special-case an unset charge (see QueueSlot::charge_basis).
    ps.slot.charge_basis = charge > 0 ? charge : cost;
}

void start(ProductionState& ps, int type, const std::vector<BuildingTypeDef>& types,
           double build_speed) {
    const BuildingTypeDef& t = types[type];
    // charge==0 -> scalar falls back to cost, so this stays BIT-IDENTICAL to
    // the pre-refactor body (max(1,build_frames(cost)), clamp, charge basis).
    start(ps, t.cost, t.charge, build_speed);
    ps.slot.type = type;   // preserve the roster index the old code set
}

void tick(ProductionState& ps, double dt, double speed_mult,
          const std::vector<BuildingTypeDef>& types) {
    if (ps.slot.type == -1 || ps.slot.ready) return;
    (void)types;  // charge basis was already resolved into ps.slot.charge_basis by start()
    ps.slot.frames_accum += dt * 15.0 * speed_mult;
    while (ps.slot.frames_accum >= ps.slot.step_ticks &&
           ps.slot.stage < kProductionSteps) {
        // [RE-GATE: construction.md §10; charge basis economy.md §6a]
        // entering the next stage charges the remaining balance (against
        // charge_basis, NOT raw cost) split over the remaining steps.
        double charge = ((double)ps.slot.charge_basis - ps.slot.spent) /
                        (kProductionSteps - ps.slot.stage);
        if (ps.credits < charge) {
            // Stall: no progress, no charge; retried next tick. Reset the
            // accumulator so a funding gap never produces a catch-up burst.
            ps.slot.frames_accum = 0.0;
            return;
        }
        ps.credits -= charge;
        ps.slot.spent += charge;
        ps.slot.stage += 1;
        ps.slot.frames_accum -= ps.slot.step_ticks;
    }
    if (ps.slot.stage >= kProductionSteps) {
        // settle rounding: total spend is exactly charge_basis
        ps.credits -= (double)ps.slot.charge_basis - ps.slot.spent;
        ps.slot.spent = (double)ps.slot.charge_basis;
        ps.slot.ready = true;
    }
}

void cancel(ProductionState& ps) {
    ps.credits += ps.slot.spent;                          // [RE: construction.md §10]
    ps.slot = QueueSlot{};
}

void placed(ProductionState& ps) { ps.slot = QueueSlot{}; }

bool can_start_unit(const ProductionState& ps, int unit_type,
                    const std::vector<UnitTypeDef>& unit_roster,
                    const std::vector<Building>& buildings,
                    const std::vector<BuildingTypeDef>& btypes, int house,
                    const std::vector<std::pair<std::string, std::vector<std::string>>>& groups,
                    bool factory_alive, const std::vector<Unit>* units) {
    if (unit_type < 0 || unit_type >= (int)unit_roster.size()) return false;
    if (!factory_alive || ps.slot.type != -1) return false;
    if (unit_roster[unit_type].tech_level <= 0) return false;
    // A unit's prereq tokens name BUILDINGS (BARRACKS/FACTORY) -> scan buildings.
    return prerequisites_met(unit_roster[unit_type].prerequisite, buildings, btypes, house, groups, units);
}

void ProductionQueues::start_unit(QueueCat c, int unit_type,
                                  const std::vector<UnitTypeDef>& unit_roster, double build_speed) {
    const UnitTypeDef& u = unit_roster[unit_type];
    start(q_[(int)c], u.cost, u.cost, build_speed, u.combat.w7.build_time_mult);   // §1.5: charge == cost (no FreeUnit); W7-04-03
    q_[(int)c].slot.type = unit_type;                 // roster index into the UNIT roster
}

void ProductionQueues::tick(double dt, double power_mult, const double factory_speedup[4],
                            const std::vector<BuildingTypeDef>& btypes) {
    const double sp[kQueueCats] = {factory_speedup[0], factory_speedup[1], factory_speedup[2],
                                   factory_speedup[3], 1.0, 1.0};
    tick_all(dt, power_mult, sp, btypes);
}

void ProductionQueues::tick_all(double dt, double power_mult,
                                const double factory_speedup[kQueueCats],
                                const std::vector<BuildingTypeDef>& btypes) {
    // Sequential single pool (deterministic Structure->Defense->Infantry->
    // Vehicle): carry the running balance across queues so total spend never
    // exceeds the money.
    double money = q_[0].credits;   // caller set all four equal via set_credits
    for (int i = 0; i < kQueueCats; ++i) {
        q_[i].credits = money;
        ra2engine::tick(q_[i], dt, power_mult * factory_speedup[i], btypes);
        money = q_[i].credits;      // this queue's spend reduces the shared pool
    }
    for (auto& q : q_) q.credits = money;   // final balance to all four
}

}  // namespace ra2engine

#pragma once
#include "diagnostics.hpp"
#include "world.hpp"
#include "weapon_unit.hpp"
#include <cmath>
namespace ra2engine::diag {
// Read-only checks: never repair the simulation or consume its random stream.
inline void probe_world(const World& w) {
    if (!state().enabled.load()) return;
    const auto& units = w.units();
    const auto& buildings = w.buildings();
    unsigned emitted = 0, invalid = 0, blocked = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
        const auto& u = units[i];
        if (!u.alive || u.die_time >= 0) continue;
        const bool finite = std::isfinite(u.fx) && std::isfinite(u.fy) && std::isfinite(u.wait_time);
        const bool path_ok = u.path_idx <= u.path.size();
        const bool stalled = u.moving() && u.wait_time >= 3.0;
        if (!finite || !path_ok) {
            ++invalid;
            if (invalid <= 8) note("CHECK unit=%zu finite=%d path_index=%zu path_size=%zu", i, finite, u.path_idx, u.path.size());
        }
        const int manager = u.w7.slave_manager;
        if ((manager >= 0 && static_cast<std::size_t>(manager) >= buildings.size()) ||
            (manager < -1 && static_cast<std::int64_t>(-2)-manager >= static_cast<std::int64_t>(units.size()))) {
            ++invalid;
            if (invalid <= 8) note("CHECK unit=%zu invalid_slave_manager=%d", i, manager);
        }
        if (stalled) ++blocked;
        const bool relevant = u.image == "SLAV" || u.image == "SMIN" || u.image == "YTNK" || u.image=="DISK" || u.image=="LTNK" || u.image=="BRUTE" || u.image=="CAOS" || u.image=="TELE";
        if ((relevant || stalled) && emitted++ < 24) {
            note("UNIT unit=%zu image=%s cell=%d,%d pos=%.3f,%.3f level=%d deck=%d moving=%d path=%zu/%zu wait_s=%.3f harv=%d ore_target=%d,%d cargo=%.1f,%.1f slave_manager=%d transport=%d target=%d building_target=%d",
                i, u.image.c_str(), u.cell_x, u.cell_y, u.fx, u.fy, u.level, u.on_deck, u.moving(),
                u.path_idx, u.path.size(), u.wait_time, static_cast<int>(u.harv), u.harv_cell_x, u.harv_cell_y,
                u.bales_ore, u.bales_gems, u.w7.slave_manager, u.cargo_of, u.target, u.target_building);
            note("COMBAT unit=%zu house=%d damage=%d range=%.3f slot=%d explicit=%d opportunity=%d frame=%d sequence=%d stage=%d",
                i,u.house,u.w_damage,u.w_range,u.w_active,u.attack_order,u.tg.opportunity,u.frame,u.ia.seq,u.ia.stage);
            note("ACQUIRE unit=%zu mission=%d area_guard=%d home=%d,%d guard_range=%.3f sight=%d scan_left=%d frozen=%d passive_disabled=%d approach=%d retaliate_disabled=%d",
                i,w.unit_mission(u),u.tg.area_guard,u.tg.home_x,u.tg.home_y,u.tg.t.guard_range,u.sight,
                u.tg.scan_timer,u.frozen,u.abil.no_passive_acquire || u.an.t.no_passive_acquire,
                u.w6.t.can_approach_target,u.abil.no_retaliate);
            // Report a candidate, not a diagnosis: distance alone does not
            // establish acquisition legality. Bound work and never draw RNG.
            int nearest=-1;double distance2=0;
            const std::size_t limit=std::min<std::size_t>(units.size(),512);
            for(std::size_t j=0;j<limit;++j) {
                const auto& v=units[j];
                if(j==i || !v.alive || v.die_time>=0 || v.cargo_of>=0 || !w.is_enemy(u.house,v.house))continue;
                const double dx=v.fx-u.fx,dy=v.fy-u.fy,d=dx*dx+dy*dy;
                if(nearest<0 || d<distance2){nearest=static_cast<int>(j);distance2=d;}
            }
            if(nearest>=0) {
                const auto& v=units[nearest];
                note("ACQUIRE_CANDIDATE unit=%zu candidate=%d image=%s distance=%.3f seen=%d hidden=%d disguise=%d legal_threat=%d weapon_slot=%d sampled=%zu total=%zu",
                    i,nearest,v.image.c_str(),std::sqrt(distance2),v.seen_by_house(u.house),
                    w.unit_hidden_from(u.house,v),w.disguise_fools(u.house,v),w.legal_threat_unit(u.house,v),
                    unit_weapon_for_unit(u,v),limit,units.size());
            }
            if(u.gattling.enabled()) {
                const bool elite=is_elite(u.vet.veterancy);
                note("GATTLING unit=%zu charge=%d stage=%d weapon=%s rof=%d",i,u.gattling.charge,
                    u.gattling.stage(elite),u.gattling.weapon(u.w_active,elite).id.c_str(),u.w_rof);
            }
            if (u.moving()) {
                const auto& p = u.path[u.path_idx];
                const auto* current = w.map().at(u.cell_x,u.cell_y);
                const auto* next = w.map().at(p.x,p.y);
                note("ROUTE unit=%zu next=%d,%d remaining=%zu ground_level=%d next_level=%d tile=%u subtile=%u next_tile=%u next_subtile=%u",
                    i, p.x, p.y, u.path.size()-u.path_idx, w.cell_level(u.cell_x,u.cell_y), w.cell_level(p.x,p.y),
                    current ? static_cast<unsigned>(current->tile) : 0, current ? static_cast<unsigned>(current->subtile) : 0,
                    next ? static_cast<unsigned>(next->tile) : 0, next ? static_cast<unsigned>(next->subtile) : 0);
            }
        }
    }
    unsigned managers = 0;
    for (std::size_t i = 0; i < buildings.size() && managers < 16; ++i) {
        const auto& b = buildings[i];
        if (!b.alive || b.type < 0 || static_cast<std::size_t>(b.type) >= w.btypes().size()) continue;
        const auto& type = w.btypes()[b.type];
        if (!type.armed && !type.tank_bunker && type.slave_count <= 0 && b.occupants.empty()) continue;
        ++managers;
        note("STRUCTURE building=%zu type=%d id=%s cell=%d,%d occupants=%zu slaves_initialized=%d regen_left=%d",
            i, b.type, type.id.c_str(), b.cx, b.cy, b.occupants.size(), b.w7.slaves_initialized, b.w7.slave_regen_left);
        if(type.armed) note("DEFENSE building=%zu id=%s target=%d offline=%d damage=%d range=%.3f ground=%d air=%d",
                i,type.id.c_str(),b.def_target,b.def_offline,type.weapon[0].damage,type.weapon[0].range,
                (type.weapon[0].valid && type.weapon[0].ag) || (type.weapon[1].valid && type.weapon[1].ag),
                (type.weapon[0].valid && type.weapon[0].aa) || (type.weapon[1].valid && type.weapon[1].aa));
        unsigned occupant_samples = 0;
        if(type.gattling.enabled()) note("GATTLING building=%zu charge=%d stage=%d weapon=%s",i,
            b.gattling_charge,type.gattling.stage(false,b.gattling_charge),
            type.gattling.weapon(b.def_slot,false,b.gattling_charge).id.c_str());
        for (int id : b.occupants) {
            if (occupant_samples++ >= 8) break;
            if (id < 0 || static_cast<std::size_t>(id) >= units.size()) { ++invalid; note("CHECK building=%zu invalid_occupant=%d", i, id); }
            else note("OCCUPANT building=%zu unit=%d image=%s alive=%d cargo_of=%d", i, id, units[id].image.c_str(), units[id].alive, units[id].cargo_of);
        }
    }
    note("CHECK_SUMMARY units=%zu buildings=%zu invalid=%u blocked_over_3s=%u unit_samples=%u", units.size(), buildings.size(), invalid, blocked, std::min(emitted,24u));
    unsigned berserk_samples=0;
    for(const auto& status:w.chaos_state().active) {
        if(berserk_samples++>=16)break;
        note("BERSERK unit=%d frames_left=%d active=%d",status.first,status.second.frames,w.unit_berserk(status.first));
    }
    for (int house=0;house<std::min(w.house_count(),16);++house)
        for (auto type:w.sw_types(house)) if (const auto* sw=w.sw_state(house,type))
            note("POWER house=%d type=%d ready=%d charge=%.3f",house,static_cast<int>(type),sw->ready,sw->charge_fraction());
}
// CPU stage duration and call rate, not GPU time or temperature. Local only.
struct StageStats {
    std::chrono::steady_clock::time_point window = std::chrono::steady_clock::now();
    unsigned calls = 0;
    double total = 0, maximum = 0;
};
class StageTimer {
    StageStats& stats;
    const char* name;
    std::chrono::steady_clock::time_point start;
public:
    StageTimer(StageStats& s, const char* n): stats(s), name(n), start(std::chrono::steady_clock::now()) {}
    ~StageTimer() {
        if (!state().enabled.load()) return;
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double,std::milli>(now-start).count();
        stats.total += ms; stats.maximum = std::max(stats.maximum,ms); ++stats.calls;
        const double seconds = std::chrono::duration<double>(now-stats.window).count();
        if (seconds >= 5.0) {
            note("PERFORMANCE stage=%s calls=%u window_s=%.3f rate_hz=%.2f cpu_avg_ms=%.3f cpu_max_ms=%.3f", name,stats.calls,seconds,stats.calls/seconds,stats.total/stats.calls,stats.maximum);
            stats = StageStats{};
        }
    }
};
}

// Wave-5 unit and building abilities (agent C) -- the World-side pass.
// [RE: docs/re/abilities-w5.md]. A World member TU (the world_abilities.cpp
// precedent) so the wave's churn stays out of world.cpp. Everything here is
// behind w5_ state that only a real-play roster (wire_w5) or a stolen-tech
// event fills; gate Worlds never arm it.
#include <algorithm>
#include <cmath>

#include "combat.hpp"   // deliver_hit_from, rearm_frames_for
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "group_move.hpp"   // nearest_free_cells
#include "veterancy.hpp"    // has_ability (SCATTER)
#include "weapon_unit.hpp"  // activate_weapon_slot (W6-04-15)
#include "world.hpp"

namespace ra2engine {

namespace {
bool live(const Unit& u) { return u.alive && u.die_time < 0.0 && u.cargo_of < 0; }
bool live_bld(const Building& b) { return b.alive && b.state != Building::State::Dead; }
}  // namespace

void World::grant_stolen_tech(int house, std::uint8_t bits) {
    if (house < 0 || bits == 0) return;
    if ((int)w5_.stolen_tech.size() <= house) w5_.stolen_tech.resize((std::size_t)house + 1, 0);
    w5_.stolen_tech[(std::size_t)house] |= bits;
}

// [RE: abilities-w5.md §1, HouseClass::HasAllStolenTech (YRpp naming; the
// flags are the house+0x2BC..0x2BE bytes FUN_004571E0 writes)]: every
// RequiresStolen*Tech bit of the type must be held.
void World::apply_stolen_tech_gate(std::vector<UnitTypeDef>& defs, int house) const {
    const std::uint8_t have = stolen_tech(house);
    for (UnitTypeDef& d : defs) {
        if (d.w5.requires_stolen == 0) continue;
        d.tech_level = (have & d.w5.requires_stolen) == d.w5.requires_stolen ? d.w5.base_tech_level : 0;
    }
}

void World::arm_w5(const RulesW5& rules, const std::map<std::string, UnitProfileW5>& profiles) {
    w5_.armed = true;
    w5_.rules = rules;
    w5_.profiles = profiles;
    for (Unit& u : units_) u.w5.bound = false;   // re-bind
}

// IsPowerOnline for a Powered building [RE: abilities-w5.md §3,
// FUN_004555C0]: offline at low power unless two or more Tesla troopers
// overpower it.
bool World::w5_power_online(int b_idx) const {
    if(building_disc_drained(b_idx))return false;
    const Building& b = buildings_[(std::size_t)b_idx];
    if (b.type < 0 || b.type >= (int)btypes().size()) return true;
    const BuildingTypeDef& t = btypes()[(std::size_t)b.type];
    if (!t.powered || t.power >= 0) return true;
    if (b.w5.overpowerers.size() >= 2) return true;
    return !house_low_power(b.house);
}

void World::step_w5(double dt) {
    // AI houses: their producible rosters follow their stolen tech (a spy the
    // AI sent into an enemy Battle Lab unlocks the stolen-tech infantry for
    // its team builder).
    for (int h = 0; h < (int)ai_houses_.size(); ++h) {
        HouseAiRec& rec = ai_houses_[(std::size_t)h];
        if (!rec.is_ai || !rec.cfg.team.enabled) continue;
        apply_stolen_tech_gate(rec.cfg.team.unit_roster, h);
    }
    if (!w5_.armed) return;
    const std::uint64_t F = w5_.frame;
    if (F % 450 == 0) w5_ai_pass();   // every 30 s
    const int nu = (int)units_.size();
    const int nb = (int)buildings_.size();
    const std::vector<BuildingTypeDef>& types = btypes();

    // (1) bind unit profiles by image.
    for (Unit& u : units_) {
        if (u.w5.bound) continue;
        u.w5.bound = true;
        const auto it = w5_.profiles.find(u.image);
        if (it != w5_.profiles.end()) u.w5.p = it->second;
    }

    // (2) Tech Hospital / Machine Shop [RE: abilities-w5.md §5,
    // TechnoClass::Update FUN_006F9380]: per house the sum of its buildings'
    // InfantryGainSelfHeal / UnitsGainSelfHeal (HouseClass +0x164/+0x168);
    // on Frame % SelfHealInfantryFrames == 0 every damaged own infantry gains
    // min(SelfHealInfantryAmount * count, missing); vehicles likewise on
    // SelfHealUnitFrames.
    {
        const RulesW5& R = w5_.rules;
        const bool inf_tick = R.self_heal_inf_frames > 0 && F % (std::uint64_t)R.self_heal_inf_frames == 0;
        const bool unit_tick = R.self_heal_unit_frames > 0 && F % (std::uint64_t)R.self_heal_unit_frames == 0;
        if (inf_tick || unit_tick) {
            std::vector<int> inf((std::size_t)house_count(), 0), veh((std::size_t)house_count(), 0);
            for (const Building& b : buildings_) {
                if (!live_bld(b) || b.house < 0 || b.house >= house_count()) continue;
                if (b.type < 0 || b.type >= (int)types.size()) continue;
                inf[(std::size_t)b.house] += types[(std::size_t)b.type].w5.inf_self_heal;
                veh[(std::size_t)b.house] += types[(std::size_t)b.type].w5.unit_self_heal;
            }
            for (Unit& u : units_) {
                if (!live(u) || u.hp <= 0 || u.hp >= u.max_hp) continue;
                if (u.house < 0 || u.house >= house_count()) continue;
                // W7-08-16 [RE: FUN_006F9380]: the Hospital branch takes
                // infantry OR an Organic UnitType (Dolphin, Giant Squid); the
                // Machine Shop the remaining units, ships included.
                const bool infantry = (!u.is_vehicle && !u.is_aircraft && !u.is_naval) ||
                                      (u.an.t.organic && !u.is_aircraft);
                const bool vehicle = u.is_vehicle && !u.is_aircraft && !infantry;
                int amt = 0;
                if (infantry && inf_tick) amt = R.self_heal_inf_amount * inf[(std::size_t)u.house];
                else if (vehicle && unit_tick) amt = R.self_heal_unit_amount * veh[(std::size_t)u.house];
                if (amt > 0) u.hp += std::min(amt, u.max_hp - u.hp);
            }
        }
    }

    // (3) Tesla Coil charging [RE: abilities-w5.md §3].
    //  a) An idle Tesla Trooper (Secondary warhead ElectricAssault) scans its 8
    //     neighbour cells for an own Overpowerable building and charges it
    //     (FootClass::Mission_Guard/AreaGuard); it keeps charging while it
    //     stays put and has no other target.
    for (int i = 0; i < nu; ++i) {
        Unit& u = units_[(std::size_t)i];
        if (!u.w5.p.assault) continue;
        if (!live(u) || u.moving() || u.target >= 0 || u.target_building >= 0 || u.attack_order) {
            u.w5.charging = -1;
            continue;
        }
        int c = u.w5.charging;
        if (c >= 0 && (c >= nb || !live_bld(buildings_[(std::size_t)c]) ||
                       !allied_with(u.house, buildings_[(std::size_t)c].house)))   // W7-02-02: allied coils too
            c = -1;
        if (c >= 0) {   // still adjacent?
            const Building& b = buildings_[(std::size_t)c];
            const int dx = std::max({b.cx - u.cell_x, u.cell_x - (b.cx + b.fw - 1), 0});
            const int dy = std::max({b.cy - u.cell_y, u.cell_y - (b.cy + b.fh - 1), 0});
            if (std::max(dx, dy) > 1) c = -1;
        }
        if (c < 0) {
            static const int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
            static const int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
            for (int k = 0; k < 8 && c < 0; ++k) {
                const int bi = building_at_cell(u.cell_x + kDx[k], u.cell_y + kDy[k]);
                if (bi < 0) continue;
                const Building& b = buildings_[(std::size_t)bi];
                if (!allied_with(u.house, b.house) || b.type < 0 || b.type >= (int)types.size()) continue;   // W7-02-02
                if (!types[(std::size_t)b.type].w5.overpowerable) continue;
                c = bi;
            }
        }
        u.w5.charging = c;
        if (c < 0) continue;
        // AssaultBolt every ROF frames: BuildingClass::Overpower (FUN_00452810)
        // moves the trooper to the end of the set; the warhead does no damage.
        if ((std::int64_t)F >= u.w5.next_assault) {
            u.w5.next_assault = (std::int64_t)F + std::max(1, u.w5.p.assault_rof);
            std::vector<int>& ov = buildings_[(std::size_t)c].w5.overpowerers;
            ov.erase(std::remove(ov.begin(), ov.end(), i), ov.end());
            ov.push_back(i);
            u.fire_flash = 0.4;
            fire_events_.push_back({i, -1, -1, c, 1, "AssaultBolt"});
        }
    }
    //  b) BuildingClass::AI upkeep: a trooper that stopped charging this coil
    //     leaves the set; IsOverpowered = n >= 3, or n >= 1 with the house at
    //     full power.
    for (int bi = 0; bi < nb; ++bi) {
        Building& b = buildings_[(std::size_t)bi];
        if (b.w5.overpowerers.empty() && !b.w5.overpowered) continue;
        std::vector<int>& ov = b.w5.overpowerers;
        ov.erase(std::remove_if(ov.begin(), ov.end(),
                                [&](int t) {
                                    return t < 0 || t >= nu || !live(units_[(std::size_t)t]) ||
                                           units_[(std::size_t)t].w5.charging != bi;
                                }),
                 ov.end());
        const std::size_t n = live_bld(b) ? ov.size() : 0;
        b.w5.overpowered = n >= 3 || (n >= 1 && !house_low_power(b.house));
    }

    // (3b) SCATTER (veteran ability; Allied/Soviet infantry and light vehicles
    // gain it at rank 1 in this rulesmd) [RE-FALLBACK: the original scatters
    // the unit from incoming fire through its threat response; here a ranked
    // unit standing idle that loses HP steps aside (order_scatter)].
    // w7fix-combat: with the W7 rules armed the damage scatter is World::
    // step_missions_w7 (the original's ReceiveDamage rule, W7-07-24).
    if (vet_any_ && !w7_rules_.armed)
        for (int i = 0; i < nu; ++i) {
            Unit& u = units_[(std::size_t)i];
            const int prev = u.w5.last_hp;
            u.w5.last_hp = u.hp;
            if (!live(u) || prev < 0 || u.hp >= prev || u.moving() || u.attack_order || u.target >= 0 ||
                u.target_building >= 0)
                continue;
            if (!has_ability(u.vet.veterancy, u.vet.vet_abilities, u.vet.elite_abilities, Ability::Scatter)) continue;
            order_scatter(i);
            ++w5_.scatters;
        }
    w5_step_airstrike();   // (R-046) Boris calls MiG airstrikes
    // (R-049) Giant Squid grapple [RE: abilities-w5.md §11 -- warhead
    // ParasitePlus Paralyzes=32767 "reset to last as long as the Squid
    // grapples", Culling=yes "kills instead of damages if victim in Red"]:
    // a host ridden by a paralysing parasite cannot move; a culling parasite
    // kills a host at or below ConditionRed (25%).
    for (int i = 0; i < nu; ++i) {
        Unit& h = units_[(std::size_t)i];
        if (h.parasite_on_me < 0 || h.parasite_on_me >= nu || !h.alive || h.die_time >= 0.0) continue;
        const Unit& d = units_[(std::size_t)h.parasite_on_me];
        if (d.w5.p.paralyzes && h.moving()) {
            occ_.release_all_of(i, h.cell_x, h.cell_y);
            occ_.set(h.cell_x, h.cell_y, i);
            h.path.clear();
            h.path_idx = 0;
            h.fx = h.cell_x;
            h.fy = h.cell_y;
        }
        if (d.w5.p.culling && h.hp > 0 && h.hp * 4 <= h.max_hp) {
            ++w5_.culls;
            ability_kill_unit(i);
        }
    }
    w5_step_defuse();   // (R-046) engineers disarming Ivan bombs
    w5_step_repair_ifv(dt);   // (R-048) the Engineer IFV repairs friendly vehicles

    // (4) OpenTopped transports: passengers fire out [RE: abilities-w5.md §6].
    {
        const RulesW5& R = w5_.rules;
        for (int ti = 0; ti < nu; ++ti) {
            Unit& t = units_[(std::size_t)ti];
            if (!t.w5.p.open_topped || !live(t)) continue;
            // W7-08-23 [RE: TechnoClass::GetWeaponRange 0x700760]: an OpenTopped
            // transport's range is min(own weapon range, every passenger's
            // weapon Range) -- without the +2 bonus -- so a Battle Fortress
            // carrying GIs closes to their 4 cells.
            if (t.w_active == 0 && t.vet.base.range > 0.0) {
                const bool el = t.vet.recv && is_elite(t.vet.veterancy) && t.vet.has_elite;
                double r = el ? t.vet.elite.range : t.vet.base.range;
                for (int pi : t.cargo)
                    if (pi >= 0 && pi < nu && units_[(std::size_t)pi].w_range > 0.0 &&
                        units_[(std::size_t)pi].w_damage > 0)
                        r = std::min(r, units_[(std::size_t)pi].w_range);
                t.w_range = r;
            }
            if (t.cargo.empty()) continue;
            for (int pi : t.cargo) {
                if (pi < 0 || pi >= nu) continue;
                Unit& p = units_[(std::size_t)pi];
                if (!p.alive || p.die_time >= 0.0) continue;
                if (p.rearm_frames > 0) --p.rearm_frames;   // Net L0: whole frames
                // W6-04-15 [rules OpenTransportWeapon=, abilities-w5.md §6]: a
                // passenger fires that slot out of the transport (a GI's Para,
                // not its M60); -1 keeps the current weapon.
                if (p.w6.t.open_transport_weapon == 0 || (p.w6.t.open_transport_weapon == 1 && p.w2.valid))
                    activate_weapon_slot(p, p.w6.t.open_transport_weapon,
                                         veterancy_enabled() ? &veterancy_rules() : nullptr);
                if (p.w_damage <= 0 || p.w_range <= 0.0) continue;
                const double range = p.w_range + R.open_topped_range_bonus;
                auto ok = [&](int j) {
                    if (j < 0 || j >= nu) return false;
                    const Unit& v = units_[(std::size_t)j];
                    if (!live(v) || !is_enemy(p.house, v.house) || v.an.submerged) return false;
                    if (!p.detect_disguise && disguise_fools(p.house, v)) return false;   // W7-01-02
                    if (rocket_on_launcher(v)) return false;
                    if (!v.seen_by_house(p.house)) return false;   // fv7c: 32-bit house mask
                    const bool air = v.is_aircraft && v.altitude > 0.0;
                    if (air ? !p.w_aa : !p.w_ag) return false;
                    return om::hypot(v.fx - t.fx, v.fy - t.fy) <= range;
                };
                int tgt = ok(t.target) ? t.target : (ok(p.target) ? p.target : -1);
                if (tgt < 0) {
                    double best = 0.0;
                    for (int j = 0; j < nu; ++j) {
                        if (!ok(j)) continue;
                        const double d = om::hypot(units_[(std::size_t)j].fx - t.fx, units_[(std::size_t)j].fy - t.fy);
                        if (tgt < 0 || d < best) { tgt = j; best = d; }
                    }
                }
                p.target = tgt;
                // W7-08-24 [RE: 0x6F9840-0x6F9960 validates a passenger target
                // that may be a structure]: with no unit to shoot, a passenger
                // fires at the transport's structure target in range + bonus.
                int tbld = -1;
                if (tgt < 0 && t.target_building >= 0 && t.target_building < static_cast<int>(buildings_.size())) {
                    const Building& o = buildings_[static_cast<std::size_t>(t.target_building)];
                    if (o.alive && o.state != Building::State::Dead && is_enemy(p.house, o.house) && p.w_ag &&
                        verses_allows(p.w_verses, o.armor_ix)) {
                        double bestd = -1.0;
                        for (const Cellxy& c : foundation_cells(o.cx, o.cy, o.fw, o.fh)) {
                            const double dd = om::hypot(c.x - t.fx, c.y - t.fy);
                            if (bestd < 0.0 || dd < bestd) bestd = dd;
                        }
                        if (bestd >= 0.0 && bestd <= range) tbld = t.target_building;
                    }
                }
                if ((tgt < 0 && tbld < 0) || p.rearm_frames > 0.0) continue;
                if (tbld >= 0) {
                    Unit shooter = p;
                    shooter.fx = t.fx;
                    shooter.fy = t.fy;
                    shooter.cell_x = t.cell_x;
                    shooter.cell_y = t.cell_y;
                    shooter.w_damage = om::ftol(p.w_damage * R.open_topped_damage_mult);
                    p.rearm_frames = unit_rearm_frames(p, &combat_rng_);
                    fire_events_.push_back({ti, -1, -1, tbld, 0, "OpenTopped"});
                    const Building& o = buildings_[static_cast<std::size_t>(tbld)];
                    int ax = 0, ay = 0, az = 0;
                    building_aim_lept(o, cell_level(o.cx, o.cy), ax, ay, az);
                    damage_area(*this, units_, buildings_, occ_, shooter, pi, -1, ax, ay, az);
                    ++w5_.passenger_shots;
                    continue;
                }
                Unit shooter = p;
                shooter.fx = t.fx;
                shooter.fy = t.fy;
                shooter.cell_x = t.cell_x;
                shooter.cell_y = t.cell_y;
                shooter.w_damage = om::ftol(p.w_damage * R.open_topped_damage_mult);
                p.rearm_frames = unit_rearm_frames(p, &combat_rng_);   // W6-04-01 Burst
                fire_events_.push_back({ti, -1, tgt, -1, 0, "OpenTopped"});
                deliver_hit_from(units_, occ_, shooter, pi, -1, tgt);
                ++w5_.passenger_shots;
            }
        }
    }
    ++w5_.frame;
}

// ---------------------------------------------------------------------------
// Gap Generator [RE: abilities-w5.md §4, TechnoClass::CreateGap FUN_006FA690,
// the 120-frame re-shroud pass FUN_00577B30]. Called from step_visibility
// between the per-house sight stamp and the SpySat reveal (`phase` 0) and
// after it (`phase` 1). The original keeps ONE shroud, the local player's;
// here every human (non-AI) house that is neither the gap's owner nor its
// ally is shrouded.
// ---------------------------------------------------------------------------
void World::w5_gap_pass(int phase) {
    if (!w5_.armed || grid_w_ <= 0 || grid_h_ <= 0) return;
    const std::vector<BuildingTypeDef>& types = btypes();
    const std::size_t cells = (std::size_t)grid_w_ * (std::size_t)grid_h_;
    const int nh = house_count();
    if (phase == 0) {
        // Which houses each covered cell hides from: a bit per house.
        w5_hide_.assign(cells, 0);
        bool any = false;
        for (int bi = 0; bi < (int)buildings_.size(); ++bi) {
            Building& b = buildings_[(std::size_t)bi];
            const int R = (b.type >= 0 && b.type < (int)types.size()) ? types[(std::size_t)b.type].w5.gap_radius : 0;
            const bool on = R > 0 && live_bld(b) && b.state == Building::State::Idle && w5_power_online(bi);
            const bool created = on && !b.w5.gap_on;
            b.w5.gap_on = on;
            if (!on) continue;
            std::uint32_t mask = 0;
            for (int h = 0; h < nh && h < 32; ++h)
                if (h != b.house && house_combatant(h) && !ai_enabled(h) && !are_allied(h, b.house))
                    mask |= (1u << h);
            if (!mask) continue;
            any = true;
            const int cx = b.cx + b.fw / 2, cy = b.cy + b.fh / 2;
            for (int dy = -R - 2; dy < R + 2; ++dy)
                for (int dx = -R - 2; dx < R + 2; ++dx) {
                    if (dx * dx + dy * dy >= (R + 1) * (R + 1)) continue;
                    const int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) continue;
                    w5_hide_[(std::size_t)y * grid_w_ + x] |= mask;
                }
            // CreateGap: the covered cells go black at once (not where the
            // victim's own units currently look).
            if (created) w5_.gap_created = true;
        }
        if (!any) { w5_hide_.clear(); w5_.gap_created = false; return; }
        const bool tick = w5_.gap_created || (w5_.frame % 120 == 0);
        w5_.gap_created = false;
        for (int h = 0; h < nh && h < 32; ++h) {
            if (visible_[(std::size_t)h].empty()) continue;
            const bool spysat = map_reveal_[(std::size_t)h] != 0;
            if (!tick && !spysat) continue;
            for (std::size_t c = 0; c < cells; ++c)
                if ((w5_hide_[c] & (1u << h)) && !visible_[(std::size_t)h][c]) explored_[(std::size_t)h][c] = 0;
        }
        // Remember which hidden cells the house's own sight covers (phase 1).
        w5_own_.assign(cells, 0);
        for (std::size_t c = 0; c < cells; ++c) {
            if (!w5_hide_[c]) continue;
            for (int h = 0; h < nh && h < 32; ++h)
                if (!visible_[(std::size_t)h].empty() && visible_[(std::size_t)h][c]) w5_own_[c] |= (1u << h);
        }
        return;
    }
    // phase 1: a SpySat reveal does not lift an enemy gap (the gap raised the
    // cell's ShroudCounter; only own sight shows it).
    if (w5_hide_.empty()) return;
    for (int h = 0; h < nh && h < 32; ++h) {
        if (!map_reveal_[(std::size_t)h] || visible_[(std::size_t)h].empty()) continue;
        for (std::size_t c = 0; c < cells; ++c)
            if ((w5_hide_[c] & (1u << h)) && !(w5_own_[c] & (1u << h))) {
                explored_[(std::size_t)h][c] = 0;
                visible_[(std::size_t)h][c] = 0;
            }
    }
}

bool World::order_defuse(int eng, int unit_target, int b_target) {
    if (eng < 0 || eng >= (int)units_.size()) return false;
    Unit& e = units_[(std::size_t)eng];
    if (!live(e)) return false;
    if (unit_target >= 0 && (unit_target >= (int)units_.size() || units_[(std::size_t)unit_target].bomb_frame < 0))
        return false;
    if (b_target >= 0 && (b_target >= (int)buildings_.size() || buildings_[(std::size_t)b_target].bomb_frame < 0))
        return false;
    if (unit_target < 0 && b_target < 0) return false;
    e.w5.defuse_unit = unit_target;
    e.w5.defuse_bld = b_target;
    e.target = -1;
    e.target_building = -1;
    e.attack_order = false;
    e.capture_target = -1;
    return true;
}

// Engineer bomb defusal [RE: abilities-w5.md §8, BulletClass::Detonate
// BombDisarm branch (WarheadType +0x16E) -> FUN_00438930 on a target whose
// AttachedBomb (+0x38) is set]: the DefuseKit (Range 1.5, FireOnce) removes
// the Ivan bomb; the engineer is not consumed.
void World::w5_step_defuse() {
    const int nu = (int)units_.size();
    for (int i = 0; i < nu; ++i) {
        Unit& e = units_[(std::size_t)i];
        if (e.w5.defuse_unit < 0 && e.w5.defuse_bld < 0) continue;
        if (!live(e)) { e.w5.defuse_unit = e.w5.defuse_bld = -1; continue; }
        double tx, ty, reach = 1.5;
        if (e.w5.defuse_unit >= 0) {
            const Unit& v = units_[(std::size_t)e.w5.defuse_unit];
            if (!v.alive || v.die_time >= 0.0 || v.bomb_frame < 0) { e.w5.defuse_unit = -1; continue; }
            tx = v.fx; ty = v.fy;
        } else {
            const Building& b = buildings_[(std::size_t)e.w5.defuse_bld];
            if (!live_bld(b) || b.bomb_frame < 0) { e.w5.defuse_bld = -1; continue; }
            // nearest foundation cell
            tx = std::max<double>(b.cx, std::min<double>(e.fx, b.cx + b.fw - 1));
            ty = std::max<double>(b.cy, std::min<double>(e.fy, b.cy + b.fh - 1));
        }
        if (om::hypot(e.fx - tx, e.fy - ty) <= reach + 1e-9) {
            if (e.w5.defuse_unit >= 0) units_[(std::size_t)e.w5.defuse_unit].bomb_frame = -1;
            else buildings_[(std::size_t)e.w5.defuse_bld].bomb_frame = -1;
            fire_events_.push_back({i, -1, e.w5.defuse_unit, e.w5.defuse_bld, 0, "DefuseKit"});
            ++w5_.defusals;
            e.w5.defuse_unit = e.w5.defuse_bld = -1;
            continue;
        }
        if (!e.moving()) {
            const std::vector<Cellxy> g = nearest_free_cells({(int)om::lround(tx), (int)om::lround(ty)}, 1,
                                                             passable_, grid_w_, grid_h_, occ_);
            if (!g.empty()) order_unit_move(i, g[0].x, g[0].y);
        }
    }
}

// IFV + Engineer (RepairBullet Damage=-50, Range 1.8, Mechanical warhead:
// vehicles 100%, infantry 0%) [RE-FALLBACK: the original routes a negative
// weapon through the friendly-target acquisition (TechnoClass target scan with
// the "repair" threat type); here the nearest damaged allied vehicle within
// Sight is chosen, approached and healed every ROF].
void World::w5_step_repair_ifv(double dt) {
    const int nu = (int)units_.size();
    for (int i = 0; i < nu; ++i) {
        Unit& u = units_[(std::size_t)i];
        if (!u.w5.healer) continue;
        if (!live(u)) { u.w5.heal_target = -1; continue; }
        if (u.target >= 0 || u.target_building >= 0) {   // never "attack" with a repair gun
            u.target = -1;
            u.target_building = -1;
            u.attack_order = false;
        }
        auto ok = [&](int j) {
            if (j < 0 || j >= nu || j == i) return false;
            const Unit& v = units_[(std::size_t)j];
            return live(v) && v.is_vehicle && !v.is_aircraft && are_allied(u.house, v.house) && v.hp < v.max_hp &&
                   damage_vs(-u.w_damage, u.w_verses, v.armor_ix) > 0;
        };
        int t = ok(u.w5.heal_target) ? u.w5.heal_target : -1;
        if (t < 0 && !u.moving()) {
            double best = 0.0;
            const double reach = std::max(u.w_range, (double)u.sight);
            for (int j = 0; j < nu; ++j) {
                if (!ok(j)) continue;
                const double d = om::hypot(units_[(std::size_t)j].fx - u.fx, units_[(std::size_t)j].fy - u.fy);
                if (d > reach || (t >= 0 && d >= best)) continue;
                t = j;
                best = d;
            }
        }
        u.w5.heal_target = t;
        if (u.rearm_frames > 0) --u.rearm_frames;   // Net L0: whole frames
        if (t < 0) continue;
        Unit& v = units_[(std::size_t)t];
        if (om::hypot(v.fx - u.fx, v.fy - u.fy) <= u.w_range + 1e-9) {
            if (u.rearm_frames > 0.0) continue;
            const int heal = damage_vs(-u.w_damage, u.w_verses, v.armor_ix);
            v.hp += std::min(heal, v.max_hp - v.hp);
            if (v.parasite_on_me >= 0) v.w6.heal_hit = true;   // W6-04-09: a healing hit ejects
            u.rearm_frames = unit_rearm_frames(u, &combat_rng_);   // W6-04-01 Burst
            u.fire_flash = 0.4;
            fire_events_.push_back({i, -1, t, -1, 0, "RepairBullet"});
            ++w5_.repairs;
        } else if (!u.moving()) {
            const std::vector<Cellxy> g = nearest_free_cells({v.cell_x, v.cell_y}, 1, passable_, grid_w_, grid_h_, occ_);
            if (!g.empty()) order_unit_move(i, g[0].x, g[0].y);
        }
    }
}

// AI use of the wave-5 world (R-081 garrisons, tech-building capture)
// [RE-FALLBACK: the original skirmish AI's garrison / tech-capture choice was
// not traced (the stock aimd scripts never name a civilian building); here an
// AI house, every 30 s, (a) sends idle Occupier infantry that no team owns
// into the nearest empty neutral garrison within 20 cells of its base centre
// and (b) sends an idle, team-less engineer to the nearest neutral
// Capturable tech building within 30 cells.]
void World::w5_ai_pass() {
    const std::vector<BuildingTypeDef>& types = btypes();
    for (int h = 0; h < (int)ai_houses_.size(); ++h) {
        const HouseAiRec& rec = ai_houses_[(std::size_t)h];
        if (!rec.is_ai || !rec.cfg.team.enabled) continue;
        const BaseView view = make_ai_base_view(h, rec);
        if (view.own.empty()) continue;
        const std::vector<IdleUnitView> idle = ai_idle_units(h, rec);
        auto dist_base = [&](const Building& b) {
            return om::hypot(b.cx + b.fw / 2.0 - view.center_x, b.cy + b.fh / 2.0 - view.center_y);
        };
        // (a) garrison -- W6-03-09: OFF. No original source for a computer
        // house garrisoning civilian buildings was found (the stock aimd
        // scripts never name one; RE-W6-b open), and the invented rule locked
        // the AI's starting infantry -- the first teams' free recruit pool --
        // inside civilian buildings for the whole game. Kept behind this
        // switch until the original trigger is found.
        constexpr bool kAiGarrisonCivilian = false;
        int gb = -1;
        double gbd = 0.0;
        for (int bi = 0; kAiGarrisonCivilian && bi < (int)buildings_.size(); ++bi) {
            const Building& b = buildings_[(std::size_t)bi];
            if (!live_bld(b) || house_combatant(b.house) || !b.occupants.empty()) continue;
            if (b.type < 0 || b.type >= (int)types.size() || !types[(std::size_t)b.type].can_be_occupied) continue;
            const double d = dist_base(b);
            if (d > 20.0 || (gb >= 0 && d >= gbd)) continue;
            gb = bi;
            gbd = d;
        }
        if (gb >= 0) {
            int sent = 0;
            const int cap = types[(std::size_t)buildings_[(std::size_t)gb].type].max_occupants;
            for (const IdleUnitView& iv : idle) {
                if (sent >= cap) break;
                const Unit& u = units_[(std::size_t)iv.unit];
                if (!u.occupier || u.occupy_target >= 0) continue;
                if (!can_occupy(iv.unit, gb)) continue;
                order_occupy(iv.unit, gb);
                ++sent;
            }
            if (sent > 0) ++w5_.ai_garrisons;
        }
        // (b) tech-building capture
        int tb = -1;
        double tbd = 0.0;
        for (int bi = 0; bi < (int)buildings_.size(); ++bi) {
            const Building& b = buildings_[(std::size_t)bi];
            if (!live_bld(b) || house_combatant(b.house) || !building_capturable(bi)) continue;
            if (b.type < 0 || b.type >= (int)types.size() || types[(std::size_t)b.type].bridge_repair_hut) continue;
            bool claimed = false;
            for (const Unit& u : units_) claimed |= u.alive && u.capture_target == bi;
            if (claimed) continue;
            const double d = dist_base(b);
            if (d > 30.0 || (tb >= 0 && d >= tbd)) continue;
            tb = bi;
            tbd = d;
        }
        if (tb >= 0)
            for (const IdleUnitView& iv : idle) {
                const Unit& u = units_[(std::size_t)iv.unit];
                if (!unit_is_engineer(u) || u.capture_target >= 0) continue;   // W7-01-33
                order_engineer_capture(iv.unit, tb);
                ++w5_.ai_tech_captures;
                break;
            }
    }
}

// Boris airstrike (R-046) [RE: abilities-w5.md §10 -- AirstrikeClass
// (ctor FUN_0041D3B0 copies TechnoType +0x61C AirstrikeTeam, +0x620
// EliteAirstrikeTeam, +0x62C/+0x630 the team types, +0x624/+0x628 the
// recharge frames; FUN_0041D800 = "may strike": not busy and
// Frame >= last + recharge (elite recharge for an elite designator))].
// [RE-FALLBACK: the planes' entry point (here the map edge nearest the target
// along x, like the paradrop plane) and their exit after the drop.]
void World::w5_step_airstrike() {
    const int nu = (int)units_.size();
    const std::int64_t F = (std::int64_t)w5_.frame;
    for (int i = 0; i < nu; ++i) {
        Unit& b = units_[(std::size_t)i];
        if (b.w5.p.airstrike_team <= 0 || !live(b)) continue;
        const int tb = b.target_building;
        if (tb < 0 || tb >= (int)buildings_.size() || !live_bld(buildings_[(std::size_t)tb])) continue;
        if (!is_enemy(b.house, buildings_[(std::size_t)tb].house)) continue;
        if (F < b.w5.airstrike_ready) continue;
        bool busy = false;
        for (const Unit& p : units_) busy |= p.alive && p.w5.strike_of == i;
        if (busy) continue;
        const Building& tgt = buildings_[(std::size_t)tb];
        const double nx = std::max<double>(tgt.cx, std::min<double>(b.fx, tgt.cx + tgt.fw - 1));
        const double ny = std::max<double>(tgt.cy, std::min<double>(b.fy, tgt.cy + tgt.fh - 1));
        if (om::hypot(nx - b.fx, ny - b.fy) > b.w5.p.flare_range) continue;
        const bool elite = is_elite(b.vet.veterancy);
        const int n = elite ? b.w5.p.elite_airstrike_team : b.w5.p.airstrike_team;
        const int tcy = tgt.cy + tgt.fh / 2;
        const bool from_left = tgt.cx + tgt.fw / 2 < grid_w_ / 2;
        int spawned = 0;
        for (int k = 0; k < n; ++k) {
            const int pi = an_spawn(b.w5.p.airstrike_type, b.house, from_left ? 0 : grid_w_ - 1,
                                    std::max(0, std::min(grid_h_ - 1, tcy - n / 2 + k)));
            if (pi < 0) break;
            Unit& p = units_[(std::size_t)pi];
            p.fx = from_left ? 0.0 : grid_w_ - 1.0;
            p.altitude = an_.sw.flight_level;
            p.flight_state = FlightState::Cruising;
            p.an.owner = -1;
            p.an.attack_building = tb;
            p.an.attack_unit = -1;
            p.an.ammo = p.an.t.ammo > 0 ? p.an.t.ammo : 1;
            p.an.sortie = 1;   // kOutbound
            p.w5.strike_of = i;
            p.w5.strike_bld = tb;
            ++spawned;
        }
        if (spawned > 0) {
            b.w5.airstrike_ready = F + (elite ? b.w5.p.elite_airstrike_recharge : b.w5.p.airstrike_recharge);
            ++w5_.airstrikes;
            fire_events_.push_back({i, -1, -1, tb, 1, "Flare"});
        }
    }
    // Spent planes leave across the map (the paradrop plane's exit run).
    for (int i = 0; i < nu; ++i) {
        Unit& p = units_[(std::size_t)i];
        if (p.w5.strike_of < 0 || !p.alive || p.die_time >= 0.0) continue;
        if (p.an.sortie != 3) continue;   // kReturn: spent or target gone
        p.an.sortie = 5;                  // kParadrop with nothing aboard = fly out
        p.an.drop_left = 0;
        p.an.exit_x = p.fx < grid_w_ / 2.0 ? grid_w_ - 1 : 0;
        p.an.exit_y = p.cell_y;
    }
}

int World::w5_gap_hidden(int h) const {
    if (h < 0 || h >= 32 || w5_hide_.empty() || h >= (int)explored_.size() || explored_[(std::size_t)h].empty())
        return 0;
    int n = 0;
    for (std::size_t c = 0; c < w5_hide_.size(); ++c)
        if ((w5_hide_[c] & (1u << h)) && !explored_[(std::size_t)h][c]) ++n;
    return n;
}

}  // namespace ra2engine

// P4 Unit special abilities (batch 1) -- the World-side pass + orders
// [RE: docs/re/abilities.md]. A World member TU split out of world.cpp so the
// phase's churn stays out of the shared file. Every entry point is behind the
// abilities_any_ firewall (opened ONLY by register_ability_profile, which no
// pre-P4 gate calls).
#include <algorithm>
#include <cmath>

#include "building.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "combat.hpp"      // damage_vs
#include "group_move.hpp"  // nearest_free_cells
#include "weapon_unit.hpp" // secondary_view (W7-10-06)
#include "world.hpp"

namespace ra2engine {

namespace {

using Ev = World::AbilityEvent;

bool on_map_alive(const Unit& u) { return u.alive && u.die_time < 0.0 && u.cargo_of < 0; }

// The parasite's instant-kill branch keys off WhatAmI == Infantry (§B2).
bool is_infantry_unit(const Unit& u) { return !u.is_vehicle && !u.is_aircraft && !u.is_naval; }

void bind_profile(Unit& u, const std::map<std::string, AbilityProfile>& profiles) {
    if (u.abil_bound) return;
    u.abil_bound = true;
    const auto it = profiles.find(u.image);
    if (it != profiles.end()) u.abil = it->second;
}

// Building "coord" = the center of its foundation (cell space).
double bld_cx(const Building& b) { return b.cx + (b.fw - 1) / 2.0; }
double bld_cy(const Building& b) { return b.cy + (b.fh - 1) / 2.0; }

}  // namespace

void World::register_ability_profile(const std::string& image, const AbilityProfile& p) {
    ability_profiles_[image] = p;
    abilities_any_ = true;
    for (Unit& u : units_)
        if (u.image == image) {
            u.abil_bound = false;                     // re-bind now
            bind_profile(u, ability_profiles_);
        }
}

std::vector<World::AbilityEvent> World::take_ability_events() {
    std::vector<AbilityEvent> out;
    out.swap(ability_events_);
    return out;
}

bool World::order_deploy(int idx) {
    // gF: a DeploysInto unit (MCV) deploys into its building -- every deploy
    // gesture (D key, deploy cursor, touch button) reaches it through here.
    if (is_mcv(idx)) return order_deploy_mcv(idx);
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return false;
    Unit& u = units_[idx];
    // Gap fix C (G-45) [RE: docs/re/weapons.md §3]: a DeployFire infantry (GI
    // Para, GGI MissileLauncher -- deploy weapon without RadLevel) deploys into
    // an immobile gun position that keeps acquiring and firing its
    // DeployFireWeapon slot through the normal step_combat path. NOT frozen:
    // only movement is refused (order_unit_move). Independent of the P4
    // abilities firewall.
    if (u.deploy_fire && u.alive && u.die_time < 0.0 && u.cargo_of < 0) {
        u.deployed = !u.deployed;
        if (u.deployed) {
            if (u.moving()) {
                occ_.release_all_of(idx, u.cell_x, u.cell_y);
                occ_.set(u.cell_x, u.cell_y, idx);
            }
            u.path.clear();
            u.path_idx = 0;
            u.fx = u.cell_x;
            u.fy = u.cell_y;
            u.attack_order = false;   // a deployed GI holds its post
            // W7-10-06 [RE: InfantryClass::Mission_Unload 0x51F3F0]: an
            // AreaFire deploy weapon (the Yuri's PsiWave; any type but DESO)
            // does SetTarget(own cell) -- one blast around the unit at once --
            // and the unit stands up again UndeployDelay frames later (Guard
            // with that delay, then the undeploy). It holds no post.
            if (u.w7.t.deploy_area_fire) {
                const WeaponSlot none;
                const WeaponSlot& w = (u.deploy_fire_slot == 1 && u.w2.valid) ? secondary_view(u) : none;
                if (w.valid) {
                    WeaponSpec ws;
                    ws.valid = true;
                    ws.damage = w.damage;
                    ws.rof = w.rof;
                    ws.range = w.range;
                    ws.verses = w.verses;
                    ws.cell_spread = w.cell_spread;
                    ws.percent_at_max = w.percent_at_max;
                    ws.affects_allies = w.affects_allies;
                    ws.prone_damage = w.prone_damage;
                    ws.inf_death = w.inf_death;
                    ws.wall_flags = w.wall_flags;
                    ability_area_damage(idx, u.fx, u.fy, ws, w.damage, unit_abs_z(u));
                    u.fire_flash = 0.4;
                }
                u.w7.undeploy_at = u.w7.t.undeploy_delay >= 0
                                       ? static_cast<std::int64_t>(ability_frame_) + u.w7.t.undeploy_delay
                                       : -1;
            }
        } else {
            u.w7.undeploy_at = -1;
        }
        ability_events_.push_back({Ev::Kind::Deploy, idx, -1, u.deployed ? 1 : 0, 0, false});
        return true;
    }
    if (!abilities_any_) return false;
    if (!on_map_alive(u)) return false;
    bind_profile(u, ability_profiles_);
    if (!u.abil.deployer || !u.abil.deploy.valid) return false;
    u.deployed = !u.deployed;
    if (u.deployed) {
        // [RE: abilities.md §A] deployed = immobile; the eruption fires on the
        // next pass and then every deploy-weapon ROF.
        occ_.release_all_of(idx, u.cell_x, u.cell_y);
        u.path.clear();
        u.path_idx = 0;
        u.fx = u.cell_x;
        u.fy = u.cell_y;
        u.target = -1;
        u.target_building = -1;
        u.attack_order = false;
        u.deploy_next_fire = static_cast<std::int64_t>(ability_frame_);
        u.frozen = true;
    } else {
        u.frozen = (static_cast<std::int64_t>(ability_frame_) < u.warp_until) || u.warp_remaining > 0;
    }
    ability_events_.push_back({Ev::Kind::Deploy, idx, -1, u.deployed ? 1 : 0, 0, false});
    return true;
}

// W7-10-06: the deployed AreaFire unit stands up after UndeployDelay (runs
// every frame above the abilities firewall; ability_frame_ always advances).
void World::w7_step_deploy_timers() {
    const std::int64_t F = static_cast<std::int64_t>(ability_frame_);
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[static_cast<std::size_t>(i)];
        // W7-01-32 / W7-02-08 / W7-04-17 [RE: Enter_Idle_Mode 0x51CB2A /
        // 0x737BB6]: a DefaultToGuardArea unit that falls idle (no move, no
        // target) enters Area_Guard on its spot. [RE-FALLBACK: the team
        // membership exception (+0x5D4) is not modelled here.]
        if (u.w7.t.default_to_guard_area && !u.tg.area_guard && !u.is_aircraft && u.alive && u.die_time < 0.0 &&
            u.cargo_of < 0 && !u.moving() && u.target < 0 && u.target_building < 0 && u.attack_cell_x < 0) {
            u.tg.area_guard = true;
            u.guard_x = u.cell_x;
            u.guard_y = u.cell_y;
        }
        if (u.w7.undeploy_at < 0 || F < u.w7.undeploy_at) continue;
        u.w7.undeploy_at = -1;
        if (u.deployed && u.alive && u.die_time < 0.0) (void)order_deploy(i);
    }
}

void World::teleport_unit(int idx, int gx, int gy) {
    Unit& u = units_[idx];
    if (!on_map_alive(u)) return;
    // Destination: the ordered cell if free, else the nearest free cell around
    // it (the clone's one-unit-per-cell stand-in for Mark_All_Occupation_Bits).
    Cellxy dest{gx, gy};
    const int there = unit_at_cell(gx, gy);
    if (!walkable(gx, gy) || (there >= 0 && there != idx)) {
        const std::vector<Cellxy> g = nearest_free_cells({gx, gy}, 1, passable_, grid_w_, grid_h_, occ_);
        if (g.empty()) return;
        dest = g[0];
    }
    if (dest.x == u.cell_x && dest.y == u.cell_y) return;
    // [RE: abilities.md §C1, 0x718761..0x7188B0] dist = ability_ftol(|dest - here|)
    // (2-D leptons, the combat.md §6 divergence), delay = max(dist/48, 16).
    const double dx = (dest.x - u.cell_x) * kAbilityLeptonsPerCell;
    const double dy = (dest.y - u.cell_y) * kAbilityLeptonsPerCell;
    const int dist = ability_ftol(om::sqrt(dx * dx + dy * dy));
    const int delay = teleport_delay_frames(dist, ability_rules_);
    // The parasite is ejected by the teleport (0x7188D9 -> ExitUnit): W7-04-13
    // [RE: ExitUnit 0x62A2A0] placed beside the old cell and paralysed for
    // 3 x ROF (no suppression running).
    if (u.parasite_on_me >= 0) parasite_exit(idx, /*suppressed=*/false);
    // Relocation is IMMEDIATE; the unit is then frozen (IsWarpingIn +0x271).
    occ_.release_all_of(idx, -1, -1);
    u.cell_x = dest.x;
    u.cell_y = dest.y;
    u.fx = dest.x;
    u.fy = dest.y;
    occ_.set(dest.x, dest.y, idx);
    u.path.clear();
    u.path_idx = 0;
    u.wait_time = 0.0;
    u.warp_until = static_cast<std::int64_t>(ability_frame_) + delay;
    u.frozen = true;
    ability_events_.push_back({Ev::Kind::Teleport, idx, -1, dist, delay, false});
}

void World::ability_kill_unit(int idx, int inf_death) {
    Unit& v = units_[idx];
    if (!v.alive || v.die_time >= 0.0) return;
    if (v.hp > 0) v.hp = 0;
    if (inf_death >= 0) v.ia.death = inf_death;   // W7-12-08 (presentation only)
    // The combat.cpp death path, verbatim (die anim -> tombstone in step_combat).
    v.die_time = 0.0;
    occ_.release_all_of(idx, -1, -1);
    v.target = -1;
    v.path.clear();
    v.path_idx = 0;
}

void World::ability_erase_unit(int idx) {
    Unit& v = units_[idx];
    // [RE: abilities.md §C2] UnInit -- removed, NOT killed: no die anim, no
    // death weapon, its bomb is dropped; a riding drone is let go.
    v.death_done = true;
    v.bomb_frame = -1;
    if (v.parasite_on_me >= 0) {
        Unit& d = units_[v.parasite_on_me];
        std::vector<Cellxy> g;   // W8-03-10: height-gated from the host
        {
            const Cellxy e = nearest_exit_from_host(d, v.cell_x, v.cell_y, v.on_deck);
            if (e.x >= 0) g.push_back(e);
        }
        v.parasite_on_me = -1;
        const int di = static_cast<int>(&d - units_.data());
        if (!g.empty()) {
            d.cargo_of = -1;
            d.cell_x = g[0].x;
            d.cell_y = g[0].y;
            d.fx = g[0].x;
            d.fy = g[0].y;
            occ_.set(g[0].x, g[0].y, di);
            ability_events_.push_back({Ev::Kind::DroneExit, di, idx, g[0].x, g[0].y, false});
        } else {
            d.alive = false;
            d.hp = 0;
            d.cargo_of = -1;
            ability_events_.push_back({Ev::Kind::DroneLost, di, idx, 0, 0, false});
        }
    }
    v.alive = false;
    v.die_time = -1.0;
    v.hp = 0;
    v.warp_remaining = 0;
    v.frozen = false;
    occ_.release_all_of(idx, -1, -1);
    v.path.clear();
    v.path_idx = 0;
}

void World::ability_damage_unit(int victim, int dmg, int inf_death) {
    Unit& v = units_[victim];
    if (!v.alive || v.die_time >= 0.0) return;
    if (v.an.iron_left_frames > 0.0) return;   // W2-AN-04 Iron Curtain
    v.hp -= dmg;
    if (v.hp <= 0) ability_kill_unit(victim, inf_death);
}

void World::ability_damage_building(int b_idx, int dmg) {
    Building& b = buildings_[b_idx];
    if (!b.alive || b.state == Building::State::Dead) return;
    if (b.iron_left_frames > 0.0) return;   // W2-AN-04 Iron Curtain
    b.hp -= dmg;
    if (b.hp <= 0) kill_building(b);
}

// W6-04-03 [RE: BombClass::Detonate 0x4386A0 -> DamageArea; FireDeathWeapon
// FUN_0070CAC0 -> BulletClass::Detonate FUN_004690A0 -> DamageArea; both are
// FUN_00489190, abilities.md §E/§F]: an ability blast (Ivan bomb, Terrorist /
// Demo Truck / death weapons, the Desolator eruption) is the faithful
// combat.cpp damage_area -- a building takes one hit per foundation cell in the
// rings, Iron Curtain / AffectsAllies / veterancy armour / ProneDamage apply.
// It used to be a 2-D linear falloff with one hit per building at its nearest
// cell (an Ivan left a Battle Lab at 50/500). The blast centre is (cx, cy) in
// cells at that cell's ground level; the source unit is excluded.
int World::unit_abs_z(const Unit& u) const {
    const int base = u.on_deck ? deck_z(u.cell_x, u.cell_y) : cell_level(u.cell_x, u.cell_y) * 104;
    return base + (u.is_aircraft ? ability_ftol(u.altitude) : 0);
}

void World::ability_area_damage(int src_unit, double cx, double cy, const WeaponSpec& w, int damage,
                                int z_abs) {
    Unit shooter;
    shooter.alive = false;   // never a real unit
    shooter.house = (src_unit >= 0 && src_unit < static_cast<int>(units_.size()))
                        ? units_[static_cast<std::size_t>(src_unit)].house : -1;
    shooter.w_damage = damage;
    shooter.w_verses = w.verses;
    shooter.w_cell_spread = w.cell_spread;
    shooter.w_percent_at_max = w.percent_at_max;
    shooter.w_affects_allies = w.affects_allies;
    shooter.w_prone_damage = w.prone_damage;
    shooter.w_inf_death = w.inf_death;
    shooter.w6.wall_flags = w.wall_flags;   // W6-04-04
    const int ix = ability_ftol(cx * kAbilityLeptonsPerCell);
    const int iy = ability_ftol(cy * kAbilityLeptonsPerCell);
    // W8-05-04 / w8fix-layer W8-04-02: one absolute blast height -- a unit on
    // a deck explodes at the deck, so DamageArea picks the deck layer and
    // rolls the bridge at that Z [RE: FUN_00489190].
    if (z_abs == kZOfSource)
        z_abs = (src_unit >= 0 && src_unit < static_cast<int>(units_.size()))
                    ? unit_abs_z(units_[static_cast<std::size_t>(src_unit)])
                    : cell_level(static_cast<int>(om::lround(cx)), static_cast<int>(om::lround(cy))) * 104;
    const int iz = z_abs;
    // P4 [RE: FUN_00700DA0 @0x701327]: a lethal CausesDelayKill hit on an
    // EligibleForDelayKill building starts the delay instead.
    const AreaBuildingHook delay = [&](int b, int dmg, int dist) {
        const Building& bb = buildings_[static_cast<std::size_t>(b)];
        if (!(bb.hp - dmg <= 0 && bb.iron_left_frames <= 0.0 && bb.type >= 0 &&
              bb.type < static_cast<int>(btypes().size()) &&
              btypes()[static_cast<std::size_t>(bb.type)].w5.delay_kill_eligible))
            return false;
        ability_delay_kill(b, w, static_cast<double>(dist));
        return true;
    };
    damage_area(*this, units_, buildings_, occ_, shooter, src_unit, -1, ix, iy, iz,
                w.delay_kill ? &delay : nullptr);
    // W8-05-04 [RE: FUN_00489190 @0x489DCB]: DamageArea's own bridge roll (an
    // Ivan bomb, a Demo Truck, a crash / death weapon on a bridge).
    bridge_area_roll(cx, cy, damage, w.wall_flags, z_abs);
}

// P4 [RE: FUN_00700DA0 @0x701327..0x701403]: a lethal hit by a CausesDelayKill
// warhead on an EligibleForDelayKill building does not destroy it. The delay is
// ftol(F + (F*AtMax - F) * distance / (ftol(CellSpread) * 256)) frames
// (F = DelayKillFrames); an already running timer (+0x6DF/+0x528/+0x530, the C4
// slot) is kept when its remaining frames are <= the new delay. Strength = 1;
// BuildingClass::AI (0x440152..0x4402A0) kills it with C4Warhead when the timer
// runs out -- the C4 expiry below.
void World::ability_delay_kill(int b_idx, const WeaponSpec& w, double dist_leptons) {
    Building& b = buildings_[static_cast<std::size_t>(b_idx)];
    const double f = static_cast<double>(w.delay_kill_frames);
    const int spread = ability_ftol(w.cell_spread) * 256;
    const double span = (f * w.delay_kill_at_max - f) * om::ftol(dist_leptons);
    const int delay = ability_ftol(spread > 0 ? f + span / spread : f);
    const std::int64_t F = static_cast<std::int64_t>(ability_frame_);
    bool set = true;
    if (b.c4_applied) {
        const std::int64_t remaining = std::max<std::int64_t>(0, b.c4_frame - F);
        if (delay >= remaining) set = false;
    }
    if (set) {
        b.c4_applied = true;
        b.c4_frame = F + delay;
    }
    b.hp = 1;
}

void World::ability_bind_unit(Unit& u) { bind_profile(u, ability_profiles_); }

void World::ability_apply_weapon_fields(Unit& u, const WeaponSpec& w_base) {
    // W6-04-28 [RE: veterancy.md V5/V7/V10]: an IFV passenger weapon follows
    // the gunner's rank like any slot -- the elite EliteWeapon<k>, then
    // FIREPOWER / ROF (and the AI handicap) as refresh_vet_stats applies them.
    const WeaponSpec* wp = &w_base;
    if (vet_any_ && u.vet.recv && is_elite(u.vet.veterancy) && u.abil.gunner) {
        for (std::size_t k = 0; k < u.abil.ifv_weapons.size() && k < u.abil.ifv_weapons_elite.size(); ++k)
            if (&u.abil.ifv_weapons[k] == &w_base && u.abil.ifv_weapons_elite[k].valid) {
                wp = &u.abil.ifv_weapons_elite[k];
                break;
            }
    }
    const WeaponSpec& w = *wp;
    u.w_damage = w.damage;
    u.w_rof = w.rof;
    if (vet_any_ && u.vet.recv) {
        const UnitVet& v = u.vet;
        auto has = [&](Ability a) { return has_ability(v.veterancy, v.vet_abilities, v.elite_abilities, a); };
        double house_fp = 1.0, house_rof = 1.0;
        if (ai_enabled(u.house)) {
            house_fp = ai_houses_[u.house].cfg.mods.firepower_mul;
            house_rof = ai_houses_[u.house].cfg.mods.rof_mul;
        }
        if (w.damage > 0)   // a healing weapon (RepairBullet) keeps its raw value
            u.w_damage = fire_damage(w.damage, v.firepower_mult, has(Ability::Firepower), vet_rules_.combat, house_fp);
        u.w_rof = rof_frames(w.rof, has(Ability::Rof), vet_rules_.rof, house_rof);
    }
    u.w6.burst = w.burst;   // W6-04-01 (IFV HoverMissile 2, CRMP5 ...)
    u.w6.omni_fire = w.omni_fire;   // W6-04-06
    u.w6.wall_flags = w.wall_flags; // W6-04-04
    u.w_range = w.range;
    u.w_verses = w.verses;
    u.w_cell_spread = w.cell_spread;
    u.w_percent_at_max = w.percent_at_max;
    u.abil.fire_kind = w.kind;
    // Wave-5 (R-048) [RE: abilities.md §G -- SetCurrentWeapon selects the
    // whole weapon]: the projectile's AA/AG and flight follow the swap.
    if (w.slot_set) {
        u.w_aa = u.p_aa = w.aa;
        u.w_ag = u.p_ag = w.ag;
        u.w_proj = w.proj;
    }
    // A healing weapon (RepairBullet Damage=-50) never auto-acquires enemies;
    // World::w5_step_repair_ifv aims it at damaged friendly vehicles.
    u.w5.healer = w.damage < 0;
    u.abil.no_passive_acquire = u.w5.healer;
    if (u.w5.healer) { u.target = -1; u.target_building = -1; u.attack_order = false; }
}

// One intercepted combat shot (combat.cpp try_fire / the building branch).
void World::ability_fire_event(int i) {
    Unit& u = units_[i];
    const int vt = u.ability_fired_unit, vb = u.ability_fired_building;
    u.ability_fired_unit = -1;
    u.ability_fired_building = -1;
    if (!on_map_alive(u)) return;
    const std::int64_t F = static_cast<std::int64_t>(ability_frame_);
    const AbilityKind kind = u.abil.fire_kind;

    if (vb >= 0 && vb < static_cast<int>(buildings_.size())) {
        Building& b = buildings_[vb];
        if (!b.alive || b.state == Building::State::Dead) return;
        if (kind == AbilityKind::None && u.abil.c4) {
            // [RE: abilities.md §D1, 0x51A2E3..0x51A3F0] refused while selling
            // (mission 0x13) or for a CanC4=no type; never re-armed.
            // W7-01-22: CanC4=no from the building type (the host-set list too).
            const bool immune = std::find(no_c4_types_.begin(), no_c4_types_.end(), b.type) != no_c4_types_.end() ||
                                (b.type >= 0 && b.type < static_cast<int>(btypes().size()) &&
                                 !btypes()[static_cast<std::size_t>(b.type)].w5.can_c4);
            // W7-01-23 [RE: 0x51A3F0]: no C4 on an Iron-Curtained building.
            if (!b.selling && !immune && !b.c4_applied && b.iron_left_frames <= 0) {
                b.c4_applied = true;
                b.c4_frame = F + c4_delay_frames(ability_rules_.c4_delay_minutes);
                b.c4_by = i;
                ability_events_.push_back({Ev::Kind::C4Plant, i, vb, static_cast<int>(b.c4_frame), 0, true});
            }
            u.target_building = -1;       // the infantry's mission resets (it scatters)
            u.attack_order = false;
            return;
        }
        switch (kind) {
        case AbilityKind::Temporal:
            // W7-08-04 [RE: 0x71A20A]: TemporalClass::Fire needs !IsIronCurtained.
            if (b.iron_left_frames > 0) {
                u.target_building = -1;
                u.attack_order = false;
                break;
            }
            // [§C2] Warpable buildings; Strength*10 (type Strength == max_hp).
            if (b.warp_remaining == 0) {
                b.warp_remaining = warp_remaining_init(b.max_hp);
                // W7-01-11 [RE: TemporalClass::Fire 0x71A1A0 -> 0x4521B0]: the
                // building loses its power (HasPower=0) while being erased.
                b.w7.warp_unpowered = true;
            }
            u.temporal_target_building = vb;
            u.temporal_target = -1;
            ability_events_.push_back({Ev::Kind::TemporalAttach, i, vb, b.warp_remaining, 0, true});
            break;
        case AbilityKind::IvanBomb:
            // [§E 0x438DF0] one bomb per target; FireOnce ends the mission.
            if (b.bomb_frame < 0 && is_infantry_unit(u)) {
                b.bomb_frame = F + ability_rules_.ivan_timed_delay;
                b.bomb_owner = i;
                b.bomb_house = u.house;
                ability_events_.push_back({Ev::Kind::BombPlant, i, vb, static_cast<int>(b.bomb_frame), 0, true});
            }
            u.target_building = -1;
            u.attack_order = false;
            break;
        case AbilityKind::Suicide:
            // [§F 0x6FD27C] no bullet: the firer dies; the death pass blasts.
            ability_events_.push_back({Ev::Kind::Suicide, i, vb, 0, 0, true});
            ability_kill_unit(i, u.w_inf_death);   // W7-12-08: its own weapon's warhead
            break;
        default:
            // Parasite vs a building: not infectable (BuildingType Parasiteable
            // cleared, 0x45E4E0) -- the drone gives up.
            u.target_building = -1;
            u.attack_order = false;
            break;
        }
        return;
    }

    if (vt < 0 || vt >= static_cast<int>(units_.size())) return;
    Unit& v = units_[vt];
    if (!on_map_alive(v)) return;
    switch (kind) {
    case AbilityKind::Parasite: {
        bind_profile(v, ability_profiles_);
        // [RE: abilities.md §B1, CanInfect 0x62A380]
        // W7-08-04 [RE: GetFireError 0x6FB5D0 -> 5 vs a curtained target];
        // W7-04-15 [RE: CanInfect 0x62A380 +0x2E4]: no host being erased.
        // W7-01-01 [RE: GetFireError 0x6FB5D0]: the parasite weapon's Verses
        // must allow the host (ParasiteDog is 0% vs every vehicle armour).
        if (v.parasite_on_me >= 0 || !v.abil.parasiteable || v.is_aircraft || v.an.iron_left_frames > 0 ||
            v.warp_remaining > 0 || !verses_allows(u.w_verses, v.armor_ix)) {
            u.target = -1;
            u.attack_order = false;
            return;
        }
        occ_.release_all_of(i, -1, -1);       // LimboLaunch: the drone became the bullet
        u.path.clear();
        u.path_idx = 0;
        u.target = -1;
        u.target_building = -1;
        u.attack_order = false;
        u.cargo_of = vt;                      // off-map, riding the victim
        u.parasite_next_tick = F;             // DamageDeliveryTimer = {Frame, 0}: expired
        u.suppress_until = -1;
        v.parasite_on_me = i;
        v.w6.sonic_hit = v.w6.heal_hit = false;   // W6-04-09: marks from before the grab
        v.last_hp = v.hp;
        ability_events_.push_back({Ev::Kind::Infect, i, vt, 0, 0, false});
        break;
    }
    case AbilityKind::Temporal:
        bind_profile(v, ability_profiles_);
        if (!v.abil.warpable) return;
        // W7-08-04 [RE: 0x71A20A]: Warpable && !IsIronCurtained.
        if (v.an.iron_left_frames > 0) {
            u.target = -1;
            u.attack_order = false;
            return;
        }
        // [§C2 0x71A1A0] the first legionnaire sets the budget; helpers join.
        if (v.warp_remaining == 0) v.warp_remaining = warp_remaining_init(v.max_hp);
        u.temporal_target = vt;
        u.temporal_target_building = -1;
        v.frozen = true;
        v.path.clear();
        v.path_idx = 0;
        v.target = -1;
        v.target_building = -1;
        ability_events_.push_back({Ev::Kind::TemporalAttach, i, vt, v.warp_remaining, 0, false});
        break;
    case AbilityKind::IvanBomb:
        if (v.bomb_frame < 0 && is_infantry_unit(u)) {
            v.bomb_frame = F + ability_rules_.ivan_timed_delay;
            v.bomb_owner = i;
            v.bomb_house = u.house;
            ability_events_.push_back({Ev::Kind::BombPlant, i, vt, static_cast<int>(v.bomb_frame), 0, false});
        }
        u.target = -1;                        // FireOnce=yes
        u.attack_order = false;
        break;
    case AbilityKind::Suicide:
        ability_events_.push_back({Ev::Kind::Suicide, i, vt, 0, 0, false});
        ability_kill_unit(i, u.w_inf_death);   // W7-12-08: its own weapon's warhead
        break;
    default:
        break;
    }
}

// Deaths this frame: death weapons (§F), bomb chain (§E), drone exit (§B3),
// legionnaire release (§C2). Repeated while a blast causes new deaths.
void World::ability_death_pass() {
    const std::int64_t F = static_cast<std::int64_t>(ability_frame_);
    for (int round = 0; round < 16; ++round) {
        bool any = false;
        for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
            Unit& u = units_[i];
            if (!u.alive || u.die_time < 0.0 || u.death_done) continue;
            u.death_done = true;
            any = true;
            // §F: Explodes -> DeathWeapon; else a Suicide current weapon
            // fires itself (0x701A23). IvanBomb death shots do nothing (§E).
            WeaponSpec dw;
            if (u.abil.explodes) {
                dw = u.abil.death;
            } else if (u.abil.fire_kind == AbilityKind::Suicide) {
                if (u.abil.gunner && u.ifv_current >= 0 &&
                    u.ifv_current < static_cast<int>(u.abil.ifv_weapons.size()))
                    dw = u.abil.ifv_weapons[u.ifv_current];
                else
                    dw = u.abil.death;
            }
            if (dw.valid && dw.kind != AbilityKind::IvanBomb) {
                ability_events_.push_back({Ev::Kind::DeathWeapon, i, -1, dw.damage, 0, false});
                ability_area_damage(i, u.fx, u.fy, dw, dw.damage, unit_abs_z(u));
                if (dw.rad_level > 0)
                    rad_.detonate(u.cell_x, u.cell_y, ability_ftol(dw.cell_spread), dw.rad_level,
                                  ability_frame_, ability_rules_);
            }
            // §E: the bombed object's death detonates its bomb at once.
            if (u.bomb_frame >= 0) {
                u.bomb_frame = -1;
                ability_events_.push_back({Ev::Kind::BombBlast, u.bomb_owner, i, 0, 0, false});
                ability_area_damage(-1, u.fx, u.fy, ability_rules_.ivan_warhead, ability_rules_.ivan_damage,
                                    unit_abs_z(u));
            }
            // §B3: the riding drone pops out, unless suppressed.
            if (u.parasite_on_me >= 0) {
                const int di = u.parasite_on_me;
                Unit& d = units_[di];
                u.parasite_on_me = -1;
                std::vector<Cellxy> g;
                if (!(F < d.suppress_until)) {   // W8-03-10: height-gated from the host
                    const Cellxy e = nearest_exit_from_host(d, u.cell_x, u.cell_y, u.on_deck);
                    if (e.x >= 0) g.push_back(e);
                }
                if (!g.empty()) {
                    d.cargo_of = -1;
                    d.cell_x = g[0].x;
                    d.cell_y = g[0].y;
                    d.fx = g[0].x;
                    d.fy = g[0].y;
                    d.guard_x = g[0].x;
                    d.guard_y = g[0].y;
                    occ_.set(g[0].x, g[0].y, di);
                    ability_events_.push_back({Ev::Kind::DroneExit, di, i, g[0].x, g[0].y, false});
                } else {
                    d.alive = false;          // destroyed with the host
                    d.hp = 0;
                    d.cargo_of = -1;
                    d.death_done = true;
                    ability_events_.push_back({Ev::Kind::DroneLost, di, i, 0, 0, false});
                }
            }
            u.warp_remaining = 0;
            u.temporal_target = -1;
            u.temporal_target_building = -1;
        }
        for (int b = 0; b < static_cast<int>(buildings_.size()); ++b) {
            Building& bb = buildings_[b];
            if (bb.state != Building::State::Dead || bb.ability_death_done) continue;
            bb.ability_death_done = true;
            bb.c4_applied = false;
            bb.warp_remaining = 0;
            // P4 [RE: FUN_00700DA0 -> FUN_0070CAC0]: an Explodes=yes building fires
            // its DeathWeapon in place (not when sold / erased).
            if (!bb.sold && bb.type >= 0 && bb.type < static_cast<int>(btypes().size())) {
                const BldTypeW5& tw = btypes()[static_cast<std::size_t>(bb.type)].w5;
                if (tw.explodes && tw.death.valid) {
                    any = true;
                    ability_area_damage(-1, bld_cx(bb), bld_cy(bb), tw.death, tw.death.damage);
                    // W6-04-12 [RE: FireDeathWeapon detonates a real bullet,
                    // BulletClass::Detonate FUN_004690A0]: a RadLevel death
                    // weapon (the Nuclear Reactor's NukePayload, RadLevel 500)
                    // leaves its fallout like a unit's (§F above).
                    if (tw.death.rad_level > 0)
                        rad_.detonate(static_cast<int>(om::lround(bld_cx(bb))),
                                      static_cast<int>(om::lround(bld_cy(bb))),
                                      ability_ftol(tw.death.cell_spread), tw.death.rad_level,
                                      ability_frame_, ability_rules_);
                }
            }
            if (bb.bomb_frame >= 0) {
                any = true;
                bb.bomb_frame = -1;
                ability_events_.push_back({Ev::Kind::BombBlast, bb.bomb_owner, b, 0, 0, true});
                ability_area_damage(-1, bld_cx(bb), bld_cy(bb), ability_rules_.ivan_warhead,
                                    ability_rules_.ivan_damage);
            }
        }
        if (!any) break;
    }
}

void World::step_abilities(double dt) {
    (void)dt;
    if (!abilities_any_) {
        ++ability_frame_;                     // the frame counter advances above the firewall
        return;
    }
    const std::int64_t F = static_cast<std::int64_t>(ability_frame_);
    const int n = static_cast<int>(units_.size());
    if (rad_.empty() && grid_w_ > 0 && grid_h_ > 0) rad_.init(grid_w_, grid_h_);

    // (1) bind profiles by image (spawn paths untouched).
    for (Unit& u : units_) bind_profile(u, ability_profiles_);

    // (2) IFV gunner sync [§G]: weapon = Weapon[IFVMode of the passenger], 0 when empty.
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.abil.gunner || !u.alive || u.die_time >= 0.0 || u.abil.ifv_weapons.empty()) continue;
        int mode = 0;
        if (!u.cargo.empty()) {
            const int p = u.cargo.front();
            if (p >= 0 && p < n) mode = units_[p].abil.ifv_mode;
        }
        int idx = ifv_weapon_index(mode, static_cast<int>(u.abil.ifv_weapons.size()));
        if (!u.abil.ifv_weapons[idx].valid) idx = 0;
        if (idx != u.ifv_current) {
            u.ifv_current = idx;
            ability_apply_weapon_fields(u, u.abil.ifv_weapons[idx]);
            ability_events_.push_back({Ev::Kind::IfvSwap, i, -1, idx, mode, false});
        }
    }

    // (3) the shots combat.cpp handed over this frame.
    for (int i = 0; i < n; ++i)
        if (units_[i].ability_fired_unit >= 0 || units_[i].ability_fired_building >= 0)
            ability_fire_event(i);

    // (4) deployed eruptions [§A]: DamageArea + RadSite at the own cell.
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.deployed || !on_map_alive(u) || !u.abil.deploy.valid) continue;
        if (u.deploy_fire) continue;   // Gap fix C: GI/GGI fire via step_combat
        if (F < u.deploy_next_fire) continue;
        const WeaponSpec& w = u.abil.deploy;
        // W7-08-22 [RE: the ROF getter 0x6FC5FF multiplies by VeteranROF;
        // DESO lists ROF in its Veteran/EliteAbilities]: a veteran Desolator
        // erupts every ftol(60 x .6) = 36 frames.
        int rof = w.rof;
        if (vet_any_ && u.vet.recv &&
            has_ability(u.vet.veterancy, u.vet.vet_abilities, u.vet.elite_abilities, Ability::Rof))
            rof = rof_frames(rof, true, vet_rules_.rof);
        u.deploy_next_fire = F + std::max(1, rof);
        ability_events_.push_back({Ev::Kind::Eruption, i, -1, u.cell_x, u.cell_y, false});
        u.ia.erupt = true;   // W7-02-05: the DeployedFire sequence (presentation only)
        ability_area_damage(i, u.fx, u.fy, w, w.damage, unit_abs_z(u));
        if (w.rad_level > 0)
            rad_.detonate(u.cell_x, u.cell_y, ability_ftol(w.cell_spread), w.rad_level, ability_frame_,
                          ability_rules_);
    }

    // (5) parasites [§B2/§B3].
    for (int i = 0; i < n; ++i) {
        Unit& d = units_[i];
        if (!d.alive || d.cargo_of < 0 || d.abil.fire_kind != AbilityKind::Parasite) continue;
        const int h = d.cargo_of;
        if (h < 0 || h >= n || units_[h].parasite_on_me != i || !units_[h].alive) {
            // P3: the host left the world by a path that bypassed the death
            // pass above (tombstoned with a transport, deleted by a script...)
            // -- the rider is let go as at the host's death [§B3]: out on a free
            // cell beside the host's last cell, else destroyed with it. Before,
            // it stayed "carried" forever and kept its house alive for the
            // Destroyed*All events (ALL06: a BadGuy1 dog).
            const Unit* hp = (h >= 0 && h < n) ? &units_[h] : nullptr;
            if (hp && hp->parasite_on_me == i) units_[h].parasite_on_me = -1;
            const int hx = hp ? hp->cell_x : d.cell_x, hy = hp ? hp->cell_y : d.cell_y;
            std::vector<Cellxy> g;   // W8-03-10: height-gated from the host's last cell
            {
                const Cellxy e = nearest_exit_from_host(d, hx, hy, hp ? hp->on_deck : 0);
                if (e.x >= 0) g.push_back(e);
            }
            d.cargo_of = -1;
            if (!g.empty()) {
                d.cell_x = g[0].x; d.cell_y = g[0].y; d.fx = g[0].x; d.fy = g[0].y;
                d.guard_x = g[0].x; d.guard_y = g[0].y;
                occ_.set(g[0].x, g[0].y, i);
                ability_events_.push_back({Ev::Kind::DroneExit, i, h, g[0].x, g[0].y, false});
            } else {
                d.alive = false;
                d.hp = 0;
                d.death_done = true;
                ability_events_.push_back({Ev::Kind::DroneLost, i, h, 0, 0, false});
            }
            continue;
        }
        Unit& host = units_[h];
        if (host.die_time >= 0.0) continue;       // the death pass ejects
        // W6-04-09 [RE: FootClass::ReceiveDamage FUN_004D7250]: a Sonic hit
        // (the Dolphin) or a healing hit (negative damage: SuppressionTimer =
        // 50, then ExitUnit) on the host frees it -- the parasite is put back
        // on a free cell of its own domain beside the host (a Giant Squid in
        // the water), else it stays.
        // W7-04-11 [RE: ExitUnit FUN_00629F40]: a healing hit sets the
        // suppression to 50 first, so a non-naval parasite is destroyed; a
        // Sonic hit (no suppression) throws it out alive and paralysed.
        if (host.w6.sonic_hit || host.w6.heal_hit) {
            const bool heal = host.w6.heal_hit;
            host.w6.sonic_hit = host.w6.heal_hit = false;
            host.w7.host_raw_hit = 0;
            if (parasite_exit(h, heal)) continue;
        }
        // Suppression: W7-04-14 [RE: 0x4D7250] the largest RAW (pre-armour)
        // damage of a non-parasite hit on the host this frame.
        if (host.w7.host_raw_hit > 0) {
            const int sf = suppression_frames(host.w7.host_raw_hit, d.abil.suppression_threshold);
            if (sf > 0) d.suppress_until = F + sf;
            host.w7.host_raw_hit = 0;
        }
        if (F >= d.parasite_next_tick) {
            // W7-08-18 [RE: 0x629A50 DamageDeliveryTimer = {Frame, weapon.ROF}]:
            // the raw weapon ROF, not the handicapped / veteran one.
            const int raw_rof = d.vet.base.rof > 0 ? d.vet.base.rof : d.w_rof;
            d.parasite_next_tick = F + std::max(1, raw_rof);
            int dmg;
            if (is_infantry_unit(host)) {
                dmg = host.hp;                                  // ignoreDefenses: instant kill
            } else {
                dmg = damage_vs(d.w_damage, d.w_verses, host.armor_ix);
            }
            ability_events_.push_back({Ev::Kind::ParasiteTick, i, h, dmg, host.hp - dmg, false});
            ability_damage_unit(h, dmg);
        }
        host.last_hp = host.hp;
    }

    // (6) temporal warp [§C2]: every frame, minus the attached legionnaires' Damage.
    for (int i = 0; i < n; ++i) {
        Unit& c = units_[i];
        if (c.temporal_target < 0 && c.temporal_target_building < 0) continue;
        // Letting go: dead/dying, ordered elsewhere, or moving.
        const bool keep = on_map_alive(c) && !c.moving() &&
                          ((c.temporal_target >= 0 && c.target == c.temporal_target) ||
                           (c.temporal_target_building >= 0 &&
                            c.target_building == c.temporal_target_building));
        if (!keep) {
            c.temporal_target = -1;
            c.temporal_target_building = -1;
        }
    }
    // W7-01-25 [RE: TemporalClass::Update 0x719A70]: the step is the raw
    // Weapon->Damage, not the FIREPOWER / handicap-scaled w_damage.
    auto raw_damage = [&](const Unit& c) {
        if (c.abil.gunner && c.ifv_current >= 0 && c.ifv_current < static_cast<int>(c.abil.ifv_weapons.size()))
            return c.abil.ifv_weapons[static_cast<std::size_t>(c.ifv_current)].damage;
        return c.vet.base.damage > 0 ? c.vet.base.damage : c.w_damage;
    };
    for (int v = 0; v < n; ++v) {
        Unit& vu = units_[v];
        if (vu.warp_remaining <= 0) continue;
        int step = 0, first = -1;
        for (int i = 0; i < n; ++i)
            if (units_[i].temporal_target == v) {
                step += raw_damage(units_[i]);
                if (first < 0) first = i;
            }
        if (step <= 0 || !vu.alive || vu.die_time >= 0.0) {
            vu.warp_remaining = 0;                              // let go: a fresh shot restarts
            continue;
        }
        vu.warp_remaining -= step;
        if (vu.warp_remaining <= 0) {
            ability_events_.push_back({Ev::Kind::Erase, -1, v, step, 0, false});
            // W7-01-10 [RE: 0x719A70 -> VeterancyStruct::Add FUN_0074ED70]: the
            // erase credits the legionnaire (Trainable gate in credit_kill).
            vu.vet.killed_by = first;
            vu.w7.erased_by = first;
            ability_erase_unit(v);
            for (Unit& c : units_)
                if (c.temporal_target == v) {
                    c.temporal_target = -1;
                    c.target = -1;
                    c.attack_order = false;
                }
        }
    }
    for (int b = 0; b < static_cast<int>(buildings_.size()); ++b) {
        Building& bb = buildings_[b];
        if (bb.warp_remaining <= 0) {
            bb.w7.warp_unpowered = false;
            continue;
        }
        int step = 0, first = -1;
        for (int i = 0; i < n; ++i)
            if (units_[i].temporal_target_building == b) {
                step += raw_damage(units_[i]);
                if (first < 0) first = i;
            }
        if (step <= 0 || !bb.alive || bb.state == Building::State::Dead) {
            // LetGo [RE: 0x452200]: the building gets its power back.
            bb.warp_remaining = 0;
            bb.w7.warp_unpowered = false;
            continue;
        }
        bb.warp_remaining -= step;
        if (bb.warp_remaining <= 0) {
            ability_events_.push_back({Ev::Kind::Erase, -1, b, step, 0, true});
            bb.killed_by = first;   // W7-01-10
            // W7-01-29 [RE: 0x719C02 -> KillOccupants FUN_004585B0]: the
            // garrison dies with an erased building (no unload).
            for (int o : bb.occupants)
                if (o >= 0 && o < n) {
                    Unit& ou = units_[static_cast<std::size_t>(o)];
                    ou.garrison_of = -1;
                    ou.cargo_of = -1;
                    ou.alive = false;
                    ou.hp = 0;
                    ou.death_done = true;
                }
            bb.occupants.clear();
            bb.ability_death_done = true;   // erased: no bomb chain
            bb.bomb_frame = -1;
            bb.c4_applied = false;
            kill_building(bb);
            bb.sold = true;                 // removed without the destruction explosion
            for (Unit& c : units_)
                if (c.temporal_target_building == b) {
                    c.temporal_target_building = -1;
                    c.target_building = -1;
                    c.attack_order = false;
                }
        }
    }

    // (7) bombs (plant+451, strict) and C4 (plant+27) [§E/§D2].
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (u.bomb_frame < 0 || !on_map_alive(u)) continue;
        if (!bomb_due(u.bomb_frame, ability_frame_)) continue;
        u.bomb_frame = -1;
        ability_events_.push_back({Ev::Kind::BombBlast, u.bomb_owner, i, 0, 0, false});
        ability_area_damage(-1, u.fx, u.fy, ability_rules_.ivan_warhead, ability_rules_.ivan_damage,
                                    unit_abs_z(u));
    }
    for (int b = 0; b < static_cast<int>(buildings_.size()); ++b) {
        Building& bb = buildings_[b];
        if (!bb.alive || bb.state == Building::State::Dead) continue;
        if (bb.bomb_frame >= 0 && bomb_due(bb.bomb_frame, ability_frame_)) {
            bb.bomb_frame = -1;
            ability_events_.push_back({Ev::Kind::BombBlast, bb.bomb_owner, b, 0, 0, true});
            ability_area_damage(-1, bld_cx(bb), bld_cy(bb), ability_rules_.ivan_warhead,
                                ability_rules_.ivan_damage);
        }
        if (bb.c4_applied && bb.state != Building::State::Dead && F >= bb.c4_frame) {
            // [§D2 0x440186..0x4402D8] dmg = Health, C4Warhead, ignoreDefenses=1.
            bb.c4_applied = false;
            ability_events_.push_back({Ev::Kind::C4Blast, bb.c4_by, b, bb.hp, 0, true});
            ability_damage_building(b, bb.hp);
        }
    }

    // (8) radiation [§A3/§A4]: RadSite AI, then the 16-frame application.
    rad_.step(ability_frame_, ability_rules_);
    if (rad_apply_due(ability_frame_, ability_rules_)) {
        for (int i = 0; i < n; ++i) {
            Unit& u = units_[i];
            if (!on_map_alive(u) || u.is_aircraft || u.abil.immune_radiation) continue;
            const int raw = rad_damage(rad_.level_at(u.cell_x, u.cell_y), ability_rules_);
            if (raw <= 0) continue;
            const int dmg = damage_vs(raw, ability_rules_.rad_verses, u.armor_ix);
            ability_events_.push_back({Ev::Kind::RadDamage, i, -1, dmg, raw, false});
            if (dmg > 0) ability_damage_unit(i, dmg, rad_inf_death_);   // W7-12-08 RadSiteWarhead
        }
    }

    // (9) deaths: death weapons, bomb chain, drone exit, temporal release.
    ability_death_pass();

    // (10) the frozen flag for the NEXT frame: deployed, warping in, or being erased.
    const std::int64_t next = F + 1;
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.abil_bound) continue;
        const bool was = u.frozen;
        // Gap fix C (G-45): a DeployFire GI's "deployed" is a firing post, not
        // the Desolator freeze.
        u.frozen = u.alive && ((u.deployed && !u.deploy_fire) || next < u.warp_until ||
                               u.warp_remaining > 0 || u.w6.unpowered ||   // W6-04-18
                               next < u.w7.paralysed_until);               // W7-04-13
        if (was && !u.frozen && u.warp_until >= 0 && next >= u.warp_until && u.alive &&
            u.warp_remaining <= 0 && (!u.deployed || u.deploy_fire)) {
            ability_events_.push_back({Ev::Kind::Unfreeze, i, -1, static_cast<int>(next), 0, false});
            u.warp_until = -1;
        }
    }
    ++ability_frame_;
}

}  // namespace ra2engine

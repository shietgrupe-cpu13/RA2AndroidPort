// world_an.cpp -- wave-2 W2-AN naval + aircraft World pass (step_air_naval):
// submarine submerge/sonar (W2-AN-13), Naval Yard repair (W2-AN-11), ship
// sinking (W2-AN-25), Iron Curtain / Chrono timers (W2-AN-04), the spawner
// weapons -- Carrier Hornets, Destroyer ASW Osprey, Dreadnought/V3 missiles
// (W2-AN-12), fixed-wing aircraft sorties from Airforce Command pads
// (W2-AN-01/02), the paradrop plane (W2-AN-17), Night Hawk landing (W2-AN-08)
// and Crashable deaths (W2-AN-07). Split out of world.cpp.
#include <algorithm>
#include <cmath>

#include "combat.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "flight.hpp"
#include "group_move.hpp"   // nearest_free_cells (W4-SQ-06)
#include "roster2.hpp"
#include "weapon_unit.hpp"
#include "world.hpp"

namespace ra2engine {

namespace {
constexpr double kLept = 256.0;
constexpr double kFlyClimbLps = 100.0 * 15.0;   // [RE-FALLBACK roster2.md §C] 100 leptons/frame
// W4: a rocket on its launcher sits this high (the deck) [RE-FALLBACK: the
// spawn's coords on the launcher were not traced]; the safety lifetime.
constexpr double kRocketDeckZ = 40.0;
constexpr int kRocketMaxFrames = 1800;
enum Sortie : int { kDocked = 0, kOutbound = 1, kMoveTo = 2, kReturn = 3, kLanding = 4, kParadrop = 5,
                    kLoiter = 6 };   // W7-06-03: a launched spawn gathering by its owner
bool live(const Unit& u) { return u.alive && u.die_time < 0.0 && u.cargo_of < 0; }
double cdist(double ax, double ay, double bx, double by) { return om::hypot(ax - bx, ay - by); }
}  // namespace

// --------------------------------------------------------------- targeting --

int an_naval_slot(const AnType& a, int want, bool has_secondary, bool victim_in_water,
                  bool victim_underwater, bool victim_cloaked, bool victim_organic,
                  bool victim_hover, bool victim_airborne, bool victim_is_building) {
    (void)victim_is_building;   // W7-06-02: a structure takes the same cell test
    // "This has no impact on anti/not anti air weapons" (rules.ini comment).
    if (victim_airborne) return want;
    if (!victim_in_water) {
        // [RE: GetFireError @0x6FBD7B] a target on the floor, off the water:
        // LandTargeting=1 (+0x604) -> ILLEGAL.
        if (a.land_targeting == 1) return -1;                 // LAND_NOT_OKAY
        if (a.naval_targeting == kNavUnderwaterOnly) return -1;
        // YR LAND_SECONDARY: BSUB's CruiseLauncher, not its torpedo.
        if (a.land_targeting == 2) return has_secondary ? 1 : -1;
        return want;
    }
    // W7-06-02 [RE: NavalTargeting switch FUN_006F2D80 (vtbl+0x2E8)]: a FIXED
    // slot, -1 = ILLEGAL in GetFireError. Cases 1/2 test the target TYPE's
    // Underwater (+0xD69); case 0 refuses an Underwater type only while it is
    // cloaked (+0x220); case 3 Organic (+0xD97) / Unnatural (+0x694, Yuri);
    // case 4 (SEAL) the secondary only vs a non-Hover (SpeedType 3), non-Organic
    // target (W7-06-09: the MP5 vs a hovercraft).
    int slot = 0;
    switch (a.naval_targeting) {
        case kNavUnderwaterNever: slot = (victim_underwater && victim_cloaked) ? -1 : 0; break;
        case kNavUnderwaterSecondary: slot = victim_underwater ? 1 : 0; break;
        case kNavUnderwaterOnly: slot = victim_underwater ? 0 : -1; break;
        case kNavOrganicSecondary: slot = victim_organic ? 1 : 0; break;
        case kNavSealSpecial: slot = (!victim_hover && !victim_organic) ? 1 : 0; break;
        case kNavNavalNone: slot = -1; break;
        case kNavNavalAll:
        default: slot = 0; break;
    }
    if (slot < 0) return -1;
    // [RE: SelectWeapon FUN_006F2890] a two-weapon unit uses the switch slot
    // against a target on the water; a one-weapon unit fires its primary.
    if (has_secondary) return slot;
    if (slot == 1) return -1;
    if (a.naval_targeting == kNavNavalAll) return want >= 0 ? want : 0;
    return want;
}

// W7-06-02 [RE: GetFireError @0x6FBC86: Target->GetCell()->LandType (+0xEC)
// is Water (2) or Beach (6); SelectWeapon @0x6F2B9F the same test]. The
// clone's armed LandType is the per-cell TMP terrain type (R-062).
bool World::an_victim_naval_cell(int x, int y) const {
    if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return false;
    if (land_speed_armed_) {
        const int lt = speed_land_at(x, y);
        return lt == 2 || lt == 6;
    }
    const std::size_t c = static_cast<std::size_t>(y) * grid_w_ + x;
    return c < water_passable_.size() && water_passable_[c] != 0;
}

// ------------------------------------------------------------- spawning ----

int World::an_spawn(const std::string& id, int house, int x, int y) {
    auto it = an_.types.find(id);
    if (it == an_.types.end()) return -1;
    const UnitCombat& c = it->second;
    add_combat_unit(std::max(0, std::min(grid_w_ - 1, x)), std::max(0, std::min(grid_h_ - 1, y)),
                    house, c);
    const int ui = static_cast<int>(units_.size()) - 1;
    Unit& u = units_[ui];
    // A flier holds no ground cell (add_unit claimed one).
    if (u.is_aircraft) occ_.release_all_of(ui, -1, -1);
    if (!anims_.count(u.image)) anims_[u.image] = default_anim();
    an_.any = true;
    return ui;
}

bool World::order_fixed_wing(int idx, int unit_target, int building_target, int gx, int gy) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return false;
    Unit& u = units_[idx];
    if (!u.an.t.fixed_wing || u.an.t.spawned) return false;
    an_.any = true;
    u.an.attack_unit = unit_target;
    u.an.attack_building = building_target;
    u.an.attack_x = gx;
    u.an.attack_y = gy;
    u.target = -1;
    u.target_building = -1;
    u.attack_order = false;
    u.path.clear();
    u.path_idx = 0;
    u.has_flight_goal = false;
    if (unit_target >= 0 || building_target >= 0) {
        u.an.strafe_left = 0;              // W7-05-02: a new order starts a fresh run
        if (u.an.ammo <= 0) return true;   // empty: stays docked until rearmed
        u.an.sortie = kOutbound;
    } else {
        u.an.sortie = kMoveTo;
    }
    if (u.flight_state == FlightState::Grounded) {
        occ_.release_all_of(idx, -1, -1);
        u.flight_state = FlightState::TakingOff;
    }
    return true;
}

// ------------------------------------------------------------------ naval ---

void World::an_step_naval(double dt) {
    (void)dt;
    const SwParams& P = an_.sw;
    const int n = static_cast<int>(units_.size());
    const auto& water = water_passable_;
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.alive) continue;
        // W7-06-02 [RE: GetFireError @0x6FBC86]: on a Water / Beach cell, not
        // in the air (IsInAir vtbl+0x54), not on a bridge (+0x8C).
        u.an.naval_cell = !unit_airborne(u) && u.on_deck == 0 && an_victim_naval_cell(u.cell_x, u.cell_y);
        if (u.is_naval) {
            u.an.in_water = true;
        } else if (u.movement_domain == MovementDomain::Amphibious) {
            const std::size_t c = static_cast<std::size_t>(u.cell_y) * grid_w_ + u.cell_x;
            u.an.in_water = c < water.size() && water[c] != 0 &&
                            (c >= passable_.size() || passable_[c] == 0);
        }
        // W2-AN-13 [RE: rules [General] CloakDelay=.02 -> 18 frames, "forced
        // delay that subs will remain on surface before allowing to submerge"]:
        // an Underwater+Cloakable unit is submerged unless it fired within the
        // last CloakDelay frames (firing surfaces it).
        if (u.an.t.underwater && u.an.t.cloakable) {
            if (u.die_time >= 0.0) {
                u.an.submerged = false;
            } else {
                // W6-04-25: a DecloakToFire=no weapon fires without surfacing.
                if (u.fire_flash >= 0.4 - 1e-9 && u.w6.t.decloak_to_fire)
                    u.an.surface_left_frames = P.cloak_delay_frames;
                const auto sp=spawn_launch_profiles_.find(u.tg.t.rules_id.empty()?u.image:u.tg.t.rules_id);
                if(u.an.launch_hold && sp!=spawn_launch_profiles_.end() && sp->second.decloak)
                    u.an.surface_left_frames=P.cloak_delay_frames;
                if (u.an.surface_left_frames > 0) --u.an.surface_left_frames;   // Net L0: whole frames
                u.an.submerged = u.an.surface_left_frames <= 0;
            }
        }
        // Iron Curtain / Chrono freeze countdowns.
        if (u.an.iron_left_frames > 0) --u.an.iron_left_frames;   // Net L0: whole frames
        if (u.an.chrono_left_frames > 0) {
            --u.an.chrono_left_frames;
            if (u.an.chrono_left_frames <= 0 && !u.abil_bound) u.frozen = false;
        }
    }
    for (Building& b : buildings_)
        if (b.iron_left_frames > 0) --b.iron_left_frames;
    // Sonar [RE: naval.md Deferred, FUN_00740780; rules Sensors=yes
    // SensorsSight=]: a submerged sub is visible (and so targetable) to a house
    // that has a live Sensors unit within SensorsSight cells of it.
    for (int j = 0; j < n; ++j) {
        Unit& v = units_[j];
        v.an.detected_mask = 0;
        if (!v.an.submerged || !live(v)) continue;
        // fv7c: 32-bit house mask (was uint8_t, `house & 7` aliased house 8+).
        v.an.detected_mask = (v.house >= 0 && v.house < 32) ? (1u << v.house) : 0u;
        for (int i = 0; i < n; ++i) {
            const Unit& s = units_[i];
            if (i == j || !s.an.t.sensors || !live(s) || s.house < 0 || s.house > 31) continue;
            if (cdist(s.fx, s.fy, v.fx, v.fy) <= s.an.t.sensors_sight)
                v.an.detected_mask |= (1u << s.house);
        }
    }
    // W2-AN-11 Naval Yard UnitRepair=yes: a damaged ship of the yard's house,
    // stopped within one cell of the yard, is repaired RepairStep hit points
    // every RepairRate. W7-06-07 [RE: the yard runs the depot's UnitRepair
    // mission FUN_0044B780 -> radio 0x1C, TechnoClass::Receive_Message
    // FUN_006F4010 case 0x1C: cost = GetRepairStepCost FUN_00711470, step =
    // RepairStep (UnitType FUN_00746D50 -- an Organic Dolphin / Squid too, not
    // IRepairStep); a house that cannot pay gets 0x20 and is released]: each
    // step is paid from the same wallet the Service Depot uses.
    if (!an_.repair_btype.empty()) {
        const bool tick = (an_.frame % std::max(1, P.repair_rate_frames)) == 0;
        if (tick)
            for (const Building& b : buildings_) {
                if (!b.alive || b.state != Building::State::Idle) continue;
                if (b.type < 0 || b.type >= static_cast<int>(an_.repair_btype.size()) ||
                    !an_.repair_btype[b.type])
                    continue;
                for (Unit& u : units_) {
                    if (!live(u) || !u.is_naval || u.house != b.house || u.hp >= u.max_hp) continue;
                    if (u.moving() || u.target >= 0) continue;
                    const int dx = u.cell_x < b.cx ? b.cx - u.cell_x : (u.cell_x >= b.cx + b.fw ? u.cell_x - (b.cx + b.fw - 1) : 0);
                    const int dy = u.cell_y < b.cy ? b.cy - u.cell_y : (u.cell_y >= b.cy + b.fh ? u.cell_y - (b.cy + b.fh - 1) : 0);
                    if (std::max(dx, dy) > 1) continue;
                    const int h = b.house;
                    if (h < 0 || h >= house_count()) continue;
                    const int step = repair_rules_.repair_step > 0 ? repair_rules_.repair_step : P.repair_step;
                    const int cost = repair_step_cost(u.vet.cost > 0 ? u.vet.cost : u.max_hp, u.max_hp, step,
                                                      repair_rules_.repair_percent);
                    double& pool = econ_active(h) ? econ_[static_cast<std::size_t>(h)].credits
                                                  : house_credits_[static_cast<std::size_t>(h)];
                    if (pool < static_cast<double>(cost)) continue;   // 0x20: broke, no free step
                    pool -= static_cast<double>(cost);
                    u.hp = std::min(u.max_hp, u.hp + step);
                    // W6-04-09 [RE-FALLBACK: the yard repair taken as a healing
                    // (negative) hit, which ejects a parasite in FUN_004D7250]
                    if (u.parasite_on_me >= 0) u.w6.heal_hit = true;
                }
            }
    }
}

// --------------------------------------------------------------- spawners ---

// W4: a rocket's target point in cells (+ its height in leptons): a live
// unit (its altitude when airborne) or a building's centre.
bool World::rocket_target_point(int tu, int tb, double& x, double& y, double& z) const {
    const int n = static_cast<int>(units_.size());
    if (tu >= 0 && tu < n && live(units_[tu])) {
        const Unit& t = units_[tu];
        x = t.fx;
        y = t.fy;
        // w8fix-layer W8-04-02: a target on a deck is aimed at the deck
        // (its coords carry BridgeHeight, 0x51F810).
        z = (t.is_aircraft && t.altitude > 0.0) ? t.altitude : (t.on_deck ? 416.0 : 0.0);
        return true;
    }
    if (tb >= 0 && tb < static_cast<int>(buildings_.size()) && buildings_[tb].alive &&
        buildings_[tb].state != Building::State::Dead) {
        const Building& b = buildings_[tb];
        x = b.cx + (b.fw - 1) / 2.0;
        y = b.cy + (b.fh - 1) / 2.0;
        z = 0.0;
        return true;
    }
    return false;
}

// W4: a rocket explodes [FUN_00662A70]: its own block's warhead (elite when
// the launcher was elite) at the nose point; then it is gone. A rocket SHOT
// DOWN dies the ordinary TechnoClass death instead: Explodes=no on
// V3ROCKET/DMISL, so no warhead damage -- only its Explosion= anims (the
// Process tail's Strength<1 -> FUN_00662A70 path is the defensive case for a
// rocket left alive at 0 HP) [PROBABLE: ALL01's Patriots, 7 cells from the
// fleet, would otherwise blow up the Dreadnoughts with their own missiles].
void World::rocket_explode(int i, int end) {
    Unit& u = units_[i];
    const RocketBlock& blk = an_.sw.rockets.block[static_cast<std::size_t>(
        std::clamp(u.an.t.rocket_kind < 0 ? 1 : u.an.t.rocket_kind, 0, 2))];
    const RocketWarhead& wh = rocket_warhead(blk, u.an.rocket_elite);
    double nx = 0, ny = 0, nz = 0;
    rocket_nose(blk, u.an.rocket, nx, ny, nz);
    if (end == 2) {   // shot down: the explosion is where the rocket is
        nx = u.an.rocket.x;
        ny = u.an.rocket.y;
        nz = u.an.rocket.z;
    }
    u.an.rocket_end = end;
    u.fx = nx / kLept;
    u.fy = ny / kLept;
    u.altitude = std::max(0.0, nz);
    const int house = u.house;
    if (end == 1)
        an_area_damage(house, nx / kLept, ny / kLept, rocket_damage(blk, u.an.rocket_elite), wh.spread,
                       wh.pmax, wh.verses, /*hit_air=*/false, /*spare_house=*/false, std::max(0.0, nz),
                       wh.wall_flags);   // W6-04-04 (V3WH / DMISLWH: Wall + Wood)
    if (end == 1) {
        // W8-05-04 / W8-04-03 [RE: FUN_00662A70 @0x662D07 -> DamageArea
        // FUN_00489190, V3WH / DMISLWH Wall=yes]: the detonation rolls the
        // bridge in its cell; Z is the nose height (the deck height when it
        // bursts on a target standing on a deck).
        const int bx = static_cast<int>(om::lround(nx / kLept)), by = static_cast<int>(om::lround(ny / kLept));
        int bz = cell_level(bx, by) * 104 + static_cast<int>(std::max(0.0, nz));
        const int tg = units_[i].target;
        if (tg >= 0 && tg < static_cast<int>(units_.size()) && units_[static_cast<std::size_t>(tg)].on_deck &&
            units_[static_cast<std::size_t>(tg)].cell_x == bx && units_[static_cast<std::size_t>(tg)].cell_y == by)
            bz = std::max(bz, deck_z(bx, by) + static_cast<int>(std::max(0.0, nz)));
        bridge_area_roll(nx / kLept, ny / kLept, rocket_damage(blk, u.an.rocket_elite), wh.wall_flags, bz);
    }
    Unit& r = units_[i];   // (an_area_damage never reallocates units_; re-fetch anyway)
    if (end == 1) {
        fire_events_.push_back({i, -1, -1, -1, 0, ""});
        r.alive = false;   // the rocket is spent (no wreck); DontScore
        r.die_time = 1.0;
    }   // (shot down: the kill already started its death)
    // W7-06-08: the launcher's node sees the unlinked rocket itself
    // (an_step_spawners, Detach FUN_006B72E0).
}

// W7-06-03/06/08 [RE: SpawnManagerClass -- ctor FUN_006B6310, AI
// FUN_006B68B0, SetTarget FUN_006B7210, ResetTarget FUN_006B7230, Detach
// FUN_006B72E0, owner lost FUN_006B6780, launching count FUN_006B7400]. The
// owner's spawner weapon only names the manager's target (TechnoClass::Fire
// FUN_006FD210 @0x6FD333 -> FUN_006B7210); the manager AI, every 10 frames,
// launches each docked node in turn one launch timer apart (20 frames, 9 for a
// MissileSpawn), gathers the aircraft by the owner until every node is out,
// then sends them in together; each node rebuilds on its own timer.
namespace {
constexpr int kSpawnAiPeriod = 10;    // FUN_006B68B0: +0x50/+0x58 timer = 10
constexpr int kSpawnGapPlane = 20;    // case 0: +0x64 = 0x14
constexpr int kSpawnGapMissile = 9;   // case 0: +0x64 = 9 (MissileSpawn +0xD68)
}  // namespace

void World::an_spawn_reset_target(int i) {
    // FUN_006B7230: a missile still in state 2 is handed to the
    // KamikazeTracker (FUN_0054E040 -- it flies on at the target it was given)
    // and detached (FUN_006B72E0: a rocket node -> 7, SpawnRegenRate).
    AnUnit& m = units_[static_cast<std::size_t>(i)].an;
    for (std::size_t k = 0; k < m.sp_state.size(); ++k) {
        if (m.sp_state[k] != kSpawnLaunched) continue;
        const int su = m.sp_unit[k];
        if (su < 0 || su >= static_cast<int>(units_.size()) || !units_[static_cast<std::size_t>(su)].an.t.missile)
            continue;
        m.sp_state[k] = kSpawnDead;
        m.sp_timer[k] = std::max(0, m.t.spawn_regen_frames);
        m.sp_unit[k] = -1;
    }
    m.sp_status = 0;
    m.sp_tgt_unit = m.sp_tgt_bld = -1;
    m.sp_next_unit = m.sp_next_bld = -1;
}

void World::an_spawn_launch(int i, int k) {
    const std::string spawns = units_[static_cast<std::size_t>(i)].an.t.spawns;
    const int regen = std::max(0, units_[static_cast<std::size_t>(i)].an.t.spawn_regen_frames);
    auto it = an_.types.find(spawns);
    if (it == an_.types.end()) return;
    const bool missile = it->second.an.missile;
    {
        AnUnit& m = units_[static_cast<std::size_t>(i)].an;
        // case 0: SpawnTimer (+0x5C/+0x64) = 0x14, or 9 for MissileSpawn.
        m.sp_launch = missile ? kSpawnGapMissile : kSpawnGapPlane;
        m.sp_state[static_cast<std::size_t>(k)] = kSpawnLaunched;
    }
    const int si = an_spawn(spawns, units_[static_cast<std::size_t>(i)].house,
                            units_[static_cast<std::size_t>(i)].cell_x, units_[static_cast<std::size_t>(i)].cell_y);
    Unit& own = units_[static_cast<std::size_t>(i)];   // re-fetch: units_ may have reallocated
    if (si < 0) {
        own.an.sp_state[static_cast<std::size_t>(k)] = kSpawnDead;
        own.an.sp_timer[static_cast<std::size_t>(k)] = regen;
        return;
    }
    Unit& s = units_[static_cast<std::size_t>(si)];
    const int tu = own.an.sp_tgt_unit, tb = own.an.sp_tgt_bld;
    own.an.sp_unit[static_cast<std::size_t>(k)] = si;
    s.fx = own.fx + (missile ? 0.0 : 0.2*k);
    s.fy = own.fy;
    s.cell_x = own.cell_x;
    s.cell_y = own.cell_y;
    s.an.owner = i;
    s.an.ammo = s.an.t.ammo > 0 ? s.an.t.ammo : 1;
    s.flight_state = FlightState::TakingOff;
    s.altitude = 1.0;
    s.body_facing = own.body_facing;
    // W4: locomotor +0x4D -- the launcher's elite rank picks the elite warhead.
    s.an.rocket_elite = missile && is_elite(own.vet.veterancy);
    if (missile) {
        // case 0, rocket node: SetDestination(target) + Mission Move -- the
        // RocketLocomotion [FUN_00661D00] pauses and tilts on the deck itself.
        s.an.attack_unit = tu;
        s.an.attack_building = tb;
        s.an.sortie = kOutbound;
        double gx = s.fx, gy = s.fy, gz = 0.0;
        rocket_target_point(tu, tb, gx, gy, gz);
        // W7-04-05 [RE: KamikazeTrackerClass::Add 0x54E040 stores
        // MapClass::Cell(Target->GetCoords()) at launch; the tracker
        // (0x54E160) and RocketLocomotion (0x661D00) steer at that FIXED
        // cell]: the impact point is the centre of the cell the target held
        // when the rocket left (a building's centre coord cell: cx + fw/2),
        // on the ground -- a moving tank dodges.
        if (tu >= 0 && tu < static_cast<int>(units_.size())) {
            gx = om::lround(units_[static_cast<std::size_t>(tu)].fx);
            gy = om::lround(units_[static_cast<std::size_t>(tu)].fy);
        } else if (tb >= 0 && tb < static_cast<int>(buildings_.size())) {
            const Building& tbd = buildings_[static_cast<std::size_t>(tb)];
            gx = tbd.cx + tbd.fw / 2;
            gy = tbd.cy + tbd.fh / 2;
            gz = 0.0;
        }
        const RocketBlock& blk = an_.sw.rockets.block[static_cast<std::size_t>(
            std::clamp(s.an.t.rocket_kind < 0 ? 1 : s.an.t.rocket_kind, 0, 2))];
        // A spawn leaves the active launcher slot, in body space. The second
        // missile adds SecondSpawnOffset before alternating the lateral sign.
        const ProjectileSpec& muzzle=own.an.t.spawner_slot==1 ? own.w2.proj : own.p_proj;
        const std::string& id=own.tg.t.rules_id.empty() ? own.image : own.tg.t.rules_id;
        const auto profile=spawn_launch_profiles_.find(id);
        SpawnLaunchProfile p;
        if(profile!=spawn_launch_profiles_.end())p=profile->second;
        double f=muzzle.flh[0],l=muzzle.flh[1],h=muzzle.flh[2];
        if(k&1) {f+=p.second[0];l=-(l+p.second[1]);h+=p.second[2];}
        const double angle=own.body_facing*(2.0*3.14159265358979323846/65536.0);
        const double dx=om::sin(angle),dy=-om::cos(angle);
        s.fx=own.fx+(f*dx+l*dy)/kLept;
        s.fy=own.fy+(f*dy-l*dx)/kLept;
        // Unbound test/legacy types retain the historical deck height.
        const double launch_z=profile!=spawn_launch_profiles_.end() ? h : kRocketDeckZ;
        rocket_launch(blk, s.an.rocket, s.fx * kLept, s.fy * kLept, launch_z, gx * kLept, gy * kLept);
        if(p.decloak && own.an.t.underwater && own.an.t.cloakable) {
            own.an.submerged=false;
            own.an.surface_left_frames=std::max(own.an.surface_left_frames,an_.sw.cloak_delay_frames);
        }
        s.an.rocket.tx = gx * kLept;
        s.an.rocket.ty = gy * kLept;
        s.an.rocket.tz = gz;
        s.altitude = launch_z;
        s.body_facing = facing_u16_from_delta(om::cos(s.an.rocket.yaw), om::sin(s.an.rocket.yaw));
    } else {
        // case 0, aircraft: SetDestination(the owner cell's neighbour in
        // direction 0 = N, FUN_00481720(0)) + Mission Move -- it gathers by
        // the owner, with no target yet.
        s.an.attack_unit = s.an.attack_building = -1;
        s.an.attack_x = std::max(0, std::min(grid_w_ - 1, own.cell_x));
        s.an.attack_y = std::max(0, std::min(grid_h_ - 1, own.cell_y - 1));
        s.an.sortie = kLoiter;
    }
}

void World::an_spawn_owner_lost(int i) {
    // FUN_006B6780: docked / reloading nodes are deleted with the owner; a
    // rocket still in its launch window (state 1) is pulled from the
    // KamikazeTracker (FUN_0054E220) and deleted; every other spawn goes to the
    // KamikazeTracker (FUN_0054E040), which crashes a non-MissileSpawn
    // (vtbl+0x3DC) and flies a missile on at its target.
    std::vector<int> crash;
    {
        AnUnit& m = units_[static_cast<std::size_t>(i)].an;
        for (std::size_t k = 0; k < m.sp_state.size(); ++k) {
            const int st = m.sp_state[k], su = m.sp_unit[k];
            if (st == kSpawnDead || st == kSpawnDocked || st == kSpawnReload) continue;
            if (su < 0 || su >= static_cast<int>(units_.size())) continue;
            Unit& s = units_[static_cast<std::size_t>(su)];
            if (!s.alive || s.die_time >= 0.0) continue;
            if (st == kSpawnRocketWait) {
                s.alive = false;            // UnInit (vtbl+0xF8): gone, no blast
                s.die_time = 1.0;
                s.deployed_away = true;     // not a loss (DontScore)
                s.an.rocket_end = 1;
            } else if (!s.an.t.missile) {
                crash.push_back(su);
            }
        }
        m.sp_state.clear();
        m.sp_timer.clear();
        m.sp_unit.clear();
        m.spawn_ready = m.spawn_out = m.spawn_pending = 0;
        m.launch_hold = false;
        m.sp_status = 0;
        m.sp_tgt_unit = m.sp_tgt_bld = m.sp_next_unit = m.sp_next_bld = -1;
    }
    for (int su : crash) an_crash_alive(su);
}

void World::an_spawn_tick(int i) {
    const std::string spawns = units_[static_cast<std::size_t>(i)].an.t.spawns;
    const int regen = std::max(0, units_[static_cast<std::size_t>(i)].an.t.spawn_regen_frames);
    const int reload = std::max(0, units_[static_cast<std::size_t>(i)].an.t.spawn_reload_frames);
    const double sp_range = units_[static_cast<std::size_t>(i)].an.t.spawner_range;
    auto it = an_.types.find(spawns);
    const bool rocket = it != an_.types.end() && it->second.an.missile;
    const int n = static_cast<int>(units_[static_cast<std::size_t>(i)].an.sp_state.size());
    auto M = [&]() -> AnUnit& { return units_[static_cast<std::size_t>(i)].an; };
    auto has_tgt = [&]() { return M().sp_tgt_unit >= 0 || M().sp_tgt_bld >= 0; };
    auto promote = [&]() {   // "if (+0x6C) { +0x68 = +0x6C; +0x6C = 0; }"
        AnUnit& m = M();
        if (m.sp_next_unit >= 0 || m.sp_next_bld >= 0) {
            m.sp_tgt_unit = m.sp_next_unit;
            m.sp_tgt_bld = m.sp_next_bld;
            m.sp_next_unit = m.sp_next_bld = -1;
        }
    };
    auto spawn_of = [&](int k) -> int {
        const int su = M().sp_unit[static_cast<std::size_t>(k)];
        return (su < 0 || su >= static_cast<int>(units_.size())) ? -1 : su;
    };
    auto send_attack = [&](int su) {   // vtbl+0x3C8 SetTarget + Mission Attack (1)
        Unit& s = units_[static_cast<std::size_t>(su)];
        s.an.attack_unit = M().sp_tgt_unit;
        s.an.attack_building = M().sp_tgt_bld;
        s.an.sortie = kOutbound;
    };
    auto send_home = [&](int su) {   // SetDestination(owner), SetTarget(0), Mission Move
        Unit& s = units_[static_cast<std::size_t>(su)];
        s.an.attack_unit = s.an.attack_building = -1;
        s.an.attack_x = s.an.attack_y = -1;
        s.an.sortie = kReturn;
    };
    for (int k = 0; k < n; ++k) {
        const std::size_t K = static_cast<std::size_t>(k);
        const int st = M().sp_state[K];
        switch (st) {
            case kSpawnDocked: {
                if (!has_tgt() || M().sp_launch > 0 || M().sp_status == 2) break;
                // A rocket node waits while the launcher's locomotor moves
                // (vtbl+0x10 Is_Moving / +0x80) [@0x6B6966..0x6B69DA].
                if (rocket && units_[static_cast<std::size_t>(i)].moving()) break;
                an_spawn_launch(i, k);
                break;
            }
            case kSpawnRocketWait:
                // Its pause + tilt ran out: detach (FUN_006B72E0, rocket node
                // flag +0x14 = 1) -> state 7 with SpawnRegenRate.
                if (M().sp_timer[K] <= 0) {
                    M().sp_state[K] = kSpawnDead;
                    M().sp_timer[K] = regen;
                    M().sp_unit[K] = -1;
                }
                break;
            case kSpawnLaunched: {
                if (rocket) break;
                promote();
                const int su = spawn_of(k);
                if (su < 0) break;
                if (has_tgt()) {   // keep gathering: the neighbour cell in direction 4 = S
                    const Unit& o = units_[static_cast<std::size_t>(i)];
                    Unit& s = units_[static_cast<std::size_t>(su)];
                    s.an.attack_x = std::max(0, std::min(grid_w_ - 1, o.cell_x));
                    s.an.attack_y = std::max(0, std::min(grid_h_ - 1, o.cell_y + 1));
                    s.an.sortie = kLoiter;
                } else {
                    send_home(su);
                    M().sp_state[K] = kSpawnReturn;
                }
                break;
            }
            case kSpawnAttack: {
                promote();
                const int su = spawn_of(k);
                if (su < 0) break;
                if (!units_[static_cast<std::size_t>(su)].alive && units_[static_cast<std::size_t>(su)].deployed_away) {
                    // (the clone's sortie already flew it home and aboard)
                    M().sp_state[K] = kSpawnReload;
                    M().sp_timer[K] = reload;
                    M().sp_unit[K] = -1;
                } else if (units_[static_cast<std::size_t>(su)].an.ammo == 0 || !has_tgt()) {
                    send_home(su);
                    M().sp_state[K] = kSpawnReturn;
                } else {
                    send_attack(su);
                }
                break;
            }
            case kSpawnReturn: {
                promote();
                const int su = spawn_of(k);
                if (su < 0) break;
                const Unit& s = units_[static_cast<std::size_t>(su)];
                const bool aboard = !s.alive && s.deployed_away;
                if (!aboard && s.an.ammo >= 1 && has_tgt()) {
                    M().sp_state[K] = kSpawnAttack;
                    send_attack(su);
                } else if (aboard) {
                    // Back on the owner: Limbo, state 6, SpawnReloadRate (+0x34).
                    M().sp_state[K] = kSpawnReload;
                    M().sp_timer[K] = reload;
                    M().sp_unit[K] = -1;
                } else if (s.alive && s.die_time < 0.0 && s.an.sortie != kReturn) {
                    send_home(su);
                }
                break;
            }
            case kSpawnReload:
                // Rearmed (Ammo = type Ammo) and healed (Strength = type Strength).
                if (M().sp_timer[K] <= 0) M().sp_state[K] = kSpawnDocked;
                break;
            case kSpawnDead:
                if (M().sp_timer[K] <= 0) M().sp_state[K] = kSpawnDocked;   // a new spawn, in Limbo
                break;
            default:
                break;
        }
    }
    // The manager status (+0x70).
    if (M().sp_status == 0) {
        promote();
        if (has_tgt()) {
            // vtbl+0x3AC (FUN_006F6CB0): InRange(target, SelectWeapon(target)).
            const Unit& o = units_[static_cast<std::size_t>(i)];
            double d = 0.0;
            if (M().sp_tgt_unit >= 0) {
                const Unit& v = units_[static_cast<std::size_t>(M().sp_tgt_unit)];
                d = cdist(o.fx, o.fy, v.fx, v.fy);
            } else {
                const Building& b = buildings_[static_cast<std::size_t>(M().sp_tgt_bld)];
                d = -1.0;
                for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
                    const double dc = cdist(o.fx, o.fy, c.x, c.y);
                    if (d < 0.0 || dc < d) d = dc;
                }
            }
            if (!range_infinite(sp_range) && d > std::max(sp_range, 1.0) + 1e-9) an_spawn_reset_target(i);   // fv7c
            else M().sp_status = 1;
        }
    } else if (M().sp_status == 1) {
        if (!has_tgt()) {
            an_spawn_reset_target(i);
            return;
        }
        for (int k = 0; k < n; ++k) {
            const int st = M().sp_state[static_cast<std::size_t>(k)];
            if (st != kSpawnLaunched && st != kSpawnDead) return;   // still launching
        }
        bool any_missile = false;
        for (int k = 0; k < n; ++k) {
            const std::size_t K = static_cast<std::size_t>(k);
            if (M().sp_state[K] != kSpawnLaunched) continue;
            const int su = spawn_of(k);
            if (su >= 0 && !units_[static_cast<std::size_t>(su)].an.t.missile) {
                M().sp_state[K] = kSpawnAttack;
                send_attack(su);
            } else {
                // KamikazeTracker (FUN_0054E040); a rocket node -> state 1 with
                // the block's PauseFrames + TiltFrames (Rules+0x4B0/0x4B4 for
                // the V3 type, else the DMisl block +0x4E4/0x4E8).
                any_missile = true;
                const int kind = (su >= 0 && units_[static_cast<std::size_t>(su)].an.t.rocket_kind == 0) ? 0 : 1;
                const RocketBlock& blk = an_.sw.rockets.block[static_cast<std::size_t>(kind)];
                M().sp_state[K] = kSpawnRocketWait;
                M().sp_timer[K] = std::max(0, blk.pause_frames + blk.tilt_frames);
            }
        }
        if (any_missile) an_spawn_reset_target(i);
        M().sp_status = 2;
    } else if (M().sp_status == 2) {
        for (int k = 0; k < n; ++k) {
            const int st = M().sp_state[static_cast<std::size_t>(k)];
            if (st == kSpawnAttack || st == kSpawnReturn) return;
        }
        M().sp_status = 0;
    }
}

void World::an_step_spawners(double dt) {
    (void)dt;
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        if (units_[i].an.t.spawner_slot < 0) continue;
        if (units_[i].an.sp_state.empty()) {
            units_[i].an.spawn_fire_unit = units_[i].an.spawn_fire_building = -1;
            continue;
        }
        if (!units_[i].alive || units_[i].die_time >= 0.0) {
            units_[i].an.spawn_fire_unit = units_[i].an.spawn_fire_building = -1;
            an_spawn_owner_lost(i);
            continue;
        }
        if (units_[i].cargo_of >= 0) {   // carried: the manager is frozen
            units_[i].an.spawn_fire_unit = units_[i].an.spawn_fire_building = -1;
            continue;
        }
        const int n = static_cast<int>(units_.size());
        {
            AnUnit& m = units_[i].an;
            const int regen = std::max(0, m.t.spawn_regen_frames);
            // Timers count down every frame; the AI reads them every 10 frames.
            if (m.sp_launch > 0) --m.sp_launch;
            for (int& tm : m.sp_timer)
                if (tm > 0) --tm;
            // FUN_006B72E0 (Detach): a spawn that died / detonated / was shot
            // down frees its node at once -> state 7, its own SpawnRegenRate
            // timer (+0x30) from this frame. A spawn back aboard waits for the AI.
            for (std::size_t k = 0; k < m.sp_state.size(); ++k) {
                const int st = m.sp_state[k], su = m.sp_unit[k];
                if (st < kSpawnRocketWait || st > kSpawnReturn || su < 0 || su >= n) continue;
                const Unit& s = units_[static_cast<std::size_t>(su)];
                if (s.alive && s.die_time < 0.0) continue;
                if (!s.alive && s.deployed_away && st != kSpawnRocketWait) continue;
                m.sp_state[k] = kSpawnDead;
                m.sp_timer[k] = regen;
                m.sp_unit[k] = -1;
            }
        }
        // The target died: +0x68 = 0, ResetTarget unless a next one is queued.
        auto tgt_live = [&](int tu, int tb) {
            if (tu >= 0) return tu < n && live(units_[static_cast<std::size_t>(tu)]);
            if (tb >= 0)
                return tb < static_cast<int>(buildings_.size()) && buildings_[static_cast<std::size_t>(tb)].alive &&
                       buildings_[static_cast<std::size_t>(tb)].state != Building::State::Dead;
            return false;
        };
        {
            AnUnit& m = units_[i].an;
            if ((m.sp_next_unit >= 0 || m.sp_next_bld >= 0) && !tgt_live(m.sp_next_unit, m.sp_next_bld))
                m.sp_next_unit = m.sp_next_bld = -1;
            if ((m.sp_tgt_unit >= 0 || m.sp_tgt_bld >= 0) && !tgt_live(m.sp_tgt_unit, m.sp_tgt_bld)) {
                m.sp_tgt_unit = m.sp_tgt_bld = -1;
                if (m.sp_next_unit < 0 && m.sp_next_bld < 0) an_spawn_reset_target(i);
            }
        }
        // TechnoClass::Fire -> FUN_006B7210: a new target is queued (+0x6C)
        // unless it is the current one.
        {
            AnUnit& m = units_[i].an;
            const int fu = m.spawn_fire_unit, fb = m.spawn_fire_building;
            m.spawn_fire_unit = m.spawn_fire_building = -1;
            if ((fu >= 0 || fb >= 0) && !(fu == m.sp_tgt_unit && fb == m.sp_tgt_bld)) {
                m.sp_next_unit = fu;
                m.sp_next_bld = fb;
            }
        }
        if (units_[i].an.sp_tick > 0) --units_[i].an.sp_tick;
        if (units_[i].an.sp_tick <= 0) {
            units_[i].an.sp_tick = kSpawnAiPeriod;
            an_spawn_tick(i);
        }
        // Counters for the probes / AI, and the launch hold (FUN_006B7400:
        // nodes in state 1 + rockets still in state 2).
        AnUnit& m = units_[i].an;
        m.spawn_ready = m.spawn_out = m.spawn_pending = 0;
        int launching = 0;
        for (std::size_t k = 0; k < m.sp_state.size(); ++k) {
            const int st = m.sp_state[k];
            if (st == kSpawnDocked) ++m.spawn_ready;
            else if (st == kSpawnReload) ++m.spawn_pending;
            else if (st >= kSpawnRocketWait && st <= kSpawnReturn) ++m.spawn_out;
            if (st == kSpawnRocketWait) {
                ++launching;
            } else if (st == kSpawnLaunched) {
                const int su = m.sp_unit[k];
                if (su >= 0 && su < static_cast<int>(units_.size()) &&
                    units_[static_cast<std::size_t>(su)].alive && units_[static_cast<std::size_t>(su)].an.t.missile)
                    ++launching;
            }
        }
        m.launch_hold = launching > 0;
    }
}

// ------------------------------------------------------------- fixed wing ---

void World::an_step_fixed_wing(double dt) {
    const SwParams& P = an_.sw;
    // W8-05-07: a superweapon plane's jump appends its trooper to units_;
    // room for one per plane keeps the loop's Unit& valid.
    {
        std::size_t planes = 0;
        for (const Unit& p : units_) planes += p.an.sortie == kParadrop && p.an.drop_left > 0;
        if (planes) units_.reserve(units_.size() + planes);
    }
    const int n = static_cast<int>(units_.size());
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!(u.an.t.fixed_wing || u.an.t.missile) || !u.alive || u.die_time >= 0.0) continue;
        // (Spawned=yes: not player controllable -- Selection::prune drops it)
        // A player/AI attack order lands in target/target_building (commit_attack_target):
        // turn it into a sortie.
        if (!u.an.t.spawned && (u.target >= 0 || u.target_building >= 0)) {
            const int tu = u.target, tb = u.target_building;
            order_fixed_wing(i, tu, tb, -1, -1);
        }
        if (u.rearm_frames > 0) --u.rearm_frames;   // Net L0: whole frames
        u.fire_flash = std::max(0.0, u.fire_flash - dt);
        // --- where to fly ---
        double gx = u.fx, gy = u.fy;
        bool have_goal = false, target_alive = false;
        int tu = u.an.attack_unit, tb = u.an.attack_building;
        if (tu >= 0 && (tu >= n || !live(units_[tu]))) tu = u.an.attack_unit = -1;
        if (tb >= 0 && (tb >= static_cast<int>(buildings_.size()) || !buildings_[tb].alive ||
                        buildings_[tb].state == Building::State::Dead))
            tb = u.an.attack_building = -1;
        if (tu >= 0) {
            gx = units_[tu].fx; gy = units_[tu].fy; have_goal = target_alive = true;
        } else if (tb >= 0) {
            const Building& b = buildings_[tb];
            gx = b.cx + (b.fw - 1) / 2.0; gy = b.cy + (b.fh - 1) / 2.0; have_goal = target_alive = true;
        }
        // W4 RocketLocomotion [FUN_00661D00]: pause, tilt, climb to the block
        // Altitude, LazyCurve arc or level-off + dive, nose-contact explode.
        if (u.an.t.missile) {
            RocketFlight& f = u.an.rocket;
            // W7-04-05: no re-aim -- the impact point was frozen at launch.
            (void)have_goal;
            const RocketBlock& blk = P.rockets.block[static_cast<std::size_t>(
                std::clamp(u.an.t.rocket_kind < 0 ? 1 : u.an.t.rocket_kind, 0, 2))];
            const int r = rocket_step(blk, u.an.t.rocket_speed, f, f.tx, f.ty, f.tz,u.an.t.rocket_kind==kRocketCMisl);
            u.fx = f.x / kLept;
            u.fy = f.y / kLept;
            u.altitude = std::max(0.0, f.z);
            u.cell_x = std::max(0, std::min(grid_w_ - 1, static_cast<int>(om::lround(u.fx))));
            u.cell_y = std::max(0, std::min(grid_h_ - 1, static_cast<int>(om::lround(u.fy))));
            u.body_facing = facing_u16_from_delta(om::cos(f.yaw), om::sin(f.yaw));
            u.flight_state = f.phase >= kRocketClimb ? FlightState::Cruising : FlightState::TakingOff;
            const bool off_map = grid_w_ > 0 && grid_h_ > 0 &&
                                 (f.x < -kLept || f.y < -kLept || f.x > grid_w_ * kLept || f.y > grid_h_ * kLept);
            if (r == kRocketDetonate || off_map || f.frames > kRocketMaxFrames) rocket_explode(i, 1);
            continue;
        }
        // Home airfield (a spawned plane's home is its spawner).
        const bool spawned = u.an.t.spawned;
        double hx = u.fx, hy = u.fy;
        bool home_ok = false;
        if (spawned && u.an.drop_left <= 0 && u.an.sortie != kParadrop) {
            const int ow = u.an.owner;
            if (ow >= 0 && ow < n && live(units_[ow])) {
                hx = units_[ow].fx; hy = units_[ow].fy; home_ok = true;
            }
        } else if (!spawned) {
            const int hb = u.an.home;
            if (hb >= 0 && hb < static_cast<int>(buildings_.size()) && buildings_[hb].alive &&
                buildings_[hb].state != Building::State::Dead && buildings_[hb].house == u.house) {
                const Building& b = buildings_[hb];
                if (u.an.pad >= 0 && u.an.pad < static_cast<int>(an_.dock_offsets.size())) {
                    hx = (b.cx * kLept + b.fw * 128.0 + an_.dock_offsets[u.an.pad].first) / kLept - 0.5;
                    hy = (b.cy * kLept + b.fh * 128.0 + an_.dock_offsets[u.an.pad].second) / kLept - 0.5;
                } else {
                    hx = b.cx + (b.fw - 1) / 2.0; hy = b.cy + (b.fh - 1) / 2.0;
                }
                home_ok = true;
            }
        }
        if (!spawned && !home_ok && u.an.sortie != kDocked) {
            // Lost its airfield: claim a free pad on another own Helipad.
            for (int bi = 0; bi < static_cast<int>(buildings_.size()) && !home_ok; ++bi) {
                const Building& b = buildings_[bi];
                if (!b.alive || b.house != u.house || b.state == Building::State::Dead) continue;
                if (b.type < 0 || b.type >= static_cast<int>(an_.airfield_btype.size()) ||
                    !an_.airfield_btype[b.type])
                    continue;
                for (int pad = 0; pad < static_cast<int>(an_.dock_offsets.size()); ++pad) {
                    bool used = false;
                    for (const Unit& o : units_)
                        if (&o != &u && live(o) && o.an.home == bi && o.an.pad == pad) used = true;
                    if (used) continue;
                    u.an.home = bi;
                    u.an.pad = pad;
                    home_ok = true;
                    break;
                }
            }
        }
        // --- state machine ---
        double goal_x = u.fx, goal_y = u.fy;
        bool fly = false;
        switch (u.an.sortie) {
            case kDocked:
                // Docked: rearm one ammo point every ReloadRate (270 frames).
                if (u.an.ammo < u.an.t.ammo && home_ok) {
                    if (++u.an.reload_timer >= std::max(1, P.reload_frames)) {
                        u.an.reload_timer = 0;
                        ++u.an.ammo;
                    }
                } else if (home_ok && !spawned && u.hp < u.max_hp) {
                    // W7-05-07 [RE: the airfield's UnitReload loop FUN_0044B780
                    // @0x44C84x, every ReloadRate x 900 frames (@0x44C93C): a docked
                    // plane is sent 0x1F (one ammo, FUN_006F4010) and, once full,
                    // 0x1C -- RepairStep hit points for GetRepairStepCost
                    // (FUN_00711470), refused (0x20) when the house cannot pay].
                    if (++u.an.reload_timer >= std::max(1, P.reload_frames)) {
                        u.an.reload_timer = 0;
                        const int h = u.house;
                        if (h >= 0 && h < house_count()) {
                            const int step = repair_rules_.repair_step > 0 ? repair_rules_.repair_step : P.repair_step;
                            const int cost = repair_step_cost(u.vet.cost > 0 ? u.vet.cost : u.max_hp, u.max_hp, step,
                                                              repair_rules_.repair_percent);
                            double& pool = econ_active(h) ? econ_[static_cast<std::size_t>(h)].credits
                                                          : house_credits_[static_cast<std::size_t>(h)];
                            if (pool >= static_cast<double>(cost)) {
                                pool -= static_cast<double>(cost);
                                u.hp = std::min(u.max_hp, u.hp + step);
                            }
                        }
                    }
                }
                break;
            case kOutbound: {
                if (!target_alive || (u.an.ammo <= 0 && u.an.strafe_left <= 0)) {
                    u.an.strafe_left = 0;
                    u.an.sortie = kReturn;
                    break;
                }
                goal_x = gx; goal_y = gy; fly = true;
                // Attack pass: in weapon range -> fire one ammo point.
                // W7-05-02 [RE: Mission_Attack FUN_00417FE0 status 3]: a plane
                // that is neither Fighter nor Strafe fires only when it is over
                // the target (within 0xF leptons; it turns to face it inside
                // 0x200); Fighter / Strafe planes go straight to status 4
                // (fire when GetFireError allows: in range, rearmed).
                const bool pass_ok = u.an.t.fighter || u.an.t.strafe ||
                                     cdist(u.fx, u.fy, gx, gy) * kLept <= 15.0;
                if (pass_ok && cdist(u.fx, u.fy, gx, gy) <= std::max(1.0, u.w_range) && u.rearm_frames <= 0.0 &&
                    u.altitude >= P.flight_level * 0.5) {
                    // W7-05-02 [RE: FUN_00417FE0 statuses 6..9]: a Strafe
                    // weapon's run fires FOUR more single shots, one per ROF,
                    // through Fire (+0x3CC) without the fired flag -- no Ammo.
                    const bool strafe_shot = u.an.strafe_left > 0;
                    // W4: a flying projectile (the Harrier / Black Eagle
                    // Maverick, the Hornet / Osprey bombs: ROT>0 homing) is
                    // launched behind the P2 firewall and hits at detonation.
                    // W6-04-01 [RE: AircraftClass::Mission_Attack FUN_00417FE0
                    // case 4: on GetFireError OK it sets the fired flag (+0x6C8)
                    // and calls Fire Burst (+0x9C) times in the same frame; the
                    // flag is consumed as ONE Ammo (+0x2FC) decrement]: Boris's
                    // MiG (Maverick3 Ammo=1 Burst=2) puts both missiles in one pass.
                    if (tu >= 0) {
                        const int slot = unit_weapon_for_unit(u, units_[tu]);
                        if (slot >= 0) {
                            activate_weapon_slot(u, slot, veterancy_enabled() ? &veterancy_rules() : nullptr);
                            for (int k = 0; k < (strafe_shot ? 1 : std::max(1, u.w6.burst)); ++k) {
                                if (k > 0 && !live(units_[tu])) break;
                                if (projectiles_enabled_ && u.w_proj.flies()) launch_projectile(i, tu, -1);
                                else if (u.w_cell_spread >= 1.0) apply_spread_damage(units_, occ_, u, units_[tu]);
                                else deliver_unit_hit(units_, occ_, i, tu);
                            }
                        }
                    } else if (tb >= 0) {
                        const Building& tbb = buildings_[tb];
                        const int slot = unit_weapon_for_building(u, tbb.armor_ix, tbb.bomb_frame >= 0,
                                                                  an_victim_naval_cell(tbb.cx + tbb.fw / 2, tbb.cy + tbb.fh / 2));
                        if (slot >= 0) {
                            activate_weapon_slot(u, slot, veterancy_enabled() ? &veterancy_rules() : nullptr);
                            for (int k = 0; k < (strafe_shot ? 1 : std::max(1, u.w6.burst)); ++k) {
                                if (k > 0 && (!buildings_[tb].alive || buildings_[tb].hp <= 0)) break;
                                if (projectiles_enabled_ && u.w_proj.flies()) launch_projectile(i, -1, tb);
                                else if (buildings_[tb].iron_left_frames <= 0.0 &&
                                         !w6_wall_building_hit(*this, units_, buildings_, tb, u.w6.wall_flags, u.w_damage))   // W6-04-04
                                    deliver_building_hit(units_, buildings_, i, tb);
                            }
                        }
                    }
                    u.rearm_frames = std::max(1, u.w_rof);
                    u.fire_flash = 0.4;
                    fire_events_.push_back({i, -1, tu, tb, u.w_active, ""});
                    if (strafe_shot) {
                        --u.an.strafe_left;
                    } else {
                        --u.an.ammo;
                        if (u.an.t.strafe) u.an.strafe_left = 4;
                    }
                    // W7-05-02 [RE: FUN_00417FE0 statuses 1/5/10]: a Fighter
                    // re-attacks and any other plane turns and fires again
                    // while it has Ammo; at 0 (and the strafe run done) it
                    // returns to base.
                    if (u.an.ammo <= 0 && u.an.strafe_left <= 0) u.an.sortie = kReturn;
                }
                break;
            }
            case kMoveTo:
                goal_x = u.an.attack_x; goal_y = u.an.attack_y; fly = true;
                if (u.an.attack_x < 0 || cdist(u.fx, u.fy, goal_x, goal_y) < 0.3) {
                    // [RE-FALLBACK: a fixed-wing move order flies to the point,
                    // then returns to its airfield -- it cannot hover.]
                    u.an.attack_x = u.an.attack_y = -1;
                    u.an.sortie = kReturn;
                }
                break;
            case kLoiter:
                // W7-06-03 [RE: SpawnManager AI FUN_006B68B0 cases 0/2]: a
                // launched spawn flies to the cell next to its owner the manager
                // named and waits there for the rest of the wing.
                goal_x = u.an.attack_x; goal_y = u.an.attack_y; fly = true;
                if (u.an.attack_x < 0) { goal_x = hx; goal_y = hy; }
                if (spawned && !home_ok) u.an.sortie = kReturn;   // owner lost: crashes below
                break;
            case kParadrop: {
                // W8-05-07 [RE: AircraftClass Mission_ParadropApproach
                // FUN_004158E0 (returns 3) / Mission_ParadropOverfly
                // FUN_00415960 (returns 5) / the jump FUN_00415C60]: the plane
                // flies at the drop site; inside ParadropRadius (Rules+0x54C)
                // Approach turns into Overfly and spends one pass (+0x6D3).
                // Overfly drops ONE trooper every 5 frames while the plane is
                // in the radius and in the playfield (FUN_00578020); a refused
                // jump is retried 5 frames later. Out of the radius it loops
                // back over the target (Approach) while passes remain, else it
                // leaves with whoever is still aboard (Mission Retreat). Every
                // jump resets the passes to 5. Team planes (W4-SQ-06) carry
                // their members; the superweapon plane counts drop_left.
                const bool aboard = (!u.cargo.empty() || u.an.drop_left > 0) && u.an.para_passes >= 0;
                goal_x = aboard ? u.an.drop_x : u.an.exit_x;
                goal_y = aboard ? u.an.drop_y : u.an.exit_y;
                fly = true;
                if (aboard) {
                    if (u.an.para_timer > 0 && --u.an.para_timer > 0) break;
                    const bool in_radius = cdist(u.fx, u.fy, u.an.drop_x, u.an.drop_y) * kLept <= P.paradrop_radius;
                    if (!u.an.para_overfly) {   // Approach
                        u.an.para_timer = 3;
                        if (in_radius) {
                            u.an.para_overfly = true;
                            --u.an.para_passes;
                        }
                        break;
                    }
                    u.an.para_timer = 5;        // Overfly
                    if (in_radius) {
                        if (cell_in_playfield(u.cell_x, u.cell_y)) an_para_jump(i);
                        break;
                    }
                    u.an.para_overfly = false;
                    if (u.an.para_passes <= 0) u.an.para_passes = -1;   // no pass left: leave
                } else if (cdist(u.fx, u.fy, goal_x, goal_y) < 0.5) {
                    units_[i].alive = false;   // left the map
                    units_[i].die_time = 1.0;
                    units_[i].deployed_away = true;   // W4-SQ-03: gone, not destroyed (no loss)
                    continue;
                }
                break;
            }
            case kReturn:
                if (!home_ok) {
                    // AirportBound=yes with no airfield (or a spawn whose carrier
                    // died): it crashes. W7-05-04/09 [RE: Enter_Idle_Mode
                    // FUN_004176F0 -> vtbl+0x3DC FUN_004DEAC0; KamikazeTracker
                    // FUN_0054E040]: a crash with its Strength left.
                    an_crash_alive(i);
                    break;
                }
                goal_x = hx; goal_y = hy; fly = true;
                if (cdist(u.fx, u.fy, hx, hy) < 0.25) {
                    if (spawned) {   // back aboard the carrier: rearm there
                        units_[i].alive = false;
                        units_[i].die_time = 1.0;
                        // W6-04-26: back aboard, not destroyed -- no unit lost on
                        // the score screen (the W4-SQ-03 deployed_away rule).
                        // W7-06-06: the owner's node takes it aboard (state 6,
                        // SpawnReloadRate) at its next manager AI tick.
                        units_[i].deployed_away = true;
                        continue;
                    }
                    u.an.sortie = kLanding;
                }
                break;
            case kLanding:
                goal_x = hx; goal_y = hy;
                u.fx = hx; u.fy = hy;
                break;
            default:
                break;
        }
        // --- altitude + translation ---
        // W7-05-02 FlyLocomotionClass [RE: movement FUN_004CD520]: the plane
        // moves ftol(type+0x678 x CurrentSpeed) leptons per frame along its
        // CURRENT body facing (@0x4CD9A4) while the FacingClass turns it at
        // ROT<<8 u16/frame toward the goal -- so it overflies and loops back
        // instead of reversing on the spot. Height: climb min(d, 20), descend
        // (cruise and landing) clamp(d/20, 20, 50) leptons/frame. The throttle
        // ramps 0.1/frame; an IsTakingOff plane gets no throttle until it is
        // at half the FlightLevel, and a plane not on an attack run slows over
        // its SlowdownDistance (FUN_004D00A0).
        const double fr = dt * 15.0;
        const double fl = P.flight_level;
        bool stopped = false;
        if (u.an.sortie == kLanding) {
            u.altitude = fly_altitude_step(u.altitude, 0.0, fr);
            u.flight_state = u.altitude > 0.0 ? FlightState::Descending : FlightState::Grounded;
            u.an.fly_throttle = 0.0;
            if (u.altitude <= 0.0) {
                u.an.sortie = kDocked;
                u.an.reload_timer = 0;
            }
        } else if (fly) {
            const bool taking_off = u.flight_state == FlightState::TakingOff ||
                                    u.flight_state == FlightState::Grounded;
            u.altitude = fly_altitude_step(u.altitude, fl, fr);
            if (u.altitude >= fl) u.flight_state = FlightState::Cruising;
            else if (taking_off) u.flight_state = FlightState::TakingOff;
            const double dist = cdist(u.fx, u.fy, goal_x, goal_y) * kLept;
            const bool slow = u.an.sortie == kReturn || u.an.sortie == kMoveTo;
            double tgt = fly_throttle_target(dist, u.an.t.slowdown_distance, slow, &stopped);
            if (stopped) u.an.fly_throttle *= 0.5;            // DAT_007E0738 = 0.5
            if (u.flight_state == FlightState::TakingOff && u.altitude < fl / 2.0) tgt = 0.0;
            u.an.fly_throttle = fly_throttle_ramp(u.an.fly_throttle, tgt, fr);
            if (dist > 0.0)
                u.body_facing = heading_toward(u.body_facing, goal_x - u.fx, goal_y - u.fy,
                                               u.flight_turn * dt, false);
            const double full = u.an.t.fly_speed_cps * kLept / 15.0;   // type+0x678 (vet-scaled)
            const double step = std::trunc(full * u.an.fly_throttle) * fr;
            step_along_facing(u.fx, u.fy, u.body_facing, step);
            // The plane may loop outside the map edge; it never leaves the grid.
            u.fx = std::clamp(u.fx, -4.0, static_cast<double>(grid_w_ + 3));
            u.fy = std::clamp(u.fy, -4.0, static_cast<double>(grid_h_ + 3));
            // Arrival at a POINT the plane has slowed to a stop near (it can
            // stop just outside the snap radius) counts as reached.
            if ((u.an.sortie == kReturn || u.an.sortie == kMoveTo) && stopped &&
                u.an.fly_throttle < 0.05) {
                if (u.an.sortie == kReturn) {
                    u.fx = hx;
                    u.fy = hy;
                } else {
                    u.an.attack_x = u.an.attack_y = -1;
                    u.an.sortie = kReturn;
                }
            }
        }
        u.cell_x = std::max(0, std::min(grid_w_ - 1, static_cast<int>(om::lround(u.fx))));
        u.cell_y = std::max(0, std::min(grid_h_ - 1, static_cast<int>(om::lround(u.fy))));
        if (fly) {
            const double a = u.body_facing / 65536.0 * 6.283185307179586;
            const int f = facing_from_delta(om::sin(a), -om::cos(a));
            if (f >= 0) u.facing = f;
        }
    }
}

// --------------------------------------------------------- death variants ---

void World::an_step_deaths(double dt) {
    const SwParams& P = an_.sw;
    const int n = static_cast<int>(units_.size());
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.alive || u.die_time < 0.0) continue;
        // W4: a rocket killed in flight (FUN_00661D00: Strength < 1 ->
        // FUN_00662A70) -- shot down; its launcher may rebuild it
        // (SpawnRegenRate counts it lost, not out).
        if (u.an.t.missile) {
            if (u.an.rocket_end == 0) {
                // FUN_00661D00 tail: Strength < 1 -> FUN_00662A70 at the rocket.
                rocket_explode(i, 2);
            }
            continue;
        }
        // W2-AN-07 [RE: aircraft.md §2 Crashable +0xD95; JumpjetCrash]: a
        // Crashable flier (and every fixed-wing aircraft) killed in the air
        // falls at its crash rate, then explodes where it hits the ground.
        // W8-05-04: a crash that came to rest on a high-bridge deck (below) is over.
        if (u.is_aircraft && !u.an.crashing && u.flight_state == FlightState::Grounded &&
            u.altitude == 416.0 && high_bridge_cell(u.cell_x, u.cell_y))
            continue;
        if (u.is_aircraft && u.altitude > 0.0 && (u.an.t.crashable || u.an.t.fixed_wing) &&
            !u.an.t.missile) {
            u.an.crashing = true;
            u.die_time = 0.0;   // hold the death anim until impact
            const double rate = u.descend_rate > 0.0 ? u.descend_rate : kFlyClimbLps;
            const double alt0 = u.altitude;
            u.altitude = std::max(0.0, u.altitude - std::max(rate, 12.0 * 15.0) * dt);
            u.flight_state = FlightState::Descending;
            u.has_flight_goal = false;
            if (u.an.t.fixed_wing) {   // keeps gliding forward while falling
                const double a = u.body_facing / 65536.0 * 2.0 * 3.14159265358979;
                u.fx += om::sin(a) * u.an.t.fly_speed_cps * 0.5 * dt;
                u.fy -= om::cos(a) * u.an.t.fly_speed_cps * 0.5 * dt;
                u.cell_x = std::max(0, std::min(grid_w_ - 1, static_cast<int>(om::lround(u.fx))));
                u.cell_y = std::max(0, std::min(grid_h_ - 1, static_cast<int>(om::lround(u.fy))));
            } else {
                // W7-05-15 [RE: Jumpjet Crashing FUN_0054C7C0 -- desired facing
                // = current + 0x2D00 every frame, so it spins at its
                // JumpJetTurnRate<<8; the integrator FUN_0054CDA0 keeps moving
                // ftol(CurSpeed) leptons along the facing while it falls].
                const double fr = dt * 15.0;
                u.body_facing = std::fmod(u.body_facing + std::min(u.flight_turn / 15.0, 11520.0) * fr,
                                          65536.0);
                step_along_facing(u.fx, u.fy, u.body_facing, std::trunc(u.an.jj_speed) * fr);
                u.cell_x = std::max(0, std::min(grid_w_ - 1, static_cast<int>(om::lround(u.fx))));
                u.cell_y = std::max(0, std::min(grid_h_ - 1, static_cast<int>(om::lround(u.fy))));
            }
            // W8-05-04 [RE: Jumpjet Crashing FUN_0054C7C0]: a falling jumpjet
            // that was above an intact high-bridge deck stops ON it (bridge
            // flag, old Z >= floor + BridgeHeight, new Z below it) and fires its
            // death weapon (message 0x117C) at the deck's Z -- a Kirov no
            // longer falls through the deck to the river.
            bool deck_hit = false;
            if (!u.an.t.fixed_wing && high_bridge_cell(u.cell_x, u.cell_y) &&
                !high_bridge_destroyed(u.cell_x, u.cell_y) && alt0 >= 416.0 && u.altitude < 416.0) {
                u.altitude = 416.0;
                deck_hit = true;
            }
            if (u.altitude <= 0.0 || deck_hit) {
                u.an.crashing = false;
                u.flight_state = FlightState::Grounded;
                const double fx = u.fx, fy = u.fy;
                const int crash_z = cell_level(u.cell_x, u.cell_y) * 104 + static_cast<int>(u.altitude);
                if (u.an.crash_hp > 0) {
                    // W7-05-04/09 [RE: FlyLoco FUN_004CD520 @0x4CD7C9: Strength
                    // left at impact -> ReceiveDamage(own Strength, Rules+0xFA8)
                    // and DamageArea FUN_00489190(coords, that Strength, C4Warhead)].
                    WeaponSpec c4;
                    c4.valid = true;
                    c4.damage = u.an.crash_hp;
                    c4.verses = P.c4_verses;
                    c4.cell_spread = P.c4_spread;
                    c4.percent_at_max = P.c4_pmax;
                    c4.inf_death = 2;
                    const int dmg = u.an.crash_hp;
                    u.an.crash_hp = 0;
                    ability_area_damage(i, fx, fy, c4, dmg, crash_z);
                } else if (u.an.t.fixed_wing || (u.balloon_hover && !u.is_infantry)) {
                    // W7-05-09 [RE: shot down -> FireDeathWeapon FUN_0070CAC0
                    // (planes @0x4CD729; BalloonHover jumpjets on message 0x117C,
                    // FUN_00744F30 @0x745001): DeathWeapon, else the Primary, x
                    // DeathWeaponDamageModifier, its warhead's DamageArea].
                    if (u.an.t.crash_weapon.valid && u.an.t.crash_weapon.damage > 0) {
                        const WeaponSpec w = u.an.t.crash_weapon;
                        ability_area_damage(i, fx, fy, w, w.damage, crash_z);
                    }
                }
                // (W7-05-09 [RE: FUN_00744F30 / FUN_0052283B]: a non-BalloonHover
                // jumpjet (Night Hawk, Siege Chopper) only plays its Explosion
                // anims (FUN_007376C0); the infantry Rocketeer just dies.)
            }
            continue;
        }
        // W2-AN-25 [rules [General] ShipSinkingWeight=3.0: "Surface ships of
        // this or greater weight will sink, not explode"].
        if (u.is_naval && !u.an.t.underwater && u.an.t.weight >= P.ship_sinking_weight)
            u.an.sinking = true;
    }
}

// W7-05-04 [RE: AircraftClass vtbl+0x3DC = FUN_004DEAC0, called by
// Enter_Idle_Mode FUN_004176F0 when no dock is found for an AirportBound plane,
// and by the KamikazeTracker FUN_0054E040 for a spawn whose owner died]: the
// aircraft falls with its Strength left; the impact deals that Strength
// (an_step_deaths). The clone starts the death now and remembers the Strength.
void World::an_crash_alive(int idx) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& u = units_[idx];
    if (!u.alive || u.die_time >= 0.0) return;
    u.an.crash_hp = std::max(1, u.hp);
    u.hp = 0;
    u.die_time = 0.0;
    u.vet.killed_by = -1;
    occ_.release_all_of(idx, -1, -1);
    u.target = -1;
    u.target_building = -1;
    u.path.clear();
    u.path_idx = 0;
    if (u.altitude <= 0.0) u.altitude = 1.0;   // falls (the crash path needs height)
}

// ------------------------------------------------------------- paradrop ----

void World::an_paradrop(int house, SuperweaponType type, int cx, int cy) {
    const SwParams& P = an_.sw;
    std::string inf;
    int num = 0;
    if (type == SuperweaponType::AmerParaDrop) {
        inf = P.amer_para_inf;
        num = P.amer_para_num;
    } else {
        const bool soviet = house_side(house) == Side::Soviet;
        inf = soviet ? P.sov_para_inf : P.ally_para_inf;
        num = soviet ? P.sov_para_num : P.ally_para_num;
    }
    // [RE-FALLBACK: the entry edge] the plane enters from the map edge nearest
    // the drop site along x and leaves across the map on the same heading.
    const bool from_left = cx < grid_w_ / 2;
    const int sx = from_left ? 0 : grid_w_ - 1;
    const int ex = from_left ? grid_w_ - 1 : 0;
    const int pi = an_spawn("PDPLANE", house, sx, cy);
    if (pi < 0) {
        // No plane art/type: drop straight onto the site.
        for (int k = 0; k < num; ++k) {
            bool placed = false;
            for (int r = 0; r <= 4 && !placed; ++r)
                for (int oy = -r; oy <= r && !placed; ++oy)
                    for (int ox = -r; ox <= r && !placed; ++ox) {
                        if (std::max(std::abs(ox), std::abs(oy)) != r) continue;
                        if (!walkable(cx + ox, cy + oy) || !occ_.is_free(cx + ox, cy + oy)) continue;
                        const int qi = an_spawn(inf, house, cx + ox, cy + oy);
                        placed = qi >= 0;
                        if (placed) para_start(qi, an_.sw.flight_level);   // W7-01-07
                    }
        }
        return;
    }
    Unit& p = units_[pi];
    p.an.loaner = true;   // W7-05-04 [RE: 0x65E0FE..0x65E550: techno +0x3D4 = 1]
    p.fx = sx;
    p.fy = cy;
    p.altitude = an_.sw.flight_level;
    p.flight_state = FlightState::Cruising;
    p.an.sortie = kParadrop;
    p.an.drop_left = num;
    p.an.drop_type = inf;
    p.an.drop_x = cx;
    p.an.drop_y = cy;
    p.an.exit_x = ex;
    p.an.exit_y = cy;
    if (!an_.types.count(inf)) {
        // the infantry type must be registered for an_spawn
        p.an.drop_left = 0;
    }
}

// ------------------------------------------------- W4-SQ-06 team paradrop --

int World::spawn_drop_plane(const std::string& plane_id, int house, int sx, int sy, int dx, int dy,
                            int ex, int ey) {
    const int pi = an_spawn(plane_id, house, sx, sy);
    if (pi < 0) return -1;
    Unit& p = units_[pi];
    p.an.loaner = true;   // W7-05-04 [RE: FUN_0065D320 @0x65D632: techno +0x3D4 = 1]
    p.fx = p.cell_x;
    p.fy = p.cell_y;
    p.altitude = an_.sw.flight_level;
    p.flight_state = FlightState::Cruising;
    p.an.sortie = kParadrop;
    p.an.drop_left = 0;
    p.an.drop_type.clear();
    p.an.drop_x = std::max(0, std::min(grid_w_ - 1, dx));
    p.an.drop_y = std::max(0, std::min(grid_h_ - 1, dy));
    p.an.exit_x = std::max(0, std::min(grid_w_ - 1, ex));
    p.an.exit_y = std::max(0, std::min(grid_h_ - 1, ey));
    const int f = facing_from_delta(dx - sx, dy - sy);
    if (f >= 0) p.facing = f;
    p.body_facing = facing_u16_from_delta(dx - sx, dy - sy);
    return pi;
}

// W4-SQ-03 [RE: AircraftClass mission FUN_004176F0 -- an AirportBound
// aircraft that finds no free dock (vtbl +0x528) takes the no-dock exit
// (vtbl +0x3DC), and an emptied team transport plane with no team is put on
// Mission Retreat (4); RE-FALLBACK: both leave over the nearest map edge]:
// a fixed-wing aircraft with nowhere to land flies off the map.
bool World::order_leave_map(int idx) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return false;
    Unit& u = units_[idx];
    if (!u.alive || u.die_time >= 0.0 || !u.is_aircraft || !u.an.t.fixed_wing || u.an.t.spawned) return false;
    // W7-05-04 [RE: Enter_Idle_Mode FUN_004176F0]: only a loaner (techno
    // +0x3D4, set by the reinforcement / superweapon plane code) takes Mission
    // Retreat (FUN_00415A50) and leaves the map; an ordinary AirportBound plane
    // that finds no dock crashes (vtbl+0x3DC), in the campaign as in skirmish.
    if (!u.an.loaner && u.an.t.airport_bound) {
        an_.any = true;
        an_crash_alive(idx);
        return true;
    }
    const int W = grid_w_, H = grid_h_;
    const int dl = u.cell_x, dr = W - 1 - u.cell_x, dt = u.cell_y, db = H - 1 - u.cell_y;
    int ex = u.cell_x, ey = u.cell_y;
    const int m = std::min(std::min(dl, dr), std::min(dt, db));
    if (m == dl) ex = 0; else if (m == dr) ex = W - 1; else if (m == dt) ey = 0; else ey = H - 1;
    an_.any = true;
    u.target = -1;
    u.target_building = -1;
    u.attack_order = false;
    u.path.clear();
    u.path_idx = 0;
    u.has_flight_goal = false;
    u.an.attack_unit = u.an.attack_building = -1;
    u.an.sortie = kParadrop;   // the straight run off the map (no cargo, nothing to drop)
    u.an.drop_left = 0;
    u.an.drop_type.clear();
    u.an.drop_x = u.cell_x;
    u.an.drop_y = u.cell_y;
    u.an.exit_x = ex;
    u.an.exit_y = ey;
    if (u.flight_state == FlightState::Grounded) {
        occ_.release_all_of(idx, -1, -1);
        u.flight_state = FlightState::TakingOff;
    }
    return true;
}

// W7-01-07: a spawned superweapon paratrooper starts under its parachute at
// `alt` leptons over its landing cell (which it keeps occupied).
void World::para_start(int ui, double alt) {
    if (ui < 0 || ui >= static_cast<int>(units_.size())) return;
    Unit& u = units_[static_cast<std::size_t>(ui)];
    u.cargo_of = kParachuteCargo;
    u.an.para_alt = std::max(1.0, alt);
    u.ia.seq = -1;   // W3-B: the machine restarts on landing
}

// W8-05-07 [RE: FUN_00415C60, the paradrop plane's one jump]: the next
// trooper leaves the plane at the plane's own position, 128 leptons
// (DAT_007E1808) off its heading -- to the right (facing + 0x3FFF) when the
// count left aboard is even, else to the left (0x415CAC) -- and falls onto
// the cell under that point. The jump is refused (the trooper stays aboard
// for the next Overfly tick) when the trooper cannot enter the cell
// (Can_Enter_Cell vtbl+0x1AC: water, cliff, building), when the cell has no
// free infantry spot (FindInfantrySubposition FUN_00481090: a vehicle there
// or all three spots taken; the spot is chosen by the quadrant of the jump
// point) or when it is outside the playfield (SpawnParachuted FUN_005F5150).
// SpawnParachuted puts a trooper landing on a bridge cell on the deck
// (OnBridge +0x8C). Every jump resets the plane's passes to 5 (0x415E93).
// Replaces the R-038 RE-FALLBACK (one trooper per frame into the first free
// whole cell around the target).
bool World::an_para_jump(int i) {
    if (i < 0 || i >= static_cast<int>(units_.size())) return false;
    const Unit& pl = units_[static_cast<std::size_t>(i)];
    const bool team = !pl.cargo.empty();
    const int left = (team ? static_cast<int>(pl.cargo.size()) : pl.an.drop_left) - 1;   // +0x2FC after the DEC
    if (left < 0) return false;
    double jx = pl.fx, jy = pl.fy;
    step_along_facing(jx, jy, pl.body_facing + ((left & 1) ? -16383.0 : 16383.0), 128.0);
    const int cx = static_cast<int>(om::lround(jx)), cy = static_cast<int>(om::lround(jy));
    if (cx < 0 || cy < 0 || cx >= grid_w_ || cy >= grid_h_) return false;
    if (!walkable(cx, cy) || building_at_cell(cx, cy) >= 0 || !cell_in_playfield(cx, cy)) return false;
    const int who = team ? pl.cargo.front() : static_cast<int>(units_.size());
    if (team && (who < 0 || who >= static_cast<int>(units_.size()))) {
        units_[static_cast<std::size_t>(i)].cargo.erase(units_[static_cast<std::size_t>(i)].cargo.begin());
        return false;
    }
    // FindInfantrySubposition: the quadrant of the jump point in its cell.
    const bool inf = team ? occ_.is_infantry(who) : occ_.subcells();
    int spot = -1;
    if (inf) {
        const int ox = static_cast<int>(om::lround((jx - cx) * kLept));
        const int oy = static_cast<int>(om::lround((jy - cy) * kLept));
        spot = occ_.free_spot(cx, cy, spot_quadrant(ox, oy), who & 3);
        if (spot < 0) return false;
    } else if (!occ_.is_free(cx, cy)) {
        return false;
    }
    const int house = pl.house;
    const double alt = pl.altitude;
    int p = who;
    if (team) {
        Unit& plane = units_[static_cast<std::size_t>(i)];
        plane.cargo.erase(plane.cargo.begin());
        Unit& u = units_[static_cast<std::size_t>(p)];
        u.cell_x = cx;
        u.cell_y = cy;
        u.fx = cx;
        u.fy = cy;
        if (const ra2assets::MapCell* mc = map_ ? map_->at(cx, cy) : nullptr) u.level = mc->level;
        if (spot >= 0) occ_.set_spot(cx, cy, p, spot);
        else occ_.set(cx, cy, p);   // the landing cell is taken from the jump on
    } else {
        const std::string type = pl.an.drop_type;
        p = an_spawn(type, house, cx, cy);
        if (p < 0) return false;
        if (spot >= 0) place_in_spot(p, spot);
    }
    Unit& u = units_[static_cast<std::size_t>(p)];
    u.guard_x = cx;
    u.guard_y = cy;
    // SpawnParachuted @0x5F5150: a bridge cell puts it on the deck.
    if (high_bridge_cell(cx, cy) && !high_bridge_destroyed(cx, cy)) u.on_deck = 1;
    para_start(p, alt);
    Unit& plane = units_[static_cast<std::size_t>(i)];
    if (!team) --plane.an.drop_left;
    plane.an.para_passes = 5;
    an_event(SwEvent::Dropped, house, SuperweaponType::ParaDrop, cx, cy);
    return true;
}

void World::an_step_parachutes(double dt) {
    const double fall = std::max(1, an_.sw.parachute_fall) * 15.0 * dt;
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[i];
        if (u.cargo_of != kParachuteCargo) continue;
        if (!u.alive) { u.cargo_of = -1; u.an.para_alt = 0.0; continue; }
        u.an.para_alt -= fall;
        // The art Paradrop= sequence while it hangs under the chute (W3-B
        // tables; the stage cycles at the DoControls rate).
        if (const InfTable* it = inf_table(u.image)) {
            const InfSeqEntry& e = it->e[kSqParadrop];
            if (e.has) {
                const int n = std::max(1, e.count);
                const int stage = om::ftol((u.an.para_alt / std::max(1.0, fall)) / 2.0) % n;
                u.frame = e.first + (e.fmult ? ((u.facing % 8 + 8) % 8) * e.fmult : 0) + (n - 1 - stage);
            }
        }
        if (u.an.para_alt > 0.0) continue;
        u.an.para_alt = 0.0;
        u.cargo_of = -1;   // touched down: an ordinary unit on its cell
        if (occ_.at(u.cell_x, u.cell_y) != i && occ_.spot_of(u.cell_x, u.cell_y, i) < 0) {   // W8-05-07: spot holders share a cell
            if (occ_.is_free(u.cell_x, u.cell_y)) occ_.set(u.cell_x, u.cell_y, i);
            else {
                const std::vector<Cellxy> g = nearest_free_cells({u.cell_x, u.cell_y}, 1, passable_, grid_w_,
                                                                 grid_h_, occ_);
                if (!g.empty()) {
                    u.cell_x = g[0].x;
                    u.cell_y = g[0].y;
                    u.fx = u.cell_x;
                    u.fy = u.cell_y;
                    occ_.set(u.cell_x, u.cell_y, i);
                }
            }
        }
        u.guard_x = u.cell_x;
        u.guard_y = u.cell_y;
    }
}

// ---------------------------------------------------------- AI naval yard --

bool World::ai_naval_spot_ok(int house, const HouseAiRec& rec, const BuildingTypeDef& t, int cx,
                             int cy) const {
    if (water_passable_.empty()) return false;
    for (const Cellxy& c : foundation_cells(cx, cy, t.fw, t.fh)) {
        if (c.x < 0 || c.y < 0 || c.x >= grid_w_ || c.y >= grid_h_) return false;
        if (!water_passable_[static_cast<std::size_t>(c.y) * grid_w_ + c.x]) return false;
        if (building_at_cell(c.x, c.y) >= 0 || !occ_.is_free(c.x, c.y)) return false;
    }
    const int adj = rec.cfg.catalog.naval_yard_adjacency;
    for (const Building& b : buildings_) {
        if (!b.alive || b.house != house || b.state == Building::State::Dead) continue;
        if (b.type < 0 || b.type >= static_cast<int>(rec.cfg.roster.size()) ||
            !rec.cfg.roster[b.type].conyard)
            continue;
        if (std::max(std::abs(cx - b.cx), std::abs(cy - b.cy)) <= adj) return true;
    }
    return false;
}

Cellxy World::ai_naval_spot_scan(int house, const HouseAiRec& rec, const BuildingTypeDef& t) const {
    int ax = -1, ay = -1;
    for (const Building& b : buildings_)
        if (b.alive && b.house == house && b.state != Building::State::Dead && b.type >= 0 &&
            b.type < static_cast<int>(rec.cfg.roster.size()) && rec.cfg.roster[b.type].conyard) {
            ax = b.cx;
            ay = b.cy;
            break;
        }
    if (ax < 0) return Cellxy{-1, -1};
    const int adj = rec.cfg.catalog.naval_yard_adjacency;
    for (int r = 1; r <= adj; ++r)   // nearest ring first, deterministic order
        for (int y = ay - r; y <= ay + r; ++y)
            for (int x = ax - r; x <= ax + r; ++x) {
                if (std::max(std::abs(x - ax), std::abs(y - ay)) != r) continue;
                if (ai_naval_spot_ok(house, rec, t, x, y)) return Cellxy{x, y};
            }
    return Cellxy{-1, -1};
}

// ------------------------------------------------------- W7-06-10 sonic ---

// [RE: WaveClass type 0 (sonic) -- ctor FUN_0075D770 from TechnoClass::Fire
// @0x6FE930 when the weapon IsSonic (+0x130); AI FUN_0075FD70 case 0 runs the
// sweep FUN_00761910 then damages every cell of the wave (FUN_0075E150) each
// frame, the wave deleted after 100 frames at the latest]:
//   - the front (+0x138) advances 0.05 of the firer->target line a frame
//     (DAT_007F5BB0), snapped to 1.0 past 0.98, while the firer still holds
//     that target (+0x2B4) within 6 x 362 leptons (DAT_00B44CF0 * 6.0);
//   - the back (+0x140) starts once the front is 0.1 ahead and follows at
//     the same rate while it is no more than 0.025 behind the front; the wave
//     ends when the back reaches the front;
//   - the cells under the line from the back to the front + 0.025, sampled
//     every 0.05 (FUN_0075FF10), each take ReceiveDamage(AmbientDamage (+0x98)
//     of the firer's weapon 0, its warhead) for everything in them except the
//     firer -- own units included.
// [RE-FALLBACK: the wave's second, parallel cell line (FUN_0075FF10 for a
// direction class > 3) and the aircraft layer are not swept.]
void World::an_step_waves() {
    const int n = static_cast<int>(units_.size());
    // New waves: a sonic weapon fired this frame (fire_flash stamped 0.4).
    for (int i = 0; i < n; ++i) {
        const Unit& u = units_[i];
        if (u.an.t.sonic_ambient <= 0 || !live(u) || u.fire_flash < 0.4 - 1e-9 || u.w_active != 0) continue;
        SonicWave w;
        w.firer = i;
        w.target_unit = u.target;
        w.target_bld = u.target < 0 ? u.target_building : -1;
        double tx = 0, ty = 0, tz = 0;
        if (!rocket_target_point(w.target_unit, w.target_bld, tx, ty, tz)) continue;
        w.sx = u.fx;
        w.sy = u.fy;
        w.tx = tx;
        w.ty = ty;
        w.damage = is_elite(u.vet.veterancy) ? u.an.t.sonic_ambient_elite : u.an.t.sonic_ambient;
        w.verses = u.w_verses;
        w.sonic_wh = u.w6.sonic;
        an_.waves.push_back(w);
    }
    if (an_.waves.empty()) return;
    std::vector<SonicWave> keep;
    keep.reserve(an_.waves.size());
    for (SonicWave& w : an_.waves) {
        // FUN_00761910: start the back once the front is 0.1 ahead.
        if (w.head - w.tail > 0.1) w.tail_moving = true;
        const bool firer_ok = w.firer >= 0 && w.firer < n && live(units_[w.firer]);
        if (w.growing) {
            bool hold = firer_ok;
            double tx = 0, ty = 0, tz = 0;
            if (hold) {
                const Unit& f = units_[w.firer];
                hold = (w.target_unit >= 0 ? f.target == w.target_unit : f.target_building == w.target_bld) &&
                       rocket_target_point(w.target_unit, w.target_bld, tx, ty, tz) &&
                       cdist(f.fx, f.fy, tx, ty) * kLept <= 362.038672 * 6.0;
                if (hold) {   // FUN_00760460: the line follows the firer and the target
                    w.sx = f.fx;
                    w.sy = f.fy;
                    w.tx = tx;
                    w.ty = ty;
                }
            }
            if (!hold) {
                w.growing = false;
                w.tail_moving = true;
            }
        }
        if (w.head < 1.0 && w.growing) {
            w.head += 0.05;
            if (w.head > 0.98) w.head = 1.0;
        }
        if (w.tail_moving && w.tail <= w.head + 0.025) {
            w.tail += 0.05;
            if (w.head <= w.tail) continue;   // the wave is gone
        }
        // FUN_0075FF10: the cells under the line, back -> front + 0.025.
        std::vector<std::pair<int, int>> cells;
        for (double t = w.tail; t <= w.head + 0.025 + 1e-9; t += 0.05) {
            const int cx = static_cast<int>(om::lround(w.sx + (w.tx - w.sx) * t));
            const int cy = static_cast<int>(om::lround(w.sy + (w.ty - w.sy) * t));
            if (cx < 0 || cy < 0 || cx >= grid_w_ || cy >= grid_h_) continue;
            if (std::find(cells.begin(), cells.end(), std::make_pair(cx, cy)) == cells.end())
                cells.emplace_back(cx, cy);
        }
        // FUN_0075E150: everything in each cell but the firer.
        for (const auto& c : cells) {
            for (int j = 0; j < n; ++j) {
                Unit& v = units_[j];
                if (j == w.firer || !live(v) || v.cell_x != c.first || v.cell_y != c.second) continue;
                if (unit_airborne(v)) continue;
                const int dmg = damage_vs(w.damage, w.verses, v.armor_ix);
                if (dmg <= 0) continue;
                if (w.sonic_wh && v.parasite_on_me >= 0) v.w6.sonic_hit = true;   // W6-04-09
                const int hp0 = v.hp;
                an_damage_unit(j, dmg, w.firer);
                feed_damage(false, j, w.firer >= 0 && w.firer < n ? units_[w.firer].house : -1, hp0 - units_[j].hp);
            }
            const int b = building_at_cell(c.first, c.second);
            if (b >= 0) {
                const Building& bb = buildings_[static_cast<std::size_t>(b)];
                const int dmg = damage_vs(w.damage, w.verses, bb.armor_ix);
                const int hp0 = bb.hp;
                an_damage_building(b, dmg);
                feed_damage(true, b, w.firer >= 0 && w.firer < n ? units_[w.firer].house : -1,
                            hp0 - buildings_[static_cast<std::size_t>(b)].hp);
            }
        }
        keep.push_back(std::move(w));
    }
    an_.waves.swap(keep);
}

// ------------------------------------------- W7-06-11 DistributedFire -----

// [RE: TechnoClass::Fire FUN_006FD210 @0x6FED3C: after a DistributedFire shot
// (TechnoType +0x6B0) outside a forced Attack, the target joins the "already
// shot" list (+0x470..+0x484) and the unit drops it (SetTarget(0));
// FUN_007089D0 then picks the next one from the candidates the scan
// collected (CanAutoTarget FUN_006F8320 +0x440/+0x458): the best one not shot
// yet; when every candidate was shot the list is cleared and the best of all
// is taken.] [RE-FALLBACK: the threat value of FUN_006F71D0 is not ported --
// the nearest candidate stands for the best one.]
void World::an_step_distributed_fire() {
    const int n = static_cast<int>(units_.size());
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.an.t.distributed_fire || !live(u)) continue;
        if (u.attack_order) {   // Mission Attack: the forced target is kept
            u.an.shot_list.clear();
            continue;
        }
        if (u.fire_flash >= 0.4 - 1e-9 && u.target >= 0) {
            if (std::find(u.an.shot_list.begin(), u.an.shot_list.end(), u.target) == u.an.shot_list.end())
                u.an.shot_list.push_back(u.target);
            u.target = -1;
        }
        // The candidates: enemies this unit could fire on now.
        std::vector<int> cand;
        for (int j = 0; j < n; ++j) {
            const Unit& v = units_[j];
            if (j == i || !live(v) || !is_enemy(u.house, v.house)) continue;
            if (!object_visible_at(u.house, v.cell_x, v.cell_y)) continue;
            const int slot = unit_weapon_for_unit(u, v);
            if (slot < 0) continue;
            const double rng = slot == 1 ? u.w2.range : (u.w_active == 0 ? u.w_range : u.vet.base.range);
            if (cdist(u.fx, u.fy, v.fx, v.fy) > rng + 1e-9) continue;
            cand.push_back(j);
        }
        std::vector<int>& shot = u.an.shot_list;
        shot.erase(std::remove_if(shot.begin(), shot.end(),
                                  [&](int t) { return std::find(cand.begin(), cand.end(), t) == cand.end(); }),
                   shot.end());
        if (cand.empty()) {
            shot.clear();
            continue;
        }
        int best = -1;
        double bd = 0.0;
        for (int j : cand) {
            if (std::find(shot.begin(), shot.end(), j) != shot.end()) continue;
            const double d = cdist(u.fx, u.fy, units_[j].fx, units_[j].fy);
            if (best < 0 || d < bd) { best = j; bd = d; }
        }
        if (best < 0) {   // every candidate shot: start over
            shot.clear();
            continue;
        }
        if (u.target < 0 || std::find(shot.begin(), shot.end(), u.target) != shot.end()) {
            u.target = best;
            u.target_building = -1;
        }
    }
}

// ------------------------------------------------------------------ driver --

void World::step_air_naval(double dt) {
    if (!an_.any) {
        // Open the firewall lazily: any unit carrying W2-AN type data.
        for (const Unit& u : units_)
            if (u.an.t.underwater || u.an.t.sensors || u.an.t.spawner_slot >= 0 || u.an.t.fixed_wing ||
                u.an.t.crashable || (u.is_naval && u.an.t.weight > 1.0) || u.an.iron_left_frames > 0.0 ||
                u.an.t.distributed_fire || u.an.t.sonic_ambient > 0 ||   // W7-06-10/11
                u.movement_domain == MovementDomain::Amphibious) {
                an_.any = true;
                break;
            }
        if (!an_.any) return;
    }
    ++an_.frame;
    an_step_naval(dt);
    an_step_waves();             // W7-06-10
    an_step_distributed_fire();  // W7-06-11
    an_step_spawners(dt);
    an_step_fixed_wing(dt);
    an_step_parachutes(dt);   // W4-SQ-06
    an_step_deaths(dt);
    // Night Hawk (W2-AN-08) [RE: aircraft.md §2, BalloonHover +0xD6A]: a
    // non-BalloonHover Landable jumpjet with no goal and no target lands.
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[i];
        // W7-05-13 [RE: FootClass Mission_Attack FUN_004D4CE0 / Guard
        // FUN_004D4F90: a HoverAttack (+0x390) unit at height 0 with a target
        // gets SetDestination(its own cell) -- it lifts off and fights hovering].
        if (u.an.t.hover_attack && u.is_aircraft && !u.an.t.fixed_wing && live(u) &&
            u.flight_state == FlightState::Grounded && (u.target >= 0 || u.target_building >= 0)) {
            occ_.release_all_of(i, -1, -1);
            u.flight_state = FlightState::TakingOff;
            u.flight_goal_x = u.cell_x;
            u.flight_goal_y = u.cell_y;
            u.has_flight_goal = true;
            u.an.landing = false;
            aircraft_any_ = true;
        }
        if (!u.is_aircraft || u.an.t.fixed_wing || u.balloon_hover || !u.an.t.landable) continue;
        if (!live(u)) continue;
        if (u.flight_state == FlightState::Cruising && !u.has_flight_goal && u.target < 0 &&
            u.target_building < 0) {
            // W8-05-01 [RE: JumpjetLocomotionClass Descending FUN_0054C280
            // @0x54C2AA..0x54C38E]: on a cell whose LandType is Water (2) or
            // Beach (6) -- a high-bridge deck over the river keeps the tile's
            // Water (BRIDGE1/2, BRIDGEB1 NoUseTileLandType=false) -- and not
            // on an Enter mission, the jumpjet claims the cell and HOVERS at
            // full height: it never lands there and never looks for land.
            // Unloading there waits too (UnitClass Mission_Unload FUN_0073C560
            // case 0 returns while the locomotor IsMoving +0x4C, which only a
            // touch-down clears).
            if (an_victim_naval_cell(u.cell_x, u.cell_y)) { u.an.landing = false; continue; }
            // Elsewhere: Can_Enter_Cell (vtbl+0x1AC; SpeedType=Hover cannot
            // enter Rock/Wall) and a free spot decide (@0x54C38F..0x54C44A).
            auto pad_ok = [&](int x, int y) {
                return walkable(x, y) && !an_victim_naval_cell(x, y) && occ_.is_free(x, y) &&
                       building_at_cell(x, y) < 0;
            };
            if (pad_ok(u.cell_x, u.cell_y)) {
                u.flight_state = FlightState::Descending;
                u.an.landing = true;
                aircraft_any_ = true;
            } else {
                // Blocked: ILocomotion::Stop_Moving (FUN_0054B200) re-targets
                // MapClass::NearByLocation (FUN_0056D750) for the unit's own
                // SpeedType (Hover: water and beach are fine, Rock is not) --
                // the nearest free hover-able cell, where it lands, or hovers
                // again when that cell is water. [RE-FALLBACK: ring order and
                // the 8-ring bound of NearByLocation's scan.]
                auto hover_ok = [&](int x, int y) {
                    return (walkable(x, y) || an_victim_naval_cell(x, y)) && occ_.is_free(x, y) &&
                           building_at_cell(x, y) < 0;
                };
                bool found = false;
                for (int r = 1; r <= 8 && !found; ++r)
                    for (int oy = -r; oy <= r && !found; ++oy)
                        for (int ox = -r; ox <= r && !found; ++ox) {
                            if (std::max(std::abs(ox), std::abs(oy)) != r) continue;
                            if (!hover_ok(u.cell_x + ox, u.cell_y + oy)) continue;
                            u.flight_goal_x = u.cell_x + ox;
                            u.flight_goal_y = u.cell_y + oy;
                            u.has_flight_goal = true;
                            u.an.landing = true;   // a landing redirect (kept by step_cargo)
                            aircraft_any_ = true;
                            found = true;
                        }
            }
        }
        if (u.an.landing && u.flight_state == FlightState::Grounded) {
            u.an.landing = false;
            u.fx = u.cell_x;
            u.fy = u.cell_y;
            // W8-05-01 [RE: FUN_0054C280 @0x54C5A9..0x54C5E9]: touching down
            // on a bridge cell above its floor sets OnBridge (+0x8C).
            u.on_deck = static_cast<std::uint8_t>(
                high_bridge_cell(u.cell_x, u.cell_y) && !high_bridge_destroyed(u.cell_x, u.cell_y) ? 1 : 0);
            if (occ_.is_free(u.cell_x, u.cell_y)) occ_.set(u.cell_x, u.cell_y, i);
        }
    }
}

}  // namespace ra2engine

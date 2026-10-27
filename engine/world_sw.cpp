// world_sw.cpp -- wave-2 W2-AN superweapons v2: one record per SW type per
// house, the seven classic RA2 effects, and the presentation event feed.
// Split out of world.cpp (the CB-4 single-Nuke registry lived there).
//
// RE notes (this fix, gamemd.exe; Ghidra pass of the auditor's w2air_sw1.py
// dump of FUN_006cba10 + a byte scan of TechnoTypeClass::LoadFromINI stores):
//  * case 0 MultiMissile: the silo branch records the target in the house and
//    plays the silo launch; the second branch creates the DOWN missile 20000
//    leptons above the target cell with velocity (0,0,-100) (cos/sin of pi/2
//    times DAT_007eca88 = -100.0) -> it falls 100 leptons/frame = 200 frames.
//    The up missile is [NukeCarrier] / [GiantNukeUp] Vertical=yes,
//    Acceleration=1 from launch speed 1 (projectile.md §6.2/§12.3) and detonates
//    above DetonationAltitude=20000: sum(1..200) = 20100 -> 200 frames.
//  * case 1 IronCurtain: the 3x3 offset table DAT_00b0afa8..cc around the
//    target; every object in those cells not already in limbo gets the
//    IronCurtain call with [General] IronCurtainDuration.
//  * case 4 ChronoWarp: the same 3x3 table around the source; an object is
//    teleported unless TechnoType+0xD97 (Organic) is set and +0xCD4
//    (Teleporter) is not -- that object takes ReceiveDamage(full Strength,
//    Rules+0xFA8 warhead), i.e. it dies.
#include <algorithm>
#include <cmath>

#include "combat.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "group_move.hpp"  // nearest_free_cells (W7-08-08)
#include "world.hpp"

namespace ra2engine {

namespace {
constexpr double kLept = 256.0;
// DAT_00b0afa8 [RE: this audit]: the nine (dx,dy) offsets of a 3x3 area.
constexpr int kArea3x3[9][2] = {{0, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0},
                                {1, 1}, {0, 1},   {-1, 1}, {-1, 0}};
int frames_of(double dt) { return std::max(1, static_cast<int>(om::lround(dt * 15.0))); }
}  // namespace

// ---------------------------------------------------------------- registry --

World::HouseSuperweapon* World::sw_rec(int house, SuperweaponType type) {
    if (house < 0 || house >= static_cast<int>(sw_houses_.size())) return nullptr;
    for (HouseSuperweapon& r : sw_houses_[house])
        if (r.has_sw && r.sw.type == type) return &r;
    return nullptr;
}
const World::HouseSuperweapon* World::sw_rec(int house, SuperweaponType type) const {
    if (house < 0 || house >= static_cast<int>(sw_houses_.size())) return nullptr;
    for (const HouseSuperweapon& r : sw_houses_[house])
        if (r.has_sw && r.sw.type == type) return &r;
    return nullptr;
}

void World::an_event(int kind, int house, SuperweaponType type, int x, int y, int x2, int y2) {
    SwEvent e;
    e.seq = ++an_.event_seq;
    e.kind = kind;
    e.house = house;
    e.type = static_cast<int>(type);
    e.x = x;
    e.y = y;
    e.x2 = x2;
    e.y2 = y2;
    an_.events.push_back(e);
    if (an_.events.size() > 64) an_.events.erase(an_.events.begin(), an_.events.begin() + 32);
}

std::uint32_t World::an_rand() {
    // Numerical Recipes LCG; World-owned, saved, no wall clock.
    an_.rng = an_.rng * 1664525u + 1013904223u;
    return an_.rng >> 8;
}

// The CB-4 single-SW call: upsert the record of `type` (fresh charge).
void World::grant_superweapon(int house, SuperweaponType type, double recharge_secs, int dmg,
                              double cell_spread, double pmax, const std::array<int, 11>& verses) {
    if (house < 0) return;
    if ((int)sw_houses_.size() <= house) sw_houses_.resize(house + 1);
    HouseSuperweapon* rec = sw_rec(house, type);
    if (!rec) {
        sw_houses_[house].push_back(HouseSuperweapon{});
        rec = &sw_houses_[house].back();
    }
    rec->has_sw = true;
    rec->sw = Superweapon{};
    rec->sw.type = type;
    rec->sw.recharge_secs = recharge_secs;
    rec->eff_damage = dmg;
    rec->eff_cell_spread = cell_spread;
    rec->eff_percent_at_max = pmax;
    rec->eff_verses = verses;
    if (type == SuperweaponType::Nuke) {
        an_.nuke_set = true;
        an_.nuke_damage = dmg;
        an_.nuke_spread = cell_spread;
        an_.nuke_pmax = pmax;
        an_.nuke_verses = verses;
    }
    sw_any_ = true;
}

void World::grant_superweapon_type(int house, SuperweaponType type, double recharge_secs,
                                   bool is_powered, int dmg, double cell_spread, double pmax,
                                   const std::array<int, 11>& verses) {
    if (house < 0 || type == SuperweaponType::None) return;
    if (sw_rec(house, type)) {   // already held: keep its charge
        auto* rec=sw_rec(house,type);
        rec->is_powered = is_powered;
        if(type==SuperweaponType::PsychicDominator || type==SuperweaponType::GeneticConverter || type==SuperweaponType::PsychicReveal) {
            rec->eff_damage=dmg;rec->eff_cell_spread=cell_spread;
            rec->eff_percent_at_max=pmax;rec->eff_verses=verses;
        }
        return;
    }
    grant_superweapon(house, type, recharge_secs, dmg, cell_spread, pmax, verses);
    sw_rec(house, type)->is_powered = is_powered;
    an_event(SwEvent::Granted, house, type, -1, -1);
}

void World::revoke_superweapon_type(int house, SuperweaponType type) {
    if (house < 0 || house >= static_cast<int>(sw_houses_.size())) return;
    auto& v = sw_houses_[house];
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const HouseSuperweapon& r) { return r.sw.type == type; }),
            v.end());
}

const Superweapon* World::sw_state(int house, SuperweaponType type) const {
    const HouseSuperweapon* r = sw_rec(house, type);
    return r ? &r->sw : nullptr;
}
bool World::sw_ready(int house, SuperweaponType type) const {
    const HouseSuperweapon* r = sw_rec(house, type);
    return r && r->sw.is_ready();
}
double World::sw_charge_fraction(int house, SuperweaponType type) const {
    const HouseSuperweapon* r = sw_rec(house, type);
    return r ? r->sw.charge_fraction() : 0.0;
}
std::vector<SuperweaponType> World::sw_types(int house) const {
    std::vector<SuperweaponType> out;
    if (house < 0 || house >= static_cast<int>(sw_houses_.size())) return out;
    for (const HouseSuperweapon& r : sw_houses_[house])
        if (r.has_sw) out.push_back(r.sw.type);
    return out;
}

// --- single-SW (first record) API, the CB-4 surface gates and tests use ---
bool World::sw_ready(int house) const {
    return house >= 0 && house < (int)sw_houses_.size() && !sw_houses_[house].empty() &&
           sw_houses_[house].front().has_sw && sw_houses_[house].front().sw.is_ready();
}
double World::sw_charge_fraction(int house) const {
    if (house < 0 || house >= (int)sw_houses_.size() || sw_houses_[house].empty()) return 0.0;
    return sw_houses_[house].front().sw.charge_fraction();
}
const Superweapon* World::sw_state(int house) const {
    if (house < 0 || house >= (int)sw_houses_.size() || sw_houses_[house].empty() ||
        !sw_houses_[house].front().has_sw)
        return nullptr;
    return &sw_houses_[house].front().sw;
}
bool World::fire_superweapon(int house, int cx, int cy) {
    const Superweapon* s = sw_state(house);
    if (!s) return false;
    return fire_superweapon_at(house, s->type, cx, cy);
}

void World::transfer_superweapon(int old_house, int new_house) {
    if (old_house < 0 || old_house >= (int)sw_houses_.size()) return;
    if (new_house < 0) return;
    if (sw_houses_[old_house].empty()) return;
    if ((int)sw_houses_.size() <= new_house) sw_houses_.resize(new_house + 1);
    for (const HouseSuperweapon& r : sw_houses_[old_house])
        if (!sw_rec(new_house, r.sw.type)) sw_houses_[new_house].push_back(r);
    sw_houses_[old_house].clear();
    sw_any_ = true;
}

void World::reset_superweapon(int house) {
    if (house < 0 || house >= (int)sw_houses_.size()) return;
    for (HouseSuperweapon& r : sw_houses_[house]) r.sw.on_fired();
}

void World::step_superweapon(double dt) {
    if (!sw_any_) return;   // firewall (spec §8)
    for (int h = 0; h < static_cast<int>(sw_houses_.size()); ++h) {
        bool low = false, low_known = false;
        for (HouseSuperweapon& rec : sw_houses_[h]) {
            if (!rec.has_sw) continue;
            // [RE: superweapon.md §1] IsPowered=true suspends the charge on low power.
            if (rec.is_powered) {
                if (!low_known) { low = house_low_power(h); low_known = true; }
                if (low) continue;
            }
            const bool was = rec.sw.ready;
            rec.sw.tick(dt);
            if (!was && rec.sw.ready) an_event(SwEvent::Ready, h, rec.sw.type, -1, -1);
        }
    }
    an_step_strikes(dt);
}

// ------------------------------------------------------------------ firing --

bool World::fire_superweapon_at(int house, SuperweaponType type, int cx, int cy, int cx2, int cy2) {
    HouseSuperweapon* rec = sw_rec(house, type);
    if (!rec || !rec->sw.is_ready()) return false;   // refused unless READY (§4)
    if (cx < 0 || cy < 0 || cx >= grid_w_ || cy >= grid_h_) return false;
    switch (type) {
        case SuperweaponType::Nuke: {
            // The silo: the house's live building whose type grants the Nuke.
            int ox = cx, oy = cy;
            for (const Building& b : buildings_) {
                if (!b.alive || b.house != house || b.state == Building::State::Dead) continue;
                if (b.type < 0 || b.type >= static_cast<int>(an_.sw_btype.size())) continue;
                if (an_.sw_btype[b.type] != static_cast<int>(SuperweaponType::Nuke)) continue;
                ox = b.cx + b.fw / 2;
                oy = b.cy + b.fh / 2;
                break;
            }
            SwStrike s;
            s.kind = 1;
            s.house = house;
            s.x = cx;
            s.y = cy;
            s.ox = ox;
            s.oy = oy;
            an_.strikes.push_back(s);
            an_event(SwEvent::Launched, house, type, cx, cy, ox, oy);
            break;
        }
        case SuperweaponType::IronCurtain:
            an_iron_curtain(house, cx, cy);
            an_event(SwEvent::Activated, house, type, cx, cy);
            break;
        case SuperweaponType::ChronoSphere:
        case SuperweaponType::ChronoWarp:
            if (cx2 < 0 || cy2 < 0 || cx2 >= grid_w_ || cy2 >= grid_h_) return false;
            an_chrono(house, cx, cy, cx2, cy2);
            an_event(SwEvent::Activated, house, type, cx, cy, cx2, cy2);
            break;
        case SuperweaponType::LightningStorm: {
            SwStrike s;
            s.kind = 2;
            s.house = house;
            s.x = cx;
            s.y = cy;
            s.rng = 0x9E3779B9u ^ (static_cast<std::uint32_t>(cx) * 73856093u) ^
                    (static_cast<std::uint32_t>(cy) * 19349663u) ^ an_rand();
            an_.strikes.push_back(s);
            an_event(SwEvent::Activated, house, type, cx, cy);
            break;
        }
        case SuperweaponType::ParaDrop:
        case SuperweaponType::AmerParaDrop:
            an_paradrop(house, type, cx, cy);
            an_event(SwEvent::Activated, house, type, cx, cy);
            break;
        case SuperweaponType::PsychicReveal:
            // Explore the targeted area, not the whole map. Current sight is
            // transient with FogOfWar; explored terrain remains mapped.
            reveal_source(house,cx,cy,std::clamp(static_cast<int>(rec->eff_cell_spread),0,256));
            an_event(SwEvent::Activated,house,type,cx,cy);
            break;
        case SuperweaponType::PsychicDominator:
        case SuperweaponType::GeneticConverter: {
            std::vector<int> affected;
            const double radius = std::max(0.5, rec->eff_cell_spread) * kLept;
            for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
                const Unit& u = units_[static_cast<std::size_t>(i)];
                if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.is_aircraft ||
                    !is_enemy(house, u.house) ||
                    om::hypot((u.fx - cx) * kLept, (u.fy - cy) * kLept) > radius)
                    continue;
                if (type == SuperweaponType::PsychicDominator) {
                    if (psionic_target_ok(i) && u.an.iron_left_frames <= 0.0 &&
                        u.w7.mc_controller < 0 && u.w7.mc_building < 0)
                        affected.push_back(i);
                } else if (u.is_infantry) {
                    affected.push_back(i);
                }
            }
            if (type == SuperweaponType::PsychicDominator) {
                for (int i : affected) set_unit_owner(i, house);
                Unit payload;
                payload.house = house;
                payload.w_damage = 1000;
                payload.w_cell_spread = 1.5;
                payload.w_percent_at_max = 0.1;
                payload.w_verses.assign(11, 100);
                payload.w_affects_allies = false;
                damage_area(*this, units_, buildings_, occ_, payload, -1, -1, cx * kLept, cy * kLept, deck_z(cx, cy));
            } else {
                const auto brute = an_.types.find("BRUTE");
                if (brute == an_.types.end()) return false;
                for (int i : affected) {
                    if (i < 0 || i >= static_cast<int>(units_.size())) continue;
                    const int x = units_[static_cast<std::size_t>(i)].cell_x;
                    const int y = units_[static_cast<std::size_t>(i)].cell_y;
                    an_damage_unit(i, units_[static_cast<std::size_t>(i)].hp, -1);
                    if (units_[static_cast<std::size_t>(i)].die_time < 0.0) continue;
                    an_spawn("BRUTE", house, x, y);
                }
            }
            an_event(SwEvent::Activated, house, type, cx, cy);
            break;
        }
        default:
            return false;
    }
    rec->sw.on_fired();   // full recharge (§1)
    an_.any = true;
    return true;
}

// ------------------------------------------------------------------ damage --

void World::an_damage_unit(int idx, int dmg, int attacker, int inf_death) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& v = units_[idx];
    if (!v.alive || v.die_time >= 0.0 || dmg <= 0) return;
    if (v.an.iron_left_frames > 0.0) return;   // W2-AN-04 Iron Curtain
    v.hp -= dmg;
    if (v.hp <= 0) {
        v.die_time = 0.0;
        v.vet.killed_by = attacker;
        if (inf_death >= 0) v.ia.death = inf_death;   // W7-12-08 (presentation only)
        occ_.release_all_of(idx, -1, -1);
        v.target = -1;
        v.target_building = -1;
        v.path.clear();
        v.path_idx = 0;
    }
}

void World::an_damage_building(int b, int dmg) {
    if (b < 0 || b >= static_cast<int>(buildings_.size())) return;
    Building& B = buildings_[b];
    if (!B.alive || B.state == Building::State::Dead || dmg <= 0) return;
    if (B.iron_left_frames > 0.0) return;
    B.hp -= dmg;
    if (B.hp <= 0) kill_building(B);
}

// A warhead blast at cell-space (cx, cy): every unit (airborne ones only when
// hit_air) and every building within round(spread) cells, linear falloff
// 1.0 -> pmax (combat.cpp's adjudicated CellSpread curve), Verses per armor.
// [RE: superweapon.md §2/§5 -- FUN_00489190 hits EVERY object in radius, the
// firing house included; spare_house keeps the CB-4 enemies-only rule for
// callers that want it.]
void World::an_area_damage(int house, double cx, double cy, int damage, double spread, double pmax,
                           const std::array<int, 11>& verses, bool hit_air, bool spare_house,
                           double z, int wall_flags, int inf_death) {
    const double radius = std::max(0.5, std::round(spread)) * kLept;
    const std::vector<int> vv(verses.begin(), verses.end());
    // w8fix-layer W8-04-02 / W8-05-06 [RE: DamageArea FUN_00489190 -- an
    // impact cell with the bridge flag and Z above floor + BridgeHeight/2
    // scans the deck chain +0xE8 of every cell, else the ground chain +0xE4]:
    // one layer per blast (z is the height over the impact cell's ground).
    const bool deck_blast = deck_intact(static_cast<int>(om::lround(cx)), static_cast<int>(om::lround(cy))) && z > 208.0;
    for (int j = 0; j < static_cast<int>(units_.size()); ++j) {
        const Unit& w = units_[j];
        if (!w.alive || w.die_time >= 0.0 || w.cargo_of >= 0) continue;
        if (!hit_air && w.is_aircraft && w.altitude > 0.0) continue;
        if (spare_house && w.house == house) continue;
        const bool w_air = w.is_aircraft && w.altitude > 0.0;
        if (!w_air && (w.on_deck != 0) != deck_blast) continue;   // w8fix-layer: the other chain
        double d = om::hypot((w.fx - cx) * kLept, (w.fy - cy) * kLept);
        // W4: an explosion above the ground (a rocket shot down in flight) is
        // measured in 3-D [FUN_00489190 distance], so ground objects far below
        // take nothing. z = 0 (every other caller) is the old 2-D test.
        if (z > 0.0) d = om::hypot(d, z - (w_air ? w.altitude : (w.on_deck ? 416.0 : 0.0)));
        if (d > radius) continue;
        const double scale = 1.0 - (1.0 - pmax) * (d / radius);
        const int dmg = damage_vs(om::ftol(damage * scale), vv, w.armor_ix);
        const int hp0 = w.hp;
        an_damage_unit(j, dmg, -1, inf_death);
        feed_damage(false, j, house, hp0 - units_[j].hp);   // W6-03-04 anger
    }
    // W6-04-04/05 [RE: FUN_00489190 @0x489581]: wall cells inside the blast
    // take Reduce_Wall per the warhead's wall keys (a Nuke's
    // WallAbsoluteDestroyer erases them), never an HP hit.
    const int wr = om::ftol(std::max(0.5, std::round(spread)));
    if (has_wall_overlays() && z <= 0.0)
        for (int dy = -wr; dy <= wr; ++dy)
            for (int dx = -wr; dx <= wr; ++dx) {
                const int x = static_cast<int>(om::lround(cx)) + dx, y = static_cast<int>(om::lround(cy)) + dy;
                if (om::hypot((x - cx) * kLept, (y - cy) * kLept) <= radius)
                    w6_overlay_wall_hit(x, y, wall_flags, damage);
            }
    for (int bi = 0; bi < static_cast<int>(buildings_.size()); ++bi) {
        const Building& b = buildings_[bi];
        if (!b.alive || b.state == Building::State::Dead) continue;
        if (spare_house && b.house == house) continue;
        if (b.type >= 0 && b.type < static_cast<int>(btypes().size()) &&
            btypes()[static_cast<std::size_t>(b.type)].w6.wall_levels > 0) {   // W6-04-04
            if (z <= 0.0 && om::hypot((b.cx - cx) * kLept, (b.cy - cy) * kLept) <= radius)
                w6_wall_building_hit(*this, units_, buildings_, bi, wall_flags, damage);
            continue;
        }
        double d = -1.0;
        for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
            const double dc = om::hypot((c.x - cx) * kLept, (c.y - cy) * kLept);
            if (d < 0.0 || dc < d) d = dc;
        }
        if (z > 0.0 && d >= 0.0) d = om::hypot(d, z);   // W4: 3-D (see above)
        if (d < 0.0 || d > radius) continue;
        const double scale = 1.0 - (1.0 - pmax) * (d / radius);
        const int hp0 = b.hp;
        an_damage_building(bi, damage_vs(om::ftol(damage * scale), vv, b.armor_ix));
        feed_damage(true, bi, house, hp0 - buildings_[bi].hp);   // W6-03-04 anger
    }
}

// ----------------------------------------------------------------- effects --

void World::an_nuke_detonate(const SwStrike& s) {
    const HouseSuperweapon* rec = sw_rec(s.house, SuperweaponType::Nuke);
    // The blast params travel with the grant; a silo sold mid-flight still
    // lands its missile with the stock [NukePayload]/[NUKE] values.
    int dmg = 600;
    double spread = 10.0, pmax = 0.02;
    std::array<int, 11> verses{};
    verses.fill(100);
    if (rec) {
        dmg = rec->eff_damage;
        spread = rec->eff_cell_spread;
        pmax = rec->eff_percent_at_max;
        verses = rec->eff_verses;
    } else if (an_.nuke_set) {
        dmg = an_.nuke_damage;
        spread = an_.nuke_spread;
        pmax = an_.nuke_pmax;
        verses = an_.nuke_verses;
    }
    // Use the same recovered DamageArea owner as ordinary weapon impacts.
    // In particular a building is encountered in EACH foundation cell, not
    // just the nearest one (combat.md section 7; Ares CellSpread hit-limit docs).
    // A 3x3 concrete silo was previously receiving only one of its nine hits.
    Unit payload;
    payload.house=s.house;payload.w_damage=dmg;
    payload.w_cell_spread=spread;payload.w_percent_at_max=pmax;
    payload.w_verses.assign(verses.begin(),verses.end());
    payload.w_affects_allies=true;payload.w_inf_death=nuke_inf_death_;
    payload.w6.wall_flags=an_.sw.nuke_wall_flags;
    const int impact_z=deck_z(s.x,s.y);
    damage_area(*this,units_,buildings_,occ_,payload,-1,-1,s.x*kLept,s.y*kLept,impact_z);
    bridge_area_roll(s.x,s.y,dmg,an_.sw.nuke_wall_flags,impact_z);
    // [NukePayload] RadLevel=500: a radiation site over the blast (P4 RadField;
    // spread = the warhead CellSpread, as BulletClass::Detonate's RadSite block).
    if (an_.sw.nuke_rad_level > 0 && grid_w_ > 0 && grid_h_ > 0) {
        if (rad_.empty()) rad_.init(grid_w_, grid_h_);
        rad_.detonate(s.x, s.y, om::ftol(spread), an_.sw.nuke_rad_level, ability_frame_,
                      ability_rules_);
    }
    an_event(SwEvent::Detonated, s.house, SuperweaponType::Nuke, s.x, s.y, s.ox, s.oy);
}

void World::an_iron_curtain(int house, int cx, int cy) {
    (void)house;
    const int secs = an_.sw.iron_frames;   // Net L0: whole frames
    for (const auto& o : kArea3x3) {
        const int x = cx + o[0], y = cy + o[1];
        for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
            Unit& u = units_[i];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            if (u.cell_x != x || u.cell_y != y) continue;
            // W7-08-05 [RE: SuperClass::Place FUN_006CBA10 case 1 @0x6CC576]:
            // the SW walks the cell's ground / bridge object lists, so an
            // airborne aircraft is not curtained; an object already flagged
            // for a chrono warp (+0x27C) is skipped.
            if (u.is_aircraft && u.altitude > 0.0) continue;
            if (u.an.chrono_left_frames > 0) continue;
            // W7-08-05 [RE: InfantryClass::IronCurtain 0x522330]: EVERY
            // infantry dies (C4Warhead, ignoreDefenses); [RE: FootClass::
            // IronCurtain 0x4DE9F0] an Organic UnitType (squid, dolphin) too.
            if (u.is_infantry || (!u.is_vehicle && !u.is_aircraft && !u.is_naval) || u.an.t.organic) {
                u.an.iron_left_frames = 0;
                an_damage_unit(i, u.hp, -1);
                continue;
            }
            // W7-04-12 / W7-08-04 [RE: FootClass::IronCurtain 0x4DEA48]: a
            // riding parasite gets Suppression 50 and ExitUnit -- a Terror
            // Drone dies, a squid is let go.
            if (u.parasite_on_me >= 0) parasite_exit(i, /*suppressed=*/true);
            u.an.iron_left_frames = secs;
        }
        const int b = building_at_cell(x, y);
        if (b >= 0) {
            Building& B = buildings_[b];
            // W7-08-06 [RE: BuildingClass::IronCurtain FUN_00457C90]: an armed
            // C4 is DEFUSED (C4Applied +0x6DF cleared, the C4 timer reset),
            // then TechnoClass::IronCurtain. (W7-01-23's blast-side proposal
            // is superseded.)
            if (B.c4_applied) {
                B.c4_applied = false;
                B.c4_frame = 0;
            }
            B.iron_left_frames = secs;
        }
    }
}

// W7-08-07/08 [RE: SuperClass::Place 0x6CBD02-0x6CC268; FootClass chrono
// arrival FUN_00717600 / FUN_00717B40 / 0x718DE0].
void World::an_chrono(int house, int sx, int sy, int tx, int ty) {
    (void)house;
    const int freeze = an_.sw.chrono_delay_frames;   // Net L0: whole frames
    // Collect first (a teleported unit must not be picked up again at its
    // destination when the two areas overlap).
    std::vector<std::pair<int, int>> movers;   // unit, offset index
    std::vector<std::uint8_t> moving(units_.size(), 0);
    for (int k = 0; k < 9; ++k) {
        const int x = sx + kArea3x3[k][0], y = sy + kArea3x3[k][1];
        for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
            const Unit& u = units_[i];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            if (u.is_aircraft && u.altitude > 0.0) continue;   // airborne: not in the cell layer
            if (u.cell_x != x || u.cell_y != y) continue;
            // [RE: 0x6CBDF0-0x6CBE52] Organic && !Teleporter dies (C4Warhead,
            // ignoreDefenses) BEFORE the curtain test -- curtained or not.
            if (u.an.t.organic && !u.an.t.teleporter) {
                Unit& k = units_[static_cast<std::size_t>(i)];
                k.an.iron_left_frames = 0;
                an_damage_unit(i, k.hp, -1);
                continue;
            }
            // W7-08-07 [RE: 0x6CBE61]: curtained, already warping (+0x27C) and
            // being-erased (+0x270 / +0x271) objects are skipped.
            if (u.an.iron_left_frames > 0 || u.an.chrono_left_frames > 0 || u.warp_remaining > 0) continue;
            {
                movers.push_back({i, k});
                moving[static_cast<std::size_t>(i)] = 1;
            }
        }
    }
    auto kill = [&](int i) {
        units_[static_cast<std::size_t>(i)].an.iron_left_frames = 0;
        an_damage_unit(i, units_[static_cast<std::size_t>(i)].hp, -1);
    };
    for (const auto& m : movers) {
        const int i = m.first;
        Unit& u = units_[i];
        const int dx = tx + kArea3x3[m.second][0], dy = ty + kArea3x3[m.second][1];
        // [RE: 0x6CBF1D-0x6CC226] every Foot object (infantry and landed
        // aircraft too) gets the piggyback Teleport locomotor to its offset cell.
        const bool inf = !u.is_vehicle && !u.is_aircraft && !u.is_naval;
        // W7-08-08 [RE: FUN_00717600]: the EXACT offset cell (no nearest-free
        // search). Off the map: destroyed.
        if (dx < 0 || dy < 0 || dx >= grid_w_ || dy >= grid_h_) {
            kill(i);
            continue;
        }
        // Telefrag [RE: FUN_00717600]: a non-curtained Foot occupant of the
        // landing cell dies (C4Warhead, ignoreDefenses) -- infantry landing on
        // infantry only kills the same sub-cell, which the clone's spots stand
        // in for by not killing (the arrival takes a free spot / cell); a
        // curtained occupant kills the ARRIVING unit (0x717B5B). A non-Foot
        // occupant (a building) blocks: the arrival is re-aimed at a nearby
        // location (MapClass::NearbyLocation 0x56D750), not killed.
        int lx = dx, ly = dy;
        if (lx >= 0 && ly >= 0 && lx < grid_w_ && ly < grid_h_ && building_at_cell(lx, ly) >= 0) {
            const std::vector<Cellxy> g = nearest_free_cells({lx, ly}, 1, grid_for(u.movement_domain), grid_w_, grid_h_, occ_);
            if (g.empty()) continue;   // stays where it is
            lx = g[0].x;
            ly = g[0].y;
        }
        // W8-06-05 [RE: FUN_00717600]: the destination layer. A cell with the
        // bridge flag (+0x140 & 0x100, an intact high deck) scans AltObject
        // (+0xE8, the deck list), otherwise FirstObject (+0xE4); the arrival
        // is placed with OnBridge (+0x8C) = 1 and Z + BridgeHeight there. The
        // clone puts a mover whose domain has a deck (land / amphibious) on
        // the deck; a ship keeps the water layer under it (no ship deck).
        auto dest_layer = [&](int x, int y) -> std::uint8_t {
            return (high_bridge_cell(x, y) && !high_bridge_destroyed(x, y) && deck_grid(u.movement_domain)) ? 1 : 0;
        };
        std::uint8_t layer = dest_layer(lx, ly);
        bool blocked_by_curtain = false, inf_left = false;
        std::vector<int> frag;
        for (int j = 0; j < static_cast<int>(units_.size()); ++j) {
            if (j == i || moving[static_cast<std::size_t>(j)]) continue;
            const Unit& o = units_[static_cast<std::size_t>(j)];
            if (!o.alive || o.die_time >= 0.0 || o.cargo_of >= 0) continue;
            if (o.is_aircraft && o.altitude > 0.0) continue;
            if (o.cell_x != lx || o.cell_y != ly) continue;
            if ((o.on_deck != 0) != (layer != 0)) continue;   // W8-06-05: the other layer is untouched
            if (o.an.iron_left_frames > 0) blocked_by_curtain = true;
            else if (inf && !o.is_vehicle && !o.is_aircraft && !o.is_naval) inf_left = true;
            else frag.push_back(j);
        }
        if (blocked_by_curtain) {
            kill(i);
            continue;
        }
        for (int j : frag) kill(j);
        if (inf_left) {   // infantry on infantry: another spot / cell
            const std::vector<Cellxy> g = nearest_free_cells({lx, ly}, 1, grid_for(u.movement_domain), grid_w_, grid_h_, occ_);
            if (g.empty()) continue;
            lx = g[0].x;
            ly = g[0].y;
            layer = dest_layer(lx, ly);
        }
        // [RE: 0x718DE0] water under a vehicle that is neither Hover (an
        // amphibious domain) nor Naval and not on a bridge deck: it SINKS
        // (IsSinking +0x3CD; its passengers drown with it). A cell its
        // movement zone cannot use on land: destroyed.
        if (!unit_can_enter_cell(u, lx, ly)) {
            const std::size_t ci = static_cast<std::size_t>(ly) * grid_w_ + lx;
            const bool water = ci < water_passable_.size() && water_passable_[ci] != 0;
            if (water && !u.is_naval && !inf) u.an.sinking = true;
            kill(i);
            continue;
        }
        // W8-06-06 [RE: Teleport locomotor 0x7188D9 -> ParasiteClass::ExitUnit
        // 0x62A2A0]: the teleport ejects a riding Terror Drone / Squid beside
        // the source cell, paralysed (as the CLEG / Chrono self-teleport).
        if (u.parasite_on_me >= 0) parasite_exit(i, /*suppressed=*/false);
        occ_.release_all_of(i, -1, -1);
        u.cell_x = lx;
        u.cell_y = ly;
        u.on_deck = layer;   // W8-06-05 [RE: FUN_00717600 OnBridge +0x8C]
        u.fx = lx;
        u.fy = ly;
        u.path.clear();
        u.path_idx = 0;
        u.target = -1;
        u.target_building = -1;
        u.attack_order = false;
        u.guard_x = lx;
        u.guard_y = ly;
        occ_.set(lx, ly, i);
        if (const ra2assets::MapCell* mc = map_ ? map_->at(lx, ly) : nullptr) u.level = mc->level;
        u.an.chrono_left_frames = freeze;
        u.frozen = true;
    }
}

bool World::trigger_lightning_strike(int house, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= grid_w_ || cy >= grid_h_) return false;
    SwStrike s;
    s.kind = 2;
    s.house = house;
    s.x = cx;
    s.y = cy;
    an_storm_bolt(s, cx, cy);
    an_.any = true;
    return true;
}

bool World::trigger_lightning_storm(int house, int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= grid_w_ || cy >= grid_h_) return false;
    for (const SwStrike& s : an_.strikes)
        if (s.kind == 2 && !s.done) return false;   // LightningStorm::IsActive
    SwStrike s;
    s.kind = 2;
    s.house = house;
    s.x = cx;
    s.y = cy;
    s.rng = 0x9E3779B9u ^ (static_cast<std::uint32_t>(cx) * 73856093u) ^
            (static_cast<std::uint32_t>(cy) * 19349663u) ^ an_rand();
    an_.strikes.push_back(s);
    an_event(SwEvent::Activated, house, SuperweaponType::LightningStorm, cx, cy);
    an_.any = true;
    sw_any_ = true;   // step_superweapon runs the strike list
    return true;
}

void World::an_storm_bolt(SwStrike& s, int x, int y) {
    std::array<int, 11> verses = an_.sw.storm_verses;
    bool any = false;
    for (int v : verses) any |= v != 0;
    if (!any) verses.fill(100);
    // The same foundation/ring damage owner as the nuke and weapon impacts.
    Unit payload;payload.house=s.house;payload.w_damage=an_.sw.storm_damage;
    payload.w_cell_spread=an_.sw.storm_wh_spread;payload.w_percent_at_max=an_.sw.storm_wh_pmax;
    payload.w_verses.assign(verses.begin(),verses.end());payload.w_affects_allies=true;
    payload.w_inf_death=storm_inf_death_;payload.w6.wall_flags=an_.sw.storm_wall_flags;
    damage_area(*this,units_,buildings_,occ_,payload,-1,-1,x*kLept,y*kLept,deck_z(x,y));
    // W8-06-04 [RE: FUN_0053A030 -- the bolt is DamageArea FUN_00489190 at
    // the cell centre with Z = Level + BridgeHeight on a bridge cell
    // (GetCoordsWithBridge), LightningWarhead IonWH Wall=yes: the bridge block
    // @0x489DCB rolls Random(1, BridgeStrength=1500) < LightningDamage 250,
    // ~16.6% per bolt, on a deck or low-bridge cell].
    bridge_area_roll(x, y, an_.sw.storm_damage, an_.sw.storm_wall_flags, deck_z(x, y));
    an_event(SwEvent::Bolt, s.house, SuperweaponType::LightningStorm, x, y);
    s.recent.push_back(x);
    s.recent.push_back(y);
    if (s.recent.size() > 12) s.recent.erase(s.recent.begin(), s.recent.begin() + 2);
}

void World::an_step_strikes(double dt) {
    if (an_.strikes.empty()) return;
    const int nf = frames_of(dt);
    const SwParams& P = an_.sw;
    for (std::size_t k = 0; k < an_.strikes.size(); ++k) {
        for (int f = 0; f < nf && !an_.strikes[k].done; ++f) {
            SwStrike& s = an_.strikes[k];
            ++s.frame;
            if (s.kind == 1) {   // nuke: up leg, then the down leg, then the blast
                if (s.frame == P.nuke_up_frames)
                    an_event(SwEvent::NukeFalling, s.house, SuperweaponType::Nuke, s.x, s.y, s.ox, s.oy);
                if (s.frame >= P.nuke_up_frames + P.nuke_down_frames) {
                    const SwStrike copy = s;
                    an_.strikes[k].done = true;
                    an_nuke_detonate(copy);
                }
            } else if (s.kind == 2) {   // lightning storm
                // [RE-FALLBACK: FUN_00539be0/00539e10 scheduler not unwound]
                // LightningDeferment frames of gathering clouds, then
                // LightningStormDuration frames of bolts: one on the target every
                // LightningHitDelay frames, one random bolt every
                // LightningScatterDelay frames inside the LightningCellSpread
                // square, kept LightningSeparation (city-block) from recent bolts.
                if (s.frame <= P.storm_deferment) continue;
                const int t = s.frame - P.storm_deferment;
                if (t > P.storm_duration) {
                    s.done = true;
                    continue;
                }
                if (++s.hit_timer >= P.storm_hit_delay) {
                    s.hit_timer = 0;
                    an_storm_bolt(s, s.x, s.y);
                }
                if (++s.scatter_timer >= P.storm_scatter_delay) {
                    s.scatter_timer = 0;
                    const int half = std::max(1, P.storm_cell_spread / 2);
                    for (int tries = 0; tries < 8; ++tries) {
                        s.rng = s.rng * 1664525u + 1013904223u;
                        const int rx = s.x - half + static_cast<int>((s.rng >> 8) % (2 * half));
                        s.rng = s.rng * 1664525u + 1013904223u;
                        const int ry = s.y - half + static_cast<int>((s.rng >> 8) % (2 * half));
                        if (rx < 0 || ry < 0 || rx >= grid_w_ || ry >= grid_h_) continue;
                        bool close = false;
                        for (std::size_t r = 0; r + 1 < s.recent.size(); r += 2)
                            if (std::abs(s.recent[r] - rx) + std::abs(s.recent[r + 1] - ry) <
                                P.storm_separation)
                                close = true;
                        if (close) continue;
                        an_storm_bolt(s, rx, ry);
                        break;
                    }
                }
            }
        }
    }
    an_.strikes.erase(std::remove_if(an_.strikes.begin(), an_.strikes.end(),
                                     [](const SwStrike& s) { return s.done; }),
                      an_.strikes.end());
}

}  // namespace ra2engine

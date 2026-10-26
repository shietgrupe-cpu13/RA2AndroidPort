#include "combat.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim

#include <algorithm>
#include <map>
#include <cctype>
#include <cmath>

#include "building.hpp"  // Building, foundation_cells, kill_building (Task 4)
#include "pathfind.hpp"  // Cellxy
#include "weapon_unit.hpp"  // Gap fix C: per-target weapon slot

namespace ra2engine {

namespace {

// --- Constants (docs/re/combat.md; per-section refs noted at each use) ---
constexpr double kLogicFps = 15.0;         // ROF frames -> seconds (§5)
constexpr double kLeptonsPerCell = 256.0;  // lepton coordinate scale (§6)
// ModEnc/YRpp fallback (Low confidence -- docs/re/combat.md §7: the ini
// knobs Stray/RelaxedStray/GuardModeStray are confirmed to exist and be
// ini-configurable in this build, but the consuming formula/call site wasn't
// located). Guard-acquire radius defaults to weapon range; kGuardStray is
// the extra slack (in cells) an auto-acquired target -- or the guarding unit
// itself -- may drift before the engagement is abandoned and the unit falls
// back to its guard post.
constexpr double kGuardStray = 2.0;
constexpr double kDieAnimSecs = 1.0;    // renderer-tuned (Task 7), not RE'd.
constexpr double kFireFlashSecs = 0.4;  // renderer-tuned (Task 7), not RE'd.

const std::vector<std::string>& armor_names() {
    static const std::vector<std::string> names = {
        "none", "flak", "plate", "light", "medium", "heavy",
        "wood", "steel", "concrete", "special_1", "special_2",
    };
    return names;
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Euclidean distance in leptons between two cell-space (fx,fy) points.
// docs/re/combat.md §6 (High): CONFIRMED Euclidean -- but the game's
// distance is full 3D (lepton Z included, `Δx²+Δy²+Δz²`); our sim carries no
// Z coordinate at all, so this is necessarily 2D. Documented divergence:
// negligible for same-plateau infantry (the overwhelming common case in this
// vertical slice), called out here rather than silently diverging from the
// calibration authority.
double dist_leptons(double x1, double y1, double x2, double y2) {
    const double dx = (x2 - x1) * kLeptonsPerCell;
    const double dy = (y2 - y1) * kLeptonsPerCell;
    return om::hypot(dx, dy);
}

// A unit is a legal combat target iff it exists, is alive, isn't mid-death-
// anim, and isn't friendly. docs/re/combat.md §1/§2 (GetFireError/
// ReceiveDamage): a dying/dead unit is never a valid target.
bool valid_target(const std::vector<Unit>& units, const World& world, const Unit& self, int idx) {
    if (idx < 0 || idx >= static_cast<int>(units.size())) return false;
    const Unit& v = units[idx];
    // W2-AN-13: a submerged sub no sonar of self's house has found is not a
    // target (its cell is not "seen" in the water layer).
    return v.alive && v.die_time < 0.0 && unit_combat_exposed(v) && world.combat_hostile(self, v) &&
           !world.unit_hidden_from(self.house, v) && unit_weapon_for_unit(self, v) >= 0;
    // Multi-house/Alliances [RE: multihouse.md §3/§4]: hostility routes through
    // World::is_enemy. carried: untargetable (CB-1).
    // Gap fix C (G-44/G-50) [RE: docs/re/weapons.md §2, combat.md §2]: the old
    // hard `!v.is_aircraft` exclusion is replaced by GetFireError's weapon
    // gate -- an AIRBORNE aircraft is a target only for a weapon whose
    // projectile is AA=yes (Flak Track, Patriot, IFV, GGI, Apocalypse tusks),
    // a landed one for an AG weapon, and a 0 % Verses warhead cannot target
    // that armor at all. unit_weapon_for_unit picks primary, else secondary.
}

// Range (cells) of unit u's weapon slot `slot` without activating it: the
// active slot's live w_range (it may carry vet/IFV adjustments), else the
// type data of the other slot.
double slot_range(const Unit& u, int slot) {
    if (u.gattling.enabled()) return u.gattling.weapon(slot, is_elite(u.vet.veterancy)).range;
    if (slot == u.w_active) return u.w_range;
    if (slot == 1) return secondary_view(u).range;
    if (u.vet.recv && is_elite(u.vet.veterancy) && u.vet.has_elite) return u.vet.elite.range;
    return u.vet.base.range;
}

// W6-02-11 [RE: the threat scan FUN_006F8320 @0x6F8520..0x6F8678 takes its
// radius from the guard-range virtual vtbl+0x31C = FUN_007072E0]: idle
// infantry / units scan with threat 1 (@0x51F092, FUN_004D4CE0 @0x4D4D5E):
// FUN_007072E0(0) = the type's GuardRange= (0 when unset), and a zero radius
// falls back to (larger weapon range >> 8) + 1 + (AirRangeBonus >> 8). Area
// guard scans with threat 2 (@0x4D4234): FUN_007072E0(1) = 2 x (GuardRange,
// else the larger weapon range), at most 0x1000 leptons. Whole cells
// (lepton >> 8). Sight is never read.
double acquire_radius(const Unit& u) {
    const double gr = u.tg.t.guard_range;
    const double wr = std::max(slot_range(u, 0), slot_range(u, 1));
    if (u.tg.area_guard) return std::floor(std::min(2.0 * (gr > 0.0 ? gr : wr), 16.0) + 1e-9);
    if (gr > 0.0) return std::floor(gr + 1e-9);
    return std::floor(wr + 1e-9) + 1.0 + std::floor(u.w6.t.air_range_bonus + 1e-9);
}

const VeterancyRules* vet_rules_of(const World& world) {
    return world.veterancy_enabled() ? &world.veterancy_rules() : nullptr;
}

// ---- w7fix-combat helpers -------------------------------------------------
// W7-07-13 [RE: GetFireError FUN_006FB5D0 -- no house test]: an explicitly
// ordered target (force-fire, retaliation) stays legal whatever its owner:
// alive, on the map, seen, not the unit itself, and a weapon can engage it.
bool forced_target_ok(const std::vector<Unit>& units, const World& world, const Unit& self, int self_idx,
                      int idx) {
    if (idx < 0 || idx >= static_cast<int>(units.size()) || idx == self_idx) return false;
    const Unit& v = units[static_cast<std::size_t>(idx)];
    // w7r2 [RE: GetFireError FUN_006FB5D0 -- the warhead's Verses against the
    // target's armor gate every weapon]: an ability weapon is let through
    // here (its slot data may not pass the AA/AG test), but not against an
    // armor its warhead does 0 % to -- the dog's ParasiteDog jaw vs a vehicle
    // (SOV09: a patrol team ordered at the IFV rode it to death as parasites).
    return v.alive && v.die_time < 0.0 && unit_combat_exposed(v) && !world.unit_hidden_from(self.house, v) &&
           (unit_weapon_for_unit(self, v) >= 0 ||
            (self.abil.fire_kind != AbilityKind::None && verses_allows(primary_view(self).verses, v.armor_ix)));
}

// The Verses fraction (percent / 100) of slot `slot` of u against armor `ai`.
double slot_verses(const Unit& u, int slot, int ai) {
    WeaponSlot other;
    if (slot != u.w_active) other = slot == 1 ? secondary_view(u) : primary_view(u);
    const std::vector<int>& use = (slot == u.w_active || other.verses.empty()) ? u.w_verses : other.verses;
    if (use.empty()) return 1.0;
    if (ai < 0 || ai >= static_cast<int>(use.size())) ai = 0;
    return use[static_cast<std::size_t>(ai)] / 100.0;
}

// W7-07-06 [RE: Mission_Area_Guard FUN_004D41A0: (FUN_007072E0(2) >> 8) + 6;
// FUN_007072E0(2) = 2 x (GuardRange, else the larger weapon range), clamped
// to 0x700..0x1000 leptons]: the area-guard leash in cells, 13..22.
int area_guard_leash_cells(const Unit& u) {
    const double gr = u.tg.t.guard_range;
    const double wr = std::max(slot_range(u, 0), slot_range(u, 1));
    int l = om::ftol(2.0 * (gr > 0.0 ? gr : wr) * 256.0);
    l = std::max(0x700, std::min(0x1000, l));
    return (l >> 8) + 6;
}

// Fires `u`'s weapon at `v` (caller has already range/cooldown-checked).
// Resets u's cooldown/fire_flash, applies damage_vs, handles idle-victim
// retaliation, and starts death if the hit is lethal. `attacker_idx`/
// `victim_idx` are u/v's own indices in `units` (needed for the retaliation
// target and occ.release_all_of).
// Projectile phase (P2) [RE: projectile.md §10]: when `launches` is non-null
// (World's projectiles_enabled_ firewall is OPEN) and the weapon's projectile
// flies, the shot emits a ProjectileLaunch instead of hitting -- World's
// step_projectiles delivers the damage at detonation through the SAME
// deliver_unit_hit / apply_spread_damage. With `launches == nullptr` (every
// byte-identity gate) this is the pre-P2 body verbatim: the extracted
// deliver_unit_hit is the old else-branch statement-for-statement.
void try_fire(const World& world, std::vector<Unit>& units, std::vector<Building>& buildings,
              Unit& u, Unit& v, int attacker_idx, int victim_idx, Occupancy& occ,
              std::vector<ProjectileLaunch>* launches, ProjRng* rng) {
    u.rearm_frames = unit_rearm_frames(u, rng);   // W6-04-01 Burst
    u.fire_flash = kFireFlashSecs;
    if(world.chaos_weapon(u)) {
        damage_area(world,units,buildings,occ,u,attacker_idx,-1,
                    unit_lept_x(u),unit_lept_y(u),unit_lept_z(u));
        return;
    }
    // W2-AN-12 [rules Spawner=yes: HornetLauncher/ASWLauncher/DredLauncher/
    // V3Launcher]: the "shot" launches a spawned aircraft/missile at the
    // victim (World::step_air_naval) -- the launcher weapon itself deals no damage.
    if (u.an.t.spawner_slot >= 0 && u.w_active == u.an.t.spawner_slot) {
        u.an.spawn_fire_unit = victim_idx;
        return;
    }
    // P4 [RE: abilities.md §B1/§C2/§E/§F]: a weapon whose warhead/weapon flags
    // make it an ABILITY (Parasite / Temporal / IvanBomb / Suicide) deals no
    // plain damage -- the shot is handed to World::step_abilities, which
    // resolves it this same frame (abilities are instant: checked BEFORE the
    // P2 projectile launch). fire_kind is None unless a P4 profile is bound
    // (no pre-P4 gate binds one) -> the existing path below runs.
    if (u.abil.fire_kind != AbilityKind::None) {
        u.ability_fired_unit = victim_idx;
        return;
    }
    // Campaign mind control: the Controller shot captures (CampaignRuntime::
    // poll_mind_control), it never damages. Only campaign-armed units carry
    // the flag -> every gate / skirmish path below is unchanged.
    // W7-10-04 [RE: BulletClass::Detonate 0x4690A0 @0x4692C0 -> Capture]: the
    // capture rides THIS shot (its ROF, facing and range already applied);
    // the runtime consumes Unit::w7.mc_shot. W7-08-13: only the Controller
    // weapon (slot 0) -- a deployed Yuri's PsiWave (slot 1) does damage.
    // IsLocomotor weapon Damage is a behavior parameter, not hit damage.
    // Until the magnetic locomotor is implemented, never detonate that value
    // as an ordinary shell (stock Magnetron values can kill tanks instantly).
    if (u.w7.t.magnetic && u.w_active == 0) return;
    if (u.w_mind_control && u.w_active == 0) {
        u.w7.mc_shot = victim_idx;
        return;
    }
    // W7-07-26 [RE: InfantryClass::Fire 0x5203E0 @0x5207A0..0x5207F7]: after
    // an infantry shot whose weapon Speed (+0xA8 [SUSPECTED]) is below
    // [CombatDamage] Incoming=10, CellClass::Incoming(shooter, 1, 0) warns the
    // target cell (Grenade, RadEruption, PsiWave, ...).
    if (!u.is_vehicle && !u.is_aircraft && !u.is_naval && u.w_proj.speed_cap > 0 &&
        u.w_proj.speed_cap < om::ftol(world.combat_w7().incoming * 2.56)) {
        HitRec r;
        r.victim = victim_idx;
        r.attacker = attacker_idx;
        r.attacker_house = u.house;
        r.ax = u.cell_x;
        r.ay = u.cell_y;
        r.incoming = true;
        log_hit(r);
    }
    if (launches && u.w_proj.flies()) {
        launches->push_back({attacker_idx, victim_idx, -1});
        return;
    }
    // W5-B R-060 [RE: DamageArea FUN_00489190]: an instant shot detonates at
    // the victim's coordinates; every warhead goes through the area applicator
    // (a CellSpread .3 AP shell reaches only what is within 76 leptons).
    int ix = unit_lept_x(v), iy = unit_lept_y(v);
    // W5-B R-057 [RE: projectile.md §5.3, BulletClass::MoveTo @0x4687B0]: a
    // FlakScatter + Inviso shell ([FlakProj], the AA flak) bursts at an impact
    // point thrown Random(0, 2*BallisticScatter) * dist / WeaponRange leptons
    // in a random direction (sync RNG). Only with the projectile layer on (the
    // real game); instant-hit gates keep the exact point.
    if (u.w_proj.flak_scatter && u.w_proj.inviso && rng && world.projectiles_enabled()) {
        const int dist = static_cast<int>(om::sqrt(double(ix - unit_lept_x(u)) * (ix - unit_lept_x(u)) +
                                          double(iy - unit_lept_y(u)) * (iy - unit_lept_y(u)) +
                                          double(unit_lept_z(v) - unit_lept_z(u)) *
                                              (unit_lept_z(v) - unit_lept_z(u))));
        const int wr = std::max(1, om::ftol(u.w_range * 256.0));
        const int r = rng->random(0, 2 * world.projectile_rules().ballistic_scatter) * dist / wr;
        const double th = rng->angle();
        ix += om::ftol(om::cos(th) * r);
        iy += om::ftol(om::sin(th) * r);
    }
    damage_area(world, units, buildings, occ, u, attacker_idx, -1, ix, iy, unit_lept_z(v));
    // W2-SK-22 Prism split [RE: [Comet] ShrapnelWeapon/ShrapnelCount;
    // RE-FALLBACK: the fragments go to the ShrapnelCount nearest other
    // hostiles within the fragment weapon's Range of the impact, ascending
    // distance then index]. No-op for every weapon without shrapnel.
    if (u.shrapnel_count > 0 && u.shrapnel_damage > 0) {
        std::vector<std::pair<double, int>> near;
        const double r2 = u.shrapnel_range * u.shrapnel_range;
        for (int j = 0; j < static_cast<int>(units.size()); ++j) {
            if (j == victim_idx || j == attacker_idx) continue;
            const Unit& w = units[j];
            if (!w.alive || w.die_time >= 0.0 || w.cargo_of >= 0 || w.house == u.house || w.is_aircraft) continue;
            const double dx = w.fx - v.fx, dy = w.fy - v.fy;
            if (dx * dx + dy * dy <= r2) near.push_back({dx * dx + dy * dy, j});
        }
        std::sort(near.begin(), near.end());
        Unit frag = u;
        frag.w_damage = u.shrapnel_damage;
        frag.w_verses = u.shrapnel_verses;
        for (int k = 0; k < static_cast<int>(near.size()) && k < u.shrapnel_count; ++k) {
            world.note_fragment(attacker_idx, v.fx, v.fy, v.level, near[static_cast<std::size_t>(k)].second);   // W7-12-07
            deliver_hit_from(units, occ, frag, attacker_idx, -1, near[static_cast<std::size_t>(k)].second);
        }
    }
}

}  // namespace

// W7-07-03 [RE: FUN_0070C190, called from Evaluate_Object 0x6F7C36 and
// CanRetaliateToAttacker 0x707F09/0x707F1A]: the threat `u` sees in unit `v`.
//   TgtEff * Verses[the weapon of v vs my armor] (sign flipped when v targets me)
// + TgtSpecial * v.SpecialThreatValue
// + EnemyHouseThreatBonus                     (the house of v is my Enemy)
// + MyEff * Verses[my weapon vs the armor of v]
// + TgtStrength * v.HealthRatio
// + TgtDistance * max(0, dist_cells - my_range_cells)
// + 100000, ftol'd, at least 1. Coefficients: the house-type defaults
// ([General] *CoefficientDefault) [SUSPECTED: the HouseClass +0x1FB switch
// between the per-type and the Rules "Dumb" set was not named; every house is
// taken as using its type set]. The spread-warhead x0.5 per allied cell
// (FUN_006F6ED0) is not modelled.
int threat_value_unit(const World& world, const Unit& u, int self_idx, const Unit& v) {
    const World::CombatW7Rules& R = world.combat_w7();
    double t = 0.0;
    const int vs = unit_weapon_for_unit(v, u);
    if (vs >= 0) {
        const double te = R.tgt_eff * slot_verses(v, vs, u.armor_ix);
        t += (v.target == self_idx && self_idx >= 0) ? -te : te;
    }
    t += R.tgt_special * v.tg.t.special_threat_value;
    const int enemy = world.ai_enemy_house(u.house);
    if (enemy >= 0 && enemy == v.house) t += R.enemy_house_bonus;
    const int ms = unit_weapon_for_unit(u, v);
    if (ms >= 0) t += R.my_eff * slot_verses(u, ms, v.armor_ix);
    t += R.tgt_strength * (v.max_hp > 0 ? static_cast<double>(v.hp) / v.max_hp : 0.0);
    const int dx = unit_lept_x(v) - unit_lept_x(u), dy = unit_lept_y(v) - unit_lept_y(u),
              dz = unit_lept_z(v) - unit_lept_z(u);
    const int dist = om::ftol(om::sqrt(double(dx) * dx + double(dy) * dy + double(dz) * dz)) >> 8;
    const int rng = om::ftol((ms >= 0 ? slot_range(u, ms) : u.w_range) * 256.0) >> 8;
    t += R.tgt_distance * std::max(0, dist - rng);
    const int r = om::ftol(t + 100000.0);
    return r < 1 ? 1 : r;
}

int threat_value_building(const World& world, const Unit& u, const Building& b) {
    const World::CombatW7Rules& R = world.combat_w7();
    double t = 0.0;
    const std::vector<BuildingTypeDef>& bt = world.btypes();
    const BuildingTypeDef* T = (b.type >= 0 && b.type < static_cast<int>(bt.size())) ? &bt[static_cast<std::size_t>(b.type)]
                                                                                    : nullptr;
    if (T && T->armed && T->weapon[0].valid && !T->weapon[0].verses.empty()) {
        int ai = u.armor_ix;
        if (ai < 0 || ai >= static_cast<int>(T->weapon[0].verses.size())) ai = 0;
        t += R.tgt_eff * (T->weapon[0].verses[static_cast<std::size_t>(ai)] / 100.0);
    }
    const int enemy = world.ai_enemy_house(u.house);
    if (enemy >= 0 && enemy == b.house) t += R.enemy_house_bonus;
    const int ms = unit_weapon_for_building(u, b.armor_ix, b.bomb_frame >= 0);
    if (ms >= 0) t += R.my_eff * slot_verses(u, ms, b.armor_ix);
    t += R.tgt_strength * (b.max_hp > 0 ? static_cast<double>(b.hp) / b.max_hp : 0.0);
    int bx, by, bz;
    building_aim_lept(b, world.cell_level(b.cx, b.cy), bx, by, bz);
    const int dx = bx - unit_lept_x(u), dy = by - unit_lept_y(u);
    const int dist = om::ftol(om::sqrt(double(dx) * dx + double(dy) * dy)) >> 8;
    const int rng = om::ftol((ms >= 0 ? slot_range(u, ms) : u.w_range) * 256.0) >> 8;
    t += R.tgt_distance * std::max(0, dist - rng);
    const int r = om::ftol(t + 100000.0);
    return r < 1 ? 1 : r;
}

namespace {
// W7-07-03 [RE: Greatest_Threat FUN_006F8320 -> Evaluate_Object FUN_006F71D0 /
// FUN_006F7E90]: one pass over units and buildings in the threat radius, the
// highest threat_value kept (ties: the first in scan order). The scan stops at
// radius/4, then radius/2, as soon as a band holds a candidate. Evaluate_Object
// rejects: not an enemy, hidden / disguised / unseen, not a legal threat
// (LegalTarget, IsInPlayfield, Insignificant, NoThreat missions), Verses
// <= 2% (W7-07-04 [RE: 0x6F7270..0x6F727C]: "1% means no passive acquire").
// `in_range_only`: Sticky picks only what its weapon reaches.
template <class BucketOf>
bool w7_greatest_threat(const World& world, std::vector<Unit>& units, std::vector<Building>& buildings, Unit& u,
                        int i, bool in_range_only, const std::vector<std::vector<int>>& buckets, int bgw, int bgh,
                        BucketOf bucket_of) {
    const double acq = acquire_radius(u);
    const double rmax = std::max(std::max(slot_range(u, 0), slot_range(u, 1)), acq) + 1.0;
    struct Cand { double d; int score; int unit; int bld; };
    std::vector<Cand> cands;
    const int bx0 = bucket_of(u.fx - rmax, bgw), bx1 = bucket_of(u.fx + rmax, bgw);
    const int by0 = bucket_of(u.fy - rmax, bgh), by1 = bucket_of(u.fy + rmax, bgh);
    for (int by = by0; by <= by1; ++by)
        for (int bx = bx0; bx <= bx1; ++bx)
            for (int j : buckets[static_cast<std::size_t>(by) * bgw + bx]) {
                if (j == i || !valid_target(units, world, u, j) ||
                    (!u.detect_disguise && world.disguise_fools(u.house, units[j])) ||   // W7-01-02
                    !units[j].seen_by_house(u.house) || !world.legal_threat_unit(u.house, units[j]) ||
                    (u.w_mind_control && units[j].tg.t.immune_psionics))
                    continue;
                const Unit& v = units[static_cast<std::size_t>(j)];
                const int ws = unit_weapon_for_unit(u, v);
                if (ws < 0 || slot_verses(u, ws, v.armor_ix) <= 0.02) continue;   // W7-07-04
                const double d = dist_leptons(u.fx, u.fy, v.fx, v.fy);
                const bool by_slot = in_range_only || u.is_aircraft || (u.deploy_fire && u.deployed);
                const double rng = by_slot ? slot_range(u, ws) : acq;
                if (!(by_slot && range_infinite(rng)) && d > rng * kLeptonsPerCell) continue;   // fv7c
                cands.push_back({d, 0, j, -1});
            }
    if (u.abil.fire_kind == AbilityKind::None && !u.abil.c4) {
        const std::vector<BuildingTypeDef>& bt = world.btypes();
        for (int bj = 0; bj < static_cast<int>(buildings.size()); ++bj) {
            const Building& b = buildings[static_cast<std::size_t>(bj)];
            if (!b.alive || b.state == Building::State::Dead) continue;
            // R-064 [RE: Evaluate_Object @0x6F7ADB]: a human unit skips an
            // unarmed building, a computer unit may pick any.
            if (b.type < 0 || b.type >= static_cast<int>(bt.size()) ||
                (!bt[static_cast<std::size_t>(b.type)].armed &&
                 !(world.unit_berserk(i) || (world.house_is_computer(u.house) && world.ai_threat_any_building()))))
                continue;
            if (!world.combat_hostile(u,b) || !world.legal_threat_building(u.house, b) ||
                !world.object_visible_at(u.house, b.cx, b.cy))
                continue;
            const int ws = unit_weapon_for_building(u, b.armor_ix, b.bomb_frame >= 0,
                                                    world.an_victim_naval_cell(b.cx + b.fw / 2, b.cy + b.fh / 2));
            if (ws < 0 || slot_verses(u, ws, b.armor_ix) <= 0.02) continue;   // W7-07-04
            // the building path engages from weapon range only (it never
            // approaches an auto-acquired structure)
            const double rng = range_infinite(slot_range(u, ws)) ? 1e18 : slot_range(u, ws) * kLeptonsPerCell;   // fv7c
            double bd = -1.0;
            for (const Cellxy& cell : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
                const double d = dist_leptons(u.fx, u.fy, cell.x, cell.y);
                if (bd < 0.0 || d < bd) bd = d;
            }
            if (bd < 0.0 || bd > rng) continue;
            cands.push_back({bd, 0, -1, bj});
        }
    }
    if (cands.empty()) return false;
    const double radius = acq * kLeptonsPerCell;
    double band = radius / 4.0;
    bool any = false;
    for (const Cand& c : cands) any = any || c.d <= band;
    if (!any) {
        band = radius / 2.0;
        for (const Cand& c : cands) any = any || c.d <= band;
    }
    if (!any) band = 1e300;
    int best = -1, best_score = 0;
    for (std::size_t k = 0; k < cands.size(); ++k) {
        Cand& c = cands[k];
        if (c.d > band) continue;
        c.score = c.unit >= 0 ? threat_value_unit(world, u, i, units[static_cast<std::size_t>(c.unit)])
                              : threat_value_building(world, u, buildings[static_cast<std::size_t>(c.bld)]);
        if (best < 0 || c.score > best_score) { best = static_cast<int>(k); best_score = c.score; }
    }
    if (best < 0) return false;
    if (cands[static_cast<std::size_t>(best)].unit >= 0) u.target = cands[static_cast<std::size_t>(best)].unit;
    else u.target_building = cands[static_cast<std::size_t>(best)].bld;
    return true;
}
}  // namespace

// Gap fix C (G-50) [RE: veterancy.md V7, FUN_006FC460 0x6FC55E..0x6FC60C --
// supersedes combat.md §5]: RearmTimer = ftol(ROF * houseROF + Random(0,2)),
// drawn from the sync RNG at EVERY rearm. houseROF = 1.0 ([Normal] ROF=1.0).
// `rng` null (direct pure-test callers) -> no jitter.
// Net L0: a WHOLE-FRAME count, like the original's frame CDTimerClass. The old
// seconds value ((ROF + j) / 15, minus 1/15 per tick) expired one frame late
// for 247 of 300 lengths (docs/gap/net/03-sim-bitexact.md D3, timer_drift.py).
int rearm_frames_for(int rof_frames, ProjRng* rng) {
    const int jitter = rng ? rng->random(0, 2) : 0;
    return rof_frames + jitter;
}

int burst_rearm_frames(int& burst_index, int burst, int rof_frames, ProjRng* rng) {
    burst = std::max(1, burst);
    ++burst_index;
    int frames;
    if (burst_index < burst) frames = rng ? rng->random(3, 5) : 3;
    else frames = rearm_frames_for(rof_frames, rng);
    burst_index %= burst;
    return frames;
}

int unit_rearm_frames(Unit& u, ProjRng* rng) {
    if (u.tg.w7_rof && u.tg.rof_raw > 0) {
        // W7-07-14 [RE: FUN_006FC460 0x6FC55E..0x6FC60C]: r = ftol(ROF *
        // houseROF + Random(0,2)); with the ROF ability r = ftol(r *
        // VeteranROF) -- the jitter BEFORE the veteran multiply (a veteran GI,
        // ROF 20: {12,12,13}, not {12,13,14}).
        int& bi = u.w6.burst_index;
        const int burst = std::max(1, u.w6.burst);
        ++bi;
        int frames;
        if (bi < burst) {
            frames = rng ? rng->random(3, 5) : 3;
        } else {
            const int j = rng ? rng->random(0, 2) : 0;
            frames = om::ftol(u.tg.rof_raw * u.tg.house_rof + j);
            if (u.tg.rof_vet != 1.0) frames = om::ftol(frames * u.tg.rof_vet);
        }
        bi %= burst;
        return frames;
    }
    return burst_rearm_frames(u.w6.burst_index, u.w6.burst, u.w_rof, rng);
}

// P2: the single-target damage tail extracted VERBATIM from try_fire's old
// else-branch (same statements, same order) so the instant path and the
// projectile detonation path share one implementation.
void deliver_unit_hit(std::vector<Unit>& units, Occupancy& occ, int attacker_idx, int victim_idx) {
    deliver_hit_from(units, occ, units[attacker_idx], attacker_idx, -1, victim_idx);
}

// Gap fix C: the single-target hit generalized over the shooter -- a unit
// (attacker_idx >= 0, `u` = units[attacker_idx]) or a base defense
// (attacker_building >= 0, `u` = a transient shooter carrying the building's
// weapon). Body = the P2 deliver_unit_hit tail; the retaliation target is the
// attacking unit, or the attacking building for a defense shot.
void deliver_hit_from(std::vector<Unit>& units, Occupancy& occ, const Unit& u, int attacker_idx,
                      int attacker_building, int victim_idx) {
    Unit& v = units[victim_idx];
    // W6-04-09 [RE: FUN_004D7250 @0x4D7262: a Sonic warhead on a host with a
    // parasite (+0x694) -> ParasiteClass::ExitUnit]: marked here, the exit
    // runs in World::step_abilities (combat.cpp cannot move units).
    if (u.w6.sonic && v.parasite_on_me >= 0) v.w6.sonic_hit = true;
    // P5 [RE: veterancy.md V6]: a vet-enabled World arms v.vet.recv; the
    // receive chain (ArmorMultiplier, STRONGER, max 1) runs BEFORE the Verses.
    // v.vet.recv is false in every legacy World -> raw == u.w_damage as before.
    int raw = u.w_damage;
    if (v.vet.recv) raw = vet_receive(raw, v.vet);
    raw = inf_prone_damage(v.ia, raw, u.w_prone_damage);   // W3-B ProneDamage (armed infantry only)
    // W7-04-14 [RE: FootClass::ReceiveDamage 0x4D7250]: a hit from anything
    // but the riding parasite feeds its SuppressionTimer with the RAW damage
    // (before Verses and the victim's armour); World::step_abilities reads it.
    if (v.parasite_on_me >= 0 && v.parasite_on_me != attacker_idx)
        v.w7.host_raw_hit = std::max(v.w7.host_raw_hit, u.w_damage);
    // W2-AN-04: an Iron-Curtained unit takes no damage. W7-01-09 [RE:
    // 0x700DA0 / 0x70BA30]: neither does one being erased (BeingWarpedOut
    // +0x270).
    const int dmg = (v.an.iron_left_frames > 0.0 || v.warp_remaining > 0) ? 0 : damage_vs(raw, u.w_verses, v.armor_ix);
    v.hp -= dmg;
    feed_damage(false, victim_idx, u.house, dmg);   // W6-03-04 anger
    if (dmg > 0)   // W3-B fear (FUN_00517db5; inert unless armed)
        inf_on_hit(v.ia, attacker_idx >= 0 || attacker_building >= 0,
                   v.max_hp > 0 ? static_cast<double>(v.hp) / v.max_hp : 0.0);
    // Return fire only if IDLE (spec: "return fire when hit" applies to
    // idle/guarding units; a unit under a move order keeps walking rather
    // than stopping to retaliate). Engineer/Spy Capture [RE: capture.md
    // §Design 6]: a still-DISGUISED attacker is treated as friendly, so the
    // victim does NOT retaliate-acquire it.
    // R3: a flyer cruising to a flight goal (Kirov, Rocketeer) is under a
    // move order too -- the same gate as auto-acquire [aircraft.md §6: a Move
    // order overrides auto-attack]; it used to stop on the first IFV that hit it.
    // w7fix-combat: with the W7 rules armed the World resolves the hit
    // (CanRetaliateToAttacker 0x707C40 + Override_Mission, call-for-help,
    // damage scatter) -- the inline idle-only rule below is the pre-W7 path.
    if (hit_log().sink) {
        if (dmg > 0 && (attacker_idx >= 0 || attacker_building >= 0))
            log_hit({victim_idx, false, attacker_idx, attacker_building, u.house,
                     attacker_idx >= 0 ? units[attacker_idx].cell_x : u.cell_x,
                     attacker_idx >= 0 ? units[attacker_idx].cell_y : u.cell_y, u.disguised});
    } else if (v.target == -1 && !v.moving() && !v.has_flight_goal && !u.disguised && !v.abil.no_retaliate) {
        // Idle victim retaliates (docs/re/combat.md §7) and records ITS OWN
        // guard anchor at its current position (retaliation is a form of
        // auto-acquire: attack_order stays false).
        if (attacker_idx >= 0) {
            v.target = attacker_idx;
        } else if (attacker_building >= 0 && v.target_building == -1) {
            v.target_building = attacker_building;   // shoot back at the defense
        }
        v.guard_x = v.cell_x;
        v.guard_y = v.cell_y;
    }
    if (v.hp <= 0) {
        v.die_time = 0.0;
        v.ia.death = u.w_inf_death;       // W3-B: the killing warhead's InfDeath
        v.vet.killed_by = attacker_idx;   // P5 [V3]: inert stamp (credit pass only)
        occ.release_all_of(victim_idx, -1, -1);
        v.target = -1;
        v.target_building = -1;
        v.path.clear();
        v.path_idx = 0;
    }
}

// P2: the attack-building damage tail extracted from step_combat's building
// branch (same statements, same order). The attacker's own target_building is
// cleared only if it still names this building -- always true on the instant
// path (it IS the branch condition), so that path is byte-identical; at a
// projectile detonation the attacker may have re-targeted meanwhile.
void deliver_building_hit(std::vector<Unit>& units, std::vector<Building>& buildings,
                          int attacker_idx, int b_idx) {
    Unit& u = units[attacker_idx];
    Building* b = &buildings[b_idx];
    // P5 [RE: veterancy.md V6]: a vet-enabled World arms b->vet_recv
    // (ArmorMultiplier, max 1, then Verses); false in every legacy World.
    int raw = u.w_damage;
    if (b->vet_recv) raw = receive_damage(raw, b->armor_mult, false, 1.0);
    const int dmg = (b->iron_left_frames > 0.0 || b->warp_remaining > 0) ? 0 : damage_vs(raw, u.w_verses, b->armor_ix);   // W2-AN-04, W7-01-09
    b->hp -= dmg;
    feed_damage(true, b_idx, u.house, dmg);   // W6-03-04 anger
    if (b->hp <= 0) {
        b->killed_by = attacker_idx;   // P5 [V3]: inert stamp
        kill_building(*b);
        if (u.target_building == b_idx) {
            u.target_building = -1;
            u.attack_order = false;
        }
        // Clear every OTHER unit's target_building pointing at the now-dead
        // building immediately (their own next-call validity check would also
        // catch it via state == Dead, but the spec calls for this to happen
        // within the same call).
        for (Unit& other : units)
            if (&other != &u && other.target_building == b_idx)
                other.target_building = -1;
    }
}

// CB-2 air-to-ground splash [RE-GATE APPLIED: aircraft.md §4 -> combat.md §7,
// FUN_00489190]. Damages every ENEMY, non-carried, non-aircraft unit within
// round(cell_spread) cells of `center` (radius in leptons = round(cell_spread)
// * kLeptonsPerCell; BlimpHE CellSpread=2 -> ftol(2*256)=512 leptons). Damage
// falls off LINEARLY from 100% at the impact point to `percent_at_max` at the
// edge (the adjudicated approximation of the un-unwound §7 curve), then runs
// the standard damage_vs(Verses) truncated per victim (combat.md §3). Ground
// AA is out of scope, so victims do NOT retaliate against the aircraft.
// CB-4 [RE: superweapon.md §2/§5]: PROMOTED to ra2engine (external linkage) so
// World::fire_superweapon can REUSE it for the nuke detonation -- body BYTE-
// IDENTICAL to the CB-2 anonymous-namespace original; only its linkage moved.
void apply_spread_damage(std::vector<Unit>& units, Occupancy& occ, const Unit& attacker,
                         const Unit& center) {
    const double radius_cells = std::round(attacker.w_cell_spread);   // round(CellSpread)
    const double radius_lept = radius_cells * kLeptonsPerCell;        // splash radius (leptons)
    const double pmax = attacker.w_percent_at_max;                    // edge scale (BlimpHE .5)
    for (int j = 0; j < static_cast<int>(units.size()); ++j) {
        Unit& w = units[j];
        if (!w.alive || w.die_time >= 0.0) continue;                  // dead/dying: skip
        if (!unit_combat_exposed(w)) continue;                     // carried: off-map
        // Gap fix C (G-44): the blast stays in its own layer -- an AA burst at
        // an airborne target hits airborne units, a ground blast ground units
        // (the original's 3-D distance; the clone has no shared Z scale).
        if (unit_airborne(w) != unit_airborne(center)) continue;
        // w7fix-combat (W7-07-17) [RE: DamageArea FUN_00489190 -- only an
        // AffectsAllies=no warhead spares the firer's own house]: with the W7
        // rules armed the splash hits the firer's own units too; the pre-W7
        // path spares them.
        if (w.house == attacker.house && (!hit_log().sink || !attacker.w_affects_allies)) continue;
        if (w.an.iron_left_frames > 0.0 || w.warp_remaining > 0) continue;   // W2-AN-04 Iron Curtain, W7-01-09 erase
        const double d = dist_leptons(center.fx, center.fy, w.fx, w.fy);
        if (d > radius_lept) continue;                               // outside the splash
        const double frac = radius_lept > 0.0 ? d / radius_lept : 0.0;
        const double scale = 1.0 - (1.0 - pmax) * frac;             // 1.0 -> pmax linear
        // P5 [RE: veterancy.md V6]: the receive chain runs on the full hit
        // before the falloff/Verses; only when the victim's hook is armed
        // (never in a legacy World -> the base expression is unchanged).
        const int full = w.vet.recv ? vet_receive(attacker.w_damage, w.vet) : attacker.w_damage;
        const int base = inf_prone_damage(w.ia, om::ftol(full * scale),   // trunc (combat.md §3)
                                          attacker.w_prone_damage);                // W3-B ProneDamage
        const int dmg = damage_vs(base, attacker.w_verses, w.armor_ix);
        w.hp -= dmg;
        feed_damage(false, j, attacker.house, dmg);   // W6-03-04 anger
        if (dmg > 0)   // W3-B fear (inert unless armed)
            inf_on_hit(w.ia, true, w.max_hp > 0 ? static_cast<double>(w.hp) / w.max_hp : 0.0);
        if (w.hp <= 0) {
            w.die_time = 0.0;
            w.ia.death = attacker.w_inf_death;   // W3-B
            // P5 [V3]: inert killer stamp; the attacker is a real unit only when
            // it lives in `units` (the nuke's virtual detonator does not).
            if (&attacker >= units.data() && &attacker < units.data() + units.size())
                w.vet.killed_by = static_cast<int>(&attacker - units.data());
            occ.release_all_of(j, -1, -1);
            w.target = -1;
            w.path.clear();
            w.path_idx = 0;
        }
    }
}

// ============================================================================
// W5-B R-060: the original's damage pipeline -- DamageArea (FUN_00489190) ->
// TechnoClass::ReceiveDamage (0x700DA0) -> ObjectClass::ReceiveDamage
// (FUN_005F4BA0) -> Modify_Damage (FUN_00489090). docs/re/combat.md §8.
// ============================================================================
namespace {
// DAT_007EC3D0: cumulative cell count of the ring set for ftol(CellSpread).
constexpr int kRingCount[12] = {1, 9, 21, 37, 61, 89, 121, 161, 205, 253, 309, 369};
// DAT_00ABC408: the 369 (dx, dy) cell offsets, nearest ring first (read out of
// the static initializer @0x561540 by emulating its stores).
constexpr signed char kRingOffsets[369][2] = {
    {0,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1}, {-1,-2}, {0,-2}, {1,-2},
    {-2,-1}, {2,-1}, {-2,0}, {2,0}, {-2,1}, {2,1}, {-1,2}, {0,2}, {1,2}, {-1,-3}, {0,-3}, {1,-3},
    {-2,-2}, {2,-2}, {-3,-1}, {3,-1}, {-3,0}, {3,0}, {-3,1}, {3,1}, {-2,2}, {2,2}, {-1,3}, {0,3},
    {1,3}, {-1,-4}, {0,-4}, {1,-4}, {-3,-3}, {-2,-3}, {2,-3}, {3,-3}, {-3,-2}, {3,-2}, {-4,-1}, {4,-1},
    {-4,0}, {4,0}, {-4,1}, {4,1}, {-3,2}, {3,2}, {-3,3}, {-2,3}, {2,3}, {3,3}, {-1,4}, {0,4},
    {1,4}, {-1,-5}, {0,-5}, {1,-5}, {-3,-4}, {-2,-4}, {2,-4}, {3,-4}, {-4,-3}, {4,-3}, {-4,-2}, {4,-2},
    {-5,-1}, {5,-1}, {-5,0}, {5,0}, {-5,1}, {5,1}, {-4,2}, {4,2}, {-4,3}, {4,3}, {-3,4}, {-2,4},
    {2,4}, {3,4}, {-1,5}, {0,5}, {1,5}, {-1,-6}, {0,-6}, {1,-6}, {-3,-5}, {-2,-5}, {2,-5}, {3,-5},
    {-4,-4}, {4,-4}, {-5,-3}, {5,-3}, {-5,-2}, {5,-2}, {-6,-1}, {6,-1}, {-6,0}, {6,0}, {-6,1}, {6,1},
    {-5,2}, {5,2}, {-5,3}, {5,3}, {-4,4}, {4,4}, {-3,5}, {-2,5}, {2,5}, {3,5}, {-1,6}, {0,6},
    {1,6}, {-1,-7}, {0,-7}, {1,-7}, {-3,-6}, {-2,-6}, {2,-6}, {3,-6}, {-5,-5}, {-4,-5}, {4,-5}, {5,-5},
    {-5,-4}, {5,-4}, {-6,-3}, {6,-3}, {-6,-2}, {6,-2}, {-7,-1}, {7,-1}, {-7,0}, {7,0}, {-7,1}, {7,1},
    {-6,2}, {6,2}, {-6,3}, {6,3}, {-5,4}, {5,4}, {-5,5}, {-4,5}, {4,5}, {5,5}, {-3,6}, {-2,6},
    {2,6}, {3,6}, {-1,7}, {0,7}, {1,7}, {-1,-8}, {0,-8}, {1,-8}, {-3,-7}, {-2,-7}, {2,-7}, {3,-7},
    {-5,-6}, {-4,-6}, {4,-6}, {5,-6}, {-6,-5}, {6,-5}, {-6,-4}, {6,-4}, {-7,-3}, {7,-3}, {-7,-2}, {7,-2},
    {-8,-1}, {8,-1}, {-8,0}, {8,0}, {-8,1}, {8,1}, {-7,2}, {7,2}, {-7,3}, {7,3}, {-6,4}, {6,4},
    {-6,5}, {6,5}, {-5,6}, {-4,6}, {4,6}, {5,6}, {-3,7}, {-2,7}, {2,7}, {3,7}, {-1,8}, {0,8},
    {1,8}, {-1,-9}, {0,-9}, {1,-9}, {-3,-8}, {-2,-8}, {2,-8}, {3,-8}, {-5,-7}, {-4,-7}, {4,-7}, {5,-7},
    {-6,-6}, {6,-6}, {-7,-5}, {7,-5}, {-7,-4}, {7,-4}, {-8,-3}, {8,-3}, {-8,-2}, {8,-2}, {-9,-1}, {9,-1},
    {-9,0}, {9,0}, {-9,1}, {9,1}, {-8,2}, {8,2}, {-8,3}, {8,3}, {-7,4}, {7,4}, {-7,5}, {7,5},
    {-6,6}, {6,6}, {-5,7}, {-4,7}, {4,7}, {5,7}, {-3,8}, {-2,8}, {2,8}, {3,8}, {-1,9}, {0,9},
    {1,9}, {-1,-10}, {0,-10}, {1,-10}, {-3,-9}, {-2,-9}, {2,-9}, {3,-9}, {-5,-8}, {-4,-8}, {4,-8}, {5,-8},
    {-7,-7}, {-6,-7}, {6,-7}, {7,-7}, {-7,-6}, {7,-6}, {-8,-5}, {8,-5}, {-8,-4}, {8,-4}, {-9,-3}, {9,-3},
    {-9,-2}, {9,-2}, {-10,-1}, {10,-1}, {-10,0}, {10,0}, {-10,1}, {10,1}, {-9,2}, {9,2}, {-9,3}, {9,3},
    {-8,4}, {8,4}, {-8,5}, {8,5}, {-7,6}, {7,6}, {-7,7}, {-6,7}, {6,7}, {7,7}, {-5,8}, {-4,8},
    {4,8}, {5,8}, {-3,9}, {-2,9}, {2,9}, {3,9}, {-1,10}, {0,10}, {1,10}, {0,11}, {0,-11}, {-1,11},
    {1,11}, {-1,-11}, {1,-11}, {-2,11}, {2,11}, {-2,-11}, {2,-11}, {-3,11}, {3,11}, {-3,-11}, {-3,11}, {-4,9},
    {4,9}, {-4,-9}, {4,-9}, {-5,9}, {5,9}, {-5,-9}, {5,-9}, {-6,8}, {6,8}, {-6,-8}, {6,-8}, {-7,8},
    {7,8}, {-7,-8}, {7,-8}, {-8,7}, {8,7}, {-8,-7}, {8,-7}, {-8,6}, {8,6}, {-8,-6}, {8,-6}, {-9,5},
    {9,5}, {-9,-5}, {9,-5}, {-9,4}, {9,4}, {-9,-4}, {9,-4}, {-10,3}, {10,3}, {-10,-3}, {10,-3}, {-10,2},
    {10,2}, {-10,-2}, {10,-2}, {-11,1}, {11,1}, {-11,-1}, {11,-1}, {11,0}, {-11,0},
};
constexpr int kLevelLept = 104;     // Unsorted::LevelHeight (DAT_0089D7E0)
constexpr int kMaxDamage = 10000;   // rulesmd [CombatDamage] MaxDamage=10000 (Rules+0x16C8)

int ftol_i(double v) { return om::ftol(v); }   // _ftol: truncate toward zero

int dist3_i(int dx, int dy, int dz) {
    return ftol_i(om::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                            static_cast<double>(dz) * dz));
}

int lept_cell(int l) { return om::ftol(std::floor((l + 128.0) / 256.0)); }

// Kill bookkeeping for a unit whose hp reached 0 (the deliver_hit_from tail).
void start_unit_death(std::vector<Unit>& units, Occupancy& occ, int j, const Unit& shooter,
                      int src_unit) {
    Unit& w = units[static_cast<std::size_t>(j)];
    w.die_time = 0.0;
    w.ia.death = shooter.w_inf_death;   // W3-B: the killing warhead's InfDeath
    w.vet.killed_by = src_unit;         // P5 [V3]
    occ.release_all_of(j, -1, -1);
    w.target = -1;
    w.target_building = -1;
    w.path.clear();
    w.path_idx = 0;
}
}  // namespace

int spread_ring_count(int r) {
    if (r < 0) r = 0;
    if (r > 11) r = 11;
    return kRingCount[r];
}

void spread_ring_offset(int k, int& dx, int& dy) {
    dx = kRingOffsets[k][0];
    dy = kRingOffsets[k][1];
}

int modify_damage(int damage, const std::vector<int>& verses, int armor_ix, double cell_spread,
                  double percent_at_max, int distance) {
    if (damage == 0) return 0;
    if (damage < 0) return distance < 8 ? damage : 0;   // healing: point-blank only
    const float fd = static_cast<float>(damage);
    const float atmax = fd * static_cast<float>(percent_at_max);   // FMUL [wh+0x12C]
    const int spread = ftol_i(static_cast<float>(cell_spread) * 256.0f);   // [wh+0x124]*256
    int d = damage;
    if (atmax != fd && spread != 0)
        d = ftol_i((static_cast<double>(fd) - atmax) * (spread - distance) / spread + atmax);
    if (d <= 0) d = 0;
    double v = 1.0;
    if (!verses.empty()) {
        int idx = armor_ix;
        if (idx < 0 || idx >= static_cast<int>(verses.size())) idx = 0;
        v = verses[static_cast<std::size_t>(idx)] / 100.0;
    }
    int r = ftol_i(d * v);
    if (r >= kMaxDamage) r = kMaxDamage;
    return r;
}

int unit_lept_x(const Unit& u) { return ftol_i(u.fx * 256.0 + u.sub_x); }   // + W5-B sub-cell offset
int unit_lept_y(const Unit& u) { return ftol_i(u.fy * 256.0 + u.sub_y); }
int unit_lept_z(const Unit& u) {
    // w8fix-layer W8-03-08 / W8-04-02 [RE: an OnBridge object's Z carries
    // BridgeHeight (0x51F810 adds DAT_00A8E19C; JumpjetLoco Descending
    // @0x54C5A9 lands at floor + BridgeHeight)]: a ground unit on a deck
    // stands 4 levels (416 leptons) up. A flier's altitude already counts the
    // deck (step_flight's floor, FUN_0054D4B0).
    if (unit_parachuting(u))
        return (u.level + (u.on_deck ? 4 : 0)) * kLevelLept + ftol_i(u.an.para_alt);
    if (unit_airborne(u)) return u.level * kLevelLept + ftol_i(u.altitude);
    return (u.level + (u.on_deck ? 4 : 0)) * kLevelLept;
}

void building_aim_lept(const Building& b, int level, int& x, int& y, int& z) {
    // Original: cell origin*256 + foundation*128; the clone's lepton origin is
    // the cell centre (cell*256), hence the -128.
    x = b.cx * 256 + b.fw * 128 - 128;
    y = b.cy * 256 + b.fh * 128 - 128;
    z = level * kLevelLept;
}

namespace {
// FUN_004CC020 / FUN_004CC280 (cliff half): step max(|dcx|,|dcy|) times from
// the shooter toward the target; a step whose cell is more than 3 levels above
// the previous step's cell blocks the shot (the target's own cell is never
// stepped into). Walls ([WallModel], SubjectToWalls) are not traced
// [RE-FALLBACK: the clone has no wall overlay state on the combat path].
bool cliff_blocks(const World& world, int ax, int ay, int az, int bx, int by, int bz) {
    const int acx = lept_cell(ax), acy = lept_cell(ay);
    const int n = std::max(std::abs(acx - lept_cell(bx)), std::abs(acy - lept_cell(by)));
    if (n == 0) return false;
    const int sx = (bx - ax) / n, sy = (by - ay) / n;
    (void)az; (void)bz;
    int pcx = acx, pcy = acy;
    for (int k = 0; k < n; ++k) {
        const int cx = lept_cell(ax + sx * k), cy = lept_cell(ay + sy * k);
        // w8fix-layer W8-03-08 [RE: FUN_004CC280 reads FUN_00487C60: a deck
        // cell counts 4 levels higher]
        if (world.cell_bridge_level(cx, cy) - world.cell_bridge_level(pcx, pcy) > 3) return true;
        pcx = cx;
        pcy = cy;
    }
    return false;
}

// FUN_006F6490: the shooter `diff` levels above the target ->
// n = diff / ElevationIncrement, bonus = min(n * IncrementBonus, Cap) cells,
// returned as sqrt((ftol(bonus) * 256)^2 + (104 * diff)^2) leptons.
int elevation_bonus(const World& world, int diff) {
    const World::ElevationRules& er = world.elevation_rules();
    if (!er.armed || diff <= 0) return 0;
    const int n = diff / er.increment;
    double b = n * er.bonus;
    if (b > er.cap) b = er.cap;
    const double cells = ftol_i(b) * 256.0;
    const double h = static_cast<double>(kLevelLept) * diff;
    return ftol_i(om::sqrt(cells * cells + h * h));
}
}  // namespace

bool weapon_in_range(const World& world, const Unit& u, int tx, int ty, int tz, int extra) {
    return weapon_in_range_from(world, u, unit_lept_x(u), unit_lept_y(u), unit_lept_z(u), tx, ty, tz,
                                extra);
}

bool weapon_in_range_from(const World& world, const Unit& u, int ux, int uy, int uz, int tx, int ty,
                          int tz, int extra) {
    // W7-05-11 [WeaponType CellRangefinding=yes, e.g. the Kirov's BlimpBomb]:
    // the range is measured between the CELL centres of shooter and target.
    if (u.an.t.cell_rangefinding && u.w_active == 0) {
        ux = lept_cell(ux) * 256;
        uy = lept_cell(uy) * 256;
        tx = lept_cell(tx) * 256;
        ty = lept_cell(ty) * 256;
    }
    if (range_infinite(u.w_range)) return true;   // fv7c [RE: FUN_006F6750 head]
    int range = ftol_i(u.w_range * 256.0) + extra;
    if (u.w_proj.subject_to_elevation)
        range += elevation_bonus(world, world.cell_bridge_level(lept_cell(ux), lept_cell(uy)) -   // w8fix-layer:
                                            world.cell_bridge_level(lept_cell(tx), lept_cell(ty)));   // FUN_00487C60
    // An airborne shooter measures in the ground plane (FUN_006F6750's
    // TechnoClass+0x29B branch: 2-D distance + the airburst lowering).
    if (dist3_i(tx - ux, ty - uy, unit_airborne(u) ? 0 : tz - uz) > range) return false;
    if (u.w_proj.subject_to_cliffs && world.elevation_rules().armed &&
        cliff_blocks(world, ux, uy, uz, tx, ty, tz))
        return false;
    return true;
}

bool unit_in_weapon_range(const World& world, const Unit& u, const Unit& t) {
    // W6-04-16 [RE-FALLBACK: TechnoType AirRangeBonus is added to the range
    // against an airborne target]: the IFV ([FV] AirRangeBonus=4) reaches
    // aircraft at 10 cells. 0 for every other type.
    const int bonus = unit_airborne(t) ? ftol_i(u.w6.t.air_range_bonus * 256.0) : 0;
    return weapon_in_range(world, u, unit_lept_x(t), unit_lept_y(t), unit_lept_z(t), bonus);
}

bool can_crush(const Unit& u, const Unit& v) {
    if (v.is_aircraft || unit_airborne(v) || v.is_naval) return false;
    // W7-08-04 / W7-07-29 [RE: IsCrushable FUN_005F64C0 @0x5F651E / 0x5F656B,
    // vt+0x160]: an Iron-Curtained object is never crushed (both branches).
    if (v.an.iron_left_frames > 0) return false;
    // W6-04-14 [RE-FALLBACK, rules semantics]: an OmniCrusher crushes any
    // ground unit that is not OmniCrushResistant (vehicles included).
    if (u.w6.t.omni_crusher) return !v.w6.t.omni_crush_resistant;
    // W6-04-29: a deployed DeployedCrushable=no infantry (the Guardian GI)
    // cannot be run over.
    if (v.deployed && !v.w6.t.deployed_crushable) return false;
    return v.crushable && !v.is_vehicle;
}

WallStep reduce_wall(int stage, int levels, int strength, int dmg, bool connected, ProjRng* rng) {
    WallStep r;
    r.stage = stage;
    levels = std::max(1, levels);
    // FUN_00480BC0: a hit below Strength advances only if Random(0,Strength) < dmg.
    if (dmg != -1 && dmg < strength && rng && dmg <= rng->random(0, strength)) return r;
    r.advanced = true;
    r.stage = std::min(stage + 1, 15);                 // OverlayData += 0x10
    r.cascade = r.stage == levels - 1 && levels > 2;
    if (dmg == -1 || r.stage >= levels || (r.stage == levels - 1 && !connected)) r.destroyed = true;
    return r;
}

int wall_hit_damage(int wall_flags, bool wood_wall, int damage) {
    if (wall_flags & kW6WallAbs) return -1;
    if ((wall_flags & kW6Wall) || ((wall_flags & kW6Wood) && wood_wall)) return damage > 0 ? damage : 0;
    return 0;
}

namespace {
constexpr int kWoodArmor = 6;   // armor_names(): none, flak, plate, light, medium, heavy, WOOD
int wall_neighbour(const std::vector<Building>& buildings, const Building& b, int dx, int dy) {
    for (int j = 0; j < static_cast<int>(buildings.size()); ++j) {
        const Building& o = buildings[static_cast<std::size_t>(j)];
        if (&o == &b || !o.alive || o.state == Building::State::Dead || o.type != b.type) continue;
        if (o.cx == b.cx + dx && o.cy == b.cy + dy) return j;
    }
    return -1;
}
void wall_building_reduce(const World& world, std::vector<Unit>& units, std::vector<Building>& buildings,
                          int bi, int dmg) {
    Building& b = buildings[static_cast<std::size_t>(bi)];
    if (!b.alive || b.state == Building::State::Dead || dmg == 0) return;
    const int levels = world.btypes()[static_cast<std::size_t>(b.type)].w6.wall_levels;
    static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};   // N, E, S, W
    int nb[4];
    bool connected = false;
    for (int k = 0; k < 4; ++k) {
        nb[k] = wall_neighbour(buildings, b, kDx[k], kDy[k]);
        connected = connected || nb[k] >= 0;
    }
    const WallStep r = reduce_wall(b.w6.wall_stage, levels, b.max_hp, dmg, connected, world.combat_rng_ptr());
    if (!r.advanced) return;
    b.w6.wall_stage = r.stage;
    if (r.cascade)
        for (int k = 0; k < 4; ++k)
            if (nb[k] >= 0 && buildings[static_cast<std::size_t>(nb[k])].w6.wall_stage < 1)
                wall_building_reduce(world, units, buildings, nb[k], 200);
    Building& w = buildings[static_cast<std::size_t>(bi)];
    if (r.destroyed) {
        w.hp = 0;
        kill_building(w);
        for (Unit& o : units)
            if (o.target_building == bi) {
                o.target_building = -1;
                o.attack_order = false;
            }
    } else {
        // The health bar follows the stage (a wall has no HP of its own).
        w.hp = std::max(1, w.max_hp - w.max_hp * r.stage / std::max(1, levels));
    }
}
}  // namespace

bool w6_wall_building_hit(const World& world, std::vector<Unit>& units, std::vector<Building>& buildings,
                          int bi, int wall_flags, int damage) {
    if (bi < 0 || bi >= static_cast<int>(buildings.size())) return false;
    const Building& b = buildings[static_cast<std::size_t>(bi)];
    if (b.type < 0 || b.type >= static_cast<int>(world.btypes().size()) ||
        world.btypes()[static_cast<std::size_t>(b.type)].w6.wall_levels <= 0)
        return false;
    if (b.iron_left_frames > 0.0) return true;
    wall_building_reduce(world, units, buildings, bi,
                         wall_hit_damage(wall_flags, b.armor_ix == kWoodArmor, damage));
    return true;
}

AreaResult damage_area(const World& world, std::vector<Unit>& units,
                       std::vector<Building>& buildings, Occupancy& occ, const Unit& shooter,
                       int src_unit, int src_building, int ix, int iy, int iz,
                       const AreaBuildingHook* building_hook) {
    AreaResult res;
    if (shooter.w_damage == 0) return res;
    const int radius = ftol_i(static_cast<float>(shooter.w_cell_spread) * 256.0f);   // local_80
    const int rings = spread_ring_count(ftol_i(shooter.w_cell_spread));
    const int icx = lept_cell(ix), icy = lept_cell(iy);
    const int ground_z = world.cell_level(icx, icy) * kLevelLept;
    // w8fix-layer W8-04-02 / W8-05-06 [RE: DamageArea FUN_00489190: when the
    // impact cell has the bridge flag (+0x140 & 0x100) and the impact Z is
    // above floor + BridgeHeight/2 the ring loop walks every cell's AltObject
    // chain +0xE8 (the deck objects), else FirstObject +0xE4 -- never both]:
    // the blast hits one layer only.
    const bool deck_blast = world.deck_intact(icx, icy) && iz > ground_z + 4 * kLevelLept / 2;
    // Ring membership of a cell offset: its index in the scan order, else -1.
    auto ring_index = [&](int dx, int dy) {
        if (dx < -11 || dx > 11 || dy < -11 || dy > 11) return -1;
        for (int k = 0; k < rings; ++k)
            if (kRingOffsets[k][0] == dx && kRingOffsets[k][1] == dy) return k;
        return -1;
    };
    // AffectsAllies=no: the firer's house allied (or equal) to the victim's -> 0.
    auto spared = [&](int victim_house) {
        if (shooter.w_affects_allies) return false;
        if (src_unit < 0 && src_building < 0) return false;   // ReceiveDamage needs a source
        return shooter.house == victim_house || world.are_allied(shooter.house, victim_house);
    };

    // --- units: (ring index, unit index) order = the cell-chain scan order.
    struct Hit { int ring; int idx; int dist; };
    std::vector<Hit> hits;
    for (int j = 0; j < static_cast<int>(units.size()); ++j) {
        const Unit& w = units[static_cast<std::size_t>(j)];
        if (!w.alive || w.die_time >= 0.0 || !unit_combat_exposed(w)) continue;
        if (j == src_unit) continue;                                 // the source itself
        const bool air = unit_airborne(w);
        if (air && iz <= ground_z) continue;     // ground burst: the air layer is not scanned
        if (!air && (w.on_deck != 0) != deck_blast) continue;   // w8fix-layer: the other chain
        const int wx = unit_lept_x(w), wy = unit_lept_y(w), wz = unit_lept_z(w);
        int k = 0;
        if (!air) {
            k = ring_index(lept_cell(wx) - icx, lept_cell(wy) - icy);
            if (k < 0) continue;
        }
        int d = dist3_i(wx - ix, wy - iy, wz - iz);
        if (d > radius) continue;
        if (air) d /= 2;                         // an aircraft in the air: distance halved
        hits.push_back({k, j, d});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit& a, const Hit& b) { return a.ring < b.ring; });
    for (const Hit& h : hits) {
        Unit& w = units[static_cast<std::size_t>(h.idx)];
        if (!w.alive || w.die_time >= 0.0) continue;
        if (spared(w.house)) continue;
        if (shooter.w6.sonic && w.parasite_on_me >= 0) w.w6.sonic_hit = true;   // W6-04-09
        if (w.parasite_on_me >= 0 && w.parasite_on_me != src_unit)            // W7-04-14
            w.w7.host_raw_hit = std::max(w.w7.host_raw_hit, shooter.w_damage);
        if (w.an.iron_left_frames > 0.0 || w.warp_remaining > 0) continue;   // W2-AN-04 Iron Curtain, W7-01-09 erase
        // TechnoClass::ReceiveDamage: the house/veterancy armour multipliers on
        // the raw damage, floor 1 (P5 vet_receive; W3-B ProneDamage first).
        int raw = shooter.w_damage;
        if (w.vet.recv) raw = vet_receive(raw, w.vet);
        raw = inf_prone_damage(w.ia, raw, shooter.w_prone_damage);
        if (raw < 1) raw = 1;
        if(world.chaos_weapon(shooter)) {
            // Techno receiver 0x701D6B: duration uses the damage kernel at
            // distance zero. Area admission above still bounds who is hit.
            const int duration=modify_damage(raw,shooter.w_verses,w.armor_ix,
                shooter.w_cell_spread,shooter.w_percent_at_max,0);
            world.note_chaos_hit(h.idx,duration);
            continue;
        }
        const int dmg = modify_damage(raw, shooter.w_verses, w.armor_ix, shooter.w_cell_spread,
                                      shooter.w_percent_at_max, h.dist);
        if (dmg <= 0) continue;
        w.hp -= dmg;
        feed_damage(false, h.idx, shooter.house, dmg);   // W6-03-04 anger
        res.damage += dmg;
        if (res.first_unit < 0 || h.idx < res.first_unit) res.first_unit = h.idx;
        inf_on_hit(w.ia, src_unit >= 0 || src_building >= 0,   // W3-B fear
                   w.max_hp > 0 ? static_cast<double>(w.hp) / w.max_hp : 0.0);
        // w7fix-combat: the W7 World resolves retaliation from the hit log.
        if (src_unit >= 0 || src_building >= 0) {
            HitRec r{h.idx, false, src_unit, src_building, shooter.house,
                     src_unit >= 0 ? units[static_cast<std::size_t>(src_unit)].cell_x : lept_cell(ix),
                     src_unit >= 0 ? units[static_cast<std::size_t>(src_unit)].cell_y : lept_cell(iy),
                     shooter.disguised};
            if (log_hit(r)) {
                if (w.hp <= 0) start_unit_death(units, occ, h.idx, shooter, src_unit);
                continue;
            }
        }
        // Idle retaliation (combat.md §7) -- only against an enemy firer: a
        // unit caught in friendly splash does not turn on its own side.
        if (!hit_log().sink && w.target == -1 && !w.moving() && !w.has_flight_goal && !shooter.disguised &&
            !w.abil.no_retaliate &&
            world.is_enemy(w.house, shooter.house)) {
            if (src_unit >= 0) {
                w.target = src_unit;
                w.guard_x = w.cell_x;
                w.guard_y = w.cell_y;
            } else if (src_building >= 0 && w.target_building == -1) {
                w.target_building = src_building;
                w.guard_x = w.cell_x;
                w.guard_y = w.cell_y;
            }
        }
        if (w.hp <= 0) start_unit_death(units, occ, h.idx, shooter, src_unit);
    }

    // W6-04-04/05 [RE: FUN_00489190 @0x489581..0x4895BD]: every ring cell
    // holding a wall takes Reduce_Wall per the warhead's wall keys -- a map
    // wall overlay through the World, a built wall (a 1x1 Wall=yes building)
    // below instead of the HP hit.
    if (world.chaos_weapon(shooter)) return res; // Status weapons never damage structures or walls.
    if (world.has_wall_overlays())
        for (int k = 0; k < rings; ++k)
            world.w6_overlay_wall_hit(icx + kRingOffsets[k][0], icy + kRingOffsets[k][1],
                                      shooter.w6.wall_flags, shooter.w_damage);

    // --- buildings: once per foundation cell inside the ring set (the
    // building sits in every foundation cell's object chain, AddContent
    // FUN_0047E7B0, and the victim list is not de-duplicated).
    for (int bi = 0; bi < static_cast<int>(buildings.size()); ++bi) {
        Building& b = buildings[static_cast<std::size_t>(bi)];
        if (bi == src_building) continue;
        if (b.type >= 0 && b.type < static_cast<int>(world.btypes().size()) &&
            world.btypes()[static_cast<std::size_t>(b.type)].w6.wall_levels > 0) {   // W6-04-04
            if (b.alive && b.state != Building::State::Dead && ring_index(b.cx - icx, b.cy - icy) >= 0)
                w6_wall_building_hit(world, units, buildings, bi, shooter.w6.wall_flags, shooter.w_damage);
            continue;
        }
        for (int cy = b.cy; cy < b.cy + b.fh; ++cy)
            for (int cx = b.cx; cx < b.cx + b.fw; ++cx) {
                if (!b.alive || b.state == Building::State::Dead) continue;
                if (ring_index(cx - icx, cy - icy) < 0) continue;
                const int cz = world.cell_level(cx, cy) * kLevelLept;
                int d;
                if (cx == icx && cy == icy) {
                    d = 0;
                    if (iz - cz > 2 * kLevelLept)
                        d = dist3_i(cx * 256 - ix, cy * 256 - iy, cz - iz) - 2 * kLevelLept;
                } else {
                    d = dist3_i(cx * 256 - ix, cy * 256 - iy, cz - iz);
                }
                if (d > radius) continue;
                if (spared(b.house)) continue;
                if (b.iron_left_frames > 0.0 || b.warp_remaining > 0) continue;   // W2-AN-04, W7-01-09
                int raw = shooter.w_damage;
                if (b.vet_recv) raw = receive_damage(raw, b.armor_mult, false, 1.0);
                if (raw < 1) raw = 1;
                int dmg = modify_damage(raw, shooter.w_verses, b.armor_ix, shooter.w_cell_spread,
                                        shooter.w_percent_at_max, d);
                if (dmg < 1) dmg = 1;                    // FUN_005F4BA0: buildings floor at 1
                if (building_hook && *building_hook && (*building_hook)(bi, dmg, d)) continue;
                b.hp -= dmg;
                feed_damage(true, bi, shooter.house, dmg);   // W6-03-04 anger
                if (src_unit >= 0)   // w7fix-combat: BuildingClass::ReceiveDamage call-for-help
                    log_hit({bi, true, src_unit, -1, shooter.house, units[static_cast<std::size_t>(src_unit)].cell_x,
                             units[static_cast<std::size_t>(src_unit)].cell_y, shooter.disguised});
                res.damage += dmg;
                if (b.hp <= 0) {
                    b.killed_by = src_unit;              // P5 [V3]
                    kill_building(b);
                    for (Unit& other : units)
                        if (other.target_building == bi) {
                            other.target_building = -1;
                            other.attack_order = false;
                        }
                }
            }
    }
    return res;
}

DamageFeed& damage_feed() {   // W6-03-04
    static DamageFeed f;
    return f;
}

HitLog& hit_log() {   // w7fix-combat
    static HitLog h;
    return h;
}

int armor_index(const std::string& armor) {
    const std::string lower = to_lower(armor);
    const auto& names = armor_names();
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == lower) return static_cast<int>(i);
    return 0;  // unknown -> "none"
}

int damage_vs(int damage, const std::vector<int>& verses, int ai) {
    if (verses.empty()) return damage;  // defensive: no Verses table -> 100%
    int idx = ai;
    if (idx < 0 || idx >= static_cast<int>(verses.size())) idx = 0;  // clamp -> "none"
    // docs/re/combat.md §3: truncate toward zero. Plain integer division
    // truncates toward zero for non-negative operands (damage and percent
    // are both always >= 0 here), which is exactly the `_ftol`-observed
    // rounding convention -- no separate std::trunc call needed.
    const int d = damage * verses[idx] / 100;
    // Gap fix C (G-50) [RE: abilities.md §H -- TechnoClass::ReceiveDamage
    // 0x700DA0 clamps a non-negative defended hit to at least 1 after the
    // armor multiply; rulesmd [CombatDamage] MinDamage=1]: 3 dmg x 25 % deals
    // 1, not 0. A 0 % Verses is "cannot affect" (GetFireError refuses the
    // shot; splash skips) and stays 0.
    if (damage > 0 && verses[idx] > 0 && d < 1) return 1;
    return d;
}

std::vector<MoveRequest> step_combat(const World& world, std::vector<Unit>& units,
                                     std::vector<Building>& buildings, Occupancy& occ, double dt,
                                     std::vector<ProjectileLaunch>* launches, ProjRng* rof_rng) {
    std::vector<MoveRequest> requests;
    const int n = static_cast<int>(units.size());
    const bool w7 = world.combat_w7().armed;   // w7fix-combat
    // w7r2: firing positions handed out this pass (cell -> attackers), so a
    // squad attacking one building spreads over distinct free cells.
    std::map<std::size_t, int> approach_claims;

    // W2-SK-15: spatial buckets for the auto-acquire scan (was O(U^2): every
    // idle unit walked every unit). kBucket x kBucket cells, filled in
    // ascending index; the scan below breaks distance ties by the lower index,
    // exactly the old ascending-loop result, so every gate stays identical.
    constexpr int kBucket = 8;
    const int bgw = std::max(1, (world.grid_w() + kBucket - 1) / kBucket);
    const int bgh = std::max(1, (world.grid_h() + kBucket - 1) / kBucket);
    std::vector<std::vector<int>> buckets(static_cast<std::size_t>(bgw) * bgh);
    auto bucket_of = [&](double c, int lim) {
        const int b = om::ftol(std::floor(c)) / kBucket;
        return std::max(0, std::min(lim - 1, b));
    };
    for (int j = 0; j < n; ++j) {
        const Unit& v = units[j];
        if (!v.alive || v.die_time >= 0.0 || !unit_combat_exposed(v)) continue;
        buckets[static_cast<std::size_t>(bucket_of(v.fy, bgh)) * bgw + bucket_of(v.fx, bgw)].push_back(j);
    }

    for (int i = 0; i < n; ++i) {
        Unit& u = units[i];

        // (a) Dying-progress: advance the death timer and tombstone at
        // kDieAnimSecs; skip every other combat action this call. Once
        // tombstoned (alive=false), die_time stays >= kDieAnimSecs forever,
        // so this branch keeps catching (and no-op'ing on) the unit -- it
        // never falls through to (b)/(c)/etc again.
        if (u.die_time >= 0.0) {
            if (u.alive) {
                u.die_time += dt;
                if (u.die_time >= kDieAnimSecs) u.alive = false;
            }
            continue;
        }
        // (b) Defensive: unreachable given the tombstone model above (alive
        // only ever flips false via the die_time path above), but a unit
        // spawned pre-dead should still never act.
        if (!u.alive) continue;

        // (b2) CB-1: a CARRIED unit is off-map -- it cannot fire, cool down,
        // acquire, or pursue [spec §4].
        if (u.cargo_of >= 0) continue;
        // (b3) P4 [RE: abilities.md §C1/§C2/§A]: a FROZEN unit (warping in,
        // being erased, deployed) neither fires, acquires nor pursues.
        // Inert: frozen is false unless a P4 profile is bound.
        if (u.frozen) continue;
        // W2-AN-02/12: fixed-wing aircraft and missiles fight in
        // World::step_air_naval (sorties / spawner strikes), not here.
        if (u.an.t.fixed_wing || u.an.t.missile) continue;

        // (c) ROF cooldown + fire-flash decay -- every living, non-dying
        // unit, every call, regardless of target state. docs/re/combat.md
        // §5: deterministic frame countdown, no jitter.
        if (u.gattling.enabled()) {
            bool engaging = valid_target(units, world, u, u.target) &&
                unit_in_weapon_range(world, u, units[u.target]);
            if (!engaging && u.attack_order && u.target >= 0 && u.target < static_cast<int>(units.size()))
                engaging = forced_target_ok(units,world,u,i,u.target) && unit_in_weapon_range(world,u,units[u.target]);
            if (!engaging && u.attack_cell_x >= 0)
                engaging = weapon_in_range(world,u,u.attack_cell_x*256,u.attack_cell_y*256,
                    world.cell_level(u.attack_cell_x,u.attack_cell_y)*104);
            if (!engaging && u.target_building >= 0 && u.target_building < static_cast<int>(buildings.size())) {
                const auto& b = buildings[u.target_building];
                engaging = b.alive && (u.attack_order || world.is_enemy(u.house,b.house)) &&
                    om::hypot(u.fx-b.cx,u.fy-b.cy) <= u.gattling.weapon(0,is_elite(u.vet.veterancy)).range;
            }
            u.gattling.step(engaging, is_elite(u.vet.veterancy));
        }
        if (u.rearm_frames > 0) --u.rearm_frames;   // Net L0: one whole frame per tick
        u.fire_flash -= dt;
        if (u.fire_flash < 0.0) u.fire_flash = 0.0;

        // --- Buildings & Construction Task 4: explicit attack-building path.
        // target_building is ONLY ever set by World::order_attack_building
        // (never by auto-acquire -- units NEVER auto-acquire buildings, and
        // buildings never retaliate, no weapons in this roster), so this
        // whole block is unreachable (u.target_building stays at its -1
        // default) for every unit in the pre-Task-4 unit-vs-unit tests and
        // for any unit not given an explicit attack-building order --
        // required for the byte-identical-when-buildings-is-empty gate.
        if (u.target_building >= 0) {
            Building* b = (u.target_building < static_cast<int>(buildings.size()))
                              ? &buildings[u.target_building]
                              : nullptr;
            const bool valid = b && b->alive && b->state != Building::State::Dead;
            if (!valid) {
                // Target validity: gone/dead/Dead-state -> clear and fall
                // back to normal (unit) targeting below, same "an explicit
                // order dies with its target" policy as (d) below.
                u.target_building = -1;
                u.attack_order = false;
            } else {
                const int b_idx = u.target_building;
                // A drain order is owned by the saved drain controller, not
                // conventional damage or the laser's range/approach path.
                if(world.disc_drain_order(u,b_idx))continue;
                // Gap fix C: the slot that can hit this structure's armor (a
                // pure-AA weapon or a 0 % Verses warhead cannot) [weapons.md §2].
                const int ws = unit_weapon_for_building(u, b->armor_ix, b->bomb_frame >= 0,
                                                        world.an_victim_naval_cell(b->cx + b->fw / 2, b->cy + b->fh / 2));   // W7-06-02
                if (ws < 0 && !u.abil.c4 && u.abil.fire_kind == AbilityKind::None) {
                    u.target_building = -1;
                    u.attack_order = false;
                    continue;
                }
                if (ws >= 0) activate_weapon_slot(u, ws, vet_rules_of(world));
                // Range to the NEAREST foundation cell (a 4x4 building is in
                // range of its EDGE, not its center).
                const auto cells = foundation_cells(b->cx, b->cy, b->fw, b->fh);
                double best_d = -1.0;
                int nx = 0, ny = 0;
                for (const Cellxy& cell : cells) {
                    const double d = dist_leptons(u.fx, u.fy, static_cast<double>(cell.x),
                                                  static_cast<double>(cell.y));
                    if (best_d < 0.0 || d < best_d) {
                        best_d = d;
                        nx = cell.x;
                        ny = cell.y;
                    }
                }
                // P4 [RE: abilities.md §D1]: a C4=yes infantry walks to its
                // Sapper (Secondary) range against a building, not its
                // primary's. abil.c4 is false unless a P4 profile is bound.
                const double range_leptons =
                    (u.abil.c4 ? u.abil.c4_range : u.w_range) * kLeptonsPerCell;
                // W5-B R-063 [RE: FUN_006F6750]: the original measures 3-D to the
                // building's centre with the range widened by (w+h)*64 leptons.
                int bax, bay, baz;
                building_aim_lept(*b, world.cell_level(b->cx, b->cy), bax, bay, baz);
                // P3: the Sapper shot uses the same in-range test [RE: combat.md §9,
                // FUN_006F6750 -- any weapon vs a building: to its centre, range
                // + (w+h)*64]; it was the nearest-foundation-cell distance, so a
                // wall-ringed silo (ALL05) could never be reached. c4_reach_from
                // is that test from a cell (2-D: the plant is a ground action).
                const int c4_reach = ftol_i(u.abil.c4_range * 256.0) + (b->fw + b->fh) * 64;
                // [Sapper]/[Sapper2] CellRangefinding=yes: the range is taken
                // from the CELL the sapper stands in (its centre), not its
                // sub-cell spot -- so the goal cell and the plant test agree.
                auto c4_reach_from = [&](int cx, int cy) {
                    return dist3_i(bax - cx * 256, bay - cy * 256, 0) <= c4_reach;
                };
                const bool b_in_range = u.abil.c4 ? c4_reach_from(u.cell_x, u.cell_y)
                                                  : weapon_in_range(world, u, bax, bay, baz,
                                                                    (b->fw + b->fh) * 64);
                // W7-04-07 [rules [V3Launcher] MinimumRange=5, GetFireError]:
                // the primary's minimum range refuses a structure too (the
                // launcher holds, as against a unit).
                const bool b_too_close =
                    u.w_active == 0 && u.an.t.min_range > 0.0 &&
                    om::hypot(u.fx * kLeptonsPerCell - bax, u.fy * kLeptonsPerCell - bay) <
                        u.an.t.min_range * kLeptonsPerCell;
                if (b_in_range && b_too_close) continue;
                if (b_in_range) {
                    if (u.rearm_frames <= 0) {
                        // Aim gate, mirroring the unit path exactly
                        // (combat.cpp ~L276-278): a vehicle holds fire until
                        // its turret has slewed onto the nearest foundation
                        // cell (step_turrets ran this frame, before
                        // step_combat, using this same facing). Infantry are
                        // exempt -- zero behavior change. rearm_frames untouched
                        // while unaimed.
                        if (u.is_vehicle && !u.omni_fire && !u.w6.omni_fire &&
                            !vehicle_aimed(u, facing_u16_from_delta(
                                                  static_cast<double>(nx) - u.fx,
                                                  static_cast<double>(ny) - u.fy)))
                            continue;  // hold fire this frame; rearm_frames untouched
                        u.rearm_frames = unit_rearm_frames(u, rof_rng);   // G-50 jitter, W6-04-01 Burst
                        u.fire_flash = kFireFlashSecs;
                        // P4 [RE: abilities.md §C2/§D/§E/§F]: an ability weapon
                        // (or a C4 infantry) hands the building shot to
                        // World::step_abilities instead of plain damage. Inert
                        // pre-P4 (fire_kind None, c4 false).
                        if (u.abil.fire_kind != AbilityKind::None || u.abil.c4) {
                            u.ability_fired_building = b_idx;
                            continue;
                        }
                        // W2-AN-12: a spawner launches at the structure.
                        if (u.an.t.spawner_slot >= 0 && u.w_active == u.an.t.spawner_slot) {
                            u.an.spawn_fire_building = b_idx;
                            continue;
                        }
                        // P2 firewall: a flying projectile defers the hit to
                        // World::step_projectiles; otherwise the pre-P2 tail.
                        if (launches && u.w_proj.flies())
                            launches->push_back({i, -1, b_idx});
                        else {
                            // W5-B R-060: the shot detonates at the building's
                            // centre through the area applicator.
                            int ax, ay, az;
                            building_aim_lept(*b, world.cell_level(b->cx, b->cy), ax, ay, az);
                            damage_area(world, units, buildings, occ, u, i, -1, ax, ay, az);
                            if (u.target_building == b_idx &&
                                (!b->alive || b->state == Building::State::Dead)) {
                                u.target_building = -1;
                                u.attack_order = false;
                            }
                        }
                    }
                } else if (u.attack_order && !(u.deploy_fire && u.deployed)) {
                    // w7r2 [RE: FootClass::Approach_Target FUN_004D55B0
                    // @0x4D5EE0..0x4D67B2]: the original walks rings round the
                    // target's centre, from the weapon range (less half a cell
                    // past 0x1FF) inward in whole cells while > 204.8 leptons
                    // (DAT_007E8238), trying the 25 offsets of DAT_008214EC
                    // (0, +-1..6, +-8, +-16, +-24, +-32, +-48, +-64 in 1/256
                    // turn) about the facing from the target to the unit. A
                    // candidate's cell centre must pass FUN_006F6750 -- the
                    // building in-range test above -- and be free
                    // (FUN_00577E90 / FUN_004833B0, else its 8 neighbours
                    // under the same tests). The clone used to head for the
                    // nearest foundation cell and fan a group out round it
                    // (assign_formation), so attackers stopped short of or past
                    // the range ring and a whole squad walked round a pillbox
                    // into the next one (SOV01). Ground land units under the
                    // W7 rules; RE-FALLBACK: the path-length check
                    // FUN_0042D0F0 (<= Chebyshev + 8) is not applied, the
                    // A* goes to the nearest reachable cell instead.
                    const bool w7_approach = w7 && !u.abil.c4 && !u.is_aircraft && !u.is_naval &&
                                             u.movement_domain == MovementDomain::Land;
                    const int b_extra = (b->fw + b->fh) * 64;
                    auto in_range_from_cell = [&](int cx, int cy) {
                        return weapon_in_range_from(world, u, cx * 256, cy * 256,
                                                    world.cell_level(cx, cy) * kLevelLept, bax, bay, baz,
                                                    b_extra);
                    };
                    auto cell_free = [&](int cx, int cy) {
                        if (cx < 0 || cy < 0 || cx >= world.grid_w() || cy >= world.grid_h()) return false;
                        if (cx == u.cell_x && cy == u.cell_y) return false;   // not a firing position (tested)
                        const std::size_t k = static_cast<std::size_t>(cy) * world.grid_w() + cx;
                        if (!world.passable()[k]) return false;
                        if (!occ.can_enter(cx, cy, i)) return false;
                        const auto it = approach_claims.find(k);
                        return it == approach_claims.end() || it->second < (u.is_vehicle ? 1 : 3);
                    };
                    auto approach_cell = [&]() -> Cellxy {
                        static const int kOff[25] = {0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6,
                                                     8, -8, 16, -16, 24, -24, 32, -32, 48, -48, 64, -64};
                        static const int kNx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
                        static const int kNy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
                        const int dir = ((om::ftol(facing_u16_from_delta(unit_lept_x(u) - bax,
                                                                         unit_lept_y(u) - bay)) >> 7) + 1) >> 1;
                        int r = ftol_i(u.w_range * 256.0) + b_extra;
                        if (r > 0x1FF) r -= 0x80;
                        constexpr double kTwoPi = 6.283185307179586;
                        for (; r > 204.8; r -= 256) {
                            for (int a : kOff) {
                                const double th = static_cast<double>(((dir + a) & 0xFF) * 256) * (kTwoPi / 65536.0);
                                const int px = bax + om::ftol(om::sin(th) * r);
                                const int py = bay - om::ftol(om::cos(th) * r);
                                const int cx = (px + 128) >> 8, cy = (py + 128) >> 8;
                                if (cx < 0 || cy < 0 || cx >= world.grid_w() || cy >= world.grid_h()) continue;
                                if (!in_range_from_cell(cx, cy)) continue;
                                if (cell_free(cx, cy)) return {cx, cy};
                                for (int k = 0; k < 8; ++k)
                                    if (cell_free(cx + kNx[k], cy + kNy[k]) &&
                                        in_range_from_cell(cx + kNx[k], cy + kNy[k]))
                                        return {cx + kNx[k], cy + kNy[k]};
                            }
                        }
                        return {-1, -1};
                    };
                    // Dedup mirrors the unit-pursuit branch below (f): only
                    // re-request when there's no live path, or the current
                    // path's goal has stopped being a firing position for the
                    // building (its nearest foundation cell to that goal has
                    // left weapon range). The building never moves, but the
                    // goal-to-nearest-cell distance can still change because
                    // "nearest" is relative to the goal, not the unit -- e.g.
                    // a goal on the far side of a multi-cell foundation.
                    bool need = !u.moving();
                    if (!need) {
                        const Cellxy g = u.path.back();
                        double g_best = -1.0;
                        for (const Cellxy& cell : cells) {
                            const double d = dist_leptons(
                                static_cast<double>(g.x), static_cast<double>(g.y),
                                static_cast<double>(cell.x), static_cast<double>(cell.y));
                            if (g_best < 0.0 || d < g_best) g_best = d;
                        }
                        need = u.abil.c4 ? !c4_reach_from(g.x, g.y) : g_best > range_leptons;
                        // w7r2: a firing position is judged by the in-range
                        // test itself (3-D to the centre, range + (w+h)*64),
                        // the rule the approach cell below is chosen by.
                        if (w7_approach) need = !in_range_from_cell(g.x, g.y);
                        // A parked unit can take a previously free firing
                        // position. Re-select a legal position instead of
                        // waiting forever merely because the old goal is in range.
                        if(w7_approach && u.wait_time>=3.0 && !occ.can_enter(g.x,g.y,i,u.on_deck))need=true;
                    }
                    if (need && w7_approach) {
                        const Cellxy c = approach_cell();
                        if (c.x >= 0) {
                            ++approach_claims[static_cast<std::size_t>(c.y) * world.grid_w() + c.x];
                            requests.push_back({i, c.x, c.y, /*approach=*/false});
                            continue;
                        }
                    }
                    // P3 [RE: infantry-anim.md §7 -- FootClass::ApproachTarget
                    // heads for a FIRING POSITION near the target; RE-FALLBACK:
                    // its cell choice is not decompiled]: a C4 infantry goes to
                    // the walkable cell nearest to it from which the plant is in
                    // range (c4_reach_from). The generic approach (a free cell
                    // next to the foundation cell nearest the unit) can pick a
                    // cell out of range behind a fence or wall, and the sapper
                    // then stood still forever (ALL05: wired-in Tesla Reactors,
                    // wall-ringed nuclear silos).
                    int cgx = -1, cgy = -1;
                    if (need && u.abil.c4) {
                        const std::vector<std::uint8_t>& pass = world.passable();
                        const int gw = world.grid_w(), gh = world.grid_h();
                        const int ccx = bax / 256, ccy = bay / 256;
                        const int rr = c4_reach / 256 + 2;
                        double cbest = -1.0;
                        for (int y = ccy - rr; y <= ccy + rr; ++y)
                            for (int x = ccx - rr; x <= ccx + rr; ++x) {
                                if (x < 0 || y < 0 || x >= gw || y >= gh) continue;
                                if (!pass[static_cast<std::size_t>(y) * gw + x] || !c4_reach_from(x, y)) continue;
                                const double ddx = x - u.cell_x, ddy = y - u.cell_y;
                                const double d2 = ddx * ddx + ddy * ddy;
                                if (cbest < 0.0 || d2 < cbest) {   // scan order breaks ties (y, then x)
                                    cbest = d2;
                                    cgx = x;
                                    cgy = y;
                                }
                            }
                    }
                    if (need && cgx >= 0) requests.push_back({i, cgx, cgy, /*approach=*/false});
                    else if (need) requests.push_back({i, nx, ny, /*approach=*/true});
                } else if (!u.attack_order) {
                    // Gap fix C: a retaliation/auto target (not an explicit
                    // order) out of range is dropped -- the unit goes back to
                    // normal acquisition instead of staring at it forever.
                    u.target_building = -1;
                }
                continue;  // handled -- skip the unit-vs-unit path entirely
            }
        }

        // G16 (agent G, G-49): force-fire at a ground cell (Ctrl+click) -- the
        // attack-building template above with a cell target. The shot itself
        // is resolved by World::step_depth (explosion on the cell + the bridge
        // roll); here: range, aim, ROF, and the approach request.
        if (u.attack_cell_x >= 0) {
            if (u.gattling.enabled()) activate_weapon_slot(u,0,vet_rules_of(world));
            if (u.w7.t.magnetic && u.w_active==0) continue;
            const double d = dist_leptons(u.fx, u.fy, static_cast<double>(u.attack_cell_x),
                                          static_cast<double>(u.attack_cell_y));
            (void)d;
            // W5-B R-063: 3-D range to the cell centre (+ elevation / cliffs).
            if (weapon_in_range(world, u, u.attack_cell_x * 256, u.attack_cell_y * 256,
                                world.cell_level(u.attack_cell_x, u.attack_cell_y) * 104)) {
                if (u.rearm_frames <= 0) {
                    if (u.is_vehicle && !u.omni_fire && !u.w6.omni_fire &&
                        !vehicle_aimed(u, facing_u16_from_delta(
                                              static_cast<double>(u.attack_cell_x) - u.fx,
                                              static_cast<double>(u.attack_cell_y) - u.fy)))
                        continue;
                    u.rearm_frames = unit_rearm_frames(u, rof_rng);   // W6-04-01 Burst
                    u.fire_flash = kFireFlashSecs;
                    if(world.chaos_weapon(u)) {
                        damage_area(world,units,buildings,occ,u,i,-1,
                            unit_lept_x(u),unit_lept_y(u),unit_lept_z(u));
                        continue;
                    }
                    u.cell_shot = true;
                }
            } else if (!u.moving()) {
                requests.push_back({i, u.attack_cell_x, u.attack_cell_y, /*approach=*/true});
            }
            continue;
        }

        // ==== w7fix-combat: the mission model's targeting layer ===========
        // docs/gap/wave7/07-shared-combat.md W7-07-02..12. Armed only with the
        // real rulesmd (World::arm_combat_w7); every synthetic gate keeps the
        // legacy block below.
        if (w7) {
            if (u.tg.scan_timer > 0) --u.tg.scan_timer;
            if (u.tg.opportunity && !u.moving()) u.tg.opportunity = false;   // arrived: a guard target now
            // (d) W7-07-13 [RE: GetFireError FUN_006FB5D0 has no house test]:
            // an EXPLICIT target (the player's force-fire on an own, allied or
            // neutral object; a retaliation Override_Mission) needs no
            // hostility; only an auto target must stay an enemy.
            if (u.target != -1 && !(u.attack_order ? forced_target_ok(units, world, u, i, u.target)
                                                   : valid_target(units, world, u, u.target))) {
                const bool chasing = !u.attack_order && u.moving() && !u.path.empty() && !u.tg.opportunity;
                u.target = -1;
                u.attack_order = false;
                u.tg.opportunity = false;
                if (u.tg.overridden) {
                    // W7-07-01 [RE: FootClass::Override_Mission 0x4D8E60 /
                    // Restore_Mission]: the retaliation is over -- back to the
                    // saved order (its move goal, its area guard).
                    u.tg.overridden = false;
                    u.tg.area_guard = u.tg.resume_area_guard;
                    if (u.tg.resume_x >= 0 && (u.tg.resume_x != u.cell_x || u.tg.resume_y != u.cell_y))
                        requests.push_back({i, u.tg.resume_x, u.tg.resume_y});
                    u.tg.resume_x = u.tg.resume_y = -1;
                } else if (chasing) {   // R-212 (plain Guard): finish the walk, guard there
                    u.guard_x = u.path.back().x;
                    u.guard_y = u.path.back().y;
                }
            }
            const int mission = world.unit_mission(u);
            const World::MissionCtl mc = World::mission_control(mission);
            // W7-07-07 [RE: 0x6F9B8A..0x6F9C1E, TargetSomethingNearby
            // 0x708CA0]: at each targeting tick a scan-acquired Guard / Move
            // target out of weapon range (GetFireError RANGE) is dropped and
            // the unit rescans from its CURRENT cell -- no anchor, no walk back.
            // W7-07-11 [RE: 0x4D5636..0x4D5660]: a Sticky unit drops it too.
            if (u.target != -1 && !u.attack_order && u.tg.scan_timer <= 0 &&
                (mission == 5 || mission == 2 || mission == 10 || mission == 6 || mission == 29 ||
                 u.tg.opportunity) &&
                !unit_in_weapon_range(world, u, units[u.target])) {
                u.target = -1;
                u.tg.opportunity = false;
            }
            // (e) W7-07-02 [RE: TechnoClass::AI 0x6F9BC7 / FUN_00708710]: the
            // scan runs in Guard and Area Guard; in Move / Harvest only for an
            // OpportunityFire type (W7-03-03) or an Aggressive team member
            // (W7-04-19); Attack Move scans on the move (W7-07-12). Sleep,
            // Harmless, Attack, Hunt, Enter, ... never scan.
            if (u.target == -1 && u.target_building == -1) {
                bool may = mission == 5 || mission == 11 || mission == 29 || mission == 6 ||
                           (mission == 15 && world.unit_berserk(i));
                if (mission == 2 || mission == 10) may = u.tg.t.opportunity_fire || u.tg.team_aggressive;
                // (an ability weapon -- the dog's ParasiteDog jaw, the Ivan --
                // scans like any other; only the structure pass skips them)
                if (u.has_flight_goal || u.abil.no_passive_acquire || u.an.t.no_passive_acquire || mc.zombie)
                    may = false;
                // Engineer/spy/medic-type weapons (no damage) never scan.
                if (u.w_damage <= 0 && !secondary_view(u).valid) may = false;
                bool found = false;
                if (may && u.tg.scan_timer <= 0) {
                    const World::CombatW7Rules& R = world.combat_w7();
                    u.tg.scan_timer = (mission == 11 ? R.guard_area_targeting_delay : R.normal_targeting_delay) +
                                      (rof_rng ? rof_rng->random(0, 2) : 0);
                    found = w7_greatest_threat(world, units, buildings, u, i, mission == 6,
                                               buckets, bgw, bgh, bucket_of);
                    if (found) {
                        const bool on_move = u.moving();
                        u.attack_order = world.unit_berserk(i);
                        u.guard_x = u.cell_x;
                        u.guard_y = u.cell_y;
                        if (mission == 11) { u.tg.home_x = u.cell_x; u.tg.home_y = u.cell_y; }
                        if (on_move && mission == 29) {
                            // W7-07-12 [RE: FootClass FUN_004DEFF0]: an attack-
                            // moving unit stops to engage (finishing the cell it
                            // is entering) and resumes the move afterwards.
                            if (u.path_idx < u.path.size()) u.path.resize(u.path_idx + 1);
                        } else if (on_move) {
                            u.tg.opportunity = true;   // W7-03-03: fire at will, keep driving
                        }
                    }
                }
                if (!found && u.target == -1 && u.target_building == -1 && !u.moving()) {
                    if (mission == 29) {
                        // W7-07-12: nothing to fight -- carry on to the destination;
                        // there, the unit idles (Guard).
                        if (u.tg.amove_x < 0 || (u.cell_x == u.tg.amove_x && u.cell_y == u.tg.amove_y)) {
                            u.tg.mission = -1;
                            u.tg.amove_x = u.tg.amove_y = -1;
                        } else if (u.tg.scan_timer > 0) {
                            requests.push_back({i, u.tg.amove_x, u.tg.amove_y});
                        }
                    } else if (mission == 11 && may && u.tg.home_x >= 0 && u.tg.scan_timer > 0 &&
                               (u.cell_x != u.tg.home_x || u.cell_y != u.tg.home_y)) {
                        // W7-07-06 [RE: Mission_Area_Guard FUN_004D41A0]: the
                        // target is lost and the rescan found nothing -- walk
                        // back home (scanning on the way).
                        requests.push_back({i, u.tg.home_x, u.tg.home_y});
                    }
                }
            }
        } else {
            // (d) Target validation: drop a stale target (gone from the vector,
            // dead, dying, or no longer hostile). Clearing also resets
            // attack_order -- an explicit order dies with its target, so a
            // fresh acquisition below starts clean rather than inheriting a
            // stale "pursue indefinitely" flag for a target it hasn't picked
            // yet.
            if (u.target != -1 && !valid_target(units, world, u, u.target)) {
                // An AUTO-acquired target that vanishes (killed, out of sight,
                // loaded into a transport) while the unit is walking its approach
                // path: the target is dropped and the unit FINISHES the walk to the
                // firing cell, then guards there (its guard anchor becomes the path's
                // end -- Enter_Idle_Mode at the destination).
                // [RE: the Guard mission approaches through FootClass::Approach_Target
                // FUN_004D55B0 (via UnitClass FUN_007402F0, vtbl+0x53C), whose
                // Destination is a CELL (SetDestination(MapClass::GetCellAt(..)),
                // vtbl+0x480); only a TechnoType +0xD27 / VehicleThief (+0xEC6)
                // infantry heads for the object itself. When the target expires,
                // TechnoClass::PointerExpired FUN_00706C50 clears TarCom (+0x2B4) and
                // FootClass::PointerExpired FUN_004D9870 clears Destination (+0x5A4)
                // only if it IS the expired object -- a cell destination stays, and
                // UnitClass::Enter_Idle_Mode FUN_00737990 picks Mission_Move while a
                // Destination remains. Nothing walks the unit back to where it
                // acquired.] fv-a's walk back to the guard anchor (RE-FALLBACK) is
                // gone: it broke the SOV01 and SOV09 real-order wins.
                const bool chasing = !u.attack_order && u.moving() && !u.path.empty();
                u.target = -1;
                u.attack_order = false;
                if (chasing) {
                    u.guard_x = u.path.back().x;
                    u.guard_y = u.path.back().y;
                }
            }

            // (e) Auto-acquire: nearest hostile within weapon range, ties ->
            // lowest index. Only runs when idle (no current target survived
            // validation, and no explicit order or retaliation set one this
            // call). Records the guard anchor at the unit's OWN current cell --
            // the post it should return to if the target strays too far (§7,
            // ModEnc fallback, Low).
            // Only IDLE/guarding units auto-acquire (spec wording): a unit
            // executing a move order walks on without stopping to fight; it will
            // acquire on arrival if a hostile is still in range.
            if (u.target == -1) {
                // No auto-acquire while under a move order. A ground unit's move is
                // its A* path (moving()); an aircraft cruising or taking off toward
                // a flight goal (has_flight_goal, with an empty ground path) is the
                // flight analog -- it flies its route without stopping to fight,
                // exactly as a ground unit walks on [RE-GATE APPLIED: aircraft.md §6
                // -- a Move order overrides auto-attack; an explicit order_attack
                // sets u.target and skips this block, so air-to-ground attack is
                // unaffected]. Cannot change the 9 ground-only gates: has_flight_goal
                // is always false there.
                // P4 [RE: abilities.md §F]: CanPassiveAquire=no (the terrorist)
                // never auto-acquires. Inert: false unless a P4 profile is bound.
                // W2-AN-14: CanPassiveAquire=no (Carrier, Dreadnought) likewise.
                if (u.moving() || u.has_flight_goal || u.abil.no_passive_acquire ||
                    u.an.t.no_passive_acquire) { /* no auto-acquire */ }
                else {
                    int best = -1;
                    double best_d = 0.0;
                    // W6-02-11 (supersedes the W2-SK-25 max(weapon range, Sight)
                    // fallback): the threat-scan radius of acquire_radius.
                    const double acq = acquire_radius(u);
                    const double rmax = std::max(std::max(slot_range(u, 0), slot_range(u, 1)), acq) + 1.0;
                    const int bx0 = bucket_of(u.fx - rmax, bgw), bx1 = bucket_of(u.fx + rmax, bgw);
                    const int by0 = bucket_of(u.fy - rmax, bgh), by1 = bucket_of(u.fy + rmax, bgh);
                    for (int by = by0; by <= by1; ++by)
                    for (int bx = bx0; bx <= bx1; ++bx)
                    for (int j : buckets[static_cast<std::size_t>(by) * bgw + bx]) {
                        // Engineer/Spy Capture [RE: capture.md §Design 6]: a still-
                        // disguised unit is treated as FRIENDLY -- never auto-
                        // acquired (a disguised spy walks through enemy territory
                        // unfired-upon). Byte-identity: Unit::disguised defaults
                        // false and no gate ever calls set_disguise (capture_any_
                        // stays false), so this clause is false for every candidate
                        // in every gate -> inert (combat 78 / tank 144 unchanged).
                        // The skip lives ONLY in this AUTO-acquire scan, NOT in
                        // valid_target (which also gates the explicit-attack /
                        // stale-target-drop path (d)) -- an explicit order_attack on
                        // a known spy is never blocked. A dog's DetectDisguise clears
                        // `disguised` -> the spy is targetable again next scan (no
                        // extra code needed).
                        // Shroud/Fog [RE: shroud.md §6]: a candidate whose cell is
                        // NOT visible to the scanning house cannot be auto-acquired
                        // (mirrors the disguise precedent above). seen_by_mask
                        // defaults 0xFF and only step_visibility (behind shroud_any_)
                        // ever changes it, so with shroud off
                        // (0xFF & (1<<u.house)) != 0 -> the clause is always false ->
                        // combat 78 / tank 144 byte-identical. This lives ONLY in the
                        // AUTO-acquire scan, NOT valid_target (an explicit
                        // order_attack on a known unit is never blocked, §6).
                        // W6-02-07/08: the Evaluate_Object rejects (LegalTarget,
                        // IsInPlayfield, campaign Insignificant) -- World::legal_threat_unit.
                        // W6-02-12: a mind-control shooter never picks an
                        // ImmuneToPsionics unit (map overrides merged).
                        if (j == i || !valid_target(units, world, u, j)
                            || (!u.detect_disguise && world.disguise_fools(u.house, units[j]))   // W7-01-02 [RE: 0x6F71D0]
                            || !units[j].seen_by_house(u.house) || !world.legal_threat_unit(u.house, units[j])
                            || (u.w_mind_control && units[j].tg.t.immune_psionics))
                            continue;
                        const Unit& v = units[j];
                        const double d = dist_leptons(u.fx, u.fy, v.fx, v.fy);
                        // Gap fix C: in range of the weapon that WOULD engage v; a
                        // ground unit scans its whole threat radius (W6-02-11).
                        const bool by_slot = u.is_aircraft || (u.deploy_fire && u.deployed);
                        const double rng = by_slot ? slot_range(u, unit_weapon_for_unit(u, v)) : acq;
                        if (((by_slot && range_infinite(rng)) || d <= rng * kLeptonsPerCell) &&   // fv7c
                            (best == -1 || d < best_d || (d == best_d && j < best))) {
                            best = j;
                            best_d = d;
                        }
                    }
                    if (best != -1) {
                        u.target = best;
                        u.guard_x = u.cell_x;
                        u.guard_y = u.cell_y;
                        u.attack_order = false;
                    } else if (u.target_building == -1 && u.abil.fire_kind == AbilityKind::None &&
                               !u.abil.c4) {
                        // Gap fix C (G-46): no unit in range -> an idle unit engages
                        // an enemy BASE DEFENSE (an armed structure) in range.
                        // [RE-FALLBACK: RE-W4 open -- Greatest_Threat's structure
                        // flags were not unwound; armed structures carry threat,
                        // plain buildings (zero threat) are left to explicit
                        // orders.] Nearest foundation cell, lowest index on ties.
                        const std::vector<BuildingTypeDef>& bt = world.btypes();
                        int bbest = -1;
                        double bbest_d = 0.0;
                        for (int bj = 0; bj < static_cast<int>(buildings.size()); ++bj) {
                            const Building& b = buildings[bj];
                            if (!b.alive || b.state == Building::State::Dead) continue;
                            // W5-B R-064 [RE: Greatest_Threat FUN_006F8320 ->
                            // Evaluate_Object FUN_006F71D0 @0x6F7ADB]: a HUMAN
                            // house's unit skips a building with no weapon; a
                            // computer house's unit may pick any enemy building.
                            if (b.type < 0 || b.type >= static_cast<int>(bt.size()) ||
                                (!bt[b.type].armed && !(world.house_is_computer(u.house) && world.ai_threat_any_building())))
                                continue;
                            if (!world.is_enemy(u.house, b.house)) continue;
                            if (!world.legal_threat_building(u.house, b)) continue;   // W6-02-07/08
                            if (!world.object_visible_at(u.house, b.cx, b.cy)) continue;
                            const int ws = unit_weapon_for_building(u, b.armor_ix, b.bomb_frame >= 0,
                                                                    world.an_victim_naval_cell(b.cx + b.fw / 2, b.cy + b.fh / 2));   // W7-06-02
                            if (ws < 0) continue;
                            const double rng = range_infinite(slot_range(u, ws)) ? 1e18 : slot_range(u, ws) * kLeptonsPerCell;   // fv7c
                            for (const Cellxy& cell : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
                                const double d = dist_leptons(u.fx, u.fy, cell.x, cell.y);
                                if (d <= rng && (bbest == -1 || d < bbest_d)) {
                                    bbest = bj;
                                    bbest_d = d;
                                }
                            }
                        }
                        if (bbest != -1) {
                            u.target_building = bbest;
                            u.guard_x = u.cell_x;
                            u.guard_y = u.cell_y;
                            u.attack_order = false;
                        }
                    }
                }
            }

        }

        if (u.target == -1) {   // nothing to engage this call
            u.crushing = false;
            continue;
        }

        // (f) Engage.
        Unit& v = units[u.target];
        // Gap fix C (G-41/G-44): bring the weapon that engages THIS target
        // into w_* (primary unless only the secondary can hit it). A no-op for
        // every unit without a secondary.
        activate_weapon_slot(u, unit_weapon_for_unit(u, v), vet_rules_of(world));
        const double range_leptons = u.w_range * kLeptonsPerCell;
        const double d = dist_leptons(u.fx, u.fy, v.fx, v.fy);
        // W2-AN-12 [rules [DredLauncher] MinimumRange=8, [V3Launcher] 5]: the
        // primary cannot fire inside its minimum range (the unit holds).
        const bool too_close = u.w_active == 0 && u.an.t.min_range > 0.0 &&
                               d < u.an.t.min_range * kLeptonsPerCell;

        // W5-B R-050 [RE: UnitClass ShouldCrush FUN_00742720 / Mission_Attack
        // FUN_007402F0]: a Crusher with a crushable enemy target inside
        // [CombatDamage] Crush (3-D, 1.8 cells) drives onto it instead of
        // firing -- computer houses always, the player only with
        // PlayerAutoCrush=yes or an AutoCrush=yes type.
        {
            const World::CrushRules& cr = world.crush_rules();
            bool may = cr.armed && u.crusher && u.is_vehicle && !u.is_aircraft &&
                             can_crush(u, v) &&   // W6-04-14/29
                             (world.house_is_computer(u.house) || cr.player_auto_crush || u.auto_crush);
            if (w7) {
                // W7-03-09 [RE: ShouldCrush FUN_00742720: (!IsHuman ||
                // PlayerAutoCrush || type AutoCrush) && (IsHuman || [IQ]
                // AutoCrush <= house IQ)]; W7-03-01: never onto an ally (a
                // force-fired friendly is shot, not run over); an opportunity
                // target never pulls a unit off its move.
                const bool human = world.human_commanded(u.house);
                may = cr.armed && u.crusher && u.is_vehicle && !u.is_aircraft && can_crush(u, v) &&
                      !world.allied_with(u.house, v.house) && !u.tg.opportunity &&
                      (human ? (cr.player_auto_crush || u.auto_crush)
                             : world.house_iq(u.house) >= world.combat_w7().iq_auto_crush);
            }
            const int dx = unit_lept_x(v) - unit_lept_x(u), dy = unit_lept_y(v) - unit_lept_y(u),
                      dz = unit_lept_z(v) - unit_lept_z(u);
            // W7-04-10: a target already in the crusher's own cell gets no
            // (empty) drive-over request -- the unit would freeze there; it
            // falls through to the fire step instead.
            const bool same_cell = v.cell_x == u.cell_x && v.cell_y == u.cell_y;
            if (may && !same_cell && om::sqrt(double(dx) * dx + double(dy) * dy + double(dz) * dz) <= cr.crush_leptons) {
                if (!u.crushing || u.path.empty() || u.path.back().x != v.cell_x ||
                    u.path.back().y != v.cell_y)
                    requests.push_back({i, v.cell_x, v.cell_y, /*approach=*/false});
                u.crushing = true;
                continue;   // no shot: the tracks do the work
            }
            u.crushing = false;
        }

        if (unit_in_weapon_range(world, u, v)) {   // W5-B R-063: 3-D + elevation + cliffs
            if (too_close) continue;
            if (u.rearm_frames <= 0) {
                // Aim gate (docs/re/voxel-render.md §5, FireError::FACING): a
                // vehicle holds fire until its turret has finished slewing onto
                // the target (step_turrets ran this frame, before step_combat,
                // computing `want` from these same fx/fy, so the exact
                // comparison in vehicle_aimed is well-defined). Infantry are
                // exempt -- zero behavior change. W6-04-06 [RE: WeaponType
                // OmniFire +0x12B bypasses the facing gate, aircraft.md §4]: an
                // OmniFire weapon (Aegis Medusa, Carrier/Dreadnought launchers)
                // fires without the hull or turret facing the target.
                if (u.is_vehicle && !u.omni_fire && !u.w6.omni_fire &&
                    !vehicle_aimed(u, facing_u16_from_delta(v.fx - u.fx, v.fy - u.fy)))
                    continue;  // hold fire this frame; rearm_frames untouched
                try_fire(world, units, buildings, u, v, i, u.target, occ, launches, rof_rng);
            }
        } else if (u.attack_order && !(u.deploy_fire && u.deployed)) {
            // (Gap fix C, G-45: a deployed GI holds its post -- no approach.)
            // Approach: ask the caller for a firing position near the target.
            // The original engine approaches until in weapon range and never
            // paths to the victim's own (occupied) cell (docs/re/
            // infantry-anim.md §7); World::step batch-assigns each attacker
            // a distinct free cell around the target. Re-request only when
            // the current path's goal has stopped being a firing position
            // for the target's CURRENT location (target moved out of the
            // goal's range circle), or there is no live path at all.
            bool need = !u.moving();
            if (!need) {
                const Cellxy g = u.path.back();
                need = dist_leptons(static_cast<double>(g.x), static_cast<double>(g.y),
                                    v.fx, v.fy) > range_leptons;
                if(u.wait_time>=3.0 && !occ.can_enter(g.x,g.y,i,u.on_deck))need=true;
            }
            if (need) requests.push_back({i, v.cell_x, v.cell_y, /*approach=*/true});
        } else if (w7) {
            // w7fix-combat: the mission model's chase rules.
            const int mission = world.unit_mission(u);
            if (u.tg.opportunity && u.moving()) {
                // W7-03-03: fire at will on the move -- never approach.
            } else if (mission == 6) {
                // W7-07-11 [RE: Approach_Target 0x4D5636..0x4D5660]: Sticky
                // never approaches (the per-tick range drop clears it).
            } else if (mission == 11 && u.tg.home_x >= 0 &&
                       dist_leptons(v.fx, v.fy, u.tg.home_x, u.tg.home_y) >=
                           area_guard_leash_cells(u) * kLeptonsPerCell) {
                // W7-07-06 [RE: Mission_Area_Guard FUN_004D41A0, FUN_0042D0F0]:
                // the target has strayed a leash from home -- drop it (the
                // rescan-or-walk-home runs next).
                u.target = -1;
            } else if (!u.moving() && !u.is_aircraft && !(u.deploy_fire && u.deployed) && !u.is_harvester &&
                       u.w_damage > 0) {
                // W7-07-07 [RE: Approach_Target 0x4D55B0 @0x4D5636..0x4D5679]:
                // approach only with CanApproachTarget, else SetTarCom(0).
                if (u.w6.t.can_approach_target) requests.push_back({i, v.cell_x, v.cell_y, /*approach=*/true});
                else u.target = -1;
            }
        } else {
            // Auto-acquired (or retaliation-acquired): leashed to the guard
            // anchor. Drop if EITHER the unit itself or its target has
            // strayed past weapon range + kGuardStray from that anchor (§7,
            // ModEnc fallback, Low), then ask to return home if not already
            // there.
            // W6-02-11: the leash is the threat-scan radius, not Sight.
            const double leash = std::max(range_leptons, acquire_radius(u) * kLeptonsPerCell);
            const double stray = leash + kGuardStray * kLeptonsPerCell;
            const double d_self = dist_leptons(u.fx, u.fy, static_cast<double>(u.guard_x),
                                                static_cast<double>(u.guard_y));
            const double d_target = dist_leptons(v.fx, v.fy, static_cast<double>(u.guard_x),
                                                  static_cast<double>(u.guard_y));
            if (d_self > stray || d_target > stray) {
                u.target = -1;
                if (u.cell_x != u.guard_x || u.cell_y != u.guard_y) {
                    if (u.path.empty() || u.path.back().x != u.guard_x ||
                        u.path.back().y != u.guard_y)
                        requests.push_back({i, u.guard_x, u.guard_y});
                }
            } else if (!u.moving() && !u.is_aircraft && !(u.deploy_fire && u.deployed) &&
                       !u.is_harvester && u.w_damage > 0 &&
                       u.w6.t.can_approach_target) {   // W6-04-30: the Mirage holds
                // W2-SK-25: close in on a sighted target inside the leash (the
                // back ranks of a big fight used to stand idle out of range).
                requests.push_back({i, v.cell_x, v.cell_y, /*approach=*/true});
            }
        }
    }

    return requests;
}

}  // namespace ra2engine

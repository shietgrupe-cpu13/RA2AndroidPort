#include "weapon_unit.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include "capture.hpp"  // veteran_firepower (legacy rank bake)

namespace ra2engine {

namespace {
bool unit_is_elite(const Unit& u) { return is_elite(u.vet.veterancy); }
}  // namespace

WeaponSlot primary_view(const Unit& u) {
    if (u.gattling.enabled()) return u.gattling.weapon(0, unit_is_elite(u));
    WeaponSlot s;
    s.valid = true;   // legacy: the primary is always "present" (range 0 = inert)
    // The live w_verses while the primary is active (an IFV's passenger swap
    // rewrites w_* in place), else the type's base/elite primary.
    if (u.w_active == 0) s.verses = u.w_verses;
    else if (u.vet.recv && unit_is_elite(u) && u.vet.has_elite) s.verses = u.vet.elite.verses;
    else s.verses = u.vet.base.verses;
    s.aa = u.p_aa;
    s.ag = u.p_ag;
    return s;
}

const WeaponSlot& secondary_view(const Unit& u) {
    if (u.gattling.enabled()) return u.gattling.weapon(1, unit_is_elite(u));
    if (u.vet.recv && unit_is_elite(u) && u.w2_elite.valid) return u.w2_elite;
    return u.w2;
}

namespace {
int unit_weapon_for_unit_base(const Unit& u, const Unit& v) {
    if (deployment_owns_fire(u)) return -1;
    if (u.deploy_fire && u.deployed) {
        const int slot=u.deploy_fire_slot==1 && u.w2.valid ? 1 : 0;
        return slot_can_target(slot==1?secondary_view(u):primary_view(u),unit_airborne(v),v.armor_ix)?slot:-1;
    }
    // W7-01-06 [RE: InfantryClass::SelectWeapon 0x521610]: an undeployed
    // DeployFire infantry (GI, Guardian GI) has weapon 0 only -- the
    // undeployed GGI never fires its anti-air MissileLauncher.
    if (u.deploy_fire && u.is_infantry)
        return slot_can_target(primary_view(u), unit_airborne(v), v.armor_ix) ? 0 : -1;
    if (!u.w2.valid) {
        // No secondary: the primary alone, gated like GetFireError.
        return slot_can_target(primary_view(u), unit_airborne(v), v.armor_ix) ? 0 : -1;
    }
    return choose_weapon(primary_view(u), secondary_view(u), unit_airborne(v), v.armor_ix);
}

// [RE: TechnoClass::GetFireError FUN_006FB5D0 -- `if (Weapon->Warhead
// ->BombDisarm (+0x16E) && Target->AttachedBomb (+0x38) == 0) return
// FireError ILLEGAL (5)`]: a BombDisarm weapon (the Engineer's DefuseKit)
// may only fire at an object carrying an Ivan bomb; the check runs on the
// weapon already selected for the target, so it does not fall back to the
// other slot. Without it an idle computer engineer scanned its GuardRange
// and "shot" an enemy building to death 1 HP at a time (ALL09's Tech Airport).
bool slot_bomb_disarm(const Unit& u, int slot) {
    if (slot == 0) return u.w6.t.bomb_disarm;
    if (slot == 1) return u.w2.valid && secondary_view(u).bomb_disarm;
    return false;
}
}  // namespace

int unit_weapon_for_unit(const Unit& u, const Unit& v) {
    const int want = unit_weapon_for_unit_base(u, v);
    // W2-AN-14 [rules NavalTargeting/LandTargeting, defaults 0]: the water /
    // land / underwater split. A unit with all-default keys attacking a land
    // or surfaced target is unchanged (the rule returns `want`).
    // W7-06-02/09 [RE: GetFireError @0x6FBC86, FUN_006F2D80]: the victim's
    // CELL decides (Water/Beach, an.naval_cell); its TYPE's Underwater, its
    // cloak, Organic and SpeedType Hover (3) pick the slot.
    const int slot = an_naval_slot(u.an.t, want, u.w2.valid, v.an.naval_cell || (v.is_naval && v.an.in_water),
                                   v.an.t.underwater, v.an.submerged,
                                   v.an.t.organic || (!v.is_vehicle && !v.is_aircraft && !v.is_naval),
                                   v.speed_type == 3, unit_airborne(v), false);
    if (slot >= 0 && v.bomb_frame < 0 && slot_bomb_disarm(u, slot)) return -1;
    return slot;
}

bool unit_primary_can_fire_at(const Unit& u, const Unit& v) {
    if (deployment_owns_fire(u)) return false;
    if (!slot_can_target(primary_view(u), unit_airborne(v), v.armor_ix)) return false;
    const int slot = an_naval_slot(u.an.t, 0, u.w2.valid, v.an.naval_cell || (v.is_naval && v.an.in_water),
                                   v.an.t.underwater, v.an.submerged,
                                   v.an.t.organic || (!v.is_vehicle && !v.is_aircraft && !v.is_naval),
                                   v.speed_type == 3, unit_airborne(v), false);
    if (slot != 0) return false;
    return !(v.bomb_frame < 0 && slot_bomb_disarm(u, 0));
}

int unit_weapon_for_building(const Unit& u, int armor_ix, bool target_bombed, bool in_water) {
    if (deployment_owns_fire(u)) return -1;
    int want;
    if (u.w7.t.magnetic) want=u.w2.valid && slot_can_target(secondary_view(u),false,armor_ix) ? 1 : -1;
    else if (u.deploy_fire && u.deployed) {
        const int slot=u.deploy_fire_slot==1 && u.w2.valid ? 1 : 0;
        want=slot_can_target(slot==1?secondary_view(u):primary_view(u),false,armor_ix)?slot:-1;
    }
    else if (u.deploy_fire && u.is_infantry) want = slot_can_target(primary_view(u), false, armor_ix) ? 0 : -1;   // W7-01-06
    else if (!u.w2.valid) want = slot_can_target(primary_view(u), false, armor_ix) ? 0 : -1;
    else want = choose_weapon(primary_view(u), secondary_view(u), false, armor_ix);
    // W2-AN-14: LandTargeting=1 / UnderwaterOnly cannot hit a structure on
    // land. W7-06-02: one standing on the water (a Naval Yard) is a naval
    // target -- a Typhoon, Dolphin or Squid may attack it.
    const int slot = an_naval_slot(u.an.t, want, u.w2.valid, in_water, false, false, false, false, false, true);
    if (slot >= 0 && !target_bombed && slot_bomb_disarm(u, slot)) return -1;
    return slot;
}

void activate_weapon_slot(Unit& u, int slot, const VeterancyRules* vr) {
    if (slot == u.w_active && !u.gattling.enabled()) return;
    // w7r3: -1 is "no weapon can engage" (GetFireError ILLEGAL); it used to
    // fall into the secondary branch and load the Secondary's stats. A YR dog
    // (BadTeeth/ParasiteDog 0% vs vehicles, NeverUse VirtualScanner secondary)
    // ordered onto a Grizzly thus carried the scanner's SA Verses (25% heavy),
    // passed the W7-01-01 infect gate and rode tanks forever (ALL06: the last
    // BadGuy1 dog kept the house alive). Keep the current slot.
    if (slot < 0) return;
    if (slot == 1 && !u.w2.valid && !u.gattling.enabled()) return;
    const bool vet = vr && u.vet.recv;
    const UnitVet& v = u.vet;
    auto has = [&](Ability a) {
        return has_ability(v.veterancy, v.vet_abilities, v.elite_abilities, a);
    };
    // W7-07-15 [RE: 0x6FC460, 0x6FD800]: the house FirePower / ROF multipliers
    // apply to every shot -- a slot switch keeps them (w7fix-combat, armed).
    const double hfp = u.tg.w7_rof ? u.tg.house_fp : 1.0, hrof = u.tg.w7_rof ? u.tg.house_rof : 1.0;
    auto dmg = [&](int d) {
        if (vet) return fire_damage(d, v.firepower_mult, has(Ability::Firepower), vr->combat, hfp);
        return u.veteran_rank > 0 ? veteran_firepower(d, u.veteran_rank) : d;
    };
    auto rof = [&](int r) {
        if (u.tg.w7_rof) u.tg.rof_raw = r;   // W7-07-14: the active slot's raw ROF
        return vet ? rof_frames(r, has(Ability::Rof), vr->rof, hrof) : r;
    };
    if (slot == 0 && !u.gattling.enabled()) {
        const bool el = vet && unit_is_elite(u) && v.has_elite;
        const WeaponStats& w = el ? v.elite : v.base;
        u.w_damage = dmg(w.damage);
        u.w_rof = rof(w.rof);
        u.w6.burst = w.burst;                 // W6-04-01
        u.w6.omni_fire = w.omni_fire;         // W6-04-06
        u.w6.sonic = w.sonic;                 // W6-04-09
        u.w6.wall_flags = w.wall_flags;       // W6-04-04
        u.shrapnel_count = w.shrapnel_count;   // W6-04-02
        u.shrapnel_damage = w.shrapnel_damage;
        u.shrapnel_range = w.shrapnel_range;
        u.shrapnel_verses = w.shrapnel_verses;
        u.w_range = w.range;
        u.w_verses = w.verses;
        u.w_cell_spread = w.cell_spread;
        u.w_percent_at_max = w.percent_at_max;
        u.w_affects_allies = w.affects_allies;
        u.w_inf_death = w.inf_death;          // W3-B
        u.w_prone_damage = w.prone_damage;
        u.w_proj = (vet && unit_is_elite(u) && u.p_proj_elite_set) ? u.p_proj_elite : u.p_proj;
        u.w_aa = u.p_aa;
        u.w_ag = u.p_ag;
    } else {
        const WeaponSlot& s = u.gattling.enabled() ? u.gattling.weapon(slot, unit_is_elite(u)) : secondary_view(u);
        u.w_damage = dmg(s.damage);
        u.w_rof = rof(s.rof);
        u.w6.burst = s.burst;                 // W6-04-01
        u.w6.omni_fire = s.omni_fire;         // W6-04-06
        u.w6.sonic = s.sonic;                 // W6-04-09
        u.w6.wall_flags = s.wall_flags;       // W6-04-04
        u.shrapnel_count = s.shrapnel_count;   // W6-04-02
        u.shrapnel_damage = s.shrapnel_damage;
        u.shrapnel_range = s.shrapnel_range;
        u.shrapnel_verses = s.shrapnel_verses;
        u.w_range = s.range;
        u.w_verses = s.verses;
        u.w_cell_spread = s.cell_spread;
        u.w_percent_at_max = s.percent_at_max;
        u.w_affects_allies = s.affects_allies;
        u.w_inf_death = s.inf_death;          // W3-B
        u.w_prone_damage = s.prone_damage;
        u.w_proj = s.proj;
        u.w_aa = s.aa;
        u.w_ag = s.ag;
    }
    u.w_active = slot;
}

}  // namespace ra2engine

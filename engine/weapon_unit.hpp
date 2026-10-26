#pragma once
// Gap fix C -- a Unit's active weapon (slot 0 primary / slot 1 secondary) and
// the per-target pick. Used by combat.cpp (choose + activate before a shot),
// world_veterancy.cpp (re-derive w_* after a rank/crate change) and the
// projectile detonation (re-activate the slot a shell was fired from).
#include "world.hpp"

namespace ra2engine {

// A parachutist is exposed in the air, unlike passengers inside a transport.
inline bool unit_parachuting(const Unit& v) {
    return v.cargo_of == kParachuteCargo && v.an.para_alt > 0.0;
}
inline bool unit_combat_exposed(const Unit& v) {
    return v.cargo_of < 0 || unit_parachuting(v);
}
inline bool unit_airborne(const Unit& v) {
    return (v.is_aircraft && v.altitude > 0.0) || unit_parachuting(v);
}
// Self-area deployment has its own ability owner; a cached frozen flag must
// not admit ordinary primary fire during the deployment boundary.
inline bool deployment_owns_fire(const Unit& u) {
    return u.deployed && (u.w7.t.deploy_area_fire ||
        (u.abil.deployer && u.abil.deploy.valid && u.abil.deploy.rad_level>0));
}
inline double unit_weapon_facing(const Unit& u) {
    return u.is_aircraft || !u.w7.t.has_turret ? u.body_facing : u.turret_facing;
}

// fv7c (13a #2) [RE: FUN_006F6750 head `if (weapon->Range == -0x200) return
// true`]: Range=-2 (ASWLauncher, MakeupKit) is "always in range".
inline bool range_infinite(double cells) { return static_cast<int>(cells * 256.0) == -0x200; }

// The primary slot as a WeaponSlot view (for choose_weapon): valid, the
// unit's ACTIVE-independent primary verses + AA/AG.
WeaponSlot primary_view(const Unit& u);
// The secondary currently in effect (elite variant when elite and defined).
const WeaponSlot& secondary_view(const Unit& u);

// Weapon slot `u` would use against unit `v` / a building (ground, armor
// `armor_ix`); -1 = none can engage. A deployed GI-type forces its
// DeployFireWeapon slot. A BombDisarm weapon (DefuseKit) is -1 unless the
// target carries an Ivan bomb (`target_bombed` for a building).
int unit_weapon_for_unit(const Unit& u, const Unit& v);
// fv7c [RE: TechnoClass::GetFireError(target, 0) != ILLEGAL (5), as
// FUN_00707500 calls it @0x707702]: weapon index 0 (the primary) alone --
// no fall-back to the secondary.
bool unit_primary_can_fire_at(const Unit& u, const Unit& v);
// W7-06-02: `in_water` = the structure's cell is Water / Beach (a Naval Yard),
// World::an_victim_naval_cell -- then the NavalTargeting switch applies.
int unit_weapon_for_building(const Unit& u, int armor_ix, bool target_bombed = false, bool in_water = false);

// Load slot `slot` into u.w_* (damage/rof/range/verses/spread/proj/AA/AG),
// applying the veterancy multipliers when `vr` is non-null and u.vet.recv
// (the vet World), else the legacy veteran_rank firepower bake. No-op when
// slot == u.w_active. Slot 1 without a valid secondary is ignored.
void activate_weapon_slot(Unit& u, int slot, const VeterancyRules* vr);

}  // namespace ra2engine

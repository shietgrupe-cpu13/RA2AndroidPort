#pragma once
#include "building.hpp"
namespace ra2engine {
// Use the event's weapon, including its staged/AA burst, rather than the
// default Unit proxy's Burst=1. The unit muzzle projection then alternates
// the lateral barrel exactly as the existing mobile-vehicle path does.
inline const WeaponSlot& defense_event_weapon(const BuildingTypeDef& type,const std::string& id,int slot) {
    for(const auto* list:{&type.gattling.weapons,&type.gattling.elite_weapons})
        for(const auto& weapon:*list)if(weapon.id==id)return weapon;
    return type.weapon[slot==1?1:0];
}
inline int defense_muzzle_burst(const BuildingTypeDef& type,const std::string& id,int slot) {
    return std::max(1,defense_event_weapon(type,id,slot).burst);
}
inline bool staged_defense_impact(bool staged,bool projectiles_enabled,const WeaponSlot& weapon) {
    return staged && (!projectiles_enabled || !weapon.proj.flies());
}
}

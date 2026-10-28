#pragma once
#include "ra2assets/art.hpp"
#include "ra2assets/rules.hpp"
#include "unit_combat.hpp"
#include <array>
#include <map>
#include <cstdlib>

namespace ra2engine {
// Ini section keys are canonical uppercase; weapon modes are zero based.
inline std::map<int,int> ifv_turret_mapping(const ra2assets::Ini& ini,const std::string& id) {
    std::map<int,int> out;
    const std::string suffix="TURRETWEAPON";
    for(const auto& kv:ini.section(id)) {
        const auto& key=kv.first;
        if(key.size()<=suffix.size() || key.compare(key.size()-suffix.size(),suffix.size(),suffix)!=0)continue;
        const auto index=ini.get(id,key.substr(0,key.size()-suffix.size())+"TurretIndex");
        if(!index.empty())out[std::atoi(kv.second.c_str())]=std::atoi(index.c_str());
    }
    return out;
}
// GetFLH's per-weapon art entries (docs/re/voxel-render.md section 6).
// Preserve the existing primary fallback when a secondary/numbered key is
// absent or malformed; an explicit zero is valid and must not fall back.
inline std::array<int,3> weapon_art_flh(const ra2assets::Art& art,
    const std::string& image,int slot,bool elite,bool numbered) {
    int f[3]={0,0,0};
    art.fire_flh(image,f);
    const std::string key=numbered ? "Weapon"+std::to_string(slot+1)+"FLH" :
        (slot==1 ? "SecondaryFireFLH" : "PrimaryFireFLH");
    art.fire_flh(image,f,key);
    if(elite) art.fire_flh(image,f,"Elite"+key);
    return {f[0],f[1],f[2]};
}
inline void bind_weapon_art(ProjectileSpec& p,const ra2assets::Art& art,
    const std::string& image,int slot,bool elite,bool numbered,int turret_offset) {
    const auto f=weapon_art_flh(art,image,slot,elite,numbered);
    for(int k=0;k<3;++k)p.flh[k]=f[k];
    p.turret_offset=turret_offset;
}
inline void bind_unit_weapon_art(UnitCombat& c,const ra2assets::Art& art,
    const ra2assets::Rules& rules,const std::string& id) {
    const bool numbered=rules.section_double(id,"WeaponCount",0)>0;
    const int offset=art.turret_offset(c.image);
    bind_weapon_art(c.proj,art,c.image,0,false,numbered,offset);
    bind_weapon_art(c.elite_proj,art,c.image,0,true,numbered,offset);
    bind_weapon_art(c.secondary.proj,art,c.image,1,false,numbered,offset);
    bind_weapon_art(c.elite_secondary.proj,art,c.image,1,true,numbered,offset);
    for(std::size_t i=0;i<c.gattling.weapons.size();++i)
        bind_weapon_art(c.gattling.weapons[i].proj,art,c.image,static_cast<int>(i),false,true,offset);
    for(std::size_t i=0;i<c.gattling.elite_weapons.size();++i)
        bind_weapon_art(c.gattling.elite_weapons[i].proj,art,c.image,static_cast<int>(i),true,true,offset);
}
}

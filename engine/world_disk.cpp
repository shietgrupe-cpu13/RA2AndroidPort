#include "world.hpp"
#include "combat.hpp"
#include "ra2assets/rules.hpp"
#include <algorithm>

namespace ra2engine {
void World::configure_disc_laser(const ra2assets::Rules& rules) {
    disk_.enabled=true;disk_.sources.clear();
    for(const char* list:{"VehicleTypes","AircraftTypes","InfantryTypes"})
        for(const auto& id:rules.listed_types(list)) {
            const auto primary=rules.section_string(id,"Primary","");
            if(rules.section_flag(primary,"DiskLaser",false))disk_.sources[id]=true;
        }
}
bool World::disc_laser_weapon(const Unit& u) const {
    const auto& id=u.tg.t.rules_id.empty()?u.image:u.tg.t.rules_id;
    return disk_.enabled && u.w_active==0 && disk_.sources.find(id)!=disk_.sources.end();
}
bool World::queue_disc_laser(int source,int target,int building,int cell_x,int cell_y) const {
    if(source<0 || source>=int(units_.size()) || !disc_laser_weapon(units_[source]))return false;
    DiskShot shot;shot.source=source;shot.target=target;shot.building=building;
    shot.cell_x=cell_x;shot.cell_y=cell_y;shot.weapon=units_[source];
    int x=cell_x*256,y=cell_y*256,z=0;
    if(target>=0 && target<int(units_.size())){x=unit_lept_x(units_[target]);y=unit_lept_y(units_[target]);}
    else if(building>=0 && building<int(buildings_.size()))
        building_aim_lept(buildings_[building],cell_level(buildings_[building].cx,buildings_[building].cy),x,y,z);
    shot.start_dx=x-unit_lept_x(units_[source]);shot.start_dy=y-unit_lept_y(units_[source]);
    // Retain combat values/veterancy for this shot; discard unrelated path data.
    shot.weapon.path.clear();shot.weapon.path_idx=0;
    disk_.shots.push_back(std::move(shot));
    return true;
}
void World::step_disc_lasers() {
    disk_.events.clear();
    if(!disk_.enabled)return;
    // New shots are queued by combat after this phase, so they begin next tick.
    // Latest shots update first, without consulting a wall clock or a new RNG.
    for(std::size_t index=disk_.shots.size();index-->0;) {
        auto& shot=disk_.shots[index];
        if(shot.wait>0){--shot.wait;continue;}
        bool valid=shot.source>=0 && shot.source<int(units_.size());
        int tx=0,ty=0,tz=0;
        if(valid) {
            const auto& live=units_[shot.source];
            valid=live.alive && live.die_time<0 && live.cargo_of<0 && !live.an.crashing;
            shot.weapon.fx=live.fx;shot.weapon.fy=live.fy;shot.weapon.cell_x=live.cell_x;
            shot.weapon.cell_y=live.cell_y;shot.weapon.level=live.level;shot.weapon.on_deck=live.on_deck;
            shot.weapon.altitude=live.altitude;shot.weapon.house=live.house;
        }
        if(shot.target>=0) {
            valid=valid && shot.target<int(units_.size());
            if(valid){const auto& v=units_[shot.target];valid=v.alive && v.die_time<0 && v.cargo_of<0;
                tx=unit_lept_x(v);ty=unit_lept_y(v);tz=unit_lept_z(v);}
        } else if(shot.building>=0) {
            valid=valid && shot.building<int(buildings_.size());
            if(valid){const auto& b=buildings_[shot.building];valid=b.alive && b.state!=Building::State::Dead;
                building_aim_lept(b,cell_level(b.cx,b.cy),tx,ty,tz);}
        } else {
            valid=valid && shot.cell_x>=0 && shot.cell_y>=0 && shot.cell_x<grid_w_ && shot.cell_y<grid_h_;
            if(valid){tx=shot.cell_x*256;ty=shot.cell_y*256;tz=cell_level(shot.cell_x,shot.cell_y)*104;}
        }
        int foundation_allowance=0;
        if(valid && shot.building>=0) {
            const auto& b=buildings_[shot.building];foundation_allowance=(b.fw+b.fh)*64;
        }
        if(valid) valid=weapon_in_range(*this,shot.weapon,tx,ty,tz,foundation_allowance);
        if(!valid){disk_.shots.erase(disk_.shots.begin()+index);continue;}
        disk_.events.push_back({shot.source,shot.target,shot.building,shot.cell_x,shot.cell_y,shot.phase,
            shot.weapon.w_proj.weapon_id,shot.weapon.w_damage,shot.start_dx,shot.start_dy});
        if(shot.phase==8) {
            damage_area(*this,units_,buildings_,occ_,shot.weapon,shot.source,-1,tx,ty,tz);
            disk_.shots.erase(disk_.shots.begin()+index);
        } else {++shot.phase;shot.wait=1;}
    }
}
}

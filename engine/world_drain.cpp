#include "world.hpp"
#include "weapon_unit.hpp"
#include "ra2assets/rules.hpp"
#include "orig_math.hpp"
#include <algorithm>

namespace ra2engine {
void World::configure_disc_drain(const ra2assets::Rules& rules) {
    drain_.sources.clear();drain_.targets.clear();drain_.enabled=true;
    drain_.amount=std::max(0,om::ftol(rules.section_double("CombatDamage","DrainMoneyAmount",30)));
    drain_.delay=std::max(1,om::ftol(rules.section_double("CombatDamage","DrainMoneyFrameDelay",30)));
    for(const auto& id:rules.listed_types("VehicleTypes")) {
        const auto weapon=rules.section_string(id,"Secondary","");
        if(rules.section_flag(weapon,"DrainWeapon",false))drain_.sources[id]=true;
    }
    for(const auto& id:rules.listed_types("BuildingTypes"))
        drain_.targets[id]={rules.section_flag(id,"Drainable",false),
            rules.section_flag(id,"ResourceDestination",false),
            om::ftol(rules.section_double(id,"Power",0))};
}

bool World::disc_drain_order(const Unit& u,int bi) const {
    if(!drain_.enabled || !u.is_aircraft || !u.is_vehicle || !u.balloon_hover ||
       !u.w2.valid || !u.attack_order || u.target_building!=bi || u.target>=0 ||
       bi<0 || bi>=static_cast<int>(buildings_.size()))return false;
    const auto src=drain_.sources.find(u.tg.t.rules_id);const auto& b=buildings_[bi];
    if(src==drain_.sources.end() || !src->second || !b.alive || b.selling ||
       b.state!=Building::State::Idle || b.type<0 || b.type>=static_cast<int>(btypes().size()) ||
       !is_enemy(u.house,b.house))return false;
    const auto target=drain_.targets.find(btypes()[b.type].id);
    return target!=drain_.targets.end() && target->second.enabled &&
        slot_can_target(secondary_view(u),false,b.armor_ix);
}

bool World::building_disc_drained(int bi) const {
    for(const auto& link:drain_.links)
        if(link.building==bi && link.unit>=0 && link.unit<static_cast<int>(units_.size())) {
            const auto& u=units_[link.unit];
            if(u.alive && u.die_time<0 && !u.frozen && !u.an.crashing &&
               u.house==link.source_house && disc_drain_order(u,bi) &&
               buildings_[bi].house==link.target_house && !u.has_flight_goal &&
               u.cell_x>=buildings_[bi].cx && u.cell_x<buildings_[bi].cx+buildings_[bi].fw &&
               u.cell_y>=buildings_[bi].cy && u.cell_y<buildings_[bi].cy+buildings_[bi].fh)return true;
        }
    return false;
}

bool World::house_disc_drained(int house) const {
    for(const auto& link:drain_.links) {
        if(link.target_house!=house || !building_disc_drained(link.building))continue;
        const auto& b=buildings_[link.building];const auto type=drain_.targets.find(btypes()[b.type].id);
        if(b.house==house && type!=drain_.targets.end() && !type->second.refinery && type->second.power>=0)return true;
    }
    return false;
}

void World::step_disc_drains() {
    if(!drain_.enabled)return;
    std::vector<DrainLink> next;
    for(int i=0;i<static_cast<int>(units_.size());++i) {
        auto& u=units_[i];const int bi=u.target_building;
        // A former drain ending through capture/sale must not turn into an
        // accidental force-fired laser shot against the new owner.
        bool ownership_ended=false;
        for(const auto& old:drain_.links)if(old.unit==i && old.building==bi) {
            ownership_ended=u.house!=old.source_house || bi<0 ||
                bi>=static_cast<int>(buildings_.size()) ||
                buildings_[bi].house!=old.target_house || buildings_[bi].selling;
            break;
        }
        if(ownership_ended){u.target_building=-1;u.attack_order=false;continue;}
        if(!u.alive || u.die_time>=0 || u.cargo_of>=0 || u.frozen || u.an.crashing ||
           !disc_drain_order(u,bi))continue;
        const auto& b=buildings_[bi];
        // Use the existing jumpjet locomotor, never teleport the source.
        if(u.cell_x<b.cx || u.cell_x>=b.cx+b.fw || u.cell_y<b.cy || u.cell_y>=b.cy+b.fh ||
           u.flight_state!=FlightState::Cruising || u.has_flight_goal) {
            const int gx=b.cx+(b.fw-1)/2,gy=b.cy+(b.fh-1)/2;
            if(!u.has_flight_goal || u.flight_goal_x!=gx || u.flight_goal_y!=gy) {
                order_unit_move(i,gx,gy);
                u.target=-1;u.target_building=bi;u.attack_order=true;
            }
            continue;
        }
        DrainLink link{i,bi,u.house,b.house,0};
        for(const auto& old:drain_.links)
            if(old.unit==i && old.building==bi && old.source_house==u.house && old.target_house==b.house)
                link.frames=old.frames;
        if(++link.frames>=drain_.delay) {
            link.frames=0;
            const auto& type=drain_.targets.at(btypes()[b.type].id);
            if(type.refinery) {
                const double available=credits(b.house);
                const double amount=std::min(double(drain_.amount),std::max(0.0,available));
                // One wallet adapter serves human, AI and campaign houses;
                // it also refreshes FactoryClass's shared production pool.
                set_wallet(b.house,available-amount);
                set_wallet(u.house,credits(u.house)+amount);
            }
        }
        next.push_back(link);
    }
    drain_.links=std::move(next);
}
}

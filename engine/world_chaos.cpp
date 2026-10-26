#include "world.hpp"
#include "weapon_unit.hpp"
#include "combat.hpp"
#include "ra2assets/rules.hpp"
#include "ra2assets/ini.hpp"
#include "orig_math.hpp"
#include <algorithm>
#include <sstream>
namespace ra2engine {
namespace {
const std::string& key(const Unit& u) { return u.tg.t.rules_id.empty()?u.image:u.tg.t.rules_id; }
bool live(const Unit& u) { return u.alive && u.die_time<0 && u.cargo_of<0; }
}
void World::configure_chaos(const ra2assets::Rules& rules,const ra2assets::Ini& ini) {
    chaos_.enabled=true;chaos_.sources.clear();chaos_.friendly.clear();chaos_.immune.clear();
    for(const auto* section:{"VehicleTypes","InfantryTypes","AircraftTypes","BuildingTypes"})
        for(const auto& id:rules.listed_types(section)) {
            int mask=0;
            for(int slot=0;slot<2;++slot) {
                const auto weapon=rules.section_string(id,slot?"Secondary":"Primary","");
                if(rules.section_flag(weapon,"AreaFire",false) &&
                   rules.section_flag(rules.weapon_warhead(weapon),"Psychedelic",false))mask|=1<<slot;
            }
            if(mask) {
                if(rules.section_flag(id,"Deployer",false) && rules.section_flag(id,"DeployFire",false))mask|=4;
                chaos_.sources[id]=mask;
            }
            chaos_.friendly[id]=rules.section_flag(id,"BerserkFriendly",false);
            chaos_.immune[id]=rules.section_flag(id,"ImmuneToPsionicWeapons",false);
        }
    chaos_.tint={0,0,0};
    const int index=om::ftol(rules.section_double("AudioVisual","BerserkColor",0));
    const auto& colors=ini.section("ColorAdd");
    if(index>=0 && index<static_cast<int>(colors.size())) {
        auto value=colors[index].second;std::replace(value.begin(),value.end(),',',' ');
        std::istringstream in(value);int r=0,g=0,b=0;
        if(in>>r>>g>>b)chaos_.tint={std::clamp(r,0,31)*8,std::clamp(g,0,63)*4,std::clamp(b,0,31)*8};
    }
}
bool World::chaos_weapon(const Unit& u) const {
    if(!chaos_.enabled)return false;
    const auto it=chaos_.sources.find(key(u));
    return it!=chaos_.sources.end() && (it->second&(1<<std::clamp(u.w_active,0,1)));
}
bool World::unit_berserk(int i) const {
    const auto it=chaos_.active.find(i);
    return it!=chaos_.active.end() && it->second.frames>0 && i>=0 &&
        i<static_cast<int>(units_.size()) && live(units_[i]);
}
bool World::combat_hostile(const Unit& source,const Unit& target) const {
    if(chaos_.enabled)for(const auto& active:chaos_.active)
        if(unit_berserk(active.first) && &units_[active.first]==&source) {
            const auto friendly=chaos_.friendly.find(key(target));
            return &source!=&target && (friendly==chaos_.friendly.end() || !friendly->second);
        }
    return is_enemy(source.house,target.house);
}
void World::note_chaos_hit(int unit,int frames) const {
    if(!chaos_.enabled || frames<=0 || unit<0 || unit>=static_cast<int>(units_.size()))return;
    const auto& u=units_[unit];const auto it=chaos_.immune.find(key(u));
    if(!live(u) || u.tg.t.immune_psionics || (it!=chaos_.immune.end() && it->second))return;
    chaos_.pending[unit]=frames;
}
bool World::combat_hostile(const Unit& source,const Building& target) const {
    if(chaos_.enabled)for(const auto& active:chaos_.active)
        if(unit_berserk(active.first) && &units_[active.first]==&source) {
            if(target.type<0 || target.type>=static_cast<int>(btypes().size()))return false;
            const auto friendly=chaos_.friendly.find(btypes()[target.type].id);
            return friendly==chaos_.friendly.end() || !friendly->second;
        }
    return is_enemy(source.house,target.house);
}
bool World::deploy_chaos(int i) {
    if(i<0 || i>=static_cast<int>(units_.size()))return false;
    auto& u=units_[i];
    if(!chaos_weapon(u) || !live(u) || u.frozen || u.rearm_frames>0)return false;
    if(!(chaos_.sources.at(key(u))&4))return false;
    order_stop(i);
    damage_area(*this,units_,buildings_,occ_,u,i,-1,unit_lept_x(u),unit_lept_y(u),unit_lept_z(u));
    u.rearm_frames=unit_rearm_frames(u,&combat_rng_);u.fire_flash=0.4;
    resolve_chaos_hits();
    return true;
}
void World::resolve_chaos_hits() {
    if(!chaos_.enabled)return;
    for(const auto& hit:chaos_.pending) {
        auto& u=units_[hit.first];
        if(!live(u))continue;
        auto it=chaos_.active.find(hit.first);
        if(it==chaos_.active.end()) {
            const bool hold=u.manual_hold;
            order_stop(hit.first);
            chaos_.active[hit.first]={hit.second,hold};
            u.manual_hold=true;u.tg.area_guard=false;u.tg.mission=15;u.tg.scan_timer=0;
            // Leave offensive AI teams; ownership and diplomacy are unchanged.
            for(auto& rec:ai_houses_)for(auto& team:rec.team_state.teams)
                for(int k=static_cast<int>(team.members.size())-1;k>=0;--k)
                    if(team.members[k]==hit.first) {
                        team.members.erase(team.members.begin()+k);
                        team.member_types.erase(team.member_types.begin()+k);
                    }
        } else {
            // Refresh only the timer: a second gas hit is not a new Hunt order.
            it->second.frames=hit.second;
        }
    }
    chaos_.pending.clear();
}
void World::step_chaos_timers(double dt) {
    if(!chaos_.enabled)return;
    const int frames=std::max(0,om::ftol(dt*15.0+0.5));
    for(auto it=chaos_.active.begin();it!=chaos_.active.end();) {
        const int i=it->first;auto& status=it->second;
        if(i<0 || i>=static_cast<int>(units_.size()) || !live(units_[i])) {it=chaos_.active.erase(it);continue;}
        status.frames=std::max(0,status.frames-frames);
        if(!status.frames) {
            const bool hold=status.manual_hold;
            order_stop(i);units_[i].manual_hold=hold;
            it=chaos_.active.erase(it);
        } else ++it;
    }
}
}

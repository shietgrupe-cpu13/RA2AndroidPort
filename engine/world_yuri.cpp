#include "world.hpp"
#include "diagnostics.hpp"
#include "harvest.hpp"
#include <algorithm>

namespace ra2engine {
namespace {
bool active(const Unit& u) { return u.alive && u.die_time < 0 && u.cargo_of < 0 && u.garrison_of < 0; }
}

void World::release_unit_mind_control(int controller) {
    if (controller < 0 || controller >= static_cast<int>(units_.size())) return;
    auto victims=std::move(units_[controller].w7.mc_victims);
    units_[controller].w7.mc_victims.clear();
    units_[controller].w7.overload_left=0;
    for (int vi : victims) {
        if (vi < 0 || vi >= static_cast<int>(units_.size())) continue;
        Unit& v=units_[vi]; if (v.w7.mc_controller != controller) continue;
        const int original=v.w7.mc_original_house;
        v.w7.mc_controller=v.w7.mc_original_house=-1;
        if (v.alive && v.die_time < 0 && original >= 0) set_unit_owner(vi,original);
    }
}

void World::prepare_skirmish_mind_control() {
    // CampaignRuntime owns unit-controller bookkeeping; towers are shared.
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& v = units_[i];
        const int c = v.w7.mc_controller;
        const int bi = v.w7.mc_building;
        if (bi >= 0 && (bi >= static_cast<int>(buildings_.size()) || !buildings_[bi].alive ||
            buildings_[bi].state != Building::State::Idle || buildings_[bi].house != v.house ||
            buildings_[bi].power_off || buildings_[bi].w7.warp_unpowered ||
            (buildings_[bi].type >= 0 && buildings_[bi].type < static_cast<int>(btypes().size()) &&
             btypes()[buildings_[bi].type].powered && house_low_power(buildings_[bi].house)) || !v.alive || v.die_time>=0)) {
            if (bi < static_cast<int>(buildings_.size())) {
                auto& held = buildings_[bi].w7.mc_victims;
                held.erase(std::remove(held.begin(),held.end(),i),held.end());
            }
            if (v.alive && v.die_time < 0 && v.w7.mc_original_house >= 0)
                set_unit_owner(i,v.w7.mc_original_house);
            v.w7.mc_building = v.w7.mc_original_house = -1;
        }
        if (!playfield_.campaign && c >= 0 && (c >= static_cast<int>(units_.size()) || !active(units_[c]) ||
                       units_[c].warp_remaining > 0 || !v.alive || v.die_time>=0 || units_[c].house != v.house)) {
            if (c < static_cast<int>(units_.size())) {
                auto& victims = units_[c].w7.mc_victims;
                victims.erase(std::remove(victims.begin(), victims.end(), i), victims.end());
            }
            if (v.alive && v.die_time < 0 && v.w7.mc_original_house >= 0)
                set_unit_owner(i, v.w7.mc_original_house);
            v.w7.mc_controller = v.w7.mc_original_house = -1;
        }
    }
    if (playfield_.campaign) return;
    for (Unit& u : units_) {
        if (!u.w7.t.mind_control) continue;
        auto& held=u.w7.mc_victims;
        const int ci=static_cast<int>(&u-units_.data());
        held.erase(std::remove_if(held.begin(),held.end(),[&](int vi) {
            return vi<0 || vi>=static_cast<int>(units_.size()) || !units_[vi].alive || units_[vi].die_time>=0 ||
                units_[vi].w7.mc_controller!=ci || units_[vi].house!=u.house;
        }),held.end());
        if (u.w7.t.mc_infinite && active(u)) {
            const auto& t=u.w7.t;
            const auto n=std::min({t.overload_count.size(),t.overload_damage.size(),t.overload_frames.size()});
            std::size_t band=0;
            while (band+1<n && static_cast<int>(held.size())>t.overload_count[band]) ++band;
            if (n && t.overload_damage[band]>0 && static_cast<int>(held.size())>u.w7.t.mc_capacity) {
                if (u.w7.overload_left>0) --u.w7.overload_left;
                else { ability_damage_unit(ci,t.overload_damage[band]); u.w7.overload_left=std::max(1,t.overload_frames[band])-1; }
            } else u.w7.overload_left=0;
        }
        u.w_mind_control = true;
        if (!active(u) || u.deployed) { u.w7.mc_shot = -1; u.target=-1; u.attack_order=false; continue; }
        if (u.target >= 0 && u.target < static_cast<int>(units_.size())) {
            const Unit& v = units_[u.target];
            const bool full = !u.w7.t.mc_infinite &&
                static_cast<int>(u.w7.mc_victims.size()) >= u.w7.t.mc_capacity;
            if (!psionic_target_ok(u.target) || v.an.iron_left_frames > 0 || v.w7.mc_controller >= 0 || v.w7.mc_building >= 0 ||
                (full && (!u.attack_order || u.w7.t.mc_capacity != 1))) {
                u.target = -1; u.attack_order = false;
            }
        }
    }
}

void World::resolve_skirmish_mind_control() {
    if (playfield_.campaign) return;
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[i];
        if (!u.w7.t.mind_control) continue;
        const int vi = u.w7.mc_shot; u.w7.mc_shot = -1;
        if (!active(u) || u.deployed || vi < 0 || vi >= static_cast<int>(units_.size()) || vi == i) continue;
        Unit& v = units_[vi];
        if (!active(v) || v.house == u.house || v.is_aircraft || !psionic_target_ok(vi) ||
            v.an.iron_left_frames > 0 || v.w7.mc_controller >= 0 || v.w7.mc_building >= 0) continue;
        auto& held = u.w7.mc_victims;
        if (!u.w7.t.mc_infinite && static_cast<int>(held.size()) >= std::max(1, u.w7.t.mc_capacity)) {
            if (u.w7.t.mc_capacity != 1) continue;
            if (!u.attack_order) { u.target=-1; continue; } // full clones never auto-swap victims
            const int old = held.front();
            if (old >= 0 && old < static_cast<int>(units_.size())) {
                Unit& freed = units_[old];
                if (freed.w7.mc_controller == i) {
                    if (freed.alive && freed.die_time < 0 && freed.w7.mc_original_house >= 0)
                        set_unit_owner(old, freed.w7.mc_original_house);
                    freed.w7.mc_controller = freed.w7.mc_original_house = -1;
                }
            }
            held.clear();
        }
        const int original = v.house;
        set_unit_owner(vi, u.house);
        v.w7.mc_original_house = original; v.w7.mc_controller = i;
        held.push_back(vi);
        u.target = -1; u.attack_order = false;
    }
}

bool World::try_building_mind_control(int bi,int vi,int slot) {
    if (bi<0 || bi>=static_cast<int>(buildings_.size()) ||
        vi<0 || vi>=static_cast<int>(units_.size()) || slot<0 || slot>1) return false;
    Building& b=buildings_[bi]; Unit& v=units_[vi];
    if (!b.alive || b.state!=Building::State::Idle || b.power_off || b.w7.warp_unpowered ||
        b.type<0 || b.type>=static_cast<int>(btypes().size())) return false;
    const auto& type=btypes()[b.type];
    if (!type.mind_control || (type.powered && house_low_power(b.house)) ||
        !active(v) || v.house==b.house || v.is_aircraft || !psionic_target_ok(vi) ||
        v.an.iron_left_frames>0 || v.w7.mc_controller>=0 || v.w7.mc_building>=0 ||
        static_cast<int>(b.w7.mc_victims.size())>=std::max(1,type.weapon[slot].damage)) return false;
    const int original=v.house;
    set_unit_owner(vi,b.house);
    v.w7.mc_original_house=original; v.w7.mc_building=bi;
    b.w7.mc_victims.push_back(vi);
    return true;
}

bool World::capture_building_with_unit(int unit_idx, int b_idx, int weapon_slot) {
    if (unit_idx < 0 || unit_idx >= static_cast<int>(units_.size()) ||
        b_idx < 0 || b_idx >= static_cast<int>(buildings_.size()) || weapon_slot != 0)
        return false;
    const Unit& controller = units_[static_cast<std::size_t>(unit_idx)];
    const Building& target = buildings_[static_cast<std::size_t>(b_idx)];
    if (!active(controller) || controller.tg.t.rules_id != "YURIPR" ||
        !target.alive || target.state == Building::State::Dead ||
        !is_enemy(controller.house, target.house))
        return false;
    const int startup = target.type >= 0 && target.type < static_cast<int>(types_.size())
                            ? types_[static_cast<std::size_t>(target.type)].produce_cash_startup
                            : 0;
    return capture_building(b_idx, controller.house, startup);
}

void World::step_yuri_workers(bool relocate) {
    // An undeployed/destroyed/captured manager releases its workers. A known
    // killer receives freed slaves; absent a killer they retain their house.
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[i]; const int manager = u.w7.slave_manager;
        if (manager == -1) continue;
        if (manager < -1) {
            const int mobile = -2-manager;
            if (mobile >= 0 && mobile < static_cast<int>(units_.size()) &&
                active(units_[mobile]) && units_[mobile].house == u.house) {
                const Unit& miner = units_[mobile];
                const bool stale=u.moving() && std::max(std::abs(u.path.back().x-miner.cell_x),std::abs(u.path.back().y-miner.cell_y))>2;
                if (active(u) && !unit_berserk(i) && (!u.moving() || u.wait_time>=3.0 || stale) &&
                    std::max(std::abs(u.cell_x-miner.cell_x),std::abs(u.cell_y-miner.cell_y)) > 2 &&
                    (ability_frame()+static_cast<unsigned>(i))%30==0) {
                    // Follow to a free reachable neighbour, not the manager's
                    // occupied centre. Distinct worker goals avoid piling up.
                    bool routed=false;
                    for(int r=1;r<=2 && !routed;++r)for(int dy=-r;dy<=r && !routed;++dy)for(int dx=-r;dx<=r && !routed;++dx) {
                        if(std::abs(dx)!=r && std::abs(dy)!=r)continue;
                        const int x=miner.cell_x+dx,y=miner.cell_y+dy;
                        if(!unit_can_enter_cell(u,x,y) || !occ_.can_enter(x,y,i,u.on_deck))continue;
                        bool claimed=false;
                        for(int j=0;j<static_cast<int>(units_.size());++j)if(j!=i && units_[j].w7.slave_manager==manager && units_[j].moving() &&
                            units_[j].path.back().x==x && units_[j].path.back().y==y){claimed=true;break;}
                        if(claimed)continue;
                        order_unit_move(i,x,y);
                        routed=u.moving() && u.path.back().x==x && u.path.back().y==y;
                    }
                }
                continue;
            }
            u.w7.slave_manager = -1;
            continue;
        }
        if (manager < static_cast<int>(buildings_.size())) {
            const Building& b = buildings_[manager];
            if (b.alive && b.state != Building::State::Dead && b.house == u.house) continue;
            if (b.killed_by >= 0 && b.killed_by < static_cast<int>(units_.size()) && active(u))
                set_unit_owner(i, units_[b.killed_by].house);
        }
        diag::note("SLAVE released unit=%d previous_manager=%d house=%d",i,manager,u.house);
        u.w7.slave_manager = -1; u.is_harvester = false;
        u.harv = HarvState::None; u.dock_refinery = -1;
        u.path.clear(); u.path_idx = 0;
    }
    for (int bi = 0; bi < static_cast<int>(buildings_.size()); ++bi) {
        Building& b = buildings_[bi];
        if (!b.alive || b.state != Building::State::Idle || b.type < 0 ||
            b.type >= static_cast<int>(btypes().size())) continue;
        const BuildingTypeDef& def = btypes()[b.type];
        if (def.slave_count <= 0 || def.slave_combat.image.empty()) continue;
        int count = 0;
        for (const Unit& u : units_)
            if (u.alive && u.die_time < 0 && u.w7.slave_manager == bi) ++count;
        if (count >= def.slave_count) continue;
        if (b.w7.slave_regen_left > 0) { --b.w7.slave_regen_left; continue; }
        const int to_spawn = b.w7.slaves_initialized ? 1 : def.slave_count - count;
        int spawned = 0;
        for (int n = 0; n < to_spawn; ++n) {
            const int id = launch_unit(bi, def, def.slave_combat);
            if (id < 0) {
                if (ability_frame()%150==0) diag::note("SLAVE spawn-blocked manager=%d live=%d wanted=%d",bi,count,def.slave_count);
                break; // blocked exits retry without deleting workers
            }
            make_harvester(id, false, std::max(1, def.slave_storage));
            units_[id].w7.slave_manager = bi;
            units_[id].dock_refinery = bi;
            diag::note("SLAVE spawned manager=%d unit=%d cell=%d,%d replacement=%d regen_frames=%d",bi,id,units_[id].cell_x,units_[id].cell_y,b.w7.slaves_initialized,def.slave_regen_rate);
            ++spawned;
        }
        if (spawned > 0) {
            b.w7.slaves_initialized = true;
            b.w7.slave_regen_left = std::max(1, def.slave_regen_rate);
        }
    }
    // A miner placed from the building strip starts deployed. If its local
    // field is exhausted (or it was placed away from ore), pack and route it
    // to a reachable field. Do not move a manager with unbanked worker cargo.
    if (relocate) for (int bi=0;bi<static_cast<int>(buildings_.size());++bi) {
        if ((ability_frame()+static_cast<unsigned>(bi))%150!=0 || !can_pack_slave_miner(bi)) continue;
        const auto& b=buildings_[bi];
        bool loaded=false;
        int near_x=0,near_y=0;
        bool local=false;
        // Workers are the reachability authority: the manager's foundation is blocked.
        for (int wi=0;wi<static_cast<int>(units_.size()) && !local;++wi) {
            const auto& worker=units_[wi];
            if (!active(worker) || worker.w7.slave_manager!=bi) continue;
            if (worker.harv==HarvState::MoveToOre || worker.harv==HarvState::Harvesting) {
                local=ore_.has_ore(worker.harv_cell_x,worker.harv_cell_y);
                if (local) break;
            }
            local=find_reachable_ore(ore_,passable_,grid_w_,grid_h_,occ_,wi,
                worker.cell_x,worker.cell_y,14,-1,-1,near_x,near_y,nullptr,
                b.cx+(b.fw-1)/2,b.cy+(b.fh-1)/2);
        }
        if (local) continue;
        for (const auto& worker:units_)
            if (active(worker) && worker.w7.slave_manager==bi && worker.bales_ore+worker.bales_gems>0) {loaded=true;break;}
        if (loaded) continue;
        int x=0,y=0;
        if (!find_reachable_ore(ore_,passable_,grid_w_,grid_h_,occ_,-1,b.cx,b.cy,48,-1,-1,x,y)) continue;
        const int house=b.house;
        if (order_pack_slave_miner(bi,house)) {
            const int mobile=static_cast<int>(units_.size())-1;
            order_unit_move(mobile,x,y);
            diag::note("MINER relocate building=%d mobile=%d ore=%d,%d moving=%d",bi,mobile,x,y,units_[mobile].moving());
        }
    }
    // Initial/packed mobile miner wake-up, staggered by stable unit ID.
    // Vanilla defaults: KickFrameDelay=150, ShortScan=8, LongScan=48.
    // Explicit Stop/Guard uses the existing saved manual_hold flag.
    for (int i=0;i<static_cast<int>(units_.size());++i) {
        auto& miner=units_[i];
        if ((ability_frame()+static_cast<unsigned>(i))%150!=0 || !active(miner) ||
            miner.moving() || miner.frozen || miner.manual_hold || miner.deploy_mission!=0 ||
            miner.target>=0 || miner.target_building>=0 || miner.attack_cell_x>=0 || miner.tg.area_guard ||
            miner.deploys_into<0 || miner.deploys_into>=static_cast<int>(btypes().size()) ||
            btypes()[miner.deploys_into].slave_count<=0) continue;
        int x=0,y=0;
        if (!find_reachable_ore(ore_,passable_,grid_w_,grid_h_,occ_,i,miner.cell_x,miner.cell_y,
            48,miner.harv_skip_x,miner.harv_skip_y,x,y)) continue;
        if (std::max(std::abs(x-miner.cell_x),std::abs(y-miner.cell_y))<=2 &&
            can_deploy_at(i,miner.cell_x,miner.cell_y)) {
            const bool deployed=order_deploy_mcv_auto(i);
            diag::note("MINER auto-deploy unit=%d ore=%d,%d accepted=%d",i,x,y,deployed);
        } else {
            // Route to a legal deployment footprint, not merely to ore which
            // may border a cliff/building. Test near-field sites in stable
            // travel-distance order; the normal pathfinder rejects pockets.
            std::vector<Cellxy> sites;
            for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
                if(can_deploy_at(i,x+dx,y+dy))sites.push_back({x+dx,y+dy});
            std::stable_sort(sites.begin(),sites.end(),[&](Cellxy a,Cellxy b){
                auto cost=[&](Cellxy p){return std::max(std::abs(p.x-miner.cell_x),std::abs(p.y-miner.cell_y))*8+
                    std::max(std::abs(p.x-x),std::abs(p.y-y));};
                return cost(a)<cost(b);
            });
            for(const auto& site:sites) {
                order_unit_move(i,site.x,site.y);
                if(miner.moving()) {
                    miner.deploy_goal_x=site.x;miner.deploy_goal_y=site.y;
                    miner.deploy_mission=4; // saved quiet walk-then-deploy
                    deploy_any_=true;
                    break;
                }
            }
            if (!miner.moving()) {miner.harv_skip_x=x;miner.harv_skip_y=y;}
            else {miner.harv_skip_x=miner.harv_skip_y=-1;}
            diag::note("MINER ore-route unit=%d ore=%d,%d moving=%d",i,x,y,miner.moving());
        }
    }
}
} // namespace ra2engine

#pragma once
#include "world.hpp"
#include "ground_presentation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace ra2engine {
// Display-only history. Never saved, hashed, or fed back to the simulator.
class UnitPresentation {
    struct Pose {
        double x,y,body,turret,ground,altitude;
        int house,cargo,garrison,deck;
        bool alive,dying,frozen;
        std::string image;
    };
    std::vector<Pose> current_, previous_;
    static Pose pose(const Unit& u) {
        return {u.fx,u.fy,u.body_facing,u.turret_facing,double(u.level+(u.on_deck?4:0)),u.altitude,u.house,u.cargo_of,u.garrison_of,
                u.on_deck,u.alive,u.die_time>=0,u.frozen,u.image};
    }
    static bool continuous(const Pose& a,const Pose& b) {
        return a.house==b.house && a.cargo==b.cargo && a.garrison==b.garrison && a.deck==b.deck &&
            a.alive && b.alive && !a.dying && !b.dying && !a.frozen && !b.frozen && a.image==b.image &&
            std::hypot(a.x-b.x,a.y-b.y)<=1.5;
    }
public:
    void clear(){current_.clear();previous_.clear();}
    bool moving(std::size_t id,const Unit& u) const {
        if(id>=previous_.size() || id>=current_.size())return false;
        const auto& a=previous_[id];const auto& b=current_[id];
        return continuous(a,b) && continuous(b,pose(u)) &&
            (a.x!=b.x || a.y!=b.y || a.body!=b.body || a.turret!=b.turret || a.ground!=b.ground || a.altitude!=b.altitude);
    }
    std::array<double,2> position(std::size_t id,const Unit& u,double fraction) const {
        if(!moving(id,u))return {u.fx,u.fy};
        const auto& a=previous_[id];const auto& b=current_[id];const double t=std::clamp(fraction,0.0,1.0);
        return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
    }
    void capture(const std::vector<Unit>& units) {
        previous_.swap(current_);current_.clear();current_.reserve(units.size());
        for(const auto& u:units)current_.push_back(pose(u));
    }
    template<class W> void capture_world(const W& w) {
        capture(w.units());
        for(std::size_t id=0;id<current_.size();++id)current_[id].ground=moving_ground_level(w,w.units()[id]);
    }
    double ground_level(std::size_t id,const Unit& u,double fallback,double fraction) const {
        if(id>=previous_.size() || id>=current_.size() || !continuous(previous_[id],current_[id]) ||
            !continuous(current_[id],pose(u)))return fallback;
        return previous_[id].ground+(current_[id].ground-previous_[id].ground)*std::clamp(fraction,0.0,1.0);
    }
    void apply(std::size_t id,Unit& draw,double fraction) const {
        if(id>=previous_.size() || id>=current_.size())return;
        const auto& a=previous_[id];const auto& b=current_[id];
        if(!continuous(a,b) || !continuous(b,pose(draw)))return;
        const double t=std::clamp(fraction,0.0,1.0);
        draw.fx=a.x+(b.x-a.x)*t;draw.fy=a.y+(b.y-a.y)*t;
        draw.altitude=a.altitude+(b.altitude-a.altitude)*t;
        auto angle=[&](double from,double to){return std::fmod(from+std::remainder(to-from,65536.0)*t+65536.0,65536.0);};
        draw.body_facing=angle(a.body,b.body);draw.turret_facing=angle(a.turret,b.turret);
    }
};
class BuildingPresentation {
    struct Pose {double facing;int type,house,x,y;bool live;};
    std::vector<Pose> current_,previous_;
public:
    void clear(){current_.clear();previous_.clear();}
    void capture(const std::vector<Building>& buildings) {
        previous_.swap(current_);current_.clear();current_.reserve(buildings.size());
        for(const auto& b:buildings)current_.push_back({b.def_turret_facing,b.type,b.house,b.cx,b.cy,
            b.alive && b.state==Building::State::Idle});
    }
    double turret(std::size_t id,const Building& b,double fraction) const {
        if(id>=previous_.size() || id>=current_.size())return b.def_turret_facing;
        const auto& a=previous_[id];const auto& c=current_[id];
        if(!a.live || !c.live || !b.alive || b.state!=Building::State::Idle || a.type!=c.type ||
           a.house!=c.house || a.x!=c.x || a.y!=c.y || c.type!=b.type || c.house!=b.house ||
           c.x!=b.cx || c.y!=b.cy || c.facing!=b.def_turret_facing)return b.def_turret_facing;
        return std::fmod(a.facing+std::remainder(c.facing-a.facing,65536.0)*
            std::clamp(fraction,0.0,1.0)+65536.0,65536.0);
    }
};
}

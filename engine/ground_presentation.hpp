#pragma once
#include <algorithm>
#include <cmath>
#include "unit_combat.hpp"   // MovementDomain
namespace ra2engine {
// The structural deck, not the sparse artwork anchors, owns unit elevation.
template<class W,class U> bool unit_on_high_bridge(const W& w,const U& u) {
    if (u.is_naval || (u.is_aircraft && u.altitude > 0.0) || u.cargo_of >= 0) return false;
    if (!w.high_bridge_cell(u.cell_x,u.cell_y) || w.high_bridge_destroyed(u.cell_x,u.cell_y)) return false;
    if (w.terrain_armed()) return u.on_deck != 0;
    const auto& water=w.grid_for(MovementDomain::Water);
    if (u.cell_x < 0 || u.cell_y < 0 || u.cell_x >= w.grid_w() || u.cell_y >= w.grid_h()) return false;
    const auto i=static_cast<std::size_t>(u.cell_y)*w.grid_w()+u.cell_x;
    return i < water.size() && water[i] != 0;
}
// The renderer may display a unit one fractional tick behind the simulation
// cell. Resolve the deck from that displayed position so the deck plane and
// sprite cannot alternate as cell_x/cell_y advance at the simulation edge.
template<class W,class U> bool unit_on_high_bridge_at(const W& w,const U& u,double fx,double fy) {
    if (u.is_naval || (u.is_aircraft && u.altitude > 0.0) || u.cargo_of >= 0) return false;
    const int x=static_cast<int>(std::floor(fx+0.5));
    const int y=static_cast<int>(std::floor(fy+0.5));
    return w.high_bridge_cell(x,y) && !w.high_bridge_destroyed(x,y);
}
inline double deck_unit_depth(double fx,double fy,int cx,int cy,bool deck, double artwork_depth = -1.0) {
    return deck ? std::max(std::max(fx+fy,double(cx+cy))+1.001, artwork_depth+0.001) : fx+fy;
}
// Visual interpolation of legal movement only; no simulation height changes.
inline double segment_ground_level(double fx,double fy,int sx,int sy,int tx,int ty,double a,double b) {
    const double dx=tx-sx,dy=ty-sy,len=dx*dx+dy*dy;
    if(len<=0)return a;
    const double t=std::clamp(((fx-sx)*dx+(fy-sy)*dy)/len,0.0,1.0);
    return a+(b-a)*t;
}
template<class W,class U> double moving_ground_level(const W& w,const U& u) {
    const double current=w.cell_level(u.cell_x,u.cell_y)+(u.on_deck?4:0);
    if(u.is_aircraft || u.is_naval || !u.moving() || !w.terrain_armed() || u.path.size()<2)return current;
    const std::size_t first=std::max<std::size_t>(1,std::min(u.path_idx,u.path.size()-1));
    double best=0.75*0.75,result=current;
    for(std::size_t k=first;k<u.path.size() && k<=first+1;++k) {
        const auto s=u.path[k-1],t=u.path[k];
        const double dx=t.x-s.x,dy=t.y-s.y,len=dx*dx+dy*dy;
        if(len<=0 || std::abs(dx)>1 || std::abs(dy)>1)continue;
        const double f=std::clamp(((u.fx-s.x)*dx+(u.fy-s.y)*dy)/len,0.0,1.0);
        const double ex=u.fx-(s.x+f*dx),ey=u.fy-(s.y+f*dy),distance=ex*ex+ey*ey;
        if(distance>best)continue;
        auto height=[&](int x,int y){const int b=w.cell_level(x,y);return double(b+(w.deck_intact(x,y) && b+4==current ? 4 : 0));};
        const double a=height(s.x,s.y),b=height(t.x,t.y);
        if(std::abs(a-b)>1)continue; // stale cliff or broken bridge, not a ramp
        best=distance;result=a+(b-a)*f;
    }
    return result;
}
}

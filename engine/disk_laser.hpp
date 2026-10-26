#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace ra2engine {
// Vanilla DiskLaser radius, in cell space. Attachment is fixed at creation;
// this helper changes only the presentation of an already-authoritative shot.
inline std::array<double,2> disk_laser_rim(double dx,double dy) {
    const double length=std::hypot(dx,dy),radius=240.0/256.0;
    if(length<0.000001)return {radius,0};
    return {radius*dx/length,radius*dy/length};
}
// Display-only regular-ring approximation; no external authored lookup table.
// Two branches meet at the target-facing rim after eight charge emissions.
inline std::array<std::array<double,2>,4> disk_charge_segments(double dx,double dy,int phase) {
    const double pi=3.14159265358979323846, radius=240.0/256.0;
    const double start=std::atan2(dy,dx)+pi,delta=pi/8;
    auto p=[&](double i){return std::array<double,2>{radius*std::cos(start+i*delta),radius*std::sin(start+i*delta)};};
    const int s=std::max(0,std::min(7,phase));
    return {p(s),p(s+1),p(-s),p(-s-1)};
}
}

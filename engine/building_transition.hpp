#pragma once
#include <algorithm>
#include <cmath>
#include <array>
namespace ra2engine {
inline std::array<int,2> bunker_docking_cell(int cx,int cy,int fw,int fh) {
    // NATBNK's platform art is anchored at the origin, not at the southeast
    // cell of its 2x2 foundation. Its retail docking offset is (-1,-1).
    // Share this socket between approach, attachment, ejection and drawing.
    return {cx+fw/2-1,cy+fh/2-1};
}
// The path grid socket is integral; the artwork's platform centre lies half
// a cell southeast of that socket. Blend only over the last cell of travel,
// so crossing between live locomotion and an attached occupant cannot snap.
inline double bunker_platform_offset(double x,double y,int cx,int cy,int fw,int fh) {
    const auto dock=bunker_docking_cell(cx,cy,fw,fh);
    const double t=std::clamp(1.0-std::hypot(x-dock[0],y-dock[1]),0.0,1.0);
    return 0.5*t*t*(3-2*t);
}
inline bool bunker_contains_vehicle(double x,double y,int cx,int cy,int fw,int fh) {
    return x>=cx-0.5 && y>=cy-0.5 && x<cx+fw-0.5 && y<cy+fh-0.5;
}
inline bool bunker_render_passage(double x,double y,int cx,int cy,int fw,int fh) {
    // Own the entire display-offset support, including the near-side approach
    // outside the foundation. Otherwise the first inside frame jumps by the
    // already nonzero platform offset. This does not expand collision space.
    return bunker_contains_vehicle(x,y,cx,cy,fw,fh) || bunker_platform_offset(x,y,cx,cy,fw,fh)>0;
}
inline bool bunker_vehicle_in_front(double x,double y,int cx,int cy,int fw,int fh) {
    const double offset=bunker_platform_offset(x,y,cx,cy,fw,fh);
    return x+y+2*offset>cx+cy+(fw+fh-2)/2.0+0.001;
}
// Opening runs Start..LoopEnd before holding; closing uses its reverse span.
inline long bunker_motion_ticks(int start,int loop_end,int delay,bool reverse) {
    const long frames=reverse?loop_end:loop_end-start;
    return std::clamp<long>(frames*std::max(1,delay),1,900);
}
inline double transition_blend(long age,long duration,double sub_tick) {
    const double t=std::clamp((age+std::clamp(sub_tick,0.0,1.0))/std::max<long>(1,duration),0.0,1.0);
    return t*t*(3.0-2.0*t);
}
inline long bunker_drive_ticks(double dx,double dy,double cells_per_second) {
    // Drive's straight track covers 161 speed units per cell, versus 256
    // used by speed_cps. This is presentation only, not a mission timer.
    return std::clamp<long>(long(std::ceil(std::hypot(dx,dy)*15*161/
                                         (256*std::max(0.05,cells_per_second)))),1,300);
}
inline double bunker_drive_fraction(long age,long drive,long delay,double sub_tick) {
    return std::clamp((age-delay+std::clamp(sub_tick,0.0,1.0))/std::max<long>(1,drive),0.0,1.0);
}
inline double bunker_horizontal_facing(double facing) {
    // A horizontal screen axis is NE/SW in map coordinates. Choose the
    // nearer hull orientation; this never changes the turret's aim.
    auto distance=[](double a,double b){return std::abs(std::remainder(a-b,65536.0));};
    return distance(facing,8192)<=distance(facing,40960)?8192:40960;
}
inline long bunker_alignment_ticks(double from,double to,double turn_per_second) {
    return std::clamp<long>(long(std::ceil(std::abs(std::remainder(to-from,65536.0))*15/
                                         std::max(1.0,turn_per_second))),0,300);
}
inline double bunker_hull_facing(double from,double to,long age,long drive,long align,double fraction) {
    const double t=std::clamp((age-drive+fraction)/std::max<long>(1,align),0.0,1.0);
    double result=std::fmod(from+std::remainder(to-from,65536.0)*t+65536.0,65536.0);
    return result;
}
// A visual transition never changes simulation state or command timing.
inline bool transition_active(long start,long duration,long now) {
    return duration>0 && now>=start && now-start<duration;
}
inline int packing_frame(int frames,long start,long duration,long now) {
    if(frames<=0 || !transition_active(start,duration,now)) return -1;
    return frames-1-std::min<long>(frames-1,(now-start)*frames/duration);
}
// Opening uses Special/SpecialTwo, closing uses the art-defined reverse
// SpecialThree/SpecialFour; steady state uses the opening animation's loop.
inline int bunker_animation_slot(bool front,int transition) {
    return transition<0 ? (front?12:13) : (front?10:11);
}
}

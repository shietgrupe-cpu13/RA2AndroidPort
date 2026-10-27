#pragma once
#include <algorithm>
#include <cstdint>
namespace ra2engine {
// Display-only queries over the existing fuse/toggle. No timer mutation.
inline int attached_bomb_clock(std::int64_t now,std::int64_t det,int duration,int flicker) {
    if(det<0 || duration<=0 || flicker<=0)return -1;
    const auto elapsed=std::clamp<std::int64_t>(duration-(det-now),0,duration);
    const auto sixth=elapsed/std::max(1,duration/6);
    return std::min(11,int(sixth*2)+int((std::max<std::int64_t>(0,now)/flicker)%2));
}
inline int repair_wrench_frame(std::uint64_t tick,int normalized_span=14) {
    const auto cycle=std::max(2,normalized_span);
    return int(tick%cycle)*6/(cycle-1);
}
}

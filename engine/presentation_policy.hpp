#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <cstddef>
#include <string>
namespace ra2engine {
inline bool health_bar_visible(int mode,bool selected,bool damaged,bool hovered=false,bool repairing=false) {
    return mode==2 || (mode==1 && (selected || damaged || hovered || repairing));
}
class RocketLaunchTracker {
    std::set<std::size_t> launched_;
public:
    bool observe(std::size_t slot,bool active,bool flying) {
        if(!active || !flying){launched_.erase(slot);return false;}
        return launched_.insert(slot).second;
    }
};
// Presentation only: repeated speech never suppresses the command itself.
class SpeechCooldown {
    std::map<std::string,std::uint64_t> spoken_;
public:
    void clear(){spoken_.clear();}
    bool allow(const std::string& category,std::uint64_t now) {
        auto it=spoken_.find(category);
        if(it!=spoken_.end() && now>=it->second && now-it->second<3000)return false;
        spoken_[category]=now;return true;
    }
};
// Visual witness of the existing fixed-tick nuclear strike, not a new bullet.
inline double nuke_visual_height(double frame,int up_frames,int down_frames) {
    const double up=std::max(1,up_frames),down=std::max(1,down_frames);
    if(frame<up) {
        const double t=std::clamp(frame,0.0,up);
        return t*(t+1.0)*0.5; // NukeCarrier acceleration=1, initial speed=1
    }
    return 20000.0*(1.0-std::clamp((frame-up)/down,0.0,1.0));
}
}

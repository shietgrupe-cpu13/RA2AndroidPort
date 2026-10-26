#pragma once
#include <array>
#include <map>
#include <string>
namespace ra2engine {
// Independently implemented status data. Duration is weapon Damage in logic
// frames, after the existing DamageArea/Verses receiver calculation, not HP.
struct ChaosStatus { int frames=0; bool manual_hold=false; };
struct ChaosState {
    bool enabled=false;
    std::map<std::string,int> sources; // AreaFire psychedelic slots (bits 0/1), deploy permission (bit 2)
    std::map<std::string,bool> friendly;
    std::map<std::string,bool> immune;
    std::map<int,ChaosStatus> active;
    std::map<int,int> pending;
    std::array<int,3> tint{};
};
}

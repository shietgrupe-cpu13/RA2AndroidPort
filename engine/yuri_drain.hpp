#pragma once
#include <map>
#include <string>
#include <vector>
namespace ra2assets { class Rules; }
namespace ra2engine {
struct DrainType { bool enabled=false, refinery=false; int power=-1; };
struct DrainLink {
    int unit=-1, building=-1, source_house=-1, target_house=-1, frames=0;
};
// Drain shutdown is separate from manual power switches and spy blackout.
// Links and transfer clocks are deterministic, saved simulation state.
struct DrainState {
    bool enabled=false;
    int amount=30, delay=30;
    std::map<std::string,bool> sources;
    std::map<std::string,DrainType> targets;
    std::vector<DrainLink> links;
};
}

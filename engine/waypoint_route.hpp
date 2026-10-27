#pragma once
// Android fork additions by Bryle13; distributed under the project 0BSD license.
#include "pathfind.hpp"
#include <array>
#include <vector>

namespace ra2engine {
// Simulation-owned queued movement. UI planning is committed as QMove events;
// these remaining destinations survive a save and are shared by lockstep peers.
struct WaypointRoute {
    int house = -1;
    std::vector<Cellxy> points;
};
struct SpawnLaunchProfile {
    std::array<int,3> second = {0,0,0};
    bool decloak = true;
};
}

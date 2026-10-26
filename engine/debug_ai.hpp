#pragma once
#include "world.hpp"
#include <algorithm>

namespace ra2engine {
// Explicit testing mode, not an adjustment to vanilla difficulty.
inline bool debug_economy_unit(const UnitTypeDef& d) {
    return d.harvester || d.id == "AMCV" || d.id == "SMCV" ||
           d.id == "PCV" || d.id == "SMIN";
}
inline void configure_debug_ai(AiHouseConfig& c) {
    if (c.difficulty != AiDifficulty::Debug) return;
    c.catalog.defenses.clear();
    c.catalog.superweapons.clear();
    c.catalog.wall = -1;
    c.catalog.naval_yard = -1;
    c.mods.defense_target = 0;
    c.fire_superweapons = false;
    c.hate_delay_frames = -1;
    c.auto_deploy_frames = -1;
    c.roster_to_protect.clear();
    // Retain the shared factory/harvester recovery pass, without military teams.
    c.team.aimd = {};
    c.team.triggers_enabled = false;
    c.team.rules.total_team_cap = 0;
    auto& r = c.team.unit_roster;
    r.erase(std::remove_if(r.begin(), r.end(), [](const UnitTypeDef& d) {
        return !debug_economy_unit(d);
    }), r.end());
}
} // namespace ra2engine

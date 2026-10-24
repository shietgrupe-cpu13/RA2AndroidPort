#pragma once
#include "world.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
namespace ra2engine {
inline bool yuri_building_type(const std::string& id) {
    static constexpr std::array<const char*,19> types = {
        "YACNST", "YAPOWR", "YABRCK", "YAREFN", "YAWEAP", "YAYARD", "YATECH",
        "YAPSYT", "YAGGUN", "YAGNTC", "YAPPET", "YAGRND", "YAPSYB", "YAROCK",
        "YAMREF", "YAVULT", "YAMISL", "YACOMD", "YAWALL"
    };
    return std::find(types.begin(), types.end(), id) != types.end();
}
inline World::Side faction_side(const std::string& country,std::string side) {
    std::transform(side.begin(),side.end(),side.begin(),[](unsigned char c){return char(std::toupper(c));});
    if(side=="THIRDSIDE" || country=="YuriCountry") return World::Side::Yuri;
    if(side=="NOD") return World::Side::Soviet;
    return World::Side::Allied;
}
inline const char* faction_country(World::Side side) {
    return side==World::Side::Yuri ? "YuriCountry" : side==World::Side::Soviet ? "Russians" : "Americans";
}
inline const char* faction_rules_side(World::Side side) {
    return side==World::Side::Yuri ? "THIRDSIDE" : side==World::Side::Soviet ? "NOD" : "GDI";
}
}

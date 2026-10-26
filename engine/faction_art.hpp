#pragma once
#include <string>
#include <utility>
namespace ra2engine {
inline std::string loading_country_palette(const std::string& country,bool soviet) {
    static const std::pair<const char*,const char*> names[] = {
        {"Americans","mplsu.pal"},{"British","mplsuk.pal"},{"French","mplsf.pal"},
        {"Germans","mplsg.pal"},{"Alliance","mplsk.pal"},{"Koreans","mplsk.pal"},
        {"Russians","mplsr.pal"},{"Confederation","mplsc.pal"},{"Africans","mplsl.pal"},
        {"Arabs","mplsi.pal"},{"YuriCountry","mpyls.pal"}};
    for(const auto& n:names)if(country==n.first)return n.second;
    return soviet?"mplsr.pal":"mplsu.pal";
}
inline std::string loading_country_art(const std::string& country,bool soviet) {
    static const std::pair<const char*,const char*> names[] = {
        {"Americans","ustates"},{"British","ukingdom"},{"French","france"},{"Germans","germany"},
        {"Alliance","korea"},{"Koreans","korea"},{"Russians","russia"},{"Confederation","cuba"},
        {"Africans","libya"},{"Arabs","iraq"},{"YuriCountry","yuri"}};
    for(const auto& n:names)if(country==n.first)return n.second;
    return soviet?"russia":"ustates";
}
inline const char* attacked_building_eva(bool slave_manager) {
    return slave_manager?"EVA_OreMinerUnderAttack":"EVA_OurBaseIsUnderAttack";
}
}

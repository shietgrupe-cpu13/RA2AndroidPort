#pragma once
#include "ra2assets/mix.hpp"
#include "ra2assets/pal.hpp"
namespace ra2engine {
// Retail Yuri inherits Soviet chrome. This archive contains overrides (not a
// complete third sidebar); requiring top.shp/sidebar.pal rejects valid installs.
inline ra2assets::MixFile yuri_sidebar_overrides(const ra2assets::MixFile& md) {
    if (md.find(std::string("sidec02md.mix"))) return md.open_nested("sidec02md.mix");
    return md.open_nested("localmd.mix").open_nested("sidec02md.mix");
}
// RADARY's indices address RADARYURI, not the inherited Soviet SIDEBAR
// palette. Keep other chrome on its own palette; this is not a global tint.
inline ra2assets::Palette sidebar_radar_palette(const ra2assets::Palette& base,
    const ra2assets::MixFile* yuri_override) {
    if(yuri_override)if(const auto* e=yuri_override->find(std::string("radaryuri.pal"))) {
        const auto raw=yuri_override->read(*e);
        return ra2assets::Palette::load(raw.data(),raw.size());
    }
    return base;
}
}

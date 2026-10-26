#pragma once
#include "ra2assets/mix.hpp"
namespace ra2engine {
// Retail Yuri inherits Soviet chrome. This archive contains overrides (not a
// complete third sidebar); requiring top.shp/sidebar.pal rejects valid installs.
inline ra2assets::MixFile yuri_sidebar_overrides(const ra2assets::MixFile& md) {
    if (md.find(std::string("sidec02md.mix"))) return md.open_nested("sidec02md.mix");
    return md.open_nested("localmd.mix").open_nested("sidec02md.mix");
}
}

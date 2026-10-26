#pragma once
#include <algorithm>
#include "terrain_tilt.hpp"
#include "voxel_raster.hpp"
namespace ra2engine {
// Render coordinates only: the centre-top of the loaded hull, projected with
// the same slope matrix and basis as the voxel. Never changes blast recipients.
inline std::array<double,2> gas_vent_projection(double hull_top, int ramp) {
    const auto p = terrain_tilt(ramp).apply(0, 0, std::max(0.0,hull_top));
    return {(p[0]+p[1])*kIsoX, -(p[0]-p[1])*kIsoY+p[2]*kIsoZ};
}
}

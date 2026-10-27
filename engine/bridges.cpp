#include "bridges.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <cctype>

namespace ra2engine {
namespace {
// Concrete low bridges (LOBRDB01..) mirror the wood set 131 indices later
// (0x4A -> 0xCD, 0x5C -> 0xDF, 0x64 -> 0xE7 in FUN_0057C720's dispatch).
constexpr int kConcreteOffset = 0xCD - 0x4A;

// Wood sets, from FUN_0057B4D0 (0x49 < i < 0x53 || 0x5B < i < 0x60 || i == 100
// -> ColumnY; 0x52 < i < 0x5C || 0x5F < i < 0x64 || i == 0x65 -> RowX).
LowAxis wood_axis(int i) {
    if ((i > 0x49 && i < 0x53) || (i > 0x5B && i < 0x60) || i == 100) return LowAxis::ColumnY;
    if ((i > 0x52 && i < 0x5C) || (i > 0x5F && i < 0x64) || i == 0x65) return LowAxis::RowX;
    return LowAxis::None;
}
// FUN_0057B720 (ColumnY) / FUN_0057BCE0 (RowX) overlay rewrites.
int wood_next(int i) {
    switch (wood_axis(i)) {
        case LowAxis::ColumnY:
            if (i == 0x5C) return 0x5D;
            if (i == 0x5E) return 0x5F;
            if (i < 0x50) return 0x50;
            if (i <= 0x52) return 100;
            return i;
        case LowAxis::RowX:
            if (i == 0x60) return 0x61;
            if (i == 0x62) return 99;
            if (i < 0x59) return 0x59;
            if (i <= 0x5B) return 0x65;
            return i;
        default:
            return i;
    }
}
int to_wood(int i) { return (i >= 0xCD && i < 0xCD + 0x1C) ? i - kConcreteOffset : -1; }
}  // namespace

BridgeClass bridge_class_of_overlay(int idx) {
    if (idx > 0x49 && idx < 0x66) return BridgeClass::LowWood;
    if (idx > 0xCC && idx < 0xE9) return BridgeClass::LowConcrete;
    if (idx == 0x18 || idx == 0x19 || idx == 0xED || idx == 0xEE) return BridgeClass::HighHead;
    return BridgeClass::None;
}

bool is_low_bridge_overlay(int idx) {
    const BridgeClass c = bridge_class_of_overlay(idx);
    return c == BridgeClass::LowWood || c == BridgeClass::LowConcrete;
}

bool high_bridge_section_cells(int overlay, int x, int y, int out_x[4], int out_y[4]) {
    if (bridge_class_of_overlay(overlay) != BridgeClass::HighHead) return false;
    // OverlayClass::Mark (0x5FC5EE): BRIDGE1/BRIDGEB1 use direction 0
    // (north); BRIDGE2/BRIDGEB2 direction 6 (west).
    const bool north = overlay == 0x18 || overlay == 0xED;
    const int dx = north ? 0 : -1, dy = north ? -1 : 0;
    const int offsets[4] = {0, 1, 2, -1};
    for (int i = 0; i < 4; ++i) {
        out_x[i] = x + offsets[i] * dx;
        out_y[i] = y + offsets[i] * dy;
    }
    return true;
}

bool low_bridge_destroyed(int idx) {
    return idx == 100 || idx == 101 || idx == 100 + kConcreteOffset || idx == 101 + kConcreteOffset;
}

LowAxis low_bridge_axis(int idx) {
    if (bridge_class_of_overlay(idx) == BridgeClass::LowWood) return wood_axis(idx);
    const int w = to_wood(idx);
    return w >= 0 ? wood_axis(w) : LowAxis::None;
}

int low_bridge_next_stage(int idx) {
    const BridgeClass c = bridge_class_of_overlay(idx);
    if (c == BridgeClass::LowWood) return wood_next(idx);
    if (c == BridgeClass::LowConcrete) {
        const int w = to_wood(idx);
        return w >= 0 ? wood_next(w) + kConcreteOffset : idx;   // [PROBABLE: +131 mirror]
    }
    return idx;
}

namespace {
int wood_repaired(int i) {
    switch (wood_axis(i)) {
        case LowAxis::ColumnY:
            if (i == 0x5D) return 0x5C;
            if (i == 0x5F) return 0x5E;
            if ((i >= 0x50 && i <= 0x52) || i == 100) return 0x4A;
            return i;
        case LowAxis::RowX:
            if (i == 0x61) return 0x60;
            if (i == 99) return 0x62;
            if ((i >= 0x59 && i <= 0x5B) || i == 0x65) return 0x53;
            return i;
        default:
            return i;
    }
}
}  // namespace

int low_bridge_repaired(int idx) {
    const BridgeClass c = bridge_class_of_overlay(idx);
    if (c == BridgeClass::LowWood) return wood_repaired(idx);
    if (c == BridgeClass::LowConcrete) {
        const int w = to_wood(idx);
        return w >= 0 ? wood_repaired(w) + kConcreteOffset : idx;
    }
    return idx;
}

void low_bridge_section_cells(int cx, int cy, LowAxis axis, int out_x[3], int out_y[3]) {
    for (int k = 0; k < 3; ++k) {
        out_x[k] = cx + (axis == LowAxis::RowX ? k - 1 : 0);
        out_y[k] = cy + (axis == LowAxis::ColumnY ? k - 1 : 0);
    }
}

std::vector<std::string> overlay_names_in_order(
    const std::vector<std::pair<std::string, std::string>>& section) {
    std::vector<std::string> out;
    for (const auto& kv : section) {
        std::string v = kv.second;
        const std::size_t sc = v.find(';');
        if (sc != std::string::npos) v.resize(sc);
        std::size_t a = 0, b = v.size();
        while (a < b && std::isspace(static_cast<unsigned char>(v[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1]))) --b;
        v = v.substr(a, b - a);
        for (char& ch : v) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (v.empty()) continue;
        out.push_back(v);
    }
    return out;
}

}  // namespace ra2engine

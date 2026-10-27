#pragma once
// G16 bridges (G-49) -- the pure half: overlay classification and the low
// bridge section state machine recovered from gamemd.exe. World-free and
// SDL-free (unit-tested by tests/test_bridges.cpp). docs/re/world-depth.md §B.
//
// Overlay indices here are RUNTIME indices -- the order of appearance in
// rulesmd [OverlayTypes] (what [OverlayPack] stores and what the game
// compares against); the ini's own numbering skips 40/41/183 and starts at 1,
// so it is NOT the runtime index (anchors: BRIDGE1 = 0x18, LOBRDG01 = 0x4A,
// LOBRDB01 = 0xCD, BRIDGEB1 = 0xED in FUN_00489190 / FUN_00586BB0).
#include <cstdint>
#include <string>
#include <vector>

namespace ra2engine {

enum class BridgeClass : std::uint8_t { None, LowWood, LowConcrete, HighHead };

// FUN_00586BB0's dispatch: 0x49 < idx < 100 wood low (FUN_0057B4D0), 0xCC <
// idx < 0xE7 concrete low (FUN_0057C720); bridge heads 0x18/0x19 (BRIDGE1/2)
// and 0xED/0xEE (BRIDGEB1/2). The repair scan (FUN_00573070 / 0x519449)
// widens the low ranges to < 0x66 / < 0xE9 so the destroyed pieces (100/101,
// 231/232) still count as bridge cells.
BridgeClass bridge_class_of_overlay(int idx);
bool is_low_bridge_overlay(int idx);          // any low section incl. destroyed ones
bool low_bridge_destroyed(int idx);           // 100/101 (wood) or 231/232 (concrete)

// Structural cells written by the high-bridge anchor setter (gamemd 1.001
// 0x47E040 / 0x47E470). The anchor is not the whole deck: forward one/two
// and opposite one carry the same structural flag. Forward three and the
// extra direction-6 cell carry side flags only and are deliberately excluded.
// Returns false for other overlays; clipping to real map cells is the caller's job.
bool high_bridge_section_cells(int overlay, int x, int y, int out_x[4], int out_y[4]);

// The cross-section axis of a low bridge section: the set whose destroy walk
// is FUN_0057B720 spans the 3 cells (x, y-1..y+1) ("ColumnY"); the other set
// (FUN_0057BCE0) spans (x-1..x+1, y) ("RowX").
enum class LowAxis : std::uint8_t { None, ColumnY, RowX };
LowAxis low_bridge_axis(int idx);

// One destroy step of a low section's overlay (FUN_0057B720 / FUN_0057BCE0):
// intact -> damaged, damaged -> destroyed, end pieces once. Returns `idx`
// unchanged when the section cannot take another step.
int low_bridge_next_stage(int idx);
// The repaired (intact) overlay of a damaged / destroyed section: the inverse
// of low_bridge_next_stage onto the set's base intact piece (0x4A ColumnY,
// 0x53 RowX; the end pieces back to their intact end) [RE-FALLBACK: the
// restore walk FUN_0057EE70 / FUN_00573070 is not traced piece by piece].
int low_bridge_repaired(int idx);

// FUN_0057B4D0 / FUN_0057C720: the centre cell of the 3-cell cross-section
// hit at (x, y). `overlay_at(x, y)` returns the runtime overlay index (or -1).
template <class OverlayAt>
void low_bridge_section_centre(int x, int y, OverlayAt overlay_at, int& cx, int& cy) {
    const int idx = overlay_at(x, y);
    cx = x;
    cy = y;
    const LowAxis ax = low_bridge_axis(idx);
    if (ax == LowAxis::None) return;
    const int dx = ax == LowAxis::RowX ? 1 : 0, dy = ax == LowAxis::ColumnY ? 1 : 0;
    if (!is_low_bridge_overlay(overlay_at(x - dx, y - dy))) { cx = x + dx; cy = y + dy; return; }
    if (is_low_bridge_overlay(overlay_at(x - 2 * dx, y - 2 * dy))) { cx = x - dx; cy = y - dy; return; }
}

// The 3 cells of the cross-section centred on (cx, cy) for `axis`.
void low_bridge_section_cells(int cx, int cy, LowAxis axis, int out_x[3], int out_y[3]);

// [OverlayTypes] names in runtime-index order (the order of appearance).
// `section` = the parsed key/value list, in file order.
std::vector<std::string> overlay_names_in_order(
    const std::vector<std::pair<std::string, std::string>>& section);

}  // namespace ra2engine

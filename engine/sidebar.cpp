#include "sidebar.hpp"

#include <algorithm>
#include <cmath>

namespace ra2engine {

namespace { int g_sidebar_style = 0, g_sidebar_scale = 100, g_sidebar_cells = 0, g_sidebar_reference_height = 0; }
void set_sidebar_reference_height(int height) { g_sidebar_reference_height=std::max(0,height); }
void set_sidebar_preferences(int scale, int cells) {
    g_sidebar_scale = std::clamp(scale,60,125);
    g_sidebar_cells = cells <= 0 ? 0 : std::clamp(cells/2*2,6,60);
}
int sidebar_scale() { return g_sidebar_scale; }
int sidebar_cells() { return g_sidebar_cells; }
void SidebarLayout::scale_rects(float f) {
    auto rect=[&](SbRect& r) { r={int(std::lround(r.x*f)),int(std::lround(r.y*f)),int(std::lround(r.w*f)),int(std::lround(r.h*f))}; };
    rect(bar);rect(credits);rect(top_cap);rect(radar);rect(side1);
    for(auto& r:tabs)rect(r);
    rect(power);rect(scroll_dn);rect(scroll_up);rect(repair_btn);rect(sell_btn);rect(opt_btn);rect(diplo_btn);
    for(auto& c:cameos)rect(c.rect);
    strip_top=int(std::lround(strip_top*f));
}
SidebarLayout SidebarLayout::unscaled() const {
    SidebarLayout n=*this;n.scale_rects(1.0f/scale);n.scale=1.0f;return n;
}
void set_sidebar_style(int style) { g_sidebar_style = style == 1 || style == 2 ? style : 0; }
int sidebar_style() { return g_sidebar_style; }

SidebarLayout SidebarLayout::compute(int view_w, int view_h,
                                     const std::vector<int>& types, int top_row) {
    SidebarLayout lay;
    lay.scale=g_sidebar_scale/100.0f;
    // Phone chrome uses screen-relative sizing, independent of the world resolution.
    if(g_sidebar_reference_height>0)lay.scale*=float(view_h)/g_sidebar_reference_height;
    view_w=int(std::ceil(view_w/lay.scale));
    view_h=int(std::ceil(view_h/lay.scale));
    const bool sov = g_sidebar_style != 0;   // Yuri shares the Soviet geometry (construction.md)
    const int side3_h = sov ? 18 : kSide3H;

    // Whole sidebar column on the right edge, full window height
    // [RE: construction.md §11a — x0 = screen_w − 168, y0 = 0].
    lay.bar = {view_w - kSidebarW, 0, kSidebarW, view_h};
    const int x0 = lay.bar.x;

    // Fixed top stack: credits(0,0) -> top cap(0,16) -> radar(0,48) ->
    // side1(0,158). 158 is the binary's literal 0x9E = 16 + 32 + 110.
    lay.credits = {x0, 0, kSidebarW, kCreditsH};
    lay.top_cap = {x0, kTopCapY, kSidebarW, kRadarY - kTopCapY};
    lay.radar = {x0, kRadarY, kSidebarW, kRadarH};
    lay.side1 = {x0, kSide1Y, kSidebarW, kSide1H};
    // Repair / Sell buttons sit ON the side1 band [RE: construction.md §11a --
    // repair at bar-relative 20, sell at 84, both y 166, 64x31].
    lay.repair_btn = {x0 + (sov ? 33 : kRepairBtnX), sov ? 165 : kSideBtnY, kSideBtnW, kSideBtnH};
    lay.sell_btn = {x0 + (sov ? 85 : kSellBtnX), sov ? 165 : kSideBtnY, kSideBtnW, kSideBtnH};
    lay.opt_btn = {x0 + (sov ? 14 : 11), sov ? 21 : 20, 72, 18};
    lay.diplo_btn = {x0 + (sov ? 86 : 83), sov ? 21 : 20, 72, 18};

    // Tab row: four separate 28x27 buttons at x = 26 + 29*i, y = 197
    // [RE: §11a — pitch 29, NOT bar.w/4].
    for (int i = 0; i < 4; ++i)
        lay.tabs[i] = {x0 + (sov ? 20 + 32 * i : kTabX0 + kTabPitch * i), kTabY, kTabW, kTabH};

    // Visible cameo rows: N = floor((view_h - 234 - side3_h)/50), min 1
    // [RE: §11a] — 6 at 600 px, 9 at 720 px. side2 tiles once per row.
    lay.side2_rows = std::max(1, (view_h - 234 - side3_h) / kCameoRowPitch);
    if(g_sidebar_cells)lay.side2_rows=std::min(lay.side2_rows,g_sidebar_cells/2);
    const int strip_h = lay.side2_rows * kSide2H;

    // Power pip column hugs the strip's left edge from the strip top;
    // capacity = (strip_h + 3)/3 pips at 3 px pitch [RE: §11a/§8].
    lay.power_pips = (strip_h + kPowerPitch) / kPowerPitch;
    lay.power = {x0 + (sov ? 0 : kPowerX), kStripTop, kPowerPipW, lay.power_pips * kPowerPitch};

    // Strip scroll buttons: r-dn left, r-up right, on the row 7 px below the
    // strip (inert this phase; rects reserved for hit-test completeness).
    const int scroll_y = kStripTop + strip_h + 7;
    lay.scroll_dn = {x0 + kScrollX, scroll_y, kScrollW, kScrollH};
    lay.scroll_up = {x0 + kScrollX + (sov ? 45 : kScrollW), scroll_y, kScrollW, kScrollH};
    if(g_sidebar_cells && scroll_y+kScrollH<view_h) {
        lay.compact=true;
        lay.bar.h=scroll_y+kScrollH;
    }

    // Cameo grid: origin (22, strip_top + 1), column pitch 63, row pitch 50,
    // 2 columns row-major — these land the 60x48 cameos INSIDE the socket
    // recesses baked into side2.shp [RE: §11a]. Cells beyond the visible
    // 2*N are not laid out (no strip scrolling with this roster).
    // G-12: the strip starts at row top_row (clamped to the scrollable range).
    lay.top_row = std::max(0, std::min(top_row, lay.max_top_row(static_cast<int>(types.size()))));
    const std::size_t first = static_cast<std::size_t>(2 * lay.top_row);
    const std::size_t max_cells = static_cast<std::size_t>(2 * lay.side2_rows);
    for (std::size_t i = 0; first + i < types.size() && i < max_cells; ++i) {
        const int col = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        CameoCell cell;
        cell.type = types[first + i];
        cell.rect = {x0 + kCameoX + col * kCameoColPitch,
                     kStripTop + 1 + row * kCameoRowPitch, kCameoW, kCameoH};
        lay.cameos.push_back(cell);
    }

    lay.scale_rects(lay.scale);
    return lay;
}

int SidebarLayout::cameo_at(int px, int py) const {
    for (std::size_t i = 0; i < cameos.size(); ++i) {
        if (cameos[i].rect.contains(px, py)) return static_cast<int>(i);
    }
    return -1;
}

int SidebarLayout::tab_at(int px, int py) const {
    for (int i = 0; i < 4; ++i) {
        if (tabs[i].contains(px, py)) return i;
    }
    return -1;
}

}  // namespace ra2engine

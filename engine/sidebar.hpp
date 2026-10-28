#pragma once
// Pure RA2 sidebar geometry + interaction states. Every pixel metric is a
// literal recovered from gamemd.exe's SidebarClass/RadarClass/TabClass/
// PowerClass geometry writers — docs/re/construction.md §11a (second hunt,
// High confidence; supersedes the pre-hunt art-derived guesses). Allied
// (sidec01) style constants; Soviet/Yuri variants are recorded in §11a but
// not implemented (the player is Allied this phase).
#include <string>
#include <vector>

namespace ra2engine {

struct SbRect { int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

// Recovered Allied layout constants [RE: construction.md §11a]. All x values
// are bar-relative (add bar.x = view_w - kSidebarW); y from the window top.
constexpr int kSidebarW = 168;
constexpr int kCreditsH = 16;        // credits.shp at (0, 0)
constexpr int kTopCapY = 16;         // top.shp 168x32 bevel cap
constexpr int kRadarY = 48;          // radar.shp 168x110 (frame 0 = eagle logo)
constexpr int kRadarH = 110;
constexpr int kSide1Y = 158;         // binary literal 0x9E = 16 + 32 + 110
constexpr int kSide1H = 69;
constexpr int kTabY = 197;           // four 28x27 tabs
constexpr int kTabX0 = 26;
constexpr int kTabPitch = 29;
constexpr int kTabW = 28, kTabH = 27;
constexpr int kStripTop = 227;       // = kSide1Y + kSide1H; side2 rows + pips + cameos
constexpr int kSide2H = 50;          // one cameo row per side2 tile
constexpr int kSide3H = 26;          // bottom cap
constexpr int kCameoX = 22;          // grid origin (22, kStripTop + 1)
constexpr int kCameoColPitch = 63;
constexpr int kCameoRowPitch = 50;
constexpr int kCameoW = 60, kCameoH = 48;
constexpr int kScrollX = 39;         // r-dn at 39, r-up at 39+46
constexpr int kScrollW = 46, kScrollH = 25;
constexpr int kPowerX = 5;           // powerp.shp 12x2 pips, 3 px pitch
constexpr int kPowerPipW = 12, kPowerPipH = 2, kPowerPitch = 3;
// Repair / Sell buttons [RE: construction.md §11a, Allied/sidec01]: repair.shp
// (2 frames) at bar-relative (20, 166) 64x31, sell.shp (2 frames) at (84, 166)
// 64x31. Frame 1 is the PRESSED state, drawn while the mode is armed. These are
// exactly the coordinates draw_sidebar already blitted at, so the rects provably
// sit under the drawn art. (Soviet/Yuri offsets 33/85 at y 165 are recorded in
// §11a but not implemented -- the player sidebar is Allied.)
constexpr int kRepairBtnX = 20;
constexpr int kSellBtnX = 84;
constexpr int kSideBtnY = 166;
constexpr int kSideBtnW = 64, kSideBtnH = 31;

enum class CameoState { Available, Disabled, Building, Ready };

// W2-UI-08 [RE: construction.md §11a, FUN_006A4720 / FUN_006A47C0 -- side
// style session+0x34B8: 0 = Allied (sidec01), 1 = Soviet (sidec02; Yuri
// shares it)]. The Soviet set: repair x 33 / sell x 85 at y 165, tabs at
// 20 + 32*i, power pips at x 0, scroll-up at 39+45, side3 18 px tall,
// options (14, 21) / diplomacy (86, 21). Default 0, so every gate / test
// layout is the Allied one; GameScene sets it from the player's side.
void set_sidebar_reference_height(int height);
void set_sidebar_preferences(int scale, int cells);
int sidebar_scale();
int sidebar_cells();
void set_sidebar_style(int style);
int sidebar_style();

// One sidebar cameo cell: which building type it shows + its rect.
struct CameoCell { int type = -1; SbRect rect; };

struct SidebarLayout {
    float scale = 1.0f;
    bool compact = false;
    int battlefield_width(int width,bool visible) const { return width-(visible && !compact ? bar.w : 0); }
    SidebarLayout unscaled() const;
    void scale_rects(float factor);
    SbRect bar;              // whole sidebar column (right edge, full height)
    SbRect credits;          // credits.shp backdrop (tally centered at x+84)
    SbRect top_cap;          // top.shp bevel cap
    SbRect radar;            // radar.shp area (frame 0 pre-radar logo)
    SbRect side1;            // side1.shp band (repair/sell chrome)
    SbRect tabs[4];          // Building, Defense, Infantry, Vehicle
    SbRect power;            // pip column: x=+5, y=kStripTop, w=12, h=pips*3
    SbRect scroll_dn, scroll_up;   // strip scroll buttons (inert this phase)
    // Repair / Sell buttons [RE: construction.md §11a; behaviour
    // repair-sell.md §C1 -- both TOGGLE mutually-exclusive cursor modes].
    SbRect repair_btn, sell_btn;
    SbRect opt_btn, diplo_btn;   // W2-UI-08: options / diplomacy over the top cap
    int strip_top = kStripTop;
    int side2_rows = 0;      // N: side2 tiles / visible cameo rows
    int power_pips = 0;      // capacity = (50*N + 3) / 3
    std::vector<CameoCell> cameos;   // 2-column grid, row-major

    // view_w/view_h = window size; `types` = roster indices to show, already
    // filtered (cameo != "" AND tech_level > 0) and ordered by tech_level
    // then roster order. Row count N = floor((view_h - 234 - side3_h)/50),
    // min 1 [RE: construction.md §11a]; only the 2*N cells from top_row are
    // laid out (G-12 strip scrolling).
    // G-12: top_row scrolls the strip (StripClass TopRowIndex): cells start at
    // types[2*top_row]; the caller clamps it with max_top_row().
    static SidebarLayout compute(int view_w, int view_h,
                                 const std::vector<int>& types, int top_row = 0);
    // Largest useful top_row for count cameos (0 when they all fit).
    int max_top_row(int count) const {
        const int rows = (count + 1) / 2;
        return rows > side2_rows ? rows - side2_rows : 0;
    }
    int top_row = 0;

    int cameo_at(int px, int py) const;   // index into cameos, or -1
    int tab_at(int px, int py) const;     // 0..3, or -1
    bool repair_btn_at(int px, int py) const { return repair_btn.contains(px, py); }
    bool sell_btn_at(int px, int py) const { return sell_btn.contains(px, py); }
    bool contains(int px, int py) const { return bar.contains(px, py); }
};

}  // namespace ra2engine

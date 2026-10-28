#include "doctest.h"
#include "../engine/sidebar.hpp"
using namespace ra2engine;

// Every pin below is a literal recovered from gamemd.exe's sidebar geometry
// writers — docs/re/construction.md §11a (Allied/sidec01 style).

TEST_CASE("sidebar layout: recovered composition at 1280x720") {
    auto lay = SidebarLayout::compute(1280, 720, {1, 2, 3});
    const int x0 = 1280 - 168;
    CHECK(lay.bar.x == x0);
    CHECK(lay.bar.w == 168);
    CHECK(lay.bar.h == 720);

    // Top stack: credits(0,0,168x16) -> top cap(0,16,168x32) ->
    // radar(0,48,168x110) -> side1(0,158,168x69). 158 = 16+32+110 = 0x9E.
    CHECK(lay.credits.y == 0);
    CHECK(lay.credits.h == 16);
    CHECK(lay.top_cap.y == 16);
    CHECK(lay.top_cap.h == 32);
    CHECK(lay.radar.y == 48);
    CHECK(lay.radar.h == 110);
    CHECK(lay.side1.y == 158);
    CHECK(lay.side1.h == 69);

    // Tabs: x = 26 + 29*i (pitch 29, NOT bar.w/4), y = 197, 28x27.
    CHECK(lay.tabs[0].x == x0 + 26);
    CHECK(lay.tabs[1].x == x0 + 26 + 29);
    CHECK(lay.tabs[3].x == x0 + 26 + 87);
    CHECK(lay.tabs[0].y == 197);
    CHECK(lay.tabs[0].w == 28);
    CHECK(lay.tabs[0].h == 27);

    // Strip: N = floor((720 - 234 - 26)/50) = 9 rows; strip top 227.
    CHECK(lay.strip_top == 227);
    CHECK(lay.side2_rows == 9);

    // Cameo grid: origin (22, 228), col pitch 63, row pitch 50, 60x48.
    REQUIRE(lay.cameos.size() == 3);
    CHECK(lay.cameos[0].type == 1);
    CHECK(lay.cameos[0].rect.x == x0 + 22);
    CHECK(lay.cameos[0].rect.y == 228);
    CHECK(lay.cameos[0].rect.w == 60);
    CHECK(lay.cameos[0].rect.h == 48);
    CHECK(lay.cameos[1].rect.x == x0 + 22 + 63);
    CHECK(lay.cameos[1].rect.y == 228);
    CHECK(lay.cameos[2].rect.x == x0 + 22);
    CHECK(lay.cameos[2].rect.y == 228 + 50);

    // Power pips: x = +5, from the strip top, capacity (450+3)/3 = 151.
    CHECK(lay.power.x == x0 + 5);
    CHECK(lay.power.y == 227);
    CHECK(lay.power_pips == 151);
    CHECK(lay.power.h == 151 * 3);

    // Scroll buttons: 7 px below the strip, r-dn left / r-up right, 46x25.
    CHECK(lay.scroll_dn.x == x0 + 39);
    CHECK(lay.scroll_dn.y == 227 + 450 + 7);
    CHECK(lay.scroll_up.x == x0 + 39 + 46);
    CHECK(lay.scroll_up.y == lay.scroll_dn.y);
    CHECK(lay.scroll_dn.w == 46);
    CHECK(lay.scroll_dn.h == 25);
}

TEST_CASE("sidebar layout: 800x600 reproduces the original's 6 rows") {
    // The binary's native case: N = floor((600 - 234 - 26)/50) = 6 rows,
    // 101 power pips [RE: construction.md §11a].
    auto lay = SidebarLayout::compute(800, 600, {1});
    CHECK(lay.bar.x == 800 - 168);
    CHECK(lay.side2_rows == 6);
    CHECK(lay.power_pips == 101);
    CHECK(lay.scroll_dn.y == 227 + 300 + 7);
}

TEST_CASE("sidebar layout: cameo cells clamp to the visible strip") {
    // 2 columns x N rows visible; extra types are not laid out (no strip
    // scrolling this phase). At 600 px: 12 cells max.
    std::vector<int> many;
    for (int i = 0; i < 20; ++i) many.push_back(i);
    auto lay = SidebarLayout::compute(800, 600, many);
    CHECK(lay.cameos.size() == 12);
}

TEST_CASE("sidebar hit-testing") {
    auto lay = SidebarLayout::compute(1280, 720, {1, 2});
    auto c0 = lay.cameos[0].rect;
    CHECK(lay.cameo_at(c0.x + 1, c0.y + 1) == 0);
    CHECK(lay.cameo_at(c0.x - 200, c0.y) == -1);         // in the world, not the bar
    CHECK(lay.tab_at(lay.tabs[2].x + 1, lay.tabs[2].y + 1) == 2);
    CHECK(lay.tab_at(0, 0) == -1);
    CHECK(lay.contains(1280 - 1, 0));
    CHECK_FALSE(lay.contains(1280 - 168 - 1, 0));
}

TEST_CASE("sidebar: repair/sell button rects [RE: construction.md §11a]") {
    auto lay = SidebarLayout::compute(1280, 720, {1, 2, 3});
    const int x0 = 1280 - 168;
    // The EXACT coordinates draw_sidebar blitted at before this phase
    // (lay.bar.x + 20, 166) and (lay.bar.x + 84, 166), 64x31, on the side1 band.
    CHECK(lay.repair_btn.x == x0 + 20);
    CHECK(lay.repair_btn.y == 166);
    CHECK(lay.repair_btn.w == 64);
    CHECK(lay.repair_btn.h == 31);
    CHECK(lay.sell_btn.x == x0 + 84);
    CHECK(lay.sell_btn.y == 166);
    CHECK(lay.sell_btn.w == 64);
    CHECK(lay.sell_btn.h == 31);
    // Both sit inside the side1 band and never overlap each other or the tabs.
    CHECK(lay.repair_btn.y >= lay.side1.y);
    CHECK(lay.repair_btn.y + lay.repair_btn.h <= lay.side1.y + lay.side1.h);
    CHECK(lay.repair_btn.y + lay.repair_btn.h <= lay.tabs[0].y);
    CHECK(lay.repair_btn.x + lay.repair_btn.w <= lay.sell_btn.x);
    CHECK(lay.repair_btn_at(x0 + 21, 167));
    CHECK_FALSE(lay.repair_btn_at(x0 + 21, 165));
    CHECK(lay.sell_btn_at(x0 + 85, 167));
    CHECK_FALSE(lay.sell_btn_at(x0 + 21, 167));       // repair rect, not sell
    CHECK_FALSE(lay.sell_btn_at(0, 0));
    CHECK(lay.tab_at(x0 + 21, 167) == -1);             // no tab under the buttons
    // 800x600 (the binary's native size) puts them at the same absolute y.
    auto s = SidebarLayout::compute(800, 600, {1});
    CHECK(s.repair_btn.x == 800 - 168 + 20);
    CHECK(s.repair_btn.y == 166);
}

TEST_CASE("sidebar layout: G-12 strip scrolling by top_row") {
    std::vector<int> types;
    for (int i = 0; i < 25; ++i) types.push_back(100 + i);   // 13 rows > 9 visible
    auto lay = SidebarLayout::compute(1280, 720, types);
    CHECK(lay.top_row == 0);
    CHECK(lay.cameos.size() == 18);
    CHECK(lay.max_top_row(25) == 4);
    auto s = SidebarLayout::compute(1280, 720, types, 2);
    CHECK(s.top_row == 2);
    REQUIRE(s.cameos.size() == 18);
    CHECK(s.cameos[0].type == 104);
    CHECK(s.cameos[0].rect.y == lay.cameos[0].rect.y);   // same socket, shifted content
    auto c = SidebarLayout::compute(1280, 720, types, 99);  // clamped
    CHECK(c.top_row == 4);
    CHECK(c.cameos.size() == 17);
    CHECK(c.cameos.back().type == 124);
    auto few = SidebarLayout::compute(1280, 720, {1, 2, 3}, 3);
    CHECK(few.top_row == 0);
}

TEST_CASE("sidebar layout: Yuri shares Soviet controls and preserves its style id") {
    using namespace ra2engine;
    set_sidebar_style(1);const auto soviet=SidebarLayout::compute(1280,720,{1,2,3});
    set_sidebar_style(2);const auto yuri=SidebarLayout::compute(1280,720,{1,2,3});
    CHECK(sidebar_style()==2);
    CHECK(yuri.repair_btn.x==soviet.repair_btn.x);CHECK(yuri.repair_btn.y==soviet.repair_btn.y);
    CHECK(yuri.sell_btn.x==soviet.sell_btn.x);CHECK(yuri.opt_btn.x==soviet.opt_btn.x);
    CHECK(yuri.diplo_btn.x==soviet.diplo_btn.x);CHECK(yuri.cameos.size()==soviet.cameos.size());
    set_sidebar_style(0);
}

TEST_CASE("sidebar: compact chrome retains aligned hit targets and even strip limits") {
    struct Reset { ~Reset(){set_sidebar_preferences(100,0);} } reset;
    std::vector<int> types;for(int i=0;i<80;++i)types.push_back(i);
    for(int size : {60,75,90,100,125})for(int count : {6,8,10,12,30,60}) {
        set_sidebar_preferences(size,count);
        auto lay=SidebarLayout::compute(1280,720,types,100);
        CHECK(lay.bar.w==doctest::Approx(168*size/100.0).epsilon(0.01));
        CHECK(lay.side2_rows*2<=count);CHECK(lay.cameos.size()==2*lay.side2_rows);
        CHECK(lay.top_row==lay.max_top_row(80));
        for(int i=0;i<int(lay.cameos.size());++i) {
            const auto& r=lay.cameos[i].rect;
            CHECK(lay.cameo_at(r.x+r.w/2,r.y+r.h/2)==i);
            CHECK(lay.contains(r.x+r.w/2,r.y+r.h/2));
            CHECK(r.y+r.h<=720);
        }
        for(int i=0;i<4;++i)CHECK(lay.tab_at(lay.tabs[i].x+2,lay.tabs[i].y+2)==i);
        auto n=lay.unscaled();CHECK(n.scale==1);CHECK(n.cameos[0].rect.w==60);
    }
    set_sidebar_preferences(1,9);CHECK(sidebar_scale()==60);CHECK(sidebar_cells()==8);
}

TEST_CASE("sidebar: phone cameo size and count stay independent of resolution override") {
    struct Reset { ~Reset(){set_sidebar_reference_height(0);set_sidebar_preferences(100,0);} } reset;
    set_sidebar_reference_height(480);set_sidebar_preferences(75,6);
    std::vector<int> types;for(int i=0;i<40;++i)types.push_back(i);
    const auto small=SidebarLayout::compute(1280,720,types);
    const auto large=SidebarLayout::compute(1920,1080,types);
    CHECK(small.cameos.size()==6);CHECK(large.cameos.size()==6);
    CHECK(double(small.cameos[0].rect.h)/720==doctest::Approx(double(large.cameos[0].rect.h)/1080).epsilon(0.02));
    CHECK(double(small.bar.w)/720==doctest::Approx(double(large.bar.w)/1080).epsilon(0.02));
    for(const auto* lay:{&small,&large}) {
        CHECK(lay->compact);CHECK(lay->bar.h==lay->scroll_dn.y+lay->scroll_dn.h);
        CHECK(lay->battlefield_width(1920,true)==1920);
        CHECK_FALSE(lay->contains(lay->bar.x+2,lay->bar.h+2)); // No dead black column below.
        CHECK(lay->contains(lay->cameos[0].rect.x+2,lay->cameos[0].rect.y+2));
    }
}

#include "doctest.h"
#include "touch_bar_layout.hpp"

TEST_CASE("unit panel fits landscape viewports with separate header and group row") {
    using L = ra2engine::TouchBarLayout;
    for (int width : {320,480,640,800,1280}) for (int h : {520,548,720})
    for (int count : {13,16,17,18}) {
        const int top = L::top(h,count);
        CHECK(top >= 0);
        for (int i=0;i<count;++i) {
            const auto a=L::button(width,h,count,i);
            CHECK(a.x >= L::margin);
            CHECK(a.x+a.w <= width-L::margin);
            CHECK(a.y >= top+L::header_height);
            CHECK(a.y+a.h <= h-L::margin);
            CHECK(a.w >= 48);
            CHECK(a.h >= 56);
            for (int j=0;j<i;++j) {
                const auto b=L::button(width,h,count,j);
                CHECK_FALSE((a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h));
            }
        }
        CHECK(L::button(width,h,count,0).x == L::margin);
        if(count==13) {
            CHECK(L::button(width,h,count,8).y+56 < L::button(width,h,count,0).y);
            CHECK(L::button(width,h,count,12).x < 308);
        }
    }
}

TEST_CASE("panel arrow fits both states and clears the expanded action buttons") {
    using L=ra2engine::TouchBarLayout;
    for(int width : {320,480,800,1280}) for(int count : {13,17}) {
        const auto folded=L::toggle(width,548,count,true);
        CHECK(folded.x==L::margin);
        CHECK(folded.y+folded.h==548-L::margin);
        CHECK(folded.w==56);
        CHECK(folded.h==56);
        const auto open=L::toggle(width,548,count,false);
        CHECK(open.y>=L::top(548,count));
        CHECK(open.x+open.w<=width-L::margin);
        CHECK(open.y+open.h<=L::top(548,count)+L::header_height);
        for(int i=0;i<count;++i) CHECK(open.y+open.h<=L::button(width,548,count,i).y);
    }
}

TEST_CASE("compact instruction card clears the panel and captures cancel safely") {
    using L=ra2engine::TouchBarLayout;using C=ra2engine::TouchBannerCapture;
    for(int width : {320,480,800,1280}) for(bool folded : {false,true}) {
        const int top=folded ? L::toggle(width,548,13,true).y : L::top(548,13);
        const auto b=L::banner(width,top);
        CHECK(b.w<=300);CHECK(b.x+b.w<=width-L::margin);CHECK(b.y+b.h<top);
        const L::Rect x{b.x+b.w-44,b.y+2,44,44}; C c;
        CHECK(c.route(C::Down,x.x+5,x.y+5,0,0,true,b,x)==C::Consume);
        CHECK(c.route(C::Up,x.x+5,x.y+5,0,0,true,b,x)==C::Cancel);
        CHECK(c.route(C::Up,x.x+5,x.y+5,0,0,true,b,x)==C::Pass);
        CHECK(c.route(C::Down,b.x+5,b.y+5,0,0,true,b,x)==C::Consume);
        CHECK(c.route(C::Up,x.x+5,x.y+5,0,0,true,b,x)==C::Consume);
        CHECK(c.route(C::Down,x.x+5,x.y+5,0,0,true,b,x)==C::Consume);
        CHECK(c.route(C::Move,700,10,0,0,true,b,x)==C::Consume);
        CHECK(c.route(C::Up,700,10,0,0,true,b,x)==C::Consume);
        CHECK(c.route(C::Pan,700,10,b.x+5,b.y+5,true,b,x)==C::Consume);
        CHECK(c.route(C::Down,x.x+5,x.y+5,0,0,false,b,x)==C::Pass);
    }
}

TEST_CASE("touch layout: density scaling preserves a 48dp target across live canvas sizes") {
    using L=ra2engine::TouchBarLayout;
    for(int w:{1280,2400}) {
        const int h=w==1280?720:1080;
        L::canvas_density(3.0f,2400,1080,w,h);
        const auto r=L::button(w,h,13,0);
        const float pixel_scale=std::min(2400.0f/w,1080.0f/h);
        CHECK(r.h*pixel_scale>=143.0f);
    }
    L::canvas_density(0,0,0,0,0);
    CHECK(L::scale_factor()==1.0f);
}

#include "../engine/presentation_policy.hpp"
TEST_CASE("native health bars: Off, selected/damaged and Always apply consistently") {
    using ra2engine::health_bar_visible;
    for(bool selected:{false,true})for(bool damaged:{false,true})for(bool hovered:{false,true})for(bool repair:{false,true}) {
        CHECK_FALSE(health_bar_visible(0,selected,damaged,hovered,repair));
        CHECK(health_bar_visible(2,selected,damaged,hovered,repair));
        CHECK(health_bar_visible(1,selected,damaged,hovered,repair)==(selected||damaged||hovered||repair));
    }
}
TEST_CASE("free touch HUD: compact button geometry is screen-wide and independent of world resolution") {
    using L=ra2engine::TouchBarLayout;
    L::canvas_density(3,2400,1080,1280,720);
    const auto r=L::floating(1280,720,100,100,60);
    CHECK(r.x+r.w==1280);CHECK(r.y+r.h==720);
    CHECK(r.h*1.5==doctest::Approx(36*3*.6).epsilon(0.02));
    L::canvas_density(3,2400,1080,1920,1080);
    const auto large=L::floating(1920,1080,100,100,60);
    CHECK(large.x+large.w==1920);CHECK(large.y+large.h==1080);
    CHECK(large.h==doctest::Approx(r.h*1.5).epsilon(0.02));
    L::canvas_density(0,0,0,0,0);
}

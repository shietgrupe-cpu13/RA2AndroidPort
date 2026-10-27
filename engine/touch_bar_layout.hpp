#pragma once
#include <algorithm>
#include <cstdlib>

namespace ra2engine {
struct TouchBarLayout {
    struct Rect { int x, y, w, h; };
    static constexpr int columns = 4, height = 56, gap = 4, margin = 8;
    static constexpr int panel_width = 300, header_height = 64;

    static float scale_factor() {
        if (const char* v = std::getenv("RA2_TOUCH_SCALE")) {
            int s = std::atoi(v);
            if (s >= 70 && s <= 160) return static_cast<float>(s) / 100.0f;
        }
        return 1.0f;
    }
    static int scaled(int val) {
        return static_cast<int>(static_cast<float>(val) * scale_factor());
    }
    static int top(int viewport_height, int count) {
        const int m = scaled(margin);
        const int g = scaled(gap);
        const int h = scaled(height);
        const int hh = scaled(header_height);
        const int row_h = h + g;
        const int content = count == 13 ? (h * 3 + g * 2) : (((count + 3) / 4) * row_h - g);
        return viewport_height - m - content - hh;
    }
    static Rect toggle(int viewport_width, int viewport_height, int count, bool collapsed) {
        const int m = scaled(margin);
        const int pw = std::min(scaled(panel_width), viewport_width - 2 * m);
        const int sz = scaled(56);
        if (collapsed) return {m, viewport_height - m - sz, sz, sz};
        return {m + pw - sz, top(viewport_height, count) + scaled(4), sz, sz};
    }
    static Rect banner(int viewport_width, int panel_top) {
        const int m = scaled(margin);
        const int pw = std::min(scaled(panel_width), viewport_width - 2 * m);
        return {m, panel_top - scaled(52), pw, scaled(48)};
    }
    static Rect button(int viewport_width, int viewport_height, int count, int index) {
        const int m = scaled(margin);
        const int g = scaled(gap);
        const int h = scaled(height);
        const int pw = std::min(scaled(panel_width), viewport_width - 2 * m);
        if (count == 13 && index >= 8) {
            const int w = (pw - 4 * g) / 5;
            return {m + (index - 8) * (w + g), viewport_height - m - scaled(176), w, h};
        }
        const int rows = count == 13 ? 2 : (count + 3) / 4;
        const int w = (pw - 3 * g) / 4;
        const int row_h = h + g;
        return {m + (index % 4) * (w + g),
                viewport_height - m - (rows * row_h - g) + (index / 4) * row_h, w, h};
    }
};

// Capture complete instruction-card gestures before placement/map input.
struct TouchBannerCapture {
    enum Phase { Down, Up, Move, Pan, Other };
    enum Result { Pass, Consume, Cancel };
    bool held=false, cancel_held=false;
    static bool contains(const TouchBarLayout::Rect& r,int x,int y) {
        return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
    }
    Result route(Phase phase,int x,int y,int x0,int y0,bool visible,
                 TouchBarLayout::Rect banner,TouchBarLayout::Rect cancel) {
        if(phase==Pan) return visible && contains(banner,x0,y0) ? Consume : Pass;
        if(phase==Down) {
            held=visible && contains(banner,x,y);
            cancel_held=held && contains(cancel,x,y);
            return held ? Consume : Pass;
        }
        if(phase==Up) {
            const bool was_held=held;
            const bool fire=held && cancel_held && visible && contains(cancel,x,y);
            held=cancel_held=false;
            return fire ? Cancel : was_held ? Consume : Pass;
        }
        return phase==Move && held ? Consume : Pass;
    }
};
}

#include "iso_renderer.hpp"

#include <iostream>

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <utility>

#include "iso_project.hpp"
#include "viewport_bounds.hpp"
#include "terrain_blit.hpp"
#include "diagnostics.hpp"
#include "texture_cache.hpp"

namespace ra2engine {

// RA2 temperate tile geometry (see iso_project.hpp / TmpFile). tile_w/tile_h
// are the diamond bounding rect; level_px is the on-screen rise per height
// step.
static constexpr int kTileW = 60;
static constexpr int kTileH = 30;
static constexpr int kLevelPx = 15;

Theater::Theater(ra2assets::MixFile isotile_mix, const std::string& control_ini_text,
                 std::string ext)
    : mix_(std::move(isotile_mix)), ext_(std::move(ext)) {
    ra2assets::Ini ini = ra2assets::Ini::parse(control_ini_text);
    // Walk [TileSet0000], [TileSet0001], ... in order; assign a running global
    // index to each template 1..TilesInSet. Sets with TilesInSet=0 contribute
    // nothing (matches OpenRA/XCC). Stop at the first missing section.
    for (int set_idx = 0;; ++set_idx) {
        char sec[32];
        std::snprintf(sec, sizeof sec, "TileSet%04d", set_idx);
        if (!ini.has(sec)) break;
        const int tiles = ini.get_int(sec, "TilesInSet", 1);
        const std::string file = ini.get(sec, "FileName", "");
        const std::string set_name = ini.get(sec, "SetName", "");
        for (int i = 1; i <= tiles; ++i) {
            // W3-B: Tile<NN>Anim of the set's own section (SetName=).
            char k[32];
            std::snprintf(k, sizeof k, "Tile%02dAnim", i);
            std::string anim = set_name.empty() ? std::string() : ini.get(set_name, k, "");
            const std::size_t sc = anim.find(';');
            if (sc != std::string::npos) anim = anim.substr(0, sc);
            while (!anim.empty() && (anim.back() == ' ' || anim.back() == '\t' || anim.back() == '\r')) anim.pop_back();
            if (!anim.empty()) {
                TileAnim ta;
                ta.anim = anim;
                auto num = [&](const char* suffix) {
                    char kk[40];
                    std::snprintf(kk, sizeof kk, "Tile%02d%s", i, suffix);
                    return ini.get_int(set_name, kk, 0);
                };
                ta.xoff = num("XOffset");
                ta.yoff = num("YOffset");
                ta.attaches_to = num("AttachesTo");
                ta.zadjust = num("ZAdjust");
                tile_anims_[static_cast<uint32_t>(slots_.size())] = ta;
            }
            slots_.push_back(Slot{file, i, set_idx});
        }
    }
    // CB-3 Naval [RE-GATE APPLIED: naval.md §2]: the [General] tile-set
    // classification the game uses to stamp each tile's LandType. Absent keys
    // stay -1 (never match a valid set index -> classified Clear).
    general_.water        = ini.get_int("General", "WaterSet", -1);
    general_.shore        = ini.get_int("General", "ShorePieces", -1);
    general_.cliff        = ini.get_int("General", "CliffSet", -1);
    general_.water_cliffs = ini.get_int("General", "WaterCliffs", -1);
    general_.rough        = ini.get_int("General", "RoughTile", -1);
    general_.clear        = ini.get_int("General", "ClearTile", -1);
}

std::string Theater::entry_name(const Slot& s) const {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s%02d.%s", s.file.c_str(), s.local, ext_.c_str());
    return buf;
}

std::string Theater::filename_for_tile(uint32_t global_index) const {
    if (global_index >= slots_.size()) return std::string();
    return entry_name(slots_[global_index]);
}

ra2assets::LandType Theater::land_type_of_cell(uint32_t global_index) const {
    if (global_index >= slots_.size()) return ra2assets::LandType::Clear;
    return ra2assets::land_type_from_tile_set(slots_[global_index].set, general_);
}

const ra2assets::TmpFile* Theater::tmp_for_tile(uint32_t global_index) {
    if (global_index >= slots_.size()) return nullptr;
    const std::string name = entry_name(slots_[global_index]);

    auto hit = cache_.find(name);
    if (hit != cache_.end()) return &hit->second;
    if (misses_.count(name)) return nullptr;

    const ra2assets::MixFile* src = &mix_;
    const ra2assets::MixEntry* e = mix_.find(name);
    for (std::size_t i = 0; !e && i < fallbacks_.size(); ++i) {   // W6-05-02
        e = fallbacks_[i].find(name);
        if (e) src = &fallbacks_[i];
    }
    if (!e) {
        misses_.emplace(name, true);
        return nullptr;
    }
    try {
        ra2assets::TmpFile t = ra2assets::TmpFile::load(src->read(*e));
        auto ins = cache_.emplace(name, std::move(t));
        return &ins.first->second;
    } catch (const std::exception&) {
        misses_.emplace(name, true);
        return nullptr;
    }
}

// NOTE: terrain_screen_bounds is declared above (iso_renderer.hpp) but
// DEFINED in engine/world.cpp, not here -- this file (iso_renderer.cpp)
// pulls in SDL and is not part of the SDL-free `ra2assets_tests` binary,
// while world.cpp already is. Input needs this pure function to clamp
// camera drag to the map's extent (Task 12 review Finding 2) without
// dragging SDL into the test build; see world.cpp for the implementation
// and rationale.

static const World* g_color_world = nullptr;
void set_color_world(const World* w) { g_color_world = w; }

namespace {
const ObjTint* g_obj_tint = nullptr;
}
void set_obj_tint(const ObjTint* t) { g_obj_tint = (t && !t->plain()) ? t : nullptr; }
const ObjTint* obj_tint() { return g_obj_tint; }
void blit_object(void* sdl_renderer, void* texp, const void* dstp) {
    SDL_Renderer* ren = static_cast<SDL_Renderer*>(sdl_renderer);
    SDL_Texture* tex = static_cast<SDL_Texture*>(texp);
    const SDL_Rect* dst = static_cast<const SDL_Rect*>(dstp);
    const ObjTint* t = g_obj_tint;
    if (!t || !tex) { SDL_RenderCopy(ren, tex, nullptr, dst); return; }
    SDL_SetTextureColorMod(tex, t->mod[0], t->mod[1], t->mod[2]);
    SDL_SetTextureAlphaMod(tex, t->alpha);
    SDL_RenderCopy(ren, tex, nullptr, dst);
    if (t->add[0] || t->add[1] || t->add[2]) {
        SDL_BlendMode prev = SDL_BLENDMODE_BLEND;
        SDL_GetTextureBlendMode(tex, &prev);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod(tex, t->add[0], t->add[1], t->add[2]);
        SDL_RenderCopy(ren, tex, nullptr, dst);
        SDL_SetTextureBlendMode(tex, prev);
    }
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}
ra2assets::Palette house_draw_palette(const ra2assets::Palette& base, int house) {
    return g_color_world ? g_color_world->house_palette(base, house) : pal_for_house(base, house);
}
int house_draw_key(int house) {
    if (!g_color_world || !g_color_world->houses_configured()) return house == 1 ? 1 : 0;
    return 10 + g_color_world->house_color(house);
}

// --- W2-GA-P1: cached painter-ordered cell index + terrain draw list ---------
// Terrain is static, so the painter order (x+y, then level, then x -- the
// order the old per-frame std::sort produced), the per-cell screen point and
// the (tile, subtile) texture are computed ONCE per map, and each frame only
// walks the depth rows (x+y) that can reach the view. The original likewise
// redraws from cached per-cell draw info rather than re-resolving tiles.

const MapIndex& map_index(const ra2assets::MapFile& map) {
    static MapIndex idx;
    const auto& cells = map.cells();
    if (idx.map == &map && idx.ncells == cells.size() &&
        (cells.empty() || idx.first_cell == &cells.front()))
        return idx;
    idx = MapIndex{};
    idx.map = &map;
    idx.ncells = cells.size();
    idx.first_cell = cells.empty() ? nullptr : &cells.front();
    idx.order.resize(cells.size());
    for (uint32_t i = 0; i < cells.size(); ++i) idx.order[i] = i;
    std::sort(idx.order.begin(), idx.order.end(), [&](uint32_t a, uint32_t b) {
        const auto& ca = cells[a];
        const auto& cb = cells[b];
        const int da = ca.x + ca.y;
        const int db = cb.x + cb.y;
        if (da != db) return da < db;
        if (ca.level != cb.level) return ca.level < cb.level;
        return ca.x < cb.x;
    });
    if (cells.empty()) return idx;
    idx.min_depth = cells[idx.order.front()].x + cells[idx.order.front()].y;
    const int max_depth = cells[idx.order.back()].x + cells[idx.order.back()].y;
    idx.row_start.assign(static_cast<size_t>(max_depth - idx.min_depth + 2), 0);
    size_t k = 0;
    for (int d = idx.min_depth; d <= max_depth + 1; ++d) {
        while (k < idx.order.size() && cells[idx.order[k]].x + cells[idx.order[k]].y < d) ++k;
        idx.row_start[static_cast<size_t>(d - idx.min_depth)] = static_cast<uint32_t>(k);
    }
    for (const auto& c : cells) idx.max_level = std::max(idx.max_level, static_cast<int>(c.level));
    return idx;
}

// Depth rows (x+y) whose cells can intersect [0, view_h): a cell of row d
// projects at p.y = d*15 - level*15, and its drawn rect spans
// [p.y + top_px, p.y + bottom_px].
static void visible_rows(const MapIndex& idx, const Camera& cam, int view_h, int top_px,
                         int bottom_px, int& r0, int& r1) {
    const double half = kTileH / 2.0;
    const int lo = static_cast<int>(std::floor((-cam.y - bottom_px) / half));
    const int hi = static_cast<int>(std::ceil((view_h - cam.y - top_px + idx.max_level * kLevelPx) / half));
    r0 = std::max(0, lo - idx.min_depth);
    r1 = std::min(static_cast<int>(idx.row_start.size()) - 2, hi - idx.min_depth);
}

void for_each_visible_cell(const ra2assets::MapFile& map, const Camera& cam, int view_w,
                           int view_h, int margin_px,
                           const std::function<void(const ra2assets::MapCell&, int, int)>& fn) {
    const MapIndex& idx = map_index(map);
    if (idx.order.empty()) return;
    const auto& cells = map.cells();
    int r0 = 0, r1 = -1;
    visible_rows(idx, cam, view_h, -margin_px, kTileH + margin_px, r0, r1);
    for (int r = r0; r <= r1; ++r)
        for (uint32_t k = idx.row_start[static_cast<size_t>(r)]; k < idx.row_start[static_cast<size_t>(r) + 1]; ++k) {
            const ra2assets::MapCell& c = cells[idx.order[k]];
            const ScreenPt p = cell_to_screen(c.x, c.y, static_cast<int>(c.level), kTileW, kTileH, kLevelPx);
            const int sx = p.x + cam.x, sy = p.y + cam.y;
            if (sx + kTileW + margin_px <= 0 || sy + kTileH + margin_px <= 0 || sx - margin_px >= view_w ||
                sy - margin_px >= view_h)
                continue;
            fn(c, sx, sy);
        }
}

namespace {

// One drawable terrain item per map cell, in painter order (parallel to
// MapIndex::order). Textures are resolved per unique (tile, subtile) once
// and pinned in the TextureCache, so the cached pointer stays valid.
struct TerrainItem {
    int32_t px = 0, py = 0;        // camera-local projected point
    uint16_t w = 0, h = 0;         // base tile rect
    int16_t ex = 0, ey = 0;        // extra image offset from (px, py)
    uint16_t ew = 0, eh = 0;       // extra image size (0 = none)
    int32_t slot = -1;             // unique (tile,subtile) slot, -1 = undrawable
    int16_t cx = 0, cy = 0;        // cell
    uint8_t level = 0;
};
struct TerrainSlot {
    uint32_t tile = 0;
    uint8_t sub = 0;
    int level = -1;   // W2-GA-05: lit palette level, -1 = the plain palette
    SDL_Texture* tex = nullptr;
    SDL_Texture* etex = nullptr;
};
struct TerrainDrawList {
    const ra2assets::MapFile* map = nullptr;
    size_t ncells = 0;
    const ra2assets::MapCell* first_cell = nullptr;
    const Theater* theater = nullptr;
    uint64_t tc_id = 0;
    uint64_t pal_sum = 0;
    std::vector<TerrainItem> items;
    std::vector<TerrainSlot> slots;
    int top_px = 0, bottom_px = kTileH;   // union of item extents rel. to p.y
    int fallbacks = 0;   // W6-05-10 cells drawn as clear ground (no tile image)
};

uint64_t g_terrain_builds = 0;
int g_terrain_fallbacks = 0;
// W2-GA-05: per-height lit terrain palettes (GameScene's map [Lighting]); null
// in every gate. g_level_sum keys the cached draw list.
const std::vector<ra2assets::Palette>* g_level_pals = nullptr;
uint64_t g_level_sum = 0;

uint64_t palette_sum(const ra2assets::Palette& pal) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 256; ++i) {
        const ra2assets::Rgba c = pal[static_cast<uint8_t>(i)];
        h = (h ^ (static_cast<uint64_t>(c.r) | (static_cast<uint64_t>(c.g) << 8) |
                  (static_cast<uint64_t>(c.b) << 16))) * 1099511628211ull;
    }
    return h;
}

// W2-GA-05: the slot's lit palette (and a key suffix naming level + lighting
// set, so a later map's lighting never reuses these textures).
const ra2assets::Palette& lit_palette(const ra2assets::Palette& pal, const TerrainSlot& s, char* key,
                                      std::size_t cap) {
    if (s.level < 0 || !g_level_pals || s.level >= static_cast<int>(g_level_pals->size())) return pal;
    const std::size_t n = std::strlen(key);
    std::snprintf(key + n, cap - n, "@%d:%llx", s.level, static_cast<unsigned long long>(g_level_sum));
    return (*g_level_pals)[static_cast<std::size_t>(s.level)];
}

SDL_Texture* extra_texture(TextureCache& tc, Theater& theater, const ra2assets::Palette& pal,
                           TerrainSlot& s) {
    if (s.etex) return s.etex;
    char ekey[128];
    std::snprintf(ekey, sizeof ekey, "tmp_extra:%s#%u", theater.filename_for_tile(s.tile).c_str(),
                  static_cast<unsigned>(s.sub));
    const ra2assets::Palette& p = lit_palette(pal, s, ekey, sizeof ekey);
    s.etex = static_cast<SDL_Texture*>(tc.peek(ekey));
    if (!s.etex) {
        const ra2assets::TmpFile* tmp = theater.tmp_for_tile(s.tile);
        if (!tmp) return nullptr;
        ra2assets::TileImage eimg = tmp->decode_extra(s.sub);
        if (eimg.w <= 0 || eimg.h <= 0) return nullptr;
        s.etex = static_cast<SDL_Texture*>(tc.get(ekey, eimg.indices.data(), eimg.w, eimg.h, p));
    }
    tc.pin(ekey);
    return s.etex;
}

SDL_Texture* slot_texture(TextureCache& tc, Theater& theater, const ra2assets::Palette& pal,
                          TerrainSlot& s) {
    if (s.tex) return s.tex;
    // Key by template file + subtile (shared with any other user of the cache).
    char key[128];
    std::snprintf(key, sizeof key, "%s#%u", theater.filename_for_tile(s.tile).c_str(),
                  static_cast<unsigned>(s.sub));
    const ra2assets::Palette& p = lit_palette(pal, s, key, sizeof key);
    s.tex = static_cast<SDL_Texture*>(tc.peek(key));
    if (!s.tex) {
        const ra2assets::TmpFile* tmp = theater.tmp_for_tile(s.tile);
        if (!tmp) return nullptr;
        ra2assets::TileImage img = tmp->decode(s.sub);
        s.tex = static_cast<SDL_Texture*>(tc.get(key, img.indices.data(), img.w, img.h, p));
    }
    tc.pin(key);
    return s.tex;
}

TerrainDrawList& terrain_list(TextureCache& tc, const ra2assets::MapFile& map, Theater& theater,
                              const ra2assets::Palette& pal) {
    static TerrainDrawList L;
    const auto& cells = map.cells();
    const uint64_t ps = palette_sum(pal);
    if (L.map == &map && L.ncells == cells.size() &&
        (cells.empty() || L.first_cell == &cells.front()) && L.theater == &theater &&
        L.tc_id == tc.instance_id() && L.pal_sum == (ps ^ g_level_sum))
        return L;
    L = TerrainDrawList{};
    ++g_terrain_builds;
    L.map = &map;
    L.ncells = cells.size();
    L.first_cell = cells.empty() ? nullptr : &cells.front();
    L.theater = &theater;
    L.tc_id = tc.instance_id();
    L.pal_sum = ps ^ g_level_sum;
    const MapIndex& idx = map_index(map);
    std::unordered_map<uint64_t, int32_t> slot_of;
    L.items.resize(idx.order.size());
    for (size_t k = 0; k < idx.order.size(); ++k) {
        const ra2assets::MapCell& c = cells[idx.order[k]];
        TerrainItem& it = L.items[k];
        it.cx = static_cast<int16_t>(c.x);
        it.cy = static_cast<int16_t>(c.y);
        it.level = c.level;
        const ScreenPt p = cell_to_screen(c.x, c.y, static_cast<int>(c.level), kTileW, kTileH, kLevelPx);
        it.px = p.x;
        it.py = p.y;
        uint32_t tile = c.tile;
        uint8_t sub = c.subtile;
        const ra2assets::TmpFile* tmp = theater.tmp_for_tile(tile);
        if (!tmp || sub >= tmp->count()) {
            // W6-05-10: a tile no archive holds (SOV01 MWater07.tem) or a
            // sub-tile past the template's image count (ALL01 glat15 BADSUB)
            // draws as clear ground -- template 0 sub-tile 0, the same
            // fallback the map loader applies to the 0xFFFF sentinel
            // (src/map.cpp) [RE-FALLBACK: the original's missing-image path
            // was not traced].
            ++L.fallbacks;
            if (L.fallbacks <= 16)
                diag::note("TERRAIN_FALLBACK cell=%d,%d level=%u tile=%u subtile=%u file=%s reason=%s",
                    c.x,c.y,static_cast<unsigned>(c.level),static_cast<unsigned>(tile),static_cast<unsigned>(sub),
                    theater.filename_for_tile(tile).c_str(),tmp ? "subtile-out-of-range" : "tile-not-found");
            tile = 0;
            sub = 0;
            tmp = theater.tmp_for_tile(tile);
            if (!tmp || sub >= tmp->count()) continue;
        }
        it.w = static_cast<uint16_t>(tmp->tile_w());
        it.h = static_cast<uint16_t>(tmp->tile_h());
        const int lvl = g_level_pals ? std::min<int>(c.level, static_cast<int>(g_level_pals->size()) - 1) : -1;
        const uint64_t key = (static_cast<uint64_t>(tile) << 16) | (static_cast<uint64_t>(sub) << 8) |
                             static_cast<uint64_t>(lvl + 1);
        auto f = slot_of.find(key);
        if (f == slot_of.end()) {
            f = slot_of.emplace(key, static_cast<int32_t>(L.slots.size())).first;
            TerrainSlot s;
            s.tile = tile;
            s.sub = sub;
            s.level = lvl;
            L.slots.push_back(s);
        }
        it.slot = f->second;
        if (tmp->has_extra(sub)) {
            // Same placement rule as before: the extra's offset relative to
            // THIS subtile's diamond is (x_extra - x, y_extra - y).
            it.ex = static_cast<int16_t>(tmp->extra_x(sub) - tmp->tile_x(sub));
            it.ey = static_cast<int16_t>(tmp->extra_y(sub) - tmp->tile_y(sub));
            if (SDL_Texture* et = extra_texture(tc, theater, pal, L.slots[static_cast<size_t>(it.slot)])) {
                int ew = 0, eh = 0;
                SDL_QueryTexture(et, nullptr, nullptr, &ew, &eh);
                it.ew = static_cast<uint16_t>(ew);
                it.eh = static_cast<uint16_t>(eh);
            }
        }
        L.top_px = std::min(L.top_px, it.ew ? static_cast<int>(it.ey) : 0);
        L.bottom_px = std::max(L.bottom_px, std::max(static_cast<int>(it.h), it.ew ? it.ey + static_cast<int>(it.eh) : 0));
    }
    g_terrain_fallbacks = L.fallbacks;
    if (L.fallbacks) std::cerr << "[terrain] " << L.fallbacks << " cells have no tile image: drawn as clear ground" << std::endl;
    return L;
}

}  // namespace

int terrain_fallback_cells() { return g_terrain_fallbacks; }

void set_terrain_level_palettes(const std::vector<ra2assets::Palette>* pals) {
    g_level_pals = (pals && !pals->empty()) ? pals : nullptr;
    g_level_sum = 0;
    if (g_level_pals)
        for (const ra2assets::Palette& p : *g_level_pals) g_level_sum = g_level_sum * 31 + palette_sum(p);
}

void prewarm_terrain(TextureCache& tc, const ra2assets::MapFile& map, Theater& theater,
                     const ra2assets::Palette& pal) {
    TerrainDrawList& L = terrain_list(tc, map, theater, pal);
    for (TerrainSlot& s : L.slots) slot_texture(tc, theater, pal, s);
}

int draw_terrain(void* sdl_renderer, TextureCache& tc,
                 const ra2assets::MapFile& map, Theater& theater,
                 const ra2assets::Palette& pal, const Camera& cam, int view_w,
                 int view_h, const World* vis, int viewer) {
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    const uint64_t built_before = g_terrain_builds;
    TerrainDrawList& L = terrain_list(tc, map, theater, pal);
    // W2-GA-P4: a (re)built list uploads every tile the map uses right away,
    // so scrolling into new terrain never pays a first-use decode later.
    if (g_terrain_builds != built_before)
        for (TerrainSlot& s : L.slots) slot_texture(tc, theater, pal, s);
    const MapIndex& idx = map_index(map);
    if (L.items.empty()) return 0;
    // Shroud [RE: presentation.md §4, W2-GA-02]: with a World whose shroud is
    // on, a cell the viewing house has never explored is not drawn at all -- neither its
    // diamond nor its cliff/slope extra -- so the black map hides the layout
    // (the SHROUD.SHP post-pass then softens the edges). FogOfWar (W2-GA-03,
    // off by default as in the original) dims explored-but-not-visible cells.
    const bool shroud = vis && vis->shroud_enabled();
    const bool fog = shroud && vis->fog_of_war();
    int r0 = 0, r1 = -1;
    visible_rows(idx, cam, view_h, L.top_px, L.bottom_px, r0, r1);
    int drawn = 0;
    for (int r = r0; r <= r1; ++r)
        for (uint32_t k = idx.row_start[static_cast<size_t>(r)]; k < idx.row_start[static_cast<size_t>(r) + 1]; ++k) {
            const TerrainItem& it = L.items[k];
            if (it.slot < 0) continue;
            SDL_Rect dst{it.px + cam.x, it.py + cam.y, it.w, it.h};
            const SDL_Rect edst{it.px + cam.x + it.ex, it.py + cam.y + it.ey, it.ew, it.eh};
            const bool base_visible = viewport_intersects(dst.x, dst.y, dst.w, dst.h, view_w, view_h);
            const bool extra_visible = viewport_intersects(edst.x, edst.y, edst.w, edst.h, view_w, view_h);
            if (!base_visible && !extra_visible) continue;
            if (shroud && !vis->explored_at(viewer, it.cx, it.cy)) continue;
            TerrainSlot& s = L.slots[static_cast<size_t>(it.slot)];
            SDL_Texture* tex = slot_texture(tc, theater, pal, s);
            if (!tex) continue;
            const bool dim = fog && !vis->visible_at(viewer, it.cx, it.cy);
            // Modulate the tile's actual opaque pixels. Filling its bounding
            // rectangle darkened transparent diamond corners and left cliff
            // extensions undimmed, producing rectangular map artifacts.
            if (base_visible) blit_terrain(renderer, tex, dst, dim);
            ++drawn;
            if (it.ew) {
                if (extra_visible)
                    if (SDL_Texture* et = extra_texture(tc, theater, pal, s))
                        blit_terrain(renderer, et, edst, dim);
            }
        }
    return drawn;
}

bool draw_unit(void* sdl_renderer, TextureCache& tc, const Unit& unit,
              ra2assets::ShpFile& shp, const ra2assets::Palette& pal,
              const Camera& cam, int view_w, int view_h) {
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    if (unit.frame < 0 || unit.frame >= shp.count()) return false;

    // Project the unit's CONTINUOUS iso-cell position (fx, fy) -- not the
    // snapped integer cell -- so it renders smoothly between cells while
    // walking. cell_to_screen is linear in (x, y), so the same shear applies
    // to fractional coordinates (see iso_project.hpp). Idle units have
    // fx/fy == cell_x/cell_y, so this is identical to the integer path.
    const double hw = kTileW / 2.0;
    const double hh = kTileH / 2.0;
    // W5-B R-061: + the infantry sub-cell spot offset (leptons, 0 for vehicles).
    const double ufx = unit.fx + unit.sub_x / 256.0;
    const double ufy = unit.fy + unit.sub_y / 256.0;
    const int tile_screen_x =
        static_cast<int>(std::lround((ufx - ufy) * hw)) + cam.x;
    const int tile_screen_y =
        static_cast<int>(std::lround((ufx + ufy) * hh - unit.level * kLevelPx)) +
        cam.y;

    const ra2assets::ShpFile::Frame f = shp.frame_info(unit.frame);

    // Anchor math (derived + verified against the real gi.shp, not just the
    // brief's starting heuristic -- see task-10-report.md): TS/RA2 SHP
    // sprites are drawn onto a fixed global cx*cy canvas whose CENTER is the
    // sprite's "ground point"; each frame's (x, y, w, h) is only the tight
    // bounding box of that frame's non-transparent pixels *within* that
    // canvas (used to shrink the stored/RLE-encoded data). So the correct
    // anchor uses the canvas center (shp.width()/2, shp.height()/2), NOT the
    // current frame's own w/h -- a per-frame-size anchor would make the feet
    // jitter up/down as the animation cycles through frames of different
    // heights. Verified: gi.shp's canvas is 78x66 (center 39,33); frame 0 is
    // 13x29 at offset (32,7) -- its bottom edge lands 3px BELOW the canvas
    // center, i.e. the character's feet sit almost exactly on the tile's
    // ground point across every frame checked (frame 4, a taller frame,
    // lands 8px below) -- both a couple pixels of slack for the foot/shadow
    // pixels, not floating or sunk.
    const int canvas_cx = shp.width();
    const int canvas_cy = shp.height();
    SDL_Rect dst;
    dst.w = f.w;
    dst.h = f.h;
    dst.x = tile_screen_x + kTileW / 2 - canvas_cx / 2 + f.x;
    dst.y = tile_screen_y + kTileH / 2 - canvas_cy / 2 + f.y;

    // Cull if fully off-screen.
    if (dst.x + dst.w <= 0 || dst.y + dst.h <= 0 || dst.x >= view_w || dst.y >= view_h)
        return false;

    // Key includes the house so the two houses' recolors (pal_for_house)
    // cache as distinct textures -- otherwise whichever house rendered a
    // given image:frame FIRST would "win" the cache slot and every other
    // house's unit sharing that image:frame would silently render in the
    // wrong color. pal_for_house is only ever called below, on a cache MISS,
    // so steady-state per-frame cost is unchanged (Task 7).
    // Engineer/Spy Capture [RE: capture.md §C1]: a disguised unit draws as its
    // disguise type in the disguised house's palette (render-only). When NOT
    // disguised these equal unit.image/unit.house -> the cache key + palette are
    // byte-identical to the pre-Task-7 path (the caller already selected the SHP
    // matching draw_image). No gate renders a disguised unit.
    const std::string& draw_image = unit.disguised ? unit.disguised_as_type : unit.image;
    const int draw_house = unit.disguised ? unit.disguised_as_house : unit.house;
    char key[80];
    std::snprintf(key, sizeof key, "unit:%s:%d:c%d", draw_image.c_str(), unit.frame,
                  house_draw_key(draw_house));
    SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
    if (!tex) {
        std::vector<uint8_t> idx = shp.decode_indexed(unit.frame);
        const ra2assets::Palette house_pal = house_draw_palette(pal, draw_house);   // G-16
        // G-90: RA2 infantry SHPs bake their ground shadow into the image frame
        // as palette index 12 (gi/cons/dog: 60-80 px under the feet; the
        // second-half shadow frames are empty) -> translucent black.
        // [RE-FALLBACK: RE-P7 open -- index + blend from the binary's blitter.]
        tex = static_cast<SDL_Texture*>(tc.get_shadowed(key, idx.data(), f.w, f.h, house_pal, 12, 128));
    }
    blit_object(renderer, tex, &dst);   // W5-PRES ObjTint (none in the gates)
    return true;
}

bool draw_voxel_sprite(void* sdl_renderer, TextureCache& tc, const std::string& key,
                       const VoxelSprite& s, double fx, double fy, int level,
                       const Camera& cam, int view_w, int view_h, int extra_dy) {
    if (s.w <= 0 || s.h <= 0) return false;
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);

    // Same fractional diamond projection as draw_unit: project the continuous
    // (fx, fy) ground point, then offset to the tile CENTER (kTileW/2,
    // kTileH/2) -- the iso ground reference. The sprite's model-origin anchor
    // is then placed on that center so hull/turret/barrel (which all share the
    // model origin) stack coherently.
    const double hw = kTileW / 2.0;
    const double hh = kTileH / 2.0;
    const int center_x =
        static_cast<int>(std::lround((fx - fy) * hw)) + cam.x + kTileW / 2;
    const int center_y =
        static_cast<int>(std::lround((fx + fy) * hh - level * kLevelPx)) + cam.y +
        kTileH / 2 - extra_dy;   // CB-2: raised-draw offset for airborne sprites

    SDL_Rect dst;
    dst.x = center_x - s.anchor_x;
    dst.y = center_y - s.anchor_y;
    dst.w = s.w;
    dst.h = s.h;

    if (dst.x + dst.w <= 0 || dst.y + dst.h <= 0 || dst.x >= view_w || dst.y >= view_h)
        return false;

    SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
    if (!tex) tex = static_cast<SDL_Texture*>(tc.get_rgba(key, s.rgba.data(), s.w, s.h));
    blit_object(renderer, tex, &dst);   // W5-PRES ObjTint (none in the gates)
    return true;
}

// Screen-space center (in projected pixels + camera) of the diamond tile at
// continuous iso position (fx, fy, level). The tile's ground reference is the
// center of its bounding rect (tile_w/2, tile_h/2 below the projected
// top-left), matching draw_unit's anchor.
static void tile_center_screen(double fx, double fy, int level, const Camera& cam,
                               int& out_x, int& out_y) {
    const double hw = kTileW / 2.0;
    const double hh = kTileH / 2.0;
    out_x = static_cast<int>(std::lround((fx - fy) * hw)) + cam.x + kTileW / 2;
    out_y = static_cast<int>(std::lround((fx + fy) * hh - level * kLevelPx)) + cam.y +
            kTileH / 2;
}

void draw_ground_shadow(void* sdl_renderer, double fx, double fy, int level,
                        int width_px, const Camera& cam, int view_w, int view_h) {
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    int cx, cy;
    tile_center_screen(fx, fy, level, cam, cx, cy);
    const int w = width_px > 0 ? width_px : kTileW;
    const int hh = w / 4;                         // 2:1 iso, half-height of the blob
    if (cx + w < 0 || cy + hh < 0 || cx - w >= view_w || cy - hh >= view_h) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 110);   // dark, translucent
    // Filled iso diamond: horizontal scanlines whose half-width tapers to 0.
    for (int dy = -hh; dy <= hh; ++dy) {
        const int hw = (w / 2) * (hh - (dy < 0 ? -dy : dy)) / (hh == 0 ? 1 : hh);
        SDL_Rect row{cx - hw, cy + dy, 2 * hw + 1, 1};
        SDL_RenderFillRect(renderer, &row);
    }
}

void draw_selection(void* sdl_renderer, const Unit& unit, const Camera& cam, int view_w,
                    int view_h, int width_px) {
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    int cx, cy;
    tile_center_screen(unit.fx + unit.sub_x / 256.0, unit.fy + unit.sub_y / 256.0, unit.level, cam, cx, cy);   // W5-B sub-cell

    // Diamond size: default one-cell footprint (60x30, 2:1), or scaled to
    // `width_px` for wide voxel sprites (keeping the 2:1 iso ratio).
    const int diamond_w = width_px > 0 ? width_px : kTileW;
    // Cull if the diamond is well off-screen.
    if (cx + diamond_w < 0 || cy + diamond_w < 0 || cx - diamond_w >= view_w ||
        cy - diamond_w >= view_h)
        return;

    const int hw = diamond_w / 2;
    const int hh = diamond_w / 4;
    // A green isometric diamond (the tile footprint) centered on the ground
    // point: top, right, bottom, left corners.
    SDL_Point pts[5] = {
        {cx, cy - hh}, {cx + hw, cy}, {cx, cy + hh}, {cx - hw, cy}, {cx, cy - hh},
    };
    SDL_SetRenderDrawColor(renderer, 40, 240, 40, 255);
    SDL_RenderDrawLines(renderer, pts, 5);
    // Small corner brackets (thicker read) at the two side vertices.
    SDL_RenderDrawLine(renderer, cx - hw, cy, cx - hw + 6, cy - 3);
    SDL_RenderDrawLine(renderer, cx + hw, cy, cx + hw - 6, cy - 3);
}

void draw_path(void* sdl_renderer, const Unit& unit, const Camera& cam, int view_w,
               int view_h) {
    if (unit.path.size() < 2) return;
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    (void)view_w;
    (void)view_h;
    // The path color's alpha (200, i.e. ~78% opaque) only takes effect if
    // the renderer's blend mode allows it -- SDL's default draw-color blend
    // mode is SDL_BLENDMODE_NONE, which ignores alpha and draws fully
    // opaque (Task 12 review Finding 4). Save/restore so this draw call
    // doesn't change blend behavior for whatever renders after it.
    SDL_BlendMode prev_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(renderer, &prev_blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (unit.attack_order)
        SDL_SetRenderDrawColor(renderer, 240, 55, 45, 220);  // attack orders read red at a glance
    else
        SDL_SetRenderDrawColor(renderer, 60, 220, 90, 200);
    // Yuri's Revenge Target Lines show the ordered unit-to-destination
    // segment, rather than the internal navigation route. Keep the endpoint
    // at the actual order destination so pathfinding detours don't redraw the
    // line every time the route is recalculated.
    int sx, sy, tx, ty;
    tile_center_screen(unit.fx + unit.sub_x / 256.0, unit.fy + unit.sub_y / 256.0,
                       unit.level, cam, sx, sy);
    tile_center_screen(unit.path.back().x, unit.path.back().y, unit.level, cam, tx, ty);
    SDL_RenderDrawLine(renderer, sx, sy, tx, ty);
    SDL_SetRenderDrawBlendMode(renderer, prev_blend);
}

void draw_selection_box(void* sdl_renderer, int x0, int y0, int x1, int y1) {
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    SDL_Rect r;
    r.x = std::min(x0, x1);
    r.y = std::min(y0, y1);
    r.w = std::abs(x1 - x0);
    r.h = std::abs(y1 - y0);
    SDL_SetRenderDrawColor(renderer, 60, 220, 90, 255);
    SDL_RenderDrawRect(renderer, &r);
}

// HP bar geometry: kHpBarUp matches input.cpp's kHitUp (46px -- "most of a
// sprite height up" from the ground point, the same anchor the click hit-box
// and draw_selection use), plus a small gap so the bar clears the selection
// diamond's top corner instead of overlapping it.
static constexpr int kHpBarW = 24;
static constexpr int kHpBarH = 3;
static constexpr int kHpBarUp = 46;
static constexpr int kHpBarGap = 6;

// G-88: health-bar colour thresholds (set_health_conditions; defaults = the
// stock rulesmd values).
static double g_condition_yellow = 0.5, g_condition_red = 0.25;
void set_health_conditions(double yellow, double red) {
    g_condition_yellow = yellow;
    g_condition_red = red;
}

void draw_hp_bar(void* sdl_renderer, const Unit& unit, const Camera& cam, int view_w, int view_h,
                 int width_px) {
    if (unit.max_hp <= 0) return;
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);

    // Bar width: default 24px, or the caller-supplied sprite width (voxel
    // hull) so the bar spans a wider vehicle.
    const int bar_w = width_px > 0 ? width_px : kHpBarW;

    int cx, cy;
    tile_center_screen(unit.fx + unit.sub_x / 256.0, unit.fy + unit.sub_y / 256.0, unit.level, cam, cx, cy);   // W5-B sub-cell
    const int bar_cy = cy - kHpBarUp - kHpBarGap;

    SDL_Rect bg{cx - bar_w / 2, bar_cy - kHpBarH / 2, bar_w, kHpBarH};
    if (bg.x + bg.w <= 0 || bg.y + bg.h <= 0 || bg.x >= view_w || bg.y >= view_h) return;

    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &bg);

    const int hp = unit.hp < 0 ? 0 : unit.hp;
    int fill_w = static_cast<int>(static_cast<long long>(hp) * bar_w / unit.max_hp);
    if (fill_w > bar_w) fill_w = bar_w;
    if (fill_w < 0) fill_w = 0;
    // clamp: a living unit always shows at least 1px of health
    if (unit.hp > 0 && fill_w < 1) fill_w = 1;
    if (fill_w > 0) {
        SDL_Rect fg{bg.x, bg.y, fill_w, kHpBarH};
        // G-88 [rulesmd [AudioVisual] ConditionYellow=50% / ConditionRed=25%]:
        // green above Yellow, yellow above Red, red at or below Red.
        const double frac = static_cast<double>(unit.hp) / unit.max_hp;
        if (frac > g_condition_yellow) SDL_SetRenderDrawColor(renderer, 40, 220, 40, 255);
        else if (frac > g_condition_red) SDL_SetRenderDrawColor(renderer, 230, 210, 40, 255);
        else SDL_SetRenderDrawColor(renderer, 220, 40, 40, 255);
        SDL_RenderFillRect(renderer, &fg);
    }
}

void draw_building_hp_bar(void* sdl_renderer, const Building& b, const Camera& cam, int view_w, int view_h) {
    if (b.max_hp <= 0) return;
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    const int bar_w = 40;

    int cx, cy;
    tile_center_screen(static_cast<double>(b.cx), static_cast<double>(b.cy), 0, cam, cx, cy);
    const int bar_cy = cy - 32;

    SDL_Rect bg{cx - bar_w / 2, bar_cy - kHpBarH / 2, bar_w, kHpBarH};
    if (bg.x + bg.w <= 0 || bg.y + bg.h <= 0 || bg.x >= view_w || bg.y >= view_h) return;

    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &bg);

    const int hp = b.hp < 0 ? 0 : b.hp;
    int fill_w = static_cast<int>(static_cast<long long>(hp) * bar_w / b.max_hp);
    if (fill_w > bar_w) fill_w = bar_w;
    if (fill_w < 0) fill_w = 0;
    if (b.hp > 0 && fill_w < 1) fill_w = 1;
    if (fill_w > 0) {
        SDL_Rect fg{bg.x, bg.y, fill_w, kHpBarH};
        const double frac = static_cast<double>(b.hp) / b.max_hp;
        if (frac > g_condition_yellow) SDL_SetRenderDrawColor(renderer, 40, 220, 40, 255);
        else if (frac > g_condition_red) SDL_SetRenderDrawColor(renderer, 230, 210, 40, 255);
        else SDL_SetRenderDrawColor(renderer, 220, 40, 40, 255);
        SDL_RenderFillRect(renderer, &fg);
    }
}

void draw_cargo_pips(void* sdl_renderer, const Unit& unit, int pips_filled, int pips_max,
                     const Camera& cam, int view_w, int view_h) {
    if (pips_max <= 0) return;
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(sdl_renderer);
    int cx, cy;
    tile_center_screen(unit.fx + unit.sub_x / 256.0, unit.fy + unit.sub_y / 256.0, unit.level, cam, cx, cy);   // W5-B sub-cell
    // One row directly UNDER the HP bar (same anchor math as draw_hp_bar).
    const int row_y = cy - kHpBarUp - kHpBarGap + kHpBarH + 2;
    const int pip_w = 4, pip_h = 3, gap = 2;
    const int total_w = pips_max * pip_w + (pips_max - 1) * gap;
    int x = cx - total_w / 2;
    if (x + total_w <= 0 || row_y + pip_h <= 0 || x >= view_w || row_y >= view_h) return;
    for (int k = 0; k < pips_max; ++k) {
        SDL_Rect r{x, row_y, pip_w, pip_h};
        if (k < pips_filled) SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
        else SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderFillRect(renderer, &r);
        x += pip_w + gap;
    }
}

}  // namespace ra2engine

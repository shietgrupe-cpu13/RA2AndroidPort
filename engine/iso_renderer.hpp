#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ra2assets/ini.hpp"
#include "ra2assets/land_type.hpp"  // LandType / TileSetClass (land_type_of_cell)
#include "ra2assets/map.hpp"
#include "ra2assets/mix.hpp"
#include "ra2assets/pal.hpp"
#include "ra2assets/shp.hpp"     // ShpFile (draw_unit)
#include "ra2assets/theater.hpp"  // TmpFile / TileImage
#include "voxel_raster.hpp"       // VoxelSprite (draw_voxel_sprite)
#include "world.hpp"              // Unit

namespace ra2engine {

class TextureCache;  // fwd (engine/texture_cache.hpp)

// Resolves a map cell's GLOBAL tile index -> a theater `.tmp` template file +
// the sub-tile within it, then decodes it. This is the TS/RA2 tile-index
// mapping: the theater control INI (temperat.ini / snow.ini / urban.ini,
// found in ra2.mix/local.mix) lists ordered `[TileSetNNNN]` sections, each
// with `FileName=` and `TilesInSet=`. Walking the sets in order and, within
// each, templates 1..TilesInSet, assigns a running GLOBAL index to every
// template; template T of set S resolves to the mix entry
// "<FileName><NN>.<ext>" (NN = 1-based within-set index, zero-padded to 2).
// A map cell's `tile` field is that global index; its `subtile` field is the
// tile within that template's cblocks_x*cblocks_y grid. Cross-checked against
// OpenRA's ImportLegacyTilesetCommand and XCC's itc_ini_reader.
//
// PURE ra2assets (no SDL) -- it only opens mix entries and decodes TMPs.
class Theater {
public:
    // isotile_mix : the theater tile mix (e.g. isotemp.mix) that holds the
    //               "<file><NN>.<ext>" template entries. Taken by value (owns
    //               the archive bytes); pass with std::move.
    // control_ini_text : the theater control INI text (e.g. temperat.ini).
    // ext : tile file extension WITHOUT the dot, e.g. "tem".
    Theater(ra2assets::MixFile isotile_mix, const std::string& control_ini_text,
            std::string ext);

    // Number of resolvable global templates (sum of TilesInSet over all sets).
    size_t template_count() const { return slots_.size(); }

    // Resolve a global tile index to its decoded TMP, loading+caching lazily.
    // Returns nullptr if the index is out of range or the mix entry is absent.
    const ra2assets::TmpFile* tmp_for_tile(uint32_t global_index);

    // Diagnostic: the "<file><NN>.<ext>" name a global index maps to
    // (empty string if out of range).
    std::string filename_for_tile(uint32_t global_index) const;

    // CB-3 Naval [RE-GATE APPLIED: naval.md §2]: the LandType of a global tile
    // index -- resolves the tile's owning [TileSetNNNN] set and classifies it
    // against the theater [General] indices (WaterSet -> Water, etc.). Returns
    // LandType::Clear for an out-of-range index (land, never water). This is the
    // recovered LandType source (the .tmp TerrainType byte was FALSIFIED).
    ra2assets::LandType land_type_of_cell(uint32_t global_index) const;

    // W6-05-02 [RE: Init_Theater FUN_00519e70 registers the iso AND the theater
    // art mix; a file opens from whichever registered MIX holds it]: archives
    // searched after the iso mix, in order (warub*.urb lives in urban.mix,
    // Ovrpsb*.tem in temperat.mix).
    void add_fallback(ra2assets::MixFile mix) { fallbacks_.push_back(std::move(mix)); }
    // Distinct tile file names that resolved nowhere (diagnostic).
    int miss_count() const { return static_cast<int>(misses_.size()); }

    // W3-B [RE-Q6]: theater tile animations -- the set's SetName section of
    // the control INI lists Tile<NN>Anim / XOffset / YOffset / AttachesTo /
    // ZAdjust for template NN (temperat.ini [Waterfalls] WA01X..WD04X, the
    // [Tunnels...] TUNTOP01..04). The anim stands on the template cell whose
    // sub-tile index == AttachesTo. nullptr when the template has none.
    struct TileAnim { std::string anim; int xoff = 0, yoff = 0, attaches_to = 0, zadjust = 0; };
    const TileAnim* tile_anim(uint32_t global_index) const {
        auto it = tile_anims_.find(global_index);
        return it == tile_anims_.end() ? nullptr : &it->second;
    }

private:
    struct Slot {
        std::string file;  // FileName= (lower/verbatim from INI)
        int local;         // 1-based index within its tile set
        int set;           // owning [TileSetNNNN] set index [RE: naval.md §2]
    };

    std::string entry_name(const Slot& s) const;

    ra2assets::MixFile mix_;
    std::vector<ra2assets::MixFile> fallbacks_;
    std::string ext_;
    std::vector<Slot> slots_;  // global index -> slot
    ra2assets::TileSetClass general_;  // [General] tile-set classification [RE: naval.md §2]
    std::unordered_map<uint32_t, TileAnim> tile_anims_;   // W3-B global index -> anim
    // filename -> decoded TmpFile (absent entries stored as a nullptr sentinel
    // via `misses_`); keeps repeated cells cheap.
    std::unordered_map<std::string, ra2assets::TmpFile> cache_;
    std::unordered_map<std::string, bool> misses_;  // name -> true if known absent
};

// Camera pan: a screen-pixel offset added to every projected tile so the
// terrain (whose cell (0,0) projects to screen (0,0)) can be scrolled into
// view. Larger x scrolls the map right, larger y scrolls it down.
struct Camera {
    int x = 0;
    int y = 0;
};

// Screen bounding box (in camera-local projected pixels, level included) of
// every cell in `map`, using the RA2 tile geometry (60x30, level step 15).
// Useful for centering the camera on a populated region. Returns false (and
// leaves outputs untouched) if the map has no cells.
bool terrain_screen_bounds(const ra2assets::MapFile& map, int& min_x, int& min_y,
                           int& max_x, int& max_y);

// W2-GA-P1: the map's cells in painter order (x+y, level, x), built once per
// MapFile (rebuilt only when a different map / cell vector is passed) with a
// row index by depth d = x+y, so per-frame passes touch only the rows that
// can reach the view instead of sorting/looping the whole map.
struct MapIndex {
    const ra2assets::MapFile* map = nullptr;
    size_t ncells = 0;
    const ra2assets::MapCell* first_cell = nullptr;
    std::vector<uint32_t> order;      // cells() indices, painter order
    std::vector<uint32_t> row_start;  // order[] range of depth d: [rs[d-min], rs[d-min+1])
    int min_depth = 0;
    int max_level = 0;
};
const MapIndex& map_index(const ra2assets::MapFile& map);

// Calls fn(cell, sx, sy) -- sx,sy = the cell's projected top-left + camera --
// for every cell whose 60x30 diamond rect, grown by margin_px, meets the
// view, in painter order.
void for_each_visible_cell(const ra2assets::MapFile& map, const Camera& cam, int view_w,
                           int view_h, int margin_px,
                           const std::function<void(const ra2assets::MapCell&, int, int)>& fn);

// W2-GA-P4: decode + upload every tile texture the map uses (the cached draw
// list's slots) up front, so scrolling never hitches on first-use tiles.
void prewarm_terrain(TextureCache& tc, const ra2assets::MapFile& map, Theater& theater,
                     const ra2assets::Palette& pal);

// W2-GA-05: per-height lit terrain palettes (index = cell level) from the
// map's [Lighting]; nullptr (the default, every gate) draws with `pal`. The
// vector must outlive its use (GameScene owns it and clears this on exit).
void set_terrain_level_palettes(const std::vector<ra2assets::Palette>* pals);

// Draw the map's terrain, back-to-front (painter's order: cells sorted by
// cell.x+cell.y ascending == increasing screen py, so nearer/lower diamonds
// overwrite farther ones). Each cell is projected with
// cell_to_screen(x, y, level, 60, 30, 15), offset by the camera, its tile
// decoded via `theater`, palettized+uploaded via `tc`, and blitted with
// SDL_RenderCopy at (screen + camera, tile_w x tile_h). Cells whose rect is
// fully outside [0,view_w) x [0,view_h) are skipped. Returns the number of
// tiles actually drawn (for diagnostics).
// Shroud/Fog [RE: presentation.md §4, W2-GA-02/03]: with a non-null `vis`
// whose shroud is on, a cell the viewing house (`viewer`, the local house --
// house 0 single-player; a network joiner's own slot) never explored is skipped
// entirely (its diamond AND its cliff/slope extra), and only with the FogOfWar
// flag is an explored-but-not-visible cell dimmed. `vis == nullptr` draws every cell.
// Uses the cached per-map draw list (W2-GA-P1).
int draw_terrain(void* sdl_renderer, TextureCache& tc,
                 const ra2assets::MapFile& map, Theater& theater,
                 const ra2assets::Palette& pal, const Camera& cam, int view_w,
                 int view_h, const World* vis = nullptr, int viewer = 0);

// Draw a unit's current SHP frame on top of the terrain (call AFTER
// draw_terrain so the sprite is not painted over). The unit's interpolated
// position is projected with cell_to_screen(fx, fy, level, 60, 30, 15) +
// camera to get the tile's on-screen position; the SHP frame is then anchored so
// the sprite's fixed "ground point" -- the CENTER of the SHP's global
// cx*cy canvas (`shp.width()/2`, `shp.height()/2`), the standard TS/RA2
// sprite-canvas convention -- lands on the tile's screen-space center
// (tile_w/2, tile_h/2), which is the isometric ground reference for a
// diamond tile. Using the canvas center (not the current frame's own w/h,
// which varies per facing/animation frame) keeps the character's feet
// planted at the same spot across every frame. See iso_renderer.cpp for the
// exact formula and its empirical validation against gi.shp (Task 10
// report). The frame is palettized/uploaded via `tc`, keyed
// "unit:<image>:<frame>". Returns false (no-op, nothing drawn) if
// unit.frame is out of range or the sprite rect is fully off-screen.
bool draw_unit(void* sdl_renderer, TextureCache& tc, const Unit& unit,
               ra2assets::ShpFile& shp, const ra2assets::Palette& pal,
               const Camera& cam, int view_w, int view_h);

// Draw one pre-rendered voxel-model sprite `s` (a FacingSet entry) for a unit
// standing at continuous iso position (fx, fy, level). Projects the ground
// point with the SAME 60x30 diamond shear (level step 15) draw_unit uses --
// tile center = cell_to_screen(fx, fy, level, 60, 30, 15) + camera + (30, 15) --
// then places `s` so its model-origin anchor (s.anchor_x/anchor_y) lands on
// that tile center (blit rect (center.x - anchor_x, center.y - anchor_y, s.w,
// s.h)), so hull/turret/barrel sprites (all sharing the model origin) stack
// coherently (docs/re/voxel-render.md §1). Uploads via tc.get_rgba(key, ...)
// (the sprite is already final RGBA). Off-screen rects are culled. Returns
// false (nothing drawn) on an empty sprite or a fully off-screen rect.
bool draw_voxel_sprite(void* sdl_renderer, TextureCache& tc, const std::string& key,
                       const VoxelSprite& s, double fx, double fy, int level,
                       const Camera& cam, int view_w, int view_h, int extra_dy = 0);

// CB-2 Aircraft [RE-GATE APPLIED: aircraft.md §3 -- the original's FUN_00755680 plots
// the model silhouette in color 1 at the un-raised ground projection with no
// z-test; CB-2 approximates it with a dark translucent filled diamond at the
// ground cell, sized to the hull sprite width]. Drawn at altitude 0 (extra_dy
// omitted), so it stays anchored under the raised sprite.
void draw_ground_shadow(void* sdl_renderer, double fx, double fy, int level,
                        int width_px, const Camera& cam, int view_w, int view_h);

// House palette remap for unit sprites. Unit palettes (unittem.pal) reserve
// indices 16..31 as the "remap ramp" -- the strip of colors the game
// substitutes per house so the same GI/CONS sprite reads as a different
// color for each side (the source ramp is red == Soviet). house 1 (Soviet)
// returns the ramp unchanged; house 0 (Allied) rescales each ramp entry's
// ORIGINAL brightness onto a blue ramp instead, preserving alpha. Pure
// (Palette in, Palette out) -- unit-tested directly in tests/test_world.cpp.
// Declared here (conceptually a renderer/palette helper -- draw_unit's call
// site is below) but DEFINED in engine/world.cpp, not iso_renderer.cpp --
// same terrain_screen_bounds precedent (see that declaration's comment and
// world.cpp): this file pulls in SDL and isn't part of the SDL-free
// `ra2assets_tests` binary, while world.cpp already is.
// TODO(cleanup): a third SDL-free-but-renderer-conceptual helper living here
// would be a good trigger to split these into their own small pure
// "renderer_pure.hpp/.cpp" unit instead of continuing to bounce definitions
// into world.cpp (same note already on terrain_screen_bounds).
ra2assets::Palette pal_for_house(const ra2assets::Palette& unit_pal, int house);

// [RE-GATE APPLIED: multihouse.md §2 -- 8 MP colors HSV->remap spread]. Generate
// a house-color remap from one of the 8 MP color indices (0..7). Spreads the
// color's hue across the art's reserved house-color band (indices 16..31) like
// pal_for_house, at a Value taken from each source entry's brightness. color_ix
// out of 0..7 -> unit_pal returned unchanged. Render-only; NOT read by
// world_hash. Defined in world.cpp beside pal_for_house (SDL-free TU, so the
// host tests can call it) -- see that file's declaration note. HSV triples
// (RA2 hue 0..255): 0 Gold=43,239,255  1 DarkRed=0,230,255  2 Orange=25,230,255
//   3 Magenta=221,102,255  4 Purple=201,201,189  5 DarkBlue=153,214,212
//   6 DarkSky=131,200,230  7 DarkGreen=81,200,210
ra2assets::Palette pal_for_color(const ra2assets::Palette& unit_pal, int color_ix);
// W6-05-01: the rules [Colors] ColorSchemes (HSV per entry, in INI order). A
// house colour index >= kColorSchemeBase names entry (ix - kColorSchemeBase):
// the campaign houses' map Color= [RE: HouseClass::ReadINI Color= -> a
// ColorScheme index; the remap is ColorScheme 0x68C110 = pal_for_hsv]. Set once
// by the campaign runtime before the first draw; render-only, NOT hashed.
constexpr int kColorSchemeBase = 8;
void set_color_schemes(const std::vector<std::array<int, 3>>& hsv);
int color_scheme_count();
// W3-B: the pal_for_color remap for an explicit RA2 HSV triple (0..255 each).
ra2assets::Palette pal_for_hsv(const ra2assets::Palette& unit_pal, int h, int s, int v);

// G-16 (render): the World whose 8 MP house colours the sprite draws use.
// render_world publishes its world here each frame; null -> pal_for_house.
// house_draw_palette = world->house_palette (pal_for_house when the world's
// houses are unconfigured, i.e. every gate); house_draw_key = the colour id
// for texture-cache keys.
void set_color_world(const World* w);

// W5-PRES (R-104/R-120): a per-object draw tint the object blits apply
// (draw_unit, draw_voxel_sprite, and main.cpp's building / turret blits via
// blit_object). `mod` multiplies the sprite (texture colour mod, <= 1.0),
// `add` brightens it by drawing it a second time additively scaled by add/255
// (dst + src*add/255 = src*(1 + add/255) over an opaque pixel, clamped at 255
// -- the original's light > 1.0 / Flash brightening). `alpha` < 255 draws the
// sprite translucent. The caller sets it around one object and clears it
// (null); null = the plain RenderCopy every gate uses.
struct ObjTint {
    unsigned char mod[3] = {255, 255, 255};
    unsigned char add[3] = {0, 0, 0};
    unsigned char alpha = 255;
    bool plain() const {
        return mod[0] == 255 && mod[1] == 255 && mod[2] == 255 && !add[0] && !add[1] && !add[2] && alpha == 255;
    }
};
void set_obj_tint(const ObjTint* t);
const ObjTint* obj_tint();
// RenderCopy `tex` (an SDL_Texture*) at `dst` (an SDL_Rect*) with the current ObjTint.
void blit_object(void* sdl_renderer, void* tex, const void* dst);
ra2assets::Palette house_draw_palette(const ra2assets::Palette& base, int house);
int house_draw_key(int house);
// W6-05-10: cells of the current terrain draw list that have no tile image
// (missing TMP / bad sub-tile) and are drawn as clear ground instead.
int terrain_fallback_cells();

// Draw a green isometric-diamond selection marker on the unit's tile
// (at its interpolated fx/fy position). No-op if fully off-screen. Call
// AFTER draw_terrain (so it's not painted over) and typically BEFORE
// draw_unit so the sprite sits on top of the marker.
// `width_px` (spec: "sized from the cached sprite bounds"): 0 keeps the default
// single-cell (60px) diamond footprint; a vehicle passes its hull sprite width
// so the selection diamond scales to the wider voxel sprite. Callers that pass
// nothing are unchanged.
void draw_selection(void* sdl_renderer, const Unit& unit, const Camera& cam, int view_w,
                    int view_h, int width_px = 0);

// Draw the unit's remaining ordered path as a green poly-line + node dots
// connecting the tile centers of its path cells (for visual feedback and
// demo verification). No-op if the unit has no active path.
void draw_path(void* sdl_renderer, const Unit& unit, const Camera& cam, int view_w,
               int view_h);

// Draw the live left-drag selection rectangle outline (green) in window-pixel
// space. Coordinates are the raw drag endpoints (any order). HUD overlay: call
// LAST, after terrain + units.
void draw_selection_box(void* sdl_renderer, int x0, int y0, int x1, int y1);
// G-88: health-bar thresholds as fractions (rulesmd [AudioVisual] ConditionYellow/Red).
void set_health_conditions(double yellow, double red);

// Draw a 24x3 px HP bar centered above the unit's ground point (tile center
// projected from its interpolated fx/fy, same anchor draw_selection uses),
// kHitUp + a small gap above it so it clears the selection diamond's top
// corner. A dark background rect plus a filled foreground rect scaled to
// hp/max_hp of the 24px width; green when hp > 2/3 max, yellow when > 1/3,
// red otherwise. Caller decides WHETHER to draw (selected units, or any
// damaged-but-not-dying unit -- dying units show no bar); this function does
// not check unit.selected/alive/die_time itself. No-op if max_hp <= 0 or the
// bar's rect is fully off-screen. Call after unit sprites, before the
// selection box (HUD overlay order).
// `width_px` (spec: "sized from the cached sprite bounds"): 0 keeps the default
// 24px bar; a vehicle passes its hull sprite width so the bar spans the wider
// voxel hull. Callers that pass nothing are unchanged.
void draw_hp_bar(void* sdl_renderer, const Unit& unit, const Camera& cam, int view_w, int view_h,
                 int width_px = 0);

// CB-1 Transports/Cargo: the minimal loaded-count indicator [RE:
// transport-cargo.md §1; spec §6] -- pips_max small tick rects in a row
// directly UNDER the HP bar (same tile_center_screen anchor and SDL_Rect
// fill primitive as draw_hp_bar; REUSE only, no new font/UI systems), the
// first pips_filled of them filled light, the rest dark. The fill count is
// the SIZE-SUM (cargo_total_size -- the original GetTotalSize, which is
// pip-only [RE-GATE APPLIED: transport-cargo.md §1]) while capacity is the
// COUNT, so a Size-2 passenger lights two pips yet consumes one slot; a
// fill above pips_max saturates the row. Vanilla's PipScale=Passengers pip
// row is the reference look, RE-documented but NOT pixel-matched in CB-1.
// Caller decides WHETHER to draw (a selected transport); no-op if
// pips_max <= 0.
void draw_cargo_pips(void* sdl_renderer, const Unit& unit, int pips_filled, int pips_max,
                     const Camera& cam, int view_w, int view_h);

void draw_building_hp_bar(void* sdl_renderer, const Building& b, const Camera& cam, int view_w, int view_h);

}  // namespace ra2engine

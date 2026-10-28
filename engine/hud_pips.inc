// hud_pips.inc -- W2-UI-11: PIPBRD.SHP / PIPS.SHP health bars, the original's
// selection marker. Included by main.cpp after hud_text.inc. Interactive only:
// g_pips is set by GameScene (init_presentation), so every gate / headless
// render path keeps its old drawing.
//
// [RE: game.exe TechnoClass::DrawHealthBar 0x6C3F00 (+ 0x6C4260 unit tail),
//  art loaded by FUN_005D5E00 (PIPBRD / PIPS / PIPS2) -- D:\ra2-re\w2fix-uiout]
//  * non-building, at the object's screen point (x, y):
//      selected -> PIPBRD frame 1 (infantry) at (x+11, y-25), else frame 0 at
//      (x+1, y-26), flags 0xE00 (centred);
//      pips: infantry max 8 from (x-5, y-24), others max 17 from (x-15, y-25),
//      2 px apart, PIPS frame 15 green / 16 (ratio <= ConditionYellow) /
//      17 (ratio <= ConditionRed), flags 0x600 (centred);
//      count = ftol(ratio * max), at least 1, at most max.
//  * building: N = half the screen height of the foundation's Y edge, pips
//      4 px apart stepping (-4, +2) from the upper-right end of the line;
//      the filled ones use PIPS frame 1 green / 2 yellow / 4 red, the rest
//      frame 0 (empty).
//  * palette: cache.mix/palette.pal (frames 15/16/17 decode green / yellow / red
//      under it only).
// The building line sits on the top of the building's 3-D box [RE: DrawHealth
// Bar FUN_006c3f00 building branch projects the type's Dimension2 corner
// (-w/2, +d/2, z); BuildingType Dimension2 @45D8B0 = (FoundationW * 256,
// FoundationH * 256, art Height * CellHeight), so the lift is Height levels
// = Height x 15 px (W6-05-09 / R-145: GAPOWR Height=4 really does sit that
// high); Height defaults to 2]. [RE-FALLBACK: the unit anchor is the ground
// point the clone already uses (raised by the flight altitude).]

struct PipArt {
    ra2assets::ShpFile pipbrd, pips;
    ra2assets::Palette pal;
    bool ok = false;
    ra2assets::ShpFile bomb, wrench; // cache.mix BOMBCURS/WRENCH, MOUSEPAL
    ra2assets::Palette status_pal;
    bool has_bomb = false;
    bool has_wrench = false;
    // W7-12-05 [RE: DrawPips 0x708F10 -- non-buildings draw their pip rows
    // with PIPS2.SHP]: the YR conqmd.mix copy (frames 1 green, 2 yellow,
    // 3 white, 4 red, 5 blue, 13 tall green ammo).
    ra2assets::ShpFile pips2;
    bool has_pips2 = false;
    // W7-12-22: the YR conqmd.mix PIPS.SHP (frame 13 red cross, 20 wrench).
    ra2assets::ShpFile pips_yr;
    bool has_pips_yr = false;
};
const PipArt* g_pips = nullptr;
// [AudioVisual] ConditionYellow / ConditionRed (set with g_pips by GameScene).
double g_pip_yellow = 0.5, g_pip_red = 0.25;

void pips_load(const ra2assets::MixFile& ra2) {
    static PipArt art;
    if (art.ok) { g_pips = &art; return; }
    try {
        ra2assets::MixFile conquer = ra2.open_nested("conquer.mix");
        ra2assets::MixFile cache = ra2.open_nested("cache.mix");
        const ra2assets::MixEntry* b = conquer.find(std::string("pipbrd.shp"));
        const ra2assets::MixEntry* p = conquer.find(std::string("pips.shp"));
        const ra2assets::MixEntry* pe = cache.find(std::string("palette.pal"));
        if (b && p && pe) {
            art.pipbrd = ra2assets::ShpFile::load(conquer.read(*b));
            art.pips = ra2assets::ShpFile::load(conquer.read(*p));
            const auto raw = cache.read(*pe);
            art.pal = ra2assets::Palette::load(raw.data(), raw.size());
            art.ok = art.pipbrd.count() >= 2 && art.pips.count() >= 18;
        }
        if (const auto* palette=cache.find(std::string("mousepal.pal"))) {
            const auto raw=cache.read(*palette);
            art.status_pal=ra2assets::Palette::load(raw.data(),raw.size());
            if(const auto* e=cache.find(std::string("bombcurs.shp"))) {
                art.bomb=ra2assets::ShpFile::load(cache.read(*e));art.has_bomb=art.bomb.count()>=12;
            }
            if(const auto* e=cache.find(std::string("wrench.shp"))) {
                art.wrench=ra2assets::ShpFile::load(cache.read(*e));art.has_wrench=art.wrench.count()>=7;
            }
        }
        // W7-12-05 / W7-12-22: PIPS2.SHP and the 21-frame PIPS.SHP of YR
        // (conqmd.mix), the RA2 conquer.mix PIPS2 as the fallback.
        try {
            ra2assets::MixFile md = ra2assets::MixFile::open_path(env_install_dir() + "ra2md.mix");
            ra2assets::MixFile conqmd = md.open_nested("conqmd.mix");
            if (const ra2assets::MixEntry* e = conqmd.find(std::string("pips2.shp"))) {
                art.pips2 = ra2assets::ShpFile::load(conqmd.read(*e));
                art.has_pips2 = art.pips2.count() > 5;
            }
            if (const ra2assets::MixEntry* e = conqmd.find(std::string("pips.shp"))) {
                art.pips_yr = ra2assets::ShpFile::load(conqmd.read(*e));
                art.has_pips_yr = art.pips_yr.count() > 20;
            }
        } catch (const std::exception&) {}
        if (!art.has_pips2)
            if (const ra2assets::MixEntry* e = conquer.find(std::string("pips2.shp"))) {
                art.pips2 = ra2assets::ShpFile::load(conquer.read(*e));
                art.has_pips2 = art.pips2.count() > 5;
            }
    } catch (const std::exception& ex) {
        std::cerr << "[pips] unavailable: " << ex.what() << "\n";
    }
    g_pips = art.ok ? &art : nullptr;
}

// A centred SHP draw (flags 0x200): the frame CANVAS is centred on (x, y).
inline void blit_shp_centered(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& key,
                              const ra2assets::ShpFile& shp, int frame, const ra2assets::Palette& pal, int x, int y) {
    blit_shp(ren, tc, key, shp, frame, pal, x - shp.width() / 2, y - shp.height() / 2);
}

inline int pip_frame_unit(double ratio, double yellow, double red) {
    int f = 15;
    if (ratio <= yellow) f = 16;
    if (ratio <= red) f = 17;
    return f;
}

// Selected unit: board + pips at the unit's screen point.
// W6-05-07 [RE: DrawHealthBar FUN_006c3f00 gates ONLY the PIPBRD board on
// IsSelected (+0x77); the pip row is drawn unconditionally, so a hovered,
// unselected object shows its pips without the board]: board = selected.
// W7-12-45 [RE: 0x6F5A00 adds TechnoType +0x3E0 PixelSelectionBracketDelta
// (loader 0x713513) to every y of the board and the pips].
int pres_bracket_delta(const ra2engine::Unit& u);
void draw_unit_pips(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::Unit& u, int x, int y,
                    double yellow, double red, bool board, bool health) {
    if (!g_pips || u.max_hp <= 0) return;
    const bool inf = pres_rtti_infantry(u);
    y += pres_bracket_delta(u);
    if (board && inf) blit_shp_centered(ren, tc, "pip:brd1", g_pips->pipbrd, 1, g_pips->pal, x + 11, y - 25);
    else if (board) blit_shp_centered(ren, tc, "pip:brd0", g_pips->pipbrd, 0, g_pips->pal, x + 1, y - 26);
    if(!health)return; // Keep the native selection board when health pips are disabled.
    const int max = inf ? 8 : 17;
    const double ratio = static_cast<double>(std::max(0, u.hp)) / u.max_hp;
    int n = static_cast<int>(ratio * max);
    if (n < 2) n = 1;   // FUN_0077F110 result < 2 -> 1
    if (n > max) n = max;
    const int f = pip_frame_unit(ratio, yellow, red);
    const int ox = inf ? -5 : -15, oy = inf ? -24 : -25;
    for (int i = 0; i < n; ++i)
        blit_shp_centered(ren, tc, "pip:" + std::to_string(f), g_pips->pips, f, g_pips->pal, x + ox + i * 2, y + oy);
}

// ---- W7-12-04/05/16/17/18/22: the rest of DrawPips [RE: 0x708F10] ---------
// Per-type rules keys the pip rows read (cached by rules id).
struct PresPipType {
    int pip_scale = 0;      // PipScale= (+0x3D4): 0 none, 1 Ammo, 2 Tiberium, 3 Passengers, 4 Power, 5 MindControl
    int pip_wrap = 0;       // PipWrap= (+0x3E4)
    int ammo = -1;          // Ammo= (+0x684)
    int spawns = 0;         // SpawnsNumber= (+0xD5C)
    int pip = 1;            // InfantryType Pip= (+0xDFC) [RE-FALLBACK: default green]
    int bracket = 0;        // PixelSelectionBracketDelta= (+0x3E0)
    double storage = 0.0;   // Storage=
    bool organic = false;   // Organic= (+0xD97)
};
inline const PresPipType& pres_pip_type(const std::string& id) {
    static std::map<std::string, PresPipType> cache;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    PresPipType t;
    if (g_walk_rules) {
        auto get = [&](const char* k) { return pres_lower(pres_clean(g_walk_rules->get(id, k))); };
        const std::string ps = get("PipScale");
        t.pip_scale = ps == "ammo" ? 1 : ps == "tiberium" ? 2 : ps == "passengers" ? 3 : ps == "power" ? 4
                    : ps == "mindcontrol" ? 5 : 0;
        t.pip_wrap = std::atoi(get("PipWrap").c_str());
        const std::string am = get("Ammo");
        t.ammo = am.empty() ? -1 : std::atoi(am.c_str());
        t.spawns = std::atoi(get("SpawnsNumber").c_str());
        const std::string pp = get("Pip");
        // [RE: Pip enum table 0x81A968] green 1, yellow 2, white 3, red 4,
        // blue 5, persongreen 7, personyellow 8.
        if (pp == "green") t.pip = 1;
        else if (pp == "yellow") t.pip = 2;
        else if (pp == "white") t.pip = 3;
        else if (pp == "red") t.pip = 4;
        else if (pp == "blue") t.pip = 5;
        else if (pp == "persongreen") t.pip = 7;
        else if (pp == "personyellow") t.pip = 8;
        t.bracket = std::atoi(get("PixelSelectionBracketDelta").c_str());
        t.storage = std::atof(get("Storage").c_str());
        const std::string og = get("Organic");
        t.organic = !og.empty() && (og[0] == 'y' || og[0] == 't' || og[0] == '1');
    }
    return cache.emplace(id, t).first->second;
}
int pres_bracket_delta(const ra2engine::Unit& u) { return pres_pip_type(pres_type_id(u)).bracket; }

inline void pip2(SDL_Renderer* ren, ra2engine::TextureCache& tc, int frame, int x, int y) {
    if (!g_pips || !g_pips->has_pips2 || frame < 0 || frame >= g_pips->pips2.count()) return;
    blit_shp_centered(ren, tc, "pip2:" + std::to_string(frame), g_pips->pips2, frame, g_pips->pal, x, y);
}

// The pip rows under the unit and the self-heal / group-number marks, for a
// selected or hovered object the player owns [RE: DrawHealthBar 0x6F5A00
// tail -> DrawPips 0x708F10 at loc = (x-10, y+10)].
//  * spawns: SpawnsNumber pips, frame 1 while the spawn is docked/ready
//    (SpawnManager FUN_006B73D0), else 0 -- from (loc.x-5, loc.y), 4 px apart;
//  * Passengers >= 1: `Passengers` slots, filled from the RIGHT end per
//    passenger (most recent first): an infantry passenger takes Size-1
//    white (3) pips then its Pip=, a vehicle Size-1 white then blue (5); a
//    Gunner type draws the first pip alone and the rest 2 slots on; nothing
//    when the cargo's total size exceeds the slots;
//  * else PipScale=Tiberium: GetPipMax (0x715630: 5 for a unit) slots,
//    gems blue (5) first, then ore yellow (2), then empty (0);
//  * PipScale=Ammo, PipWrap=0: the CURRENT ammo as tall green pips (frame
//    13) at y-3; PipWrap>0: PipWrap pips of frame 15+level / 14;
//  * the group number (Group +0x214, 1..9, 10 -> 0) in a black box with a
//    house-colour border, house-colour digit, at loc + (-4, -39), +3 when not
//    infantry;
//  * the Tech Hospital cross (YR PIPS.SHP 13) at row + (19,-35) on infantry /
//    Organic units, the Machine Shop wrench (20) at row + (38,-32) on other
//    units, while the house owns one; it flashes (0x601) for 6 frames of
//    every SelfHeal*Frames while damaged.
void draw_unit_overlays(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& world,
                        const std::vector<ra2engine::Unit>& us, const ra2engine::Unit& u, int x, int y, int group,
                        SDL_Color house, int inf_heal, int unit_heal, int heal_inf_frames, int heal_unit_frames) {
    if (!g_pips) return;
    const PresPipType& pt = pres_pip_type(pres_type_id(u));
    const bool inf = pres_rtti_infantry(u);
    const int lx = x - 10, ly = y + 10;
    int rx = lx - 5 + (inf ? 11 : 0), ry = ly;
    const int step = 4;
    // Spawn pips (Carrier / Dreadnought / V3 / Destroyer).
    if (pt.spawns > 0) {
        const int ready = std::clamp(u.an.spawn_ready, 0, pt.spawns);
        for (int i = 0; i < pt.spawns; ++i) pip2(ren, tc, i < ready ? 1 : 0, rx + i * step, ry);
    }
    if (u.passengers_max > 0) {
        const int max = u.passengers_max;
        std::vector<int> slot(static_cast<std::size_t>(max), 0);
        int total = 0;
        for (int p : u.cargo)
            if (p >= 0 && p < static_cast<int>(us.size())) total += std::max(1, us[static_cast<std::size_t>(p)].size);
        int k = total - 1;
        if (k < max) {
            for (auto it = u.cargo.rbegin(); it != u.cargo.rend() && k >= 0; ++it) {
                if (*it < 0 || *it >= static_cast<int>(us.size())) continue;
                const ra2engine::Unit& pu = us[static_cast<std::size_t>(*it)];
                const int extra = std::max(1, pu.size) - 1;
                for (int e = 0; e < extra && k >= 0; ++e) slot[static_cast<std::size_t>(k--)] = 3;
                if (k < 0) break;
                if (pres_rtti_infantry(pu)) slot[static_cast<std::size_t>(k--)] = pres_pip_type(pres_type_id(pu)).pip;
                else if (pu.is_vehicle) slot[static_cast<std::size_t>(k--)] = 5;
                else slot[static_cast<std::size_t>(k--)] = 1;
            }
            int sx = 0;
            for (int i = 0; i < max; ++i) {
                pip2(ren, tc, slot[static_cast<std::size_t>(i)], rx + sx * step, ry);
                sx += (i == 0 && u.abil.gunner) ? 2 : 1;
            }
        }
    } else if (pt.pip_scale == 2) {
        const int max = 5;   // GetPipMax 0x715630 case Tiberium (a UnitType)
        const double st = pt.storage > 0.0 ? pt.storage : std::max(1, u.storage_bales);
        int gems = static_cast<int>(u.bales_gems / st * max);
        int ore = static_cast<int>(u.bales_ore / st * max);
        for (int i = 0; i < max; ++i) {
            int f = 0;
            if (gems > 0) { --gems; f = 5; }
            else if (ore > 0) { --ore; f = 2; }
            pip2(ren, tc, f, rx + i * step, ry);
        }
    } else if (pt.pip_scale == 1) {
        const int max = std::max(0, pt.ammo);
        const int cur = std::clamp(u.an.ammo, 0, max);
        if (pt.pip_wrap <= 0) {
            for (int i = 0; i < cur; ++i) pip2(ren, tc, 13, rx + i * step, ry - 3);
        } else {
            const int layers = max / pt.pip_wrap - 1;
            for (int i = 0; i < pt.pip_wrap; ++i) {
                int f = 14;
                for (int l = layers; l >= 0; --l)
                    if (l * pt.pip_wrap + i < cur) { f = 15 + l; break; }
                pip2(ren, tc, f, rx + i * step, ry - 3);
            }
        }
    }
    // Self-heal mark.
    if (g_pips->has_pips_yr) {
        const bool organic = inf || pt.organic;
        const int count = organic ? inf_heal : unit_heal;
        if (count > 0) {
            const int frames = organic ? heal_inf_frames : heal_unit_frames;
            const bool flash = frames > 0 && static_cast<long>(world.ability_frame() % static_cast<std::uint64_t>(frames)) < 6 &&
                               u.hp < u.max_hp;
            const int f = organic ? 13 : 20;
            const int hx = lx + (organic ? 19 : 38), hy = ly + (organic ? -35 : -32);
            if (flash)
                blit_shp_alpha(ren, tc, "pipyr:" + std::to_string(f), g_pips->pips_yr, f, g_pips->pal,
                               hx - g_pips->pips_yr.width() / 2, hy - g_pips->pips_yr.height() / 2, 128);
            else
                blit_shp_centered(ren, tc, "pipyr:" + std::to_string(f), g_pips->pips_yr, f, g_pips->pal, hx, hy);
        }
    }
    // Group number.
    if (group > 0) {
        const std::string digit = std::to_string(group % 10);
        const int tx = lx - 4, ty = ly - 39 + (inf ? 0 : 3);
        const int w = hud_text_width(digit) + 4, h = 11;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_Rect box{tx - 2, ty - 1, w, h};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderFillRect(ren, &box);
        SDL_SetRenderDrawColor(ren, house.r, house.g, house.b, 255);
        SDL_RenderDrawRect(ren, &box);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        hud_text(ren, tc, digit, tx, ty - 3, house, kHudLeft, false, false);
    }
}

// Selected building: the diagonal pip line along the foundation's top-left
// edge. (lx, ly) = the foundation's LEFT corner, (tx, ty) = its TOP corner.
void draw_building_pips(SDL_Renderer* ren, ra2engine::TextureCache& tc, int hp, int max_hp, int tx, int ty,
                        int fh_cells, int lift_px, double yellow, double red) {
    if (!g_pips || max_hp <= 0) return;
    const int n = std::max(1, fh_cells * 15 / 2);   // (screen dy of the Y edge) / 2
    const double ratio = static_cast<double>(std::max(0, hp)) / max_hp;
    int filled = static_cast<int>(ratio * n);
    if (filled < 2) filled = 1;
    if (filled > n) filled = n;
    int f = 1;
    if (ratio <= yellow) f = 2;
    if (ratio <= red) f = 4;
    // i = 0 at the upper-right (top corner) end, stepping (-4, +2).
    // RE: x = loc + s1.x + 3 + 4(N-i), y = loc + s1.y + 4 - 2(N-i), s1 = the left
    // corner of the box top -> i = 0 sits at the top corner (+3, +4).
    const int x0 = tx + 3, y0 = ty + 4 - lift_px;
    for (int i = 0; i < n; ++i) {
        const int fr = i < filled ? f : 0;
        blit_shp_centered(ren, tc, "pipb:" + std::to_string(fr), g_pips->pips, fr, g_pips->pal, x0 - 4 * i,
                          y0 + 2 * i);
    }
}

// W6-04-11: see the declaration in main.cpp.
void draw_bomb_icons(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& world,
                     const std::vector<ra2engine::Unit>& us, const ra2engine::Camera& cam) {
    if (g_pips) {
        const std::uint64_t now = world.ability_frame();
        const int duration=world.ability_rules().ivan_timed_delay;
        const auto rate=g_walk_rules?pres_clean(g_walk_rules->get("AudioVisual","IvanIconFlickerRate")):std::string();
        const int flicker=rate.empty()?8:std::max(1,std::atoi(rate.c_str()));
        auto bomb_frame = [&](std::int64_t det) {
            return ra2engine::attached_bomb_clock(now,det,duration,flicker);
        };
        auto bomb_visible=[&](int planter_house,int x,int y,int z) {
            if(planter_house==g_local_house)return true;
            if(!g_walk_rules)return false;
            // Rendering-only detector query. The native retained 46-frame
            // visibility refresh is not modelled here; never show every
            // enemy bomb merely because its carrier is visible.
            for(const auto& detector:us) {
                if(!detector.alive || detector.die_time>=0 || detector.cargo_of>=0 || detector.house!=g_local_house)continue;
                const auto& id=detector.tg.t.rules_id.empty()?detector.image:detector.tg.t.rules_id;
                const int sight=std::atoi(pres_clean(g_walk_rules->get(id,"BombSight")).c_str());
                if(sight<=0)continue;
                const double dx=ra2engine::unit_lept_x(detector)-double(x),dy=ra2engine::unit_lept_y(detector)-double(y),
                    dz=ra2engine::unit_lept_z(detector)-double(z),radius=sight*256.0;
                if(dx*dx+dy*dy+dz*dz<radius*radius)return true;
            }
            return false;
        };
        for (const ra2engine::Unit& u : us) {
            if (!g_pips->has_bomb || bomb_frame(u.bomb_frame)<0 || !u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            if (world.unit_hidden_from(g_local_house, u) ||
                (u.house!=g_local_house && !world.object_visible_at(g_local_house,u.cell_x,u.cell_y))) continue;
            if(!bomb_visible(u.bomb_house,ra2engine::unit_lept_x(u),ra2engine::unit_lept_y(u),ra2engine::unit_lept_z(u)))continue;
            const int up = (u.is_aircraft && u.altitude > 0.0) ? ra2engine::altitude_to_screen_dy(u.altitude) : 0;
            const double ufx = u.fx + u.sub_x / 256.0, ufy = u.fy + u.sub_y / 256.0;
            const int sx = static_cast<int>(std::lround((ufx - ufy) * 30.0)) + cam.x + 30;
            const int sy = static_cast<int>(std::lround((ufx + ufy) * 15.0 - u.level * 15)) + cam.y + 15 - up;
            blit_shp_centered(ren, tc, "w6bomb:" + std::to_string(bomb_frame(u.bomb_frame)), g_pips->bomb,
                              bomb_frame(u.bomb_frame), g_pips->status_pal, sx, sy);
        }
        for (const ra2engine::Building& b : world.buildings()) {
            if (!b.alive || b.state == ra2engine::Building::State::Dead ||
                (b.house!=g_local_house && !world.object_visible_at(g_local_house,b.cx,b.cy))) continue;
            const auto point=ra2engine::building_draw_point(b.cx,b.cy,world.cell_level(b.cx,b.cy));
            int bx=0,by=0,bz=0;ra2engine::building_aim_lept(b,world.cell_level(b.cx,b.cy),bx,by,bz);
            if(g_pips->has_bomb && bomb_frame(b.bomb_frame)>=0 && bomb_visible(b.bomb_house,bx,by,bz))
                blit_shp_centered(ren,tc,"w6bomb:"+std::to_string(bomb_frame(b.bomb_frame)),g_pips->bomb,
                    bomb_frame(b.bomb_frame),g_pips->status_pal,point.x+cam.x,point.y+cam.y);
            if (b.c4_applied || b.c4_frame >= 0) {
                if (g_pips && g_pips->has_bomb) {
                    blit_shp_centered(ren, tc, "c4-charge", g_pips->bomb, 0, g_pips->status_pal, point.x + cam.x, point.y + cam.y - 12);
                }
            }
            if(g_pips->has_wrench && b.being_repaired) {
                const int frame=ra2engine::repair_wrench_frame(world.repair_frame());
                blit_shp_centered(ren,tc,"repair-wrench:"+std::to_string(frame),g_pips->wrench,
                    frame,g_pips->status_pal,point.x+cam.x,point.y+cam.y);
            }
        }
    }
}

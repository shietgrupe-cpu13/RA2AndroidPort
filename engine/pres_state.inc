// pres_state.inc -- W5-PRES R-120: object state effects of the real game --
// the Iron Curtain brightness envelope, the elite-promotion flash and the
// chrono warp (translucent while warping, WARPOUT at both ends, CHRONOSK
// sparkles). Included by main.cpp before render_world (which asks
// pres_unit_tint / pres_building_tint per object); the per-tick half runs from
// Presentation::logic_tick. Render-only: nothing here touches the World.
// [RE: docs/re/w5-presentation.md §10-12]

struct PresStateFx {
    std::vector<std::pair<int, int>> prev_cell;   // per unit (cell_x, cell_y) last tick
    std::vector<char> prev_warp;
};
PresStateFx g_state_fx;
const ra2engine::World* g_state_world = nullptr;   // GameScene's world (null in gates)
int g_iron_frames = 750;                            // [SuperWeapon] IronCurtainDuration

// [RE: TechnoClass IC tint stages 0x70D9D0 / scale 0x70D7B0]: brightness
// multiplier f/256 by the frames elapsed `t` and the frames left `L` of the
// curtain. Stages 1..3 (flash up to 2.0x, decay to 0.2x over ~30 frames), a
// 24-frame 0.2x..0.5x pulse while L >= 54, 0.2x until L < 31, then the 30-frame
// flash back to 1.0x. Stage 3's +-5 random length is taken as 20 [RE-FALLBACK].
inline int pres_iron_f256(long t, long L) {
    if (L < 31) {
        const long s = 30 - std::max(0L, L);
        if (s < 6) { const long rem = 6 - s; return static_cast<int>((3072 - 461 * rem) / 6); }
        if (s < 10) return 512;
        const long rem = std::max(0L, 20 - (s - 10));
        return static_cast<int>((rem + 20) * 256 / 20);
    }
    if (t < 6) { const long rem = 6 - t; return static_cast<int>((12 - rem) * 256 / 6); }
    if (t < 10) return 512;
    if (t < 30) { const long rem = 20 - (t - 10); return static_cast<int>((rem * 461 + 1020) / 20); }
    if (L < 54) return 51;
    const long u = (t - 30) % 24;
    if (u < 8) { const long rem = 8 - u; return static_cast<int>((1024 - 77 * rem) / 8); }
    const long rem = 16 - (u - 8);
    return static_cast<int>((77 * rem + 816) / 16);
}

// Brightness (per mille, 1000 = normal, cap 2000) into an ObjTint.
inline void pres_tint_brightness(ra2engine::ObjTint& t, int b) {
    b = std::clamp(b, 0, 2000);
    const int m = std::min(b, 1000) * 255 / 1000;
    const int a = std::max(0, b - 1000) * 255 / 1000;
    t.mod[0] = t.mod[1] = t.mod[2] = static_cast<unsigned char>(m);
    t.add[0] = t.add[1] = t.add[2] = static_cast<unsigned char>(a);
}

inline bool pres_unit_warping(const ra2engine::World& w, const ra2engine::Unit& u) {
    return u.an.chrono_left_frames > 0.0 || u.warp_remaining > 0 ||
           (u.warp_until >= 0 && static_cast<std::int64_t>(w.ability_frame()) < u.warp_until);
}

// The draw tint of unit `idx` (false = plain draw).
inline bool pres_unit_tint(const ra2engine::World& w, int idx, ra2engine::ObjTint& out) {
    if (!g_state_world || &w != g_state_world) return false;
    const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(idx)];
    int b = 1000;
    bool any = false;
    // W5-PRES R-104: the object's own height light (cell height, +4 levels on
    // a bridge deck) relative to the ground-level light pass.
    if (g_light) {
        const double rel = g_light->object_rel(u.level + pres_unit_lift_levels(w, u));
        if (std::abs(rel - 1.0) > 1e-3) { b = static_cast<int>(std::lround(b * rel)); any = true; }
    }
    if (u.an.iron_left_frames > 0.0) {   // [RE: 0x70D7B0] brightness * f / 256
        const long L = u.an.iron_left_frames;
        const long t = std::max(0L, static_cast<long>(g_iron_frames) - L);
        b = std::min(2000, b * pres_iron_f256(t, L) / 256);
        any = true;
    }
    // [RE: FlashData::Update 0x4CC690, Techno 0x70C610]: while (counter & 2)
    // the brightness becomes b <= 1500 ? 2000 : 500 -- the elite promotion
    // (EliteFlashTimer, the sim's vet.elite_flash) and trigger FlashTeam (104).
    long counter = u.vet.elite_flash;
    auto ft = g_flash_units.find(idx);
    if (ft != g_flash_units.end()) counter = std::max(counter, ft->second - static_cast<long>(w.vet_frame()));
    if (counter > 0 && (counter & 2)) { b = b <= 1500 ? 2000 : 500; any = true; }
    pres_tint_brightness(out, b);
    if(w.unit_berserk(idx) && u.an.iron_left_frames<=0) {
        const auto& rgb=w.chaos_state().tint;
        for(int channel=0;channel<3;++channel)
            out.add[channel]=static_cast<unsigned char>(std::min(255,int(out.add[channel])+rgb[channel]));
        any=true;
    }
    if (pres_unit_warping(w, u)) {   // [RE: 0x705290 / 0x705AD0] Translucent50 while warping
        out.alpha = 128;
        any = true;
    }
    return any;
}

inline bool pres_building_tint(const ra2engine::World& w, const ra2engine::Building& b, ra2engine::ObjTint& out) {
    if (!g_state_world || &w != g_state_world) return false;
    int br = 1000;
    bool any = false;
    if (g_light && g_map_dressing && g_map_dressing->map()) {   // W5-PRES R-104 height light
        const ra2assets::MapCell* mc = g_map_dressing->map()->at(b.cx, b.cy);
        const double rel = g_light->object_rel(mc ? mc->level : 0);
        if (std::abs(rel - 1.0) > 1e-3) { br = static_cast<int>(std::lround(br * rel)); any = true; }
    }
    if (b.iron_left_frames > 0.0) {
        const long L = b.iron_left_frames;
        const long t = std::max(0L, static_cast<long>(g_iron_frames) - L);
        br = std::min(2000, br * pres_iron_f256(t, L) / 256);
        any = true;
    }
    // [RE: building flash 0x456F80]: +-500 while (counter & 2) (trigger 131).
    auto ft = g_flash_buildings.find(static_cast<int>(&b - w.buildings().data()));
    if (ft != g_flash_buildings.end()) {
        const long c = ft->second - static_cast<long>(w.vet_frame());
        if (c > 0 && (c & 2)) { br = br <= 1500 ? br + 500 : br - 500; any = true; }
    }
    if (any) pres_tint_brightness(out, br);
    return any;
}

// Per logic tick: WARPOUT at both ends of a teleport, CHRONOSK every 24 frames
// while warping [RE: teleport loco 0x718600, Unit AI 0x7352E3], elite flash
// [RE: TechnoClass::AI 0x6F9380].
void pres_state_tick(const ra2engine::World& w, PresFx& fx) {
    g_state_world = &w;
    g_iron_frames = std::max(1, w.sw_params().iron_frames);
    const auto& us = w.units();
    g_state_fx.prev_cell.resize(us.size(), {-1, -1});
    g_state_fx.prev_warp.resize(us.size(), 0);
    for (std::size_t i = 0; i < us.size(); ++i) {
        const ra2engine::Unit& u = us[i];
        auto& pc = g_state_fx.prev_cell[i];
        const bool warp = u.alive && pres_unit_warping(w, u);
        if (u.alive && u.cargo_of < 0 && pc.first >= 0 && warp &&
            std::max(std::abs(u.cell_x - pc.first), std::abs(u.cell_y - pc.second)) >= 2) {
            auto level_at = [&](int x, int y) {
                const ra2assets::MapCell* mc = g_map_dressing && g_map_dressing->map() ? g_map_dressing->map()->at(x, y)
                                                                                        : nullptr;
                return mc ? static_cast<int>(mc->level) : u.level;
            };
            if (w.object_visible_at(g_local_house, pc.first, pc.second))
                fx.spawn("WARPOUT", pc.first, pc.second, level_at(pc.first, pc.second), 0.0);
            if (w.object_visible_at(g_local_house, u.cell_x, u.cell_y))
                fx.spawn("WARPOUT", u.cell_x, u.cell_y, u.level, 0.0);
        }
        if (warp && g_pres_tick % 24 == 0 && w.object_visible_at(g_local_house, u.cell_x, u.cell_y))
            fx.spawn("CHRONOSK", u.fx + 120.0 / 256.0, u.fy + 120.0 / 256.0, u.level, 0.0);
        pc = {u.cell_x, u.cell_y};
        // R-122: a chute opens on the tick its paratrooper starts to fall.
        if (u.alive && pres_para_alt(u) > 0.0) {
            PresChute& c = g_chutes[static_cast<int>(i)];
            if (c.start < 0 || c.landed >= 0) c = PresChute{g_pres_tick, -1};
        }
        g_state_fx.prev_warp[i] = warp ? 1 : 0;
    }
}

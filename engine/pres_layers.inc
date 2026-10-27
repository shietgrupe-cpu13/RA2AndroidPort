// pres_layers.inc -- W5-PRES R-100 / R-101: render layers for the real game.
// Included by main.cpp after pres_map.inc (needs g_map_dressing).
//
// Ground layer: one depth sort (key = cell x + y of the object's centre) over
// buildings, map objects, ground units AND the high-bridge decks:
//   * a deck at cell (x, y) sorts at x + y + kPresDeckBias, after every ground
//     object in or behind its cell -> a ship / unit UNDER the deck is covered
//     by the deck above it and drawn over the decks behind it;
//   * a ground unit ON a deck (a non-naval unit on a deck cell over water)
//     draws BridgeHeight higher and sorts at fx + fy + kPresOnDeckBias, after
//     its own deck and before the next one in front.
// Air layer: airborne aircraft / jumpjets draw after the whole ground layer
// (sorted by fx + fy among themselves), so a plane over a building is never
// hidden behind it. [RE: docs/re/w5-presentation.md §1-2]

constexpr double kPresDeckBias = 0.99;
constexpr double kPresOnDeckBias = 1.001;
// BridgeHeight = 416 leptons = 4 cell levels (104 leptons / 15 px each)
// [RE: world-depth.md B3; docs/re/w5-presentation.md §1].
constexpr int kPresBridgeLevels = 4;

inline bool pres_airborne(const ra2engine::Unit& u) { return u.is_aircraft && u.altitude > 0.0; }

// A ground unit standing on a high-bridge deck.
inline bool pres_on_high_deck(const ra2engine::World& w, const ra2engine::Unit& u) {
    if (!g_map_dressing || !g_map_dressing->layer_decks()) return false;
    // Artwork lives at its anchor; the structural deck also includes the
    // neighboring cells stamped by that anchor. Use the simulation authority
    // so a vehicle in an outer lane keeps its bridge height and depth sort.
    return ra2engine::unit_on_high_bridge(w, u);
}

// The unit's ground-layer sort key.
inline double pres_unit_key(const ra2engine::World& w, const ra2engine::Unit& u) {
    return ra2engine::deck_unit_depth(u.fx,u.fy,u.cell_x,u.cell_y,pres_on_high_deck(w,u));
}

// Levels the draw position is raised by (bridge deck).
inline int pres_unit_lift_levels(const ra2engine::World& w, const ra2engine::Unit& u) {
    return pres_on_high_deck(w, u) ? kPresBridgeLevels : 0;
}

inline double pres_ground_level(const ra2engine::World& w,const ra2engine::Unit& u) {
    return ra2engine::moving_ground_level(w,u);
}

// w8fix-layer W8-02-03 [RE: an OnBridge object's Z includes BridgeHeight
// (0x51F810), and anims are created at the object's coords]: the level an
// effect of this unit (muzzle flash, beam end, impact, death anim, corpse)
// is drawn at -- the deck's for a unit standing on one.
inline int pres_unit_fx_level(const ra2engine::World& w, const ra2engine::Unit& u) {
    return u.level + pres_unit_lift_levels(w, u);
}

// W5-PRES R-122 [RE: ObjectClass::SpawnParachuted 0x5F5150, SetOwnerObject
// 0x424AD0, fall 0x5F3680; docs/re/w5-presentation.md §11]: PARACH is created
// at the paratrooper's coords + 75 leptons Z (~11 px up) and follows it; it
// plays its opening frames (Start..LoopStart) then loops LoopStart..LoopEnd
// while falling; on touchdown the loop is released and the tail frames
// (LoopEnd..End) play out on the ground. AltPalette = the owner's remap.
struct PresChute { long start = -1; long landed = -1; double fx = 0, fy = 0; int level = 0, house = 0; };
std::map<int, PresChute> g_chutes;
inline void pres_chute_blit(SDL_Renderer* ren, ra2engine::TextureCache& tc, const FxAnimType& t, int fr, double fx,
                            double fy, int level, int house, const ra2engine::Camera& cam, int W, int H) {
    const int cx = static_cast<int>(std::lround((fx - fy) * 30.0)) + cam.x + 30;
    const int cy = static_cast<int>(std::lround((fx + fy) * 15.0 - level * 15)) + cam.y + 15 - 11 + t.ydraw;
    g_fx->draw_anim_frame(ren, tc, t, fr, cx, cy, W, H, t.altpal ? g_fx->alt_palette(house) : nullptr,
                          t.altpal ? ":h" + std::to_string(house) : std::string());
}
inline void pres_draw_parachute(SDL_Renderer* ren, ra2engine::TextureCache& tc, int idx, const ra2engine::Unit& at,
                                const ra2engine::Camera& cam, int W, int H) {
    if (!g_fx) return;
    const FxAnimType* t = g_fx->type("PARACH");
    if (!t) return;
    PresChute& c = g_chutes[idx];
    if (c.start < 0 || c.landed >= 0) c = PresChute{g_pres_tick, -1};   // (pres_state_tick registers it first)
    c.fx = at.fx; c.fy = at.fy; c.level = at.level; c.house = at.house;
    const long step = (g_pres_tick - c.start) / std::max(1, t->delay);
    const int open = t->loop_start - t->start, span = std::max(1, t->loop_end - t->loop_start);
    const int fr = step < open ? static_cast<int>(t->start + step)
                               : static_cast<int>(t->loop_start + (step - open) % span);
    pres_chute_blit(ren, tc, *t, fr, at.fx, at.fy, at.level, at.house, cam, W, H);
}
// After the falling pass: chutes whose paratrooper landed play their tail.
inline void pres_draw_landed_chutes(const ra2engine::World& w, SDL_Renderer* ren, ra2engine::TextureCache& tc,
                                    const ra2engine::Camera& cam, int W, int H) {
    if (!g_fx || g_chutes.empty()) return;
    const FxAnimType* t = g_fx->type("PARACH");
    for (auto it = g_chutes.begin(); it != g_chutes.end();) {
        PresChute& c = it->second;
        const bool falling = it->first < static_cast<int>(w.units().size()) && pres_para_alt(w.units()[it->first]) > 0.0;
        if (falling) { ++it; continue; }
        if (c.landed < 0) {
            c.landed = g_pres_tick;
            const ra2engine::Unit& u = w.units()[it->first];
            if (u.alive) { c.fx = u.fx; c.fy = u.fy; c.level = u.level; }
        }
        const long step = (g_pres_tick - c.landed) / std::max(1, t ? t->delay : 1);
        const int fr = t ? static_cast<int>(t->loop_end + step) : 0;
        if (!t || fr >= t->end) { it = g_chutes.erase(it); continue; }
        pres_chute_blit(ren, tc, *t, fr, c.fx, c.fy, c.level, c.house, cam, W, H);
        ++it;
    }
}

// W5-PRES (item 4) [RE: JumpjetLocomotionClass movement 0x54CDA0 (ILocomotion
// vtable 0x7EBD68), docs/re/w5-presentation.md §13]: a hovering jumpjet's
// target height swings sin(phase) * Deviation leptons, phase += 2pi /
// (15 / WobblesPerSecond) per frame, unless JumpjetNoWobbles. Type keys
// JumpjetWobbles / JumpjetDeviation / JumpjetNoWobbles, defaults [JumpjetControls]
// WobblesPerSecond / WobbleDeviation. Render-only here: the screen lift of the
// wobble (the sim keeps its altitude) [RE-FALLBACK: the per-unit phase starts
// at 0 on spawn in the original; the clone offsets it by the unit index].
inline int pres_jumpjet_wobble_px(int idx, const ra2engine::Unit& u) {
    if (!g_walk_rules || !u.is_aircraft || u.an.t.fixed_wing || u.altitude <= 0.0 || u.an.crashing) return 0;
    struct Wob { double per_sec = 0.15; double dev = 40.0; bool off = false; };
    static std::map<std::string, Wob> cache;
    static std::uint64_t cache_epoch=0;
    if(cache_epoch!=g_pres_epoch) { cache.clear();cache_epoch=g_pres_epoch; }
    // W7-05-08: the keys live in the RULES id's section ([JUMPJET], not [ROCK]).
    auto it = cache.find(pres_type_id(u));
    if (it == cache.end()) {
        auto num = [&](const char* sname, const char* k, double d) {
            const std::string v = pres_clean(g_walk_rules->get(sname, k));
            return v.empty() ? d : std::atof(v.c_str());
        };
        Wob w;
        w.per_sec = num(pres_type_id(u).c_str(), "JumpjetWobbles", num("JumpjetControls", "WobblesPerSecond", 0.15));
        w.dev = num(pres_type_id(u).c_str(), "JumpjetDeviation", num("JumpjetControls", "WobbleDeviation", 40.0));
        const std::string nw = pres_lower(pres_clean(g_walk_rules->get(pres_type_id(u), "JumpjetNoWobbles")));
        w.off = !nw.empty() && (nw[0] == 'y' || nw[0] == 't' || nw[0] == '1');
        it = cache.emplace(pres_type_id(u), w).first;
    }
    const Wob& w = it->second;
    if (w.off || w.per_sec <= 0.0 || w.dev == 0.0) return 0;
    const double phase = (g_pres_tick + idx * 37L) * 6.283185307179586 / (15.0 / w.per_sec);
    const double lep = std::sin(phase) * w.dev;
    return static_cast<int>(std::lround(lep / 256.0 * 36.74));
}

// The cell's TMP ramp type (image header byte 42, XCC t_tmp_image_header
// ramp_type), 0 = flat. W5-PRES: PLACE.SHP blocked frames, ore slope art.
Assets* g_pres_assets = nullptr;   // render_world's assets (live game + gates)
inline int pres_cell_ramp(const Assets& a, int x, int y) {
    const ra2assets::MapCell* mc = a.map.at(x, y);
    if (!mc) return 0;
    const ra2assets::TmpFile* tmp = const_cast<Assets&>(a).theater.tmp_for_tile(mc->tile);
    if (!tmp || mc->subtile >= tmp->count()) return 0;
    const uint8_t* h = tmp->image_header(mc->subtile);
    return h ? h[42] : 0;
}
inline int pres_unit_ramp(const ra2engine::Unit& u) {
    if(!g_pres_assets || !u.is_vehicle || u.is_aircraft || u.is_naval || u.on_deck || u.altitude>0 || u.an.t.missile)
        return 0;
    const auto kind=ra2engine::unit_loco_kind(u);
    if(kind!=ra2engine::LocoKind::Drive && kind!=ra2engine::LocoKind::Teleport) return 0;
    int ramp=pres_cell_ramp(*g_pres_assets,u.cell_x,u.cell_y);
    if(u.moving() && u.path.size()>1) {
        const auto first=std::max<std::size_t>(1,std::min(u.path_idx,u.path.size()-1));
        double best=0.75*0.75;
        for(std::size_t k=first;k<u.path.size() && k<=first+1;++k) {
            const auto a=u.path[k-1],b=u.path[k];
            const double dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
            if(len<=0 || std::abs(dx)>1 || std::abs(dy)>1)continue;
            const double f=std::clamp(((u.fx-a.x)*dx+(u.fy-a.y)*dy)/len,0.0,1.0);
            const double ex=u.fx-a.x-f*dx,ey=u.fy-a.y-f*dy,d=ex*ex+ey*ey;
            if(d>best)continue;
            best=d;ramp=ra2engine::terrain_tilt_transition(pres_cell_ramp(*g_pres_assets,a.x,a.y),
                                                       pres_cell_ramp(*g_pres_assets,b.x,b.y),f);
        }
    }
    return ramp;
}

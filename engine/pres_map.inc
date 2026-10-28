// pres_map.inc -- G-87 map dressing in the real game: the map's [Terrain]
// objects (trees, rocks, lamp posts), non-ore [OverlayPack] overlays (walls,
// fences, sandbags, bridges), [Smudge] craters/scorch marks and the neutral
// [Structures] (tech buildings join the world as neutral buildings; civilian
// eye-candy is drawn + blocks its foundation). Formats per maps.md §4.5/§4.7
// (packed cell 1000*y+x; Smudge/Structures "TYPE,x,y,..."/"HOUSE,TYPE,HP,x,y").
// Art resolves through the theater candidates (Theater=yes -> .tem/.sno/.urb,
// NewTheater buildings -> munged .shp), conquer.mix last.
// Included by main.cpp BEFORE render_world (which calls the draw hooks).

class PresMap {
public:
    struct Obj {
        std::shared_ptr<ra2assets::ShpFile> shp;
        int x = 0, y = 0, fw = 1, fh = 1, level = 0, frame = 0;
        int pal = 0;   // 0 iso/terrain palette, 1 theater (overlay) palette, 2 unit palette
        std::string key;
        std::shared_ptr<ra2assets::Palette> own_pal;   // W3-FIX-A art Palette= (no remap)
        // W4-FIX-POS draw anchor (the SHP canvas centre) [RE: docs/gap/wave4/
        // 01-positioning.md §1]: terrain objects at the cell centre (TerrainClass
        // Location); civilian buildings and smudges at the origin cell's top
        // vertex (building_draw_point / FUN_00480260); overlays at the cell
        // centre + the per-type yoff (FUN_005fd4b0/FUN_00480020).
        enum class Anchor { CellCentre, TopVertex, Overlay };
        Anchor anchor = Anchor::CellCentre;
        int yoff = 0;           // Anchor::Overlay only
        int ovl_id = -1;        // G16: runtime overlay index (live bridge state lookup)
        // W2-GA-06/08 [RE: TerrainClass::Draw_It FUN_0071b450]: terrain objects
        // draw their shadow half (frame + Frames/2, Darken) -- DAT_00821cf1 is a
        // constant-true gate (RE-Q1); SpawnsTiberium types (ore mines) draw
        // through the UNIT palette convert 0x87E63C 16 px higher, and
        // IsAnimated types step frames at AnimationRate.
        bool shadow = false;
        int lift = 0;                 // screen px up
        bool animated = false;
        int anim_rate = 1;
        double anim_prob = 0.0;
        // Render-only animation state (never touches the sim).
        mutable int anim_stage = -1;  // -1 idle, else current frame
        mutable int anim_timer = 0;
        mutable uint32_t anim_rng = 0;
        // W2-GA-P1: per-frame texture pointers (pinned), no per-frame string keys.
        mutable std::vector<SDL_Texture*> tex, stex;
        mutable uint64_t tex_tc = 0;
        std::string anim_image;       // W3-B: civilian building art section (slot anims)
        bool high_deck = false;       // W5-PRES R-100: a high-bridge deck (BRIDGE1/2, BRIDGEB1/B2)
    };

    // `map_ini` = the raw map text, `defs` the playable building roster (for
    // tech buildings that join the world). Returns cells to block.
    void load(const ra2assets::MixFile& ra2, const Ini& map_ini, const Ini& rules_ini, const Ini& art_ini,
              const ra2assets::MapFile& map) {
        objects_.clear(); smudges_.clear(); overlays_.clear(); wall_ids_.clear(); crush_ids_.clear(); block_.clear(); lights_.clear();
        decks_.clear(); deck_cells_.clear(); deck_depth_.clear();
        missing_ = placed_ = civ_ = 0;
        ra2_ = &ra2;
        rules_ = &rules_ini;
        art_ = &art_ini;
        map_ = &map;
        // [Terrain]
        for (const auto& kv : map_ini.section("Terrain")) {
            const int packed = std::atoi(kv.first.c_str());
            const std::string type = pres_upper(pres_clean(kv.second));
            Obj o;
            o.x = packed % 1000;
            o.y = packed / 1000;
            o.shp = art_shp(type);
            if (!o.shp) { ++missing_; continue; }
            o.key = "terr:" + type;
            o.level = level(o.x, o.y);
            o.pal = 0;
            o.shadow = true;
            if (pres_lower(rget(type, "SpawnsTiberium")).rfind("y", 0) == 0) {
                o.pal = 2;          // RE-Q1: the unit palette convert
                o.lift = 16;
            }
            if (pres_lower(rget(type, "IsAnimated")).rfind("y", 0) == 0) {
                o.animated = true;
                o.anim_rate = std::max(1, std::atoi(rget(type, "AnimationRate").c_str()));
                o.anim_prob = std::atof(rget(type, "AnimationProbability").c_str());
                o.anim_rng = 0x9E3779B9u ^ static_cast<uint32_t>(packed * 2654435761u);
            }
            objects_.push_back(o);
            block_.push_back({o.x, o.y});
        }
        // [Smudge] TYPE,x,y,0
        for (const auto& kv : map_ini.section("Smudge")) {
            const std::vector<std::string> f = fields(kv.second);
            if (f.size() < 3) continue;
            Obj o;
            const std::string type = pres_upper(pres_clean(f[0]));
            o.x = std::atoi(f[1].c_str());
            o.y = std::atoi(f[2].c_str());
            o.shp = art_shp(type);
            if (!o.shp) { ++missing_; continue; }
            o.key = "smudge:" + type;
            o.level = level(o.x, o.y);
            o.pal = 0;
            o.anchor = Obj::Anchor::TopVertex;
            smudges_.push_back(o);
        }
        // Non-ore overlays.
        // G16 (agent G) [RE: world-depth.md §B1]: [OverlayPack] stores the
        // RUNTIME index = order of appearance in [OverlayTypes]; the ini's own
        // numbers start at 1 and skip 40/41/183, so keying by them shifted every
        // overlay (BRIDGE1 read as DUMMY15, LOBRDG01 as PALET02, ...).
        std::map<int, std::string> otypes;
        names_ = ra2engine::overlay_names_in_order(rules_ini.section("OverlayTypes"));
        for (std::size_t i = 0; i < names_.size(); ++i) otypes[static_cast<int>(i)] = names_[i];
        if (map.has_overlay()) {
            for (const ra2assets::MapCell& c : map.cells()) {
                const int id = map.overlay_at(c.x, c.y);
                if (id < 0 || id == 255) continue;
                auto it = otypes.find(id);
                if (it == otypes.end()) continue;
                const std::string& name = it->second;
                if (pres_lower(rget(name, "Tiberium")).rfind("y", 0) == 0) continue;   // ore/gems: OreField
                if (name.rfind("DUMMY", 0) == 0) continue;
                auto shp = art_shp(name);
                if (!shp) { ++missing_; continue; }
                Obj o;
                o.shp = shp;
                o.x = c.x;
                o.y = c.y;
                o.level = c.level;
                o.frame = map.overlay_data_at(c.x, c.y);
                o.key = "ovl:" + name;
                o.ovl_id = id;
                const bool wall = pres_lower(rget(name, "Wall")).rfind("y", 0) == 0;
                // Non-ore overlays (rocks, bridges, low bridges) draw through the
                // cell convert = the ISO palette (visual check: srock/bridge art is
                // clean only with iso*.pal); walls use the unit palette.
                o.pal = wall ? 2 : 0;
                o.anchor = Obj::Anchor::Overlay;
                // [RE: FUN_005fd4b0] Wall/Tiberium/Crate -12, Land=Railroad -1;
                // [FUN_00480020] high-bridge cells (CellFlags 0x80) -16, and -15
                // more for OverlayData 9..17. The bridge-owner flag is stood in
                // for by the high-bridge overlay family (BRIDGE1/2, BRIDGEB1/B2).
                const bool crate = pres_lower(rget(name, "Crate")).rfind("y", 0) == 0;
                const bool rail = pres_lower(rget(name, "Land")) == "railroad";
                const bool high_bridge = name.rfind("BRIDGE", 0) == 0;
                o.yoff = ra2engine::overlay_y_offset(wall || crate, rail, id, high_bridge, o.frame);
                o.high_deck = high_bridge;
                if (high_bridge) {   // W5-PRES R-100: the deck joins the object sort
                    decks_.push_back(static_cast<int>(overlays_.size()));
                    deck_cells_.insert(c.y * 1000 + c.x);
                    int xs[4], ys[4];
                    if (ra2engine::high_bridge_section_cells(id, c.x, c.y, xs, ys))
                        for (int k = 0; k < 4; ++k) {
                            auto& depth = deck_depth_[ys[k]*1000+xs[k]];
                            depth = std::max(depth, double(c.x+c.y)+0.99);
                        }
                }
                overlays_.push_back(o);
                if (wall) { wall_ids_[c.y * 1000 + c.x] = id; block_.push_back({c.x, c.y}); }
                // P4: OverlayType Crushable=yes walls (sandbags, prison fences)
                if (wall && pres_lower(rget(name, "Crushable")).rfind("y", 0) == 0) crush_ids_.insert(c.y * 1000 + c.x);
            }
            // Walls draw their N/E/S/W connection frame (roster2.md §D: frame =
            // stage*16 + mask; map walls start undamaged).
            for (Obj& o : overlays_) {
                auto self = wall_ids_.find(o.y * 1000 + o.x);
                if (self == wall_ids_.end()) continue;
                auto same = [&](int x, int y) {
                    auto w = wall_ids_.find(y * 1000 + x);
                    return w != wall_ids_.end() && w->second == self->second;
                };
                o.frame = ra2engine::wall_mask(o.x, o.y, same);
            }
        }
        std::cerr << "[map-dressing] terrain=" << objects_.size() << " smudge=" << smudges_.size()
                  << " overlays=" << overlays_.size() << " missing-art=" << missing_ << "\n";
    }

    // Neutral [Structures]: roster types -> World buildings of `neutral_house`;
    // everything else (civilian buildings) -> drawn dressing with a blocked
    // foundation.
    template <class AddBuilding>
    void place_structures(const Ini& map_ini, AddBuilding add_building) {
        for (const auto& kv : map_ini.section("Structures")) {
            const std::vector<std::string> f = fields(kv.second);
            if (f.size() < 5) continue;
            const std::string type = pres_upper(pres_clean(f[1]));
            const int x = std::atoi(f[3].c_str()), y = std::atoi(f[4].c_str());
            if (add_building(type, x, y, std::atoi(f[2].c_str()))) { ++placed_; continue; }
            std::string image = pres_clean(rget(type, "Image"));
            if (image.empty()) image = type;
            int fw = 1, fh = 1;
            const std::string fnd = pres_clean(art_->get(pres_upper(image), "Foundation"));
            if (fnd.size() >= 3) {
                fw = std::max(1, std::atoi(fnd.c_str()));
                const std::size_t xx = fnd.find_first_of("xX");
                if (xx != std::string::npos) fh = std::max(1, std::atoi(fnd.c_str() + xx + 1));
            }
            // W3-FIX-A [RE: asset-search.md §3/§4]: a light post keeps its light
            // source; InvisibleInGame=yes is never drawn (it still occupies).
            if (std::atof(rget(type, "LightIntensity").c_str()) != 0.0) lights_.push_back({type, x, y, fw, fh});
            if (pres_lower(rget(type, "InvisibleInGame")).rfind("y", 0) == 0) {
                for (int j = 0; j < fh; ++j)
                    for (int i = 0; i < fw; ++i) block_.push_back({x + i, y + j});
                ++invisible_;
                continue;
            }
            Obj o;
            o.shp = art_shp(pres_upper(image));
            if (!o.shp) { ++missing_; continue; }
            o.own_pal = asset_art_palette(pres_clean(art_->get(pres_upper(image), "Palette")));
            o.x = x;
            o.y = y;
            o.fw = fw;
            o.fh = fh;
            o.level = level(x, y);
            o.key = "civ:" + image;
            o.anchor = Obj::Anchor::TopVertex;
            o.anim_image = pres_upper(image);   // W3-B
            o.pal = 2;
            o.shadow = true;
            objects_.push_back(o);
            for (int j = 0; j < fh; ++j)
                for (int i = 0; i < fw; ++i) block_.push_back({x + i, y + j});
            ++civ_;
        }
        std::cerr << "[map-dressing] structures: neutral-world=" << placed_ << " civilian-dressing=" << civ_
                  << " invisible=" << invisible_ << " lights=" << lights_.size() << "\n";
    }

    // W2-GA-15: a crater (Crater=yes smudge types) or scorch mark (Burn=yes)
    // left where a Crater=/Scorch= explosion anim went off; one per cell.
    void add_smudge(bool crater, double fx, double fy) {
        const int x = static_cast<int>(std::lround(fx)), y = static_cast<int>(std::lround(fy));
        if (!map_ || !map_->at(x, y)) return;
        if (!smudge_cells_.insert(y * 1000 + x).second) return;
        if (smudge_kinds_[0].empty() && rules_) {
            for (const auto& kv : rules_->section("SmudgeTypes")) {
                const std::string n = pres_upper(pres_clean(kv.second));
                if (n.empty()) continue;
                if (!rget(n, "Width").empty() || !rget(n, "Height").empty()) continue;   // 1x1 only
                if (pres_lower(rget(n, "Crater")).rfind("y", 0) == 0) smudge_kinds_[0].push_back(n);
                else if (pres_lower(rget(n, "Burn")).rfind("y", 0) == 0) smudge_kinds_[1].push_back(n);
            }
        }
        const std::vector<std::string>& kinds = smudge_kinds_[crater ? 0 : 1];
        if (kinds.empty()) return;
        smudge_rng_ = smudge_rng_ * 1103515245u + 12345u;
        const std::string& type = kinds[(smudge_rng_ >> 8) % kinds.size()];
        Obj o;
        o.shp = art_shp(type);
        if (!o.shp) return;
        o.x = x;
        o.y = y;
        o.level = level(x, y);
        o.key = "smudge:" + type;
        o.pal = 0;
        o.anchor = Obj::Anchor::TopVertex;
        smudges_.push_back(o);
    }
    const std::vector<std::pair<int, int>>& blocked() const { return block_; }
    bool is_wall(int x, int y) const { return wall_ids_.count(y * 1000 + x) != 0; }   // W5-A
    // W6-04-05: the wall OverlayType name at a cell ("" none).
    std::string wall_name(int x, int y) const {
        auto it = wall_ids_.find(y * 1000 + x);
        return it != wall_ids_.end() && it->second >= 0 && it->second < static_cast<int>(names_.size())
                   ? names_[static_cast<std::size_t>(it->second)] : std::string();
    }
    bool is_crushable_wall(int x, int y) const { return crush_ids_.count(y * 1000 + x) != 0; }   // P4
    // W2-SK-01: the map's ore mines ([Terrain] types with SpawnsTiberium=yes)
    // with their animation data, for World::add_ore_mine.
    template <class F> void for_each_ore_mine(F f) const {
        for (const Obj& o : objects_) {
            if (o.key.rfind("terr:", 0) != 0 || !o.shp) continue;
            const std::string type = o.key.substr(5);
            if (pres_lower(rget(type, "SpawnsTiberium")).rfind("y", 0) != 0) continue;
            const std::string rate = rget(type, "AnimationRate"), prob = rget(type, "AnimationProbability");
            f(o.x, o.y, std::max(1, o.shp->count() / 2), rate.empty() ? 3 : std::atoi(rate.c_str()),
              prob.empty() ? 0.003 : std::atof(prob.c_str()));
        }
    }
    const std::vector<Obj>& objects() const { return objects_; }
    int missing() const { return missing_; }   // W3-FIX-A: objects dropped for missing art
    // W3-FIX-A: dressing structures that carry a light source (LightIntensity != 0).
    struct LightObj { std::string type; int x, y, fw, fh; };
    const std::vector<LightObj>& lights() const { return lights_; }
    const std::vector<Obj>& overlays() const { return overlays_; }
    const std::vector<Obj>& smudges() const { return smudges_; }   // W4-FIX-POS (cheat view_key)

    void draw_ground(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w,
                     const ra2assets::Palette* pals[3], const ra2engine::Camera& cam, int W, int H) {
        step_anims();
        for (const Obj& o : smudges_) draw(ren, tc, w, o, pals, cam, W, H);
        for (const Obj& o : overlays_) {
            if (o.high_deck && layer_decks_) continue;   // W5-PRES R-100: sorted with the objects
            // W4-SQ-10: a broken high bridge deck is gone.
            if (o.high_deck && w.high_bridge_destroyed(o.x, o.y)) continue;
            if (w.wall_crushed(o.x, o.y) && is_wall(o.x, o.y)) continue;   // P4: flattened by a tank
            // W6-04-05: a damaged wall draws its stage (frame = stage*16 + mask).
            const int stage = is_wall(o.x, o.y) ? w.wall_overlay_stage(o.x, o.y) : 0;
            if (stage > 0) {
                Obj d = o;
                d.frame += 16 * stage;
                draw(ren, tc, w, d, pals, cam, W, H);
                continue;
            }
            draw(ren, tc, w, o, pals, cam, W, H);
        }
        draw_tile_anims(ren, tc, w, pals, cam, W, H);   // W3-B
    }
    // W5-PRES R-100 [RE-FALLBACK pending: docs/re/w5-presentation.md §1]: the
    // high-bridge decks leave the ground pass and are depth-sorted with the
    // objects (render_world, kind 3) so a ship under a deck is covered by it and
    // a unit on the deck draws over it. `layer_decks_` false = the old ground
    // pass (diagnostic).
    // W5-PRES R-047: a disguised Mirage drawn as its DefaultMirageDisguises
    // tree (a [Terrain] object at the unit's cell, shadow half included).
    bool draw_tree_at(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w,
                      const std::string& type, int x, int y, int lvl, const ra2assets::Palette* pals[3],
                      const ra2engine::Camera& cam, int W, int H) {
        auto it = trees_.find(type);
        if (it == trees_.end()) {
            Obj o;
            o.shp = art_shp(pres_upper(type));
            o.key = "terr:" + pres_upper(type);
            o.shadow = true;
            it = trees_.emplace(type, o).first;
        }
        if (!it->second.shp) return false;
        Obj o = it->second;
        o.x = x;
        o.y = y;
        o.level = lvl;
        o.tex.clear();
        o.stex.clear();
        o.tex_tc = 0;
        draw(ren, tc, w, o, pals, cam, W, H);
        return true;
    }
    const std::vector<int>& decks() const { return decks_; }
    const ra2assets::MapFile* map() const { return map_; }
    double deck_depth(int x, int y) const {
        const auto it = deck_depth_.find(y*1000+x);
        return it == deck_depth_.end() ? -1.0 : it->second;
    }
    bool deck_cell(int x, int y) const { return deck_cells_.count(y * 1000 + x) != 0; }
    void set_layer_decks(bool on) { layer_decks_ = on; }
    bool layer_decks() const { return layer_decks_; }
    void draw_deck(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w, int i,
                   const ra2assets::Palette* pals[3], const ra2engine::Camera& cam, int W, int H) {
        if (i < 0 || i >= static_cast<int>(overlays_.size())) return;
        const Obj& o = overlays_[static_cast<std::size_t>(i)];
        if (pres_high_deck_destroyed(w, o.x, o.y)) return;   // w4fix-seq: a broken span is gone
        draw(ren, tc, w, o, pals, cam, W, H);
    }
    // W3-B [RE-Q6]: theater tile anims (ALL07's waterfalls). One anim per
    // template instance, on the cell whose sub-tile == AttachesTo, at that
    // cell's centre + (XOffset, YOffset) px [Medium: the offset origin was
    // fitted to the waterfall art, not traced]. Flat, ShouldUseCellDrawer=yes
    // -> the iso (tile) palette; loops on the logic-tick clock.
    struct TileAnimObj { int x, y, level, xoff, yoff; std::string anim; };
    void load_tile_anims(ra2engine::Theater& th, const ra2assets::MapFile& map) {
        tile_anims_.clear();
        for (const ra2assets::MapCell& c : map.cells()) {
            const ra2engine::Theater::TileAnim* ta = th.tile_anim(c.tile);
            if (!ta || static_cast<int>(c.subtile) != ta->attaches_to) continue;
            tile_anims_.push_back({c.x, c.y, c.level, ta->xoff, ta->yoff, ta->anim});
        }
        std::cerr << "[map-dressing] tile anims=" << tile_anims_.size();
        for (std::size_t i = 0; i < tile_anims_.size() && i < 4; ++i)
            std::cerr << " " << tile_anims_[i].anim << "@" << tile_anims_[i].x << "," << tile_anims_[i].y;
        std::cerr << std::endl;
    }
    std::size_t tile_anim_count() const { return tile_anims_.size(); }
    void draw_tile_anims(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w,
                         const ra2assets::Palette* pals[3], const ra2engine::Camera& cam, int W, int H) {
        if (!g_fx) return;
        for (const TileAnimObj& o : tile_anims_) {
            if (!w.explored_at(g_local_house, o.x, o.y)) continue;
            const FxAnimType* t = g_fx->theater_type(o.anim);
            if (!t) continue;
            const int fr = bld_run_frame(*t, 0, g_pres_tick);
            if (fr < 0) continue;
            const ra2engine::ScreenPt p = ra2engine::cell_to_screen(o.x, o.y, o.level, 60, 30, 15);
            g_fx->blit(ren, tc, *t->shp, "tanim:" + t->name, fr, p.x + cam.x + 30 + o.xoff,
                       p.y + cam.y + 15 + o.yoff, PresFx::alpha_for(*t, fr), W, H, pals[0]);
        }
    }
    // One depth-sorted object (render_world merges these with buildings/units).
    void draw(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w, const Obj& o,
              const ra2assets::Palette* pals[3], const ra2engine::Camera& cam, int W, int H) {
        if (!w.explored_at(g_local_house, o.x, o.y)) return;
        // G16: a low bridge section shows its LIVE overlay (damaged / destroyed /
        // repaired) -- World::bridge_overlay_at.
        if (o.ovl_id >= 0 && w.bridges_enabled()) {
            const int live = w.bridge_overlay_at(o.x, o.y);
            if (live >= 0 && live != o.ovl_id && live < static_cast<int>(names_.size())) {
                Obj alt = o;
                alt.ovl_id = -1;
                alt.tex.clear();
                alt.stex.clear();
                alt.tex_tc = 0;
                alt.shp = art_shp(names_[live]);
                alt.key = "ovl:" + names_[live];
                if (alt.shp) draw(ren, tc, w, alt, pals, cam, W, H);
                return;
            }
        }
        const int images = std::max(1, o.shp->count() / 2);   // second half = shadows
        const int want = o.animated && o.anim_stage >= 0 ? o.anim_stage : o.frame;
        const int fr = std::clamp(want, 0, std::max(0, std::min(images, o.shp->count()) - 1));
        const ra2assets::ShpFile::Frame f = o.shp->frame_info(fr);
        if (f.w <= 0 || f.h <= 0) return;
        const ra2engine::ScreenPt p = ra2engine::cell_to_screen(o.x, o.y, o.level, 60, 30, 15);
        // W4-FIX-POS: every kind is a centred blit; only the anchor differs.
        int cx = p.x + cam.x + 30, cy = p.y + cam.y;          // TopVertex
        if (o.anchor == Obj::Anchor::CellCentre) cy += 15;
        else if (o.anchor == Obj::Anchor::Overlay) cy += 15 + o.yoff;
        const int ox = cx - o.shp->width() / 2;               // SHP canvas origin on screen
        const int oy = cy - o.shp->height() / 2 - o.lift;
        const SDL_Rect dst{ox + f.x, oy + f.y, f.w, f.h};
        // Shadow extents can exceed the image: cull on a generous box.
        if (dst.x + dst.w + 96 <= 0 || dst.y + dst.h + 64 <= 0 || dst.x - 96 >= W || dst.y - 64 >= H) return;
        if (o.tex_tc != tc.instance_id()) {
            o.tex.assign(static_cast<std::size_t>(o.shp->count()), nullptr);
            o.stex.assign(static_cast<std::size_t>(o.shp->count()), nullptr);
            o.tex_tc = tc.instance_id();
        }
        // Shadow half first, 50 % Darken (same blend as buildings, G-85).
        const int sf = fr + o.shp->count() / 2;
        if (o.shadow && o.shp->count() >= 2 && sf < o.shp->count()) {
            const ra2assets::ShpFile::Frame s = o.shp->frame_info(sf);
            if (s.w > 0 && s.h > 0) {
                SDL_Texture*& st = o.stex[static_cast<std::size_t>(sf)];
                if (!st) {
                    const std::string skey = o.key + ":sh" + std::to_string(sf);
                    st = static_cast<SDL_Texture*>(tc.peek(skey));
                    if (!st) {
                        const std::vector<uint8_t> si = o.shp->decode_indexed(sf);
                        std::vector<uint8_t> rgba(si.size() * 4, 0);
                        for (std::size_t k = 0; k < si.size(); ++k) if (si[k]) rgba[k * 4 + 3] = 128;
                        st = static_cast<SDL_Texture*>(tc.get_rgba(skey, rgba.data(), s.w, s.h));
                    }
                    tc.pin(skey);
                }
                // W4-FIX-POS [RE: FUN_0071b450]: the image and the shadow share
                // one point -- a SpawnsTiberium mine's shadow is lifted 16 px too.
                const SDL_Rect sd{ox + s.x, oy + s.y, s.w, s.h};
                SDL_RenderCopy(ren, st, nullptr, &sd);
            }
        }
        SDL_Texture*& tex = o.tex[static_cast<std::size_t>(fr)];
        if (!tex) {
            const std::string key = o.key + ":" + std::to_string(fr);
            tex = static_cast<SDL_Texture*>(tc.peek(key));
            if (!tex) {
                const std::vector<uint8_t> idx = o.shp->decode_indexed(fr);
                tex = static_cast<SDL_Texture*>(tc.get(key, idx.data(), f.w, f.h, o.own_pal ? *o.own_pal : *pals[o.pal]));
            }
            tc.pin(key);
        }
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        // W3-B: a civilian building's Active/Idle anims (were frozen at frame 0).
        if (!o.anim_image.empty())
            pres_dressing_overlay(ren, tc, o.anim_image, ox + o.shp->width() / 2, oy + o.shp->height() / 2 + o.lift,
                                  *pals[o.pal], W, H);
    }

    // IsAnimated terrain (TIBTRE ore mines) [RE: FUN_0071b450 frame = anim
    // stage when IsAnimated]: each game frame an idle object starts with
    // AnimationProbability, then steps one frame per AnimationRate ticks
    // through the image half and returns to idle. Render-only RNG.
    void step_anims() {
        const long now = g_pres_tick;
        if (now == last_tick_) return;
        // W3-B: every logic tick since the last draw is replayed (a sparse
        // render -- --play render_every, a stalled phone frame -- must not drop
        // ticks, or the result would depend on the render rate).
        const long n = std::min<long>(now - last_tick_, 100000);
        last_tick_ = now;
        for (long t = 0; t < n; ++t)
            for (const Obj& o : objects_) {
                if (!o.animated || !o.shp) continue;
                const int images = std::max(1, o.shp->count() / 2);
                if (o.anim_stage < 0) {
                    o.anim_rng = o.anim_rng * 1664525u + 1013904223u;
                    if ((o.anim_rng >> 8) * (1.0 / 16777216.0) < o.anim_prob) { o.anim_stage = 0; o.anim_timer = 0; }
                } else if (++o.anim_timer >= o.anim_rate) {
                    o.anim_timer = 0;
                    if (++o.anim_stage >= images) o.anim_stage = -1;
                }
            }
    }

private:
    static std::vector<std::string> fields(const std::string& v) {
        std::vector<std::string> f;
        std::string cur;
        for (char c : v) {
            if (c == ',') { f.push_back(cur); cur.clear(); } else cur += c;
        }
        f.push_back(cur);
        return f;
    }
    std::string rget(const std::string& s, const std::string& k) const {
        return pres_clean(rules_->get(s, k));
    }
    int level(int x, int y) const {
        const ra2assets::MapCell* c = map_->at(x, y);
        return c ? c->level : 0;
    }
    MixFile* nested(const std::string& name) {
        auto it = nested_.find(name);
        if (it != nested_.end()) return it->second.get();
        std::unique_ptr<MixFile> m;
        try { m = std::make_unique<MixFile>(ra2_->open_nested(name)); } catch (...) {}
        MixFile* raw = m.get();
        nested_.emplace(name, std::move(m));
        return raw;
    }
    // Theater=yes art -> <name>.<theater ext>; NewTheater/plain -> .shp;
    // generic.mix / conquer.mix last.
    std::shared_ptr<ra2assets::ShpFile> art_shp(const std::string& name) {
        auto it = cache_.find(name);
        if (it != cache_.end()) return it->second;
        const ra2assets::TheaterChain& ch = art_chain();
        // W2-GA-07: Image= from artmd, else from rules (BRIDGE1/2 carry
        // Image=BRIDGE in rulesmd only), else the name itself.
        std::string image = pres_clean(art_->get(name, "Image"));
        if (image.empty()) image = rget(name, "Image");
        if (image.empty()) image = name;
        (void)ch;
        // W3-FIX-A: the theater extension, then .shp, over the global MIX
        // search list (isogen / <ext>.mix / patch caches / YR tail included).
        std::shared_ptr<ra2assets::ShpFile> out;
        ra2assets::ShpFile shp;
        if (!asset_art_shp(image, /*theater_ext=*/true, shp).empty())
            out = std::make_shared<ra2assets::ShpFile>(std::move(shp));
        else
            std::cerr << "[map-dressing] no art for " << name << " (image " << image << ")\n";
        cache_[name] = out;
        return out;
    }

    const ra2assets::MixFile* ra2_ = nullptr;
    const Ini* rules_ = nullptr;
    const Ini* art_ = nullptr;
    const ra2assets::MapFile* map_ = nullptr;
    std::map<std::string, std::unique_ptr<MixFile>> nested_;
    std::map<std::string, std::shared_ptr<ra2assets::ShpFile>> cache_;
    std::vector<Obj> objects_, smudges_, overlays_;
    std::vector<int> decks_;               // W5-PRES R-100: overlays_ indices of high decks
    std::map<std::string, Obj> trees_;     // W5-PRES R-047: Mirage disguise art
    std::map<int, double> deck_depth_;
    std::set<int> deck_cells_;             // 1000*y + x
    bool layer_decks_ = true;
    std::vector<std::string> names_;   // [OverlayTypes] by runtime index
    std::map<int, int> wall_ids_;
    std::set<int> crush_ids_;   // P4: crushable wall cells (y*1000+x)
    std::vector<std::pair<int, int>> block_;
    int missing_ = 0, placed_ = 0, civ_ = 0, invisible_ = 0;
    std::vector<LightObj> lights_;
    long last_tick_ = 0;
    std::vector<TileAnimObj> tile_anims_;   // W3-B
    std::set<int> smudge_cells_;
    std::vector<std::string> smudge_kinds_[2];
    uint32_t smudge_rng_ = 0x5A17u;
};

PresMap* g_map_dressing = nullptr;                    // live game only
const ra2assets::Palette* g_map_pals[3] = {nullptr, nullptr, nullptr};   // iso, theater, unit

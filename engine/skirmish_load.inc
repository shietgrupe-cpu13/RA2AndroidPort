// skirmish_load.inc -- W5-E: the skirmish / MP loading screen art. Included by
// main.cpp right before defer_game_scene.
//
// [RE: FUN_005529F0 (the loading-screen setup): outside a campaign the
// background is "ls%s<country>.shp" from load.mix -- %s = the resolution
// ("800" / "640") and <country> one of ustates / ukingdom / france / germany
// / korea / russia / cuba / libya / iraq (plus yuri / obs), chosen by the
// local player's country index; the palette is the side's loading palette
// (the MPLS<country>.PAL strings beside LDSCRN*.PAL; YR loadmd.mix carries
// both the art and those palettes -- the RA2 load.mix copies under LDSCRN*.PAL
// come out miscoloured).] [RE-FALLBACK: the text block
// (map name + one line per player with the country) and its position; the
// original's per-country CSF load message is not drawn.]

struct SkirmishLoadArt {
    ra2assets::ShpFile bg;
    ra2assets::Palette pal;
    bool ok = false;
    std::string map_name, art_name;
    std::vector<std::string> players;

    static std::string country_art(const std::string& country, bool soviet) {
        return ra2engine::loading_country_art(country,soviet);
    }
    void load(const ra2engine::SkirmishConfig& cfg) {
        // L2: the loading screen shows the LOCAL player's country.
        ra2engine::SkirmishConfig c2 = cfg;
        apply_debug_humans(c2);
        const std::size_t ls = static_cast<std::size_t>(local_slot_of(c2));
        const bool soviet = ls < cfg.slots.size() && cfg.slots[ls].side == ra2engine::World::Side::Soviet;
        const std::string country = ls < cfg.slots.size() ? cfg.slots[ls].country : std::string();
        art_name = "ls800" + country_art(country, soviet) + ".shp";
        const std::string pal_name = ra2engine::loading_country_palette(country,soviet);
        try {
            const std::string dir = env_install_dir();
            // [RE: the MPLS*.PAL names next to LDSCRN*.PAL in gamemd] the YR
            // loadmd.mix art + its per-country palette.
            ra2assets::MixFile md = ra2assets::MixFile::open_path(dir + "ra2md.mix");
            ra2assets::MixFile ld = md.open_nested("loadmd.mix");
            if (const ra2assets::MixEntry* e = ld.find(art_name)) {
                bg = ra2assets::ShpFile::load(ld.read(*e));
                if (const ra2assets::MixEntry* pe = ld.find(pal_name)) {
                    const auto raw = ld.read(*pe);
                    pal = ra2assets::Palette::load(raw.data(), raw.size());
                    ok = bg.count() > 0;
                    if (ok) g_skirmish_load_art_drawn = art_name;   // --play evidence (art found)
                }
            }
            map_name = cfg.map_file;
            for (std::size_t i = 0; i < cfg.slots.size(); ++i) {
                const ra2engine::SkirmishSlot& s = cfg.slots[i];
                if (!s.combatant) continue;
                std::string c = s.country.empty() ? std::string("Random") : s.country;
                const std::string lbl = camp::csf_text(dir, "Name:" + c);
                if (lbl.rfind("Name:", 0) != 0) c = lbl;
                players.push_back((s.is_ai ? std::string("Computer") : std::string("Player")) + "  -  " + c);
            }
        } catch (const std::exception& ex) {
            std::cerr << "[skirmish-load] art unavailable (" << ex.what() << ")\n";
        }
    }
    void draw(SDL_Renderer* ren, ra2engine::TextureCache& tc, double frac) const {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        const int aw = ok ? bg.width() : 800, ah = ok ? bg.height() : 600;
        const double sc = std::min(1280.0 / aw, 720.0 / ah);
        const int dw = static_cast<int>(aw * sc), dh = static_cast<int>(ah * sc);
        const int ox = (1280 - dw) / 2, oy = (720 - dh) / 2;
        if (ok)
            if (SDL_Texture* t = shp_frame_tex(tc, "sk-ls:" + art_name, bg, 0, pal)) {
                const ra2assets::ShpFile::Frame f = bg.frame_info(0);
                SDL_Rect d{ox + static_cast<int>(f.x * sc), oy + static_cast<int>(f.y * sc),
                           static_cast<int>(f.w * sc), static_cast<int>(f.h * sc)};
                SDL_RenderCopy(ren, t, nullptr, &d);
            }
        const SDL_Color col{164, 210, 255, 255};
        int y = oy + static_cast<int>(20 * sc);
        const int tx = ox + static_cast<int>(24 * sc);
        if (!map_name.empty()) {
            hud_text(ren, tc, map_name, tx, y, col, kHudLeft, false, true, 2.0);
            y += 2 * (hud_line_h() + 4);
        }
        for (const std::string& p : players) {
            hud_text(ren, tc, p, tx, y, col, kHudLeft, false, true, 1.5);
            y += static_cast<int>(1.5 * (hud_line_h() + 4));
        }
        const SDL_Rect bar{ox + dw / 4, oy + dh - static_cast<int>(18 * sc), dw / 2, static_cast<int>(8 * sc)};
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xAF);
        SDL_RenderFillRect(ren, &bar);
        SDL_Rect fill{bar.x + 1, bar.y + 1, static_cast<int>((bar.w - 2) * std::max(0.0, std::min(1.0, frac))), bar.h - 2};
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
        SDL_RenderFillRect(ren, &fill);
        SDL_RenderDrawRect(ren, &bar);
    }
    static inline std::string g_skirmish_load_art_drawn;
};

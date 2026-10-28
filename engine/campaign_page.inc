// campaign_page.inc -- W2-UI-18: the Campaign select page (dialog 0x94),
// drawn inside ShellScene's PAGE_CAMPAIGN. Included by main.cpp before
// ShellScene (after shell_pages.inc).
//
// [RE: wave2/04 W2-UI-18 + mainmenu_true.md §3/§8.2]
//   * header GUI:CampaignMenu "Campaign"; Load 0x40E at row 0; Back row 5;
//     the 0x71C WARNING monitor runs.
//   * campaign listbox 0x455 in the sidebar column (477, 247, 160, 159);
//     items = battle.ini [Battles] TUT1 / ALL1 / SOV1 by their CSF
//     Description (DESC:*); the DebugOnly / demo entries are not listed.
//   * difficulty slider 0x50F (135, 406, 210, 21), labels "Difficulty"
//     (135, 380) and "Harder" (232, 380); seeded from [Options] Difficulty.
//   * three SS_BLACKFRAME emblem wells over FSBKGDSM.SHP / FSSCRN.PAL:
//     0x6EA Allied (24, 16, 426, 115), 0x6EC tutorial (61, 138, 348, 91),
//     0x6EB Soviet (78, 235, 318, 115); tooltips STT:CampaignAnim*.
// [RE-FALLBACK: the wells' hover animation art was not traced -- a hovered
// well brightens, the selected one carries a red frame; a click selects, a
// double click (or Load) starts the campaign.]

struct CampaignPageState {
    struct Entry { const char* battle; const char* side; const char* desc; const char* fallback; int well; };
    static const Entry& entry(int i) {
        static const Entry k[5] = {
            {"TUT1", "GDI", "DESC:TUT1", "Boot Camp", 1},
            {"ALL1", "GDI", "DESC:ALL1", "Allied Campaign", 0},
            {"SOV1", "Nod", "DESC:SOV1", "Soviet Campaign", 2},
            {"YR_ALL1", "GDI", "", "Yuri's Revenge: Allied", 0},
            {"YR_SOV1", "Nod", "", "Yuri's Revenge: Soviet", 2},
        };
        return k[i];
    }
    static constexpr int kEntries = 5;
    static ra2engine::SbRect well(int w) {
        static const ra2engine::SbRect k[3] = {{24, 16, 426, 115}, {61, 138, 348, 91}, {78, 235, 318, 115}};
        return k[w];
    }
    static const char* well_stt(int w) {
        static const char* k[3] = {"STT:CampaignAnimAllied", "STT:CampaignAnimTutorial", "STT:CampaignAnimSoviet"};
        return k[w];
    }
    static constexpr ra2engine::SbRect kSlider{135, 406, 210, 21};

    ShellList list{{477, 247, 160, 159}, {}, {}, {}, 0, 1, 9, 17};
    int difficulty = 1;   // 0 easy .. 2 hard
    bool drag = false;
    Uint32 last_click_ms = 0;
    int last_click_well = -1;

    void load(const ra2engine::Settings* s) {
        list.items.clear();
        for (int i = 0; i < kEntries; ++i) list.items.push_back(gf_csf(entry(i).desc, entry(i).fallback));
        list.sel = 1;   // ALL1
        difficulty = s ? std::max(0, std::min(2, s->difficulty)) : 1;
    }
    int selected() const { return list.sel; }
    int entry_of_well(int w) const {
        for (int i = 0; i < kEntries; ++i)
            if (entry(i).well == w) return i;
        return -1;
    }

    // Returns true when the event asks to start the selected campaign.
    bool handle(const ra2engine::InputEvent& ev, int lx, int ly, bool& consumed) {
        using IE = ra2engine::InputEvent;
        consumed = false;
        auto set_diff = [&]() {
            difficulty = ra2engine::trackbar_pos_from_x(lx - kSlider.x, 2, kSlider.w, 0);
            if (g_user_settings) g_user_settings->difficulty = difficulty;
        };
        if (drag && (ev.kind == IE::PointerMove || ev.kind == IE::PointerUp)) {
            set_diff();
            if (ev.kind == IE::PointerUp) drag = false;
            consumed = true;
            return false;
        }
        if (ev.kind != IE::PointerDown || ev.button != IE::Left) return false;
        if (kSlider.contains(lx, ly)) { set_diff(); drag = true; consumed = true; return false; }
        if (list.scrolls() && list.strip_up().contains(lx, ly)) { list.scroll(-1); consumed = true; return false; }
        if (list.scrolls() && list.strip_down().contains(lx, ly)) { list.scroll(1); consumed = true; return false; }
        int hit = list.hit(lx, ly);
        // R-151: the rows are 17 px of an 800x600 page (~14 dp on a phone);
        // a finger anywhere in the list box picks the nearest row.
        if (hit < 0 && ev.touch && list.r.contains(lx, ly) && !list.items.empty()) {
            int best = 1 << 30;
            const int last = std::min(static_cast<int>(list.items.size()), list.top + list.rows());
            for (int i = list.top; i < last; ++i) {
                const ra2engine::SbRect r = list.item_rect(i);
                const int d = std::abs(ly - (r.y + r.h / 2));
                if (d < best) { best = d; hit = i; }
            }
        }
        if (hit >= 0) { list.sel = hit; consumed = true; return false; }
        for (int w = 0; w < 3; ++w) {
            if (!well(w).contains(lx, ly)) continue;
            const int e = list.sel >= 3 && entry(list.sel).well == w ? list.sel : entry_of_well(w);
            const bool dbl = (w == last_click_well && ev.t_ms - last_click_ms < 400);
            last_click_well = w;
            last_click_ms = ev.t_ms;
            if (e >= 0) list.sel = e;
            consumed = true;
            return dbl;
        }
        return false;
    }

    std::string status_at(int lx, int ly) const {
        for (int w = 0; w < 3; ++w)
            if (well(w).contains(lx, ly)) return gf_csf(well_stt(w), "");
        if (list.r.contains(lx, ly)) return gf_csf("STT:CampaignList", "");
        if (kSlider.contains(lx, ly)) return gf_csf("STT:CampaignSliderDifficulty", "");
        return "";
    }

    void render(SDL_Renderer* ren, ra2engine::TextureCache& tc, const MenuArt& art, const ShellXform& xf, int lx,
                int ly) const {
        const ShellPainter p{ren, tc, art, xf};
        if (art.fsbkgd && art.fsscrn_pal)
            if (SDL_Texture* t = shp_frame_tex(tc, "shell:fsbkgd", *art.fsbkgd, 0, *art.fsscrn_pal)) {
                const SDL_Rect d = p.dev(0, 0, 472, 448);
                SDL_RenderCopy(ren, t, nullptr, &d);
            }
        const int sel_well = (list.sel >= 0 && list.sel < kEntries) ? entry(list.sel).well : -1;
        for (int w = 0; w < 3; ++w) {
            const SDL_Rect d = p.dev(well(w));
            if (well(w).contains(lx, ly)) {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
                SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
                SDL_RenderFillRect(ren, &d);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            }
            if (w == sel_well) p.frame(d, true);
        }
        list.draw(p);
        p.label(gf_csf("GUI:Difficulty", "Difficulty"), 135, 380, 97, 24, 0);
        p.label(gf_csf("GUI:Harder", "Harder"), 232, 380, 113, 24, 2);
        p.trackbar(kSlider, difficulty, 2, false, "", true);
        shell_status_line(p, status_at(lx, ly));
    }

    void play_probe(ra2play::Probe& pr) const {
        static const char* kW[3] = {"allied", "tutorial", "soviet"};
        auto put = [&](const std::string& k, const ra2engine::SbRect& lr) {
            const ra2engine::SbRect r = ui_dev(lr);
            pr.targets[k] = ra2play::Rect{r.x, r.y, r.w, r.h};
        };
        for (int w = 0; w < 3; ++w) put(std::string("well:") + kW[w], well(w));
        for (int i = list.top; i < list.top + list.rows(); ++i) put("campaign:" + std::to_string(i), list.item_rect(i));
        for (int v = 0; v <= 2; ++v)
            put("slider:difficulty=" + std::to_string(v),
                {kSlider.x + shell_trackbar_x_for(v, 2, kSlider.w, 0), kSlider.y + kSlider.h / 2, 1, 1});
        pr.set("campaign_sel", list.sel);
        pr.set_s("campaign_battle", (list.sel >= 0 && list.sel < kEntries) ? entry(list.sel).battle : "");
        pr.set("difficulty", difficulty);
    }
};

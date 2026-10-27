// ============================================================================
// The Skirmish setup page = dialog 0x102 (+ the Customize Battle dialog 0x6B),
// rendered on the faithful shell (draw_shell, SDTP frame 1, no WARNING monitor,
// no Bink) at the template's 640x480 rects through the shared ShellXform.
// Included into main.cpp where the placeholder SkirmishSetupScene used to be
// (after ShellScene, so Back can construct the Single Player page).
//
// Authority: docs/gap/07-skirmish-page.md §1 (layout spec, controls, CSF keys,
// behaviour) and the pure model engine/skirmish_lobby.{hpp,cpp}.
//   Start  -> validate (0x6829F0) -> save RA2.INI -> GameScene(lobby_to_config)
//   Back   -> commit + save RA2.INI -> Single Player page (Esc = Back)
//   Customize Battle -> dialog 0x6B: Game Type + Game Map lists, preview,
//                       Use Map / Random Map (greyed: no RandMap generator) /
//                       Cancel
// --play targets (README "lobby"): button:<label>, start, back (aliases),
// edit:name, combo:ai<N>, combo:country<N>, combo:color<N>, pick:<item label>
// / item:<i> / list:up / list:down while a list is open, check:<id>,
// slider:<id>=<value>, map:<file> / map:+ / map:- on Customize Battle.
// ============================================================================

namespace skp {

// [Preview] Size + [PreviewPack] (maps.md §4.6): base64 -> Format5 LZO ->
// 24-bit RGB (R first). Start markers are baked into the picture.
inline bool decode_preview(const std::string& text, int& w, int& h, std::vector<uint8_t>& rgba) {
    w = h = 0;
    std::string pack;
    std::string section;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(i, e - i);
        i = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] == '[') {
            const std::size_t c = line.find(']');
            section = line.substr(1, c == std::string::npos ? std::string::npos : c - 1);
            for (char& ch : section) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (section == "PREVIEW") {
            std::string k = line.substr(0, eq);
            for (char& ch : k) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (k == "SIZE") {
                int a = 0, b = 0, c = 0, d = 0;
                if (std::sscanf(line.c_str() + eq + 1, "%d,%d,%d,%d", &a, &b, &c, &d) == 4) { w = c; h = d; }
            }
        } else if (section == "PREVIEWPACK") {
            pack += line.substr(eq + 1);
        }
    }
    if (w <= 0 || h <= 0 || pack.empty()) return false;
    try {
        const std::vector<uint8_t> raw = ra2assets::base64_decode(pack);
        const std::vector<uint8_t> px =
            ra2assets::decompress_mappack(raw.data(), raw.size(), ra2assets::MapPackCodec::LZO);
        if (px.size() < static_cast<std::size_t>(w) * h * 3) return false;
        rgba.resize(static_cast<std::size_t>(w) * h * 4);
        for (int k = 0; k < w * h; ++k) {
            rgba[k * 4 + 0] = px[k * 3 + 0];
            rgba[k * 4 + 1] = px[k * 3 + 1];
            rgba[k * 4 + 2] = px[k * 3 + 2];
            rgba[k * 4 + 3] = 255;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// The page's CSF strings, resolved once (gf_csf falls back to the English).
struct Text {
    std::string header, start, choose, back, battle, none_label;
    std::string ai[4];               // None, Easy Enemy, Medium Enemy, Brutal Enemy
    std::string random_country, random_color;
    std::string color_abbrev[8];
    std::string short_game, mcv, crates, sw, boa, speed, credits, units;
    std::string sel_engagement, game_type, game_map, use_map, random_map, cancel;
    std::string too_small, need_two, ok;
    std::vector<std::string> country;   // UIName per lobby country
    std::map<std::string, std::string> tip;   // STT:* by key
    void load(const std::vector<std::string>& countries) {
        header = gf_csf("GUI:SkirmishGame", "Skirmish Game");
        start = gf_csf("GUI:StartGame", "Start Game");
        choose = gf_csf("GUI:ChooseMap", "Customize Battle");
        back = gf_csf("GUI:Back", "Back");
        battle = gf_csf("GUI:Battle", "Battle");
        none_label = gf_csf("GUI:None", "None");
        ai[0] = none_label;
        ai[1] = gf_csf("GUI:AIEasy", "Easy Enemy");
        ai[2] = gf_csf("GUI:AINormal", "Medium Enemy");
        ai[3] = gf_csf("GUI:AIHard", "Brutal Enemy");
        random_country = gf_csf("GUI:RandomEx", "Random (??" "?)");
        random_color = gf_csf("GUI:RandomAsSymbols", "??" "?");
        static const char* kAb[8][2] = {{"GUI:GoldAbbrev", "GLD"},   {"GUI:RedAbbrev", "RED"},
                                        {"GUI:BlueAbbrev", "BLU"},   {"GUI:GreenAbbrev", "GRN"},
                                        {"GUI:OrangeAbbrev", "ORA"}, {"GUI:SkyBlueAbbrev", "SKY"},
                                        {"GUI:PurpleAbbrev", "PUR"}, {"GUI:PinkAbbrev", "PNK"}};
        for (int i = 0; i < 8; ++i) color_abbrev[i] = gf_csf(kAb[i][0], kAb[i][1]);
        short_game = gf_csf("GUI:ShortGame", "Short Game");
        mcv = gf_csf("GUI:MCVRepacks", "MCV Repacks");
        crates = gf_csf("GUI:CratesAppear", "Crates Appear");
        sw = gf_csf("GUI:SuperWeaponsAllowed", "Superweapons");
        boa = gf_csf("GUI:BuildOffAlly", "Build Off Ally ConYards");
        speed = gf_csf("GUI:GameSpeed", "Game Speed");
        credits = gf_csf("GUI:Credits", "Credits");
        units = gf_csf("GUI:UnitCount", "Unit Count");
        sel_engagement = gf_csf("GUI:SelectEngagement", "Select Engagement");
        game_type = gf_csf("GUI:GameType", "Game Type");
        game_map = gf_csf("GUI:GameMap", "Game Map");
        use_map = gf_csf("GUI:UseMap", "Use Map");
        random_map = gf_csf("GUI:CreateRandomMap", "Random Map");
        cancel = gf_csf("GUI:Cancel", "Cancel");
        too_small = gf_csf("TXT_SCENARIO_TOO_SMALL",
                           "This map has a %d player max. The max includes human and computer players.");
        need_two = gf_csf("TXT_NEED_AT_LEAST_TWO_PLAYERS", "You need at least two players to start the game!");
        ok = gf_csf("TXT_OK", "OK");
        country.clear();
        for (const std::string& c : countries) {
            std::string t = camp::csf_text(env_install_dir(), "Name:" + c);
            country.push_back(t == "Name:" + c ? c : t);
        }
        static const char* kTips[][2] = {
            {"STT:SkirmishMapThumbnail", "Displays a satellite image of the chosen battlefield."},
            {"STT:SkirmishLabelGameType", "The type of game being played."},
            {"STT:SkirmishLabelScenario", "The battlefield being fought on."},
            {"STT:SkirmishButtonStartGame", "Start the game."},
            {"STT:SkirmishButtonChooseMap", "Use this to select a battlefield."},
            {"STT:SkirmishButtonBack", "Return to previous menu."},
            {"STT:SkirmishEditPlayer", "Enter your name here."},
            {"STT:SkirmishComboAIPlayer", "Add an opponent."},
            {"STT:SkirmishPictureFlag", "The flag of the chosen nationality."},
            {"STT:SkirmishComboCountry", "Select a nationality."},
            {"STT:SkirmishComboColor", "Select a color."},
            {"STT:SkirmishCBoxShortGame", "Players win when all enemy buildings are destroyed."},
            {"STT:SkirmishCBoxRedeploys", "Players have the ability to move their ConYard after it's deployed."},
            {"STT:SkirmishCBoxCrates", "Random Power-Up Crates will appear."},
            {"STT:SkirmishCBoxSWAllowed", "Super Weapons can be built."},
            {"STT:SkirmishCBoxBuildOffAlly", "Players can build off of their allies' ConYards."},
            {"STT:SkirmishSliderSpeed", "Controls unit speed and build times."},
            {"STT:SkirmishSliderCredits", "Sets the starting credits."},
            {"STT:SkirmishSliderUnit", "Sets the number of starting units."},
            {"STT:PlayerNone", "No player."},
            {"STT:PlayerDumbAI", "An easy computer opponent."},
            {"STT:PlayerSmartAI", "A medium computer opponent."},
            {"STT:PlayerGeniusAI", "A brutal computer opponent."},
        };
        for (const auto& t : kTips) tip[t[0]] = gf_csf(t[0], t[1]);
    }
};

// Replace the first "%d" of a CSF format string.
inline std::string fmt_d(const std::string& f, int v) {
    const std::size_t k = f.find("%d");
    return k == std::string::npos ? f : f.substr(0, k) + std::to_string(v) + f.substr(k + 2);
}

}  // namespace skp

class SkirmishSetupScene : public Scene {
public:
    SkirmishSetupScene(const Rules& rules, bool no_audio, const MenuArt& art,
                       bool page_transition = false)
        : rules_(rules), no_audio_(no_audio), art_(art),
          enter_sound_pending_(page_transition) {
        countries_ = gf_lobby_countries(true);
        text_.load(countries_);
        // [Rules] [MultiplayerDialogSettings] defaults + slider ranges.
        auto ri = [&](const char* k, int d) {
            return static_cast<int>(rules.section_double("MultiplayerDialogSettings", k, d));
        };
        auto rb = [&](const char* k, bool d) {
            const std::string v = rules.section_string("MultiplayerDialogSettings", k, d ? "yes" : "no");
            return v.empty() ? d : (v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' || v[0] == '1');
        };
        ru_.money = ri("Money", 10000);
        ru_.min_money = ri("MinMoney", 5000);
        ru_.max_money = std::max(ru_.min_money, ri("MaxMoney", 10000));
        ru_.money_step = std::max(1, ri("MoneyIncrement", 100));
        ru_.unit_count = ri("UnitCount", 10);
        ru_.min_units = ri("MinUnitCount", 0);
        ru_.max_units = std::max(ru_.min_units, ri("MaxUnitCount", 10));
        ru_.game_speed = ri("GameSpeed", 1);
        ru_.short_game = rb("ShortGame", true);
        ru_.superweapons = rb("SuperWeaponsAllowed", true);
        ru_.build_off_ally = rb("BuildOffAlly", true);
        ru_.mcv_repacks = rb("MCVRedeploys", true);
        ru_.crates = rb("Crates", true);
        ru_.tech_level = ri("TechLevel", 10);
        // [RE: maps.md §2] The Battle map list. Guarded: a partial install
        // keeps an empty list and the engine's historical default map.
        try {
            catalog_ = std::make_unique<ra2engine::MapCatalog>(
                ra2engine::load_yr_map_catalog(env_install_dir(), "standard"));
            // W5-PRES R-105: hide the YR-only DESERT/NEWURBAN maps (drawn with
            // temperate art) -- a presentation choice, see maplist.hpp.
            for(const auto& rejected:catalog_->rejected_addons)std::cerr<<"[addon-map] "<<rejected<<"\n";
            const int hidden = ra2engine::filter_lobby_theaters(*catalog_);
            std::cerr << "[lobby] theater filter hid " << hidden << " YR-only maps" << std::endl;
        } catch (const std::exception&) { catalog_.reset(); }
        // Session restore (0x683FC0): RA2.INI when the real game published
        // its settings (never under --menu-shot / gates), else first run.
        st_ = ra2engine::lobby_first_run(ru_);
        if (g_user_settings)
            st_ = ra2engine::lobby_load(read_settings_doc(settings_file_path()), st_, ru_, countries_);
        map_ix_ = 0;
        if (maps().empty()) map_ix_ = -1;
        else {
            int by_file = -1;
            for (int i = 0; i < static_cast<int>(maps().size()); ++i)
                if (!st_.scen_file.empty() && maps()[(size_t)i].file_name == st_.scen_file) by_file = i;
            if (by_file >= 0) map_ix_ = by_file;
            else if (st_.scen_index >= 0 && st_.scen_index < static_cast<int>(maps().size()))
                map_ix_ = st_.scen_index;
        }
        enter_customize();mode_=MODE_SETUP;
        // [RE-FALLBACK] an empty Handle shows the CSF "Player" default.
        if (st_.name.empty()) st_.name = gf_csf("GUI:Player", "Player");
        ra2engine::lobby_apply_max_players(st_, map_max());
        std::cerr << "[lobby] maps=" << maps().size() << " map=" << map_file()
                  << " max=" << map_max() << " ai=" << ra2engine::lobby_ai_count(st_) << "\n";
    }
    ~SkirmishSetupScene() override { set_editing(false); set_searching(false); }

    void handle(const ra2engine::InputEvent& ev) override;
    void update(double dt) override {
        clock_ += dt;
        if (!art_.audio || !art_.audio->sys.enabled || !art_.audio->sys.mixer) return;
        if (enter_sound_pending_) {
            art_.audio->sys.mixer->play("menu:slidein");
            enter_sound_pending_ = false;
        }
        if (!art_.audio->sys.mixer->playing("menu:grinder")) art_.audio->sys.mixer->play("menu:grinder");
        art_.audio->sys.out.pump(*art_.audio->sys.mixer);
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) override;
    Scene* next() override { Scene* n = next_; next_ = nullptr; return n; }
    const char* name() const override { return "lobby"; }
    int logical_width() const override {return menu_canvas_width();}
    int logical_height() const override {return menu_canvas_height();}
    bool locate(const std::string& what, int& x, int& y) const override {
        const std::map<std::string, ra2engine::SbRect> t = targets();
        const auto it = t.find(what);
        if (it == t.end()) return false;
        const ra2engine::SbRect d = xform().hit(it->second);
        x = d.x + d.w / 2;
        y = d.y + d.h / 2;
        return true;
    }
    void play_probe(ra2play::Probe& p) override;

private:
    enum Mode { MODE_SETUP, MODE_CUSTOMIZE };
    enum Combo { CB_NONE, CB_AI, CB_COUNTRY, CB_COLOR, CB_TEAM, CB_SPAWN, CB_MAP_FILTER };
    enum Check { CK_SHORT, CK_MCV, CK_CRATES, CK_SW, CK_BOA, CK_COUNT };
    enum Slider { SL_SPEED, SL_CREDITS, SL_UNITS, SL_COUNT };
    enum Act { A_NONE, A_START, A_CHOOSE, A_BACK, A_USEMAP, A_RANDMAP, A_CANCEL };

    static constexpr int kWinW = 1280, kWinH = 720;
    static ShellXform xform() {
        // Fill wide landscape canvases horizontally while retaining the
        // 640x480 control layout and its vertical sizing.
        const double aspect = static_cast<double>(g_ui_w) / std::max(1, g_ui_h);
        if (aspect > 640.0 / 480.0) {
            const double sx = static_cast<double>(g_ui_w) / 640.0;
            const double sy = static_cast<double>(g_ui_h) / 480.0;
            return {sy, 0, 0, false, sx, sy};
        }
        const double s = std::min(g_ui_w / 640.0, g_ui_h / 480.0);
        return {s, (g_ui_w - 640 * s) / 2, (g_ui_h - 480 * s) / 2, false};
    }
    static void to_logical(int dx, int dy, int& lx, int& ly) {
        const ShellXform xf = xform();
        const auto point=xf.logical(dx,dy);lx=point.first;ly=point.second;
    }

    // --- §1.2-§1.4 template rects (LOGICAL 640x480) ---
    static int rowY(int k) { return 30 + 24 * k; }
    static ra2engine::SbRect name_rect() { return {200,30,72,19}; }
    static ra2engine::SbRect ai_rect(int k) { return {200,rowY(k),72,19}; }
    static ra2engine::SbRect flag_rect(int k) { return {277,rowY(k),32,19}; }
    static ra2engine::SbRect country_rect(int k) { return {315,rowY(k),95,19}; }
    static ra2engine::SbRect color_rect(int k) { return {416,rowY(k),45,19}; }
    static ra2engine::SbRect spawn_rect(int k) {return {503,rowY(k),25,19};}
    static ra2engine::SbRect team_rect(int k) { return {467,rowY(k),34,19}; }
    static ra2engine::SbRect check_rect(int c) {
        static const ra2engine::SbRect k[CK_COUNT] = {
            {535,30,95,16}, {535,55,95,16}, {535,80,95,16}, {535,105,95,16},
            {535,130,95,17}};
        return k[c];
    }
    static ra2engine::SbRect slider_rect(int s) { return {535,172+s*43,95,21}; }
    static ra2engine::SbRect slider_label(int s) { return {535,slider_rect(s).y-19,95,17}; }
    static ra2engine::SbRect preview_rect() { return {200,240,328,180}; }
    static ra2engine::SbRect status_rect() { return {200, 420, 328, 14}; }
    static ra2engine::SbRect type_list_rect() { return {30, 126, 195, 260}; }
    static ra2engine::SbRect map_list_rect() { return {10,30,180,378}; }
    static ra2engine::SbRect map_filter_rect() { return {10,5,180,20}; }
    static ra2engine::SbRect map_search_rect() { return {10,411,180,18}; }

    static constexpr int kBtnX = 484, kBtnW = 156, kBtnH = 42;
    std::vector<ShellButton> buttons() const {
        auto btn = [](int row, const std::string& s, bool en, int act) {
            return ShellButton{{kBtnX, 199 + 42 * row, kBtnW, kBtnH}, s, en, act, PAGE_SINGLE};
        };
        if (mode_ == MODE_SETUP) return {ShellButton{{10,435,140,35},text_.start,true,A_START,PAGE_SINGLE},
            ShellButton{{490,435,140,35},text_.back,true,A_BACK,PAGE_SINGLE}};
        if (mode_ == MODE_CUSTOMIZE)   // dialog 0x6B (§1.7)
            return {btn(0, text_.use_map, true, A_USEMAP), btn(1, text_.random_map, false, A_RANDMAP),
                    btn(5, text_.cancel, true, A_CANCEL)};
        return {btn(1, text_.start, true, A_START), btn(2, text_.choose, true, A_CHOOSE),
                btn(5, text_.back, true, A_BACK)};
    }

    // --- map catalog ---
    const std::vector<ra2engine::MapEntry>& maps() const {
        static const std::vector<ra2engine::MapEntry> kEmpty;
        return catalog_ ? catalog_->entries : kEmpty;
    }
    const ra2engine::MapEntry* map_at(int i) const {
        return (i >= 0 && i < static_cast<int>(maps().size())) ? &maps()[(size_t)i] : nullptr;
    }
    std::string map_file() const { const auto* m = map_at(map_ix_); return m ? m->file_name : ""; }
    std::string map_filter_label() const {
        switch (map_filter_) {
            case ra2engine::MapCatalogFilter::Addon: return "Maps: Custom";
            case ra2engine::MapCatalogFilter::Standard: return "Maps: Official";
            default: return "Maps: All";
        }
    }
    void rebuild_map_list() {
        map_list_ = ShellList{};
        map_list_.r = map_list_rect();
        const std::vector<int> indices = catalog_
            ? ra2engine::map_catalog_search_indices(maps(), map_filter_, map_search_)
            : std::vector<int>{};
        for (int i : indices) {
            const auto& entry = maps()[static_cast<std::size_t>(i)];
            map_list_.items.push_back(entry.display_name);
            map_list_.data.push_back(i);
        }
        map_list_.visible = 23;
        map_list_.sel = -1;
        for (int i = 0; i < static_cast<int>(map_list_.data.size()); ++i) {
            if (map_list_.data[static_cast<std::size_t>(i)] == map_ix_) {
                map_list_.sel = i;
                break;
            }
        }
        map_list_.reveal(map_list_.sel);
    }
    int map_index_at_list(int list_index) const {
        return list_index >= 0 && list_index < static_cast<int>(map_list_.data.size())
                   ? map_list_.data[static_cast<std::size_t>(list_index)] : -1;
    }
    int map_max_of(int i) const {
        const auto* m = map_at(i);
        return (m && m->max_players > 1) ? std::min(8, m->max_players) : 8;
    }
    int map_max() const { return map_max_of(map_ix_); }
    int visible_rows() const { return ra2engine::lobby_visible_rows(map_max()); }

    // --- sliders: value <-> trackbar position ---
    int slider_range(int s) const {
        if (s == SL_SPEED) return 6;
        if (s == SL_CREDITS) return (ru_.max_money - ru_.min_money) / ru_.money_step;
        return ru_.max_units - ru_.min_units;
    }
    int slider_pos(int s) const {
        if (s == SL_SPEED) return 6 - st_.game_speed;                       // pos = 6 - GameSpeed
        if (s == SL_CREDITS) return (st_.credits - ru_.min_money) / ru_.money_step;
        return st_.unit_count - ru_.min_units;
    }
    void set_slider_pos(int s, int p) {
        p = std::max(0, std::min(slider_range(s), p));
        if (s == SL_SPEED) st_.game_speed = 6 - p;
        else if (s == SL_CREDITS) st_.credits = ru_.min_money + p * ru_.money_step;
        else st_.unit_count = ru_.min_units + p;
    }
    // The readout shows the trackbar position (credits: the value itself).
    std::string slider_text(int s) const {
        if (s == SL_CREDITS) return std::to_string(st_.credits);
        if (s == SL_UNITS) return std::to_string(st_.unit_count);
        return std::to_string(slider_pos(s));
    }
    bool* check_ptr(int c) {
        switch (c) {
            case CK_SHORT: return &st_.short_game;
            case CK_MCV: return &st_.mcv_repacks;
            case CK_CRATES: return &st_.crates;
            case CK_SW: return &st_.superweapons;
            default: return &st_.build_off_ally;
        }
    }
    bool check_val(int c) const { return *const_cast<SkirmishSetupScene*>(this)->check_ptr(c); }
    std::string check_label(int c) const {
        switch (c) {
            case CK_SHORT: return text_.short_game;
            case CK_MCV: return text_.mcv;
            case CK_CRATES: return text_.crates;
            case CK_SW: return text_.sw;
            default: return text_.boa;
        }
    }
    static const char* check_id(int c) {
        static const char* k[CK_COUNT] = {"short", "mcv", "crates", "superweapons", "buildoffally"};
        return k[c];
    }
    static const char* slider_id(int s) {
        static const char* k[SL_COUNT] = {"speed", "credits", "units"};
        return k[s];
    }
    // Build Off Ally is visible but WS_DISABLED in RA2 (style 0x58000003).
    static bool check_enabled(int c) { return c != CK_BOA; }

    // --- row labels ---
    std::string ai_label(int ai) const {
        switch (ai) {
            case ra2engine::kLobbyAiEasy: return text_.ai[1];
            case ra2engine::kLobbyAiMedium: return text_.ai[2];
            case ra2engine::kLobbyAiBrutal: return text_.ai[3];
            case ra2engine::kLobbyAiDebug: return "DEBUG-AI";
            default: return text_.ai[0];
        }
    }
    std::string country_label(int c) const {
        if(c>=0 && c<static_cast<int>(countries_.size()) && countries_[c]=="YuriCountry")
            return c<static_cast<int>(text_.country.size()) && text_.country[c]!="YuriCountry" ? text_.country[c] : "Yuri";
        return (c >= 0 && c < static_cast<int>(text_.country.size())) ? text_.country[(size_t)c]
                                                                        : text_.random_country;
    }
    std::string color_label(int c) const {
        return (c >= 0 && c < 8) ? text_.color_abbrev[c] : text_.random_color;
    }
    static SDL_Color swatch_of(int c) {
        if (c < 0) return SDL_Color{0, 0, 0, 0};
        Uint8 r, g, b;
        ra2engine::lobby_color_rgb(c, r, g, b);
        return SDL_Color{r, g, b, 255};
    }

    // --- dropped list (one at a time; owns input while open) ---
    void open_combo(Combo kind, int row) {
        combo_ = kind;
        combo_row_ = row;
        list_ = ShellList{};
        ra2engine::SbRect face = kind == CB_MAP_FILTER ? map_filter_rect() :
                                 kind == CB_AI ? ai_rect(row) : kind == CB_COUNTRY ? country_rect(row)
                                                                                 : kind == CB_TEAM ? team_rect(row) : kind==CB_SPAWN?spawn_rect(row):color_rect(row);
        int cur = 0;
        if (kind == CB_MAP_FILTER) {
            list_.items = {"Maps: All", "Maps: Custom", "Maps: Official"};
            list_.data = {static_cast<int>(ra2engine::MapCatalogFilter::All),
                          static_cast<int>(ra2engine::MapCatalogFilter::Addon),
                          static_cast<int>(ra2engine::MapCatalogFilter::Standard)};
            cur = map_filter_ == ra2engine::MapCatalogFilter::All ? 0
                : map_filter_ == ra2engine::MapCatalogFilter::Addon ? 1 : 2;
            list_.visible = 3;
        } else if (kind == CB_AI) {
            const int order[5] = {ra2engine::kLobbyAiNone, ra2engine::kLobbyAiEasy,
                                  ra2engine::kLobbyAiMedium, ra2engine::kLobbyAiBrutal, ra2engine::kLobbyAiDebug};
            for (int v : order) {
                list_.items.push_back(ai_label(v));
                list_.data.push_back(v);
                if (v == st_.rows[row].ai) cur = static_cast<int>(list_.data.size()) - 1;
            }
            list_.visible = 5;
        } else if (kind == CB_COUNTRY) {
            list_.items.push_back(text_.random_country);
            list_.data.push_back(ra2engine::kLobbyRandom);
            for (int c = 0; c < static_cast<int>(countries_.size()); ++c) {
                if(row>0 && countries_[c]=="YuriCountry" && st_.rows[row].ai!=ra2engine::kLobbyAiDebug) continue;
                list_.items.push_back(country_label(c));
                list_.data.push_back(c);
                if (c == st_.rows[row].country) cur = static_cast<int>(list_.data.size()) - 1;
            }
            list_.visible = 7;                 // msg 0x4DE, 7
        } else if(kind==CB_SPAWN) {
            for(int start=-1;start<map_max();++start){list_.items.push_back(start<0?"?":std::to_string(start+1));list_.data.push_back(start);if(start==st_.rows[row].spawn)cur=int(list_.data.size())-1;}
            list_.visible=8;
        } else if(kind==CB_TEAM) {
            for(int team=-1;team<ra2engine::kLobbyTeams;++team) {
                list_.items.push_back(ra2engine::lobby_team_label(team));
                list_.data.push_back(team);
                if(team==st_.rows[row].team) cur=static_cast<int>(list_.data.size())-1;
            }
            list_.visible=8;
        } else {
            for (int c : ra2engine::lobby_color_choices(st_, row)) {
                list_.items.push_back(color_label(c));
                list_.data.push_back(c);
                list_.swatch.push_back(swatch_of(c));
                if (c == st_.rows[row].color) cur = static_cast<int>(list_.data.size()) - 1;
            }
            list_.visible = 9;                 // msg 0x4DE, 9
        }
        list_.sel = cur;
        const int h = list_.rows() * list_.item_h + 2;
        int y = face.y + face.h;
        if (y + h > 448) y = face.y - h;       // drop up near the bottom bar
        const int extra = (kind == CB_COLOR) ? 22 : 0;   // GLD + swatch + strip
        list_.r = {face.x, y, face.w + extra, h};
        list_.reveal(cur);
        play_ui("menu:acbopen");
    }
    void close_combo(bool commit) {
        if (combo_ == CB_NONE) return;
        if (commit && list_.sel >= 0 && list_.sel < static_cast<int>(list_.data.size())) {
            const int v = list_.data[(size_t)list_.sel];
            if (combo_ == CB_MAP_FILTER) {
                map_filter_ = static_cast<ra2engine::MapCatalogFilter>(v);
                rebuild_map_list();
            } else if (combo_ == CB_AI) ra2engine::lobby_set_ai(st_, combo_row_, v);
            else if (combo_ == CB_COUNTRY) ra2engine::lobby_set_country(st_, combo_row_, v);
            else if(combo_==CB_SPAWN) st_.rows[combo_row_].spawn=v;
            else if(combo_==CB_TEAM) st_.rows[combo_row_].team=v;
            else ra2engine::lobby_set_color(st_, combo_row_, v);
        }
        combo_ = CB_NONE;
        play_ui("menu:acbclose");
    }
    void set_editing(bool on) {
        // W9-04-02: a tap on the edit while editing brings the keyboard back
        // after the IME's Back hid it.
        if (on == editing_ && (!on || SDL_IsTextInputActive())) return;
        if (on) set_searching(false);
        const bool was = editing_;
        editing_ = on;
        // Show the soft keyboard on Android; restore the previous text-input
        // state on leave (desktop SDL starts with text input active).
        if (on) {
            if (!was) text_was_active_ = SDL_IsTextInputActive();
            const ra2engine::SbRect e = name_rect();   // W2-AR-15: keep the edit in view
            const SDL_Rect d = xform().dev(e.x, e.y, e.w, e.h);
            // W9-04-01: the IME starts from the name (its delete key erases it).
            ime_open(d.x, d.y, d.w, d.h, st_.name, ra2engine::kOsImeText, ra2engine::kLobbyNameMax);
        }
        else if (ime_screen_keyboard() || !text_was_active_) SDL_StopTextInput();   // W9-04-03
    }
    void set_searching(bool on) {
        if (on == search_editing_ && (!on || SDL_IsTextInputActive())) return;
        if (on) {
            set_editing(false);
            if (!search_editing_) text_was_active_ = SDL_IsTextInputActive();
            search_editing_ = true;
            const ra2engine::SbRect r = map_search_rect();
            const SDL_Rect d = xform().dev(r.x, r.y, r.w, r.h);
            ime_open(d.x, d.y, d.w, d.h, map_search_, ra2engine::kOsImeText, 64);
        } else {
            search_editing_ = false;
            if (ime_screen_keyboard() || !text_was_active_) SDL_StopTextInput();
        }
    }
    void play_ui(const char* key) const {
        if (art_.audio && art_.audio->sys.enabled && art_.audio->sys.mixer)
            art_.audio->sys.mixer->play(key);
    }

    // --- Customize Battle (dialog 0x6B) ---
    void enter_customize() {
        mode_ = MODE_CUSTOMIZE;
        type_list_ = ShellList{};
        type_list_.r = type_list_rect();
        type_list_.items = {text_.battle};
        type_list_.data = {1};
        type_list_.sel = 0;
        type_list_.visible = 16;
        rebuild_map_list();
    }
    void leave_customize(bool use) {
        if (use && map_index_at_list(map_list_.sel) >= 0) {
            map_ix_ = map_index_at_list(map_list_.sel);
            ra2engine::lobby_apply_max_players(st_, map_max());   // 0x683920 / 0x6836F0
        }
        mode_ = MODE_SETUP;
    }
    // The map shown in the preview well (the pending pick on 0x6B).
    int shown_map() const {
        return mode_ == MODE_CUSTOMIZE && map_index_at_list(map_list_.sel) >= 0
                   ? map_index_at_list(map_list_.sel) : map_ix_;
    }

    // --- persistence (0x6829F0 commit -> 0x00670380 / 0x005D91B0) ---
    void commit_and_save() {
        st_.scen_index = std::max(0, map_ix_);
        st_.scen_file = map_file();
        if (!g_user_settings) return;              // --menu-shot / gates: never touch a file
        g_user_settings->game_speed = st_.game_speed;   // OptionsClass+0 (0x00A40B18)
        const std::string path = settings_file_path();
        if (path.empty()) return;
        ra2engine::IniDoc doc = read_settings_doc(path);
        ra2engine::lobby_store(st_, doc, countries_);
        ra2engine::store_settings(*g_user_settings, doc);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { std::cerr << "[lobby] cannot write " << path << "\n"; return; }
        const std::string t = doc.serialize();
        f.write(t.data(), static_cast<std::streamsize>(t.size()));
        std::cerr << "[lobby] saved " << path << "\n";
    }
    ra2engine::SkirmishConfig build_config(std::uint32_t seed) const {
        ra2engine::LobbyConvertCtx ctx;
        ctx.countries = countries_;
        const bool explicit_yuri=st_.rows[0].country>=0 && st_.rows[0].country<static_cast<int>(countries_.size()) &&
                                 countries_[st_.rows[0].country]=="YuriCountry";
        ctx.country_allowed=[this,explicit_yuri](int row,const std::string& c){
            return c!="YuriCountry" || (row==0 && explicit_yuri) ||
                   (row>0 && st_.rows[row].ai==ra2engine::kLobbyAiDebug);
        };
        const Rules* r = &rules_;
        ctx.side_of = [r](const std::string& c) { return gf_side_of_country(*r, c); };
        ctx.seed = seed;
        ctx.tech_level = ru_.tech_level;
        ctx.map_file = map_file();
        return ra2engine::lobby_to_config(st_, ctx);
    }
    void press(int act, const ra2engine::InputEvent& ev) {
        play_ui("menu:click");
        switch (act) {
            case A_START: {
                const ra2engine::LobbyCheck chk = ra2engine::lobby_validate(st_, map_max());
                if (chk == ra2engine::LobbyCheck::ScenarioTooSmall) {
                    confirm_.ask(skp::fmt_d(text_.too_small, map_max()), {text_.ok});
                    return;
                }
                if (chk == ra2engine::LobbyCheck::NeedTwoPlayers) {
                    confirm_.ask(text_.need_two, {text_.ok});
                    return;
                }
                if(chk==ra2engine::LobbyCheck::NeedEnemy) {
                    confirm_.ask("Choose at least one enemy team or a player with no team (-).",{text_.ok});
                    return;
                }
                commit_and_save();
                // Random resolution seed: the click's timestamp (virtual and
                // deterministic under --play) mixed with the map.
                const std::uint32_t seed = gf_seed_of(map_file()) ^ (ev.t_ms * 2654435761u) ^ 0x9E3779B9u;
                ra2engine::SkirmishConfig cfg = build_config(seed);
                std::cerr << "[lobby] start map=" << cfg.map_file << " slots=" << cfg.slots.size()
                          << " speed=" << cfg.game_speed << " units=" << cfg.unit_count
                          << " cash=" << cfg.starting_cash << "\n";
                g_skirmish_requested_starts.clear();for(int row=0;row<ra2engine::kLobbyRows;++row)if(ra2engine::lobby_row_active(st_,row))g_skirmish_requested_starts.push_back(st_.rows[row].spawn);
                next_ = defer_game_scene(no_audio_, cfg);   // W2-AR-02/-10: built under the loading screen (ctor runs launch_skirmish)
                return;
            }
            case A_CHOOSE: enter_customize(); return;
            case A_BACK:
                commit_and_save();                       // Back commits + saves too (§1.5)
                if (art_.audio && art_.audio->sys.enabled && art_.audio->sys.mixer)
                    art_.audio->sys.mixer->play("menu:slideout");
                next_ = new ShellScene(rules_, no_audio_, art_, PAGE_SINGLE, true);
                return;
            case A_USEMAP: leave_customize(true); return;
            case A_CANCEL: leave_customize(false); return;
            default: return;
        }
    }

    // --- tooltips (status line 0x695, dispatcher 0x5E22B0) ---
    std::string tooltip_at(int lx, int ly) const;
    std::map<std::string, ra2engine::SbRect> targets() const;   // LOGICAL rects

    const Rules& rules_;
    bool no_audio_ = false;
    const MenuArt& art_;
    std::vector<std::string> countries_;
    skp::Text text_;
    ra2engine::LobbyRulesInfo ru_;
    ra2engine::LobbyState st_;
    std::unique_ptr<ra2engine::MapCatalog> catalog_;
    int map_ix_ = -1;
    ra2engine::MapCatalogFilter map_filter_ = ra2engine::MapCatalogFilter::All;
    Mode mode_ = MODE_SETUP;
    Combo combo_ = CB_NONE;
    int combo_row_ = 0;
    ShellList list_, type_list_, map_list_;
    bool editing_ = false;
    bool search_editing_ = false;
    std::string map_search_;
    bool text_was_active_ = true;
    bool enter_sound_pending_ = false;
    int drag_ = -1;
    int mx_ = -1, my_ = -1;
    bool down_ = false;
    double clock_ = 0.0;
    GfConfirm confirm_;
    Scene* next_ = nullptr;
    // Preview cache: map file -> decoded size (0 = no preview).
    mutable std::map<std::string, std::pair<int, int>> preview_size_;
    mutable std::map<std::string,std::vector<std::pair<int,int>>> preview_starts_;
    SDL_Texture* preview_tex(ra2engine::TextureCache& tc, int map_i) const {
        const auto* m = map_at(map_i);
        if (!m || !catalog_) return nullptr;
        const std::string key = "skp:prev:" + m->file_name;
        if (void* t = tc.peek(key)) return static_cast<SDL_Texture*>(t);
        if (preview_size_.count(m->file_name)) return nullptr;   // known: no preview
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        try {
            {
                const auto raw=ra2engine::read_catalog_map(*catalog_,m->file_name);
                const std::string text(raw.begin(),raw.end());
                skp::decode_preview(text,w,h,rgba);
                const auto ini=Ini::parse(text);
                const int sx=std::atoi(ini.get("Header","StartX").c_str()),sy=std::atoi(ini.get("Header","StartY").c_str());
                for(int i=1;i<=8;++i){int x=0,y=0;
                    const auto point=ini.get("Header","Waypoint"+std::to_string(i));
                    if(std::sscanf(point.c_str(),"%d,%d",&x,&y)==2 && (x||y))
                        preview_starts_[m->file_name].push_back({(x-sx)*2,y-sy});}
                if(preview_starts_[m->file_name].empty()) {
                    int lx=0,ly=0,lw=0,lh=0,mx=0,my=0,mw=0,mh=0;
                    const auto local=ini.get("Map","LocalSize"),size=ini.get("Map","Size");
                    std::sscanf(local.c_str(),"%d,%d,%d,%d",&lx,&ly,&lw,&lh);
                    std::sscanf(size.c_str(),"%d,%d,%d,%d",&mx,&my,&mw,&mh);
                    if(lw<=0)lw=mw;if(lh<=0)lh=mh;
                    for(int i=0;i<8;++i){const auto value=ini.get("Waypoints",std::to_string(i));if(value.empty())continue;
                        const int packed=std::atoi(value.c_str()),x=packed%1000,y=packed/1000;
                        if(lw>0 && lh>0)preview_starts_[m->file_name].push_back({
                            int(((x-y+mw)*0.5-lx)*w/lw),int(((x+y-mw)*0.5-ly)*h/lh)});
                    }
                }


            }
        } catch (const std::exception&) { w = h = 0; }
        preview_size_[m->file_name] = {w, h};
        if (w <= 0 || h <= 0) return nullptr;
        return static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), w, h));
    }
};

std::string SkirmishSetupScene::tooltip_at(int lx, int ly) const {
    auto tip = [&](const char* k) {
        const auto it = text_.tip.find(k);
        return it == text_.tip.end() ? std::string() : it->second;
    };
    const std::vector<ShellButton> btns = buttons();
    for (const ShellButton& b : btns)
        if (b.rect.contains(lx, ly) && mode_ == MODE_SETUP)
            return tip(b.action == A_START ? "STT:SkirmishButtonStartGame"
                       : b.action == A_CHOOSE ? "STT:SkirmishButtonChooseMap" : "STT:SkirmishButtonBack");
    if (preview_rect().contains(lx, ly)) return tip("STT:SkirmishMapThumbnail");
    if (mode_ != MODE_SETUP) return {};
    if (combo_ == CB_AI) {   // per-item tooltips (msg 0x4E9): STT:Player*
        const int i = list_.hit(lx, ly);
        if (i >= 0) {
            const int v = list_.data[(size_t)i];
            return tip(v == ra2engine::kLobbyAiNone ? "STT:PlayerNone"
                       : v == ra2engine::kLobbyAiEasy ? "STT:PlayerDumbAI"
                       : v == ra2engine::kLobbyAiMedium ? "STT:PlayerSmartAI" : "STT:PlayerGeniusAI");
        }
    }
    if (ra2engine::SbRect{490, 167, 135, 16}.contains(lx, ly)) return tip("STT:SkirmishLabelGameType");
    if (ra2engine::SbRect{490, 188, 135, 32}.contains(lx, ly)) return tip("STT:SkirmishLabelScenario");
    if (name_rect().contains(lx, ly)) return tip("STT:SkirmishEditPlayer");
    for (int k = 0; k < visible_rows(); ++k) {
        if (k > 0 && ai_rect(k).contains(lx, ly)) return tip("STT:SkirmishComboAIPlayer");
        if (flag_rect(k).contains(lx, ly)) return tip("STT:SkirmishPictureFlag");
        if (country_rect(k).contains(lx, ly)) return tip("STT:SkirmishComboCountry");
        if (color_rect(k).contains(lx, ly)) return tip("STT:SkirmishComboColor");
        if (team_rect(k).contains(lx,ly)) return "Same team letter = allies. - = no team.";
    }
    static const char* kChk[CK_COUNT] = {"STT:SkirmishCBoxShortGame", "STT:SkirmishCBoxRedeploys",
                                         "STT:SkirmishCBoxCrates", "STT:SkirmishCBoxSWAllowed",
                                         "STT:SkirmishCBoxBuildOffAlly"};
    for (int c = 0; c < CK_COUNT; ++c)
        if (check_rect(c).contains(lx, ly)) return tip(kChk[c]);
    static const char* kSl[SL_COUNT] = {"STT:SkirmishSliderSpeed", "STT:SkirmishSliderCredits",
                                        "STT:SkirmishSliderUnit"};
    for (int s = 0; s < SL_COUNT; ++s)
        if (slider_rect(s).contains(lx, ly)) return tip(kSl[s]);
    return {};
}

std::map<std::string, ra2engine::SbRect> SkirmishSetupScene::targets() const {
    std::map<std::string, ra2engine::SbRect> t;
    for (const ShellButton& b : buttons()) {
        if (!b.enabled) continue;
        t["button:" + b.label] = b.rect;
        if (b.action == A_START) t["start"] = b.rect;          // pre-0x102 aliases
        if (b.action == A_BACK || b.action == A_CANCEL) t["back"] = b.rect;
    }
    t["map:filter"] = map_filter_rect();
    if (confirm_.open()) return t;                             // the message box owns input
    if (mode_ == MODE_CUSTOMIZE) {
        for (int i = map_list_.top; i < map_list_.top + map_list_.rows(); ++i)
            if (const auto* m = map_at(map_index_at_list(i))) t["map:" + m->file_name] = map_list_.item_rect(i);
        if (map_list_.scrolls()) {
            t["list:up"] = map_list_.strip_up();
            t["list:down"] = map_list_.strip_down();
        }
        return t;
    }
    if (mode_ == MODE_SETUP) t["edit:map_search"] = map_search_rect();
    if (combo_ != CB_NONE) {
        for (int i = list_.top; i < list_.top + list_.rows(); ++i) {
            t["item:" + std::to_string(i)] = list_.item_rect(i);
            t["pick:" + list_.items[(size_t)i]] = list_.item_rect(i);
        }
        if (list_.scrolls()) {
            t["list:up"] = list_.strip_up();
            t["list:down"] = list_.strip_down();
        }
        return t;
    }
    t["edit:name"] = name_rect();
    for (int i = map_list_.top; i < map_list_.top + map_list_.rows(); ++i)
        if (const auto* m = map_at(map_index_at_list(i))) t["map:" + m->file_name] = map_list_.item_rect(i);
    for (int k = 0; k < visible_rows(); ++k) {
        const std::string n = std::to_string(k);
        if (k > 0) t["combo:ai" + n] = ai_rect(k);
        if (ra2engine::lobby_row_active(st_, k)) {
            t["combo:country" + n] = country_rect(k);
            t["combo:color" + n] = color_rect(k);
            t["combo:team" + n] = team_rect(k);
        }
    }
    for (int c = 0; c < CK_COUNT; ++c)
        if (check_enabled(c)) t[std::string("check:") + check_id(c)] = {check_rect(c).x, check_rect(c).y, 18, 16};
    for (int s = 0; s < SL_COUNT; ++s) {
        const ra2engine::SbRect r = slider_rect(s);
        const int range = slider_range(s);
        for (int p = 0; p <= range; ++p) {
            const int x = shell_trackbar_x_for(p, range, r.w, ra2engine::kTrackbarReadoutW);
            std::string v;
            if (s == SL_CREDITS) v = std::to_string(ru_.min_money + p * ru_.money_step);
            else if (s == SL_UNITS) v = std::to_string(ru_.min_units + p);
            else v = std::to_string(p);
            t[std::string("slider:") + slider_id(s) + "=" + v] = {r.x + x, r.y + r.h / 2, 1, 1};
        }
    }
    return t;
}

void SkirmishSetupScene::play_probe(ra2play::Probe& p) {
    p.scene = "lobby";
    p.set_s("page", mode_ == MODE_CUSTOMIZE ? text_.choose : text_.header);
    const ShellXform xf = xform();
    for (const auto& kv : targets()) {
        const ra2engine::SbRect d = xf.hit(kv.second);
        p.targets[kv.first] = ra2play::Rect{d.x, d.y, std::max(1, d.w), std::max(1, d.h)};
    }
    for (const ShellButton& b : buttons()) p.set("button_enabled(" + b.label + ")", b.enabled ? 1 : 0);
    static const char* kAi[] = {"None", "Brutal", "Medium", "Easy", "DEBUG-AI"};
    for (int k = 0; k < ra2engine::kLobbyRows; ++k) {
        const std::string n = "(" + std::to_string(k) + ")";
        const ra2engine::LobbyRow& r = st_.rows[k];
        p.set_s("ai" + n, k == 0 ? "Human" : kAi[r.ai + 1]);
        p.set_s("country" + n, r.country >= 0 && r.country < (int)countries_.size()
                                   ? countries_[(size_t)r.country] : "Random");
        p.set_s("side" + n, r.country < 0 || r.country >= (int)countries_.size() ? "RANDOM"
                            : gf_side_of_country(rules_, countries_[(size_t)r.country]) ==
                                      ra2engine::World::Side::Soviet ? "SOVIET" : gf_side_of_country(rules_,countries_[(size_t)r.country])==ra2engine::World::Side::Yuri ? "YURI" : "ALLIED");
        p.set("color" + n, r.color);
        p.set("team" + n,r.team);
        p.set("row_active" + n, ra2engine::lobby_row_active(st_, k) ? 1 : 0);
    }
    p.set_s("name", st_.name);
    p.set("editing", editing_ ? 1 : 0);
    p.set("ai_players", ra2engine::lobby_ai_count(st_));
    p.set("visible_rows", visible_rows());
    p.set("max_players", map_max());
    p.set("cash", st_.credits);
    p.set("unit_count", st_.unit_count);
    p.set("game_speed", st_.game_speed);
    for (int c = 0; c < CK_COUNT; ++c) p.set(std::string("check(") + check_id(c) + ")", check_val(c) ? 1 : 0);
    p.set("crates", st_.crates ? 1 : 0);
    p.set_s("combo_open", combo_ == CB_MAP_FILTER ? "map_filter" :
                           combo_ == CB_AI ? "ai" : combo_ == CB_COUNTRY ? "country"
                                            : combo_ == CB_COLOR ? "color" : "");
    p.set("combo_row", combo_ != CB_NONE ? combo_row_ : -1);
    p.set("combo_count", combo_ != CB_NONE ? static_cast<double>(list_.items.size()) : 0.0);
    p.set("map_ix", map_ix_);
    p.set("map_count", static_cast<double>(maps().size()));
    p.set_s("map_filter", map_filter_label());
    p.set_s("map_search", map_search_);
    p.set("searching_maps", search_editing_ ? 1 : 0);
    p.set("visible_map_count", static_cast<double>(map_list_.items.size()));
    if (const auto* m = map_at(map_ix_)) {
        p.set_s("map", m->file_name);
        p.set_s("map_name", m->display_name);
    }
    if (mode_ == MODE_CUSTOMIZE) p.set("map_sel", map_list_.sel);
    int slx = -1000, sly = -1000;
    if (mx_ >= 0) to_logical(mx_, my_, slx, sly);
    p.set_s("status", tooltip_at(slx, sly));
    confirm_.play_probe(p);
}

void SkirmishSetupScene::render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) {
    SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
    const ShellXform xf = xform();
    const std::vector<ShellButton> btns = buttons();
    int lmx = -1000, lmy = -1000;
    if (mx_ >= 0) to_logical(mx_, my_, lmx, lmy);
    int hover_ix = -1;
    for (int i = 0; i < static_cast<int>(btns.size()); ++i)
        if (btns[i].enabled && btns[i].rect.contains(lmx, lmy)) { hover_ix = i; break; }
    const int pressed_ix = (hover_ix >= 0 && down_) ? hover_ix : -1;
    SDL_SetRenderDrawColor(ren,12,12,22,255);SDL_RenderClear(ren);
    const ShellPainter p{ren,tc,art_,xf};
    p.fill(p.dev(8,27,184,403),18,20,30);p.fill(p.dev(198,27,332,202),18,20,30);
    p.fill(p.dev(533,27,99,396),18,20,30);
    p.combo(map_filter_rect(), map_filter_label(), true,
            combo_ == CB_MAP_FILTER);
    p.label("Players",200,5,72,20,0);p.label("Side",315,5,95,20,0);
    p.label("Color",416,5,45,20,0);p.label("Team",467,5,34,20,0);p.label("Start",503,5,28,20,0);
    if(mode_==MODE_SETUP) {
        map_list_.draw(p,false);
        if (map_list_.items.empty())
            p.label(map_filter_ == ra2engine::MapCatalogFilter::Addon
                        ? "No custom maps imported" : "No maps available",
                    14, 30, 168, 24, 1);
        p.edit(map_search_rect(), map_search_.empty() ? "Search maps..." : map_search_,
               search_editing_, std::fmod(clock_, 1.0) < 0.5);
    }
    for(const auto& button:btns){const SDL_Rect r=p.dev(button.rect);p.fill(r,65,20,24);
        SDL_SetRenderDrawColor(ren,180,135,65,255);SDL_RenderDrawRect(ren,&r);
        p.label(button.label,button.rect.x,button.rect.y,button.rect.w,button.rect.h,1,button.enabled);}

    // --- sidebar: preview well 0x468 (aspect-fit, black frame) ---
    {
        const ra2engine::SbRect pr = preview_rect();
        p.fill(p.dev(pr), 0, 0, 0);
        const int mi = shown_map();
        if (SDL_Texture* t = preview_tex(tc, mi)) {
            const auto* m = map_at(mi);
            const auto sz = preview_size_[m->file_name];
            // [RE-FALLBACK, audit Q4: scaling mode not recovered] aspect-fit,
            // centred, nearest-neighbour.
            const auto image=ra2engine::fit_skirmish_image(sz.first,sz.second,{pr.x+1,pr.y+1,pr.w-2,pr.h-2});
            const double sc=std::min(double(image.w)/sz.first,double(image.h)/sz.second);
            const int w=image.w,h=image.h;
            const SDL_Rect d=p.dev(image);
            SDL_SetTextureColorMod(t, 255, 255, 255);
            SDL_RenderCopy(ren, t, nullptr, &d);
            const auto& starts=preview_starts_[m->file_name];
            for(std::size_t i=0;i<starts.size();++i){
                const int x=image.x+int(starts[i].first*sc);
                const int y=image.y+int(starts[i].second*sc);
                if(x<pr.x || y<pr.y || x>=pr.x+pr.w || y>=pr.y+pr.h)continue;
                const auto mark=ra2engine::skirmish_marker(x,y,image);
                p.fill(p.dev(mark),12,12,12);
                p.label(std::to_string(i+1),mark.x,mark.y,mark.w,mark.h,1);
            }

        }
    }
    if (mode_ == MODE_SETUP) {

        // --- 8 player rows (hidden past the map's max) ---
        for (int k = 0; k < visible_rows(); ++k) {
            const bool active = ra2engine::lobby_row_active(st_, k);
            const ra2engine::LobbyRow& r = st_.rows[k];
            if (k == 0) {
                p.edit(name_rect(), st_.name, editing_, std::fmod(clock_, 1.0) < 0.5);
            } else {
                p.combo(ai_rect(k), ai_label(r.ai), true, combo_ == CB_AI && combo_row_ == k);
            }
            // Flag (FUN_004D1120): country PCX, rani.pcx for Random / None.
            const bool yuri=active && r.country>=0 && r.country<int(countries_.size()) && countries_[r.country]=="YuriCountry";
            const int fi = yuri?11:(active && r.country >= 0 && r.country < 9) ? r.country : 9;
            const ra2engine::SbRect fr = flag_rect(k);
            if (art_.flags[fi]) {
                const std::string key = "skp:flag" + std::to_string(fi);
                p.pcx(key.c_str(),art_.flags[fi],p.dev(ra2engine::fit_skirmish_image(art_.flags[fi]->w,art_.flags[fi]->h,fr)),true);
            } else if(yuri) {p.fill(p.dev(fr),65,25,85);p.label("Y",fr.x,fr.y,fr.w,fr.h,1);}
            p.combo(country_rect(k), active ? country_label(r.country) : text_.random_country, active,
                    combo_ == CB_COUNTRY && combo_row_ == k);
            const SDL_Color sw = swatch_of(active ? r.color : -1);
            p.combo(color_rect(k), active ? color_label(r.color) : text_.random_color, active,
                    combo_ == CB_COLOR && combo_row_ == k, sw.a ? &sw : nullptr);
            p.combo(spawn_rect(k),r.spawn<0?"?":std::to_string(r.spawn+1),active,combo_==CB_SPAWN && combo_row_==k);
            p.combo(team_rect(k),ra2engine::lobby_team_label(r.team),active,
                    combo_==CB_TEAM && combo_row_==k);
        }
        // --- options: checkboxes (left), labelled sliders (right) ---
        p.header(p.fit(map_at(shown_map())?map_at(shown_map())->display_name:"Map preview",328),200,220,328,18);
        for (int c = 0; c < CK_COUNT; ++c)
            p.checkbox(check_rect(c), check_val(c), check_label(c), check_enabled(c));
        const std::string sl_lbl[SL_COUNT] = {text_.speed, text_.credits, text_.units};
        for (int s = 0; s < SL_COUNT; ++s) {
            const ra2engine::SbRect lr = slider_label(s);
            p.label(sl_lbl[s], lr.x, lr.y, lr.w, lr.h, 0);
            p.trackbar(slider_rect(s), slider_pos(s), slider_range(s), true, slider_text(s), true);
        }
    } else {
        // --- dialog 0x6B: Select Engagement / Game Type / Game Map ---
        p.header(text_.sel_engagement, 34, 32, 385, 19);
        p.label(text_.game_type, 30, 97, 195, 16, 0);
        p.label(text_.game_map, 252, 97, 195, 16, 0);
        type_list_.draw(p, false);
        map_list_.draw(p, false);
        if (map_list_.items.empty())
            p.label(map_filter_ == ra2engine::MapCatalogFilter::Addon
                        ? "No custom maps imported" : "No maps available",
                    14, 30, 168, 24, 1);
    }
    if (combo_ != CB_NONE) list_.draw(p);

    // --- status line 0x695: the hovered control's STT:* tooltip ---
    const bool yuri_preview=st_.rows[0].country>=0 && st_.rows[0].country<static_cast<int>(countries_.size()) && countries_[st_.rows[0].country]=="YuriCountry";
    if (yuri_preview || !g_user_settings || g_user_settings->tooltips) {
        std::string tip = tooltip_at(lmx, lmy);
        if(st_.rows[0].country>=0 && st_.rows[0].country<static_cast<int>(countries_.size()) &&
           countries_[st_.rows[0].country]=="YuriCountry") tip="Yuri preview: special abilities / superweapons incomplete.";
        const ra2engine::SbRect s = status_rect();
        p.label(p.fit(tip, s.w), s.x, s.y, s.w, s.h, 0);
    }
    confirm_.render(ren, tc);   // W2-UI-17 popup 0x120
}

void SkirmishSetupScene::handle(const ra2engine::InputEvent& ev) {
    using ra2engine::InputEvent;
    if (ev.kind == InputEvent::PointerMove || ev.kind == InputEvent::PointerDown ||
        ev.kind == InputEvent::PointerUp) {
        mx_ = ev.x; my_ = ev.y;
    }
    if (ev.button == InputEvent::Left) {
        if (ev.kind == InputEvent::PointerDown) down_ = true;
        if (ev.kind == InputEvent::PointerUp) down_ = false;
    }
    // The validation message box owns input until OK.
    if (confirm_.open()) { confirm_.handle(ev); return; }
    int lx = 0, ly = 0;
    to_logical(ev.x, ev.y, lx, ly);

    // --- keyboard ---
    if (ev.kind == InputEvent::Key) {
        if (combo_ != CB_NONE) {               // the dropped list's keys
            if (ev.key == ra2engine::kKeyEscape) close_combo(false);
            else if (ev.key == ra2engine::kKeyEnter) close_combo(true);
            else if (ev.key == ra2engine::kKeyUp && list_.sel > 0) list_.reveal(--list_.sel);
            else if (ev.key == ra2engine::kKeyDown && list_.sel + 1 < (int)list_.items.size())
                list_.reveal(++list_.sel);
            return;
        }
        if (search_editing_) {
            if (ev.key == ra2engine::kKeyBackspace) {
                if (!map_search_.empty()) {
                    map_search_.pop_back();
                    rebuild_map_list();
                }
                return;
            }
            if (ev.key == ra2engine::kKeyEnter) { set_searching(false); return; }
        }
        if (editing_) {
            if (ev.key == ra2engine::kKeyBackspace) { if (!st_.name.empty()) st_.name.pop_back(); return; }
            if (ev.key == ra2engine::kKeyEnter) { set_editing(false); return; }
        }
        if (ev.key == ra2engine::kKeyEscape) {   // Esc = the dialog's Cancel = Back / Cancel
            if (search_editing_) { set_searching(false); return; }
            set_editing(false);
            if (mode_ == MODE_CUSTOMIZE) leave_customize(false);
            else press(A_BACK, ev);
            return;
        }
        if (mode_ == MODE_CUSTOMIZE) {
            if (ev.key == ra2engine::kKeyUp && map_list_.sel > 0) map_list_.reveal(--map_list_.sel);
            else if (ev.key == ra2engine::kKeyDown && map_list_.sel + 1 < (int)map_list_.items.size())
                map_list_.reveal(++map_list_.sel);
            else if (ev.key == ra2engine::kKeyEnter) leave_customize(true);
        }
        return;
    }
    if (ev.kind == InputEvent::Text) {
        if (search_editing_) {
            for (const char* c = ev.text; *c; ++c) {
                const unsigned char ch = static_cast<unsigned char>(*c);
                if (ch >= 32 && ch < 127 && map_search_.size() < 64)
                    map_search_ += static_cast<char>(ch);
            }
            rebuild_map_list();
            return;
        }
        if (!editing_) return;
        for (const char* c = ev.text; *c; ++c) {
            const unsigned char ch = static_cast<unsigned char>(*c);
            if (ch >= 32 && ch < 127 && st_.name.size() < static_cast<std::size_t>(ra2engine::kLobbyNameMax))
                st_.name += static_cast<char>(ch);
        }
        return;
    }
    // --- trackbar drag ---
    auto set_from_x = [&](int s) {
        const ra2engine::SbRect r = slider_rect(s);
        const int pos = ra2engine::trackbar_pos_from_x(lx - r.x, slider_range(s), r.w,
                                                       ra2engine::kTrackbarReadoutW);
        set_slider_pos(s, pos);
    };
    if (ev.kind == InputEvent::PointerMove && drag_ >= 0) { set_from_x(drag_); return; }
    if (ev.kind == InputEvent::PointerUp && ev.button == InputEvent::Left && drag_ >= 0) {
        set_from_x(drag_);
        drag_ = -1;
        return;
    }
    if (ev.kind != InputEvent::PointerDown || ev.button != InputEvent::Left) return;
    if (search_editing_) set_searching(false);

    // --- an open list owns the click ---
    if (combo_ != CB_NONE) {
        if (list_.scrolls() && list_.strip_up().contains(lx, ly)) { list_.scroll(-1); return; }
        if (list_.scrolls() && list_.strip_down().contains(lx, ly)) { list_.scroll(+1); return; }
        const int i = list_.hit(lx, ly);
        if (i >= 0) { list_.sel = i; close_combo(true); }
        else close_combo(false);               // click outside: close, no change
        return;
    }
    // --- sidebar buttons ---
    for (const ShellButton& b : buttons())
        if (b.enabled && b.rect.contains(lx, ly)) { set_editing(false); press(b.action, ev); return; }

    if (map_filter_rect().contains(lx, ly)) { open_combo(CB_MAP_FILTER, 0); return; }
    if (mode_ == MODE_SETUP && map_search_rect().contains(lx, ly)) {
        set_searching(true);
        return;
    }
    if (mode_ == MODE_SETUP && map_list_rect().contains(lx,ly)) {
        if(map_list_.strip_up().contains(lx,ly)){map_list_.scroll(-6);return;}
        if(map_list_.strip_down().contains(lx,ly)){map_list_.scroll(6);return;}
        const int picked=map_list_.hit(lx,ly);
        if(picked>=0) {
            map_list_.sel=picked;
            map_ix_=map_index_at_list(picked);
            ra2engine::lobby_apply_max_players(st_,map_max());
        }
        return;
    }
    if (mode_ == MODE_CUSTOMIZE) {
        if (map_list_.scrolls() && map_list_.strip_up().contains(lx, ly)) { map_list_.scroll(-map_list_.visible); return; }
        if (map_list_.scrolls() && map_list_.strip_down().contains(lx, ly)) { map_list_.scroll(+map_list_.visible); return; }
        const int i = map_list_.hit(lx, ly);
        if (i >= 0) { map_list_.sel = i; play_ui("menu:click"); }
        const int t = type_list_.hit(lx, ly);
        if (t >= 0) type_list_.sel = t;
        return;
    }
    // --- name edit (focus) ---
    set_editing(name_rect().contains(lx, ly));
    if (editing_) return;
    // --- row combos ---
    for (int k = 0; k < visible_rows(); ++k) {
        if (k > 0 && ai_rect(k).contains(lx, ly)) { open_combo(CB_AI, k); return; }
        if (!ra2engine::lobby_row_active(st_, k)) continue;   // EnableWindow(0) on None rows
        if (country_rect(k).contains(lx, ly)) { open_combo(CB_COUNTRY, k); return; }
        if (color_rect(k).contains(lx, ly)) { open_combo(CB_COLOR, k); return; }
        if (spawn_rect(k).contains(lx,ly)){open_combo(CB_SPAWN,k);return;}
        if (team_rect(k).contains(lx, ly)) { open_combo(CB_TEAM,k); return; }
    }
    // --- checkboxes (BS_AUTOCHECKBOX; Build Off Ally disabled) ---
    for (int c = 0; c < CK_COUNT; ++c)
        if (check_enabled(c) && check_rect(c).contains(lx, ly)) {
            *check_ptr(c) = !*check_ptr(c);
            play_ui("menu:click");
            return;
        }
    // --- trackbars: thumb starts a drag, elsewhere jumps (skin proc 0x5FB270) ---
    for (int s = 0; s < SL_COUNT; ++s) {
        const ra2engine::SbRect r = slider_rect(s);
        const ra2engine::SbRect track{r.x, r.y, r.w - ra2engine::kTrackbarReadoutW, r.h};
        if (!track.contains(lx, ly)) continue;
        const int tx = r.x + ra2engine::trackbar_thumb_x(slider_pos(s), slider_range(s), r.w,
                                                         ra2engine::kTrackbarReadoutW);
        if (lx >= tx && lx < tx + ra2engine::kTrackbarThumbW) drag_ = s;
        else set_from_x(s);
        return;
    }
}

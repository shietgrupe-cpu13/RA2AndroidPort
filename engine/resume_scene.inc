// resume_scene.inc -- W6-06-09 / W6-06-10 / W6-01-10: the startup page that
// offers to resume an interrupted game. Included by main.cpp after the
// campaign / result scenes.
//
// Two things can be waiting when the app starts:
//  * Saved Games/AUTOSAVE.SAV -- the background autosave of a game that the
//    OS killed (or whose activity Android restarted, W6-01-10). A game that
//    ends normally deletes it (game_scene_lifecycle.inc).
//  * RA2.INI [Engine] ResumeCampaign=<map>,<side>,<difficulty> -- the
//    campaign checkpoint written when the Campaign Results page opens on a
//    won mission (W6-06-10), so a kill on the Results page or on the next
//    mission's loading screen does not lose the win. It is cleared when a
//    campaign mission starts and when the player leaves the Results page for
//    the main menu.
//
// [Q4: a platform affordance (phones kill background apps); the original
// has no campaign progress store and no autosave.] The page is a shell page
// (the main menu's art, the gold header, the button column); MAIN MENU asks
// before it discards the waiting game.

std::string autosave_file_path() {
    const std::string d = save_games_dir();
    return d.empty() ? std::string() : d + "/AUTOSAVE.SAV";
}

// ---- the campaign checkpoint (RA2.INI [Engine] ResumeCampaign) -----------
// Written only in a real menu session (run_menu / --play publish
// g_user_settings); gates never touch RA2.INI.
static bool engine_ini_set(const std::string& key, const std::string& value) {
    if (!g_user_settings) return false;
    const std::string path = settings_file_path();
    if (path.empty()) return false;
    ra2engine::IniDoc doc = read_settings_doc(path);
    const std::optional<std::string> old = doc.get("Engine", key);
    if ((!old && value.empty()) || (old && *old == value)) return true;
    doc.set("Engine", key, value);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::string text = doc.serialize();
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}
void campaign_checkpoint_set(const std::string& map, const std::string& side, int difficulty) {
    if (engine_ini_set("ResumeCampaign", map + "," + side + "," + std::to_string(difficulty)))
        SDL_Log("[lifecycle] campaign checkpoint: %s (%s, difficulty %d)", map.c_str(), side.c_str(), difficulty);
}
void campaign_checkpoint_clear() {
    engine_ini_set("ResumeCampaign", "");
}
struct CampaignCheckpoint {
    std::string map, side;
    int difficulty = 1;
    bool valid() const { return !map.empty() && !side.empty(); }
};
CampaignCheckpoint campaign_checkpoint_get() {
    CampaignCheckpoint c;
    const std::string path = settings_file_path();
    if (path.empty()) return c;
    const std::optional<std::string> v = read_settings_doc(path).get("Engine", "ResumeCampaign");
    if (!v || v->empty()) return c;
    const std::size_t a = v->find(','), b = a == std::string::npos ? a : v->find(',', a + 1);
    if (a == std::string::npos) return c;
    c.map = v->substr(0, a);
    c.side = b == std::string::npos ? v->substr(a + 1) : v->substr(a + 1, b - a - 1);
    if (b != std::string::npos) c.difficulty = std::clamp(std::atoi(v->c_str() + b + 1), 0, 2);
    if (camp::up(c.side) != "GDI" && camp::up(c.side) != "NOD") c = CampaignCheckpoint{};
    return c;
}

// A mission's display name (mission.ini UIName via the CSF), else the map.
static std::string resume_mission_name(const std::string& map) {
    if (map.empty()) return std::string();
    try {
        const std::string dir = env_install_dir();
        ra2assets::MixFile ra2 = ra2assets::MixFile::open_path(dir + "ra2.mix");
        const ra2engine::MissionInfo mi = camp::load_tables(ra2).mission(map);
        if (!mi.ui_name.empty()) return camp::csf_text(dir, mi.ui_name);
    } catch (const std::exception&) {}
    return map;
}

class ResumeScene : public Scene {
public:
    enum Kind { Autosave, Campaign, Stale };
    // Autosave: `f` is the decoded AUTOSAVE.SAV.
    ResumeScene(bool no_audio, ra2engine::save::SaveFile f)
        : no_audio_(no_audio), kind_(Autosave), save_(std::make_shared<ra2engine::save::SaveFile>(std::move(f))) {
        const ra2engine::save::SaveHeader& h = save_->header;
        if (h.kind == ra2engine::save::ScenarioKind::Campaign) {
            what_ = resume_mission_name(h.map_id);
        } else {
            std::string m = h.map_file;
            const std::size_t s = m.find_last_of("/\\");
            if (s != std::string::npos) m = m.substr(s + 1);
            what_ = gf_csf("GUI:SkirmishGame", "Skirmish") + (m.empty() ? std::string() : ": " + m);
        }
        const long fr = static_cast<long>(h.frame);
        char t[64];
        std::snprintf(t, sizeof t, "%s %02ld:%02ld:%02ld", gf_csf("GUI:Time", "Time").c_str(), fr / 15 / 3600,
                      (fr / 15 / 60) % 60, (fr / 15) % 60);
        detail_ = t;
    }
    // Campaign: the next mission of a won campaign mission.
    ResumeScene(bool no_audio, CampaignCheckpoint c) : no_audio_(no_audio), kind_(Campaign), cp_(std::move(c)) {
        what_ = resume_mission_name(cp_.map);
        static const char* kDiff[3] = {"GUI:Easy", "GUI:Normal", "GUI:Hard"};
        static const char* kDiffFb[3] = {"Easy", "Normal", "Hard"};
        const int d = std::clamp(cp_.difficulty, 0, 2);
        detail_ = gf_csf(kDiff[d], kDiffFb[d]);
    }
    // Stale: an autosave this version cannot read (W6-06-01: a save from
    // another engine version). Said plainly, then discarded.
    explicit ResumeScene(bool no_audio) : no_audio_(no_audio), kind_(Stale) {}

    const char* name() const override { return "resume"; }
    void handle(const ra2engine::InputEvent& ev) override {
        if (confirm_.open()) {
            const int a = confirm_.handle(ev);
            if (a == 0) choice_ = 2;   // OK: discard
            return;
        }
        if (ev.kind == ra2engine::InputEvent::PointerMove || ev.kind == ra2engine::InputEvent::PointerDown ||
            ev.kind == ra2engine::InputEvent::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == ra2engine::InputEvent::Left && ev.kind == ra2engine::InputEvent::PointerUp) down_ = false;
        // Esc / Android Back = MAIN MENU (with its confirm); Enter = RESUME.
        if (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEscape) { ask_discard(); return; }
        if (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEnter && kind_ != Stale) { choice_ = 1; return; }
        if (ev.kind != ra2engine::InputEvent::PointerDown || ev.button != ra2engine::InputEvent::Left) return;
        down_ = true;
        if (kind_ != Stale && ui_dev(shell_row(0)).contains(ev.x, ev.y)) choice_ = 1;
        if (ui_dev(shell_row(5)).contains(ev.x, ev.y)) ask_discard();
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) override {
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
        const MenuArt& art = shell_art();
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art, xf};
        const std::vector<ShellButton> b{
            ShellButton{shell_row(0), kind_ == Campaign ? gf_csf("GUI:Continue", "Continue") : std::string("Resume"),
                        kind_ != Stale, ACT_NONE, PAGE_MAIN},
            ShellButton{shell_row(5), gf_csf("GUI:MainMenu", "Main Menu"), true, ACT_NONE, PAGE_MAIN}};
        int lx = 0, ly = 0;
        ui_to_logical(mx_, my_, lx, ly);
        int hov = -1;
        if (!confirm_.open())
            for (int i = 0; i < 2; ++i)
                if (b[(std::size_t)i].enabled && b[(std::size_t)i].rect.contains(lx, ly)) hov = i;
        const char* title = kind_ == Campaign ? "Continue Campaign?" : "Resume Game?";
        draw_shell(ren, tc, art, xf, b, hov, (hov >= 0 && down_) ? hov : -1, title, "", 0);
        p.fill(p.dev(24, 60, 423, 200), 0, 0, 0, 170);
        p.frame(p.dev(24, 60, 423, 200), true);
        int y = 72;
        for (const std::string& l : intro()) {
            p.label(p.fit(l, 407), 32, y, 407, 20, 0);
            y += 22;
        }
        y += 10;
        if (!what_.empty()) { p.header(p.fit(what_, 407), 32, y, 407, 24, 0); y += 28; }
        if (!detail_.empty()) p.label(detail_, 32, y, 407, 20, 0);
        std::string st;
        if (hov == 0) st = kind_ == Campaign ? "Start the next mission of the campaign" : "Go back to the interrupted game";
        if (hov == 1) st = "Discard it and go to the main menu";
        shell_status_line(p, st);
        confirm_.render(ren, tc);
    }
    Scene* next() override {
        const int c = choice_;
        choice_ = 0;
        if (c == 1 && kind_ == Autosave) {
            // Kept on disk until the resumed game ends normally (a kill while
            // it reloads must not lose it): the GameScene takes this flag.
            g_resumed_from_autosave = true;
            SDL_Log("[lifecycle] autosave resumed");
            return defer_load_scene(no_audio_, *save_);
        }
        if (c == 1 && kind_ == Campaign) {
            SDL_Log("[lifecycle] campaign checkpoint resumed: %s", cp_.map.c_str());
            return new CampaignLoadScene(no_audio_, campaign_config(cp_.map, cp_.side, cp_.difficulty));
        }
        if (c == 2) {
            discard();
            if (Scene* m = make_main_menu_scene(no_audio_)) return m;
            quit_ = true;
        }
        return nullptr;
    }
    bool wants_quit() const override { return quit_; }
    void play_probe(ra2play::Probe& p) override {
        p.scene = "resume";
        p.set_s("resume_kind", kind_ == Autosave ? "autosave" : kind_ == Campaign ? "campaign" : "stale");
        p.set_s("resume_what", what_);
        p.set_s("resume_map", kind_ == Campaign ? cp_.map : save_ ? save_->header.map_id : std::string());
        auto put = [&](const char* k, int row) {
            const ra2engine::SbRect r = ui_dev(shell_row(row));
            p.targets[k] = ra2play::Rect{r.x, r.y, r.w, r.h};
        };
        if (kind_ != Stale) put("resume", 0);
        put("menu", 5);
        confirm_.play_probe(p);
    }

private:
    std::vector<std::string> intro() const {
        if (kind_ == Campaign) return {"You won the last mission, but the game was closed", "before the next one started."};
        if (kind_ == Stale)
            return {"A game was interrupted, but it was saved by a", "different version of the engine and cannot be",
                    "resumed. Saves do not carry over between versions yet."};
        return {"The last game was interrupted (the app was closed", "in the background). It was saved automatically:"};
    }
    void ask_discard() {
        if (kind_ == Stale) { choice_ = 2; return; }
        confirm_.ask(kind_ == Campaign ? "Forget the campaign progress? You will have to start this mission again."
                                       : "Discard the interrupted game? It cannot be resumed later.",
                     {gf_csf("GUI:OK", "OK"), gf_csf("GUI:Cancel", "Cancel")});
    }
    void discard() {
        if (kind_ == Campaign) {
            campaign_checkpoint_clear();
            SDL_Log("[lifecycle] campaign checkpoint discarded");
            return;
        }
        std::error_code ec;
        std::filesystem::remove(autosave_file_path(), ec);
        SDL_Log("[lifecycle] autosave discarded");
    }
    bool no_audio_;
    Kind kind_;
    std::shared_ptr<ra2engine::save::SaveFile> save_;
    CampaignCheckpoint cp_;
    std::string what_, detail_;
    GfConfirm confirm_;
    int choice_ = 0;
    bool quit_ = false, down_ = false;
    int mx_ = -1, my_ = -1;
};

// The first scene of a menu session: the Resume page when an autosave or a
// campaign checkpoint is waiting, else nullptr (the caller shows the menu).
Scene* make_resume_scene(bool no_audio) {
    const std::string path = autosave_file_path();
    std::error_code ec;
    if (!path.empty() && std::filesystem::is_regular_file(path, ec)) {
        std::vector<uint8_t> bytes;
        ra2engine::save::SaveFile f;
        const bool ok = ra2engine::save::read_file_bytes(path, bytes) && ra2engine::save::decode_save(bytes, f) &&
                        ra2engine::save::supported_version(f.header.version) &&
                        f.section(ra2engine::save::kTagWorld) && f.section(ra2engine::save::kTagConfig);
        SDL_Log("[lifecycle] autosave found (%s)", ok ? "valid" : "unreadable or another version");
        if (ok) return new ResumeScene(no_audio, std::move(f));
        return new ResumeScene(no_audio);   // Stale: explained, then removed
    }
    const CampaignCheckpoint cp = campaign_checkpoint_get();
    if (cp.valid()) {
        SDL_Log("[lifecycle] campaign checkpoint found: %s", cp.map.c_str());
        return new ResumeScene(no_audio, cp);
    }
    return nullptr;
}

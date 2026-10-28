// W2-AR-09: Android lifecycle inside GameScene (textually included in the
// class body). Desktop never receives these events.
//
// Backgrounded (Home, a call, the app switcher): the game is PAUSED by opening
// the in-game options menu (the update() pause the original's modal menu
// loop gives), and the session is written to Saved Games/AUTOSAVE.SAV --
// the low-memory killer may end a 0.7-1.4 GB process while it is in the
// background. On return the menu is up, so play resumes only when the player
// chooses. The autosave is removed when the game ends normally (this
// scene's destructor), so a leftover one means the process was killed, and
// run_menu offers to resume it at the next launch. [Q4: a platform norm,
// not a traced behaviour of the original.]
static std::string autosave_path() {
    const std::string d = save_games_dir();
    return d.empty() ? std::string() : d + "/AUTOSAVE.SAV";
}
bool lifecycle_event(const ra2engine::InputEvent& ev) {
    switch (ev.kind) {
        case ra2engine::InputEvent::AppBackground: {
            if (!menu_.open_ && !sld_.is_open() && !gf_abort_.open()) menu_.open();   // pause
            const std::string p = autosave_path();
            if (!p.empty() && !game_over_for_autosave()) {
                const Uint32 t0 = SDL_GetTicks();
                const bool ok = save_game(p, "AUTOSAVE");
                autosaved_ = autosaved_ || ok;
                SDL_Log("[lifecycle] background: paused, autosave %s (%u ms) -> %s", ok ? "ok" : "FAILED",
                        SDL_GetTicks() - t0, p.c_str());
            }
            return true;
        }
        case ra2engine::InputEvent::AppForeground:
            SDL_Log("[lifecycle] foreground: game stays paused in the options menu");
            return true;
        case ra2engine::InputEvent::AppLowMemory:
        case ra2engine::InputEvent::RenderReset:
            return true;   // the driver already trimmed / cleared the texture cache
        case ra2engine::InputEvent::Resize:
            relayout_canvas();
            return true;
        default:
            return false;
    }
}
// No autosave once the outcome is decided (the result screen follows).
bool game_over_for_autosave() const {
    return gf_outcome_ != 0 || (campaign_ && campaign_->outcome() != 0);
}
void lifecycle_teardown() {
    if (!autosaved_) return;
    // W6-01-10: Android destroyed the activity without the player quitting
    // (an activity restart / config change): keep the autosave so the next
    // launch offers RESUME.
    if (ra2engine::os_keep_autosave()) {
        SDL_Log("[lifecycle] teardown: activity restart, autosave kept");
        return;
    }
    const std::string p = autosave_path();
    if (!p.empty()) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }
}
// W6-06-09: true too when this game was resumed FROM the autosave (the file
// is kept until the resumed game ends normally, so a kill during the reload
// does not lose it).
bool autosaved_ = g_resumed_from_autosave_take();

// R-162: the panel changed size mid-game (a foldable opened, split screen, a
// resized desktop window). Recompute the canvas exactly as a new game would
// (game_logical_size), re-lay the sidebar and the tactical view, and keep the
// world point at the centre of the view where it was. [Engine affordance:
// the original runs at a fixed ScreenWidth x ScreenHeight.]
void relayout_canvas(int gw=0,int gh=0) {
    const bool was_zoomed=world_zoom_!=1.0;
    world_zoom_=1.0;input_.set_screen_zoom(1.0);
    if(gw<=0 || gh<=0)game_logical_size(gw, gh);
    if (gw == kWinW && gh == kWinH) {if(was_zoomed)input_.set_viewport(playfield_w_,kWinH);return;}
    const int cx = -cam_.x + playfield_w_ / 2, cy = -cam_.y + kWinH / 2;   // world centre
    SDL_Log("[lifecycle] resize: canvas %dx%d -> %dx%d", kWinW, kWinH, gw, gh);
    set_canvas_size(gw, gh);
    lay_ = ra2engine::SidebarLayout::compute(kWinW, kWinH, buildable_);
    playfield_w_ = lay_.battlefield_width(kWinW,sidebar_up_);
    input_.set_viewport(playfield_w_, kWinH);
    if (campaign_ && local_.valid) {   // the LocalSize clamp (campaign_view_setup)
        const int W = local_.size_w;
        const int top = 15 * (W - 5 + 2 * local_.y);
        input_.set_view_limits(30 * (2 * local_.x - W) + 30, top, 30 * (2 * (local_.x + local_.w) - W) + 30,
                               top + 30 * local_.h + 135);
    }
    cam_.x = playfield_w_ / 2 - cx;
    cam_.y = kWinH / 2 - cy;
    input_.pan(0, 0);   // clamp
}

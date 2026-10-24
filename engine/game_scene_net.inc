// NET-PLAY: GameScene in lockstep mode -- #included INSIDE the GameScene class
// body (public section). net_ == nullptr is single player: nothing here runs.
//
// Tick gate. update()'s fixed-tick loop asks net_gate() before every tick:
// NetGame::step() (begin_frame(state CRC) / poll / end_frame) says Run with
// the commands of THIS frame (every house, the original's Execute_DoList
// order), which go into the command queue stamped for this tick and apply at
// its start exactly like single player's; Stall leaves the tick for later
// (the "Waiting for players" overlay after 1 s, the reconnect dialog after
// the original's dialog timeout); OutOfSync / ScenarioMismatch / Timeout end
// the match with the original's message (TXT_OUT_OF_SYNC, ...), a desync
// dump goes to the saves folder.
// Local commands never enter the queue directly: issue() sends them through
// the session (frame = now + MaxAhead) and they come back like everyone's.
// W9-01-01 / W9-01-05: the tick rate is the session's DesiredFrameRate (the
// master's TIMING, capped by the lobby / GAMESPEED speed and lowered to the
// slowest machine's PROCESS_TIME), at most ONE tick per rendered frame and no
// catch-up -- the original's frame timer simply waits, time lost in
// Wait_For_Players is lost (net_tick_multiplier, main.cpp's update loop).
// [RE: 01-original-protocol.md §4.6 (EXIT / REMOVEPLAYER), §5 (sync),
//  05-transport-infra.md §6.3 (in game), §7.2 (Android away).]

    std::shared_ptr<NetPlay> net_;
    bool net_stalled_ = false;
    std::uint64_t net_stall_since_ = 0;        // wall ms of the current stall (0 none)
    struct NetAway { int house; std::uint64_t until_ms; };
    std::vector<NetAway> net_away_;            // players who announced AWAY
    std::vector<int> net_left_;                // houses already handled (left / dropped)
    bool net_local_left_ = false;              // our own EXIT executed
    bool net_exit_requested_ = false;
    Scene* net_held_next_ = nullptr;           // where to go once our EXIT executed
    bool net_end_dialog_ = false;              // the match ended abnormally (OOS / mismatch / ...)
    std::string net_end_title_, net_end_text_;
    SDL_Rect net_btn_ok_{0, 0, 0, 0}, net_btn_drop_{0, 0, 0, 0}, net_btn_quit_{0, 0, 0, 0},
        net_btn_chat_{0, 0, 0, 0}, net_btn_send_{0, 0, 0, 0};
    bool net_chat_open_ = false;
    std::string net_chat_text_;
    bool net_bg_away_ = false;                 // we announced AWAY (app in background)
    // W9-04-04/-08: the background we return from.
    std::uint64_t net_bg_at_ = 0;              // wall ms of AppBackground
    bool net_bg_touch_ = false;                // the last pointer before it was a finger
    ra2engine::Camera net_bg_cam_;             // the camera before it
    std::uint64_t net_resume_hold_until_ = 0;  // wall ms: our stall timers held until then
    std::uint64_t net_ignore_mouse_until_ = 0; // wall ms: a phantom mouse is ignored until then
    SDL_Rect net_chat_box_{0, 0, 0, 0};        // W9-04-02: the open chat line
    std::uint32_t net_last_trail_ = 0xFFFFFFFFu;
    double net_hash_ms_sum_ = 0.0, net_hash_ms_max_ = 0.0;
    std::uint32_t net_hash_n_ = 0;
    int net_stalls_ = 0;
    // Presentation events drained at tick end in lockstep mode (the World's
    // event queues are part of the save bytes the sync hash covers, so they
    // must be empty at every tick boundary on every peer).
    std::vector<ra2engine::World::FireEvent> net_fire_buf_;
    std::vector<ra2engine::World::AbilityEvent> net_ability_buf_;
    std::vector<ra2engine::World::GarrisonEvent> net_garrison_buf_;
    std::vector<ra2engine::ProjectileEvent> net_proj_buf_;

    bool net_on() const { return net_ != nullptr; }

    // --- W9-01-01 / W9-01-03 / W9-01-07: match pacing ---
    // [RE: main loop FUN_0055CFF0 @0x55D10E..0x55D1C0, network branch: the
    // frame timer is 1000 / DesiredFrameRate ms (DesiredFrameRate 0xA8A4C0,
    // 0 -> 33 ms); @0x55D1C8..0x55D2F0 on the Internet (GameMode 4) with
    // MaxAhead 0xA8A4B8 > 30 the frame grows by +10 ms for each of
    // MaxAhead/4, /2 and 3/4 that the most-behind peer lags.] The clone's rule
    // time base is 15 frames per engine second (gamespeed.md §2), so the
    // accumulator multiplier is the wall frame rate / 15.
    double net_tick_fps() const {
        const ra2::net::LockstepSession& s = net_->game->session();
        const std::uint32_t fps = s.desired_fps();
        int frame_ms = fps == 0 ? 33 : static_cast<int>(1000 / fps);
        if (net_->game->config().mode == ra2::net::GameMode::Internet && s.max_ahead() > 30) {
            const std::uint32_t lag = s.peer_lag_frames(), ma = s.max_ahead();
            if (lag != 0) {
                if (lag >= ma / 4) frame_ms += 10;
                if (lag >= ma / 2) frame_ms += 10;
                if (lag >= ma * 3 / 4) frame_ms += 10;
            }
        }
        return frame_ms > 0 ? 1000.0 / frame_ms : 60.0;
    }
    double net_tick_multiplier() const { return net_tick_fps() / 15.0; }
    // W9-01-03: a tick's processing time (gate + commands + logic + CRC) plus
    // the CPU time of the last rendered frame -- the original's ProcessTicks
    // span covers its render and logic but neither the frame-limiter wait nor
    // the Wait_For_Players stall. Reported to the session per executed frame.
    double net_render_ms_ = 0.0;
    void net_note_tick(std::chrono::steady_clock::time_point t0) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        net_->game->session().note_process_time(ms + net_render_ms_);
    }
    struct NetRenderClock {   // times GameScene::render for net_note_tick
        double& out;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        explicit NetRenderClock(double& o) : out(o) {}
        ~NetRenderClock() {
            out = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
    };
    // W9-01-07 [RE: the in-game Options dialog FUN_004E1EF0 shows the speed
    // slider in a network game too (6 - GameSpeed 0xA8DAC8, the session's
    // speed); its OK handler FUN_004E1CF0 does NOT set the speed in a network
    // game (GameMode != 0 and != 5) but queues a GAMESPEED event (0x0D) with
    // the new value; EventClass::Execute FUN_004C6BD0 @0x4C786E stores it in
    // 0xA8DAC8 on every peer at the same frame, and the master's next TIMING
    // caps DesiredFrameRate with it]. Any player may change it. The clone's
    // Options overlay edits the shared Settings::game_speed: in a match the
    // slider shows the match speed, a change becomes a GAMESPEED command, and
    // the user's own single-player speed is put back when the match ends.
    int net_match_speed_ = 1;
    int net_user_speed_ = -1;   // the Options value before the match (restored at teardown)
    void net_speed_attach() {
        net_match_speed_ = std::max(0, std::min(6, net_->session.lobby.opt.game_speed));
        net_->game->session().set_game_speed(net_match_speed_);
        if (g_user_settings) {
            net_user_speed_ = g_user_settings->game_speed;
            g_user_settings->game_speed = net_match_speed_;
        }
    }
    void net_speed_poll() {
        if (!g_user_settings || g_user_settings->game_speed == net_match_speed_ || net_local_left_) return;
        ra2engine::Command c;
        c.type = ra2engine::EventType::GameSpeed;
        c.value = std::max(0, std::min(6, g_user_settings->game_speed));
        g_user_settings->game_speed = net_match_speed_;   // shown again until the event executes
        issue(c);
    }
    void net_speed_restore() {
        if (g_user_settings && net_user_speed_ >= 0) g_user_settings->game_speed = net_user_speed_;
        net_user_speed_ = -1;
    }
    static constexpr std::uint32_t kNetAwaySeconds = 90;   // 05 §7.2

    void net_attach() {
        if (!g_pending_netplay) return;
        net_ = std::move(g_pending_netplay);
        g_pending_netplay.reset();
        world_.set_order_issuer(local_house_);
        net_speed_attach();   // W9-01-07
        std::cerr << "[net] lockstep match: local house " << local_house_ << ", " << net_->session.players.size()
                  << " houses, hash every " << net_->hash_every << " frames, max_ahead "
                  << net_->game->session().max_ahead() << " fsr " << net_->game->session().frame_send_rate() << "\n";
        post_message("Network game: waiting for every player to finish loading...", 8.0);
    }
    template <class T> static std::vector<T> net_take(std::vector<T>& v) {
        std::vector<T> out;
        out.swap(v);
        return out;
    }
    // Tick end (lockstep only): the frame-time presentation events move out of
    // the World so the next tick's state hash never depends on frame timing.
    void net_drain_tick_events() {
        auto fe = world_.take_fire_events();
        net_fire_buf_.insert(net_fire_buf_.end(), fe.begin(), fe.end());
        auto ae = world_.take_ability_events();
        net_ability_buf_.insert(net_ability_buf_.end(), ae.begin(), ae.end());
        auto ge = world_.take_garrison_events();
        net_garrison_buf_.insert(net_garrison_buf_.end(), ge.begin(), ge.end());
        auto pe = world_.take_projectile_events();
        net_proj_buf_.insert(net_proj_buf_.end(), pe.begin(), pe.end());
    }

    // The sync CRC fed to FRAMEINFO. W9-01-04 [RE: ComputeFrameCRC
    // FUN_0064D520]: by default the cheap per-frame CRC over the sim state
    // (net_frame_crc: positions, facings, health, economy, every RNG stream)
    // plus the scene's sim-side FreeUnit tracker -- microseconds, no
    // allocation; it follows the W9-05 sync-hash policy (canonical NaN, no
    // derived caches, the econ-rules digest and the campaign trigger runtime
    // in). Debug mode ([Net] FullHash=1): the complete World streamed through
    // save::world_sync_hash (SyncHasher, the same policy) + the trigger
    // runtime + the tracker. UI state (camera,
    // sidebar, pending placement) is in neither. Timed for the perf log.
    std::uint32_t net_state_hash() {
        const auto t0 = std::chrono::steady_clock::now();
        const ra2engine::MapTriggerSystem* trig = campaign_ ? &campaign_->triggers() : nullptr;
        std::uint32_t out = 0;
        if (net_ && net_->full_hash) {
            // W9-05: streamed under the sync-hash policy (derived caches out,
            // the production-rules digest in), plus the campaign trigger
            // runtime -- no 8 MB save buffer.
            std::uint64_t h = ra2engine::save::world_sync_hash(world_, trig);
            for (int v : bstates_) {
                h ^= static_cast<std::uint32_t>(v);
                h *= 1099511628211ull;
            }
            out = static_cast<std::uint32_t>(h ^ (h >> 32));
        } else {
            out = ra2engine::net_frame_crc(world_, &bstates_, trig);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        net_hash_ms_sum_ += ms;
        net_hash_ms_max_ = std::max(net_hash_ms_max_, ms);
        ++net_hash_n_;
        return out;
    }

    // Before every tick: may this tick run? (Run: this frame's commands are queued.)
    bool net_gate() {
        if (!net_ || net_end_dialog_ || net_local_left_) return false;
        std::vector<ra2engine::Command> cmds;
        const ra2::net::NetGame::Step st = net_->game->step([this]() { return net_state_hash(); }, cmds);
        if (st == ra2::net::NetGame::Step::Stall) {
            if (!net_stalled_) {
                net_stalled_ = true;
                net_stall_since_ = ra2mp::now_ms();
                ++net_stalls_;
            }
            return false;
        }
        if (st != ra2::net::NetGame::Step::Run) {
            net_fail(st);
            return false;
        }
        net_stalled_ = false;
        net_stall_since_ = 0;
        for (ra2engine::Command& c : cmds)   // W9-05-12: forged / corrupt events are dropped
            if (ra2engine::valid_net_command(world_, c)) cmdq_.push(std::move(c));
        // Test hook (scripts/play/net-lan-desync.ps1): RA2_NET_DESYNC_AT=<frame>
        // makes THIS machine diverge deliberately at that frame (one credit).
        static const long desync_at = [] {
            const char* e = std::getenv("RA2_NET_DESYNC_AT");
            return e && *e ? std::atol(e) : -1L;
        }();
        if (desync_at >= 0 && sim_frame_ == static_cast<std::uint32_t>(desync_at)) {
            world_.deposit_credits(local_house_, 1.0);
            std::cerr << "[net] TEST: injected a divergence at frame " << sim_frame_ << "\n";
            pres_log(0, "net test divergence");
        }
        // W9-05-12 test hook (net-match.ps1 -Fuzz): RA2_NET_FUZZ=1 makes THIS
        // machine send forged / malformed game packets the others must drop.
        static const bool fuzz = [] {
            const char* e = std::getenv("RA2_NET_FUZZ");
            return e && *e == '1';
        }();
        if (fuzz && sim_frame_ >= 300 && sim_frame_ < 2400 && sim_frame_ % 37 == 0)
            net_->game->session().send_test_garbage(static_cast<int>(sim_frame_ / 37));
        // The hash trail (stderr / play log): one line per hash period, the
        // two-process tests compare these between the machines.
        const std::uint32_t f = sim_frame_;
        // W9-01-04: the trail stays one line per 15 frames whatever the CRC
        // period (the CRC now runs every frame).
        const std::uint32_t every = static_cast<std::uint32_t>(std::max(15, net_->hash_every));
        if (f % every == 0 && f != net_last_trail_) {
            net_last_trail_ = f;
            const std::uint32_t crc = net_->game->crc_of(f);
            char b[96];
            std::snprintf(b, sizeof b, "[net] trail f=%u crc=%08x", f, crc);
            std::cerr << b << "\n";   // ~1 line / s: net-match.ps1 compares the two machines' trails
        }
        return true;
    }

    // Exit / RemovePlayer reach apply_due_commands like any command (every peer,
    // same frame): the leaver's house is defeated (or handed to the AI).
    void net_on_command(const ra2engine::Command& c) {
        if (!net_) return;
        if (c.type == ra2engine::EventType::GameSpeed) {   // W9-01-07 (FUN_004C6BD0 @0x4C786E)
            net_match_speed_ = std::max(0, std::min(6, static_cast<int>(c.value)));
            net_->game->session().set_game_speed(net_match_speed_);
            if (g_user_settings) g_user_settings->game_speed = net_match_speed_;
            std::cerr << "[net] game speed " << net_match_speed_ << " (house " << static_cast<int>(c.house)
                      << ") at frame " << sim_frame_ << "\n";
            return;
        }
        if (c.type == ra2engine::EventType::Exit) net_house_left(c.house, /*left=*/true);
        else if (c.type == ra2engine::EventType::RemovePlayer) net_house_left(c.value, /*left=*/false);
    }
    void net_house_left(int h, bool left) {
        if (h < 0 || h >= world_.house_count()) return;
        if (std::find(net_left_.begin(), net_left_.end(), h) != net_left_.end()) return;
        net_left_.push_back(h);
        const std::string who = net_->name_of(h);
        if (h == local_house_) {
            net_local_left_ = true;
            std::cerr << "[net] our " << (left ? "EXIT" : "REMOVEPLAYER") << " executed at frame " << sim_frame_ << "\n";
            if (!left && !net_exit_requested_ && !net_end_dialog_) {
                // W9-05-12: the others dropped THIS machine (too slow or
                // unreachable for them): end with a message, never freeze.
                net_end_dialog_ = true;
                net_end_title_ = "Connection lost";
                net_end_text_ = "The other players removed you from the game.";
            }
        } else {
            // W9-05-16: the CSF line TXT_LEFT_GAME "%s has left the game."
            std::string t = left ? camp::csf_text(dir_, "TXT_LEFT_GAME") : std::string();
            const std::size_t ps = t.find("%s");
            if (ps != std::string::npos) t.replace(ps, 2, who);
            else t = who + (left ? " has left the game." : " lost connection and was removed.");
            post_message(t, 10.0, net_color(h));
            std::cerr << "[net] house " << h << (left ? " left" : " removed") << " at frame " << sim_frame_ << "\n";
        }
        if (net_->ai_takeover && h != local_house_ && world_.house_outcome(h) == 0 && !world_.house_defeated(h)) {
            // 05 §7: the host chose "AI takes over" -- the house keeps its
            // wallet, queues and units under a Normal AI.
            ra2engine::SkirmishConfig c2 = cfg_;
            for (std::size_t i = 0; i < c2.slots.size(); ++i) c2.slots[i].is_ai = false;
            if (h < static_cast<int>(c2.slots.size())) {
                c2.slots[static_cast<std::size_t>(h)].is_ai = true;
                c2.slots[static_cast<std::size_t>(h)].diff = ra2engine::AiDifficulty::Normal;
                world_.set_human(h, false);
                arm_skirmish_ai(world_, c2, ba_, rules_, rules_ini_, ur_ai_, load_aimd_or_empty(dir_), &a_.map, ua_);
                post_message("The computer takes over " + who + ".", 10.0, net_color(h));
            }
            return;
        }
        world_.resign_house(h);
    }
    SDL_Color net_color(int house) const { return house_text_color(house); }

    void net_fail(ra2::net::NetGame::Step st) {
        if (net_end_dialog_) return;
        net_end_dialog_ = true;
        auto csf = [this](const char* key, const char* fallback) {
            const std::string t = camp::csf_text(dir_, key);
            return t.empty() || t == key ? std::string(fallback) : t;
        };
        const ra2::net::OutOfSyncInfo& o = net_->game->session().out_of_sync();
        switch (st) {
            case ra2::net::NetGame::Step::OutOfSync: {
                net_end_title_ = csf("TXT_OUT_OF_SYNC", "Out of sync!");
                const std::string dump = net_write_desync_dump();
                net_end_text_ = "The game is out of sync with " + net_->name_of(o.house) + " (frame " +
                                std::to_string(o.frame) + "). It cannot continue." +
                                (dump.empty() ? std::string() : "\nDesync report: " + dump);
                break;
            }
            case ra2::net::NetGame::Step::ScenarioMismatch:
                net_end_title_ = csf("TXT_SCENARIOS_DO_NOT_MATCH", "Scenarios do not match!");
                net_end_text_ = "The players' game data or map differ, the match cannot start.";
                net_write_desync_dump();
                break;
            case ra2::net::NetGame::Step::Timeout:
                net_end_title_ = "Connection failed";
                net_end_text_ = "Not every player made it into the game.";
                break;
            default:
                net_end_title_ = "Network error";
                net_end_text_ = "The match had to end.";
                break;
        }
        std::cerr << "[net] match ended: " << ra2::net::NetGame::step_name(st) << " at frame " << sim_frame_
                  << " (oos house " << o.house << " frame " << o.frame << " local " << o.local_crc << " remote "
                  << o.remote_crc << ")\n";
        pres_log(0, std::string("net end ") + ra2::net::NetGame::step_name(st));
    }

    // SYNC dump (the original writes SYNC<house>.TXT, FUN_0064D8E0): this
    // machine's full save at the detection frame + a text report with the
    // frames, both CRCs and the last 256 fed CRCs. The other machine writes
    // its own pair, so the two saves can be diffed (simdiff / save viewer).
    std::string net_write_desync_dump() {
        const std::string dir = save_games_dir();
        if (dir.empty()) return "";
        const ra2::net::OutOfSyncInfo& o = net_->game->session().out_of_sync();
        const std::string stem = dir + "/DESYNC_F" + std::to_string(o.frame) + "_H" + std::to_string(local_house_);
        const bool saved = save_game(stem + ".SAV", "Desync frame " + std::to_string(o.frame));
        std::ofstream t(stem + ".TXT", std::ios::binary | std::ios::trunc);
        if (t) {
            t << "RA2 clone network desync report\n";
            t << "engine " << RA2_VERSION << "\n";
            t << "local house " << local_house_ << " (" << net_->name_of(local_house_) << ")\n";
            t << "out of sync with house " << o.house << " (" << net_->name_of(o.house) << ")\n";
            t << "frame " << o.frame << "  local crc " << o.local_crc << "  remote crc " << o.remote_crc << "\n";
            t << "detected at frame " << sim_frame_ << "  hash every " << net_->hash_every << " frames\n";
            t << "save " << (saved ? stem + ".SAV" : std::string("(write failed)")) << "\n";
            t << "session seed " << net_->session.seed << "  map " << net_->session.lobby.map.file << "\n";
            // Per-member hashes of the World at this frame: diff the two
            // machines' lists to name the member that differs.
            t << "world members (name fnv32):\n";
            for (const auto& fh : ra2engine::save::world_field_hashes(world_))
                t << "  " << fh.first << " " << fh.second << "\n";
            t << "fed CRCs (frame crc), oldest first:\n";
            const std::uint32_t f1 = net_->game->frame();
            for (std::uint32_t k = 0; k < 256 && k <= f1; ++k) {
                const std::uint32_t f = f1 - std::min<std::uint32_t>(f1, 255) + k;
                if (f > f1) break;
                t << f << " " << net_->game->crc_of(f) << "\n";
            }
        }
        std::cerr << "[net] desync dump " << stem << ".SAV / .TXT\n";
        return stem + ".SAV";
    }

    // Per frame (lockstep): the global channel (chat, away), removed players,
    // away countdowns, and leaving once our EXIT executed.
    void net_frame() {
        if (!net_) return;
        net_speed_poll();   // W9-01-07
        if (net_->mpnet) net_->mpnet->tick(ra2mp::now_ms());   // tunnel keep-alive (no-op on LAN)
        std::vector<ra2::net::NetChat> chat;
        std::vector<ra2::net::AwayNotice> away;
        std::vector<ra2::net::BeaconMsg> beacons;
        net_->game->poll_global(chat, away, beacons);
        for (const ra2::net::BeaconMsg& b : beacons) mp_beacon_received(b);   // W9-05-08
        for (const ra2::net::NetChat& c : chat) {
            const std::string who = c.house >= 0 ? net_->name_of(c.house) : c.sender;
            // W9-05-07 [RE: FUN_0048D0F0 case 0x0C -> MessageListClass::Add,
            // MessageDelay x 900 ticks]: [General] MessageDelay minutes.
            post_message(who + ": " + c.text, message_seconds(), c.house >= 0 ? net_color(c.house) : hud_ui_color());
            pres_log(0, "net chat " + who + ": " + c.text);
            std::cerr << "[net] chat " << who << ": " << c.text << "\n";
        }
        const std::uint64_t now = ra2mp::now_ms();
        for (const ra2::net::AwayNotice& a : away) {
            net_away_.erase(std::remove_if(net_away_.begin(), net_away_.end(),
                                           [&](const NetAway& w) { return w.house == a.house; }),
                            net_away_.end());
            if (!a.back) net_away_.push_back(NetAway{a.house, now + 1000ull * std::min<uint32_t>(a.seconds, 300)});
            post_message(net_->name_of(a.house) + (a.back ? " is back." : " paused the game (app in the background)."),
                         8.0, net_color(a.house));
            pres_log(0, std::string("net away ") + (a.back ? "back " : "away ") + std::to_string(a.house));
            std::cerr << "[net] " << (a.back ? "back" : "away") << " house " << a.house << " (" << a.seconds << " s)" << std::endl;
        }
        // An away player's countdown holds the stall timers (no dialog, no kick).
        net_away_.erase(std::remove_if(net_away_.begin(), net_away_.end(),
                                       [&](const NetAway& w) { return now >= w.until_ms || !net_->game->is_remote(w.house); }),
                        net_away_.end());
        if (!net_away_.empty()) net_->game->session().hold_wait();
        if (now < net_resume_hold_until_) net_->game->session().hold_wait();   // W9-04-08: back from the background
        for (const auto& r : net_->game->take_removed())
            std::cerr << "[net] connection to house " << r.first << (r.second ? " lost" : " closed") << "\n";
        // Our EXIT executed (or nobody is left to tell): the scene moves on.
        if (net_exit_requested_ && !net_end_dialog_ &&
            (net_local_left_ || net_->game->session().players() <= 1) && !next_scene_) {
            next_scene_ = net_held_next_ ? net_held_next_ : make_main_menu_scene(no_audio_);
            net_held_next_ = nullptr;
        }
    }

    // The scene wants to leave (result screen, Abort): in a match, first EXIT
    // like the original (scheduled; the game keeps running until it executes
    // on every machine at the same frame). Returns the scene to switch to now.
    Scene* net_filter_next(Scene* n) {
        if (!net_ || !n) return n;
        if (net_end_dialog_) return n;   // the session is dead: just go
        if (net_local_left_ || net_->game->session().players() <= 1) return n;
        if (!net_exit_requested_) {
            net_exit_requested_ = true;
            net_->game->request_exit();
            std::cerr << "[net] leaving: EXIT queued at frame " << sim_frame_ << "\n";
        }
        if (net_held_next_ && net_held_next_ != n) delete net_held_next_;
        net_held_next_ = n;
        return nullptr;
    }
    // Scene teardown: a live session keeps flushing its EXIT in the driver.
    void net_teardown() {
        if (!net_) return;
        if (net_held_next_) {
            delete net_held_next_;
            net_held_next_ = nullptr;
        }
        if (!net_end_dialog_ && !net_->game->exiting()) net_->game->request_exit();
        net_speed_restore();   // W9-01-07
        std::cerr << "[net] pacing: desired fps " << net_->game->session().desired_fps() << ", tick fps "
                  << net_tick_fps() << ", max_ahead " << net_->game->session().max_ahead() << " fsr "
                  << net_->game->session().frame_send_rate() << "\n";
        {
            const ra2::net::LockstepStats& st = net_->game->session().stats();
            std::cerr << "[net] rejected: " << st.rejected_packets << " packets, " << st.rejected_events
                      << " events, " << net_->game->rejected_removes() << " removeplayer" << std::endl;   // W9-05-12
        }
        if (net_hash_n_)
            std::cerr << "[net] sync hash: " << net_hash_n_ << " computed, avg "
                      << (net_hash_ms_sum_ / net_hash_n_) << " ms, max " << net_hash_ms_max_ << " ms; stalls "
                      << net_stalls_ << "\n";
        g_net_lingering.push_back(std::move(net_));
    }

    // --- Android lifecycle (05 §7.2): AWAY on background, BACK on return ---
    bool net_lifecycle(const ra2engine::InputEvent& ev) {
        if (!net_) return false;
        if (ev.kind == ra2engine::InputEvent::AppBackground) {
            if (!net_bg_away_) {
                net_bg_away_ = true;
                net_bg_at_ = ra2mp::now_ms();
                net_bg_touch_ = !mouse_present_;
                net_bg_cam_ = cam_;
                net_->game->send_away(false, kNetAwaySeconds);
                SDL_Log("[lifecycle] background in a network game: AWAY sent (%u s)", kNetAwaySeconds);
                pres_log(0, "net sent away");
            }
            return true;   // no pause menu, no autosave: the match is shared
        }
        if (ev.kind == ra2engine::InputEvent::AppForeground) {
            if (net_bg_away_) {
                net_bg_away_ = false;
                const std::uint64_t now = ra2mp::now_ms();
                const std::uint64_t away_ms = now - net_bg_at_;
                net_->game->send_away(true, 0);
                SDL_Log("[lifecycle] foreground in a network game: BACK sent (away %llu ms)",
                        static_cast<unsigned long long>(away_ms));
                pres_log(0, "net sent back");
                // W9-04-08: our own stall timers ran on while the app was
                // frozen; the first poll after the return compared a stale
                // wait start against the timeout and kicked the very peers
                // that were holding for our AWAY. Restart them now and hold
                // them for the frame-wait dialog time (LAN 7 s / Internet
                // 12 s), so the packets that arrived meanwhile are read
                // before any timeout is judged.
                const ra2::net::WaitTimeouts to = ra2::net::wait_timeouts(net_->game->config().mode);
                net_->game->session().hold_wait();
                net_resume_hold_until_ = now + static_cast<std::uint64_t>(to.dialog_frame) * 16;
                // Away longer than the others hold for us (our AWAY countdown,
                // then their stall timeout): they have removed us already.
                const std::uint64_t others_wait_ms =
                    1000ull * kNetAwaySeconds + static_cast<std::uint64_t>(to.total) * 16 + 10000;
                if (away_ms > others_wait_ms && !net_end_dialog_ && net_->game->session().players() > 1) {
                    net_end_dialog_ = true;
                    net_end_title_ = "Removed from the game";
                    net_end_text_ = "You were away for " + std::to_string(away_ms / 1000) +
                                    " s; the other players waited " + std::to_string(others_wait_ms / 1000) +
                                    " s and continued without you.";
                    std::cerr << "[net] away " << away_ms << " ms > " << others_wait_ms << " ms: removed\n";
                    pres_log(0, "net end away-too-long");
                }
                // W9-04-04: on a phone the return can bring a synthetic mouse
                // event at the window corner, which made the camera
                // edge-scroll into the black. The player was using a finger:
                // stay in touch mode, ignore mouse events for a moment and
                // put the camera back where it was.
                if (net_bg_touch_) {
                    mouse_present_ = false;
                    net_ignore_mouse_until_ = now + 1500;
                }
                cam_ = net_bg_cam_;
            }
            return true;
        }
        return false;
    }

    // --- input: chat box, stall / end dialogs -----------------------------
    // W9-04-02/-12: the chat line's rect (the IME pans the view to keep it
    // above the keyboard) and (re)opening the keyboard on it, seeded with
    // the line's text (W9-04-01).
    SDL_Rect net_chat_box_rect() const {
        const double sc = hud_touch_scale();
        const int h = static_cast<int>(28 * sc);
        int y = kWinH - h - static_cast<int>(70 * sc);
        const int sw = static_cast<int>((hud_text_width("Send") + 24) * sc);
        if (touch_bar_visible() && !touch_panel_collapsed_) {
            const SDL_Rect panel = touch_panel_rect();
            if (y + h > panel.y) y = std::max(8, panel.y - h - 8);
        }
        return SDL_Rect{16, y, std::max(200, playfield_w_ - 48 - sw), h};
    }
    void net_chat_ime() {
        const SDL_Rect b = net_chat_box_rect();
        ime_open(b.x, b.y, b.w, b.h, net_chat_text_, ra2engine::kOsImeChat, 100);
    }
    void net_chat_send() {
        std::string t = net_chat_text_;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        net_chat_text_.clear();
        net_chat_open_ = false;
        SDL_StopTextInput();
        if (beacon_editing_) {   // W9-05-08: the selected beacon's text (global 0x22)
            beacon_editing_ = false;
            beacon_set_text(local_house_, beacon_sel_, t, true);
            return;
        }
        if (t.empty()) return;
        const std::string me = net_->name_of(local_house_);
        // W9-05-07: the chosen channel's recipients; an observer may only talk
        // to everyone (TXT_OBSERVER_CANT_SEND).
        bool blocked = false;
        const std::vector<int> to = mp_chat_recipients(chat_mode_, blocked);
        if (blocked) {
            ++chat_blocked_;
            post_message(gf_csf("TXT_OBSERVER_CANT_SEND", "Observers can only send chat messages to all players"));
            return;
        }
        mp_chat_last_to_ = to;
        ++chat_sent_;
        net_->game->send_chat(me, t, net_->color_of(local_house_), &to);
        post_message(me + ": " + t, message_seconds(), net_color(local_house_));
        pres_log(0, "net chat " + me + ": " + t);
        std::cerr << "[net] chat sent mode " << chat_mode_ << " to " << to.size() << " player(s)\n";
    }
    static bool net_hit(const SDL_Rect& r, int x, int y) {
        return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
    }
    bool net_handle(const ra2engine::InputEvent& ev) {
        if (!net_) return false;
        const bool down = ev.kind == ra2engine::InputEvent::PointerDown && ev.button == ra2engine::InputEvent::Left;
        if (net_end_dialog_) {
            if ((down && net_hit(net_btn_ok_, ev.x, ev.y)) ||
                (ev.kind == ra2engine::InputEvent::Key &&
                 (ev.key == ra2engine::kKeyEnter || ev.key == ra2engine::kKeyEscape))) {
                if (!next_scene_) next_scene_ = make_main_menu_scene(no_audio_);
            }
            return ev.kind != ra2engine::InputEvent::Quit;
        }
        if (net_reconnect_dialog_up()) {
            if (down && net_hit(net_btn_drop_, ev.x, ev.y)) {
                const int h = net_->game->session().slowest_peer();
                if (h >= 0) {
                    std::cerr << "[net] dropping house " << h << "\n";
                    net_->game->drop_player(h);
                }
                return true;
            }
            if (down && net_hit(net_btn_quit_, ev.x, ev.y)) {
                gf_abort_answer(0);
                return true;
            }
        }
        if (net_chat_open_) {
            if (ev.kind == ra2engine::InputEvent::Text) {
                if (net_chat_text_.size() < 100) net_chat_text_ += ev.text;
                return true;
            }
            if (ev.kind == ra2engine::InputEvent::Key) {
                if (ev.key == ra2engine::kKeyEnter) net_chat_send();
                else if (ev.key == ra2engine::kKeyEscape) {
                    net_chat_open_ = false;
                    beacon_editing_ = false;
                    net_chat_text_.clear();
                    SDL_StopTextInput();
                } else if (ev.key == ra2engine::kKeyBackspace && !net_chat_text_.empty()) {
                    do net_chat_text_.pop_back();
                    while (!net_chat_text_.empty() && (static_cast<unsigned char>(net_chat_text_.back()) & 0xC0) == 0x80);
                } else if (ev.key == ra2engine::kKeyDelete) {
                    net_chat_text_.clear();
                    ime_sync(net_chat_text_, ra2engine::kOsImeChat, 100);
                }
                return true;   // every key goes to the chat line while it is open
            }
            if (down && net_hit(net_btn_send_, ev.x, ev.y)) {
                net_chat_send();
                return true;
            }
            // W9-04-02: the keyboard's Back hid the keyboard (SDL_StopTextInput)
            // but the line stays open: a tap on it (or on Chat) brings the
            // keyboard back.
            if (down && (net_hit(net_chat_box_, ev.x, ev.y) || net_hit(net_btn_chat_, ev.x, ev.y))) {
                if (!SDL_IsTextInputActive()) net_chat_ime();
                return true;
            }
            return false;
        }
        if (mp_beacon_handle(ev)) return true;   // W9-05-08: the beacon cursor / Enter on a beacon
        if (down && net_hit(net_btn_chat_, ev.x, ev.y)) {
            mp_open_chat(1);   // Android: the soft keyboard; the listeners channel
            return true;
        }
        // W9-05-07 [RE: FUN_0055E050]: Enter = listeners, Backspace = allies,
        // backslash = everyone.
        if (ev.kind == ra2engine::InputEvent::Key && !menu_.open_) {
            if (ev.key == ra2engine::kKeyEnter) { mp_open_chat(1); return true; }
            if (ev.key == ra2engine::kKeyBackspace) { mp_open_chat(2); return true; }
            if (ev.vk == 0xDC) { mp_open_chat(3); return true; }   // VK_OEM_5 (backslash)
        }
        return false;
    }
    bool net_reconnect_dialog_up() const {
        return net_ && net_stalled_ && sim_frame_ > 0 && net_away_.empty() && net_->game->session().reconnect_dialog();
    }

    // --- drawing --------------------------------------------------------------
    void net_box(SDL_Renderer* ren, const SDL_Rect& r) const {
        const SDL_Color col = hud_ui_color();
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xD0);
        SDL_RenderFillRect(ren, &r);
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
        SDL_RenderDrawRect(ren, &r);
    }
    SDL_Rect net_button(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& label, int cx, int y,
                        double sc) const {
        const int w = static_cast<int>((hud_text_width(label) + 24) * sc);
        const int h = static_cast<int>(28 * sc);   // >= 28 px logical: a finger target
        SDL_Rect r{cx - w / 2, y, w, h};
        net_box(ren, r);
        hud_text(ren, tc, label, cx, y + static_cast<int>(8 * sc), hud_ui_color(), kHudCenter, false, true, sc);
        return r;
    }
    void net_render(SDL_Renderer* ren, ra2engine::TextureCache& tc) {
        if (!net_) return;
        const double sc = hud_touch_scale();
        const SDL_Color col = hud_ui_color();
        const int cx = playfield_w_ / 2;
        net_btn_ok_ = net_btn_drop_ = net_btn_quit_ = net_btn_send_ = SDL_Rect{0, 0, 0, 0};
        // The chat button (touch-friendly; Enter on a keyboard).
        if (!net_end_dialog_ && !menu_.open_) {
            const std::string lbl = "Chat";
            const int w = static_cast<int>((hud_text_width(lbl) + 24) * sc), h = static_cast<int>(26 * sc);
            net_btn_chat_ = SDL_Rect{playfield_w_ - w - 8, static_cast<int>(8 * sc) + 120, w, h};
            net_box(ren, net_btn_chat_);
            hud_text(ren, tc, lbl, net_btn_chat_.x + w / 2, net_btn_chat_.y + static_cast<int>(7 * sc), col, kHudCenter,
                     false, true, sc);
        } else {
            net_btn_chat_ = SDL_Rect{0, 0, 0, 0};
        }
        net_chat_box_ = SDL_Rect{0, 0, 0, 0};
        if (net_chat_open_) {
            const SDL_Rect box = net_chat_box_rect();   // W9-04-12: the same rect the IME was given
            const int h = box.h, y = box.y;
            const std::string send = "Send";
            const int sw = static_cast<int>((hud_text_width(send) + 24) * sc);
            net_chat_box_ = box;
            net_box(ren, box);
            const bool caret = (SDL_GetTicks() / 500) % 2 == 0;
            hud_text(ren, tc, mp_chat_prompt() + " " + net_chat_text_ + (caret ? "_" : ""), box.x + 6, y + static_cast<int>(8 * sc),
                     col, kHudLeft, false, true, sc);
            net_btn_send_ = SDL_Rect{box.x + box.w + 8, y, sw, h};
            net_box(ren, net_btn_send_);
            hud_text(ren, tc, send, net_btn_send_.x + sw / 2, y + static_cast<int>(8 * sc), col, kHudCenter, false, true, sc);
        }
        const std::uint64_t now = ra2mp::now_ms();
        int y = kWinH / 3;
        // Away players (05 §7.2): the countdown before the normal stall rules resume.
        for (const NetAway& a : net_away_) {
            const long left = static_cast<long>((a.until_ms > now ? a.until_ms - now : 0) / 1000);
            hud_text(ren, tc, net_->name_of(a.house) + " paused the game -- waiting " + std::to_string(left) + " s",
                     cx, y, net_color(a.house), kHudCenter, true, true, sc);
            y += static_cast<int>(22 * sc);
        }
        if (net_stalled_ && net_stall_since_ && now - net_stall_since_ > 1000 && !net_end_dialog_) {
            const std::string w = sim_frame_ == 0 ? "Waiting for the other players to finish loading..."
                                                  : "Waiting for players...";
            hud_text(ren, tc, w, cx, y, col, kHudCenter, true, true, sc);
            y += static_cast<int>(26 * sc);
        }
        if (net_reconnect_dialog_up()) {
            // TXT_RECONNECTING_TO / TXT_WAITING_FOR_CONNECTIONS (FUN_0064A8C0):
            // the slowest player, a countdown to the timeout kick, drop / quit.
            const int h = net_->game->session().slowest_peer();
            const ra2::net::WaitTimeouts to = ra2::net::wait_timeouts(net_->game->config().mode);
            const long waited = static_cast<long>((now - net_stall_since_) / 1000);
            const long total = static_cast<long>(to.total * 16 / 1000);
            SDL_Rect box{cx - static_cast<int>(230 * sc), y, static_cast<int>(460 * sc), static_cast<int>(110 * sc)};
            net_box(ren, box);
            hud_text(ren, tc, "Waiting for " + (h >= 0 ? net_->name_of(h) : std::string("players")) + "  (" +
                                  std::to_string(std::max(0L, total - waited)) + " s)",
                     cx, y + static_cast<int>(10 * sc), col, kHudCenter, false, true, sc);
            hud_text(ren, tc, "The game continues when the connection comes back.", cx, y + static_cast<int>(32 * sc),
                     col, kHudCenter, false, true, sc);
            net_btn_drop_ = net_button(ren, tc, "Drop player", cx - static_cast<int>(90 * sc), y + static_cast<int>(66 * sc), sc);
            net_btn_quit_ = net_button(ren, tc, "Quit game", cx + static_cast<int>(90 * sc), y + static_cast<int>(66 * sc), sc);
        }
        if (net_end_dialog_) {
            SDL_Rect box{cx - static_cast<int>(260 * sc), kWinH / 3, static_cast<int>(520 * sc), static_cast<int>(130 * sc)};
            net_box(ren, box);
            hud_text(ren, tc, net_end_title_, cx, box.y + static_cast<int>(10 * sc), col, kHudCenter, false, true, sc);
            hud_text(ren, tc, hud_wrap(net_end_text_, static_cast<int>(500)), cx, box.y + static_cast<int>(32 * sc), col,
                     kHudCenter, false, true, sc);
            net_btn_ok_ = net_button(ren, tc, "OK", cx, box.y + box.h - static_cast<int>(36 * sc), sc);
        }
    }

    // --play probe keys (scripts/play/README.md "Network play").
    void net_probe(ra2play::Probe& p) const {
        if (!net_) return;   // single player: no keys (its play_hash must not move)
        p.set("net", 1);
        p.set("sim_frame", static_cast<double>(sim_frame_));
        p.set("net_frame", static_cast<double>(net_->game->frame()));
        p.set("net_stalled", net_stalled_ ? 1 : 0);
        p.set("net_stalls", net_stalls_);
        p.set("net_players", net_->game->session().players());
        p.set("net_max_ahead", static_cast<double>(net_->game->session().max_ahead()));
        p.set("net_fsr", static_cast<double>(net_->game->session().frame_send_rate()));
        p.set("net_left", net_local_left_ ? 1 : 0);
        p.set("net_exiting", net_exit_requested_ ? 1 : 0);
        p.set("net_end", net_end_dialog_ ? 1 : 0);
        p.set_s("mode_rules", cfg_.mode_rules);   // W9-03-04 the game type's rules override
        p.set_s("rule(GAREFN.TechLevel)", rules_ini_.get("GAREFN", "TechLevel", ""));
        p.set_s("net_end_title", net_end_title_);
        p.set("net_oos_frame", static_cast<double>(net_->game->session().out_of_sync().frame));
        p.set("net_chat_open", net_chat_open_ ? 1 : 0);
        p.set("text_input_active", SDL_IsTextInputActive() ? 1 : 0);   // W9-04-02
        p.set("cam_x", cam_.x);                                        // W9-04-04
        p.set("cam_y", cam_.y);
        p.set("mouse_present", mouse_present_ ? 1 : 0);
        p.set("net_away", static_cast<double>(net_away_.size()));
        p.set("net_hash", static_cast<double>(net_->game->last_hash()));
        p.set("net_hash_avg_us", net_hash_n_ ? 1000.0 * net_hash_ms_sum_ / net_hash_n_ : 0.0);
        for (const ra2mp::MpPlayer& pl : net_->session.players)
            p.set("net_house_out(" + std::to_string(pl.house) + ")", world_.house_outcome(pl.house));
        mp_probe(p);   // W9-05 (game_scene_mp.inc)
    }

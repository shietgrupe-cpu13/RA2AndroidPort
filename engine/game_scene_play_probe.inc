// G0 --play (agent G): the extended state probe + script hooks of GameScene,
// #included INSIDE the class body (public section) next to agent B's
// game_scene_play.inc (name/locate/probe/cheat). Keys documented in
// scripts/play/README.md. Interactive-only: nothing here runs in a gate.

    // --- G0 --play state probe (playtest.hpp) --------------------------------
    // Append-only and read-only (the cheats below are the one exception, and
    // only a script can call them). Keys are documented in
    // scripts/play/README.md; a field that a later phase renames or removes
    // makes scripts report missing-probe:<key> instead of crashing.
    mutable int w5_peak_sorties_ = 0;   // wave-5: peak AI fixed-wing sorties seen
    // P4: cells `place` already clicked for the armed type; a repeated `place`
    // while the same placement is still armed (the click was refused, e.g. the
    // pointer resolved onto a raised neighbour cell) tries the next candidate.
    int play_place_type_ = -1;
    std::vector<std::pair<int, int>> play_place_tried_;
    void play_probe(ra2play::Probe& p) override {
        p.scene = "game";
        p.zero_families = {"units", "buildings", "built", "selected_type", "harv", "harvesters",
                           "harv_bales", "can_build", "cameo", "cameo_tab", "moving",
                           "bad_place"};
        p.set_s("mode", campaign_ ? "campaign" : "skirmish");
        net_probe(p);   // NET-PLAY keys (net, net_frame, net_stalled, net_end, ...)
        // W5-E: sound / EVA evidence (pres_audio.inc counters): snd(KEY),
        // why(<trigger>), eva(NAME) started, evareq(NAME) requested.
        for (const char* f : {"snd", "why", "eva", "evareq"}) p.zero_families.insert(f);
        for (const auto& kv : g_pres_audio_counts) p.set(kv.first, static_cast<double>(kv.second));
        p.set("follow_unit", follow_unit_);           // W5-E Follow hotkey
        p.set("hidden_marked", static_cast<double>(hidden_.marked));   // W5-E2 occluded objects (last frame)
        p.set_s("load_art", SkirmishLoadArt::g_skirmish_load_art_drawn);   // W5-E skirmish loading art
        p.set_s("screencap", last_screencap_);        // W5-E ScreenCapture hotkey
        p.set("hotkeys_version", hotkeys_ver_);
        p.set("saves_indicated", saves_indicated_);          // W5-E saving indicator
        p.set_s("music_track", pres_.sound().current_track());
        p.set_s("loaded_track", loaded_track_);
        p.set("loaded_audio", loaded_audio_ ? 1 : 0);
        p.set("credits", lcredits());
        p.set("credits_shown", credits_shown_);
        p.set_s("tooltip", tooltip_now());   // W2-UI-07
        p.set("playfield_w", playfield_w_);
        p.set("cam_x", cam_.x);
        p.set("cam_y", cam_.y);
        p.set("anim_tick", static_cast<double>(g_pres_tick));   // W3-B: the logic-tick anim clock
        {   // W3-B: live infantry per sequence (iseq(Walk), iseq(Prone) ...), prone /
            // vanished counts, and the presentation's corpse / death-anim counters.
            p.zero_families.insert("iseq");
            p.set("iprone", 0);
            for (const ra2engine::Unit& u : world_.units()) {
                if (!u.ia.armed || !u.alive) continue;
                p.add(std::string("iseq(") + ra2engine::inf_seq_name(u.ia.seq) + ")", 1);
                if (u.ia.prone) p.add("iprone", 1);
            }
            p.set("corpses", pres_.corpses_);
            p.set("death_anims", pres_.death_anims_);
            p.set("veh_explosions", pres_.explosions_);
            p.set("unloading_drawn", static_cast<double>(g_unloading_class.size()));
        }
        // W3-FX effects counters (pres_effects.inc).
        p.set("fx_systems", static_cast<double>(pres_.effects().system_count()));
        p.set("fx_particles", static_cast<double>(pres_.effects().particle_count()));
        p.set("fx_debris", static_cast<double>(pres_.effects().debris_count()));
        p.set("fx_twinkles", static_cast<double>(pres_.effects().twinkle_count()));
        p.set("fx_wakes", static_cast<double>(pres_.effects().wakes_spawned()));
        p.set("fx_lines", static_cast<double>(pres_.effects().line_count()));
        p.set("fx_v3_puffs", static_cast<double>(pres_.effects().v3_puffs()));
        p.set("fx_splashes", static_cast<double>(pres_.effects().splashes()));
        p.set("ambient_live", static_cast<double>(pres_.ambient_live()));
        p.set("projectiles", static_cast<double>(world_.projectiles().size()));
        {   // W3-FX: who fired the newest projectile (trail checks)
            std::string who;
            if (!world_.projectiles().empty()) {
                const ra2engine::Projectile& pj = world_.projectiles().back();
                if (pj.attacker >= 0 && pj.attacker < static_cast<int>(world_.units().size()))
                    who = world_.units()[static_cast<std::size_t>(pj.attacker)].image;
                else if (pj.attacker_building >= 0)
                    who = "building";
            }
            p.set_s("proj_last", who);
        }
        p.set("fx_anims", static_cast<double>(pres_.fx().live()));
        {   // W7-12 presentation evidence: pres(<trigger>) counters, beams,
            // mind-control pairs, sinking hulls, the voice table by rules id.
            p.zero_families.insert("pres");
            for (const auto& kv : pres_.w5_counts()) {
                std::string k = kv.first;
                for (char& c : k) if (c == ' ') c = '_';
                p.set("pres(" + k + ")", static_cast<double>(kv.second));
            }
            p.set("beams_added", static_cast<double>(pres_.fx().beams_added()));
            p.set("beams_live", static_cast<double>(pres_.fx().beams_live()));
            p.set("mc_pairs", static_cast<double>(pres_.mc_count()));
            p.set("sinking_hulls", static_cast<double>(g_pres_sinkers.size()));
            p.set("first_selected", pres_.first_selected());
            for (const char* id : {"E1", "E2", "HTNK", "ENGINEER", "SENGINEER", "CIV1", "CIV3", "CTECH", "COW", "JUMPJET",
                                   "ORCA", "DTRUCK", "GHOST", "SAPC"})
                for (const char* f : {"select", "die", "deploy", "feedback", "move_sound", "crush", "report"})
                    p.set_s(std::string("voice(") + id + "," + f + ")", pres_.voice_key(id, f));
        }
        p.set("radar_pings", static_cast<double>(g_radar_pings.size()));
        p.set("sw_strikes", static_cast<double>(world_.sw_strikes().size()));
        {
            int bolts = 0;
            int bx = -1, by = -1;
            for (const ra2engine::SwEvent& e : world_.sw_events())
                if (e.kind == ra2engine::SwEvent::Bolt) { ++bolts; bx = e.x; by = e.y; }
            p.set("sw_bolts", bolts);
            p.set("sw_bolt_x", bx);
            p.set("sw_bolt_y", by);
        }
        const int houses = world_.house_count();
        p.set("houses", houses);
        p.set("hover_unit", g_hover_unit);   // W6-05-07 (as of the last render)
        p.set("tile_misses", a_.theater.miss_count());             // W6-05-02 tile files found nowhere
        p.set("tile_fallbacks", ra2engine::terrain_fallback_cells());   // W6-05-10 cells drawn as clear
        // W6-05-04/05: the bottom-right timer stack, line i's text.
        build_timer_stack();
        p.set("timer_lines", static_cast<int>(timer_lines_.size()));
        for (std::size_t i = 0; i < timer_lines_.size(); ++i)
            p.set_s("timer_line(" + std::to_string(i) + ")", timer_lines_[i].text);
        if (campaign_) p.set_s("timer_text", campaign_->triggers().timer_text());
        for (int h = 0; h < houses; ++h) {
            const std::string hs = std::to_string(h);
            p.set("house_credits(" + hs + ")", world_.house_credits(h));
            p.set("defeated(" + hs + ")", world_.house_defeated(h) ? 1 : 0);
            p.set("blackout(" + hs + ")", world_.blackout_frames(h));   // P3: spy power-plant blackout left
            p.set("team(" + hs + ")", world_.house_team(h));
            // [SK] what the Skirmish page handed the game, per house.
            const ra2engine::World::Side hsd = world_.house_side(h);
            p.set_s("side(" + hs + ")", hsd == ra2engine::World::Side::Soviet ? "SOVIET"
                                        : hsd == ra2engine::World::Side::Allied ? "ALLIED"
                                        : hsd == ra2engine::World::Side::Yuri ? "YURI" : "NEUTRAL");
            p.set("house_color(" + hs + ")", world_.house_color(h));
            if (h < static_cast<int>(cfg_.slots.size())) {
                p.set_s("country(" + hs + ")", cfg_.slots[(size_t)h].country);
                p.set("combatant(" + hs + ")", cfg_.slots[(size_t)h].combatant ? 1 : 0);
            }
            if (const ra2engine::AiHouseConfig* ac = world_.ai_config(h))
                p.set_s("ai(" + hs + ")", ac->difficulty == ra2engine::AiDifficulty::Easy ? "Easy"
                                          : ac->difficulty == ra2engine::AiDifficulty::Normal ? "Medium"
                                          : "Brutal");
            else
                p.set_s("ai(" + hs + ")", "Human");
            p.set("sw(" + hs + ")", world_.sw_state(h) ? 1 : 0);
            p.set("sw_ready(" + hs + ")", world_.sw_ready(h) ? 1 : 0);
            p.set("units(" + hs + ")", 0);
            p.set("buildings(" + hs + ")", 0);
            p.set("carried(" + hs + ")", 0);   // W4-SQ: aboard a transport / plane / chute
        }
        // Units (alive, not dying), keyed by rules id where the roster knows it.
        int selected = 0;
        for (const ra2engine::Unit& u : world_.units()) {
            if (!u.alive || u.die_time >= 0.0) continue;
            const std::string hs = std::to_string(u.house);
            const std::string id = play_unit_id(u);
            p.add("units(" + hs + ")", 1);
            p.add("units(" + hs + "," + id + ")", 1);
            if (u.cargo_of >= 0 && u.cargo_of != ra2engine::kGarrisonCargo) p.add("carried(" + hs + ")", 1);
            // P1: units a `unit H:TYPE:n` target can reach (not aboard anything).
            if (u.cargo_of < 0) p.add("free(" + hs + "," + id + ")", 1);
            if (u.moving()) p.add("moving(" + hs + ")", 1);
            if (!p.n.count("uhp(" + hs + "," + id + ")")) {   // W2-SK: first live unit's HP / rank
                p.set("uhp(" + hs + "," + id + ")", u.hp);
                p.set("urank(" + hs + "," + id + ")", u.veteran_rank);
                p.set("udisguised(" + hs + "," + id + ")", u.disguised ? 1 : 0);
                p.set("urof(" + hs + "," + id + ")", u.w_rof);   // W6-02-01: house handicap ROF
            }
            if (!p.n.count("weapon_damage(" + id + ")")) {   // first live unit of the type
                p.set("weapon_damage(" + id + ")", u.w_damage);
                p.set("weapon_range(" + id + ")", u.w_range);
                p.set("weapon_rof(" + id + ")", u.w_rof);
            }
            if (usel(u)) { ++selected; p.add("selected_type(" + id + ")", 1); }
            if (u.is_harvester) {
                static const char* kHs[] = {"None", "SeekOre", "MoveToOre", "Harvesting",
                                            "ReturnToRef", "WaitDock", "Unloading"};
                const int st = static_cast<int>(u.harv);
                p.add("harvesters(" + hs + ")", 1);
                p.add("harv(" + hs + "," + (st >= 0 && st < 7 ? kHs[st] : "?") + ")", 1);
                p.add("harv_bales(" + hs + ")", u.bales_ore + u.bales_gems);
            }
        }
        p.set("selected", selected);
        {   // W2-SK economy probes: ore left, ore-mine output, AI wallets
            int cells = 0, mined = 0;
            for (int y = 0; y < world_.ore().h(); ++y)
                for (int x = 0; x < world_.ore().w(); ++x) cells += world_.ore().has_ore(x, y) ? 1 : 0;
            for (const ra2engine::OreMine& m : world_.ore().mines()) mined += m.spawned;
            p.set("ore_cells", cells);
            p.set("ore_mines", static_cast<double>(world_.ore().mines().size()));
            p.set("mine_spawned", mined);
            for (int h = 0; h < world_.house_count(); ++h) {
                if (world_.ai_enabled(h)) p.set("ai_credits(" + std::to_string(h) + ")", world_.ai_credits(h));
                p.set("income(" + std::to_string(h) + ")", world_.harvest_income(h));
            }
        }
        {   // the lowest-index selected unit's cell (for "did it move" checks)
            const ra2engine::Unit* s0 = nullptr;
            for (const ra2engine::Unit& u : world_.units())
                if (usel(u) && u.alive && u.die_time < 0.0) { s0 = &u; break; }
            p.set("sel_cx", s0 ? std::floor(s0->fx + 0.5) : -1);
            p.set("sel_cy", s0 ? std::floor(s0->fy + 0.5) : -1);
            p.set("sel_moving", s0 && s0->moving() ? 1 : 0);
            p.set("sel_occupier", s0 && s0->occupier ? 1 : 0);          // G16
            p.set("sel_occupy_target", s0 ? s0->occupy_target : -1);
            p.set("sel_attack_cell", s0 ? s0->attack_cell_x : -1);
            p.set("sel_target", s0 ? s0->target : -1);
            p.set("sel_rof_cool", s0 ? s0->rearm_frames : -1);
            p.set("sel_turret", s0 ? s0->turret_facing : -1);
            p.set("sel_body", s0 ? s0->body_facing : -1);
            p.set("sel_damage", s0 ? s0->w_damage : -1);
            p.set("sel_wall", s0 ? (s0->w_active == 1 ? s0->w2.wall : s0->p_wall) : -1);
        }
        for (int h = 0; h < houses; ++h) {   // losses since the game began
            int lost = 0, blost = 0;
            long hp_lost = 0;
            for (const ra2engine::Unit& u : world_.units())
                if (u.house == h && (!u.alive || u.die_time >= 0.0) && !u.vet.dont_score && !u.deployed_away) ++lost;   // W4: DontScore
            for (const ra2engine::Building& b : world_.buildings()) {
                if (b.house != h) continue;
                if (!b.alive || b.state == ra2engine::Building::State::Dead) ++blost;
                else hp_lost += b.max_hp - b.hp;
            }
            p.set("units_lost(" + std::to_string(h) + ")", lost);
            p.set("buildings_lost(" + std::to_string(h) + ")", blost);
            p.set("hp_lost(" + std::to_string(h) + ")", static_cast<double>(hp_lost));
        }
        for (const ra2engine::Building& b : world_.buildings()) {
            if (!b.alive || b.state == ra2engine::Building::State::Dead) continue;
            const std::string hs = std::to_string(b.house);
            const std::string id = (b.type >= 0 && b.type < static_cast<int>(ba_.roster.size()))
                                       ? ba_.roster[b.type].id : std::to_string(b.type);
            p.add("buildings(" + hs + ")", 1);
            p.add("buildings(" + hs + "," + id + ")", 1);
            if (b.state == ra2engine::Building::State::Idle) p.add("built(" + hs + "," + id + ")", 1);
            // W8-03-01 (w8fix-place): buildings standing on a cell the
            // placement rule refuses (overlay / slope / bridge / unbuildable
            // land; FUN_0047C530). 0 for every built or deployed structure.
            const std::vector<std::uint8_t>& bs = world_.build_static();
            if (!bs.empty()) {
                bool bad = false;
                for (int j = 0; j < b.fh && !bad; ++j)
                    for (int i = 0; i < b.fw && !bad; ++i) {
                        const int x = b.cx + i, y = b.cy + j;
                        if (x < 0 || y < 0 || x >= world_.grid_w() || y >= world_.grid_h()) continue;
                        bad = bs[static_cast<std::size_t>(y) * world_.grid_w() + x] == 0 ||
                              (!ba_.roster.empty() && b.type >= 0 && b.type < static_cast<int>(ba_.roster.size()) &&
                               !ba_.roster[b.type].wall && world_.ore().has_ore(x, y));
                    }
                if (bad) p.add("bad_place(" + hs + ")", 1);
            }
        }
        p.set("selected_building", input_.selected_building());
        {   // W6-01-01: the selected building's rules id ("" none)
            const int sb = input_.selected_building();
            const auto& bs = world_.buildings();
            std::string sid;
            if (sb >= 0 && sb < static_cast<int>(bs.size()) && bs[(size_t)sb].type >= 0 &&
                bs[(size_t)sb].type < static_cast<int>(ba_.roster.size()))
                sid = ba_.roster[(size_t)bs[(size_t)sb].type].id;
            p.set_s("selected_building_type", sid);
        }
        {   // own objects inside the playfield view (camera opens on the player?)
            int on = 0;
            auto vis = [&](double fx, double fy) {
                const int sx = static_cast<int>(std::lround((fx - fy) * 30.0)) + 30 + cam_.x;
                const int sy = static_cast<int>(std::lround((fx + fy) * 15.0)) + 15 + cam_.y;
                return sx >= 0 && sx < playfield_w_ && sy >= 0 && sy < kWinH;
            };
            // W4-MC-07 (HB-3): only objects a human can actually see and use:
            // alive, not carried, on the board (LocalSize) and not under shroud.
            auto seen = [&](int cx, int cy) {
                // (before the first logic tick nothing is revealed yet)
                return on_board(cx, cy) && (world_.vet_frame() == 0 || world_.object_visible_at(local_house_, cx, cy));
            };
            int ctl = 0, ctl_on = 0;
            for (const ra2engine::Unit& u : world_.units()) {
                if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
                const bool usable = seen(u.cell_x, u.cell_y);
                if (u.house == local_house_ && usable && vis(u.fx, u.fy)) ++on;
                if (world_.controllable(u.house) && usable) {
                    ++ctl;
                    if (vis(u.fx, u.fy)) ++ctl_on;
                }
            }
            for (const ra2engine::Building& b : world_.buildings())
                if (b.house == local_house_ && b.alive && seen(b.cx, b.cy) && vis(b.cx + b.fw / 2.0, b.cy + b.fh / 2.0)) ++on;
            p.set("own_on_screen", on);
            {   // fix-black: what the LOCAL house sees (a network joiner plays house != 0)
                int expl = 0, expl_view = 0, own_n = 0;
                double ox = 0, oy = 0;
                for (const ra2engine::Unit& u : world_.units())
                    if (u.alive && u.die_time < 0.0 && u.cargo_of < 0 && u.house == local_house_) { ox += u.cell_x; oy += u.cell_y; ++own_n; }
                for (const ra2engine::Building& b : world_.buildings())
                    if (b.alive && b.house == local_house_) { ox += b.cx; oy += b.cy; ++own_n; }
                for (int y = 0; y < world_.grid_h(); ++y)
                    for (int x = 0; x < world_.grid_w(); ++x) {
                        if (!world_.shroud_enabled() || !world_.explored_at(local_house_, x, y)) continue;
                        ++expl;
                        if (vis(x, y)) ++expl_view;
                    }
                p.set("local_explored", expl);            // cells the local house explored
                p.set("local_explored_on_screen", expl_view);   // ... of which inside the view
                p.set("terrain_drawn", g_terrain_drawn);  // terrain tiles the last rendered frame drew
                // the view centre and the local house's object centroid (cells)
                const double sx = playfield_w_ / 2.0 - 30 - cam_.x, sy = kWinH / 2.0 - 15 - cam_.y;
                p.set("cam_cell_x", (sx / 30.0 + sy / 15.0) / 2.0);
                p.set("cam_cell_y", (sy / 15.0 - sx / 30.0) / 2.0);
                p.set("own_cx", own_n ? ox / own_n : -1);
                p.set("own_cy", own_n ? oy / own_n : -1);
                {   // cells between the view centre and the local house's centroid
                    const double ccx = (sx / 30.0 + sy / 15.0) / 2.0, ccy = (sy / 15.0 - sx / 30.0) / 2.0;
                    const double dx = own_n ? ccx - ox / own_n : 9999, dy = own_n ? ccy - oy / own_n : 9999;
                    p.set("cam_own_dist", std::sqrt(dx * dx + dy * dy));
                }
            }
            p.set("ctl_units", ctl);         // controllable (house 0 + PlayerControl) usable units
            p.set("ctl_on_screen", ctl_on);  // ... of which inside the view
            p.set("input_locked", input_locked_ ? 1 : 0);
            if (campaign_) p.set("view_resized", campaign_->local_version());   // P3: ResizeView (40) count
            p.set("touch_ui", touch_ui_ ? 1 : 0);              // W4-AM touch keys
            p.set("touch_bar_open", touch_bar_visible() && !touch_panel_collapsed_ ? 1 : 0);
            p.set("touch_banner", touch_banner_visible() ? 1 : 0);
            p.set("touch_mode", touch_mode());
            p.set("touch_aim", touch_aim_bld_);                 // W6-01-02 aimed building (-1 none)
            p.set("touch_aim_sw", touch_aim_cx_ != INT_MIN ? 1 : 0);   // W6-01-07 SW target aimed
            p.set("cameo_menu", cameo_menu_buttons().empty() ? 0 : 1); // W6-01-03 cameo popup up
            p.set("touch_box", touch_box_ ? 1 : 0);             // W6-01-11 sticky BOX
            p.set("touch_page2", touch_page2_ ? 1 : 0);         // W6-01-09 MORE page
            p.set("tooltip_up", tooltip_now().empty() ? 0 : 1);
            double vcx = 0, vcy = 0;
            if (campaign_) camp_view_cell(vcx, vcy);
            p.set("view_cx", vcx);
            p.set("view_cy", vcy);
        }
        // Session verdict as the player sees it. A skirmish has no end path on
        // this tree (GameScene never reads the World's victory state, G-02), so
        // `outcome` stays RUNNING there; `world_outcome` is the World's view.
        std::string outcome = "RUNNING";
        if (campaign_ && campaign_->outcome() != 0) outcome = campaign_->outcome() == 1 ? "WON" : "LOST";
        else if (!campaign_ && gf_outcome_ != 0) outcome = gf_outcome_ > 0 ? "WON" : "LOST";   // gF G-02
        p.set_s("outcome", outcome);
        std::string wo = "RUNNING";
        if (world_.victory_enabled()) {
            if (world_.house_defeated(local_house_)) wo = "LOST";
            else if (world_.house_outcome(local_house_) > 0) wo = "WON";   // W9-05-04
        }
        p.set_s("world_outcome", wo);
        p.set("victory_enabled", world_.victory_enabled() ? 1 : 0);
        p.set("winning_team", world_.winning_team());
        // [SK] lobby options as the game consumes them.
        p.set("game_speed", cfg_.game_speed >= 0 ? cfg_.game_speed
                                                 : g_user_settings ? g_user_settings->game_speed : -1);
        p.set("mcv_redeploys", world_.mcv_redeploys() ? 1 : 0);
        p.set("sw_allowed", cfg_.superweapons ? 1 : 0);
        p.set("short_game", cfg_.short_game ? 1 : 0);
        p.set("crates_on", cfg_.crates ? 1 : 0);
        {   // W5-E: live crates on the map
            int n = 0;
            for (const auto& c : world_.crates()) n += c.x >= 0;
            p.set("crates_live", n);
        }
        p.set("unit_count_cfg", cfg_.unit_count);
        p.set("start_cash", cfg_.starting_cash);
        // Power (house 0).
        const ra2engine::PowerTotals pt = ra2engine::power_totals(world_.buildings(), ba_.roster, local_house_);
        p.set("power_out", pt.output);
        p.set("power_drain", pt.drain);
        // P2: every house's power (campaign proofs that black out an AI base).
        for (int h = 1; h < world_.house_count() && h < 16; ++h) {
            const ra2engine::PowerTotals ph = ra2engine::power_totals(world_.buildings(), ba_.roster, h);
            if (ph.output == 0 && ph.drain == 0) continue;
            p.set("power_out(" + std::to_string(h) + ")", ph.output);
            p.set("power_drain(" + std::to_string(h) + ")", ph.drain);
            p.set("low_power(" + std::to_string(h) + ")", world_.house_low_power(h) ? 1 : 0);
        }
        // Sidebar: active tab, its cameos (click targets), every tab's roster
        // and whether a click would start each entry now.
        p.set("tab", active_tab_);
        p.set("sidebar", sidebar_up_ ? 1 : 0);
        auto rect = [](const ra2engine::SbRect& r) { return ra2play::Rect{r.x, r.y, r.w, r.h}; };
        for (int t = 0; t < 4; ++t) p.targets["tab:" + std::to_string(t)] = rect(lay_.tabs[t]);
        if (ba_.has_repair_sell) {
            p.targets["repair"] = rect(lay_.repair_btn);
            p.targets["sell"] = rect(lay_.sell_btn);
        }
        if (!rp_on_ && world_.sw_state(0))
            p.targets["sw"] = ra2play::Rect{lay_.bar.x + ra2engine::kCameoX, lay_.radar.y + 2,
                                            ra2engine::kCameoW, ra2engine::kCameoH};
        if (!rp_on_) {   // Demo powers use the legacy radar positions.
            const std::vector<ra2engine::SuperweaponType> swt = world_.sw_types(local_house_);
            for (int k = 0; k < static_cast<int>(swt.size()); ++k) {
                const ra2engine::SbRect r = sw_slot_rect(lay_, k);
                p.targets[std::string("sw:") + ra2engine::superweapon_type_name(swt[k])] =
                    ra2play::Rect{r.x, r.y, r.w, r.h};
            }
        }
        if (sidebar_up_ && ba_.has_optbtns) {
            const int w = ba_.sb_optbtn.width() > 0 ? ba_.sb_optbtn.width() : 70;
            const int h = ba_.sb_optbtn.height() > 0 ? ba_.sb_optbtn.height() : 16;
            p.targets["options"] = ra2play::Rect{lay_.opt_btn.x, lay_.opt_btn.y, w, h};
        }
        for (const auto& cell : lay_.cameos) {
            const auto power=ra2engine::power_cameo_type(cell.type);
            if (power!=ra2engine::SuperweaponType::None)
                p.targets[std::string("sw:")+ra2engine::superweapon_type_name(power)]=rect(cell.rect);
            else p.targets["cameo:"+play_entry_id(active_tab_,cell.type)]=rect(cell.rect);
        }
        {   // Publish the restore target before the first scripted touch.
            const SDL_Rect arrow = touch_panel_toggle_rect();
            p.targets["touch:panel-toggle"] = ra2play::Rect{arrow.x,arrow.y,arrow.w,arrow.h};
            p.targets["touch:CMD"] = ra2play::Rect{arrow.x,arrow.y,arrow.w,arrow.h};
        }
        if (touch_bar_visible()) {
            for (int i = 0; i < touch_btn_count(); ++i) {
                const SDL_Rect r = touch_btn_rect(i);
                p.targets[std::string("touch:") + touch_buttons()[static_cast<std::size_t>(i)].label] =
                    ra2play::Rect{r.x, r.y, r.w, r.h};
            }
        }
        // W6-01-01: `art:<ID>` -- a point on the UPPER art of house 0's first
        // building of that type (an opaque pixel of the drawn frame, one fifth
        // down, nearest the frame's centre column): a tap there is a tap on
        // the tower head / the lab dome, not on the foundation.
        if (ba_.ok) {
            for (int bi = 0; bi < static_cast<int>(world_.buildings().size()); ++bi) {
                const ra2engine::Building& b = world_.buildings()[static_cast<std::size_t>(bi)];
                if (b.house != local_house_ || !b.alive || b.state == ra2engine::Building::State::Dead) continue;
                if (b.type < 0 || b.type >= static_cast<int>(ba_.roster.size())) continue;
                const std::string key = "art:" + ba_.roster[static_cast<std::size_t>(b.type)].id;
                if (p.targets.count(key)) continue;
                const BuildingArt& art = ba_.art[static_cast<std::size_t>(b.type)];
                if (!art.ok || art.shp.count() <= 0) continue;
                const ra2assets::MapCell* mc = a_.map.at(b.cx, b.cy);
                const ra2engine::ScreenPt dp = ra2engine::building_draw_point(b.cx, b.cy, mc ? mc->level : 0);
                const ra2assets::ShpFile::Frame f = art.shp.frame_info(0);
                const int x0 = dp.x + cam_.x - art.shp.width() / 2 + f.x, y0 = dp.y + cam_.y - art.shp.height() / 2 + f.y;
                const int yy = y0 + f.h / 5;
                for (int d = 0; d < f.w; ++d) {
                    const int xx = x0 + f.w / 2 + ((d & 1) ? -(d + 1) / 2 : d / 2);
                    if (input_.building_art_at(xx, yy) == bi) {
                        p.targets[key] = ra2play::Rect{xx, yy, 1, 1};
                        break;
                    }
                }
            }
        }
        if (touch_banner_visible()) {
            const SDL_Rect r = touch_cancel_rect();
            p.targets["touch:cancel"] = ra2play::Rect{r.x, r.y, r.w, r.h};
        }
        for (const CameoMenuBtn& b : cameo_menu_buttons())   // W6-01-03 popup buttons
            p.targets[std::string("cmenu:") + b.label] = ra2play::Rect{b.r.x, b.r.y, b.r.w, b.r.h};
        std::string ids;
        for (int t = 0; t < 4; ++t) {
            for (int e : tab_buildable(t)) {
                const std::string id = play_entry_id(t, e);
                p.set("cameo(" + id + ")", 1);
                p.set("cameo_tab(" + id + ")", t);
                p.set("can_build(" + id + ")", play_can_start(t, e) ? 1 : 0);
                if (t == active_tab_) ids += (ids.empty() ? "" : ",") + id;
            }
        }
        p.set_s("tab_ids", ids);
        // Queues.
        static const std::pair<ra2engine::QueueCat, const char*> kCats[] = {
            {ra2engine::QueueCat::Structure, "structure"}, {ra2engine::QueueCat::Defense, "defense"},
            {ra2engine::QueueCat::Infantry, "infantry"}, {ra2engine::QueueCat::Vehicle, "vehicle"},
            {ra2engine::QueueCat::Naval, "naval"}, {ra2engine::QueueCat::Aircraft, "aircraft"}};
        for (const auto& [cat, nm] : kCats) {
            const ra2engine::QueueSlot& sl = lq().state(cat).slot;
            const int t = (cat == ra2engine::QueueCat::Structure) ? 0 : (cat == ra2engine::QueueCat::Defense) ? 1
                        : (cat == ra2engine::QueueCat::Infantry) ? 2 : 3;   // 2/3 = unit roster
            p.set_s(std::string("queue(") + nm + ")", sl.type >= 0 ? play_entry_id(t, sl.type) : "");
            p.set(std::string("queue_stage(") + nm + ")", sl.stage);
            p.set(std::string("queue_ready(") + nm + ")", sl.ready ? 1 : 0);
        }
        // Armed modes / overlays.
        p.set("placing", input_.placement_active() ? 1 : 0);
        if (!input_.placement_active()) { play_place_tried_.clear(); play_place_type_ = -1; }
        if (input_.placement_active()) {   // P4: the origin a click would place at now
            int ox = 0, oy = 0;
            input_.placement_cell(ox, oy);
            p.set("place_origin_x", ox);
            p.set("place_origin_y", oy);
            if (!play_place_tried_.empty() && play_place_type_ == input_.placement_type()) {
                p.set("place_target_x", play_place_tried_.back().first);
                p.set("place_target_y", play_place_tried_.back().second);
            }
        }
        p.set_s("placing_type", input_.placement_active() && input_.placement_type() >= 0 &&
                                        input_.placement_type() < static_cast<int>(ba_.roster.size())
                                    ? ba_.roster[input_.placement_type()].id : "");
        const ra2engine::Input::BaseMode bm = input_.base_mode();
        p.set_s("base_mode", bm == ra2engine::Input::BaseMode::Repair ? "repair"
                             : bm == ra2engine::Input::BaseMode::Sell ? "sell"
                             : bm == ra2engine::Input::BaseMode::PowerToggle ? "power" : "none");
        {   // W5-E2: the player's powered-down buildings
            int off = 0;
            for (const ra2engine::Building& b : world_.buildings()) off += (b.alive && b.house == local_house_ && b.power_off);
            p.set("powered_down(0)", off);
        }
        p.set("sw_targeting", input_.sw_targeting() ? 1 : 0);
        p.set("dialog_open", 0);
        gf_abort_.play_probe(p);   // gF G-14: Abort Mission confirm
        menu_.play_probe(p);       // W2-UI-15: page 0xB6 publishes confirm_* too (after)
        sld_.play_probe(p);
        p.set_s("banner", newest_message());   // W2-UI-21 message list
        if (campaign_) {
            p.set_s("map", cfg_.campaign_map);
            p.set("campaign_frame", static_cast<double>(campaign_->frame_count()));
            p.set("objectives", static_cast<double>(campaign_->objectives().size()));
            p.set("triggers_fired", campaign_->triggers().fired_count());
            p.set("campaign_outcome", campaign_->outcome());
            p.set("briefing_open", show_briefing_ ? 1 : 0);
            {   // P4: the newest trigger that fired ("f=N fire ID \"Name\" k=K"), for proof debugging
                const std::vector<std::string>& wl = campaign_->witness();
                for (auto it = wl.rbegin(); it != wl.rend(); ++it)
                    if (it->find(" fire ") != std::string::npos) { p.set_s("last_fired", *it); break; }
            }
        } else {
            p.set_s("map", cfg_.map_file);
        }
        // ---- hooks ----
        p.cell_to_screen = [this](double cx, double cy, int& sx, int& sy) {
            const ra2assets::MapCell* mc =
                world_.map().at(static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)));
            const int level = mc ? mc->level : 0;
            sx = static_cast<int>(std::lround((cx - cy) * 30.0)) + 30 + cam_.x;
            sy = static_cast<int>(std::lround((cx + cy) * 15.0 - level * 15)) + 15 + cam_.y;
            return sx >= 40 && sx < playfield_w_ - 40 && sy >= 40 && sy < kWinH - 40;
        };
        p.center_on = [this](double cx, double cy) {
            const ra2assets::MapCell* mc =
                world_.map().at(static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)));
            const int level = mc ? mc->level : 0;
            const int tx = static_cast<int>(std::lround(playfield_w_ / 2.0 - (cx - cy) * 30.0 - 30));
            const int ty = static_cast<int>(std::lround(kWinH / 2.0 - (cx + cy) * 15.0 + level * 15 - 15));
            input_.pan(tx - cam_.x, ty - cam_.y);
        };
        p.find_unit = [this](const std::string& spec, double& cx, double& cy) {
            int h = 0, n = 0;
            std::string type;
            // P3: "H:TYPE:near" / "e:TYPE:near" = the live unit of house H (e = any
            // house hostile to it) nearest the first selected unit, within 9 cells
            // (in sight) -- what a player clicks to pick off the closest threat.
            if (spec.size() > 5 && spec.compare(spec.size() - 5, 5, ":near") == 0) {
                const std::string head = spec.substr(0, spec.size() - 5);
                const std::size_t c = head.find(':');
                if (c == std::string::npos) return false;
                const bool any_enemy = head.substr(0, c) == "e";
                const int hh = any_enemy ? -1 : std::atoi(head.substr(0, c).c_str());
                const std::string ty = head.substr(c + 1);
                const ra2engine::Unit* me = nullptr;
                for (const ra2engine::Unit& u : world_.units())
                    if (u.alive && u.die_time < 0.0 && usel(u)) { me = &u; break; }
                if (!me) return false;
                double best = 9.0 * 9.0 + 1e-9;
                bool found = false;
                for (const ra2engine::Unit& u : world_.units()) {
                    if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.is_aircraft) continue;
                    if (any_enemy ? !world_.is_enemy(me->house, u.house) : u.house != hh) continue;
                    if (ty != "*" && play_unit_id(u) != ty && u.image != ty) continue;
                    const double dx = u.fx - me->fx, dy = u.fy - me->fy, d2 = dx * dx + dy * dy;
                    if (d2 > best) continue;
                    best = d2;
                    cx = u.fx + u.sub_x / 256.0;
                    cy = u.fy + u.sub_y / 256.0;
                    found = true;
                }
                return found;
            }
            if (!play_parse_spec(spec, h, type, n)) return false;
            // P1: TYPE ending in '~' = only a transport with no passenger aboard
            // (`unit 0:FV~:0` = the first empty IFV -- a free seat).
            const bool empty_only = !type.empty() && type.back() == '~';
            if (empty_only) type.pop_back();
            double nx = 0, ny = 0;
            const bool near = n < 0 && play_sel_centre(nx, ny);   // P4 `H:TYPE:near`
            const ra2engine::Unit* best = nullptr;
            double best_d = 0;
            for (const ra2engine::Unit& u : world_.units()) {
                if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.house != h) continue;
                if (type != "*" && play_unit_id(u) != type && u.image != type) continue;
                if (empty_only && !u.cargo.empty()) continue;
                if (near) {
                    const double d = std::hypot(u.fx - nx, u.fy - ny);
                    if (!best || d < best_d) { best = &u; best_d = d; }
                    continue;
                }
                if (n-- > 0) continue;
                best = &u;
                break;
            }
            if (!best) return false;
            cx = best->fx + best->sub_x / 256.0;   // W5-B: the infantry's sub-cell spot
            cy = best->fy + best->sub_y / 256.0;
            return true;
        };
        p.find_building = [this](const std::string& spec, double& cx, double& cy) {
            const int bi = play_find_building(spec);
            if (bi < 0) return false;
            const ra2engine::Building& b = world_.buildings()[bi];
            cx = b.cx + b.fw / 2;
            cy = b.cy + b.fh / 2;
            // P3: a ":near" sweep clicks the building's back (origin) cell -- the
            // centre is often under the player's own tanks crowding its front,
            // and a click there selects a tank instead of ordering the attack.
            if (spec.size() > 5 && spec.compare(spec.size() - 5, 5, ":near") == 0) {
                cx = b.cx; cy = b.cy;
                // the first foundation cell (back rows first) whose pixel is
                // the building and no unit -- what a player's click lands on
                for (int s = 0; s <= b.fw + b.fh - 2; ++s)
                    for (int dx = 0; dx < b.fw; ++dx) {
                        const int dy = s - dx;
                        if (dy < 0 || dy >= b.fh) continue;
                        int px0 = 0, py0 = 0;
                        cell_to_window(b.cx + dx, b.cy + dy, px0, py0);
                        // a 5x5 grid of window points over the cell's diamond
                        for (int oy = -12; oy <= 12; oy += 6)
                            for (int ox = -24; ox <= 24; ox += 12) {
                                const int px = px0 + ox, py = py0 + oy;
                                if (input_.pick_unit_at(px, py) >= 0 || input_.building_under(px, py) != bi) continue;
                                const double fx = ox / 30.0, fy = oy / 15.0;
                                cx = b.cx + dx + (fx + fy) / 2.0;
                                cy = b.cy + dy + (fy - fx) / 2.0;
                                return true;
                            }
                    }
            }
            return true;
        };
        p.find_building_rect = [this](const std::string& spec, int& ox, int& oy, int& fw, int& fh) {
            const int bi = play_find_building(spec);
            if (bi < 0) return false;
            const ra2engine::Building& b = world_.buildings()[bi];
            ox = b.cx; oy = b.cy; fw = b.fw; fh = b.fh;
            return true;
        };
        // ---- G16 world depth (agent G): garrisons + bridges ----
        p.zero_families.insert("occupants");
        p.zero_families.insert("garrisoned");
        p.zero_families.insert("in_garrison");
        p.zero_families.insert("on_bridge");
        p.zero_families.insert("owned_garrisons");
        int garrisonable = 0;
        for (int bi = 0; bi < static_cast<int>(world_.buildings().size()); ++bi) {
            const ra2engine::Building& b = world_.buildings()[bi];
            if (!b.alive || b.state == ra2engine::Building::State::Dead) continue;
            if (b.type < 0 || b.type >= static_cast<int>(ba_.roster.size()) || !ba_.roster[b.type].can_be_occupied)
                continue;
            ++garrisonable;
            const std::string hs = std::to_string(b.house);
            p.add("owned_garrisons(" + hs + ")", 1);
            if (!b.occupants.empty()) {
                p.add("garrisoned(" + hs + ")", 1);
                p.add("occupants(" + hs + ")", static_cast<double>(b.occupants.size()));
            }
        }
        p.set("garrisonable", garrisonable);
        p.set("garrison_shots", static_cast<double>(world_.garrison_shots()));
        // Wave-5 (agent C) witnesses.
        p.set("prism_supports", static_cast<double>(world_.w5_state().prism_supports));
        {
            int bombs = 0;
            for (const ra2engine::Unit& u : world_.units()) bombs += u.alive && u.house == local_house_ && u.bomb_frame >= 0;
            for (const ra2engine::Building& b : world_.buildings()) bombs += b.alive && b.house == local_house_ && b.bomb_frame >= 0;
            p.set("bombs", static_cast<double>(bombs));
        }
        {
            int air = 0;
            for (const ra2engine::Unit& u : world_.units())
                air += u.alive && u.house == 1 && u.an.t.fixed_wing && !u.an.t.spawned && u.an.sortie != 0;
            if (air > w5_peak_sorties_) w5_peak_sorties_ = air;
            p.set("ai_air_sorties", static_cast<double>(w5_peak_sorties_));
        }
        p.set("ai_garrisons", static_cast<double>(world_.w5_state().ai_garrisons));
        p.set("ai_tech_captures", static_cast<double>(world_.w5_state().ai_tech_captures));
        p.set("ship_c4", static_cast<double>(world_.w5_state().ship_c4));
        p.set("culls", static_cast<double>(world_.w5_state().culls));
        {
            int grabbed = 0;
            for (const ra2engine::Unit& u : world_.units()) grabbed += u.alive && u.parasite_on_me >= 0;
            p.set("parasited", static_cast<double>(grabbed));
        }
        p.set("airstrikes", static_cast<double>(world_.w5_state().airstrikes));
        p.set("ifv_repairs", static_cast<double>(world_.w5_state().repairs));
        p.set("defusals", static_cast<double>(world_.w5_state().defusals));
        p.set("passenger_shots", static_cast<double>(world_.w5_state().passenger_shots));
        p.set("stolen_tech", static_cast<double>(world_.stolen_tech(local_house_)));
        p.set("gap_hidden", static_cast<double>(world_.w5_gap_hidden(0)));
        {
            int carried = 0;
            for (const ra2engine::Unit& u : world_.units())
                carried += u.alive && u.house == local_house_ && u.cargo_of >= 0 && u.cargo_of != ra2engine::kGarrisonCargo;
            p.set("in_cargo", static_cast<double>(carried));
        }
        {
            int ov = 0, opw = 0;
            for (const ra2engine::Building& b : world_.buildings()) {
                ov += b.alive && b.house == local_house_ && b.w5.overpowered;
                opw += b.alive && b.house == local_house_ ? (int)b.w5.overpowerers.size() : 0;
            }
            p.set("overpowered", static_cast<double>(ov));
            p.set("overpowerers", static_cast<double>(opw));
        }
        for (const ra2engine::Unit& u : world_.units()) {
            if (!u.alive || u.die_time >= 0.0) continue;
            if (u.garrison_of >= 0) p.add("in_garrison(" + std::to_string(u.house) + ")", 1);
            const int bo = world_.bridge_overlay_at(u.cell_x, u.cell_y);
            if (u.cargo_of < 0 && ra2engine::is_low_bridge_overlay(bo) && !ra2engine::low_bridge_destroyed(bo))
                p.add("on_bridge(" + std::to_string(u.house) + ")", 1);
        }
        {
            int cells = 0;
            for (int y = 0; y < world_.grid_h(); ++y)
                for (int x = 0; x < world_.grid_w(); ++x)
                    if (world_.bridge_overlay_at(x, y) >= 0) ++cells;
            p.set("bridge_cells", cells);
            p.set("bridge_destroyed", world_.bridge_cells_destroyed());
            if (world_.bridges_enabled())
                for (const char* nm : {"bridge", "bridge_near", "bridge_far", "hut"}) {
                    double px = -1, py = -1;
                    if (play_find_point(nm, px, py)) {
                        p.set(std::string("pt_") + nm + "_x", px);
                        p.set(std::string("pt_") + nm + "_y", py);
                        // Chebyshev distance from the first selected unit to the point.
                        for (const ra2engine::Unit& u : world_.units())
                            if (usel(u) && u.alive && u.die_time < 0.0) {
                                p.set(std::string("sel_dist_") + nm,
                                      std::max(std::abs(u.cell_x - px), std::abs(u.cell_y - py)));
                                break;
                            }
                    }
                }
            p.set("bridge_version", world_.bridge_version());
            p.set("depth_armed", world_.depth_rules().armed ? 1 : 0);
        }
        w2_play_probe(p);   // wave-2 air/naval/SW keys (w2air_play.inc)
        w5_play_probe(p);   // W5-B combat/movement keys (w5_play.inc)
        r210_play_probe(p);   // R-210 bridges and cliffs (r210_play.inc)
        {   // P4: sel_near(h,TYPE) = cells from the selection's centre to the
            // nearest live unit of that house/type (only while units are selected)
            double nx = 0, ny = 0;
            if (play_sel_centre(nx, ny)) {
                for (const ra2engine::Unit& u : world_.units()) {
                    if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.house == local_house_) continue;
                    const std::string k = "sel_near(" + std::to_string(u.house) + "," + play_unit_id(u) + ")";
                    const double d = std::floor(std::hypot(u.fx - nx, u.fy - ny));
                    auto it = p.n.find(k);
                    if (it == p.n.end() || d < it->second) p.set(k, d);
                }
            }
        }
        p.find_point = [this](const std::string& name, double& cx, double& cy) {
            return play_find_point(name, cx, cy);
        };
        auto place_near = [this](int ax, int ay, int& cx, int& cy) {
            if (!input_.placement_active()) return false;
            const int type = input_.placement_type();
            if (type < 0 || type >= static_cast<int>(ba_.roster.size())) return false;
            const ra2engine::BuildingTypeDef& d = ba_.roster[type];
            if (ax < 0) {   // default anchor: the player's first live building
                for (const ra2engine::Building& b : world_.buildings())
                    if (b.alive && b.house == local_house_ && b.state != ra2engine::Building::State::Dead) {
                        ax = b.cx + b.fw / 2;
                        ay = b.cy + b.fh / 2;
                        break;
                    }
            }
            if (ax < 0) return false;
            if (type != play_place_type_) { play_place_type_ = type; play_place_tried_.clear(); }
            // On a retry (an earlier click for this placement was refused) only
            // flat candidates: the pointer's level-aware inversion is exact there.
            const bool retry = !play_place_tried_.empty();
            auto flat = [this, &d](int ox, int oy) {
                const ra2assets::MapCell* c0 = world_.map().at(ox, oy);
                if (!c0) return false;
                for (int y = oy - 1; y <= oy + d.fh; ++y)
                    for (int x = ox - 1; x <= ox + d.fw; ++x) {
                        const ra2assets::MapCell* c = world_.map().at(x, y);
                        if (!c || c->level != c0->level) return false;
                    }
                return true;
            };
            for (int r = 1; r <= 24; ++r)
                for (int oy = ay - r; oy <= ay + r; ++oy)
                    for (int ox = ax - r; ox <= ax + r; ++ox) {
                        if (std::max(std::abs(ox - ax), std::abs(oy - ay)) != r) continue;
                        if (!placement_ok(type, ox, oy)) continue;   // the confirm's own test
                        // W4-MC-04: a human places where the view can show it --
                        // not on the last rows at the LocalSize edge.
                        if (!on_board(ox - 2, oy - 2) || !on_board(ox + d.fw + 1, oy + d.fh + 1)) continue;
                        if (retry && !flat(ox, oy)) continue;
                        if (std::find(play_place_tried_.begin(), play_place_tried_.end(),
                                      std::make_pair(ox, oy)) != play_place_tried_.end())
                            continue;
                        play_place_tried_.emplace_back(ox, oy);
                        cx = ox + d.fw / 2;
                        cy = oy + d.fh / 2;
                        // P1: the click must resolve back to this cell (the
                        // level-aware resolver can land one cell off on raised
                        // ground, putting a 4x4 Chronosphere on the factory).
                        int wx = 0, wy = 0, rx = -1, ry = -1;
                        cell_to_window(cx, cy, wx, wy);
                        input_.resolve_ground_cell(wx, wy, rx, ry);
                        if (rx != cx || ry != cy) continue;
                        return true;
                    }
            return false;
        };
        p.auto_place_near = place_near;
        p.auto_place = [place_near](int& cx, int& cy) { return place_near(-1, -1, cx, cy); };
        p.cheat = [this](const std::vector<std::string>& a) -> std::string {
            const std::string c = a.empty() ? "" : a[0];
            if (c == "kill_house" && a.size() >= 2) {
                const int h = std::atoi(a[1].c_str());
                for (int i = 0; i < static_cast<int>(world_.units().size()); ++i) {
                    const ra2engine::Unit& u = world_.units()[i];
                    if (u.alive && u.die_time < 0.0 && u.house == h) world_.campaign_kill_unit(i);
                }
                for (int i = 0; i < static_cast<int>(world_.buildings().size()); ++i) {
                    const ra2engine::Building& b = world_.buildings()[i];
                    if (b.alive && b.house == h && b.state != ra2engine::Building::State::Dead)
                        world_.campaign_damage_building(i, b.hp + 1);
                }
                return "";
            }
            if (c == "damage_building" && a.size() >= 3) {
                const int bi = play_find_building(a[1]);
                if (bi < 0) return "no building " + a[1];
                world_.campaign_damage_building(bi, std::atoi(a[2].c_str()));
                return "";
            }
            if (c == "bridge_hit" && a.size() >= 4) {   // G16 debug: one explosion roll
                const int bx = std::atoi(a[1].c_str()), by = std::atoi(a[2].c_str());
                world_.bridge_explosion(bx, by, std::atoi(a[3].c_str()), true,
                                        a.size() >= 5 ? std::atoi(a[4].c_str()) : world_.deck_z(bx, by));
                return "";
            }
            if (c == "storm_bolt" && a.size() >= 3) {   // W8-06-04: one Lightning Storm bolt (trigger 90)
                const int h = a.size() >= 4 ? std::atoi(a[3].c_str()) : local_house_;
                return world_.trigger_lightning_strike(h, std::atoi(a[1].c_str()), std::atoi(a[2].c_str()))
                           ? "" : "storm_bolt off the map";
            }
            if (c == "infkill" && a.size() >= 3) {   // W3-B: infkill H:TYPE[:n] INFDEATH (death-kind check)
                int h = 0, n = 0;
                std::string type;
                if (!play_parse_spec(a[1], h, type, n)) return "bad spec " + a[1];
                for (ra2engine::Unit& u : world_.units()) {
                    if (!u.alive || u.die_time >= 0.0 || u.house != h || play_unit_id(u) != type) continue;
                    if (n-- > 0) continue;
                    u.hp = 0;
                    u.die_time = 0.0;
                    u.ia.death = std::atoi(a[2].c_str());
                    u.target = -1;
                    u.path.clear();
                    u.path_idx = 0;
                    return "";
                }
                return "no unit " + a[1];
            }
            if (c == "cheer" && a.size() >= 2) {   // W3-B: cheer H (MakeHouseCheer)
                world_.make_house_cheer(std::atoi(a[1].c_str()));
                return "";
            }
            if (c == "credits" && a.size() >= 2) {
                world_.set_wallet(local_house_, lcredits() + std::atof(a[1].c_str()));
                return "";
            }
            // W2-SK probes: damage_unit H:TYPE[:n] DMG ; place TYPE H building H:T +DX +DY
            if (c == "damage_unit" && a.size() >= 3) {
                int h = 0, n = 0;
                std::string type;
                if (!play_parse_spec(a[1], h, type, n)) return "bad spec " + a[1];
                for (int i = 0; i < static_cast<int>(world_.units().size()); ++i) {
                    ra2engine::Unit& u = world_.units()[i];
                    if (!u.alive || u.die_time >= 0.0 || u.house != h) continue;
                    if (type != "*" && play_unit_id(u) != type) continue;
                    if (n-- > 0) continue;
                    u.hp = std::max(1, u.hp - std::atoi(a[2].c_str()));
                    return "";
                }
                return "no unit " + a[1];
            }
            if (c == "place" && a.size() >= 7 && a[3] == "building") {
                int t = roster_index(ba_, a[1]);
                if (t < 0 && depth_world_building(rules_, a[1])) {   // wave-5: tech/civilian types
                    t = depth_register_building(ba_, ra2_, rules_, art_, a[1]);
                    world_.refresh_building_types(ba_.roster);
                }
                if (t < 0) return "no building type " + a[1];
                const int bi = play_find_building(a[4]);
                if (bi < 0) return "no building " + a[4];
                const ra2engine::Building& ab = world_.buildings()[bi];
                const ra2engine::BuildingTypeDef& d = ba_.roster[static_cast<std::size_t>(t)];
                const int x = ab.cx + std::atoi(a[5].c_str()), y = ab.cy + std::atoi(a[6].c_str());
                const int idx = world_.add_building(t, x, y, d.fw, d.fh, std::atoi(a[2].c_str()), d.strength,
                                                    d.armor_ix, 0.0, /*instant=*/true, d.sight,
                                                    d.grants_map_reveal);
                return idx >= 0 ? "" : "place failed";
            }
            // spawn TYPE H (cell X Y | building H:T[:n] +DX +DY)
            if (c == "spawn" && a.size() >= 5) {
                const ra2engine::UnitTypeDef* def = nullptr;
                for (const UnitRoster* r : {&ur_, &ur_ai_})
                    for (const ra2engine::UnitTypeDef& d : r->defs)
                        if (!def && d.id == a[1]) def = &d;
                // Not in either roster: resolve straight from rules (as the crews are).
                ra2engine::UnitTypeDef tmp;
                if (!def) {
                    tmp.id = a[1];
                    tmp.combat = resolve_combat_unit(rules_, a[1]).combat;
                    if (tmp.combat.image.empty()) return "unknown unit type " + a[1];
                    def = &tmp;
                }
                const int h = std::atoi(a[2].c_str());
                int x = 0, y = 0;
                if (a[3] == "cell") {
                    x = std::atoi(a[4].c_str());
                    y = a.size() > 5 ? std::atoi(a[5].c_str()) : 0;
                } else if (a[3] == "building" && a.size() >= 7) {
                    const int bi = play_find_building(a[4]);
                    if (bi < 0) return "no building " + a[4];
                    const ra2engine::Building& b = world_.buildings()[bi];
                    x = b.cx + std::atoi(a[5].c_str());
                    y = b.cy + std::atoi(a[6].c_str());
                } else if (a[3] == "point" && a.size() >= 7) {   // G16: named map point
                    double px = 0, py = 0;
                    if (!play_find_point(a[4], px, py)) return "no point " + a[4];
                    x = static_cast<int>(px) + std::atoi(a[5].c_str());
                    y = static_cast<int>(py) + std::atoi(a[6].c_str());
                } else {
                    return "spawn TYPE H cell X Y | spawn TYPE H building H:T[:n] +DX +DY | spawn TYPE H point NAME +DX +DY";
                }
                world_.add_combat_unit(x, y, h, def->combat);
                world_.set_anim(def->combat.image, ua_.anims.count(def->combat.image)
                                                       ? ua_.anims.at(def->combat.image)
                                                       : ra2engine::default_anim());
                return "";
            }
            {   // wave-2 air/naval/SW cheats (w2air_play.inc)
                std::string r;
                if (w2_cheat(a, r)) return r;
            }
            // A campaign verb that did nothing is not "unknown" (m-all04: camp_deploy
            // while the MCV was still inside its landing craft).
            if (!a.empty() && a[0].rfind("camp_", 0) == 0)
                return a[0] + ": no effect (campaign only; no matching object -- camp_deploy needs a "
                              "controllable MCV on the map, not inside a transport)";
            return "unknown cheat (have: kill_house H, damage_building H:TYPE[:n] DMG, credits N, "
                   "spawn TYPE H cell X Y | spawn TYPE H building H:T[:n] +DX +DY)";
        };
    }

// Agent B (controls/UI) GameScene members, #included INSIDE the GameScene class
// body (public section) to keep main.cpp edits to single include/call lines.

    // NET L1: is this unit (a reference into world_.units()) in the local
    // player's selection? The selection is Input's UI state.
    bool usel(const ra2engine::Unit& u) const {
        return input_.selection().has(static_cast<int>(&u - world_.units().data()));
    }

    // G-08: the cursor STATE under the pointer (what a left click would do).
    // Agent E draws the cursor art from this; over the sidebar it is None.
    ra2engine::Input::Action cursor_action() const {
        if (sidebar_up_ && lay_.contains(mx_, my_)) return ra2engine::Input::Action::None;
        return input_.action_at(int(mx_/world_zoom_),int(my_/world_zoom_));
    }

    // G-10: selection bracket + health bar over the selected own building.
    // [RE-FALLBACK: the original's building health pips (pips.shp frames) are
    // agent E's art; this draws the same information with plain rects.]
    // Colour thresholds = [General] ConditionYellow / ConditionRed.
    mutable ra2engine::TextureCache* pip_tc_ = nullptr;   // W2-UI-11 (set by render)
    void draw_psychic_sensors(SDL_Renderer* ren) const {
        std::map<int,ra2assets::Rgba> intent_colors;
        auto point=[&](double x,double y) {
            return SDL_Point{static_cast<int>(std::lround((x-y)*30))+cam_.x+30,
                static_cast<int>(std::lround((x+y)*15-world_.cell_level(static_cast<int>(x),static_cast<int>(y))*15))+cam_.y+15};
        };
        SDL_Rect clip{0,0,playfield_w_,kWinH};SDL_RenderSetClipRect(ren,&clip);
        for(int bi=0;bi<static_cast<int>(world_.buildings().size());++bi) {
            if(!ra2engine::psychic_sensor_active(world_,bi,local_house_)) continue;
            const auto& b=world_.buildings()[bi];
            if(input_.selected_building()==bi) {
                const double radius=world_.btypes()[b.type].w5.psychic_radius;
                SDL_SetRenderDrawColor(ren,215,155,255,230);
                SDL_Point prev=point(b.cx+(b.fw-1)*0.5+radius,b.cy+(b.fh-1)*0.5);
                for(int k=1;k<=64;++k) {
                    const double a=k*6.283185307179586/64;
                    const auto next=point(b.cx+(b.fw-1)*0.5+radius*std::cos(a),b.cy+(b.fh-1)*0.5+radius*std::sin(a));
                    // Mobile accessibility styling: preserve the true radius,
                    // with readable dashes instead of a solid boundary.
                    if((k-1)%4<2) {
                        SDL_RenderDrawLine(ren,prev.x,prev.y,next.x,next.y);
                        SDL_RenderDrawLine(ren,prev.x,prev.y+1,next.x,next.y+1);
                    }
                    prev=next;
                }
            }
            for(int ui=0;ui<static_cast<int>(world_.units().size());++ui) {
                int x=0,y=0;if(!ra2engine::psychic_sensor_target(world_,bi,ui,x,y)) continue;
                const auto& u=world_.units()[ui];
                const auto from=point(u.fx,u.fy),to=point(x,y);
                auto c=intent_colors.find(u.house);
                if(c==intent_colors.end())c=intent_colors.emplace(u.house,
                    ra2engine::psychic_intent_color(world_,ua_.pal,u.house)).first;
                const auto color=c->second;
                // One geometry batch per intent: black outline plus owner-
                // coloured dash cores, without a draw call for every dash.
                std::vector<SDL_Vertex> vertices;std::vector<int> indices;
                auto quad=[&](const std::array<double,4>& d,double width,SDL_Color c) {
                    const double length=std::hypot(d[2]-d[0],d[3]-d[1]);if(length<0.001)return;
                    const float nx=float(-(d[3]-d[1])/length*width/2),ny=float((d[2]-d[0])/length*width/2);
                    const int first=int(vertices.size());
                    vertices.push_back({{float(d[0])+nx,float(d[1])+ny},c,{0,0}});
                    vertices.push_back({{float(d[2])+nx,float(d[3])+ny},c,{0,0}});
                    vertices.push_back({{float(d[2])-nx,float(d[3])-ny},c,{0,0}});
                    vertices.push_back({{float(d[0])-nx,float(d[1])-ny},c,{0,0}});
                    for(int i:{0,1,2,0,2,3})indices.push_back(first+i);
                };
                for(const auto& dash:ra2engine::psychic_intent_dashes(from.x,from.y,to.x,to.y)) {
                    quad(dash,3.5,SDL_Color{0,0,0,240});quad(dash,1.5,SDL_Color{color.r,color.g,color.b,255});
                }
                if(!vertices.empty())SDL_RenderGeometry(ren,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
                SDL_SetRenderDrawColor(ren,color.r,color.g,color.b,255);
                SDL_RenderDrawLine(ren,to.x-4,to.y,to.x+4,to.y);
                SDL_RenderDrawLine(ren,to.x,to.y-4,to.x,to.y+4);
            }
        }
        SDL_RenderSetClipRect(ren,nullptr);
    }
    void draw_selected_building(SDL_Renderer* ren) const {
        const int sb = input_.selected_building();
        if (sb < 0 || sb >= static_cast<int>(world_.buildings().size())) return;
        const ra2engine::Building& b = world_.buildings()[sb];
        if (!b.alive || b.state == ra2engine::Building::State::Dead) return;
        // Footprint corner pixels (top, left, right, bottom of the diamond).
        auto px = [&](int cx, int cy, int& x, int& y) {
            const ra2assets::MapCell* c = world_.map().at(cx, cy);
            const int level = c ? c->level : 0;
            x = (cx - cy) * 30 + 30 + cam_.x;
            y = (cx + cy) * 15 - level * 15 + cam_.y;
        };
        int tx, ty, lx, ly, rx, ry, bx, by;
        px(b.cx, b.cy, tx, ty);
        px(b.cx, b.cy + b.fh, lx, ly);
        px(b.cx + b.fw, b.cy, rx, ry);
        px(b.cx + b.fw, b.cy + b.fh, bx, by);
        if (g_pips) {   // W2-UI-11: the original's diagonal PIPS line (hud_pips.inc)
            const std::string& id = ba_.roster[(std::size_t)b.type].id;
            const std::string h = pres_clean(art_ini_.get(ba_.roster[(std::size_t)b.type].image.empty()
                                                              ? id : ba_.roster[(std::size_t)b.type].image, "Height"));
            // W6-05-09 / R-145 [RE: BuildingTypeClass ctor FUN_00457AC0 @457C2A sets
            // Height (+0xC7C) = 2; the art reader @45A965 keeps it when the key
            // is absent].
            const int height = h.empty() ? 2 : std::atoi(h.c_str());
            draw_building_pips(ren, *pip_tc_, b.hp, b.max_hp, tx, ty, b.fh, height * 15, g_pip_yellow, g_pip_red);
            return;
        }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 230);
        const int k = 10;   // bracket arm length
        SDL_RenderDrawLine(ren, tx - k, ty + k / 2, tx, ty);
        SDL_RenderDrawLine(ren, tx, ty, tx + k, ty + k / 2);
        SDL_RenderDrawLine(ren, bx - k, by - k / 2, bx, by);
        SDL_RenderDrawLine(ren, bx, by, bx + k, by - k / 2);
        SDL_RenderDrawLine(ren, lx, ly - k / 2, lx, ly + k / 2);
        SDL_RenderDrawLine(ren, rx, ry - k / 2, rx, ry + k / 2);
        const double frac = b.max_hp > 0 ? static_cast<double>(b.hp) / b.max_hp : 0.0;
        const double red = rules_.general_double("ConditionRed", 25.0) / 100.0;
        const int w = std::max(24, (rx - lx) / 2);
        SDL_Rect back{tx - w / 2, ty - 14, w, 5};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
        SDL_RenderFillRect(ren, &back);
        if (frac > ba_.condition_yellow) SDL_SetRenderDrawColor(ren, 40, 220, 40, 255);
        else if (frac > red) SDL_SetRenderDrawColor(ren, 230, 220, 40, 255);
        else SDL_SetRenderDrawColor(ren, 230, 40, 40, 255);
        SDL_Rect fill{back.x + 1, back.y + 1, static_cast<int>((w - 2) * frac), 3};
        SDL_RenderFillRect(ren, &fill);
    }

    // --- G-04 / W2-UI-27: Keyboard.ini hotkeys --------------------------------
    // RA2 game.exe reads KEYBOARD.INI (FUN_00519240, string 0x7DD718) -- the
    // install's Keyboard.ini overrides the shipped defaults. KeyboardMD.ini is
    // the YR file; it is only a fallback when Keyboard.ini is absent.
    // W5-E: the Keyboard page (0xA3) writes the player's remap to
    // <pref dir>/Keyboard.ini, which overrides the install's file.
    static ra2engine::HotkeyTable load_hotkeys() { return load_hotkey_table(); }
    ra2engine::HotkeyTable hotkeys_ = load_hotkeys();
    int hotkeys_ver_ = g_hotkeys_version;
    int view_cam_[4][2] = {};          // View1..4 bookmarks (SetView = Ctrl+F1..F4)
    bool view_set_[4] = {false, false, false, false};

    void switch_tab(int tab) {
        if (tab < 0 || tab > 3 || tab == active_tab_) return;
        active_tab_ = tab;
        buildable_ = tab_buildable(active_tab_);
        lay_ = ra2engine::SidebarLayout::compute(kWinW, kWinH, buildable_);
    }

    // CenterBase [RE: FUN_00730670]: the player's ConYard (the primary one if
    // flagged), else any own building, else the first own unit.
    void center_base() {
        int pick = -1;
        for (int i = 0; i < static_cast<int>(world_.buildings().size()); ++i) {
            const ra2engine::Building& b = world_.buildings()[i];
            if (!b.alive || b.house != local_house_ || b.state == ra2engine::Building::State::Dead) continue;
            const bool cy = b.type >= 0 && b.type < static_cast<int>(ba_.roster.size()) &&
                            ba_.roster[b.type].conyard;
            if (cy && (pick < 0 || b.primary || !ba_.roster[world_.buildings()[pick].type].conyard)) {
                pick = i;
                if (b.primary) break;
            } else if (pick < 0) {
                pick = i;
            }
        }
        if (pick >= 0) {
            const ra2engine::Building& b = world_.buildings()[pick];
            input_.center_on_cell(b.cx + b.fw / 2.0 - 0.5, b.cy + b.fh / 2.0 - 0.5);
            return;
        }
        for (const ra2engine::Unit& u : world_.units())
            if (world_.controllable(u.house) && u.alive && u.die_time < 0.0 && u.cargo_of < 0) {
                input_.center_on_cell(u.fx, u.fy);
                return;
            }
    }

    // DeployObject (D): unload selected transports + toggle deployers (the
    // former raw-scancode path, now a real key event).
    void deploy_selected() {
        // G16 (agent G) [RE: world-depth.md §A5]: Deploy on a selected own
        // garrison = Mission_Unload -> UnloadOccupants (FUN_0044D880).
        // NET L1 [RE: DeployObject queues EVENT_DEPLOY per object]: ONE DEPLOY
        // command -- Whom = the garrison (if any) + the selected units; its
        // apply unloads the garrison, then the transports, then deploys.
        input_.selection().prune(world_);
        ra2engine::Command c;
        c.type = ra2engine::EventType::Deploy;
        const int sb = input_.selected_building();
        if (sb >= 0 && sb < static_cast<int>(world_.buildings().size()) &&
            world_.buildings()[sb].house == local_house_ &&
            (!world_.buildings()[sb].occupants.empty() || world_.can_pack_slave_miner(sb)))
            c.whom.push_back(ra2engine::Target::building(sb));
        int first = -1;
        for (int i : input_.selection().ids()) {
            if (i >= static_cast<int>(world_.units().size())) break;
            c.whom.push_back(ra2engine::Target::unit(world_, i));
            if (first < 0 && world_.controllable(world_.units()[i].house)) first = i;
        }
        if (!c.whom.empty()) issue(std::move(c));
        pres_.note_deploy_order(world_, first);   // W5-E Mission_Unload -> VoiceDeploy
    }

    // Next/PreviousObject [RE-FALLBACK: FUN_005362D0/536740 execs not
    // transcribed]: cycle the selection through own alive units and centre.
    void cycle_object(int dir) {
        const auto& us = world_.units();
        const int n = static_cast<int>(us.size());
        if (n == 0) return;
        const int start = input_.selection().lowest();
        for (int k = 1; k <= n; ++k) {
            const int i = ((start < 0 ? (dir > 0 ? -1 : 0) : start) + dir * k + n * 4) % n;
            const ra2engine::Unit& u = us[i];
            if (!world_.controllable(u.house) || !u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            input_.selection().clear();
            input_.selection().set(i);
            input_.center_on_selection();
            return;
        }
    }

    // Returns true when the key was a bound command (consumed).
    bool hotkey(const ra2engine::InputEvent& ev) {
        if (ev.kind != ra2engine::InputEvent::Key || ev.vk == 0) return false;
        if (hotkeys_ver_ != g_hotkeys_version) {   // W5-E: the Keyboard page saved a remap
            hotkeys_ = load_hotkeys();
            hotkeys_ver_ = g_hotkeys_version;
        }
        const ra2engine::HotBinding b = hotkeys_.lookup(ev.vk, ev.shift, ev.ctrl, ev.alt);
        using C = ra2engine::HotCmd;
        switch (b.cmd) {
            case C::None: return false;
            case C::Options: menu_.open(); return true;
            case C::PlanningMode: input_.toggle_planning(); return true;
            case C::StopObject: input_.stop_selected(); return true;
            case C::GuardObject: input_.guard_selected(); return true;
            case C::ScatterObject: input_.scatter_selected(); return true;
            case C::DeployObject: deploy_selected(); return true;
            case C::TypeSelect: input_.type_select(); return true;
            case C::TeamCreate: input_.group_create(b.arg); return true;
            case C::TeamSelect: input_.group_select(b.arg, ev.t_ms); return true;
            case C::TeamAddSelect: input_.group_add_select(b.arg); return true;
            case C::TeamCenter: input_.group_center(b.arg); return true;
            case C::CenterView: input_.center_on_selection(); return true;
            case C::CenterBase: center_base(); return true;
            case C::ToggleRepair: if (ba_.has_repair_sell) input_.set_repair_mode(-1); return true;
            case C::ToggleSell: if (ba_.has_repair_sell) input_.set_sell_mode(-1); return true;
            case C::StructureTab: switch_tab(0); return true;
            case C::DefenseTab: switch_tab(1); return true;
            case C::InfantryTab: switch_tab(2); return true;
            case C::UnitTab: switch_tab(3); return true;
            case C::NextObject: cycle_object(+1); return true;
            case C::PreviousObject: cycle_object(-1); return true;
            case C::SetView:
                view_cam_[b.arg - 1][0] = cam_.x;
                view_cam_[b.arg - 1][1] = cam_.y;
                view_set_[b.arg - 1] = true;
                return true;
            case C::View:
                if (view_set_[b.arg - 1]) {
                    cam_.x = view_cam_[b.arg - 1][0];
                    cam_.y = view_cam_[b.arg - 1][1];
                }
                return true;
            case C::SidebarUp: strip_scroll(-1); return true;
            case C::SidebarDown: strip_scroll(+1); return true;
            // W5-E [RE: the CommandClass execs, see hotkey_follow & co. below].
            case C::Follow: hotkey_follow(); return true;
            case C::CombatantSelect: hotkey_combatant_select(); return true;
            case C::HealthNav: hotkey_nav(false); return true;
            case C::VeterancyNav: hotkey_nav(true); return true;
            case C::AllToCheer: hotkey_cheer(); return true;
            case C::CenterOnRadarEvent: hotkey_radar_event(ev.t_ms); return true;
            case C::ScreenCapture: screencap_pending_ = true; return true;
            // W9-05-03 ToggleAlliance [RE: 0x536C50 -> FUN_007309C0] / W9-05-08
            // PlaceBeacon [RE: 0x536DD0 -> FUN_00730B00] / Delete [RE:
            // FUN_00730AE0 -> FUN_00431390]: a network game only.
            case C::ToggleAlliance: hotkey_toggle_alliance(); return true;
            case C::PlaceBeacon: hotkey_place_beacon(); return true;
            case C::Delete: hotkey_delete_beacon(); return true;
            default:
                // W5-E [RE]: PlaceBeacon FUN_00536DD0 and ToggleAlliance /
                // PageUser FUN_00537B80 / Taunts act only in a network game
                // (GameMode != 0 && != 5 skirmish); Delete FUN_00730AE0
                // deletes a planning path / beacon; PlanningMode FUN_00730B20
                // [not implemented: the waypoint planning mode]. In a
                // campaign or skirmish they are consumed with no effect.
                return true;
        }
    }

    // --- W5-E hotkey bodies ---------------------------------------------------
    // Follow [RE: FUN_00537740 -> FUN_004AEA40]: with a selection and no
    // follow running, the tactical view follows the first selected object;
    // otherwise following stops.
    int follow_unit_ = -1;
    void hotkey_follow() {
        if (follow_unit_ >= 0) { follow_unit_ = -1; return; }
        const int first = input_.selection().lowest();
        if (first >= 0 && first < static_cast<int>(world_.units().size())) follow_unit_ = first;
    }
    void follow_tick() {
        if (follow_unit_ < 0) return;
        if (follow_unit_ >= static_cast<int>(world_.units().size())) { follow_unit_ = -1; return; }
        const ra2engine::Unit& u = world_.units()[static_cast<std::size_t>(follow_unit_)];
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) { follow_unit_ = -1; return; }
        input_.center_on_cell(u.fx, u.fy);
    }
    bool unit_on_screen(const ra2engine::Unit& u) const {
        const double sx = (u.fx - u.fy) * 30.0 + cam_.x + 30.0;
        const double sy = (u.fx + u.fy) * 15.0 + cam_.y + 15.0 - u.level * 15.0;
        return sx >= 0 && sx < playfield_w_ && sy >= 0 && sy < kWinH;
    }
    bool own_live(const ra2engine::Unit& u) const {
        return world_.controllable(u.house) && u.alive && u.die_time < 0.0 && u.cargo_of < 0;
    }
    // CombatantSelect [RE: FUN_00731350]: every own armed (combatant) unit on
    // screen; a second press when they are all already selected widens to the
    // whole map (DAT_00B0EDC4).
    void hotkey_combatant_select() {
        // W7-01-13 / W7-02-07 [RE: FUN_00731690 / 0x7316A7]: the predicate is
        // the type's IsSelectableCombatant (+0xDBC) -- Engineers, Spies and
        // Terrorists say no -- and an armed unit (vtbl+0x13C).
        auto combatant = [&](const ra2engine::Unit& u) {
            if (u.tg.t.rules_id.empty()) return own_live(u) && u.w_damage > 0 && !u.is_harvester;
            return own_live(u) && u.w7.t.selectable_combatant && u.w_damage > 0;
        };
        bool all_sel = true, any = false;
        for (const ra2engine::Unit& u : world_.units())
            if (combatant(u) && unit_on_screen(u)) { any = true; if (!usel(u)) all_sel = false; }
        const bool whole_map = any && all_sel;
        input_.selection().clear();
        for (const ra2engine::Unit& u : world_.units())
            if (combatant(u) && (whole_map || unit_on_screen(u))) input_.selection().set(static_cast<int>(&u - world_.units().data()));
        last_nav_ = 0;
    }
    // HealthNav / VeterancyNav [RE: FUN_00732450 / FUN_00732790]: repeated
    // presses step through the categories (health green / yellow / red;
    // rookie / veteran / elite) and select the own objects on screen in that
    // category (the whole map when none are on screen). Any other command
    // restarts at the first category.
    int last_nav_ = 0, nav_step_ = -1;   // 1 health, 2 veterancy
    void hotkey_nav(bool vet) {
        const int kind = vet ? 2 : 1;
        if (last_nav_ != kind) nav_step_ = -1;
        last_nav_ = kind;
        auto cat_of = [&](const ra2engine::Unit& u) {
            if (vet) return std::min(2, std::max(0, u.veteran_rank));
            const double r = u.max_hp > 0 ? static_cast<double>(u.hp) / u.max_hp : 1.0;
            return r > ba_.condition_yellow ? 0 : (r > ba_.condition_yellow * 0.5 ? 1 : 2);
        };
        for (int tries = 0; tries < 3; ++tries) {
            nav_step_ = (nav_step_ + 1) % 3;
            bool on_screen = false;
            for (const ra2engine::Unit& u : world_.units())
                if (own_live(u) && cat_of(u) == nav_step_ && unit_on_screen(u)) on_screen = true;
            bool found = false;
            for (const ra2engine::Unit& u : world_.units())
                if (own_live(u) && cat_of(u) == nav_step_ && (!on_screen || unit_on_screen(u))) found = true;
            if (!found) continue;
            input_.selection().clear();
            for (const ra2engine::Unit& u : world_.units())
                if (own_live(u) && cat_of(u) == nav_step_ && (!on_screen || unit_on_screen(u)))
                    input_.selection().set(static_cast<int>(&u - world_.units().data()));
            return;
        }
    }
    // AllToCheer [RE: FUN_00730000 queues event 0x2D, throttled by Rules
    // +0x580 frames; handler FUN_0050C6E0]: every own infantry cheers, then
    // CheerSound. [RE-FALLBACK: the +0x580 throttle value -- 30 frames.]
    long cheer_next_ = 0;
    void hotkey_cheer() {
        last_nav_ = 0;
        const long now = static_cast<long>(g_pres_tick);
        if (now < cheer_next_) return;
        cheer_next_ = now + 30;
        issue(ra2engine::Command{ra2engine::EventType::AllCheer});   // EVENT_ALLCHEER 0x2D
        pres_.note_cheer();
    }
    // CenterOnRadarEvent [RE: FUN_007331F0]: a press within 1600 ms of the
    // previous one steps to the next older live radar event, otherwise the
    // newest; the view centres on its cell.
    std::uint32_t radar_ev_ms_ = 0;
    int radar_ev_ix_ = -1;
    void hotkey_radar_event(std::uint32_t t_ms) {
        last_nav_ = 0;
        const auto ev = pres_.radar_pings();
        if (ev.empty()) return;
        const bool next = radar_ev_ix_ >= 0 && t_ms - radar_ev_ms_ < 1600u;
        radar_ev_ms_ = t_ms;
        radar_ev_ix_ = next ? radar_ev_ix_ + 1 : 0;
        if (radar_ev_ix_ >= static_cast<int>(ev.size())) radar_ev_ix_ = 0;
        const auto& e = ev[ev.size() - 1 - static_cast<std::size_t>(radar_ev_ix_)];
        input_.center_on_cell(e.x, e.y);
    }
    // ScreenCapture [RE: FUN_005378F0 writes SCRN%04d.pcx, the first free
    // number]: the frame after the key, as <pref dir>/SCRN%04d.png
    // [deviation: PNG, in the writable pref dir].
    bool screencap_pending_ = false;
    std::string last_screencap_;
    void screencap_tick(SDL_Renderer* ren) {
        if (!screencap_pending_) return;
        screencap_pending_ = false;
        const std::string dir = pref_dir_path();
        if (dir.empty()) return;
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren, &w, &h);
        std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
        if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4) != 0) return;
        for (int n = 0; n < 10000; ++n) {
            char b[32];
            std::snprintf(b, sizeof b, "SCRN%04d.png", n);
            const std::string p = dir + b;
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) continue;
            if (stbi_write_png(p.c_str(), w, h, 4, px.data(), w * 4)) last_screencap_ = p;
            return;
        }
    }

    // --- G-12: sidebar strip scrolling (r-up / r-dn gadgets + SidebarUp/Down) --
    // [RE: SidebarUp/Down exec -> FUN_006A6090(+-1); RE-FALLBACK: one row per
    // press; the strip returns to the top on a tab switch.]
    void strip_scroll(int rows) {
        const int top = std::max(0, std::min(lay_.top_row + rows, lay_.max_top_row(static_cast<int>(buildable_.size()))));
        if (top == lay_.top_row) return;
        lay_ = ra2engine::SidebarLayout::compute(kWinW, kWinH, buildable_, top);
    }
    bool strip_button_click(int x, int y) {
        if (lay_.scroll_up.contains(x, y)) { strip_scroll(-1); return true; }
        if (lay_.scroll_dn.contains(x, y)) { strip_scroll(+1); return true; }
        return false;
    }

    // --- G-13: fixed logic tick ------------------------------------------------
    // 1/15 s = one logic frame of the rules' time base (ROF / build / mission
    // frames are 15-per-engine-second units). RE-T1 pinned (docs/re/
    // gamespeed.md): the per-GameSpeed rate is applied as the accumulator
    // multiplier fps/15 (gamespeed.hpp), NOT by changing kLogicDt. At most
    // kMaxLogicSteps ticks run in one frame [RE-FALLBACK: catch-up cap]; the
    // rest of the backlog is carried to later frames, never dropped (Net L0).
    static constexpr double kLogicDt = 1.0 / 15.0;
    static constexpr int kMaxLogicSteps = 4;
    double logic_acc_ = 0.0;

    // --- G-07: build-queue click semantics ---------------------------------------
    // [RE: gamemd fixB hunt, StripClass cameo action FUN_006AA390; event ids
    // PRODUCE=0xE, SUSPEND=0xF, ABANDON=0x10, ABANDON_ALL=0x2E, PLACE=0xB]
    //  LEFT  (flag 0x01): this type is the current product and it is ON HOLD ->
    //        PRODUCE (resume); it is DONE -> building: start placement; else a
    //        BUILDING while the building factory is busy -> refused; otherwise
    //        PRODUCE = start, or for units queue one more (FactoryClass queue,
    //        capped by [General] MaximumQueuedObjects).
    //  RIGHT (flag 0x10): the current product running -> SUSPEND (hold); held
    //        or done -> ABANDON one (Shift: ABANDON_ALL); a type that is only
    //        queued -> ABANDON one queued copy.
    // L2: the strip decides WHICH event a click means (StripClass, client
    // side) from the local house's queue state in the World; the event itself
    // runs in the sim (World::econ_produce / econ_suspend / econ_abandon --
    // HouseClass::BeginProduction FUN_004FA170, FUN_004FA730, FUN_004FA830),
    // which re-validates it like EventClass::Execute FUN_004C6BD0 does. The
    // backlog and the hold flags are sim state now (HouseEcon, saved).
    int max_queued() const {
        return static_cast<int>(rules_.general_double("MaximumQueuedObjects", 29.0));
    }
    int queued_count(int type, ra2engine::QueueCat c) const {
        return world_.econ_queued_count(local_house_, c, type);
    }
    // NET L1: the click picks the EVENT (the StripClass action's decision);
    // the economy change happens at its apply (game_scene_commands.inc
    // cmd_produce / cmd_suspend / cmd_abandon), so a replay or a lockstep peer
    // runs the same code on the same tick.
    void cameo_click(int type, bool right, bool shift) {
        const auto power=ra2engine::power_cameo_type(type);
        if(power!=ra2engine::SuperweaponType::None) {
            if(!right && active_tab_==1 && world_.sw_ready(local_house_,power)) w2_arm_sw(power);
            return;
        }
        using ET = ra2engine::EventType;
        const int lh = local_house_;
        if (active_tab_ >= 2) {   // ---- units ----
            const ra2engine::QueueCat cat = rp_cat_of_unit(type);   // agent A: own factory queue
            const bool current = lq().producing(cat) == type;
            const bool held = world_.econ_held(lh, cat);
            if (right) {
                if (shift) {   // ABANDON_ALL
                    issue_type_event(ET::AbandonAll, false, type);
                    pres_.note_eva("EVA_Canceled");   // W7-12-01 [RE: FUN_006AA390 @6AA4C4]
                    return;
                }
                if (current && !held && !lq().ready(cat)) {   // SUSPEND
                    issue_type_event(ET::Suspend, false, type);
                    pres_.note_eva("EVA_OnHold");   // W5-E [RE: FUN_006AA390 @6AA692]
                    return;
                }
                issue_type_event(ET::Abandon, false, type);   // a queued copy, else the current
                pres_.note_eva("EVA_Canceled");   // W7-12-01 [RE: FUN_006AA390 @6AA4C4]
                return;
            }
            issue_type_event(ET::Produce, false, type);   // resume / start / queue one more
            return;
        }
        // ---- buildings (one at a time) ----
        const ra2engine::QueueCat bcat =
            (active_tab_ == 1) ? ra2engine::QueueCat::Defense : ra2engine::QueueCat::Structure;
        const ra2engine::ProductionState& bq = lq().state(bcat);
        const bool current = bq.slot.type == type;
        const bool held = world_.econ_held(lh, bcat);
        if (right) {
            if (!current) return;
            if (!held && !bq.slot.ready) {   // SUSPEND
                issue_type_event(ET::Suspend, true, type);
                pres_.note_eva("EVA_OnHold");   // W5-E [RE: FUN_006AA390 @6AA793]
                return;
            }
            issue_type_event(ET::Abandon, true, type);                   // ABANDON
            pres_.note_eva("EVA_Canceled");   // W7-12-01 [RE: FUN_006AA390 @6AA4C4]
            return;
        }
        if (current && bq.slot.ready) {
            place_cat_ = bcat;
            input_.begin_placement(type, ba_.roster[type].fw, ba_.roster[type].fh);
            return;
        }
        if (current && held) { issue_type_event(ET::Produce, true, type); return; }   // resume
        if (current) return;                                // running: no effect
        // W5-E [RE: FUN_006AA390 @6AAA3C]: the building factory is busy with
        // another type -> EVA_UnableToComply.
        if (bq.slot.type >= 0) { pres_.note_eva("EVA_UnableToComply"); return; }
        issue_type_event(ET::Produce, true, type);         // PRODUCE = start (re-checked at apply)
    }
    // W2-UI-03/04 [RE: wave2/04 #2, FUN_0067F800 + FUN_0069D770]: the queue
    // count "%d" right-aligned at (x+60, y+1) -- shown when > 1, or when 1 and
    // the type is queued but is not the current product -- and the CSF status
    // TXT_READY "Ready" / TXT_HOLD "On Hold" centred at (x+30, y+1), moved to
    // (x+2, y+1) left-aligned when a count is shown; game.fnt in the UI colour
    // over a 0xAF black box (hud_text.inc draw_cameo_text).
    void draw_queue_badges(SDL_Renderer* ren, ra2engine::TextureCache& tc) const {
        if (!(ba_.ok && ba_.sidebar_ok)) return;
        for (const ra2engine::CameoCell& cell : lay_.cameos) {
            if (ra2engine::power_cameo_type(cell.type)!=ra2engine::SuperweaponType::None) continue;
            ra2engine::QueueCat cat;
            if (active_tab_ >= 2) cat = rp_cat_of_unit(cell.type);
            else cat = active_tab_ == 1 ? ra2engine::QueueCat::Defense : ra2engine::QueueCat::Structure;
            const int n = queued_count(cell.type, cat);
            const bool current = lq().producing(cat) == cell.type;
            const int shown = (n > 1 || (n == 1 && !current)) ? n : 0;
            std::string status;
            if (current && lq().state(cat).slot.ready) status = g_txt_ready;
            else if (current && world_.econ_held(local_house_, cat)) status = g_txt_hold;
            if (shown || !status.empty()) draw_cameo_text(ren, tc, cell.rect.x, cell.rect.y, status, shown);
        }
    }

    // --- G-09: radar click [RE: RadarClass action; TS lineage RadarClass::Action:
    // a click on the ACTIVE radar with a selection is a move order to that cell,
    // otherwise it re-centres the tactical view; an inactive radar (no powered
    // radar building / shroud off) ignores clicks.] [RE-FALLBACK: the
    // cell <-> radar-pixel mapping is the clone's draw_radar_minimap stretch
    // (grid scaled to the radar rect); if agent E moves the minimap to the
    // original 140x108 iso viewport, radar_cell_at must follow it.]
    bool radar_active() const {
        if (!world_.shroud_enabled() || !house_owns_operational_radar(world_, ba_, local_house_)) return false;
        // The radar takes clicks only once its power-up animation has finished
        // and the map picture is up (agent E's boot frames) [RE: RadarClass
        // activation -- TS lineage IsRadarActive is set at the end of the
        // activation animation].
        if (g_radar && ba_.has_radar && g_radar->boot_frame(ba_.sb_radar.count()) != 0) return false;
        return true;
    }
    bool radar_cell_at(int px, int py, int& cx, int& cy) const {
        // G-09 (E): the live isometric radar owns the pixel<->cell mapping.
        if (g_radar && g_radar->ok()) return g_radar->cell_at(px, py, cx, cy);
        const ra2engine::SbRect& r = lay_.radar;
        if (!r.contains(px, py) || world_.grid_w() <= 0 || world_.grid_h() <= 0) return false;
        const double sx = static_cast<double>(r.w) / world_.grid_w();
        const double sy = static_cast<double>(r.h) / world_.grid_h();
        cx = std::min(world_.grid_w() - 1, static_cast<int>((px - r.x) / sx));
        cy = std::min(world_.grid_h() - 1, static_cast<int>((py - r.y) / sy));
        return true;
    }
    bool radar_click(const ra2engine::InputEvent& ev) {
        if (!lay_.radar.contains(ev.x, ev.y)) return false;
        int cx = 0, cy = 0;
        if (!radar_active() || !radar_cell_at(ev.x, ev.y, cx, cy)) return true;   // consumed, inert
        bool any = false;
        for (const ra2engine::Unit& u : world_.units())
            if (usel(u) && world_.controllable(u.house) && u.alive && u.die_time < 0.0) { any = true; break; }
        // W6-01-08 [engine deviation, touch]: a phone player looks around with
        // the radar, so a finger tap always centres the view; a radar MOVE
        // order by touch needs [MOVE] armed in the command column (one-shot).
        // A mouse keeps the original rule (selection -> move, else centre).
        const bool order = any && (!ev.touch || touch_move_);
        if (ev.touch && touch_move_) touch_move_ = false;
        if (order)   // NET L1: MEGAMISSION(Move) with the selection as Whom
            issue(ra2engine::make_mega(world_, input_.selection().ids(), ra2engine::Mission::Move,
                                       ra2engine::Target{}, ra2engine::Target::cell(cx, cy)));

        else input_.center_on_cell(cx, cy);
        return true;
    }

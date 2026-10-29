// hud_messages.inc -- W2-UI-12/21: the in-game message list, mission timer,
// end banner and F1 briefing, all in the ONE HUD font (hud_text.inc).
// #included INSIDE the GameScene class body (public section).
//
// [RE: wave2/04 W2-UI-21 -- TXT lines, objectives, TXT_LOW_POWER "Low Power -
// Production Slowed", TXT_PLAYER_DEFEATED, TXT_GAME_WAS_SAVED go to the
// top-left message list with a per-line timeout ([General] MessageDelay,
// minutes).]
// [RE-FALLBACK: list origin (8, 8) of the tactical view, at most 6 lines, UI
// colour; the timer sits top-centre as "Time Remaining: mm:ss"
// (GUI:TimeRemaining); the end banner is game.fnt at 2x centred on the
// tactical view -- the original's banner face/scale was not traced.]

    struct HudMsg {
        std::string text;
        double expire = 0.0;   // msg_clock_ seconds
        SDL_Color col{0, 0, 0, 0};   // NET-PLAY: a chat line in the sender's colour (a == 0: UI colour)
    };
    std::vector<HudMsg> msgs_;
    double msg_clock_ = 0.0;
    bool low_power_was_ = false;
    static constexpr std::size_t kMaxMsgs = 6;

    double message_seconds() const {
        const double m = rules_.general_double("MessageDelay", 0.6);   // minutes
        return std::max(4.0, m * 60.0);
    }
    void post_message(const std::string& s, double seconds = -1.0, SDL_Color col = SDL_Color{0, 0, 0, 0}) {
        if (s.empty()) return;
        msgs_.push_back({s, msg_clock_ + (seconds > 0 ? seconds : message_seconds()), col});
        while (msgs_.size() > kMaxMsgs) msgs_.erase(msgs_.begin());
    }
    // The newest live message ("" = none) -- the `banner` probe key.
    std::string newest_message() const {
        for (auto it = msgs_.rbegin(); it != msgs_.rend(); ++it)
            if (it->expire > msg_clock_) return it->text;
        return "";
    }
    void messages_tick(double dt) {
        msg_clock_ += dt;
        msgs_.erase(std::remove_if(msgs_.begin(), msgs_.end(),
                                   [&](const HudMsg& m) { return m.expire <= msg_clock_; }),
                    msgs_.end());
        // TXT_LOW_POWER on the drop into low power (edge).
        const ra2engine::PowerTotals pt = ra2engine::power_totals(world_.buildings(), ba_.roster, local_house_);
        const bool low = pt.drain > pt.output;
        if (low && !low_power_was_) post_message(camp::csf_text(dir_, "TXT_LOW_POWER"));
        low_power_was_ = low;
        post_campaign_texts();
    }
    // W6-05-08 [RE: wave2/04 W2-UI-21 -- TextTrigger lines go to the one
    // message list]: a mission's TextTrigger / objective lines are posted to
    // the same list as every other message, so they share its line cap
    // (kMaxMsgs, oldest dropped) and its [General] MessageDelay timeout,
    // instead of a separate unbounded list (ALL05: 17 stacked lines).
    // Lines older than 20 s when first seen (a loaded game) are not replayed.
    std::size_t campaign_texts_posted_ = 0;
    bool campaign_texts_seen_ = false;
    void post_campaign_texts() {
        if (!campaign_) return;
        const auto& obj = campaign_->objectives();
        if (obj.size() < campaign_texts_posted_) campaign_texts_posted_ = 0;   // list reset (load)
        const long f = campaign_->frame_count();
        for (std::size_t i = campaign_texts_posted_; i < obj.size(); ++i)
            if (campaign_texts_seen_ || f - obj[i].frame <= 300)
                post_message(hud_wrap(obj[i].text, static_cast<int>(std::max(playfield_w_ - 32, 400) / hud_touch_scale())));
        campaign_texts_posted_ = obj.size();
        campaign_texts_seen_ = true;
    }
    void draw_messages(SDL_Renderer* ren, ra2engine::TextureCache& tc) const {
        int y = 8;
        const SDL_Color col = hud_ui_color();
        const double sc = hud_touch_scale();   // W6-01-12: 1.5x on a touch canvas
        for (const HudMsg& m : msgs_) {
            if (m.expire <= msg_clock_) continue;
            hud_text(ren, tc, m.text, 8, y, m.col.a ? m.col : col, kHudLeft, false, true, sc);
            y += static_cast<int>(hud_text_height(m.text) * sc) + 4;
        }
    }
    // W6-05-04/05 [RE: TacticalClass draw FUN_006a45a0 -> FUN_006a5280]: the
    // bottom-right timer stack. Line 0 is the mission timer (when running)
    // with its TimerText label (Scenario+0x7D4, action 103) in the player's
    // colour; then one line per ShowTimer superweapon (the SuperClass list
    // DAT_00a35dbc) "<UIName>  mm:ss" in the OWNER house's colour, skipped
    // while it has not started charging (+0x58 and TimeLeft == RechargeTime).
    // Each line: "%s  %02d:%02d" ("%s  %d:%02d:%02d" from one hour), right-
    // aligned at x = TacticalW - 3, y = TacticalH - (fontH + 2) * (line + 1).
    // At 0:00 a SW line toggles every 1000 ms to ColorSchemes[0]
    // (*DAT_00ab6efc). [RE-FALLBACK: the ShowTimer list-insert site was not
    // traced; ShowTimer=yes gates membership.]
    struct TimerLine { std::string text; SDL_Color col; };
    std::vector<TimerLine> timer_lines_;   // the last drawn stack (probe)
    std::map<int, std::string> sw_timer_names_;   // SW type -> UIName text ("" = no ShowTimer)
    static std::string timer_fmt(const std::string& label, long secs) {
        char b[64];
        if (secs < 0) secs = 0;
        if (secs / 3600) std::snprintf(b, sizeof b, "  %ld:%02ld:%02ld", secs / 3600, (secs / 60) % 60, secs % 60);
        else std::snprintf(b, sizeof b, "  %02ld:%02ld", (secs / 60) % 60, secs % 60);
        return label + b;
    }
    SDL_Color house_text_color(int house) const {
        const ra2assets::Rgba c = world_.house_palette(ba_.unit_pal, house)[16];
        return SDL_Color{c.r, c.g, c.b, 255};
    }
    const std::string& sw_timer_name(ra2engine::SuperweaponType t) {
        auto it = sw_timer_names_.find(static_cast<int>(t));
        if (it != sw_timer_names_.end()) return it->second;
        std::string name;
        for (const auto& bt : ba_.roster) {
            const std::string sw = rp_building_supported_sw(rules_, bt.id);
            if (sw.empty() || rp_sw_type_of(rp_upper(rules_.section_string(sw, "Type", ""))) != t) continue;
            if (!rp_flag(rules_, sw, "ShowTimer")) break;
            const std::string ui = rules_.section_string(sw, "UIName", "");
            name = ui.empty() ? sw : camp::csf_text(dir_, ui);
            if (name == ui || name == sw)
                name = rules_.section_string(sw, "Name", ra2engine::superweapon_type_name(t));
            break;
        }
        return sw_timer_names_[static_cast<int>(t)] = name;
    }
    SDL_Color blink_color() const {
        const auto& cs = rules_ini_.section("Colors");
        int hh = 25, ss = 255, vv = 255;   // LightGold
        if (!cs.empty()) std::sscanf(cs.front().second.c_str(), "%d,%d,%d", &hh, &ss, &vv);
        const ra2assets::Rgba c = ra2engine::pal_for_hsv(ba_.unit_pal, hh, ss, vv)[16];
        return SDL_Color{c.r, c.g, c.b, 255};
    }
    void build_timer_stack() {
        timer_lines_.clear();
        if (campaign_) {
            const ra2engine::MapTriggerSystem& ts = campaign_->triggers();
            if (ts.mission_timer_start() != -1) {
                const long left = std::max(0L, ts.mission_timer_left() - (ts.frame() - ts.mission_timer_start()));
                std::string label;
                if (!ts.timer_text().empty()) label = camp::csf_text(dir_, ts.timer_text());
                if (label.empty()) {
                    label = camp::csf_text(dir_, "GUI:TimeRemaining");
                    if (label == "GUI:TimeRemaining") label = "Time Remaining:";
                }
                timer_lines_.push_back({timer_fmt(label, left / 15), house_text_color(0)});
            }
        }
        const bool blink_on = static_cast<long>(msg_clock_) % 2 == 1;
        for (int h = 0; h < world_.house_count(); ++h)
            for (ra2engine::SuperweaponType t : world_.sw_types(h)) {
                const ra2engine::Superweapon* sw = world_.sw_state(h, t);
                if (!sw) continue;
                const std::string& name = sw_timer_name(t);
                if (name.empty()) continue;
                if (!sw->ready && sw->timer <= 0.0) continue;   // not started charging
                const long secs = sw->ready ? 0 : static_cast<long>(std::max(0.0, sw->recharge_secs - sw->timer));
                SDL_Color col = house_text_color(h);
                if (secs == 0 && blink_on) col = blink_color();   // ColorSchemes[0]
                timer_lines_.push_back({timer_fmt(name, secs), col});
            }
    }
    void draw_timer_stack(SDL_Renderer* ren, ra2engine::TextureCache& tc) {
        build_timer_stack();
        const double sc = hud_touch_scale();   // W6-01-12: 1.5x on a touch canvas
        const int fh = static_cast<int>((hud_line_h() + 2) * sc);
        // Keep the stack level with the visible sidebar bottom. In compact
        // layouts, anchoring to the screen bottom leaves it below the sidebar.
        const int right = sidebar_up_ ? lay_.bar.x - 3 : playfield_w_ - 3;
        const int bottom = sidebar_up_ ? std::min(kWinH, lay_.bar.y + lay_.bar.h) : kWinH;
        for (std::size_t i = 0; i < timer_lines_.size(); ++i)
            hud_text(ren, tc, timer_lines_[i].text, right, bottom - fh * static_cast<int>(i + 1),
                     timer_lines_[i].col, kHudRight, false, true, sc);
    }
    void draw_end_banner(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& s) const {
        if (s.empty()) return;
        hud_text(ren, tc, s, playfield_w_ / 2, kWinH / 2 - 14, hud_ui_color(), kHudCenter, true, true, 2.0);
    }
    // Word-wrap to `max_w` px in the HUD font (explicit '\n' kept).
    static std::string hud_wrap(const std::string& text, int max_w) {
        std::string out, line, word;
        auto flush_word = [&]() {
            if (word.empty()) return;
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && hud_text_width(cand) > max_w) {
                out += line + "\n";
                line = word;
            } else {
                line = cand;
            }
            word.clear();
        };
        for (char c : text) {
            if (c == '\n') { flush_word(); out += line + "\n"; line.clear(); }
            else if (c == ' ') flush_word();
            else word += c;
        }
        flush_word();
        out += line;
        return out;
    }

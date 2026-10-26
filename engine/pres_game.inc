// pres_game.inc -- the GameScene-facing presentation driver (G-80/G-81/G-82 +
// effects/visual hooks). GameScene owns one `Presentation` and calls
// init()/update()/the render hooks; everything is observation of World /
// queue state (edge detection), so the simulation and every gate hash are
// untouched. Included by main.cpp after pres_audio.inc / pres_fx.inc.

class Presentation {
    bool miner_role_alerts_=true;
public:
    // side_ix: 0 Allied, 1 Soviet (Russian clips), 2 Yuri.
    void init(const std::string& dir, const ra2assets::MixFile& ra2, AudioSystem& audio, bool audio_on,
              const Rules& rules, const Ini& rules_ini, const Ini& art_ini,
              const std::vector<const std::vector<ra2engine::UnitTypeDef>*>& defs, int side_ix) {
        audio_ = &audio;
        const auto role_alert=read_settings_doc(settings_file_path()).get("Audio","MinerRoleAlerts");
        miner_role_alerts_=role_alert?ra2engine::ww_parse_bool(*role_alert,true):true;
        g_bld_events.clear();
        g_bunker_motion.clear();   // W2-GA-14 per-building anim state is per game
        speech_cooldown_.clear();
        g_projectile_motion.clear();
        g_unit_motion.clear();
        g_pres_crushed.clear();  // W7-04-25
        g_pres_sinkers.clear();  // W7-12-11
        g_pres_tick = 0;        // W3-B: the logic-tick anim clock restarts per game
        g_walk_stage.clear();
        g_walk_rates.clear();
        g_walk_rules = &rules_ini;
        ++g_pres_epoch;
        // G-83: the effects layer (muzzle / impact / death anims, projectile art).
        fx_.init(dir, ra2, rules_ini, art_ini);
        for (const auto* list : defs)
            for (const ra2engine::UnitTypeDef& d : *list) fx_.add_unit(d.id, d.combat.image);
        for (const char* id : {"E1", "E2", "HTNK", "HARV", "CMIN"}) fx_.add_unit(id, rules.image(id));
        g_fx = &fx_;
        fx_.split_light_ = true;   // W7-12-33
        {   // W7-12-06: [Radiation] RadColor / [AudioVisual] ChronoBeamColor, read once.
            auto rgb = [&](const std::string& v, SDL_Color d) {
                std::stringstream ss(pres_clean(v));
                std::string t;
                int c[3] = {d.r, d.g, d.b}, k = 0;
                while (k < 3 && std::getline(ss, t, ',')) c[k++] = std::clamp(std::atoi(pres_clean(t).c_str()), 0, 255);
                return k == 3 ? SDL_Color{Uint8(c[0]), Uint8(c[1]), Uint8(c[2]), 255} : d;
            };
            fx_.set_beam_colors(rgb(rules_ini.get("Radiation", "RadColor"), SDL_Color{0, 255, 0, 255}),
                                rgb(rules_ini.get("AudioVisual", "ChronoBeamColor"), SDL_Color{128, 200, 255, 255}));
        }
        // W3-FX: particles, debris, trails, wakes, twinkles (pres_effects.inc).
        effects_.init(fx_, rules_ini, ra2, dir);
        for (const auto* list : defs)
            for (const ra2engine::UnitTypeDef& d : *list) effects_.note_unit_id(d.combat.image, d.id);
        g_fx3 = &effects_;
        rules_ini_ = &rules_ini;
        combat_rules_ = &rules;
        w5_art_ini_ = &art_ini;   // W5-E2 sequence sounds
        snd_.open(dir, &audio, audio_on || g_pres_log_on);
        snd_.set_side(side_ix);
        // [G-81] Every producible / AI / crew unit gets its voice, death and
        // primary-weapon Report keys, keyed by image like the existing table.
        // W7-12-27/28, W7-09-01: keyed by RULES id; every other type (map
        // civilians, animals, cars, campaign / trigger / cheat spawns) is
        // registered the first time voices_of() meets it.
        rules_ = &rules;
        for (const auto* list : defs)
            for (const ra2engine::UnitTypeDef& d : *list) add_voices(rules, d.id);
        for (const char* id : {"E1", "E2", "HTNK", "HARV", "CMIN"}) add_voices(rules, id);
        auto av = [&](const char* k) { return pres_clean(rules_ini.get("AudioVisual", k)); };
        // G-88: health-bar colours from [AudioVisual] ConditionYellow/Red ("50%").
        auto pct = [&](const char* k, double d) {
            const std::string v = av(k);
            return v.empty() ? d : std::atof(v.c_str()) / 100.0;
        };
        ra2engine::set_health_conditions(pct("ConditionYellow", 0.5), pct("ConditionRed", 0.25));
        // RadarEvent table overrides + EVA repeat delay ([General]).
        {
            auto gen = [&](const char* k) { return pres_clean(rules_ini.get("General", k)); };
            std::stringstream ds(gen("RadarEventSuppressionDistances")), us(gen("RadarEventDurations"));
            std::string tok;
            for (int i = 0; i < 6 && std::getline(ds, tok, ','); ++i) radar_dist_[i] = std::atof(tok.c_str());
            for (int i = 0; i < 6 && std::getline(us, tok, ','); ++i) radar_dur_[i] = std::atoi(tok.c_str());
            std::stringstream vs(gen("RadarEventVisibilityDurations"));   // W3-FX minimap square
            for (int i = 0; i < 6 && std::getline(vs, tok, ','); ++i) radar_vis_[i] = std::atoi(tok.c_str());
            auto avd = [&](const char* k, double& out) {
                const std::string v = av(k);
                if (!v.empty()) out = std::atof(v.c_str());
            };
            avd("RadarEventMinRadius", g_radar_min_r);
            avd("RadarEventSpeed", g_radar_speed);
            avd("RadarEventRotationSpeed", g_radar_rot);
            avd("RadarEventColorSpeed", g_radar_col);
            g_radar_pings.clear();
            const std::string sd = gen("SpeakDelay");
            if (!sd.empty()) speak_delay_frames_ = static_cast<long>(std::atof(sd.c_str()) * 900.0);
        }
        base_attack_siren_ = av("BaseUnderAttackSound");
        radar_on_ = av("RadarOn");
        radar_off_ = av("RadarOff");
        building_die_ = av("BuildingDieSound");
        building_damage_ = av("BuildingDamageSound");
        sell_sound_ = av("SellSound");
        vet_sound_ = av("UpgradeVeteranSound");
        elite_sound_ = av("UpgradeEliteSound");
        impact_water_ = av("ImpactWaterSound");
        snd_.ensure_sound(impact_water_);
        for (const std::string& k : {base_attack_siren_, building_die_, building_damage_, sell_sound_,
                                     vet_sound_, elite_sound_})
            snd_.ensure_sound(k);
        snd_.music_start();
        // G-88: 12metfnt.fnt for the HUD text (credits, banners).
        try {
            MixFile cache = ra2.open_nested("cache.mix");
            if (const MixEntry* fe = cache.find(std::string("12metfnt.fnt"))) {
                font_ = std::make_unique<ra2assets::Font>(ra2assets::load_fnt(cache.read(*fe)));
                // [RE-FALLBACK: HUD text colour not read] pale yellow ramp.
                font_pal_ = make_font_palette_tinted(255, 240, 170);
                g_game_font = font_.get();
                g_game_font_pal = &font_pal_;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[pres] 12metfnt.fnt unavailable: " << ex.what() << "\n";
        }
        // G-84: SHROUD.SHP (conquer.mix) for the shroud/fog post-pass.
        try {
            MixFile conquer = ra2.open_nested("conquer.mix");
            if (const MixEntry* e = conquer.find(std::string("shroud.shp"))) {
                shroud_ = std::make_unique<ShpFile>(ShpFile::load(conquer.read(*e)));
                g_shroud_shp = shroud_.get();
            }
            if (const MixEntry* e = conquer.find(std::string("fog.shp"))) {
                fog_ = std::make_unique<ShpFile>(ShpFile::load(conquer.read(*e)));
                g_fog_shp = fog_.get();
            }
        } catch (const std::exception& ex) {
            std::cerr << "[pres] shroud.shp unavailable: " << ex.what() << "\n";
        }
    }
    ~Presentation() {
        for(auto& kv:drain_loops_)kv.second.update(snd_,false,0,0);
        for (auto& loop : shot_loops_) loop.update(snd_,false,0,0);
        for (auto& loop : building_shot_loops_) loop.update(snd_,false,0,0);
        if (g_shroud_shp == shroud_.get()) g_shroud_shp = nullptr;
        if (g_fog_shp == fog_.get()) g_fog_shp = nullptr;
        if (g_game_font == font_.get()) { g_game_font = nullptr; g_game_font_pal = nullptr; }
        if (g_fx == &fx_) g_fx = nullptr;
        if (g_fx3 == &effects_) g_fx3 = nullptr;
        g_state_world = nullptr;      // W5-PRES R-120
        g_state_fx = PresStateFx{};
        g_chutes.clear();             // W5-PRES R-122
    }

    PresAudio& sound() { return snd_; }
    // W3-FX: the trigger visuals (campaign_visuals.inc) drive these.
    PresFx& fx() { return fx_; }
    PresEffects& effects() { return effects_; }
    // W6-06-03: after a load, the World's SW event feed (saved with it) is
    // already announced: start the cursor at its newest event so loading
    // does not replay "Nuclear silo detected" / "Reinforcements ready".
    // W6-06-04: and rebuild the damage smoke on the next effects tick.
    void after_load(const ra2engine::World& w) {
        for (const ra2engine::SwEvent& e : w.sw_events()) sw_seen_ = std::max(sw_seen_, e.seq);
        effects_.after_load();
    }
    // Action 55 / any caller: a radar event at a cell (the minimap flash).
    bool radar_ping(int type, double x, double y) {
        return radar_event(std::clamp(type, 0, 16), x, y);
    }
    struct RadarPing { int type; double x, y; long born, until; };
    // Live radar events for the minimap "zoom square" (pres_radar.inc).
    std::vector<RadarPing> radar_pings() const {
        std::vector<RadarPing> out;
        for (const RadarEv& e : radar_) out.push_back(RadarPing{e.type, e.x, e.y, e.born, e.until});
        return out;
    }

    // Per-frame. `credits` = the player's wallet after this frame's production.
    void update(double dt, const ra2engine::World& w, const ra2engine::ProductionQueues& q,
                const BuildingAssets& ba, double credits, const ra2engine::InputActions& act,
                const ra2engine::Camera& cam, int view_w, int view_h,
                const std::vector<ra2engine::World::VetEvent>& vet,
                const std::vector<ra2engine::ProjectileEvent>& pev) {
        ++frame_;
        // W3-B anim clock: g_pres_tick is the LOGIC tick count (logic_tick()
        // below, called once per fixed 15 Hz step), not this per-render-frame
        // counter -- every anim keyed on it is frame-rate independent.
        world_ = &w;
        // W3-FX: particles / debris / trails / wakes on the World-frame clock.
        if (ba.ok) effects_.set_unit_palette(&ba.unit_pal);
        effects_.update(w, ba);
        ambient_sounds(w, ba);   // W3-FX AmbientSound loops
        snd_.set_view(cam.x, cam.y, view_w, view_h);   // W2-GA-23 positional audio
        snd_.update_voices();
        projectile_fx(w, pev);   // events not already taken per tick (W3-B)
        clock_ += dt;
        snd_.set_frame(frame_);
        cam_ = cam;
        vw_ = view_w;
        vh_ = view_h;
        production_eva(w, q, credits);
        power_eva(w, ba);
        if (ba.ok) unit_pal_ = &ba.unit_pal;
        if (ba.ok) fx_.set_unit_palette(&ba.unit_pal, &w);   // W3-B AltPalette base
        unit_audio(w, act);
        w5_update(w, ba, act, credits);   // W5-E (pres_audio_events.inc)
        for (const auto& e : vet) {
            if (e.unit < 0 || e.unit >= static_cast<int>(w.units().size())) continue;
            const ra2engine::Unit& u = w.units()[e.unit];
            if (u.house != g_local_house) continue;
            snd_.play_at(e.new_rank >= 2 ? elite_sound_ : vet_sound_, u.fx, u.fy, "promotion");
            snd_.eva("EVA_UnitPromoted");
        }
        superweapon_events(w);   // W2-AN-05/20: the World SW event feed (any house)
        // [RE: presentation.md §1.3 FUN_004fc800 / FUN_004fc9f0] end of game,
        // local player only, once.
        if (!end_spoken_) {
            if (w.house_defeated(g_local_house)) { snd_.eva("EVA_YouHaveLost"); end_spoken_ = true; }
            else if (w.house_outcome(g_local_house) > 0) {   // W9-05-04: allied winners of any team
                snd_.eva("EVA_YouAreVictorious");
                end_spoken_ = true;
            }
        }
        // G-09: radar power-up animation + RadarOn/RadarOff ([AudioVisual]).
        {
            const bool online = w.shroud_enabled() && house_owns_operational_radar(w, ba, g_local_house);
            if (online && !radar_online_) {
                g_radar_online_since = g_pres_tick;
                snd_.play(radar_on_, 0.0, "radar on");
            } else if (!online && radar_online_) {
                g_radar_offline_since = g_pres_tick;   // W2-UI-09 power-down animation
                snd_.play(radar_off_, 0.0, "radar off");
            }
            radar_online_ = online;
        }
        // W3-FX: the live radar events for the minimap zoom square.
        g_radar_pings.clear();
        for (const RadarEv& e : radar_)
            if (e.until > frame_ && radar_vis_[e.type] > 0)
                g_radar_pings.push_back(RadarPingDraw{e.x, e.y, frame_ - e.born, radar_vis_[e.type]});
        snd_.pump(clock_);
        snd_.music_update(clock_);
        if (audio_ && audio_->enabled && audio_->mixer) audio_->out.pump(*audio_->mixer);
    }

    long frame() const { return frame_; }

    // W3-B [RE: presentation.md §5 -- AnimClass::AI steps once per LOGIC
    // frame; Rate stores 900/Rate game frames per anim frame]: GameScene calls
    // this once per fixed logic tick (inside its G-13 accumulator loop), so
    // every anim (effects, building slots, terrain, radar) advances with game
    // time -- at the original rate on a 60 Hz desktop and a 30 Hz phone alike,
    // and scaled by GameSpeed exactly as the original's frames are.
    void logic_tick(const ra2engine::World& w, const BuildingAssets& ba, double player_credits,
                    const std::vector<ra2engine::ProjectileEvent>& pev) {
        g_projectile_motion.capture(w.projectiles(),pev);
        g_unit_motion.capture_world(w);
        g_pres_tick = ++logic_;
        fx_.tick();
        pres_state_tick(w, fx_);   // W5-PRES R-120 chrono warp anims / sparkles
        projectile_fx(w, pev);   // W3-B: impacts on the tick they detonate
        // W3-B: the building slot machine sees this tick's state and events.
        const auto& fe = w.pending_fire_events();
        std::vector<ra2engine::World::FireEvent> fresh;
        for (std::size_t i = std::min(fire_seen_, fe.size()); i < fe.size(); ++i) fresh.push_back(fe[i]);
        fire_seen_ = fe.size();
        unit_fx_tick(w, fresh);
        defense_fire_tick(w, fresh);   // muzzle / beam / report on the tick of the shot
        disc_drain_tick(w);
        fragment_fx(w);                // W7-12-07 shrapnel beams
        pres_bld_anim_tick(w, ba, fx_, g_pres_tick, fresh, units_seen_, bld_seen_,
                           [&](int h) { return w.credits(h); });   // L2: every house's wallet
        (void)player_credits;
        building_anim_events(w, ba);   // W2-GA-14 SW charge edges, per tick
        building_events(w, ba);        // damage EVA, death explosion + DestroyAnim, per tick (W3-B)
        w5_tick(w, ba);                // W5-E sounds observed per logic tick (pres_audio_events.inc)
        g_walk_rules = rules_ini_;
        pres_walk_tick(w, g_pres_tick); // W3-B walk stage (SHP vehicle frames, HVA frames)
        sinker_tick();                  // W7-12-11
    }
    std::size_t fire_seen_ = 0, bld_seen_ = 0;   // W3-B
    void projectile_fx(const ra2engine::World& w, const std::vector<ra2engine::ProjectileEvent>& pev) {
        for (const ra2engine::ProjectileEvent& e : pev)
            if (e.kind == ra2engine::ProjectileEvent::Detonate) {
                const double fx = e.x / 256.0, fy = e.y / 256.0;
                if (!w.object_visible_at(g_local_house, ra2engine::lepton_to_cell(e.x), ra2engine::lepton_to_cell(e.y))) continue;
                const ra2engine::Unit* a = (e.attacker >= 0 && e.attacker < static_cast<int>(w.units().size()))
                                               ? &w.units()[e.attacker] : nullptr;
                if (a) impact(*a, e.damage, fx, fy, level_at(w, fx, fy), e.z * 36.74 / 256.0);
            }
    }

private:
    std::map<int,ra2engine::WeaponSoundLoop> drain_loops_;
    std::set<int> drain_attached_;
    void disc_drain_tick(const ra2engine::World& w) {
        std::set<int> active;
        const std::string anim=rules_ini_ ? pres_clean(rules_ini_->get("CombatDamage","DrainAnimationType")) : std::string();
        for(const auto& link:w.disc_drains()) {
            if(!w.building_disc_drained(link.building) || link.unit<0 ||
               link.unit>=static_cast<int>(w.units().size()))continue;
            const auto& u=w.units()[link.unit];active.insert(link.unit);
            if(!u.alive || u.die_time>=0 || u.house!=link.source_house ||
               !w.disc_drain_order(u,link.building) || u.has_flight_goal) {
                active.erase(link.unit);continue;
            }
            if(!drain_attached_.count(link.unit) && !anim.empty()) {
                fx_.attach(link.unit,anim,0,true);
                drain_attached_.insert(link.unit);
            }
            const FxAnimType* type=anim.empty()?nullptr:fx_.type(anim);
            auto& loop=drain_loops_[link.unit];
            if(type && !type->report.empty() && !loop.handle)
                loop.shot(snd_,type->report,u.fx,u.fy);
            loop.update(snd_,true,u.fx,u.fy);
        }
        for(auto it=drain_loops_.begin();it!=drain_loops_.end();)
            if(!active.count(it->first)) {
                it->second.update(snd_,false,0,0);fx_.detach(it->first,true);
                drain_attached_.erase(it->first);it=drain_loops_.erase(it);
            } else ++it;
    }
    // ---- G-08 (draw half) action cursors from MOUSE.SHP ---------------------
    // MouseCursorType ids (YRpp GeneralDefinitions.h) index the 86-entry
    // descriptor table at gamemd.exe 0x0082C038 {start, count, rate, hotX,
    // hotY} (dumped in D:\ra2-re\out\rs10_log.txt; repair-sell.md §D8 pinned
    // entries 30..35 against MOUSE.SHP). Hotspot codes: 0 = left/top,
    // 12345 = centre, 54321 = right/bottom.
    enum Cursor : int {
        kDefault = 0x00, kScrollN = 0x01, kNoScrollN = 0x09, kSelect = 0x11, kMove = 0x12,
        kNoMove = 0x13, kAttack = 0x14, kAttackOutOfRange = 0x15, kEnter = 0x19, kNoEnter = 0x1A,
        kDeploy = 0x1B, kNoDeploy = 0x1C, kNuke = 0x35, kIronCurtain = 0x39, kChronosphere = 0x3A,
        kLightningStorm = 0x32, kAirstrike = 0x52,
    };
    // W2-AN-04 [RE: presentation.md §3 ConvertAction: 0x14 Nuke -> 0x35,
    // 0x25 IronCurtain -> 0x39, 0x26 LightningStorm -> 0x32, 0x27/0x28 Chrono
    // -> 0x3A, 0x29 ParaDrop -> 0x2F]: the armed SW's targeting cursor.
    mutable int armed_sw_ = 0;
    static int sw_cursor(int armed, const ra2engine::World& w) {
        using T = ra2engine::SuperweaponType;
        T t = static_cast<T>(armed);
        if (t == T::None) {
            const ra2engine::Superweapon* sw = w.sw_state(g_local_house);
            if (sw) t = sw->type;
        }
        switch (t) {
            case T::IronCurtain: return kIronCurtain;
            case T::LightningStorm: return kLightningStorm;
            case T::ChronoSphere: case T::ChronoWarp: return kChronosphere;
            case T::ParaDrop: case T::AmerParaDrop: return kNuke;   // [0x2F frames not in the dumped table]
            default: return kNuke;
        }
    }
    struct CursorDesc { int start, count, rate, hx, hy; };
    static const CursorDesc& cursor_desc(int id) {
        static const CursorDesc kTable[] = {
            {0, 1, 0, 0, 0},             // 0x00 Default
            {2, 1, 0, 12345, 0},         // 0x01 scroll N
            {3, 1, 0, 54321, 0},         // NE
            {4, 1, 0, 54321, 12345},     // E
            {5, 1, 0, 54321, 54321},     // SE
            {6, 1, 0, 12345, 54321},     // S
            {7, 1, 0, 0, 54321},         // SW
            {8, 1, 0, 0, 12345},         // W
            {9, 1, 0, 0, 0},             // NW
            {10, 1, 0, 12345, 0},        // 0x09 no-scroll N
            {11, 1, 0, 54321, 0},
            {12, 1, 0, 54321, 12345},
            {13, 1, 0, 54321, 54321},
            {14, 1, 0, 12345, 54321},
            {15, 1, 0, 0, 54321},
            {16, 1, 0, 0, 12345},
            {17, 1, 0, 0, 0},
            {18, 13, 4, 12345, 12345},   // 0x11 Select
            {31, 10, 4, 12345, 12345},   // 0x12 Move
            {41, 1, 0, 12345, 12345},    // 0x13 NoMove
            {53, 5, 4, 12345, 12345},    // 0x14 Attack
            {58, 5, 4, 12345, 12345},    // 0x15 AttackOutOfRange
            {68, 5, 4, 12345, 12345},    // 0x16 Protect
            {78, 10, 4, 12345, 12345},   // 0x17 DesolatorDeploy
            {88, 1, 0, 12345, 12345},    // 0x18
            {89, 10, 4, 12345, 12345},   // 0x19 Enter
            {99, 1, 0, 12345, 12345},    // 0x1A NoEnter
            {110, 9, 4, 12345, 12345},   // 0x1B Deploy
            {119, 1, 0, 12345, 12345},   // 0x1C NoDeploy
            {120, 9, 4, 12345, 12345},   // 0x1D
            {129, 10, 4, 12345, 12345},  // 0x1E Sell
            {139, 10, 4, 12345, 12345},  // 0x1F SellUnit
            {149, 1, 0, 12345, 12345},   // 0x20 NoSell
            {150, 20, 4, 12345, 12345},  // 0x21 Repair
            {170, 20, 4, 12345, 12345},  // 0x22 EngineerRepair
            {190, 1, 0, 12345, 12345},   // 0x23 NoRepair
        };
        static const CursorDesc kRallyD{269, 10, 0, 12345, 12345};   // 0x30 RallyPoint [table [48]]
        static const CursorDesc kNukeD{319, 10, 4, 12345, 12345};     // 0x35
        static const CursorDesc kIronD{394, 10, 4, 12345, 12345};     // 0x39 [table [57]]
        static const CursorDesc kChronoD{404, 9, 4, 12345, 12345};    // 0x3A
        static const CursorDesc kStormD{279, 20, 4, 12345, 12345};    // 0x32
        // W7-02-06 [RE: table 0x0082C038 entry 0x52 = {480, 8, 4, -1, -1, C, M}]
        static const CursorDesc kAirD{480, 8, 4, 12345, 12345};       // 0x52 airstrike
        if (id >= 0 && id < static_cast<int>(sizeof kTable / sizeof kTable[0])) return kTable[id];
        if (id == kNuke) return kNukeD;
        if (id == kIronCurtain) return kIronD;
        if (id == kChronosphere) return kChronoD;
        if (id == kLightningStorm) return kStormD;
        if (id == kAirstrike) return kAirD;
        if (id == 0x30) return kRallyD;
        return kTable[0];
    }

    // Fallback "what would a click do" probe [G-08: B owns the authoritative
    // cursor state; this mirrors the current Input order semantics so the
    // pointer never lies about them]. Returns a MouseCursorType id.
    int probe_cursor(const ra2engine::World& w, const ra2engine::Input& in, int mx, int my,
                     bool over_sidebar, int view_w, int win_w, int win_h) const {
        constexpr int kEdge = 24;   // GameScene's edge-scroll band
        int dx = (mx >= win_w - kEdge) - (mx <= kEdge), dy = (my >= win_h - kEdge) - (my <= kEdge);
        if (dx || dy) {
            static const int dir[3][3] = {{7, 0, 1}, {6, -1, 2}, {5, 4, 3}};   // [dy+1][dx+1]
            return kScrollN + dir[dy + 1][dx + 1];
        }
        if (over_sidebar || in.placement_active()) return kDefault;
        if (in.sw_targeting()) return sw_cursor(in.sw_armed_type(), w);   // W2-AN-04
        // Object under the pointer.
        int hit = -1;
        const auto& us = w.units();
        for (int i = static_cast<int>(us.size()) - 1; i >= 0; --i) {
            const ra2engine::Unit& u = us[i];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            if (u.house != g_local_house && !w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) continue;
            const int gx = static_cast<int>(std::lround((u.fx - u.fy) * 30.0)) + cam_.x + 30;
            int gy = static_cast<int>(std::lround((u.fx + u.fy) * 15.0 - u.level * 15)) + cam_.y + 15;
            // W2-AN-24: the cursor tracks the RAISED sprite of an airborne unit.
            if (u.is_aircraft && u.altitude > 0.0) gy -= ra2engine::altitude_to_screen_dy(u.altitude);
            const int hw = u.is_vehicle ? 22 : 10, up = u.is_vehicle ? 24 : 30;
            if (mx >= gx - hw && mx <= gx + hw && my >= gy - up && my <= gy + 6) { hit = i; break; }
        }
        const ra2engine::Selection& sel = in.selection();
        auto sel_has = [&](const ra2engine::Unit& u) { return sel.has(static_cast<int>(&u - us.data())); };
        bool any_sel = false;
        for (const ra2engine::Unit& u : us)
            if (sel_has(u) && u.house == g_local_house && u.alive && u.die_time < 0.0) { any_sel = true; break; }
        if (hit >= 0) {
            const ra2engine::Unit& u = us[hit];
            if (u.house == g_local_house || !w.is_enemy(g_local_house, u.house)) return kSelect;
            if (!any_sel) return kDefault;
            // [RE: presentation.md §3 FUN_004aada0] Attack only when in range,
            // else AttackOutOfRange (measured from the lowest selected unit).
            for (const ra2engine::Unit& s : us)
                if (sel_has(s) && s.house == g_local_house && s.alive && s.die_time < 0.0) {
                    const double d = std::hypot(s.fx - u.fx, s.fy - u.fy);
                    return d <= s.w_range ? kAttack : kAttackOutOfRange;
                }
            return kAttack;
        }
        const int bi = in.building_under(mx, my);
        if (bi >= 0 && bi < static_cast<int>(w.buildings().size())) {
            const ra2engine::Building& b = w.buildings()[bi];
            if (any_sel && b.house != g_local_house && w.is_enemy(g_local_house, b.house) && pres_building_seen(w, b)) return kAttack;
            if (b.house == g_local_house) return any_sel ? kNoMove : kDefault;
        }
        if (!any_sel) return kDefault;
        if (mx >= view_w) return kDefault;
        // Ground: move where passable (or unexplored -- the original lets you
        // order into the shroud), no-move otherwise.
        int cx = 0, cy = 0;
        ra2engine::screen_to_cell(mx - cam_.x, my - cam_.y, 60, 30, cx, cy);
        if (!w.explored_at(g_local_house, cx, cy)) return kMove;
        const int gw = w.grid_w(), gh = w.grid_h();
        if (cx < 0 || cy < 0 || cx >= gw || cy >= gh) return kNoMove;
        return w.group_can_enter(sel.ids(), g_local_house, cx, cy) ? kMove : kNoMove;   // W2-AN-09
    }

    // B's Input::Action (the authoritative "what would a click do") ->
    // MouseCursorType [RE: presentation.md §3, DisplayClass::ConvertAction
    // FUN_004aada0]: Attack shows AttackOutOfRange when the selection cannot
    // reach; over a shrouded cell Attack and Move both show Move.
    int cursor_for_action(ra2engine::Input::Action a, const ra2engine::World& w, const ra2engine::Input& in,
                          int mx, int my) const {
        using A = ra2engine::Input::Action;
        int cx = 0, cy = 0;
        ra2engine::screen_to_cell(mx - cam_.x, my - cam_.y, 60, 30, cx, cy);
        const bool shrouded = !w.explored_at(g_local_house, cx, cy);
        switch (a) {
            case A::Select: case A::ToggleSelect: case A::SelectBuilding: case A::SetPrimary: return kSelect;
            case A::Move: return kMove;
            case A::NoMove: return kNoMove;
            case A::Attack: {
                if (shrouded) return kMove;
                // W7-02-06 [RE: InfantryClass::What_Action 0x51E81E -- a weapon
                // with MigAttackCursor (+0x13A) on a building -> action 0x47;
                // ConvertAction 0x4AADA0 case 0x47 -> MouseCursorType 0x52]: Boris
                // designating an airstrike.
                if (in.building_under(mx, my) >= 0)
                    for (const ra2engine::Unit& s : w.units())
                        if (in.selection().has(static_cast<int>(&s - w.units().data())) && s.house == g_local_house &&
                            s.alive && s.die_time < 0.0 && s.w2.valid && rules_ini_) {
                            const std::string m = pres_lower(pres_clean(rules_ini_->get(s.w2.id, "MigAttackCursor")));
                            if (!m.empty() && (m[0] == 'y' || m[0] == 't' || m[0] == '1')) return kAirstrike;
                        }
                for (const ra2engine::Unit& s : w.units())
                    if (in.selection().has(static_cast<int>(&s - w.units().data())) && s.house == g_local_house && s.alive &&
                        s.die_time < 0.0)
                        return std::hypot(s.fx - cx, s.fy - cy) <= s.w_range + 0.5 ? kAttack : kAttackOutOfRange;
                return kAttack;
            }
            case A::Harvest: return kAttackOutOfRange;   // Action 6 Harvest -> 0x15
            case A::Enter: case A::Dock: case A::Capture: case A::Infiltrate: return kEnter;
            case A::EngineerRepair: return 0x22;
            case A::Deploy: return kDeploy;
            case A::Rally: return 0x30;
            case A::Sell: return 0x1E;
            case A::NoSell: return 0x20;
            case A::Repair: return 0x21;
            case A::NoRepair: return 0x23;
            case A::SuperWeapon: return sw_cursor(armed_sw_, w);   // W2-AN-04
            default: return kDefault;
        }
    }
public:
    // Draws the action cursor (repair/sell modes keep draw_mode_cursor).
    // Returns true when it drew, so the caller hides the OS pointer.
    bool draw_cursor(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ModeCursor& mc,
                     const ra2engine::World& w, const ra2engine::Input& in, int mx, int my,
                     bool over_sidebar, int view_w, int win_w, int win_h, std::uint64_t game_frame,
                     int action = -1) {
        if (!mc.ok) return false;
        armed_sw_ = in.sw_armed_type();
        int id = probe_cursor(w, in, mx, my, over_sidebar, view_w, win_w, win_h);
        // Edge-scroll arrows and the sidebar keep the probe's answer; the
        // playfield follows B's action_at when supplied.
        const bool scroll = id >= kScrollN && id < kNoScrollN;
        if (action >= 0 && !scroll && !over_sidebar && mx < view_w)
            id = cursor_for_action(static_cast<ra2engine::Input::Action>(action), w, in, mx, my);

        if (id != last_cursor_) {
            pres_log(frame_, "cursor " + std::to_string(id));
            last_cursor_ = id;
        }
        const CursorDesc& d = cursor_desc(id);
        // [RE: presentation.md §3] one frame every Interval ticks of
        // timeGetTime()>>4 (16 ms), not game frames.
        (void)game_frame;
        const std::uint64_t tick = SDL_GetTicks() >> 4;
        int frame = d.start + (d.rate > 0 ? static_cast<int>((tick / d.rate) % d.count) : 0);
        if (frame >= mc.shp.count()) frame = 0;
        const ra2assets::ShpFile::Frame f = mc.shp.frame_info(frame);
        if (f.w <= 0 || f.h <= 0) return true;
        char key[32];
        std::snprintf(key, sizeof key, "mouse:%d", frame);
        SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
        if (!tex) {
            std::vector<uint8_t> idx = mc.shp.decode_indexed(frame);
            tex = static_cast<SDL_Texture*>(tc.get(key, idx.data(), f.w, f.h, mc.pal));
        }
        auto hot = [](int code, int size) { return code == 12345 ? size / 2 : code == 54321 ? size - 1 : 0; };
        const int hx = hot(d.hx, mc.shp.width()), hy = hot(d.hy, mc.shp.height());
        SDL_Rect dst{mx - hx + f.x, my - hy + f.y, f.w, f.h};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        return true;
    }
    int cursor() const { return last_cursor_; }

private:
    int last_cursor_ = -1;
    // G-83 / W2-AN-05/20 superweapon visuals + EVA, driven by the World's SW
    // event feed (World::sw_events, read by sequence number) so an AI launch
    // shows and sounds like the player's.
    //  * Launched: [General] NukeTakeOff=NUKETO at the silo; EVA
    //    EVA_NuclearMissileLaunched for EVERY player [RE: presentation.md §1,
    //    FUN_006cba10 @6cd208 -- no local-player check].
    //  * Detonated: [NUKE] AnimList=NUKEANIM at the target + its Report=.
    //  * Activated: IronCurtainInvokeAnim=IRONBLST / ChronoBlast=CHRONOFD +
    //    ChronoBlastDest=CHRONOTG / the storm clouds; EVA *Activated /
    //    LightningStormCreated for every player.
    //  * Granted to an enemy house: EVA_NuclearSiloDetected /
    //    IronCurtainDetected / ChronosphereDetected / WeatherDeviceReady [RE:
    //    FUN_00445ee0 -- played only when the placing house is not the player
    //    and not allied; SW types 0..3 only].
    //  * Ready (own): EVA_*Ready.
    //  * Bolt: WeatherConBolts=WCLBOLT1 + WeatherConBoltExplosion + LightningSounds.
    int sw_seen_ = 0;
    std::string gen_s(const char* k) const {
        std::string v = pres_clean(rules_ini_->get("General", k));
        if (v.empty()) v = pres_clean(rules_ini_->get("AudioVisual", k));
        const std::size_t c = v.find(',');
        return c == std::string::npos ? v : pres_clean(v.substr(0, c));
    }
    void superweapon_events(const ra2engine::World& w) {
        using T = ra2engine::SuperweaponType;
        using E = ra2engine::SwEvent;
        for (const ra2engine::SwEvent& e : w.sw_events()) {
            if (e.seq <= sw_seen_) continue;
            sw_seen_ = e.seq;
            const T t = static_cast<T>(e.type);
            const bool own = e.house == g_local_house;
            const bool hostile = !own && w.is_enemy(g_local_house, e.house);
            switch (e.kind) {
                case E::Granted:
                    if (!hostile) break;
                    if (t == T::Nuke) snd_.eva("EVA_NuclearSiloDetected");
                    else if (t == T::IronCurtain) snd_.eva("EVA_IronCurtainDetected");
                    else if (t == T::ChronoSphere) snd_.eva("EVA_ChronosphereDetected");
                    else if (t == T::LightningStorm) snd_.eva("EVA_WeatherDeviceReady");
                    break;
                case E::Ready:
                    if (!own) break;
                    if (t == T::Nuke) snd_.eva("EVA_NuclearMissileReady");
                    else if (t == T::IronCurtain) snd_.eva("EVA_IronCurtainReady");
                    else if (t == T::ChronoSphere) snd_.eva("EVA_ChronosphereReady");
                    else if (t == T::LightningStorm) snd_.eva("EVA_LightningStormReady");
                    else if (t == T::ParaDrop || t == T::AmerParaDrop) snd_.eva("EVA_ReinforcementsReady");
                    break;
                case E::Launched:
                    ra2engine::diag::note("NUKE_FX launch=%s available=%d",gen_s("NukeTakeOff").c_str(),
                        fx_.spawn(gen_s("NukeTakeOff"),e.x2,e.y2,level_at(w,e.x2,e.y2),20.0)!=nullptr);
                    snd_.eva("EVA_NuclearMissileLaunched");
                    pres_log(frame_, "fx superweapon launch");
                    break;
                case E::Detonated: {
                    std::string wh = gen_s("NukeWarhead");
                    if (wh.empty()) wh = "NUKE";
                    std::string list = pres_clean(rules_ini_->get(pres_upper(wh), "AnimList"));
                    const std::size_t comma = list.find(',');
                    if (comma != std::string::npos) list = pres_clean(list.substr(0, comma));
                    const FxAnimType* ft=fx_.spawn(list,e.x,e.y,level_at(w,e.x,e.y),0.0);
                    ra2engine::diag::note("NUKE_FX impact=%s available=%d",list.c_str(),ft!=nullptr);
                    if (ft)
                        snd_.play_at(ft->report, e.x, e.y, "nuke detonation");
                    pres_log(frame_, "fx superweapon " + list);
                    break;
                }
                case E::Activated:
                    if (t == T::IronCurtain) {
                        fx_.spawn(gen_s("IronCurtainInvokeAnim"), e.x, e.y, level_at(w, e.x, e.y), 0.0);
                        snd_.eva("EVA_IronCurtainActivated");
                    } else if (t == T::ChronoSphere || t == T::ChronoWarp) {
                        fx_.spawn(gen_s("ChronoBlast"), e.x, e.y, level_at(w, e.x, e.y), 0.0);
                        fx_.spawn(gen_s("ChronoBlastDest"), e.x2, e.y2, level_at(w, e.x2, e.y2), 0.0);
                        snd_.eva("EVA_ChronosphereActivated");
                    } else if (t == T::LightningStorm) {
                        fx_.spawn(gen_s("WeatherConClouds"), e.x, e.y, level_at(w, e.x, e.y), 60.0);
                        snd_.eva("EVA_LightningStormCreated");
                    }
                    pres_log(frame_, std::string("fx superweapon activated ") + ra2engine::superweapon_type_name(t));
                    break;
                case E::Bolt:
                    // The launch cloud is finite and may finish during the
                    // deferment. Each real strike also needs its cloud above
                    // the bolt, rather than leaving the active storm cloudless.
                    fx_.spawn(gen_s("WeatherConClouds"),e.x,e.y,level_at(w,e.x,e.y),60.0);
                    if (const FxAnimType* ft = fx_.spawn(gen_s("WeatherConBolts"), e.x, e.y, level_at(w, e.x, e.y), 0.0))
                        snd_.play_at(ft->report, e.x, e.y, "bolt");
                    fx_.spawn(gen_s("WeatherConBoltExplosion"), e.x, e.y, level_at(w, e.x, e.y), 0.0);
                    snd_.play_at(gen_s("LightningSounds"), e.x, e.y, "bolt");
                    break;
                case E::Dropped:
                    if (own) snd_.eva("EVA_ReinforcementsHaveArrived");
                    break;
                default:
                    break;
            }
        }
    }
    // ---- G-83 effects -------------------------------------------------------
    // W4-FIX-POS: the terrain level of the cell holding cell-space point
    // (x, y) (was a stub returning 0: every FX anim on raised ground drew
    // 15 px/level too low). Cell (x, y)'s centre is the integer point.
    static int level_at(const ra2engine::World& w, double x, double y) {
        const ra2assets::MapCell* c =
            w.map().at(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
        return c ? static_cast<int>(c->level) : 0;
    }
    static int building_level(const ra2engine::World& w, const ra2engine::Building& b) {
        return level_at(w, b.cx, b.cy);   // a building flattens to its origin cell's level
    }
    // Warhead AnimList impact + the anim's Report=.
    // [RE-FALLBACK: RE-P5 open -- AnimList pick] index = damage / 25, clamped
    // to the list (bigger hits -> later, larger anims).
    void impact(const ra2engine::Unit& shooter, int damage, double fx, double fy, int level,
                double lift_px) {
        // W7-12-30: the warhead of the weapon that fired (secondary / elite).
        const FxWeapon* wp = world_ ? &fx_.weapon_fx(fired_weapon(*world_, shooter)) : fx_.weapon(shooter.image);
        if (!wp || wp->anim_list.empty()) return;
        // W3-FX [RE: FUN_0048a400 SelectDamageAnimation]: a Conventional
        // warhead landing low on a water cell shows SplashList[min(damage,
        // n*35-1)/35] instead of its AnimList.
        const std::vector<std::string>& splash = effects_.splash_list();
        if (wp->conventional && !splash.empty() && on_water(fx, fy) && lift_px < 30.0) {
            const int ns = static_cast<int>(splash.size());
            const std::string& sname = splash[static_cast<std::size_t>(std::min(damage, ns * 35 - 1) / 35)];
            if (const FxAnimType* st = fx_.spawn(sname, fx, fy, level, 0.0)) {
                if (!impact_water_.empty()) snd_.play_at(impact_water_, fx, fy, "impact water");
                else snd_.play_at(st->report, fx, fy, "splash");
                pres_log(frame_, "fx splash " + sname);
                effects_.note_splash();
                return;
            }
        }
        const int n = static_cast<int>(wp->anim_list.size());
        const int ix = std::clamp(damage / 25, 0, n - 1);
        const FxAnimType* t = fx_.spawn(wp->anim_list[ix], fx, fy, level, lift_px);
        // W2-GA-25: a hit on a water cell sounds [AudioVisual] ImpactWaterSound.
        if (on_water(fx, fy) && !impact_water_.empty()) snd_.play_at(impact_water_, fx, fy, "impact water");
        else if (t) snd_.play_at(t->report, fx, fy, "impact");
        pres_log(frame_, "fx impact " + wp->anim_list[ix]);
    }
    // W4: a RocketLocomotion rocket's explosion -- its block's warhead
    // AnimList (FUN_0048A400 pick: damage / 25, as impact()), at its height;
    // a splash when it comes down low on water.
    void rocket_fx(const ra2engine::World& w, const ra2engine::Unit& u) {
        const ra2engine::RocketBlock& blk = w.sw_params().rockets.block[static_cast<std::size_t>(
            std::clamp(u.an.t.rocket_kind < 0 ? 1 : u.an.t.rocket_kind, 0, 2))];
        const ra2engine::RocketWarhead& wh = ra2engine::rocket_warhead(blk, u.an.rocket_elite);
        const int damage = ra2engine::rocket_damage(blk, u.an.rocket_elite);
        const double lift = u.altitude * kFxLepPx;
        if (u.an.rocket_end == 2 || (u.alive && u.an.rocket_end == 0)) {
            // Shot down (killed this tick; World marks rocket_end next step): the ordinary death -- a random Explosion= entry, in the air.
            const auto* ex = fx_.explosions(pres_type_id(u));
            if (ex && !ex->empty()) {
                const uint32_t h = (static_cast<uint32_t>(u.cell_x * 131 + u.cell_y) * 2654435761u) ^
                                   (static_cast<uint32_t>(g_pres_tick) * 40503u);
                if (const FxAnimType* t = fx_.spawn((*ex)[(h >> 7) % ex->size()], u.fx, u.fy, level_at(w, u.fx, u.fy), lift)) {
                    snd_.play_at(t->report, u.fx, u.fy, "rocket shot down");
                    ++explosions_;
                }
            }
            return;
        }
        const std::vector<std::string>& splash = effects_.splash_list();
        if (!splash.empty() && on_water(u.fx, u.fy) && lift < 30.0) {
            const int ns = static_cast<int>(splash.size());
            const std::string& sname = splash[static_cast<std::size_t>(std::min(damage, ns * 35 - 1) / 35)];
            if (const FxAnimType* st = fx_.spawn(sname, u.fx, u.fy, level_at(w, u.fx, u.fy), 0.0)) {
                snd_.play_at(st->report, u.fx, u.fy, "splash");
                effects_.note_splash();
                return;
            }
        }
        std::vector<std::string> list;
        {
            const std::string raw = pres_clean(rules_ini_->get(pres_upper(wh.id), "AnimList"));
            std::string cur;
            for (char c : raw + ",") {
                if (c == ',') {
                    const std::string t = pres_clean(cur);
                    if (!t.empty()) list.push_back(t);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
        }
        if (list.empty()) return;
        const int ix = std::clamp(damage / 25, 0, static_cast<int>(list.size()) - 1);
        if (const FxAnimType* t = fx_.spawn(list[static_cast<std::size_t>(ix)], u.fx, u.fy, level_at(w, u.fx, u.fy), lift)) {
            snd_.play_at(t->report, u.fx, u.fy, "rocket explosion");
            ++explosions_;
        }
    }
    // W7-12-30 / W7-03-06 [RE: TechnoClass::Fire FUN_006fd210 -- the fired
    // WeaponTypeClass]: the weapon id this unit's current shot uses, by the
    // TechnoType slot rule (FUN_00711510): a Gunner (IFV) fires Weapon<n> of
    // its current mode, a WeaponCount type (Prism Tank) Weapon1, else the
    // active slot's Primary / Secondary; elite (rank 2) takes the Elite*
    // variant when the type defines one.
    std::string fired_weapon(const ra2engine::World& w, const ra2engine::Unit& u) {
        const std::string id = pres_type_id(u);
        const bool elite = w.veterancy_enabled() ? ra2engine::vet_rank(u.vet.veterancy) >= 2 : false;
        const int wc = std::atoi(pres_clean(rules_ini_->get(id, "WeaponCount")).c_str());
        const bool gunner = u.abil.gunner && !u.abil.ifv_weapons.empty();
        char key[64];
        const bool gatling = combat_rules_ && combat_rules_->section_flag(id,"IsGattling",false);
        const int slot = gunner ? std::max(0, u.ifv_current) : (wc > 0 && !gatling ? 0 : (u.w_active == 1 ? 1 : 0) + (gatling ? 2*u.gattling.stage(ra2engine::is_elite(u.vet.veterancy)) : 0));
        std::snprintf(key, sizeof key, "%s|%d|%d|%d", id.c_str(), slot, elite ? 1 : 0, (gunner || wc > 0) ? 1 : 0);
        auto it = fired_cache_.find(key);
        if (it != fired_cache_.end()) return it->second;
        auto get = [&](const std::string& k) { return pres_clean(rules_ini_->get(id, k)); };
        std::string wid, flh_key, flh_def;
        if (!gunner && combat_rules_) {
            wid = ra2engine::type_weapon_id(*combat_rules_,id,slot,elite);
            if(elite && wid.empty()) wid=ra2engine::type_weapon_id(*combat_rules_,id,slot,false);
        } else if (gunner || wc > 0) {
            const std::string n = std::to_string(slot + 1);
            if (elite) wid = get("EliteWeapon" + n);
            if (wid.empty()) wid = get("Weapon" + n);
        } else {
            const char* base = slot == 1 ? "Secondary" : "Primary";
            if (elite) wid = get(std::string("Elite") + base);
            if (wid.empty()) wid = get(base);
            if (wid.empty()) wid = get(slot == 1 ? "Weapon2" : "Weapon1");
        }
        return fired_cache_[key] = wid;
    }
    std::map<std::string, std::string> fired_cache_;
    // W7-12-15 [RE: TechnoTypeClass::LoadFromINI 0x711510 art block
    // @0x714EB7-0x715338]: the fired weapon's FLH -- PrimaryFireFLH /
    // SecondaryFireFLH, ElitePrimaryFireFLH (default PrimaryFireFLH) /
    // EliteSecondaryFireFLH (default SecondaryFireFLH), or Weapon%dFLH /
    // EliteWeapon%dFLH (default Weapon%dFLH) for a WeaponCount type. Leptons.
    std::array<double, 3> fire_flh(const ra2engine::World& w, const ra2engine::Unit& u) {
        const std::string id = pres_type_id(u);
        const std::string& img = u.image;
        const bool elite = w.veterancy_enabled() ? ra2engine::vet_rank(u.vet.veterancy) >= 2 : false;
        const int wc = std::atoi(pres_clean(rules_ini_->get(id, "WeaponCount")).c_str());
        const bool gunner = u.abil.gunner && !u.abil.ifv_weapons.empty();
        const bool gatling = combat_rules_ && combat_rules_->section_flag(id,"IsGattling",false);
        const int slot = gunner ? std::max(0, u.ifv_current) : (wc > 0 && !gatling ? 0 : (u.w_active == 1 ? 1 : 0) + (gatling ? 2*u.gattling.stage(ra2engine::is_elite(u.vet.veterancy)) : 0));
        char key[80];
        std::snprintf(key, sizeof key, "%s|%d|%d|%d", img.c_str(), slot, elite ? 1 : 0, (gunner || wc > 0) ? 1 : 0);
        auto it = flh_cache_.find(key);
        if (it != flh_cache_.end()) return it->second;
        std::array<double,3> f{0,0,0};
        if(w5_art_ini_) {
            const auto v=ra2engine::weapon_art_flh(Art::load(*w5_art_ini_),img,slot,elite,gunner || wc>0);
            for(int k=0;k<3;++k)f[k]=v[k];
        }
        return flh_cache_[key] = f;
    }
    std::map<std::string, std::array<double, 3>> flh_cache_;
    // The muzzle point of the current shot in screen terms (ox, lift px) --
    // voxel-render.md §6 projection [RE: GetFLH 0x6F3030 -- L negated on
    // every other burst shot; 0x6F32C0 -- the turret origin is
    // T(TurretOffset) along the BODY facing, then the turret yaw].
    void muzzle_point(const ra2engine::World& w, const ra2engine::Unit& u, double& ox, double& lift,
                      std::array<double,3>* source = nullptr) {
        std::array<double, 3> flh = fire_flh(w, u);
        const int burst = std::max(1, u.w6.burst);
        const int shot = (u.w6.burst_index + burst - 1) % burst;
        if (shot & 1) flh[1] = -flh[1];
        const bool turret = u.is_vehicle && !u.is_aircraft && u.w7.t.has_turret;
        double facing = u.is_vehicle ? ra2engine::unit_weapon_facing(u) : 0.0;
        if (!u.is_vehicle) {
            // Infantry block k: screen direction 45k degrees counter-clockwise
            // from screen-up (infantry-anim.md §4) -> a cell delta -> the
            // render u16 space (0 = cell north, clockwise).
            const double a = (u.facing & 7) * 3.14159265358979 / 4.0;
            const double sx = -std::sin(a), sy = -std::cos(a);
            facing = ra2engine::facing_u16_from_delta(sx / 60.0 + sy / 30.0, sy / 30.0 - sx / 60.0);
        }
        const double yaw = ra2engine::index_to_yaw(ra2engine::facing_to_index(facing, ra2engine::kVehicleFacings),
                                                   ra2engine::kVehicleFacings);
        double mx = (flh[0] * std::cos(yaw) - flh[1] * std::sin(yaw)) / 256.0;
        double my = (flh[0] * std::sin(yaw) + flh[1] * std::cos(yaw)) / 256.0;
        if (turret) {
            const std::string to = w5_art_ini_ ? pres_clean(w5_art_ini_->get(u.image, "TurretOffset")) : std::string();
            if (!to.empty()) {
                const double byaw = ra2engine::index_to_yaw(
                    ra2engine::facing_to_index(u.body_facing, ra2engine::kVehicleFacings), ra2engine::kVehicleFacings);
                const double f = std::atof(to.c_str()) / 256.0;
                mx += f * std::cos(byaw);
                my += f * std::sin(byaw);
            }
        }
        const auto point=ra2engine::terrain_tilt(pres_unit_ramp(u)).apply(mx,my,flh[2]/256.0);
        if(source) *source={u.fx*256.0+point[0]*256.0,u.fy*256.0-point[1]*256.0,
            pres_unit_fx_level(w,u)*104.0+point[2]*256.0+(u.is_aircraft ? u.altitude : 0.0)};
        ox = (point[0] + point[1]) * 30.0;
        lift = -((point[0] - point[1]) * 15.0) + point[2] * 36.74;
        if (u.is_aircraft && u.altitude > 0.0) lift += ra2engine::altitude_to_screen_dy(u.altitude);
    }
    // Muzzle Anim= at the firing point; an instant (no flying projectile)
    // weapon also lands its impact on the target now. W7-12-30: everything
    // comes from the weapon that fired.
    void fire_fx(const ra2engine::World& w, const ra2engine::Unit& u) {
        const FxWeapon* wp = &fx_.weapon_fx(fired_weapon(w, u));
        if (wp->id.empty()) wp = fx_.weapon(u.image);
        if (!wp) return;
        double ox = 0, lift = 0;
        std::array<double,3> source{};
        muzzle_point(w, u, ox, lift, &source);
        auto shot_target = [&](double& x,double& y,int& level,double& target_lift) {
            if (target_point(w,u.target,u.target_building,x,y,level,target_lift)) return true;
            if (u.attack_cell_x < 0 || u.attack_cell_y < 0) return false;
            x=u.attack_cell_x;y=u.attack_cell_y;
            level=w.cell_level(u.attack_cell_x,u.attack_cell_y);target_lift=0;
            return true;
        };
        if (!wp->muzzle.empty()) {
            std::string name = wp->muzzle.front();
            if (wp->muzzle.size() >= 8) {
                if (u.is_vehicle) {
                    // W7-12-29 [RE: FUN_006fd210 muzzle block -- Anim[(((d >> 12)
                    // + 1 >> 1 & 7) + 1) & 7], d = the turret facing for a
                    // Turret=yes type, else the body (UnitClass 0x73fd90)]: the
                    // lists are named by screen compass, map-north = "NE".
                    const double d = ra2engine::unit_weapon_facing(u);
                    const int k = ((((static_cast<int>(d) & 0xFFFF) >> 12) + 1) >> 1 & 7);
                    name = wp->muzzle[static_cast<std::size_t>((k + 1) & 7)];
                } else {
                    // Infantry: the 8 facing-ordered anims N,NE,E,SE,S,SW,W,NW;
                    // Unit::facing is 0=N counter-clockwise (infantry-anim.md §4).
                    name = wp->muzzle[(8 - (u.facing & 7)) % 8];
                }
            }
            // W7-12-38: at the weapon's FLH (infantry too), [RE-FALLBACK: the
            // anim is not attached to the firer (SetOwnerObject 0x424ad0)].
            fx_.spawn(name, u.fx, u.fy, pres_unit_fx_level(w, u), lift, ox);   // w8fix-layer: deck
        }
        // W6-04-24: a slot-1 shot draws the Secondary's beam (Boris's Flare);
        // W7-12-30: the fired weapon's own beam flags.
        if (wp->bolt || wp->laser || wp->disk || wp->rad_beam || wp->line) {
            double tx = 0, ty = 0, tl = 0;
            int tlev = 0;
            if (shot_target(tx,ty,tlev,tl)) {
                fx_.add_beam(u.fx, u.fy, pres_unit_fx_level(w, u), lift, tx, ty, tlev, tl, *wp,
                             house_color(w, u.house), ox);
                // W7-12-31 [RE: EBolt::Fire 0x4c2980 -- ParticleSystem([General]
                // DefaultSparkSystem=SparkSys) at the bolt end].
                if (wp->bolt) {
                    static std::string spark;
                    if (spark.empty() && rules_ini_) spark = pres_clean(rules_ini_->get("General", "DefaultSparkSystem"));
                    if (!spark.empty())
                        effects_.spawn_system(spark, tx * 256.0, ty * 256.0, tlev * 104.0 + tl / kFxLepPx);
                }
            }
        }
        if (!u.w_proj.flies()) {
            // W3-FX: an instant shot whose projectile art has a line trail.
            {
                double tx = 0, ty = 0, tl = 0;
                int tlev = 0;
                if (shot_target(tx,ty,tlev,tl))
                    effects_.instant_line(wp->id.empty() ? effects_.unit_weapon(u.image, 0) : wp->id, source[0],
                                          source[1], source[2], tx * 256.0, ty * 256.0, tlev * 104.0 + 40.0);
            }
            const auto& us = w.units();
            if (u.target >= 0 && u.target < static_cast<int>(us.size())) {
                const ra2engine::Unit& t = us[u.target];
                impact(u, u.w_damage, t.fx, t.fy, pres_unit_fx_level(w, t), t.is_vehicle ? 10.0 : 6.0);
            } else if (u.target_building >= 0 &&
                       u.target_building < static_cast<int>(w.buildings().size())) {
                const ra2engine::Building& b = w.buildings()[u.target_building];
                impact(u, u.w_damage, b.cx + (b.fw - 1) / 2.0, b.cy + (b.fh - 1) / 2.0, building_level(w, b), 20.0);
            } else if (u.attack_cell_x >= 0 && u.attack_cell_y >= 0) {
                impact(u,u.w_damage,u.attack_cell_x,u.attack_cell_y,
                    w.cell_level(u.attack_cell_x,u.attack_cell_y),0);
            }
        }
    }
    // W7-12-07 [RE: BulletClass shrapnel FUN_0046a230 -- CreateLaser
    // 0x6fc6d0 / CreateEBolt 0x6fc920 per fragment, from the impact point to
    // the fragment's target]: the Prism split lasers / elite Tesla bounces.
    // W7-12-11: the sinking hulls' wakes (every 4th frame, +-170 leptons) and
    // their end after 400 leptons (80 frames).
    void sinker_tick() {
        const std::string wake = rules_ini_ ? pres_clean(rules_ini_->get("General", "Wake")) : std::string();
        for (auto it = g_pres_sinkers.begin(); it != g_pres_sinkers.end();) {
            const long age = g_pres_tick - it->start;
            if (age * 5 > 400) { it = g_pres_sinkers.erase(it); continue; }
            if ((g_pres_tick & 3) == 0 && !wake.empty()) {
                sink_rng_ = sink_rng_ * 1664525u + 1013904223u;
                const double dx = (static_cast<int>((sink_rng_ >> 8) % 341) - 170) / 256.0;
                sink_rng_ = sink_rng_ * 1664525u + 1013904223u;
                const double dy = (static_cast<int>((sink_rng_ >> 8) % 341) - 170) / 256.0;
                fx_.spawn(wake, it->fx + dx, it->fy + dy, it->level, 0.0);
            }
            ++it;
        }
    }
    std::uint32_t sink_rng_ = 0x51C4u;   // render-only RNG
    void fragment_fx(const ra2engine::World& w) {
        const auto& fe = w.frag_events();   // cleared by GameScene after every logic tick
        for (std::size_t i = 0; i < fe.size(); ++i) {
            const auto& e = fe[i];
            ++w5_counts_["fragment event"];
            if (e.attacker < 0 || e.attacker >= static_cast<int>(w.units().size())) continue;
            const ra2engine::Unit& a = w.units()[static_cast<std::size_t>(e.attacker)];
            const FxWeapon& main_w = fx_.weapon_fx(fired_weapon(w, a));
            if (main_w.shrapnel.empty()) continue;
            const FxWeapon& fw = fx_.weapon_fx(main_w.shrapnel);
            if (!(fw.bolt || fw.laser || fw.rad_beam || fw.line)) continue;
            double tx = 0, ty = 0, tl = 0;
            int tlev = 0;
            if (!target_point(w, e.target, -1, tx, ty, tlev, tl)) continue;
            if (!w.object_visible_at(g_local_house, static_cast<int>(std::lround(e.x0)), static_cast<int>(std::lround(e.y0))))
                continue;
            fx_.add_beam(e.x0, e.y0, e.level0, 10.0, tx, ty, tlev, tl, fw, house_color(w, a.house));
            ++w5_counts_["fragment beam"];
        }
    }
    bool target_point(const ra2engine::World& w, int tu, int tb, double& x, double& y, int& level,
                      double& lift) const {
        const auto& us = w.units();
        if (tu >= 0 && tu < static_cast<int>(us.size())) {
            const ra2engine::Unit& t = us[static_cast<std::size_t>(tu)];
            x = t.fx; y = t.fy; level = pres_unit_fx_level(w, t);   // w8fix-layer: a deck target
            lift = t.is_vehicle ? 10.0 : 8.0;
            if(t.is_aircraft && t.altitude>0)lift+=ra2engine::altitude_to_screen_dy(t.altitude);
            else if(ra2engine::unit_parachuting(t))lift+=ra2engine::altitude_to_screen_dy(t.an.para_alt);
            return true;
        }
        if (tb >= 0 && tb < static_cast<int>(w.buildings().size())) {
            const ra2engine::Building& b = w.buildings()[static_cast<std::size_t>(tb)];
            x = b.cx + (b.fw - 1) / 2.0; y = b.cy + (b.fh - 1) / 2.0; level = building_level(w, b);
            lift = 20.0;
            return true;
        }
        return false;
    }
    SDL_Color house_color(const ra2engine::World& w, int house) const {
        if (!unit_pal_) return SDL_Color{255, 0, 255, 255};
        const ra2assets::Rgba c = w.house_palette(*unit_pal_, house)[24];
        return SDL_Color{c.r, c.g, c.b, 255};
    }
    const ra2assets::Palette* unit_pal_ = nullptr;
    // Building death: the type's Explosion= anims scattered over the
    // foundation [RE-FALLBACK: RE-P5 open -- count/placement; one per
    // foundation cell, cycling the list], each with its Report=.
    void building_explode(const ra2engine::World& w, const ra2engine::Building& b, const std::string& id) {
        const auto* ex = fx_.explosions(id);
        if (!ex || ex->empty()) return;
        int k = 0;
        for (int j = 0; j < b.fh; ++j)
            for (int i = 0; i < b.fw; ++i, ++k) {
                const std::string& name = (*ex)[k % ex->size()];
                if (const FxAnimType* t = fx_.spawn(name, b.cx + i, b.cy + j, building_level(w, b), 10.0 + 6.0 * (k % 3)))
                    if (k == 0) snd_.play_at(t->report, b.cx + i, b.cy + j, "building explosion");
            }
    }

    // [G-81] One type's voice / death / Report / MoveSound / CrushSound keys,
    // read from the type's own rules section [RE: FUN_00711510] and stored
    // under its RULES id (W7-12-27: ENGINEER / SENGINEER / YENGINEER share an
    // image but not their lines). Idempotent.
    void add_voices(const Rules& rules, const std::string& id_in) {
        const std::string id = pres_upper(id_in);
        if (id.empty() || !audio_) return;
        auto found = audio_->voices.find(id);
        if (found != audio_->voices.end() && found->second.registered) return;
        UnitVoices v;
        v.registered = true;
        v.select = pres_clean(rules.voice_select(id));
        v.move = pres_clean(rules.voice_move(id));
        v.attack = pres_clean(rules.voice_attack(id));
        v.die = pres_clean(rules.die_sound(id));
        v.report = pres_clean(rules.weapon_report(pres_clean(rules.primary(id))));
        if (v.report.empty()) {
            // WeaponCount/Weapon1 units (SREF etc.) carry no Primary=.
            const std::string w1 = pres_clean(rules_ini_->get(id, "Weapon1"));
            if (!w1.empty()) v.report = pres_clean(rules.weapon_report(w1));
        }
        // Report= may list several sounds ("Report=A,B"): the game picks one
        // per shot (W2-GA-24; uniform pick on the audio-local RNG, the same
        // RandomRanged rule the event variants use) -- see pick_report.
        // W2-GA-25: MoveSound (the vehicle move-start one-shot, e.g.
        // GrizzlyTankMoveStart: Control=random predelay) and CrushSound
        // (TankCrush on crushers, InfantrySquish on crushable infantry).
        v.move_sound = pres_clean(rules_ini_->get(id, "MoveSound"));
        v.crush = pres_clean(rules_ini_->get(id, "CrushSound"));
        add_w5_voices(id, v);   // W5-E (pres_audio_events.inc)
        audio_->voices[id] = v;
        for (const std::string& k : {v.select, v.move, v.attack, v.die, v.move_sound, v.crush}) snd_.ensure_sound(k);
        std::stringstream rs(v.report);
        for (std::string tok; std::getline(rs, tok, ',');) snd_.ensure_sound(pres_clean(tok));
    }
    // The voice set of `u`'s type, registered on first sight (W7-09-01: map
    // civilians, cows, Einstein, civilian cars and any runtime-created type).
    const UnitVoices& voices_of(const ra2engine::Unit& u) {
        const std::string& id = pres_type_id(u);
        auto it = audio_->voices.find(id);
        if (it != audio_->voices.end() && it->second.registered) return it->second;
        if (rules_) add_voices(*rules_, id);
        return audio_->voices[pres_upper(id)];
    }
public:
    // --play evidence (W7-12-03 / W7-09-01): a registered voice key of a type.
    std::string voice_key(const std::string& id, const std::string& field) {
        if (!audio_) return std::string();
        if (rules_) add_voices(*rules_, id);
        const UnitVoices& v = audio_->voices[pres_upper(id)];
        if (field == "select") return v.select;
        if (field == "die") return v.die;
        if (field == "deploy") return v.deploy_snd;
        if (field == "feedback") return v.feedback;
        if (field == "move_sound") return v.move_sound;
        if (field == "crush") return v.crush;
        if (field == "report") return v.report;
        return std::string();
    }
private:
    const Rules* rules_ = nullptr;

    std::string pick_report(const std::string& list) {
        if (list.find(',') == std::string::npos) return list;
        std::vector<std::string> opts;
        std::stringstream rs(list);
        for (std::string tok; std::getline(rs, tok, ',');)
            if (!pres_clean(tok).empty()) opts.push_back(pres_clean(tok));
        if (opts.empty()) return std::string();
        report_rng_ = report_rng_ * 1664525u + 1013904223u;
        return opts[(report_rng_ >> 8) % opts.size()];
    }
#include "pres_audio_events.inc"   // W5-E: the remaining unit/world sounds and EVA lines
    std::uint32_t report_rng_ = 0x51A7u;
    std::vector<uint8_t> moving_prev_;
    std::vector<int> uhp_prev_;
    std::string impact_water_;
    const ra2engine::World* world_ = nullptr;
    bool on_water(double fx, double fy) const {
        if (!world_) return false;
        const auto& wg = world_->grid_for(ra2engine::MovementDomain::Water);
        const int x = static_cast<int>(std::lround(fx)), y = static_cast<int>(std::lround(fy));
        if (wg.empty() || x < 0 || y < 0 || x >= world_->grid_w() || y >= world_->grid_h()) return false;
        return wg[static_cast<std::size_t>(y) * world_->grid_w() + x] != 0;
    }

    double dist_of(double fx, double fy) const {
        // [RE: audio-playback.md "Decisions"] Chebyshev max(dx, 2|dy|) px from
        // the tactical view centre, /60 -> cells.
        const double vcx = vw_ / 2.0 - cam_.x, vcy = vh_ / 2.0 - cam_.y;
        const double sx = (fx - fy) * 30.0 - vcx, sy = (fx + fy) * 15.0 - vcy;
        return std::max(std::abs(sx), 2.0 * std::abs(sy)) / 60.0;
    }

    // [G-80] Production lines [RE: presentation.md §1.3]: Building/Training
    // when production starts (FUN_006aa390, every queue: W7-12-01 covers the
    // Naval and Aircraft queues too), ConstructionComplete when a structure
    // finishes (FUN_006a81c0). UnitReady and Canceled are NOT slot edges any
    // more (W7-12-01): the ready slot lives for zero frames (tick_all sets it,
    // econ_launch_ready_units launches it in the same step), so UnitReady is
    // spoken from the launch list (note_launches) and Canceled from the
    // player's abandon click (note_eva). InsufficientFunds (FUN_004f8350):
    // local house, money < 100 with production running, every SpeakDelay.
    void production_eva(const ra2engine::World& w, const ra2engine::ProductionQueues& q, double credits) {
        (void)w;
        bool producing = false;
        for (int c = 0; c < ra2engine::kQueueCats; ++c) {
            const ra2engine::QueueCat cat = static_cast<ra2engine::QueueCat>(c);
            const ra2engine::QueueSlot& s = q.state(cat).slot;
            Slot& p = slots_[c];
            const bool structure = c < 2;
            if (p.type < 0 && s.type >= 0) note_eva(structure ? "EVA_Building" : "EVA_Training");
            if (!p.ready && s.ready && structure) snd_.eva("EVA_ConstructionComplete");
            if (s.type >= 0 && !s.ready) producing = true;
            p.type = s.type;
            p.ready = s.ready;
        }
        if (producing && credits < 100.0 && frame_ >= next_funds_frame_) {
            snd_.eva("EVA_InsufficientFunds");
            next_funds_frame_ = frame_ + speak_delay_frames_;
        }
    }
public:
    // W7-12-01 [RE: FUN_004faf00 @4FB45F -- a produced unit leaving its
    // factory, local house only, gated by RadarEvent::Create(6 UnitProduced;
    // 2 cells, 200 frames)]: EVA_UnitReady per launched unit. GameScene hands
    // over World::take_econ_launches() every logic tick.
    void note_launches(const ra2engine::World& w, const std::vector<std::pair<int, int>>& launched) {
        for (const auto& hl : launched) {
            if (hl.first != g_local_house) continue;
            if (hl.second < 0 || hl.second >= static_cast<int>(w.units().size())) continue;
            const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(hl.second)];
            if (radar_event(6, u.fx, u.fy)) snd_.eva("EVA_UnitReady");
        }
    }
private:

    // [G-80] Low power once per low-power episode (drain > output edge,
    // construction.md §8), only while the house owns a construction yard
    // [RE: presentation.md §1.3 -- 3-entry Rules list, probably BuildConst: Medium].
    void power_eva(const ra2engine::World& w, const BuildingAssets& ba) {
        if (!ba.ok) return;
        const ra2engine::PowerTotals pt = ra2engine::power_totals(w.buildings(), ba.roster, g_local_house);
        const bool low = pt.drain > pt.output;
        bool yard = false;
        for (const ra2engine::Building& b : w.buildings())
            if (b.alive && b.house == g_local_house && b.type >= 0 && b.type < static_cast<int>(ba.roster.size()) &&
                ba.roster[b.type].conyard)
                yard = true;
        if (low && !low_power_ && yard) snd_.eva("EVA_LowPower");
        g_pres_low_power = low;
        low_power_ = low;
    }

    // [G-80] Building-level lines + sounds: base under attack (throttled),
    // building lost/sold/captured, half-strength damage sound, die sound,
    // new construction options.
    void building_events(const ra2engine::World& w, const BuildingAssets& ba) {
        const auto& bs = w.buildings();
        if (bhp_.size() < bs.size()) bhp_.resize(bs.size(), Bld{});
        int buildable = 0;
        for (std::size_t i = 0; i < bs.size(); ++i) {
            const ra2engine::Building& b = bs[i];
            Bld& p = bhp_[i];
            const double cx = b.cx + (b.fw - 1) / 2.0, cy = b.cy + (b.fh - 1) / 2.0;
            if (!p.seen) {
                p = Bld{true, b.hp, b.house, b.state == ra2engine::Building::State::Dead, b.selling};
                continue;
            }
            const bool dead = b.state == ra2engine::Building::State::Dead;
            // W2-GA-26 [RE: FUN_004f9200 @4f93ce]: an ALLIED (non-local)
            // house's building attacked -> RadarEvent 16 AllyBaseAttacked ->
            // EVA_OurAllyIsUnderAttack plus the siren.
            if (b.house != g_local_house && b.house == p.house && b.house >= 0 && w.are_allied(g_local_house, b.house) &&
                b.hp < p.hp && !dead && radar_event(16, cx, cy)) {
                snd_.eva("EVA_OurAllyIsUnderAttack");
                snd_.play(base_attack_siren_, 0.0, "ally under attack");
            }
            if (b.house == g_local_house && p.house == g_local_house && b.hp < p.hp && !dead) {
                // [RE: presentation.md §1.2 FUN_004f9200] throttled by
                // RadarEvent 3 BaseAttacked (8 cells, alive 400 frames); the
                // same event is the radar flash. Plays BaseUnderAttackSound.
                const bool miner=miner_role_alerts_ && b.type>=0 && b.type<int(w.btypes().size()) && w.btypes()[b.type].slave_count>0;
                if (radar_event(miner?4:3, cx, cy)) {
                    // Requested harvesting-role alert; this does not change
                    // the deployed manager's authoritative building type.
                    snd_.eva(ra2engine::attacked_building_eva(miner));
                    if(!miner)snd_.play(base_attack_siren_, 0.0, "base under attack");
                }
                const double half = 0.5 * b.max_hp;
                if (p.hp > half && b.hp <= half) snd_.play_at(building_damage_, cx, cy, "half strength");
            }
            if (dead && !p.dead && !b.sold) {
                snd_.play_at(building_die_, cx, cy, "building die");
                if (ba.ok && b.type >= 0 && b.type < static_cast<int>(ba.roster.size()) &&
                    pres_building_seen(w, b)) {
                    building_explode(w, b, ba.roster[b.type].id);
                    // W3-FX: building debris (the same TechnoClass death path).
                    const ra2assets::MapCell* mc = w.map().at(b.cx, b.cy);
                    effects_.spawn_debris(ba.roster[b.type].id, (b.cx + (b.fw - 1) / 2.0) * 256.0,
                                          (b.cy + (b.fh - 1) / 2.0) * 256.0, (mc ? mc->level : 0) * 104.0, false);
                }
                // W3-B: DestroyAnim= ruins (landmarks) [RE: TechnoType read
                // FUN_00711510 "DestroyAnim"], drawn through the owner's convert.
                if (ba.ok && b.type >= 0 && b.type < static_cast<int>(ba.roster.size())) {
                    const std::string& da = fx_.building_rules(pres_upper(ba.roster[b.type].id)).destroy_anim;
                    // W4-FIX-POS [RE: 0x441C2C-0x441C9A new AnimClass(DestroyAnim,
                    // vtbl+0xAC, ...)]: the ruin shares the building's draw point
                    // (origin-cell top vertex = cell (cx-0.5, cy-0.5)'s centre) and
                    // level; ruin SHPs share the building canvas.
                    std::shared_ptr<ra2assets::Palette> own;   // W4: art Palette= (lib) for the ruin
                    if (static_cast<std::size_t>(b.type) < ba.art.size()) own = ba.art[static_cast<std::size_t>(b.type)].own_pal;
                    if (!da.empty()) fx_.spawn_hold(da, b.cx - 0.5, b.cy - 0.5, building_level(w, b), b.house, own);
                }
            }
            if (b.selling && !p.selling && b.house == g_local_house && !w.packing_conyard(static_cast<int>(i))) {
                snd_.play_at(sell_sound_, cx, cy, "sell");
                snd_.eva("EVA_StructureSold");
            }
            // (W5-E: capture lines moved to pres_audio_events.inc w5_tick.)
            p.hp = b.hp;
            p.house = b.house;
            p.dead = dead;
            p.selling = b.selling;
        }
        // New construction options: the count of roster types whose
        // prerequisites the player now meets grew.
        if (ba.ok) {
            for (int t = 0; t < static_cast<int>(ba.roster.size()); ++t)
                if (ba.roster[t].tech_level > 0 &&
                    ra2engine::prerequisites_met(ba.roster[t].prerequisite, bs, ba.roster, 0, ba.groups))
                    ++buildable;
            if (buildable_ >= 0 && buildable > buildable_) snd_.eva("EVA_NewConstructionOptions");
            buildable_ = buildable;
        }
    }

public:
    // Base-defense shots (units consume the same event stream separately): the weapon's Report=
    // sound and muzzle Anim= at the building's centre, when visible.
    // W3-B: GameScene drains the queue once per frame; the events were
    // already handled tick by tick (defense_fire_tick from logic_tick).
    void defense_fire(const ra2engine::World&, const std::vector<ra2engine::World::FireEvent>&) {
        fire_seen_ = 0;
    }
    void defense_fire_tick(const ra2engine::World& w, const std::vector<ra2engine::World::FireEvent>& evs) {
        const auto& bs = w.buildings();
        building_shot_loops_.resize(bs.size());
        for (const auto& e : evs) {
            if (e.building < 0 || e.building >= static_cast<int>(bs.size()) || e.weapon.empty()) continue;
            const ra2engine::Building& b = bs[e.building];
            const double fx = b.cx + (b.fw - 1) / 2.0, fy = b.cy + (b.fh - 1) / 2.0;
            const std::string report=pick_report(fx_.weapon_report_list(e.weapon));
            building_shot_loops_[e.building].shot(snd_,report,fx,fy);
            // Only changes of a staged weapon's report/start result are
            // recorded, not every shot or MIX lookup. A silent phone report
            // can then distinguish missing events/assets from mixer limits.
            const bool staged=b.type>=0 && b.type<static_cast<int>(w.btypes().size()) && w.btypes()[b.type].gattling.enabled();
            if(staged && ra2engine::diag::state().enabled.load()) {
                building_audio_trace_.resize(bs.size());
                const auto& loop=building_shot_loops_[e.building];
                const std::string state=e.weapon+"|"+report+"|"+(loop.handle?"playing":"unavailable");
                if(building_audio_trace_[e.building]!=state) {
                    building_audio_trace_[e.building]=state;
                    const auto* sound=audio_ && audio_->mixer?audio_->mixer->sound(report):nullptr;
                    ra2engine::diag::note("WEAPON_AUDIO building=%d type=%s weapon=%s slot=%d charge=%d report=%s handle=%u enabled=%d variants=%d loop=%d",
                        e.building,w.btypes()[b.type].id.c_str(),e.weapon.c_str(),e.slot,b.gattling_charge,
                        report.c_str(),loop.handle,audio_ && audio_->enabled,sound?int(sound->variants.size()):0,snd_.sound_loops(report));
                }
            }
            if (b.house != g_local_house && !w.object_visible_at(g_local_house, b.cx, b.cy)) continue;
            // W2-GA-15: Tesla Coil bolts / Prism Tower beams from the tower top.
            const FxWeapon& bw = fx_.weapon_def(e.weapon);
            if (bw.bolt || bw.laser) {
                double tx = 0, ty = 0, tl = 0;
                int tlev = 0;
                bool target=target_point(w,e.target_unit,e.target_building,tx,ty,tlev,tl);
                if(!target && e.cell_x>=0 && e.cell_y>=0) {
                    tx=e.cell_x;ty=e.cell_y;tlev=w.cell_level(e.cell_x,e.cell_y);tl=0;target=true;
                }
                if (target)
                    fx_.add_beam(fx, fy, building_level(w, b), 60.0, tx, ty, tlev, tl, bw, house_color(w, b.house));
            }
            const std::vector<std::string> m = fx_.weapon_muzzle(e.weapon);
            if (!m.empty()) {
                const int k=((((static_cast<int>(b.def_turret_facing)&0xFFFF)>>12)+1)>>1)&7;
                const std::size_t ix=m.size()>=8 ? static_cast<std::size_t>((k+1)&7) : 0;
                // Building art FLH uses the same turret-space transform as units.
                ra2engine::Unit muzzle;
                muzzle.fx=fx; muzzle.fy=fy; muzzle.is_vehicle=true;
                muzzle.image = combat_rules_ && b.type >= 0 && b.type < static_cast<int>(w.btypes().size()) ?
                    w.btypes()[b.type].image : std::string();
                if (muzzle.image.empty() && b.type >= 0 && b.type < static_cast<int>(w.btypes().size()))
                    muzzle.image=w.btypes()[b.type].id;
                muzzle.tg.t.rules_id = b.type >= 0 && b.type < static_cast<int>(w.btypes().size()) ? w.btypes()[b.type].id : muzzle.image;
                muzzle.w7.t.has_turret = b.type >= 0 && b.type < static_cast<int>(w.btypes().size()) && w.btypes()[b.type].turret;
                muzzle.body_facing=muzzle.turret_facing=b.def_turret_facing;
                muzzle.w_active=e.slot;
                muzzle.w6.burst_index=b.w6.burst_index;
                if(b.type>=0 && b.type<static_cast<int>(w.btypes().size())) {
                    muzzle.gattling=w.btypes()[b.type].gattling;
                    muzzle.gattling.charge=b.gattling_charge;
                }
                if(b.type>=0 && b.type<static_cast<int>(w.btypes().size()) &&
                    w.btypes()[b.type].tank_bunker && !b.occupants.empty() &&
                    b.occupants.front()>=0 && b.occupants.front()<static_cast<int>(w.units().size())) {
                    muzzle=w.units()[b.occupants.front()];
                    muzzle.fx=fx;muzzle.fy=fy;
                    if(e.target_unit>=0 && e.target_unit<static_cast<int>(w.units().size()))
                        muzzle.w_active=std::max(0,ra2engine::unit_weapon_for_unit(muzzle,w.units()[e.target_unit]));
                }
                double ox=0,lift=0;
                muzzle_point(w,muzzle,ox,lift);
                fx_.spawn(m[ix], fx, fy, building_level(w, b), lift, ox);
            }
        }
        for (std::size_t i=0;i<bs.size();++i) {
            const auto& b=bs[i];
            const bool staged=b.type>=0 && b.type<static_cast<int>(w.btypes().size()) && w.btypes()[b.type].gattling.enabled();
            const auto old_handle=building_shot_loops_[i].handle;
            const bool firing=ra2engine::building_weapon_loop_active(
                b.alive && b.state==ra2engine::Building::State::Idle && !b.selling && !b.power_off,
                b.def_offline,b.def_target>=0 || b.w6.def_target_building>=0,b.attack_order_kind,
                b.def_fire_flash,staged,b.gattling_charge);
            building_shot_loops_[i].update(snd_,firing,
                b.cx+(b.fw-1)/2.0,b.cy+(b.fh-1)/2.0);
            if(staged && old_handle && !building_shot_loops_[i].handle) {
                if(i<building_audio_trace_.size())building_audio_trace_[i].clear();
                ra2engine::diag::note("WEAPON_AUDIO stop building=%d firing=%d offline=%d order=%d target=%d building_target=%d charge=%d",
                    int(i),firing,b.def_offline,b.attack_order_kind,b.def_target,b.w6.def_target_building,b.gattling_charge);
            }
        }
    }

private:
    // W3-B [RE: InfantryClass::ReceiveDamage FUN_00517db5 InfDeath switch;
    // sequence-finished FUN_00520810 Die1-5 case]: an armed infantry whose
    // kill picked a death ANIM (World set ia.vanish) gets it at the body:
    // 3 InfantryExplode, 4 FlamingInfantry, 5 ELECTRO (AnimTypes[1] = rules
    // [Animations] second entry), 6 InfantryHeadPop, 7 InfantryNuked,
    // 8 InfantryVirus, 10 InfantryBrute (0: nothing). A body that played
    // Die1/Die2 leaves a corpse when it tombstones: a random entry of the
    // type's DeadBodies= or, for humans, [General] DeadBodies=.
    std::vector<char> alive_prev_;
    uint32_t corpse_rng_ = 0xC0FFEEu;
public:
    int corpses_ = 0, death_anims_ = 0, explosions_ = 0;   // W3-B --play probe counters
private:
    void infantry_death_fx(const ra2engine::World& w, const ra2engine::Unit& u, bool was_living, bool was_alive) {
        if (!u.ia.armed || u.is_vehicle || u.is_aircraft || u.cargo_of >= 0) return;
        if (!w.object_visible_at(g_local_house, u.cell_x, u.cell_y) && u.house != g_local_house) return;
        // w8fix-layer W8-02-03: a soldier dying on a deck dies on the deck.
        const int lvl = level_at(w, u.fx, u.fy) + pres_unit_lift_levels(w, u);
        if (u.die_time >= 0.0 && was_living && u.ia.vanish) {
            static const char* kKey[11] = {nullptr, nullptr, nullptr, "InfantryExplode", "FlamingInfantry",
                                           nullptr, "InfantryHeadPop", "InfantryNuked", "InfantryVirus",
                                           nullptr, "InfantryBrute"};
            const int d = u.ia.death;
            std::string anim;
            if (d == 5) anim = "ELECTRO";
            else if (d >= 0 && d <= 10 && kKey[d]) {
                anim = pres_clean(rules_ini_->get("AudioVisual", kKey[d]));
                if (anim.empty()) anim = pres_clean(rules_ini_->get("General", kKey[d]));
            }
            if (!anim.empty())
                if (const FxAnimType* t = fx_.spawn(anim, u.fx, u.fy, lvl, 0.0, 0.0, u.house)) {
                    snd_.play_at(t->report, u.fx, u.fy, "infantry death anim");
                    ++death_anims_;
                }
            return;
        }
        // W7-04-25: a crushed soldier leaves no corpse. W7-12-35 [RE:
        // 0x520810 -- WetDie1/2 (0x14/0x15) and AirDeathFinish (0x24) UnInit
        // with no DeadBodies; only Die1-5 leave a corpse].
        const std::size_t ui = static_cast<std::size_t>(&u - w.units().data());
        if (ui < crushed_.size() && crushed_[ui]) return;
        if (u.ia.seq == ra2engine::kSqWetDie1 || u.ia.seq == ra2engine::kSqWetDie2 ||
            u.ia.seq == ra2engine::kSqAirDeathStart || u.ia.seq == ra2engine::kSqAirDeathFalling ||
            u.ia.seq == ra2engine::kSqAirDeathFinish)
            return;
        if (!u.alive && was_alive && u.die_time >= 0.0 && !u.ia.vanish) {
            std::string list = pres_clean(rules_ini_->get(pres_type_id(u), "DeadBodies"));   // W7-12-28
            const ra2engine::InfTable* it = w.inf_table(u.image);
            if (list.empty() && !(it && it->not_human))
                list = pres_clean(rules_ini_->get("General", "DeadBodies"));
            std::vector<std::string> v;
            std::stringstream ss(list);
            std::string tok;
            while (std::getline(ss, tok, ',')) { tok = pres_clean(tok); if (!tok.empty()) v.push_back(tok); }
            if (v.empty()) return;
            corpse_rng_ = corpse_rng_ * 1664525u + 1013904223u;
            if (fx_.spawn(v[(corpse_rng_ >> 8) % v.size()], u.fx, u.fy, lvl, 0.0)) ++corpses_;
        }
    }
    std::vector<std::uint8_t> crushed_, crash_pending_;   // W7-04-25 / W7-12-12
    // The death Explosion= of a vehicle at its point [RE: FUN_007376C0]:
    //  * W7-12-43: Explodes=yes with ammo left (Ammo -1 = unlimited) takes
    //    the LAST entry, else a uniform pick;
    //  * W7-12-14 [RE: UnitClass::ReceiveDamage death 0x736DC5 @0x736E3F]: a
    //    unit dying low (< 11 leptons) on a Water cell without sinking shows
    //    [General] Wake plus the LAST SplashList anim (z + 5) instead, and no
    //    Explosion=;
    //  then Min/MaxDebris (none low over water).
    void vehicle_death_fx(const ra2engine::World& w, const ra2engine::Unit& u, std::size_t i) {
        const double height = u.is_aircraft ? u.altitude : 0.0;
        const bool water_death = !u.an.sinking && height < 11.0 && w.is_water_cell(u.cell_x, u.cell_y) &&
                                 u.on_deck == 0;
        if (water_death) {
            const std::vector<std::string>& splash = effects_.splash_list();
            const std::string wake = pres_clean(rules_ini_->get("General", "Wake"));
            if (!wake.empty()) fx_.spawn(wake, u.fx, u.fy, u.level, 0.0);
            if (!splash.empty())
                if (const FxAnimType* t = fx_.spawn(splash.back(), u.fx, u.fy, u.level, 5.0 * kFxLepPx)) {
                    snd_.play_at(t->report, u.fx, u.fy, "water explosion");
                    ++explosions_;
                    ++w5_counts_["water explosion"];
                }
        } else {
            const auto* ex = fx_.explosions(pres_type_id(u));
            if (ex && !ex->empty()) {
                const uint32_t h = (static_cast<uint32_t>(i) * 2654435761u) ^ (static_cast<uint32_t>(g_pres_tick) * 40503u);
                std::size_t pick = (h >> 7) % ex->size();
                const std::string id = pres_type_id(u);
                const std::string ex_key = pres_lower(pres_clean(rules_ini_->get(id, "Explodes")));
                const std::string ammo = pres_clean(rules_ini_->get(id, "Ammo"));
                const bool has_ammo = ammo.empty() || std::atoi(ammo.c_str()) < 0 || u.an.ammo > 0;
                if (!ex_key.empty() && (ex_key[0] == 'y' || ex_key[0] == 't' || ex_key[0] == '1') && has_ammo)
                    pick = ex->size() - 1;
                if (const FxAnimType* t = fx_.spawn((*ex)[pick], u.fx, u.fy, pres_unit_fx_level(w, u), height * kFxLepPx)) {
                    snd_.play_at(t->report, u.fx, u.fy, "explosion");
                    ++explosions_;
                }
            }
        }
        // W3-FX [RE: FUN_00700da0 death]: Min/MaxDebris debris (voxel
        // DebrisTypes / DebrisAnims / MetallicDebris), none when low over water.
        const double z = pres_unit_fx_level(w, u) * 104.0 + height;   // w8fix-layer: deck
        effects_.spawn_debris(pres_type_id(u), u.fx * 256.0, u.fy * 256.0, z, on_water(u.fx, u.fy) && height < 11.0);
    }
    // W7-12-13 [RE: FireDeathWeapon FUN_0070CAC0 -> a bullet of the weapon's
    // warhead detonates; its anim is SelectDamageAnimation(damage, WH)
    // FUN_0048A400: AnimList[min(damage, n*25-1) / 25], SplashList for a
    // Conventional warhead low over water]: the detonation anim + its Report.
    bool death_weapon_fx(const std::string& wid, int damage, double fx, double fy, int level) {
        if (wid.empty()) return false;
        const std::string wh = pres_clean(rules_ini_->get(wid, "Warhead"));
        if (wh.empty()) return false;
        std::vector<std::string> list;
        {
            std::stringstream ss(pres_clean(rules_ini_->get(wh, "AnimList")));
            for (std::string t; std::getline(ss, t, ',');)
                if (!pres_clean(t).empty()) list.push_back(pres_clean(t));
        }
        const std::string conv = pres_lower(pres_clean(rules_ini_->get(wh, "Conventional")));
        const std::vector<std::string>& splash = effects_.splash_list();
        std::string name;
        if (!conv.empty() && (conv[0] == 'y' || conv[0] == 't') && !splash.empty() && on_water(fx, fy)) {
            const int ns = static_cast<int>(splash.size());
            name = splash[static_cast<std::size_t>(std::min(std::max(damage, 0), ns * 35 - 1) / 35)];
        } else if (!list.empty()) {
            const int n = static_cast<int>(list.size());
            name = list[static_cast<std::size_t>(std::min(std::max(damage, 0), n * 25 - 1) / 25)];
        }
        if (name.empty()) { pres_log(frame_, "fx death weapon " + wid + " (no anim)"); return false; }
        const FxAnimType* t = fx_.spawn(name, fx, fy, level, 0.0);
        if (!t) { pres_log(frame_, "fx death weapon " + wid + " " + name + " (no art)"); return false; }
        snd_.play_at(t->report, fx, fy, "death weapon");
        ++w5_counts_["death weapon"];
        pres_log(frame_, "fx death weapon " + wid + " " + name);
        return true;
    }
    // W3-B: weapon Report + muzzle/impact anims on each shot and the death
    // sound/explosion, observed once per LOGIC tick (Presentation::logic_tick)
    // so an effect is born on the tick its event happened, whatever the
    // render rate (was per render frame inside unit_audio).
    void unit_fx_tick(const ra2engine::World& w, const std::vector<ra2engine::World::FireEvent>& events) {
        const auto& units = w.units();
        // A fast weapon can reset fire_flash to the same value on consecutive
        // ticks. Observe actual shots rather than losing those equal edges.
        std::vector<unsigned> shots(units.size(),0);
        for (const auto& e : events)
            if(e.unit>=0 && e.unit<static_cast<int>(shots.size())) ++shots[e.unit];
        prev_.resize(units.size(), {0.0, -1.0});
        shot_loops_.resize(units.size());
        alive_prev_.resize(units.size(), 1);
        for (std::size_t i = 0; i < units.size(); ++i) {
            const auto& u = units[i];
            const UnitVoices& v = voices_of(u);
            infantry_death_fx(w, u, prev_[i].second < 0.0, alive_prev_[i] != 0);   // W3-B
            alive_prev_[i] = u.alive ? 1 : 0;
            const unsigned count=shots[i] ? shots[i] : (u.fire_flash > prev_[i].first ? 1u : 0u);
            for(unsigned shot=0;shot<count && u.cargo_of<0;++shot) {
                // W7-03-06 / W7-12-30: the FIRED weapon's Report (IFV modes,
                // deployed GI Para / GGI MissileLauncher, elite weapons).
                const std::string wid = fired_weapon(w, u);
                const std::string rep = wid.empty() ? v.report : fx_.weapon_fx(wid).report;
                const std::string report=pick_report(rep);
                shot_loops_[i].shot(snd_,report,u.fx,u.fy);
                if (u.house == g_local_house || w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) fire_fx(w, u);
            }
            auto& shot_loop=shot_loops_[i];
            if (shot_loop.handle) {
                const bool firing=ra2engine::weapon_loop_active(u.alive && u.die_time<0 && u.cargo_of<0,
                    u.target>=0 || u.target_building>=0 || u.attack_cell_x>=0,
                    u.fire_flash,u.gattling.enabled(),u.gattling.charge);
                shot_loop.update(snd_,firing,u.fx,u.fy);
            }
            if (u.die_time >= 0.0 && prev_[i].second < 0.0) {
                // W2-GA-25: crushed infantry (killed by an adjacent Crusher
                // vehicle that ran into its cell) squish instead of dying.
                // W7-04-25 [RE: per-cell crush 0x7406A2-0x740764]: only the
                // VICTIM's CrushSound plays (InfantrySquish; a crushed tank's
                // own CrushSound=TankCrush), no DieSound, and the body is
                // removed outright -- no Die sequence, no corpse.
                const int k = u.vet.killed_by;
                const bool crushed = k >= 0 && k < static_cast<int>(units.size()) && u.crushable &&
                                     units[k].is_vehicle && units[k].crusher &&
                                     std::abs(units[k].fx - u.fx) <= 1.6 && std::abs(units[k].fy - u.fy) <= 1.6;
                if (crushed) {
                    snd_.play_at(v.crush, u.fx, u.fy, "crushed");
                    if (crushed_.size() <= i) crushed_.resize(units.size(), 0);
                    crushed_[i] = 1;
                    pres_mark_crushed(static_cast<int>(i));
                } else {
                    snd_.play_at(v.die, u.fx, u.fy, "die");
                }
                // W7-12-12 [RE: from W7-05-09 -- fixed-wing FUN_004CD520 /
                // BalloonHover FUN_00744F30 FireDeathWeapon, other crashable
                // jumpjets FUN_007376C0 at the IMPACT point]: an aircraft killed
                // in the air falls first; nothing explodes on the kill tick.
                const bool will_crash = u.is_aircraft && u.altitude > 0.0 && !u.an.t.missile &&
                                        (u.an.t.crashable || u.an.t.fixed_wing);
                if (will_crash) {
                    if (crash_pending_.size() <= i) crash_pending_.resize(units.size(), 0);
                    crash_pending_[i] = 1;
                }
                // Vehicle death: the Explosion= anim carries its own Report=.
                // W3-B [RE: presentation.md §5 FUN_007376c0]: spawned here on
                // the death tick.
                if (u.an.t.missile) {
                    // W4 [FUN_00662A70]: a rocket (detonated or shot down)
                    // explodes with its warhead's anim where it is -- in the
                    // air when it was hit in flight.
                    if (w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) rocket_fx(w, u);
                }
                // W2-AN-25: a ship heavy enough to sink (the sim flags it the
                // next tick) does not explode [RE: 0x736DC5 sink branch].
                const bool will_sink = u.is_naval && !u.an.t.underwater &&
                                       u.an.t.weight >= w.sw_params().ship_sinking_weight;
                if (!u.an.t.missile && u.is_vehicle && !will_crash && !will_sink && !u.an.crashing && !u.an.sinking &&
                           w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) {
                    vehicle_death_fx(w, u, i);
                }
                // W7-12-02 [RE: Foot death 0x4D97D0 @4D981C -- EVA_UnitLost only
                // when the type is not Spawned= (+0xD54); IsPlayerControl;
                // RadarEvent 7]: no "Unit lost" for V3 / Dreadnought rockets,
                // Hornets, ASW, MiGs, paradrop and spy planes.
                if (u.house == g_local_house && (v.spawned || u.an.t.spawned || u.an.t.missile))
                    ++w5_counts_["spawned no unit lost"];
                else if (u.house == g_local_house && radar_event(7, u.fx, u.fy))
                    snd_.eva("EVA_UnitLost");
            }
            // W7-12-12: the falling wreck reached the ground -> one blast there.
            if (i < crash_pending_.size() && crash_pending_[i] && !u.an.crashing && u.altitude <= 0.0 &&
                prev_[i].second >= 0.0) {
                crash_pending_[i] = 0;
                if (w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) {
                    if (u.an.t.fixed_wing || u.balloon_hover) {
                        // FireDeathWeapon: DeathWeapon, else the Primary.
                        const std::string id = pres_type_id(u);
                        std::string wid = pres_clean(rules_ini_->get(id, "DeathWeapon"));
                        if (wid.empty()) wid = pres_clean(rules_ini_->get(id, "Primary"));
                        int dmg = std::atoi(pres_clean(rules_ini_->get(wid, "Damage")).c_str());
                        const std::string mod = pres_clean(rules_ini_->get(id, "DeathWeaponDamageModifier"));
                        if (!mod.empty()) dmg = static_cast<int>(dmg * std::atof(mod.c_str()));
                        if (!death_weapon_fx(wid, dmg, u.fx, u.fy, u.level)) vehicle_death_fx(w, u, i);
                    } else {
                        vehicle_death_fx(w, u, i);
                    }
                    ++w5_counts_["crash impact"];
                }
            }
            prev_[i] = {u.fire_flash, u.die_time};
        }
    }
    // [G-81] Voices on the player's orders, weapon Report on every shot, death
    // sounds, EVA_UnitLost -- the former GameScene block, now for every unit.
    void unit_audio(const ra2engine::World& w, const ra2engine::InputActions& act) {
        const auto& units = w.units();
        moving_prev_.resize(units.size(), 0);
        uhp_prev_.resize(units.size(), -1);
        move_loop_.resize(units.size(), 0);
        unpowered_prev_.resize(units.size(), 0);
        for (std::size_t i = 0; i < units.size(); ++i) {
            const auto& u = units[i];
            const UnitVoices& v = voices_of(u);
            // W2-GA-25: a vehicle that starts moving plays its MoveSound.
            // W7-04-30 [soundmd Control=loop: TerrorDroneMoveLoop]: a LOOP
            // MoveSound is the unit's own controller -- it follows the unit
            // and stops when it halts (the w6_flight pattern), instead of a
            // one-shot pinned where the move started.
            const bool mv = u.alive && u.die_time < 0.0 && u.cargo_of < 0 && u.moving();
            // (the Terror Drone is a UnitType drawn as SHP: is_vehicle false)
            if ((u.is_vehicle || !pres_rtti_infantry(u)) && !u.is_aircraft && !v.move_sound.empty()) {
                const bool loops = snd_.sound_loops(v.move_sound);
                if (mv && !moving_prev_[i]) {
                    if (loops) {
                        pres_log(frame_, "sound " + v.move_sound + " (move loop)");
                        move_loop_[i] = snd_.loop_at(v.move_sound, u.fx, u.fy);
                        if (!move_loop_[i]) move_loop_[i] = 1;   // deviceless: remember it runs
                    } else {
                        snd_.play_at(v.move_sound, u.fx, u.fy, "move start");
                    }
                } else if (!mv && move_loop_[i]) {
                    if (move_loop_[i] != 1) snd_.stop_loop(move_loop_[i]);
                    pres_log(frame_, "sound-stop " + v.move_sound + " (move loop)");
                    move_loop_[i] = 0;
                } else if (mv && move_loop_[i] > 1 && !snd_.update_loop(move_loop_[i], u.fx, u.fy)) {
                    move_loop_[i] = snd_.loop_at(v.move_sound, u.fx, u.fy);   // keep it going
                }
            }
            moving_prev_[i] = mv ? 1 : 0;
            // W7-03-10 [rules ActivateSound / DeactivateSound]: a PoweredUnit
            // (Robot Tank) losing / regaining its Robot Control Center.
            const std::uint8_t unp = (u.alive && u.die_time < 0.0 && u.w6.unpowered) ? 1 : 0;
            if (unp != unpowered_prev_[i] && u.alive && u.die_time < 0.0)
                snd_.play_at(unp ? v.deactivate : v.activate, u.fx, u.fy, unp ? "deactivate" : "activate");
            unpowered_prev_[i] = unp;
            // W2-GA-26 [RE: presentation.md §1.3 FUN_00736d60]: the player's
            // harvester taking (non-lethal) damage -> RadarEvent 4
            // HarvesterAttacked (8 cells) -> EVA_OreMinerUnderAttack, no siren.
            if (u.house == g_local_house && u.is_harvester && u.alive && u.die_time < 0.0 && uhp_prev_[i] >= 0 &&
                u.hp < uhp_prev_[i] && u.hp > 0 && radar_event(4, u.fx, u.fy))
                snd_.eva("EVA_OreMinerUnderAttack");
            uhp_prev_[i] = u.hp;
        }
        // W7-12-26 [RE: TechnoClass::Select 0x6FB4C0 -- the object a click
        // or box newly selects speaks; the order dispatch 0x4AE660 clears
        // AllowVoice after CurrentObjects[0], so the FIRST selected answers].
        int speaker = act.actor;
        if (act.selected) {
            if (!sel_new_.empty()) speaker = sel_new_.front();
        } else if (act.attack_ordered || act.move_ordered) {
            for (int k : sel_order_)
                if (k >= 0 && k < static_cast<int>(units.size()) && units[static_cast<std::size_t>(k)].house == g_local_house) {
                    speaker = k;
                    break;
                }
        }
        if (speaker >= 0 && speaker < static_cast<int>(units.size())) {
            const auto& au = units[static_cast<std::size_t>(speaker)];
            const UnitVoices& av = voices_of(au);
            // [RE: 0x6FB4C0] VoiceSelect only for the player's own objects or
            // a MultiplayPassive house's (civilians, cows, Einstein).
            const bool may_speak = au.house == g_local_house || w.passive_house(au.house);
            if (act.selected && may_speak) {
                // W7-03-10: a de-powered Robot Tank answers VoiceSelectDeactivated.
                const std::string& sel = (au.w6.unpowered && !av.select_deactivated.empty()) ? av.select_deactivated
                                                                                             : av.select;
                queue_voice(speaker, sel, "select");
            } else if ((act.attack_ordered || act.move_ordered) && au.house == g_local_house) {
                order_voice(w, act, speaker, au, av);   // W5-E ClickedMission
            }
        }
        if (act.repair_clicked) snd_.eva("EVA_Repairing");
        pump_voices();
    }
    // W7-12-25 [RE: QueueVoice 0x708210 stores techno+0x4F0; TechnoClass::AI
    // 0x6F9380 @6F9400 starts it with FUN_0074F740(1.0) -- NON-positional,
    // full volume -- only when the unit's voice controller (+0x4DC) is idle;
    // the same voice while busy is dropped, a different one waits; only the
    // last request survives]. One controller per unit.
    struct UVoice { std::uint32_t h = 0; std::string playing, queued; std::string why; };
    std::map<int, UVoice> uvoice_;
    void queue_voice(int unit, const std::string& key, const char* why) {
        if (key.empty() || unit < 0) return;
        UVoice& c = uvoice_[unit];
        c.queued = key;
        c.why = why;
    }
    void pump_voices() {
        for (auto& kv : uvoice_) {
            UVoice& c = kv.second;
            if (c.queued.empty()) continue;
            if (snd_.voice_alive(c.h)) {
                if (c.queued == c.playing) c.queued.clear();   // same line while busy: dropped
                continue;                                       // a different one waits
            }
            c.h = snd_.play_voice(c.queued, c.why.c_str());
            c.playing = c.queued;
            c.queued.clear();
        }
    }
    std::vector<std::uint32_t> move_loop_;
    std::vector<std::uint8_t> unpowered_prev_;
    // W7-12-26: the selection in the order it was made (the clone's Selection
    // is an index bitset). Units first selected in the same frame are taken
    // in index order [RE-FALLBACK: the box select walks the tactical display
    // list (0x6D9B30), not reproduced].
    std::vector<int> sel_order_, sel_new_;
    const ra2engine::Input* input_ = nullptr;
public:
    void observe_selection(const ra2engine::Selection& s, const ra2engine::Input& in) {
        input_ = &in;
        sel_new_.clear();
        sel_order_.erase(std::remove_if(sel_order_.begin(), sel_order_.end(), [&](int i) { return !s.has(i); }),
                         sel_order_.end());
        for (int i : s.ids())
            if (std::find(sel_order_.begin(), sel_order_.end(), i) == sel_order_.end()) {
                sel_order_.push_back(i);
                sel_new_.push_back(i);
            }
    }
    int first_selected() const { return sel_order_.empty() ? -1 : sel_order_.front(); }
    // W7-10-05 / W7-12-23 [RE: CaptureManager FUN_00471c80 / 0x471f20 /
    // 0x472090 / 0x472570]: on capture the MINDANIM loop is attached to the
    // victim (MINDANIMR for permanent control -- not modelled), the
    // YuriMindControlSound plays (the player's unit taken) and the warhead's
    // YURICNTL anim plays at the victim; the link line is drawn for
    // MindControlAttackLineFrames (Rules+0x310, 20) after the capture and
    // while the controller or the victim is selected; on release the
    // victim's MindClearedSound (else the Rules one) plays.
    std::map<int, int> mc_prev_;   // victim -> controller
    std::map<int, long> mc_since_;  // victim -> capture tick
    void observe_mind_control(const ra2engine::World& w, const std::vector<std::pair<int, int>>& pairs) {
        if (!rules_ini_) return;
        std::map<int, int> now;
        for (const auto& p : pairs)
            if (p.second >= 0 && p.second < static_cast<int>(w.units().size())) now[p.second] = p.first;
        auto av = [&](const char* k) {
            std::string v = pres_clean(rules_ini_->get("AudioVisual", k));
            if (v.empty()) v = pres_clean(rules_ini_->get("General", k));
            if (v.empty()) v = pres_clean(rules_ini_->get("CombatDamage", k));
            return v;
        };
        for (const auto& kv : now) {
            if (mc_prev_.count(kv.first) && mc_prev_[kv.first] == kv.second) continue;
            const ra2engine::Unit& v = w.units()[static_cast<std::size_t>(kv.first)];
            const int was = mc_was_house_.count(kv.first) ? mc_was_house_[kv.first] : -1;
            if (was == g_local_house || v.house == g_local_house)
                snd_.play_at(av("YuriMindControlSound"), v.fx, v.fy, "mind control");
            fx_.attach(kv.first, av("ControlledAnimationType"), pres_rtti_infantry(v) ? 25.0 : 30.0);
            mc_since_[kv.first] = g_pres_tick;
            if (w.object_visible_at(g_local_house, v.cell_x, v.cell_y))
                fx_.spawn("YURICNTL", v.fx, v.fy, v.level, 10.0);
            ++w5_counts_["mind control"];
        }
        for (const auto& kv : mc_prev_) {
            if (now.count(kv.first)) continue;
            fx_.detach(kv.first);
            mc_since_.erase(kv.first);
            if (kv.first < static_cast<int>(w.units().size())) {
                const ra2engine::Unit& v = w.units()[static_cast<std::size_t>(kv.first)];
                if (v.alive && v.die_time < 0.0) {
                    snd_.play_at(voices_of(v).mind_cleared, v.fx, v.fy, "mind cleared");
                    ++w5_counts_["mind cleared"];
                }
            }
        }
        mc_was_house_.clear();
        for (std::size_t i = 0; i < w.units().size(); ++i) mc_was_house_[static_cast<int>(i)] = w.units()[i].house;
        mc_prev_ = now;
        // The link lines.
        static int line_frames = -1;
        if (line_frames < 0) {
            const std::string v = av("MindControlAttackLineFrames");
            line_frames = v.empty() ? 20 : std::atoi(v.c_str());
        }
        fx_.links_.clear();
        for (const auto& kv : now) {
            const bool sel = (kv.second<0 && input_ && input_->selected_building()==-1-kv.second) ||
                             std::find(sel_order_.begin(), sel_order_.end(), kv.first) != sel_order_.end() ||
                             std::find(sel_order_.begin(), sel_order_.end(), kv.second) != sel_order_.end();
            const long since = mc_since_.count(kv.first) ? g_pres_tick - mc_since_[kv.first] : 1 << 30;
            if (!sel && since > line_frames) continue;
            int house=-1;
            if(kv.second>=0 && kv.second<static_cast<int>(w.units().size())) house=w.units()[kv.second].house;
            else if(kv.second<0 && -1-kv.second<static_cast<int>(w.buildings().size())) house=w.buildings()[-1-kv.second].house;
            if(house>=0) fx_.links_.push_back({kv.second, kv.first, house_color(w,house)});
        }
    }
    long mc_count() const { return static_cast<long>(mc_prev_.size()); }
    std::map<int, int> mc_was_house_;
private:

    // W2-GA-26: each superweapon type speaks its own lines (eva.ini
    // [EVA_*]): the player's READY line per type (Weather Control no longer
    // says "Chronosphere ready"); the "Warning: ... detected" line when an
    // ENEMY house gains a superweapon (EVA_WeatherDeviceReady is the Weather
    // Control *detected* warning); and the launch / activation warning heard
    // by everyone when any house fires one. Types without a line (paradrop,
    // spy plane, ...) stay silent.
    static const char* sw_line(ra2engine::SuperweaponType t, int what) {   // 0 ready, 1 detected, 2 fired
        using T = ra2engine::SuperweaponType;
        static const char* nuke[3] = {"EVA_NuclearMissileReady", "EVA_NuclearSiloDetected", "EVA_NuclearMissileLaunched"};
        static const char* ic[3] = {"EVA_IronCurtainReady", "EVA_IronCurtainDetected", "EVA_IronCurtainActivated"};
        static const char* ls[3] = {"EVA_LightningStormReady", "EVA_WeatherDeviceReady", "EVA_LightningStormCreated"};
        static const char* cs[3] = {"EVA_ChronosphereReady", "EVA_ChronosphereDetected", "EVA_ChronosphereActivated"};
        switch (t) {
            case T::Nuke: return nuke[what];
            case T::IronCurtain: return ic[what];
            case T::LightningStorm: return ls[what];
            case T::ChronoSphere: return cs[what];
            default: return nullptr;
        }
    }
    void superweapon_eva(const ra2engine::World& w) {
        const int n = w.house_count();
        if (static_cast<int>(sw_prev_.size()) < n) sw_prev_.resize(static_cast<std::size_t>(n));
        for (int h = 0; h < n; ++h) {
            const ra2engine::Superweapon* sw = w.sw_state(h);
            SwSeen& p = sw_prev_[static_cast<std::size_t>(h)];
            const ra2engine::SuperweaponType t = sw ? sw->type : ra2engine::SuperweaponType::None;
            const bool ready = sw && sw->ready;
            if (t != ra2engine::SuperweaponType::None) {
                if (h == g_local_house && ready && !p.ready)
                    if (const char* l = sw_line(t, 0)) snd_.eva(l);
                if (h != g_local_house && p.type != t && w.is_enemy(g_local_house, h))
                    if (const char* l = sw_line(t, 1)) snd_.eva(l);
                // Fired: was ready, same type, charge restarted from zero.
                if (p.ready && !ready && p.type == t && sw->timer <= 0.5)
                    if (const char* l = sw_line(t, 2)) snd_.eva(l);
            }
            p.type = t;
            p.ready = ready;
        }
    }
    // W2-GA-14: start the factory door when a unit appears at a factory, and
    // the superweapon building's charge anims on READY / FIRE edges.
    void building_anim_events(const ra2engine::World& w, const BuildingAssets& ba) {
        if (!ba.ok) return;
        const auto& bs = w.buildings();
        auto image_of = [&](const ra2engine::Building& b) -> std::string {
            if (b.type < 0 || b.type >= static_cast<int>(ba.roster.size())) return std::string();
            const ra2engine::BuildingTypeDef& d = ba.roster[static_cast<std::size_t>(b.type)];
            return pres_upper(d.image.empty() ? d.id : d.image);
        };
        for (const auto& b : bs) {
            if (!b.alive || b.state != ra2engine::Building::State::Idle ||
                b.type < 0 || b.type >= static_cast<int>(ba.roster.size())) continue;
            const std::string img = image_of(b);
            if (img.empty() || !fx_.building(img).slot[kBsSuper].t || !combat_rules_) continue;
            const std::string power = combat_rules_->section_string(ba.roster[b.type].id,"SuperWeapon", "");
            const std::string kind = pres_upper(combat_rules_->section_string(power,"Type", ""));
            ra2engine::SuperweaponType type = ra2engine::SuperweaponType::None;
            for (int i=1;i<=12;++i) {
                const auto candidate = ra2engine::superweapon_type_from_id(i);
                if (pres_upper(ra2engine::superweapon_type_name(candidate)) == kind) { type=candidate; break; }
            }
            const auto* sw = type == ra2engine::SuperweaponType::None ? nullptr : w.sw_state(b.house,type);
            const int st = !sw ? 0 : (sw->ready ? 2 : 1);
            auto& e = g_bld_events[bld_event_key(b)];
            if (st != e.super_previous) {
                if (st == 2) { e.super_phase=0; e.super_start=g_pres_tick; }
                else if (e.super_previous == 2) { e.super_phase=2; e.super_start=g_pres_tick; }
                else { e.super_phase=2; e.super_start=-1; }
                ra2engine::diag::note("SW_BUILDING type=%s house=%d state=%d->%d phase=%d tick=%ld",
                    ba.roster[b.type].id.c_str(),b.house,e.super_previous,st,e.super_phase,g_pres_tick);
                e.super_previous=st;
            } else if (e.super_phase < 0) { e.super_phase=2; e.super_start=-1; }
        }
    }

    // W3-FX AmbientSound: one looping positional voice per live object whose
    // type has AmbientSound= (units, buildings, map terrain objects such as
    // the ore mines' OreMineExtract), restarted when it ends, stopped when the
    // object dies. Keyed by kind + index ("u", "b", "t").
    struct Amb { std::string key; std::uint32_t h = 0; double x = 0, y = 0; bool seen = false; };
    std::map<std::string, Amb> amb_;
    std::map<std::string, std::string> amb_type_;   // type id -> AmbientSound ("" none)
    long amb_started_ = 0;
    const std::string& ambient_of(const std::string& id) {
        auto it = amb_type_.find(id);
        if (it != amb_type_.end()) return it->second;
        return amb_type_.emplace(id, effects_.rules_key(id, "AmbientSound")).first->second;
    }
    void ambient_sounds(const ra2engine::World& w, const BuildingAssets& ba) {
        for (auto& kv : amb_) kv.second.seen = false;
        auto keep = [&](const std::string& k, const std::string& snd, double x, double y) {
            if (snd.empty()) return;
            Amb& a = amb_[k];
            a.seen = true;
            a.x = x;
            a.y = y;
            if (a.key.empty()) {
                a.key = snd;
                a.h = snd_.loop_at(snd, x, y);
                ++amb_started_;
                pres_log(frame_, "sound " + snd + " (ambient " + k + ")");
            } else if (a.h && !snd_.update_loop(a.h, x, y)) {
                a.h = snd_.loop_at(snd, x, y);   // the controller keeps it going
            }
        };
        const auto& us = w.units();
        for (std::size_t i = 0; i < us.size(); ++i) {
            const ra2engine::Unit& u = us[i];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            keep("u" + std::to_string(i), ambient_of(pres_type_id(u)), u.fx, u.fy);
        }
        const auto& bs = w.buildings();
        for (std::size_t i = 0; i < bs.size(); ++i) {
            const ra2engine::Building& b = bs[i];
            if (!b.alive || b.state == ra2engine::Building::State::Dead || b.type < 0 ||
                b.type >= static_cast<int>(ba.roster.size()))
                continue;
            keep("b" + std::to_string(i), ambient_of(ba.roster[static_cast<std::size_t>(b.type)].id),
                 b.cx + (b.fw - 1) / 2.0, b.cy + (b.fh - 1) / 2.0);
        }
        if (g_map_dressing) {
            const auto& os = g_map_dressing->objects();
            for (std::size_t i = 0; i < os.size(); ++i)
                if (os[i].key.rfind("terr:", 0) == 0)
                    keep("t" + std::to_string(i), ambient_of(os[i].key.substr(5)), os[i].x, os[i].y);
        }
        for (auto it = amb_.begin(); it != amb_.end();) {
            if (!it->second.seen) {
                snd_.stop_loop(it->second.h);
                it = amb_.erase(it);
            } else {
                ++it;
            }
        }
    }
public:
    long ambient_started() const { return amb_started_; }
    std::size_t ambient_live() const { return amb_.size(); }
private:
    std::size_t units_seen_ = 0;
    std::vector<int> sw_anim_prev_;
    struct SwSeen { ra2engine::SuperweaponType type = ra2engine::SuperweaponType::None; bool ready = false; };
    std::vector<SwSeen> sw_prev_;

    struct Slot { int type = -1; bool ready = false; };
    struct Bld { bool seen = false; int hp = 0; int house = -1; bool dead = false; bool selling = false; };
    // RadarEvent::Create suppression [RE: presentation.md §1.2, FUN_0065f4b0]:
    // false while a live event of the same type lies within its distance.
    // {dist cells, duration frames}; entries 0..5 are overridden by [General]
    // RadarEventSuppressionDistances / RadarEventDurations (read in init).
    struct RadarEv { int type; double x, y; long until; long born = 0; };
    double radar_dist_[17] = {8, 8, 8, 8, 8, 6, 2, 8, 2, 5, 8, 8, 8, 8, 8, 8, 8};
    int radar_dur_[17] = {400, 400, 400, 600, 400, 400, 200, 200, 400, 400, 100, 200, 400, 5, 200, 400, 600};
    int radar_vis_[17] = {200, 200, 200, 200, 200, 200, 0, 0, 0, 0, 0, 200, 200, 0, 0, 0, 200};
    bool radar_supp_[17] = {1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1};
    std::vector<RadarEv> radar_;
    bool radar_event(int type, double x, double y) {
        // [Medium: removal point not read -- an event leaves at its duration.]
        radar_.erase(std::remove_if(radar_.begin(), radar_.end(),
                                    [&](const RadarEv& e) { return e.until <= frame_; }),
                     radar_.end());
        if (radar_supp_[type])
            for (const RadarEv& e : radar_)
                if (e.type == type &&
                    static_cast<int>(std::sqrt((e.x - x) * (e.x - x) + (e.y - y) * (e.y - y))) <
                        radar_dist_[type])
                    return false;
        radar_.push_back(RadarEv{type, x, y, frame_ + radar_dur_[type], frame_});
        return true;
    }
    const std::vector<RadarEv>& radar_events() const { return radar_; }
    long next_funds_frame_ = 0;
    long speak_delay_frames_ = 1800;   // [General] SpeakDelay (minutes) x 900

    PresAudio snd_;
    PresFx fx_;
    PresEffects effects_;   // W3-FX
    std::unique_ptr<ShpFile> shroud_, fog_;
    std::unique_ptr<ra2assets::Font> font_;
    ra2assets::Palette font_pal_;
    AudioSystem* audio_ = nullptr;
    const Ini* rules_ini_ = nullptr;
    const Rules* combat_rules_ = nullptr;
    long frame_ = 0;
    long logic_ = 0;   // W3-B: logic ticks since init (g_pres_tick)
    double clock_ = 0.0;
    ra2engine::SpeechCooldown speech_cooldown_;
    ra2engine::Camera cam_;
    int vw_ = 1280, vh_ = 720;
    Slot slots_[ra2engine::kQueueCats];
    std::vector<Bld> bhp_;
    std::vector<std::pair<double, double>> prev_;
    std::vector<ra2engine::WeaponSoundLoop> shot_loops_;
    std::vector<ra2engine::WeaponSoundLoop> building_shot_loops_;
    std::vector<std::string> building_audio_trace_;
    bool low_power_ = false;
    bool sw_ready_ = false;
    bool radar_online_ = false;
    bool end_spoken_ = false;
    std::string radar_on_, radar_off_;
    int buildable_ = -1;
    std::string base_attack_siren_, building_die_, building_damage_, sell_sound_, vet_sound_,
        elite_sound_;
};

// pres_audio_events.inc -- W5-E (wave 5, agent E): the unit / world sounds and
// EVA lines that were never played (REMAINING R-130 / R-131), at their
// original trigger points. Included INSIDE class Presentation (pres_game.inc)
// so it shares snd_ / radar_event / rules_ini_; everything is observation of
// World / input state (edge detection) or an explicit GameScene note, so the
// simulation and every gate hash are untouched.
//
// RE (gamemd.exe YR 1.001, Ghidra copy D:\ra2-re\w5-audio, jobs j1..j22):
//   TechnoTypeClass::LoadFromINI FUN_00711510 stores VoiceSelect 0x414 (list),
//   VoiceMove 0x468, VoiceAttack 0x484, VoiceSpecialAttack 0x4A0 (list),
//   VoiceDie 0x4BC, VoiceFeedback 0x4D8 (list), CrashingSound 0x544,
//   SinkingSound 0x548, VoiceCrashing 0x550, VoiceSinking 0x554, VoiceEnter
//   0x558, VoiceCapture 0x55C, TurretRotateSound 0x560, DeploySound 0x56C,
//   UndeploySound 0x570, VoiceHarvest 0x57C, VoiceDeploy 0x590.
//   RulesClass FUN_00668C20: BuildingGarrisonedSound +0x1BC,
//   BuildingRepairedSound +0x1C4, CheerSound +0x1C8, CrateMoney/Reveal/Fire/
//   Armour/Speed/Unit/PromoteSound +0x1E4..+0x1FC, SinkingSound +0x208,
//   CreditTicks list +0x6CC.
//
//   * ClickedMission FUN_006FF0A0: the order voice by mission (input.hpp).
//   * TechnoClass::ReceiveDamage FUN_00700DA0 @701B20: on the NowYellow damage
//     state (health just crossed ConditionYellow) a player-controlled object
//     with VoiceFeedback= says a random entry 30% of the time
//     (Random(0,99) < 0x1E), positional.
//   * FootClass AI FUN_004DA440 @4DAB2E/@4DAC61: when an object starts sinking
//     it plays VoiceSinking then SinkingSound (the type's, else the Rules
//     one) on a looping controller at its coords; crashing likewise plays
//     VoiceCrashing then CrashingSound (no Rules fallback).
//   * TechnoClass::AI FUN_006F9380 @6F94D9: a type with TurretRotateSound keeps
//     it going (controller +0x488) while the turret is turning (+0x4A0) and
//     stops it when it stops.
//   * UnitClass deploy FUN_007383E0/@738A6C and InfantryClass deploy: the
//     type's DeploySound at the object; undeploy @738ECB/@51D792 UndeploySound.
//   * InfantryClass occupy FUN_00522640: the FIRST occupant of a player
//     building -> EVA_StructureGarrisoned + BuildingGarrisonedSound at it.
//   * FUN_004581F0: a garrison emptied back to neutral -> RadarEvent 15 ->
//     EVA_StructureAbandoned for the old owner.
//   * FUN_007008B0 (Repair_To_Full, engineer repair) -> BuildingRepairedSound.
//   * Crate pickup FUN_00481EAD.. (CellClass crate): per powerup the matching
//     Crate*Sound / HealCrateSound and, for the local player, the
//     EVA_Unit{Armor,Speed,FirePower}Upgraded line (@482DC6/@482FB5/@483195).
//   * Capture FUN_004481AB @448310: a tech building (type flag +0x1552) ->
//     EVA_TechBuildingLost for the losing player and the type's
//     CaptureEvaEvent (+0x1554) for the gaining player; any other building ->
//     RadarEvent 10 then EVA_BuildingCaptured when either side is the player.
//   * Bridge repair FUN_00519449 @5199E4 -> RadarEvent 14 -> EVA_BridgeRepaired
//     (+ Rules RepairBridgeSound).
//   * Service depot FUN_0044B780 @44B96E -> RadarEvent 8 (2 cells) ->
//     EVA_UnitRepaired.
//   * AllToCheer FUN_0050C6E0: every techno of the house cheers, then
//     FUN_0074F740(CheerSound, 1.0).
//   * CreditClass::AI FUN_004A2510 / draw FUN_004A2280: the shown credits move
//     |diff|>>3 (clamped 1..143) toward the real value per call; a moving
//     counter plays CreditTicks[0] (up) / [1] (down) at volume 0.5.
public:
    // ---- per-type data (add_voices) ------------------------------------------
    void add_w5_voices(const std::string& id, UnitVoices& v) {
        if (!rules_ini_) return;
        auto k = [&](const char* key) { return pres_clean(rules_ini_->get(id, key)); };
        v.feedback = k("VoiceFeedback");
        v.special = k("VoiceSpecialAttack");
        v.enter = k("VoiceEnter");
        v.capture = k("VoiceCapture");
        v.harvest = k("VoiceHarvest");
        v.deploy_voice = k("VoiceDeploy");
        v.deploy_snd = k("DeploySound");
        v.undeploy_snd = k("UndeploySound");
        v.sinking = k("SinkingSound");
        v.crashing = k("CrashingSound");
        v.voice_sinking = k("VoiceSinking");
        v.voice_crashing = k("VoiceCrashing");
        v.turret_rotate = k("TurretRotateSound");
        const std::string c4 = pres_lower(k("C4"));
        v.c4 = !c4.empty() && (c4[0] == 'y' || c4[0] == 't' || c4[0] == '1');
        // W7-02-06 [RE: 0x708520 / loader @0x712AEA]: per-weapon attack voices.
        v.attack_primary = k("VoicePrimaryWeaponAttack");
        v.attack_primary_elite = k("VoicePrimaryEliteWeaponAttack");
        v.attack_secondary = k("VoiceSecondaryWeaponAttack");
        v.attack_secondary_elite = k("VoiceSecondaryEliteWeaponAttack");
        // W7-03-10: Robot Tank activate / deactivate; W7-10-05 MindClearedSound.
        v.activate = k("ActivateSound");
        v.deactivate = k("DeactivateSound");
        v.select_deactivated = k("VoiceSelectDeactivated");
        v.mind_cleared = k("MindClearedSound");
        if (v.mind_cleared.empty()) v.mind_cleared = pres_clean(rules_ini_->get("AudioVisual", "MindClearedSound"));
        // W7-12-02 [RE: TechnoType loader FUN_00711510 @0x714210 "Spawned" ->
        // +0xD54, read by the Foot death 0x4D97D0 @4D981C].
        const std::string sp = pres_lower(k("Spawned"));
        v.spawned = !sp.empty() && (sp[0] == 'y' || sp[0] == 't' || sp[0] == '1');
        for (const std::string* s : {&v.feedback, &v.special})
            for (const std::string& t : w5_split(*s)) snd_.ensure_sound(t);
        for (const std::string& s : {v.enter, v.capture, v.harvest, v.deploy_voice, v.deploy_snd, v.undeploy_snd,
                                     v.sinking, v.crashing, v.voice_sinking, v.voice_crashing, v.turret_rotate,
                                     v.attack_primary, v.attack_primary_elite, v.attack_secondary,
                                     v.attack_secondary_elite, v.activate, v.deactivate, v.select_deactivated,
                                     v.mind_cleared})
            snd_.ensure_sound(s);
    }

    // ---- GameScene notes -----------------------------------------------------
    // A UI-originated EVA line (cameo click / hotkey / mode), logged + queued.
    void note_eva(const std::string& name) {
        const bool repeated=name=="EVA_Building" || name=="EVA_Training" ||
            name=="EVA_NewRallyPointEstablished" || name=="EVA_Repairing";
        if(repeated && !speech_cooldown_.allow(name,static_cast<std::uint64_t>(std::max(0.0,clock_)*1000.0)))return;
        snd_.eva(name);
    }
    // DeployObject on the selection: Mission_Unload (16) -> VoiceDeploy
    // [RE: FUN_006FF0A0 / FUN_00708280].
    void note_deploy_order(const ra2engine::World& w, int unit) {
        if (unit < 0 || unit >= static_cast<int>(w.units().size()) || !audio_) return;
        const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(unit)];
        const UnitVoices& v = voices_of(u);
        queue_voice(unit, v.deploy_voice, "deploy order");   // W7-12-25
    }
    // AllToCheer [RE: FUN_0050C6E0]: CheerSound at full volume, non-positional.
    void note_cheer() {
        w5_init_once();
        snd_.play_scaled(w5_cheer_, 1.0, "cheer");
    }
    // [RE: FUN_004A2280] one credit-counter step this frame (up / down).
    void credit_tick(bool up) {
        w5_init_once();
        const std::string& k = up ? w5_credit_up_ : w5_credit_down_;
        if (w5_credit_ticks_ >= 2) snd_.play_scaled(k, 0.5, up ? "credits up" : "credits down");
    }
    // A building's power toggle [RE: FUN_00452350 / FUN_00452250].
    void note_power_toggle(bool now_online) {
        snd_.eva(now_online ? "EVA_BuildingOnLine" : "EVA_BuildingOffLine");
    }
    // World event feeds GameScene used to discard: crates, abilities (deploy),
    // garrisons, MCV deploys.
    void on_world_events(const ra2engine::World& w, const BuildingAssets& ba,
                         const std::vector<ra2engine::World::CrateEvent>& crates,
                         const std::vector<ra2engine::World::AbilityEvent>& abil,
                         const std::vector<ra2engine::World::GarrisonEvent>& gar) {
        w5_init_once();
        using P = ra2engine::Powerup;
        for (const auto& e : crates) {
            const double x = e.x, y = e.y;
            const bool own = e.house == g_local_house;
            switch (static_cast<P>(e.applied)) {
                case P::Money: snd_.play_at(w5_crate_[0], x, y, "crate money"); break;
                case P::Reveal: snd_.play_at(w5_crate_[1], x, y, "crate reveal"); break;
                case P::Firepower:
                    snd_.play_at(w5_crate_[2], x, y, "crate firepower");
                    if (own) snd_.eva("EVA_UnitFirePowerUpgraded");
                    break;
                case P::Armor:
                    snd_.play_at(w5_crate_[3], x, y, "crate armor");
                    if (own) snd_.eva("EVA_UnitArmorUpgraded");
                    break;
                case P::Speed:
                    snd_.play_at(w5_crate_[4], x, y, "crate speed");
                    if (own) snd_.eva("EVA_UnitSpeedUpgraded");
                    break;
                case P::Unit: snd_.play_at(w5_crate_[5], x, y, "crate unit"); break;
                case P::Veteran: snd_.play_at(w5_crate_[6], x, y, "crate promote"); break;
                case P::HealBase: snd_.play_at(w5_heal_crate_, x, y, "crate heal"); break;
                default: break;   // explosion / napalm / ICBM ... carry their own anims
            }
        }
        for (const auto& e : abil) {
            // W6-04-27 [RE-FALLBACK: the teleport locomotor's sound calls were
            // not traced]: a Chrono Legionnaire / Commando jump plays the
            // type's ChronoOutSound where it left and ChronoInSound where it
            // lands ([AudioVisual] ChronoOut/InSound when the type has none).
            if (e.kind == ra2engine::World::AbilityEvent::Kind::Teleport && e.a >= 0 &&
                e.a < static_cast<int>(w.units().size())) {
                const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(e.a)];
                double ox = u.fx, oy = u.fy;
                if (e.a < static_cast<int>(w5_u_.size()) && w5_u_[static_cast<std::size_t>(e.a)].seen) {
                    ox = w5_u_[static_cast<std::size_t>(e.a)].fx;
                    oy = w5_u_[static_cast<std::size_t>(e.a)].fy;
                }
                w6_play(w6_type_or_av(pres_type_id(u), "ChronoOutSound"), ox, oy, "chrono out");
                w6_play(w6_type_or_av(pres_type_id(u), "ChronoInSound"), u.fx, u.fy, "chrono in");
                continue;
            }
            // W7-12-13 [RE: FireDeathWeapon FUN_0070CAC0]: the death weapon's
            // detonation anim at the unit (Demo Truck DEMTEXP); x = damage.
            if (e.kind == ra2engine::World::AbilityEvent::Kind::DeathWeapon && e.a >= 0 &&
                e.a < static_cast<int>(w.units().size())) {
                const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(e.a)];
                // An aircraft killed in the air detonates where it hits the
                // ground (W7-12-12, unit_fx_tick's crash impact).
                const bool falls = u.is_aircraft && u.altitude > 0.0 && (u.an.t.crashable || u.an.t.fixed_wing);
                pres_log(frame_, "death weapon event " + pres_type_id(u) + (falls ? " (falls)" : "") + (w.object_visible_at(g_local_house, u.cell_x, u.cell_y) ? "" : " (unseen)"));
                if (!falls && w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) {
                    const std::string id = pres_type_id(u);
                    std::string wid = pres_clean(rules_ini_->get(id, "DeathWeapon"));
                    if (wid.empty()) wid = pres_clean(rules_ini_->get(id, "Primary"));   // Suicide: its own weapon
                    if (wid.empty()) wid = pres_clean(rules_ini_->get(id, "Weapon1"));
                    death_weapon_fx(wid, e.x, u.fx, u.fy, u.level);
                }
                continue;
            }
            // W7-02-05 [RE: TechnoClass::Fire FUN_006FD210 plays the fired
            // weapon's Report]: a Desolator eruption sounds its deploy weapon
            // (RadEruptionWeapon Report=DesolatorDeploy) at the unit; the
            // DeployedFire sequence is inf_anim's (InfAnimState::erupt).
            if (e.kind == ra2engine::World::AbilityEvent::Kind::Eruption && e.a >= 0 &&
                e.a < static_cast<int>(w.units().size())) {
                const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(e.a)];
                const std::string id = pres_type_id(u);
                std::string dw = pres_clean(rules_ini_->get(id, "DeployFireWeapon"));
                const std::string wid =
                    pres_clean(rules_ini_->get(id, (dw.empty() || std::atoi(dw.c_str()) == 1) ? "Secondary" : "Primary"));
                w6_play(pick_report(fx_.weapon_report_list(wid)), u.fx, u.fy, "eruption");
                continue;
            }
            // W7-04-26 [RE: 0x629A50 -- each drone tick on a vehicle host creates
            // ParticleSystem(Rules+0x1020 = [General] DefaultSparkSystem) at
            // the host] [the host's Rock (vt+0x3D8) is not modelled].
            if (e.kind == ra2engine::World::AbilityEvent::Kind::ParasiteTick && e.b >= 0 &&
                e.b < static_cast<int>(w.units().size())) {
                const ra2engine::Unit& h = w.units()[static_cast<std::size_t>(e.b)];
                if (h.is_vehicle && w.object_visible_at(g_local_house, h.cell_x, h.cell_y)) {
                    const std::string spark = pres_clean(rules_ini_->get("General", "DefaultSparkSystem"));
                    if (!spark.empty()) effects_.spawn_system(spark, h.fx * 256.0, h.fy * 256.0, h.level * 104.0 + 60.0);
                    ++w5_counts_["drone sparks"];
                }
                continue;
            }
            if (e.kind != ra2engine::World::AbilityEvent::Kind::Deploy) continue;
            if (e.a < 0 || e.a >= static_cast<int>(w.units().size())) continue;
            const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(e.a)];
            const UnitVoices& v = voices_of(u);
            if (e.x) snd_.play_at(v.deploy_snd, u.fx, u.fy, "deploy");
            else snd_.play_at(v.undeploy_snd, u.fx, u.fy, "undeploy");
        }
        const auto& bs = w.buildings();
        for (const auto& e : gar) {
            if (e.building < 0 || e.building >= static_cast<int>(bs.size())) continue;
            const ra2engine::Building& b = bs[static_cast<std::size_t>(e.building)];
            const double cx = b.cx + (b.fw - 1) / 2.0, cy = b.cy + (b.fh - 1) / 2.0;
            if (e.kind == ra2engine::World::GarrisonEvent::Garrisoned && e.house == g_local_house) {
                snd_.eva("EVA_StructureGarrisoned");
                snd_.play_at(w5_garrisoned_, cx, cy, "garrisoned");
            } else if (e.kind == ra2engine::World::GarrisonEvent::Abandoned && e.house == g_local_house &&
                       radar_event(15, cx, cy)) {
                snd_.eva("EVA_StructureAbandoned");
            }
            w5_garrison_flip_[e.building] = frame_;   // not a capture (building_events)
        }
        (void)ba;
    }
    // gF MCV deploy: the unit type's DeploySound at the new building.
    // W7-12-28: `unit_type` = the MCV's rules id (pres_type_id).
    void on_mcv_deployed(const ra2engine::World& w, const std::string& unit_type, int building) {
        if (!audio_ || building < 0 || building >= static_cast<int>(w.buildings().size())) return;
        const ra2engine::Building& b = w.buildings()[static_cast<std::size_t>(building)];
        if (rules_) add_voices(*rules_, unit_type);
        snd_.play_at(audio_->voices[pres_upper(unit_type)].deploy_snd, b.cx + (b.fw - 1) / 2.0, b.cy + (b.fh - 1) / 2.0,
                     "deploy");
    }
    // Probe counters (--play evidence).
    const std::map<std::string, long>& w5_counts() const { return w5_counts_; }
    long w5_count(const std::string& what) const {
        auto it = w5_counts_.find(what);
        return it == w5_counts_.end() ? 0 : it->second;
    }

private:
    static std::vector<std::string> w5_split(const std::string& list) {
        std::vector<std::string> out;
        std::stringstream ss(list);
        for (std::string t; std::getline(ss, t, ',');)
            if (!pres_clean(t).empty()) out.push_back(pres_clean(t));
        return out;
    }
    void w5_init_once() {
        if (w5_inited_ || !rules_ini_) return;
        w5_inited_ = true;
        auto av = [&](const char* k) { return pres_clean(rules_ini_->get("AudioVisual", k)); };
        w5_cheer_ = av("CheerSound");
        w5_bld_repaired_ = av("BuildingRepairedSound");
        w5_garrisoned_ = av("BuildingGarrisonedSound");
        static const char* kCrate[7] = {"CrateMoneySound", "CrateRevealSound", "CrateFireSound", "CrateArmourSound",
                                        "CrateSpeedSound", "CrateUnitSound", "CratePromoteSound"};
        for (int i = 0; i < 7; ++i) w5_crate_[i] = av(kCrate[i]);
        w5_heal_crate_ = av("HealCrateSound");
        if (w5_heal_crate_.empty()) w5_heal_crate_ = pres_clean(rules_ini_->get("General", "HealCrateSound"));
        w5_sinking_ = av("SinkingSound");
        w5_repair_bridge_ = av("RepairBridgeSound");
        const std::vector<std::string> ct = w5_split(av("CreditTicks"));
        w5_credit_ticks_ = static_cast<int>(ct.size());
        if (ct.size() >= 1) w5_credit_up_ = ct[0];
        if (ct.size() >= 2) w5_credit_down_ = ct[1];
        const std::string cy = av("ConditionYellow");
        if (!cy.empty()) w5_yellow_ = std::atof(cy.c_str()) / 100.0;
        for (const std::string& s : {w5_cheer_, w5_bld_repaired_, w5_garrisoned_, w5_heal_crate_, w5_sinking_,
                                     w5_repair_bridge_, w5_credit_up_, w5_credit_down_})
            snd_.ensure_sound(s);
        for (const std::string& s : w5_crate_) snd_.ensure_sound(s);
        // Type=SHROUD events are silent over unrevealed cells (local player).
        snd_.set_reveal_fn([this](int x, int y) { return !world_ || world_->object_visible_at(g_local_house, x, y); });
    }

    // [RE: FUN_006FF0A0] the acknowledgement voice of the click's mission,
    // queued on the speaker's own voice controller (W7-12-25).
    void order_voice(const ra2engine::World& w, const ra2engine::InputActions& act, int speaker,
                     const ra2engine::Unit& au, const UnitVoices& av) {
        using O = ra2engine::InputActions::Order;
        if (act.attack_ordered) {
            // Sabotage (C4 on a building) is not Attack/Move -> VoiceSpecialAttack.
            if (act.target_building >= 0 && av.c4 && !av.special.empty())
                queue_voice(speaker, pick_report(av.special), "special attack order");
            else
                queue_voice(speaker, attack_voice(w, act, au, av), "attack order");
            return;
        }
        switch (act.order) {
            case O::Harvest: queue_voice(speaker, av.harvest.empty() ? av.move : av.harvest, "harvest order"); return;
            case O::Enter: queue_voice(speaker, av.enter.empty() ? av.move : av.enter, "enter order"); return;
            case O::Capture:
                queue_voice(speaker, !av.capture.empty() ? av.capture : !av.enter.empty() ? av.enter : av.move,
                            "capture order");
                return;
            case O::Deploy: queue_voice(speaker, av.deploy_voice, "deploy order"); return;
            case O::Special:
                queue_voice(speaker, av.special.empty() ? av.move : pick_report(av.special), "special order");
                return;
            case O::Move: default: queue_voice(speaker, av.move, "move order"); return;
        }
    }
    // W7-02-06 [RE: VoiceAttack 0x708520]: the voice of the weapon index the
    // target would get -- Voice{Primary,Secondary}[Elite]WeaponAttack, else
    // VoiceAttack. The slot is the presentation's estimate of SelectWeapon:
    // a building target with an Airstrike=yes secondary (Boris's flare)
    // takes slot 1 [RE: 0x51E81E MigAttackCursor], else the primary when it
    // can hit the target, else the secondary.
    const std::string& attack_voice(const ra2engine::World& w, const ra2engine::InputActions& act,
                                    const ra2engine::Unit& au, const UnitVoices& av) {
        int slot = 0;
        if (au.w2.valid) {
            if (act.target_building >= 0) {
                const std::string wh = pres_clean(rules_ini_->get(au.w2.id, "Warhead"));
                const std::string as = pres_lower(pres_clean(rules_ini_->get(wh, "Airstrike")));
                if (!as.empty() && (as[0] == 'y' || as[0] == 't' || as[0] == '1')) slot = 1;
            } else if (au.target >= 0 && au.target < static_cast<int>(w.units().size())) {
                const ra2engine::Unit& t = w.units()[static_cast<std::size_t>(au.target)];
                const bool air = t.is_aircraft && t.altitude > 0.0;
                if ((air && !au.p_aa && au.w2.aa) || (!air && !au.p_ag && au.w2.ag)) slot = 1;
            }
        }
        const bool elite = w.veterancy_enabled() && ra2engine::vet_rank(au.vet.veterancy) >= 2;
        const std::string& e = slot == 0 ? av.attack_primary_elite : av.attack_secondary_elite;
        const std::string& n = slot == 0 ? av.attack_primary : av.attack_secondary;
        if (elite && !e.empty()) return e;
        if (!n.empty()) return n;
        return av.attack;
    }

    // Per render frame: the click-made EVA lines.
    void w5_update(const ra2engine::World& w, const BuildingAssets& ba, const ra2engine::InputActions& act, double) {
        w5_init_once();
        (void)w;
        (void)ba;
        // [RE: @448171 IsPlayerControl] / [@4439D4 from a player click].
        if (act.primary_set) snd_.eva("EVA_PrimaryBuildingSelected");
        if (act.rally_set) note_eva("EVA_NewRallyPointEstablished");
    }

    // Per logic tick: the state-edge sounds.
    void w5_tick(const ra2engine::World& w, const BuildingAssets& ba) {
        if (!audio_) return;
        w5_init_once();
        const auto& us = w.units();
        if (w5_u_.size() < us.size()) w5_u_.resize(us.size());
        for (std::size_t i = 0; i < us.size(); ++i) {
            const ra2engine::Unit& u = us[i];
            W5U& p = w5_u_[i];
            const UnitVoices& v = voices_of(u);
            // W5-E2 [RE: spy infiltration FUN_004571E0]: a spy consumed on the
            // tick after it had an infiltrate order -> the victim's and the
            // spy owner's lines by purpose (Medium: the per-branch guards).
            if (p.seen && p.inf_target >= 0 && !u.alive && u.die_time < 0.0 && p.alive)
                w5_infiltrated(w, p.inf_target, u.house, p.inf_purpose);
            // W5-E2 (R-133) [RE: InfantryType sequence reader FUN_00523A30 reads
            // "<Seq>Sounds= frame sound [frame sound]" (strtok, at most 2 pairs)
            // from the art Sequence section; the DoType machine FUN_00520810
            // plays a sound when stage % count == its frame].
            if (u.ia.armed && u.alive && u.ia.seq >= 0 && (u.ia.seq != p.seq || u.ia.stage != p.stage)) {
                const std::vector<SeqSound>& ss = w5_seq_sounds(u.image, u.ia.seq);
                if (!ss.empty()) {
                    const ra2engine::InfTable* it = w.inf_table(u.image);
                    const int cnt = it ? std::max(1, it->e[static_cast<std::size_t>(u.ia.seq)].count) : 1;
                    for (const SeqSound& q : ss)
                        if (u.ia.stage % cnt == q.frame) snd_.play_at(q.sound, u.fx, u.fy, "sequence sound");
                }
            }
            p.seq = u.ia.seq;
            p.stage = u.ia.stage;
            p.alive = u.alive;
            p.inf_target = u.infiltrate_target;
            p.inf_purpose = u.infiltrate_purpose;
            if (!p.seen) {
                p.seen = true;
                if (w6_live_ && u.alive && u.die_time < 0.0) w6_created(w, ba, u);
                p.bomb = u.bomb_frame >= 0;
                p.air = w6_airborne(u);
                p.wet = w6_wet(w, u);
                p.fx = u.fx;
                p.fy = u.fy;
                p.hp = u.hp;
                p.sinking = u.an.sinking;
                p.crashing = u.an.crashing;
                p.turret = u.turret_facing;
                continue;
            }
            // VoiceFeedback on the NowYellow edge, 30%, player-controlled.
            if (u.house == g_local_house && u.alive && u.die_time < 0.0 && u.max_hp > 0 && u.hp > 0 && u.hp < p.hp &&
                !v.feedback.empty()) {
                const double was = static_cast<double>(p.hp) / u.max_hp, now = static_cast<double>(u.hp) / u.max_hp;
                if (was > w5_yellow_ && now <= w5_yellow_) {
                    w5_rng_ = w5_rng_ * 1103515245u + 12345u;
                    if ((w5_rng_ >> 8) % 100u < 30u) snd_.play_at(pick_report(v.feedback), u.fx, u.fy, "feedback");
                }
            }
            // UnitRepaired: a player vehicle the service depot brought to full.
            if (u.house == g_local_house && u.alive && u.die_time < 0.0 && u.hp >= u.max_hp && p.hp < u.max_hp && p.hp > 0 &&
                u.is_vehicle && w5_next_to_depot(w, ba, u) && radar_event(8, u.fx, u.fy))
                snd_.eva("EVA_UnitRepaired");
            // Sinking / crashing start: voice, then the sound on a controller.
            if (u.an.sinking && !p.sinking) {
                PresSinker sk;   // W7-12-11
                sk.unit = static_cast<int>(i);
                sk.start = g_pres_tick;
                sk.fx = u.fx;
                sk.fy = u.fy;
                sk.level = u.level;
                sk.body = u.body_facing;
                sk.turret = u.turret_facing;
                sk.house = u.house;
                sk.image = u.image;
                g_pres_sinkers.push_back(sk);
                ++w5_counts_["sinking hull"];
                snd_.play_at(v.voice_sinking, u.fx, u.fy, "voice sinking");
                const std::string& s = v.sinking.empty() ? w5_sinking_ : v.sinking;
                p.loop = w5_loop(s, u.fx, u.fy, "sinking");
            }
            if (u.an.crashing && !p.crashing) {
                snd_.play_at(v.voice_crashing, u.fx, u.fy, "voice crashing");
                p.loop = w5_loop(v.crashing, u.fx, u.fy, "crashing");
            }
            if (p.loop && (!u.alive || (!u.an.sinking && !u.an.crashing))) {
                snd_.stop_loop(p.loop);
                p.loop = 0;
            } else if (p.loop && !snd_.update_loop(p.loop, u.fx, u.fy)) {
                p.loop = 0;
            }
            // TurretRotateSound while the turret turns.
            if (!v.turret_rotate.empty()) {
                const bool turning = u.alive && u.die_time < 0.0 && std::abs(u.turret_facing - p.turret) > 1e-6;
                w5_turret(p.turret_loop, turning, v.turret_rotate, u.fx, u.fy);
            }
            // W6-05-06 [RE: FlyLocomotion crash FUN_004bcd50 @4BCF83 -- the
            // falling aircraft reaching the ground plays the type's
            // ImpactWaterSound (+0x4C0, else [AudioVisual] ImpactWaterSound)
            // over water, else its ImpactLandSound (+0x4C4, else [AudioVisual]
            // ImpactLandSound) at its coords, then it is destroyed].
            if (p.crashing && !p.impact && (!u.alive || !u.an.crashing)) {
                p.impact = true;
                const bool water = w.is_water_cell(u.cell_x, u.cell_y);
                std::string k = w5_bld_key(pres_type_id(u), water ? "ImpactWaterSound" : "ImpactLandSound");
                if (k.empty()) k = w6_av(water ? "ImpactWaterSound" : "ImpactLandSound");
                w6_play(k, u.fx, u.fy, water ? "impact water" : "impact land");
            }
            w6_bomb(p.bomb, p.bomb_loop, u.alive && u.die_time < 0.0 && u.bomb_frame >= 0, u.fx, u.fy);
            w6_flight(p.air, p.move_loop, u);
            // W6-04-27 [RE-FALLBACK: the locomotor water edge was not traced]:
            // amphibious infantry (Tanya, SEAL, Boris) stepping into / out of
            // water play the type's EnterWaterSound / LeaveWaterSound.
            // W8-01-07 [RE: 0x51D4C0, the edge lives in InfantryClass::PlayAnim
            // on the +0x6E8 wet flag]: a passenger in limbo plays no anim, so
            // its flag keeps its last value while it rides; a swimmer boarding
            // from water plays LeaveWaterSound at its unload onto land, not at
            // boarding.
            if (u.cargo_of < 0) {
                const bool wet = w6_wet(w, u);
                if (wet != p.wet && u.alive && u.die_time < 0.0)
                    w6_play(w5_bld_key(pres_type_id(u), wet ? "EnterWaterSound" : "LeaveWaterSound"), u.fx, u.fy,
                            wet ? "enter water" : "leave water");
                p.wet = wet;
            }
            p.fx = u.fx;
            p.fy = u.fy;
            p.hp = u.hp;
            p.sinking = u.an.sinking;
            p.crashing = u.an.crashing;
            p.turret = u.turret_facing;
        }
        w6_live_ = true;   // units first seen after this tick are new arrivals
        // Buildings: turret rotation (Grand Cannon), engineer repair-to-full,
        // capture lines.
        const auto& bs = w.buildings();
        if (w5_b_.size() < bs.size()) w5_b_.resize(bs.size());
        for (std::size_t i = 0; i < bs.size(); ++i) {
            const ra2engine::Building& b = bs[i];
            W5B& p = w5_b_[i];
            const double cx = b.cx + (b.fw - 1) / 2.0, cy = b.cy + (b.fh - 1) / 2.0;
            const bool dead = !b.alive || b.state == ra2engine::Building::State::Dead;
            const ra2engine::BuildingTypeDef* t =
                (ba.ok && b.type >= 0 && b.type < static_cast<int>(ba.roster.size())) ? &ba.roster[static_cast<std::size_t>(b.type)]
                                                                                       : nullptr;
            if (!p.seen) {
                p = W5B{true, b.hp, b.house, b.def_turret_facing, b.being_repaired, 0, b.power_off};
                p.bomb = b.bomb_frame >= 0;
                // W6-05-06: a new building starts "not working" (+0x598 = 0)
                // and falls through to the WorkingSound edge below.
                if (!t || (w5_bld_key(t->id, "WorkingSound").empty() && w5_bld_key(t->id, "NotWorkingSound").empty()))
                    continue;
            }
            // W5-E2 [RE: FUN_00452350 / FUN_00452250 -- IsPlayerControl(owner)]:
            // the player's building powered down / up (toggle mode or trigger).
            if (!dead && b.house == g_local_house && b.power_off != p.power_off) note_power_toggle(!b.power_off);
            p.power_off = b.power_off;
            if (t) {
                const std::string& trs = w5_bld_key(t->id, "TurretRotateSound");
                if (!trs.empty())
                    w5_turret(p.turret_loop, !dead && std::abs(b.def_turret_facing - p.turret) > 1e-6, trs, cx, cy);
            }
            // Repair_To_Full by an engineer: an allied building jumped to full
            // health without the wrench running.
            if (!dead && b.house == g_local_house && b.hp >= b.max_hp && p.hp < b.max_hp && !p.wrench && b.house == p.house &&
                b.max_hp - p.hp > 8)
                snd_.play_at(w5_bld_repaired_, cx, cy, "building repaired");
            // Capture [RE: FUN_004481AB @448310]. A garrison flip is not a capture.
            if (!dead && b.house != p.house && p.house >= 0 && !w5_garrison_flip(static_cast<int>(i))) {
                const bool tech = t && t->capturable;
                if (tech) {
                    if (p.house == g_local_house) snd_.eva("EVA_TechBuildingLost");
                    if (b.house == g_local_house) {
                        const std::string& ev = w5_bld_key(t->id, "CaptureEvaEvent");
                        if (!ev.empty() && snd_.has_eva(ev)) snd_.eva(ev);
                    }
                } else if ((p.house == g_local_house || b.house == g_local_house) && radar_event(10, cx, cy)) {
                    snd_.eva("EVA_BuildingCaptured");
                }
            }
            // W6-05-06 [RE: BuildingClass AI @43C940 -- "working" (powered,
            // not mission 0x12/0x13) compared with +0x598; on a change the
            // type's WorkingSound (+0xC18) / NotWorkingSound (+0xC1C) starts
            // on the building's audio controller +0x584 (FUN_00713E60); the
            // controller follows the building otherwise (FUN_007141B0)].
            if (t) {
                const std::string& ws = w5_bld_key(t->id, "WorkingSound");
                const std::string& ns = w5_bld_key(t->id, "NotWorkingSound");
                if (!ws.empty() || !ns.empty()) {
                    const bool working = !dead && b.state == ra2engine::Building::State::Idle && !b.power_off &&
                                         !b.def_offline;
                    if (working != p.working) {
                        if (p.work_loop) snd_.stop_loop(p.work_loop);
                        p.work_loop = 0;
                        const std::string& k = working ? ws : ns;
                        if (!k.empty() && !dead) {
                            pres_log(frame_, "sound " + k + (working ? " (working)" : " (not working)"));
                            ++w5_counts_[working ? "working" : "not working"];
                            p.work_loop = snd_.loop_at(k, cx, cy);
                        }
                        p.working = working;
                    } else if (p.work_loop && !snd_.update_loop(p.work_loop, cx, cy)) {
                        p.work_loop = 0;
                    }
                }
            }
            w6_bomb(p.bomb, p.bomb_loop, !dead && b.bomb_frame >= 0, cx, cy);
            p.hp = b.hp;
            p.house = b.house;
            p.turret = b.def_turret_facing;
            p.wrench = b.being_repaired;
        }
        // Bridge repaired (a hut entered): fewer destroyed bridge cells.
        // W9-02-08: recounted only when a bridge cell changed (bridge_version
        // bumps on every section / high-bridge state change) -- it was a
        // full-map scan on every logic tick.
        if (w.bridges_enabled() && (w5_bridge_dead_ < 0 || w.bridge_version() != w5_bridge_ver_)) {
            w5_bridge_ver_ = w.bridge_version();
            const int d = w.bridge_cells_destroyed();
            if (w5_bridge_dead_ >= 0 && d < w5_bridge_dead_) {
                double bx = 0, by = 0;
                w5_bridge_pos(w, ba, bx, by);
                if (radar_event(14, bx, by)) snd_.eva("EVA_BridgeRepaired");
                snd_.play_at(w5_repair_bridge_, bx, by, "bridge repaired");
            }
            w5_bridge_dead_ = d;
        }
    }

public:
    // W6-04-27: transport boarding / unloading -- the TRANSPORT type's
    // EnterTransportSound / LeaveTransportSound (IFV IFVTransform, BFRT,
    // LCRF ...), else the [AudioVisual] default, at the transport.
    void w6_cargo_events(const ra2engine::World& w, const std::vector<ra2engine::CargoEvent>& ev) {
        w5_init_once();
        const auto& us = w.units();
        for (const ra2engine::CargoEvent& e : ev) {
            if (e.kind == ra2engine::CargoEvent::Kind::Refuse) continue;
            if (e.transport < 0 || e.transport >= static_cast<int>(us.size())) continue;
            const ra2engine::Unit& t = us[static_cast<std::size_t>(e.transport)];
            const bool board = e.kind == ra2engine::CargoEvent::Kind::Board;
            w6_play(w6_type_or_av(pres_type_id(t), board ? "EnterTransportSound" : "LeaveTransportSound"), t.fx, t.fy,
                    board ? "enter transport" : "leave transport");
        }
    }
    // Chrono Miner teleports: the miner type's ChronoOut/InSound, positional.
    void w6_harvest_events(const ra2engine::World& w, const std::vector<ra2engine::HarvestEvent>& ev) {
        w5_init_once();
        const auto& us = w.units();
        for (const ra2engine::HarvestEvent& e : ev) {
            const bool out = e.kind == ra2engine::HarvestEvent::Kind::ChronoOut;
            if (!out && e.kind != ra2engine::HarvestEvent::Kind::ChronoIn) continue;
            if (e.unit < 0 || e.unit >= static_cast<int>(us.size())) continue;
            const ra2engine::Unit& u = us[static_cast<std::size_t>(e.unit)];
            double x = u.fx, y = u.fy;
            if (out && e.unit < static_cast<int>(w5_u_.size()) && w5_u_[static_cast<std::size_t>(e.unit)].seen) {
                x = w5_u_[static_cast<std::size_t>(e.unit)].fx;
                y = w5_u_[static_cast<std::size_t>(e.unit)].fy;
            }
            w6_play(w6_type_or_av(pres_type_id(u), out ? "ChronoOutSound" : "ChronoInSound"), x, y,
                    out ? "chrono out" : "chrono in");
        }
    }

private:
    // Helpers ------------------------------------------------------------------
    std::string w6_type_or_av(const std::string& id, const char* key) {
        const std::string& k = w5_bld_key(id, key);
        return k.empty() ? w6_av(key) : k;
    }
    static bool w6_airborne(const ra2engine::Unit& u) {
        return u.is_aircraft && u.altitude > 0.0 && u.alive && u.die_time < 0.0 && !u.an.crashing;
    }
    bool w6_wet(const ra2engine::World& w, const ra2engine::Unit& u) const {
        // W7-12-09: Water or Beach, the PlayAnim wet test.
        return u.alive && !u.is_aircraft && !u.is_vehicle && u.cargo_of < 0 && !u.on_deck &&   // R-210: on a bridge deck
               (w.is_water_cell(u.cell_x, u.cell_y) || (w.land_speed_armed() && w.speed_land_at(u.cell_x, u.cell_y) == 6));
    }
    // W6-04-27 aircraft [RE-FALLBACK: the fly/jumpjet locomotor sound calls
    // were not traced]: AuxSound1 on take-off, AuxSound2 on landing, and the
    // type's MoveSound (soundmd Control=loop: KirovMoveLoop, RocketeerMoveLoop,
    // IntruderMoveLoop ...) running while it is in the air.
    void w6_flight(bool& p_air, std::uint32_t& p_loop, const ra2engine::Unit& u) {
        if (!u.is_aircraft) return;
        const bool air = w6_airborne(u);
        if (air && !p_air) {
            w6_play(w5_bld_key(pres_type_id(u), "AuxSound1"), u.fx, u.fy, "take off");
            const std::string& ms = w5_bld_key(pres_type_id(u), "MoveSound");
            if (!ms.empty() && !p_loop) {
                pres_log(frame_, "sound " + ms + " (flight loop)");
                ++w5_counts_["flight loop"];
                p_loop = snd_.loop_at(ms, u.fx, u.fy);
            }
        } else if (!air && p_air) {
            if (u.alive && u.die_time < 0.0 && !u.an.crashing)
                w6_play(w5_bld_key(pres_type_id(u), "AuxSound2"), u.fx, u.fy, "landing");
            if (p_loop) snd_.stop_loop(p_loop);
            p_loop = 0;
        } else if (air && p_loop && !snd_.update_loop(p_loop, u.fx, u.fy)) {
            p_loop = 0;
        }
        p_air = air;
    }
    // W6-05-06: [AudioVisual] keys (cached).
    const std::string& w6_av(const char* key) { return w5_bld_key("AudioVisual", key); }
    void w6_play(const std::string& key, double fx, double fy, const char* why) {
        if (key.empty()) return;
        ++w5_counts_[why];
        snd_.play_at(key, fx, fy, why);
    }
    // [RE: HouseClass::JustBuilt FUN_004E6F10 -- a factory's finished techno
    // plays the type's CreateSound (+0x4B8), else the Rules default by kind
    // (Unit +0x170 CreateUnitSound, Infantry +0x174 CreateInfantrySound,
    // Aircraft +0x178 CreateAircraftSound), at the object, for any house].
    // The presentation has no JustBuilt hook, so a new unit on or next to a
    // factory of its own house and kind counts as just built [RE-FALLBACK:
    // map reinforcements and paradrops arrive at waypoints, not factories].
    void w6_created(const ra2engine::World& w, const BuildingAssets& ba, const ra2engine::Unit& u) {
        if (!ba.ok) return;
        const int want = u.is_aircraft ? 4 : u.is_vehicle ? 0 : 1;
        bool at_factory = false;
        for (const ra2engine::Building& b : w.buildings()) {
            if (!b.alive || b.house != u.house || b.type < 0 || b.type >= static_cast<int>(ba.roster.size())) continue;
            const int fk = ba.roster[static_cast<std::size_t>(b.type)].factory_kind;
            if (fk == 0 || (want == 1 && fk != 1) || (want == 4 && fk != 4 && fk != 2) ||
                (want == 0 && fk != 2 && fk != 3))
                continue;
            const int dx = u.cell_x < b.cx ? b.cx - u.cell_x : (u.cell_x >= b.cx + b.fw ? u.cell_x - (b.cx + b.fw - 1) : 0);
            const int dy = u.cell_y < b.cy ? b.cy - u.cell_y : (u.cell_y >= b.cy + b.fh ? u.cell_y - (b.cy + b.fh - 1) : 0);
            if (std::max(dx, dy) <= 2) { at_factory = true; break; }
        }
        if (!at_factory) return;
        std::string k = w5_bld_key(pres_type_id(u), "CreateSound");
        if (k.empty())
            k = w6_av(u.is_aircraft ? "CreateAircraftSound" : u.is_vehicle ? "CreateUnitSound" : "CreateInfantrySound");
        w6_play(k, u.fx, u.fy, "create");
    }
    // W6-04-11 [RE-FALLBACK: the bomb object's AI was not traced in game.exe;
    // the rules keys are [AudioVisual] BombAttachSound (once, on planting)
    // and BombTickingSound (soundmd Control=loop, while the bomb is on)].
    void w6_bomb(bool& had, std::uint32_t& loop, bool has, double fx, double fy) {
        if (has && !had) {
            w6_play(w6_av("BombAttachSound"), fx, fy, "bomb attach");
            const std::string& tick = w6_av("BombTickingSound");
            if (!tick.empty()) {
                pres_log(frame_, "sound " + tick + " (bomb ticking)");
                ++w5_counts_["bomb ticking"];
                loop = snd_.loop_at(tick, fx, fy);
            }
        } else if (!has && had) {
            if (loop) snd_.stop_loop(loop);
            loop = 0;
        } else if (has && loop && !snd_.update_loop(loop, fx, fy)) {
            loop = 0;
        }
        had = has;
    }
    std::uint32_t w5_loop(const std::string& key, double fx, double fy, const char* why) {
        if (key.empty()) return 0;
        pres_log(frame_, std::string("sound ") + key + " (" + why + ")");
        ++w5_counts_[why];
        return snd_.loop_at(key, fx, fy);
    }
    void w5_turret(std::uint32_t& h, bool turning, const std::string& key, double fx, double fy) {
        if (turning && !h) {
            pres_log(frame_, "sound " + key + " (turret rotate)");
            ++w5_counts_["turret rotate"];
            h = snd_.loop_at(key, fx, fy);
            if (!h) h = 1;   // deviceless: remember "playing" so the log shows edges only
        } else if (!turning && h) {
            if (h != 1) snd_.stop_loop(h);
            h = 0;
        } else if (h > 1 && !snd_.update_loop(h, fx, fy)) {
            h = 1;   // one-shot ended while still turning: do not restart (one per turn)
        }
    }
    bool w5_next_to_depot(const ra2engine::World& w, const BuildingAssets& ba, const ra2engine::Unit& u) const {
        if (!ba.ok) return false;
        for (const ra2engine::Building& b : w.buildings()) {
            if (!b.alive || b.house != u.house || b.type < 0 || b.type >= static_cast<int>(ba.roster.size()) ||
                !ba.roster[static_cast<std::size_t>(b.type)].unit_repair)
                continue;
            const int dx = u.cell_x < b.cx ? b.cx - u.cell_x : (u.cell_x >= b.cx + b.fw ? u.cell_x - (b.cx + b.fw - 1) : 0);
            const int dy = u.cell_y < b.cy ? b.cy - u.cell_y : (u.cell_y >= b.cy + b.fh ? u.cell_y - (b.cy + b.fh - 1) : 0);
            if (std::max(dx, dy) <= 1) return true;
        }
        return false;
    }
    void w5_bridge_pos(const ra2engine::World& w, const BuildingAssets& ba, double& x, double& y) const {
        // The repair hut nearest any own engineer is not tracked; use the
        // nearest bridge-repair hut to the view centre [RE-FALLBACK: the radar
        // event cell].
        x = y = 0;
        double best = 1e18;
        const double vx = (vw_ / 2.0 - cam_.x) / 60.0 + (vh_ / 2.0 - cam_.y) / 30.0;
        const double vy = (vh_ / 2.0 - cam_.y) / 30.0 - (vw_ / 2.0 - cam_.x) / 60.0;
        for (const ra2engine::Building& b : w.buildings()) {
            if (!b.alive || !ba.ok || b.type < 0 || b.type >= static_cast<int>(ba.roster.size()) ||
                !ba.roster[static_cast<std::size_t>(b.type)].bridge_repair_hut)
                continue;
            const double d = (b.cx - vx) * (b.cx - vx) + (b.cy - vy) * (b.cy - vy);
            if (d < best) { best = d; x = b.cx; y = b.cy; }
        }
    }
    const std::string& w5_bld_key(const std::string& id, const char* key) {
        const std::string k = id + "|" + key;
        auto it = w5_bkeys_.find(k);
        if (it != w5_bkeys_.end()) return it->second;
        return w5_bkeys_.emplace(k, rules_ini_ ? pres_clean(rules_ini_->get(id, key)) : std::string()).first->second;
    }
    bool w5_garrison_flip(int b) const {
        auto it = w5_garrison_flip_.find(b);
        return it != w5_garrison_flip_.end() && frame_ - it->second <= 2;
    }

    struct W5U {
        bool seen = false;
        int hp = 0;
        bool sinking = false, crashing = false;
        double turret = 0.0;
        std::uint32_t loop = 0, turret_loop = 0;
        bool alive = true;
        int inf_target = -1, inf_purpose = 0;
        int seq = -1, stage = -1;
        bool bomb = false;               // W6: an Ivan bomb is attached
        std::uint32_t bomb_loop = 0;     // W6: its BombTickingSound loop
        bool impact = false;             // W6: the crash impact was played
        bool air = false;                // W6: airborne (aircraft / jumpjet)
        std::uint32_t move_loop = 0;     // W6: the aircraft MoveSound loop
        bool wet = false;                // W6: standing in water
        double fx = 0.0, fy = 0.0;       // W6: last tick's position (teleport origin)
    };
    struct SeqSound { int frame; std::string sound; };
    const Ini* w5_art_ini_ = nullptr;
    std::map<std::string, std::vector<SeqSound>> w5_seq_sounds_;
    const std::vector<SeqSound>& w5_seq_sounds(const std::string& image, int seq) {
        const std::string key = image + "#" + std::to_string(seq);
        auto it = w5_seq_sounds_.find(key);
        if (it != w5_seq_sounds_.end()) return it->second;
        std::vector<SeqSound> v;
        if (w5_art_ini_) {
            const std::string sec = pres_clean(w5_art_ini_->get(image, "Sequence"));
            if (!sec.empty()) {
                std::stringstream ss(pres_clean(w5_art_ini_->get(sec, std::string(ra2engine::inf_seq_name(seq)) + "Sounds")));
                std::string f, snd;
                while (v.size() < 2 && ss >> f >> snd) {   // at most 2 pairs (the 0x24-byte entry)
                    v.push_back(SeqSound{std::atoi(f.c_str()), snd});
                    snd_.ensure_sound(snd);
                }
            }
        }
        return w5_seq_sounds_.emplace(key, std::move(v)).first->second;
    }
    void w5_infiltrated(const ra2engine::World& w, int bi, int spy_house, int purpose) {
        if (bi < 0 || bi >= static_cast<int>(w.buildings().size())) return;
        const int victim = w.buildings()[static_cast<std::size_t>(bi)].house;
        using P = ra2engine::World::InfiltratePurpose;
        const char* vline = nullptr;   // the infiltrated player
        std::vector<const char*> sline;   // the spy's owner
        switch (static_cast<P>(purpose)) {
            case P::RadarLab: vline = "EVA_RadarSabotaged"; sline = {"EVA_BuildingInfRadarSabotaged"}; break;
            case P::PowerPlant: vline = "EVA_PowerSabotaged"; sline = {"EVA_BuildingInfiltrated", "EVA_EnemyBasePoweredDown"}; break;
            case P::Refinery: vline = "EVA_CashStolen"; sline = {"EVA_BuildingInfCashStolen"}; break;
            case P::StolenAllied: case P::StolenSoviet: case P::StolenThird:
                vline = "EVA_TechnologyStolen"; sline = {"EVA_BuildingInfiltrated", "EVA_NewTechnologyAcquired"}; break;
            case P::Factory: vline = "EVA_TechnologyStolen"; sline = {"EVA_BuildingInfiltrated"}; break;
            default: sline = {"EVA_BuildingInfiltrated"}; break;
        }
        pres_log(frame_, "infiltrated b" + std::to_string(bi) + " purpose=" + std::to_string(purpose));
        if (victim == 0 && vline) snd_.eva(vline);
        if (spy_house == g_local_house)
            for (const char* l : sline) snd_.eva(l);
    }
    struct W5B {
        bool seen = false;
        int hp = 0, house = -1;
        double turret = 0.0;
        bool wrench = false;
        std::uint32_t turret_loop = 0;
        bool power_off = false;
        bool working = false;            // W6: +0x598, the last "working" state
        std::uint32_t work_loop = 0;     // W6: the +0x584 audio controller
        bool bomb = false;
        std::uint32_t bomb_loop = 0;
    };
    std::vector<W5U> w5_u_;
    std::vector<W5B> w5_b_;
    std::map<std::string, std::string> w5_bkeys_;
    std::map<int, long> w5_garrison_flip_;
    std::map<std::string, long> w5_counts_;
    bool w5_inited_ = false;
    bool w6_live_ = false;   // W6: past the first tick (initial units are not "created")
    std::string w5_cheer_, w5_bld_repaired_, w5_garrisoned_, w5_crate_[7], w5_heal_crate_, w5_sinking_,
        w5_repair_bridge_, w5_credit_up_, w5_credit_down_;
    int w5_credit_ticks_ = 0;
    double w5_yellow_ = 0.5;
    std::uint32_t w5_rng_ = 0x5EED5u;   // audio-local (never the sim RNG)
    int w5_bridge_dead_ = -1;
    std::uint32_t w5_bridge_ver_ = 0;   // W9-02-08: bridge_version at the last count

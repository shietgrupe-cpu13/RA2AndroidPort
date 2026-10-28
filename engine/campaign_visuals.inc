// W3-FX: the map triggers' presentation actions, executed in the campaign
// scene (they were recorded only). Textually included INSIDE class GameScene;
// every member is inert unless campaign_ is set. Presentation only: the
// requests come from TriggerHost::present (typed, never hashed or
// saved), so the trigger log and every gate stay byte-identical.
//
// [RE: TActionClass::Execute FUN_006dce20]
//  41 PlayAnimAt   FUN_006e1800: AnimTypes[Value] at the waypoint cell centre
//                  + its ground height (+ bridge height on a bridge cell),
//                  loops 1. Index space: see camp_anim_list below.
//  42 DoExplosionAt (host, campaign_runtime.inc) -> the warhead's impact anim.
//  48 MoveCameraToWaypoint FUN_006d1990: TacticalClass pan to the waypoint,
//                  progress += speed table 0x84188c[Value] per frame
//                  {0.0015, 0.003, 0.0075, 0.03, 0.06}.
//  55 CreateRadarEvent FUN_0065f4b0(type = Value, waypoint cell).
//  88 ParticleAnim ParticleSystemTypes[Value] at the waypoint (ground z).
// 104 FlashTeam    FUN_006f1490: every member Flash(Value frames).
// 112 CenterCamera FUN_006d55e0: the view jumps to the waypoint.
// 113 MakeHouseCheer FUN_0050c6e0: CheerSound (the infantry cheer: W3-B).
// 114 SetTabTo     the sidebar tab Value.
// 131 FlashBuildingsOfType: every building of BuildingTypes[Value] flashes.
private:
    struct CampPan { bool on = false; double x0 = 0, y0 = 0, x1 = 0, y1 = 0, p = 0, rate = 0;
                     bool moved = false; int cx = 0, cy = 0; };   // W4-MC-05: the camera the pan last set
    CampPan camp_pan_;
    std::vector<std::string> camp_anims_, camp_psys_;
    // W6-06-04: the trigger-started particle systems (saved while alive).
    struct TrigFx { std::string name; double x, y, z; int id; };
    std::vector<TrigFx> trig_fx_;
    bool camp_lists_ = false;

    // PlayAnimAt / ParticleAnim index spaces. [RE: the AnimTypeClass and
    // ParticleSystemTypeClass arrays are find-or-allocated in [Animations] /
    // [ParticleSystems] list order, so a repeated name takes no new slot
    // (FUN_00668630 list pass).] Which RA2 rules.ini: the census of all 26
    // RA2 maps (68 PlayAnimAt, 9 ParticleAnim) fits ONLY the ra2.mix/local.mix
    // [Animations] (deduplicated: 479 ARRWSE / 471 ARRWDEST point at the bridge
    // hut / garrison, 238 FLAMEGUY on a burning building, 253 CHRONOTG on
    // every "Chrono" trigger) -- the expand01 patch inserted anims before the
    // ARRW* block (it would give CAWA19_AG / ARRWNE) -- while ParticleAnim 12
    // (SOV01 "Building 9") exists only in the expand01 [ParticleSystems]
    // (BigRedSmokeSys). [RE-FALLBACK: game.exe was not disassembled; the
    // lists are chosen by that census, docs/re/effects.md §5.]
    void camp_load_lists() {
        if (camp_lists_) return;
        camp_lists_ = true;
        auto dedup = [](const Ini& ini, const char* sec, std::vector<std::string>& out) {
            for (const auto& kv : ini.section(sec)) {
                const std::string v = camp::up(pres_clean(kv.second));
                if (!v.empty() && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
            }
        };
        if (camp::yr_map(cfg_.campaign_map)) dedup(campaign_->ra2_rules(), "Animations", camp_anims_);
        else dedup(camp::load_ra2_local_ini(ra2_, "rules.ini"), "Animations", camp_anims_);
        dedup(campaign_->ra2_rules(), "ParticleSystems", camp_psys_);
    }
    // A waypoint's world point: the cell centre at its ground height.
    bool camp_wp(int wp, int& x, int& y, int& level) {
        if (!campaign_->waypoint_xy(wp, x, y)) return false;
        const ra2assets::MapCell* mc = a_.map.at(x, y);
        level = mc ? mc->level : 0;
        return true;
    }

    // Once per scene update, after campaign_->update (with the audio pump).
    void campaign_visuals_pump() {
        if (!campaign_) return;
        // W4-MC-02 no soft-lock [RE-FALLBACK: the original releases a stuck
        // lock through its main-loop deadline DAT_00b066f4 and the game-end
        // paths]: a lock no trigger undoes is released after 2 game minutes
        // (the longest stock lock, SOV09, is 65 s), and a finished mission
        // never stays locked.
        if (input_locked_ && ((long)world_.vet_frame() - input_lock_frame_ > 1800 ||
                              campaign_->finished())) {
            input_locked_ = false;
            pres_log((long)world_.vet_frame(), "input lock released (safety)");
        }
        camp_pan_step();
        for (const ra2engine::VisualRequest& r : campaign_->take_visuals()) {
            int x = 0, y = 0, lv = 0;
            switch (r.action) {
            case 41: {   // PlayAnimAt
                camp_load_lists();
                if (r.value < 0 || r.value >= (int)camp_anims_.size() || !camp_wp(r.waypoint, x, y, lv)) break;
                const std::string& name = camp_anims_[(std::size_t)r.value];
                const FxAnimType* t = pres_.fx().spawn(name, x, y, lv, 0.0);
                if (!t && pres_.fx().theater_type(name)) t = pres_.fx().spawn(name, x, y, lv, 0.0);
                if (t) pres_.sound().play_at(t->report, x, y, "trigger anim");
                pres_log((long)world_.vet_frame(), "trigger anim " + name + (t ? "" : " (no art)"));
                break;
            }
            case 42: {   // DoExplosionAt: the warhead's AnimList by damage (FUN_0048a400)
                if (!camp_wp(r.waypoint, x, y, lv)) break;
                std::vector<std::string> list;
                {
                    std::stringstream ss(pres_clean(rules_ini_.get(r.text, "AnimList")));
                    for (std::string tok; std::getline(ss, tok, ',');)
                        if (!pres_clean(tok).empty()) list.push_back(pres_clean(tok));
                }
                if (list.empty()) break;
                const int n = (int)list.size();
                const std::string& name = list[(std::size_t)(std::min(std::max(0, r.value), n * 25 - 1) / 25)];
                if (const FxAnimType* t = pres_.fx().spawn(name, x, y, lv, 0.0))
                    pres_.sound().play_at(t->report, x, y, "trigger explosion");
                pres_log((long)world_.vet_frame(), "trigger explosion " + name);
                break;
            }
            case 48: case 112: {   // MoveCameraToWaypoint / CenterCamera
                if (!camp_wp(r.waypoint, x, y, lv)) break;
                if (r.action == 112) {
                    camp_pan_.on = false;
                    input_.center_on_cell(x, y);
                    break;
                }
                static const double kRate[5] = {0.0015, 0.003, 0.0075, 0.03, 0.06};
                camp_pan_.on = true;
                camp_pan_.moved = false;
                camp_view_cell(camp_pan_.x0, camp_pan_.y0);
                camp_pan_.x1 = x;
                camp_pan_.y1 = y;
                camp_pan_.p = 0.0;
                camp_pan_.rate = kRate[std::clamp(r.value, 0, 4)];
                break;
            }
            case 46:   // W4-MC-02 LockInput (FUN_00683c80): drop the pending
                       // drag / band state too (FUN_0054f3b0).
                input_locked_ = true;
                input_lock_frame_ = (long)world_.vet_frame();
                input_.cancel_gesture();
                pres_log((long)world_.vet_frame(), "trigger LockInput");
                break;
            case 47:   // W4-MC-02 UnlockInput (FUN_00683cd0)
                if (input_locked_) pres_log((long)world_.vet_frame(), "trigger UnlockInput");
                input_locked_ = false;
                break;
            case 55:   // CreateRadarEvent(type = Value)
                if (camp_wp(r.waypoint, x, y, lv)) pres_.radar_ping(r.value, x, y);
                break;
            case 88: {   // ParticleAnim
                camp_load_lists();
                if (r.value < 0 || r.value >= (int)camp_psys_.size() || !camp_wp(r.waypoint, x, y, lv)) break;
                {
                    const std::string& nm = camp_psys_[(std::size_t)r.value];
                    const int id = pres_.effects().spawn_system(nm, x * 256.0, y * 256.0, lv * 104.0);
                    if (id) trig_fx_.push_back({nm, x * 256.0, y * 256.0, lv * 104.0, id});   // W6-06-04
                }
                break;
            }
            case 104: {   // FlashTeam
                const long until = (long)world_.vet_frame() + std::max(1, r.value);
                for (int u : campaign_->team_unit_indices(r.team)) g_flash_units[u] = until;
                break;
            }
            case 131: {   // FlashBuildingsOfType
                const std::string id = campaign_->building_type_id(r.value);
                const long until = (long)world_.vet_frame() + 60;   // [RE-FALLBACK: flash length]
                for (int i = 0; i < (int)world_.buildings().size(); ++i) {
                    const ra2engine::Building& b = world_.buildings()[(std::size_t)i];
                    if (b.alive && b.type >= 0 && b.type < (int)ba_.roster.size() &&
                        camp::up(ba_.roster[(std::size_t)b.type].id) == id)
                        g_flash_buildings[i] = until;
                }
                break;
            }
            case 113:   // MakeHouseCheer: the infantry Cheer is the World's
                        // (agent B, make_house_cheer from the trigger log); the
                        // scene adds [AudioVisual] CheerSound (FUN_0050c6e0).
                pres_.sound().play(pres_clean(rules_ini_.get("AudioVisual", "CheerSound")), 0.0, "cheer");
                break;
            case 114:   // SetTabTo
                if (r.value >= 0 && r.value <= 3 && r.value != active_tab_) {
                    active_tab_ = r.value;
                    buildable_ = tab_buildable(active_tab_);
                }
                break;
            case 115: {   // FlashCameo [RE: 0x6E36C0 -> FlashCameo 0x6ABC80]: the
                          // named BuildingType's cameo, Value frames (W5-PRES R-039)
                std::string id = r.text;
                for (char& ch : id) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (id.empty()) id = campaign_->building_type_id(r.value);
                if (!id.empty()) g_cameo_flash[id] = g_pres_tick + std::max(1, r.value);
                break;
            }
            default:
                break;
            }
        }
    }
    // The cell at the centre of the tactical view (inverse of center_on_cell).
    void camp_view_cell(double& cx, double& cy) const {
        const double u = (playfield_w_ / 2.0 - cam_.x - 30.0) / 30.0;   // cx - cy
        const double v = (kWinH / 2.0 - cam_.y - 15.0) / 15.0;          // cx + cy
        cx = (u + v) / 2.0;
        cy = (v - u) / 2.0;
    }
    void camp_pan_step() {
        if (!camp_pan_.on) return;
        // W4-MC-05 [RE-FALLBACK: the original pans with input locked; where a
        // map unlocks mid-pan (SOV01: unlock at 26 s, the speed-0 pan runs
        // ~44 s) the player's own scroll wins]: anything else that moved the
        // camera since the last pan step (finger / arrow / edge / right-drag
        // scroll, a radar click) ends the scripted pan.
        if (camp_pan_.moved && !input_locked_ && (cam_.x != camp_pan_.cx || cam_.y != camp_pan_.cy)) {
            camp_pan_.on = false;
            return;
        }
        camp_pan_.p = std::min(1.0, camp_pan_.p + camp_pan_.rate);
        input_.center_on_cell(camp_pan_.x0 + (camp_pan_.x1 - camp_pan_.x0) * camp_pan_.p,
                              camp_pan_.y0 + (camp_pan_.y1 - camp_pan_.y0) * camp_pan_.p);
        camp_pan_.moved = true;
        camp_pan_.cx = cam_.x;
        camp_pan_.cy = cam_.y;
        if (camp_pan_.p >= 1.0) camp_pan_.on = false;
    }
public:

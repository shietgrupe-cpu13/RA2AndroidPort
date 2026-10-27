// w2air_play.inc -- wave-2 air/naval/superweapon (W2-AN-*) --play support,
// #included INSIDE the GameScene class body right after
// game_scene_play_probe.inc. Probe keys, named map points and dev cheats for
// scripts/play/{naval,air,sw}.play. Interactive-only: no gate reaches it.

    // The first selected live house-0 unit, or -1.
    int w2_first_selected() const {
        const auto& us = world_.units();
        for (int i = 0; i < static_cast<int>(us.size()); ++i)
            if (input_.selection().has(i) && us[i].house == local_house_ && us[i].alive && us[i].die_time < 0.0) return i;
        return -1;
    }
    bool w2_water_cell(int x, int y) const {
        const auto& g = world_.grid_for(ra2engine::MovementDomain::Water);
        if (x < 0 || y < 0 || x >= world_.grid_w() || y >= world_.grid_h() || g.empty()) return false;
        return g[static_cast<std::size_t>(y) * world_.grid_w() + x] != 0;
    }

    // P1: the centre cell whose 3x3 (the Chronosphere's source area) holds the
    // most selected live house-0 ground units -- where a player clicks "the
    // middle of the group" as the Chronoshift source. n = units in that 3x3.
    void w2_sel_pack(double& cx, double& cy, int& n) const {
        n = 0;
        std::vector<std::pair<int, int>> cells;
        for (const ra2engine::Unit& u : world_.units())
            if (usel(u) && u.house == local_house_ && u.alive && u.die_time < 0.0 && u.cargo_of < 0 &&
                !(u.is_aircraft && u.altitude > 0.0))
                cells.push_back({u.cell_x, u.cell_y});
        for (const auto& c0 : cells)
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const int x = c0.first + ox, y = c0.second + oy;
                    int k = 0;
                    for (const auto& c : cells)
                        if (std::abs(c.first - x) <= 1 && std::abs(c.second - y) <= 1) ++k;
                    if (k > n) { n = k; cx = x; cy = y; }
                }
    }

    // Named points (`point NAME` / `click point NAME`):
    //   sel_pack        (P1) see w2_sel_pack
    //   water_near_sel  a free water cell 4..8 cells from the first selected unit
    //                   that the selection can enter
    //   shore_near_sel  the nearest LAND cell (walkable, free) that touches water,
    //                   from the first selected unit
    //   beach_near_sel  the nearest WATER cell touching land from the selected unit
    //   pad_free        (reserved)
    bool w2_find_point(const std::string& name, double& cx, double& cy) const {
        const int si = w2_first_selected();
        if (si < 0) return false;
        const ra2engine::Unit& s = world_.units()[si];
        const int gw = world_.grid_w(), gh = world_.grid_h();
        auto free_at = [&](int x, int y) { return world_.occupancy().is_free(x, y) && world_.building_at_cell(x, y) < 0; };
        if (name == "sel_pack") {   // P1: the 3x3 holding the most selected units
            int n = 0;
            w2_sel_pack(cx, cy, n);
            return n > 0;
        }
        if (name == "water_near_sel") {
            for (int r = 6; r <= 12; ++r)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        const int x = s.cell_x + dx, y = s.cell_y + dy;
                        if (!w2_water_cell(x, y) || !free_at(x, y)) continue;
                        if (!world_.group_can_enter(input_.selection().ids(), local_house_, x, y)) continue;
                        // keep away from the shore so a group fans out on water
                        bool open = true;
                        for (int k = -1; k <= 1 && open; ++k)
                            for (int l = -1; l <= 1 && open; ++l) open = w2_water_cell(x + k, y + l);
                        if (!open) continue;
                        cx = x; cy = y;
                        return true;
                    }
            return false;
        }
        if (name == "yard_dock") {
            // The selected ship's own Naval Yard (UnitRepair=yes): the free
            // water cell touching its footprint nearest the dock pad
            // (DockingOffset0 = 384,-128 leptons from the centre).
            int best = -1, bx = 0, by = 0;
            for (const ra2engine::Building& b : world_.buildings()) {
                if (!b.alive || b.house != local_house_ || b.type < 0 || b.type >= static_cast<int>(ba_.roster.size()))
                    continue;
                if (!rp_flag(rules_, ba_.roster[b.type].id, "UnitRepair")) continue;
                const double dkx = (b.cx * 256.0 + b.fw * 128.0 + 384.0) / 256.0;
                const double dky = (b.cy * 256.0 + b.fh * 128.0 - 128.0) / 256.0;
                for (int y = b.cy - 1; y <= b.cy + b.fh; ++y)
                    for (int x = b.cx - 1; x <= b.cx + b.fw; ++x) {
                        const bool ring = x == b.cx - 1 || y == b.cy - 1 || x == b.cx + b.fw || y == b.cy + b.fh;
                        if (!ring || !w2_water_cell(x, y) || !free_at(x, y)) continue;
                        const int d = static_cast<int>((x - dkx) * (x - dkx) + (y - dky) * (y - dky));
                        if (best < 0 || d < best) { best = d; bx = x; by = y; }
                    }
                break;
            }
            if (best < 0) return false;
            cx = bx; cy = by;
            return true;
        }
        if (name == "shore_near_sel" || name == "beach_near_sel" || name == "shore_far_sel") {
            const bool want_land = name != "beach_near_sel";
            const int min_d2 = name == "shore_far_sel" ? 11 * 11 : 0;   // beyond MinimumRange=8
            int best = -1, bx = 0, by = 0;
            for (int y = 0; y < gh; ++y)
                for (int x = 0; x < gw; ++x) {
                    const bool water = w2_water_cell(x, y);
                    if (want_land ? (water || !world_.walkable(x, y)) : !water) continue;
                    if (!free_at(x, y)) continue;
                    bool touches = false;
                    for (int k = -1; k <= 1 && !touches; ++k)
                        for (int l = -1; l <= 1 && !touches; ++l)
                            touches = want_land ? w2_water_cell(x + k, y + l)
                                                : (world_.walkable(x + k, y + l) && !w2_water_cell(x + k, y + l));
                    if (!touches) continue;
                    const int d = (x - s.cell_x) * (x - s.cell_x) + (y - s.cell_y) * (y - s.cell_y);
                    if (d < min_d2) continue;
                    if (best < 0 || d < best) { best = d; bx = x; by = y; }
                }
            if (best < 0) return false;
            cx = bx; cy = by;
            return true;
        }
        return false;
    }

    void w2_play_probe(ra2play::Probe& p) const {
        p.set_s("cursor_name", ra2engine::Input::action_name(cursor_action()));
        for (const char* f : {"submerged", "sub_detected_by0", "iron_units", "chrono_units", "airborne",
                              "spawned_out", "planes", "planes_docked", "plane_ammo", "amphib_on_water",
                              "unit_hp", "cargo_total", "iron_buildings", "sw_has", "sw_ready_t",
                              "sw_charge", "sunk", "crashing", "free", "spawn_ready", "spawn_out",
                              "spawn_pending", "spawn_regen", "launch_hold"})
            p.zero_families.insert(f);
        p.set("sw_armed", input_.sw_targeting() ? input_.sw_armed_type() : 0);
        p.set("sw_armed_phase", input_.sw_armed_phase());
        {   // P1: selected units inside the best 3x3 (named point sel_pack)
            double px = 0, py = 0;
            int n = 0;
            w2_sel_pack(px, py, n);
            p.set("sel_pack_n", n);
        }
        // Superweapons v2: per house and type (TYPE = Nuke IronCurtain
        // LightningStorm ChronoSphere ParaDrop AmerParaDrop).
        for (int h = 0; h < world_.house_count(); ++h) {
            const std::string hs = std::to_string(h);
            int nsw = 0;
            for (ra2engine::SuperweaponType t : world_.sw_types(h)) {
                ++nsw;
                const std::string k = hs + "," + ra2engine::superweapon_type_name(t);
                p.set("sw_has(" + k + ")", 1);
                p.set("sw_ready_t(" + k + ")", world_.sw_ready(h, t) ? 1 : 0);
                p.set("sw_charge(" + k + ")", world_.sw_charge_fraction(h, t));
            }
            p.set("sw_count(" + hs + ")", nsw);
        }
        p.set("sw_strikes", static_cast<double>(world_.sw_strikes().size()));
        int launched = 0, detonated = 0, activated = 0, bolts = 0, dropped = 0;
        for (const ra2engine::SwEvent& e : world_.sw_events()) {
            launched += e.kind == ra2engine::SwEvent::Launched;
            detonated += e.kind == ra2engine::SwEvent::Detonated;
            activated += e.kind == ra2engine::SwEvent::Activated;
            bolts += e.kind == ra2engine::SwEvent::Bolt;
            dropped += e.kind == ra2engine::SwEvent::Dropped;
        }
        p.set("sw_ev_launched", launched);
        p.set("sw_ev_detonated", detonated);
        p.set("sw_ev_activated", activated);
        p.set("sw_ev_bolts", bolts);
        p.set("sw_ev_dropped", dropped);
        p.set("sw_ev_seq", world_.sw_events().empty() ? 0 : world_.sw_events().back().seq);
        // P1: where the last nuke was launched at (house, target cell).
        for (auto it = world_.sw_events().rbegin(); it != world_.sw_events().rend(); ++it)
            if (it->kind == ra2engine::SwEvent::Launched) {
                p.set("sw_last_nuke_house", it->house);
                p.set("sw_last_nuke_x", it->x);
                p.set("sw_last_nuke_y", it->y);
                break;
            }
        // Naval / aircraft per house.
        for (const ra2engine::Unit& u : world_.units()) {
            if (!u.alive || u.die_time >= 0.0) continue;
            const std::string hs = std::to_string(u.house);
            if (u.an.submerged) p.add("submerged(" + hs + ")", 1);
            if (u.an.submerged && (u.an.detected_mask & 1u)) p.add("sub_detected_by0(" + hs + ")", 1);
            if (u.an.iron_left_frames > 0.0) p.add("iron_units(" + hs + ")", 1);
            if (u.an.chrono_left_frames > 0.0) p.add("chrono_units(" + hs + ")", 1);
            if (u.is_aircraft && u.altitude > 0.0) p.add("airborne(" + hs + ")", 1);
            if (u.an.t.spawned && u.cargo_of < 0) p.add("spawned_out(" + hs + ")", 1);
            if (u.an.t.fixed_wing && !u.an.t.spawned) {
                p.add("planes(" + hs + ")", 1);
                if (u.an.sortie == 0 && u.altitude <= 0.0) p.add("planes_docked(" + hs + ")", 1);
                p.add("plane_ammo(" + hs + ")", u.an.ammo);
            }
            if (u.an.in_water && !u.is_naval) p.add("amphib_on_water(" + hs + ")", 1);
            p.add("unit_hp(" + hs + "," + play_unit_id(u) + ")", u.hp);
            {
                const std::string kx = "pos_x(" + hs + "," + play_unit_id(u) + ")";
                if (!p.n.count(kx)) {
                    p.set(kx, u.cell_x);
                    p.set("pos_y(" + hs + "," + play_unit_id(u) + ")", u.cell_y);
                }
            }
            if (u.cargo_of < 0) p.add("cargo_total(" + hs + ")", static_cast<double>(u.cargo.size()));
            // W7-06-03/06/16: the spawn manager -- docked, out, reloading and
            // rebuilding nodes, and owners holding still for their rockets.
            if (!u.an.sp_state.empty()) {
                p.add("spawn_ready(" + hs + ")", u.an.spawn_ready);
                p.add("spawn_out(" + hs + ")", u.an.spawn_out);
                p.add("spawn_pending(" + hs + ")", u.an.spawn_pending);
                int dead = 0;
                for (int st : u.an.sp_state) dead += st == ra2engine::kSpawnDead;
                p.add("spawn_regen(" + hs + ")", dead);
                if (u.an.launch_hold) p.add("launch_hold(" + hs + ")", 1);
            }
        }
        p.set("sonic_waves", static_cast<double>(world_.an_waves_count()));
        for (const ra2engine::Building& b : world_.buildings())
            if (b.alive && b.iron_left_frames > 0.0) p.add("iron_buildings(" + std::to_string(b.house) + ")", 1);
        // W4: RocketLocomotion rockets per house -- launched (ever spawned),
        // shot down (killed by damage before detonating) and detonated.
        for (const char* f : {"rockets_launched", "rockets_shot", "rockets_hit", "rockets_live", "bhp"})
            p.zero_families.insert(f);
        for (const ra2engine::Unit& u : world_.units()) {
            if (!u.an.t.missile) continue;
            const std::string hs = std::to_string(u.house);
            p.add("rockets_launched(" + hs + ")", 1);
            if (u.an.rocket_end == 2) p.add("rockets_shot(" + hs + ")", 1);
            if (u.an.rocket_end == 1) p.add("rockets_hit(" + hs + ")", 1);
            if (u.alive && u.die_time < 0.0) {   // live rockets: count, highest altitude / phase
                p.add("rockets_live(" + hs + ")", 1);
                const std::string ka = "rocket_alt(" + hs + ")", kp = "rocket_phase(" + hs + ")";
                if (!p.n.count(ka) || p.n.at(ka) < u.altitude) p.set(ka, u.altitude);
                if (!p.n.count(kp) || p.n.at(kp) < u.an.rocket.phase) p.set(kp, u.an.rocket.phase);
            }
        }
        {   // W4: homing projectiles in flight (count, top height, nearest miss distance)
            int nh = 0, top = 0, near = -1, spd = 0;
            for (const ra2engine::Projectile& q : world_.projectiles()) {
                if (q.kind != ra2engine::ProjKind::Homing) continue;
                ++nh;
                top = std::max(top, q.z);
                spd = std::max(spd, static_cast<int>(q.speed));
                if (q.prev_dist >= 0 && (near < 0 || q.prev_dist < near)) near = q.prev_dist;
            }
            p.set("homing_live", nh);
            p.set("homing_top", top);
            p.set("homing_near", near);
            p.set("homing_speed", spd);
        }
        for (const ra2engine::Building& b : world_.buildings()) {   // W4: first live building's HP
            if (!b.alive || b.state == ra2engine::Building::State::Dead) continue;
            if (b.type < 0 || b.type >= static_cast<int>(ba_.roster.size())) continue;
            const std::string k = "bhp(" + std::to_string(b.house) + "," + ba_.roster[(std::size_t)b.type].id + ")";
            if (!p.n.count(k)) p.set(k, b.hp);
        }
        for (const ra2engine::Unit& u : world_.units())   // W2-AN-25: ever sunk (dying or gone)
            if (u.an.sinking) p.add("sunk(" + std::to_string(u.house) + ")", 1);
        for (const ra2engine::Unit& u : world_.units())   // W2-AN-07: falling wrecks
            if (u.alive && u.an.crashing) p.add("crashing(" + std::to_string(u.house) + ")", 1);
        p.set("rad_total", world_.rad_field().total());
        const int si = w2_first_selected();
        if (si >= 0) {
            const ra2engine::Unit& u = world_.units()[si];
            p.set("sel_on_water", w2_water_cell(u.cell_x, u.cell_y) ? 1 : 0);
            p.set("sel_hp", u.hp);
            p.set("sel_max_hp", u.max_hp);
            p.set("sel_cargo", static_cast<double>(u.cargo.size()));
            p.set("sel_altitude", u.altitude);
            p.set("sel_flight_state", static_cast<int>(u.flight_state));
            p.set("sel_flight_goal", u.has_flight_goal ? 1 : 0);
            p.set("sel_unload_pending", u.unload_pending ? 1 : 0);
        }
    }

    // ---------------------------------------------------------------------
    // Scene set-up (rp_setup): the World's SW / naval / aircraft parameters,
    // the per-building SW / UnitRepair / Helipad flags, the GAAIRC dock pads,
    // and the spawned / paradrop unit types (+ their render assets).
    // ---------------------------------------------------------------------
    std::deque<VehicleAssets> w2_va_;   // spawned / paradrop hulls (stable addresses)
    int w2_chrono_x_ = -1, w2_chrono_y_ = -1;   // Chronosphere source pick
    int w2_armed_ = 0;                          // SuperweaponType armed via the sidebar

    void w2_setup(bool skirmish) {
        world_.set_sw_params(resolve_sw_params(rules_));
        auto launch_profile=[&](const std::string& id) {
            if(id.empty())return;
            const auto weapons=ra2engine::resolve_type_weapons(rules_,id);
            const std::string primary=weapons.slot[0].id, secondary=weapons.slot[1].id;
            const std::string weapon=rules_.section_flag(primary,"Spawner",false)?primary:
                (rules_.section_flag(secondary,"Spawner",false)?secondary:"");
            if(weapon.empty())return;
            ra2engine::SpawnLaunchProfile p;
            art_.fire_flh(rules_.image(id),p.second.data(),"SecondSpawnOffset");
            p.decloak=rules_.section_flag(weapon,"DecloakToFire",true);
            world_.set_spawn_launch_profile(id,p);
        };
        // Configuration enters the synchronized World: enumerate the full
        // rules registry, not this peer's local faction/sidebar roster.
        for(const char* list:{"VehicleTypes","AircraftTypes","InfantryTypes"})
            for(const auto& id:ra2engine::ini_list_values(rules_ini_,list))launch_profile(id);
        for(const auto& u:world_.units())launch_profile(u.tg.t.rules_id);
        const std::size_t n = ba_.roster.size();
        std::vector<int> swb(n, 0), rep(n, 0), air(n, 0);
        std::vector<std::pair<int, int>> pads;
        for (std::size_t i = 0; i < n; ++i) {
            const std::string& id = ba_.roster[i].id;
            if (skirmish && i < rp_sw_table_.size()) swb[i] = static_cast<int>(rp_sw_table_[i].type);
            rep[i] = rp_flag(rules_, id, "UnitRepair") ? 1 : 0;
            air[i] = rp_flag(rules_, id, "Helipad") ? 1 : 0;
            if (air[i] && pads.empty() && i < ba_.dock_offsets.size())
                for (const ra2engine::LeptonXY& o : ba_.dock_offsets[i]) pads.push_back({o.x, o.y});
        }
        world_.set_sw_building_types(swb);
        world_.set_an_building_flags(rep, air);
        world_.set_dock_offsets(pads);
        // Spawned aircraft / missiles (Spawns=, V3/DMisl types) + the paradrop
        // plane and its infantry.
        const ra2engine::SwParams sp = world_.sw_params();
        std::vector<std::string> ids = {"PDPLANE", sp.amer_para_inf, sp.ally_para_inf, sp.sov_para_inf,
                                        rules_.section_string("General", "DMislType", "DMISL"),
                                        rules_.section_string("General", "V3RocketType", "V3ROCKET"),
                                        "HORNET", "ASW",   // any side's spawns (captured / cheat units)
                                        "BPLN"};           // wave-5 R-046: the AirstrikeTeamType
        for (const UnitRoster* r : {&ur_, &ur_ai_})
            for (const ra2engine::UnitTypeDef& d : r->defs)
                if (!d.combat.an.spawns.empty()) ids.push_back(d.combat.an.spawns);
        std::vector<std::string> vehicle_ids, infantry_ids;
        for (const std::string& id : ids) {
            if (id.empty() || world_.has_an_type(id)) continue;
            ResolvedUnit ru = resolve_combat_unit(rules_, id);
            if (ru.combat.image.empty()) continue;
            world_.register_an_type(id, ru.combat);
            (ru.combat.is_vehicle ? vehicle_ids : infantry_ids).push_back(id);
        }
        for (const std::string& id : vehicle_ids) {
            if (gf_has_vehicle_art(rules_.image(id))) continue;
            VehicleAssets va = load_vehicle_assets(dir_, ra2_, ua_.pal, id,&rules_);
            if (!va.ok) {
                std::cerr << "[w2] no voxel art for spawned " << id << "\n";
                continue;
            }
            w2_va_.push_back(std::move(va));
            vlist_.push_back(&w2_va_.back());
        }
        if (!infantry_ids.empty()) gf_load_assets(infantry_ids);
        // W2-AN-15: SHP ships (Dolphin, Giant Squid: artmd Voxel=no) -- body
        // SHP from conquer.mix, the vehicle SHP layout [WalkFrames x 8 facings,
        // then FiringFrames x 8 facings].
        for (const UnitRoster* r : {&ur_, &ur_ai_})
            for (const ra2engine::UnitTypeDef& d : r->defs) {
                if (!d.combat.is_vehicle || art_.voxel(d.combat.image)) continue;
                if (gf_has_vehicle_art(d.combat.image) || ua_.shps.count(d.combat.image)) continue;
                try {
                    MixFile conquer = ra2_.open_nested("conquer.mix");
                    ua_.shps.emplace(d.combat.image, load_unit_shp(conquer, d.combat.image));
                    const int walk = std::max(1, static_cast<int>(std::atof(
                        art_ini_.get(d.combat.image, "WalkFrames", "1").c_str())));
                    const int fire = std::max(1, static_cast<int>(std::atof(
                        art_ini_.get(d.combat.image, "FiringFrames", "1").c_str())));
                    ra2engine::UnitAnimSeq a;
                    a.stand = {0, 1, walk};
                    a.walk = {0, walk, walk};
                    a.fire = {walk * 8, fire, fire};
                    a.die = {0, 1, 0};
                    ua_.anims[d.combat.image] = a;
                    world_.set_anim(d.combat.image, a);
                    std::cerr << "[w2] SHP ship " << d.id << " image=" << d.combat.image << "\n";
                } catch (const std::exception& ex) {
                    std::cerr << "[w2] SHP ship " << d.id << " art unavailable (" << ex.what() << ")\n";
                }
            }
        std::cerr << "[w2] setup pads=" << pads.size() << " an-types=" << vehicle_ids.size()
                  << "+" << infantry_ids.size() << "\n";
    }

    // Sidebar SW slot clicked (READY): arm that SW's targeting.
    void w2_arm_sw(ra2engine::SuperweaponType t) {
        w2_armed_ = static_cast<int>(t);
        w2_chrono_x_ = w2_chrono_y_ = -1;
        input_.arm_superweapon(w2_armed_, 0);
        pres_.note_eva("EVA_SelectTarget");   // W5-E [RE: FUN_006AA390 @6AA632]
    }
    // A committed SW target cell. The Chronosphere takes two picks [rules
    // [ChronoSphereSpecial] PreClick=yes, [ChronoWarpSpecial] PostClick=yes]:
    // the source area, then the destination.
    void w2_sw_target(int cx, int cy) {
        using T = ra2engine::SuperweaponType;
        T t = static_cast<T>(w2_armed_);
        if (t == T::None) {
            const ra2engine::Superweapon* s = world_.sw_state(local_house_);
            if (!s) return;
            t = s->type;
        }
        if (t == T::ChronoSphere && w2_chrono_x_ < 0) {
            w2_chrono_x_ = cx;
            w2_chrono_y_ = cy;
            input_.arm_superweapon(w2_armed_, 1);   // now pick the destination
            return;
        }
        // NET L1: EVENT_SPECIAL_PLACE (SWIdx, cell); the Chronosphere's second
        // pick rides in the same command (the codec sends it as the ChronoWarp
        // SPECIAL_PLACE).
        ra2engine::Command c;
        c.type = ra2engine::EventType::SpecialPlace;
        c.value = static_cast<std::int32_t>(t);
        if (t == T::ChronoSphere) {
            c.cx = static_cast<std::int16_t>(w2_chrono_x_);
            c.cy = static_cast<std::int16_t>(w2_chrono_y_);
            c.cx2 = static_cast<std::int16_t>(cx);
            c.cy2 = static_cast<std::int16_t>(cy);
        } else {
            c.cx = static_cast<std::int16_t>(cx);
            c.cy = static_cast<std::int16_t>(cy);
        }
        issue(std::move(c));

        w2_armed_ = 0;
        w2_chrono_x_ = w2_chrono_y_ = -1;
    }

    // Cheats: `cheat w2 ...` (dev hooks; acceptance scripts prefer real input).
    //   w2 damage H:TYPE[:n] DMG  damage a unit (yard-repair set-up)
    bool w2_cheat(const std::vector<std::string>& a, std::string& out) {
        out.clear();
        if (a.size() < 2 || a[0] != "w2") return false;
        if (a[1] == "build" && a.size() >= 3) {
            // w2 build ID [H]: an instant, fully built structure on the nearest
            // legal spot to house 0's first building (the sidebar's own
            // placement test) -- a set-up shortcut like B's `building` cheat.
            int t = -1;
            for (int i = 0; i < static_cast<int>(ba_.roster.size()); ++i)
                if (ba_.roster[i].id == a[2]) t = i;
            if (t < 0) { out = "unknown building " + a[2]; return true; }
            const int house = a.size() >= 4 ? std::atoi(a[3].c_str()) : 0;
            int ax = -1, ay = -1;
            for (const ra2engine::Building& b : world_.buildings())
                if (b.alive && b.house == local_house_ && b.state != ra2engine::Building::State::Dead) {
                    ax = b.cx + b.fw / 2;
                    ay = b.cy + b.fh / 2;
                    break;
                }
            if (ax < 0) { out = "no own building"; return true; }
            const ra2engine::BuildingTypeDef& d = ba_.roster[t];
            for (int r = 1; r <= 30; ++r)
                for (int oy = ay - r; oy <= ay + r; ++oy)
                    for (int ox = ax - r; ox <= ax + r; ++ox) {
                        if (std::max(std::abs(ox - ax), std::abs(oy - ay)) != r) continue;
                        if (!placement_ok(t, ox, oy)) continue;
                        world_.add_building(t, ox, oy, d.fw, d.fh, house, d.strength, d.armor_ix,
                                            ba_.art[t].buildup_total, /*instant=*/true, d.sight,
                                            d.grants_map_reveal);
                        return true;
                    }
            out = "no spot for " + a[2];
            return true;
        }
        if (a[1] == "kill" && a.size() >= 3) {
            // w2 kill H:TYPE[:n]: a lethal hit on that unit (the combat death
            // path: die_time 0, occupancy released) -- crash / sink checks.
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : a[2]) {
                if (ch == ':') { parts.push_back(cur); cur.clear(); }
                else cur += ch;
            }
            parts.push_back(cur);
            if (parts.size() < 2) { out = "kill H:TYPE[:n]"; return true; }
            const int h = std::atoi(parts[0].c_str());
            const int nth = parts.size() > 2 ? std::atoi(parts[2].c_str()) : 0;
            int seen = 0;
            for (int i = 0; i < static_cast<int>(world_.units().size()); ++i) {
                ra2engine::Unit& u = world_.units()[i];
                if (!u.alive || u.die_time >= 0.0 || u.house != h || play_unit_id(u) != parts[1]) continue;
                if (seen++ != nth) continue;
                u.hp = 0;
                u.die_time = 0.0;
                u.target = -1;
                u.path.clear();
                u.path_idx = 0;
                return true;
            }
            out = "no unit " + a[2];
            return true;
        }
        if (a[1] == "damage" && a.size() >= 4) {
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : a[2]) {
                if (ch == ':') { parts.push_back(cur); cur.clear(); }
                else cur += ch;
            }
            parts.push_back(cur);
            if (parts.size() < 2) { out = "damage H:TYPE[:n] DMG"; return true; }
            const int h = std::atoi(parts[0].c_str());
            const int nth = parts.size() > 2 ? std::atoi(parts[2].c_str()) : 0;
            int seen = 0;
            for (ra2engine::Unit& u : world_.units()) {
                if (!u.alive || u.die_time >= 0.0 || u.house != h) continue;
                bool match = false;
                for (const UnitRoster* r : {&ur_, &ur_ai_})
                    for (const ra2engine::UnitTypeDef& d : r->defs)
                        if (d.id == parts[1] && d.combat.image == u.image) match = true;
                if (!match || seen++ != nth) continue;
                u.hp = std::max(1, u.hp - std::atoi(a[3].c_str()));
                return true;
            }
            out = "no unit " + a[2];
            return true;
        }
        out = "unknown w2 cheat";
        return true;
    }

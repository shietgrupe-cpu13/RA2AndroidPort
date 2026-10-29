// realplay_scene.inc -- GameScene member functions owned by gap owner A
// (economy / production / base). #included INSIDE `class GameScene` (private
// section) so the scene body in main.cpp stays a thin caller and parallel
// fixers do not collide on it.
public:
    friend struct RealPlayProbe;   // --probe-realplay drives the real scene

    // Is a building of roster type `type` placeable with its origin at
    // (cx, cy) for the local house? L2: the ONE rule set lives in the World
    // (World::econ_placement: WaterBound, BaseNormal G-28, LocalSize W4-MC-04
    // [RE: construction.md §9]); the cursor and the PLACE command share it.
    ra2engine::PlacementCheck placement_eval(int type, int cx, int cy) const {
        return world_.econ_placement(local_house_, type, cx, cy);
    }
    bool placement_ok(int type, int cx, int cy) const { return placement_eval(type, cx, cy).ok; }
    // Direct placement for the local house (probes / cheats; real play goes
    // through the PLACE command, World::econ_place).
    bool place_building(int type, int cx, int cy) { return world_.econ_place_building(local_house_, type, cx, cy); }

    // L2: the local house's queues (read-only) and wallet -- the sidebar,
    // credits tally, EVA and probes read these; changes go through the
    // World's production commands (econ_produce/suspend/abandon/place).
    const ra2engine::ProductionQueues& lq() const {
        static const ra2engine::ProductionQueues kEmpty;
        const ra2engine::HouseEcon* e = world_.econ(local_house_);
        return e ? e->queues : kEmpty;
    }
    double lcredits() const { return world_.credits(local_house_); }
    int local_house() const { return local_house_; }

    // L2: every human house gets a World player economy; its wallet takes the
    // lobby Money launch_skirmish put in its pool (G-21). The scene issues
    // its orders as local_house_.
    void setup_player_economies() {
        for (int h : human_slots_of(cfg_)) {
            if (h >= world_.house_count() || world_.ai_enabled(h)) continue;
            world_.enable_player_economy(h, world_.take_income(h));
        }
        world_.set_order_issuer(local_house_);
    }
    // The production rules of human house h (asset-derived; re-installed
    // after a load and whenever the building roster grows).
    ra2engine::PlayerEconRules make_econ_rules(int h) const {
        ra2engine::PlayerEconRules r;
        const UnitRoster& ur = house_roster(h);
        r.units = ur.defs;
        r.unit_anims.resize(r.units.size());
        r.unit_has_anim.assign(r.units.size(), 0);
        for (std::size_t i = 0; i < r.units.size(); ++i) {
            const auto it = ua_.anims.find(r.units[i].combat.image);
            if (it == ua_.anims.end()) continue;
            r.unit_anims[i] = it->second;
            r.unit_has_anim[i] = 1;
        }
        r.factory_types[static_cast<int>(ra2engine::QueueCat::Structure)] = bld_ft_;
        r.factory_types[static_cast<int>(ra2engine::QueueCat::Defense)] = bld_ft_;
        r.factory_types[static_cast<int>(ra2engine::QueueCat::Infantry)] = inf_ft_;
        r.factory_types[static_cast<int>(ra2engine::QueueCat::Vehicle)] = veh_ft_;
        r.factory_types[static_cast<int>(ra2engine::QueueCat::Naval)] = nav_ft_;
        r.factory_types[static_cast<int>(ra2engine::QueueCat::Aircraft)] = air_ft_;
        r.groups = ba_.groups;
        r.water_bound = ba_.water_bound;
        r.wall = ba_.wall;
        r.wall_fill_range = ba_.wall_fill_range;
        r.buildup_total.resize(ba_.art.size());
        for (std::size_t i = 0; i < ba_.art.size(); ++i) r.buildup_total[i] = ba_.art[i].buildup_total;
        r.base_normal = rp_on_ ? rp_base_normal_ : std::vector<char>{};
        // W9-05-09: BuildOffAlly (the lobby option) + EligibileForAllyBuilding.
        r.build_off_ally = cfg_.build_off_ally;
        if (r.build_off_ally) {
            r.ally_eligible.assign(ba_.roster.size(), 0);
            for (std::size_t i = 0; i < ba_.roster.size(); ++i)
                r.ally_eligible[i] = rp_flag(rules_, ba_.roster[i].id, "EligibileForAllyBuilding") ? 1 : 0;
        }
        r.build_speed = build_speed_;
        r.low_power_mod = low_mod_;
        r.multiple_factory = mf_;
        r.wall_coefficient =
            rules_.general_double("WallBuildSpeedCoefficient", ra2engine::kWallBuildSpeedCoefficient);
        r.max_queued = max_queued();
        r.wall_rules = rp_on_;
        r.bld_allowed = bld_allowed_for(h);
        r.unit_build_limit.assign(r.units.size(), 0);
        for (std::size_t i = 0; i < r.units.size(); ++i)
            r.unit_build_limit[i] = static_cast<int>(rules_.section_double(r.units[i].id, "BuildLimit", 0.0));
        return r;
    }
    // House h's buildable building mask (the sidebar's rp_bld_allowed_ rule
    // for that house's country; the local house's is rp_bld_allowed_).
    // W9-05: computed the SAME way for the local house as for the others --
    // rp_bld_allowed_ is sized at rp_setup, before map tech buildings join
    // the roster, so the local peer's mask for its own house was shorter
    // (missing = allowed) than the one the other peers computed for it. The
    // econ-rules digest in the sync hash caught it (a frame-0 desync).
    std::vector<char> bld_allowed_for(int h) const {
        if (!cfg_.campaign_map.empty() || h < 0 || h >= static_cast<int>(cfg_.slots.size()))
            return rp_bld_allowed_;
        const std::string country = rp_player_country(rules_, cfg_.slots[(std::size_t)h]);
        const camp::PlayerRules pr;
        std::vector<char> m(ba_.roster.size(), 0);
        for (std::size_t i = 0; i < ba_.roster.size(); ++i) {
            const ra2engine::BuildingTypeDef& d = ba_.roster[i];
            m[i] = d.tech_level > 0 && d.tech_level <= cfg_.tech_level &&
                   rp_country_allows(rules_, d.id, country) && camp::allowed_type(pr, d.id);
            if (!cfg_.superweapons && m[i] && rp_is_lobby_sw_building(rules_, d.id)) m[i] = 0;
            if (m[i] && i < rp_sw_table_.size() &&
                rp_sw_table_[i].type != ra2engine::SuperweaponType::None) {
                for (const ra2engine::Building& b : world_.buildings())
                    if (b.alive && b.house == h && b.type == static_cast<int>(i)) { m[i] = 0; break; }
            }
        }
        return m;
    }
    void install_econ_rules() {
        for (int h = 0; h < world_.house_count(); ++h)
            if (world_.player_economy(h)) world_.set_player_econ_rules(h, make_econ_rules(h));
    }
    // House h's unit roster: the scene's ur_ is the LOCAL house's; another
    // human house of a different country gets its own (loaded on demand,
    // with its art registered so its units render).
    const UnitRoster& house_roster(int h) const {
        if (h == local_house_) return ur_;
        for (const auto& [hh, r] : extra_ur_)
            if (hh == h) return r;
        return const_cast<GameScene*>(this)->load_extra_roster(h);
    }
    UnitRoster& load_extra_roster(int h) {
        ra2engine::SkirmishConfig c = cfg_;
        if (!c.campaign_map.empty() || h < 0 || h >= static_cast<int>(c.slots.size()))
            return const_cast<UnitRoster&>(ur_);   // the campaign has one human house
        extra_ur_.emplace_back(h, load_player_unit_roster(dir_, ra2_, ua_, rules_, art_, rules_ini_,
                                                          rp_player_country(rules_, c.slots[(std::size_t)h]),
                                                          c.tech_level, /*with_aircraft=*/true));
        UnitRoster& r = extra_ur_.back().second;
        for (const VehicleAssets& v : r.vehicles) vlist_.push_back(&v);
        try {
            MixFile conquer = ra2_.open_nested("conquer.mix");
            for (const ra2engine::UnitTypeDef& d : r.defs) {
                if ((d.is_vehicle && d.combat.is_vehicle) || ua_.shps.count(d.combat.image)) continue;
                try {
                    ua_.shps.emplace(d.combat.image, load_unit_shp(conquer, d.combat.image));
                    ua_.anims.emplace(d.combat.image, anim_from_art(art_, d.combat.image));
                } catch (const std::exception&) {}
            }
        } catch (const std::exception&) {}
        return r;
    }
private:
    // --- gap owner A state (skirmish real play; empty/false in a campaign) ---
    bool                 rp_on_ = false;     // the real-play roster/sidebar path is active
    std::string          rp_country_;        // the human's country (Owner/Required/Forbidden)
    std::vector<char>    rp_bld_allowed_;    // per roster building: country + lobby tech ok
    std::vector<int>     nav_ft_;            // Naval=yes UnitType factories (GAYARD/NAYARD)
    std::vector<int>     air_ft_;            // AircraftType factories (GAAIRC/AMRADR)
    std::vector<int>     bld_ft_;            // BuildingType factories (the ConYards)
    std::vector<RpSwGrant> rp_sw_table_;     // per roster building: SW granted (G-25)
    SuperweaponParams    rp_nuke_;
    std::vector<char>    rp_base_normal_;
    std::vector<int>     rp_build_limit_;    // per unit def: rules BuildLimit= (0 = none)    // per roster building: rules BaseNormal (default yes)
    std::deque<std::pair<int, UnitRoster>> extra_ur_;   // L2: other human houses' rosters (stable)

    // The roster the ctor init list loads for the human: the full side/country/
    // tech-level roster in skirmish (G-22), the historical tables in a campaign.
    static UnitRoster rp_load_roster(const std::string& dir, const ra2assets::MixFile& ra2,
                                     const UnitAssets& ua, const Rules& rules, const Art& art,
                                     const Ini& rules_ini, const ra2engine::SkirmishConfig& cfg) {
        load_progress("ROSTER");   // W2-AR-02 loading-screen checkpoint
        // W2-CA-03: a campaign player builds from the same rules-driven roster
        // as skirmish -- its house's country (ParentCountry) and map
        // TechLevel, the map overrides already in `rules`, RA2 types only.
        if (!cfg.campaign_map.empty()) {
            const camp::PlayerRules pr =
                camp::campaign_player_rules(dir, ra2, cfg.campaign_map, cfg.campaign_side);
            return load_player_unit_roster(dir, ra2, ua, rules, art, rules_ini, pr.country,
                                           pr.tech_level, /*with_aircraft=*/true, &pr.allowed);
        }
        const ra2engine::SkirmishSlot s0 =
            cfg.slots.empty() ? ra2engine::SkirmishSlot{} : cfg.slots[(std::size_t)local_slot_of(cfg)];
        return load_player_unit_roster(dir, ra2, ua, rules, art, rules_ini,
                                       rp_player_country(rules, s0), cfg.tech_level,
                                       /*with_aircraft=*/true);   // W2-AN-01: fixed-wing in World
    }

    // Ctor tail (skirmish only): factories by rules Factory=, the building
    // country/tech mask, and the first sidebar list.
    void rp_setup() {
        load_progress("SIDEBAR");   // W2-AR-02 loading-screen checkpoint
        rp_on_ = true;
        const ra2engine::SkirmishSlot s0 =
            cfg_.slots.empty() ? ra2engine::SkirmishSlot{} : cfg_.slots[(std::size_t)local_house_];
        int tech = cfg_.tech_level;
        bool sw = cfg_.superweapons;
        camp::PlayerRules pr;   // W2-CA-03: campaign = the mission's player house
        if (!cfg_.campaign_map.empty()) {
            pr = camp::campaign_player_rules(dir_, ra2_, cfg_.campaign_map, cfg_.campaign_side);
            rp_country_ = pr.country;
            tech = pr.tech_level;
            sw = true;
        } else {
            rp_country_ = rp_player_country(rules_, s0);
        }
        // [RE: roster2.md TL;DR 7; production.md §1.1] every factory of the
        // category counts (a captured enemy factory still produces).
        inf_ft_ = rp_factory_types(ba_, rules_, "InfantryType");
        veh_ft_ = rp_factory_types(ba_, rules_, "UnitType", /*naval=*/false);
        nav_ft_ = rp_factory_types(ba_, rules_, "UnitType", /*naval=*/true);
        air_ft_ = rp_factory_types(ba_, rules_, "AircraftType");
        bld_ft_ = rp_factory_types(ba_, rules_, "BuildingType");
        std::vector<int> unit_ft = inf_ft_;
        for (const std::vector<int>* v : {&veh_ft_, &nav_ft_, &air_ft_})
            unit_ft.insert(unit_ft.end(), v->begin(), v->end());
        std::vector<int> any_veh = veh_ft_;
        any_veh.insert(any_veh.end(), nav_ft_.begin(), nav_ft_.end());
        any_veh.insert(any_veh.end(), air_ft_.begin(), air_ft_.end());
        input_.set_unit_factory_types(unit_ft);
        input_.set_infantry_factory_types(inf_ft_);
        input_.set_vehicle_factory_types(any_veh);
        // Every supported SW building grants its SW; the skirmish checkbox
        // controls all supported types. Campaign rules keep their own grants.
        rp_sw_table_ = rp_superweapon_table(ba_, rules_);
        if (!sw)
            for (RpSwGrant& g : rp_sw_table_)
                if (g.type != ra2engine::SuperweaponType::None) g = RpSwGrant{};
        rp_nuke_ = resolve_superweapon(rules_);
        w2_setup(/*skirmish=*/true);   // campaigns run the rules-driven path too (W2-CA-03)
        rp_base_normal_.assign(ba_.roster.size(), 1);
        for (std::size_t i = 0; i < ba_.roster.size(); ++i) {
            const std::string v = rules_.section_string(ba_.roster[i].id, "BaseNormal", "");
            if (!v.empty()) rp_base_normal_[i] = rp_flag(rules_, ba_.roster[i].id, "BaseNormal");
        }
        rp_build_limit_.assign(ur_.defs.size(), 0);
        for (std::size_t i = 0; i < ur_.defs.size(); ++i)
            rp_build_limit_[i] = static_cast<int>(rules_.section_double(ur_.defs[i].id, "BuildLimit", 0.0));
        rp_bld_allowed_.assign(ba_.roster.size(), 0);
        for (std::size_t i = 0; i < ba_.roster.size(); ++i) {
            const ra2engine::BuildingTypeDef& d = ba_.roster[i];
            rp_bld_allowed_[i] = d.tech_level > 0 && d.tech_level <= tech &&
                                 rp_country_allows(rules_, d.id, rp_country_) &&
                                 camp::allowed_type(pr, d.id);
            // [SK-27] Lobby Superweapons off: "Super Weapons can be built" is
            // false, so no SuperWeapon= structure is offered. [RE-FALLBACK: the
            // [AI] BuildTech tech centres stay buildable -- their YR
            // SuperWeapon= is the side special, not a lobby superweapon.]
            if (!sw && rp_bld_allowed_[i] && rp_is_lobby_sw_building(rules_, d.id))
                rp_bld_allowed_[i] = 0;
        }
        buildable_ = tab_buildable(active_tab_);
        lay_ = ra2engine::SidebarLayout::compute(kWinW, kWinH, buildable_);
    }

    ra2engine::QueueCat rp_cat_of_unit(int uslot) const {
        if (uslot < 0 || uslot >= static_cast<int>(ur_.defs.size()))
            return ra2engine::QueueCat::Infantry;
        return rp_unit_cat(ur_.defs[uslot]);
    }
    const std::vector<int>& rp_ft_of(ra2engine::QueueCat c) const {
        switch (c) {
            case ra2engine::QueueCat::Vehicle:  return veh_ft_;
            case ra2engine::QueueCat::Naval:    return nav_ft_;
            case ra2engine::QueueCat::Aircraft: return air_ft_;
            default:                            return inf_ft_;
        }
    }

    // [RE-FALLBACK: the RA2 sidebar strip lists a cameo once the item becomes
    // buildable (the "New construction options" moment); the StripClass add
    // path itself was not hunted]. Side membership falls out of the
    // prerequisites (GA* need GACNST, NA* need NACNST -- rulesmd Owner= lists
    // every country for buildings), country rules from Owner/Required/
    // ForbiddenHouses, and the lobby TechLevel. An item already in a queue
    // stays listed so it can be cancelled/placed.
    std::vector<int> rp_tab_buildable(int tab) const {
        std::vector<int> v;
        const std::vector<ra2engine::Building>& bs = world_.buildings();
        if (tab < 2) {
            const ra2engine::QueueCat bc =
                tab == 1 ? ra2engine::QueueCat::Defense : ra2engine::QueueCat::Structure;
            for (int i = 0; i < static_cast<int>(ba_.roster.size()); ++i) {
                const ra2engine::BuildingTypeDef& d = ba_.roster[i];
                if (!ba_.art[i].ok || d.cameo.empty()) continue;
                if (i >= static_cast<int>(rp_bld_allowed_.size()) || !rp_bld_allowed_[i]) continue;
                const bool def = i < static_cast<int>(ba_.defense.size()) && ba_.defense[i] != 0;
                if (def != (tab == 1)) continue;
                const bool queued = lq().producing(bc) == i;
                const bool water_bound = i < static_cast<int>(ba_.water_bound.size()) && ba_.water_bound[i];
                const auto& water = world_.grid_for(ra2engine::MovementDomain::Water);
                if (!queued && water_bound &&
                    std::none_of(water.begin(), water.end(), [](std::uint8_t cell) { return cell != 0; }))
                    continue;
                if (!queued &&
                    !ra2engine::prerequisites_met(d.prerequisite, bs, ba_.roster, local_house_, ba_.groups, &world_.units()))
                    continue;
                // A skirmish house may own at most one copy of each supported
                // superweapon building. Keep an already queued item visible so
                // it can still be cancelled from the sidebar.
                if (!queued && i < static_cast<int>(rp_sw_table_.size()) &&
                    rp_sw_table_[static_cast<std::size_t>(i)].type != ra2engine::SuperweaponType::None) {
                    const bool already_built = std::any_of(bs.begin(), bs.end(), [&](const ra2engine::Building& b) {
                        return b.alive && b.house == local_house_ && b.type == i;
                    });
                    if (already_built) continue;
                }
                v.push_back(i);
            }
            std::stable_sort(v.begin(), v.end(), [&](int a, int b) {
                return ba_.roster[a].tech_level < ba_.roster[b].tech_level;
            });
            if(tab==1) for(auto type:world_.sw_types(local_house_)) v.push_back(ra2engine::power_cameo_id(type));
            return v;
        }
        for (int i = 0; i < static_cast<int>(ur_.defs.size()); ++i) {
            const ra2engine::UnitTypeDef& d = ur_.defs[i];
            const ra2engine::QueueCat c = rp_unit_cat(d);
            if ((c == ra2engine::QueueCat::Infantry) != (tab == 2)) continue;
            const bool queued = lq().producing(c) == i;
            if (!queued && d.tech_level <= 0) continue;   // wave-5 R-042: tech not (yet) stolen
            if (!queued &&
                !ra2engine::prerequisites_met(d.prerequisite, bs, ba_.roster, local_house_, ba_.groups, &world_.units()))
                continue;
            // BuildLimit= (Tanya, Boris: 1): no new one while the limit is on
            // the field [RE-FALLBACK: counted by the unit's art image].
            if (!queued && i < static_cast<int>(rp_build_limit_.size()) && rp_build_limit_[i] > 0) {
                int alive = 0;
                for (const ra2engine::Unit& u : world_.units())
                    alive += u.alive && u.house == local_house_ && u.image == d.combat.image;
                if (alive >= rp_build_limit_[i]) continue;
            }
            v.push_back(i);
        }
        return v;
    }

    // Once per frame: the strip follows the buildable set (a finished Barracks
    // makes its units appear, a lost Tech Center removes its items).
    void rp_after_world_step() {
        // Wave-5 (R-056): map tech buildings (CAAIRP ParaDropSpecial) join the
        // roster after rp_setup -- extend the SW table to the grown roster.
        if (rp_on_ && rp_sw_table_.size() != ba_.roster.size()) {
            const std::vector<RpSwGrant> full = rp_superweapon_table(ba_, rules_);
            const std::size_t old_n = rp_sw_table_.size();
            rp_sw_table_.resize(full.size());
            for (std::size_t i = old_n; i < full.size(); ++i) rp_sw_table_[i] = full[i];
            std::vector<int> swb(rp_sw_table_.size(), 0);
            for (std::size_t i = 0; i < swb.size(); ++i) swb[i] = static_cast<int>(rp_sw_table_[i].type);
            world_.set_sw_building_types(swb);
            w5_arm_building_art(ba_, art_ini_);           // wave-5: art keys of the new types
            world_.refresh_building_types(ba_.roster);
            install_econ_rules();   // L2: the per-type placement extras follow the roster
        }
        if (rp_on_ && !rp_sw_table_.empty()) rp_sync_superweapons(world_, rp_sw_table_, rp_nuke_);
    }

    void rp_refresh_sidebar() {
        if (!rp_on_) return;
        std::vector<int> now = rp_tab_buildable(active_tab_);
        if (now != buildable_) {
            buildable_ = std::move(now);
            lay_ = ra2engine::SidebarLayout::compute(kWinW, kWinH, buildable_, lay_.top_row);   // keep the G-12 strip scroll
        }
    }

    // The unit-tab queue whose progress the strip draws: tab 2 = Infantry; tab
    // 3 = the first busy of Vehicle / Naval / Aircraft (one strip, three
    // concurrent factory queues -- only one clock is drawn at a time).
    ra2engine::QueueCat rp_vis_unit_cat() const {
        if (active_tab_ == 2) return ra2engine::QueueCat::Infantry;
        for (ra2engine::QueueCat c : {ra2engine::QueueCat::Vehicle, ra2engine::QueueCat::Naval,
                                      ra2engine::QueueCat::Aircraft})
            if (lq().producing(c) >= 0) return c;
        return ra2engine::QueueCat::Vehicle;
    }

    // (L2: the unit exit handoff moved into the World -- World::econ_launch_ready_units.)

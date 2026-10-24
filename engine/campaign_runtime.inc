// P10 Campaign runtime [RE: docs/re/campaign.md, docs/re/map-triggers.md].
//
// Textually included by main.cpp (inside its anonymous namespace, before
// GameScene) because it needs main.cpp's asset types (BuildingAssets,
// VehicleAssets, UnitAssets, the loaders). Everything here is reached ONLY from
// the campaign GameScene and the --demo-campaign gate: no existing mode or
// byte-identity gate constructs a CampaignRuntime.

namespace camp {

std::string up(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string trim_comment(std::string v) {
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v.erase(sc);
    while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
    std::size_t i = 0;
    while (i < v.size() && std::isspace((unsigned char)v[i])) ++i;
    return v.substr(i);
}
std::vector<std::string> list_values(const Ini& ini, const char* sec) {
    std::vector<std::string> out;
    for (const auto& kv : ini.section(sec)) {
        const std::string v = trim_comment(kv.second);
        if (!v.empty()) out.push_back(v);
    }
    return out;
}
int index_of(const std::vector<std::string>& v, const std::string& id) {
    const std::string u = up(id);
    for (std::size_t i = 0; i < v.size(); ++i) if (up(v[i]) == u) return (int)i;
    return -1;
}

// Read a file from ra2.mix/local.mix (the RA2 control INIs, RE §1).
Ini load_ra2_local_ini(const MixFile& ra2, const std::string& name) {
    try {
        MixFile local = ra2.open_nested("local.mix");
        if (const MixEntry* e = local.find(name)) {
            const auto raw = local.read(*e);
            return Ini::parse(std::string(raw.begin(), raw.end()));
        }
    } catch (const std::exception&) {}
    return Ini::parse("");
}
// RA2 rules.ini: the expand01.mix patch copy wins over ra2.mix/local.mix (RE §1/§6).
Ini load_ra2_rules_ini(const std::string& dir, const MixFile& ra2) {
    try {
        MixFile ex = MixFile::open_path(dir + "expand01.mix");
        if (const MixEntry* e = ex.find(std::string("rules.ini"))) {
            const auto raw = ex.read(*e);
            return Ini::parse(std::string(raw.begin(), raw.end()));
        }
    } catch (const std::exception&) {}
    return load_ra2_local_ini(ra2, "rules.ini");
}
// The mix holding a RA2 campaign map (RE §1): Soviet maps in MAPS02.MIX, the
// Allied/training/ending maps in Maps01.mix. W7-09-06: the Yuri's Revenge
// campaign maps (all01umd.map ... sov07umd.map, the *MD.MAP names; YR's
// [Campaigns] battles live in battlemd.ini) are in mapsmd03.mix, which the
// clone never searched -- a referenced YR map now loads (no YR campaign UI).
std::string mix_for_map(const std::string& map) {
    const std::string u = up(map);
    if (u.size() > 6 && u.compare(u.size() - 6, 6, "MD.MAP") == 0) return "mapsmd03.mix";
    return u.rfind("SOV", 0) == 0 ? "MAPS02.MIX" : "Maps01.mix";
}
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
Ini load_map_ini(const std::string& dir, const std::string& map) {
    MixFile maps = MixFile::open_path(dir + mix_for_map(map));
    const MixEntry* e = maps.find(lower(map));
    if (!e) throw std::runtime_error("campaign map not found: " + map);
    const auto raw = maps.read(*e);
    return Ini::parse(std::string(raw.begin(), raw.end()));
}

// CSF strings (RE §3.2 labels): the loose patch ra2.csf first, then
// language.mix/ra2.csf. Loaded once.
const ra2assets::CsfStrings* csf(const std::string& dir) {
    static std::unique_ptr<ra2assets::CsfStrings> s;
    static bool tried = false;
    if (tried) return s.get();
    tried = true;
    try {
        const std::vector<std::uint8_t> raw = ra2assets::read_file(dir + "ra2.csf");
        if (!raw.empty()) s = std::make_unique<ra2assets::CsfStrings>(
                              ra2assets::CsfStrings::load(raw.data(), raw.size()));
    } catch (const std::exception&) {}
    if (!s) {
        try {
            MixFile lang = MixFile::open_path(dir + "language.mix");
            if (const MixEntry* e = lang.find(std::string("ra2.csf"))) {
                const auto raw = lang.read(*e);
                s = std::make_unique<ra2assets::CsfStrings>(
                    ra2assets::CsfStrings::load(raw.data(), raw.size()));
            }
        } catch (const std::exception&) {}
    }
    return s.get();
}
std::string csf_text(const std::string& dir, const std::string& label) {
    const ra2assets::CsfStrings* c = csf(dir);
    const std::u16string* t = c ? c->find(label) : nullptr;
    if (!t) return label;
    std::string out;
    for (char16_t ch : *t) out += (ch >= 32 && ch < 127) ? (char)ch : (ch == '\n' ? '\n' : ' ');
    return out;
}

// battle.ini / mission.ini / mapsel.ini from ra2.mix/local.mix (RE §1).
ra2engine::CampaignTables load_tables(const MixFile& ra2) {
    return ra2engine::CampaignTables::parse(load_ra2_local_ini(ra2, "battle.ini"),
                                            load_ra2_local_ini(ra2, "mission.ini"),
                                            load_ra2_local_ini(ra2, "mapsel.ini"));
}

// The campaign selection a GameScene / gate starts from.
struct Launch {
    std::string map;          // e.g. "ALL01t.MAP"
    std::string side;         // mapsel side section: "GDI" / "Nod"
    int difficulty = 1;       // 0 Easy, 1 Normal, 2 Hard (Scenario +0x60C)
};

// ---------------------------------------------------------------------------
// Generic type resolution for map objects + TaskForce members [RE §4].
// Buildings are appended to ba.roster (campaign-only, after every existing
// index, so nothing that indexes the roster moves); units get a UnitCombat and,
// for vehicles, a VehicleAssets (stable pointer for the render list).
struct UnitType {
    ra2engine::UnitCombat combat;
    bool vehicle = false;
    bool ok = false;
};

class Types {
public:
    Types(const std::string& dir, const MixFile& ra2, const MixFile& language, UnitAssets& ua,
          const Rules& rules, const Art& art, const Ini& rules_ini)
        : dir_(dir), ra2_(ra2), language_(language), ua_(ua), rules_(rules), art_(art),
          rules_ini_(rules_ini) {
        infantry_ = list_values(rules_ini, "InfantryTypes");
        vehicles_ = list_values(rules_ini, "VehicleTypes");
        aircraft_ = list_values(rules_ini, "AircraftTypes");
    }

    const UnitType* unit(const std::string& id_in) {
        const std::string id = up(id_in);
        auto it = units_.find(id);
        if (it != units_.end()) return it->second.ok ? &it->second : nullptr;
        UnitType t;
        try {
            if (index_of(infantry_, id) >= 0) {
                const ResolvedUnit ru = resolve_combat_unit(rules_, id);
                t.combat = ru.combat;
                t.combat.is_vehicle = false;
                t.combat.speed_cps = speed_to_cps(ru.speed);
                const std::string& img = t.combat.image;
                if (!ua_.shps.count(img)) {
                    ua_.anims.emplace(img, anim_from_art(art_, img));
                    // W7-09-15 (R-035): ALL04's [CIVBFM] Image=BeInvisibleHack
                    // names an SHP that does not exist -- the original still
                    // creates the object (the "1BeachMaleHack" team, script
                    // BeAsleep) and simply draws nothing. A missing SHP no
                    // longer drops the type: it exists, unrendered (the live
                    // renderer still tries conquer/conqmd on first sight).
                    try {
                        MixFile conquer = ra2_.open_nested("conquer.mix");
                        ua_.shps.emplace(img, load_unit_shp(conquer, img));
                    } catch (const std::exception& ex) {
                        std::cerr << "[campaign] unit type " << id << " has no SHP " << img << " (" << ex.what()
                                  << "): placed unrendered\n";
                    }
                }
                t.ok = true;
            } else if (index_of(vehicles_, id) >= 0 && !art_.voxel(rules_.image(id))) {
                // W3-FIX-A: Voxel=no SHP vehicles (Terror Drone, Dolphin, Giant
                // Squid) were sent to the voxel loader, which threw -- they never
                // existed in a mission, as map units or TaskForce members. Same
                // registration as skirmish: a land SHP vehicle rides the
                // infantry path (W2-GA-11), an SHP ship is a vehicle (W2-AN-15).
                const ResolvedUnit ru = resolve_combat_unit(rules_, id);
                t.combat = ru.combat;
                t.combat.speed_cps = speed_to_cps(ru.speed);
                const bool ship = t.combat.is_naval;
                t.combat.is_vehicle = ship;
                t.vehicle = ship;
                const std::string img = t.combat.image;
                if (!ua_.shps.count(img)) {
                    MixFile conquer = ra2_.open_nested("conquer.mix");
                    ua_.shps.emplace(img, load_unit_shp(conquer, img));
                }
                if (!ua_.anims.count(img)) {
                    if (ship) {
                        auto sv = g_shp_vehicles.find(img);
                        const int walk = sv != g_shp_vehicles.end() ? std::max(1, sv->second.walk) : 1;
                        const int fire = sv != g_shp_vehicles.end() ? std::max(1, sv->second.fire) : 1;
                        ra2engine::UnitAnimSeq a;   // the w2_setup SHP-ship layout
                        a.stand = {0, 1, walk};
                        a.walk = {0, walk, walk};
                        a.fire = {walk * 8, fire, fire};
                        a.die = {0, 1, 0};
                        ua_.anims.emplace(img, a);
                    } else {
                        ua_.anims.emplace(img, anim_from_art(art_, img));
                    }
                }
                t.ok = true;
                std::cerr << "[campaign] SHP vehicle " << id << " image=" << img << (ship ? " ship" : " land") << "\n";
            } else if (index_of(vehicles_, id) >= 0 || index_of(aircraft_, id) >= 0) {
                // W2-CA-16: aircraft resolve through the same voxel loader
                // (resolve_an_type sets the fixed-wing / jumpjet flight model).
                auto va = std::make_unique<VehicleAssets>(load_vehicle_assets(dir_, ra2_, ua_.pal, id));
                if (va->ok) {
                    t.combat = va->combat;
                    // W6 fix #2: the voxel loader resolves with the stock rulesmd;
                    // the targeting data takes the MAP overrides (SOV09 [CAR]
                    // ImmuneToPsionics=yes).
                    t.combat.tg = resolve_unit_w6(rules_, id);
                    t.vehicle = true;
                    t.ok = true;
                    vas_.push_back(std::move(va));
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[campaign] unit type " << id << " unavailable (" << ex.what() << ")\n";
            t.ok = false;
        }
        auto res = units_.emplace(id, std::move(t));
        return res.first->second.ok ? &res.first->second : nullptr;
    }
    bool is_aircraft(const std::string& id) const { return index_of(aircraft_, id) >= 0; }

    // Roster index of building `id`, appending it (def + art) when absent.
    int building(BuildingAssets& ba, const std::string& id_in) {
        const std::string id = up(id_in);
        for (std::size_t i = 0; i < ba.roster.size(); ++i)
            if (up(ba.roster[i].id) == id) return (int)i;
        if (!rules_ini_.has(id)) return -1;
        ra2engine::BuildingTypeDef d;
        d.id = id;
        const std::string art_id = rules_.image(id);
        d.fw = d.fh = 1;
        art_.foundation(art_id, d.fw, d.fh);
        d.strength = rules_.strength(id);
        if (d.strength <= 0) d.strength = 1;
        d.sight = rules_.sight(id);
        d.armor_ix = ra2engine::armor_index(rules_.armor(id));
        d.cost = rules_.cost(id);
        d.charge = d.cost;
        d.power = rules_.power(id);
        d.adjacent = rules_.adjacent(id);
        d.tech_level = rules_.tech_level(id);
        d.conyard = rules_.is_construction_yard(id);
        d.prerequisite = rules_.prerequisite(id);
        d.image = art_id;
        d.cameo = art_.cameo(art_id);
        d.explosion = rules_.explosion_first(id);
        d.unsellable = rules_.section_flag(id, "Unsellable", false);
        d.click_repairable = rules_.section_flag(id, "ClickRepairable", true);
        // W2-SK-12/19/20: the same Capturable/Spyable/UnitRepair flags as the roster.
        d.capturable = rules_.section_flag(id, "Capturable", false);
        d.spyable = rules_.section_flag(id, "Spyable", false);
        d.unit_repair = rules_.section_flag(id, "UnitRepair", false);
        d.crewed = rules_.section_flag(id, "Crewed", false);
        // W3-FIX-A: the MAP's theater (was hard-coded TEMPERATE -- snow missions
        // drew summer art) over the global MIX search list (isogen.mix holds
        // the landmarks, <ext>.mix the ammo crates) [RE: asset-search.md §1].
        BuildingArt slot;
        slot.ok = asset_building_shp(art_id, slot.shp);
        if (slot.ok) building_art_extras(slot, art_, rules_, id, art_id);
        else std::cerr << "[campaign] building " << id << " image " << art_id << " has no art\n";
        ra2engine::fill_building_weapons(rules_, id, d);   // Gap fix C (G-40)
        d.wall = rules_.section_flag(id, "Wall", false);                // W6-03-01 flag (campaign roster too)
        d.w6.wall_levels = d.wall ? art_.damage_levels(art_id) : 0;   // W6-04-04
        ba.roster.push_back(d);
        ba.art.push_back(std::move(slot));
        ba.defense.push_back(rules_.is_buildcat_combat(id) ? 1 : 0);
        // Keep the P6 per-type vectors index-aligned (inert flags).
        ba.wall.push_back(0);
        ba.naval.push_back(0);
        // W3-FIX-A: WaterBound=yes (Naval Yards) places on the water grid.
        ba.water_bound.push_back(rules_.section_flag(id, "WaterBound", false) ? 1 : 0);
        ba.factory_kind.push_back(rules_.section_string(id, "Factory", ""));
        ba.wall_fill_range.push_back(0);
        ba.dock_offsets.emplace_back();
        return (int)ba.roster.size() - 1;
    }

    std::vector<const VehicleAssets*> vehicle_assets() const {
        std::vector<const VehicleAssets*> v;
        for (const auto& p : vas_) v.push_back(p.get());
        return v;
    }
    const std::vector<std::string>& infantry_list() const { return infantry_; }
    const std::vector<std::string>& vehicle_list() const { return vehicles_; }

private:
    std::string dir_;
    const MixFile& ra2_;
    const MixFile& language_;
    UnitAssets& ua_;
    const Rules& rules_;
    const Art& art_;
    const Ini& rules_ini_;
    std::vector<std::string> infantry_, vehicles_, aircraft_;
    std::map<std::string, UnitType> units_;
    std::vector<std::unique_ptr<VehicleAssets>> vas_;
};

}  // namespace camp

// ---------------------------------------------------------------------------
// The mission in progress on a real World.
class CampaignRuntime {
public:
    struct Objective { long frame; std::string text; };

    CampaignRuntime(const camp::Launch& launch, const std::string& dir, const MixFile& ra2,
                    const MixFile& language, ra2engine::World& world, Assets& a, UnitAssets& ua,
                    BuildingAssets& ba, const Rules& rules, const Art& art, const Ini& rules_ini)
        : launch_(launch), dir_(dir), w_(world), a_(a), ba_(ba), rules_ini_(rules_ini),
          map_(camp::load_map_ini(dir, launch.map)),
          ra2_rules_(camp::load_ra2_rules_ini(dir, ra2)),
          basic_(ra2engine::parse_scenario_basic(map_)),
          types_(dir, ra2, language, ua, rules, art, rules_ini),
          data_(ra2engine::parse_map_triggers(map_, camp::list_values(ra2_rules_, "Countries"),
                                              camp::list_values(ra2_rules_, "BuildingTypes"))),
          teams_data_(ra2engine::parse_map_teams(map_)),
          end_(ra2engine::borrowed_time_frames(rules.general_double("SavourDelay", 0.1))),
          host_(*this) {
        ra2_vehicles_ = camp::list_values(ra2_rules_, "VehicleTypes");
        ra2_infantry_ = camp::list_values(ra2_rules_, "InfantryTypes");
        ra2_aircraft_ = camp::list_values(ra2_rules_, "AircraftTypes");
        reveal_radius_ = rules_ini.get_int("General", "RevealTriggerRadius", 9);
        setup_houses();
        // Every type the mission can place, resolved up front (so the render
        // list is complete before the first frame).
        const std::vector<ra2engine::MapObjectRec> objs = ra2engine::parse_map_objects(map_);
        for (const ra2engine::MapObjectRec& o : objs)
            if (o.kind == 0) types_.building(ba_, o.type);
            else types_.unit(o.type);   // W2-CA-16: aircraft too
        for (const auto& tf : teams_data_.taskforces)
            for (const ra2engine::MapTaskMember& m : tf.second) types_.unit(m.type);
        resolve_base_nodes();   // [G-69] before the roster is handed to the World
        // W5-A: ARM THE ECONOMY. The campaign World (build_campaign_world) never
        // went through launch_skirmish's real_play_arm_economy, so types_ stayed
        // empty: no ore growth, no harvester FSM, no refinery income for the
        // player or any computer house in any mission (ALL03's Chrono Miner sat
        // in SeekOre forever). The water grid is already built by
        // depth_set_terrain, so only the economy wiring is needed here.
        wire_economy(w_, ba_, rules);
        w_.refresh_building_types(ba_.roster);   // G16: btypes() must see mission types
        w_.set_repair_sell_rules(ba_.repair_sell);
        place_objects(objs);
        sync_tracks();       // W6 fix #2: the placed buildings' Insignificant/LegalTarget flags
        apply_playfield();   // W6-02-08: IsInPlayfield of every placed object (Unlimbo)

        sys_ = std::make_unique<ra2engine::MapTriggerSystem>(data_, launch.difficulty,
                                                              w_.session_stream_seed(0x5EEDu, 7u));   // Net L0
        sys_->set_campaign_mode(true);
        // Objects were placed (and attached) before scenario start [RE
        // map-triggers.md §1.8]: attach their tags now that the system exists.
        for (const Track& t : utrack_) if (t.tag >= 0) sys_->attach_tag(t.tag);
        for (const Track& t : btrack_) if (t.tag >= 0) sys_->attach_tag(t.tag);
        sys_->start(host_);
        drain_log();
        char b[200];
        std::snprintf(b, sizeof b,
                      "[camp] %s houses=%d player=%s objects=%d placed_b=%d placed_u=%d skipped=%d "
                      "triggers=%d tags=%d teams=%d",
                      launch.map.c_str(), (int)house_names_.size(), house_names_[0].c_str(),
                      (int)objs.size(), placed_b_, placed_u_, skipped_, (int)data_.triggers.size(),
                      (int)data_.tags.size(), (int)teams_data_.teams.size());
        witness_.emplace_back(b);
    }

    // --- per frame (15 Hz) --------------------------------------------------
    // One logic frame: the trigger loops, object event polling, team scripts,
    // hunters, mission-end bookkeeping. The caller steps the World afterwards.
    void frame() {
        if (finished()) return;
        // Net L0: the poll loops hold Unit& / Building& / Track& across raises,
        // and a raise can run trigger actions that create objects (and their
        // tracks). Found by the cross-platform gate: on Android (bionic) the
        // freed vector read hp=0 at ALL01 frame 410 and raised an extra
        // AttackedByAnybody; Windows happened to read the old bytes. Keep
        // headroom so no vector reallocates while a reference is live.
        w_.reserve_object_headroom(kObjectHeadroom);
        reserve_track_headroom();
        // W5-A: a capture / infiltration raises EnteredBy synchronously in the
        // original (the infantry's Enter code calls TagClass::RaiseEvent before
        // any later house AI pass), so the World's entered events of the last
        // step are raised BEFORE this frame's trigger loops. Raised after them,
        // an engineer capturing ALL02's chapel on a house-loop frame let
        // 'Chapel Destroyed' (BuildingDoesNotExist for the old owner) fire first.
        sync_tracks();
        wake_ordered();   // P4: an ordered Sleep/Harmless unit leaves that mission
        poll_entered();
        sys_->tick(host_);
        drain_log();
        arm_production_houses();   // [G-69]
        poll_objects();
        poll_bridges();       // W5-A event 31
        step_teams();
        push_script_demand();
        step_hunters();
        step_house_states();  // W5-A 9 FireSale state + 35 PreferredTarget
        publish_team_flags(); // fv7c: TeamType Aggressive/Suicide/IsBaseDefense -> the World
        if (sys_->outcome() != 0) flag_end(sys_->outcome());
        ++frame_;
    }
    // fv7c (13b 1b) [RE: Team +0x5D4 -> TeamType +0xAD/+0xAF/+0xF6, read for
    // every house]: each campaign team member's TeamType flags, for the
    // World's retaliation (Suicide), on-the-move scan (Aggressive) and call
    // for help (team members excluded unless IsBaseDefense).
    void publish_team_flags() {
        std::vector<std::uint8_t> f(w_.units().size(), 0);
        for (const Team& t : teams_) {
            if (t.type < 0 || t.type >= (int)teams_data_.teams.size()) continue;
            const ra2engine::MapTeamType& tt = teams_data_.teams[(std::size_t)t.type];
            const std::uint8_t b = (std::uint8_t)(ra2engine::World::kTeamMember |
                                                  (tt.is_base_defense ? ra2engine::World::kTeamBaseDefense : 0) |
                                                  (tt.aggressive ? ra2engine::World::kTeamAggressive : 0) |
                                                  (tt.suicide ? ra2engine::World::kTeamSuicide : 0));
            for (int m : t.members)
                if (m >= 0 && m < (int)f.size()) f[(std::size_t)m] |= b;
        }
        w_.set_campaign_team_flags(std::move(f));
    }
    // Interactive: run as many 15 Hz logic frames as the wall-clock dt covers.
    void update(double dt) {
        accum_ += dt;
        int n = 0;
        while (accum_ >= 1.0 / 15.0 && n < 4) {
            accum_ -= 1.0 / 15.0;
            frame();
            ++n;
        }
        if (n == 4) accum_ = 0.0;
    }

    bool finished() const { return end_.ended(frame_); }
    int outcome() const { return end_.outcome(); }
    long frame_count() const { return frame_; }
    const camp::Launch& launch() const { return launch_; }
    const ra2engine::ScenarioBasic& basic() const { return basic_; }
    // W4-MC-04: the map's [Map] Size / LocalSize (the usable area).
    ra2engine::MapLocalSize local_size() const {
        return local_overridden_ ? local_override_ : ra2engine::parse_map_local_size(map_);
    }
    int local_version() const { return local_version_; }   // W5-A: bumped by 40 ResizeView
    // W6-02-08 [RE: Unlimbo FUN_006F61D0 / ResizeView FUN_00566E60]: hand the
    // usable area to the World, which recomputes every object's IsInPlayfield
    // and switches on the campaign Evaluate_Object rules (legal_threat_*).
    void apply_playfield() {
        const ra2engine::MapLocalSize m = local_size();
        w_.set_playfield(/*campaign=*/true, m.valid, m.size_w, m.x, m.y, m.w, m.h);
    }
    const std::vector<Objective>& objectives() const { return objectives_; }
    const std::vector<std::string>& witness() const { return witness_; }
    std::vector<const VehicleAssets*> vehicle_assets() const { return types_.vehicle_assets(); }
    double player_credits() const { return player_credits_; }
    ra2engine::World::Side player_side() const { return player_side_; }
    const ra2engine::MapTriggerSystem& triggers() const { return *sys_; }
    int teams_created() const { return teams_created_; }
    int house_index(const std::string& name) const { return engine_house_of_name(name); }
    const std::vector<std::string>& house_names() const { return house_names_; }   // W5-E score page
    int raises() const { return raises_; }

    // --- P7 save/load (HOST section; the triggers go in TRIG) -----------------
    ra2engine::MapTriggerSystem& trigger_system() { return *sys_; }
    ra2engine::CampaignHostSave capture() const {
        ra2engine::CampaignHostSave h;
        for (const Track& t : utrack_) {
            h.u_tag.push_back(t.tag); h.u_cx.push_back(t.cx); h.u_cy.push_back(t.cy);
            h.u_hp.push_back(t.hp); h.u_team.push_back(t.team); h.u_type.push_back(t.type);
            h.u_flags.push_back(flags_of(t));
            h.u_mc_victim.push_back(t.mc_victim); h.u_mc_orig.push_back(t.mc_orig);
            h.u_mission.push_back(t.mission);
        }
        for (const Track& t : btrack_) {
            h.b_tag.push_back(t.tag); h.b_hp.push_back(t.hp); h.b_type.push_back(t.type);
            h.b_flags.push_back(flags_of(t));
        }
        for (const Team& t : teams_) {
            h.t_type.push_back(t.type); h.t_script.push_back(t.script); h.t_house.push_back(t.house);
            h.t_pc.push_back(t.st.pc); h.t_goal_x.push_back(t.goal_x); h.t_goal_y.push_back(t.goal_y);
            h.t_members.push_back(t.members);
            h.t_wait_until.push_back(t.st.wait_until);
            h.t_issued_frame.push_back(t.issued_frame);
            h.t_created.push_back(t.created);
            h.t_flags.push_back((std::uint8_t)((t.st.issued ? 1 : 0) | (t.st.done ? 2 : 0) |
                                               (t.recruiting ? 4 : 0) | (t.airdrop ? 8 : 0)));
        }
        h.destroyed_units = destroyed_units_;
        h.destroyed_buildings = destroyed_buildings_;
        for (const auto& lb : last_built_) h.last_built.insert(h.last_built.end(), lb.begin(), lb.end());
        for (const Objective& o : objectives_) { h.obj_frames.push_back(o.frame); h.obj_texts.push_back(o.text); }
        h.end_outcome = end_.outcome();
        h.end_flagged_at = end_.flagged_at();
        h.frame = frame_;
        h.accum = accum_;
        h.logged = logged_;
        h.teams_created = teams_created_;
        h.raises = raises_;
        for (const Track& t : btrack_) h.b_off.push_back(t.off ? 1 : 0);
        h.house_quarry = quarry_;
        h.house_fire_sale = fire_sale_;
        h.left_teams = left_teams_;
        if (local_overridden_)
            h.local_override = {1, local_override_.x, local_override_.y, local_override_.w, local_override_.h};
        return h;
    }
    bool restore(const ra2engine::CampaignHostSave& h) {
        if (h.u_tag.size() != w_.units().size() || h.b_tag.size() != w_.buildings().size())
            return false;
        utrack_.clear();
        for (std::size_t i = 0; i < h.u_tag.size(); ++i) {
            Track t;
            t.tag = h.u_tag[i]; t.cx = h.u_cx[i]; t.cy = h.u_cy[i]; t.hp = h.u_hp[i];
            t.team = h.u_team[i]; t.type = h.u_type[i];
            set_flags(t, h.u_flags[i]);
            if (i < h.u_mc_victim.size()) t.mc_victim = h.u_mc_victim[i];
            if (i < h.u_mc_orig.size()) t.mc_orig = h.u_mc_orig[i];
            if (i < h.u_mission.size()) t.mission = h.u_mission[i];
            utrack_.push_back(t);
        }
        btrack_.clear();
        for (std::size_t i = 0; i < h.b_tag.size(); ++i) {
            Track t;
            t.tag = h.b_tag[i]; t.hp = h.b_hp[i]; t.type = h.b_type[i];
            set_flags(t, h.b_flags[i]);
            btrack_.push_back(t);
        }
        teams_.clear();
        for (std::size_t i = 0; i < h.t_type.size(); ++i) {
            Team t;
            t.type = h.t_type[i]; t.script = h.t_script[i]; t.house = h.t_house[i];
            t.st.pc = h.t_pc[i]; t.goal_x = h.t_goal_x[i]; t.goal_y = h.t_goal_y[i];
            t.members = h.t_members[i];
            t.st.wait_until = (long)h.t_wait_until[i];
            t.issued_frame = (long)h.t_issued_frame[i];
            t.created = (long)h.t_created[i];
            t.st.issued = (h.t_flags[i] & 1) != 0;
            t.st.done = (h.t_flags[i] & 2) != 0;
            t.recruiting = (h.t_flags[i] & 4) != 0;
            t.airdrop = (h.t_flags[i] & 8) != 0;   // W4-SQ-06 (save v16)
            teams_.push_back(t);
        }
        destroyed_units_ = h.destroyed_units;
        destroyed_buildings_ = h.destroyed_buildings;
        for (std::size_t i = 0; i < last_built_.size() && 4 * i + 3 < h.last_built.size(); ++i)
            for (int k = 0; k < 4; ++k) last_built_[i][(std::size_t)k] = h.last_built[4 * i + (std::size_t)k];
        objectives_.clear();
        for (std::size_t i = 0; i < h.obj_frames.size() && i < h.obj_texts.size(); ++i)
            objectives_.push_back({(long)h.obj_frames[i], h.obj_texts[i]});
        end_ = ra2engine::MissionEnd(end_.borrowed_frames());
        if (h.end_outcome != 0) end_.flag(h.end_outcome, (long)h.end_flagged_at);
        frame_ = (long)h.frame;
        accum_ = h.accum;
        logged_ = (std::size_t)h.logged;
        teams_created_ = h.teams_created;
        raises_ = h.raises;
        for (std::size_t i = 0; i < btrack_.size() && i < h.b_off.size(); ++i) btrack_[i].off = h.b_off[i] != 0;
        quarry_ = h.house_quarry;
        fire_sale_ = h.house_fire_sale;
        left_teams_ = h.left_teams;
        local_overridden_ = h.local_override.size() == 5 && h.local_override[0] == 1;
        if (local_overridden_) {
            local_override_ = ra2engine::parse_map_local_size(map_);
            local_override_.x = h.local_override[1]; local_override_.y = h.local_override[2];
            local_override_.w = h.local_override[3]; local_override_.h = h.local_override[4];
        }
        ++local_version_;
        bridge_seen_.clear();   // rebuilt from the loaded World
        return true;
    }

    // W3-FX play-script cheat: fire a map trigger by name now (camp_fire).
    int force_trigger(const std::string& name) {
        const int n = sys_->force_fire(name, host_);
        drain_log();
        return n;
    }
    // W2-CA-19 play-script cheat: place a map/TaskForce type like a reinforcement.
    int spawn(const std::string& type, int house, int x, int y) {
        return place_unit(type, house, x, y, 256, std::string(), "Guard", /*far=*/true);
    }
    const std::string& unit_type(int i) const {
        static const std::string none;
        return i >= 0 && i < (int)utrack_.size() ? utrack_[(std::size_t)i].type : none;
    }

    // W2-CA-12: trigger audio the scene plays through its presentation.
    struct AudioReq { int action; std::string name; int value; int waypoint; };
    std::vector<AudioReq> take_audio() { std::vector<AudioReq> o; o.swap(audio_); return o; }
    // W3-FX: trigger presentation requests (the trigger system's typed queue
    // plus the host's own DoExplosionAt anims). Presentation only.
    std::vector<ra2engine::VisualRequest> take_visuals() {
        std::vector<ra2engine::VisualRequest> o;
        o.swap(visuals_);
        return o;
    }
    // RA2 rules.ini lists the PlayAnimAt / ParticleAnim index spaces (see
    // campaign_visuals.inc) need.
    const Ini& ra2_rules() const { return ra2_rules_; }
    // W3-FX FlashTeam / MakeHouseCheer / FlashBuildingsOfType helpers.
    std::vector<int> team_unit_indices(const std::string& team_id) const {
        std::vector<int> out;
        const int ti = teams_data_.find_team(team_id);
        if (ti < 0) return out;
        for (const Team& t : teams_)
            if (t.type == ti && !t.st.done)
                for (int m : t.members) out.push_back(m);
        return out;
    }
    int engine_house_of_type(int ht) const { return engine_house(ht); }
    std::string building_type_id(int bt) const {
        return bt >= 0 && bt < (int)data_.building_types.size() ? camp::up(data_.building_types[(std::size_t)bt])
                                                                 : std::string();
    }
    // W3-FX DoExplosionAt Value -> WeaponType id. [RE-FALLBACK: the
    // WeaponTypeClass array is find-or-allocated as each TechnoType's INI is
    // read (FUN_00711510 Primary/Secondary/Elite*/Occupy/DeathWeapon); that
    // object-read order was not recovered, so the RA2 rules.ini FILE order of
    // those keys stands in.]
    std::string ra2_weapon_by_index(int ix) {
        if (ra2_weapons_.empty()) {
            static const char* kKeys[] = {"Primary", "Secondary", "ElitePrimary", "EliteSecondary",
                                          "OccupyWeapon", "EliteOccupyWeapon", "DeathWeapon"};
            for (const std::string& sec : ra2_rules_.section_names())
                for (const char* k : kKeys) {
                    const std::string w = camp::up(camp::trim_comment(ra2_rules_.get(sec, k, "")));
                    if (w.empty() || std::find(ra2_weapons_.begin(), ra2_weapons_.end(), w) != ra2_weapons_.end())
                        continue;
                    ra2_weapons_.push_back(w);
                }
        }
        return ix >= 0 && ix < (int)ra2_weapons_.size() ? ra2_weapons_[(std::size_t)ix] : std::string();
    }
    bool waypoint_xy(int wp, int& x, int& y) const { return waypoint_cell(wp, x, y); }
    std::string map_theme() const { return camp::trim_comment(map_.get("Basic", "Theme", "")); }

    std::uint64_t hash() const {
        std::uint64_t h = sys_->state_hash();
        auto mix = [&](std::uint64_t v) {
            for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xff; h *= 0x100000001b3ull; }
        };
        mix((std::uint64_t)frame_);
        mix((std::uint64_t)teams_created_);
        mix((std::uint64_t)raises_);
        mix((std::uint64_t)end_.outcome());
        for (const Team& t : teams_) {
            mix((std::uint64_t)t.type);
            mix((std::uint64_t)t.st.pc);
            mix((std::uint64_t)t.members.size());
        }
        for (const std::string& s : witness_)
            for (char c : s) { h ^= (unsigned char)c; h *= 0x100000001b3ull; }
        return h;
    }

private:
    // Tracking record per World unit/building (parallel to world arrays).
    struct Track {
        int tag = -1;               // tag type (-1 none)
        std::string type;           // TechnoType id (upper)
        int cx = -1, cy = -1;       // last cell (units)
        int hp = 0;                 // last hp
        bool dead = false;          // death already raised
        bool discovered = false;    // event 4 raised
        bool selected = false;      // last selected state
        int team = -1;              // map team index
        bool hunt = false;          // Hunt mission
        bool initial = true;        // placed by the map (not "built")
        std::uint8_t dmg_stage = 0; // W2-CA-14: 1 damaged, 2 <= half, 4 <= quarter raised
        int mc_victim = -1;         // W2-CA-15: unit this controller holds
        int mc_orig = -1;           // W2-CA-15: house before it was mind-controlled
        std::uint8_t mission = 0;   // W4-SQ-05: 1 Sleep, 2 Harmless (placed Mission=)
        bool off = false;           // W5-A 61/62 TurnOff/TurnOn (buildings)
    };
    struct Team {
        int type = -1;              // teams_data_.teams index
        int script = -1;            // teams_data_.scripts index
        int house = -1;             // engine house
        std::vector<int> members;   // world unit indices
        ra2engine::MapTeamState st;
        int goal_x = -1, goal_y = -1;
        long issued_frame = 0;
        bool recruiting = false;
        long created = 0;
        bool airdrop = false;       // W4-SQ-06: members still in the drop plane / falling
    };

    static std::uint8_t flags_of(const Track& t) {
        return (std::uint8_t)((t.dead ? 1 : 0) | (t.discovered ? 2 : 0) | (t.selected ? 4 : 0) |
                              (t.hunt ? 8 : 0) | (t.initial ? 16 : 0) | ((t.dmg_stage & 7) << 5));
    }
    static void set_flags(Track& t, std::uint8_t f) {
        t.dead = f & 1; t.discovered = f & 2; t.selected = f & 4; t.hunt = f & 8; t.initial = f & 16;
        t.dmg_stage = (std::uint8_t)((f >> 5) & 7);
    }

    // --- houses [RE §3.3, map-triggers §6] ------------------------------------
    void setup_houses() {
        house_names_ = ra2engine::campaign_house_order(map_, basic_.player);
        if (house_names_.empty()) house_names_.push_back(basic_.player);
        for (const auto& kv : map_.section("Houses")) {
            const std::string hn = camp::trim_comment(kv.second);
            houses_order_ht_.push_back(data_.find_house_type(map_.get(hn, "Country", "")));
        }
        // W6-05-01: the rules [Colors] ColorSchemes (rulesmd.ini, else the RA2
        // rules.ini) in INI order; each house's Color= names one [RE: HouseClass
        // ::ReadINI Color= -> ColorScheme; country Color= is the default].
        std::vector<std::string> scheme_names;
        {
            std::vector<std::array<int, 3>> hsv;
            const Ini& cini = rules_ini_.section("Colors").empty() ? ra2_rules_ : rules_ini_;
            for (const auto& kv : cini.section("Colors")) {
                int h = 0, s = 0, v = 0;
                if (std::sscanf(camp::trim_comment(kv.second).c_str(), "%d,%d,%d", &h, &s, &v) != 3) continue;
                scheme_names.push_back(camp::up(camp::trim_comment(kv.first)));
                hsv.push_back({h, s, v});
            }
            ra2engine::set_color_schemes(hsv);
        }
        auto scheme_of = [&](const std::string& hn, const std::string& country) {
            std::string c = camp::trim_comment(map_.get(hn, "Color", ""));
            if (c.empty()) c = camp::trim_comment(map_.get(country, "Color", ""));
            if (c.empty()) c = camp::trim_comment(rules_ini_.get(country, "Color", ""));
            if (c.empty()) c = camp::trim_comment(ra2_rules_.get(country, "Color", ""));
            const int k = camp::index_of(scheme_names, c);
            return k < 0 ? -1 : ra2engine::kColorSchemeBase + k;
        };
        auto allies_of = [&](const std::string& h) {
            std::vector<std::string> v;
            std::string cur;
            for (char c : map_.get(h, "Allies", "") + ",") {
                if (c == ',') { v.push_back(camp::up(camp::trim_comment(cur))); cur.clear(); }
                else cur += c;
            }
            return v;
        };
        auto lists = [&](const std::string& a, const std::string& b) {
            const std::vector<std::string> v = allies_of(a);
            return std::find(v.begin(), v.end(), camp::up(b)) != v.end();
        };
        const std::string& player = house_names_[0];
        for (std::size_t i = 0; i < house_names_.size(); ++i) {
            const std::string& hn = house_names_[i];
            const std::string country = camp::trim_comment(map_.get(hn, "Country", ""));
            house_type_of_.push_back(data_.find_house_type(country));
            // W2-CA-05: [<Country>] Side= from the map, else from RA2 rules.ini
            // [Countries] sections (the standard countries live there; the map
            // only adds countries) [campaign.md §6].
            std::string side = camp::up(camp::trim_comment(map_.get(country, "Side", "")));
            if (side.empty()) side = camp::up(camp::trim_comment(ra2_rules_.get(country, "Side", "")));
            ra2engine::World::HouseRec rec;
            rec.side = side == "GDI" ? ra2engine::World::Side::Allied
                     : side == "NOD" ? ra2engine::World::Side::Soviet
                                     : ra2engine::World::Side::Neutral;
            rec.country = country;
            rec.color_ix = (int)(i % 8);
            if (const int sc = scheme_of(hn, country); sc >= 0) rec.color_ix = sc;   // W6-05-01
            // [DEVIATION: engine alliances are symmetric teams] team 0 = the
            // player + houses MUTUALLY allied with it; a civilian-side house the
            // player lists as an ally is a neutral non-combatant; every other
            // house is the enemy team 1.
            const bool mutual = i == 0 || (lists(player, hn) && lists(hn, player));
            // W2-CA-02 [RE: FUN_00500960 reads PlayerControl -> house+0x1ED;
            // FUN_0050B510 ControlledByHuman = +0x1EC IsHuman || +0x1ED outside a
            // network session]: the player commands this house's units. It
            // fights on the player's side [RE-FALLBACK: the maps list it in
            // the player's Allies=; the clone forces the mutual team-0 bond].
            {
                const std::string pc = camp::up(camp::trim_comment(map_.get(hn, "PlayerControl", "no")));
                rec.player_control = i > 0 && !pc.empty() && (pc[0] == 'Y' || pc[0] == 'T' || pc[0] == '1');
            }
            if (rec.player_control) {
                rec.combatant = true;
                rec.alliance_team = 0;
            } else if (rec.side == ra2engine::World::Side::Neutral && (i == 0 || lists(player, hn))) {
                rec.combatant = false;
                rec.alliance_team = (int)i + 100;
            } else {
                rec.combatant = true;
                rec.alliance_team = mutual ? 0 : 1;
            }
            if (i == 0) player_side_ = rec.side;
            w_.set_house((int)i, rec);
            // w7fix-combat [RE: house +0x24C IQ, FUN_00500960]: the map's IQ=
            // (the AutoCrush / Scatter / GuardArea gates). A human house is 0.
            w_.set_house_iq((int)i, (i == 0 || rec.player_control) ? 0 : house_iq((int)i));
            {   // W6-02-01/02 [RE: Start_Scenario FUN_00686560 0x6865B2..0x6865D5 --
                // campaign Scen+0x60C = slider, Scen+0x610 = 2 - slider; FUN_005007D0
                // 0x5008E7..: a house with IsHuman (+0x1EC) or IsInPlayerControl
                // (+0x1ED) gets AssignHandicap(+0x60C), EVERY other house
                // AssignHandicap(+0x610); FUN_004F6DD0 copies [Easy]/[Normal]/
                // [Difficult] FirePower (+0x188) and ROF (+0x1A8)]. Set for every
                // house at scenario start, armed by an AI or not.
                const int d = std::clamp(launch_.difficulty, 0, 2);
                const int cls = (i == 0 || rec.player_control) ? d : 2 - d;
                static const char* kGroups[3] = {"Easy", "Normal", "Difficult"};
                auto num = [&](const char* key) {
                    const std::string v = camp::trim_comment(rules_ini_.get(kGroups[cls], key, ""));
                    return v.empty() ? 1.0 : std::atof(v.c_str());
                };
                w_.set_house_handicap((int)i, num("FirePower"), num("ROF"));
                // fv7c [RE: FUN_004F6DD0 copies RepairDelay (minutes) -> the
                // house +0x280 repair timer]: the AI repair of a computer house.
                if (!(i == 0 || rec.player_control))
                    w_.set_house_repair_delay((int)i, (int)(num("RepairDelay") * 900.0));
            }
            {   // W4-SQ-02: [<Country>] MultiplayPassive= (map, else RA2 rules.ini)
                std::string mp = camp::up(camp::trim_comment(map_.get(country, "MultiplayPassive", "")));
                if (mp.empty()) mp = camp::up(camp::trim_comment(ra2_rules_.get(country, "MultiplayPassive", "no")));
                w_.set_passive_house((int)i, !mp.empty() && (mp[0] == 'Y' || mp[0] == 'T' || mp[0] == '1'));
            }
            // [X House] Credits= is in hundreds (RA1 HouseClass::Read_INI
            // lineage, PROBABLE).
            double credits = 100.0 * map_.get_int(hn, "Credits", 0);
            {   // W5-A [RE: FUN_00500960 -- a PlayerControl house on Easy / Hard
                // gets [General] CampaignMoneyDeltaEasy / Hard added (0 in the
                // YR rulesmd), floored at 0]
                const std::string pc = camp::up(camp::trim_comment(map_.get(hn, "PlayerControl", "no")));
                if (!pc.empty() && (pc[0] == 'Y' || pc[0] == 'T' || pc[0] == '1') && launch_.difficulty != 1) {
                    const char* key = launch_.difficulty == 0 ? "CampaignMoneyDeltaEasy" : "CampaignMoneyDeltaHard";
                    credits += std::atof(camp::trim_comment(rules_ini_.get("General", key, "0")).c_str());
                    if (credits < 0.0) credits = 0.0;
                }
            }
            {   // [RE: FUN_006862D0 @0x686421..0x6864B3 (Start_Scenario tail, no
                // GameMode gate): every house that is not IsHuman (+0x1EC) and not
                // MultiplayPassive gets GiveMoney FUN_004F9770(ftol(
                // MultiplayerAICM[+0x184] (Rules+0x1308) x 0.01 x Available_Money))].
                // +0x184 is the handicap class (computer 2 - slider, PlayerControl
                // the slider); YR rulesmd 400,0,0 -> only class 0 gets +400 %:
                // computer houses on Hard, PlayerControl houses on Easy.
                std::string mp = camp::up(camp::trim_comment(map_.get(country, "MultiplayPassive", "")));
                if (mp.empty()) mp = camp::up(camp::trim_comment(ra2_rules_.get(country, "MultiplayPassive", "no")));
                const bool passive = !mp.empty() && (mp[0] == 'Y' || mp[0] == 'T' || mp[0] == '1');
                if (i != 0 && !passive) {
                    const int d = std::clamp(launch_.difficulty, 0, 2);
                    const int cls = rec.player_control ? d : 2 - d;
                    std::vector<std::string> cm(1);
                    for (char ch : camp::trim_comment(rules_ini_.get("General", "MultiplayerAICM", "")))
                        if (ch == ',') cm.emplace_back(); else cm.back() += ch;
                    const int pct = cls < (int)cm.size() ? std::atoi(camp::trim_comment(cm[(std::size_t)cls]).c_str()) : 0;
                    credits += (double)(int)((double)pct * 0.01 * (double)(int)credits);
                }
            }
            if (i == 0) player_credits_ = credits;
            else w_.grant_credits_for_test((int)i, credits);
        }
        // W4-SQ (SOV09 / ALL05 triage) [RE: HouseClass::IsAlliedWith reads the
        // house's OWN Allies= bitfield]: every house sees every other through
        // its own Allies= list, one-sided where the map makes it so. The
        // player's side (team 0: the player, PlayerControl houses, mutual
        // allies) stays allied both ways [the W2-CA-02 deviation]. Before, all
        // non-player combatants were one alliance: SOV09's Russian drop groups
        // never fought the Americans and hunted the Yuris instead, and ALL05's
        // Tanya shot down the spy helicopter of a house that lists her.
        for (std::size_t a = 0; a < house_names_.size(); ++a)
            for (std::size_t b = 0; b < house_names_.size(); ++b) {
                if (a == b) continue;
                // W7-09-04 [RE: IsAlliedWith 0x4F98B0 reads the house's own
                // Allies= bits, neutral houses included]: a non-combatant pair
                // records its map view too (World::allied_with, the crush gate);
                // are_allied() still answers false for a non-combatant.
                if (!w_.house_combatant((int)a) || !w_.house_combatant((int)b)) {
                    w_.set_ally_view((int)a, (int)b, lists(house_names_[a], house_names_[b]));
                    continue;
                }
                const bool team0 = w_.house_team((int)a) == 0 && w_.house_team((int)b) == 0;
                w_.set_ally_view((int)a, (int)b, team0 || lists(house_names_[a], house_names_[b]));
            }
        w_.enable_shroud(true);
        // W2-GA-03 hand-off: the scenario's own FogOfWar flag ([Basic], every
        // RA2 campaign map says no).
        {
            std::string fog = camp::up(camp::trim_comment(map_.get("Basic", "FogOfWar", "")));
            if (fog.empty()) fog = camp::up(camp::trim_comment(map_.get("Map", "FogOfWar", "no")));
            w_.set_fog_of_war(!fog.empty() && (fog[0] == 'Y' || fog[0] == 'T' || fog[0] == '1'));
        }
        destroyed_units_.assign(house_names_.size(), 0);
        destroyed_buildings_.assign(house_names_.size(), 0);
        last_built_.assign(house_names_.size(), {-1, -1, -1, -1});
    }
    // --- [G-69] campaign enemy base build-out ---------------------------------
    // [RE: campaign.md §9; map [<House>] NodeCount= + NNN=TYPE,X,Y base nodes
    // (TS/RA2 BaseClass::Read_INI lineage, the format on every RA2 map that
    // has nodes); trigger action 3 "Production begins" sets the house's
    // production flag (map_trigger.cpp house_flags bit0)]. Once a computer
    // house's production flag is set it runs the AI-1 base brain over its
    // OWN node list: builds missing nodes and rebuilds destroyed ones in
    // place, repairs (G-65) and fires superweapons it owns (G-66).
    // [RE (W6-02-01): FUN_005007D0 gives every non-human, non-PlayerControl
    // campaign house AssignHandicap(Scen+0x610 = 2 - slider)]: the
    // DifficultyClass of a campaign computer house is 2 - campaign difficulty;
    // AI-2/AI-3 (action 74, aimd teams for map houses) are not armed yet.
    void resolve_base_nodes() {
        base_nodes_.assign(house_names_.size(), {});
        for (std::size_t i = 1; i < house_names_.size(); ++i) {
            const std::string& hn = house_names_[i];
            const int n = map_.get_int(hn, "NodeCount", 0);
            for (int k = 0; k < n; ++k) {
                char key[8];
                std::snprintf(key, sizeof key, "%03d", k);
                const std::string v = map_.get(hn, key, "");
                std::vector<std::string> f;
                std::string cur;
                for (char c : v + ",") {
                    if (c == ',') { f.push_back(camp::trim_comment(cur)); cur.clear(); }
                    else cur += c;
                }
                if (f.size() < 3) continue;
                const int t = types_.building(ba_, f[0]);
                if (t < 0) continue;
                ra2engine::PlannedNode node;
                node.type = t;
                node.cx = std::atoi(f[1].c_str());
                node.cy = std::atoi(f[2].c_str());
                base_nodes_[i].push_back(node);
            }
        }
    }
    // W2-CA-07 [map-triggers.md §5 action 74 AITriggersBegin; ai-trigger.md]:
    // once a computer house's AI-trigger flag is set (action 74, house_flags
    // bit 2) its team brain runs the MAP's own [AITriggerTypes]/[TeamTypes]/
    // [TaskForces]/[ScriptTypes] (the aimd.ini format) with the map country
    // as the House=/OwnerHouse key. [RE-FALLBACK (RE-A5 open): the map list
    // replaces aimd.ini for map houses; IsForSkirmish is not a filter in a
    // campaign (every map trigger has it 0).] A house with no base nodes
    // gets an empty catalog: it trains from the factories it owns but plans
    // no base of its own.
    void arm_production_houses() {
        for (std::size_t i = 1; i < house_names_.size(); ++i) {
            if (!w_.house_combatant((int)i) || w_.human_commanded((int)i)) continue;
            const ra2engine::World::Side side = w_.house_side((int)i);
            if (side == ra2engine::World::Side::Neutral) continue;
            const unsigned fl = sys_->house_flags(house_type((int)i));
            // W5-A R-028: base building is NOT IQ-gated -- SOV03's Other3 (IQ=0)
            // must build its NAPSYB node after ProductionBegins (the beacon the
            // player is handed; without it 'Beacon Destroyed' loses the mission).
            const bool want_base = (fl & 1u) && i < base_nodes_.size() && !base_nodes_[i].empty();
            const bool want_teams = (fl & 4u) && ai_roster_ && map_.has("AITriggerTypes");
            if (armed_.size() < house_names_.size()) armed_.resize(house_names_.size(), 0);
            // A loaded game's AI houses come back armed with the saved config.
            if (!armed_[i] && w_.ai_enabled((int)i)) armed_[i] = (std::uint8_t)((want_base ? 1 : 0) | (want_teams ? 2 : 0) | 4);
            const bool has_base = armed_[i] & 1, has_teams = armed_[i] & 2;
            if ((has_base || !want_base) && (has_teams || !want_teams)) continue;
            if (!want_base && !want_teams) continue;
            const Rules rules = Rules::load(rules_ini_);
            const std::string country = camp::trim_comment(map_.get(house_names_[i], "Country", ""));
            ra2engine::AiHouseConfig cfg = make_ai_config(
                ba_, rules, static_cast<ra2engine::AiDifficulty>(launch_.difficulty),
                /*start_credits=*/0.0, side, country);   // the house pool flows in via take_income
            const bool base = want_base || has_base;
            if (!base) cfg.catalog = ra2engine::AiCatalog{};   // trains, never plans a base
            if (want_teams || has_teams) arm_map_teams(cfg, (int)i, side, country, rules);
            cfg.fire_superweapons = house_iq((int)i) >= iq_rule("SuperWeapons", 4);
            armed_[i] = (std::uint8_t)((base ? 1 : 0) | ((want_teams || has_teams) ? 2 : 0) | 4);
            w_.enable_ai_house((int)i, std::move(cfg));
            if (base) w_.set_ai_base_nodes((int)i, base_nodes_[i]);
            {   // W6-02-05 [RE: AssignHandicap FUN_004F6DD0 seeds the team-check
                // timer at scenario start: +0x5798 = now, +0x57A0 = TeamDelays[d]
                // + HouseArray index (+0x30) x 175; HouseClass::AI FUN_004F8350
                // checks and resets it]: the timer has been running since frame 0,
                // so the first pass is due at TeamDelays + index x 175, not a
                // whole period after arming.
                int arr = (int)i;
                int k = 0;
                for (const auto& kv : map_.section("Houses")) {
                    if (camp::up(camp::trim_comment(kv.second)) == camp::up(house_names_[i])) { arr = k; break; }
                    ++k;
                }
                w_.set_ai_team_check_clock((int)i, (double)(frame_ - 175L * arr));   // Net L0: frames
            }
            char b[200];
            std::snprintf(b, sizeof b, "[camp] f%ld %s%s: %s nodes=%d", frame_,
                          base ? "production begins" : "", (want_teams || has_teams) ? " +map AI triggers" : "",
                          house_names_[i].c_str(), (int)base_nodes_[i].size());
            witness_.emplace_back(b);
        }
    }
    void arm_map_teams(ra2engine::AiHouseConfig& cfg, int h, ra2engine::World::Side side,
                       const std::string& country, const Rules& rules) {
        // This house's share of the AI roster by its stock (parent) country.
        UnitRoster mine;
        const std::string parent = camp::trim_comment(map_.get(country, "ParentCountry", ""));
        const std::string own = parent.empty() ? country : parent;
        for (const ra2engine::UnitTypeDef& d : ai_roster_->defs)
            if (aiside::owned_by(rules, d.id, side, own)) mine.defs.push_back(d);
        int ax = w_.grid_w() / 2, ay = w_.grid_h() / 2;
        for (const ra2engine::Unit& u : w_.units())
            if (live(u) && u.house == 0) { ax = u.cell_x; ay = u.cell_y; break; }
        // W5-A R-028 [RE: AITriggerTypeClass filter FUN_0041E740 -- a global
        // (aimd.ini) trigger (+0x9C == 1) is rejected only when Scenario+0x34B4
        // ([Basic] IgnoreGlobalAITriggers) is set; the map's own lists load on
        // top (find-or-create by ID, map-definition wins)]: 19 stock missions
        // set it (map triggers only), ALL05/09/12 and SOV04/12 keep aimd.ini.
        const std::string ig = camp::up(camp::trim_comment(map_.get("Basic", "IgnoreGlobalAITriggers", "no")));
        const bool ignore_global = !ig.empty() && (ig[0] == 'Y' || ig[0] == 'T' || ig[0] == '1');
        const Ini merged = ignore_global ? Ini::parse("") : merged_ai_ini();
        cfg.team = make_ai_team_config(ba_, mine, rules, ignore_global ? map_ : merged, cfg.difficulty, ax, ay,
                                       /*enable_triggers=*/true, &w_.map());
        for (ra2engine::AITriggerType& t : cfg.team.aimd.triggers) t.is_for_skirmish = true;
        auto id_of = [&](int t) -> const char* {
            return (t >= 0 && t < (int)ba_.roster.size()) ? ba_.roster[(std::size_t)t].id.c_str() : "";
        };
        const ra2engine::AiCatalog full = make_ai_config(ba_, rules, cfg.difficulty, 0.0, side, own).catalog;
        cfg.team.inf_factory_types = factory_types_for(ba_, id_of(full.barracks));
        cfg.team.veh_factory_types = factory_types_for(ba_, id_of(full.war_factory));
        cfg.team.trigger_side = side == ra2engine::World::Side::Soviet ? 2 : 1;
        cfg.team.trigger_tech_level = map_.get_int(house_names_[(std::size_t)h], "TechLevel", 10);
        cfg.team.house_name = country;
        fill_ai_building_maps(cfg.team, ba_, rules, rules_ini_);
    }
    // W5-A R-028: aimd.ini (global) + the map's AI lists, the map's entries
    // overriding same-ID ones and its index entries appended after aimd.ini's.
    Ini merged_ai_ini() const {
        Ini aimd = Ini::parse("");
        try { aimd = load_ini_from_ra2md(dir_, "aimd.ini"); } catch (const std::exception&) {}
        static const char* kIndex[] = {"TaskForces", "ScriptTypes", "TeamTypes"};
        std::string out;
        std::vector<std::string> ids;   // every entity section id written
        for (const char* ix : kIndex) {
            out += std::string("[") + ix + "]\n";
            std::vector<std::string> list;
            for (const Ini* src : std::initializer_list<const Ini*>{&aimd, &map_})
                for (const auto& kv : src->section(ix)) {
                    const std::string id = camp::trim_comment(kv.second);
                    if (id.empty() || std::find(list.begin(), list.end(), camp::up(id)) != list.end()) continue;
                    list.push_back(camp::up(id));
                    out += std::to_string(list.size() - 1) + "=" + id + "\n";
                }
            for (const std::string& id : list) {
                const Ini& src = map_.has(id) ? map_ : aimd;
                out += "[" + id + "]\n";
                for (const auto& kv : src.section(id)) out += kv.first + "=" + kv.second + "\n";
            }
        }
        for (const char* flat : {"AITriggerTypes", "AITriggerTypesEnable"}) {
            out += std::string("[") + flat + "]\n";
            std::vector<std::string> keys;
            for (const auto& kv : map_.section(flat)) keys.push_back(camp::up(kv.first));
            for (const auto& kv : aimd.section(flat))
                if (std::find(keys.begin(), keys.end(), camp::up(kv.first)) == keys.end())
                    out += kv.first + "=" + kv.second + "\n";
            for (const auto& kv : map_.section(flat)) out += kv.first + "=" + kv.second + "\n";
        }
        return Ini::parse(out);
    }
    // W5-A R-028: the map house's IQ (FUN_00500960) and a Rules [IQ] threshold.
    int house_iq(int eh) const {
        if (eh < 0 || eh >= (int)house_names_.size()) return 0;
        int iq = map_.get_int(house_names_[(std::size_t)eh], "IQ", 0);
        if (iq > iq_rule("MaxIQLevels", 5)) iq = 1;
        return iq;
    }
    int iq_rule(const char* key, int def) const {
        return std::atoi(camp::trim_comment(rules_ini_.get("IQ", key, std::to_string(def))).c_str());
    }
public:
    // W2-CA-07: the GameScene's AI roster (unit defs the map teams can field).
    void set_ai_roster(const UnitRoster* r) { ai_roster_ = r; }
    // W7-10-05 / W7-12-23 (presentation): every live (controller, victim)
    // mind-control pair.
    std::vector<std::pair<int, int>> mind_control_pairs() const {
        std::vector<std::pair<int, int>> v;
        for (std::size_t i = 0; i < utrack_.size(); ++i)
            if (utrack_[i].mc_victim >= 0) v.push_back({static_cast<int>(i), utrack_[i].mc_victim});
        return v;
    }
private:
    int engine_house_of_name(const std::string& name) const {
        const std::string u = camp::up(name);
        for (std::size_t i = 0; i < house_names_.size(); ++i)
            if (camp::up(house_names_[i]) == u) return (int)i;
        // TeamType House= names a country.
        for (std::size_t i = 0; i < house_names_.size(); ++i)
            if (camp::up(map_.get(house_names_[i], "Country", "")) == u) return (int)i;
        return -1;
    }
    int engine_house(int ht) const {
        for (std::size_t i = 0; i < house_type_of_.size(); ++i)
            if (house_type_of_[i] == ht && ht >= 0) return (int)i;
        return -1;
    }
    int house_type(int eh) const {
        return eh >= 0 && eh < (int)house_type_of_.size() ? house_type_of_[(std::size_t)eh] : -1;
    }

    // --- objects [RE §4] ------------------------------------------------------
    static bool live(const ra2engine::Unit& u) { return u.alive && u.die_time < 0.0; }
    static bool live(const ra2engine::Building& b) {
        return b.alive && b.state != ra2engine::Building::State::Dead;
    }
    // [RE: HouseClass RegisterGain FUN_004FF520 / RegisterLoss FUN_004FF370 --
    // a techno whose type has Insignificant (+0x232, ObjectType INI read at
    // 0x5F8D0B) or DontScore (+0xC9F, 0x7132F8) set is never added to the
    // house's building/unit/infantry counters (+0x2F0/+0x2E8/...), which the
    // Destroyed*All events (9/10/11/55/56) read.] Flags, lamps, gates, guard
    // shacks therefore do not keep a house "alive" (ALL06: BadGuy1's flags).
    bool untracked_type(const std::string& id) const {
        auto it = untracked_.find(id);
        if (it != untracked_.end()) return it->second;
        auto yes = [&](const char* k) {
            const std::string v = rules_ini_.get(id, k, "");
            const char c = v.empty() ? 'n' : (char)std::tolower((unsigned char)v[0]);
            return c == 'y' || c == 't' || c == '1';
        };
        const bool r = yes("Insignificant") || yes("DontScore");
        untracked_[id] = r;
        return r;
    }
    bool tracked(const ra2engine::Building& b) const {
        return live(b) && !(b.type >= 0 && b.type < (int)ba_.roster.size() && untracked_type(ba_.roster[(std::size_t)b.type].id));
    }
    bool tracked(const ra2engine::Unit& u) const {
        return live(u) && !u.vet.dont_score &&
               !untracked_type(u.tg.t.rules_id.empty() ? u.image : u.tg.t.rules_id);   // W6-02-15
    }
    bool free_cell_near(int x, int y, int& ox, int& oy, int maxr = 6) const {
        for (int r = 0; r <= maxr; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int cx = x + dx, cy = y + dy;
                    if (w_.walkable(cx, cy) && w_.unit_at_cell(cx, cy) < 0 &&
                        w_.building_at_cell(cx, cy) < 0) {
                        ox = cx;
                        oy = cy;
                        return true;
                    }
                }
        return false;
    }
    bool free_water_near(int x, int y, int& ox, int& oy, int maxr) const {
        const std::vector<std::uint8_t>& wg = w_.grid_for(ra2engine::MovementDomain::Water);
        if (wg.empty()) return false;
        for (int r = 0; r <= maxr; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int cx = x + dx, cy = y + dy;
                    if (cx < 0 || cy < 0 || cx >= w_.grid_w() || cy >= w_.grid_h()) continue;
                    if (!wg[(std::size_t)cy * w_.grid_w() + cx]) continue;
                    if (w_.unit_at_cell(cx, cy) >= 0 || w_.building_at_cell(cx, cy) >= 0) continue;
                    if (!w_.occupancy().is_free(cx, cy)) continue;
                    ox = cx;
                    oy = cy;
                    return true;
                }
        return false;
    }
    bool waypoint_cell(int wp, int& x, int& y) const {
        for (const ra2assets::MapWaypoint& m : w_.map().waypoints())
            if (m.index == wp) { x = m.x; y = m.y; return true; }
        return false;
    }
    static constexpr std::size_t kObjectHeadroom = 1024;   // objects one frame may create
    void reserve_track_headroom() {
        if (utrack_.capacity() < utrack_.size() + kObjectHeadroom) utrack_.reserve(utrack_.size() * 2 + kObjectHeadroom);
        if (btrack_.capacity() < btrack_.size() + kObjectHeadroom) btrack_.reserve(btrack_.size() * 2 + kObjectHeadroom);
    }
    void sync_tracks() {
        while (utrack_.size() < w_.units().size()) {
            Track t;
            t.initial = false;
            const ra2engine::Unit& u = w_.units()[utrack_.size()];
            // W6-02-15 [RE: the build events 19-22 compare the produced
            // TechnoType index, map-triggers.md]: a factory-built unit is
            // tracked under its RULES id (E1), not its art Image= (GI);
            // the image is the fallback for a hand-built UnitCombat.
            t.type = camp::up(u.tg.t.rules_id.empty() ? u.image : u.tg.t.rules_id);
            t.cx = u.cell_x;
            t.cy = u.cell_y;
            t.hp = u.hp;
            // A unit the World produced (factory) counts as last-built.
            if (!placing_ && u.house >= 0 && u.house < (int)last_built_.size()) {
                const int vi = camp::index_of(ra2_vehicles_, t.type);
                const int ii = camp::index_of(ra2_infantry_, t.type);
                if (vi >= 0) last_built_[(std::size_t)u.house][1] = vi;
                // W2-CA-14: event 22 BuildAircraftType.
                const int ai = camp::index_of(ra2_aircraft_, t.type);
                if (ai >= 0) last_built_[(std::size_t)u.house][3] = ai;
                if (ii >= 0) last_built_[(std::size_t)u.house][2] = ii;
            }
            utrack_.push_back(t);
        }
        while (btrack_.size() < w_.buildings().size()) {
            Track t;
            t.initial = false;
            const ra2engine::Building& b = w_.buildings()[btrack_.size()];
            if (b.type >= 0 && b.type < (int)ba_.roster.size())
                t.type = camp::up(ba_.roster[(std::size_t)b.type].id);
            t.hp = b.hp;
            // W6-02-07: the type's Insignificant= / LegalTarget= (map overrides
            // merged) for World::legal_threat_building.
            if (!t.type.empty())
                w_.set_building_target_flags((int)btrack_.size(), rules_flag_default(t.type, "Insignificant", false),
                                             rules_flag_default(t.type, "LegalTarget", true));
            if (!placing_ && b.house >= 0 && b.house < (int)last_built_.size())
                last_built_[(std::size_t)b.house][0] = data_.find_building_type(t.type);
            btrack_.push_back(t);
        }
    }
    // W2-CA-08 [RE-C1 open -> RE-FALLBACK]: a reinforcement whose waypoint
    // has no legal cell within 12 (at sea, off the land grid, beyond the
    // map edge) lands on the nearest free legal cell anywhere: ships on the
    // nearest free water, ground units on the nearest free land cell that is
    // CONNECTED to the team's first move-waypoint (the beach the landing
    // craft would have run to), else the nearest free land cell. The
    // original delivers by transport/edge entry (TeamClass FUN_006eff20).
    bool far_cell(int x, int y, bool water, int goal_x, int goal_y, int& ox, int& oy) {
        const int W = w_.grid_w(), H = w_.grid_h();
        if (W <= 0 || H <= 0) return false;
        const std::vector<std::uint8_t>& wg = w_.grid_for(ra2engine::MovementDomain::Water);
        std::vector<std::uint8_t> reach;
        if (!water && goal_x >= 0 && w_.walkable(goal_x, goal_y)) {
            reach.assign((std::size_t)W * H, 0);
            std::vector<int> q{goal_y * W + goal_x};
            reach[(std::size_t)q[0]] = 1;
            for (std::size_t k = 0; k < q.size(); ++k) {
                const int cx = q[k] % W, cy = q[k] / W;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                        const std::size_t ni = (std::size_t)ny * W + nx;
                        if (reach[ni] || !w_.walkable(nx, ny)) continue;
                        reach[ni] = 1;
                        q.push_back((int)ni);
                    }
            }
        }
        x = std::clamp(x, 0, W - 1);
        y = std::clamp(y, 0, H - 1);
        const int maxr = std::max(W, H);
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1 && reach.empty()) break;
            const bool need_reach = pass == 0 && !reach.empty();
            for (int r = 0; r <= maxr; ++r)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        const int cx = x + dx, cy = y + dy;
                        if (cx < 0 || cy < 0 || cx >= W || cy >= H) continue;
                        const std::size_t ci = (std::size_t)cy * W + cx;
                        if (water) {
                            if (wg.empty() || !wg[ci] || !w_.occupancy().is_free(cx, cy)) continue;
                        } else {
                            if (!w_.walkable(cx, cy)) continue;
                            if (need_reach && !reach[ci]) continue;
                        }
                        if (w_.unit_at_cell(cx, cy) >= 0 || w_.building_at_cell(cx, cy) >= 0) continue;
                        ox = cx;
                        oy = cy;
                        return true;
                    }
            if (water) break;
        }
        return false;
    }
    int place_unit(const std::string& type, int eh, int x, int y, int health, const std::string& tag,
                   const std::string& mission, bool far = false, int goal_x = -1, int goal_y = -1,
                   int subcell = -1) {
        const camp::UnitType* ut = types_.unit(type);
        if (!ut) return -1;
        int cx = 0, cy = 0;
        // W5-B R-061 [RE: map [Infantry] SubCell indexes the spot table
        // DAT_0089D960 directly, FUN_0051F810]: a map infantry goes into its
        // own spot of its own cell -- up to three share a cell.
        const bool inf = !ut->combat.is_vehicle && !ut->combat.is_aircraft && !ut->combat.is_naval;
        int spot = -1;
        if (inf && subcell >= 0 && w_.subcells_enabled() && w_.walkable(x, y) &&
            w_.building_at_cell(x, y) < 0)
            spot = w_.occupancy().free_spot(x, y, subcell);
        // gF G-73: a ship is placed on the nearest free WATER cell (naval.md
        // §1 water grid); a ground unit whose waypoint lies at sea goes to the
        // nearest free shore cell within 12 [RE-FALLBACK §9.3: the original
        // delivers by transport / edge entry; no TeamClass delivery yet].
        if (ut->combat.is_aircraft) {
            // W2-CA-16: an aircraft enters at its waypoint (the map edge for
            // most teams) on a free cell nearby; it takes off on its first order.
            x = std::clamp(x, 0, std::max(0, w_.grid_w() - 1));
            y = std::clamp(y, 0, std::max(0, w_.grid_h() - 1));
            cx = x;
            cy = y;
            bool found = false;
            for (int r = 0; r <= 12 && !found; ++r)
                for (int dy = -r; dy <= r && !found; ++dy)
                    for (int dx = -r; dx <= r && !found; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        const int ax = x + dx, ay = y + dy;
                        if (ax < 0 || ay < 0 || ax >= w_.grid_w() || ay >= w_.grid_h()) continue;
                        if (w_.unit_at_cell(ax, ay) >= 0 || w_.building_at_cell(ax, ay) >= 0) continue;
                        if (!w_.occupancy().is_free(ax, ay)) continue;
                        cx = ax; cy = ay; found = true;
                    }
            if (!found) return -1;
        } else if (ut->combat.is_naval) {
            if (!free_water_near(x, y, cx, cy, 12) &&
                !(far && far_cell(x, y, /*water=*/true, -1, -1, cx, cy)))
                return -1;
        } else if (spot >= 0) {
            cx = x;
            cy = y;
        } else if (ut->combat.movement_domain == ra2engine::MovementDomain::Amphibious && !w_.walkable(x, y) &&
                   free_water_near(x, y, cx, cy, 12)) {
            // R-210: an amphibious unit (the ALL04 landing craft, LCRF) whose
            // waypoint lies at sea enters on the water there. It used to be put
            // on the nearest LAND cell -- the ALL04 craft landed on the level-4
            // pier at (127,57) and only reached the sea by dropping four levels,
            // which the height gate (FUN_004D9B70) now refuses.
        } else if (!free_cell_near(x, y, cx, cy) && !free_cell_near(x, y, cx, cy, 12) &&
                   !(far && far_cell(x, y, /*water=*/false, goal_x, goal_y, cx, cy))) {
            return -1;
        }
        if (place_vet_ > 0) {   // W4-SQ-04: TeamType VeteranLevel on a created member
            ra2engine::UnitCombat c = ut->combat;
            c.veteran_rank = place_vet_;
            w_.add_combat_unit(cx, cy, eh, c);
        } else {
            w_.add_combat_unit(cx, cy, eh, ut->combat);
        }
        const int idx = (int)w_.units().size() - 1;
        if (spot >= 0) w_.place_in_spot(idx, spot);   // W5-B R-061
        ra2engine::Unit& u = w_.units()[(std::size_t)idx];
        if (health > 0) u.hp = ra2engine::campaign_strength(u.max_hp, health, false);
        if (ua_anims_has(u.image)) {}
        // W3-FIX-A (R7): a BalloonHover jumpjet (the Kirov) never lands, so a
        // map-placed or reinforcing one starts at its hover height instead of
        // parked on the ground. order_unit_move releases its ground cell and
        // opens the flight step (the factory-exit lift-off path, W2-AN-07).
        if (u.is_aircraft && u.balloon_hover) {
            w_.order_unit_move(idx, cx, cy);
            ra2engine::Unit& a = w_.units()[(std::size_t)idx];
            a.altitude = a.cruise_altitude > 0.0 ? a.cruise_altitude : a.altitude;
            if (a.altitude > 0.0) a.flight_state = ra2engine::FlightState::Cruising;
        }
        placing_ = true;
        sync_tracks();
        placing_ = false;
        Track& t = utrack_[(std::size_t)idx];
        t.type = camp::up(type);
        t.initial = true;
        t.hp = u.hp;
        t.tag = tag.empty() ? -1 : data_.find_tag(tag);
        // An object created after scenario start attaches its tag now (the
        // ctor attaches the map's own objects once the system exists).
        if (t.tag >= 0 && sys_) sys_->attach_tag(t.tag);
        t.hunt = ra2engine::mission_from_name(mission) == ra2engine::kMissionHunt;
        // W4-SQ-05 [RE: map-triggers.md §1 object Mission=; TS lineage Mission_Sleep /
        // Mission_Harmless]: a Sleep or Harmless object never picks a target and
        // does not shoot back until a WakeUp action (81/82/83) [RE-FALLBACK: the
        // clone's acquire/retaliate switches stand in for the mission].
        {
            const int mi = ra2engine::mission_from_name(mission);
            // W6-02-11: Mission=Area Guard scans at the doubled guard radius.
            if (mi == ra2engine::kMissionAreaGuard) w_.units()[(std::size_t)idx].tg.area_guard = true;
            if (mi == ra2engine::kMissionSleep || mi == 23 /* Harmless */) {
                t.mission = mi == ra2engine::kMissionSleep ? 1 : 2;
                set_dormant(idx, true);
                // w7fix-combat W7-07-02: the World's mission model holds it.
                w_.units()[(std::size_t)idx].tg.mission = mi;
            }
            // W7-07-11: Mission=Sticky holds its ground (no approach, no scatter).
            if (mi == 6) w_.units()[(std::size_t)idx].tg.mission = 6;
            // P1 [RE: campaign.md §4 -- campaign objects get their INI
            // mission (vtbl+0x1E8); economy.md §1 -- Mission_Harvest runs the
            // harvester FSM FUN_0073C560]: a map-placed miner with
            // Mission=Harvest (ALL11's three Chrono Miners, the AI's HARVs in
            // 14 maps) starts hunting ore like a factory-built one. Before,
            // it stood idle forever (ALL11: no income at all).
            if (mi == 10 /* Harvest */ && rules_flag_default(camp::up(type), "Harvester", false)) {
                const std::string id = camp::up(type);
                const int st = std::atoi(camp::trim_comment(rules_ini_.get(id, "Storage", "0")).c_str());
                w_.make_harvester(idx, id == "CMIN", st > 0 ? st : 20);
            }
        }
        return idx;
    }
    // W5-A R-033: a map object's Facing byte as the unit's facing. Vehicles
    // (and aircraft) keep the original's 16-bit space (facing << 8 = body and
    // turret); infantry get the 8-block index of that direction.
    void apply_map_facing(int idx, int facing) { set_facing_u16(idx, (double)((facing & 0xFF) << 8)); }
    static bool ua_anims_has(const std::string&) { return true; }
    void set_dormant(int idx, bool on) {
        ra2engine::Unit& u = w_.units()[(std::size_t)idx];
        if (on) {
            u.an.t.no_passive_acquire = true;
            u.abil.no_retaliate = true;
            u.target = -1;
            u.target_building = -1;
            return;
        }
        const std::string& id = utrack_[(std::size_t)idx].type;
        u.an.t.no_passive_acquire = !rules_flag_default(id, "CanPassiveAquire", true);
        u.abil.no_retaliate = !rules_flag_default(id, "CanRetaliate", true);
        if (u.tg.mission == 0 || u.tg.mission == 23) w_.wake_unit(idx);   // w7fix-combat
    }
    bool rules_flag_default(const std::string& id, const char* key, bool def) const {
        const std::string v = camp::up(camp::trim_comment(rules_ini_.get(id, key, def ? "yes" : "no")));
        return !v.empty() && (v[0] == 'Y' || v[0] == 'T' || v[0] == '1');
    }
    // P4 [RE-FALLBACK: the TS/RA1 mission model -- an order is Assign_Mission,
    // which replaces Mission_Sleep / Mission_Harmless; the unit then arrives into
    // Guard and acquires as usual]: a dormant (W4-SQ-05) unit that is moving or
    // carries an attack order -- the player's or its team's -- wakes up. ALL09's
    // freed POW GIs / Grizzlies (Harmless, handed to the player by ChangeHouse)
    // otherwise never fired a shot for the rest of the mission.
    void wake_ordered() {
        for (std::size_t i = 0; i < utrack_.size() && i < w_.units().size(); ++i) {
            Track& t = utrack_[i];
            if (t.dead || t.mission == 0) continue;
            const ra2engine::Unit& u = w_.units()[i];
            // w7fix-combat W7-09-08/09: the World woke it (hit while Harmless,
            // an owner change) -- the dormant flags follow.
            const bool woke = live(u) && u.tg.mission != 0 && u.tg.mission != 23;
            if (!live(u) || !(u.moving() || u.attack_order || woke)) continue;
            t.mission = 0;
            set_dormant((int)i, false);
        }
    }
    // 81 WakeupSelf (tagged objects), 82 WakeupAllSleepers, 83 WakeupAllHarmless.
    int wake(int kind, int trigger_type) {
        int n = 0;
        for (std::size_t i = 0; i < utrack_.size(); ++i) {
            Track& t = utrack_[i];
            if (t.dead || t.mission == 0) continue;
            if (kind == 81 && !(t.tag >= 0 && sys_->tag_has_trigger(t.tag, trigger_type))) continue;
            if (kind == 82 && t.mission != 1) continue;
            if (kind == 83 && t.mission != 2) continue;
            t.mission = 0;
            set_dormant((int)i, false);
            ++n;
        }
        return n;
    }
    void place_objects(const std::vector<ra2engine::MapObjectRec>& objs) {
        for (const ra2engine::MapObjectRec& o : objs) {
            const int eh = engine_house_of_name(o.owner);
            if (eh < 0) { ++skipped_; continue; }
            if (o.kind == 0) {
                const int bt = types_.building(ba_, o.type);
                if (bt < 0) { ++skipped_; continue; }
                const ra2engine::BuildingTypeDef& d = ba_.roster[(std::size_t)bt];
                // W3-FIX-A: a WaterBound type (Naval Yard) is legal on WATER, as
                // the player's placement already checks (realplay_scene.inc);
                // the land grid refused every map-placed yard (ALL04/07/11).
                const bool water = (std::size_t)bt < ba_.water_bound.size() && ba_.water_bound[(std::size_t)bt] &&
                                   !w_.grid_for(ra2engine::MovementDomain::Water).empty();
                const ra2engine::PlacementCheck chk = ra2engine::placement_check(
                    o.x, o.y, d.fw, d.fh, eh, d.adjacent,
                    water ? w_.grid_for(ra2engine::MovementDomain::Water) : w_.passable(), w_.grid_w(),
                    w_.grid_h(), w_.occupancy(), w_.buildings(), /*bypass=*/true);
                if (!chk.ok) {   // Unlimbo failed -> deleted [RE §4]
                    std::cerr << "[campaign] structure " << o.type << " at " << o.x << "," << o.y
                              << " refused (" << (water ? "water" : "land") << ")\n";
                    ++skipped_;
                    continue;
                }
                const int idx = w_.add_building(bt, o.x, o.y, d.fw, d.fh, eh, d.strength, d.armor_ix,
                                                0.0, /*instant=*/true, d.sight, d.grants_map_reveal);
                if (idx < 0) { ++skipped_; continue; }
                if (water) w_.block_water_footprint(o.x, o.y, d.fw, d.fh);   // ships path around it
                ra2engine::Building& b = w_.buildings()[(std::size_t)idx];
                b.hp = ra2engine::campaign_strength(d.strength, o.health, true);
                b.ai_repairable = o.ai_repairable;   // fv7c [RE: FUN_0044F810 field 16 -> +0x6CB]
                placing_ = true;
                sync_tracks();
                placing_ = false;
                Track& t = btrack_[(std::size_t)idx];
                t.tag = o.tag.empty() ? -1 : data_.find_tag(o.tag);
                t.hp = b.hp;
                ++placed_b_;
            } else {
                if (o.kind != 3 && !w_.walkable(o.x, o.y) && !types_.unit(o.type)) { ++skipped_; continue; }
                // W5-A R-033 [RE: campaign.md §4 field order; the object reader
                // feeds Veterancy (0..200, 100 = veteran, 200 = elite: the
                // VeterancyStruct value x100) and Facing (0..255 = DirStruct >> 8)
                // to the new techno]: per-object rank and facing.
                place_vet_ = o.veterancy >= 200 ? 2 : o.veterancy >= 100 ? 1 : 0;
                const int idx = place_unit(o.type, eh, o.x, o.y, o.health, o.tag, o.mission, false, -1, -1,
                                           o.kind == 2 ? o.subcell : -1);   // W5-B SubCell
                place_vet_ = 0;
                if (idx < 0) { ++skipped_; continue; }
                ++placed_u_;
                apply_map_facing(idx, o.facing);
            }
        }
    }

    // --- event polling [RE map-triggers §3.2] --------------------------------
    ra2engine::RaiseContext ctx_for(int house) const {
        ra2engine::RaiseContext c;
        c.has_object = true;
        c.object_house_type = house_type(house);
        return c;
    }
    void raise_obj(Track& t, int kind, int house) {
        if (t.tag < 0) return;
        const ra2engine::MapTriggerSystem::ObjectRaise r =
            sys_->raise_object(t.tag, kind, ctx_for(house), host_);
        ++raises_;
        if (r.detached || r.tag_gone) t.tag = -1;
    }
    void raise_obj_ctx(Track& t, int kind, const ra2engine::RaiseContext& c) {
        if (t.tag < 0) return;
        const ra2engine::MapTriggerSystem::ObjectRaise r = sys_->raise_object(t.tag, kind, c, host_);
        ++raises_;
        if (r.detached || r.tag_gone) t.tag = -1;
    }
    // W5-A event 31 AllBridgesDestroyed [RE: map-triggers.md §3.1 -- a Cell
    // kind; the stock maps tag a bridge cell, e.g. ALL03 (98,115)]: when a
    // bridge breaks, every [CellTags] cell that is a now-broken bridge cell
    // raises 31 [RE-FALLBACK: the raise site inside the bridge-destruction
    // code (FUN_005766D0 / FUN_00570FC0) was not traced; the edge is taken
    // per tagged cell].
    bool bridge_cell_broken(int x, int y) const {
        if (w_.high_bridge_cell(x, y)) return w_.high_bridge_destroyed(x, y);
        const int ov = w_.bridges_enabled() ? w_.bridge_overlay_at(x, y) : -1;
        return ov >= 0 && ra2engine::is_low_bridge_overlay(ov) && ra2engine::low_bridge_destroyed(ov);
    }
    void poll_bridges() {
        const std::uint32_t v = w_.bridge_version();
        if (bridge_seen_.size() != data_.cell_tags.size()) {
            bridge_seen_.assign(data_.cell_tags.size(), 0);
            for (std::size_t i = 0; i < data_.cell_tags.size(); ++i)
                bridge_seen_[i] = bridge_cell_broken(data_.cell_tags[i].x, data_.cell_tags[i].y) ? 1 : 0;
            bridge_ver_ = v;
            return;
        }
        if (v == bridge_ver_) return;
        bridge_ver_ = v;
        for (std::size_t i = 0; i < data_.cell_tags.size(); ++i) {
            const ra2engine::MapCellTag& ct = data_.cell_tags[i];
            const std::uint8_t now = bridge_cell_broken(ct.x, ct.y) ? 1 : 0;
            if (now && !bridge_seen_[i] && sys_->tag_waits_on(ct.tag, ra2engine::kEvAllBridgesDestroyed)) {
                ra2engine::RaiseContext c;
                if (sys_->raise_cell(ct.x, ct.y, ra2engine::kEvAllBridgesDestroyed, c, host_)) ++raises_;
                char b[96];
                std::snprintf(b, sizeof b, "[camp] f%ld bridge broken under cell tag (%d,%d) -> event 31", frame_,
                              ct.x, ct.y);
                witness_.emplace_back(b);
            }
            bridge_seen_[i] = now;
        }
    }
    // W5-A 9 FireSale [RE: Execute case 9 sets house +0x250 = 4; HouseClass AI
    // FUN_004FD320 then runs, every AI pass, vtbl +0x34 / +0x38 on the house
    // (the RA1-lineage Fire_Sale + Do_All_To_Hunt)]: every building of the
    // house is sold and every unit hunts, for as long as the house lives.
    // 35 PreferredTarget: the house quarry goes to the World's AI SW picker.
    void step_house_states() {
        for (std::size_t h = 0; h < quarry_.size(); ++h)
            if (quarry_[h] != 1) w_.set_sw_quarry((int)h, quarry_[h]);
        if (frame_ % 15 != 0) return;
        for (std::size_t h = 0; h < fire_sale_.size(); ++h)
            if (fire_sale_[h]) fire_sale_pass((int)h);
    }
    void fire_sale_pass(int h) {
        for (int i = 0; i < (int)w_.buildings().size(); ++i)
            if (live(w_.buildings()[(std::size_t)i]) && w_.buildings()[(std::size_t)i].house == h &&
                !w_.buildings()[(std::size_t)i].selling)
                w_.begin_sell(i, h);
        for (std::size_t i = 0; i < w_.units().size() && i < utrack_.size(); ++i)
            if (live(w_.units()[i]) && w_.units()[i].house == h) utrack_[i].hunt = true;
    }
    bool team_left(const std::string& id) const {
        for (const std::string& t : left_teams_) if (camp::up(t) == camp::up(id)) return true;
        return false;
    }
    // gF G-74 [RE: docs/re/mcv-deploy.md §7]: event 44 AttackedByHouse is
    // raised by ReceiveDamage right after 6 with the ATTACKER's house
    // (FUN_005F4BA0 @0x5F5015, BuildingClass FUN_004690A0 @0x4692A8). World
    // keeps no per-hit source, so the attacker is the first live enemy unit
    // whose current target is the victim [RE-FALLBACK: inferred source].
    // RA2_CAMP_DEBUG only: the base defences aiming at a unit as it dies
    // (" def=TYPE@x,y" each) -- a defence kill leaves killed_by at -1.
    std::string debug_defence_shooters(int victim) const {
        std::string s;
        for (const ra2engine::Building& b : w_.buildings())
            if (b.alive && b.def_target == victim && b.type >= 0 && b.type < (int)w_.btypes().size())
                s += " def=" + w_.btypes()[(std::size_t)b.type].id + "@" + std::to_string(b.cx) + "," +
                     std::to_string(b.cy);
        return s;
    }
    int gf_attacker_house(int unit_victim, int building_victim, int victim_house) const {
        const std::vector<ra2engine::Unit>& us = w_.units();
        for (const ra2engine::Unit& a : us) {
            if (!live(a) || a.house == victim_house) continue;
            if ((unit_victim >= 0 && a.target == unit_victim) ||
                (building_victim >= 0 && a.target_building == building_victim))
                return a.house;
        }
        return -1;
    }
    void raise_attacked(Track& t, int house, int src_house) {
        raise_obj(t, ra2engine::kEvAttackedByAnybody, house);
        if (t.tag < 0 || src_house < 0) return;
        ra2engine::RaiseContext c = ctx_for(house);
        c.source_house_type = house_type(src_house);
        const ra2engine::MapTriggerSystem::ObjectRaise r =
            sys_->raise_object(t.tag, ra2engine::kEvAttackedByHouse, c, host_);
        ++raises_;
        if (r.detached || r.tag_gone) t.tag = -1;
    }
    // gF G-74: 25/26 CrossesHorizontal/VerticalLine -- FootClass per-cell
    // (FUN_004D84F0 @0x4D8990..0x4D8A8A): entering a cell whose row (column)
    // is flagged scans that whole row (column) and raises 25 (26) on every
    // cell tag in it.
    void gf_raise_lines(int x, int y, const ra2engine::RaiseContext& c) {
        for (const ra2engine::MapCellTag& ct : data_.cell_tags) {
            if (ct.y == y && sys_->raise_cell(ct.x, ct.y, ra2engine::kEvCrossesHorizontalLine, c, host_)) ++raises_;
            if (ct.x == x && sys_->raise_cell(ct.x, ct.y, ra2engine::kEvCrossesVerticalLine, c, host_)) ++raises_;
        }
    }
    // W2-CA-01 [RE: FUN_00519449 @0x519545/0x5195AB/0x51982E/0x519973/
    // 0x519D2C/0x51A052/0x51A304]: an infantry arriving INTO a building
    // raises 1 EnteredBy on the building's tag with the infantry as the
    // object; the consumed infantry then raises 48 on its own tag
    // (@0x5196B4/0x519E0D) -- not 7/29, and it is not a loss.
    // [RE-FALLBACK: no SpiedBy (2) RaiseEvent site was found in the infantry
    // handler; 2 is raised on infiltration beside 1 as the event's name says.]
    void poll_entered() {
        w_.set_track_entered(true);
        for (const ra2engine::World::EnteredEvent& e : w_.take_entered_events()) {
            if (e.building >= 0 && e.building < (int)btrack_.size()) {
                Track& bt = btrack_[(std::size_t)e.building];
                raise_obj(bt, ra2engine::kEvEnteredBy, e.house);
                if (e.kind == ra2engine::World::EnteredEvent::Infiltrate)
                    raise_obj(bt, ra2engine::kEvSpiedBy, e.house);
                char b[120];
                std::snprintf(b, sizeof b, "[camp] f%ld entered kind=%d unit=%d building=%d %s house=%d",
                              frame_, (int)e.kind, e.unit, e.building, bt.type.c_str(), e.house);
                witness_.emplace_back(b);
            }
            if (e.consumed && e.unit >= 0 && e.unit < (int)utrack_.size()) {
                Track& ut = utrack_[(std::size_t)e.unit];
                if (ut.dead) continue;
                ut.dead = true;
                raise_obj(ut, ra2engine::kEvDestroyedByAnything, e.house);
                if (ut.tag >= 0) { sys_->detach_tag(ut.tag); ut.tag = -1; }
            }
        }
    }
    // W2-CA-14: 38/41 first damage, 39/42 at or below half, 40/43 at or below
    // a quarter of Strength, each once per object [map-triggers.md §3 event
    // table; RE-W2-4 open: the combat-only (38-40) vs any-source (41-43) split
    // is not modelled -- the clone has no non-combat damage source on these
    // objects, so both of a pair are raised together (RE-FALLBACK)].
    void raise_damage_stages(Track& t, int hp, int max_hp, int house) {
        if (max_hp <= 0 || t.tag < 0) return;
        const struct { std::uint8_t bit; bool hit; int ev_combat, ev_any; } st[] = {
            {1, hp < max_hp, 38, 41}, {2, 2 * hp <= max_hp, 39, 42}, {4, 4 * hp <= max_hp, 40, 43}};
        for (const auto& s : st) {
            if (!s.hit || (t.dmg_stage & s.bit)) continue;
            t.dmg_stage |= s.bit;
            raise_obj(t, s.ev_combat, house);
            if (t.tag >= 0) raise_obj(t, s.ev_any, house);
            if (t.tag < 0) return;
        }
    }
    // W2-CA-15 [RE-W2-5 open -> RE-FALLBACK; rules: a weapon whose Warhead has
    // MindControl=yes (Yuri, Psychic Tower/Beacon lineage)]: a controller
    // whose current target is an enemy unit in weapon range takes it over --
    // the victim changes to the controller's house and drops its orders; one
    // victim per controller (a new capture frees the previous one back to
    // its house); a dead controller frees its victim. The capture raises 6
    // and 44 (attacker = controller's house) on the victim, as a hit does.
    bool mind_controller(const std::string& type) {
        auto it = mc_type_.find(type);
        if (it != mc_type_.end()) return it->second;
        const Rules rules = Rules::load(rules_ini_);
        const std::string wh = rules.weapon_warhead(rules.section_string(type, "Primary", ""));
        const bool mc = !wh.empty() && rules.section_flag(wh, "MindControl", false);
        mc_type_[type] = mc;
        return mc;
    }
    // W7-10-03 / W7-08-11 [RE: CaptureManager::FreeUnit FUN_00471F20]: free
    // victim `v` of controller `ctl` back to the house it had at capture
    // (stop and guard). W7-08-12(c) [RE: Free @0x472013 -> DecideUnitFate]: a
    // unit handed back to a computer house gets its fate (Hunt, or the
    // releasing controller's team refused); a human owner keeps it standing.
    // `fate` false for the capacity-1 swap (W5-A R-036), which gets no roll.
    void mc_free(int ctl, int v, bool fate = true) {
        auto& us = w_.units();
        if (ctl >= 0 && ctl < (int)us.size()) {
            std::vector<int>& vv = us[(std::size_t)ctl].w7.mc_victims;
            vv.erase(std::remove(vv.begin(), vv.end(), v), vv.end());
        }
        if (v < 0 || v >= (int)utrack_.size()) return;
        Track& vt = utrack_[(std::size_t)v];
        ra2engine::Unit& vu = us[(std::size_t)v];
        if (vu.w7.mc_controller != ctl) return;   // held by somebody else now
        const int orig = vt.mc_orig;
        vt.mc_orig = -1;
        w_.release_unit_mind_control_victim(ctl, v, orig);
        if (orig < 0 || !live(vu)) return;
        if (fate && !w_.human_commanded(orig) && ctl >= 0 && ctl < (int)utrack_.size()) {
            if (vt.team >= 0 && vt.team < (int)teams_.size()) {
                std::vector<int>& mm = teams_[(std::size_t)vt.team].members;
                mm.erase(std::remove(mm.begin(), mm.end(), v), mm.end());
            }
            vt.team = -1;
            vt.hunt = false;
            decide_unit_fate(ctl, v, /*allow_team=*/false);
        }
    }
    // W7-10-01 / W7-08-12(c) [RE: CaptureManagerClass::DecideUnitFate
    // FUN_004722E0, called after Capture FUN_00471C80 for a computer new owner
    // (+0x1EC IsHuman clear)]. The situation picks a rulesmd [General] weight
    // list -- AICaptureLowMoney when the capturer's money is under
    // AICaptureLowMoneyMark (Rules+0xEBC), else AICaptureLowPower when its power
    // is short (FUN_004FCC50 < 1.0), else AICaptureNormal while the victim's
    // health ratio is >= AICaptureWoundedMark (Rules+0xEC0), else
    // AICaptureWounded -- and a 1..100 roll walks its four weights: 1 Add To
    // Team, 2 Grinder, 3 Bio Reactor, 4 Hunt. The capturer's TeamType
    // MindControlDecision (+0xC0) overrides the roll (5 = do nothing). Any
    // option that fails ("AICapture: Naw, let's put him in hunt") becomes
    // Mission Hunt (0xF). Grinder / Bio Reactor are Yuri-only structures, so in
    // the clone they always fall through to Hunt. [RE-FALLBACK: the game
    // Random is a deterministic per-capture hash here.]
    // `allow_team` false on the release path (the team join is refused there:
    // the flag tested @0x4724E8 before AddMember) [RE-FALLBACK: flag identity].
    void decide_unit_fate(int ctl, int vi, bool allow_team = true) {
        Track& ct = utrack_[(std::size_t)ctl];
        Track& vt = utrack_[(std::size_t)vi];
        const ra2engine::Unit& v = w_.units()[(std::size_t)vi];
        const int h = w_.units()[(std::size_t)ctl].house;
        auto gen = [&](const char* key, const char* def) {
            return camp::trim_comment(rules_ini_.get("General", key, def));
        };
        const double money = w_.ai_enabled(h) ? w_.ai_credits(h) : w_.credits(h);
        const char* list = "AICaptureNormal";
        const char* list_def = "75,5,5,15";
        if (money < std::atof(gen("AICaptureLowMoneyMark", "2000").c_str())) {
            list = "AICaptureLowMoney"; list_def = "15,75,5,5";
        } else if (w_.house_low_power(h)) {
            list = "AICaptureLowPower"; list_def = "15,5,75,5";
        } else if (v.max_hp > 0 && (double)v.hp / v.max_hp < std::atof(gen("AICaptureWoundedMark", ".25").c_str())) {
            list = "AICaptureWounded"; list_def = "15,40,40,5";
        }
        std::vector<int> wts;
        {
            std::string cur;
            for (char c : gen(list, list_def) + ",") {
                if (c == ',') { if (!cur.empty()) wts.push_back(std::atoi(cur.c_str())); cur.clear(); }
                else cur += c;
            }
        }
        std::uint32_t r = (std::uint32_t)frame_ * 2654435761u ^ (std::uint32_t)vi * 40503u ^ 0x9E3779B9u;
        r ^= r >> 15; r *= 0x2C1B3C6Du; r ^= r >> 12;
        const int roll = 1 + (int)(r % 100u);
        int decision = 0, acc = 0;
        for (std::size_t k = 0; k < wts.size() && k < 6 && acc < roll; ++k) {
            acc += wts[k];
            decision = (int)k + 1;
        }
        // The capturer's TeamType MindControlDecision overrides the roll.
        if (ct.team >= 0 && ct.team < (int)teams_.size()) {
            const int tt = teams_[(std::size_t)ct.team].type;
            if (tt >= 0 && tt < (int)teams_data_.teams.size()) {
                const int o = std::atoi(camp::trim_comment(
                    map_.get(teams_data_.teams[(std::size_t)tt].id, "MindControlDecision", "0")).c_str());
                if (o != 0) decision = o;
            }
        }
        const char* fate = "hunt";
        if (decision == 5) {
            fate = "none";
        } else if (decision == 1 && allow_team && ct.team >= 0 && ct.team < (int)teams_.size()) {
            teams_[(std::size_t)ct.team].members.push_back(vi);   // FUN_006E9A60 AddMember
            vt.team = ct.team;
            fate = "team";
        } else {
            vt.hunt = true;   // vtbl+0x1E8 QueueMission(Hunt)
        }
        char b[120];
        std::snprintf(b, sizeof b, "[camp] f%ld capture fate unit=%d roll=%d decision=%d -> %s", frame_, vi, roll,
                      decision, fate);
        witness_.emplace_back(b);
    }
    // [RE: CaptureManager::FreeAll FUN_00472070]
    void mc_free_all(int ctl) {
        if (ctl < 0 || ctl >= (int)w_.units().size()) return;
        const std::vector<int> vv = w_.units()[(std::size_t)ctl].w7.mc_victims;
        for (int v : vv) mc_free(ctl, v);
        w_.units()[(std::size_t)ctl].w7.mc_victims.clear();
        if (ctl < (int)utrack_.size()) utrack_[(std::size_t)ctl].mc_victim = -1;
    }
    // W2-CA-15 / W7-10-02..04 / W7-10-09 / W7-08-11..12 [RE: CaptureManager
    // FUN_004716F0 (ctor: Max = primary Damage, Infinite =
    // InfiniteMindControl), CanCapture FUN_00471BB0, Capture FUN_00471C80,
    // FreeUnit FUN_00471F20, FreeAll FUN_00472070, CannotControlAnyMore
    // FUN_004721D0, DecideUnitFate FUN_004722E0]:
    //   * the capture rides the controller's real shot (Unit::w7.mc_shot,
    //     set by try_fire -- ROF, facing and range apply);
    //   * CanCapture refuses the own house, ImmuneToPsionics, an Iron-Curtained
    //     target and one that is already mind-controlled (no stealing);
    //   * the capacity is the weapon's Damage (YR rules; SOV09's map gives 10),
    //     Infinite lifts it; with Max == 1 a new capture frees the old victim,
    //     with Max > 1 a full controller cannot take more;
    //   * a full controller does not auto-acquire (CannotControlAnyMore);
    //   * FreeAll on the controller's death, when it boards a transport /
    //     building, and when a Temporal shot starts on it; a victim that
    //     boards is freed first; a dead victim leaves the list;
    //   * the victim always leaves its team; a computer new owner puts it on
    //     Hunt (DecideUnitFate's fallback; Grinder / Bio Reactor are Yuri-only).
    void poll_mind_control() {
        auto& us = w_.units();
        for (std::size_t i = 0; i < utrack_.size(); ++i) {
            Track& t = utrack_[i];
            ra2engine::Unit& u = us[i];
            const int ci = static_cast<int>(i);
            // Victim bookkeeping: dead victims drop out; a boarding victim is freed.
            if (!u.w7.mc_victims.empty()) {
                const std::vector<int> vv = u.w7.mc_victims;
                for (int v : vv) {
                    if (v < 0 || v >= (int)us.size()) continue;
                    const ra2engine::Unit& vu = us[(std::size_t)v];
                    if (!live(vu) || vu.w7.mc_controller != ci) {
                        std::vector<int>& lst = u.w7.mc_victims;
                        lst.erase(std::remove(lst.begin(), lst.end(), v), lst.end());
                        if (!live(vu) && v < (int)utrack_.size()) utrack_[(std::size_t)v].mc_orig = -1;
                        if (!live(vu) && vu.w7.mc_controller == ci) us[(std::size_t)v].w7.mc_controller = -1;
                    } else if ((vu.cargo_of >= 0 && vu.cargo_of != ra2engine::kParachuteCargo) || vu.garrison_of >= 0) {   // W7-08-12(b)
                        mc_free(ci, v);
                    }
                }
            }
            t.mc_victim = u.w7.mc_victims.empty() ? -1 : u.w7.mc_victims.back();
            if (!live(u) || u.cargo_of >= 0 || u.warp_remaining > 0) {
                if (!u.w7.mc_victims.empty()) mc_free_all(ci);
                u.w7.mc_shot = -1;
                continue;
            }
            if (!t.dead) u.w_mind_control = mind_controller(t.type);
            if (t.dead || !mind_controller(t.type)) continue;
            const int cap = std::max(1, u.w7.t.mc_capacity);
            const bool infinite = u.w7.t.mc_infinite;
            const bool full = !infinite && (int)u.w7.mc_victims.size() >= cap;
            // Targeting refusals (GetFireError 5 at 0x6FC05F): immune, curtained
            // or already-controlled targets are dropped before a shot.
            if (u.target >= 0 && u.target < (int)us.size()) {
                const ra2engine::Unit& tv = us[(std::size_t)u.target];
                if (!w_.psionic_target_ok(u.target) || tv.an.iron_left_frames > 0 ||
                    (tv.w7.mc_controller >= 0 && tv.w7.mc_controller != ci)) {
                    u.target = -1;
                    u.attack_order = false;
                }
            }
            // CannotControlAnyMore: a full controller picks no victim by itself.
            if (full && !u.attack_order && u.target >= 0) u.target = -1;
            const int vi = u.w7.mc_shot;
            u.w7.mc_shot = -1;
            if (vi < 0 || vi >= (int)us.size()) continue;
            ra2engine::Unit& v = us[(std::size_t)vi];
            if (!live(v) || v.cargo_of >= 0 || v.house == u.house || v.is_aircraft) continue;
            if (!w_.psionic_target_ok(vi) || v.an.iron_left_frames > 0) continue;   // W7-08-04
            if (v.w7.mc_controller >= 0) continue;                                    // W7-10-03 / W7-08-11
            if (full) {
                if (cap != 1 || infinite) continue;           // Max > 1: no room
                mc_free(ci, u.w7.mc_victims.front(), /*fate=*/false);   // Max == 1: the old one goes
            }
            Track& vt = utrack_[(std::size_t)vi];
            const int from = v.house;
            if (vt.mc_orig < 0) vt.mc_orig = from;
            raise_attacked(vt, from, u.house);
            // [RE: TechnoClass::SetOwningHouse FUN_00700940]: the victim drops
            // its target and move and guards where it stands.
            if (!w_.capture_unit_mind_control(ci, vi)) {
                vt.mc_orig = -1;
                continue;
            }
            // [RE: Capture 0x471C80 -> 0x6E9DD0]: the victim always leaves its
            // team (W7-10-01 [RE: DecideUnitFate FUN_004722E0 @0x4722FB]); a
            // computer capturer then rolls its fate below.
            if (vt.team >= 0 && vt.team < (int)teams_.size()) {
                std::vector<int>& mm = teams_[(std::size_t)vt.team].members;
                mm.erase(std::remove(mm.begin(), mm.end(), vi), mm.end());
            }
            vt.team = -1;
            vt.hunt = false;
            t.mc_victim = vi;
            u.target = -1;
            u.attack_order = false;
            char b[120];
            std::snprintf(b, sizeof b, "[camp] f%ld mind control unit=%d %s house %d -> %d", frame_, vi,
                          vt.type.c_str(), from, u.house);
            witness_.emplace_back(b);
            if (!w_.human_commanded(u.house)) decide_unit_fate((int)i, vi);
        }
    }
    void poll_objects() {
        sync_tracks();
        poll_entered();
        poll_mind_control();
        for (std::size_t i = 0; i < utrack_.size(); ++i) {
            Track& t = utrack_[i];
            if (t.dead) continue;
            const ra2engine::Unit& u = w_.units()[i];
            if (!live(u) && u.deployed_away) {
                // gF: an MCV that unfolded into its ConYard left the map by
                // Limbo+delete, not by destruction -- no Destroyed event, not
                // counted as a loss [RE: docs/re/mcv-deploy.md §2 step 6].
                t.dead = true;
                if (t.tag >= 0) { sys_->detach_tag(t.tag); t.tag = -1; }
                continue;
            }
            if (!live(u) && !u.alive && u.die_time < 0.0 && t.team >= 0 && t.team < (int)teams_.size()) {
                // W5-A event 23: a team member flew off the map edge
                // (World::order_leave_map); once its team has nobody left the
                // TeamType "left the map" [RE: FUN_0071DBE0 case 0x17 -- a team
                // of the event's type with no members and its left-map flag].
                Team& lt = teams_[(std::size_t)t.team];
                lt.members.erase(std::remove(lt.members.begin(), lt.members.end(), (int)i), lt.members.end());
                if (lt.members.empty()) {
                    const std::string& id = teams_data_.teams[(std::size_t)lt.type].id;
                    if (!team_left(id)) left_teams_.push_back(id);
                    ra2engine::RaiseContext lc;
                    if (sys_->raise_logic(23, lc, host_)) ++raises_;
                }
                t.team = -1;
            }
            if (!live(u)) {
                t.dead = true;
                if (std::getenv("RA2_CAMP_DEBUG"))
                    std::cerr << "[camp-debug] f" << frame_ << " unit " << i << " " << t.type << " house " << u.house
                              << " died at " << u.cell_x << "," << u.cell_y << " by house "
                              << gf_attacker_house((int)i, -1, u.house) << " killer=" << u.vet.killed_by
                              << (u.vet.killed_by >= 0 && u.vet.killed_by < (int)w_.units().size()
                                      ? " " + w_.units()[(std::size_t)u.vet.killed_by].image + "/h" +
                                            std::to_string(w_.units()[(std::size_t)u.vet.killed_by].house)
                                      : std::string())
                              << debug_defence_shooters((int)i) << "\n";
                if (u.house >= 0 && u.house < (int)destroyed_units_.size())
                    ++destroyed_units_[(std::size_t)u.house];
                // gF G-74 [FUN_00743550 / FUN_007021D0]: the original order is
                // 7 DestroyedByAnybody (killer present), 48, then 29.
                raise_obj(t, ra2engine::kEvDestroyedByAnybody, u.house);
                raise_obj(t, ra2engine::kEvDestroyedByAnything, u.house);
                raise_obj(t, ra2engine::kEvDestroyedFakesAll, u.house);
                if (t.tag >= 0) { sys_->detach_tag(t.tag); t.tag = -1; }
                continue;
            }
            if (u.cell_x != t.cx || u.cell_y != t.cy) {
                t.cx = u.cell_x;
                t.cy = u.cell_y;
                ra2engine::RaiseContext c = ctx_for(u.house);
                c.obj_x = t.cx;
                c.obj_y = t.cy;
                if (sys_->raise_cell(t.cx, t.cy, ra2engine::kEvEnteredBy, c, host_)) ++raises_;
                sys_->raise_cell(t.cx, t.cy, ra2engine::kEvEnteredOrOverflownBy, c, host_);
                gf_raise_lines(t.cx, t.cy, c);
                // W5-A 53/54 SpyAsHouse / SpyAsInfantry (Cell kinds): a
                // disguised unit entering a tagged cell [RE: FUN_0071DBE0 cases
                // 0x35/0x36 read the object's disguise house / type].
                if (u.disguised) {
                    ra2engine::RaiseContext dc = c;
                    dc.disguise_house_type = house_type(u.disguised_as_house);
                    dc.disguise_infantry = camp::up(u.disguised_as_type);
                    if (sys_->cell_waits_on(t.cx, t.cy, 53)) sys_->raise_cell(t.cx, t.cy, 53, dc, host_);
                    if (sys_->cell_waits_on(t.cx, t.cy, 54)) sys_->raise_cell(t.cx, t.cy, 54, dc, host_);
                }
                // W5-A 34 ComesNearWaypoint (an Object kind) on the unit's own tag
                // [RE-FALLBACK: the original's raise site is untraced; raised on
                // each cell change, only on a tag that waits for 34].
                if (t.tag >= 0 && sys_->tag_waits_on(t.tag, 34)) raise_obj_ctx(t, 34, c);
            }
            if (u.hp < t.hp) raise_attacked(t, u.house, gf_attacker_house((int)i, -1, u.house));
            if (u.hp < t.hp) raise_damage_stages(t, u.hp, u.max_hp, u.house);
            t.hp = u.hp;
            if (!t.discovered && u.house != 0 && w_.explored_at(0, u.cell_x, u.cell_y)) {
                t.discovered = true;
                raise_obj(t, ra2engine::kEvDiscoveredByPlayer, u.house);
            }
            // NET L1: the player's selection as the sim last saw it (the
            // SELECT_NOTIFY command log; GameScene feeds it, a bare runtime
            // has none).
            const bool usel = sel_ && sel_->has(static_cast<int>(i));
            if (usel && !t.selected) raise_obj(t, ra2engine::kEvSelectedByPlayer, u.house);
            t.selected = usel;
        }
        for (std::size_t i = 0; i < btrack_.size(); ++i) {
            Track& t = btrack_[i];
            if (t.dead) continue;
            const ra2engine::Building& b = w_.buildings()[i];
            if (!live(b)) {
                t.dead = true;
                if (b.house >= 0 && b.house < (int)destroyed_buildings_.size())
                    ++destroyed_buildings_[(std::size_t)b.house];
                // gF G-74 [FUN_00743550 / FUN_007021D0]: the original order is
                // 7 DestroyedByAnybody (killer present), 48, then 29.
                raise_obj(t, ra2engine::kEvDestroyedByAnybody, b.house);
                raise_obj(t, ra2engine::kEvDestroyedByAnything, b.house);
                raise_obj(t, ra2engine::kEvDestroyedFakesAll, b.house);
                if (t.tag >= 0) { sys_->detach_tag(t.tag); t.tag = -1; }
                continue;
            }
            if (b.hp < t.hp) raise_attacked(t, b.house, gf_attacker_house(-1, (int)i, b.house));
            if (b.hp < t.hp) raise_damage_stages(t, b.hp, b.max_hp, b.house);
            t.hp = b.hp;
            if (!t.discovered && b.house != 0 && w_.explored_at(0, b.cx, b.cy)) {
                t.discovered = true;
                raise_obj(t, ra2engine::kEvDiscoveredByPlayer, b.house);
            }
        }
    }

    // --- teams [RE §7, §9 item 3] ---------------------------------------------
    std::vector<int> taskforce_counts(const Team& tm, std::vector<std::string>& types) const {
        std::vector<int> need;
        types.clear();
        const ra2engine::MapTeamType& tt = teams_data_.teams[(std::size_t)tm.type];
        if (const auto* tf = teams_data_.taskforce(tt.taskforce))
            for (const ra2engine::MapTaskMember& m : *tf) {
                types.push_back(camp::up(m.type));
                need.push_back(m.count);
            }
        return need;
    }
    // W6-02-16: the TaskForce total the team must reach before its script
    // starts (types the clone cannot resolve are left out).
    int team_total(const Team& tm) {
        std::vector<std::string> types;
        const std::vector<int> need = taskforce_counts(tm, types);
        int total = 0;
        for (std::size_t k = 0; k < types.size(); ++k)
            if (types_.unit(types[k])) total += std::max(0, need[k]);
        return total;
    }
    bool recruit(Team& tm) {
        std::vector<std::string> types;
        std::vector<int> need = taskforce_counts(tm, types);
        for (int m : tm.members)
            for (std::size_t k = 0; k < types.size(); ++k)
                if (utrack_[(std::size_t)m].type == types[k] && need[k] > 0) { --need[k]; break; }
        bool any = false;
        // W6-02-17 [RE: Recruit FUN_006E9FF0 -- per TaskForce entry, ONE unit
        // per call, the candidate nearest the team's anchor]: the anchor is the
        // first live member, else the TeamType Waypoint=, else none (index
        // order). [RE-FALLBACK: the +0x3200 foreign-group penalty and the
        // RecruitableA/B (+0x421/+0x422) autocreate split are not modelled.]
        int ax = -1, ay = -1;
        for (int m : tm.members)
            if (live(w_.units()[(std::size_t)m])) { ax = w_.units()[(std::size_t)m].cell_x; ay = w_.units()[(std::size_t)m].cell_y; break; }
        if (ax < 0 && tm.type >= 0) {
            const int wp = teams_data_.teams[(std::size_t)tm.type].waypoint;
            if (wp >= 0 && !waypoint_cell(wp, ax, ay)) ax = ay = -1;
        }
        std::vector<int> best(types.size(), -1);
        std::vector<long> best_d(types.size(), 0);
        for (std::size_t i = 0; i < utrack_.size(); ++i) {
            Track& t = utrack_[i];
            if (t.dead) continue;
            // W2-CA-09 [ai-team.md §2]: a unit is free when it has no team, its
            // team is finished, or its TeamType says AreTeamMembersRecruitable.
            if (t.team >= 0 && !member_recruitable(t.team, &tm)) continue;
            const ra2engine::Unit& u = w_.units()[i];
            if (!live(u) || u.house != tm.house || u.cargo_of >= 0) continue;   // W4-SQ-01: on the map only
            // W5-A [RE: Can_Add FUN_006E9B80 returns `techno+0x2E4 == 0`, read as
            // the carried-passenger count -- PROBABLE]: a loaded transport is
            // never recruited (SOV02's MCV boat keeps its MCV).
            if (!u.cargo.empty()) continue;
            // W6-02-17 [RE: Can_Add FUN_006E9B80 / the mission test FUN_005B3110]:
            // a Sleeping / Harmless / Hunting / Harvesting / Capturing unit is
            // not recruitable, nor a mind-controlled one.
            if (t.mission != 0 || t.hunt || t.mc_orig >= 0) continue;
            if (u.is_harvester || u.capture_target >= 0 || u.infiltrate_target >= 0) continue;
            for (std::size_t k = 0; k < types.size(); ++k)
                if (need[k] > 0 && t.type == types[k]) {
                    const long d = ax < 0 ? (long)i
                                          : (long)(u.cell_x - ax) * (u.cell_x - ax) + (long)(u.cell_y - ay) * (u.cell_y - ay);
                    if (best[k] < 0 || d < best_d[k]) { best[k] = (int)i; best_d[k] = d; }
                    break;
                }
        }
        for (std::size_t k = 0; k < types.size(); ++k) {
            const int i = best[k];
            if (i < 0) continue;
            Track& t = utrack_[(std::size_t)i];
            if (t.team >= 0) {   // leave the old team
                std::vector<int>& om = teams_[(std::size_t)t.team].members;
                om.erase(std::remove(om.begin(), om.end(), i), om.end());
                t.team = -1;
            }
            tm.members.push_back(i);
            any = true;
        }
        return any;
    }
    bool member_recruitable(int team_ix, const Team* self) const {
        if (team_ix < 0 || team_ix >= (int)teams_.size()) return true;
        const Team& o = teams_[(std::size_t)team_ix];
        if (&o == self) return false;
        if (o.st.done) return true;
        // W5-A [RE: TeamClass::Can_Add FUN_006E9B80 -- a member of a live team
        // may be taken only by a team whose TeamType Priority (+0xB4) is
        // strictly higher, and only if its own team's TeamType set
        // AreTeamMembersRecruitable (+0xF5, copied to techno +0x422 on Add,
        // FUN_006E9A70)]. SOV02: 'Transports Off' must not take the MCV boat
        // of the higher-priority 'Land the MCV' team.
        if (o.type < 0 || !teams_data_.teams[(std::size_t)o.type].recruitable) return false;
        if (!self || self->type < 0) return true;
        return teams_data_.teams[(std::size_t)o.type].priority < teams_data_.teams[(std::size_t)self->type].priority;
    }
    // W2-CA-09: a finished team lets its members go.
    void release_team(Team& tm, int team_idx) {
        for (int m : tm.members)
            if (utrack_[(std::size_t)m].team == team_idx) {
                utrack_[(std::size_t)m].team = -1;
                maybe_leave_map(m);
            }
        tm.members.clear();
    }
    // W4-SQ-03: a team-less fixed-wing aircraft of a computer house with no
    // live airfield of its own (ALL02's British intro Harriers, SOV06/ALL10
    // flyovers) flies off the map. Player-controlled aircraft are left alone.
    void maybe_leave_map(int m) {
        const ra2engine::Unit& u = w_.units()[(std::size_t)m];
        if (std::getenv("RA2_CAMP_DEBUG"))
            std::cerr << "[camp-debug] f" << frame_ << " leave? " << m << " " << u.image << " air=" << u.is_aircraft
                      << " fw=" << u.an.t.fixed_wing << " home=" << u.an.home << "\n";
        if (!live(u) || !u.is_aircraft || !u.an.t.fixed_wing || w_.human_commanded(u.house)) return;
        const int hb = u.an.home;
        if (hb >= 0 && hb < (int)w_.buildings().size() && live(w_.buildings()[(std::size_t)hb]) &&
            w_.buildings()[(std::size_t)hb].house == u.house)
            return;
        w_.order_leave_map(m);
    }
    void adopt(Team& tm, int team_idx) {
        // W6-02-14 [RE: the team ctor FUN_006E8000 builds the TagClass
        // (FUN_006E4350) from TeamType+0xD0; AddMember FUN_006E9A70 calls
        // ObjectClass::AttachTrigger FUN_005F5360 on EVERY member -- it
        // replaces the member's own tag (old count -1, new +1); a change of
        // owner keeps obj+0x34]. Recruited members carried no tag before: ALL05
        // Tanya (CreateTeam 'Tanya (2)', Tag=088361A0) died without 'Tanya
        // Dies'. [RE-FALLBACK: the original skips OnTransOnly non-transports;
        // OnTransOnly is not parsed. The clone's tags are shared by index, as
        // for reinforcements, not one instance per team.]
        int tg = -1;
        if (tm.type >= 0 && tm.type < (int)teams_data_.teams.size()) {
            const std::string& tag = teams_data_.teams[(std::size_t)tm.type].tag;
            if (!tag.empty() && camp::up(tag) != "<NONE>") tg = data_.find_tag(tag);
        }
        for (int m : tm.members) {
            Track& t = utrack_[(std::size_t)m];
            t.team = team_idx;
            if (tg >= 0 && sys_ && t.tag != tg) {
                if (t.tag >= 0) sys_->detach_tag(t.tag);
                t.tag = tg;
                sys_->attach_tag(tg);
            }
        }
    }
    // --- W4-SQ-01/06 reinforcement delivery [RE: FUN_0065d770] -------------
    struct Member { std::string type; bool transport = false; };
    bool rules_flag(const std::string& id, const char* key) const {
        const std::string v = camp::up(camp::trim_comment(rules_ini_.get(id, key, "no")));
        return !v.empty() && (v[0] == 'Y' || v[0] == 'T' || v[0] == '1');
    }
    // The TaskForce expanded to one entry per object, in creation order, with
    // the transport split of FUN_0065d770: with Full=yes, a member whose type
    // has Passengers>0 is a transport when it is a UnitClass (an AircraftClass
    // when the TaskForce holds an AircraftType with Passengers, FUN_006EE9D0)
    // and, if any TaskForce type is Naval (TechnoType +0xCCE), only a Naval one.
    std::vector<Member> team_members_of(const ra2engine::MapTeamType& tt) {
        std::vector<Member> ms;
        unresolved_ = 0;
        const auto* tf = teams_data_.taskforce(tt.taskforce);
        if (!tf) return ms;
        bool air_team = false, naval_team = false;
        for (const ra2engine::MapTaskMember& m : *tf) {
            const camp::UnitType* ut = types_.unit(m.type);
            if (!ut) continue;
            if (ut->combat.is_aircraft && ut->combat.passengers > 0) air_team = true;
            if (rules_flag(camp::up(m.type), "Naval")) naval_team = true;
        }
        for (const ra2engine::MapTaskMember& m : *tf) {
            const camp::UnitType* ut = types_.unit(m.type);
            if (!ut) { unresolved_ += std::max(0, m.count); continue; }
            const std::string id = camp::up(m.type);
            const bool unit_class = !ut->combat.is_aircraft && camp::index_of(ra2_vehicles_, id) >= 0;
            const bool klass = air_team ? ut->combat.is_aircraft : unit_class;
            const bool transport = tt.full && ut->combat.passengers > 0 && klass &&
                                   (!naval_team || rules_flag(id, "Naval"));
            for (int k = 0; k < m.count; ++k) ms.push_back({m.type, transport});
        }
        return ms;
    }
    // A member created inside `carrier` (FUN_004732d0): never on the map.
    int add_passenger(int carrier, const std::string& type, int house, const std::string& tag) {
        const camp::UnitType* ut = types_.unit(type);
        if (!ut) return -1;
        ra2engine::UnitCombat c = ut->combat;
        if (place_vet_ > 0) c.veteran_rank = place_vet_;
        const int idx = w_.add_unit_in_transport(carrier, house, c);
        if (idx < 0) return -1;
        placing_ = true;
        sync_tracks();
        placing_ = false;
        Track& t = utrack_[(std::size_t)idx];
        t.type = camp::up(type);
        t.initial = true;
        t.hp = w_.units()[(std::size_t)idx].hp;
        t.cx = t.cy = -1;   // its first on-map cell raises the cell events
        t.tag = tag.empty() ? -1 : data_.find_tag(tag);
        if (t.tag >= 0 && sys_) sys_->attach_tag(t.tag);
        return idx;
    }
    // W4-SQ-06 [RE: FUN_0065d320 Droppod branch, @0x65d5e6..0x65d74d]: the
    // house's paradrop plane (AircraftTypes "PDPLANE", the name string at
    // 0x838700) is created flying, at the TransportWaypoint when
    // UseTransportOrigin=yes, else on the house's Edge= (house +0x1E0; an
    // invalid edge -> FUN_0050D8A0), takes every created member aboard
    // (FUN_004732D0) and flies Mission ParadropApproach (0x1A) to the
    // reinforcement waypoint. [RE-FALLBACK: MapClass::PickCellOnEdge
    // FUN_004AA350 picks a random edge cell; the clone takes the edge cell
    // straight up/right/down/left of the drop point on screen, and the plane
    // leaves across the map on the same heading.] Returns the plane index, -1
    // when no plane type is registered (the members are then put down at the
    // waypoint, the old fallback).
    int deliver_droppod(Team& tm, const ra2engine::MapTeamType& tt, const std::vector<Member>& ms, int x,
                        int y, int& placed, int& carried) {
        const int W = w_.grid_w(), H = w_.grid_h();
        int sx = -1, sy = -1;
        if (tt.use_transport_origin && tt.transport_waypoint >= 0 && !waypoint_cell(tt.transport_waypoint, sx, sy))
            sx = sy = -1;
        // Screen directions in cell space: N (-1,-1), E (+1,-1), S (+1,+1), W (-1,+1).
        static const int kDir[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        if (sx < 0) {
            const int edge = edge_of_house(tm.house);
            int cx = x, cy = y;
            while (cx + kDir[edge][0] >= 0 && cx + kDir[edge][0] < W && cy + kDir[edge][1] >= 0 &&
                   cy + kDir[edge][1] < H) {
                cx += kDir[edge][0];
                cy += kDir[edge][1];
            }
            sx = cx;
            sy = cy;
        }
        // Exit: on across the map along the entry -> drop heading.
        double dx = x - sx, dy = y - sy;
        const double len = ra2engine::om::hypot(dx, dy);
        if (len < 0.5) { dx = 1.0; dy = 1.0; } else { dx /= len; dy /= len; }
        double ex = x, ey = y;
        for (int k = 0; k < 4 * (W + H); ++k) {
            const double nx = ex + dx, ny = ey + dy;
            if (nx < 0 || ny < 0 || nx > W - 1 || ny > H - 1) break;
            ex = nx;
            ey = ny;
        }
        const std::string team_tag =
            tt.tag.empty() || camp::up(tt.tag) == "<NONE>" ? std::string() : tt.tag;
        const int plane = w_.spawn_drop_plane("PDPLANE", tm.house, sx, sy, x, y, (int)ra2engine::om::lround(ex),
                                              (int)ra2engine::om::lround(ey));
        if (plane >= 0) {
            placing_ = true;
            sync_tracks();   // the plane is not a team member
            placing_ = false;
            utrack_[(std::size_t)plane].type = "PDPLANE";
        }
        for (const Member& m : ms) {
            const int idx = plane >= 0 ? add_passenger(plane, m.type, tm.house, team_tag)
                                       : place_unit(m.type, tm.house, x, y, 256, team_tag, "Guard", /*far=*/true);
            if (idx < 0) continue;
            tm.members.push_back(idx);
            (plane >= 0 ? carried : placed)++;
        }
        return plane;
    }
    // [Houses] Edge= (North/East/South/West) [RE: house +0x1E0; any other
    // value -> FUN_0050D8A0 -> 0].
    int edge_of_house(int eh) const {
        if (eh < 0 || eh >= (int)house_names_.size()) return 0;
        const std::string e = camp::up(camp::trim_comment(map_.get(house_names_[(std::size_t)eh], "Edge", "")));
        if (e == "EAST") return 1;
        if (e == "SOUTH") return 2;
        if (e == "WEST") return 3;
        return 0;
    }
    // CreateTeam (4) / Reinforcement (7/80/107).
    bool create_team(const std::string& id, bool reinforce, int waypoint) {
        const int ti = teams_data_.find_team(id);
        if (ti < 0) { note_ = "[team not found]"; return false; }
        const ra2engine::MapTeamType& tt = teams_data_.teams[(std::size_t)ti];
        Team tm;
        tm.type = ti;
        tm.script = teams_data_.find_script(tt.script);
        tm.house = engine_house_of_name(tt.house);
        tm.created = frame_;
        if (tm.house < 0) { note_ = "[no house]"; return false; }
        char b[160];
        if (reinforce) {
            // [RE: FUN_0065d320 Reinforcement -> FUN_0065d770 create + FUN_0065da50
            // place]: the members are created, then a Droppod team flies in by
            // plane (W4-SQ-06), a Full team rides inside its transport
            // (W4-SQ-01), and the rest are put down at the waypoint.
            const int wp = waypoint >= 0 ? waypoint : tt.waypoint;
            int x = 0, y = 0;
            if (!waypoint_cell(wp, x, y)) { note_ = "[no waypoint]"; return false; }
            int placed = 0, skipped = 0, carried = 0;
            // W2-CA-08: the team's first move-waypoint is the landing goal.
            int gx = -1, gy = -1;
            if (tm.script >= 0)
                for (const ra2engine::MapScriptAction& sa : teams_data_.scripts[(std::size_t)tm.script].actions)
                    if (sa.action == ra2engine::kSaMoveWaypoint) {
                        if (!waypoint_cell(sa.arg, gx, gy)) gx = gy = -1;
                        break;
                    }
            // TeamType Tag= rides on every member [map-triggers.md §1].
            const std::string team_tag =
                tt.tag.empty() || camp::up(tt.tag) == "<NONE>" ? std::string() : tt.tag;
            // W4-SQ-04 [RE: FUN_0065d770 -> FUN_0074ee80/eeb0/eed0]: VeteranLevel
            // 2 = veteran, 3 = elite (0 = the -0.25 "green" rookie, no stat effect).
            place_vet_ = tt.veteran_level >= 3 ? 2 : tt.veteran_level == 2 ? 1 : 0;
            const std::vector<Member> ms = team_members_of(tt);
            skipped += unresolved_;
            if (tt.droppod) {
                const int n = deliver_droppod(tm, tt, ms, x, y, placed, carried);
                place_vet_ = 0;
                char db[200];
                std::snprintf(db, sizeof db, "[house=%d wp=%d(%d,%d) droppod plane=%d placed=%d aboard=%d skipped=%d]",
                              tm.house, wp, x, y, n, placed, carried, skipped);
                note_ = db;
                if (placed + carried == 0) return false;
                tm.airdrop = n >= 0;
                teams_.push_back(tm);
                adopt(teams_.back(), (int)teams_.size() - 1);
                ++teams_created_;
                return true;
            }
            bool any_t = false, any_p = false;
            for (const Member& m : ms) (m.transport ? any_t : any_p) = true;
            const bool load = any_t && any_p;   // FUN_004732d0 into the transport list head
            // [RE: FUN_0065d320 @0x65d3fa..0x65d540: an all-infantry, non-Droppod
            // reinforcement whose waypoint cell or a neighbour holds a building
            // with a free cell around it (FUN_00459C90) is kicked out of that
            // building (vtbl +0x100 KickOutUnit), the misfits deleted; the clone's
            // placement around the waypoint stands in for the door exit.]
            int carrier = -1;
            for (const Member& m : ms) {
                if (load && !m.transport) continue;
                const int idx = place_unit(m.type, tm.house, x, y, 256, team_tag, "Guard",
                                           /*far=*/true, gx, gy);
                // W2-CA-08: a far landing becomes the team's anchor, so
                // the rest spread around it instead of lining the shore.
                if (idx >= 0) {
                    const ra2engine::Unit& pu = w_.units()[(std::size_t)idx];
                    const int ddx = pu.cell_x - x, ddy = pu.cell_y - y;
                    if (!pu.is_naval && std::max(std::abs(ddx), std::abs(ddy)) > 12) {
                        x = pu.cell_x;
                        y = pu.cell_y;
                    }
                    if (m.transport) carrier = idx;   // the LAST created transport is the list head
                }
                if (idx >= 0) { tm.members.push_back(idx); ++placed; }
                else ++skipped;
            }
            // W7-05-04 [RE: FUN_0065D770 @0x65D9C1..0x65D9E1]: the loaded
            // transport, when it is an aircraft, is a loaner (+0x3D4).
            if (load && carrier >= 0 && w_.units()[(std::size_t)carrier].is_aircraft) w_.set_loaner(carrier);
            if (load)
                for (const Member& m : ms) {
                    if (m.transport) continue;
                    const int idx = carrier >= 0 ? add_passenger(carrier, m.type, tm.house, team_tag)
                                                 : place_unit(m.type, tm.house, x, y, 256, team_tag, "Guard",
                                                              /*far=*/true, gx, gy);
                    if (idx >= 0) { tm.members.push_back(idx); (carrier >= 0 ? carried : placed)++; }
                    else ++skipped;
                }
            place_vet_ = 0;
            std::snprintf(b, sizeof b, "[house=%d wp=%d(%d,%d) placed=%d aboard=%d skipped=%d]", tm.house, wp,
                          x, y, placed, carried, skipped);
            if (placed == 0) { note_ = b; return false; }
        } else {
            recruit(tm);
            // W6-02-16: the team waits (recruits, regroups) until full strength.
            tm.recruiting = (int)tm.members.size() < team_total(tm);
            std::snprintf(b, sizeof b, "[house=%d recruited=%d]", tm.house, (int)tm.members.size());
        }
        note_ = b;
        teams_.push_back(tm);
        adopt(teams_.back(), (int)teams_.size() - 1);
        ++teams_created_;
        return true;
    }
    int nearest_enemy(int house, int x, int y, int quarry, bool& is_building) const {
        int best = -1;
        long bd = 0;
        is_building = false;
        const bool want_b = quarry == 2 || quarry == 6 || quarry == 7 || quarry == 9;
        const bool want_inf = quarry == 4, want_veh = quarry == 5;
        if (!want_b)
            for (std::size_t i = 0; i < w_.units().size(); ++i) {
                const ra2engine::Unit& u = w_.units()[i];
                if (!live(u) || !w_.is_enemy(house, u.house)) continue;
                // W6-02-07/08 [RE: Evaluate_Object FUN_006F71D0]: LegalTarget,
                // IsInPlayfield, campaign Insignificant (World::legal_threat_*).
                if (!w_.legal_threat_unit(house, (int)i)) continue;
                // A still-disguised spy passes for a friend (the auto-acquire
                // precedent, capture.md §Design 6) -- the W6-02-18 patrol scan
                // must not see through it.
                if (w_.disguise_fools(house, u)) continue;   // W7-01-02
                if (want_inf && u.is_vehicle) continue;
                if (want_veh && !u.is_vehicle) continue;
                const long d = (long)(u.cell_x - x) * (u.cell_x - x) + (long)(u.cell_y - y) * (u.cell_y - y);
                if (best < 0 || d < bd) { best = (int)i; bd = d; }
            }
        if (!want_inf && !want_veh)
            for (std::size_t i = 0; i < w_.buildings().size(); ++i) {
                const ra2engine::Building& b = w_.buildings()[i];
                if (!live(b) || !w_.is_enemy(house, b.house)) continue;
                if (!w_.legal_threat_building(house, (int)i)) continue;   // W6-02-07/08
                const long d = (long)(b.cx - x) * (b.cx - x) + (long)(b.cy - y) * (b.cy - y);
                if (best < 0 || d < bd || (!is_building && quarry != 1 && want_b)) {
                    best = (int)i;
                    bd = d;
                    is_building = true;
                }
            }
        return best;
    }
    void order_attack(int m, int target, bool building) {
        // W2-CA-16: a fixed-wing team member flies a strike sortie.
        if (w_.order_fixed_wing(m, building ? -1 : target, building ? target : -1, -1, -1)) return;
        if (building) w_.order_unit_attack_building(m, target);
        else w_.order_unit_attack(m, target);
    }
    // W4-SQ-12: what every computer house's recruiting scripted teams still
    // miss, handed to its AI production (World::set_ai_script_demand).
    void push_script_demand() {
        std::vector<std::map<std::string, int>> want(house_names_.size());
        for (const Team& tm : teams_) {
            if (tm.st.done || !tm.recruiting || tm.house <= 0 || tm.house >= (int)want.size()) continue;
            if (w_.human_commanded(tm.house) || !w_.ai_enabled(tm.house)) continue;
            std::vector<std::string> types;
            std::vector<int> need = taskforce_counts(tm, types);
            for (int m : tm.members)
                for (std::size_t k = 0; k < types.size(); ++k)
                    if (utrack_[(std::size_t)m].type == types[k] && need[k] > 0) { --need[k]; break; }
            for (std::size_t k = 0; k < types.size(); ++k)
                if (need[k] > 0) want[(std::size_t)tm.house][types[k]] += need[k];
        }
        for (std::size_t h = 1; h < want.size(); ++h) {
            if (!w_.ai_enabled((int)h)) continue;
            std::vector<std::string> ids;
            std::vector<int> counts;
            for (const auto& kv : want[h]) { ids.push_back(kv.first); counts.push_back(kv.second); }
            w_.set_ai_script_demand((int)h, std::move(ids), std::move(counts));
        }
    }
    // W4-SQ-01: a member inside a transport / drop plane / under its
    // parachute / in a garrison is not on the map: team coordination skips it
    // (the original tests ObjectClass +0x90 IsOnMap per member).
    bool on_map(int m) const { return w_.units()[(std::size_t)m].cargo_of < 0; }
    // Still travelling: a ground path, a helicopter/jumpjet flight goal, or a
    // fixed-wing outbound/move sortie (W2-AN kOutbound=1 / kMoveTo=2).
    static bool busy(const ra2engine::Unit& u) {
        if (u.moving()) return true;
        if (!u.is_aircraft) return false;
        return u.has_flight_goal || u.an.sortie == 1 || u.an.sortie == 2 ||
               u.flight_state == ra2engine::FlightState::TakingOff;
    }
    bool is_team_transport(const Team& tm, int m) const {
        const ra2engine::Unit& u = w_.units()[(std::size_t)m];
        if (u.passengers_max <= 0) return false;
        // [RE: FUN_006ee670] when the TaskForce holds an aircraft transport type,
        // only aircraft members count as the transports.
        bool air_team = false;
        for (int o : tm.members) {
            const ra2engine::Unit& v = w_.units()[(std::size_t)o];
            if (v.is_aircraft && v.passengers_max > 0) air_team = true;
        }
        return !air_team || u.is_aircraft;
    }
    void step_teams() {
        // W4-MC-06 [RE-FALLBACK, RE-W4-MC-2 open: the RA1/TS lineage removes a
        // unit the player gives an order from its TeamClass]: a player order
        // takes the unit out of its script team, so the script no longer
        // overrides the player (SOV09's Yuris, ALL01's Tanya).
        for (int idx : w_.take_player_orders()) {
            if (idx < 0 || idx >= (int)utrack_.size()) continue;
            Track& t = utrack_[(std::size_t)idx];
            t.hunt = false;
            if (t.team < 0 || t.team >= (int)teams_.size()) continue;
            std::vector<int>& mm = teams_[(std::size_t)t.team].members;
            mm.erase(std::remove(mm.begin(), mm.end(), idx), mm.end());
            t.team = -1;
        }
        for (std::size_t ti = 0; ti < teams_.size(); ++ti) {
            Team& tm = teams_[ti];
            if (tm.st.done) { if (!tm.members.empty()) release_team(tm, (int)ti); continue; }
            // Prune dead members.
            std::vector<int> live_m;
            for (int m : tm.members)
                if (live(w_.units()[(std::size_t)m]) && w_.units()[(std::size_t)m].house == tm.house)
                    live_m.push_back(m);
            tm.members = live_m;
            // W6-02-16 [RE: TeamClass::AI FUN_006E86B0 -- a team that has not
            // begun (+0x7F) starts its script only once IsFullStrength (+0x79,
            // FUN_006E9950: TotalObjects == the TaskForce total) or
            // IsForcedActive (+0x77, never set: Force_Active FUN_006E95F0 has no
            // callers); until then it recruits per TaskForce entry
            // (FUN_006E9FF0) and regroups (FUN_006EB030). In a campaign a team
            // that never filled is never dropped. A begun Reinforce=yes (+0xAB)
            // team keeps recruiting while under strength, and at count <=
            // total/3 goes back to regroup and refill (FUN_006E9640). Full= is
            // not read by team code.] Before, a team ran as soon as it had one
            // member and was dropped after 900 member-less frames.
            // [RE-FALLBACK: the regroup movement itself is not modelled -- a
            // waiting team's members hold their posts.]
            {
                const int total = team_total(tm);
                const int count = (int)tm.members.size();
                const bool reinforce = teams_data_.teams[(std::size_t)tm.type].reinforce;
                if (!tm.recruiting && reinforce && total > 0 && count <= total / 3) tm.recruiting = true;
                if (tm.recruiting || (reinforce && count < total)) {
                    if (recruit(tm)) { adopt(tm, (int)ti); tm.created = frame_; }
                    if ((int)tm.members.size() >= total) tm.recruiting = false;
                }
                if (tm.recruiting) continue;
            }
            // W4-SQ-06: the script starts once the paratroopers are down.
            if (tm.airdrop) {
                bool aloft = false;
                for (int m : tm.members) if (!on_map(m)) aloft = true;
                if (aloft) continue;
                tm.airdrop = false;
            }
            if (tm.script < 0) { tm.st.done = true; continue; }
            const ra2engine::MapScript& sc = teams_data_.scripts[(std::size_t)tm.script];
            ra2engine::MapTeamView v;
            v.live_members = (int)tm.members.size();
            bool all_idle = true, all_close = true;
            int lead = -1;
            // fv7e [RE: TeamClass coordinate-move FUN_006EB030, the regroup
            // callee of every move-family handler (3 FUN_006EBD30, 47
            // FUN_006ED950, 53 FUN_006EEC60, 54 FUN_006EEF70, 58 FUN_006EDB20)]:
            // a member farther than Stray from the team destination (+0x40)
            // whose NavCom (+0x5A4) is empty gets Mission Move + the team
            // destination again (vtbl+0x1E8(2), +0x480), and the line is not
            // complete while one is out. A member shoved off its route (a
            // vehicle pushed aside by CellClass::ScatterContent, UnitClass::
            // Scatter 0x742880 replaces its destination) therefore resumes the
            // team move; the clone took the stopped member as "arrived" (SOV02:
            // an SAPC pushed to (92,50) at sea held its unload forever).
            // [RE-FALLBACK: the clone's CloseEnough 2.25 arrival radius is
            // kept for the straggler test, not Stray 2.0; an unreachable goal
            // still ends on the all-idle fallback.]
            bool coord_move = false;
            if (tm.goal_x >= 0 && tm.st.issued && tm.st.pc >= 0 && tm.st.pc < (int)sc.actions.size()) {
                const int act = sc.actions[(std::size_t)tm.st.pc].action;
                coord_move = act == ra2engine::kSaMoveWaypoint || act == ra2engine::kSaMoveCell ||
                             act == ra2engine::kSaMoveEnemyBuilding || act == ra2engine::kSaGatherEnemy ||
                             act == ra2engine::kSaGatherBase || act == ra2engine::kSaMoveFriendlyBuilding;
            }
            for (int m : tm.members) {
                const ra2engine::Unit& u = w_.units()[(std::size_t)m];
                if (!on_map(m)) {
                    // W4-SQ-01 [RE: FUN_006ec760 / case 43]: who is still to board.
                    continue;
                }
                if (lead < 0) lead = m;
                if (tm.goal_x >= 0) {
                    const double dx = u.cell_x - tm.goal_x, dy = u.cell_y - tm.goal_y;
                    const bool out = dx * dx + dy * dy > 2.25 * 2.25;
                    if (out) all_close = false;
                    if (out && coord_move && frame_ - tm.issued_frame > 2 && !busy(u) && u.target < 0 &&
                        u.target_building < 0 && !w_.order_fixed_wing(m, -1, -1, tm.goal_x, tm.goal_y))
                        w_.order_unit_move(m, tm.goal_x, tm.goal_y);   // fv7e: FUN_006EB030 straggler
                }
                if (busy(u) || u.target >= 0 || u.target_building >= 0) all_idle = false;
            }
            // Transport bookkeeping for Unload (8) / Load (14) / WaitLoaded (43).
            for (int m : tm.members) {
                const ra2engine::Unit& u = w_.units()[(std::size_t)m];
                if (u.passengers_max > 0 && is_team_transport(tm, m)) {
                    if (!u.cargo.empty()) v.unloaded = false;
                    if ((int)u.cargo.size() < u.passengers_max) v.full = false;
                } else if (on_map(m)) {
                    v.loaded = false;   // a non-transport member still on the map
                }
            }
            v.arrived = all_close || (all_idle && frame_ - tm.issued_frame > 2);
            v.members_idle = false;
            v.target_exists = true;
            if (lead >= 0 && tm.st.pc >= 0 && tm.st.pc < (int)sc.actions.size()) {
                const ra2engine::MapScriptAction& a = sc.actions[(std::size_t)tm.st.pc];
                const ra2engine::Unit& lu = w_.units()[(std::size_t)lead];
                if (a.action == ra2engine::kSaAttack) {
                    bool isb = false;
                    v.target_exists = nearest_enemy(tm.house, lu.cell_x, lu.cell_y, a.arg, isb) >= 0;
                } else if (a.action == ra2engine::kSaAttackEnemyBuilding) {
                    v.target_exists = find_team_building(tm, a.arg, lu.cell_x, lu.cell_y) >= 0;
                } else if (a.action == ra2engine::kSaAttackWaypoint) {
                    int x = 0, y = 0;
                    v.target_exists = waypoint_cell(a.arg, x, y) &&
                                      (w_.building_at_cell(x, y) >= 0 || w_.unit_at_cell(x, y) >= 0);
                    v.members_idle = all_idle && frame_ - tm.issued_frame > 2;
                }
            }
            // W4-SQ-02: a garrison team whose infantry all went inside has
            // done its building order (the building is now theirs).
            if (lead < 0 && !tm.members.empty()) {
                bool garrisoned = true;
                for (int m : tm.members)
                    if (w_.units()[(std::size_t)m].cargo_of != ra2engine::kGarrisonCargo) garrisoned = false;
                if (garrisoned) { v.target_exists = false; v.arrived = true; }
            }
            if (tm.st.pc >= 0 && tm.st.pc < (int)sc.actions.size() &&
                sc.actions[(std::size_t)tm.st.pc].action == ra2engine::kSaIronCurtainMe)
                v.iron_state = iron_curtain_state(tm.house);
            const ra2engine::MapTeamCommand c = ra2engine::step_map_team(tm.st, sc, v, frame_);
            execute_team(tm, (int)ti, c, all_idle, lead);
        }
    }
    // W4-SQ-02 [RE: FUN_006EE130, called by script 46 FUN_006ED870 / 47
    // FUN_006ED950 / 58]: arg low 16 bits = the BuildingTypes index, arg >> 16
    // the selector (0 least threat, 1 most threat, 2 nearest, 3 farthest).
    // Candidates: live buildings of that type owned by a house the team's
    // house is not allied with, or by a passive (neutral) house; for 58 the
    // team's own / allied ones. [RE-FALLBACK: the threat selectors 0/1 rank by
    // the type's Strength x Cost threat value, not modelled -- 1 takes the
    // strongest, 0 the weakest.]
    int find_team_building(const Team& tm, int arg, int x, int y, bool friendly = false) const {
        const std::string want = building_type_id(arg & 0xFFFF);
        if (want.empty()) return -1;
        const int sel = (arg >> 16) & 0xFFFF;
        int best = -1;
        long key = 0;
        for (std::size_t i = 0; i < w_.buildings().size() && i < btrack_.size(); ++i) {
            const ra2engine::Building& b = w_.buildings()[i];
            if (!live(b) || btrack_[i].type != want) continue;
            const bool own_side = w_.are_allied(tm.house, b.house);
            const bool passive = !w_.house_combatant(b.house) || w_.passive_house(b.house);
            if (friendly ? !own_side : (own_side && !passive)) continue;
            const long d = (long)(b.cx - x) * (b.cx - x) + (long)(b.cy - y) * (b.cy - y);
            long k = sel == 3 ? -d : sel == 1 ? -(long)b.hp : sel == 0 ? (long)b.hp : d;
            if (best < 0 || k < key) { best = (int)i; key = k; }
        }
        return best;
    }
    // W5-A 55 IronCurtainMe [RE: FUN_006EF1D0]: 1 = the house's Iron Curtain is
    // charged and the house has full power (fire), 2 = still charging but past
    // [General] AIMinorSuperReadyPercent (the line waits), 0 = otherwise (the
    // line completes without firing).
    int iron_curtain_state(int house) const {
        const ra2engine::Superweapon* sw = w_.sw_state(house, ra2engine::SuperweaponType::IronCurtain);
        if (!sw) return 0;
        if (sw->is_ready()) return w_.house_low_power(house) ? 0 : 1;
        const double pct = std::atof(camp::trim_comment(rules_ini_.get("General", "AIMinorSuperReadyPercent", ".7")).c_str());
        return sw->charge_fraction() >= pct ? 2 : 0;
    }
    // A 16-bit DirStruct facing on a unit: vehicles keep it as body (and
    // turret) facing, infantry get the matching 8-block index.
    void set_facing_u16(int idx, double f16) {
        ra2engine::Unit& u = w_.units()[(std::size_t)idx];
        if (u.is_vehicle || u.is_aircraft || u.is_naval) {
            u.body_facing = f16;
            u.turret_facing = f16;
            return;
        }
        constexpr double kPi = 3.14159265358979323846;
        const double th = f16 * (2.0 * kPi / 65536.0);
        const int b = ra2engine::facing_from_delta(ra2engine::om::sin(th), -ra2engine::om::cos(th));   // inverse of facing_u16_from_delta
        if (b >= 0) u.facing = b;
    }
    // W6-02-18 [RE: TMission_Patrol FUN_006EC240 -- every few frames the team
    // scans for a threat from its strongest member, retargets (FUN_006E85C0)
    // and attacks (FUN_006EA9F0); otherwise it regroups and moves on]: script
    // 16 Patrol is an attack-move, not a plain move (supersedes the R-027
    // "patrol = move" fallback). [RE-FALLBACK: the lead member stands in for
    // the strongest one; the scan radius is its weapon range + 1 cell and runs
    // every 15 frames.] While a member fights, the team is kept from
    // "arriving"; once the fight is over the members resume the move.
    void patrol_tick(Team& tm, int lead) {
        bool fighting = false, moving = false, all_close = true;
        for (int m : tm.members) {
            if (!on_map(m)) continue;
            const ra2engine::Unit& u = w_.units()[(std::size_t)m];
            if (u.target >= 0 || u.target_building >= 0) fighting = true;
            if (busy(u)) moving = true;
            if (tm.goal_x >= 0) {
                const double dx = u.cell_x - tm.goal_x, dy = u.cell_y - tm.goal_y;
                if (dx * dx + dy * dy > 2.25 * 2.25) all_close = false;
            }
        }
        if (fighting) { tm.issued_frame = frame_; return; }
        if (frame_ % 15 == 0) {
            const ra2engine::Unit& lu = w_.units()[(std::size_t)lead];
            const long r = (long)std::floor(lu.w_range) + 1;
            bool isb = false;
            const int t = nearest_enemy(tm.house, lu.cell_x, lu.cell_y, 1, isb);
            if (t >= 0) {
                const int tx = isb ? w_.buildings()[(std::size_t)t].cx : w_.units()[(std::size_t)t].cell_x;
                const int ty = isb ? w_.buildings()[(std::size_t)t].cy : w_.units()[(std::size_t)t].cell_y;
                if ((long)(tx - lu.cell_x) * (tx - lu.cell_x) + (long)(ty - lu.cell_y) * (ty - lu.cell_y) <= r * r) {
                    for (int m : tm.members)
                        if (on_map(m)) order_attack(m, t, isb);
                    tm.issued_frame = frame_;
                    return;
                }
            }
        }
        if (!moving && !all_close && tm.goal_x >= 0) {   // the fight is over: move on
            for (int m : tm.members)
                if (on_map(m) && !w_.order_fixed_wing(m, -1, -1, tm.goal_x, tm.goal_y))
                    w_.order_unit_move(m, tm.goal_x, tm.goal_y);
            tm.issued_frame = frame_;
        }
    }
    void execute_team(Team& tm, int team_idx, const ra2engine::MapTeamCommand& c, bool all_idle, int lead) {
        using K = ra2engine::MapTeamCommand::Kind;
        switch (c.kind) {
        case K::MoveWaypoint:
        case K::MoveCell: {
            if (c.action == ra2engine::kSaPatrol && !c.fresh && lead >= 0) { patrol_tick(tm, lead); return; }
            if (!c.fresh || lead < 0) return;
            int x = -1, y = -1;
            const ra2engine::Unit& lu = w_.units()[(std::size_t)lead];
            if (c.kind == K::MoveCell) { x = c.arg % 128; y = c.arg / 128; }
            else if (c.action == ra2engine::kSaMoveEnemyBuilding ||
                     c.action == ra2engine::kSaMoveFriendlyBuilding) {
                const int t = find_team_building(tm, c.arg, lu.cell_x, lu.cell_y,
                                                 c.action == ra2engine::kSaMoveFriendlyBuilding);
                if (t >= 0) { x = w_.buildings()[(std::size_t)t].cx; y = w_.buildings()[(std::size_t)t].cy; }
            } else if (c.action == ra2engine::kSaGatherEnemy) {
                bool isb = false;
                const int t = nearest_enemy(tm.house, lu.cell_x, lu.cell_y, 2, isb);
                if (t >= 0 && isb) { x = w_.buildings()[(std::size_t)t].cx; y = w_.buildings()[(std::size_t)t].cy; }
            } else if (c.action == ra2engine::kSaGatherBase) {
                x = lu.cell_x;
                y = lu.cell_y;
            } else if (!waypoint_cell(c.arg, x, y)) {
                x = -1;
            }
            tm.goal_x = x;
            tm.goal_y = y;
            tm.issued_frame = frame_;
            if (x < 0) return;
            for (int m : tm.members)
                if (on_map(m) && !w_.order_fixed_wing(m, -1, -1, x, y)) w_.order_unit_move(m, x, y);
            return;
        }
        case K::Attack:
        case K::AttackBuilding:
        case K::AttackWaypoint: {
            if (!c.fresh && !all_idle) return;
            tm.issued_frame = frame_;
            tm.goal_x = -1;
            for (int m : tm.members) {
                const ra2engine::Unit& u = w_.units()[(std::size_t)m];
                if (!on_map(m)) continue;
                if (!c.fresh && (u.target >= 0 || u.target_building >= 0 || u.moving())) continue;
                // A mind controller holding its victim is done with the team's
                // quarry [RE-FALLBACK: TMission_Attack FUN_006EC5F0 keeps ONE team
                // target (+0x3C); re-arming the controller every idle tick made
                // it trade its victim for the next one each frame (SOV09)].
                if (!c.fresh && utrack_[(std::size_t)m].mc_victim >= 0) continue;
                bool isb = false;
                int t = -1;
                if (c.kind == K::AttackWaypoint) {
                    // Every attack-waypoint executes literally -- including
                    // Allied 1's Dreadnought shelling of the Statue (W4 fix:
                    // the old W2-CA-11 hold is lifted). It was only "needed"
                    // because every rocket exploded with the V3 warhead
                    // (150000 damage per hit); with the per-type DMISLWH the
                    // original's 6 damage per missile lands, and the Liberty
                    // Island Patriots shoot the missiles down
                    // (docs/gap/wave4/03-all01-opening.md §0).
                    int wx = 0, wy = 0;
                    if (!waypoint_cell(c.arg, wx, wy)) continue;
                    const int tb = w_.building_at_cell(wx, wy);
                    const int tu = tb < 0 ? w_.unit_at_cell(wx, wy) : -1;
                    if (tb >= 0) { t = tb; isb = true; }
                    else if (tu >= 0) { t = tu; isb = false; }
                } else if (c.kind == K::AttackBuilding) {
                    t = find_team_building(tm, c.arg, u.cell_x, u.cell_y);
                    isb = true;
                    if (std::getenv("RA2_CAMP_DEBUG") && c.fresh)
                        std::cerr << "[camp-debug] f" << frame_ << " team 46 arg " << c.arg << " type "
                                  << building_type_id(c.arg & 0xFFFF) << " -> b" << t << " occ="
                                  << (t >= 0 ? (int)w_.can_occupy(m, t) : -1) << "\n";
                    // fv7d [RE: InfantryClass::Mission_Attack 0x51F0F0]: a
                    // computer-owned Infiltrate infantry whose TarCom is a
                    // building gets Assign_Destination(TarCom) + Mission
                    // Capture (8) and walks in; Mission_Enter 0x519449 then
                    // repairs a BridgeRepairHut's bridge or captures. The
                    // INI loader sets Infiltrate (+0xEBE) for every
                    // Engineer= type (0x5243B9). Without it TRN02's
                    // RedTeamRepairBridge engineer stood idle at its
                    // waypoint and the bridge stayed down. [RE-FALLBACK:
                    // the Agent (spy) half of the same branch is not
                    // routed here.]
                    if (t >= 0 && w_.unit_is_engineer(u) && !w_.human_commanded(u.house)) {
                        if (u.capture_target != t) w_.order_engineer_capture(m, t);
                        continue;
                    }
                    // An Occupier "attacking" an occupiable building it may
                    // enter goes inside (FUN_00457CE0 CanBeOccupiedBy -> the
                    // infantry's Enter path), the ALL03/04 garrison teams.
                    if (t >= 0 && w_.can_occupy(m, t)) {
                        w_.order_occupy(m, t);
                        continue;
                    }
                } else {
                    t = nearest_enemy(tm.house, u.cell_x, u.cell_y, c.arg, isb);
                }
                if (t >= 0) order_attack(m, t, isb);
            }
            return;
        }
        case K::Mission:
            if (c.arg == ra2engine::kMissionHunt)
                for (int m : tm.members) utrack_[(std::size_t)m].hunt = true;
            return;
        case K::Win: flag_end(1); return;
        case K::Lose: flag_end(2); return;
        case K::SetGlobal: sys_->set_global(c.arg, true); return;
        case K::ClearGlobal: sys_->set_global(c.arg, false); return;
        case K::SetLocal: sys_->set_local(c.arg, true); return;
        case K::ClearLocal: sys_->set_local(c.arg, false); return;
        case K::ChangeHouse: {
            const int eh = engine_house(c.arg);
            if (eh < 0) return;
            for (int m : tm.members) w_.set_unit_owner(m, eh);   // SetOwningHouse FUN_00700940
            tm.st.done = true;
            return;
        }
        // W2-CA-17: Deploy (9) unfolds every member MCV (enemy MCVs too);
        // Unload (8) empties every member transport; Scatter (21) steps each
        // member to a nearby free cell [campaign.md §7].
        case K::Deploy:
            for (int m : tm.members)
                if (w_.is_mcv(m)) w_.order_deploy_mcv(m);
            return;
        // W4-SQ-01 [RE: FUN_006ee670]: every member transport unloads; the line
        // completes once they are all empty (step_map_team), and then the arg
        // mode decides who stays in the team: 0 everyone, 1 the transports
        // (the unloaded units leave), 2 the unloaded units (the transports
        // leave), 3 nobody. TransportsReturnOnUnload=yes sends the transports
        // home whatever the mode.
        case K::Unload:
            for (int m : tm.members)
                if (on_map(m) && is_team_transport(tm, m)) w_.order_unload(m);
            return;
        case K::UnloadDone: {
            const ra2engine::MapTeamType& tt = teams_data_.teams[(std::size_t)tm.type];
            std::vector<int> keep;
            for (int m : tm.members) {
                const bool tr = w_.units()[(std::size_t)m].passengers_max > 0 && is_team_transport(tm, m);
                bool leave = tr ? (tt.transports_return || c.arg == 2 || c.arg == 3) : (c.arg == 1 || c.arg == 3);
                if (leave) {
                    utrack_[(std::size_t)m].team = -1;
                    if (tr && tt.transports_return) send_home(m);
                } else {
                    keep.push_back(m);
                }
            }
            tm.members = keep;
            char b[120];
            std::snprintf(b, sizeof b, "[camp] f%ld team %s unloaded mode=%d members=%d", frame_,
                          tt.name.c_str(), c.arg, (int)keep.size());
            witness_.emplace_back(b);
            return;
        }
        // W4-SQ-01 [RE: FUN_006ec760]: the first member transport that is not
        // full; every other member on the map walks over and boards it.
        case K::Load: {
            int tr = -1;
            for (int m : tm.members) {
                const ra2engine::Unit& u = w_.units()[(std::size_t)m];
                if (on_map(m) && is_team_transport(tm, m) && (int)u.cargo.size() < u.passengers_max) { tr = m; break; }
            }
            if (tr < 0) return;
            for (int m : tm.members) {
                if (m == tr || !on_map(m) || is_team_transport(tm, m)) continue;
                const ra2engine::Unit& u = w_.units()[(std::size_t)m];
                if (u.enter_target == tr) continue;   // already on its way
                if (!w_.order_board(m, tr)) {
                    // Cannot board (size / full): it stays out and stops
                    // blocking the line [RE-FALLBACK: the original would keep
                    // re-issuing the Enter mission].
                    utrack_[(std::size_t)m].team = -1;
                    tm.members.erase(std::remove(tm.members.begin(), tm.members.end(), m), tm.members.end());
                    return;
                }
            }
            return;
        }
        case K::Scatter:
            for (int m : tm.members) {
                const ra2engine::Unit& u = w_.units()[(std::size_t)m];
                if (!on_map(m)) continue;
                int x = 0, y = 0;
                if (u.moving() || !free_cell_near(u.cell_x + 1, u.cell_y + 1, x, y, 2)) continue;
                w_.order_unit_move(m, x, y);
            }
            return;
        case K::DeleteMembers:
            for (int m : tm.members) w_.campaign_erase_unit(m);
            tm.members.clear();
            return;
        // W5-A R-027 [RE: FUN_006ED200]: every member's body facing becomes
        // (arg & 7) << 13 (the 8 compass points of the 16-bit DirStruct).
        case K::ForceFacing:
            for (int m : tm.members)
                if (on_map(m)) set_facing_u16(m, (double)((c.arg & 7) << 13));
            return;
        // [RE: FUN_006EF1D0]: the team house's charged Iron Curtain is fired at
        // the team (the leader's cell) [RE-FALLBACK: the original's leader is
        // the member with the highest Leadership-like +0x5FC rating; the clone
        // uses the first member on the map].
        case K::IronCurtain:
            if (lead >= 0 && iron_curtain_state(tm.house) == 1) {
                const ra2engine::Unit& lu = w_.units()[(std::size_t)lead];
                const bool ok = w_.fire_superweapon_at(tm.house, ra2engine::SuperweaponType::IronCurtain,
                                                       lu.cell_x, lu.cell_y);
                char b[120];
                std::snprintf(b, sizeof b, "[camp] f%ld team %s IronCurtainMe at %d,%d -> %s", frame_,
                              teams_data_.teams[(std::size_t)tm.type].name.c_str(), lu.cell_x, lu.cell_y,
                              ok ? "fired" : "refused");
                witness_.emplace_back(b);
            }
            return;
        // 19 Panic / 41 Unpanic [RE: per member vtbl +0x518 / +0x51C,
        // InfantryClass only; RE-FALLBACK: Panic = the fear ceiling (the walk
        // turns into the Panic run), Unpanic = fear 0].
        case K::Panic:
        case K::Unpanic:
            for (int m : tm.members) {
                ra2engine::Unit& u = w_.units()[(std::size_t)m];
                if (!on_map(m) || u.is_vehicle || u.is_aircraft || u.is_naval || u.ia.fearless) continue;
                u.ia.fear = c.kind == K::Panic ? 300 : 0;
            }
            return;
        // 10 Follow [RE: FUN_006ED0B0, the line never completes; RE-FALLBACK:
        // the followed object is the nearest live unit of the team's own side
        // outside the team, re-targeted every 2 s].
        case K::Follow: {
            if (lead < 0 || (!c.fresh && frame_ - tm.issued_frame < 30)) return;
            tm.issued_frame = frame_;
            const ra2engine::Unit& lu = w_.units()[(std::size_t)lead];
            int best = -1;
            long bd = 0;
            for (std::size_t i = 0; i < w_.units().size(); ++i) {
                const ra2engine::Unit& o = w_.units()[i];
                if (!live(o) || o.cargo_of >= 0 || !w_.are_allied(tm.house, o.house)) continue;
                if (i < utrack_.size() && utrack_[i].team == team_idx) continue;
                const long d = (long)(o.cell_x - lu.cell_x) * (o.cell_x - lu.cell_x) +
                               (long)(o.cell_y - lu.cell_y) * (o.cell_y - lu.cell_y);
                if (best < 0 || d < bd) { best = (int)i; bd = d; }
            }
            if (best < 0 || bd <= 4) return;
            const ra2engine::Unit& t = w_.units()[(std::size_t)best];
            for (int m : tm.members)
                if (on_map(m) && !w_.units()[(std::size_t)m].moving()) w_.order_unit_move(m, t.cell_x, t.cell_y);
            return;
        }
        // 50 Flash [RE: every member's flash counter +0xF0 = arg]: the
        // FlashTeam presentation (action 104) on this team.
        case K::Flash: {
            ra2engine::VisualRequest vr;
            vr.action = 104;
            vr.value = c.arg;
            vr.team = teams_data_.teams[(std::size_t)tm.type].id;
            host_.present(vr);
            return;
        }
        // 18 ChangeTeam [RE: handler @0x6E8C2D: new TeamClass(TeamTypes[arg],
        // house) (FUN_006E8000) and every member moves into it].
        case K::ChangeTeam: {
            if (c.arg < 0 || c.arg >= (int)teams_data_.teams.size()) return;
            const ra2engine::MapTeamType& nt = teams_data_.teams[(std::size_t)c.arg];
            Team t2;
            t2.type = c.arg;
            t2.script = teams_data_.find_script(nt.script);
            t2.house = tm.house;
            t2.created = frame_;
            t2.members = tm.members;
            tm.members.clear();
            tm.st.done = true;
            teams_.push_back(t2);
            adopt(teams_.back(), (int)teams_.size() - 1);
            ++teams_created_;
            return;
        }
        default:
            return;
        }
        (void)team_idx;
    }
    // [RE-FALLBACK: TransportsReturnOnUnload sends the transport to its
    // ArchiveTarget (+0x218), not traced for a reinforcement] -- the nearest
    // building of its own house, else it stays.
    void send_home(int m) {
        const ra2engine::Unit& u = w_.units()[(std::size_t)m];
        int best = -1;
        long bd = 0;
        for (std::size_t i = 0; i < w_.buildings().size(); ++i) {
            const ra2engine::Building& b = w_.buildings()[i];
            if (!live(b) || b.house != u.house) continue;
            const long d = (long)(b.cx - u.cell_x) * (b.cx - u.cell_x) + (long)(b.cy - u.cell_y) * (b.cy - u.cell_y);
            if (best < 0 || d < bd) { best = (int)i; bd = d; }
        }
        if (best < 0) return;
        int x = 0, y = 0;
        const ra2engine::Building& b = w_.buildings()[(std::size_t)best];
        if (u.is_naval) { if (!free_water_near(b.cx, b.cy, x, y, 8)) return; }
        else if (!free_cell_near(b.cx, b.cy, x, y, 8)) return;
        w_.order_unit_move(m, x, y);
    }
    void step_hunters() {
        if (frame_ % 15 != 0) return;
        for (std::size_t i = 0; i < utrack_.size(); ++i) {
            const Track& t = utrack_[i];
            if (!t.hunt || t.dead) continue;
            const ra2engine::Unit& u = w_.units()[i];
            if (!live(u) || u.moving() || u.target >= 0 || u.target_building >= 0) continue;
            // W7-01-08 [RE: InfantryClass Mission_Hunt FUN_0051F250]: a deployed
            // computer infantry hunting with no target plays Undeploy first.
            if (u.deployed && (u.deploy_fire || u.abil.deployer) && !w_.human_commanded(u.house))
                w_.order_deploy((int)i);
            bool isb = false;
            const int tg = nearest_enemy(u.house, u.cell_x, u.cell_y, 1, isb);
            if (tg >= 0) order_attack((int)i, tg, isb);
        }
    }

    // --- outcome [RE §5] ------------------------------------------------------
    void flag_end(int o) {
        if (end_.outcome() != 0) return;
        end_.flag(o, frame_);
        char b[120];
        std::snprintf(b, sizeof b, "[camp] f=%ld outcome=%s borrowed=%d", frame_,
                      o == 1 ? "WON" : "LOST", end_.borrowed_frames());
        witness_.emplace_back(b);
    }

    // Trigger log -> witness; TextTrigger labels -> objectives; W2-CA-12
    // speech/sound/theme actions -> audio requests for the scene.
    void drain_log() {
        const std::vector<std::string>& lg = sys_->log();
        for (; logged_ < lg.size(); ++logged_) {
            const std::string& l = lg[logged_];
            witness_.push_back(l);
            {
                int k = 0, v = 0, wp = -1;
                char name[128] = {0};
                if (std::sscanf(l.c_str(), "  a%d %*s v=%d wp=%d %127s", &k, &v, &wp, name) == 4 &&
                    (k == 19 || k == 20 || k == 21 || k == 99 || k == 10 || k == 100 || k == 116) &&   // W5-E movies
                    l.find("(recorded)") != std::string::npos)
                    audio_.push_back({k, name[0] == '-' && name[1] == 0 ? std::string() : std::string(name), v, wp});
            }
            {   // W3-B: action 113 MakeHouseCheer -> that house's infantry cheer.
                int k = 0, v = 0;
                if (std::sscanf(l.c_str(), "  a%d %*s v=%d", &k, &v) == 2 && k == 113 &&
                    l.find("(recorded)") != std::string::npos) {
                    const int eh = engine_house(v);
                    if (eh >= 0) w_.make_house_cheer(eh);
                }
            }
            if (l.rfind("  a11 ", 0) == 0) {
                // "  a11 TextTrigger v=.. wp=.. LABEL (recorded)"
                const std::size_t e = l.rfind(" (recorded)");
                const std::size_t s = l.rfind(' ', e == std::string::npos ? std::string::npos : e - 1);
                if (e != std::string::npos && s != std::string::npos && s + 1 < e)
                    objectives_.push_back({frame_, camp::csf_text(dir_, l.substr(s + 1, e - s - 1))});
            }
        }
    }

    // --- the TriggerHost over this runtime ------------------------------------
    struct Host : ra2engine::TriggerHost {
        explicit Host(CampaignRuntime& r) : rt(r) {}
        CampaignRuntime& rt;
        bool house_exists(int ht) const override { return ht >= 0 && rt.engine_house(ht) >= 0; }
        std::vector<int> house_instances() const override { return rt.houses_order_ht_; }
        int player_house_type() const override { return rt.house_type(0); }
        // P3 + P4 [RE: HouseClass RegisterGain FUN_004FF520 @0x4FF53B..0x4FF55B]:
        // the counters skip Insignificant= (+0x232) / DontScore= (+0xC9F) types
        // (CampaignRuntime::tracked).
        int house_buildings(int ht) const override {
            const int eh = rt.engine_house(ht);
            int n = 0;
            for (const ra2engine::Building& b : rt.w_.buildings()) if (rt.tracked(b) && b.house == eh) ++n;
            return n;
        }
        // P5 [RE: FUN_0071dbe0 case 9/0xB]: the DestroyedUnits/DestroyedAll
        // counts are the house's OwnedUnitTypes (+0x5564) and OwnedInfantryTypes
        // (+0x5578) totals -- the RULES lists, not the locomotor. A flying
        // [VehicleTypes] object (the Kirov ZEP) is a UnitClass and counts as a
        // vehicle, a jumpjet [InfantryTypes] one (the Rocketeer) as infantry;
        // only [AircraftTypes] objects stay out. (SOV12: a player whose base has
        // fallen but whose Kirovs still fly has not lost.)
        // 0 = not counted (aircraft), 1 = vehicle, 2 = infantry.
        int flying_class(std::size_t i) const {
            const ra2engine::Unit& u = rt.w_.units()[i];
            const std::string& t = i < rt.utrack_.size() ? rt.utrack_[i].type : u.image;
            if (camp::index_of(rt.ra2_vehicles_, t) >= 0 || camp::index_of(rt.ra2_vehicles_, camp::up(u.image)) >= 0) return 1;
            if (camp::index_of(rt.ra2_infantry_, t) >= 0 || camp::index_of(rt.ra2_infantry_, camp::up(u.image)) >= 0) return 2;
            return 0;
        }
        int house_vehicles(int ht) const override {
            const int eh = rt.engine_house(ht);
            int n = 0;
            for (std::size_t i = 0; i < rt.w_.units().size(); ++i) {
                const ra2engine::Unit& u = rt.w_.units()[i];
                if (!rt.tracked(u) || u.house != eh) continue;   // P3: + Insignificant/DontScore
                if (u.is_aircraft ? flying_class(i) == 1 : (u.is_vehicle || u.is_naval)) ++n;
            }
            return n;
        }
        int house_naval_units(int ht) const override {
            const int eh = rt.engine_house(ht);
            int n = 0;
            for (const ra2engine::Unit& u : rt.w_.units())
                if (rt.tracked(u) && u.house == eh && u.is_naval && !u.is_aircraft) ++n;
            return n;
        }
        int house_infantry(int ht) const override {
            const int eh = rt.engine_house(ht);
            int n = 0;
            for (std::size_t i = 0; i < rt.w_.units().size(); ++i) {
                const ra2engine::Unit& u = rt.w_.units()[i];
                if (!rt.tracked(u) || u.house != eh) continue;   // P3: + Insignificant/DontScore
                if (u.is_aircraft ? flying_class(i) == 2 : (!u.is_vehicle && !u.is_naval)) ++n;
            }
            return n;
        }
        int house_building_type_count(int ht, int bt) const override {
            if (bt < 0 || bt >= (int)rt.data_.building_types.size()) return 0;
            const std::string id = camp::up(rt.data_.building_types[(std::size_t)bt]);
            const int eh = rt.engine_house(ht);
            int n = 0;
            for (const ra2engine::Building& b : rt.w_.buildings())
                if (live(b) && b.house == eh && b.type >= 0 &&
                    camp::up(rt.ba_.roster[(std::size_t)b.type].id) == id)
                    ++n;
            return n;
        }
        long house_credits(int ht) const override {
            const int eh = rt.engine_house(ht);
            // L2: the house's real wallet (HouseEcon for the player / AI houses,
            // else the pool). Was the player's START credits (a stale copy)
            // for house 0 and the per-frame income pool for the computer
            // houses [RE: TEvent Credits reads the house's money].
            return (long)std::llround(rt.w_.credits(eh));
        }
        double house_power_ratio(int ht) const override {
            const int eh = rt.engine_house(ht);
            long out = 0, drain = 0;
            for (const ra2engine::Building& b : rt.w_.buildings()) {
                if (!live(b) || b.house != eh || b.type < 0) continue;
                const int p = rt.ba_.roster[(std::size_t)b.type].power;
                if (p > 0) out += p; else drain -= p;
            }
            return drain == 0 ? 1.0 : (double)out / (double)drain;
        }
        bool house_has_factory(int ht) const override {
            const int eh = rt.engine_house(ht);
            for (const ra2engine::Building& b : rt.w_.buildings())
                if (live(b) && b.house == eh && b.type >= 0 &&
                    !rt.rules_ini_.get(rt.ba_.roster[(std::size_t)b.type].id, "Factory", "").empty())
                    return true;
            return false;
        }
        int house_destroyed_buildings(int ht) const override {
            const int eh = rt.engine_house(ht);
            return eh >= 0 ? rt.destroyed_buildings_[(std::size_t)eh] : 0;
        }
        bool house_discovered(int ht) const override {
            const int eh = rt.engine_house(ht);
            if (eh < 0) return false;
            for (std::size_t i = 0; i < rt.utrack_.size() && i < rt.w_.units().size(); ++i)
                if (rt.utrack_[i].discovered && rt.w_.units()[i].house == eh) return true;
            for (std::size_t i = 0; i < rt.btrack_.size() && i < rt.w_.buildings().size(); ++i)
                if (rt.btrack_[i].discovered && rt.w_.buildings()[i].house == eh) return true;
            return false;
        }
        int house_destroyed_units(int ht) const override {
            const int eh = rt.engine_house(ht);
            return eh >= 0 ? rt.destroyed_units_[(std::size_t)eh] : 0;
        }
        int house_last_built(int ht, int cat) const override {
            const int eh = rt.engine_house(ht);
            return (eh >= 0 && cat >= 0 && cat < 4) ? rt.last_built_[(std::size_t)eh][(std::size_t)cat] : -1;
        }
        int techno_count(const std::string& id) const override {
            int n = 0;
            if (id.empty()) {
                for (const ra2engine::Building& b : rt.w_.buildings()) if (live(b)) ++n;
                for (const ra2engine::Unit& u : rt.w_.units()) if (live(u)) ++n;
                return n;
            }
            if (!rt.rules_ini_.has(id)) return -1;
            const std::string u = camp::up(id);
            for (std::size_t i = 0; i < rt.w_.buildings().size() && i < rt.btrack_.size(); ++i)
                if (live(rt.w_.buildings()[i]) && rt.btrack_[i].type == u) ++n;
            for (std::size_t i = 0; i < rt.w_.units().size() && i < rt.utrack_.size(); ++i)
                if (live(rt.w_.units()[i]) && rt.utrack_[i].type == u) ++n;
            return n;
        }
        std::string take_note() override {
            std::string n;
            n.swap(rt.note_);
            return n;
        }
        bool team_left_map(const std::string& id) const override { return rt.team_left(id); }
        bool waypoint_cell(int wp, int& x, int& y) const override { return rt.waypoint_cell(wp, x, y); }
        std::string infantry_type_id(int idx) const override {
            const std::vector<std::string> v = camp::list_values(rt.ra2_rules_, "InfantryTypes");
            return idx >= 0 && idx < (int)v.size() ? camp::up(v[(std::size_t)idx]) : std::string();
        }
        bool apply(const ra2engine::TriggerEffect& e) override { return rt.apply(e); }
        void present(const ra2engine::VisualRequest& v) override { rt.visuals_.push_back(v); }   // W3-FX
    };

    bool apply(const ra2engine::TriggerEffect& e) {
        switch (e.action) {
        case 4:
            return create_team(e.team, /*reinforce=*/false, -1);
        case 7: case 80: case 107:
            return create_team(e.team, /*reinforce=*/true, e.action == 7 ? -1 : e.waypoint);
        case 5: {   // DestroyTeam: the team objects go; members stay
            const int ti = teams_data_.find_team(e.team);
            int n = 0;
            for (std::size_t i = 0; i < teams_.size(); ++i)
                if (teams_[i].type == ti && !teams_[i].st.done) {
                    for (int m : teams_[i].members) { utrack_[(std::size_t)m].team = -1; maybe_leave_map(m); }
                    teams_[i].st.done = true;
                    ++n;
                }
            return n > 0;
        }
        case 6: {   // AllToHunt
            const int eh = engine_house(e.house_type);
            for (std::size_t i = 0; i < utrack_.size(); ++i)
                if (!utrack_[i].dead && w_.units()[i].house == eh) utrack_[i].hunt = true;
            return eh >= 0;
        }
        case 16:
            w_.reveal_all_house(0);
            return true;
        case 17: {
            int x = 0, y = 0;
            if (!waypoint_cell(e.waypoint, x, y)) return false;
            w_.reveal_source(0, x, y, reveal_radius_);
            return true;
        }
        case 125: {
            const int bt = types_.building(ba_, e.text);
            const int eh = engine_house(e.house_type);
            int x = 0, y = 0;
            if (bt < 0 || eh < 0 || !waypoint_cell(e.waypoint, x, y)) return false;
            const ra2engine::BuildingTypeDef& d = ba_.roster[(std::size_t)bt];
            const ra2engine::PlacementCheck chk = ra2engine::placement_check(
                x, y, d.fw, d.fh, eh, d.adjacent, w_.passable(), w_.grid_w(), w_.grid_h(),
                w_.occupancy(), w_.buildings(), /*bypass=*/true);
            if (!chk.ok) return false;
            w_.refresh_building_types(ba_.roster);   // G16
            w_.add_building(bt, x, y, d.fw, d.fh, eh, d.strength, d.armor_ix, 0.0, true, d.sight,
                            d.grants_map_reveal);
            return true;
        }
        case 14: {   // ChangeHouse: every object whose tag holds the trigger (FUN_006e0010)
            const int eh = engine_house(e.house_type);
            if (eh < 0) return false;
            int n = 0;
            for (std::size_t i = 0; i < utrack_.size(); ++i)
                if (!utrack_[i].dead && utrack_[i].tag >= 0 &&
                    sys_->tag_has_trigger(utrack_[i].tag, e.trigger_type)) {
                    w_.set_unit_owner((int)i, eh);   // SetOwningHouse FUN_00700940
                    ++n;
                }
            for (std::size_t i = 0; i < btrack_.size(); ++i)
                if (!btrack_[i].dead && btrack_[i].tag >= 0 &&
                    sys_->tag_has_trigger(btrack_[i].tag, e.trigger_type)) {
                    w_.capture_building((int)i, eh, 0);
                    w_.buildings()[i].house = eh;
                    ++n;
                }
            char b[48];
            std::snprintf(b, sizeof b, "[changed=%d to=%d]", n, eh);
            note_ = b;
            return n > 0;
        }
        case 36: {   // AllChangeHouse (FUN_006e00d0)
            const int from = engine_house(e.trigger_house_type), to = engine_house(e.house_type);
            if (from < 0 || to < 0) return false;
            for (std::size_t i = 0; i < w_.units().size(); ++i)
                if (live(w_.units()[i]) && w_.units()[i].house == from) w_.set_unit_owner((int)i, to);
            for (std::size_t i = 0; i < w_.buildings().size(); ++i)
                if (live(w_.buildings()[i]) && w_.buildings()[i].house == from) {
                    w_.capture_building((int)i, to, 0);
                    w_.buildings()[i].house = to;
                }
            return true;
        }
        case 37: case 38: {   // MakeAlly / MakeEnemy (Execute 0x25/0x26)
            // W2-CA-10 [RE FUN_004f9990/FUN_004f9db0]: pairwise and mutual for
            // the two houses only -- house 0 is never moved onto the enemy team.
            const int a = engine_house(e.trigger_house_type), b = engine_house(e.house_type);
            if (a < 0 || b < 0 || a == b) return false;
            for (int h : {a, b})
                if (e.action == 37 && !w_.house_combatant(h)) {
                    // A civilian-side house that is allied becomes a combatant
                    // on its own (it has no team of its own to bring along).
                    ra2engine::World::HouseRec r;
                    r.side = w_.house_side(h);
                    r.color_ix = w_.house_color(h);
                    r.country = map_.get(house_names_[(std::size_t)h], "Country", "");
                    r.combatant = true;
                    r.alliance_team = 200 + h;
                    r.player_control = w_.human_commanded(h) && h != 0;
                    w_.set_house(h, r);
                }
            w_.set_pair_alliance(a, b, e.action == 37);
            return true;
        }
        case 32: {   // DestroyAttachedObject (FUN_006e15c0)
            int n = 0;
            for (std::size_t i = 0; i < utrack_.size(); ++i)
                if (!utrack_[i].dead && utrack_[i].tag >= 0 &&
                    sys_->tag_has_trigger(utrack_[i].tag, e.trigger_type) && live(w_.units()[i])) {
                    w_.campaign_kill_unit((int)i);
                    ++n;
                }
            for (std::size_t i = 0; i < btrack_.size(); ++i)
                if (!btrack_[i].dead && btrack_[i].tag >= 0 &&
                    sys_->tag_has_trigger(btrack_[i].tag, e.trigger_type) && live(w_.buildings()[i])) {
                    w_.campaign_damage_building((int)i, w_.buildings()[i].hp);
                    ++n;
                }
            return n > 0;
        }
        case 60:      // W2-CA-13 SellBuilding: every tagged building of the trigger
        case 111: {   // EvictOccupiers: its garrison steps out [map-triggers.md §5]
            int n = 0;
            for (std::size_t i = 0; i < btrack_.size(); ++i)
                if (!btrack_[i].dead && btrack_[i].tag >= 0 &&
                    sys_->tag_has_trigger(btrack_[i].tag, e.trigger_type) && live(w_.buildings()[i])) {
                    if (e.action == 60) {
                        if (w_.begin_sell((int)i, w_.buildings()[i].house)) ++n;
                    } else if (!w_.buildings()[i].occupants.empty()) {
                        w_.unload_occupants((int)i);
                        ++n;
                    }
                }
            return n > 0;
        }
        case 108: {   // CreateCrate: Value = [Powerups] index; 20 = untyped (random)
            int x = 0, y = 0;
            if (!waypoint_cell(e.waypoint, x, y)) return false;
            return w_.place_crate(x, y, e.value == 20 ? -1 : e.value);
        }
        case 101: {   // ReshroudMapAtWaypoint, radius [General] RevealTriggerRadius
            int x = 0, y = 0;
            if (!waypoint_cell(e.waypoint, x, y)) return false;
            w_.reshroud_area(0, x, y, reveal_radius_);
            return true;
        }
        case 9: {   // FireSale -> house AI state 4 (see step_house_states)
            const int eh = engine_house(e.house_type);
            if (eh < 0) return false;
            if ((int)fire_sale_.size() <= eh) fire_sale_.resize((std::size_t)eh + 1, 0);
            fire_sale_[(std::size_t)eh] = 1;
            fire_sale_pass(eh);   // the first AI pass now
            return true;
        }
        case 35: {   // PreferredTarget: trigger house quarry (+0x54EC)
            const int eh = engine_house(e.house_type);
            if (eh < 0) return false;
            if ((int)quarry_.size() <= eh) quarry_.resize((std::size_t)eh + 1, 1);
            quarry_[(std::size_t)eh] = e.value;
            w_.set_sw_quarry(eh, e.value);
            return true;
        }
        case 40: {   // ResizeView: [Map] LocalSize = (P2, P3, P4, P5) [RE: FUN_006E1750]
            ra2engine::MapLocalSize m = ra2engine::parse_map_local_size(map_);
            if (!m.valid) return false;
            m.x = e.p[0]; m.y = e.p[1]; m.w = e.p[2]; m.h = e.p[3];
            local_override_ = m;
            local_overridden_ = true;
            ++local_version_;
            apply_playfield();   // W6-02-08: every object's IsInPlayfield recomputed
            char b[64];
            std::snprintf(b, sizeof b, "[local %d,%d,%d,%d]", m.x, m.y, m.w, m.h);
            note_ = b;
            return true;
        }
        case 44: {   // IonStorm (YR: LightningStorm::Start unless one rages) [RE: case @0x6DE28C;
                     // RE-FALLBACK: the original's "no cell" storm is centred on the usable area]
            const ra2engine::MapLocalSize m = local_size();
            const int x = m.valid ? m.x + m.w / 2 : w_.grid_w() / 2;
            const int y = m.valid ? m.y + m.h / 2 : w_.grid_h() / 2;
            return w_.trigger_lightning_storm(0, x, y);
        }
        case 51:     // Reshroud [RE: FUN_005777C0]: the player's map back under shroud
            w_.reshroud_house(0);
            return true;
        case 61: case 62: {   // TurnOff / TurnOn the tagged buildings [RE: FUN_00452350 / FUN_00452250]
            // The World's GoOffline/GoOnline (set_building_power, W5-E2) does the
            // power switch; the flag is kept for the save / witness.
            int n = 0;
            for (std::size_t i = 0; i < btrack_.size(); ++i)
                if (!btrack_[i].dead && btrack_[i].tag >= 0 && sys_->tag_has_trigger(btrack_[i].tag, e.trigger_type)) {
                    btrack_[i].off = e.action == 61;
                    w_.set_building_power((int)i, e.action == 61 ? 0 : 1);
                    ++n;
                }
            char b[48];
            std::snprintf(b, sizeof b, "[buildings=%d]", n);
            note_ = b;
            return n > 0;
        }
        case 77: case 78:   // AI air / infantry ratio (+0x5660 / +0x5664): no consumer in the AI
            note_ = "[ratio recorded; AI-2 has no ratio input]";
            return true;
        case 90: case 102: {   // W3-FX LightningStrikeAt / LightningStorm (World)
            int x = 0, y = 0;
            if (!waypoint_cell(e.waypoint, x, y)) return false;
            const int eh = std::max(0, engine_house(e.house_type));
            return e.action == 90 ? w_.trigger_lightning_strike(eh, x, y)
                                  : w_.trigger_lightning_storm(eh, x, y);
        }
        case 42: {   // W3-FX DoExplosionAt [RE: FUN_006e1900]
            int x = 0, y = 0;
            if (!waypoint_cell(e.waypoint, x, y)) return false;
            const std::string wpn = ra2_weapon_by_index(e.value);
            if (wpn.empty()) return false;
            const int dmg = std::atoi(camp::trim_comment(ra2_rules_.get(wpn, "Damage", "0")).c_str());
            // [DEVIATION, as 63: the waypoint cell's objects take the weapon's
            // Damage; the warhead's CellSpread falloff is not applied.]
            const int u = w_.unit_at_cell(x, y);
            if (u >= 0 && dmg > 0) w_.campaign_damage_unit(u, dmg);
            const int b = w_.building_at_cell(x, y);
            if (b >= 0 && dmg > 0) w_.campaign_damage_building(b, dmg);
            ra2engine::VisualRequest v;   // the warhead's impact anim (presentation)
            v.action = 42;
            v.value = dmg;
            v.waypoint = e.waypoint;
            v.text = camp::trim_comment(ra2_rules_.get(wpn, "Warhead", ""));
            visuals_.push_back(v);
            return true;
        }
        case 18: {   // W4-SQ-11 RevealZone [RE-FALLBACK: MapClass zone = the
                     // waypoint cell's connected ground area, walked to 64 cells out]
            int x = 0, y = 0;
            if (!waypoint_cell(e.waypoint, x, y)) return false;
            const int W = w_.grid_w(), H = w_.grid_h();
            std::vector<std::uint8_t> seen((std::size_t)W * H, 0);
            std::vector<int> q{y * W + x};
            seen[(std::size_t)q[0]] = 1;
            const bool land = w_.walkable(x, y);
            for (std::size_t k = 0; k < q.size(); ++k) {
                const int cx = q[k] % W, cy = q[k] / W;
                if ((cx + cy) % 3 == 0) w_.reveal_source(0, cx, cy, 2);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                        if (std::abs(nx - x) > 64 || std::abs(ny - y) > 64) continue;
                        const std::size_t ni = (std::size_t)ny * W + nx;
                        if (seen[ni] || w_.walkable(nx, ny) != land) continue;
                        seen[ni] = 1;
                        q.push_back((int)ni);
                    }
            }
            return true;
        }
        case 81: case 82: case 83:   // W4-SQ-05 WakeupSelf / WakeupAllSleepers / WakeupAllHarmless
            return wake(e.action, e.trigger_type) > 0;
        case 63: {   // Apply100Damage [DEVIATION §9.6: the waypoint cell, no splash]
            int x = 0, y = 0;
            if (!waypoint_cell(e.waypoint, x, y)) return false;
            const int u = w_.unit_at_cell(x, y);
            if (u >= 0) w_.campaign_damage_unit(u, 100);
            const int b = w_.building_at_cell(x, y);
            if (b >= 0) w_.campaign_damage_building(b, 100);
            // W4-SQ-10 [RE: FUN_006DFA00 then calls FUN_00586BB0 (the bridge
            // destroy dispatch) on the waypoint cell, up to 4 times until one
            // succeeds -- no BridgeStrength roll, no DestroyableBridges gate].
            int broke = 0;
            for (int k = 0; k < 4 && broke == 0; ++k) broke = w_.force_bridge_break(x, y);
            if (broke > 0) {
                char nb[80];
                std::snprintf(nb, sizeof nb, "[bridge cells broken=%d]", broke);
                note_ = nb;
            }
            return u >= 0 || b >= 0 || broke > 0;
        }
        default:
            return false;
        }
    }

public:
    // NET L1: the local player's selection the event "Selected by player"
    // (33) watches -- set by GameScene from the SELECT_NOTIFY commands.
    void set_selection(const ra2engine::Selection* s) { sel_ = s; }
private:
    const ra2engine::Selection* sel_ = nullptr;
    camp::Launch launch_;
    std::string dir_;
    ra2engine::World& w_;

    Assets& a_;
    BuildingAssets& ba_;
    const Ini& rules_ini_;
    Ini map_;
    Ini ra2_rules_;
    ra2engine::ScenarioBasic basic_;
    camp::Types types_;
    ra2engine::MapTriggerData data_;
    ra2engine::MapTeamData teams_data_;
    ra2engine::MissionEnd end_;
    Host host_;
    std::unique_ptr<ra2engine::MapTriggerSystem> sys_;
    std::vector<std::string> house_names_;
    std::vector<int> house_type_of_;
    std::vector<int> houses_order_ht_;
    std::vector<std::vector<ra2engine::PlannedNode>> base_nodes_;   // [G-69] per engine house
    std::vector<std::string> ra2_vehicles_, ra2_infantry_, ra2_aircraft_;
    std::vector<int> destroyed_units_, destroyed_buildings_;
    std::vector<std::array<int, 4>> last_built_;
    std::vector<Track> utrack_, btrack_;
    std::vector<Team> teams_;
    std::vector<Objective> objectives_;
    std::vector<std::string> witness_;
    const UnitRoster* ai_roster_ = nullptr;   // W2-CA-07 (GameScene's ur_ai_)
    std::map<std::string, bool> mc_type_;      // W2-CA-15 cache: type -> MindControl weapon
    mutable std::map<std::string, bool> untracked_;   // P3 cache: type -> Insignificant/DontScore (derived)
    std::vector<std::uint8_t> armed_;         // W2-CA-07 per house: 1 base, 2 map teams (derived)
    std::vector<AudioReq> audio_;   // W2-CA-12 (presentation only, not hashed/saved)
    std::vector<ra2engine::VisualRequest> visuals_;   // W3-FX (presentation only, not hashed/saved)
    std::vector<std::string> ra2_weapons_;            // W3-FX DoExplosionAt index space (lazy)
    std::string note_;
    std::size_t logged_ = 0;
    ra2engine::World::Side player_side_ = ra2engine::World::Side::Allied;
    double player_credits_ = 0.0;
    double accum_ = 0.0;
    long frame_ = 0;
    int reveal_radius_ = 9;
    int placed_b_ = 0, placed_u_ = 0, skipped_ = 0;
    int teams_created_ = 0, raises_ = 0;
    bool placing_ = false;
    int place_vet_ = 0;      // W4-SQ-04: rank given to the members being created
    // W5-A (saved, v22): house quarries (35), FireSale houses (9), TeamTypes
    // that left the map (23), the ResizeView usable area (40).
    std::vector<int> quarry_;
    std::vector<std::uint8_t> fire_sale_;
    std::vector<std::string> left_teams_;
    ra2engine::MapLocalSize local_override_;
    bool local_overridden_ = false;
    int local_version_ = 0;
    std::vector<std::uint8_t> bridge_seen_;   // derived (event 31 edge), rebuilt after a load
    std::uint32_t bridge_ver_ = 0;
    int unresolved_ = 0;     // TaskForce objects whose type did not resolve
};

// The World a campaign GameScene starts from: the plain map world (no
// pre-spawned squads) with the water grid; the CampaignRuntime then configures
// houses and places the map's own objects.
ra2engine::World build_campaign_world(Assets& a, UnitAssets& ua) {
    load_progress("CAMPAIGN WORLD");   // W2-AR-02 loading-screen checkpoint
    ra2engine::World world = build_world(a, ua, 0);
    const int gw = world.grid_w(), gh = world.grid_h();
    // Gap fix C (G-43): water grid + ground blocked on water/cliffs.
    (void)gw; (void)gh;
    depth_set_terrain(world, a);   // G16 (agent G): + low bridges [RE: world-depth.md §B]
    return world;
}

// Scene factories the campaign GameScene transitions through; defined in
// campaign_scenes.inc (after the shell scenes exist).
Scene* make_campaign_result_scene(bool no_audio, const ra2engine::SkirmishConfig& cfg, int outcome,
                                  long frames);
Scene* make_main_menu_scene(bool no_audio);   // nullptr when no shell is running

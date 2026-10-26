// ============================================================================
// [G-60 / G-62 / G-64] The REAL-PLAY skirmish AI: every AI slot gets the full
// three-layer brain (AI-1 base + AI-2 teams + AI-3 triggers) with a
// side-correct building catalog, unit roster, factories, trigger side and
// country. Included by main.cpp after make_ai_team_config.
//
// Regression origin: commit f77e68e (Scene framework) dropped the aimd.ini
// load and `acfg.team = make_ai_team_config(...)` from the interactive path;
// since then make_ai_team_config had only the two gate callers, so
// step_ai_teams skipped every real AI house and the AI never trained a unit.
// arm_skirmish_ai is called from GameScene (ctor + the load-failed restart)
// and from the --ai-soak acceptance harness -- one code path.
// ============================================================================

// Every combat technotype the stock aimd.ini TaskForces name that this engine
// can field on the ground (aircraft/naval/engineers/spies excluded: the AI
// never recruits aircraft, there is no naval AI, capture is not scripted).
// Prerequisites are FULL-parsed from rulesmd (APOC needs NATECH, V3 needs
// NARADR, ...), so a house builds only what its base allows; each house's
// share is filtered by Owner/RequiredHouses/ForbiddenHouses (ai_side_catalog).
#include "debug_ai.hpp"
UnitRoster load_ai_skirmish_roster(const std::string& dir, const ra2assets::MixFile& ra2,
                                   const UnitAssets& ua, const Rules& rules, const Art& art) {
    load_progress("AI ROSTER");   // W2-AR-02 loading-screen checkpoint
    static const UnitRosterEnt kAiUnits[] = {
        // Soviet
        {"E2", false, "NAHAND"},   {"DOG", false, "NAHAND"},  {"FLAKT", false, "NAHAND"},
        {"SHK", false, "NAHAND"},  {"TERROR", false, "NAHAND"}, {"DESO", false, "NAHAND"},
        {"IVAN", false, "NAHAND"}, {"HTNK", true, "NAWEAP"},  {"HTK", true, "NAWEAP"},
        {"APOC", true, "NAWEAP"},  {"V3", true, "NAWEAP"},    {"DRON", true, "NAWEAP"},
        {"TTNK", true, "NAWEAP"},  {"DTRUCK", true, "NAWEAP"},
        // Allied
        {"E1", false, "GAPILE"},   {"GGI", false, "GAPILE"},  {"ADOG", false, "GAPILE"},
        {"SNIPE", false, "GAPILE"},{"TANY", false, "GAPILE"}, {"CLEG", false, "GAPILE"},
        {"GHOST", false, "GAPILE"},{"MTNK", true, "GAWEAP"},  {"FV", true, "GAWEAP"},
        {"SREF", true, "GAWEAP"},  {"MGTK", true, "GAWEAP"},  {"TNKD", true, "GAWEAP"},
        // W2-AN-16: the navies and jumpjets the stock aimd TaskForces name
        // (ships start only once a Naval Yard stands: Prerequisite=NAYARD/GAYARD)
        {"SUB", true, "NAYARD"},   {"DRED", true, "NAYARD"},  {"HYD", true, "NAYARD"},
        {"SQD", true, "NAYARD"},   {"DEST", true, "GAYARD"},  {"AEGIS", true, "GAYARD"},
        {"CARRIER", true, "GAYARD"}, {"DLPH", true, "GAYARD"},
        {"ZEP", true, "NAWEAP"},   {"SHAD", true, "GAWEAP"},  {"JUMPJET", false, "GAPILE"},
        // [G-68] the miners (IQ Harvester replacement)
        {"HARV", true, "NAWEAP"},  {"CMIN", true, "GAWEAP"},
        // W2-SK-09: engineers and the spy (their TaskForces were dropped)
        {"ENGINEER", false, "GAPILE"}, {"SENGINEER", false, "NAHAND"}, {"SPY", false, "GAPILE"},
        // W2-SK-07: the MCVs (a ConYard-less AI rebuys and redeploys one)
        {"AMCV", true, "GAWEAP"},  {"SMCV", true, "NAWEAP"},
        // Wave-5 (R-042): the stolen-tech infantry aimd names (TaskForce
        // "0=2,CCOMAND"); buildable only once a spy stole the tech.
        {"CCOMAND", false, "GAPILE"}, {"CIVAN", false, "NAHAND"},
        // Wave-5 (R-080): the fixed-wing strike aircraft the aimd TaskForces
        // name ("4 Harriers" 0=4,ORCA / "4 Black Eagles" 0=4,BEAG); built onto
        // a free Airforce Command pad (launch_unit's fixed-wing branch).
        {"ORCA", true, "GAAIRC"}, {"BEAG", true, "GAAIRC"},
        // W7-03-08 / W7-05-12: the Robot Tank, Battle Fortress and Siege
        // Chopper the stock aimd TaskForces name (0=2,ROBO / 0=4,BFRT /
        // 0=2,SCHP). Appended (every older roster index is unchanged); each is
        // gated by its own full-parsed Prerequisite= (ROBO needs GAROBO, which
        // no [AI] Build* list names, so a skirmish AI fields it only once it
        // owns a Robot Control Center by other means, as in the original).
        {"ROBO", true, "GAWEAP"}, {"BFRT", true, "GAWEAP"}, {"SCHP", true, "NAWEAP"},
        {"SMIN", true, "YAWEAP"}, {"PCV", true, "YAWEAP"},
    };
    UnitRoster ur = load_unit_roster_table(dir, ra2, ua, rules, art, kAiUnits,
                                           sizeof(kAiUnits) / sizeof(kAiUnits[0]), /*full_parse=*/true);
    for (ra2engine::UnitTypeDef& d : ur.defs) {   // same resolution as rp_load_roster
        d.harvester = rules.harvester(d.id);
        if (d.harvester) {
            d.storage = rules.storage(d.id);
            d.chrono_miner = d.id == "CMIN";
        }
        rp_arm_stolen_tech(rules, d);   // wave-5 (R-042)
    }
    return ur;
}

// aimd.ini lives in ra2md.mix/localmd.mix next to the other INIs. A missing
// file degrades to zero teams (logged), never a crash.
ra2assets::Ini load_aimd_or_empty(const std::string& dir) {
    try {
        return load_ini_from_ra2md(dir, "aimd.ini");
    } catch (const std::exception& ex) {
        std::cerr << "[ai-team] aimd.ini unavailable (" << ex.what() << ")\n";
        return ra2assets::Ini::parse("");
    }
}

// [G-61] rulesmd [BuildingTypes] index -> building roster index (actions
// 46/47/58), and the quarry class bits (6 factories / 7 base defenses /
// 9 power / 11 tech).
void fill_ai_building_maps(ra2engine::AiTeamConfig& tc, const BuildingAssets& ba,
                           const Rules& rules, const ra2assets::Ini& rules_ini) {
    tc.building_type_roster.clear();
    if (rules_ini.has("BuildingTypes"))
        for (const auto& kv : rules_ini.section("BuildingTypes")) {
            const std::vector<std::string> t = aiside::csv_tokens(kv.second);
            tc.building_type_roster.push_back(t.empty() ? -1 : roster_index(ba, t[0]));
        }
    tc.building_class.assign(ba.roster.size(), 0);
    for (std::size_t i = 0; i < ba.roster.size(); ++i) {
        const std::string& id = ba.roster[i].id;
        std::uint8_t bits = 0;
        if (!rules.section_string(id, "Factory", "").empty()) bits |= ra2engine::kAiBldFactory;
        if (rules.section_flag(id, "IsBaseDefense", false)) bits |= ra2engine::kAiBldDefense;
        if (ba.roster[i].power > 0) bits |= ra2engine::kAiBldPower;
        // [RE-FALLBACK] "tech building" = an engineer-capturable neutral
        // structure (NeedsEngineer=yes: oil derricks, labs, ...).
        if (rules.section_flag(id, "NeedsEngineer", false)) bits |= ra2engine::kAiBldTech;
        tc.building_class[i] = bits;
    }
}

// The full real-play AI config for one slot.
ra2engine::AiHouseConfig make_ai_slot_config(const ra2engine::SkirmishSlot& s, double start_cash,
                                             const BuildingAssets& ba, const Rules& rules,
                                             const ra2assets::Ini& rules_ini,
                                             const UnitRoster& ur_ai, const ra2assets::Ini& aimd,
                                             int anchor_x, int anchor_y,
                                             const ra2assets::MapFile* map) {
    const std::string country = aiside::house_country(s.side, s.country, &rules);
    ra2engine::AiHouseConfig acfg =
        make_ai_config(ba, rules, s.diff, start_cash, s.side, country);
    // This house's share of the AI roster (country/side ownership).
    UnitRoster mine;
    for (const ra2engine::UnitTypeDef& d : ur_ai.defs)
        if (aiside::owned_by(rules, d.id, s.side, country)) mine.defs.push_back(d);
    acfg.team = make_ai_team_config(ba, mine, rules, aimd, s.diff, anchor_x, anchor_y,
                                    /*enable_triggers=*/true, map);
    // Side-correct factories / trigger side / country [G-64].
    auto id_of = [&](int t) -> const char* {
        return (t >= 0 && t < (int)ba.roster.size()) ? ba.roster[t].id.c_str() : "";
    };
    acfg.team.inf_factory_types = factory_types_for(ba, id_of(acfg.catalog.barracks));
    acfg.team.veh_factory_types = factory_types_for(ba, id_of(acfg.catalog.war_factory));
    // W2-AN-16 [rules [AI] BuildNavalYard=, [General] AINavalYardAdjacency=]:
    // real play only (the AI gates keep their historical plan).
    acfg.catalog.naval_yard =
        aiside::pick_from_list(rules, "AI", "BuildNavalYard", s.side, country, [&](const char* id) {
            const int t = roster_index(ba, id);
            return (t >= 0 && ba.art[t].ok) ? t : -1;
        });
    acfg.catalog.naval_yard_adjacency =
        static_cast<int>(rules.general_double("AINavalYardAdjacency", 20.0));
    acfg.team.nav_factory_types = factory_types_for(ba, id_of(acfg.catalog.naval_yard));
    acfg.team.trigger_side = s.side == ra2engine::World::Side::Soviet ? 2 : 1;  // [ai-trigger.md §5]
    acfg.team.house_name = country;
    fill_ai_building_maps(acfg.team, ba, rules, rules_ini);
    // W7-04-18 [RE: TechnoTypeClass ToProtect=, read by ReceiveDamage
    // @0x701C43]: HARV, CMIN and the navy call for help when hit.
    acfg.roster_to_protect.clear();
    for (const ra2engine::UnitTypeDef& d : acfg.team.unit_roster)
        acfg.roster_to_protect.push_back(rules.section_flag(d.id, "ToProtect", false) ? 1 : 0);
    acfg.multiple_factory = rules.general_double("MultipleFactory", 0.8);   // W2-SK-06
    // W2-SK-24 [RE: rulesmd DissolveUnfilledTeamDelay=5000 "multiplay only";
    // RE-FALLBACK: a skirmish session is a non-campaign (multiplayer-type)
    // session, so unfilled teams dissolve -- they piled up at 15+ empty
    // Recruiting teams in 40-minute soaks].
    acfg.team.rules.multiplay = true;
    {   // [G-68] HarvestersPerRefinery=2,2,1 ;h,m,e at house+0x184 = 2 - lobby
        const std::vector<std::string> v =
            aiside::csv_tokens(rules.section_string("General", "HarvestersPerRefinery", ""));
        const int c = ra2engine::difficulty_class_index(s.diff);
        acfg.team.harvesters_per_refinery = c < (int)v.size() ? std::atoi(v[(std::size_t)c].c_str()) : 1;
    }
    {   // W6-03-06 [RE: FUN_005052d0 Rules+0x1378 / FUN_004fe200 Rules+0xdd8] at
        // house+0x184: AIExtraRefineries=2,1,0 and AIPickWallDefensePercent=50,25,10.
        const int c = ra2engine::difficulty_class_index(s.diff);
        auto at = [&](const char* key, int def) {
            const std::vector<std::string> v = aiside::csv_tokens(rules.section_string("General", key, ""));
            return c < (int)v.size() ? std::max(0, std::atoi(v[(std::size_t)c].c_str())) : def;
        };
        acfg.ai.extra_refineries = at("AIExtraRefineries", 0);
        acfg.ai.pick_wall_pct = at("AIPickWallDefensePercent", 0);
        // W6-03-04 [RE: FUN_006862d0 -> Rules+0x1178]: AIHateDelays=30,50,70 frames.
        acfg.hate_delay_frames = at("AIHateDelays", 0);
        // W7-01-08 [RE: FUN_00521050 @0x52128C -- Rules+0xE30[house+0x184]]:
        // AIAutoDeployFrameDelay=15,25,100 ;h,m,e (loader @0x66FC72).
        acfg.auto_deploy_frames = at("AIAutoDeployFrameDelay", 0);
        // W6-03-10 [RE: FUN_006862d0 @0x686492-0x6864B3 -- at the game start
        // every computer house gets GiveMoney(ftol(Rules+0x1308[house+0x184]
        // (MultiplayerAICM=400,0,0) * 0.01 * its money)) (FUN_004f9770 adds to
        // house+0x30c)]: a Brutal AI starts with 5x the lobby money.
        acfg.start_credits += (double)(int)((double)at("MultiplayerAICM", 0) * 0.01 * (double)(int)start_cash);
    }
    {   // W6-03-03 [RE: FUN_00522a80; loader FUN_0066cf70 -> Rules+0x1320]:
        // AIVirtualPurifiers=4,2,0 ;h,m,e -- the skirmish AI's harvest bonus.
        const std::vector<std::string> v =
            aiside::csv_tokens(rules.section_string("General", "AIVirtualPurifiers", ""));
        const int c = ra2engine::difficulty_class_index(s.diff);
        acfg.virtual_purifiers = c < (int)v.size() ? std::max(0, std::atoi(v[(std::size_t)c].c_str())) : 0;
    }
    ra2engine::configure_debug_ai(acfg);
    std::cerr << "[ai] slot side=" << (s.side == ra2engine::World::Side::Yuri ? "Yuri" : s.side == ra2engine::World::Side::Soviet ? "Soviet" : "Allied")
              << " mode=" << (ra2engine::debug_ai_mode(s.diff) ? "DEBUG-AI" : "normal")
              << " country=" << country << " diff=" << (int)s.diff
              << " roster=" << acfg.team.unit_roster.size()
              << " teams=" << acfg.team.aimd.teams.size()
              << " triggers=" << acfg.team.aimd.triggers.size()
              << " conyard=" << id_of(acfg.catalog.conyard)
              << " barracks=" << id_of(acfg.catalog.barracks) << "\n";
    return acfg;
}

// Arm every AI slot of a freshly launched skirmish World with the full brain.
// Overwrites the AI-1-only config launch_skirmish installed (same start cash,
// same tick 0), so it must run before the first World::step.
void arm_skirmish_ai(ra2engine::World& world, const ra2engine::SkirmishConfig& cfg,
                     const BuildingAssets& ba, const Rules& rules,
                     const ra2assets::Ini& rules_ini, const UnitRoster& ur_ai,
                     const ra2assets::Ini& aimd, const ra2assets::MapFile* map,
                     const UnitAssets& ua) {
    load_progress("AI TEAMS");   // W2-AR-02 loading-screen checkpoint
    if (!ba.ok) return;
    // Waypoint-ring anchor: the first non-AI combatant's base (the human),
    // else the grid centre.
    int ax = world.grid_w() / 2, ay = world.grid_h() / 2;
    for (const ra2engine::Building& b : world.buildings()) {
        const int h = b.house;
        if (b.alive && h >= 0 && h < (int)cfg.slots.size() && cfg.slots[h].combatant &&
            !cfg.slots[h].is_ai) { ax = b.cx; ay = b.cy; break; }
    }
    // gF G-23: a real skirmish starts from an MCV (no building yet) -- anchor
    // on the human's MCV/start force instead of the grid centre.
    if (ax == world.grid_w() / 2 && ay == world.grid_h() / 2)
        for (const ra2engine::Unit& u : world.units()) {
            const int h = u.house;
            if (u.alive && h >= 0 && h < (int)cfg.slots.size() && cfg.slots[h].combatant &&
                !cfg.slots[h].is_ai) { ax = u.cell_x; ay = u.cell_y; break; }
        }
    for (int i = 0; i < (int)cfg.slots.size(); ++i) {
        const ra2engine::SkirmishSlot& s = cfg.slots[i];
        if (!s.is_ai || !s.combatant) continue;
        ra2engine::AiHouseConfig acfg = make_ai_slot_config(
            s, cfg.starting_cash, ba, rules, rules_ini, ur_ai, aimd, ax, ay, map);
        // [SK-27] Lobby Superweapons off: the AI plans no SuperWeapon= building.
        if (!cfg.superweapons) acfg.catalog.superweapons.clear();
        // Register the AI infantry anims so launched infantry animate.
        for (const ra2engine::UnitTypeDef& d : acfg.team.unit_roster)
            if (!d.is_vehicle)
                world.set_anim(d.combat.image, ua.anims.count(d.combat.image)
                                                   ? ua.anims.at(d.combat.image)
                                                   : ra2engine::default_anim());
        world.enable_ai_house(i, std::move(acfg));
    }
}

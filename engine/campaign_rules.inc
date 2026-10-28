// W2-CA-03 / W2-CA-04: the campaign GameScene's rules and the player's
// rules-driven roster. Textually included by main.cpp right after
// campaign_runtime.inc (inside its anonymous namespace, before GameScene).
// Reached ONLY when a SkirmishConfig carries a campaign_map: skirmish and the
// byte-identity gates never call anything here.

namespace camp {

// Map sections that are scenario data, never rules (the RA2 map format).
bool map_only_section(const std::string& up_name) {
    static const char* kMapOnly[] = {
        "BASIC", "MAP", "PREVIEW", "PREVIEWPACK", "HEADER", "DIGEST", "ISOMAPPACK5",
        "OVERLAYPACK", "OVERLAYDATAPACK", "WAYPOINTS", "STRUCTURES", "UNITS", "INFANTRY",
        "AIRCRAFT", "TERRAIN", "SMUDGE", "TRIGGERS", "EVENTS", "ACTIONS", "TAGS", "CELLTAGS",
        "VARIABLENAMES", "TASKFORCES", "SCRIPTTYPES", "TEAMTYPES", "AITRIGGERTYPES",
        "AITRIGGERTYPESENABLE", "HOUSES", "LIGHTING", "SPECIALFLAGS", "TUBES", "RANKING"};
    for (const char* s : kMapOnly)
        if (up_name == s) return true;
    // TaskForce / ScriptType / TeamType bodies are named by 8-hex-digit ids.
    if (up_name.size() == 8 &&
        std::all_of(up_name.begin(), up_name.end(), [](char c) { return std::isxdigit((unsigned char)c); }))
        return true;
    return false;
}

// Rules registries whose map entries ADD a type by name (find-or-create), not
// replace the stock entry at the same key.
bool rules_list_section(const std::string& up_name) {
    static const char* kLists[] = {
        "INFANTRYTYPES", "VEHICLETYPES", "AIRCRAFTTYPES", "BUILDINGTYPES", "COUNTRIES",
        "ANIMATIONS", "WARHEADS", "VOXELANIMS", "PARTICLES", "PARTICLESYSTEMS",
        "SUPERWEAPONTYPES", "OVERLAYTYPES", "TERRAINTYPES", "SMUDGETYPES", "TIBERIUMS"};
    for (const char* s : kLists)
        if (up_name == s) return true;
    return false;
}

// W2-CA-04 [RE-W2-3 RE-FALLBACK: the scenario INI is read as a rules overlay
// after the stock rules (the documented RA2 map-override behaviour that every
// campaign map relies on -- [GAWEAP] TechLevel=-1 bans, [NAPSYB] Cost=50,
// [TANY] Sight=10; the ScenarioClass call itself was not traced)]. Every map
// section that is not scenario data overrides the stock keys; registry lists
// gain the map's new type names; the map's house sections ([Player House])
// and its own countries ([Player], ParentCountry=) ride along harmlessly.
Ini overlay_map_rules(const Ini& base, const Ini& map) {
    std::string text;
    text.reserve(1u << 20);
    for (const std::string& sec : base.section_names()) {
        text += '[';
        text += sec;
        text += "]\n";
        for (const auto& kv : base.section(sec)) {
            text += kv.first;
            text += '=';
            text += kv.second;
            text += '\n';
        }
    }
    int added = 0;
    for (const std::string& sec : map.section_names()) {
        if (map_only_section(sec)) continue;
        text += '[';
        text += sec;
        text += "]\n";
        if (rules_list_section(sec)) {
            std::vector<std::string> have;
            for (const auto& kv : base.section(sec)) have.push_back(up(trim_comment(kv.second)));
            for (const auto& kv : map.section(sec)) {
                const std::string v = trim_comment(kv.second);
                if (v.empty() || std::find(have.begin(), have.end(), up(v)) != have.end()) continue;
                have.push_back(up(v));
                text += "W2CA" + std::to_string(added++) + '=' + v + '\n';
            }
            continue;
        }
        for (const auto& kv : map.section(sec)) {
            text += kv.first;
            text += '=';
            text += kv.second;
            text += '\n';
        }
    }
    return Ini::parse(text);
}

// The campaign GameScene's rules Ini: rulesmd with the mission map overlaid.
Ini load_campaign_rules_ini(const std::string& dir, const std::string& map_name) {
    const Ini base = load_ini_from_ra2md(dir, "rulesmd.ini");
    try {
        return overlay_map_rules(base, load_map_ini(dir, map_name));
    } catch (const std::exception& ex) {
        std::cerr << "[campaign] map rules overlay unavailable (" << ex.what() << ")\n";
        return base;
    }
}

// W9-03-04: a multiplayer game type's rules (MPModesMD.ini: "Rules.INI
// override" files such as MPMWMD.ini) -- the same section-wise overlay of
// rulesmd.ini. [RE-FALLBACK: the original reads the override after the
// rules (FUN_005D6FB0 stores the file name per mode); the merge call itself
// was not traced.]
Ini load_mode_rules_ini(const std::string& dir, const std::string& override_name) {
    const Ini base = load_ini_from_ra2md(dir, "rulesmd.ini");
    try {
        const std::vector<uint8_t> raw = read_ra2md_file(dir, override_name);
        return overlay_map_rules(base, Ini::parse(std::string(raw.begin(), raw.end())));
    } catch (const std::exception& ex) {
        std::cerr << "[mp] game type rules " << override_name << " unavailable (" << ex.what() << ")" << std::endl;
        return base;
    }
}

// W2-CA-03: who the campaign player is for the rules-driven roster.
struct PlayerRules {
    std::string country;              // Owner=/Required/Forbidden key (a stock country)
    int tech_level = 10;              // [<Player house>] TechLevel=
    std::vector<std::string> allowed; // upper ids the classic RA2 game knows
};

// The player's country for Owner= checks: the house's Country= when it is a
// stock [Countries] entry, else that country's ParentCountry= (the map
// countries "Player"/"BadGuy1" carry ParentCountry=Americans/Russians), else
// the side default (by the country's Side=). [RE-FALLBACK: HouseTypeClass
// ParentCountry resolution as documented for RA2 maps; the Owner bit test
// itself was not traced.]
// `allowed` = the types listed by the classic RA2 rules.ini (expand01 copy)
// plus the map's own registry additions: the YR-only types of rulesmd (GGI,
// ...) are never offered in an RA2 campaign.
PlayerRules campaign_player_rules(const std::string& dir, const MixFile& ra2, const std::string& map_name,
                                  const std::string& side) {
    PlayerRules pr;
    pr.country = up(side) == "NOD" ? "Russians" : "Americans";
    try {
        const Ini map = load_map_ini(dir, map_name);
        const Ini ra2r = yr_map(map_name) ? load_ini_from_ra2md(dir, "rulesmd.ini") : load_ra2_rules_ini(dir, ra2);
        const std::string house = trim_comment(map.get("Basic", "Player", ""));
        pr.tech_level = map.get_int(house, "TechLevel", 10);
        const std::string country = trim_comment(map.get(house, "Country", ""));
        const std::vector<std::string> stock = list_values(ra2r, "Countries");
        // The country's side: the map section, else the stock section.
        std::string cside = up(trim_comment(map.get(country, "Side", "")));
        if (cside.empty()) cside = up(trim_comment(ra2r.get(country, "Side", "")));
        if (cside == "GDI") pr.country = "Americans";
        if (cside == "NOD") pr.country = "Russians";
        if (index_of(stock, country) >= 0) {
            pr.country = country;
        } else {
            // ParentCountry only when it is on the same side: SOV01's "Player"
            // says ParentCountry=Americans with Side=Nod (a map-author slip
            // the Soviet roster must survive).
            const std::string parent = trim_comment(map.get(country, "ParentCountry", ""));
            if (index_of(stock, parent) >= 0 &&
                (cside.empty() || up(trim_comment(ra2r.get(parent, "Side", ""))) == cside))
                pr.country = parent;
        }
        for (const char* l : {"InfantryTypes", "VehicleTypes", "AircraftTypes", "BuildingTypes"}) {
            for (const std::string& v : list_values(ra2r, l)) pr.allowed.push_back(up(v));
            for (const std::string& v : list_values(map, l)) pr.allowed.push_back(up(v));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[campaign] player rules unavailable (" << ex.what() << ")\n";
    }
    return pr;
}

bool allowed_type(const PlayerRules& pr, const std::string& id) {
    if (pr.allowed.empty()) return true;
    return std::find(pr.allowed.begin(), pr.allowed.end(), up(id)) != pr.allowed.end();
}

// Loose skirmish maps may override supported rules keys; use the same merger
// as campaigns, keeping scenario data out and registry additions additive.
Ini load_skirmish_rules_ini(const std::string& dir,const std::string& map,const std::string& mode) {
    Ini base=mode.empty()?load_ini_from_ra2md(dir,"rulesmd.ini"):load_mode_rules_ini(dir,mode);
    if(!ra2engine::addon_map_token(map))return base;
    const auto raw=ra2engine::read_addon_map(dir,map);
    return overlay_map_rules(base,Ini::parse(std::string(raw.begin(),raw.end())));
}

}  // namespace camp

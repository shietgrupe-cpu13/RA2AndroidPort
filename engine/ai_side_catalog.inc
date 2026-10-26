// ============================================================================
// [G-64] Side-correct AI catalogs [RE: rulesmd [AI] Build* lists + the
// per-building AIBasePlanningSide= / Owner= / RequiredHouses= /
// ForbiddenHouses= keys; ai-base.md §2]. Included by main.cpp just before
// make_ai_config (it needs roster_index); everything here is file-local.
//
// The [AI] lists name EVERY side's candidate (BuildPower=NAPOWR,GAPOWR,YAPOWR,
// BuildRadar=GAAIRC,NARADR,AMRADR,NAPSIS, ...); the AI base planner keeps the
// entries its house may own: AIBasePlanningSide (0 = Good/Allied, 1 = Evil/
// Soviet) must match, the house's country must be in Owner= (if present) and
// in RequiredHouses= (if present) and NOT in ForbiddenHouses= (GAAIRC is
// ForbiddenHouses=Americans -> the Americans' radar is AMRADR). For the
// Soviet/Russians house this resolves to exactly the previously hardcoded
// NACNST/NAPOWR/NAREFN/NAHAND/NAWEAP/NARADR set, so the AI gates (all
// Soviet) are unchanged by it.
// ============================================================================

namespace aiside {

// Comma list with the INI's trailing ';' comment stripped; tokens trimmed and
// upper-cased.
inline std::vector<std::string> csv_tokens(std::string v) {
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v.erase(sc);
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        std::size_t a = cur.find_first_not_of(" \t\r\n");
        std::size_t b = cur.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) {
            std::string t = cur.substr(a, b - a + 1);
            for (char& c : t) c = (char)std::toupper((unsigned char)c);
            out.push_back(t);
        }
        cur.clear();
    };
    for (char c : v) {
        if (c == ',') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

inline bool list_has(const std::vector<std::string>& l, const std::string& tok) {
    std::string u = tok;
    for (char& c : u) c = (char)std::toupper((unsigned char)c);
    for (const std::string& s : l)
        if (s == u) return true;
    return false;
}

// 0 = Allied ("GDI"/Good), 1 = Soviet ("Nod"/Evil) -- AIBasePlanningSide's
// encoding (rulesmd: "0 for Good, 1 for Evil").
inline int planning_side(ra2engine::World::Side s) {
    return s == ra2engine::World::Side::Yuri ? 2 : s == ra2engine::World::Side::Soviet ? 1 : 0;
}

// The lobby slot's country when it belongs to the slot's side per rulesmd
// [Sides] (GDI=British,French,Germans,Americans,Alliance / Nod=Russians,...);
// otherwise (blank, or a lobby that cycled the side but kept the country --
// e.g. side Allied + "Russians") the side's default country.
inline std::string house_country(ra2engine::World::Side side, const std::string& slot_country,
                                 const Rules* rules = nullptr) {
    const bool soviet = side == ra2engine::World::Side::Soviet;
    const bool yuri = side == ra2engine::World::Side::Yuri;
    const std::string def = yuri ? "YuriCountry" : soviet ? "Russians" : "Americans";
    if (slot_country.empty()) return def;
    if (rules) {
        const std::vector<std::string> members =
            csv_tokens(rules->section_string("Sides", yuri ? "ThirdSide" : soviet ? "Nod" : "GDI", ""));
        if (!members.empty() && !list_has(members, slot_country)) return def;
    }
    return slot_country;
}

// May a house of (side, country) own technotype `id`? (buildings AND units)
inline bool owned_by(const Rules& rules, const std::string& id, ra2engine::World::Side side,
                     const std::string& country) {
    const std::string ps = rules.section_string(id, "AIBasePlanningSide", "");
    if (!csv_tokens(ps).empty() && std::atoi(csv_tokens(ps)[0].c_str()) != planning_side(side))
        return false;
    const std::vector<std::string> owner = csv_tokens(rules.section_string(id, "Owner", ""));
    if (!owner.empty() && !list_has(owner, country)) return false;
    const std::vector<std::string> req = csv_tokens(rules.section_string(id, "RequiredHouses", ""));
    if (!req.empty() && !list_has(req, country)) return false;
    if (list_has(csv_tokens(rules.section_string(id, "ForbiddenHouses", "")), country)) return false;
    return true;
}

// First entry of an [AI]/[General] list this house may own (art-guarded via
// `ok_index`; an owned-but-art-missing entry is dropped, never substituted).
template <class OkIndex>
int pick_from_list(const Rules& rules, const char* section, const char* key,
                   ra2engine::World::Side side, const std::string& country, OkIndex ok_index) {
    for (const std::string& id : csv_tokens(rules.section_string(section, key, "")))
        if (owned_by(rules, id, side, country)) return ok_index(id.c_str());
    return -1;
}

// "<Side>BaseDefenseCounts=25,22,6 ;h,m,e" indexed by house+0x184 (hardest
// first) [ai-base.md §2; G-63]. -1 when absent.
inline int defense_count(const Rules& rules, ra2engine::World::Side side, int class_index) {
    const char* key = side == ra2engine::World::Side::Yuri ? "ThirdBaseDefenseCounts" : side == ra2engine::World::Side::Soviet ? "SovietBaseDefenseCounts"
                                                             : "AlliedBaseDefenseCounts";
    const std::vector<std::string> v = csv_tokens(rules.section_string("General", key, ""));
    if (class_index < 0 || class_index >= (int)v.size()) return -1;
    return std::atoi(v[(std::size_t)class_index].c_str());
}

}  // namespace aiside

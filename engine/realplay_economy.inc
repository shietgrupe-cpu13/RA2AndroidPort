// realplay_economy.inc -- gap owner A (economy / production / base) for the
// REAL interactive game (GameScene + launch_skirmish). Included into main.cpp
// once, just before `class GameScene`, so everything here sees the asset
// loaders above it and GameScene/launch_skirmish can call it. Kept out of
// main.cpp's body so six parallel fixers do not collide.
//
// Gap rows: docs/gap/02-economy-production.md, GAP-ANALYSIS.md G-20..G-31.

// G-20 [RE: economy.md §1-§4, naval.md §1]: everything launch_skirmish must
// open so the real game's World has a working economy: the full set_economy
// (types_ non-empty -> ore growth/spread + harvester FSM step), the rules
// [General] repair/sell scalars, and the water passability grid (Naval Yard
// placement + ship movement, G-24).
void real_play_arm_economy(ra2engine::World& world, const BuildingAssets& ba, const Rules& rules,
                           Assets& a) {
    wire_economy(world, ba, rules);
    world.set_water_terrain(resolve_land_types(a.map, a.theater, world.grid_w(), world.grid_h()));
}

// ---------------------------------------------------------------------------
// G-22 / G-26 / G-19 / row 15 / row 18: the side-correct, country-correct,
// lobby-TechLevel-correct producible roster for the human in real play.
// ---------------------------------------------------------------------------

std::string rp_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Upper-cased, trimmed comma list of rules [id] key (inline ';' stripped by
// Rules::section_string).
std::vector<std::string> rp_list(const Rules& rules, const std::string& id, const char* key) {
    std::vector<std::string> out;
    const std::string v = rules.section_string(id, key, "");
    std::string cur;
    auto flush = [&] {
        std::size_t a = 0, b = cur.size();
        while (a < b && std::isspace(static_cast<unsigned char>(cur[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(cur[b - 1]))) --b;
        if (b > a) out.push_back(rp_upper(cur.substr(a, b - a)));
        cur.clear();
    };
    for (char c : v) {
        if (c == ',') flush();
        else cur += c;
    }
    flush();
    return out;
}
bool rp_has(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), rp_upper(x)) != v.end();
}
bool rp_flag(const Rules& rules, const std::string& id, const char* key) {
    const std::string v = rules.section_string(id, key, "");
    return !v.empty() && (v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' || v[0] == '1');
}

// [RE: roster.md §1 "Side is from Owner="; RE-FALLBACK RE-E8: the
// HouseTypeClass Owner bitmask + RequiredHouses/ForbiddenHouses binary checks
// were not hunted -- the semantics below are the documented rules meaning]
// May `country` build rules type `id`? Owner= must list the country (an
// absent Owner= list allows everyone); RequiredHouses=, when present, must
// list it; ForbiddenHouses= must not.
bool rp_country_allows(const Rules& rules, const std::string& id, const std::string& country) {
    const std::vector<std::string> owner = rp_list(rules, id, "Owner");
    if (!owner.empty() && !rp_has(owner, country)) return false;
    const std::vector<std::string> req = rp_list(rules, id, "RequiredHouses");
    if (!req.empty() && !rp_has(req, country)) return false;
    return !rp_has(rp_list(rules, id, "ForbiddenHouses"), country);
}

// Supported RA2/YR [SuperWeaponTypes] (rules.ini/rulesmd.ini):
// MultiSpecial, IronCurtainSpecial, LightningStormSpecial, ChronoSphereSpecial,
// ChronoWarpSpecial, ParaDropSpecial, AmericanParaDropSpecial, and the three
// implemented Yuri specials. SpyPlane and ForceShield remain unsupported.
bool rp_supported_sw_type(const std::string& type_upper) {
    return type_upper == "MULTIMISSILE" || type_upper == "IRONCURTAIN" ||
           type_upper == "LIGHTNINGSTORM" || type_upper == "CHRONOSPHERE" ||
           type_upper == "CHRONOWARP" || type_upper == "PARADROP" || type_upper == "AMERPARADROP" ||
           type_upper == "PSYCHICDOMINATOR" || type_upper == "GENETICCONVERTER" ||
           type_upper == "PSYCHICREVEAL";
}

// The supported SW section a building grants ("" when unsupported).
std::string rp_building_supported_sw(const Rules& rules, const std::string& id) {
    const std::string sw = rules.section_string(id, "SuperWeapon", "");
    if (sw.empty()) return "";
    return rp_supported_sw_type(rp_upper(rules.section_string(sw, "Type", ""))) ? sw : "";
}

// [SK-27] W2-AN-18: a structure the lobby's Superweapons checkbox removes:
// it grants a supported superweapon whose [<Special>] DisableableFromShell=yes
// (Nuke, Iron Curtain, Lightning Storm, Chronosphere). AmerParaDrop
// (AMRADR) is DisableableFromShell=no; unsupported SW keys (SpyPlane,
// ForceShield) do not count -- so the radars stay.
bool rp_is_lobby_sw_building(const Rules& rules, const std::string& id) {
    const std::string sw = rp_building_supported_sw(rules, id);
    if (sw.empty()) return false;
    return rp_flag(rules, sw, "DisableableFromShell");
}

// The human's country: the lobby slot's country when its [Country] Side=
// matches the slot's side (GDI=Allied, Nod=Soviet), else the side's stock
// default (Americans / Russians). The lobby's side button changes only the
// side, so a stale "Americans" on a Soviet slot must not leak through.
std::string rp_player_country(const Rules& rules, const ra2engine::SkirmishSlot& s) {
    const std::string want = ra2engine::faction_rules_side(s.side);
    if (!s.country.empty() && rp_upper(rules.section_string(s.country, "Side", "")) == want)
        return s.country;
    return ra2engine::faction_country(s.side);
}

// One unit queue category per rules type [RE: roster2.md TL;DR 7;
// production.md §1.1]: [InfantryTypes] -> Infantry; [AircraftTypes] ->
// Aircraft; [VehicleTypes] Naval=yes -> Naval; else Vehicle.
ra2engine::QueueCat rp_unit_cat(const ra2engine::UnitTypeDef& d) {
    if (d.aircraft) return ra2engine::QueueCat::Aircraft;
    if (d.naval) return ra2engine::QueueCat::Naval;
    return d.is_vehicle ? ra2engine::QueueCat::Vehicle : ra2engine::QueueCat::Infantry;
}

// The REAL-PLAY player roster: every [InfantryTypes]/[VehicleTypes]/
// [AircraftTypes] entry this country may own, at or below the lobby
// TechLevel, not stolen-tech-only, then the shared art/cost/cameo guards of
// load_unit_roster_table. Prerequisites stay full-parsed (the sidebar shows
// an item once they are met). Aircraft are included only when
// `with_aircraft` (their producer is wired).
UnitRoster load_player_unit_roster(const std::string& dir, const ra2assets::MixFile& ra2,
                                   const UnitAssets& ua, const Rules& rules, const Art& art,
                                   const Ini& rules_ini, const std::string& country,
                                   int lobby_tech, bool with_aircraft,
                                   const std::vector<std::string>* allowed_upper = nullptr) {
    std::vector<std::string> ids;
    std::vector<char> veh;
    std::vector<char> air;
    const struct { const char* list; bool vehicle; bool aircraft; } kLists[] = {
        {"InfantryTypes", false, false}, {"VehicleTypes", true, false},
        {"AircraftTypes", true, true}};
    for (const auto& L : kLists) {
        if (L.aircraft && !with_aircraft) continue;
        for (const std::string& id : ra2engine::ini_list_values(rules_ini, L.list)) {
            const int tl = rules.tech_level(id);
            if (tl <= 0 || tl > lobby_tech) continue;              // [RE: skirmish.md §1 Tech Level]
            if (!rp_country_allows(rules, id, country)) continue;   // side + country units
            // Wave-5 (R-042): RequiresStolen*Tech types stay in the roster;
            // World::apply_stolen_tech_gate keeps them unbuildable (TechLevel
            // 0) until a spy infiltrates the matching Battle Lab.
            if (std::find(ids.begin(), ids.end(), id) != ids.end()) continue;
            // W2-CA-03: a campaign offers only the types its game knows.
            if (allowed_upper && !allowed_upper->empty() &&
                std::find(allowed_upper->begin(), allowed_upper->end(), rp_upper(id)) ==
                    allowed_upper->end())
                continue;
            ids.push_back(id);
            veh.push_back(L.vehicle ? 1 : 0);
            air.push_back(L.aircraft ? 1 : 0);
        }
    }
    std::vector<UnitRosterEnt> ents;
    ents.reserve(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i)
        ents.push_back(UnitRosterEnt{ids[i].c_str(), veh[i] != 0, ""});
    UnitRoster ur = load_unit_roster_table(dir, ra2, ua, rules, art, ents.data(), ents.size(),
                                           /*full_parse=*/true);
    for (ra2engine::UnitTypeDef& d : ur.defs) {
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == d.id) d.aircraft = air[i] != 0;
        d.naval = d.is_vehicle && !d.aircraft && rp_flag(rules, d.id, "Naval");
        d.harvester = rules.harvester(d.id);
        if (d.harvester) {
            d.storage = rules.storage(d.id);
            d.chrono_miner = rp_upper(d.id) == "CMIN";   // [RE: economy.md §3] Teleporter miner
        }
        rp_arm_stolen_tech(rules, d);   // wave-5 (R-042)
    }
    std::cerr << "[realplay-roster] country=" << country << " tech<=" << lobby_tech
              << " producible=" << ur.defs.size() << "\n";
    return ur;
}

// Roster indices of the buildings whose rules Factory= builds `factory` (and,
// for UnitType, whose Naval= equals `naval`) [RE: roster2.md TL;DR 7].
std::vector<int> rp_factory_types(const BuildingAssets& ba, const Rules& rules,
                                  const char* factory, bool naval = false) {
    std::vector<int> v;
    for (int i = 0; i < static_cast<int>(ba.roster.size()); ++i) {
        const std::string& id = ba.roster[i].id;
        if (rp_upper(rules.section_string(id, "Factory", "")) != rp_upper(factory)) continue;
        if (std::string(factory) == "UnitType" && rp_flag(rules, id, "Naval") != naval) continue;
        v.push_back(i);
    }
    return v;
}

// ---------------------------------------------------------------------------
// G-25 / G-29: superweapons come from their buildings, charge only while
// powered (IsPowered), and go away with the building.
// ---------------------------------------------------------------------------
struct RpSwGrant {
    ra2engine::SuperweaponType type = ra2engine::SuperweaponType::None;
    bool is_powered = false;
    double recharge_secs = 0.0;
    bool disableable = false;   // DisableableFromShell=yes (the lobby checkbox)
    double radius = 15.0; // Cell radius for area-targeted Yuri powers
};

// Supported SW Type= names -> the engine enum.
ra2engine::SuperweaponType rp_sw_type_of(const std::string& type_upper) {
    using T = ra2engine::SuperweaponType;
    if (type_upper == "MULTIMISSILE") return T::Nuke;
    if (type_upper == "IRONCURTAIN") return T::IronCurtain;
    if (type_upper == "LIGHTNINGSTORM") return T::LightningStorm;
    if (type_upper == "CHRONOSPHERE") return T::ChronoSphere;
    if (type_upper == "CHRONOWARP") return T::ChronoWarp;
    if (type_upper == "PARADROP") return T::ParaDrop;
    if (type_upper == "AMERPARADROP") return T::AmerParaDrop;
    if (type_upper == "PSYCHICDOMINATOR") return T::PsychicDominator;
    if (type_upper == "GENETICCONVERTER") return T::GeneticConverter;
    if (type_upper == "PSYCHICREVEAL") return T::PsychicReveal;
    return T::None;
}

// Per roster building: the supported SW its SuperWeapon= key grants [RE:
// superweapon.md §1/§4 -- [<Special>] Type= names the effect, RechargeTime=
// is minutes, IsPowered= the low-power pause].
std::vector<RpSwGrant> rp_superweapon_table(const BuildingAssets& ba, const Rules& rules) {
    std::vector<RpSwGrant> t(ba.roster.size());
    for (std::size_t i = 0; i < ba.roster.size(); ++i) {
        const std::string sw = rules.section_string(ba.roster[i].id,"SuperWeapon","");
        if (sw.empty()) continue;
        t[i].type = rp_sw_type_of(rp_upper(rules.section_string(sw, "Type", "")));
        if (t[i].type==ra2engine::SuperweaponType::None) {
            ra2engine::diag::note("CAPABILITY_UNSUPPORTED building=%s superweapon=%s type=%s",ba.roster[i].id.c_str(),sw.c_str(),rules.section_string(sw,"Type","").c_str());
            continue;
        }
        double default_radius = 15.0;
        if (t[i].type == ra2engine::SuperweaponType::PsychicReveal)
            default_radius = rules.section_double("CombatDamage", "PsychicRevealRadius", 15.0);
        else if (t[i].type == ra2engine::SuperweaponType::PsychicDominator)
            default_radius = rules.section_double(
                sw, "Range", rules.section_double(sw, "DominatorCaptureRange",
                    rules.section_double("CombatDamage", "PsychicDominatorRadius", 15.0)));
        else if (t[i].type == ra2engine::SuperweaponType::GeneticConverter)
            default_radius = rules.section_double(
                sw, "Range", rules.section_double("CombatDamage", "GeneticConverterRadius", 5.0));
        t[i].radius=std::clamp(default_radius,0.0,256.0);
        t[i].is_powered = rp_flag(rules, sw, "IsPowered");
        t[i].recharge_secs = rules.section_double(sw, "RechargeTime", 5.0) * 60.0;
        t[i].disableable = rp_flag(rules, sw, "DisableableFromShell");
    }
    return t;
}

// Once per frame, every house: hold one SW per type granted by an own
// operational (Idle) building -- granted (fresh, full RechargeTime) when first
// owned, revoked when the last such building is gone (W2-AN-04: a house may
// hold several types at once). The grant starts at the building's grand
// opening (Buildup -> Idle).
void rp_sync_superweapons(ra2engine::World& w, const std::vector<RpSwGrant>& table,
                          const SuperweaponParams& nuke) {
    std::vector<const RpSwGrant*> held;
    for (int h = 0; h < w.house_count(); ++h) {
        held.clear();
        for (const ra2engine::Building& b : w.buildings()) {
            if (!b.alive || b.house != h || b.state != ra2engine::Building::State::Idle) continue;
            if (b.type < 0 || b.type >= static_cast<int>(table.size())) continue;
            const RpSwGrant& g = table[b.type];
            if (g.type == ra2engine::SuperweaponType::None) continue;
            bool dup = false;
            for (const RpSwGrant* o : held) dup |= o->type == g.type;
            if (!dup) held.push_back(&g);
        }
        for (ra2engine::SuperweaponType t : w.sw_types(h)) {
            bool keep = false;
            for (const RpSwGrant* g : held) keep |= g->type == t;
            if (!keep) w.revoke_superweapon_type(h, t);
        }
        for (const RpSwGrant* g : held) {
            const bool area_yuri = g->type == ra2engine::SuperweaponType::PsychicReveal ||
                                   g->type == ra2engine::SuperweaponType::PsychicDominator ||
                                   g->type == ra2engine::SuperweaponType::GeneticConverter;
            const double spread = area_yuri ? g->radius : nuke.cell_spread;
            w.grant_superweapon_type(h, g->type, g->recharge_secs, g->is_powered, nuke.damage,
                                     spread, nuke.percent_at_max, nuke.verses);
        }
    }
}

// --- L2 multi-human debug switch -------------------------------------------
// `--local-house N` / `--humans 0,1` on the command line, or `set local_house
// N` / `set humans 0,1` in a --play script: the listed skirmish slots become
// human houses (is_ai=false -> a World player economy each), and the scene
// plays house N (sidebar, radar, shroud, EVA, orders). Default: nothing
// changes (slot 0 is the one human, the scene plays house 0).
std::vector<int> g_debug_humans;
int g_debug_local_house = -1;
void apply_debug_humans(ra2engine::SkirmishConfig& cfg) {
    if (!cfg.campaign_map.empty()) return;
    for (int sl : g_debug_humans)
        if (sl >= 0 && sl < static_cast<int>(cfg.slots.size()) && cfg.slots[(std::size_t)sl].combatant)
            cfg.slots[(std::size_t)sl].is_ai = false;
}
// The house this client plays: the debug override when it names a human
// combatant slot, else house 0 (the campaign player is always house 0).
int g_net_local_house_value();   // NET-PLAY (net_play.inc)
int local_slot_of(const ra2engine::SkirmishConfig& cfg) {
    if (!cfg.campaign_map.empty() || cfg.slots.empty()) return 0;
    // NET-PLAY: the networked match being built plays the lobby's local house.
    if (const int n = g_net_local_house_value(); n >= 0 && n < static_cast<int>(cfg.slots.size())) return n;
    const int l = g_debug_local_house;
    if (l >= 0 && l < static_cast<int>(cfg.slots.size()) && cfg.slots[(std::size_t)l].combatant &&
        !cfg.slots[(std::size_t)l].is_ai)
        return l;
    return 0;
}
// Human houses of a config: every combatant non-AI slot (the campaign: 0).
std::vector<int> human_slots_of(const ra2engine::SkirmishConfig& cfg) {
    if (!cfg.campaign_map.empty() || cfg.slots.empty()) return {0};
    std::vector<int> v;
    for (int i = 0; i < static_cast<int>(cfg.slots.size()); ++i)
        if (cfg.slots[(std::size_t)i].combatant && !cfg.slots[(std::size_t)i].is_ai) v.push_back(i);
    return v;
}

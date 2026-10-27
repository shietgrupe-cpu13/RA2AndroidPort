// P7 Save / Load [RE: docs/re/saveload.md; spec 2026-10-06-saveload-design.md].
#include "doctest.h"

#include <chrono>
#include <fstream>
#include <regex>
#include <sstream>
#include <set>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "../engine/save_fields.hpp"
#include "../engine/savegame.hpp"
#include "../engine/net_frame_crc.hpp"
#include "asset_fixture.hpp"
#include "ra2assets/ini.hpp"

using namespace ra2engine;
using namespace ra2engine::save;

// ===========================================================================
// Archive core
// ===========================================================================

namespace {
struct Toy {
    int a = 0;
    double b = 0.0;
    std::string s;
    std::vector<int> v;
    bool flags[3] = {false, false, false};
    std::map<std::string, int> m;
    long l = 0;
    std::uint8_t u8 = 0;
};
}  // namespace

namespace ra2engine {
namespace save {
template <class Ar> void fields(Ar& ar, Toy& t) {
    io(ar, t.a); io(ar, t.b); io(ar, t.s); io(ar, t.v); io(ar, t.flags);
    io(ar, t.m); io(ar, t.l); io(ar, t.u8);
}
}  // namespace save
}  // namespace ra2engine

TEST_CASE("saveload: archive round-trips scalars, strings, containers bit-exactly") {
    Toy t;
    t.a = -123456;
    t.b = 0.1 + 0.2;          // a non-representable sum: bit-exact or bust
    t.s = std::string("desc\0x", 6);
    t.v = {1, -2, 3};
    t.flags[1] = true;
    t.m = {{"GI", 4}, {"E2", -9}};
    t.l = -7;
    t.u8 = 250;
    std::vector<std::uint8_t> b = to_bytes(t);
    Toy u;
    REQUIRE(from_bytes(b, u));
    CHECK(u.a == t.a);
    CHECK(std::memcmp(&u.b, &t.b, sizeof(double)) == 0);
    CHECK(u.s == t.s);
    CHECK(u.v == t.v);
    CHECK(u.flags[1]);
    CHECK_FALSE(u.flags[0]);
    CHECK(u.m == t.m);
    CHECK(u.l == -7);
    CHECK(u.u8 == 250);
    CHECK(to_bytes(u) == b);
}

TEST_CASE("saveload: truncated or corrupt input fails cleanly") {
    Toy t;
    t.v.assign(100, 5);
    std::vector<std::uint8_t> b = to_bytes(t);
    for (std::size_t cut : {std::size_t(0), std::size_t(3), b.size() / 2, b.size() - 1}) {
        std::vector<std::uint8_t> c(b.begin(), b.begin() + static_cast<long>(cut));
        Toy u;
        CHECK_FALSE(from_bytes(c, u));
    }
    std::vector<std::uint8_t> huge = b;
    huge[8 + 8 + 8] = 0xFF;   // the string's count byte region: absurd lengths refuse
    Toy u;
    (void)from_bytes(huge, u);  // must not crash / allocate wildly
    CHECK(aggregate_arity<Toy>() == visited_fields<Toy>());
}

// ===========================================================================
// Field-coverage tripwires: adding a member to any of these structs without
// adding it to its visitor in engine/save_fields.hpp FAILS here.
// ===========================================================================

#define CHECK_ARITY(T)                                                              \
    do {                                                                            \
        INFO("struct " #T ": add the new member to its visitor in save_fields.hpp " \
             "and bump save::kFormatVersion");                                      \
        CHECK(aggregate_arity<T>() == visited_fields<T>());                         \
    } while (0)

TEST_CASE("saveload: every serialized aggregate's visitor covers all its members") {
    CHECK_ARITY(Gattling);
    CHECK_ARITY(DrainType);
    CHECK_ARITY(DrainLink);
    CHECK_ARITY(DrainState);
    CHECK_ARITY(ChaosStatus);
    CHECK_ARITY(ChaosState);
    CHECK_ARITY(WaypointRoute);CHECK_ARITY(SpawnLaunchProfile);CHECK_ARITY(DiskShot);CHECK_ARITY(DiskState);CHECK_ARITY(DiskEvent);
    CHECK_ARITY(Unit);
    CHECK_ARITY(QueueBacklogSave);   // R-180
    CHECK_ARITY(Building);
    CHECK_ARITY(BuildingTypeDef);
    CHECK_ARITY(UnitCombat);
    CHECK_ARITY(UnitTypeDef);
    CHECK_ARITY(ProjectileSpec);
    CHECK_ARITY(ProjectileRules);
    CHECK_ARITY(ProjRng);
    CHECK_ARITY(OreRng);
    CHECK_ARITY(OreMine);   // W2-SK-01
    CHECK_ARITY(HarvestScan);   // W2-SK-02
    CHECK_ARITY(Projectile);
    CHECK_ARITY(ProjectileEvent);
    CHECK_ARITY(CargoEvent);
    CHECK_ARITY(World::DeployEvent);   // gF
    CHECK_ARITY(Cellxy);
    CHECK_ARITY(SeqEntry);
    CHECK_ARITY(UnitAnimSeq);
    CHECK_ARITY(InfAnimState);   // W3-B
    CHECK_ARITY(InfSeqEntry);
    CHECK_ARITY(InfTable);
    CHECK_ARITY(Superweapon);
    CHECK_ARITY(AnType);   // W2-AN
    CHECK_ARITY(AnUnit);
    CHECK_ARITY(SwParams);
    CHECK_ARITY(RocketWarhead);   // W4
    CHECK_ARITY(BldTypeW5);   // wave-5 abilities (agent C)
    CHECK_ARITY(UnitTypeW5);
    CHECK_ARITY(BldW5);
    CHECK_ARITY(UnitW5);
    CHECK_ARITY(WorldW5);
    CHECK_ARITY(UnitTypeW6);   // wave-6 #4 (v25)
    CHECK_ARITY(UnitW6);
    CHECK_ARITY(BldW6);
    CHECK_ARITY(BldTypeW6);
    CHECK_ARITY(WallOvType);
    CHECK_ARITY(UnitProfileW5);
    CHECK_ARITY(RulesW5);
    CHECK_ARITY(UnitTypeTgt);   // W6 fix #2 (save v29)
    CHECK_ARITY(UnitTgt);
    CHECK_ARITY(UnitTypeW7);   // wave-7 fix (units) (v35)
    CHECK_ARITY(UnitW7);
    CHECK_ARITY(BldW7);
    CHECK_ARITY(BldTgt);
    CHECK_ARITY(PlayfieldW6);
    CHECK_ARITY(RocketBlock);
    CHECK_ARITY(RocketParams);
    CHECK_ARITY(RocketFlight);
    CHECK_ARITY(SwStrike);
    CHECK_ARITY(SwEvent);
    CHECK_ARITY(SaveAccess::AnWorld);
    CHECK_ARITY(RepairSellRules);
    CHECK_ARITY(World::HouseRec);
    CHECK_ARITY(World::EnteredEvent);   // W2-CA-01
    CHECK_ARITY(CampaignHostSave);   // P10
    CHECK_ARITY(QueueSlot);
    CHECK_ARITY(ProductionState);
    CHECK_ARITY(HouseEcon);   // L2 (v32)
    CHECK_ARITY(DifficultyMods);
    CHECK_ARITY(AiRules);
    CHECK_ARITY(PlannedNode);
    CHECK_ARITY(AiBaseState);
    CHECK_ARITY(AiCatalog);
    CHECK_ARITY(TeamInstance);
    CHECK_ARITY(AiTeamState);
    CHECK_ARITY(AiTeamRules);
    CHECK_ARITY(TaskForceMember);
    CHECK_ARITY(TaskForce);
    CHECK_ARITY(ScriptAction);
    CHECK_ARITY(ScriptType);
    CHECK_ARITY(TeamType);
    CHECK_ARITY(TriggerObject);
    CHECK_ARITY(TriggerComparator);
    CHECK_ARITY(AITriggerType);
    CHECK_ARITY(AimdData);
    CHECK_ARITY(AiTriggerState);
    CHECK_ARITY(AiTeamConfig);
    CHECK_ARITY(AiHouseConfig);
    CHECK_ARITY(SkirmishSlot);
    CHECK_ARITY(SkirmishConfig);
    CHECK_ARITY(SceneSave);
    // SaveHeader::version is written OUTSIDE the header block (the magic+version
    // prefix every reader must parse first), so its visitor skips exactly it.
    CHECK(aggregate_arity<SaveHeader>() == visited_fields<SaveHeader>() + 1);
    // P5 veterancy + crates
    CHECK_ARITY(VeterancyRules);
    CHECK_ARITY(WeaponStats);
    CHECK_ARITY(UnitVet);
    CHECK_ARITY(PowerupDef);
    CHECK_ARITY(CrateRules);
    CHECK_ARITY(World::Crate);
    CHECK_ARITY(World::CrateEvent);
    CHECK_ARITY(World::VetEvent);
    // P4 abilities
    CHECK_ARITY(WeaponSpec);
    CHECK_ARITY(AbilityProfile);
    CHECK_ARITY(AbilityRules);
    CHECK_ARITY(RadSite);
    CHECK_ARITY(World::AbilityEvent);
    // Private nested aggregates (through the SaveAccess aliases).
    CHECK_ARITY(SaveAccess::HouseAiRec);
    CHECK_ARITY(SaveAccess::HouseSuperweapon);
    CHECK_ARITY(SaveAccess::TechIncome);
    CHECK_ARITY(SaveAccess::PendingHarvestEvent);
    CHECK_ARITY(SaveAccess::Trig);
    CHECK_ARITY(SaveAccess::Tag);
    // W9-05: the visitor structs that had no tripwire.
    CHECK_ARITY(HitRec);
    CHECK_ARITY(LocoRules);
    CHECK_ARITY(LocoType);
    CHECK_ARITY(SonicWave);
    CHECK_ARITY(UnitAi);
    CHECK_ARITY(UnitLoco);
    CHECK_ARITY(WeaponSlot);
    CHECK_ARITY(World::CombatW7Rules);
    CHECK_ARITY(World::CrushRules);
    CHECK_ARITY(World::DepthRules);
    CHECK_ARITY(World::ElevationRules);
    CHECK_ARITY(World::LandSpeedRules);
    CHECK_ARITY(World::FireEvent);
    CHECK_ARITY(World::FragEvent);
    CHECK_ARITY(World::GarrisonEvent);
    CHECK_ARITY(PlayerEconRules);   // hashed only (econ_rules_digest)
}

// Non-aggregate classes cannot be counted; their sizeof is pinned on the
// desktop MinGW x64 / libstdc++ ABI (other ABIs skip). A new member changes it
// -- update the visitor in save_fields.hpp, then this number. (A lone bool can
// hide in padding; the round-trip determinism test below covers behavior.)
TEST_CASE("saveload: private-state classes' sizeof is pinned (World et al.)") {
#if defined(__MINGW64__) && defined(__GLIBCXX__) && defined(__x86_64__)
    INFO("a class with private save-state changed size: add the member to "
         "SaveAccess in engine/save_fields.hpp, bump kFormatVersion, re-pin");
    CHECK(sizeof(World) == 7936 + sizeof(DrainState) + sizeof(ChaosState) + sizeof(DiskState) + sizeof(std::map<int,WaypointRoute>) + sizeof(std::map<std::string,SpawnLaunchProfile>)); // v53: embedded ProjectileSpec launch weapon identity (+32), visited via existing ability rules.   // W9-05 econ_rules_digest_ (+24, hashed, not saved); W9-02-01 Occupancy claim index claims_/claims_dirty_ (+32, derived, NOT saved); w9fix-match diplomacy rules / game_over_ / observer_reveal_ / victory_defeats_seen_ (+8), save v46; w8fix-place build_static_ (+24, save v45) + build_mask_ scratch (+48, not saved); w8fix-layer: Occupancy deck plane owner_d_/spots_d_/lay_ (+72), save v42; fv7c (+120): ai_protect_reqs_ removed (-24), campaign_team_flags_ + team_flags_ (+48), beach_ (+24), house_repair_delay_/clock_ (+48), CombatW7Rules base_defense_delay/iq_repair_sell/ai_credit_reserve (+24), save v40; w7fix-move loco_rules_ (+48), save v39; w7fix-combat w7_rules_/house_iq_/w7_hits_ (+152), save v39; W7 pres rad/nuke/storm_inf_death_ + frag_events_ (+40);  W7 AI transient ai_protect_reqs_ (+24);  W7 air/naval: AnWorld sonic waves + SwParams C4Warhead (+64), save v36; wave-7 fix (units) side_disguise_ + infantry_blink_disguise_ (+40), save v35; L2 per-house economy econ_/econ_rules_/econ_rules_set_/human_mask_/victory_cheered_/house_outcome_/order_issuer_/econ_launches_ (+160) minus wallet_hint_ (-24), save v32; R-210 deck_land_/deck_amph_/deck_ver_ derived deck grids (+56), save v30; W6 fix #2 usable area playfield_ + per-house handicap vectors (+96), save v29; wave-6 #4: map wall overlay types/stages/kill list (+96) and SwParams storm/nuke wall keys (+8), WeaponSpec Burst/OmniFire/AffectsAllies/ProneDamage/InfDeath/wall keys in the ability rules (+24), save v25; P4 crushable walls crush_wall_ + derived crush grids (+72) and WeaponSpec delay-kill fields in the ability rules (+16), save v23; W5-A wall leak mask + AI SW quarry (+48), save v22; W5-B sub-cells/land speed/crush/elevation rules (+600), save v20; wave-5 abilities: w5_ (stolen tech, rules, unit profiles, witnesses) + gap scratch (+344), save v17; W4-SQ player-order queue + passive houses + high-bridge state + scripted-team AI demand (+128), save v16; W4 per-type RocketParams in SwParams (+824) + ProjectileRules homing keys + proj_seq_ (+24), save v15; W3-B infantry anim switches + tables (+64), save v14; gap C + gF MCV deploy (3744) + G16 garrison/bridge state (+128), save v7; W2-AN air/naval/SW (+648) + W2-GA-03 fog flag (in padding), save v10; w2fix-campaign entered events + pair alliances (+56), save v12; W2-SK harvest scan/income, wallet hints, depot/AI state (+96), save v13
    CHECK(sizeof(MapTriggerSystem) == 408);   // W6-05-04 +timer_text_ (save v25); P10: +campaign_mode_/semi_detached_
    CHECK(sizeof(OreField) == 128);   // W2-SK-01 +mines_ (v10)
    CHECK(sizeof(Occupancy) == 184);   // W9-02-01 claims_ + claims_dirty_ (+32, derived, rebuilt after a load, NOT saved); w8fix-layer +owner_d_ +spots_d_ +lay_ (v42); W5-B +spots_ +inf_ (v20)
    CHECK(sizeof(ProductionQueues) == 336);
    CHECK(sizeof(GameRandom) == 1008);
    CHECK(sizeof(RadField) == 56);
#else
    MESSAGE("sizeof pins are only checked on the MinGW x64 desktop ABI");
#endif
}

// Source-level tripwire for classes whose members cannot be counted: every
// data member named `x_` declared in the class body must be visited by the
// SaveAccess visitor (as `<obj>.x_`). Catches even a lone bool hiding in
// padding. Needs the source tree (RA2_SOURCE_DIR); skips without it.
namespace {
std::string slurp_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::set<std::string> class_members(const std::string& src, const std::string& head) {
    std::set<std::string> out;
    std::string s;
    for (char ch : src)
        if (ch != '\r') s += ch;   // CRLF checkouts
    const std::size_t b = s.find(head);
    if (b == std::string::npos) return out;
    const std::size_t e = s.find("\n};", b);
    std::string body = s.substr(b, e == std::string::npos ? std::string::npos : e - b);
    body = std::regex_replace(body, std::regex("//[^\\n]*"), "");
    const std::regex mem(R"(\b([a-z_0-9]+_)\s*(=[^;()]*)?;)");
    for (std::sregex_iterator it(body.begin(), body.end(), mem), end; it != end; ++it)
        out.insert((*it)[1].str());
    return out;
}
}  // namespace

TEST_CASE("saveload: every World / MapTriggerSystem data member is visited (source scan)") {
#ifdef RA2_SOURCE_DIR
    const std::string root = RA2_SOURCE_DIR;
    const std::string fields = slurp_text(root + "/engine/save_fields.hpp");
    REQUIRE(!fields.empty());
    struct Case { const char* header; const char* head; const char* obj; std::set<std::string> skip; };
    const Case cases[] = {
        // L2: the per-house production rules are asset-derived (re-installed
        // by the host after a load), the order issuer and the launch events
        // are transient -- deliberately NOT saved.
        // W9-05: econ_rules_ is hashed through econ_rules_digest_ (visited).
        {"/engine/world.hpp", "class World {", "w.",
         {"map_", "econ_rules_", "order_issuer_", "econ_launches_",
          "team_flags_"}},   // fv7c: scratch, rebuilt at the head of every step_missions_w7
        {"/engine/map_trigger.hpp", "class MapTriggerSystem {", "s.", {"d_"}},
        // W9-05: the other SaveAccess classes -- every member is saved AND
        // (visited with save::io) in the sync hash.
        // W9-02-01 (w9fix-speed): the per-unit claim index is derived from the
        // planes (rebuilt after a load: claims_dirty_), neither saved nor hashed.
        {"/engine/occupancy.hpp", "class Occupancy {", "o.", {"claims_", "claims_dirty_"}},
        {"/engine/ore.hpp", "class OreField {", "f.", {}},
        {"/engine/production.hpp", "class ProductionQueues {", "q.", {}},
        {"/engine/crate.hpp", "class GameRandom {", "g.", {}},
        {"/engine/abilities.hpp", "class RadField {", "f.", {}},
    };
    for (const Case& c : cases) {
        const std::set<std::string> m = class_members(slurp_text(root + c.header), c.head);
        // (A brace-initialised lone member, e.g. ProductionQueues::q_{}, is
        // not matched by the scan; the sizeof pins above cover those.)
        if (std::string(c.head).find("World") != std::string::npos ||
            std::string(c.head).find("MapTrigger") != std::string::npos)
            CHECK(m.size() > 5);
        for (const std::string& name : m) {
            if (c.skip.count(name)) continue;
            INFO(c.head << " member " << name << " is not serialized: add `save::io(ar, "
                        << c.obj << name << ");` to SaveAccess in engine/save_fields.hpp");
            CHECK(fields.find(std::string(c.obj) + name + ")") != std::string::npos);
        }
    }
#else
    MESSAGE("RA2_SOURCE_DIR not defined; source scan skipped");
#endif
}

// ===========================================================================
// World round trip
// ===========================================================================
namespace {

UnitCombat gun(const char* img, int dmg, double range, bool vehicle = false, bool ballistic = false) {
    UnitCombat c;
    c.strength = 300;
    c.armor = "none";
    c.image = img;
    c.damage = dmg;
    c.rof = 20;
    c.range = range;
    c.sight = 6;
    c.cost = 600;
    c.verses = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    c.is_vehicle = vehicle;
    if (vehicle) { c.rot = 5; c.turret = true; c.speed_cps = 2.0; }
    if (ballistic) {
        c.proj.kind = ProjKind::Ballistic;
        c.proj.arcing = true;
        c.proj.inaccurate = true;
        c.proj.flak_scatter = true;
        c.proj.weapon_range = static_cast<int>(range * 256);
        c.proj.speed_cap = 102;
        c.proj.flh[2] = 100;
    }
    return c;
}

int free_cell_near(World& w, int x, int y, int& ox, int& oy) {
    for (int r = 0; r <= 12; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (w.walkable(x + dx, y + dy) && w.unit_at_cell(x + dx, y + dy) < 0 &&
                    w.building_at_cell(x + dx, y + dy) < 0) {
                    ox = x + dx; oy = y + dy;
                    return 1;
                }
            }
    return 0;
}

// A World with (nearly) every subsystem's firewall open and live state in it.
void populate(World& w) {
    const ra2assets::MapCell* c = pick_start_cell(w.map());
    REQUIRE(c != nullptr);
    const int bx = c->x, by = c->y;
    w.set_house(0, {World::Side::Allied, 0, "Americans", 0, true});
    w.set_house(1, {World::Side::Soviet, 1, "Russians", 1, true});
    w.set_house(2, {World::Side::Neutral, 2, "Neutral", 2, false});
    w.enable_shroud(true);
    w.enable_victory(true, true);
    w.enable_projectiles(ProjectileRules{});
    w.enable_veterancy(VeterancyRules{});
    {
        // P4: open the abilities firewall; the E2s get a death weapon that
        // seeds radiation, the tanks a parasite-able default profile.
        AbilityProfile deso;
        deso.explodes = true;
        deso.death.valid = true;
        deso.death.damage = 50;
        deso.death.cell_spread = 2.0;
        deso.death.rad_level = 200;
        deso.death.verses.assign(11, 100);
        w.set_ability_rules(AbilityRules{});
        w.register_ability_profile("E2", deso);
        w.register_ability_profile("HTNK", AbilityProfile{});
    }
    w.grant_superweapon(1, SuperweaponType::Nuke, 30.0, 600, 3.0, 0.2,
                        {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100});
    std::vector<BuildingTypeDef> types(2);
    types[0].id = "GAPOWR"; types[0].cost = 800; types[0].strength = 750; types[0].fw = 2;
    types[0].fh = 2; types[0].power = 100; types[0].crewed = true;
    types[1].id = "NAPOWR"; types[1].cost = 600; types[1].strength = 750; types[1].fw = 2;
    types[1].fh = 2; types[1].power = 100;
    w.set_building_types(types);
    UnitCombat crew = gun("E1", 10, 3.0);
    w.set_survivor_crew(0, crew);

    int ox = 0, oy = 0;
    // Two buildings, one being repaired after taking damage.
    int b0 = -1, b1 = -1;
    if (free_cell_near(w, bx - 6, by - 6, ox, oy))
        b0 = w.add_building(0, ox, oy, 1, 1, 0, 750, 0, 0.0, true, 5);
    if (free_cell_near(w, bx + 7, by + 7, ox, oy))
        b1 = w.add_building(1, ox, oy, 1, 1, 1, 750, 0, 2.0, false, 5);
    if (b0 >= 0) {
        w.buildings()[b0].hp = 400;
        w.grant_credits_for_test(0, 5000.0);
        w.set_repair(b0, 0, 1);
    }
    (void)b1;
    // Opposing armies: tanks with arcing scattered shells + infantry.
    for (int k = 0; k < 4; ++k) {
        if (free_cell_near(w, bx - 2, by + k, ox, oy))
            w.add_combat_unit(ox, oy, 0, gun("HTNK", 40, 6.0, true, true));
        if (free_cell_near(w, bx + 3, by + k, ox, oy))
            w.add_combat_unit(ox, oy, 1, gun("E2", 15, 4.0));
    }
    // A selected group order so paths/formations are in flight.
    std::vector<int> own;
    for (std::size_t i = 0; i < w.units().size(); ++i)
        if (w.units()[i].house == 0) own.push_back(static_cast<int>(i));
    w.group_move(ra2engine::kAnyHouse, own, bx + 2, by + 2);

    // Disguise + a pending spy order to keep capture state non-default.
    if (!w.units().empty()) w.set_disguise(static_cast<int>(w.units().size()) - 1, "E1", 0);
}

}  // namespace

TEST_CASE("saveload: rich World round-trips and continues bit-identically") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    const ra2assets::MapFile& map = ra2fixture::mapsmd03_map();
    World a(map);
    populate(a);
    for (int f = 0; f < 45; ++f) a.step(1.0 / 15.0);
    const bool shells = !a.projectiles().empty();
    MESSAGE("projectiles in flight at save: " << a.projectiles().size());

    const std::vector<std::uint8_t> s0 = save_world(a);
    World b(map);
    REQUIRE(load_world(b, s0));
    CHECK(save_world(b) == s0);               // save(load(x)) == x
    CHECK(save_world(a) == s0);               // saving is read-only

    for (int f = 0; f < 90; ++f) {
        a.step(1.0 / 15.0);
        b.step(1.0 / 15.0);
    }
    const std::vector<std::uint8_t> sa = save_world(a), sb = save_world(b);
    CHECK(sa == sb);                          // continued bit-identically
    CHECK(sa != s0);                          // ...and the sim actually moved
    (void)shells;
}

TEST_CASE("saveload: load_world rejects a truncated blob") {
    ra2assets::MapFile map;
    World a(map);
    a.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
    std::vector<std::uint8_t> s = save_world(a);
    s.resize(s.size() / 2);
    World b(map);
    CHECK_FALSE(load_world(b, s));
}

TEST_CASE("saveload: load_world range-checks every index (R-181)") {
    ra2assets::MapFile map;
    World a(map);
    a.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
    a.add_combat_unit(1, 0, 1, gun("E2", 10, 3.0));
    a.units()[0].target = 1;   // a valid reference
    {
        World b(map);
        CHECK(load_world(b, save_world(a)));
    }
    auto refused = [&](auto mutate) {
        World c(map);
        c.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
        c.add_combat_unit(1, 0, 1, gun("E2", 10, 3.0));
        mutate(c);
        World d(map);
        return !load_world(d, save_world(c));
    };
    CHECK(refused([](World& w) { w.units()[0].target = 2; }));            // past the end
    CHECK(refused([](World& w) { w.units()[1].cargo_of = 77; }));
    CHECK(refused([](World& w) { w.units()[0].target_building = 0; }));   // no buildings
    CHECK(refused([](World& w) { w.units()[0].house = 1000000; }));
    CHECK(refused([](World& w) { w.units()[0].target = -5; }));
    CHECK_FALSE(refused([](World& w) { w.units()[0].target = -1; }));
}

// ===========================================================================
// Map-trigger runtime round trip
// ===========================================================================
namespace {
struct NullHost : TriggerHost {
    bool house_exists(int ht) const override { return ht == 0; }
    std::vector<int> house_instances() const override { return {0}; }
    int player_house_type() const override { return 0; }
    int house_buildings(int) const override { return 1; }
    int house_vehicles(int) const override { return 0; }
    int house_infantry(int) const override { return 0; }
    int house_building_type_count(int, int) const override { return 0; }
    int techno_count(const std::string&) const override { return 0; }
    bool apply(const TriggerEffect&) override { return true; }
};
const char* kTrigMap =
    "[Countries]\n0=Player\n"
    "[Triggers]\n"
    "TRGA=Player,<none>,A,0,1,1,1,0\n"
    "TRGB=Player,<none>,B,0,1,1,1,0\n"
    "TRGC=Player,<none>,C,0,1,1,1,0\n"
    "[Events]\nTRGA=1,13,0,2\nTRGB=1,51,0,3\nTRGC=1,36,0,1\n"
    "[Actions]\nTRGA=1,56,0,1,0,0,0,0,A\nTRGB=1,57,0,1,0,0,0,0,A\nTRGC=1,56,0,2,0,0,0,0,A\n"
    "[Tags]\nTAGA=2,a,TRGA\nTAGB=2,b,TRGB\nTAGC=2,c,TRGC\n";
}  // namespace

TEST_CASE("saveload: MapTriggerSystem round-trips timers/latches/RNG/log") {
    const MapTriggerData d = parse_map_triggers(ra2assets::Ini::parse(kTrigMap), {"Player"}, {});
    NullHost h;
    MapTriggerSystem a(d, 1, 0x1234u);
    a.start(h);
    for (int i = 0; i < 77; ++i) a.tick(h);
    const std::vector<std::uint8_t> s0 = save_triggers(a);

    MapTriggerSystem b(d, 1, 0x9999u);   // a different seed: load must overwrite it
    REQUIRE(load_triggers(b, s0));
    CHECK(save_triggers(b) == s0);
    for (int i = 0; i < 300; ++i) {
        a.tick(h);
        b.tick(h);
    }
    CHECK(a.state_hash() == b.state_hash());
    CHECK(a.log() == b.log());
    CHECK(save_triggers(a) == save_triggers(b));
    CHECK(a.fired_count() > 3);
}

// ===========================================================================
// File container + slots
// ===========================================================================
TEST_CASE("saveload: container encodes header + sections; wrong version is flagged") {
    SaveFile f;
    f.header.description = std::string(100, 'x') + "\n";   // clamped to 80
    f.header.map_file = "xmp31s2.map";
    f.header.frame = 4242;
    f.sections[kTagWorld] = {1, 2, 3};
    f.sections[kTagScene] = {};
    std::vector<std::uint8_t> bytes = encode_save(f);
    SaveFile g;
    REQUIRE(decode_save(bytes, g));
    CHECK(g.header.version == kFormatVersion);
    CHECK(g.header.description.size() == kMaxDescription);
    CHECK(g.header.map_file == "xmp31s2.map");
    CHECK(g.header.frame == 4242);
    REQUIRE(g.section(kTagWorld));
    CHECK(*g.section(kTagWorld) == std::vector<std::uint8_t>{1, 2, 3});
    CHECK(g.section(kTagTriggers) == nullptr);

    std::vector<std::uint8_t> other = bytes;
    other[8] ^= 0x7F;   // version field
    SaveFile h;
    REQUIRE(decode_save(other, h));
    CHECK(h.header.version != kFormatVersion);
    CHECK(h.sections.empty());

    std::vector<std::uint8_t> junk = {'M', 'Z', 0, 0};
    CHECK_FALSE(decode_save(junk, h));
}

// W2-AR-20: the v10 CRC-32 trailer refuses any flipped / truncated byte.
TEST_CASE("saveload: CRC-32 trailer - every single-byte flip or truncation is refused") {
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926u);   // the IEEE check value
    SaveFile f;
    f.header.description = "crc";
    f.header.frame = 77;
    f.sections[kTagWorld] = std::vector<std::uint8_t>(300, 0x5A);
    f.sections[kTagScene] = {9, 8, 7};
    const std::vector<std::uint8_t> bytes = encode_save(f);
    SaveFile g;
    REQUIRE(decode_save(bytes, g));
    CHECK(g.sections.size() == 2);
    int refused = 0, total = 0;
    for (std::size_t i = 12; i < bytes.size(); ++i) {   // past magic + version
        for (std::uint8_t bit : {std::uint8_t(0x01), std::uint8_t(0x80)}) {
            std::vector<std::uint8_t> b = bytes;
            b[i] ^= bit;
            ++total;
            if (!decode_save(b, g)) ++refused;
        }
    }
    CHECK(refused == total);
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 1);
    CHECK_FALSE(decode_save(cut, g));
    std::vector<std::uint8_t> extra = bytes;
    extra.push_back(0);
    CHECK_FALSE(decode_save(extra, g));
    // The header stays readable for the Load list even when the body is bad.
    std::vector<std::uint8_t> bad = bytes;
    bad[bad.size() - 10] ^= 0xFF;
    REQUIRE(decode_save(bad, g, /*header_only=*/true));
    CHECK(g.header.description == "crc");
}

TEST_CASE("saveload: slots - SAVE%04X.SAV naming, newest-first listing, delete") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ra2_p7_saveload_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const std::string n1 = new_save_name(dir.string(), 7);
    CHECK(n1.size() == 12);
    CHECK(n1.rfind("SAVE", 0) == 0);
    CHECK(n1.substr(8) == ".SAV");
    SaveFile f;
    f.header.description = "first";
    REQUIRE(write_file((dir / n1).string(), encode_save(f)));
    const std::string n2 = new_save_name(dir.string(), 7);
    CHECK(n2 != n1);   // retried past the taken name
    f.header.description = "second";
    REQUIRE(write_file((dir / n2).string(), encode_save(f)));
    fs::last_write_time(dir / n1, fs::last_write_time(dir / n2) - std::chrono::hours(1));
    { std::FILE* fp = std::fopen((dir / "notes.txt").string().c_str(), "wb"); if (fp) std::fclose(fp); }
    { std::FILE* fp = std::fopen((dir / "JUNK.SAV").string().c_str(), "wb");
      if (fp) { std::fputs("nope", fp); std::fclose(fp); } }

    std::vector<SlotEntry> all = list_saves(dir.string());
    // W2-AR-21: the junk *.SAV is listed, flagged damaged (never loadable).
    std::vector<SlotEntry> s;
    int damaged = 0;
    for (const SlotEntry& e : all) {
        if (e.damaged) { ++damaged; CHECK(e.file_name == "JUNK.SAV"); }
        else s.push_back(e);
    }
    CHECK(damaged == 1);
    REQUIRE(s.size() == 2);
    CHECK(s[0].header.description == "second");   // newest first
    CHECK(s[1].header.description == "first");
    CHECK_FALSE(s[0].wrong_version);
    CHECK(delete_save(s[1].path));
    CHECK(list_saves(dir.string()).size() == 2);   // "second" + the damaged junk
    fs::remove_all(dir, ec);
    CHECK(clean_description("a\tb\x01" "c") == "abc");
}

TEST_CASE("saveload: the build-queue backlog + hold section round-trips (R-180)") {
    QueueBacklogSave q;
    q.backlog[2] = {5, 5, 7};
    q.backlog[3] = {1};
    q.held = {2, 4};
    SaveFile f;
    f.sections[kTagBacklog] = to_bytes(q);
    SaveFile g;
    REQUIRE(decode_save(encode_save(f), g));
    const std::vector<std::uint8_t>* b = g.section(kTagBacklog);
    REQUIRE(b != nullptr);
    QueueBacklogSave r;
    REQUIRE(from_bytes(*b, r));
    CHECK(r.backlog == q.backlog);
    CHECK(r.held == q.held);
}

// ===========================================================================
// W9-05: the sync-hash policy (what two lockstep peers compare)
// ===========================================================================

// The ONLY World members the sync hash leaves out: derived caches a const
// accessor rebuilds lazily. Adding a member to this list needs a reason in
// save_fields.hpp; a member visited with io_unhashed but missing here (or
// the reverse) FAILS -- a new member cannot be silently unhashed.
TEST_CASE("sync hash: the unhashed World members are exactly the derived caches (source scan)") {
#ifdef RA2_SOURCE_DIR
    const std::string fields = slurp_text(std::string(RA2_SOURCE_DIR) + "/engine/save_fields.hpp");
    REQUIRE(!fields.empty());
    const std::set<std::string> kUnhashedWorldMembers = {
        "amphib_passable_", "crush_grid_", "crush_leak_", "deck_land_", "deck_amph_", "deck_ver_",
        "w5_hide_", "w5_own_"};
    std::set<std::string> found;
    const std::regex re(R"(io_unhashed\(ar,\s*\w+\.([a-z_0-9]+_)\))");
    for (std::sregex_iterator it(fields.begin(), fields.end(), re), end; it != end; ++it) found.insert((*it)[1].str());
    CHECK(found == kUnhashedWorldMembers);
    // Every class SaveAccess serialises is visited by the shared visitors,
    // so the hash archives see each member the save sees (no second list).
    CHECK(fields.find("template <class Ar> static void world(Ar& ar, World& w)") != std::string::npos);
#else
    MESSAGE("RA2_SOURCE_DIR not defined; source scan skipped");
#endif
}

TEST_CASE("sync hash: NaN bit patterns are canonical in the save and the hash (W9-05-13)") {
    Toy a, b, c;
    const std::uint64_t pos = 0x7FF8000000000000ull, neg = 0xFFF8000000000000ull, pay = 0x7FF0000000000123ull;
    std::memcpy(&a.b, &pos, 8);
    std::memcpy(&b.b, &neg, 8);   // the SSE "default NaN"
    std::memcpy(&c.b, &pay, 8);   // a signalling NaN with a payload
    CHECK(a.b != a.b);
    CHECK(to_bytes(a) == to_bytes(b));
    CHECK(to_bytes(a) == to_bytes(c));
    SyncHasher ha, hb;
    io(ha, a);
    io(hb, b);
    CHECK(ha.h == hb.h);
    Toy r;
    REQUIRE(from_bytes(to_bytes(b), r));
    CHECK(r.b != r.b);   // still a NaN after the round trip
}

TEST_CASE("sync hash: equals the policy writer's bytes; a hash-only field moves only the hash") {
    ra2assets::MapFile map;
    World a(map), b(map);
    a.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
    b.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
    CHECK(world_sync_hash(a) == world_sync_hash(b));
    SyncWriter sw;
    SaveAccess::world(sw, a);
    CHECK(fnv1a(sw.buf) == world_sync_hash(a));
    // W9-05: the per-house production rules are not saved but are hashed.
    PlayerEconRules ra, rb;
    rb.build_speed = 0.5;   // a different rulesmd on one peer
    a.set_player_econ_rules(0, ra);
    b.set_player_econ_rules(0, rb);
    CHECK(save_world(a) == save_world(b));
    CHECK(world_sync_hash(a) != world_sync_hash(b));
    b.set_player_econ_rules(0, ra);
    CHECK(world_sync_hash(a) == world_sync_hash(b));
    // The campaign trigger runtime joins the hash when given (co-op).
    const MapTriggerData d = parse_map_triggers(ra2assets::Ini::parse(kTrigMap), {"Player"}, {});
    MapTriggerSystem ts(d, 1, 0x1234u), ts2(d, 1, 0x9999u);
    CHECK(world_sync_hash(a, &ts) != world_sync_hash(a));
    CHECK(world_sync_hash(a, &ts) != world_sync_hash(a, &ts2));   // trigger RNG state is hashed
}

// w9int: the per-frame CRC (net_frame_crc, w9fix-speed) under the same
// policy -- hash-only state and the campaign trigger runtime are folded.
TEST_CASE("sync hash: the per-frame CRC folds the econ-rules install and the trigger runtime (W9-05 policy)") {
    ra2assets::MapFile map;
    World a(map), b(map);
    a.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
    b.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
    CHECK(net_frame_crc(a) == net_frame_crc(b));
    PlayerEconRules ra, rb;
    rb.build_speed = 0.5;   // a different rulesmd on one peer
    a.set_player_econ_rules(0, ra);
    CHECK(net_frame_crc(a) != net_frame_crc(b));   // installed on one peer only
    b.set_player_econ_rules(0, rb);
    CHECK(net_frame_crc(a) != net_frame_crc(b));   // installed differently
    b.set_player_econ_rules(0, ra);
    CHECK(net_frame_crc(a) == net_frame_crc(b));
    const MapTriggerData d = parse_map_triggers(ra2assets::Ini::parse(kTrigMap), {"Player"}, {});
    MapTriggerSystem ts(d, 1, 0x1234u), ts2(d, 1, 0x9999u), ts3(d, 1, 0x1234u);
    CHECK(net_frame_crc(a, nullptr, &ts) != net_frame_crc(a));
    CHECK(net_frame_crc(a, nullptr, &ts) != net_frame_crc(a, nullptr, &ts2));   // trigger RNG state
    CHECK(net_frame_crc(a, nullptr, &ts) == net_frame_crc(a, nullptr, &ts3));
}

// The CRC never reads a derived cache: none of the io_unhashed members (nor
// the occupancy / claim index) appears in its single input list.
TEST_CASE("sync hash: the per-frame CRC reads no derived cache (source scan)") {
#ifdef RA2_SOURCE_DIR
    const std::string fields = slurp_text(std::string(RA2_SOURCE_DIR) + "/engine/save_fields.hpp");
    const std::string crc = slurp_text(std::string(RA2_SOURCE_DIR) + "/engine/net_frame_crc.cpp");
    REQUIRE(!fields.empty());
    REQUIRE(!crc.empty());
    std::set<std::string> unhashed;
    const std::regex re(R"(io_unhashed\(ar,\s*\w+\.([a-z_0-9]+_)\))");
    for (std::sregex_iterator it(fields.begin(), fields.end(), re), end; it != end; ++it) unhashed.insert((*it)[1].str());
    REQUIRE(!unhashed.empty());
    for (const std::string& m : unhashed) {
        INFO("net_frame_crc.cpp reads the derived cache " << m);
        CHECK(crc.find("." + m) == std::string::npos);
    }
    CHECK(crc.find(".occ_") == std::string::npos);
    CHECK(crc.find("claims_") == std::string::npos);
    // ...and it folds the io_hash_only members.
    CHECK(crc.find("w.econ_rules_digest_") != std::string::npos);
    CHECK(crc.find("w.econ_rules_set_") != std::string::npos);
#else
    MESSAGE("RA2_SOURCE_DIR not defined; source scan skipped");
#endif
}

TEST_CASE("sync hash: a presentation-side cache rebuild is not a desync (W9-05)") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    const ra2assets::MapFile& map = ra2fixture::mapsmd03_map();
    World a(map);
    populate(a);
    for (int f = 0; f < 15; ++f) a.step(1.0 / 15.0);
    const std::vector<std::uint8_t> s0 = save_world(a);
    World b(map);
    REQUIRE(load_world(b, s0));
    const std::uint64_t h0 = world_sync_hash(a);
    const auto m0 = world_field_hashes(a);
    // Peer B's renderer / placement preview asks for the derived grids
    // between ticks; peer A never does.
    (void)b.amphib_grid();
    (void)b.crush_grid();
    (void)b.crush_leak();
    (void)b.deck_grid(MovementDomain::Land);
    CHECK(world_sync_hash(b) == h0);
    CHECK(world_field_hashes(b) == m0);
    // ...and they keep agreeing as the sim moves on.
    for (int f = 0; f < 30; ++f) {
        a.step(1.0 / 15.0);
        b.step(1.0 / 15.0);
    }
    CHECK(world_sync_hash(a) == world_sync_hash(b));
}

TEST_CASE("sync hash: player orders are recorded only outside a skirmish session (W9-05)") {
    ra2assets::MapFile map;
    World w(map);
    w.add_combat_unit(0, 0, 0, gun("GI", 10, 3.0));
    w.mark_player_orders({0});
    CHECK(w.take_player_orders().size() == 1);   // campaign: step_teams drains them
    w.set_skirmish_session(true);
    for (int k = 0; k < 100; ++k) w.mark_player_orders({0});
    CHECK(w.take_player_orders().empty());       // skirmish / network: nothing grows
}

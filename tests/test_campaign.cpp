#include "doctest.h"
#include "../engine/campaign.hpp"
#include "asset_fixture.hpp"
#include "ra2assets/ini.hpp"
#include <string>
#include <vector>

using namespace ra2engine;
using ra2assets::Ini;

namespace {
const char* kBattle =
    "[Battles]\n1=TUT1\n2=ALL1\n3=SOV1\n"
    "[TUT1]\nCD=0\nScenario=trn01t.MAP\nDescription=DESC:TUT1\n"
    "[ALL1]\nCD=0\nScenario=ALL01t.MAP\nFinalMovie=\nDescription=DESC:ALL1\n"
    "[SOV1]\nCD=1\nScenario=SOV01t.MAP\nDescription=DESC:SOV1\nDebugOnly=no\n";
const char* kMission =
    "[ALL01T.MAP]\nBriefing=Brief:ALL01\nUIName=Name:ALL01\nLSLoadMessage=LoadMsg:ALL01\n"
    "LSLoadBriefing=LoadBrief:ALL01\nLS800BriefLocX=20\nLS800BriefLocY=30\n"
    "LS640BkgdName=LS640A01.SHP\nLS800BkgdName=LS800A01.SHP\n";
const char* kMapSel =
    "[GDI]\nAnims=Anims\n1=Trn01\n2=Trn02\n3=ALL01\n4=ALL02\n"
    "[ALL00]\nScenario=\n1=ALL01 \n"
    "[ALL01]\nScenario=ALL01t.MAP\nDescription=768\n2=ALL02\n"
    "[ALL02]\nScenario=ALL02s.MAP\n3=ALL03\n"
    "[ALL03]\nScenario=\n"
    "[Trn02]\nScenario=trn02t.MAP\n2=ALL01\n";
}  // namespace

TEST_CASE("campaign: battle.ini [Battles] list (FUN_0046cd30/0046cbf0)") {
    const CampaignTables t = CampaignTables::parse(Ini::parse(kBattle), Ini::parse(""), Ini::parse(""));
    REQUIRE(t.battles().size() == 3);
    CHECK(t.battles()[1].id == "ALL1");
    CHECK(t.battles()[1].scenario == "ALL01t.MAP");
    CHECK(t.battles()[2].cd == 1);
    REQUIRE(t.battle("sov1"));
    CHECK(t.battle("sov1")->description == "DESC:SOV1");
    CHECK(t.battle("nope") == nullptr);
}

TEST_CASE("campaign: mission.ini per-map section (FUN_00686560)") {
    const CampaignTables t = CampaignTables::parse(Ini::parse(""), Ini::parse(kMission), Ini::parse(""));
    const MissionInfo m = t.mission("all01t.map");
    CHECK(m.found);
    CHECK(m.briefing == "Brief:ALL01");
    CHECK(m.ls_load_briefing == "LoadBrief:ALL01");
    CHECK(m.ls800_bkgd == "LS800A01.SHP");
    CHECK(m.ls800_brief_y == 30);
    CHECK_FALSE(t.mission("sov01t.map").found);
}

TEST_CASE("campaign: next scenario follows the mapsel stage chain by scenario name [RE §3.5]") {
    const CampaignTables t = CampaignTables::parse(Ini::parse(""), Ini::parse(""), Ini::parse(kMapSel));
    CHECK(t.next_scenario("GDI", "ALL01T.MAP") == "ALL02s.MAP");
    CHECK(t.next_scenario("gdi", "all01t.map") == "ALL02s.MAP");   // case-insensitive
    CHECK(t.next_scenario("GDI", "trn02t.MAP") == "ALL01t.MAP");
    CHECK(t.next_scenario("GDI", "ALL02s.MAP") == "");    // next stage has no Scenario
    CHECK(t.next_scenario("Nod", "ALL01t.MAP") == "");    // not on that side's list
    CHECK(t.side_stages("GDI").size() == 4);
}

TEST_CASE("campaign: [Basic] fields (FUN_00689890)") {
    const Ini m = Ini::parse(
        "[Basic]\nBriefing=Brief:ALL01\nNextScenario=GDI2A.map\nEndOfGame=no\nSkipScore=yes\n"
        "Player=Player House\nIntro=A01_f00e\nBrief=<none>\nTheme=No theme\nCarryOverMoney=0.5\n");
    const ScenarioBasic b = parse_scenario_basic(m);
    CHECK(b.player == "Player House");
    CHECK(b.intro == "A01_f00e");
    CHECK(b.brief.empty());
    CHECK(b.theme.empty());
    CHECK(b.skip_score);
    CHECK_FALSE(b.end_of_game);
    CHECK(b.carry_over_money == doctest::Approx(0.5));
    CHECK(parse_scenario_basic(Ini::parse("")).player == "Americans");   // the default
}

TEST_CASE("campaign: object field orders per section [RE §4]") {
    const Ini m = Ini::parse(
        "[Basic]\nNewINIFormat=4\n"
        "[Structures]\n1018591=Other1 House,GACNST,256,71,31,0,0965BDDC,1,0,1,0,0,None,None,None,0,0\n"
        "[Units]\n7=Civie1 House,MTNK,128,55,18,96,Guard,0B2295AC,100,-1,0,-1,1,1\n"
        "[Infantry]\n0=Player House,TANY,256,62,108,2,Area Guard,64,08B9B35C,0,-1,0,1,1\n"
        "1=BadGuy1 House,E2,256,85,59,2,Guard,0,None,0,-1,0,1,1\n"
        "[Aircraft]\n0=BadGuy1 House,ZEP,256,10,11,32,Guard,None,0,-1,1,1\n"
        "[Bogus]\n");
    const std::vector<MapObjectRec> o = parse_map_objects(m);
    REQUIRE(o.size() == 5);
    CHECK(o[0].kind == 0);
    CHECK(o[0].type == "GACNST");
    CHECK(o[0].x == 71);
    CHECK(o[0].y == 31);
    CHECK(o[0].tag == "0965BDDC");
    CHECK(o[1].kind == 1);
    CHECK(o[1].facing == 96);
    CHECK(o[1].mission == "Guard");
    CHECK(o[1].tag == "0B2295AC");
    CHECK(o[1].veterancy == 100);
    CHECK(o[1].health == 128);
    CHECK(o[2].kind == 2);
    CHECK(o[2].subcell == 2);
    CHECK(o[2].mission == "Area Guard");
    CHECK(o[2].facing == 64);
    CHECK(o[2].tag == "08B9B35C");
    CHECK(o[3].tag.empty());   // None
    CHECK(o[4].kind == 3);
    CHECK(o[4].type == "ZEP");
    CHECK(o[4].facing == 32);
}

TEST_CASE("campaign: legacy packed cell (NewINIFormat < 4)") {
    const Ini m = Ini::parse("[Basic]\nNewINIFormat=3\n[Units]\n0=A,MTNK,256,1290,0,Guard,None,0,-1,0,-1,1,1\n");
    const std::vector<MapObjectRec> o = parse_map_objects(m);
    REQUIRE(o.size() == 1);
    CHECK(o[0].x == 1290 % 128);
    CHECK(o[0].y == 1290 / 128);
}

TEST_CASE("campaign: strength rule (ftol, >Strength-3 snaps full, floor 1) [RE §4]") {
    CHECK(campaign_strength(600, 256, false) == 600);
    CHECK(campaign_strength(600, 255, false) == 600);   // 597.66 -> 598 > 597 -> full
    CHECK(campaign_strength(600, 128, false) == 300);
    CHECK(campaign_strength(150, 42, false) == 25);      // 24.6 -> 25
    CHECK(campaign_strength(150, 0, false) == 1);
    CHECK(campaign_strength(1000, 400, true) == 1000);   // clamped to 256
}

TEST_CASE("campaign: house order puts the player first") {
    const Ini m = Ini::parse(
        "[Houses]\n0=BadGuy House\n1=Player House\n2=Civ House\n"
        "[Player House]\nCountry=Player\n[BadGuy House]\nCountry=BadGuy\n");
    const std::vector<std::string> h = campaign_house_order(m, "Player House");
    REQUIRE(h.size() == 3);
    CHECK(h[0] == "Player House");
    CHECK(h[1] == "BadGuy House");
    CHECK(h[2] == "Civ House");
    CHECK(campaign_house_order(m, "Player")[0] == "Player House");   // by Country=
}

TEST_CASE("campaign: BorrowedTime mission end (FUN_004fc800 / FUN_004f8350) [RE §5]") {
    CHECK(borrowed_time_frames(0.1) == 90);   // SavourDelay=.1 on this install
    MissionEnd e(90);
    CHECK_FALSE(e.ended(1000));
    e.flag(1, 500);
    e.flag(2, 510);   // the first decision sticks
    CHECK(e.outcome() == 1);
    CHECK_FALSE(e.ended(589));
    CHECK(e.ended(590));
}

TEST_CASE("campaign: map team parse (TeamTypes/TaskForces/ScriptTypes)") {
    const Ini m = Ini::parse(
        "[TeamTypes]\n0=T1\n[T1]\nName=Dread\nHouse=BadGuy2\nScript=S1\nTaskForce=TF1\nWaypoint=B\nMax=1\nTag=0B1\n"
        "[TaskForces]\n0=TF1\n[TF1]\nName=x\n0=2,DRED\n1=1,E2\nGroup=-1\n"
        "[ScriptTypes]\n0=S1\n[S1]\nName=s\n0=3,5\n1=6,1\n");
    const MapTeamData d = parse_map_teams(m);
    REQUIRE(d.teams.size() == 1);
    CHECK(d.teams[0].house == "BadGuy2");
    CHECK(d.teams[0].waypoint == 1);
    CHECK(d.teams[0].tag == "0B1");
    REQUIRE(d.taskforce("tf1"));
    CHECK(d.taskforce("tf1")->size() == 2);
    CHECK((*d.taskforce("tf1"))[0].count == 2);
    REQUIRE(d.find_script("S1") == 0);
    CHECK(d.scripts[0].actions.size() == 2);
    CHECK(d.scripts[0].actions[0].action == 3);
    CHECK(d.scripts[0].actions[0].arg == 5);
}

TEST_CASE("campaign: map team script interpreter (RE §7 numbering)") {
    MapScript s;
    s.actions = {{3, 5}, {5, 1}, {12, 4}, {6, 1}};
    MapTeamState st;
    MapTeamView v;
    v.live_members = 2;
    // Move: issued, then waits for arrival.
    MapTeamCommand c = step_map_team(st, s, v, 0);
    CHECK(c.kind == MapTeamCommand::Kind::MoveWaypoint);
    CHECK(c.arg == 5);
    CHECK(c.fresh);
    c = step_map_team(st, s, v, 1);
    CHECK(c.kind == MapTeamCommand::Kind::MoveWaypoint);
    CHECK_FALSE(c.fresh);
    v.arrived = true;
    // Arrived -> Guard 1 (15 frames, c14 idx 5).
    c = step_map_team(st, s, v, 2);
    CHECK(c.kind == MapTeamCommand::Kind::Guard);
    v.arrived = false;
    c = step_map_team(st, s, v, 16);
    CHECK(c.kind == MapTeamCommand::Kind::Guard);
    // Timer out -> SetGlobal 4 in the same tick.
    c = step_map_team(st, s, v, 17);
    CHECK(c.kind == MapTeamCommand::Kind::SetGlobal);
    CHECK(c.arg == 4);
    // Jump 1 (1-based) -> line 0; consumes the tick.
    c = step_map_team(st, s, v, 18);
    CHECK(c.kind == MapTeamCommand::Kind::None);
    CHECK(st.pc == 0);
    c = step_map_team(st, s, v, 19);
    CHECK(c.kind == MapTeamCommand::Kind::MoveWaypoint);
    // No live members -> done.
    v.live_members = 0;
    CHECK(step_map_team(st, s, v, 20).kind == MapTeamCommand::Kind::Done);
}

TEST_CASE("campaign: W2-CA-17 interpreter deploy/scatter are one-shot commands") {
    MapScript s;
    s.actions = {{9, 0}, {21, 0}};
    MapTeamState st;
    MapTeamView v;
    v.live_members = 1;
    CHECK(step_map_team(st, s, v, 0).kind == MapTeamCommand::Kind::Deploy);
    CHECK(step_map_team(st, s, v, 1).kind == MapTeamCommand::Kind::Scatter);
    CHECK(step_map_team(st, s, v, 2).kind == MapTeamCommand::Kind::Done);
}

TEST_CASE("campaign: W4-SQ-01 Unload waits for empty transports, then reports its mode") {
    MapScript s;
    s.actions = {{8, 1}, {39, 2}};
    MapTeamState st;
    MapTeamView v;
    v.live_members = 3;
    v.unloaded = false;
    MapTeamCommand c = step_map_team(st, s, v, 0);
    CHECK(c.kind == MapTeamCommand::Kind::Unload);
    CHECK(c.fresh);
    c = step_map_team(st, s, v, 1);
    CHECK(c.kind == MapTeamCommand::Kind::Unload);
    CHECK_FALSE(c.fresh);
    v.unloaded = true;
    c = step_map_team(st, s, v, 2);
    CHECK(c.kind == MapTeamCommand::Kind::UnloadDone);
    CHECK(c.arg == 1);
    CHECK(step_map_team(st, s, v, 3).kind == MapTeamCommand::Kind::SetLocal);
}

TEST_CASE("campaign: W4-SQ-01 Load (14) and WaitLoaded (43)") {
    MapScript s;
    s.actions = {{14, 0}, {43, 0}, {3, 7}};
    MapTeamState st;
    MapTeamView v;
    v.live_members = 4;
    v.loaded = false;
    v.full = false;
    CHECK(step_map_team(st, s, v, 0).kind == MapTeamCommand::Kind::Load);
    CHECK(step_map_team(st, s, v, 1).kind == MapTeamCommand::Kind::Load);
    v.loaded = true;
    // Loaded -> WaitLoaded holds while a transport is not full.
    CHECK(step_map_team(st, s, v, 2).kind == MapTeamCommand::Kind::None);
    CHECK(st.pc == 1);
    v.full = true;
    CHECK(step_map_team(st, s, v, 3).kind == MapTeamCommand::Kind::MoveWaypoint);
}

TEST_CASE("campaign: W4-SQ-01/06 TeamType delivery keys") {
    const ra2assets::Ini map = ra2assets::Ini::parse(
        "[TeamTypes]\n0=T1\n[T1]\nName=x\nDroppod=yes\nUseTransportOrigin=yes\n"
        "TransportWaypoint=J\nTransportsReturnOnUnload=yes\nVeteranLevel=3\nFull=yes\n");
    const MapTeamData d = parse_map_teams(map);
    REQUIRE(d.teams.size() == 1);
    CHECK(d.teams[0].droppod);
    CHECK(d.teams[0].use_transport_origin);
    CHECK(d.teams[0].transports_return);
    CHECK(d.teams[0].transport_waypoint == 9);
    CHECK(d.teams[0].veteran_level == 3);
    CHECK(d.teams[0].full);
}

TEST_CASE("campaign: interpreter win/lose/unknown actions") {
    MapScript s;
    s.actions = {{13, 0}, {7, 0}, {23, 0}};
    MapTeamState st;
    MapTeamView v;
    v.live_members = 1;
    CHECK(step_map_team(st, s, v, 0).kind == MapTeamCommand::Kind::Skip);   // idle anim: inert
    CHECK(step_map_team(st, s, v, 1).kind == MapTeamCommand::Kind::Win);
    CHECK(step_map_team(st, s, v, 2).kind == MapTeamCommand::Kind::Lose);
    CHECK(step_map_team(st, s, v, 3).kind == MapTeamCommand::Kind::Done);
}

TEST_CASE("campaign: mission names") {
    CHECK(mission_from_name("Sleep") == kMissionSleep);
    CHECK(mission_from_name("area guard") == kMissionAreaGuard);
    CHECK(mission_from_name("Hunt") == kMissionHunt);
    CHECK(mission_from_name("???") == kMissionGuard);
}

// ---------------------------------------------------------------------------
// Real install: the RA2 control files + both campaign chains.
namespace {
std::string read_local(const char* name) {
    const ra2assets::MixFile local = ra2fixture::ra2_mix().open_nested("local.mix");
    const ra2assets::MixEntry* e = local.find(std::string(name));
    if (!e) return std::string();
    const std::vector<uint8_t> raw = local.read(*e);
    return std::string(raw.begin(), raw.end());
}
}  // namespace

TEST_CASE("campaign: real RA2 battle/mission/mapsel chains (Allied 1..12, Soviet 1..12)") {
    if (!ra2fixture::ra2_available()) {
        MESSAGE("RA2 install not found; skipping");
        return;
    }
    const CampaignTables t = CampaignTables::parse(Ini::parse(read_local("battle.ini")),
                                                   Ini::parse(read_local("mission.ini")),
                                                   Ini::parse(read_local("mapsel.ini")));
    REQUIRE(t.battle("ALL1"));
    REQUIRE(t.battle("SOV1"));
    CHECK(t.battle("ALL1")->scenario == "ALL01t.MAP");
    std::vector<std::string> allied{t.battle("ALL1")->scenario};
    while (allied.size() < 20) {
        const std::string n = t.next_scenario("GDI", allied.back());
        if (n.empty()) break;
        allied.push_back(n);
    }
    REQUIRE(allied.size() == 12);
    CHECK(allied[1] == "ALL02s.MAP");
    CHECK(allied[11] == "ALL12s.MAP");
    std::vector<std::string> soviet{t.battle("SOV1")->scenario};
    while (soviet.size() < 20) {
        const std::string n = t.next_scenario("Nod", soviet.back());
        if (n.empty()) break;
        soviet.push_back(n);
    }
    REQUIRE(soviet.size() == 12);
    CHECK(soviet[11] == "sov12s.MAP");
    const MissionInfo m = t.mission("ALL01T.MAP");
    CHECK(m.ls800_bkgd == "LS800A01.SHP");
    CHECK(m.ls_load_briefing == "LoadBrief:ALL01");
}

TEST_CASE("campaign: real all01t objects + teams parse") {
    if (ra2fixture::mix_bytes_by_name("Maps01.mix").empty()) {
        MESSAGE("Maps01.mix not found; skipping");
        return;
    }
    const ra2assets::MixFile& mm = ra2fixture::mix_by_name("Maps01.mix");
    const ra2assets::MixEntry* e = mm.find(std::string("all01t.map"));
    REQUIRE(e);
    const std::vector<uint8_t> raw = mm.read(*e);
    const Ini map = Ini::parse(std::string(raw.begin(), raw.end()));
    const std::vector<MapObjectRec> o = parse_map_objects(map);
    int s = 0, u = 0, i = 0;
    for (const MapObjectRec& r : o) (r.kind == 0 ? s : r.kind == 1 ? u : i)++;
    CHECK(s == 190);
    CHECK(u == 19);
    CHECK(i == 40);
    CHECK(parse_scenario_basic(map).player == "Player House");
    const MapTeamData d = parse_map_teams(map);
    CHECK(d.teams.size() == 49);
    CHECK(d.scripts.size() == 38);
    CHECK(campaign_house_order(map, "Player House")[0] == "Player House");
}

// ---------------------------------------------------------------------------
// MapTriggerSystem campaign mode [RE: campaign.md §2/§4; map-triggers.md §3.4]
#include "../engine/map_trigger.hpp"
namespace {
struct CampHost : TriggerHost {
    std::vector<TriggerEffect> effects;
    bool house_exists(int ht) const override { return ht == 3 || ht == 4; }
    std::vector<int> house_instances() const override { return {3, 4}; }
    int player_house_type() const override { return 3; }
    int house_buildings(int) const override { return 1; }
    int house_vehicles(int) const override { return 1; }
    int house_infantry(int) const override { return 1; }
    int house_building_type_count(int, int) const override { return 0; }
    int techno_count(const std::string&) const override { return 0; }
    bool apply(const TriggerEffect& e) override { effects.push_back(e); return true; }
};
const char* kCampMap =
    "[Countries]\n0=Player\n1=Bad\n"
    "[Triggers]\n"
    "T1=Player,<none>,ChangeHouse,0,1,1,1,0\n"
    "T2=Player,<none>,Semi death,0,1,1,1,0\n"
    "[Events]\nT1=1,48,0,0\nT2=1,48,0,0\n"
    "[Actions]\nT1=2,14,0,4,0,0,0,0,A,63,0,7,0,0,0,0,A\nT2=1,56,0,3,0,0,0,0,A\n"
    "[Tags]\nG1=0,g1,T1\nG2=1,g2,T2\n";
MapTriggerData camp_data() {
    return parse_map_triggers(Ini::parse(kCampMap), {"Americans", "Alliance", "Russians"},
                              {"GAPOWR"});
}
}  // namespace

TEST_CASE("campaign: object actions reach the host only in campaign mode") {
    const MapTriggerData d = camp_data();
    for (bool camp : {false, true}) {
        MapTriggerSystem sys(d, 1, 1);
        sys.set_campaign_mode(camp);
        CampHost h;
        sys.start(h);
        const int g1 = d.find_tag("G1");
        REQUIRE(sys.attach_tag(g1));
        RaiseContext ctx;
        ctx.has_object = true;
        ctx.object_house_type = 3;
        const MapTriggerSystem::ObjectRaise r = sys.raise_object(g1, kEvDestroyedByAnything, ctx, h);
        CHECK(r.fired);
        CHECK(r.tag_gone);            // volatile: the tag is marked for deletion
        if (!camp) {
            CHECK(h.effects.empty());
        } else {
            REQUIRE(h.effects.size() == 2);
            CHECK(h.effects[0].action == 14);
            CHECK(h.effects[0].house_type == 4);          // Value 4 = map country "Bad"
            CHECK(h.effects[0].trigger_type == d.find_trigger("T1"));
            CHECK(h.effects[1].action == 63);
            CHECK(h.effects[1].waypoint == 7);            // Apply100Damage: Value = waypoint
        }
        sys.tick(h);
        CHECK_FALSE(sys.tag_alive(g1));                    // volatile tag destroyed
        CHECK(sys.raise_object(g1, kEvDestroyedByAnything, ctx, h).tag_gone);
        CHECK(sys.tag_has_trigger(g1, d.find_trigger("T1")));
        CHECK_FALSE(sys.tag_has_trigger(g1, d.find_trigger("T2")));
    }
}

TEST_CASE("campaign: SemiPersistant tag fires on the last attached object (RE §3.4)") {
    const MapTriggerData d = camp_data();
    MapTriggerSystem sys(d, 1, 1);
    sys.set_campaign_mode(true);
    CampHost h;
    sys.start(h);
    const int g2 = d.find_tag("G2");
    sys.attach_tag(g2);
    sys.attach_tag(g2);
    sys.attach_tag(g2);
    RaiseContext ctx;
    ctx.has_object = true;
    MapTriggerSystem::ObjectRaise r = sys.raise_object(g2, kEvDestroyedByAnything, ctx, h);
    CHECK_FALSE(r.fired);
    CHECK(r.detached);
    r = sys.raise_object(g2, kEvDestroyedByAnything, ctx, h);
    CHECK_FALSE(r.fired);
    CHECK(r.detached);
    CHECK_FALSE(sys.local(3));
    r = sys.raise_object(g2, kEvDestroyedByAnything, ctx, h);
    CHECK(r.fired);
    CHECK(sys.local(3));
}

// W4-MC-03/04 [RE: FUN_00689890 HomeCell, FUN_00577e90 IsWithinUsableArea]:
// ALL01 ([Map] Size=0,0,50,64 LocalSize=2,6,46,52): the GIs' home waypoint 98
// (58,72) is playable; Tanya's off-board staging cell (62,108) is not.
TEST_CASE("W4-MC-03/04: HomeCell and LocalSize usable area (ALL01)") {
    const Ini m = Ini::parse(
        "[Basic]\nHomeCell=98\nAltHomeCell=99\n[Map]\nSize=0,0,50,64\nLocalSize=2,6,46,52\n");
    const ScenarioBasic b = parse_scenario_basic(m);
    CHECK(b.home_cell == 98);
    CHECK(b.alt_home_cell == 99);
    const ra2engine::MapLocalSize ls = ra2engine::parse_map_local_size(m);
    REQUIRE(ls.valid);
    CHECK(ls.size_w == 50);
    CHECK(ls.h == 52);
    CHECK(ra2engine::within_usable_area(ls, 58, 72));
    CHECK_FALSE(ra2engine::within_usable_area(ls, 62, 108));   // staged Tanya
    // edges: s = cx+cy in (62, 168], d = cx-cy in (-46, 46)
    CHECK_FALSE(ra2engine::within_usable_area(ls, 31, 31));    // s = 62
    CHECK(ra2engine::within_usable_area(ls, 32, 31));          // s = 63
    CHECK(ra2engine::within_usable_area(ls, 84, 84));          // s = 168
    CHECK_FALSE(ra2engine::within_usable_area(ls, 85, 84));    // s = 169
    CHECK_FALSE(ra2engine::within_usable_area(ls, 76, 30));    // d = 46
    CHECK_FALSE(ra2engine::within_usable_area(ls, 30, 76));    // d = -46
    CHECK(ra2engine::within_usable_area(ls, 75, 30));
    const ScenarioBasic d = parse_scenario_basic(Ini::parse("[Basic]\n"));
    CHECK(d.home_cell == 98);
    CHECK_FALSE(ra2engine::parse_map_local_size(Ini::parse("")).valid);
}

TEST_CASE("campaign: Yuri's Revenge tables retain separate Allied and Soviet progression") {
    auto t=CampaignTables::parse(Ini::parse("[Battles]\n1=ALL1\n2=SOV1\n[ALL1]\nScenario=all01umd.map\n[SOV1]\nScenario=sov01umd.map\n"),Ini::parse(""),
        Ini::parse("[GDI]\n1=ALL01\n2=ALL02\n[Nod]\n1=SOV01\n2=SOV02\n[ALL01]\nScenario=ALL01umd.MAP\n2=ALL02\n[ALL02]\nScenario=ALL02umd.MAP\n[SOV01]\nScenario=SOV01umd.MAP\n2=SOV02\n[SOV02]\nScenario=SOV02smd.MAP\n"));
    REQUIRE(t.battle("ALL1")); CHECK(t.battle("ALL1")->scenario=="all01umd.map");
    CHECK(t.next_scenario("GDI","all01umd.map")=="ALL02umd.MAP");
    CHECK(t.next_scenario("Nod","sov01umd.map")=="SOV02smd.MAP");
}

TEST_CASE("campaign: retail YR control files expose both seven-mission chains") {
    if (!ra2fixture::assets_available()) { MESSAGE("YR assets unavailable"); return; }
    auto ini=[](const char* name) {
        const auto raw=ra2fixture::resolve_nested_bytes(name);
        return Ini::parse(std::string(raw.begin(),raw.end()));
    };
    const auto t=CampaignTables::parse(ini("battlemd.ini"),ini("missionmd.ini"),ini("mapselmd.ini"));
    for(const auto& side : {"GDI","Nod"}) {
        const auto* b=t.battle(std::string(side)=="GDI"?"ALL1":"SOV1"); REQUIRE(b);
        auto map=b->scenario; int count=0;
        while(!map.empty() && count<10) {
            CHECK(t.mission(map).found);
            if (!ra2fixture::mix_bytes_by_name("mapsmd03.mix").empty()) {
                const auto& archive=ra2fixture::mix_by_name("mapsmd03.mix");
                const auto* entry=archive.find(map); REQUIRE(entry);
                const auto raw=archive.read(*entry);
                const auto scenario=Ini::parse(std::string(raw.begin(),raw.end()));
                CHECK_FALSE(parse_map_objects(scenario).empty());
                CHECK_FALSE(parse_scenario_basic(scenario).player.empty());
                CHECK_FALSE(scenario.section("Triggers").empty());
            }
            ++count; map=t.next_scenario(side,map);
        }
        CHECK(count==7); CHECK(map.empty());
    }
}

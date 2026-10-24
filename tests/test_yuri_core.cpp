#include "doctest.h"
#include "world.hpp"
#include "faction_support.hpp"
#include "harvest.hpp"
#include "savegame.hpp"
#include "weapon_resolve.hpp"
#include "ra2assets/rules.hpp"
#include "weapon_sound_loop.hpp"
#include "weapon_unit.hpp"
#include "report_probe.hpp"
#include "psychic_links.hpp"
#include "psychic_sensor.hpp"
#include "selection.hpp"
#include <fstream>
#include <iterator>
using namespace ra2engine;
namespace {
struct Rig {
    ra2assets::MapFile map = ra2assets::MapFile::parse("[Map]\nSize=0,0,20,20\nTheater=TEMPERATE\n");
    Rig() {
        // The non-const fixture owns this vector; no retail map/codec needed.
        auto& cells = const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x)
            cells.push_back({x,y,0,0,0});
    }
};
UnitCombat unit(const char* image) {
    UnitCombat c; c.image = image; c.strength = 100; c.speed_cps = 2;
    c.verses.assign(11,100); c.tg.rules_id = image; return c;
}
TEST_CASE("Yuri roster whitelist includes faction buildings, not shared map structures") {
    CHECK(yuri_building_type("YACNST"));
    CHECK(yuri_building_type("YAPSYT"));
    CHECK(yuri_building_type("YAWALL"));
    CHECK_FALSE(yuri_building_type("NATBNK"));
    CHECK_FALSE(yuri_building_type("CAWASH"));
    CHECK_FALSE(yuri_building_type("FENC01"));
}
TEST_CASE("enslaved workers are not commandable") {
    Rig r; World w(r.map); w.add_combat_unit(2,2,0,unit("SLAV"));
    CHECK(w.controllable_unit(0));
    w.units()[0].w7.slave_manager=0;
    CHECK_FALSE(w.controllable_unit(0));
    Selection selection; selection.set(0); selection.prune(w);
    CHECK(selection.empty());
}
TEST_CASE("slave rules timing survives save and load") {
    Rig r; World w(r.map); BuildingTypeDef d; d.id="YAREFN"; d.slave_combat=unit("SLAV");
    d.slave_combat.harvest_rate_micros=56000;
    d.slave_combat.slave_reload_rate_micros=30000;
    w.set_economy({d},{},OreValues{},1000,0,0);
    const auto bytes=save::save_world(w);
    World loaded(r.map); REQUIRE(save::load_world(loaded,bytes));
    CHECK(loaded.btypes()[0].slave_combat.harvest_rate_micros==56000);
    CHECK(loaded.btypes()[0].slave_combat.slave_reload_rate_micros==30000);
}
TEST_CASE("diagnostics: unit probes preserve saved state and catch broken references") {
    Rig r; World w(r.map);
    w.add_combat_unit(2,2,0,unit("SLAV"));
    auto& worker=w.units()[0];
    worker.w7.slave_manager=999;
    worker.path={{2,2},{3,2}}; worker.path_idx=1; worker.wait_time=4;
    worker.bales_ore=3;
    const auto before=save::save_world(w);
    const auto root=std::filesystem::temp_directory_path()/"ra2-probe-stability";
    struct Stop { ~Stop(){diag::stop();} } stop;
    REQUIRE(diag::start(root.u8string()));
    diag::probe_world(w);
    CHECK(save::save_world(w)==before);
    std::ifstream stream(root/"engine-current.log",std::ios::binary);
    const std::string log{std::istreambuf_iterator<char>(stream),{}};
    CHECK(log.find("invalid_slave_manager=999")!=std::string::npos);
    CHECK(log.find("blocked_over_3s=1")!=std::string::npos);
    CHECK(log.find("ROUTE unit=0 next=3,2")!=std::string::npos);
    worker.path_idx=900;
    diag::probe_world(w); // Invalid index must be reported without dereferencing it.
}

TEST_CASE("Yuri core: numbered Gatling guns retain ground and air weapons and auto-acquire") {
    const auto ini=ra2assets::Ini::parse(
        "[YAGGUN]\nIsGattling=yes\nWeaponCount=6\nWeapon1=Ground\nWeapon2=Air\nTurret=yes\n"
        "[YTNK]\nIsGattling=yes\nPrimary=Ground\nSecondary=Air\n"
        "[Ground]\nDamage=5\nROF=15\nRange=5\nProjectile=Bullet\nWarhead=AP\n"
        "[Air]\nDamage=5\nROF=15\nRange=5\nProjectile=AA\nWarhead=AP\n"
        "[Bullet]\nAG=yes\nAA=no\n[AA]\nAG=no\nAA=yes\n[AP]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n");
    const auto rules=ra2assets::Rules::load(ini);
    auto tw=resolve_type_weapons(rules,"YAGGUN");
    CHECK(tw.slot[0].id=="Ground");CHECK(tw.slot[1].id=="Air");
    CHECK(tw.slot[1].aa);CHECK_FALSE(tw.slot[1].ag);
    CHECK(resolve_type_weapons(rules,"YTNK").slot[0].id=="Ground");
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAGGUN";d.strength=500;
    fill_building_weapons(rules,"YAGGUN",d);REQUIRE(d.armed);
    w.set_economy({d},{},OreValues{},1000,0,0);w.add_building(0,8,8,1,1,0,500,0,0,true);
    auto enemy=unit("ENEMY");enemy.strength=1000;w.add_combat_unit(10,8,1,enemy);
    for(int i=0;i<120;++i)w.step(1.0/15.0);
    CHECK(w.units()[0].hp<1000);
}

TEST_CASE("Yuri core: automatic ranged pursuit stops before melee contact") {
    Rig r;World w(r.map);auto tank=unit("LTNK");tank.range=5;tank.damage=5;tank.rof=15;tank.w6.omni_fire=true;
    w.add_combat_unit(2,2,0,tank);auto enemy=unit("TARGET");enemy.strength=1000;
    w.add_combat_unit(8,2,1,enemy);
    w.order_unit_move(0,7,2);w.units()[0].target=1;w.units()[0].attack_order=false;
    w.units()[0].guard_x=2;w.units()[0].guard_y=2;
    for(int i=0;i<180;++i)w.step(1.0/15.0);
    INFO("pursuer x="<<w.units()[0].fx<<" target="<<w.units()[0].target<<" range="<<w.units()[0].w_range<<" damage="<<w.units()[0].w_damage<<" flash="<<w.units()[0].fire_flash);
    CHECK(w.units()[0].fx<=4);CHECK(w.units()[1].hp<1000);
}

TEST_CASE("Yuri core: psychic sensor uses powered allied radius and commanded destinations") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="NAPSIS";d.strength=500;d.w5.psychic_radius=4;
    w.set_building_types({d});w.add_building(0,8,8,1,1,0,500,0,0,true);
    w.add_combat_unit(2,2,1,unit("ENEMY"));w.order_unit_move(0,9,9);
    int x=0,y=0;CHECK(psychic_sensor_active(w,0,0));CHECK_FALSE(psychic_sensor_active(w,0,1));
    CHECK(psychic_sensor_target(w,0,0,x,y));CHECK(x==9);CHECK(y==9);
    w.order_unit_move(0,18,18);CHECK_FALSE(psychic_sensor_target(w,0,0,x,y));
    w.buildings()[0].power_off=true;CHECK_FALSE(psychic_sensor_active(w,0,0));
}

TEST_CASE("Yuri core: presentation collects unit and building mind-control links") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAPSYT";d.strength=500;
    w.set_building_types({d});w.add_building(0,8,8,1,1,0,500,0,0,true);
    for(int i=0;i<3;++i)w.add_combat_unit(2+i,2,0,unit("UNIT"));
    w.units()[1].w7.mc_controller=0;w.units()[2].w7.mc_building=0;
    const auto before=save::save_world(w);const auto links=skirmish_control_links(w);
    REQUIRE(links.size()==2);CHECK(links[0]==std::make_pair(0,1));CHECK(links[1]==std::make_pair(-1,2));
    CHECK(save::save_world(w)==before);
    w.units()[0].alive=false;CHECK(skirmish_control_links(w).size()==1);
}
TEST_CASE("Yuri core: infantry caps never auto-swap and deployed control stays linked") {
    for (const char* id : {"YURI","YURIPR"}) {
        Rig r;World w(r.map);auto c=unit(id);c.w7.mind_control=true;
        c.deploy_fire=true;c.deploy_fire_slot=1;c.w7.deploy_area_fire=true;
        c.secondary.valid=true;c.secondary.range=1;c.secondary.damage=0;
        w.add_combat_unit(2,2,0,c);w.add_combat_unit(5,2,1,unit("A"));w.add_combat_unit(7,2,1,unit("B"));
        w.units()[0].w7.mc_shot=1;w.resolve_skirmish_mind_control();
        for(int n=0;n<20;++n){w.units()[0].w7.mc_shot=2;w.resolve_skirmish_mind_control();}
        CHECK(w.units()[0].w7.mc_victims.size()==1);CHECK(w.units()[1].house==0);CHECK(w.units()[2].house==1);
        REQUIRE(w.order_deploy(0));CHECK(w.units()[1].house==0);CHECK(w.units()[0].w7.mc_victims.size()==1);
        w.units()[0].w7.mc_shot=2;w.resolve_skirmish_mind_control();CHECK(w.units()[2].house==1);
        w.units()[0].alive=false;w.prepare_skirmish_mind_control();
        CHECK(w.units()[1].house==1);CHECK(w.units()[0].w7.mc_victims.empty());
    }
}
TEST_CASE("Yuri core: Mastermind overload uses configured damage and releases on death") {
    Rig r;World w(r.map);auto c=unit("MIND");c.strength=500;c.w7.mind_control=true;
    c.w7.mc_capacity=3;c.w7.mc_infinite=true;c.w7.overload_count={3,6,10};
    c.w7.overload_damage={0,50,100};c.w7.overload_frames={2,2,2};w.add_combat_unit(2,2,0,c);
    for(int j=0;j<4;++j){w.add_combat_unit(4+j,2,1,unit("TANK"));w.units()[0].w7.mc_shot=j+1;w.resolve_skirmish_mind_control();}
    REQUIRE(w.units()[0].w7.mc_victims.size()==4);w.prepare_skirmish_mind_control();CHECK(w.units()[0].hp==450);
    const auto state=save::save_world(w);World loaded(r.map);REQUIRE(save::load_world(loaded,state));
    CHECK(loaded.units()[0].w7.overload_left==1);
    w.prepare_skirmish_mind_control();CHECK(w.units()[0].hp==450);
    w.prepare_skirmish_mind_control();CHECK(w.units()[0].hp==400);
    w.units()[0].alive=false;w.prepare_skirmish_mind_control();
    for(int j=1;j<=4;++j)CHECK(w.units()[j].house==1);
}
TEST_CASE("Yuri core: Psychic Tower refuses its fourth victim") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAPSYT";d.mind_control=true;d.strength=100;
    d.weapon[0].damage=3;w.set_economy({d},{},OreValues{},1000,0,0);w.add_building(0,2,2,1,1,0,100,0,0,true);
    for(int i=0;i<4;++i)w.add_combat_unit(5+i,2,1,unit("TANK"));
    for(int i=0;i<3;++i)CHECK(w.try_building_mind_control(0,i));
    CHECK_FALSE(w.try_building_mind_control(0,3));CHECK(w.units()[3].house==1);
    CHECK(w.buildings()[0].w7.mc_victims.size()==3);
}
TEST_CASE("Yuri core: Tank Bunker accepts one eligible tank, fires and unloads without changing owner") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="NATBNK";d.tank_bunker=true;
    d.can_be_occupied=d.can_occupy_fire=true;d.max_occupants=1;d.fw=d.fh=2;d.strength=1000;
    w.set_economy({d},{},OreValues{},1000,0,0);w.add_building(0,8,8,2,2,0,1000,0,0,true);
    auto tank=unit("LTNK");tank.is_vehicle=true;tank.w7.bunkerable=true;
    tank.damage=20;tank.rof=10;tank.range=5;tank.primary_ag=true;
    tank.occupy_weapon.valid=true;tank.occupy_weapon.damage=2;tank.occupy_weapon.rof=10;tank.occupy_weapon.range=1;
    tank.occupy_weapon.verses.assign(11,100);
    w.add_combat_unit(7,8,0,tank);w.add_combat_unit(6,8,0,tank);w.add_combat_unit(12,8,1,unit("TARGET"));
    CHECK(w.can_occupy(0,0));REQUIRE(w.campaign_occupy_now(0,0));
    CHECK_FALSE(w.can_occupy(1,0));CHECK(w.units()[0].garrison_of==0);
    w.buildings()[0].garrison_target=2;w.step(1.0/15.0);
    CHECK(w.units()[2].hp==74);CHECK(w.garrison_range(0)==7);
    const auto state=save::save_world(w);World loaded(r.map);REQUIRE(save::load_world(loaded,state));
    CHECK(loaded.units()[0].garrison_of==0);CHECK(loaded.btypes()[0].tank_bunker);
    w.unload_occupants(0);CHECK(w.units()[0].cargo_of<0);CHECK(w.units()[0].alive);CHECK(w.buildings()[0].house==0);
    w.units()[1].w7.t.bunkerable=false;CHECK_FALSE(w.can_occupy(1,0));
    w.units()[1].w7.t.bunkerable=true;w.units()[1].house=1;CHECK_FALSE(w.can_occupy(1,0));
}
TEST_CASE("Yuri core: a controlled tank retains its link inside a bunker and leaves on controller loss") {
    Rig r;World w(r.map);BuildingTypeDef b;b.id="NATBNK";b.tank_bunker=true;
    b.can_be_occupied=b.can_occupy_fire=true;b.max_occupants=1;b.strength=1000;
    w.set_economy({b},{},OreValues{},1000,0,0);w.add_building(0,8,8,1,1,0,1000,0,0,true);
    auto psychic=unit("YURI");psychic.w7.mind_control=true;w.add_combat_unit(2,2,0,psychic);
    auto tank=unit("LTNK");tank.is_vehicle=true;tank.w7.bunkerable=true;w.add_combat_unit(7,8,1,tank);
    w.units()[0].w7.mc_shot=1;w.resolve_skirmish_mind_control();REQUIRE(w.campaign_occupy_now(1,0));
    w.prepare_skirmish_mind_control();CHECK(w.units()[1].house==0);CHECK(w.units()[0].w7.mc_victims.size()==1);
    w.units()[0].alive=false;w.step(1.0/15.0);w.step(1.0/15.0);
    CHECK(w.units()[1].house==1);CHECK(w.units()[1].cargo_of<0);CHECK(w.buildings()[0].house==0);
}
TEST_CASE("Yuri core: magnetic locomotor damage never becomes an ordinary lethal shot") {
    Rig r;World w(r.map);auto c=unit("TELE");c.w7.magnetic=true;c.damage=5000;c.range=10;c.rof=1;
    w.add_combat_unit(2,2,0,c);w.add_combat_unit(4,2,1,unit("TANK"));w.units()[0].target=1;w.units()[0].attack_order=true;
    for(int i=0;i<30;++i)w.step(1.0/15.0);
    CHECK(w.units()[1].hp==100);CHECK(w.units()[1].alive);
    w.order_attack_cell(0,4,2);for(int i=0;i<30;++i)w.step(1.0/15.0);
    CHECK(w.units()[1].hp==100);
    w.units()[0].w2.valid=true;w.units()[0].w2.damage=80;w.units()[0].w2.ag=true;
    CHECK(unit_weapon_for_building(w.units()[0],0)==1);
    w.units()[0].w2.range=10;w.units()[0].w2.rof=10;w.units()[0].w2.verses.assign(11,100);
    BuildingTypeDef target;target.id="TARGET";target.strength=200;
    w.set_economy({target},{},OreValues{},1000,0,0);w.add_building(0,5,5,1,1,1,200,0,0,true);
    w.order_unit_attack_building(0,0);
    for(int i=0;i<30 && w.buildings()[0].hp==200;++i)w.step(1.0/15.0);
    CHECK(w.buildings()[0].hp==120);
}
TEST_CASE("Yuri core: rules resolve bunker eligibility, magnetic behavior and overload tables") {
    const auto ini=ra2assets::Ini::parse(
        "[CombatDamage]\nOverloadCount=3,6,10\nOverloadDamage=0,50,100\nOverloadFrames=30,60,60\nBunkerWeaponRangeBonus=4\n"
        "[VehicleTypes]\n0=LTNK\n1=TELE\n2=MIND\n[LTNK]\nTurret=yes\nPrimary=Gun\n[TELE]\nTurret=yes\nBunkerable=no\nPrimary=Beam\n"
        "[MIND]\nPrimary=MC\n[NATBNK]\nBunker=yes\n[Gun]\nDamage=65\nROF=10\nRange=5\n[Beam]\nDamage=5000\nWarhead=Lift\n"
        "[Lift]\nIsLocomotor=yes\n[MC]\nDamage=3\nInfiniteMindControl=yes\nWarhead=Control\n[Control]\nMindControl=yes\n");
    const auto rules=ra2assets::Rules::load(ini);UnitCombat tank,mag,mind;
    fill_unit_combat_extras(rules,"LTNK",resolve_type_weapons(rules,"LTNK"),tank);
    fill_unit_combat_extras(rules,"TELE",resolve_type_weapons(rules,"TELE"),mag);
    fill_unit_combat_extras(rules,"MIND",resolve_type_weapons(rules,"MIND"),mind);
    CHECK(tank.w7.bunkerable);CHECK(tank.occupy_weapon.damage==65);CHECK_FALSE(mag.w7.bunkerable);CHECK(mag.w7.magnetic);
    CHECK(mind.w7.mc_capacity==3);CHECK(mind.w7.mc_infinite);CHECK(mind.w7.overload_damage==std::vector<int>{0,50,100});
    BuildingTypeDef b;fill_building_weapons(rules,"NATBNK",b);
    CHECK(b.tank_bunker);CHECK(b.can_be_occupied);CHECK(b.max_occupants==1);CHECK(b.bunker_range==4);
}
TEST_CASE("Yuri core: firing report loop has one owner and stops on stop or distance loss") {
    struct Audio {
        int starts=0,stops=0,oneshots=0;bool audible=true;
        bool sound_loops(const std::string& key){return key=="Gattling";}
        unsigned loop_at(const std::string&,double,double){return ++starts;}
        void stop_loop(unsigned){++stops;}
        bool update_loop(unsigned,double,double){return audible;}
        void play_at(const std::string&,double,double,const char*){++oneshots;}
    } audio;
    WeaponSoundLoop loop;
    for(int i=0;i<100;++i){loop.shot(audio,"Gattling",2,2);loop.update(audio,true,2,2);}
    CHECK(audio.starts==1);loop.update(audio,false,2,2);CHECK(audio.stops==1);CHECK(loop.handle==0);
    loop.shot(audio,"Gattling",2,2);CHECK(audio.starts==2);audio.audible=false;loop.update(audio,true,2,2);CHECK(loop.handle==0);
    loop.update(audio,true,2,2);CHECK(audio.starts==2);
    audio.audible=true;loop.shot(audio,"Gattling",2,2);loop.shot(audio,"Cannon",2,2);
    CHECK(audio.stops==2);CHECK(audio.oneshots==1);CHECK(loop.handle==0);
}
TEST_CASE("Yuri core: mobile slave miner satisfies PROC but not other prerequisites") {
    Rig r; World w(r.map);
    BuildingTypeDef cy; cy.id="YACNST"; cy.conyard=true; cy.strength=100;
    BuildingTypeDef barr; barr.id="YABRCK"; barr.strength=100;
    BuildingTypeDef factory; factory.id="YAWEAP"; factory.tech_level=1;
    factory.prerequisite={"PROC","YABRCK","YACNST"};
    BuildingTypeDef ref; ref.id="YAREFN";
    const std::vector<BuildingTypeDef> types={cy,barr,factory,ref};
    w.set_economy(types,{},OreValues{},1000,0,0);
    w.add_building(0,2,2,1,1,0,100,0,0,true);
    w.add_building(1,4,2,1,1,0,100,0,0,true);
    PlayerEconRules econ; econ.groups={{"PROC",{"YAREFN"}},{"PROC_ALTERNATE",{"SMIN"}}};
    w.enable_player_economy(0,5000); w.set_player_econ_rules(0,econ);
    CHECK_FALSE(w.econ_can_build(0,QueueCat::Structure,2));
    w.add_combat_unit(6,2,1,unit("SMIN"));
    CHECK_FALSE(w.econ_can_build(0,QueueCat::Structure,2));
    w.set_unit_owner(0,0);
    CHECK(w.econ_can_build(0,QueueCat::Structure,2));
    w.units()[0].die_time=0;
    CHECK_FALSE(w.econ_can_build(0,QueueCat::Structure,2));
    w.units()[0].die_time=-1;
    w.buildings()[1].alive=false;
    CHECK_FALSE(w.econ_can_build(0,QueueCat::Structure,2));
    w.buildings()[1].alive=true;
    CHECK(w.econ_produce(0,QueueCat::Structure,2)==ProdResult::Started);
}

TEST_CASE("Yuri core: mobile miner seeks ore and explicit Stop persists through save") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.slave_count=2;
    d.slave_combat=unit("SLAV");
    w.set_economy({d},{0},OreValues{},1000,0,0);
    auto miner=unit("SMIN");miner.deploys_into="YAREFN";
    w.set_undeploy_unit(0,miner);
    w.add_combat_unit(2,2,0,miner);
    w.ore().set(17,17,OreKind::Ore,5,0);
    w.step_yuri_workers();
    REQUIRE(w.units()[0].moving());CHECK(w.units()[0].path.back().x==17);
    w.order_stop(0);CHECK(w.units()[0].manual_hold);
    w.step_yuri_workers();CHECK_FALSE(w.units()[0].moving());
    World restored(r.map);REQUIRE(save::load_world(restored,save::save_world(w)));
    CHECK(restored.units()[0].manual_hold);
    w.order_unit_move(0,3,3);CHECK_FALSE(w.units()[0].manual_hold);
}

TEST_CASE("Yuri core: building-strip miner relocates only after its local ore is gone") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.strength=100;d.slave_count=2;
    d.slave_combat=unit("SLAV");d.slave_combat.is_infantry=true;
    w.set_economy({d},{0},OreValues{},1000,0,0);
    auto miner=unit("SMIN");miner.deploys_into="YAREFN";
    w.set_undeploy_unit(0,miner);
    w.add_building(0,2,2,2,2,0,100,0,0,true);
    w.ore().set(5,5,OreKind::Ore,5,0);
    w.ore().set(18,18,OreKind::Ore,5,0);
    w.step_yuri_workers();CHECK(w.buildings()[0].alive);REQUIRE(w.units().size()==2);
    w.ore().set(5,5,OreKind::None,0,0);
    w.step_yuri_workers();CHECK(w.buildings()[0].state==Building::State::Dead);REQUIRE(w.units().size()==3);
    CHECK(w.units()[2].image=="SMIN");REQUIRE(w.units()[2].moving());
    CHECK(w.units()[2].path.back().x==18);CHECK(w.units()[0].w7.slave_manager==-4);
}

TEST_CASE("garrison: other passive owners transfer control and infantry evacuate alive") {
    Rig r;World w(r.map);
    w.set_house(0,{World::Side::Allied,0,"Player",0,true});
    w.set_house(2,{World::Side::Neutral,2,"Neutral",2,false});
    w.set_house(3,{World::Side::Neutral,6,"Civilian",3,false});
    BuildingTypeDef d;d.id="CIVILIAN";d.fw=d.fh=2;d.strength=100;
    d.can_be_occupied=true;d.max_occupants=5;
    w.set_building_types({d});
    const int bi=w.add_building(0,8,8,2,2,3,100,0,0,true);
    auto infantry=unit("GI");infantry.is_infantry=true;infantry.occupier=true;
    w.add_combat_unit(7,8,0,infantry);
    REQUIRE(w.campaign_occupy_now(0,bi));CHECK(w.buildings()[bi].house==0);
    w.unload_occupants(bi);
    CHECK(w.buildings()[bi].occupants.empty());CHECK(w.units()[0].alive);
    CHECK(w.units()[0].garrison_of==-1);CHECK(w.units()[0].cargo_of==-1);
    CHECK(w.units()[0].house==0);CHECK(w.buildings()[bi].house==2);
}

TEST_CASE("Yuri core: Psychic Reveal maps only its area and fog sight is temporary") {
    Rig r;World w(r.map);w.enable_shroud(true);w.set_fog_of_war(true);
    std::array<int,11> verses{};verses.fill(100);
    w.grant_superweapon_type(0,SuperweaponType::PsychicReveal,0,false,0,2,0,verses);
    w.step(1.0/15.0);
    REQUIRE(w.sw_ready(0,SuperweaponType::PsychicReveal));
    CHECK_FALSE(w.explored_at(0,10,10));
    REQUIRE(w.fire_superweapon_at(0,SuperweaponType::PsychicReveal,10,10));
    CHECK(w.explored_at(0,10,10));CHECK(w.visible_at(0,10,10));
    CHECK_FALSE(w.explored_at(0,0,0));CHECK_FALSE(w.explored_at(1,10,10));
    w.step(1.0/15.0);
    CHECK(w.explored_at(0,10,10));CHECK_FALSE(w.visible_at(0,10,10));
}

TEST_CASE("Yuri core: miner packs without selling and reuses workers after redeploy") {
    Rig r; World w(r.map); BuildingTypeDef d;
    d.id="YAREFN"; d.fw=d.fh=2; d.strength=200; d.slave_count=2;
    d.slave_combat=unit("SLAV"); d.slave_combat.is_infantry=true;
    w.set_economy({d},{0},OreValues{},1000,0,0);
    auto mobile=unit("SMIN"); mobile.deploys_into="YAREFN";
    w.set_undeploy_unit(0,mobile);
    w.add_building(0,8,8,2,2,0,200,0,0,true); w.buildings()[0].hp=100;
    w.step_yuri_workers(); REQUIRE(w.units().size()==2);
    CHECK_FALSE(w.order_pack_slave_miner(0,1));
    REQUIRE(w.order_pack_slave_miner(0,0));
    CHECK(w.buildings()[0].state==Building::State::Dead); REQUIRE(w.units().size()==3);
    CHECK(w.units()[2].hp==50); CHECK(w.units()[2].deploys_into==0);
    CHECK(w.units()[0].w7.slave_manager==-4); CHECK_FALSE(w.units()[0].is_harvester);
    const auto state=save::save_world(w); World loaded(r.map); REQUIRE(save::load_world(loaded,state));
    CHECK(loaded.units()[0].w7.slave_manager==-4);
    REQUIRE(w.order_deploy(2));
    for(int i=0;i<150 && w.units()[2].alive;++i) w.step(1.0/15.0);
    REQUIRE_FALSE(w.units()[2].alive); REQUIRE(w.buildings().size()==2);
    CHECK(w.units().size()==3); CHECK(w.units()[0].w7.slave_manager==1);
    CHECK(w.units()[0].is_harvester); CHECK_FALSE(w.units()[0].manual_hold);
    CHECK(w.buildings()[1].hp==100); CHECK(w.take_income(0)==0);
}

TEST_CASE("Yuri core: blocked miner packing keeps the building and workforce") {
    Rig r; World w(r.map); BuildingTypeDef d;
    d.id="YAREFN";d.fw=d.fh=2;d.strength=100;d.slave_count=2;d.slave_combat=unit("SLAV");
    w.set_economy({d},{0},OreValues{},1000,0,0);w.set_undeploy_unit(0,unit("SMIN"));
    w.add_building(0,8,8,2,2,0,100,0,0,true);w.step_yuri_workers();
    w.add_combat_unit(8,8,0,unit("BLOCKER"));
    CHECK_FALSE(w.order_pack_slave_miner(0,0));
    CHECK(w.buildings()[0].state==Building::State::Idle);CHECK(w.buildings()[0].hp==100);
    CHECK(w.units().size()==3);CHECK(w.units()[0].w7.slave_manager==0);
}

TEST_CASE("Yuri core: worker shovel and carry sequences animate and return to ready") {
    InfTable t; t.crawls=false;
    t.e[kSqReady]={0,1,0,-1,true}; t.e[kSqWalk]={8,4,4,-1,true};
    t.e[kSqShovel]={40,3,3,-1,true}; t.e[kSqCarry]={64,4,4,-1,true};
    InfAnimState s; InfTickCtx c; int facing=0; c.shoveling=true;
    inf_anim_tick(s,t,facing,c); CHECK(s.seq==kSqShovel);
    int first=inf_anim_frame(s,t,facing); bool changed=false;
    for(int i=0;i<20;++i) changed |= inf_anim_tick(s,t,facing,c)!=first;
    CHECK(changed);
    c.shoveling=false; c.moving=true; c.carrying=true;
    inf_anim_tick(s,t,facing,c); CHECK(s.seq==kSqCarry);
    c.moving=false; c.carrying=false;
    inf_anim_tick(s,t,facing,c); CHECK(s.seq==kSqReady);
}
}
TEST_CASE("Yuri core: shot captures and controller death restores original owner") {
    Rig r; World w(r.map);
    auto c = unit("YURI"); c.w7.mind_control = true;
    w.add_combat_unit(2,2,0,c); w.add_combat_unit(4,2,1,unit("TANK"));
    w.prepare_skirmish_mind_control(); CHECK(w.units()[0].w_mind_control);
    CHECK(w.units()[1].house == 1); // being in range alone never captures
    w.units()[0].w7.mc_shot = 1; w.resolve_skirmish_mind_control();
    CHECK(w.units()[1].house == 0); CHECK(w.units()[1].w7.mc_controller == 0);
    w.units()[0].alive = false; w.prepare_skirmish_mind_control();
    CHECK(w.units()[1].house == 1); CHECK(w.units()[1].w7.mc_controller == -1);
}
TEST_CASE("Yuri core: capacity one swaps; multiple capacity refuses extra; immunity refuses") {
    Rig r; World w(r.map); auto c = unit("YURI"); c.w7.mind_control = true;
    w.add_combat_unit(2,2,0,c);
    w.add_combat_unit(4,2,1,unit("A")); w.add_combat_unit(6,2,1,unit("B"));
    w.units()[0].w7.mc_shot = 1; w.resolve_skirmish_mind_control();
    w.units()[0].attack_order = true;
    w.units()[0].w7.mc_shot = 2; w.resolve_skirmish_mind_control();
    CHECK(w.units()[1].house == 1); CHECK(w.units()[2].house == 0);
    w.units()[0].w7.t.mc_capacity = 2;
    w.units()[0].w7.mc_shot = 1; w.resolve_skirmish_mind_control();
    REQUIRE(w.units()[0].w7.mc_victims.size() == 2);
    auto immune = unit("IMMUNE"); immune.tg.immune_psionics = true;
    w.add_combat_unit(8,2,1,immune);
    w.units()[0].w7.mc_shot = 3; w.resolve_skirmish_mind_control();
    CHECK(w.units()[3].house == 1);
}
TEST_CASE("Yuri core: ordinary combat shot captures without damaging victim") {
    Rig r; World w(r.map); auto c = unit("YURI");
    c.w7.mind_control = true; c.damage = 1; c.rof = 20; c.range = 6;
    w.add_combat_unit(2,2,0,c); w.add_combat_unit(4,2,1,unit("TANK"));
    w.units()[0].target = 1; w.units()[0].attack_order = true;
    for (int i = 0; i < 30 && w.units()[1].house == 1; ++i) w.step(1.0/15.0);
    CHECK(w.units()[1].house == 0); CHECK(w.units()[1].hp == 100);
    const auto state = save::save_world(w);
    World loaded(r.map); REQUIRE(save::load_world(loaded,state));
    CHECK(loaded.units()[1].w7.mc_original_house == 1);
    loaded.units()[0].alive = false; loaded.prepare_skirmish_mind_control();
    CHECK(loaded.units()[1].house == 1);
}
TEST_CASE("Yuri core: deployed miner spawns workers and harvests finite ore") {
    Rig r; World w(r.map); BuildingTypeDef d;
    d.id = "YAREFN"; d.fw = d.fh = 2; d.strength = 100;
    d.slave_count = 2; d.slave_regen_rate = 3; d.slave_storage = 1; d.slave_combat = unit("SLAV");
    w.set_economy({d},{0},OreValues{},1000,0,0);
    w.add_building(0,8,8,2,2,0,100,0,0,true);
    w.buildings()[0].state = Building::State::Idle;
    w.step_yuri_workers(); REQUIRE(w.units().size() == 2);
    CHECK(w.units()[0].is_harvester); CHECK(w.units()[0].w7.slave_manager == 0);
    w.ore().set(5,5,OreKind::Ore,10,0);
    for (int i = 0; i < 1800; ++i) w.step(1.0/15.0);
    CHECK(w.house_credits(0) > 0);
    CHECK(w.ore().density_at(5,5) < 10);
    const auto state = save::save_world(w);
    World loaded(r.map); REQUIRE(save::load_world(loaded,state));
    CHECK(save::save_world(loaded) == state);
    for (int i = 0; i < 30; ++i) { w.step(1.0/15.0); loaded.step(1.0/15.0); }
    CHECK(save::save_world(w) == save::save_world(loaded));
    w.buildings()[0].alive = false; w.step_yuri_workers();
    CHECK(w.units()[0].w7.slave_manager == -1); CHECK_FALSE(w.units()[0].is_harvester);
}

TEST_CASE("Yuri core: ground force fire stops at range instead of marching into target") {
    Rig r; World w(r.map); auto c=unit("TANK");c.damage=20;c.rof=10;c.range=3;
    w.add_combat_unit(2,2,0,c);
    w.order_unit_move(0,18,2);
    w.order_attack_cell(0,10,2);
    bool fired=false;
    for(int i=0;i<300;++i){w.step(1.0/15.0);fired=fired||w.units()[0].fire_flash>0;}
    CHECK(fired);CHECK(w.units()[0].fx<9);CHECK_FALSE(w.units()[0].moving());
    const double x=w.units()[0].fx,y=w.units()[0].fy;
    for(int i=0;i<60;++i)w.step(1.0/15.0);
    CHECK(w.units()[0].fx==x);CHECK(w.units()[0].fy==y);
}
TEST_CASE("Yuri core: Psychic Tower combat captures to capacity and releases after power off") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAPSYT";d.mind_control=true;
    d.strength=100;d.armed=true;d.weapon[0].valid=true;d.weapon[0].damage=2;
    d.weapon[0].range=10;d.weapon[0].rof=1;d.weapon[0].verses.assign(11,100);
    w.set_building_types({d});w.add_building(0,8,8,1,1,0,100,0,0,true);
    for(int i=0;i<3;++i)w.add_combat_unit(3+i,3,1,unit("ENEMY"));
    for(int i=0;i<60;++i)w.step(1.0/15.0);
    REQUIRE(w.buildings()[0].w7.mc_victims.size()==2);
    int controlled=0;for(const auto& u:w.units()){if(u.house==0)++controlled;CHECK(u.hp==100);}
    CHECK(controlled==2);
    const auto state=save::save_world(w);World loaded(r.map);REQUIRE(save::load_world(loaded,state));
    loaded.buildings()[0].power_off=true;loaded.step(1.0/15.0);
    CHECK(loaded.buildings()[0].w7.mc_victims.empty());
    for(const auto& u:loaded.units()){CHECK(u.house==1);CHECK(u.w7.mc_building==-1);}
}

TEST_CASE("Yuri core: captured units leave their previous AI team immediately") {
    Rig r;World w(r.map);AiHouseConfig cfg;cfg.team.enabled=true;
    w.enable_ai_house(1,cfg);w.add_combat_unit(2,2,1,unit("TANK"));
    auto* state=const_cast<AiTeamState*>(w.ai_team_state(1));REQUIRE(state);
    TeamInstance team;team.members={0};team.member_types={0};state->teams.push_back(team);
    w.units()[0].tg.team_aggressive=true;w.units()[0].tg.team_suicide=true;
    w.set_unit_owner(0,0);
    CHECK(state->teams[0].members.empty());CHECK(state->teams[0].member_types.empty());
    CHECK_FALSE(w.units()[0].tg.team_aggressive);CHECK_FALSE(w.units()[0].tg.team_suicide);
}

#include "doctest.h"
#include "world.hpp"
#include "harvest.hpp"
#include "savegame.hpp"
#include "weapon_resolve.hpp"
#include "ra2assets/rules.hpp"
#include "weapon_sound_loop.hpp"
#include "audio_mixer.hpp"
#include "weapon_unit.hpp"
#include "report_probe.hpp"
#include "psychic_links.hpp"
#include "psychic_sensor.hpp"
#include "presentation_policy.hpp"
#include "weapon_art.hpp"
#include "ai_base.hpp"
#include "ground_presentation.hpp"
#include "production.hpp"
#include "asset_fixture.hpp"
#include "projectile_presentation.hpp"
#include "building_transition.hpp"
#include "faction_support.hpp"
#include "command.hpp"
#include "selection.hpp"
#include "input.hpp"
#include "slave_passage.hpp"
#include "unit_presentation.hpp"
#include "faction_art.hpp"
#include "disk_laser.hpp"
#include <fstream>
#include <iterator>
using namespace ra2engine;
TEST_CASE("psychic intent: dashed segments preserve direction gaps and bounded work") {
    const auto d=psychic_intent_dashes(0,0,100,0);REQUIRE(d.size()==10);
    CHECK(d[0]==std::array<double,4>{0,0,6,0});CHECK(d[1][0]>d[0][2]);
    const auto diagonal=psychic_intent_dashes(10,10,-20,-20);CHECK_FALSE(diagonal.empty());
    for(const auto& s:diagonal){CHECK(s[2]<=s[0]);CHECK(s[3]<=s[1]);}
    CHECK(psychic_intent_dashes(0,0,1e7,0).size()<=513);
    CHECK(psychic_intent_dashes(1,1,1,1).empty());
}
TEST_CASE("Floating Disc laser: fixed rim point is nearest target on vanilla radius") {
    for(const auto& d:{std::array<double,2>{1,0},{0,1},{-1,-1},{10,-3}}) {
        const auto r=disk_laser_rim(d[0],d[1]);CHECK(std::hypot(r[0],r[1])==doctest::Approx(240.0/256));
        CHECK(r[0]*d[1]-r[1]*d[0]==doctest::Approx(0));CHECK(r[0]*d[0]+r[1]*d[1]>0);
    }
    const auto r=disk_laser_rim(0,0);CHECK(std::isfinite(r[0]));CHECK(std::isfinite(r[1]));
}

TEST_CASE("Tank Bunker presentation: platform offset is continuous and never changes world coordinates") {
    CHECK(bunker_platform_offset(8,8,8,8,2,2)==0.5);
    CHECK(bunker_platform_offset(9,8,8,8,2,2)==0);
    for(int i=0;i<100;++i) {
        const double x=7+i/100.0;
        const double a=x+bunker_platform_offset(x,8,8,8,2,2);
        const double b=x+0.01+bunker_platform_offset(x+0.01,8,8,8,2,2);
        CHECK(std::abs(b-a)<0.02);
    }
    CHECK(bunker_contains_vehicle(8,8,8,8,2,2));
    CHECK_FALSE(bunker_contains_vehicle(8,9.5,8,8,2,2));
    CHECK_FALSE(bunker_contains_vehicle(7.49,8,8,8,2,2));
    CHECK_FALSE(bunker_vehicle_in_front(8,8,8,8,2,2));
    CHECK_FALSE(bunker_vehicle_in_front(7.5,8,8,8,2,2));
    CHECK(bunker_vehicle_in_front(9,9,8,8,2,2));
}

TEST_CASE("weapon art: correct image, numbered, secondary and elite muzzle entries") {
    const auto ini=ra2assets::Ini::parse(
        "[LTNK]\nPrimaryFireFLH=225,0,100\nSecondaryFireFLH=80,15,90\n"
        "ElitePrimaryFireFLH=250,0,120\nWeapon1FLH=160,30,150\n"
        "Weapon2FLH=160,-30,150\nEliteWeapon2FLH=170,-35,160\n"
        "Weapon3FLH=bad\nWeapon4FLH=0,0,0\nTurretOffset=-16\n"
        "[RULESID]\nPrimaryFireFLH=999,999,999\n");
    const auto art=ra2assets::Art::load(ini);
    CHECK(weapon_art_flh(art,"LTNK",0,false,false)==std::array<int,3>{225,0,100});
    CHECK(weapon_art_flh(art,"LTNK",0,true,false)==std::array<int,3>{250,0,120});
    CHECK(weapon_art_flh(art,"LTNK",1,true,false)==std::array<int,3>{80,15,90});
    CHECK(weapon_art_flh(art,"LTNK",1,true,true)==std::array<int,3>{170,-35,160});
    CHECK(weapon_art_flh(art,"LTNK",2,false,true)==std::array<int,3>{225,0,100});
    CHECK(weapon_art_flh(art,"LTNK",3,false,true)==std::array<int,3>{0,0,0});
    const auto ri=ra2assets::Ini::parse("[RULESID]\nImage=LTNK\nWeaponCount=6\n");
    const auto rules=ra2assets::Rules::load(ri);
    UnitCombat c;c.image="LTNK";c.proj.kind=ProjKind::Ballistic;c.damage=37;
    c.gattling.weapons.resize(6);c.gattling.elite_weapons.resize(6);
    bind_unit_weapon_art(c,art,rules,"RULESID");
    CHECK(c.proj.flh[0]==160);CHECK(c.secondary.proj.flh[1]==-30);
    CHECK(c.elite_secondary.proj.flh[2]==160);
    CHECK(c.gattling.weapons[1].proj.flh[1]==-30);
    CHECK(c.gattling.elite_weapons[1].proj.flh[2]==160);
    CHECK(c.proj.turret_offset==-16);CHECK(c.proj.kind==ProjKind::Ballistic);CHECK(c.damage==37);
}
TEST_CASE("projectile presentation: first interval starts at launch and subsequent ticks interpolate") {
    ProjectilePresentation motion;Projectile p;p.attacker=4;p.weapon_id="120mm";
    p.launch_frame=20;p.x=1100;p.y=500;p.z=140;
    ProjectileEvent e;e.kind=ProjectileEvent::Launch;e.attacker=4;e.frame=20;
    e.x=1000;e.y=500;e.z=100;
    motion.capture({p},{e});
    CHECK(motion.sample(0,p,0)==std::array<double,3>{1000,500,100});
    CHECK(motion.sample(0,p,0.5)==std::array<double,3>{1050,500,120});
    CHECK(motion.sample(0,p,1)==std::array<double,3>{1100,500,140});
    CHECK(p.x==1100);CHECK(p.z==140);
    p.x=1200;p.z=170;motion.capture({p},{});
    CHECK(motion.sample(0,p,0)==std::array<double,3>{1100,500,140});
    CHECK(motion.sample(0,p,0.5)==std::array<double,3>{1150,500,155});
    CHECK(motion.sample(0,p,2)==std::array<double,3>{1200,500,170});
    motion.capture({},{});CHECK(motion.size()==0);
    motion.clear();CHECK(motion.sample(0,p,0)==std::array<double,3>{1200,500,170});
}
TEST_CASE("projectile presentation: reordered bullets, salvos and separate defense launches stay independent") {
    ProjectilePresentation m;Projectile a;a.attacker=1;a.launch_frame=3;a.x=100;
    Projectile b=a;b.attacker=2;b.x=1000;m.capture({a,b},{});
    a.x=200;b.x=1200;m.capture({b,a},{});
    CHECK(m.sample(0,b,0.5)[0]==1100);CHECK(m.sample(1,a,0.5)[0]==150);
    a.attacker=b.attacker=-1;a.attacker_building=10;b.attacker_building=20;
    a.launch_frame=b.launch_frame=9;a.x=200;b.x=1200;
    ProjectileEvent e;e.frame=9;e.attacker=-1;e.x=100;
    auto f=e;f.x=1100;m.clear();m.capture({b,a},{e,f});
    CHECK(m.sample(0,b,0)[0]==1100);CHECK(m.sample(1,a,0)[0]==100);
    a.attacker=1;a.attacker_building=-1;b=a;b.x=220;
    e.attacker=f.attacker=1;e.x=100;f.x=120;
    m.clear();m.capture({a,b},{e,f});
    CHECK(m.size()==2);CHECK(m.sample(0,a,0)[0]!=m.sample(1,b,0)[0]);
    // One salvo sibling expires; its survivor must retain its own segment.
    a.x=100;b.x=300;m.clear();m.capture({a,b},{});
    b.x=320;b.vx=20;m.capture({b},{});
    CHECK(m.sample(0,b,0)[0]==300);CHECK(m.sample(0,b,0.5)[0]==310);
}

TEST_CASE("presentation policy: repeated queue and rally speech has independent three-second windows") {
    SpeechCooldown gate;
    CHECK(gate.allow("building",0));CHECK_FALSE(gate.allow("building",1000));
    CHECK_FALSE(gate.allow("building",2999));CHECK(gate.allow("building",3000));
    CHECK(gate.allow("rally",3001));CHECK_FALSE(gate.allow("rally",4000));
    CHECK(gate.allow("training",4000));gate.clear();CHECK(gate.allow("rally",4000));
}
TEST_CASE("presentation policy: nuclear visual follows existing up and down strike phases") {
    CHECK(nuke_visual_height(0,200,200)==0);
    CHECK(nuke_visual_height(10,200,200)==55);
    CHECK(nuke_visual_height(200,200,200)==20000);
    CHECK(nuke_visual_height(300,200,200)==10000);
    CHECK(nuke_visual_height(400,200,200)==0);
    CHECK(nuke_visual_height(500,200,200)==0);
}
TEST_CASE("Tank Bunker: drive state is reset on attachment and ejection without changing type settings") {
    // Fixture is declared below; standalone minimal terrain keeps this test isolated.
    ra2assets::MapFile map;
    auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
    for(int y=0;y<20;++y)for(int x=0;x<20;++x)cells.push_back({x,y,0,0,0});
    World w(map);BuildingTypeDef d;d.id="NATBNK";d.tank_bunker=true;
    d.can_be_occupied=true;d.max_occupants=1;d.strength=1000;d.fw=d.fh=2;
    w.set_building_types({d});w.add_building(0,8,8,2,2,0,1000,0,0,true);
    UnitCombat tank;tank.strength=200;tank.is_vehicle=true;tank.w7.bunkerable=true;
    w.add_combat_unit(7,8,0,tank);
    auto poison=[&](){auto& u=w.units()[0];u.loco.trk=5;u.loco.accum=20;u.loco.path_delay=80;u.wait_time=5;u.loco.t.accel_factor=0.07;};
    poison();REQUIRE(w.campaign_occupy_now(0,0));
    CHECK(w.units()[0].loco.trk==-1);CHECK(w.units()[0].loco.accum==0);
    CHECK(w.units()[0].wait_time==0);CHECK(w.units()[0].loco.t.accel_factor==doctest::Approx(0.07));
    poison();w.unload_occupants(0);
    CHECK(w.buildings()[0].bunker_exit_left==18);
    w.buildings()[0].bunker_exit_left=0;w.unload_occupants(0);
    CHECK(w.units()[0].garrison_of==-1);CHECK(w.units()[0].loco.trk==-1);
    CHECK(w.units()[0].loco.path_delay==0);CHECK(w.units()[0].wait_time==0);
    World restored(map);REQUIRE(save::load_world(restored,save::save_world(w)));
    for(int i=0;i<120;++i){w.step(1.0/15.0);restored.step(1.0/15.0);}
    CHECK(save::save_world(w)==save::save_world(restored));
}
TEST_CASE("Yuri roster: real structures exclude shared scenery and dormant prototypes") {
    for(const char* id:{"YACNST","YAREFN","NAPSIS","NACLON","GAFWLL","NATBNK"})
        CHECK(vanilla_yuri_building(id));
    for(const char* id:{"GASAND","CAFNCB","CAFNCP","GALAMP","GAWALL","NAWALL","YACOMD","YAROCK","YADEPT"})
        CHECK_FALSE(vanilla_yuri_building(id));
}
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
TEST_CASE("missile launchers: explicit V3 and Boomer land orders launch spawned rockets") {
    for(bool boomer : {false,true}) {
        Rig r;World w(r.map);
        auto rocket=unit(boomer ? "CMISL" : "V3ROCKET");
        rocket.is_aircraft=true;rocket.is_vehicle=true;rocket.an.spawned=true;
        rocket.an.missile=true;rocket.an.rocket_kind=boomer ? 2 : 0;rocket.an.rocket_speed=38;
        w.register_an_type(rocket.image,rocket);
        auto launcher=unit(boomer ? "BSUB" : "V3");launcher.is_vehicle=true;
        launcher.damage=1;launcher.range=18;launcher.rof=150;launcher.omni_fire=true;
        launcher.an.spawner_slot=boomer ? 1 : 0;launcher.an.spawns=rocket.image;
        launcher.an.spawns_number=boomer ? 2 : 1;launcher.an.spawner_range=20;
        launcher.an.spawn_regen_frames=400;
        if(boomer) {
            launcher.an.naval_targeting=7;launcher.an.land_targeting=2;
            launcher.secondary.valid=true;launcher.secondary.damage=25;
            launcher.secondary.range=20;launcher.secondary.rof=50;
            launcher.secondary.verses.assign(11,100);launcher.secondary.omni_fire=true;
        }
        w.add_combat_unit(2,2,0,launcher);w.add_combat_unit(14,2,1,unit("TARGET"));
        w.group_attack(0,{0},1);
        for(int f=0;f<180 && w.units().size()<3;++f)w.step(1.0/15.0);
        REQUIRE(w.units().size()>=3);
        CHECK(w.units()[0].w_active==(boomer ? 1 : 0));
        CHECK(w.units()[2].an.t.missile);CHECK(w.units()[2].an.owner==0);
    }
}
TEST_CASE("diagnostics: unit probes preserve saved state and catch broken references") {
    Rig r; World w(r.map);
    w.add_combat_unit(2,2,0,unit("SLAV"));
    auto& worker=w.units()[0];
    worker.w7.slave_manager=999;
    worker.path={{2,2},{3,2}}; worker.path_idx=1; worker.wait_time=4;
    worker.bales_ore=3;
    BuildingTypeDef aa;aa.id="NASAM";aa.armed=true;
    aa.weapon[0].valid=true;aa.weapon[0].ag=false;aa.weapon[0].aa=true;
    w.set_economy({aa},{},OreValues{},1000,0,0);
    w.add_building(0,10,10,1,1,0,100,0,0,true);
    const auto before=save::save_world(w);
    const auto root=std::filesystem::temp_directory_path()/"ra2-probe-stability";
    struct Stop { ~Stop(){diag::stop();} } stop;
    REQUIRE(diag::start(root.u8string()));
    diag::probe_world(w);
    CHECK(save::save_world(w)==before);
    diag::flush();
    std::ifstream stream(root/"engine-current.log",std::ios::binary);
    const std::string log{std::istreambuf_iterator<char>(stream),{}};
    CHECK(log.find("invalid_slave_manager=999")!=std::string::npos);
    CHECK(log.find("blocked_over_3s=1")!=std::string::npos);
    CHECK(log.find("ROUTE unit=0 next=3,2")!=std::string::npos);
    CHECK(log.find("ground=0 air=1")!=std::string::npos);
    CHECK(log.find("ACQUIRE unit=0 mission=")!=std::string::npos);
    worker.path_idx=900;
    diag::probe_world(w); // Invalid index must be reported without dereferencing it.
}

TEST_CASE("diagnostics: nearest enemy context reports fog and slot without changing the world") {
 Rig r;World w(r.map);auto c=unit("BRUTE");c.damage=100;c.range=1.4;
 w.add_combat_unit(8,8,0,c);w.add_combat_unit(11,8,1,unit("TARGET"));
 w.units()[1].seen_by_mask=0;
 const auto before=save::save_world(w);
 const auto root=std::filesystem::temp_directory_path()/"ra2-acquire-probe";
 struct Stop {~Stop(){diag::stop();}} stop;
 REQUIRE(diag::start(root.u8string()));diag::probe_world(w);diag::flush();
 CHECK(save::save_world(w)==before);
 std::ifstream stream(root/"engine-current.log",std::ios::binary);
 const std::string log{std::istreambuf_iterator<char>(stream),{}};
 CHECK(log.find("ACQUIRE_CANDIDATE unit=0 candidate=1 image=TARGET distance=3.000 seen=0")!=std::string::npos);
 CHECK(log.find("sampled=2 total=2")!=std::string::npos);
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
    ra2assets::Palette pal{};
    for(int i=0;i<256;++i)pal[i]={static_cast<unsigned char>(i),static_cast<unsigned char>(i),static_cast<unsigned char>(i),255};
    w.set_house(1,World::HouseRec{World::Side::Soviet,2,"Russians",-1,true});
    w.set_house(2,World::HouseRec{World::Side::Allied,5,"Americans",-1,true});
    const auto color=psychic_intent_color(w,pal,1),expected=w.house_palette(pal,1)[24];
    CHECK(color.r==expected.r);CHECK(color.g==expected.g);CHECK(color.b==expected.b);
    const auto other=psychic_intent_color(w,pal,2);
    CHECK((color.r!=other.r || color.g!=other.g || color.b!=other.b));
    // Presentation queries cannot alter the simulation/save state.
    const auto before=save::save_world(w);(void)psychic_intent_color(w,pal,2);
    CHECK(save::save_world(w)==before);

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
TEST_CASE("Yuri core: infantry caps never auto-swap and deploy preserves ownership") {
    for (const char* id : {"YURI","YURIPR"}) {
        Rig r;World w(r.map);auto c=unit(id);c.w7.mind_control=true;
        c.deploy_fire=true;c.deploy_fire_slot=1;c.w7.deploy_area_fire=true;
        c.secondary.valid=true;c.secondary.range=1;c.secondary.damage=0;
        w.add_combat_unit(2,2,0,c);w.add_combat_unit(5,2,1,unit("A"));w.add_combat_unit(7,2,1,unit("B"));
        w.units()[0].w7.mc_shot=1;w.resolve_skirmish_mind_control();
        for(int n=0;n<20;++n){w.units()[0].w7.mc_shot=2;w.resolve_skirmish_mind_control();}
        CHECK(w.units()[0].w7.mc_victims.size()==1);CHECK(w.units()[1].house==0);CHECK(w.units()[2].house==1);
        REQUIRE(w.order_deploy(0));CHECK(w.units()[1].house==0);
        REQUIRE(w.units()[0].w7.mc_victims.size()==1);
        CHECK(w.units()[0].w7.mc_victims[0]==1);
        CHECK(w.units()[1].w7.mc_controller==0);
        const auto saved=save::save_world(w);World loaded(r.map);REQUIRE(save::load_world(loaded,saved));
        CHECK(loaded.units()[1].house==0);CHECK(loaded.units()[1].w7.mc_controller==0);
        w.units()[0].w7.mc_shot=2;w.resolve_skirmish_mind_control();CHECK(w.units()[2].house==1);
    }
}
TEST_CASE("Yuri core: enslaved workers reject player commands but retain internal movement") {
    Rig r;World w(r.map);auto slave=unit("SLAV");slave.is_infantry=true;
    w.add_combat_unit(2,2,0,slave);w.set_order_issuer(0);
    for(int manager:{0,-2}) {
        auto& u=w.units()[0];u.w7.slave_manager=manager;
        CHECK(Selection::unselectable(u));
        w.order_unit_move(0,6,2);REQUIRE(u.moving());
        const auto path=u.path;
        apply_command(w,make_whom(w,EventType::Idle,{0}));
        CHECK(u.path.size()==path.size());CHECK(u.moving());
        apply_command(w,make_mega(w,{0},Mission::Move,Target{},Target::cell(2,8)));
        CHECK(u.path.back().x==6);CHECK(u.path.back().y==2);
        apply_command(w,make_whom(w,EventType::Scatter,{0}));
        CHECK(u.path.back().x==6);CHECK(u.path.back().y==2);
    }
    w.units()[0].w7.slave_manager=-1;
    CHECK_FALSE(Selection::unselectable(w.units()[0]));
    apply_command(w,make_whom(w,EventType::Idle,{0}));
    CHECK_FALSE(w.units()[0].moving());
}
TEST_CASE("Yuri core: surviving slaves become controllable by their miner's killer") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;
    d.slave_count=2;d.slave_combat=unit("SLAV");d.slave_combat.is_infantry=true;
    w.set_economy({d},{0},OreValues{},1000,0,0);
    w.add_building(0,8,8,2,2,0,100,0,0,true);w.step_yuri_workers(false);
    REQUIRE(w.units().size()==2);
    auto killer=unit("LTNK");w.add_combat_unit(2,2,1,killer);
    w.units()[1].alive=false;w.units()[1].die_time=0;
    w.buildings()[0].alive=false;w.buildings()[0].state=Building::State::Dead;
    w.buildings()[0].killed_by=2;w.step_yuri_workers(false);
    CHECK(w.units()[0].house==1);CHECK(w.units()[0].w7.slave_manager==-1);
    CHECK_FALSE(w.units()[0].is_harvester);CHECK_FALSE(Selection::unselectable(w.units()[0]));
    w.set_order_issuer(0);
    apply_command(w,make_mega(w,{0},Mission::Move,Target{},Target::cell(3,3)));
    CHECK_FALSE(w.units()[0].moving());
    w.set_order_issuer(1);
    auto order=make_mega(w,{0},Mission::Move,Target{},Target::cell(3,3));order.house=1;
    apply_command(w,order);
    REQUIRE(w.units()[0].moving());CHECK(w.units()[0].path.back().x==3);
    CHECK_FALSE(w.units()[1].alive); // dead workers are not resurrected/transferred
    const auto state=save::save_world(w);World loaded(r.map);REQUIRE(save::load_world(loaded,state));
    CHECK(loaded.units()[0].house==1);CHECK(loaded.units()[0].w7.slave_manager==-1);
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
    tank.occupy_weapon.valid=true;tank.occupy_weapon.damage=20;tank.occupy_weapon.rof=10;tank.occupy_weapon.range=5;
    tank.occupy_weapon.verses.assign(11,100);
    w.add_combat_unit(7,8,0,tank);w.add_combat_unit(6,8,0,tank);w.add_combat_unit(12,8,1,unit("TARGET"));
    CHECK(w.can_occupy(0,0));REQUIRE(w.campaign_occupy_now(0,0));
    CHECK_FALSE(w.can_occupy(1,0));CHECK(w.units()[0].garrison_of==0);
    w.buildings()[0].garrison_target=2;w.step(1.0/15.0);
    CHECK(w.units()[2].hp==74);CHECK(w.garrison_range(0)==7);
    const auto state=save::save_world(w);World loaded(r.map);REQUIRE(save::load_world(loaded,state));
    CHECK(loaded.units()[0].garrison_of==0);CHECK(loaded.btypes()[0].tank_bunker);
    w.unload_occupants(0);CHECK(w.units()[0].cargo_of==kGarrisonCargo);
    for(int n=0;n<18;++n)w.step(1.0/15.0);
    CHECK(w.units()[0].cargo_of<0);CHECK(w.units()[0].alive);CHECK(w.buildings()[0].house==0);
    w.units()[1].w7.t.bunkerable=false;CHECK_FALSE(w.can_occupy(1,0));
    w.units()[1].w7.t.bunkerable=true;w.units()[1].house=1;CHECK_FALSE(w.can_occupy(1,0));
}
TEST_CASE("building force fire: Tank Bunker fires occupant weapon and capture cancels explicit target") {
 Rig r;World w(r.map);BuildingTypeDef d;d.id="NATBNK";d.tank_bunker=true;
 d.can_be_occupied=d.can_occupy_fire=true;d.max_occupants=1;d.fw=d.fh=2;d.strength=1000;d.rot=100;
 w.set_building_types({d});w.add_building(0,8,8,2,2,0,1000,0,0,true);
 auto tank=unit("LTNK");tank.is_vehicle=true;tank.w7.bunkerable=true;
 tank.occupy_weapon.valid=true;tank.occupy_weapon.id="OwnTankGun";tank.occupy_weapon.damage=20;
 tank.occupy_weapon.rof=10;tank.occupy_weapon.range=5;tank.occupy_weapon.verses.assign(11,100);
 auto target=unit("TARGET");target.strength=1000;
 w.add_combat_unit(7,8,0,tank);w.add_combat_unit(12,8,0,target);
 REQUIRE(w.campaign_occupy_now(0,0));REQUIRE(w.order_building_attack(0,0,1,-1,-1,-1,true));
 for(int i=0;i<30;++i)w.step(1.0/15);
 CHECK(w.units()[1].hp<1000);bool fired=false;
 for(const auto& e:w.take_fire_events())if(e.building==0){CHECK(e.weapon=="OwnTankGun");fired=true;}
 CHECK(fired);w.buildings()[0].house=1;w.step(1.0/15);CHECK(w.buildings()[0].attack_order_kind==0);
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
TEST_CASE("Yuri core: building audio retains ground Force Fire and stops on low power") {
    CHECK(building_weapon_loop_active(true,false,false,3,0.4,true,1));
    CHECK(building_weapon_loop_active(true,false,false,3,0,true,200));
    CHECK_FALSE(building_weapon_loop_active(true,false,false,0,0.4,true,200));
    CHECK_FALSE(building_weapon_loop_active(true,true,true,1,0.4,true,200));
    CHECK_FALSE(building_weapon_loop_active(false,false,true,1,0.4,true,200));
    CHECK(building_weapon_loop_active(true,false,true,1,0,true,200));
    CHECK_FALSE(building_weapon_loop_active(true,false,true,1,0,false,0));
}
TEST_CASE("Yuri core: staged defense fire events drive audible mixer loops for ground and air") {
    // Tiny stage thresholds make this deterministic integration fixture fast;
    // the numbered ground/air pairs and Report names follow retail rules.
    const auto ini=ra2assets::Ini::parse(
        "[YAGGUN]\nIsGattling=yes\nWeaponCount=6\nWeaponStages=3\nStage1=2\nStage2=4\nStage3=6\n"
        "Weapon1=G1\nWeapon2=A1\nWeapon3=G2\nWeapon4=A2\nWeapon5=G3\nWeapon6=A3\n"
        "[G1]\nDamage=1\nROF=1\nRange=6\nProjectile=Ground\nReport=GattlingGunAttackLoop1\n"
        "[G2]\nDamage=1\nROF=1\nRange=6\nProjectile=Ground\nReport=GattlingGunAttackLoop2\n"
        "[G3]\nDamage=1\nROF=1\nRange=6\nProjectile=Ground\nReport=GattlingGunAttackLoop3\n"
        "[A1]\nDamage=1\nROF=1\nRange=12\nProjectile=Air\nReport=GattlingGunAttackLoop1\n"
        "[A2]\nDamage=1\nROF=1\nRange=12\nProjectile=Air\nReport=GattlingGunAttackLoop2\n"
        "[A3]\nDamage=1\nROF=1\nRange=12\nProjectile=Air\nReport=GattlingGunAttackLoop3\n"
        "[Ground]\nAG=yes\nAA=no\n[Air]\nAG=no\nAA=yes\n");
    const auto rules=ra2assets::Rules::load(ini);
    for(bool air:{false,true})for(bool force:{false,true}) {
        if(air && force)continue; // A ground-cell order cannot select an AA target.
        Rig r;World w(r.map);BuildingTypeDef d;d.id="YAGGUN";d.strength=500;
        fill_building_weapons(rules,d.id,d);REQUIRE(d.gattling.enabled());
        w.set_economy({d},{},OreValues{},1000,0,0);w.add_building(0,8,8,1,1,0,500,0,0,true);
        if(force)REQUIRE(w.order_building_attack(0,0,-1,-1,10,8,true));
        else {auto enemy=unit("TARGET");enemy.is_aircraft=air;enemy.strength=10000;w.add_combat_unit(10,8,1,enemy);}
        struct Audio {
            SoundBank bank;Mixer mixer;Audio():mixer(bank,1) {
                const int clip=bank.add(std::vector<int16_t>(20,1000));
                for(int i=1;i<=3;++i){MixerSound s;s.variants={clip};s.loop=true;s.volume=100;
                    mixer.register_sound("GattlingGunAttackLoop"+std::to_string(i),s);}
            }
            bool sound_loops(const std::string& s){auto* p=mixer.sound(s);return p && p->loop;}
            unsigned loop_at(const std::string& s,double,double){return mixer.play_at(s,0,8192);}
            void stop_loop(unsigned h){mixer.stop_voice(h);}
            bool update_loop(unsigned h,double,double){return mixer.update_voice(h,0,8192);}
            void play_at(const std::string& s,double,double,const char*){mixer.play_at(s,0,8192);}
        } audio;
        WeaponSoundLoop loop;bool sounded=false,top_stage=false;int shots=0;
        for(int frame=0;frame<50;++frame) {
            if(air)w.units()[0].altitude=500;
            w.step(1.0/15);const auto& b=w.buildings()[0];
            for(const auto& e:w.take_fire_events())if(e.building==0) {
                ++shots;CHECK(e.slot==(air?1:0));CHECK_FALSE(e.weapon.empty());
                const auto report=ini.get(e.weapon,"Report");CHECK_FALSE(report.empty());
                if(report=="GattlingGunAttackLoop3")top_stage=true;
                loop.shot(audio,report,8,8);
            }
            loop.update(audio,building_weapon_loop_active(b.alive && !b.power_off,b.def_offline,
                b.def_target>=0 || b.w6.def_target_building>=0,b.attack_order_kind,b.def_fire_flash,true,b.gattling_charge),8,8);
            int16_t samples[8]{};audio.mixer.render(samples,8);
            for(auto sample:samples)sounded|=sample!=0;
        }
        INFO("air="<<air<<" force="<<force);CHECK(shots>0);CHECK(sounded);CHECK(top_stage);
        const auto sim=save::save_world(w);loop.update(audio,false,8,8);
        CHECK(save::save_world(w)==sim);CHECK(loop.handle==0);
        int16_t sample=1;audio.mixer.render(&sample,1);CHECK(sample==0);
    }
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
    REQUIRE(w.units()[0].moving());
    CHECK(std::abs(w.units()[0].path.back().x-17)<=2);
    CHECK(w.can_deploy_at(0,w.units()[0].path.back().x,w.units()[0].path.back().y));
    CHECK(w.units()[0].deploy_mission==4);
    w.order_stop(0);CHECK(w.units()[0].manual_hold);
    CHECK(w.units()[0].deploy_mission==0);
    w.step(1.0/15.0);CHECK(w.buildings().empty());
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

TEST_CASE("Battle Bunker: empty and manually evacuated structure keeps its owner and repair access") {
    Rig r;World w(r.map);
    w.set_house(0,{World::Side::Soviet,0,"Player",0,true});
    w.set_house(2,{World::Side::Neutral,2,"Neutral",2,false});
    BuildingTypeDef d;d.id="NABNKR";d.strength=600;d.can_be_occupied=true;
    d.max_occupants=5;d.click_repairable=true;
    w.set_building_types({d});
    const int bi=w.add_building(0,8,8,1,1,0,600,0,0,true);
    auto c=unit("CONS");c.is_infantry=true;c.occupier=true;
    w.add_combat_unit(7,8,0,c);
    REQUIRE(w.campaign_occupy_now(0,bi));
    w.unload_occupants(bi);w.step(1.0/15.0);
    CHECK(w.buildings()[bi].occupants.empty());
    CHECK(w.buildings()[bi].house==0);
    w.buildings()[bi].hp=1;
    CHECK(w.set_repair(bi,0,1));
    CHECK(w.buildings()[bi].being_repaired);
    CHECK_FALSE(w.set_repair(bi,2,1));
}

TEST_CASE("Battle Bunker: red damage retains infantry while civilian garrisons evacuate") {
    for(bool military:{false,true}) {
        Rig r;World w(r.map);
        w.set_house(0,{World::Side::Soviet,0,"Player",0,true});
        w.set_house(2,{World::Side::Neutral,2,"Neutral",2,false});
        BuildingTypeDef d;d.id=military?"NABNKR":"CIVILIAN";d.strength=600;
        d.can_be_occupied=true;d.max_occupants=5;
        w.set_building_types({d});const int bi=w.add_building(0,8,8,1,1,0,600,0,0,true);
        auto c=unit("CONS");c.is_infantry=true;c.occupier=true;
        w.add_combat_unit(7,8,0,c);REQUIRE(w.campaign_occupy_now(0,bi));
        w.buildings()[bi].hp=100;w.step(1.0/15.0);
        CHECK(w.buildings()[bi].occupants.empty()==!military);
        CHECK(w.units()[0].alive);
        CHECK((w.units()[0].garrison_of==bi)==military);
        CHECK(w.buildings()[bi].house==(military?0:2));
        World restored(r.map);
        REQUIRE(save::load_world(restored,save::save_world(w)));
        for(int frame=0;frame<120;++frame) {
            w.step(1.0/15.0);restored.step(1.0/15.0);
        }
        CHECK(save::save_world(restored)==save::save_world(w));
    }
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

TEST_CASE("Slave Miner move command: owner packs and drives; enemy cannot convert building") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;
    d.strength=200;d.slave_count=2;d.slave_combat=unit("SLAV");
    w.set_economy({d},{0},OreValues{},1000,0,0);
    auto mobile=unit("SMIN");mobile.deploys_into="YAREFN";mobile.rot=8;
    w.set_undeploy_unit(0,mobile);w.add_building(0,8,8,2,2,0,200,0,0,true);
    w.buildings()[0].hp=100;w.step_yuri_workers();const int index=int(w.units().size());
    auto cmd=make_mega(w,{},Mission::Move,Target{},Target::cell(14,14));
    cmd.whom.push_back(Target::building(0));cmd.house=1;apply_command(w,cmd);
    REQUIRE(w.buildings()[0].alive);CHECK(w.units().size()==index);
    World restored(r.map);REQUIRE(save::load_world(restored,save::save_world(w)));
    cmd.house=0;apply_command(w,cmd);apply_command(restored,cmd);
    REQUIRE(w.units().size()==index+1);CHECK(w.buildings()[0].state==Building::State::Dead);
    CHECK(w.units()[index].hp==50);REQUIRE(w.units()[index].moving());
    CHECK(w.units()[index].path.back().x==14);CHECK(w.units()[index].path.back().y==14);
    for(int t=0;t<180;++t){w.step(1.0/15.0);restored.step(1.0/15.0);}
    CHECK(save::save_world(w)==save::save_world(restored));
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

TEST_CASE("Slave Miner deploy: friendly blockers walk out before conversion; no teleport") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.strength=1000;d.slave_count=5;
    w.set_building_types({d});auto miner=unit("SMIN");miner.is_vehicle=true;miner.deploys_into="YAREFN";
    w.add_combat_unit(8,8,0,miner);w.add_combat_unit(9,8,0,unit("FRIEND"));
    REQUIRE_FALSE(w.can_deploy_at(0,8,8));REQUIRE(w.order_deploy_mcv(0));
    CHECK(w.units()[1].cell_x==9);CHECK(w.units()[1].cell_y==8);CHECK(w.units()[1].fx==9);
    CHECK(w.units()[1].moving());CHECK(w.units()[0].deploy_mission==1);
    for(int i=0;i<240 && w.buildings().empty();++i)w.step(1.0/15.0);
    REQUIRE(w.buildings().size()==1);CHECK(w.buildings()[0].cx==8);CHECK(w.buildings()[0].cy==8);
    CHECK_FALSE((w.units()[1].cell_x>=8 && w.units()[1].cell_x<10 && w.units()[1].cell_y>=8 && w.units()[1].cell_y<10));
    World restored(r.map);REQUIRE(save::load_world(restored,save::save_world(w)));
    for(int i=0;i<90;++i){w.step(1.0/15.0);restored.step(1.0/15.0);}
    CHECK(save::save_world(w)==save::save_world(restored));
}
TEST_CASE("Slave Miner deploy: enemy and static blockers are not pushed by footprint clearing") {
    for(bool enemy:{false,true}) {
        Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.strength=1000;d.slave_count=5;
        w.set_building_types({d});auto m=unit("SMIN");m.is_vehicle=true;m.deploys_into="YAREFN";
        w.add_combat_unit(8,8,0,m);w.add_combat_unit(9,8,enemy?1:0,unit("BLOCKER"));
        if(!enemy)w.units()[1].frozen=true;
        (void)w.order_deploy_mcv(0);
        CHECK_FALSE(w.units()[1].moving());CHECK(w.units()[1].cell_x==9);CHECK(w.units()[1].cell_y==8);
        CHECK_FALSE(w.can_deploy_at(0,8,8));
    }
}
TEST_CASE("bridge deck speed: Road on deck, river below, no fictitious hill at bridge end") {
    Rig r;World w(r.map);const int W=w.grid_w(),H=w.grid_h();
    std::vector<uint8_t> hb(W*H,0);hb[8*W+8]=1;w.set_high_bridges(hb);
    std::vector<uint8_t> land(W*H,0);land[8*W+8]=2;
    std::vector<int8_t> levels(W*H,4);levels[8*W+8]=0;
    MoveSpeedCtx c;c.armed=true;c.land=&land;c.level=&levels;c.world=&w;
    c.cost[0][1]=1;c.cost[1][1]=0.9f;c.cost[2][1]=0;c.cost[2][5]=1;
    c.up[0]=0.25;c.down[0]=0.5;
    Unit u;u.is_vehicle=true;u.land_speed_loco=true;u.speed_type=1;u.max_hp=u.hp=100;
    u.cell_x=7;u.cell_y=8;
    CHECK(land_speed_mult(c,u,W,H,8,8)==doctest::Approx(0.9));
    u.cell_x=8;u.on_deck=1;
    CHECK(land_speed_mult(c,u,W,H,9,8)==doctest::Approx(1.0));
    u.on_deck=0;u.is_naval=true;u.speed_type=5;u.movement_domain=MovementDomain::Water;
    levels[8*W+7]=0;u.cell_x=7;
    CHECK(land_speed_mult(c,u,W,H,8,8)==doctest::Approx(1.0));
}
TEST_CASE("bridge traversal: infantry and tracked units cross both directions on legal height layer") {
    for(bool vehicle:{false,true})for(bool reverse:{false,true}) {
        Rig r;
        // An unparsed synthetic map uses the linear lookup, not parse()'s
        // immutable coordinate index (the Rig appends cells after parsing).
        r.map=ra2assets::MapFile{};
        auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(r.map.cells());
        for(int y=0;y<40;++y)for(int x=0;x<40;++x)cells.push_back({x,y,0,0,0});
        for(auto& cell:const_cast<std::vector<ra2assets::MapCell>&>(r.map.cells()))cell.level=(cell.x>=18 && cell.x<=21)?0:4;
        REQUIRE(r.map.at(17,18));REQUIRE(r.map.at(22,18));
        World w(r.map);const int gw=w.grid_w(),gh=w.grid_h();
        std::vector<ra2assets::LandType> land(gw*gh,ra2assets::LandType::Clear);
        std::vector<uint8_t> hb(gw*gh,0),speed(gw*gh,0),ramp(gw*gh,0);
        for(int y=0;y<gh;++y)for(int x=18;x<=21;++x){land[y*gw+x]=ra2assets::LandType::Water;speed[y*gw+x]=2;}
        w.set_land_terrain(land);for(int x=18;x<=21;++x)hb[18*gw+x]=1;w.set_high_bridges(hb);
        World::LandSpeedRules rules;w.arm_land_speed(speed,ramp,rules);
        auto mover=unit(vehicle?"SMCV":"GI");mover.is_vehicle=vehicle;mover.speed_cps=4;
        mover.land_speed_loco=vehicle;mover.speed_type=1;mover.rot=8;
        w.add_combat_unit(reverse?24:15,18,0,mover);w.order_unit_move(0,reverse?15:24,18);
        REQUIRE(w.units()[0].moving());bool saw_deck=false;
        for(int t=0;t<500 && w.units()[0].moving();++t){w.step(1.0/15.0);saw_deck=saw_deck || w.units()[0].on_deck;}
        CHECK(saw_deck);CHECK(w.units()[0].cell_x==(reverse?15:24));CHECK(w.units()[0].on_deck==0);
    }
}
TEST_CASE("Gatling loop: keep audio over ROF gaps, stop on target loss and charge exhaustion") {
    CHECK(weapon_loop_active(true,true,0,true,200));
    CHECK_FALSE(weapon_loop_active(true,true,0,true,0));
    CHECK_FALSE(weapon_loop_active(true,false,0,true,200));
    CHECK_FALSE(weapon_loop_active(false,true,0,true,200));
    CHECK_FALSE(weapon_loop_active(true,true,0,false,200));
    CHECK(weapon_loop_active(true,true,0.2,false,0));
}
TEST_CASE("AI plan: TECH group recognizes the existing side-specific tech laboratory") {
    std::vector<BuildingTypeDef> defs(5);defs[0].id="YACNST";defs[1].id="YAPOWR";
    defs[2].id="YATECH";defs[3].id="SPECIAL";defs[3].prerequisite={"TECH"};
    defs[4].id="YABRCK";
    AiCatalog cat;cat.conyard=0;cat.power=1;cat.tech=2;cat.barracks=4;cat.defenses={3};
    AiRules rules;rules.extra_refineries=0;DifficultyMods mods;mods.defense_target=1;
    std::uint32_t rng=123;
    const auto plan=build_base_plan(BaseView{},cat,defs,rules,mods,&rng);
    int tech=-1,special=-1;for(int i=0;i<int(plan.size());++i){if(plan[i].type==2)tech=i;if(plan[i].type==3)special=i;}
    REQUIRE(tech>=0);REQUIRE(special>=0);CHECK(tech<special);
}
TEST_CASE("Flying Disc retail weapon: anti-air remains available after a secondary selection") {
    if(!ra2fixture::assets_available())return;
    const auto& rules=ra2fixture::rulesmd();const auto tw=resolve_type_weapons(rules,"DISK");
    REQUIRE(tw.slot[0].aa);REQUIRE_FALSE(tw.slot[1].aa);
    Rig r;World w(r.map);auto disk=unit("DISK");disk.is_vehicle=disk.is_aircraft=true;
    disk.damage=90;disk.range=7;disk.rof=80;disk.verses=tw.slot[0].verses;disk.proj=tw.slot[0].proj;
    fill_unit_combat_extras(rules,"DISK",tw,disk);
    w.add_combat_unit(2,2,0,disk);auto target=unit("ROCK");target.is_aircraft=true;w.add_combat_unit(4,2,1,target);
    w.units()[1].altitude=300;w.units()[1].flight_state=FlightState::Cruising;
    CHECK(unit_weapon_for_unit(w.units()[0],w.units()[1])==0);
    activate_weapon_slot(w.units()[0],1,nullptr);CHECK_FALSE(w.units()[0].w_aa);
    CHECK(unit_weapon_for_unit(w.units()[0],w.units()[1])==0);
}

TEST_CASE("Magnetron retail audit: building weapon damages structures, infantry remains ineligible") {
    if(!ra2fixture::assets_available())return;
    const auto& rules=ra2fixture::rulesmd();const auto tw=resolve_type_weapons(rules,"TELE");
    REQUIRE(tw.slot[1].valid);CHECK(tw.slot[1].damage==80);
    Rig r;World w(r.map);BuildingTypeDef d;d.id="TARGET";d.strength=1000;d.armor_ix=6;w.set_building_types({d});
    w.add_building(0,7,2,1,1,1,1000,6,0,true);
    auto magnet=unit("TELE");magnet.is_vehicle=true;magnet.w7.magnetic=true;magnet.rot=8;
    magnet.damage=5000;magnet.range=12;magnet.rof=20;magnet.verses=tw.slot[0].verses;magnet.omni_fire=true;
    fill_unit_combat_extras(rules,"TELE",tw,magnet);w.add_combat_unit(2,2,0,magnet);
    REQUIRE(unit_weapon_for_building(w.units()[0],6)==1);
    w.units()[0].target_building=0;w.units()[0].attack_order=true;
    for(int t=0;t<180;++t)w.step(1.0/15.0);
    INFO("slot="<<w.units()[0].w_active<<" damage="<<w.units()[0].w_damage<<" target="<<w.units()[0].target_building<<" range="<<w.units()[0].w_range<<" rearm="<<w.units()[0].rearm_frames);
    CHECK(w.buildings()[0].hp<1000);CHECK(w.buildings()[0].hp>0);
    Unit infantry;infantry.armor_ix=0;infantry.is_infantry=true;
    CHECK(unit_weapon_for_unit(w.units()[0],infantry)<0);
}

TEST_CASE("deploy feedback: automatic attempts stay quiet across save while manual failures report") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.slave_count=5;
    w.set_building_types({d});auto m=unit("SMIN");m.deploys_into="YAREFN";
    w.add_combat_unit(8,8,0,m);REQUIRE(w.order_deploy_mcv_auto(0));
    CHECK(w.units()[0].deploy_mission==3);
    World loaded(r.map);REQUIRE(save::load_world(loaded,save::save_world(w)));
    for(auto* world:{&w,&loaded}) {
        world->add_building(0,8,8,2,2,1,100,0,0,true);world->step(1.0/15.0);
        const auto events=world->take_deploy_events();
        CHECK(std::none_of(events.begin(),events.end(),[](const auto& e){return e.kind==World::DeployEvent::Kind::CannotDeploy;}));
        CHECK(world->units()[0].deploy_mission==0);
    }
    CHECK(save::save_world(w)==save::save_world(loaded));
    // Cover the entire relocation ring: a manual attempt must still report failure.
    for(int y=3;y<14;++y)for(int x=3;x<14;++x)if(w.building_at_cell(x,y)<0)w.add_building(0,x,y,1,1,1,100,0,0,true);
    CHECK_FALSE(w.order_deploy_mcv(0));const auto failed=w.take_deploy_events();
    REQUIRE(failed.size()==1);CHECK(failed[0].kind==World::DeployEvent::Kind::CannotDeploy);
    CHECK_FALSE(w.order_deploy_mcv_auto(0));CHECK(w.take_deploy_events().empty());
}
TEST_CASE("ConYard deploy command: all faction mappings obey option ownership and blockers") {
    for(const auto& name:{"GACNST","NACNST","YACNST"})for(bool enabled:{false,true}) {
        Rig r;World w(r.map);BuildingTypeDef d;d.id=name;d.fw=d.fh=4;d.strength=1000;d.conyard=true;
        w.set_building_types({d});auto m=unit("MCV");m.deploys_into=name;w.set_undeploy_unit(0,m);
        w.set_skirmish_session(true);w.set_mcv_redeploys(enabled);
        w.add_building(0,8,8,4,4,0,1000,0,0,true);w.buildings()[0].hp=500;
        auto c=make_building_event(EventType::Deploy,0);c.house=1;apply_command(w,c);CHECK(w.units().empty());
        c.house=0;apply_command(w,c);
        if(!enabled){CHECK(w.units().empty());CHECK(w.buildings()[0].alive);continue;}
        REQUIRE(w.units().size()==1);CHECK(w.units()[0].hp==50);CHECK(w.units()[0].deploys_into==0);
        CHECK(w.buildings()[0].state==Building::State::Dead);
    }
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YACNST";d.fw=d.fh=4;d.conyard=true;
    w.set_building_types({d});w.set_undeploy_unit(0,unit("PCV"));w.set_skirmish_session(true);w.set_mcv_redeploys(true);
    w.add_building(0,8,8,4,4,0,100,0,0,true);w.add_combat_unit(9,9,0,unit("BLOCKER"));
    CHECK_FALSE(w.can_pack_conyard(0));CHECK_FALSE(w.order_pack_conyard(0,0));CHECK(w.buildings()[0].alive);
}
TEST_CASE("ground presentation: legal slope Z interpolates and bridge deck cannot overpaint own tank") {
    for(int i=0;i<=10;++i){const double t=i/10.0;
        CHECK(segment_ground_level(8+t,8,8,8,9,8,2,3)==doctest::Approx(2+t));
        CHECK(segment_ground_level(9-t,8,9,8,8,8,3,2)==doctest::Approx(3-t));
        CHECK(segment_ground_level(8+t,8,8,8,9,8,4,4)==4);
    }
    CHECK(deck_unit_depth(7.7,8,8,8,true)>16.99);
    CHECK(deck_unit_depth(7.7,8,8,8,false)==15.7);
    ra2assets::MapFile map;auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
    for(int y=0;y<20;++y)for(int x=0;x<20;++x)cells.push_back({x,y,0,0,static_cast<std::uint8_t>(x<9?2:3)});
    World w(map);const int n=w.grid_w()*w.grid_h();w.arm_land_speed(std::vector<uint8_t>(n),std::vector<uint8_t>(n),World::LandSpeedRules{});
    w.add_combat_unit(9,8,0,unit("TANK"));auto& u=w.units()[0];u.path={{8,8},{9,8}};u.path_idx=1;u.fx=8.25;u.fy=8;
    CHECK(moving_ground_level(w,u)==doctest::Approx(2.25));
    u.path={{9,8},{8,8}};u.cell_x=8;u.fx=8.75;CHECK(moving_ground_level(w,u)==doctest::Approx(2.75));
}
TEST_CASE("Slave followers: packed workers use distinct reachable neighbours and deterministic continuation") {
    Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.slave_count=4;d.fw=d.fh=2;
    d.slave_combat=unit("SLAV");w.set_economy({d},{0},OreValues{},1000,0,0);
    auto m=unit("SMIN");m.deploys_into="YAREFN";w.set_undeploy_unit(0,m);
    w.add_building(0,4,4,2,2,0,100,0,0,true);w.step_yuri_workers();REQUIRE(w.order_pack_slave_miner(0,0));
    const int mobile=int(w.units().size())-1;w.order_unit_move(mobile,13,13);
    World loaded(r.map);REQUIRE(save::load_world(loaded,save::save_world(w)));
    for(int t=0;t<450;++t){w.step(1.0/15.0);loaded.step(1.0/15.0);}
    CHECK(save::save_world(w)==save::save_world(loaded));
    for(int i=0;i<mobile;++i){CHECK(w.units()[i].w7.slave_manager==-2-mobile);
        CHECK(std::max(std::abs(w.units()[i].cell_x-w.units()[mobile].cell_x),std::abs(w.units()[i].cell_y-w.units()[mobile].cell_y))<=2);}
}

TEST_CASE("retail Boomer prerequisites: yard and radar required; Battle Lab is not required") {
    if(!ra2fixture::assets_available())return;
    const auto& rules=ra2fixture::rulesmd();const auto required=rules.prerequisite("BSUB");
    CHECK(required==std::vector<std::string>{"YAYARD","RADAR"});CHECK(rules.section_double("BSUB","TechLevel",-1)==2);
    std::vector<BuildingTypeDef> defs(3);defs[0].id="YAYARD";defs[1].id="NAPSIS";defs[2].id="YATECH";
    std::vector<std::pair<std::string,std::vector<std::string>>> groups{{"RADAR",rules.prerequisite_group("RADAR")}};
    std::vector<Building> buildings(3);for(int i=0;i<3;++i){buildings[i].type=i;buildings[i].house=0;buildings[i].alive=true;buildings[i].state=Building::State::Idle;}
    buildings[2].alive=false;CHECK(prerequisites_met(required,buildings,defs,0,groups));
    buildings[0].alive=false;CHECK_FALSE(prerequisites_met(required,buildings,defs,0,groups));
    buildings[0].alive=true;buildings[1].house=1;CHECK_FALSE(prerequisites_met(required,buildings,defs,0,groups));
    buildings[1].house=0;buildings[1].state=Building::State::Dead;CHECK_FALSE(prerequisites_met(required,buildings,defs,0,groups));
}

TEST_CASE("ConYard move-to-pack: saved reverse buildup retains faction destination") {
 for(const auto& name:{"GACNST","NACNST","YACNST"}) {
  Rig r;World w(r.map);BuildingTypeDef d;d.id=name;d.fw=d.fh=4;d.strength=1000;d.conyard=true;
  w.set_building_types({d});auto m=unit("MCV");m.deploys_into=name;w.set_undeploy_unit(0,m);
  w.set_skirmish_session(true);w.set_mcv_redeploys(true);
  w.add_building(0,8,8,4,4,0,1000,0,0,true);w.buildings()[0].buildup_total=1;
  auto c=make_mega(w,{},Mission::Move,Target{},Target::cell(14,14));c.house=0;c.whom.push_back(Target::building(0));apply_command(w,c);
  CHECK(w.buildings()[0].selling);World loaded(r.map);REQUIRE(save::load_world(loaded,save::save_world(w)));
  CHECK(w.packing_conyard(0));CHECK(loaded.packing_conyard(0));
  for(int tick=0;tick<150;++tick){w.step(1.0/15.0);loaded.step(1.0/15.0);}
  REQUIRE(w.units().size()==1);CHECK(w.buildings()[0].state==Building::State::Dead);
  CHECK(w.units()[0].cell_x==14);CHECK(w.units()[0].cell_y==14);
  CHECK(save::save_world(w)==save::save_world(loaded));
 }
}
TEST_CASE("Tank Bunker: arrival reservation cannot consume an unfinished approach") {
 Rig r;World w(r.map);BuildingTypeDef d;d.id="NATBNK";d.tank_bunker=true;d.can_be_occupied=true;
 d.max_occupants=1;d.fw=d.fh=2;d.strength=1000;w.set_building_types({d});w.add_building(0,8,8,2,2,0,1000,0,0,true);
 auto t=unit("LTNK");t.is_vehicle=true;t.w7.bunkerable=true;w.add_combat_unit(7,8,0,t);
 auto& u=w.units()[0];u.fx=6.25;u.path={{6,8},{7,8}};u.path_idx=1;
 w.order_occupy(0,0);w.step_depth(0);CHECK(w.buildings()[0].occupants.empty());CHECK(u.occupy_target==0);
 u.path.clear();u.path_idx=0;u.cell_x=8;u.cell_y=8;u.fx=8;u.fy=8;u.body_facing=8192;w.step_depth(0);REQUIRE(w.buildings()[0].occupants.size()==1);
 CHECK(u.garrison_of==0);
}
TEST_CASE("Tank Bunker presentation: drive speed and lower-before-exit sequence") {
 CHECK(bunker_drive_ticks(1,0,1)>bunker_drive_ticks(1,0,2));
 CHECK(bunker_drive_fraction(0,15,30,0)==0);CHECK(bunker_drive_fraction(29,15,30,1)==0);
 CHECK(bunker_drive_fraction(37,15,30,0.5)==doctest::Approx(0.5));
 CHECK(bunker_drive_fraction(45,15,30,0)==1);
 CHECK(bunker_drive_fraction(7,15,0,0.5)==doctest::Approx(0.5));
}

TEST_CASE("ConYard original input: map move is emitted from a selected redeployable yard") {
 Rig r;World w(r.map);BuildingTypeDef d;d.id="YACNST";d.fw=d.fh=4;d.strength=1000;d.conyard=true;
 w.set_building_types({d});auto m=unit("PCV");m.is_vehicle=true;m.deploys_into="YACNST";
 w.set_undeploy_unit(0,m);w.set_skirmish_session(true);w.set_mcv_redeploys(true);w.add_building(0,8,8,4,4,0,1000,0,0,true);
 Camera cam;Input input(w,cam,1280,720);std::vector<Command> commands;input.set_command_sink([&](Command c){commands.push_back(c);});
 auto click=[&](int x,int y,unsigned time){InputEvent e;e.kind=InputEvent::PointerDown;e.button=InputEvent::Left;e.x=x;e.y=y;e.t_ms=time;input.handle(e);e.kind=InputEvent::PointerUp;input.handle(e);};
 click(30,255,1000);REQUIRE(input.selected_building()==0);
 CHECK(input.action_at(30,435)==Input::Action::Move);click(30,435,2000);
 REQUIRE(commands.size()==1);CHECK(commands[0].mission==Mission::Move);
 REQUIRE(commands[0].whom.size()==1);CHECK(commands[0].whom[0].is_building());CHECK(commands[0].whom[0].id==0);
 w.set_mcv_redeploys(false);CHECK(input.action_at(30,435)==Input::Action::None);
}
TEST_CASE("building force fire input: selected defense emits a building actor and Stop cancels it") {
 Rig r;World w(r.map);BuildingTypeDef d;d.id="GUN";d.armed=true;d.strength=500;
 d.weapon[0].valid=true;d.weapon[0].damage=10;d.weapon[0].range=8;
 w.set_building_types({d});w.add_building(0,8,8,1,1,0,500,0,0,true);
 Camera cam;Input input(w,cam,1280,720);std::vector<Command> commands;
 input.set_command_sink([&](Command c){commands.push_back(c);});
 auto click=[&](int x,int y,unsigned time,bool force){InputEvent e;e.kind=InputEvent::PointerDown;e.button=InputEvent::Left;e.x=x;e.y=y;e.t_ms=time;e.ctrl=force;input.handle(e);e.kind=InputEvent::PointerUp;input.handle(e);};
 click(30,255,1000,false);REQUIRE(input.selected_building()==0);
 click(90,285,2000,true);REQUIRE(commands.size()==1);
 CHECK(commands[0].mission==Mission::Attack);CHECK(commands[0].dest==Target::cell(-1,-1));
 REQUIRE(commands[0].whom.size()==1);CHECK(commands[0].whom[0].is_building());
 apply_command(w,commands[0]);CHECK(w.buildings()[0].attack_order_kind==3);
 input.stop_selected();REQUIRE(commands.size()==2);apply_command(w,commands[1]);
 CHECK(w.buildings()[0].attack_order_kind==0);
}
TEST_CASE("Tank Bunker: normal ordered drive finishes approach before attachment") {
 Rig r;World w(r.map);BuildingTypeDef d;d.id="NATBNK";d.tank_bunker=true;d.can_be_occupied=true;
 d.max_occupants=1;d.fw=d.fh=2;d.strength=1000;w.set_building_types({d});w.add_building(0,8,8,2,2,0,1000,0,0,true);
 auto t=unit("LTNK");t.is_vehicle=true;t.w7.bunkerable=true;w.add_combat_unit(3,8,0,t);w.order_occupy(0,0);
 for(int tick=0;tick<200 && w.buildings()[0].occupants.empty();++tick)w.step(1.0/15.0);
 REQUIRE(w.buildings()[0].occupants.size()==1);CHECK(w.units()[0].garrison_of==0);
 CHECK(w.units()[0].fx==w.units()[0].cell_x);CHECK(w.units()[0].fy==w.units()[0].cell_y);
}

TEST_CASE("Slave passage: only linked workers cross their deployed manager in a blocked corridor") {
 for(bool linked:{false,true}) {
  Rig r;World w(r.map);for(int y=0;y<20;++y)for(int x=0;x<20;++x)if(y!=8)w.block_cell(x,y);
  BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.slave_count=1;d.strength=1000;
  w.set_building_types({d});w.add_building(0,8,8,2,2,0,1000,0,0,true);
  auto c=unit("SLAV");c.is_infantry=true;w.add_combat_unit(6,8,0,c);w.enable_subcells();
  if(linked)w.units()[0].w7.slave_manager=0;
  CHECK(w.unit_can_enter_cell(w.units()[0],8,8)==linked);w.order_unit_move(0,11,8);
  bool crossed=false;for(int tick=0;tick<70;++tick){w.step(1.0/15.0);if(w.units()[0].cell_x==11){crossed=true;break;}}
  CHECK(crossed==linked);CHECK_FALSE(w.walkable(8,8));
  if(linked){w.buildings()[0].house=1;CHECK_FALSE(w.unit_can_enter_cell(w.units()[0],8,8));
   w.buildings()[0].house=0;w.buildings()[0].alive=false;CHECK_FALSE(w.unit_can_enter_cell(w.units()[0],8,8));}
 }
}
TEST_CASE("Slave passage: mobile manager retains its claim while linked worker crosses and saves") {
 for(bool linked:{false,true}) {
  Rig r;World w(r.map);for(int y=0;y<20;++y)for(int x=0;x<20;++x)if(y!=8)w.block_cell(x,y);
  auto m=unit("SMIN");m.is_vehicle=true;w.add_combat_unit(8,8,0,m);w.units()[0].frozen=true;
  auto c=unit("SLAV");c.is_infantry=true;w.add_combat_unit(6,8,0,c);w.enable_subcells();
  if(linked)w.units()[1].w7.slave_manager=-2;
  w.order_unit_move(1,10,8);bool shared=false,reached=false;
  for(int tick=0;tick<100;++tick){w.step(1.0/15.0);
   if(linked && !shared && w.units()[1].cell_x==8){
    shared=true;CHECK(w.occupancy().at(8,8)==0);CHECK(w.occupancy().spot_of(8,8,1)>=0);
    World loaded(r.map);REQUIRE(save::load_world(loaded,save::save_world(w)));
    for(int t=0;t<40;++t){w.step(1.0/15.0);loaded.step(1.0/15.0);}
    CHECK(save::save_world(w)==save::save_world(loaded));
   }
   if(w.units()[1].cell_x==10){reached=true;break;}
  }
  CHECK(reached==linked);CHECK(shared==linked);CHECK(w.occupancy().at(8,8)==0);
  CHECK(w.units()[0].alive);CHECK(w.units()[1].alive);
 }
}
TEST_CASE("Slave passage: ownership death and bridge layer revoke the mobile exception") {
 auto c=unit("SLAV");c.is_infantry=true;auto m=unit("SMIN");m.is_vehicle=true;
 Rig r;World w(r.map);w.add_combat_unit(7,8,0,c);w.add_combat_unit(8,8,0,m);
 auto& worker=w.units()[0];auto& miner=w.units()[1];worker.w7.slave_manager=-3;
 CHECK(slave_mobile_pair(worker,miner,1));miner.house=1;CHECK_FALSE(slave_mobile_pair(worker,miner,1));
 miner.house=0;miner.on_deck=1;CHECK_FALSE(slave_mobile_pair(worker,miner,1));
 miner.on_deck=0;miner.alive=false;CHECK_FALSE(slave_mobile_pair(worker,miner,1));
}

TEST_CASE("Slave passage: ore reachability opens only the supplied manager footprint") {
 OreField ore(12,1);ore.set(10,0,OreKind::Ore,10,0);std::vector<uint8_t> grid(12,1);grid[8]=grid[9]=0;
 Occupancy occ(12,1);int x=0,y=0;std::vector<int> own{8,9};
 CHECK_FALSE(find_reachable_ore(ore,grid,12,1,occ,0,6,0,14,-1,-1,x,y));
 CHECK(find_reachable_ore(ore,grid,12,1,occ,0,6,0,14,-1,-1,x,y,nullptr,8,0,&own));CHECK(x==10);
 grid[7]=0;CHECK_FALSE(find_reachable_ore(ore,grid,12,1,occ,0,6,0,14,-1,-1,x,y,nullptr,8,0,&own));
}
TEST_CASE("Slave passage: a mobile manager can traverse its worker without overwriting the spot") {
 Rig r;World w(r.map);for(int y=0;y<20;++y)for(int x=0;x<20;++x)if(y!=8)w.block_cell(x,y);
 auto m=unit("SMIN");m.is_vehicle=true;m.rot=10;w.add_combat_unit(6,8,0,m);
 auto c=unit("SLAV");c.is_infantry=true;w.add_combat_unit(8,8,0,c);w.units()[1].w7.slave_manager=-2;
 w.enable_subcells();w.order_unit_move(0,10,8);bool shared=false;
 for(int tick=0;tick<100;++tick){w.step(1.0/15.0);if(w.units()[0].cell_x==8 && w.units()[1].cell_x==8){shared=true;CHECK(w.occupancy().spot_of(8,8,1)>=0);}if(w.units()[0].cell_x==10)break;}
 CHECK(shared);CHECK(w.units()[0].cell_x==10);CHECK(w.units()[1].alive);
}

TEST_CASE("Tank Bunker hull: travel then horizontal alignment gates the lift") {
 CHECK(bunker_horizontal_facing(0)==8192);CHECK(bunker_horizontal_facing(40000)==40960);
 const long alignment=bunker_alignment_ticks(0,8192,16384);CHECK(alignment==8);
 CHECK(bunker_hull_facing(0,8192,14,15,alignment,0)==0);
 CHECK(bunker_hull_facing(0,8192,19,15,alignment,0)==doctest::Approx(4096));
 CHECK(bunker_hull_facing(0,8192,23,15,alignment,0)==8192);
}

TEST_CASE("Tank Bunker automation: drive into foundation then align before attachment") {
 for(int direction=0;direction<4;++direction){Rig r;World w(r.map);BuildingTypeDef d;d.id="NATBNK";d.tank_bunker=true;d.can_be_occupied=true;d.max_occupants=1;d.fw=d.fh=2;d.strength=1000;
 w.set_building_types({d});w.add_building(0,8,8,2,2,0,1000,0,0,true);
 auto t=unit("LTNK");t.is_vehicle=true;t.rot=8;t.w7.bunkerable=true;int xs[]={4,13,9,9},ys[]={9,9,4,13};w.add_combat_unit(xs[direction],ys[direction],0,t);
 w.order_occupy(0,0);World copy(r.map);REQUIRE(save::load_world(copy,save::save_world(w)));
 for(int tick=0;tick<400;++tick){w.step(1.0/15.0);copy.step(1.0/15.0);if(w.units()[0].garrison_of==0)break;}
 REQUIRE(w.units()[0].garrison_of==0);CHECK(w.units()[0].cell_x==8);CHECK(w.units()[0].cell_y==8);
 CHECK(w.units()[0].body_facing==bunker_horizontal_facing(w.units()[0].body_facing));CHECK(save::save_world(w)==save::save_world(copy));}
}

TEST_CASE("Tank Bunker automation: lower first then physically path out with deterministic save") {
 Rig r;World w(r.map);BuildingTypeDef d;d.id="NATBNK";d.tank_bunker=true;d.can_be_occupied=true;
 d.max_occupants=1;d.fw=d.fh=2;d.strength=1000;w.set_building_types({d});w.add_building(0,8,8,2,2,0,1000,0,0,true);
 auto t=unit("LTNK");t.is_vehicle=true;t.rot=8;t.w7.bunkerable=true;w.add_combat_unit(4,9,0,t);
 w.order_occupy(0,0);for(int n=0;n<400 && w.units()[0].garrison_of<0;++n)w.step(1.0/15.0);
 REQUIRE(w.units()[0].garrison_of==0);w.unload_occupants(0);
 for(int n=0;n<17;++n){w.step(1.0/15.0);CHECK(w.units()[0].garrison_of==0);}
 World copy(r.map);REQUIRE(save::load_world(copy,save::save_world(w)));
 w.step(1.0/15.0);copy.step(1.0/15.0);
 REQUIRE(w.units()[0].garrison_of==-1);CHECK(w.units()[0].fx==8);CHECK(w.units()[0].fy==8);
 REQUIRE(w.units()[0].moving());CHECK(w.units()[0].bunker_exit_host==0);
 double px=8,py=8;bool moved=false;
 for(int n=0;n<200;++n){w.step(1.0/15.0);copy.step(1.0/15.0);const auto& u=w.units()[0];
   CHECK(std::hypot(u.fx-px,u.fy-py)<0.6);moved=moved || u.fx!=px || u.fy!=py;px=u.fx;py=u.fy;
   CHECK(save::save_world(w)==save::save_world(copy));if(!u.moving())break;}
 CHECK(moved);CHECK(w.units()[0].bunker_exit_host==-1);CHECK(w.units()[0].alive);
}

TEST_CASE("Unit presentation: interpolate display rates without changing simulation or teleport boundaries") {
 Rig r;World w(r.map);auto c=unit("LTNK");c.is_vehicle=true;w.add_combat_unit(4,4,0,c);
 UnitPresentation motion;motion.capture(w.units());w.units()[0].fx=4.5;w.units()[0].body_facing=65000;
 motion.capture(w.units());const auto saved=save::save_world(w);
 for(int hz:{30,60,120})for(int k=0;k<=hz;++k){auto draw=w.units()[0];motion.apply(0,draw,double(k)/hz);
   CHECK(draw.fx==doctest::Approx(4+0.5*k/hz));CHECK(save::save_world(w)==saved);}
 w.units()[0].fx=15;motion.capture(w.units());auto draw=w.units()[0];motion.apply(0,draw,0);CHECK(draw.fx==15);
 w.units()[0].fx=15.2;w.units()[0].cargo_of=kGarrisonCargo;motion.capture(w.units());draw=w.units()[0];
 motion.apply(0,draw,0);CHECK(draw.fx==15.2);motion.clear();draw.fx=20;motion.apply(0,draw,0);CHECK(draw.fx==20);
}

TEST_CASE("Slave Miner: planned legal ore site drives and deploys quietly across save load") {
 Rig r;World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.strength=500;d.slave_count=2;
 d.slave_combat=unit("SLAV");w.set_economy({d},{0},OreValues{},1000,0,0);
 auto miner=unit("SMIN");miner.is_vehicle=true;miner.rot=8;miner.deploys_into="YAREFN";
 w.set_undeploy_unit(0,miner);w.add_combat_unit(2,2,0,miner);w.ore().set(17,17,OreKind::Ore,5,0);
 w.step_yuri_workers();REQUIRE(w.units()[0].deploy_mission==4);
 World copy(r.map);REQUIRE(save::load_world(copy,save::save_world(w)));
 for(int n=0;n<900 && w.buildings().empty();++n){w.step(1.0/15.0);copy.step(1.0/15.0);}
 REQUIRE_FALSE(w.buildings().empty());CHECK(w.buildings()[0].alive);
 CHECK(w.buildings()[0].cx>=15);CHECK(w.buildings()[0].cy>=15);
 CHECK(save::save_world(w)==save::save_world(copy));
 for(const auto& e:w.take_deploy_events())CHECK(e.kind!=World::DeployEvent::Kind::CannotDeploy);
}

TEST_CASE("Slave Miner: planned deployment route reaches its exact site beside an isolated plateau") {
 Rig r;
 for(auto& cell:const_cast<std::vector<ra2assets::MapCell>&>(r.map.cells()))
   if(cell.x>=14 && cell.x<=16 && cell.y>=14 && cell.y<=16)cell.level=3;
 World w(r.map);BuildingTypeDef d;d.id="YAREFN";d.fw=d.fh=2;d.strength=500;d.slave_count=2;
 d.slave_combat=unit("SLAV");w.set_economy({d},{0},OreValues{},1000,0,0);
 auto miner=unit("SMIN");miner.is_vehicle=true;miner.rot=8;miner.deploys_into="YAREFN";
 w.set_undeploy_unit(0,miner);w.add_combat_unit(2,2,0,miner);w.ore().set(17,17,OreKind::Ore,5,0);
 w.step_yuri_workers();const auto& u=w.units()[0];
 REQUIRE(u.deploy_mission==4);REQUIRE(u.moving());
 CHECK(u.path.back().x==u.deploy_goal_x);CHECK(u.path.back().y==u.deploy_goal_y);
 World copy(r.map);REQUIRE(save::load_world(copy,save::save_world(w)));
 for(int n=0;n<900 && w.buildings().empty();++n){w.step(1.0/15);copy.step(1.0/15);}
 REQUIRE_FALSE(w.buildings().empty());CHECK(w.buildings()[0].alive);
 CHECK(save::save_world(w)==save::save_world(copy));
}

TEST_CASE("Traffic yield: blocked walking units accept forced clearance but retain player scatter rules") {
 Rig r;World w(r.map);auto c=unit("BRUTE");c.is_infantry=true;w.add_combat_unit(8,8,0,c);
 w.order_unit_move(0,15,8);REQUIRE(w.units()[0].moving());w.units()[0].wait_time=4;
 w.order_scatter(0);CHECK(w.units()[0].path.back().x==15);
 w.order_scatter(0,true);REQUIRE(w.units()[0].moving());
 CHECK((w.units()[0].path.back().x!=15 || w.units()[0].path.back().y!=8));
 CHECK(std::max(std::abs(w.units()[0].path.back().x-8),std::abs(w.units()[0].path.back().y-8))==1);
}

TEST_CASE("Yuri idle guard: default Area Guard records its home before the combat scan") {
 Rig r;World w(r.map);World::CombatW7Rules rules;rules.armed=true;w.arm_combat_w7(rules);
 auto brute=unit("BRUTE");brute.is_infantry=true;brute.damage=100;brute.range=1.4;
 brute.rof=60;brute.tg.guard_range=2;brute.w7.default_to_guard_area=true;
 w.add_combat_unit(8,8,0,brute);
 w.step(1.0/15);
 CHECK(w.units()[0].tg.area_guard);
 CHECK(w.units()[0].tg.home_x==8);CHECK(w.units()[0].tg.home_y==8);
 // The anchor belongs to the idle transition, not a frame-by-frame chase reset.
 w.units()[0].fx=9;w.units()[0].cell_x=9;w.step(1.0/15);
 CHECK(w.units()[0].tg.home_x==8);
 World copy(r.map);REQUIRE(save::load_world(copy,save::save_world(w)));
 CHECK(copy.units()[0].tg.home_x==8);CHECK(copy.units()[0].tg.home_y==8);
 for(int n=0;n<120;++n){w.step(1.0/15);copy.step(1.0/15);}
 CHECK(save::save_world(w)==save::save_world(copy));
 // Existing format-55 worlds can already carry the bad default guard state.
 const int idle_x=w.units()[0].cell_x,idle_y=w.units()[0].cell_y;
 w.units()[0].tg.home_x=w.units()[0].tg.home_y=-1;
 REQUIRE(save::load_world(copy,save::save_world(w)));
 copy.step(1.0/15);
 CHECK(copy.units()[0].tg.home_x==idle_x);CHECK(copy.units()[0].tg.home_y==idle_y);
}

TEST_CASE("Yuri melee: visible guard targets are acquired and pursued without overriding Move") {
 for(bool visible:{false,true}) {
  Rig r;World w(r.map);World::CombatW7Rules rules;rules.armed=true;w.arm_combat_w7(rules);
  auto brute=unit("BRUTE");brute.is_infantry=true;brute.damage=100;brute.range=1.4;
  brute.rof=60;brute.tg.guard_range=2;brute.w7.default_to_guard_area=true;
  auto victim=unit("TARGET");victim.strength=1000;
  w.add_combat_unit(8,8,0,brute);w.add_combat_unit(11,8,1,victim);
  w.units()[1].seen_by_mask=visible?0xFFFFFFFFu:0;
  w.step(1.0/15);
  if(visible) {CHECK(w.units()[0].target==1);CHECK(w.units()[0].moving());}
  else {CHECK(w.units()[0].target==-1);CHECK_FALSE(w.units()[0].moving());}
 }
 Rig r;World w(r.map);World::CombatW7Rules rules;rules.armed=true;w.arm_combat_w7(rules);
 auto brute=unit("BRUTE");brute.is_infantry=true;brute.damage=100;brute.range=1.4;
 brute.tg.guard_range=2;brute.w7.default_to_guard_area=true;
 w.add_combat_unit(8,8,0,brute);w.add_combat_unit(9,8,1,unit("TARGET"));
 w.order_unit_move(0,8,15);w.step(1.0/15);
 CHECK(w.units()[0].target==-1);REQUIRE(w.units()[0].moving());
 CHECK(w.units()[0].path.back().y==15);
}

TEST_CASE("Yuri idle guard: special-unit passive acquisition remains disabled") {
 for(const char* id:{"CAOS","TELE"}) {
  Rig r;World w(r.map);World::CombatW7Rules rules;rules.armed=true;w.arm_combat_w7(rules);
  auto c=unit(id);c.is_vehicle=true;c.damage=50;c.range=5;c.w7.default_to_guard_area=true;
  c.an.no_passive_acquire=true;
  w.add_combat_unit(8,8,0,c);w.add_combat_unit(9,8,1,unit("TARGET"));
  for(int n=0;n<120;++n)w.step(1.0/15);
  CHECK(w.units()[0].target==-1);CHECK_FALSE(w.units()[0].moving());
  CHECK(w.units()[1].hp==100);
 }
}

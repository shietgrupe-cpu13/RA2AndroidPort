#include "doctest.h"
#include "world.hpp"
#include "combat.hpp"
#include "harvest.hpp"
#include "command.hpp"
#include "savegame.hpp"
#include "ra2assets/rules.hpp"
#include <chrono>
#include "debug_ai.hpp"
#include "gas_emitter.hpp"
#include "disk_laser.hpp"
#include "defense_muzzle.hpp"
#include "weapon_unit.hpp"
#include "skirmish_lobby.hpp"
using ra2assets::Rules;
#include "../engine/ai_side_catalog.inc"
using namespace ra2engine;
namespace {
struct ChaosRig {
    ra2assets::MapFile map;
    ChaosRig() {auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
        for(int y=0;y<30;++y)for(int x=0;x<30;++x)cells.push_back({x,y,0,0,0});}
};
ra2assets::Ini chaos_rules() {
 return ra2assets::Ini::parse("[VehicleTypes]\n0=CAOS\n1=TANK\n2=SAFE\n3=ROBOT\n[CAOS]\nPrimary=Gas\nDeployer=yes\nDeployFire=yes\nBerserkFriendly=yes\n[SAFE]\nBerserkFriendly=yes\n[ROBOT]\nImmuneToPsionicWeapons=yes\n[Gas]\nAreaFire=yes\nWarhead=Psych\n[Psych]\nPsychedelic=yes\n[AudioVisual]\nBerserkColor=1\n[ColorAdd]\nNone=0,0,0\nRed=24,0,0\n");
}
void configure(World& w) {auto ini=chaos_rules();w.configure_chaos(ra2assets::Rules::load(ini),ini);
 World::CombatW7Rules r;r.armed=true;w.arm_combat_w7(r);}
UnitCombat type(const char* id,int damage=0) {
 UnitCombat c;c.image=id;c.tg.rules_id=id;c.strength=1000;c.is_vehicle=true;c.rot=100;
 c.damage=damage;c.rof=30;c.range=5;c.verses=std::vector<int>(11,100);return c;
}
void drone(World& w) {auto c=type("CAOS",600);c.range=3;c.cell_spread=3;c.omni_fire=true;
 c.verses={100,100,100,50,50,50,0,0,0,100,100};w.add_combat_unit(10,10,0,c);}
}
TEST_CASE("Deployment legality: GI cannot fire at air while Guardian GI retains AA") {
 ChaosRig rig;World w(rig.map);auto gi=type("GI",15);gi.is_vehicle=false;gi.is_infantry=true;
 gi.primary_aa=false;gi.deploy_fire=true;gi.deploy_fire_slot=1;
 gi.secondary.valid=true;gi.secondary.damage=25;gi.secondary.range=5;
 gi.secondary.ag=true;gi.secondary.aa=false;gi.secondary.verses.assign(11,100);
 w.add_combat_unit(10,10,0,gi);auto plane=type("AIR");plane.is_aircraft=true;
 w.add_combat_unit(12,10,1,plane);w.units()[1].altitude=100;
 REQUIRE(w.order_deploy(0));CHECK(unit_weapon_for_unit(w.units()[0],w.units()[1])==-1);
 w.units()[0].w2.aa=true;CHECK(unit_weapon_for_unit(w.units()[0],w.units()[1])==1);
 w.units()[0].w2.aa=false;w.units()[1].altitude=0;
 CHECK(unit_weapon_for_unit(w.units()[0],w.units()[1])==1);
 w.units()[0].w2.verses.assign(11,0);
 CHECK(unit_weapon_for_unit(w.units()[0],w.units()[1])==-1);
 CHECK(unit_weapon_for_building(w.units()[0],0)==-1);
}
TEST_CASE("Deployment legality: area deployment cannot leak targeted primary fire with a stale frozen cache") {
 ChaosRig rig;World w(rig.map);w.add_combat_unit(10,10,0,type("DESO",125));
 w.add_combat_unit(12,10,1,type("TARGET"));
 auto& u=w.units()[0];u.deployed=true;u.frozen=false;u.abil.deployer=true;
 u.abil.deploy.valid=true;u.abil.deploy.rad_level=500;
 CHECK(unit_weapon_for_unit(u,w.units()[1])==-1);CHECK(unit_weapon_for_building(u,0)==-1);
 CHECK_FALSE(unit_primary_can_fire_at(u,w.units()[1]));
 w.order_unit_attack(0,1);for(int f=0;f<20;++f)w.step(1.0/15);
 CHECK(w.units()[1].hp==1000);CHECK(w.units()[0].fire_flash==0);
 w.units()[0].deployed=false;
 for(int f=0;f<20;++f)w.step(1.0/15);
 CHECK(w.units()[1].hp<1000);
}
TEST_CASE("CMisl launch: vertical raise uses RaiseRate without changing Dreadnought tilt") {
 RocketBlock b;b.pause_frames=2;b.tilt_frames=5;b.raise_rate=3;
 b.pitch_initial=1;b.pitch_final=1;RocketFlight cm,dm;
 rocket_launch(b,cm,100,200,40,1000,200);dm=cm;
 for(int f=0;f<6;++f){rocket_step(b,20,cm,1000,200,0,true);rocket_step(b,20,dm,1000,200,0);}
 CHECK(cm.x==100);CHECK(cm.y==200);CHECK(cm.z==52);
 CHECK(dm.z==40);CHECK(cm.phase==kRocketTilt);
 CHECK(rocket_step(b,20,cm,1000,200,0,true)==kRocketLeftLauncher);
 CHECK(cm.phase==kRocketClimb);
}
TEST_CASE("DEBUG-AI: explicit lobby mode survives settings and config conversion") {
 LobbyState st; lobby_set_ai(st,1,kLobbyAiDebug);
 CHECK(st.rows[1].ai==kLobbyAiDebug);CHECK(lobby_ai_count(st)==1);
 CHECK(lobby_ai_to_slot_type(kLobbyAiDebug)==7);
 CHECK(lobby_slot_type_to_ai(7)==kLobbyAiDebug);
 CHECK(lobby_ai_difficulty(kLobbyAiDebug)==AiDifficulty::Debug);
 CHECK(difficulty_class_index(AiDifficulty::Debug)==1);
 auto ini=IniDoc::parse("");lobby_store(st,ini,{"Americans","YuriCountry"});
 const auto restored=lobby_load(ini,{}, {},{"Americans","YuriCountry"});CHECK(restored.rows[1].ai==kLobbyAiDebug);
 LobbyConvertCtx ctx;ctx.countries={"Americans","YuriCountry"};
 CHECK(lobby_to_config(st,ctx).slots[1].diff==AiDifficulty::Debug);
}
TEST_CASE("DEBUG-AI: Yuri base selection uses ThirdSide and planning side two") {
 const auto ini=ra2assets::Ini::parse("[Sides]\nGDI=Americans\nNod=Russians\nThirdSide=YuriCountry\n[YAREFN]\nOwner=Americans,Russians,YuriCountry\nAIBasePlanningSide=2\n");
 const auto rules=Rules::load(ini);
 CHECK(aiside::planning_side(World::Side::Yuri)==2);
 CHECK(aiside::house_country(World::Side::Yuri,"",&rules)=="YuriCountry");
 CHECK(aiside::house_country(World::Side::Yuri,"YuriCountry",&rules)=="YuriCountry");
 CHECK(aiside::owned_by(rules,"YAREFN",World::Side::Yuri,"YuriCountry"));
 CHECK_FALSE(aiside::owned_by(rules,"YAREFN",World::Side::Allied,"Americans"));
 CHECK_FALSE(aiside::owned_by(rules,"YAREFN",World::Side::Soviet,"Russians"));
}
TEST_CASE("DEBUG-AI: base economy constructs prerequisites without defenses or combat production") {
 ChaosRig rig;World w(rig.map);World::HouseRec h;h.side=World::Side::Allied;w.set_house(1,h);
 AiHouseConfig c;c.difficulty=AiDifficulty::Debug;c.start_credits=10000;c.build_speed=0.001;
 c.mods.build_delay_min=0;c.catalog.conyard=0;c.catalog.power=1;c.catalog.refinery=2;
 c.catalog.war_factory=3;c.catalog.barracks=4;c.catalog.radar=5;c.catalog.tech=6;
 c.catalog.defenses={7};c.catalog.superweapons={8};c.ai.refinery_limit=1;c.ai.war_limit=1;
 for(const auto* id:{"CY","POWER","REF","WEAP","BARR","RADAR","TECH","GUN","NUKE"}) {
  BuildingTypeDef d;d.id=id;d.strength=1000;d.cost=100;d.tech_level=1;d.adjacent=4;
  c.roster.push_back(d);
 }
 c.roster[0].conyard=true;c.roster[1].power=1000;
 for(int i=2;i<=6;++i)c.roster[i].prerequisite={"POWER"};
 c.roster[3].prerequisite={"REF"};c.team.enabled=true;c.team.veh_factory_types={3};c.team.harvesters_per_refinery=1;
 UnitTypeDef miner;miner.id="HARV";miner.harvester=true;miner.storage=10;miner.is_vehicle=true;
 miner.cost=100;miner.tech_level=1;miner.combat=type("HARV",10);c.team.unit_roster={miner};
 w.set_economy(c.roster,{2},{},0,0,0);
 w.add_building(0,3,3,1,1,1,1000,0,0,true);w.enable_ai_house(1,c);
 for(int i=0;i<1400;++i)w.step(1.0/15);
 int power=0,refinery=0,factory=0;
 for(const auto& b:w.buildings())if(b.alive && b.house==1){CHECK(b.type!=7);CHECK(b.type!=8);power+=b.type==1;refinery+=b.type==2;factory+=b.type==3;}
 CHECK(power>0);CHECK(refinery>0);CHECK(factory>0);
 int miners=0;for(const auto& u:w.units())if(u.alive && u.house==1){CHECK(u.image=="HARV");++miners;}
 CHECK(miners>0);CHECK(w.ai_team_state(1)->teams.empty());
}
TEST_CASE("DEBUG-AI: economy-only policy leaves normal AI unchanged") {
 AiHouseConfig c;c.team.enabled=true;c.catalog.defenses={4};c.catalog.superweapons={5};c.catalog.wall=6;
 for(const auto* id:{"HARV","CMIN","SMIN","AMCV","SMCV","PCV","HTNK","E1","ORCA","SUB"}) {
  UnitTypeDef d;d.id=id;d.harvester=d.id=="HARV" || d.id=="CMIN";c.team.unit_roster.push_back(d);
 }
 const auto normal=c;configure_debug_ai(c);
 CHECK(c.team.unit_roster.size()==normal.team.unit_roster.size());CHECK(c.catalog.defenses==normal.catalog.defenses);
 c.difficulty=AiDifficulty::Debug;configure_debug_ai(c);
 CHECK(c.team.unit_roster.size()==6);CHECK(c.catalog.defenses.empty());CHECK(c.catalog.superweapons.empty());
 CHECK(c.catalog.wall==-1);CHECK_FALSE(c.fire_superweapons);CHECK(c.team.rules.total_team_cap==0);
 CHECK(c.team.aimd.teams.empty());CHECK_FALSE(c.team.triggers_enabled);CHECK(c.hate_delay_frames==-1);
}
TEST_CASE("DEBUG-AI: armed economic units never fire and saved continuation stays deterministic") {
 ChaosRig rig;World w(rig.map);configure(w);
 World::HouseRec h;h.side=World::Side::Soviet;w.set_house(1,h);
 AiHouseConfig c;c.difficulty=AiDifficulty::Debug;w.enable_ai_house(1,c);
 w.add_combat_unit(10,10,1,type("HARV",20));w.add_combat_unit(11,10,0,type("TARGET"));
 auto& u=const_cast<Unit&>(w.units()[0]);u.target=1;u.attack_order=true;
 for(int i=0;i<60;++i)w.step(1.0/15);
 CHECK(w.debug_ai(1));CHECK(w.units()[1].hp==1000);CHECK(w.units()[0].fire_flash==0);
 CHECK(w.ai_team_state(1)->teams.empty());
 World loaded(rig.map);REQUIRE(save::load_world(loaded,save::save_world(w)));CHECK(loaded.debug_ai(1));
 for(int i=0;i<60;++i){w.step(1.0/15);loaded.step(1.0/15);}
 CHECK(save::save_world(w)==save::save_world(loaded));CHECK(loaded.units()[1].hp==1000);
}
TEST_CASE("Chaos vent: render-only centre top follows slope projection") {
 const auto flat=gas_vent_projection(20.871,0);CHECK(flat[0]==0);CHECK(flat[1]==doctest::Approx(20.871*kIsoZ));
 for(int ramp=1;ramp<=16;++ramp){const auto p=gas_vent_projection(20.871,ramp);CHECK(std::isfinite(p[0]));CHECK(p[1]>0);}
 CHECK(gas_vent_projection(-1,0)[1]==0);
}
namespace {
void disk_setup(World& w) {
 const auto ini=ra2assets::Ini::parse("[VehicleTypes]\n0=DISK\n[DISK]\nPrimary=DiskLaser\n[DiskLaser]\nDiskLaser=yes\n");
 w.configure_disc_laser(Rules::load(ini));
 auto c=type("DISK",90);c.range=7;c.primary_aa=true;c.proj.weapon_id="DiskLaser";
 w.add_combat_unit(10,10,0,c);w.add_combat_unit(12,10,1,type("TARGET"));
}
}
TEST_CASE("Disc charge: eight spaced emissions precede one authoritative discharge") {
 ChaosRig r;World w(r.map);disk_setup(w);REQUIRE(w.queue_disc_laser(0,1,-1));
 int charge=0,discharge=0;
 for(int tick=1;tick<=17;++tick) {
  w.step_disc_lasers();
  for(const auto& e:w.disk_state().events){charge+=e.phase<8;discharge+=e.phase==8;}
  CHECK(w.units()[1].hp==(tick<17?1000:910));
 }
 CHECK(charge==8);CHECK(discharge==1);CHECK(w.disk_state().shots.empty());
 w.step_disc_lasers();CHECK(w.units()[1].hp==910);CHECK(w.disk_state().events.empty());
}
TEST_CASE("Disc charge: tracks the original target through movement and source retargeting") {
 ChaosRig r;World w(r.map);disk_setup(w);REQUIRE(w.queue_disc_laser(0,1,-1));
 w.add_combat_unit(16,10,1,type("OTHER"));
 auto& units=const_cast<std::vector<Unit>&>(w.units());units[0].target=2;units[1].fx=13;
 for(int tick=0;tick<17;++tick)w.step_disc_lasers();
 CHECK(w.units()[1].hp==910);CHECK(w.units()[2].hp==1000);
}
TEST_CASE("Disc charge: range expiry, death and crashes cancel damage") {
 for(int mode=0;mode<4;++mode){ChaosRig r;World w(r.map);disk_setup(w);REQUIRE(w.queue_disc_laser(0,1,-1));
  auto& units=const_cast<std::vector<Unit>&>(w.units());
  if(mode==0)units[1].fx=29;
  if(mode==1)units[0].alive=false;
  if(mode==2)units[1].alive=false;
  if(mode==3)units[0].an.crashing=true;
  for(int tick=0;tick<18;++tick)w.step_disc_lasers();
  CHECK(w.units()[1].hp==1000);CHECK(w.disk_state().shots.empty());
 }
}
TEST_CASE("Disc charge: actual combat queues a retained shot instead of immediate damage") {
 ChaosRig r;World w(r.map);disk_setup(w);
 auto& u=const_cast<Unit&>(w.units()[0]);u.target=1;u.attack_order=true;u.omni_fire=true;
 w.step(1.0/15);CHECK(w.units()[1].hp==1000);REQUIRE(w.disk_state().shots.size()==1);
 for(int i=0;i<16;++i){w.step(1.0/15);CHECK(w.units()[1].hp==1000);}
 w.step(1.0/15);CHECK(w.units()[1].hp==910);
}
TEST_CASE("Disc charge: airborne targets and ground force fire discharge through the existing receiver") {
 for(bool air:{false,true}){ChaosRig r;World w(r.map);disk_setup(w);
  if(air){auto& v=const_cast<Unit&>(w.units()[1]);v.is_aircraft=true;v.altitude=256;REQUIRE(w.queue_disc_laser(0,1,-1));}
  else REQUIRE(w.queue_disc_laser(0,-1,-1,12,10));
  for(int i=0;i<17;++i)w.step_disc_lasers();CHECK(w.units()[1].hp==910);
 }
}
TEST_CASE("Disc charge: building discharge retains its original target and weapon") {
 ChaosRig r;World w(r.map);disk_setup(w);BuildingTypeDef b;b.id="TEST";b.strength=1000;b.fw=b.fh=2;
 w.set_economy({b},{},OreValues{},1000,0,0);w.add_building(0,13,10,2,2,1,1000,0,0,true);
 REQUIRE(w.queue_disc_laser(0,-1,0));for(int i=0;i<16;++i){w.step_disc_lasers();CHECK(w.buildings()[0].hp==1000);}
 w.step_disc_lasers();CHECK(w.buildings()[0].hp==910);
}
TEST_CASE("Disc charge: save-load preserves phase, captured damage and deterministic continuation") {
 ChaosRig r;World w(r.map);disk_setup(w);REQUIRE(w.queue_disc_laser(0,1,-1));
 for(int i=0;i<6;++i)w.step_disc_lasers();
 const_cast<Unit&>(w.units()[0]).w_damage=999;World loaded(r.map);
 REQUIRE(save::load_world(loaded,save::save_world(w)));
 for(int i=0;i<14;++i){w.step_disc_lasers();loaded.step_disc_lasers();CHECK(save::save_world(w)==save::save_world(loaded));}
 CHECK(loaded.units()[1].hp==910);
 auto& state=const_cast<DiskState&>(loaded.disk_state());DiskShot bad;bad.source=999;state.shots.push_back(bad);
 CHECK_FALSE(save::load_world(loaded,save::save_world(loaded)));
}
TEST_CASE("Disc charge: old state migration clears retained shots and ordinary guns bypass it") {
 ChaosRig r;World old(r.map),w(r.map);disk_setup(w);REQUIRE(w.queue_disc_laser(0,1,-1));
 REQUIRE(save::load_world(w,save::save_world(old)));CHECK(w.disk_state().shots.empty());
 CHECK(w.disk_state().enabled);CHECK_FALSE(w.queue_disc_laser(999,1,-1));
 old.add_combat_unit(10,10,0,type("TANK",90));CHECK_FALSE(old.queue_disc_laser(0,1,-1));
}
TEST_CASE("Disc charge display: independent ring segments stay on the original radius") {
 for(int stage=0;stage<8;++stage)for(const auto& p:disk_charge_segments(3,-2,stage))
  CHECK(std::hypot(p[0],p[1])==doctest::Approx(240.0/256));
}
TEST_CASE("Gatling defense muzzle: staged weapon burst is shared with the mobile projection") {
 BuildingTypeDef t;t.gattling.weapons.resize(6);
 for(int i=0;i<6;++i){t.gattling.weapons[i].id="W"+std::to_string(i);t.gattling.weapons[i].burst=1+i/2;}
 for(int i=0;i<6;++i)CHECK(defense_muzzle_burst(t,"W"+std::to_string(i),i%2)==1+i/2);
 t.weapon[1].burst=4;CHECK(defense_muzzle_burst(t,"ordinary",1)==4);
 WeaponSlot instant;CHECK(staged_defense_impact(true,true,instant));
 CHECK_FALSE(staged_defense_impact(false,true,instant));
 instant.proj.kind=ProjKind::Ballistic;
 CHECK_FALSE(staged_defense_impact(true,true,instant));
 CHECK(staged_defense_impact(true,false,instant));
}
TEST_CASE("Chaos: deploy is a self-centered status blast without HP damage") {
 ChaosRig rig;World w(rig.map);configure(w);drone(w);
 w.units()[0].tg.t.immune_psionics=true;
 w.add_combat_unit(11,10,1,type("TANK"));w.add_combat_unit(9,10,0,type("TANK"));
 w.add_combat_unit(10,13,1,type("TANK"));w.add_combat_unit(10,14,1,type("TANK"));
 auto immune=type("TANK");immune.tg.immune_psionics=true;w.add_combat_unit(12,10,1,immune);
 w.add_combat_unit(10,9,1,type("ROBOT"));
 REQUIRE(w.order_deploy(0));
 CHECK(w.unit_berserk(1));CHECK(w.unit_berserk(2));CHECK(w.unit_berserk(3));
 CHECK_FALSE(w.unit_berserk(0));CHECK_FALSE(w.unit_berserk(4));
 CHECK_FALSE(w.unit_berserk(5));CHECK_FALSE(w.unit_berserk(6));
 for(const auto& u:w.units())CHECK(u.hp==1000);
 CHECK(w.units()[1].house==1);CHECK(w.units()[2].house==0);
 CHECK(w.chaos_state().active.at(1).frames==600);
 CHECK(w.chaos_state().tint[0]==192);CHECK(w.chaos_state().tint[1]==0);
 CHECK_FALSE(w.order_deploy(0));
}
TEST_CASE("Chaos: repeated exposure refreshes timer and expiry restores ordinary hostility") {
 ChaosRig rig;World w(rig.map);configure(w);drone(w);w.add_combat_unit(11,10,0,type("TANK"));
 w.note_chaos_hit(1,60);w.resolve_chaos_hits();w.step_chaos_timers(1.0/15);
 CHECK(w.chaos_state().active.at(1).frames==59);
 w.note_chaos_hit(1,20);w.resolve_chaos_hits();CHECK(w.chaos_state().active.at(1).frames==20);
 w.note_chaos_hit(1,120);w.resolve_chaos_hits();CHECK(w.chaos_state().active.at(1).frames==120);
 w.add_combat_unit(12,10,0,type("SAFE"));w.add_combat_unit(12,11,0,type("TANK"));
 CHECK_FALSE(w.combat_hostile(w.units()[1],w.units()[0]));
 CHECK_FALSE(w.combat_hostile(w.units()[1],w.units()[2]));
 CHECK(w.combat_hostile(w.units()[1],w.units()[3]));
 for(int i=0;i<120;++i)w.step_chaos_timers(1.0/15);
 CHECK_FALSE(w.unit_berserk(1));CHECK_FALSE(w.units()[1].manual_hold);
 CHECK_FALSE(w.combat_hostile(w.units()[1],w.units()[3]));
}
TEST_CASE("Chaos native contract: admitted gas has distance-zero duration and refresh preserves its target") {
 ChaosRig rig;World w(rig.map);configure(w);drone(w);
 w.units()[0].w_percent_at_max=0;
 w.add_combat_unit(12,10,1,type("TANK"));
 w.add_combat_unit(10,13,1,type("TANK"));
 REQUIRE(w.order_deploy(0));
 REQUIRE(w.unit_berserk(1));REQUIRE(w.unit_berserk(2));
 CHECK(w.chaos_state().active.at(1).frames==600);
 CHECK(w.chaos_state().active.at(2).frames==600);
 w.units()[1].target=2;w.units()[1].attack_order=true;w.units()[1].tg.mission=1;
 w.note_chaos_hit(1,10);w.resolve_chaos_hits();
 CHECK(w.units()[1].target==2);CHECK(w.units()[1].attack_order);
 CHECK(w.units()[1].tg.mission==1);CHECK(w.chaos_state().active.at(1).frames==10);
}
TEST_CASE("Chaos: berserk units fire on nearby allies while human commands are refused") {
 ChaosRig rig;World w(rig.map);configure(w);
 w.add_combat_unit(10,10,0,type("TANK",10));w.add_combat_unit(11,10,0,type("TANK"));
 w.note_chaos_hit(0,100);w.resolve_chaos_hits();
 auto move=make_whom(w,EventType::MegaMission,{0});move.house=0;move.mission=Mission::Move;
 move.dest=Target::cell(20,20);apply_command(w,move);
 CHECK(w.units()[0].tg.mission==15);CHECK_FALSE(w.units()[0].moving());
 for(int f=0;f<50;++f)w.step(1.0/15);
 CHECK(w.units()[1].hp<1000);CHECK(w.units()[0].house==0);
}
TEST_CASE("Chaos: direct fire uses centered gas and save-load continuation is deterministic") {
 ChaosRig rig;World w(rig.map);configure(w);drone(w);
 w.add_combat_unit(12,10,1,type("TANK"));w.add_combat_unit(14,10,1,type("TANK"));
 w.order_unit_attack(0,1);w.step(1.0/15);
 REQUIRE(w.unit_berserk(1));CHECK_FALSE(w.unit_berserk(2));CHECK(w.units()[1].hp==1000);
 auto bytes=save::save_world(w);World loaded(rig.map);REQUIRE(save::load_world(loaded,bytes));
 CHECK(save::save_world(loaded)==bytes);
 for(int f=0;f<90;++f){w.step(1.0/15);loaded.step(1.0/15);}
 CHECK(save::save_world(w)==save::save_world(loaded));
 w.units()[1].die_time=0;w.step_chaos_timers(1.0/15);CHECK_FALSE(w.unit_berserk(1));
}
TEST_CASE("Chaos: old worlds migrate and malformed saved statuses are refused") {
 ChaosRig rig;World old(rig.map);old.add_combat_unit(10,10,0,type("TANK"));
 const auto legacy=save::save_world(old);World loaded(rig.map);configure(loaded);
 loaded.add_combat_unit(10,10,0,type("TANK"));loaded.note_chaos_hit(0,20);loaded.resolve_chaos_hits();
 REQUIRE(save::load_world(loaded,legacy));CHECK_FALSE(loaded.unit_berserk(0));
 CHECK(loaded.chaos_state().enabled);
 loaded.note_chaos_hit(0,20);loaded.resolve_chaos_hits();
 auto& state=const_cast<ChaosState&>(loaded.chaos_state());state.active[9999]={20,false};
 World bad(rig.map);CHECK_FALSE(save::load_world(bad,save::save_world(loaded)));
 CHECK(save::supported_version(55));CHECK(save::supported_version(56));CHECK(save::supported_version(57));
 World previous(rig.map);auto ini=chaos_rules();previous.configure_disc_drain(ra2assets::Rules::load(ini));
 previous.add_combat_unit(10,10,0,type("TANK"));
 // A v56 world has the drain extension but no Chaos tail.
 REQUIRE(save::load_world(loaded,save::save_world(previous)));
 CHECK(loaded.chaos_state().enabled);CHECK(loaded.chaos_state().active.empty());
}
TEST_CASE("Chaos: armor affects duration and invisible/invulnerable recipients are excluded") {
 ChaosRig rig;World w(rig.map);configure(w);drone(w);
 auto armored=type("TANK");armored.armor="heavy";w.add_combat_unit(11,10,1,armored);
 w.add_combat_unit(12,10,1,type("TANK"));w.units()[2].an.iron_left_frames=100;
 w.add_combat_unit(10,11,1,type("TANK"));w.units()[3].cargo_of=0;
 REQUIRE(w.order_deploy(0));
 REQUIRE(w.unit_berserk(1));CHECK(w.chaos_state().active.at(1).frames==300);
 CHECK_FALSE(w.unit_berserk(2));CHECK_FALSE(w.unit_berserk(3));
 CHECK(w.units()[1].hp==1000);
}
TEST_CASE("Chaos: source death does not remove a recipient status and normal commands return after expiry") {
 ChaosRig rig;World w(rig.map);configure(w);drone(w);
 w.add_combat_unit(11,10,0,type("TANK"));REQUIRE(w.order_deploy(0));
 w.units()[0].die_time=0;w.step_chaos_timers(1.0/15);CHECK(w.unit_berserk(1));
 auto stop=make_whom(w,EventType::Idle,{1});stop.house=0;
 w.units()[1].target=0;apply_command(w,stop);CHECK(w.units()[1].target==0);
 for(int f=0;f<599;++f)w.step_chaos_timers(1.0/15);
 CHECK_FALSE(w.unit_berserk(1));
 w.units()[1].target=0;apply_command(w,stop);CHECK(w.units()[1].target==-1);
}
TEST_CASE("Chaos: mobile slave-manager following cannot replace berserk Hunt") {
 ChaosRig rig;World w(rig.map);configure(w);
 w.add_combat_unit(3,3,0,type("SMIN"));w.add_combat_unit(15,15,0,type("SLAV"));
 w.units()[1].w7.slave_manager=-2;
 w.note_chaos_hit(1,100);w.resolve_chaos_hits();
 for(int f=0;f<30;++f)w.step(1.0/15);
 REQUIRE(w.unit_berserk(1));CHECK(w.units()[1].tg.mission==15);
 CHECK_FALSE(w.units()[1].moving());CHECK(w.units()[1].w7.slave_manager==-2);
}
TEST_CASE("Chaos: target-query benchmark preserves hostility and saved state") {
 ChaosRig rig;World w(rig.map);configure(w);
 for(int i=0;i<512;++i)w.add_combat_unit(1+i%25,1+i/25,i%2,type("TANK"));
 for(int i=0;i<256;++i)w.note_chaos_hit(i,600);
 w.resolve_chaos_hits();const auto bytes=save::save_world(w);
 for(int sample=0;sample<5;++sample) {
  int accepted=0,expected=0;
  const auto start=std::chrono::steady_clock::now();
  for(int query=0;query<10000;++query) {
   const int i=250+query%200;
   accepted+=w.combat_hostile(w.units()[i],w.units()[0]);
   expected+=i<256 || i%2==1;
  }
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  MESSAGE("CHAOS_TARGET_QUERY sample="<<sample<<" queries=10000 active=256 milliseconds="<<ms);
  CHECK(accepted==expected);
 }
 CHECK(save::save_world(w)==bytes);
}

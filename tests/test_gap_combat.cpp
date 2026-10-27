// Gap fix C (combat / units / movement) -- unit tests for the pure pieces and
// the real-rulesmd resolution. The real-play proof (worlds built through
// launch_skirmish + the GameScene wiring) is `ra2engine --check-gap-combat`.
#include "doctest.h"

#include <cmath>

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "weapon.hpp"
#include "weapon_resolve.hpp"
#include "weapon_unit.hpp"
#include "world.hpp"
#include "savegame.hpp"
#include "command.hpp"

using namespace ra2engine;

TEST_CASE("building force fire: explicit command respects owner range power and Stop, survives deterministic save") {
    auto map=ra2assets::MapFile::parse("[Map]\nSize=0,0,20,20\nTheater=TEMPERATE\n");
    auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
    for(int y=0;y<20;++y)for(int x=0;x<20;++x)cells.push_back({x,y,0,0,0});
    World w(map);BuildingTypeDef d;d.id="GUN";d.armed=true;d.strength=1000;
    auto& gun=d.weapon[0];gun.valid=true;gun.id="testgun";gun.damage=10;gun.rof=8;gun.range=5;gun.verses.assign(11,100);
    w.set_building_types({d});w.add_building(0,4,4,1,1,0,1000,0,0,true);
    UnitCombat target;target.strength=1000;w.add_combat_unit(6,4,0,target);
    auto cmd=make_building_event(EventType::MegaMission,0);cmd.house=1;cmd.mission=Mission::Attack;cmd.dest=Target::cell(-1,-1);
    cmd.target=Target::unit(w,0);apply_command(w,cmd);CHECK(w.buildings()[0].attack_order_kind==0);
    cmd.house=0;cmd.dest=Target{};apply_command(w,cmd);CHECK(w.buildings()[0].attack_order_kind==0);
    cmd.dest=Target::cell(-1,-1);apply_command(w,cmd);CHECK(w.buildings()[0].attack_order_kind==1);
    w.step(1.0/15);CHECK(w.units()[0].hp<1000);
    World restored(map);REQUIRE(save::load_world(restored,save::save_world(w)));
    for(int i=0;i<20;++i){w.step(1.0/15);restored.step(1.0/15);}
    CHECK(save::save_world(w)==save::save_world(restored));
    auto stop=make_building_event(EventType::Idle,0);stop.house=0;apply_command(w,stop);
    CHECK(w.buildings()[0].attack_order_kind==0);const int hp=w.units()[0].hp;
    for(int i=0;i<20;++i)w.step(1.0/15);CHECK(w.units()[0].hp==hp);
    cmd.target=Target::cell(18,18);apply_command(w,cmd);w.take_fire_events();
    for(int i=0;i<20;++i)w.step(1.0/15);CHECK(w.take_fire_events().empty());
    cmd.target=Target::cell(6,4);apply_command(w,cmd);w.buildings()[0].power_off=true;
    for(int i=0;i<20;++i)w.step(1.0/15);CHECK(w.take_fire_events().empty());
    w.buildings()[0].power_off=false;w.step(1.0/15);CHECK_FALSE(w.take_fire_events().empty());
}

TEST_CASE("building force fire: ground projectile uses turret muzzle, AA-only and unarmed cannot shoot ground") {
    auto map=ra2assets::MapFile::parse("[Map]\nSize=0,0,20,20\nTheater=TEMPERATE\n");
    auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
    for(int y=0;y<20;++y)for(int x=0;x<20;++x)cells.push_back({x,y,0,0,0});
    World w(map);BuildingTypeDef d;d.id="GUN";d.armed=true;d.strength=1000;d.turret=true;d.rot=100;
    auto& gun=d.weapon[0];gun.valid=true;gun.damage=10;gun.range=8;gun.rof=30;
    gun.proj.kind=ProjKind::Ballistic;gun.proj.weapon_id="testgun";gun.proj.speed_cap=100;
    gun.proj.flh[0]=128;gun.proj.flh[1]=32;gun.proj.flh[2]=80;gun.proj.weapon_range=8*256;
    BuildingTypeDef aa=d;aa.id="AA";aa.weapon[0].ag=false;aa.weapon[0].aa=true;
    BuildingTypeDef unarmed;unarmed.id="HOUSE";unarmed.strength=1000;
    w.set_building_types({d,aa,unarmed});w.add_building(0,4,4,1,1,0,1000,0,0,true);
    w.add_building(1,4,8,1,1,0,1000,0,0,true);w.add_building(2,4,10,1,1,0,1000,0,0,true);
    w.enable_projectiles(ProjectileRules{});
    CHECK_FALSE(w.order_building_attack(0,2,-1,-1,7,10,true));
    REQUIRE(w.order_building_attack(0,1,-1,-1,7,8,true));
    REQUIRE(w.order_building_attack(0,0,-1,-1,7,4,true));
    for(int i=0;i<20;++i)w.step(1.0/15);
    const auto events=w.take_projectile_events();REQUIRE_FALSE(events.empty());
    bool launch=false;for(const auto& e:events)if(e.kind==ProjectileEvent::Launch) {
        CHECK(e.x==4*256+128);CHECK(std::abs(e.y-4*256)==32);CHECK(e.z==80);launch=true;
    }
    CHECK(launch);
    for(const auto& e:w.take_fire_events())CHECK(e.building==0);
}

TEST_CASE("Gattling: stock tank and turret resolve all stage pairs and elite thresholds") {
    if (!ra2fixture::assets_available()) return;
    const auto& rules = ra2fixture::rulesmd();
    UnitCombat c;
    fill_unit_combat_extras(rules,"YTNK",resolve_type_weapons(rules,"YTNK"),c);
    REQUIRE(c.gattling.enabled());
    REQUIRE(c.gattling.weapons.size()==6);
    CHECK(c.gattling.stage(false)==0);
    c.gattling.charge=c.gattling.thresholds[0];
    CHECK(c.gattling.stage(false)==1);
    c.gattling.charge=c.gattling.thresholds[1];
    CHECK(c.gattling.stage(false)==2);
    CHECK(c.gattling.weapon(0,false).id==type_weapon_id(rules,"YTNK",4,false));
    CHECK(c.gattling.weapon(1,false).aa);
    const int charge=c.gattling.charge;
    c.gattling.step(false,false);
    CHECK(c.gattling.charge==std::max(0,charge-c.gattling.rate_down));
    BuildingTypeDef d;
    fill_building_weapons(rules,"YAGGUN",d);
    REQUIRE(d.gattling.enabled());
    CHECK(d.gattling.weapons.size()==6);
    CHECK(d.gattling.weapon(1,false,d.gattling.thresholds.back()).aa);
    CHECK(d.gattling.weapon(0,false,d.gattling.thresholds.back()).id==type_weapon_id(rules,"YAGGUN",4,false));
}

TEST_CASE("Gattling: charge clamps, spins down, and stage activation updates the firing attributes") {
    Gattling g;
    g.thresholds={3,6,9}; g.elite_thresholds={1,2,3};
    g.rate_up=1;g.rate_down=2;
    for(int i=0;i<6;++i) {
        WeaponSlot w;w.valid=true;w.damage=10+i;w.rof=20-i;w.range=8;
        w.aa=(i%2)==1;w.ag=!w.aa;
        g.weapons.push_back(w);g.elite_weapons.push_back(w);
    }
    for(int i=0;i<30;++i)g.step(true,false);
    CHECK(g.charge==9); CHECK(g.stage(false)==2);
    Unit u;u.gattling=g;
    activate_weapon_slot(u,0,nullptr);
    CHECK(u.w_damage==14);CHECK(u.w_rof==16);CHECK_FALSE(u.w_aa);
    activate_weapon_slot(u,1,nullptr);
    CHECK(u.w_damage==15);CHECK(u.w_aa);
    for(int i=0;i<10;++i)u.gattling.step(false,false);
    CHECK(u.gattling.charge==0);
    activate_weapon_slot(u,0,nullptr);
    CHECK(u.w_damage==10); CHECK(u.w_rof==20);
    u.gattling.charge=2;
    CHECK(u.gattling.stage(false)==0);CHECK(u.gattling.stage(true)==2);
    u.gattling.rate_down=0;u.gattling.step(false,false);
    CHECK(u.gattling.charge==0);
}

TEST_CASE("Gattling: ground force-fire spins up through every stage and stops on cancel") {
    ra2assets::MapFile map;
    auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
    for(int y=0;y<12;++y)for(int x=0;x<12;++x)cells.push_back({x,y,0,0,0});
    World w(map);UnitCombat c;
    c.strength=400;c.damage=10;c.range=10;c.rof=20;
    c.gattling.thresholds={5,10,15};
    for(int i=0;i<6;++i) {
        WeaponSlot s;s.valid=true;s.damage=10+i;s.rof=20-i;s.range=10;s.ag=(i%2==0);s.aa=!s.ag;
        c.gattling.weapons.push_back(s);
    }
    w.add_combat_unit(2,2,0,c);w.order_attack_cell(0,6,2);
    for(int i=0;i<30;++i)w.step(1.0/15.0);
    CHECK(w.units()[0].gattling.charge==15);
    CHECK(w.units()[0].gattling.stage(false)==2);
    CHECK(w.units()[0].w_damage==14);
    w.order_stop(0);
    for(int i=0;i<3;++i)w.step(1.0/15.0);
    CHECK(w.units()[0].gattling.charge==0);
}

TEST_CASE("force-fire infantry face the selected ground cell, including targets behind them") {
    ra2assets::MapFile map;
    auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
    for(int y=0;y<12;++y)for(int x=0;x<12;++x)cells.push_back({x,y,0,0,0});
    World w(map);
    UnitCombat infantry;infantry.strength=400;infantry.damage=10;infantry.range=8;infantry.rof=20;
    infantry.is_infantry=true;
    Unit& unit=w.add_combat_unit(6,6,0,infantry);
    unit.facing=4;

    w.order_attack_cell(0,4,6);
    w.step(1.0/15.0);
    CHECK(unit.facing==1);
    CHECK(unit.fire_flash>0.0);
    CHECK(unit.attack_cell_x==4);
    CHECK(unit.attack_cell_y==6);
}

TEST_CASE("Gattling: defensive turret reaches later stages through long reload intervals") {
    ra2assets::MapFile map; World w(map);
    BuildingTypeDef d; d.id="TESTGAT"; d.strength=100; d.armed=true;
    d.gattling.thresholds={5,10,15}; d.gattling.elite_thresholds={3,6,9};
    for(int i=0;i<6;++i) {
        WeaponSlot s;s.valid=true;s.damage=10+i;s.rof=30;s.range=10;s.ag=(i%2==0);s.aa=!s.ag;
        s.id="stage"+std::to_string(i);d.gattling.weapons.push_back(s);
    }
    d.weapon[0]=d.gattling.weapons[0];d.weapon[1]=d.gattling.weapons[1];
    w.set_building_types({d});w.add_building(0,2,2,1,1,0,100,0,0,true);
    UnitCombat target;target.strength=10000;w.add_combat_unit(4,2,1,target);
    for(int i=0;i<100;++i)w.step(1.0/15.0);
    CHECK(w.buildings()[0].gattling_charge==15);
    bool last_stage=false;
    for(const auto& e:w.take_fire_events()) if(e.weapon=="stage4")last_stage=true;
    CHECK(last_stage);
    CHECK(w.units()[0].hp<10000);
    World restored(map);
    REQUIRE(save::load_world(restored,save::save_world(w)));
    CHECK(restored.buildings()[0].gattling_charge==15);
    CHECK(restored.btypes()[0].gattling.weapons[4].id=="stage4");
    for(int i=0;i<40;++i) {w.step(1.0/15.0);restored.step(1.0/15.0);}
    CHECK(save::save_world(w)==save::save_world(restored));
}

TEST_CASE("parachute combat: exposed air target until touchdown, passengers remain hidden") {
    Unit p;
    p.cargo_of = kParachuteCargo;
    p.an.para_alt = 300.0;
    p.level = 2;
    CHECK(unit_airborne(p));
    CHECK(unit_combat_exposed(p));
    CHECK(unit_lept_z(p) == 508);
    p.on_deck = 1;
    CHECK(unit_lept_z(p) == 924);
    p.cargo_of = 0;
    CHECK_FALSE(unit_combat_exposed(p));
    CHECK_FALSE(unit_airborne(p));
    p.cargo_of = -1;
    p.an.para_alt = 0.0;
    CHECK(unit_combat_exposed(p));
    CHECK_FALSE(unit_airborne(p));
}

TEST_CASE("weapon muzzle: turretless vehicles and aircraft use hull direction") {
    Unit u;
    u.body_facing = 1000;
    u.turret_facing = 30000;
    CHECK(unit_weapon_facing(u) == 30000);
    u.w7.t.has_turret = false;
    CHECK(unit_weapon_facing(u) == 1000);
    u.w7.t.has_turret = true;
    u.is_aircraft = true;
    CHECK(unit_weapon_facing(u) == 1000);
}

namespace {
WeaponSlot slot(bool aa, bool ag, std::vector<int> verses = {}) {
    WeaponSlot s;
    s.valid = true;
    s.aa = aa;
    s.ag = ag;
    s.verses = std::move(verses);
    return s;
}
}  // namespace

TEST_CASE("gapC weapon: choose_weapon = primary if it can engage, else secondary (G-41/G-44)") {
    const WeaponSlot ground = slot(false, true);
    const WeaponSlot aa = slot(true, false);
    CHECK(choose_weapon(ground, aa, /*air=*/false, 0) == 0);
    CHECK(choose_weapon(ground, aa, /*air=*/true, 0) == 1);
    CHECK(choose_weapon(ground, WeaponSlot{}, /*air=*/true, 0) == -1);
    // 0 % Verses forbids the shot (GetFireError, combat.md §2).
    const WeaponSlot no_vs_heavy = slot(false, true, {100, 100, 100, 100, 100, 0});
    CHECK(choose_weapon(no_vs_heavy, WeaponSlot{}, false, 5) == -1);
    CHECK(choose_weapon(no_vs_heavy, ground, false, 5) == 1);
}

TEST_CASE("gapC weapon: real rulesmd slots (SREF Weapon1, APOC/HTK secondary AA, FV no secondary)") {
    if (!ra2fixture::assets_available()) { MESSAGE("no install"); return; }
    const ra2assets::Rules& r = ra2fixture::rulesmd();
    const TypeWeapons sref = resolve_type_weapons(r, "SREF");
    CHECK(sref.slot[0].id == "Comet");
    CHECK(sref.slot[0].damage > 0);
    CHECK(sref.slot[0].range > 0.0);
    CHECK(sref.elite[0].id == "SuperComet");
    CHECK_FALSE(sref.slot[1].valid);
    const TypeWeapons htk = resolve_type_weapons(r, "HTK");
    CHECK(htk.slot[0].id == "FlakTrackGun");
    CHECK(htk.slot[1].id == "FlakTrackAAGun");
    CHECK(htk.slot[1].aa);
    CHECK_FALSE(htk.slot[0].aa);
    const TypeWeapons apoc = resolve_type_weapons(r, "APOC");
    CHECK(apoc.slot[1].id == "MammothTusk");
    CHECK(apoc.slot[1].aa);
    const TypeWeapons fv = resolve_type_weapons(r, "FV");
    CHECK(fv.slot[0].id == "HoverMissile");
    CHECK_FALSE(fv.slot[1].valid);   // Weapon2=RepairBullet is an IFV mode, not a secondary
    const TypeWeapons gi = resolve_type_weapons(r, "E1");
    CHECK(gi.slot[0].id == "M60");
    CHECK(gi.slot[1].id == "Para");
    CHECK(gi.elite[1].id == "ParaE");
    const TypeWeapons ggi = resolve_type_weapons(r, "GGI");
    CHECK(ggi.slot[1].id == "MissileLauncher");   // ";GEF New Guardian GI weapon" cut (RC-5)
    CHECK(ggi.slot[1].aa);
    UnitCombat c;
    fill_unit_combat_extras(r, "E1", gi, c);
    CHECK(c.deploy_fire);
    CHECK(c.deploy_fire_slot == 1);
    CHECK(c.crushable);
    UnitCombat d;
    fill_unit_combat_extras(r, "DESO", resolve_type_weapons(r, "DESO"), d);
    CHECK_FALSE(d.deploy_fire);   // RadEruptionWeapon has RadLevel -> P4 eruption
    UnitCombat m;
    fill_unit_combat_extras(r, "MTNK", resolve_type_weapons(r, "MTNK"), m);
    CHECK(m.crusher);
}

TEST_CASE("gapC weapon: a Flak Track engages an airborne Kirov with its AA secondary") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat flak;
    flak.strength = 300;
    flak.damage = 25;
    flak.rof = 25;
    flak.range = 5.0;
    flak.secondary = slot(true, false);
    flak.secondary.damage = 30;
    flak.secondary.rof = 20;
    flak.secondary.range = 8.0;
    Unit& f = w.add_combat_unit(2, 2, 0, flak);
    f.vet.base.range = 5.0;
    UnitCombat zep;
    zep.strength = 2000;
    zep.is_aircraft = true;
    Unit& k = w.add_combat_unit(8, 2, 1, zep);
    k.altitude = 500.0;
    k.flight_state = FlightState::Cruising;
    Unit& fu = w.units()[0];
    const Unit& ku = w.units()[1];
    CHECK(unit_weapon_for_unit(fu, ku) == 1);
    activate_weapon_slot(fu, 1, nullptr);
    CHECK(fu.w_range == 8.0);
    CHECK(fu.w_damage == 30);
    activate_weapon_slot(fu, 0, nullptr);
    CHECK(fu.w_range == 5.0);
    CHECK(fu.w_damage == 25);
}

TEST_CASE("gapC deploy: a GI deploys into a firing post with Para, not a frozen eruption (G-45)") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat gi;
    gi.strength = 125;
    gi.damage = 15;
    gi.rof = 20;
    gi.range = 4.0;
    gi.secondary = slot(false, true);
    gi.secondary.damage = 25;
    gi.secondary.rof = 15;
    gi.secondary.range = 5.0;
    gi.deploy_fire = true;
    gi.deploy_fire_slot = 1;
    w.add_combat_unit(2, 2, 0, gi);
    UnitCombat e2;
    e2.strength = 125;
    w.add_combat_unit(5, 2, 1, e2);
    CHECK(unit_weapon_for_unit(w.units()[0], w.units()[1]) == 0);
    REQUIRE(w.order_deploy(0));
    CHECK(w.units()[0].deployed);
    CHECK_FALSE(w.units()[0].frozen);
    CHECK(unit_weapon_for_unit(w.units()[0], w.units()[1]) == 1);
    // It keeps fighting from the post: step until the Para shot lands.
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK(w.units()[1].hp < 125);
    CHECK(w.units()[0].w_active == 1);
    CHECK(w.units()[0].cell_x == 2);
    // W7-01-05 [RE: 0x51F510 / 0x51F370]: a human's deployed GI refuses a
    // move order (it stays dug in).
    w.order_unit_move(0, 2, 4);
    CHECK(w.units()[0].deployed);
    CHECK_FALSE(w.units()[0].moving());
}

TEST_CASE("parachute combat: AA defense damages descending infantry while ground gun ignores it") {
    ra2assets::MapFile map;
    for (bool aa : {false, true}) {
        World w(map);
        BuildingTypeDef gun;
        gun.id = "PARATEST";
        gun.strength = 400;
        gun.armed = true;
        gun.weapon[0] = slot(aa, !aa);
        gun.weapon[0].damage = 20;
        gun.weapon[0].rof = 10;
        gun.weapon[0].range = 10.0;
        w.set_building_types({gun});
        w.add_building(0, 2, 2, 1, 1, 0, 400, 0, 0.0, true);
        UnitCombat infantry;
        infantry.strength = 125;
        auto& target = w.add_combat_unit(4, 2, 1, infantry);
        target.cargo_of = kParachuteCargo;
        target.an.para_alt = 300.0;
        for (int f = 0; f < 3; ++f) w.step(1.0 / 15.0);
        if (aa) CHECK(w.units()[0].hp < 125);
        else CHECK(w.units()[0].hp == 125);
    }
}

TEST_CASE("parachute combat: mobile AA acquires parachutist but not transport passenger") {
    ra2assets::MapFile map;
    for (bool parachute : {false, true}) {
        World w(map);
        UnitCombat gun;
        gun.strength = 300;
        gun.damage = 25;
        gun.range = 8.0;
        gun.rof = 10;
        gun.primary_aa = true;
        gun.primary_ag = false;
        w.add_combat_unit(2, 2, 0, gun);
        UnitCombat infantry;
        infantry.strength = 125;
        auto& target = w.add_combat_unit(4, 2, 1, infantry);
        target.cargo_of = parachute ? kParachuteCargo : 0;
        target.an.para_alt = 300.0;
        for (int f = 0; f < 3; ++f) w.step(1.0 / 15.0);
        if (parachute) CHECK(w.units()[1].hp < 125);
        else CHECK(w.units()[1].hp == 125);
    }
}

TEST_CASE("gapC defenses: a pillbox acquires, turns and kills; Powered goes offline at low power (G-40)") {
    ra2assets::MapFile map;
    World w(map);
    BuildingTypeDef pill;
    pill.id = "GAPILL";
    pill.strength = 400;
    pill.weapon[0] = slot(false, true);
    pill.weapon[0].damage = 20;
    pill.weapon[0].rof = 10;
    pill.weapon[0].range = 5.0;
    pill.armed = true;
    pill.turret = true;
    pill.rot = 10;
    BuildingTypeDef sam = pill;
    sam.id = "NASAM";
    sam.powered = true;
    sam.power = -50;
    sam.turret = false;
    w.set_building_types({pill, sam});
    w.add_building(0, 2, 2, 1, 1, 0, 400, 0, 0.0, /*instant=*/true);
    UnitCombat e2;
    e2.strength = 125;
    w.add_combat_unit(5, 2, 1, e2);
    for (int f = 0; f < 120; ++f) w.step(1.0 / 15.0);
    CHECK(w.units()[0].hp <= 0);
    bool fired = false;
    for (const auto& ev : w.take_fire_events()) fired = fired || ev.building == 0;
    CHECK(fired);

    // Powered defense with no power plant: drain 50 > output 0 -> offline.
    World w2(map);
    w2.set_building_types({pill, sam});
    w2.add_building(1, 2, 2, 1, 1, 0, 400, 0, 0.0, true);
    w2.add_combat_unit(4, 2, 1, e2);
    for (int f = 0; f < 60; ++f) w2.step(1.0 / 15.0);
    CHECK(w2.units()[0].hp == 125);
    CHECK(w2.buildings()[0].def_offline);
    CHECK_FALSE(w2.defense_power_ok(0));
}

TEST_CASE("gapC passability: water / cliff / wall / beach cells close the Land grid (G-43; fv7c Beach)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    const int gw = w.grid_w(), gh = w.grid_h();
    REQUIRE(gw > 0);
    // Pick four materialized land cells.
    std::vector<int> picks;
    for (int i = 0; i < gw * gh && picks.size() < 4; ++i)
        if (w.passable()[i]) picks.push_back(i);
    REQUIRE(picks.size() == 4);
    std::vector<ra2assets::LandType> lt(static_cast<std::size_t>(gw) * gh, ra2assets::LandType::Clear);
    lt[picks[0]] = ra2assets::LandType::Water;
    lt[picks[1]] = ra2assets::LandType::Rock;
    lt[picks[2]] = ra2assets::LandType::Wall;
    lt[picks[3]] = ra2assets::LandType::Beach;
    w.set_land_terrain(lt);
    CHECK_FALSE(w.walkable(picks[0] % gw, picks[0] / gw));
    CHECK_FALSE(w.walkable(picks[1] % gw, picks[1] / gw));
    CHECK_FALSE(w.walkable(picks[2] % gw, picks[2] / gw));
    // fv7c (13a #1): the LandType is per sub-tile now, so Beach is blocked for
    // Foot/Track/Wheel as rulesmd [Beach] says -- and open to the amphibious grid.
    CHECK_FALSE(w.walkable(picks[3] % gw, picks[3] / gw));
    CHECK(w.amphib_grid()[picks[3]] == 1);
    CHECK(w.grid_for(MovementDomain::Water)[picks[3]] == 0);
    CHECK(w.grid_for(MovementDomain::Water)[picks[0]] == 1);   // ships still sail it
}

TEST_CASE("gapC G-50: min-1 damage after Verses, 0 % stays 0; ROF rearm jitter is Random(0,2) frames") {
    const std::vector<int> v = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    CHECK(damage_vs(3, v, 4) == 1);     // 3 x 25 % = 0.75 -> 1 (ReceiveDamage clamp)
    CHECK(damage_vs(15, v, 4) == 3);
    CHECK(damage_vs(0, v, 4) == 0);
    const std::vector<int> z = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(damage_vs(50, z, 3) == 0);
    ProjRng rng;
    int seen[3] = {0, 0, 0};
    for (int i = 0; i < 300; ++i) {
        const int extra = rearm_frames_for(20, &rng) - 20;   // Net L0: whole frames
        REQUIRE(extra >= 0);
        REQUIRE(extra <= 2);
        ++seen[extra];
    }
    CHECK(seen[0] > 0);
    CHECK(seen[1] > 0);
    CHECK(seen[2] > 0);
    CHECK(rearm_frames_for(20, nullptr) == 20);
}

TEST_CASE("gapC crush + stop/scatter on the real map (G-47)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    const ra2assets::MapCell* c = pick_start_cell(ra2fixture::mapsmd03_map());
    REQUIRE(c != nullptr);
    // A free straight 1x6 corridor along +x from the start cell.
    int x0 = c->x, y0 = c->y;
    UnitCombat tank;
    tank.strength = 300;
    tank.is_vehicle = true;
    tank.speed_cps = 2.0;
    tank.rot = 127;
    tank.crusher = true;
    UnitCombat inf;
    inf.strength = 125;
    inf.crushable = true;
    REQUIRE(w.walkable(x0, y0));
    REQUIRE(w.walkable(x0 + 1, y0));
    REQUIRE(w.walkable(x0 + 2, y0));
    w.add_combat_unit(x0, y0, 0, tank);
    w.add_combat_unit(x0 + 1, y0, 1, inf);
    w.order_unit_move(0, x0 + 2, y0);
    for (int f = 0; f < 60; ++f) w.step(1.0 / 15.0);
    CHECK(w.units()[1].die_time >= 0.0);   // squashed
    CHECK(w.units()[0].cell_x == x0 + 2);

    // Stop: the tank halts at the cell it is entering.
    w.order_unit_move(0, x0 + 2, y0 + 6);
    w.step(1.0 / 15.0);
    w.order_stop(0);
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK_FALSE(w.units()[0].moving());
    CHECK(std::abs(w.units()[0].cell_y - y0) <= 1);
    // Scatter: steps to an adjacent free cell.
    const int sx = w.units()[0].cell_x, sy = w.units()[0].cell_y;
    w.order_scatter(0);
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK((w.units()[0].cell_x != sx || w.units()[0].cell_y != sy));
}

TEST_CASE("last-fix: an owner change drops target and move and re-plants the guard post (SetOwningHouse FUN_00700940)") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat ifv;
    ifv.strength = 200;
    ifv.is_vehicle = true;
    w.add_combat_unit(5, 5, 1, ifv);
    w.add_combat_unit(9, 5, 0, ifv);
    Unit& u = w.units()[0];
    // Walking its old owner's path (standing on a cell), chasing a target,
    // with a guard post far away.
    u.target = 1;
    u.attack_order = false;
    u.guard_x = 30;
    u.guard_y = 30;
    u.path = {{5, 5}, {6, 5}, {7, 5}};
    u.path_idx = 1;
    w.set_unit_owner(0, 6);
    const Unit& v = w.units()[0];
    CHECK(v.house == 6);
    CHECK(v.target == -1);
    CHECK(v.target_building == -1);
    CHECK_FALSE(v.moving());
    CHECK(v.guard_x == 5);
    CHECK(v.guard_y == 5);
    // Same owner: a no-op (the guard post set by hand stays).
    w.units()[0].guard_x = 12;
    w.set_unit_owner(0, 6);
    CHECK(w.units()[0].guard_x == 12);
}

TEST_CASE("gapC G-46: an idle unit engages an enemy base defense in range, not a plain building") {
    ra2assets::MapFile map;
    World w(map);
    BuildingTypeDef pill;
    pill.id = "GAPILL";
    pill.strength = 400;
    pill.weapon[0] = slot(false, true);
    pill.weapon[0].damage = 0;     // harmless here: only the unit's reaction is tested
    pill.weapon[0].rof = 10;
    pill.weapon[0].range = 1.0;
    pill.armed = true;
    BuildingTypeDef pow;
    pow.id = "GAPOWR";
    pow.strength = 750;
    w.set_building_types({pill, pow});
    w.add_building(1, 2, 6, 1, 1, 1, 750, 0, 0.0, true);   // plain, in range
    w.add_building(0, 2, 2, 1, 1, 1, 400, 0, 0.0, true);   // defense, in range
    UnitCombat tank;
    tank.strength = 400;
    tank.damage = 65;
    tank.rof = 60;
    tank.range = 5.0;
    w.add_combat_unit(5, 4, 0, tank);
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK(w.buildings()[1].hp < 400);
    CHECK(w.buildings()[0].hp == 750);
}

// Net L0 (docs/gap/net/03-sim-bitexact.md D3): the rearm timer is a whole-frame
// countdown, so a k-frame ROF fires exactly every k ticks -- the old seconds
// timer ((k)/15 minus 1/15 per tick) slipped to k+1 for most k.
TEST_CASE("netL0: a k-frame rearm expires after exactly k decrements, for every k") {
    for (int k = 1; k <= 300; ++k) {
        int t = ra2engine::rearm_frames_for(k, nullptr);
        int ticks = 0;
        while (t > 0) { --t; ++ticks; }
        CHECK(ticks == k);
    }
}

TEST_CASE("w7r3: a Prism Tower / Tesla Coil never engages an airborne Kirov (no AA projectile, FUN_007718B0)") {
    ra2assets::MapFile map;
    for (int kind = 0; kind < 2; ++kind) {   // 0 = Prism (PrismType), 1 = Tesla (Overpowerable)
        World w(map);
        w.arm_w5(RulesW5{}, {});
        BuildingTypeDef tower;
        tower.id = kind == 0 ? "ATESLA" : "TESLA";
        tower.strength = 600;
        tower.weapon[0] = slot(/*aa=*/false, /*ag=*/true);   // PrismShot / CoilBolt: InvisibleHigh / Invisible
        tower.weapon[0].damage = 120;
        tower.weapon[0].rof = 45;
        tower.weapon[0].range = 8.0;
        tower.weapon[1] = tower.weapon[0];                    // PrismSupport / OPCoilBolt
        tower.armed = true;
        if (kind == 0) tower.w5.prism = true;
        else tower.w5.overpowerable = true;
        w.set_building_types({tower});
        w.add_building(0, 2, 2, 1, 1, 0, 600, 0, 0.0, /*instant=*/true);
        UnitCombat zep;
        zep.strength = 2000;
        zep.is_aircraft = true;
        Unit& k = w.add_combat_unit(5, 2, 1, zep);
        k.altitude = 500.0;
        k.flight_state = FlightState::Cruising;
        for (int f = 0; f < 120; ++f) w.step(1.0 / 15.0);
        CHECK(w.units()[0].hp == 2000);
        CHECK(w.buildings()[0].def_target == -1);
        // the same tower takes a ground unit
        UnitCombat htnk;
        htnk.strength = 400;
        Unit& g = w.add_combat_unit(5, 4, 1, htnk);
        for (int f = 0; f < 120; ++f) w.step(1.0 / 15.0);
        CHECK(w.units()[1].hp < 400);
        CHECK(w.units()[0].hp == 2000);
    }
}

TEST_CASE("w7r3: no legal weapon (-1) keeps the active slot -- a dog's NeverUse scanner never loads") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat dog;
    dog.strength = 100;
    dog.damage = 30;
    dog.rof = 20;
    dog.range = 1.5;
    dog.verses = {100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0};   // ParasiteDog
    dog.secondary = slot(true, true, {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100});   // VirtualScanner / SA
    dog.secondary.never_use = true;
    dog.secondary.range = 5.0;
    w.add_combat_unit(2, 2, 1, dog);
    UnitCombat tank;
    tank.strength = 400;
    tank.armor = "heavy";
    w.add_combat_unit(3, 2, 0, tank);
    Unit& du = w.units()[0];
    const Unit& tu = w.units()[1];
    CHECK(unit_weapon_for_unit(du, tu) == -1);
    const std::vector<int> before = du.w_verses;
    activate_weapon_slot(du, unit_weapon_for_unit(du, tu), nullptr);
    CHECK(du.w_active == 0);
    CHECK(du.w_verses == before);
    CHECK(du.w_verses[5] == 0);
}

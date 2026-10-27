// Wave-7 air / naval fixes (docs/gap/wave7/05-aircraft.md, 06-naval.md):
// the NavalTargeting switch on the victim's cell, and the SpawnManager node
// machine (launch spacing, gathering, per-node rebuild, rocket hand-off).
#include "doctest.h"

#include "air_naval.hpp"
#include "asset_fixture.hpp"
#include "world.hpp"

using namespace ra2engine;

TEST_CASE("w7airnav W7-06-02/09: NavalTargeting switch FUN_006F2D80 on a Water/Beach cell") {
    AnType sub;   // Typhoon: NavalTargeting=5 LandTargeting=1, one weapon
    sub.naval_targeting = kNavNavalAll;
    sub.land_targeting = 1;
    // A Naval Yard / a GI on a beach: on a naval cell -> legal (the old rule refused every structure).
    CHECK(an_naval_slot(sub, 0, false, /*in_water*/ true, false, false, false, false, false, /*bld*/ true) == 0);
    CHECK(an_naval_slot(sub, 0, false, true, false, false, true, false, false, false) == 0);
    // The same targets on land: LAND_NOT_OKAY.
    CHECK(an_naval_slot(sub, 0, false, false, false, false, false, false, false, true) == -1);
    CHECK(an_naval_slot(sub, 0, false, false, false, false, true, false, false, false) == -1);

    AnType dest;   // Destroyer: NavalTargeting=1, 155mm + ASWLauncher
    dest.naval_targeting = kNavUnderwaterSecondary;
    // An Underwater TYPE takes the ASW slot even when surfaced (case 1 tests +0xD69).
    CHECK(an_naval_slot(dest, 0, true, true, /*underwater*/ true, /*cloaked*/ false, false, false, false, false) == 1);
    CHECK(an_naval_slot(dest, 0, true, true, false, false, false, false, false, false) == 0);
    // A Naval Yard on the water: the cannon.
    CHECK(an_naval_slot(dest, 0, true, true, false, false, false, false, false, true) == 0);

    AnType deflt;   // NavalTargeting=0: a cloaked Underwater type is illegal, a surfaced one is not
    CHECK(an_naval_slot(deflt, 0, false, true, true, true, false, false, false, false) == -1);
    CHECK(an_naval_slot(deflt, 0, false, true, true, false, false, false, false, false) == 0);

    AnType osprey;   // NavalTargeting=2: subs only
    osprey.naval_targeting = kNavUnderwaterOnly;
    osprey.land_targeting = 1;
    CHECK(an_naval_slot(osprey, 0, false, true, true, false, false, false, false, false) == 0);
    CHECK(an_naval_slot(osprey, 0, false, true, false, false, false, false, false, false) == -1);

    AnType seal;   // NavalTargeting=4: C4 vs a ship, MP5 vs a hovercraft or an organic target
    seal.naval_targeting = kNavSealSpecial;
    CHECK(an_naval_slot(seal, 0, true, true, false, false, false, false, false, false) == 1);
    CHECK(an_naval_slot(seal, 1, true, true, false, false, false, /*hover*/ true, false, false) == 0);
    CHECK(an_naval_slot(seal, 1, true, true, false, false, /*organic*/ true, false, false, false) == 0);

    AnType aegis;   // NavalTargeting=6: nothing on the water, a plane yes
    aegis.naval_targeting = kNavNavalNone;
    aegis.land_targeting = 1;
    CHECK(an_naval_slot(aegis, 0, false, true, false, false, false, false, false, true) == -1);
    CHECK(an_naval_slot(aegis, 0, false, false, false, false, false, false, /*airborne*/ true, false) == 0);

    AnType boomer; // Stock BSUB: NavalTargeting=7, LandTargeting=2.
    boomer.naval_targeting=kNavBoomer;boomer.land_targeting=2;
    for(bool building : {false,true}) {
        CHECK(an_naval_slot(boomer,0,true,false,false,false,false,false,false,building)==1);
        CHECK(an_naval_slot(boomer,1,true,true,false,false,false,false,false,building)==0);
        CHECK(an_naval_slot(boomer,0,false,false,false,false,false,false,false,building)==-1);
    }
    CHECK(an_naval_slot(boomer,1,true,true,false,false,false,false,false,false)==0);
    CHECK(an_naval_slot(boomer,0,true,false,false,false,false,false,true,false)==0);
}

namespace {
bool w7_map(ra2assets::MapFile& out) {
    if (!ra2fixture::mapsmd03_available()) return false;
    out = ra2fixture::mapsmd03_map();
    return true;
}
UnitCombat w7_owner(const std::string& spawns, int number, int regen, int reload) {
    UnitCombat c;
    c.strength = 800;
    c.armor = "heavy";
    c.image = "W7OWN";
    c.damage = 0;
    c.rof = 60;
    c.range = 25.0;
    c.verses = std::vector<int>(11, 100);
    c.an.spawner_slot = 0;
    c.an.spawns = spawns;
    c.an.spawns_number = number;
    c.an.spawn_regen_frames = regen;
    c.an.spawn_reload_frames = reload;
    c.an.spawner_range = 25.0;
    return c;
}
UnitCombat w7_target() {
    UnitCombat c;
    c.strength = 5000;
    c.armor = "heavy";
    c.image = "W7TGT";
    c.damage = 0;
    c.rof = 1000;
    c.range = 0.0;
    c.verses = std::vector<int>(11, 0);
    return c;
}
int live_spawns(const World& w) {
    int n = 0;
    for (const Unit& u : w.units())
        if (u.an.t.spawned && u.alive && u.die_time < 0.0) ++n;
    return n;
}
}  // namespace

TEST_CASE("w7airnav W7-05-05/W7-06-03: the wing launches 20 frames apart and attacks together") {
    ra2assets::MapFile map;
    if (!w7_map(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(map);
    const ra2assets::MapCell* c = pick_start_cell(map);
    REQUIRE(c != nullptr);
    UnitCombat hornet;
    hornet.strength = 75;
    hornet.image = "W7HRN";
    hornet.is_aircraft = true;
    hornet.is_vehicle = true;
    hornet.damage = 0;
    hornet.range = 1.0;
    hornet.verses = std::vector<int>(11, 0);
    hornet.an.fixed_wing = true;
    hornet.an.spawned = true;
    hornet.an.ammo = 1;
    hornet.an.fly_speed_cps = 0.5;
    w.register_an_type("W7HRN", hornet);
    w.add_combat_unit(c->x, c->y, 0, w7_owner("W7HRN", 3, 600, 150));
    w.add_combat_unit(c->x + 6, c->y, 1, w7_target());
    REQUIRE(w.units()[0].an.sp_state.size() == 3);
    std::vector<int> launch_frames;
    int handoff = -1;
    for (int f = 0; f < 80; ++f) {
        w.units()[0].an.spawn_fire_unit = 1;   // the HornetLauncher names the target every frame
        const int before = live_spawns(w);
        w.step(1.0 / 15.0);
        if (live_spawns(w) > before) launch_frames.push_back(f);
        if (handoff < 0) {
            bool all = true;
            for (int st : w.units()[0].an.sp_state) all = all && st == kSpawnAttack;
            if (all) handoff = f;
        }
    }
    REQUIRE(launch_frames.size() == 3);
    CHECK(launch_frames[1] - launch_frames[0] == 20);
    CHECK(launch_frames[2] - launch_frames[1] == 20);
    // Nobody leaves for the target before the last one is out: the hand-off
    // runs in the tick that launched the third.
    CHECK(handoff == launch_frames[2]);
}

TEST_CASE("w7airnav W7-06-06/08/16: missiles 10 frames apart, per-node rebuild after pause + tilt, launch hold") {
    ra2assets::MapFile map;
    if (!w7_map(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(map);
    const ra2assets::MapCell* c = pick_start_cell(map);
    REQUIRE(c != nullptr);
    SwParams sw;
    sw.rockets.block[1].pause_frames = 20;   // [General] DMislPauseFrames / DMislTiltFrames
    sw.rockets.block[1].tilt_frames = 60;
    w.set_sw_params(sw);
    UnitCombat m;
    m.strength = 50;
    m.image = "W7MSL";
    m.is_aircraft = true;
    m.is_vehicle = true;
    m.verses = std::vector<int>(11, 0);
    m.an.spawned = true;
    m.an.missile = true;
    m.an.rocket_kind = 1;
    m.an.rocket_speed = 1;   // crawl: it must still be flying when the node rebuilds
    w.register_an_type("W7MSL", m);
    w.add_combat_unit(c->x, c->y, 0, w7_owner("W7MSL", 2, 80, 0));
    w.add_combat_unit(c->x + 12, c->y, 1, w7_target());
    std::vector<int> launch_frames;
    int wait_frame = -1, regen_frame = -1, ready_frame = -1;
    bool held = false;
    for (int f = 0; f < 260; ++f) {
        if (f < 5) w.units()[0].an.spawn_fire_unit = 1;
        const int before = live_spawns(w);
        w.step(1.0 / 15.0);
        if (live_spawns(w) > before) launch_frames.push_back(f);
        const AnUnit& a = w.units()[0].an;
        held = held || a.launch_hold;
        if (wait_frame < 0 && a.sp_state[0] == kSpawnRocketWait && a.sp_state[1] == kSpawnRocketWait) wait_frame = f;
        if (wait_frame >= 0 && regen_frame < 0 && a.sp_state[0] == kSpawnDead) regen_frame = f;
        if (regen_frame >= 0 && ready_frame < 0 && a.sp_state[0] == kSpawnDocked && a.sp_state[1] == kSpawnDocked)
            ready_frame = f;
    }
    // (the owner's own launcher keeps naming the target, so a second volley
    // follows once both nodes are rebuilt)
    REQUIRE(launch_frames.size() >= 2);
    CHECK(launch_frames[1] - launch_frames[0] == 10);   // 9-frame timer, 10-frame AI
    CHECK(wait_frame == launch_frames[1]);              // handed off in the second launch's tick
    CHECK(held);                                        // FUN_006B7400 > 0 -> the launcher holds
    REQUIRE(regen_frame > 0);
    CHECK(regen_frame - wait_frame >= 80);              // PauseFrames + TiltFrames
    CHECK(regen_frame - wait_frame < 90);
    REQUIRE(ready_frame > 0);
    CHECK(ready_frame - regen_frame >= 80);             // both nodes rebuild in parallel (SpawnRegenRate 80)
    CHECK(ready_frame - regen_frame < 90);
    if (launch_frames.size() > 2) CHECK(launch_frames[2] >= ready_frame);
}

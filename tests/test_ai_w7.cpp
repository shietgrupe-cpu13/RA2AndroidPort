// Wave-7 AI per-unit rules (engine/ai_unit.cpp): W7-01-08 auto-deploy,
// W7-04-18 ToProtect, W7-09-17 rules-id unit typing.
#include "doctest.h"

#include <vector>

#include "asset_fixture.hpp"
#include "world.hpp"

namespace {

ra2engine::UnitCombat w7_gi(bool deployer) {
    ra2engine::UnitCombat c;
    c.strength = 125;
    c.armor = "none";
    c.image = "GI";
    c.damage = 15;
    c.rof = 20;
    c.range = 4.0;
    c.verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    c.tg.rules_id = "E1";
    if (deployer) {
        c.deploy_fire = true;
        c.deploy_fire_slot = 1;
        c.secondary.valid = true;
        c.secondary.id = "Para";
        c.secondary.damage = 15;
        c.secondary.rof = 10;
        c.secondary.range = 5.0;
        c.secondary.verses = c.verses;
    }
    return c;
}

ra2engine::UnitCombat attack_move_unit(bool vehicle) {
    ra2engine::UnitCombat c = w7_gi(false);
    c.is_vehicle = vehicle;
    c.is_infantry = !vehicle;
    c.range = 1.0;
    c.sight = 6;
    return c;
}

bool free_cell(const ra2engine::World& w, int x, int y) {
    return w.walkable(x, y) && w.unit_at_cell(x, y) < 0 && w.building_at_cell(x, y) < 0;
}

// A free walkable cell in the ring around (cx,cy) at Chebyshev radius >= r0.
bool ring_cell(const ra2engine::World& w, int cx, int cy, int r0, int& ox, int& oy) {
    for (int r = r0; r <= r0 + 10; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (free_cell(w, cx + dx, cy + dy)) { ox = cx + dx; oy = cy + dy; return true; }
            }
    return false;
}

void three_houses(ra2engine::World& world) {
    for (int h = 0; h < 3; ++h) {
        ra2engine::World::HouseRec r;
        r.side = h == 1 ? ra2engine::World::Side::Soviet : ra2engine::World::Side::Allied;
        r.color_ix = h;
        world.set_house(h, r);
    }
}

}  // namespace

TEST_CASE("W7 attack-move scans sight, pauses infantry, and lets tanks fire while driving") {
    auto make_map = [] {
        auto map = ra2assets::MapFile::parse("[Map]\nSize=0,0,20,20\nTheater=TEMPERATE\n");
        auto& cells = const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x) cells.push_back({x, y, 0, 0, 0});
        return map;
    };
    auto arm = [](ra2engine::World& world) {
        three_houses(world);
        ra2engine::World::CombatW7Rules rules;
        rules.armed = true;
        rules.normal_targeting_delay = 0;
        world.arm_combat_w7(rules);
    };

    auto infantry_map = make_map();
    ra2engine::World infantry_world(infantry_map);
    arm(infantry_world);
    infantry_world.add_combat_unit(2, 2, 0, attack_move_unit(false));
    auto target = w7_gi(false);
    target.damage = 0;
    target.sight = 0;
    infantry_world.add_combat_unit(5, 2, 1, target);
    infantry_world.group_attack_move(0, {0}, 14, 2);
    const std::size_t infantry_route = infantry_world.units()[0].path.size();
    REQUIRE(infantry_route > 1);
    infantry_world.step(1.0 / 15.0);
    CHECK(infantry_world.units()[0].target == 1);
    CHECK_FALSE(infantry_world.units()[0].tg.opportunity);
    CHECK(infantry_world.units()[0].path.size() < infantry_route);

    auto tank_map = make_map();
    ra2engine::World tank_world(tank_map);
    arm(tank_world);
    tank_world.add_combat_unit(2, 10, 0, attack_move_unit(true));
    tank_world.add_combat_unit(5, 10, 1, target);
    tank_world.group_attack_move(0, {0}, 14, 10);
    const std::size_t tank_route = tank_world.units()[0].path.size();
    REQUIRE(tank_route > 1);
    tank_world.step(1.0 / 15.0);
    CHECK(tank_world.units()[0].target == 1);
    CHECK(tank_world.units()[0].tg.opportunity);
    CHECK(tank_world.units()[0].path.size() == tank_route);
    CHECK(tank_world.units()[0].fire_flash == 0.0);
}

// [RE: InfantryClass FUN_00521050 @0x52128C, rulesmd [General]
// AIAutoDeployFrameDelay=15,25,100]: a computer house's GI standing in Guard
// deploys once the delay has run; a house without the rule (or a human one)
// never does; a move order packs it up again.
TEST_CASE("W7-01-08: a computer GI auto-deploys after AIAutoDeployFrameDelay frames in Guard") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(ra2fixture::mapsmd03_map());
    three_houses(world);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(ra2fixture::mapsmd03_map());
    REQUIRE(c != nullptr);
    ra2engine::AiHouseConfig cfg;
    cfg.auto_deploy_frames = 15;            // the Hard (house+0x184 = 0) entry
    world.enable_ai_house(1, cfg);
    ra2engine::AiHouseConfig off;          // auto_deploy_frames = -1: no rule
    world.enable_ai_house(2, off);
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x0 = 0, y0 = 0;
    REQUIRE(ring_cell(world, c->x, c->y, 0, x1, y1));
    world.add_combat_unit(x1, y1, 1, w7_gi(true));
    REQUIRE(ring_cell(world, c->x, c->y, 3, x2, y2));
    world.add_combat_unit(x2, y2, 2, w7_gi(true));
    world.set_pair_alliance(1, 2, true);
    REQUIRE(ring_cell(world, c->x + 40, c->y + 40, 0, x0, y0));
    world.add_combat_unit(x0, y0, 0, w7_gi(true));   // human house: never auto-deploys
    for (int f = 0; f < 10; ++f) world.step(1.0 / 15.0);
    CHECK_FALSE(world.units()[0].deployed);          // the delay has not run yet
    for (int f = 0; f < 20; ++f) world.step(1.0 / 15.0);
    CHECK(world.units()[0].deployed);                 // computer house, rule armed
    CHECK_FALSE(world.units()[1].deployed);           // rule off
    CHECK_FALSE(world.units()[2].deployed);           // human house
    // A move undeploys (FUN_0051F370) and the guard clock restarts on arrival.
    int mx = 0, my = 0;
    REQUIRE(ring_cell(world, x1, y1, 2, mx, my));
    world.order_unit_move(0, mx, my);
    CHECK_FALSE(world.units()[0].deployed);
    for (int f = 0; f < 600 && world.units()[0].moving(); ++f) world.step(1.0 / 15.0);
    CHECK_FALSE(world.units()[0].moving());
    for (int f = 0; f < 20; ++f) world.step(1.0 / 15.0);
    CHECK(world.units()[0].deployed);                 // re-deployed at the new post
}

// [RE: FUN_00522070]: in Attack, a computer GI deploys once its target is inside
// the deploy weapon's range, and undeploys (to pursue) when it is not.
TEST_CASE("W7-01-08: a computer GI deploys on an in-range attack target and undeploys for a far one") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(ra2fixture::mapsmd03_map());
    three_houses(world);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(ra2fixture::mapsmd03_map());
    REQUIRE(c != nullptr);
    ra2engine::AiHouseConfig cfg;
    cfg.auto_deploy_frames = 100000;   // keep the Guard branch out of the way
    world.enable_ai_house(1, cfg);
    int x1 = 0, y1 = 0;
    REQUIRE(ring_cell(world, c->x, c->y, 0, x1, y1));
    world.add_combat_unit(x1, y1, 1, w7_gi(true));
    int tx = 0, ty = 0;
    REQUIRE(ring_cell(world, x1, y1, 3, tx, ty));     // 3 cells: inside Para's 5
    ra2engine::UnitCombat tgt = w7_gi(false);
    tgt.strength = 100000;                            // survives the test
    tgt.damage = 0;
    world.add_combat_unit(tx, ty, 0, tgt);
    world.order_unit_attack(0, 1);
    world.step(1.0 / 15.0);
    CHECK(world.units()[0].deployed);
}

// fv7c: the call for help (FUN_00707500) is ONE routine, World::call_for_help,
// run from the W7 hit log for every computer house; the type's ToProtect flag
// (not the AI roster) triggers it, and the candidates are ranked by
// FUN_004D96B0 (ThreatPosed) within attacker ThreatPosed x 3.
namespace {
void arm_w7(ra2engine::World& world) {
    ra2engine::World::CombatW7Rules r;
    r.armed = true;
    world.arm_combat_w7(r);
}
struct HelpRig {
    ra2engine::World world{ra2fixture::mapsmd03_map()};
    int vx = 0, vy = 0;
    bool ok = false;
    HelpRig() {
        three_houses(world);
        arm_w7(world);
        const ra2assets::MapCell* c = ra2engine::pick_start_cell(ra2fixture::mapsmd03_map());
        if (!c || !ring_cell(world, c->x, c->y, 0, vx, vy)) return;
        ok = true;
    }
    int add(int r0, int house, ra2engine::UnitCombat u) {
        int x = 0, y = 0;
        if (!ring_cell(world, vx, vy, r0, x, y)) return -1;
        world.add_combat_unit(x, y, house, u);
        return (int)world.units().size() - 1;
    }
    bool helped(int i, int a) const {
        const ra2engine::Unit& u = world.units()[(std::size_t)i];
        return u.target == a;
    }
};
ra2engine::UnitCombat protect_victim(bool to_protect) {
    ra2engine::UnitCombat v = w7_gi(false);
    v.tg.rules_id = "HARV";
    v.tg.to_protect = to_protect;
    v.strength = 100000;
    v.damage = 0;
    return v;
}
ra2engine::UnitCombat threat_gi(int threat) {
    ra2engine::UnitCombat u = w7_gi(false);
    u.tg.threat_posed = threat;
    return u;
}
}  // namespace

TEST_CASE("W7-04-18 / fv7c: an attacked ToProtect unit calls idle units onto its attacker (FUN_00707500)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(true));                 // the "miner"
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);                                    // the enemy
    ra2engine::UnitCombat helper = threat_gi(10);
    helper.range = 1.0;                                                // cannot see the attacker itself
    const int h = g.add(9, 1, helper);                                 // an idle defender
    REQUIRE((v >= 0 && a >= 0 && h >= 0));
    g.world.order_unit_attack(a, v);
    bool called = false;
    for (int f = 0; f < 120 && !called; ++f) {
        g.world.step(1.0 / 15.0);
        called = g.helped(h, a);
    }
    CHECK(g.world.units()[(std::size_t)v].hp < 100000);   // the miner was hit
    CHECK(called);                                         // and its house answered
}

TEST_CASE("fv7c: the call for help needs GetFireError(attacker, 0) != ILLEGAL -- a dog is not sent at a tank") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(true));
    ra2engine::UnitCombat tank = threat_gi(10);
    tank.strength = 100000;
    tank.armor = "heavy";
    const int a = g.add(3, 0, tank);
    ra2engine::UnitCombat dog = threat_gi(20);
    dog.range = 1.0;
    dog.verses = {100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0};   // [ParasiteDog]-like: 0% vs vehicles
    const int d = g.add(2, 1, dog);
    ra2engine::UnitCombat e2 = threat_gi(5);
    e2.range = 1.0;
    const int e = g.add(9, 1, e2);
    REQUIRE((v >= 0 && a >= 0 && d >= 0 && e >= 0));
    g.world.order_unit_attack(a, v);
    bool e_called = false;
    for (int f = 0; f < 120 && !e_called; ++f) {
        g.world.step(1.0 / 15.0);
        e_called = g.helped(e, a);
    }
    CHECK(e_called);
    CHECK_FALSE(g.helped(d, a));
}

TEST_CASE("fv7c: a hit unit WITHOUT ToProtect calls nobody") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(false));
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);
    ra2engine::UnitCombat helper = threat_gi(10);
    helper.range = 1.0;
    const int h = g.add(9, 1, helper);
    REQUIRE((v >= 0 && a >= 0 && h >= 0));
    g.world.order_unit_attack(a, v);
    for (int f = 0; f < 120; ++f) g.world.step(1.0 / 15.0);
    CHECK(g.world.units()[(std::size_t)v].hp < 100000);
    CHECK_FALSE(g.helped(h, a));
}

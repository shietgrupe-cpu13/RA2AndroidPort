// Wave-6 #4 (units and weapons, docs/gap/wave6/04-units.md) -- unit tests for
// the pure pieces and the real-rulesmd resolution.
#include "doctest.h"

#include <cmath>

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "weapon.hpp"
#include "weapon_resolve.hpp"
#include "weapon_unit.hpp"
#include "world.hpp"

using namespace ra2engine;

TEST_CASE("weapon rules: IsMagBeam is read from the weapon section") {
    const ra2assets::Ini ini = ra2assets::Ini::parse(
        "[MagneticBeam]\nDamage=5000\nROF=100\nRange=12\nIsMagBeam=yes\n"
        "[OrdinaryBeam]\nDamage=10\nROF=10\nRange=5\nIsLaser=yes\n");
    const ra2assets::Rules rules = ra2assets::Rules::load(ini);
    CHECK(rules.section_flag("MagneticBeam", "IsMagBeam", false));
    CHECK_FALSE(rules.section_flag("OrdinaryBeam", "IsMagBeam", false));
}

TEST_CASE("w6units W6-04-01: Burst rearm = Random(3,5) inside a burst, ROF+Random(0,2) after the last shot") {
    // Burst 1 is exactly rearm_frames_for, with the same RNG draw.
    {
        ProjRng a, b;
        int idx = 0;
        for (int k = 0; k < 50; ++k) {
            CHECK(burst_rearm_frames(idx, 1, 60, &a) == rearm_frames_for(60, &b));
            CHECK(idx == 0);
        }
    }
    // Burst 4 (Apocalypse 120mmxE): three short rearms, then the full ROF.
    ProjRng rng;
    int idx = 0;
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (int shot = 0; shot < 4; ++shot) {
            const int f = burst_rearm_frames(idx, 4, 80, &rng);   // Net L0: whole frames
            if (shot < 3) {
                CHECK(f >= 3);
                CHECK(f <= 5);
                CHECK(idx == shot + 1);
            } else {
                CHECK(f >= 80);
                CHECK(f <= 82);
                CHECK(idx == 0);
            }
        }
    }
    // A weapon switch to a smaller Burst wraps the index (modulo, as Fire does).
    idx = 3;
    burst_rearm_frames(idx, 2, 60, nullptr);
    CHECK(idx == 0);
}

TEST_CASE("w6units W6-04-01: real rulesmd Burst resolves on every slot") {
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("no install"); return; }
    const ra2assets::Rules& r = ra2fixture::rulesmd();
    CHECK(resolve_weapon_slot(r, "120mmx").burst == 2);
    CHECK(resolve_weapon_slot(r, "120mmxE").burst == 4);
    CHECK(resolve_weapon_slot(r, "HoverMissile").burst == 2);
    CHECK(resolve_weapon_slot(r, "Maverick3").burst == 2);
    CHECK(resolve_weapon_slot(r, "105mm").burst == 1);
    CHECK(resolve_weapon_slot(r, "105mmE").burst == 2);
    CHECK(resolve_weapon_slot(r, "M60").burst == 1);
    const TypeWeapons apoc = resolve_type_weapons(r, "APOC");
    CHECK(apoc.slot[0].burst == 2);
    CHECK(apoc.slot[1].burst == 2);   // MammothTusk
    CHECK(apoc.elite[0].burst == 4);
}

TEST_CASE("w6units W6-04-02: shrapnel keys come from the projectile, elite slots included") {
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("no install"); return; }
    const ra2assets::Rules& r = ra2fixture::rulesmd();
    const WeaponSlot comet = resolve_weapon_slot(r, "Comet");          // LargeCometP
    CHECK(comet.shrapnel_count == 5);
    CHECK(comet.shrapnel_damage == r.weapon_damage("CometFragment"));
    CHECK(comet.shrapnel_range == r.weapon_range("CometFragment"));
    const WeaponSlot super = resolve_weapon_slot(r, "SuperComet");     // SuperCometP
    CHECK(super.shrapnel_count == 5);
    CHECK(super.shrapnel_damage == r.weapon_damage("SuperCometFragment"));
    CHECK(resolve_weapon_slot(r, "ElectricBoltE").shrapnel_count == 2);  // Electricbounce
    CHECK(resolve_weapon_slot(r, "TankBoltE").shrapnel_count == 2);
    CHECK(resolve_weapon_slot(r, "ElectricBolt").shrapnel_count == 0);
    CHECK(resolve_weapon_slot(r, "TankBolt").shrapnel_count == 0);
    const TypeWeapons sref = resolve_type_weapons(r, "SREF");
    CHECK(sref.slot[0].shrapnel_count == 5);
    CHECK(sref.elite[0].shrapnel_count == 5);
}

TEST_CASE("w6units W6-04-06: OmniFire is read for every weapon slot (Aegis Medusa, launchers)") {
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("no install"); return; }
    const ra2assets::Rules& r = ra2fixture::rulesmd();
    CHECK(resolve_weapon_slot(r, "Medusa").omni_fire);
    CHECK(resolve_weapon_slot(r, "HornetLauncher").omni_fire);
    CHECK(resolve_weapon_slot(r, "DredLauncher").omni_fire);
    CHECK_FALSE(resolve_weapon_slot(r, "120mm").omni_fire);
    // activate_weapon_slot carries the slot's OmniFire into the unit.
    Unit u;
    u.w2.valid = true;
    u.w2.omni_fire = true;
    u.vet.base.omni_fire = false;
    activate_weapon_slot(u, 1, nullptr);
    CHECK(u.w6.omni_fire);
    activate_weapon_slot(u, 0, nullptr);
    CHECK_FALSE(u.w6.omni_fire);
}

namespace {
UnitCombat w6mk(const char* image, int strength, const char* armor, int dmg, int rof, double range,
                std::vector<int> verses, bool vehicle = false) {
    UnitCombat c;
    c.image = image;
    c.strength = strength;
    c.armor = armor;
    c.damage = dmg;
    c.rof = rof;
    c.range = range;
    c.verses = std::move(verses);
    c.is_vehicle = vehicle;
    return c;
}
bool w6_open_rect(const World& w, int W, int H, int& x0, int& y0) {
    for (int y = 4; y + H < w.grid_h() - 4; ++y)
        for (int x = 4; x + W < w.grid_w() - 4; ++x) {
            bool ok = true;
            for (int dy = 0; dy < H && ok; ++dy)
                for (int dx = 0; dx < W && ok; ++dx)
                    if (!w.walkable(x + dx, y + dy) || w.unit_at_cell(x + dx, y + dy) >= 0) ok = false;
            if (ok && w.cell_level(x, y) == w.cell_level(x + W - 1, y + H - 1)) { x0 = x; y0 = y; return true; }
        }
    return false;
}
}  // namespace

TEST_CASE("w6units W6-04-03: an Ivan bomb on a 2x2 building hits once per foundation cell (DamageArea)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World world(ra2fixture::mapsmd03_map());
    int x0 = 0, y0 = 0;
    REQUIRE(w6_open_rect(world, 12, 4, x0, y0));
    AbilityProfile ip;
    ip.fire_kind = AbilityKind::IvanBomb;
    ip.infantry = true;
    world.register_ability_profile("IVAN", ip);
    const std::vector<int> all100 = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    const int b = world.add_building(1, x0 + 6, y0, 2, 2, 1, 2000, armor_index("wood"), 0.0,
                                     /*instant=*/true);
    world.add_combat_unit(x0, y0, 0, w6mk("IVAN", 125, "none", 400, 50, 1.5, all100));
    world.units()[0].target_building = b;
    world.units()[0].attack_order = true;
    int blast = -1;
    for (int k = 0; k < 900 && blast < 0; ++k) {
        world.step(1.0 / 15.0);
        for (const World::AbilityEvent& e : world.take_ability_events())
            if (e.kind == World::AbilityEvent::Kind::BombBlast) blast = k;
    }
    REQUIRE(blast >= 0);
    // IvanWH 450, CellSpread 1.5 (384 lept), PercentAtMax .25, wood 100 %: the
    // impact cell takes 450, the other three cells (181 lept) 290 each.
    CHECK(world.buildings()[b].hp == 2000 - 450 - 3 * 290);
}

TEST_CASE("w6units W6-04-09: a Sonic hit on the host frees it from its parasite (FUN_004D7250)") {
    if (ra2fixture::rulesmd_bytes().empty() || !ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    CHECK(resolve_weapon_slot(ra2fixture::rulesmd(), "SonicZap").sonic);
    CHECK_FALSE(resolve_weapon_slot(ra2fixture::rulesmd(), "120mm").sonic);
    World world(ra2fixture::mapsmd03_map());
    int x0 = 0, y0 = 0;
    REQUIRE(w6_open_rect(world, 12, 4, x0, y0));
    AbilityProfile dp;
    dp.fire_kind = AbilityKind::Parasite;
    dp.parasiteable = false;
    dp.suppression_threshold = 5;
    world.register_ability_profile("DRON", dp);
    const std::vector<int> para = {100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0};
    const std::vector<int> all100 = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    world.add_combat_unit(x0, y0, 1, w6mk("DRON", 100, "special_1", 50, 60, 1.83, para));
    world.add_combat_unit(x0 + 1, y0, 0, w6mk("HTNK", 4000, "heavy", 0, 60, 0.0, {}, true));
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    for (int k = 0; k < 30; ++k) world.step(1.0 / 15.0);
    REQUIRE(world.units()[1].parasite_on_me == 0);
    // A Sonic shooter of the drone's side hits the infected tank.
    world.add_combat_unit(x0 + 4, y0, 1, w6mk("SHK", 125, "none", 4, 15, 6.0, all100));
    world.units()[2].w6.sonic = true;
    world.units()[2].target = 1;
    world.units()[2].attack_order = true;
    bool freed = false;
    for (int k = 0; k < 60 && !freed; ++k) {
        world.step(1.0 / 15.0);
        freed = world.units()[1].parasite_on_me < 0;
    }
    CHECK(freed);
    CHECK(world.units()[0].cargo_of < 0);
    CHECK(world.units()[0].alive);
}

TEST_CASE("w6units W6-04-14/15/16/29/30: small rules keys resolve and gate crushing") {
    Unit bfrt, tank, ggi, gi, mcv;
    bfrt.crusher = true;
    bfrt.is_vehicle = true;
    bfrt.w6.t.omni_crusher = true;
    tank.is_vehicle = true;
    mcv.is_vehicle = true;
    mcv.w6.t.omni_crush_resistant = true;
    gi.crushable = true;
    ggi.crushable = true;
    ggi.deployed = true;
    ggi.w6.t.deployed_crushable = false;
    Unit rhino;
    rhino.crusher = true;
    rhino.is_vehicle = true;
    CHECK(can_crush(bfrt, tank));          // OmniCrusher: vehicles too
    CHECK_FALSE(can_crush(bfrt, mcv));     // OmniCrushResistant
    CHECK_FALSE(can_crush(rhino, tank));   // a plain Crusher: infantry only
    CHECK(can_crush(rhino, gi));
    CHECK_FALSE(can_crush(rhino, ggi));    // deployed Guardian GI
    ggi.deployed = false;
    CHECK(can_crush(rhino, ggi));
}

TEST_CASE("w6units W6-04-04: Reduce_Wall stage model (FUN_00480BC0) and the warhead gate") {
    // WallAbsoluteDestroyer erases at once.
    CHECK(reduce_wall(0, 3, 300, -1, true, nullptr).destroyed);
    // A hit >= Strength always advances; GAWALL (3 levels) connected: 1, 2, gone at 3.
    WallStep a = reduce_wall(0, 3, 300, 600, true, nullptr);
    CHECK(a.advanced);
    CHECK(a.stage == 1);
    CHECK_FALSE(a.destroyed);
    WallStep b = reduce_wall(1, 3, 300, 600, true, nullptr);
    CHECK(b.stage == 2);
    CHECK(b.cascade);            // DamageLevels-1 with DamageLevels > 2
    CHECK_FALSE(b.destroyed);    // still has a neighbour
    CHECK(reduce_wall(2, 3, 300, 600, true, nullptr).destroyed);
    // An isolated wall is gone at DamageLevels-1.
    CHECK(reduce_wall(1, 3, 300, 600, false, nullptr).destroyed);
    // Below Strength: advances only when Random(0,Strength) < dmg.
    ProjRng rng;
    int adv = 0;
    for (int k = 0; k < 2000; ++k) adv += reduce_wall(0, 3, 300, 30, true, &rng).advanced ? 1 : 0;
    CHECK(adv > 100);
    CHECK(adv < 300);            // ~30/301 of 2000 = ~199
    // The warhead gate: no Wall key = untouched, Wood only on wood walls.
    CHECK(wall_hit_damage(0, false, 15) == 0);
    CHECK(wall_hit_damage(kW6Wall, false, 90) == 90);
    CHECK(wall_hit_damage(kW6Wood, false, 90) == 0);
    CHECK(wall_hit_damage(kW6Wood, true, 90) == 90);
    CHECK(wall_hit_damage(kW6WallAbs | kW6Wood, false, 600) == -1);
}

TEST_CASE("w6units BombDisarm: the DefuseKit only targets an object carrying an Ivan bomb (GetFireError FUN_006FB5D0)") {
    // [RE: FUN_006FB5D0 -- Warhead->BombDisarm (+0x16E) && Target->AttachedBomb
    // (+0x38) == 0 -> FireError ILLEGAL]. Before the fix an idle computer
    // engineer acquired an enemy building in its GuardRange and wore it down
    // 1 HP per DefuseKit shot (ALL09's Tech Airport, 800 HP in ~7000 frames).
    Unit eng;
    eng.w6.t.bomb_disarm = true;
    eng.w_verses = std::vector<int>(11, 100);
    eng.vet.base.verses = eng.w_verses;
    Unit victim;
    CHECK(unit_weapon_for_unit(eng, victim) == -1);
    CHECK(unit_weapon_for_building(eng, 0, false) == -1);
    CHECK(unit_weapon_for_building(eng, 0) == -1);
    victim.bomb_frame = 500;
    CHECK(unit_weapon_for_unit(eng, victim) == 0);
    CHECK(unit_weapon_for_building(eng, 0, true) == 0);
    // An ordinary weapon is unaffected.
    Unit gi;
    gi.w_verses = eng.w_verses;
    gi.vet.base.verses = eng.w_verses;
    victim.bomb_frame = -1;
    CHECK(unit_weapon_for_unit(gi, victim) == 0);
    CHECK(unit_weapon_for_building(gi, 0, false) == 0);
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("no install"); return; }
    CHECK(resolve_weapon_slot(ra2fixture::rulesmd(), "DefuseKit").bomb_disarm);
    CHECK_FALSE(resolve_weapon_slot(ra2fixture::rulesmd(), "M60").bomb_disarm);
    CHECK(resolve_type_weapons(ra2fixture::rulesmd(), "ENGINEER").slot[0].bomb_disarm);
}

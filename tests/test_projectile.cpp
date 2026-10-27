#include "doctest.h"
#include "iso_project.hpp"
#include "building_transition.hpp"

TEST_CASE("presentation: projectiles keep their terrain anchor and relative flight height") {
    CHECK(ra2engine::projectile_screen_y(300,0)==300);
    CHECK(ra2engine::projectile_screen_y(300,104)==285);
    CHECK(ra2engine::projectile_screen_y(240,104)==225);
    CHECK(ra2engine::projectile_screen_y(240,416)==180);
}
TEST_CASE("presentation: bunker motion follows authored opening and reverse closing durations") {
    CHECK(ra2engine::bunker_motion_ticks(0,10,4,false)==40);
    CHECK(ra2engine::bunker_motion_ticks(10,20,4,false)==40);
    CHECK(ra2engine::bunker_motion_ticks(0,10,4,true)==40);
    CHECK(ra2engine::bunker_motion_ticks(0,0,0,false)==1);
    CHECK(ra2engine::transition_blend(0,40,0)==0);
    CHECK(ra2engine::transition_blend(40,40,0)==1);
    CHECK(ra2engine::transition_blend(20,40,0)==doctest::Approx(0.5));
    CHECK(ra2engine::transition_blend(20,40,0.5)>0.5);
    double last=0;
    for(int render=0;render<=160;++render) {
        const double value=ra2engine::transition_blend(render/4,40,(render%4)/4.0);
        CHECK(value>=last);CHECK(value<=1);last=value;
    }
}

#include <algorithm>
#include <cmath>
#include <set>

#include "projectile.hpp"

using namespace ra2engine;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

TEST_CASE("projectile: weapon Speed= -> leptons/frame (FUN_00474740)") {
    CHECK(weapon_speed_leptons(20) == 51);    // [BlimpBomb] -> Kirov terminal speed (§6.2)
    CHECK(weapon_speed_leptons(40) == 102);   // [120mm]
    CHECK(weapon_speed_leptons(25) == 64);    // [SubTorpedo] (§6.5 table)
    CHECK(weapon_speed_leptons(100) == 255);  // 256 clamped to 255
    CHECK(weapon_speed_leptons(120) == 255);  // clamp(.,0,100) first
    CHECK(weapon_speed_leptons(-5) == 0);
}

TEST_CASE("projectile: ROT==0 launch speed = ftol(sqrt(d*g*1.2)) (§0.2 table)") {
    CHECK(arcing_launch_speed(1472, 6.0) == 102);  // Rhino at 5.75 cells
    CHECK(arcing_launch_speed(768, 6.0) == 74);    // Rhino at 3 cells
    CHECK(arcing_launch_speed(1280, 6.0) == 96);   // Grizzly / Flak Track at 5 cells
    CHECK(arcing_launch_speed(2048, 6.0) == 121);  // Destroyer at 8 cells
    CHECK(arcing_launch_speed(0, 6.0) == 0);
}

TEST_CASE("projectile: lepton_to_cell rounds to the clone's cell-centre convention") {
    CHECK(lepton_to_cell(0) == 0);
    CHECK(lepton_to_cell(127) == 0);
    CHECK(lepton_to_cell(128) == 1);
    CHECK(lepton_to_cell(-128) == 0);
    CHECK(lepton_to_cell(-129) == -1);
    CHECK(lepton_to_cell(5 * 256) == 5);
}

TEST_CASE("projectile: arc solver flat ground matches sin(2theta) = d*g/v^2 (§12.1)") {
    double th = 0.0;
    REQUIRE(arc_solve_core(false, 102, 1472, 0, 6.0, th));
    // Net L0: the solver runs on the original's FastMath tables (sqrt/acos), so
    // the identity holds to table precision, not to libm's last bit.
    CHECK(std::sin(2.0 * th) == doctest::Approx(1472.0 * 6.0 / (102.0 * 102.0)).epsilon(2e-3));
    CHECK(th < kPi / 4.0);  // the plus root is the LOW arc
    double hi = 0.0;
    REQUIRE(arc_solve_core(true, 102, 1472, 0, 6.0, hi));
    CHECK(hi > kPi / 4.0);  // the minus root is the lofted arc
    CHECK(th + hi == doctest::Approx(kPi / 2.0).epsilon(2e-3));  // complementary on flat ground (FastMath precision)
}

TEST_CASE("projectile: arc solver - target above the muzzle is a flat 45 degrees") {
    double th = 0.0;
    REQUIRE(arc_solve_core(false, 50, 1000, 1, 6.0, th));
    CHECK(th == doctest::Approx(kPi / 4.0));
    double phi = 0.0;
    REQUIRE(arc_pitch(50, 1000, 300, 6.0, phi));
    CHECK(phi == doctest::Approx(kPi / 4.0).epsilon(1e-3));
}

TEST_CASE("projectile: arc solver refuses a shot with no solution") {
    double th = 0.0;
    CHECK_FALSE(arc_solve_core(false, 10, 2000, 0, 6.0, th));  // far too slow
    double phi = 0.0;
    CHECK_FALSE(arc_pitch(10, 2000, -100, 6.0, phi));
}

TEST_CASE("projectile: arc_pitch derivative sign test makes a far-below low shot point DOWN") {
    // Muzzle 1000 leptons above the target, short range: the textbook low root is
    // below the horizontal; acos alone would lose that sign (§12.1 wrapper).
    double phi = 0.0;
    REQUIRE(arc_pitch(80, 300, -1000, 6.0, phi));
    CHECK(phi < 0.0);
    // Ordinary flat tank shot points up.
    REQUIRE(arc_pitch(102, 1472, -100, 6.0, phi));
    CHECK(phi > 0.0);
    CHECK(phi < kPi / 4.0);
}

TEST_CASE("projectile: straight_pitch is level within 200 leptons of height") {
    CHECK(straight_pitch(1000, 150) == doctest::Approx(0.0).epsilon(1e-3));
    CHECK(straight_pitch(1000, -199) == doctest::Approx(0.0).epsilon(1e-3));
    const double down = straight_pitch(300, -610);  // Kirov over its target
    CHECK(down < -1.0);                              // steeply down
    CHECK(down == doctest::Approx(-std::atan2(590.0, 300.0)).epsilon(3e-2));   // Net L0: atan2 table (0x860034) step
}

TEST_CASE("projectile: ProjRng.random reproduces FUN_0065C220's inclusive range mapping") {
    ProjRng r;
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) {
        const int v = r.random(3, 7);
        CHECK(v >= 3);
        CHECK(v <= 7);
        seen.insert(v);
    }
    CHECK(seen.size() == 5u);       // inclusive both ends
    CHECK(r.random(9, 9) == 9);     // lo == hi -> lo, no draw needed
    for (int i = 0; i < 200; ++i) {  // swapped bounds behave like (lo, hi)
        const int v = r.random(256, 128);
        CHECK(v >= 128);
        CHECK(v <= 256);
    }
    ProjRng a, b;  // deterministic: same seed, same stream
    for (int i = 0; i < 50; ++i) CHECK(a.random(0, 0x7FFFFFFE) == b.random(0, 0x7FFFFFFE));
}

TEST_CASE("projectile: ProjRng.angle covers the circle") {
    ProjRng r;
    double mn = 10.0, mx = -10.0;
    for (int i = 0; i < 4000; ++i) {
        const double a = r.angle();
        mn = std::min(mn, a);
        mx = std::max(mx, a);
    }
    CHECK(mx - mn > 1.9 * kPi);  // spans (nearly) a full turn
}

TEST_CASE("projectile: ballistic integrator one step (§6.1 semi-implicit Euler, ftol)") {
    Projectile p;
    p.x = 100; p.y = 200; p.z = 50;
    p.vx = 10.7; p.vy = -3.2; p.vz = 20.0;
    integrate_ballistic(p, 6.0);
    CHECK(p.vz == doctest::Approx(14.0));  // gravity BEFORE the position step
    CHECK(p.x == 110);                      // ftol(110.7)
    CHECK(p.y == 196);                      // ftol(196.8)
    CHECK(p.z == 64);                       // 50 + 14
    CHECK_FALSE(p.slow);
    Projectile q;
    q.vx = 3.0;
    integrate_ballistic(q, 6.0);
    CHECK(q.slow);  // |V| < 8.0 sampled before gravity
}

TEST_CASE("projectile: vertical integrator accelerates +Acceleration to the Speed cap, direction frozen") {
    Projectile p;
    p.kind = ProjKind::Vertical;
    p.z = 610;
    p.vz = -1.0;  // launched at 1 lepton/frame (§12.3)
    p.speed_cap = 51;
    p.acceleration = 1;
    int frames = 0;
    while (p.z > 0 && frames < 100) {
        integrate_vertical(p);
        ++frames;
        CHECK(p.vx == 0.0);
        CHECK(p.vy == 0.0);
    }
    // n(n+1)/2 >= 610 -> ~34 frames with the first step at speed 2 (§6.2 worked example).
    CHECK(frames >= 30);
    CHECK(frames <= 40);
    CHECK(std::fabs(p.vz) < 51.0);  // the cap is not reached from Kirov height
    // From high enough the speed saturates at the weapon Speed cap (51).
    Projectile q;
    q.kind = ProjKind::Vertical;
    q.z = 5000;
    q.vz = -1.0;
    q.speed_cap = 51;
    q.acceleration = 1;
    for (int i = 0; i < 80; ++i) integrate_vertical(q);
    CHECK(std::fabs(q.vz) == doctest::Approx(51.0));
}

TEST_CASE("projectile: compute_launch - Rhino at 5.75 cells, flat, no scatter") {
    ProjectileSpec spec;
    spec.kind = ProjKind::Ballistic;
    spec.arcing = true;
    ProjectileRules rules;
    ProjRng rng;
    const ProjRng before = rng;
    const LaunchResult lr = compute_launch(spec, rules, Vec3i{0, 0, 0}, Vec3i{1472, 0, 0}, rng);
    REQUIRE(lr.ok);
    CHECK(lr.speed == 102);
    CHECK(rng.s == before.s);  // no scatter -> no RNG draw
    CHECK(std::sqrt(lr.vx * lr.vx + lr.vy * lr.vy + lr.vz * lr.vz) ==
          doctest::Approx(102.0).epsilon(1e-6));
    CHECK(lr.vx > 0.0);
    CHECK(lr.vy == doctest::Approx(0.0));
    CHECK(lr.vz > 0.0);
}

TEST_CASE("projectile: a flat Rhino shot flies ~16 frames and comes down in the target cell") {
    ProjectileSpec spec;
    spec.kind = ProjKind::Ballistic;
    spec.arcing = true;
    ProjectileRules rules;
    ProjRng rng;
    const LaunchResult lr = compute_launch(spec, rules, Vec3i{0, 0, 0}, Vec3i{1472, 0, 0}, rng);
    REQUIRE(lr.ok);
    Projectile p;
    p.vx = lr.vx; p.vy = lr.vy; p.vz = lr.vz;
    int frames = 0, apex = 0;
    bool arrived = false;
    while (frames < 60) {
        integrate_ballistic(p, rules.gravity);
        ++frames;
        apex = std::max(apex, p.z);
        if (lepton_to_cell(p.x) == lepton_to_cell(1472) && lepton_to_cell(p.y) == 0 &&
            p.z < kArrivalHeight) { arrived = true; break; }
        if (p.z < 0) break;
    }
    CHECK(arrived);
    CHECK(frames >= 13);   // §4.1: T ~ 0.4226*sqrt(1472) = 16.2 frames to landing
    CHECK(frames <= 17);
    CHECK(apex > 150);
}

TEST_CASE("projectile: speed cap - a shot never crosses more than half the distance in frame 1") {
    ProjectileSpec spec;
    spec.kind = ProjKind::Ballistic;  // straight (non-arcing) ROT==0
    ProjectileRules rules;
    ProjRng rng;
    const LaunchResult lr = compute_launch(spec, rules, Vec3i{0, 0, 0}, Vec3i{20, 0, 0}, rng);
    REQUIRE(lr.ok);
    CHECK(lr.speed == 10);  // sqrt(20*7.2)=12 capped to 20/2
}

TEST_CASE("projectile: compute_launch - Vertical launches at speed 1 toward the target") {
    ProjectileSpec spec;
    spec.kind = ProjKind::Vertical;
    spec.speed_cap = 51;
    spec.acceleration = 1;
    ProjectileRules rules;
    ProjRng rng;
    const LaunchResult lr = compute_launch(spec, rules, Vec3i{0, 0, 610}, Vec3i{200, 0, 0}, rng);
    REQUIRE(lr.ok);
    CHECK(lr.speed == 1);
    CHECK(std::sqrt(lr.vx * lr.vx + lr.vy * lr.vy + lr.vz * lr.vz) ==
          doctest::Approx(1.0).epsilon(1e-3));   // Net L0: FastSqrt-normalised
    CHECK(lr.vz < -0.9);  // nearly straight down
    CHECK(lr.vx > 0.0);
}

TEST_CASE("projectile: §5.1 scatter radius in [BS/2, BS]; §5.2 FlakScatter scales with distance") {
    ProjectileRules rules;
    ProjectileSpec s1;
    s1.kind = ProjKind::Ballistic;
    s1.arcing = true;
    s1.inaccurate = true;  // no FlakScatter -> §5.1
    ProjRng rng;
    for (int i = 0; i < 100; ++i) {
        const LaunchResult lr = compute_launch(s1, rules, Vec3i{0, 0, 100}, Vec3i{1280, 0, 0}, rng);
        const double r = std::hypot(lr.scatter_dx, lr.scatter_dy);
        CHECK(r >= 128.0 - 2.0);
        CHECK(r <= 256.0 + 2.0);
    }
    ProjectileSpec s2 = s1;
    s2.flak_scatter = true;  // [FlakTProj] -> §5.2
    s2.weapon_range = 1280;
    double near_max = 0.0, far_max = 0.0;
    for (int i = 0; i < 200; ++i) {
        const LaunchResult a = compute_launch(s2, rules, Vec3i{0, 0, 100}, Vec3i{256, 0, 0}, rng);
        const LaunchResult b = compute_launch(s2, rules, Vec3i{0, 0, 100}, Vec3i{1280, 0, 0}, rng);
        near_max = std::max(near_max, std::hypot(a.scatter_dx, a.scatter_dy));
        far_max = std::max(far_max, std::hypot(b.scatter_dx, b.scatter_dy));
    }
    CHECK(near_max <= 256.0 * 280.0 / 1280.0 + 2.0);  // ~ BS * d/range at 1 cell
    CHECK(far_max > near_max);
    CHECK(far_max <= 256.0 + 2.0);
}

// ---- Task 3: rules resolution ------------------------------------------------
#include "asset_fixture.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"

TEST_CASE("projectile: ini_bool_value follows the game's first-char bool with inline comments") {
    CHECK(ini_bool_value("yes ;can't turn or do much of anything", false));
    CHECK(ini_bool_value("true", false));
    CHECK(ini_bool_value("Yes", false));
    CHECK(ini_bool_value("1", false));
    CHECK_FALSE(ini_bool_value("no", true));
    CHECK_FALSE(ini_bool_value("false ; x", true));
    CHECK(ini_bool_value("", true));        // absent -> fallback
    CHECK(ini_bool_value("  ; only", true));
}

TEST_CASE("projectile: resolve_projectile_spec classifies a synthetic roster") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[120mm]\nDamage=90\nRange=5.75\nProjectile=Cannon\nSpeed=40\n"
        "[Cannon]\nArcing=true\n"
        "[BlimpBomb]\nRange=1.5\nProjectile=BlimpBombP\nSpeed=20\n"
        "[BlimpBombP]\nArm=10\nAcceleration=1\nVertical=yes ;can't turn\nDetonationAltitude=20000 ; nuke\n"
        "[M60]\nProjectile=InvisibleLow\nSpeed=100\n[InvisibleLow]\nInviso=yes\n"
        "[SubTorpedo]\nProjectile=Torpedo\nSpeed=25\n[Torpedo]\nROT=12 ;4\n"
        "[FlakTrackGun]\nRange=5\nProjectile=FlakTProj\nSpeed=50\n"
        "[FlakTProj]\nArcing=true\nInaccurate=yes  ; no snap\nFlakScatter=yes ; scatter\n"
        "[AudioVisual]\nGravity=6               ; gravity\n"
        "[CombatDamage]\nBallisticScatter=1.0 ; cells\n");
    const ra2assets::Rules rules = ra2assets::Rules::load(ini);

    const ProjectileSpec c = resolve_projectile_spec(rules, "120mm");
    CHECK(c.kind == ProjKind::Ballistic);
    CHECK(c.arcing);
    CHECK_FALSE(c.inaccurate);
    CHECK(c.weapon_range == 1472);
    CHECK(c.speed_cap == 102);

    const ProjectileSpec k = resolve_projectile_spec(rules, "BlimpBomb");
    CHECK(k.kind == ProjKind::Vertical);  // inline comment tolerated
    CHECK(k.speed_cap == 51);
    CHECK(k.acceleration == 1);
    CHECK(k.detonation_altitude == 20000);

    CHECK(resolve_projectile_spec(rules, "M60").kind == ProjKind::Instant);        // Inviso
    CHECK(resolve_projectile_spec(rules, "SubTorpedo").kind == ProjKind::Homing);  // W4: homing flies
    CHECK(resolve_projectile_spec(rules, "SubTorpedo").acceleration == 3);          // ctor default
    CHECK(resolve_projectile_spec(rules, "SubTorpedo").rot == 12);
    CHECK(resolve_projectile_spec(rules, "").kind == ProjKind::Instant);
    CHECK(resolve_projectile_spec(rules, "NoSuchWeapon").kind == ProjKind::Instant);

    const ProjectileSpec f = resolve_projectile_spec(rules, "FlakTrackGun");
    CHECK(f.kind == ProjKind::Ballistic);
    CHECK(f.inaccurate);
    CHECK(f.flak_scatter);
    CHECK(f.weapon_range == 1280);

    const ProjectileRules pr = resolve_projectile_rules(rules);
    CHECK(pr.gravity == doctest::Approx(6.0));
    CHECK(pr.ballistic_scatter == 256);
}

TEST_CASE("projectile: real rulesmd pins (Cannon/BlimpBombP/FlakTProj/InvisibleLow, Gravity in [AudioVisual])") {
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("skipped: rulesmd.ini not found"); return; }
    const ra2assets::Rules& rules = ra2fixture::rulesmd();
    const ProjectileRules pr = resolve_projectile_rules(rules);
    CHECK(pr.gravity == doctest::Approx(6.0));   // [AudioVisual] Gravity=6 (§0.1)
    CHECK(pr.ballistic_scatter == 256);          // [CombatDamage] BallisticScatter=1.0

    const ProjectileSpec rhino = resolve_projectile_spec(rules, rules.primary("HTNK"));
    CHECK(rhino.kind == ProjKind::Ballistic);
    CHECK(rhino.arcing);
    CHECK(rhino.weapon_range == 1472);

    const ProjectileSpec kirov = resolve_projectile_spec(rules, rules.primary("ZEP"));
    CHECK(kirov.kind == ProjKind::Vertical);
    CHECK(kirov.speed_cap == 51);
    CHECK(kirov.acceleration == 1);
    CHECK(kirov.detonation_altitude == 20000);

    const ProjectileSpec flak = resolve_projectile_spec(rules, rules.primary("HTK"));
    CHECK(flak.kind == ProjKind::Ballistic);
    CHECK(flak.inaccurate);
    CHECK(flak.flak_scatter);

    CHECK(resolve_projectile_spec(rules, rules.primary("E1")).kind == ProjKind::Instant);
    CHECK(resolve_projectile_spec(rules, rules.primary("E2")).kind == ProjKind::Instant);
}

// ---- Task 4: World integration (the firewalled flight pass) -------------------
#include <vector>

#include "ra2assets/map.hpp"
#include "selection.hpp"
#include "world.hpp"

namespace {

// A Rhino-like attacker WITHOUT the vehicle aim gate (is_vehicle=false so the
// shot is not held for turret slew in a ROT-less test unit): 120mm 90 dmg,
// ROF 65, Range 5.75, flat verses, Cannon (arcing ballistic) projectile.
ra2engine::UnitCombat cannon_stats() {
    ra2engine::UnitCombat c;
    c.strength = 400;
    c.armor = "heavy";
    c.image = "HTNK";
    c.damage = 90;
    c.rof = 65;
    c.range = 5.75;
    c.verses = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    // W5-B R-060: every hit now goes through DamageArea (FUN_00489190); the
    // Rhino's real AP warhead is CellSpread=.3 (76 leptons), which catches a
    // shell landing just short of / past its target.
    c.cell_spread = 0.3;
    c.proj.kind = ProjKind::Ballistic;
    c.proj.arcing = true;
    c.proj.weapon_range = 1472;
    c.proj.speed_cap = 102;
    c.proj.flh[2] = 100;  // H: muzzle 100 leptons up
    return c;
}

ra2engine::UnitCombat soft_target_stats() {
    ra2engine::UnitCombat c;
    c.strength = 1000;
    c.armor = "none";
    c.image = "CONS";
    c.damage = 0;
    c.rof = 1000;
    c.range = 0.0;
    c.verses = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    return c;
}

struct Rig {
    ra2assets::MapFile map;
    ra2engine::World world{map};
};

int count_kind(const std::vector<ProjectileEvent>& evs, ProjectileEvent::Kind k) {
    int n = 0;
    for (const ProjectileEvent& e : evs) n += (e.kind == k) ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("projectile world: firewall CLOSED -> the instant hit lands the same step it fires, no events") {
    Rig r;
    r.world.add_combat_unit(0, 0, 0, cannon_stats());
    r.world.add_combat_unit(5, 0, 1, soft_target_stats());
    r.world.step(1.0 / 15.0);  // auto-acquire + fire (rearm_frames starts 0)
    CHECK(r.world.units()[1].hp == 1000 - 90);
    CHECK(r.world.take_projectile_events().empty());
    CHECK(r.world.projectiles().empty());
}

TEST_CASE("projectile muzzle: lateral art offset and hull turret origin agree at launch") {
    Rig r;
    r.world.enable_projectiles(ProjectileRules{});
    auto gun=cannon_stats();gun.is_vehicle=true;gun.w6.omni_fire=true;
    gun.proj.flh[0]=160;gun.proj.flh[1]=30;gun.proj.turret_offset=50;
    gun.proj.weapon_id="TEST_CANNON";
    r.world.add_combat_unit(2,8,0,gun);
    r.world.add_combat_unit(2,3,1,soft_target_stats());
    r.world.units()[0].body_facing=16384; // hull east
    r.world.units()[0].turret_facing=0;   // gun north
    r.world.step(1.0/15.0);
    const auto events=r.world.take_projectile_events();
    REQUIRE(!events.empty());
    CHECK(events.front().kind==ProjectileEvent::Launch);
    CHECK(events.front().x==2*256-30+50);
    CHECK(events.front().y==8*256-160);
    REQUIRE(!r.world.projectiles().empty());
    CHECK(r.world.projectiles().front().weapon_id=="TEST_CANNON");
}

TEST_CASE("projectile world: ground force-fire launches from FLH and damages only at impact") {
    ra2assets::MapFile map;
    auto& cells=const_cast<std::vector<ra2assets::MapCell>&>(map.cells());
    for(int y=0;y<12;++y)for(int x=0;x<12;++x)cells.push_back({x,y,0,0,0});
    World w(map);w.enable_projectiles(ProjectileRules{});
    auto gun=cannon_stats();gun.proj.weapon_id="GROUND_CANNON";
    w.add_combat_unit(2,2,0,gun);
    w.add_combat_unit(6,2,0,soft_target_stats());
    w.order_attack_cell(0,6,2);
    w.step(1.0/15.0);
    CHECK(w.units()[1].hp==1000);
    REQUIRE(w.projectiles().size()==1);
    CHECK(w.projectiles()[0].weapon_id=="GROUND_CANNON");
    CHECK(w.projectiles()[0].tx==6*256);
    CHECK(w.projectiles()[0].ty==2*256);
    CHECK(w.projectiles()[0].z==100);
    CHECK(count_kind(w.take_projectile_events(),ProjectileEvent::Launch)==1);
    for(int i=0;i<45 && w.units()[1].hp==1000;++i)w.step(1.0/15.0);
    CHECK(w.units()[1].hp<1000);
}

TEST_CASE("projectile world: firewall OPEN -> damage arrives at detonation, ~15 frames later") {
    Rig r;
    r.world.enable_projectiles(ProjectileRules{});
    r.world.add_combat_unit(0, 0, 0, cannon_stats());
    r.world.add_combat_unit(5, 0, 1, soft_target_stats());
    std::vector<ProjectileEvent> all;
    int hit_step = -1;
    for (int step = 1; step <= 40 && hit_step < 0; ++step) {
        r.world.step(1.0 / 15.0);
        for (const ProjectileEvent& e : r.world.take_projectile_events()) all.push_back(e);
        if (r.world.units()[1].hp < 1000) hit_step = step;
    }
    REQUIRE(count_kind(all, ProjectileEvent::Launch) >= 1);
    REQUIRE(count_kind(all, ProjectileEvent::Detonate) >= 1);
    const ProjectileEvent& launch = all.front();
    CHECK(launch.kind == ProjectileEvent::Launch);
    CHECK(launch.speed == arcing_launch_speed(5 * 256, 6.0));  // 96, §0.2
    CHECK(launch.z == 100);                                     // FLH H
    CHECK(hit_step >= 1 + 2);   // never the same frame (>= 2 frames, §4 step 3)
    CHECK(hit_step <= 1 + 18);
    CHECK(r.world.units()[1].hp == 1000 - 90);
    for (const ProjectileEvent& e : all)
        if (e.kind == ProjectileEvent::Detonate) {
            CHECK(e.victim == 1);
            CHECK(e.damage == 90);
            CHECK(e.apex > 100);   // it arced
            CHECK(e.flight_frames >= 2);
        }
}

TEST_CASE("projectile world: target moved away mid-flight -> a miss, no damage") {
    Rig r;
    r.world.enable_projectiles(ProjectileRules{});
    r.world.add_combat_unit(0, 0, 0, cannon_stats());
    r.world.add_combat_unit(5, 0, 1, soft_target_stats());
    r.world.step(1.0 / 15.0);  // fire
    auto evs = r.world.take_projectile_events();
    REQUIRE(count_kind(evs, ProjectileEvent::Launch) == 1);
    // Teleport the target 4 cells sideways (out of the shell's cell and path).
    ra2engine::Unit& t = r.world.units()[1];
    t.cell_y = 4;
    t.fy = 4.0;
    int detonations = 0;
    for (int step = 0; step < 30 && detonations == 0; ++step) {
        r.world.step(1.0 / 15.0);
        for (const ProjectileEvent& e : r.world.take_projectile_events())
            if (e.kind == ProjectileEvent::Detonate) {
                ++detonations;
                CHECK(e.victim == -1);
                CHECK(e.damage == 0);
                CHECK(lepton_to_cell(e.x) == 5);  // landed on the OLD cell (§8)
            }
    }
    CHECK(detonations == 1);
    CHECK(r.world.units()[1].hp == 1000);
}

TEST_CASE("projectile world: target dies mid-flight -> the shell still detonates on its cell") {
    Rig r;
    r.world.enable_projectiles(ProjectileRules{});
    r.world.add_combat_unit(0, 0, 0, cannon_stats());
    r.world.add_combat_unit(5, 0, 1, soft_target_stats());
    r.world.step(1.0 / 15.0);
    REQUIRE(r.world.projectiles().size() == 1u);
    r.world.units()[1].die_time = 0.0;  // killed by something else
    bool detonated = false;
    for (int step = 0; step < 30 && !detonated; ++step) {
        r.world.step(1.0 / 15.0);
        for (const ProjectileEvent& e : r.world.take_projectile_events())
            if (e.kind == ProjectileEvent::Detonate) {
                detonated = true;
                CHECK(e.victim == -1);
                CHECK(e.damage == 0);
            }
    }
    CHECK(detonated);
    CHECK(r.world.projectiles().empty());
}

TEST_CASE("projectile world: Vertical bomb from altitude falls ~35 frames and splashes") {
    Rig r;
    r.world.enable_projectiles(ProjectileRules{});
    ra2engine::UnitCombat k;
    k.strength = 2000;
    k.armor = "heavy";
    k.image = "ZEP";
    k.damage = 250;
    k.rof = 50;
    k.range = 1.5;
    k.cell_spread = 2.0;      // BlimpHE
    k.percent_at_max = 0.5;
    k.verses = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    k.is_aircraft = true;
    k.proj.kind = ProjKind::Vertical;
    k.proj.speed_cap = 51;
    k.proj.acceleration = 1;
    k.proj.detonation_altitude = 20000;
    k.proj.flh[0] = -50;
    k.proj.flh[2] = -140;
    ra2engine::Unit& z = r.world.add_combat_unit(0, 0, 0, k);
    z.altitude = 750.0;   // JumpjetHeight
    r.world.add_combat_unit(1, 0, 1, soft_target_stats());
    r.world.add_combat_unit(2, 0, 1, soft_target_stats());
    std::vector<ProjectileEvent> all;
    for (int step = 0; step < 60; ++step) {
        r.world.step(1.0 / 15.0);
        for (const ProjectileEvent& e : r.world.take_projectile_events()) all.push_back(e);
        if (count_kind(all, ProjectileEvent::Detonate) >= 1) break;
    }
    REQUIRE(count_kind(all, ProjectileEvent::Launch) >= 1);
    REQUIRE(count_kind(all, ProjectileEvent::Detonate) >= 1);
    CHECK(all.front().speed == 1);    // §12.3
    CHECK(all.front().z == 610);      // 750 - 140
    for (const ProjectileEvent& e : all)
        if (e.kind == ProjectileEvent::Detonate) {
            CHECK(e.flight_frames >= 25);
            CHECK(e.flight_frames <= 40);
            CHECK(e.damage > 0);
        }
    CHECK(r.world.units()[1].hp < 1000);  // both in the 2-cell splash
    CHECK(r.world.units()[2].hp < 1000);
}

TEST_CASE("projectile world: building target takes the hit only at detonation") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    Rig r;
    r.world.enable_projectiles(ProjectileRules{});
    r.world.add_combat_unit(0, 0, 0, cannon_stats());
    const int b = r.world.add_building(0, 5, 0, 2, 2, 1, 1000, 0, 0.0, /*instant=*/true);
    tsel_.set(0);
    r.world.group_attack_building(0, tsel_.ids(), b);
    int launch_step = -1, hit_step = -1;
    for (int step = 1; step <= 60 && hit_step < 0; ++step) {
        r.world.step(1.0 / 15.0);
        for (const ProjectileEvent& e : r.world.take_projectile_events()) {
            if (e.kind == ProjectileEvent::Launch && launch_step < 0) launch_step = step;
            if (e.kind == ProjectileEvent::Detonate && e.building == b) hit_step = step;
        }
    }
    REQUIRE(launch_step > 0);
    REQUIRE(hit_step > 0);
    CHECK(hit_step - launch_step >= 2);
    CHECK(r.world.buildings()[b].hp == 1000 - 90);
}

TEST_CASE("projectile world: Yuri Prime captures a building on projectile impact") {
    ra2engine::Selection tsel_;
    Rig r;
    r.world.enable_projectiles(ProjectileRules{});
    ra2engine::UnitCombat prime = cannon_stats();
    prime.damage = 0;
    prime.verses.assign(11, 0);
    ra2engine::Unit& controller = r.world.add_combat_unit(0, 0, 0, prime);
    controller.tg.t.rules_id = "YURIPR";
    const int b = r.world.add_building(0, 5, 0, 2, 2, 1, 1000, 0, 0.0, true);
    tsel_.set(0);
    r.world.group_attack_building(0, tsel_.ids(), b);

    int launch_step = -1;
    for (int step = 1; step <= 60 && r.world.buildings()[b].house != 0; ++step) {
        r.world.step(1.0 / 15.0);
        for (const ProjectileEvent& e : r.world.take_projectile_events())
            if (e.kind == ProjectileEvent::Launch && launch_step < 0) launch_step = step;
    }

    REQUIRE(launch_step > 0);
    CHECK(r.world.buildings()[b].house == 0);
    CHECK(r.world.buildings()[b].hp == 1000);
}

TEST_CASE("projectile world: scattered (Inaccurate+FlakScatter) launches are deterministic") {
    auto run = [] {
        Rig r;
        r.world.enable_projectiles(ProjectileRules{});
        ra2engine::UnitCombat f = cannon_stats();
        f.damage = 25;
        f.rof = 25;
        f.range = 5.0;
        f.cell_spread = 1.0;  // FlakTWH
        f.proj.inaccurate = true;
        f.proj.flak_scatter = true;
        f.proj.weapon_range = 1280;
        r.world.add_combat_unit(0, 0, 0, f);
        r.world.add_combat_unit(4, 0, 1, soft_target_stats());
        std::vector<ProjectileEvent> all;
        for (int step = 0; step < 120; ++step) {
            r.world.step(1.0 / 15.0);
            for (const ProjectileEvent& e : r.world.take_projectile_events()) all.push_back(e);
        }
        return all;
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    int scattered = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].kind == b[i].kind);
        CHECK(a[i].x == b[i].x);
        CHECK(a[i].y == b[i].y);
        CHECK(a[i].scatter_dx == b[i].scatter_dx);
        CHECK(a[i].damage == b[i].damage);
        if (a[i].kind == ProjectileEvent::Launch && (a[i].scatter_dx || a[i].scatter_dy)) ++scattered;
    }
    CHECK(scattered >= 3);
}

// ---- W4: homing (ROT>0) flight -------------------------------------------------
namespace {
Projectile homing_shot(int rot, int cap, int accel, int z0) {
    Projectile p;
    p.kind = ProjKind::Homing;
    p.z = z0;
    p.vx = 1.0;
    p.speed = 1.0;
    p.rot = rot;
    p.speed_cap = cap;
    p.acceleration = accel;
    p.locked = true;
    p.prev_dist = -1;
    return p;
}
}  // namespace

TEST_CASE("projectile homing: a Patriot RedEye2 (ROT 80, Speed 100, Acc 3) reaches a rocket 7 cells away") {
    Projectile p = homing_shot(80, weapon_speed_leptons(100), 3, 64);
    p.target_air = true;
    const HomingRules hr;
    const Vec3i tgt{7 * 256, 3 * 256, 900};
    int r = 0, f = 0;
    double max_speed = 0.0;
    for (f = 1; f <= 200 && r == 0; ++f) {
        r = integrate_homing(p, tgt, f, hr);
        max_speed = std::max(max_speed, p.speed);
    }
    CHECK(r == 1);                 // snapped onto the target
    CHECK(f > 20);                 // it FLIES: 1 lept/frame + 3/frame, ~40 frames
    CHECK(f < 80);
    CHECK(p.x == tgt.x);
    CHECK(p.z == tgt.z);
    CHECK(max_speed > 50.0);
}

TEST_CASE("projectile homing: a Level torpedo never leaves the water line") {
    Projectile p = homing_shot(12, weapon_speed_leptons(25), 3, 0);
    p.level = true;
    const HomingRules hr;
    const Vec3i tgt{-5 * 256, 4 * 256, 0};   // behind and to the side: it has to turn
    int r = 0, f = 0;
    for (f = 1; f <= 400 && r == 0; ++f) {
        r = integrate_homing(p, tgt, f, hr);
        REQUIRE(p.z == 0);
    }
    CHECK(r != 0);
    CHECK(std::abs(p.x - tgt.x) < 256);
    CHECK(std::abs(p.y - tgt.y) < 256);
}

TEST_CASE("projectile homing: an air target lost mid-flight -> climb, self-destruct at MissileSafetyAltitude") {
    Projectile p = homing_shot(80, 255, 3, 64);
    p.target_air = true;
    p.lost = true;
    const HomingRules hr;   // 750
    int r = 0, f = 0;
    for (f = 1; f <= 300 && r == 0; ++f) r = integrate_homing(p, Vec3i{5000, 0, 0}, f, hr);
    CHECK(r == 2);
    CHECK(p.z >= 750);
}

TEST_CASE("projectile homing: the MissileROTVar wobble keeps the turn between ROT and 1.5 ROT") {
    // A target straight behind: the yaw turns by the limit each frame.
    for (int frame = 0; frame < 15; ++frame) {
        Projectile p = homing_shot(20, 255, 0, 500);
        p.locked = false;
        p.speed = 10.0;
        p.vx = 10.0;
        integrate_homing(p, Vec3i{-10000, 1, 500}, frame, HomingRules{});
        const double turned = std::fabs(std::atan2(p.vy, p.vx));
        const double u = 2.0 * 3.14159265358979323846 / 65536.0 * 256.0;   // 1/256 circle
        // Net L0: yaw goes through the original's atan2 table (step ~0.024 rad).
        CHECK(turned >= 20 * u - 0.03);
        CHECK(turned <= 30 * u + 0.03);
    }
}

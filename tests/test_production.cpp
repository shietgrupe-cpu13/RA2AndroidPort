#include "doctest.h"
#include "../engine/production.hpp"
#include "../engine/unit_type.hpp"
using namespace ra2engine;

static std::vector<BuildingTypeDef> roster() {
    std::vector<BuildingTypeDef> t(3);
    t[0].id = "GACNST"; t[0].conyard = true; t[0].cost = 3000; t[0].tech_level = -1;
    t[1].id = "GAPOWR"; t[1].cost = 800; t[1].power = 200; t[1].tech_level = 1;
    t[1].prerequisite = {"GACNST"};
    t[2].id = "GAPILE"; t[2].cost = 500; t[2].power = -10; t[2].tech_level = 2;
    t[2].prerequisite = {"POWER", "GACNST"};
    return t;
}
static Building live(int type, int house = 0) {
    Building b; b.type = type; b.house = house; b.alive = true;
    b.state = Building::State::Idle; b.hp = b.max_hp = 100; return b;
}
static const std::vector<std::pair<std::string, std::vector<std::string>>> kGroups =
    {{"POWER", {"GAPOWR", "NAPOWR"}}};

TEST_CASE("build_frames: BuildSpeed formula [RE: construction.md §7]") {
    // Pins recovered from the binary (FUN_00711280): ftol(Cost*BuildSpeed*0.9).
    // Operation-order sensitive: the algebraically equal /1000*900 rounds to
    // 503/1889 under IEEE 754 instead of the binary's 504/1890.
    CHECK(build_frames(1000, 0.7) == 630);   // 0.7 min * 900
    CHECK(build_frames(800, 0.7) == 504);    // GAPOWR pin (Task-1 hunt)
    CHECK(build_frames(500, 0.7) == 315);
    CHECK(build_frames(2000, 0.7) == 1260);
    CHECK(build_frames(3000, 0.7) == 1890);  // GACNST pin (Task-1 hunt)
}

TEST_CASE("power_totals + build_speed_multiplier [RE-GATE: construction.md §8]") {
    auto t = roster();
    std::vector<Building> bs = {live(1), live(2)};       // +200, -10
    auto p = power_totals(bs, t, 0);
    CHECK(p.output == 200); CHECK(p.drain == 10);
    CHECK(build_speed_multiplier(p, 1.0) == doctest::Approx(1.0));
    bs[0].state = Building::State::Buildup;              // powerplant not up yet
    p = power_totals(bs, t, 0);
    CHECK(p.output == 0); CHECK(p.drain == 10);
    // f = 0/10 = 0 -> speed 1-1*(1-0) = 0 -> max(.5) -> min(.8) = 0.5 exactly
    CHECK(build_speed_multiplier(p, 1.0) == doctest::Approx(kMinLowPowerSpeed));
    // partial shortage: out 90 / drain 100 -> 1-1*(1-.9) = .9 -> capped at .8
    PowerTotals part; part.output = 90; part.drain = 100;
    CHECK(build_speed_multiplier(part, 1.0) == doctest::Approx(kMaxLowPowerSpeed));
    // heavy modifier: out 90/drain 100, mod 4 -> 1-4*.1 = .6 (within [.5,.8])
    CHECK(build_speed_multiplier(part, 4.0) == doctest::Approx(0.6));
    kill_building(bs[1]);
    p = power_totals(bs, t, 0);
    CHECK(p.drain == 0);                                 // dead building drains nothing
    // enemy buildings don't count
    std::vector<Building> enemy = {live(1, 1)};
    CHECK(power_totals(enemy, t, 0).output == 0);
}

TEST_CASE("power_totals: output scales with health, drain does not [RE: FUN_0044E7B0]") {
    auto t = roster();
    std::vector<Building> bs = {live(1), live(2)};       // +200 (hp 100/100), -10
    bs[0].hp = 50;
    CHECK(power_totals(bs, t, 0).output == 100);         // ftol(50/100*200)
    bs[0].hp = 33;
    CHECK(power_totals(bs, t, 0).output == 66);          // ftol(66.0) -- truncation
    bs[0].hp = 1;
    CHECK(power_totals(bs, t, 0).output == 2);
    bs[1].hp = 1;
    CHECK(power_totals(bs, t, 0).drain == 10);           // FUN_0044E880: no scaling
}

TEST_CASE("prerequisites_met: plain ids, groups, Buildup counts (placement, W5-B R-065)") {
    auto t = roster();
    std::vector<Building> bs = {live(0)};                // ConYard only
    CHECK(prerequisites_met({"GACNST"}, bs, t, 0, kGroups));
    CHECK_FALSE(prerequisites_met({"POWER", "GACNST"}, bs, t, 0, kGroups));
    bs.push_back(live(1));                               // + power plant
    CHECK(prerequisites_met({"POWER", "GACNST"}, bs, t, 0, kGroups));
    // W5-B R-065: counted from placement (Unlimbo -> RegisterGain), so a
    // power plant still in build-up already satisfies POWER; a dead one not.
    bs[1].state = Building::State::Buildup;              // not finished
    CHECK(prerequisites_met({"POWER"}, bs, t, 0, kGroups));
    bs[1].state = Building::State::Dead;
    CHECK_FALSE(prerequisites_met({"POWER"}, bs, t, 0, kGroups));
    CHECK_FALSE(prerequisites_met({"PROC"}, bs, t, 0, kGroups));   // unknown group
}

TEST_CASE("can_start: slot, conyard, tech level") {
    auto t = roster();
    std::vector<Building> bs = {live(0)};
    ProductionState ps; ps.credits = 10000;
    CHECK(can_start(ps, 1, t, bs, 0, kGroups, true));
    CHECK_FALSE(can_start(ps, 1, t, bs, 0, kGroups, false));   // no ConYard
    CHECK_FALSE(can_start(ps, 0, t, bs, 0, kGroups, true));    // TechLevel -1
    CHECK_FALSE(can_start(ps, 2, t, bs, 0, kGroups, true));    // POWER missing
    start(ps, 1, t, 0.7);
    CHECK_FALSE(can_start(ps, 1, t, bs, 0, kGroups, true));    // slot busy
}

TEST_CASE("tick: 54-step economy [RE-GATE: construction.md §10]") {
    auto t = roster();
    ProductionState ps; ps.credits = 10000;
    start(ps, 1, t, 0.7);                    // GAPOWR: 504 frames, 800 credits
    CHECK(ps.slot.total_frames == 504);
    CHECK(ps.slot.step_ticks == 9);          // trunc(504/54)
    // 27 stages = half the steps = 27*9 = 243 frames = 16.2 s
    tick(ps, 243.0 / 15.0, 1.0, t);
    CHECK(ps.slot.stage == 27);
    CHECK(ps.credits == doctest::Approx(10000 - 400.0));   // equal cost/54 chunks
    CHECK_FALSE(ps.slot.ready);
    tick(ps, 504.0, 1.0, t);                 // way past the end
    CHECK(ps.slot.ready);
    CHECK(ps.slot.stage == kProductionSteps);
    CHECK(ps.credits == doctest::Approx(9200.0));           // exactly cost deducted
    placed(ps);
    CHECK(ps.slot.type == -1);

    // stall: 100 credits fund 6 chunks of 800/54 (= 88.9); the 7th (14.8) is
    // unaffordable -> stalls with ~11.1 left, no progress past stage 6
    ps.credits = 100;
    start(ps, 1, t, 0.7);
    tick(ps, 1000.0, 1.0, t);
    CHECK(ps.slot.stage == 6);
    CHECK(ps.credits == doctest::Approx(100.0 - 800.0 * 6 / 54).epsilon(0.001));
    CHECK_FALSE(ps.slot.ready);
    ps.credits += 5000;                       // funds arrive -> resumes
    tick(ps, 1000.0, 1.0, t);
    CHECK(ps.slot.ready);
    CHECK(ps.credits == doctest::Approx(100.0 + 5000.0 - 800.0));  // total spend == cost
    placed(ps);

    // stall never produces a catch-up burst: broke for a long time, then
    // funded -> at most ONE stage per step_ticks afterwards
    ProductionState nb; nb.credits = 0;
    start(nb, 1, t, 0.7);
    tick(nb, 100.0, 1.0, t);                  // long broke stretch
    CHECK(nb.slot.stage == 0);
    nb.credits = 10000;
    tick(nb, 9.0 / 15.0, 1.0, t);             // exactly one step_ticks worth
    CHECK(nb.slot.stage == 1);                // NOT a burst to stage 11

    // cancel refunds spent
    ProductionState c; c.credits = 1000;
    start(c, 2, t, 0.7);                      // GAPILE: 315 frames, step_ticks 5
    CHECK(c.slot.step_ticks == 5);            // trunc(315/54)
    tick(c, 90.0 / 15.0, 1.0, t);             // 90 frames = 18 stages
    CHECK(c.slot.stage == 18);
    CHECK(c.credits < 1000.0);
    cancel(c);
    CHECK(c.credits == doctest::Approx(1000.0));
    CHECK(c.slot.type == -1);

    // half-speed multiplier halves stage cadence
    ProductionState s; s.credits = 10000;
    start(s, 1, t, 0.7);                      // step_ticks 9
    tick(s, 1.0, 0.5, t);                     // 7.5 frames < 9 -> no stage yet
    CHECK(s.slot.stage == 0);
    tick(s, 1.0, 0.5, t);                     // 15 frames -> one stage
    CHECK(s.slot.stage == 1);
}

TEST_CASE("tick: FreeUnit charge split -- charge basis diverges from raw cost "
         "[RE-GATE: economy.md §6a]") {
    // GAREFN/NAREFN-shaped type: cost 2000 (drives build TIME), charge 600
    // (cost 2000 minus the FreeUnit HARV/CMIN's 1400 -- the credits actually
    // deducted). build_speed 0.7 matches every other pin in this file.
    std::vector<BuildingTypeDef> t(1);
    t[0].id = "GAREFN"; t[0].cost = 2000; t[0].charge = 600; t[0].tech_level = 1;

    ProductionState ps; ps.credits = 10000;
    start(ps, 0, t, 0.7);
    CHECK(ps.slot.total_frames == 1260);   // ftol(2000*0.7*0.9) -- raw cost, unaffected by charge
    CHECK(ps.slot.step_ticks == 23);       // trunc(1260/54)
    CHECK(ps.slot.charge_basis == 600);

    tick(ps, 10000.0, 1.0, t);             // way past the end
    CHECK(ps.slot.ready);
    CHECK(ps.slot.stage == kProductionSteps);
    CHECK(ps.credits == doctest::Approx(10000.0 - 600.0));   // exactly charge, not cost, deducted
    CHECK(ps.slot.spent == doctest::Approx(600.0));

    // Existing charge-unset roster types (GAPOWR etc.) are UNCHANGED: charge
    // defaults to 0 -> start() falls back to cost, so every pre-existing pin
    // in this file (504/9 frames, 800 total spend) keeps passing unmodified.
    auto t2 = roster();
    ProductionState ps2; ps2.credits = 10000;
    start(ps2, 1, t2, 0.7);                // GAPOWR: cost 800, charge unset
    CHECK(ps2.slot.charge_basis == 800);
    tick(ps2, 504.0, 1.0, t2);
    CHECK(ps2.slot.ready);
    CHECK(ps2.credits == doctest::Approx(10000.0 - 800.0));
}

// --- Task 3: multi-queue production (scalar start, UnitTypeDef, ProductionQueues) ---

static std::vector<UnitTypeDef> unit_roster() {
    std::vector<UnitTypeDef> u(2);
    u[0].id = "E1";   u[0].cost = 200; u[0].tech_level = 1; u[0].is_vehicle = false;
    u[0].prerequisite = {"BARRACKS"};
    u[1].id = "MTNK"; u[1].cost = 2000; u[1].tech_level = 2; u[1].is_vehicle = true;
    u[1].prerequisite = {"FACTORY"};
    return u;
}
static const std::vector<std::pair<std::string, std::vector<std::string>>> kUGroups = {
    {"BARRACKS", {"GAPILE"}}, {"FACTORY", {"GAWEAP"}}, {"POWER", {"GAPOWR", "NAPOWR"}}};
static Building livet(int type, int house = 0) {  // like live() but arbitrary building type roster
    Building b; b.type = type; b.house = house; b.alive = true;
    b.state = Building::State::Idle; b.hp = b.max_hp = 100; return b;
}

TEST_CASE("production: scalar start matches the BuildingType wrapper bit-for-bit") {
    auto t = roster();  // GACNST/GAPOWR/GAPILE, no FreeUnit -> charge == cost
    ProductionState a, b;
    a.credits = b.credits = 10000;
    start(a, 1, t, 0.7);                                   // GAPOWR via wrapper
    start(b, t[1].cost, t[1].cost, 0.7);                   // scalar, charge==cost
    CHECK(a.slot.total_frames == b.slot.total_frames);
    CHECK(a.slot.step_ticks == b.slot.step_ticks);
    CHECK(a.slot.charge_basis == b.slot.charge_basis);
    // A FreeUnit type (charge < cost) still routes through the scalar correctly:
    std::vector<BuildingTypeDef> refn(1);
    refn[0].id = "GAREFN"; refn[0].cost = 2000; refn[0].charge = 600; refn[0].tech_level = 1;
    ProductionState c, d;
    c.credits = d.credits = 10000;
    start(c, 0, refn, 0.7);
    start(d, refn[0].cost, refn[0].charge, 0.7);
    CHECK(c.slot.total_frames == 1260);                    // raw cost 2000
    CHECK(c.slot.charge_basis == 600);                     // FreeUnit-subtracted
    CHECK(c.slot.total_frames == d.slot.total_frames);
    CHECK(c.slot.charge_basis == d.slot.charge_basis);
}

TEST_CASE("production: can_start_unit gates on TechLevel + building prereqs") {
    auto u = unit_roster();
    std::vector<BuildingTypeDef> bt(1); bt[0].id = "GAPILE";  // BARRACKS group member
    ProductionState ps; ps.credits = 5000;
    std::vector<Building> none;
    CHECK_FALSE(can_start_unit(ps, 0, u, none, bt, 0, kUGroups, /*factory_alive=*/true)); // no barracks
    std::vector<Building> barracks = {livet(0)};  // GAPILE idle
    CHECK(can_start_unit(ps, 0, u, barracks, bt, 0, kUGroups, true));
    CHECK_FALSE(can_start_unit(ps, 0, u, barracks, bt, 0, kUGroups, /*factory_alive=*/false));
    // GI prereq met but MTNK needs FACTORY (not present) -> blocked:
    CHECK_FALSE(can_start_unit(ps, 1, u, barracks, bt, 0, kUGroups, true));
}

TEST_CASE("production: infantry queue reproduces the 54-step economy for a unit") {
    auto u = unit_roster();
    ProductionQueues q;
    q.set_credits(10000);
    q.start_unit(QueueCat::Infantry, 0, u, 0.7);           // E1 cost 200
    const ProductionState& inf = q.state(QueueCat::Infantry);
    CHECK(inf.slot.total_frames == build_frames(200, 0.7));  // = 126
    CHECK(inf.slot.step_ticks == std::max(1, 126 / 54));     // clamp(2,1,255)=2
    const double speedup[4] = {1.0, 1.0, 1.0, 1.0};
    std::vector<BuildingTypeDef> bt;
    // Drive to completion; charge_basis == cost (no FreeUnit).
    for (int i = 0; i < 10000 && !q.ready(QueueCat::Infantry); ++i)
        q.tick(1.0 / 15.0, 1.0, speedup, bt);
    CHECK(q.ready(QueueCat::Infantry));
    CHECK(inf.slot.spent == doctest::Approx(200.0));
}

TEST_CASE("production: FOUR queues advance concurrently and independently [RE-GATE §1.1]") {
    // [RE-GATE APPLIED: production.md §1.1] Structures and Defenses are
    // SEPARATE concurrent queues — a structure and a defense build at once
    // from the same ConYard, alongside the two unit queues. (The queue math is
    // category-blind, so roster() entries stand in for both building queues.)
    auto t = roster();
    auto u = unit_roster();
    ProductionQueues q;
    q.set_credits(100000);
    start(q.structure(), 1, t, 0.7);                       // GAPOWR in the Structure queue
    start(q.state(QueueCat::Defense), 2, t, 0.7);          // Defense queue (GAPILE as stand-in)
    q.start_unit(QueueCat::Infantry, 0, u, 0.7);           // E1
    q.start_unit(QueueCat::Vehicle, 1, u, 0.7);            // MTNK
    const double speedup[4] = {1.0, 1.0, 1.0, 1.0};
    for (int i = 0; i < 5; ++i) q.tick(1.0 / 15.0, 1.0, speedup, t);
    // All four queues are producing at once, each tracking its own slot:
    CHECK(q.producing(QueueCat::Structure) == 1);
    CHECK(q.producing(QueueCat::Defense) == 2);
    CHECK(q.producing(QueueCat::Infantry) == 0);
    CHECK(q.producing(QueueCat::Vehicle) == 1);
    // Infantry (cheap, fast) reaches ready long before the vehicle:
    for (int i = 0; i < 10000 && !q.ready(QueueCat::Infantry); ++i)
        q.tick(1.0 / 15.0, 1.0, speedup, t);
    CHECK(q.ready(QueueCat::Infantry));
    CHECK_FALSE(q.ready(QueueCat::Vehicle));
}

TEST_CASE("production: naval and vehicle jobs retain separate progress") {
    auto u=unit_roster();
    u[1].is_vehicle=true;
    u.push_back(u[1]);u.back().id="DRED";u.back().cost=4000;u.back().naval=true;
    ProductionQueues q;q.set_credits(10000);
    q.start_unit(QueueCat::Vehicle,1,u,0.7);
    q.start_unit(QueueCat::Naval,2,u,0.7);
    const double speed[kQueueCats]={1,1,1,1,1,1};
    std::vector<BuildingTypeDef> buildings;
    for(int i=0;i<60;++i)q.tick_all(1.0/15.0,1.0,speed,buildings);
    CHECK(q.producing(QueueCat::Vehicle)==1);
    CHECK(q.producing(QueueCat::Naval)==2);
    CHECK(q.state(QueueCat::Vehicle).slot.stage>0);
    CHECK(q.state(QueueCat::Naval).slot.stage>0);
    CHECK(q.state(QueueCat::Vehicle).slot.total_frames!=q.state(QueueCat::Naval).slot.total_frames);
}

TEST_CASE("production: cancelling TWO queues in one frame refunds BOTH (no lost credits)") {
    // Regression: a lost ConYard cancels BOTH building queues in one frame, so
    // multiple cancels must each land in the shared pool. The prior
    // reconcile-max kept only the largest refund; cancel() now broadcasts each
    // refund so they accumulate. Uses the Infantry+Vehicle queues (same math).
    auto u = unit_roster();
    ProductionQueues q;
    const double kStart = 10000.0;
    q.set_credits(kStart);
    q.start_unit(QueueCat::Infantry, 0, u, 0.7);   // E1  (cost 200)
    q.start_unit(QueueCat::Vehicle, 1, u, 0.7);    // MTNK (cost 2000)
    std::vector<BuildingTypeDef> bt;
    const double speedup[4] = {1.0, 1.0, 1.0, 1.0};
    // Advance until BOTH queues have actually spent (unequal amounts, so a
    // max()-only reconcile would visibly drop the smaller refund).
    for (int i = 0; i < 60; ++i) q.tick(1.0 / 15.0, 1.0, speedup, bt);
    const double spent_inf = q.state(QueueCat::Infantry).slot.spent;
    const double spent_veh = q.state(QueueCat::Vehicle).slot.spent;
    REQUIRE(spent_inf > 0.0);
    REQUIRE(spent_veh > 0.0);
    REQUIRE(spent_inf != spent_veh);
    CHECK(q.credits() == doctest::Approx(kStart - spent_inf - spent_veh));

    // Cancel BOTH in the same frame, then reconcile: the shared pool must
    // return to EXACTLY the pre-build total — both refunds landed.
    q.cancel(QueueCat::Infantry);
    q.cancel(QueueCat::Vehicle);
    q.reconcile_refund();
    CHECK(q.credits() == doctest::Approx(kStart));
    CHECK(q.producing(QueueCat::Infantry) == -1);   // both slots cleared
    CHECK(q.producing(QueueCat::Vehicle) == -1);
}

TEST_CASE("production: ConYard-death cancels Structure+Defense, BOTH refunds land [final-review FIX 1]") {
    // Regression for the ConYard-death branch in prod_tick: a lost ConYard
    // cancels BOTH the Structure and Defense queues in one frame. The OLD code
    // used raw ra2engine::cancel(q.structure()) (refunds q_[0].credits only, no
    // broadcast) then q.cancel(Defense) (broadcasts q_[1]'s balance to ALL four
    // queues) — the Defense broadcast OVERWROTE the Structure refund and
    // reconcile_refund's max() could not recover it (all four already equal), so
    // the Structure spend was permanently lost. The fix cancels BOTH through the
    // broadcasting ProductionQueues::cancel, Structure FIRST, so the refunds
    // accumulate. Mirror prod_tick's exact order (Structure then Defense) with
    // UNEQUAL spent amounts so a dropped refund is visible.
    auto t = roster();
    ProductionQueues q;
    const double kStart = 20000.0;
    q.set_credits(kStart);
    start(q.structure(), 1, t, 0.7);                 // GAPOWR (cost 800) in Structure queue
    start(q.state(QueueCat::Defense), 2, t, 0.7);    // GAPILE (cost 500) in Defense queue
    const double speedup[4] = {1.0, 1.0, 1.0, 1.0};
    // Advance until BOTH building queues have spent DIFFERENT amounts.
    for (int i = 0; i < 40; ++i) q.tick(1.0 / 15.0, 1.0, speedup, t);
    const double spent_str = q.state(QueueCat::Structure).slot.spent;
    const double spent_def = q.state(QueueCat::Defense).slot.spent;
    REQUIRE(spent_str > 0.0);
    REQUIRE(spent_def > 0.0);
    REQUIRE(spent_str != spent_def);
    CHECK(q.credits() == doctest::Approx(kStart - spent_str - spent_def));

    // Cancel BOTH in prod_tick's order (Structure first, then Defense). With the
    // fix each refund broadcasts into the shared pool and accumulates.
    q.cancel(QueueCat::Structure);
    q.cancel(QueueCat::Defense);
    q.reconcile_refund();
    CHECK(q.credits() == doctest::Approx(kStart));   // BOTH refunds landed
    CHECK(q.producing(QueueCat::Structure) == -1);
    CHECK(q.producing(QueueCat::Defense) == -1);
}

TEST_CASE("production: MultipleFactory speedup scales the vehicle queue [RE-GATE §1.2]") {
    auto u = unit_roster();
    // [RE-GATE APPLIED: production.md §1.2 — CONFIRMED: MultipleFactory = 0.8,
    // compounding x0.8 per EXTRA same-class factory (2 -> x0.8, 3 -> x0.64),
    // applied as a build-TIME multiplier in Time_To_Build FUN_006F3D00] two war
    // factories multiply the effective build time by 0.8 once (n-1 = 1) -> a
    // build with 2 factories completes in FEWER frames than with 1.
    ProductionQueues one, two;
    one.set_credits(1e9); two.set_credits(1e9);
    one.start_unit(QueueCat::Vehicle, 1, u, 0.7);
    two.start_unit(QueueCat::Vehicle, 1, u, 0.7);
    std::vector<BuildingTypeDef> bt;
    const double s1[4] = {1.0, 1.0, 1.0, 1.0};
    const double s2[4] = {1.0, 1.0, 1.0, 1.0 / 0.8};   // 2 factories: divide time by 0.8
    int f1 = 0, f2 = 0;
    while (!one.ready(QueueCat::Vehicle)) { one.tick(1.0/15.0, 1.0, s1, bt); ++f1; }
    while (!two.ready(QueueCat::Vehicle)) { two.tick(1.0/15.0, 1.0, s2, bt); ++f2; }
    CHECK(f2 < f1);
}

TEST_CASE("production: Structure (BuildingType) queue path is unchanged by the multi-queue layer") {
    // The Structure queue reached via ProductionQueues behaves EXACTLY like a
    // standalone ProductionState driven by start()+tick().
    auto t = roster();
    ProductionQueues q; q.set_credits(10000);
    start(q.structure(), 1, t, 0.7);                // GAPOWR through the shared start()
    ProductionState solo; solo.credits = 10000;
    start(solo, 1, t, 0.7);
    CHECK(q.structure().slot.total_frames == solo.slot.total_frames);
    CHECK(q.structure().slot.step_ticks == solo.slot.step_ticks);
    const double speedup[4] = {1.0, 1.0, 1.0, 1.0};
    for (int i = 0; i < 10; ++i) {
        q.tick(1.0/15.0, 1.0, speedup, t);
        tick(solo, 1.0/15.0, 1.0, t);
    }
    CHECK(q.structure().slot.stage == solo.slot.stage);
    CHECK(q.structure().slot.spent == doctest::Approx(solo.slot.spent));
}

TEST_CASE("prerequisites_met: unit multi-token prereq incl. a TECH group token "
          "[RE: techtree.md §1]") {
    // The SREF shape: Prerequisite=GAWEAP,TECH. GAWEAP is a plain factory id;
    // TECH is a group whose Allied member is GATECH (production.md §1.6).
    std::vector<BuildingTypeDef> bt(3);
    bt[0].id = "GAWEAP"; bt[1].id = "GATECH"; bt[2].id = "GAPILE";
    const std::vector<std::pair<std::string, std::vector<std::string>>> groups = {
        {"TECH", {"GATECH", "NATECH", "YATECH"}}, {"FACTORY", {"GAWEAP", "NAWEAP"}}};
    const std::vector<std::string> sref = {"GAWEAP", "TECH"};

    std::vector<Building> only_weap = {livet(0)};              // GAWEAP idle
    CHECK_FALSE(prerequisites_met(sref, only_weap, bt, 0, groups));   // TECH unmet
    std::vector<Building> weap_tech = {livet(0), livet(1)};    // + GATECH idle
    CHECK(prerequisites_met(sref, weap_tech, bt, 0, groups));         // TECH satisfied
    weap_tech[1].state = Building::State::Buildup;             // GATECH not finished
    CHECK(prerequisites_met(sref, weap_tech, bt, 0, groups));   // W5-B R-065: Buildup counts
}

TEST_CASE("can_start_unit: TECH-gated advanced unit + plain-token unit unchanged "
          "[RE: techtree.md §1/§5]") {
    // Roster: [0]=E1 (plain GAPILE, the byte-identity anchor), [1]=SREF (GAWEAP,TECH).
    std::vector<UnitTypeDef> u(2);
    u[0].id = "E1";   u[0].cost = 200;  u[0].tech_level = 1; u[0].prerequisite = {"GAPILE"};
    u[1].id = "SREF"; u[1].cost = 1200; u[1].tech_level = 5; u[1].prerequisite = {"GAWEAP", "TECH"};
    std::vector<BuildingTypeDef> bt(3);
    bt[0].id = "GAPILE"; bt[1].id = "GAWEAP"; bt[2].id = "GATECH";
    const std::vector<std::pair<std::string, std::vector<std::string>>> groups = {
        {"TECH", {"GATECH"}}};
    ProductionState ps; ps.credits = 5000;

    // E1: plain GAPILE resolves exactly as before (the anchor).
    std::vector<Building> pile = {livet(0)};
    CHECK(can_start_unit(ps, 0, u, pile, bt, 0, groups, /*factory_alive=*/true));
    // SREF: only GAWEAP owned -> UNBUILDABLE (TECH unmet).
    std::vector<Building> weap = {livet(1)};
    CHECK_FALSE(can_start_unit(ps, 1, u, weap, bt, 0, groups, true));
    // SREF: GAWEAP + GATECH -> BUILDABLE.
    std::vector<Building> weap_tech = {livet(1), livet(2)};
    CHECK(can_start_unit(ps, 1, u, weap_tech, bt, 0, groups, true));
    // Factory-alive + tech-level gates still apply.
    CHECK_FALSE(can_start_unit(ps, 1, u, weap_tech, bt, 0, groups, /*factory_alive=*/false));
    u[1].tech_level = 0;                                        // TechLevel<=0 = never buildable
    CHECK_FALSE(can_start_unit(ps, 1, u, weap_tech, bt, 0, groups, true));
}

TEST_CASE("prerequisites_met: killing the TECH building flips an advanced unit back "
          "to unbuildable [RE: techtree.md §4]") {
    std::vector<BuildingTypeDef> bt(2);
    bt[0].id = "GAWEAP"; bt[1].id = "GATECH";
    const std::vector<std::pair<std::string, std::vector<std::string>>> groups = {
        {"TECH", {"GATECH", "NATECH", "YATECH"}}};
    const std::vector<std::string> sref = {"GAWEAP", "TECH"};
    std::vector<Building> bs = {livet(0), livet(1)};          // GAWEAP + GATECH idle
    CHECK(prerequisites_met(sref, bs, bt, 0, groups));        // buildable
    kill_building(bs[1]);                                     // Battle Lab destroyed
    CHECK_FALSE(prerequisites_met(sref, bs, bt, 0, groups));  // TECH unmet again
}

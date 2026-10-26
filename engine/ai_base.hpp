#pragma once
// Pure base-planning AI brain [RE: docs/re/ai-base.md]. No SDL, no World —
// consumes a read-only BaseView the caller (World) fills, plus a per-house
// AiBaseState. doctest-testable. EVERY numeric constant below is a HUNT
// OUTPUT recorded in docs/re/ai-base.md; none is a guess — tests assert
// against the named constants so a RE-GATE amendment updates constant, doc,
// and test together.
#include <cstdint>
#include <functional>
#include <vector>
#include "building.hpp"  // BuildingTypeDef
#include "pathfind.hpp"  // Cellxy

namespace ra2engine {

// --- Recovered [AI] constants [RE-GATE APPLIED: ai-base.md §2 — recovered
// values from the [AI] loader FUN_00672520 + the real rulesmd; every value
// below is a hunt output, cited by §] ---
constexpr double kAiPowerSurplus = 50.0;      // [AI] PowerSurplus= — build power until surplus >= this (the RESERVE/top-up) [RE: ai-base.md §2]
constexpr double kAiPowerEmergency = 0.75;    // [AI] PowerEmergency= (the key is PowerEmergency, NOT PowerEmergencyFraction) — SELL trigger below 75% power; recorded for later sub-phases, AI-1 does not sell. DISTINCT from PowerSurplus and from the drainer-preemption gate [RE: ai-base.md §2]
constexpr int kAiBaseSizeAdd = 3;             // [AI] BaseSizeAdd= — AI base <= largest RIVAL base + this [RE: ai-base.md §2]
constexpr int kAiBaseSpacing = 1;             // [AI] AIBaseSpacing= — cell gap between AI-placed buildings [RE: ai-base.md §2/§3]
constexpr double kAiRefineryRatio = 0.16;     // [AI] RefineryRatio= [RE: ai-base.md §2]
constexpr int kAiRefineryLimit = 4;           // [AI] RefineryLimit= [RE: ai-base.md §2]
constexpr double kAiBarracksRatio = 0.16;     // [AI] BarracksRatio= [RE: ai-base.md §2]
constexpr int kAiBarracksLimit = 2;           // [AI] BarracksLimit= [RE: ai-base.md §2]
constexpr double kAiWarRatio = 0.1;           // [AI] WarRatio= [RE: ai-base.md §2]
constexpr int kAiWarLimit = 2;                // [AI] WarLimit= [RE: ai-base.md §2]
constexpr double kAiDefenseRatio = 0.4;       // [AI] DefenseRatio= [RE: ai-base.md §2]
constexpr int kAiDefenseLimit = 40;           // [AI] DefenseLimit= [RE: ai-base.md §2]
constexpr double kAiAARatio = 0.14;           // [AI] AARatio= [RE: ai-base.md §2]
constexpr int kAiAALimit = 10;                // [AI] AALimit= [RE: ai-base.md §2]
constexpr double kAiTeslaRatio = 0.16;        // [AI] TeslaRatio= [RE: ai-base.md §2]
constexpr int kAiTeslaLimit = 10;             // [AI] TeslaLimit= [RE: ai-base.md §2]
constexpr int kAiInfantryReserve = 50000;     // [AI] InfantryReserve= (recorded; consumed by later AI sub-phases) [RE: ai-base.md §2]
constexpr double kAiCreditReserve = 100.0;    // [AI] CreditReserve= — banked floor [RE: ai-base.md §2]
constexpr int kAiComputerBaseDefenseResponse = 3;  // [AI] ComputerBaseDefenseResponse= (recorded; later sub-phases) [RE: ai-base.md §2]
constexpr int kAiForcePredictionFudge[3] = {5, 25, 80};  // [AI] AIForcePredictionFudge= — defense-mix weighting (recorded; the AI-1 defense pick cycles the side list deterministically) [RE: ai-base.md §2]
constexpr int kAiSafeDistance = 20;           // [General] AISafeDistance= — TEAM gather distance, NOT placement [RE: ai-base.md §2]
constexpr double kAiBaseBias = 2.0;           // [General] BaseBias= — THREAT-TARGETING value multiplier when the enemy nears own base; recorded only, NEVER a placement bias [RE: ai-base.md §2]
// NOTE: there is NO HarvestersPerRefinery in [AI] — harvesters come from the
// refinery's FreeUnit (economy phase, economy.md §6) [RE: ai-base.md §2].
// Per-difficulty planned base-defense count, indexed by AiDifficulty
// (Easy/Normal/Difficult) = SovietBaseDefenseCounts [RE: ai-base.md §2;
// RE-FALLBACK: the count-array index->difficulty binding is adjudicated
// hardest->25 (Easy 6, Normal 22, Difficult 25)].
constexpr int kAiDefenseTarget[3] = {6, 22, 25};
// Placement scan cap (Chebyshev radius from the base center)
// [RE-FALLBACK: ai-base.md §3 — engine bound, not a binary constant].
constexpr int kAiMaxScanRadius = 40;

// --- Recovered difficulty-group values, indexed by AiDifficulty
// [RE-GATE APPLIED: ai-base.md §4 — REAL recovered values (no longer 1.0
// placeholders) from [Easy]/[Normal]/[Difficult] -> Rules+0x1538/+0x1588/
// +0x15d8, loader FUN_0066ccb0] ---
constexpr double kDifficultyCost[3] = {1.0, 1.0, 1.0};              // Cost= [RE: ai-base.md §4]
constexpr double kDifficultyBuildTime[3] = {0.8, 1.0, 1.0};         // BuildTime= [RE: ai-base.md §4]
constexpr double kDifficultyBuildDelayMin[3] = {0.03, 0.03, 0.1};   // BuildDelay= MINUTES, not a multiplier [RE: ai-base.md §4; RE-FALLBACK: secs = min x 60]
constexpr double kDifficultyRepairDelayMin[3] = {0.02, 0.02, 0.05}; // RepairDelay= minutes (recorded) [RE: ai-base.md §4]
constexpr bool kDifficultyBuildSlowdown[3] = {false, true, true};   // BuildSlowdown= [RE: ai-base.md §4]

// Sections [Easy]/[Normal]/[Difficult] — NOT Medium/Hard [RE: ai-base.md §4].
// Human and AI difficulty groups are INDEPENDENT and typically inverted
// (human "Easy" => weaker AI group); no starting-credit multiplier exists in
// the difficulty sections. The lobby->index arithmetic is [RE-FALLBACK:
// index 0=Easy,1=Normal,2=Difficult as loaded; default Normal].
// AiDifficulty is the LOBBY choice (Easy / Normal / Brutal=Difficult).
enum class AiDifficulty { Easy = 0, Normal = 1, Difficult = 2, Debug = 3 };
inline bool debug_ai_mode(AiDifficulty d) { return d == AiDifficulty::Debug; }
inline int normal_difficulty_index(AiDifficulty d) { return debug_ai_mode(d) ? 1 : static_cast<int>(d); }

// [G-63; RE: FUN_004f6dd0 = HouseClass::AssignHandicap(diff) -- stores diff at
// house+0x184 and copies the DifficultyClass at Rules+0x1538 + diff*0x50
// (FUN_00668630 loads [Easy] -> +0x1538, [Normal] -> +0x1588, [Difficult] ->
// +0x15d8). The MP house setup FUN_00687950 passes the slot's AI difficulty
// straight through (humans get 1); FUN_0050a3e0 passes 2 - x; the AITrigger
// consult maps house+0x184 == 0 -> Enabled_Hard (ai-trigger.md §5). So index
// 0 is the HARDEST AI and it runs on the [Easy] (bonus) DifficultyClass:
// house+0x184 = 2 - lobby. The per-difficulty CSV arrays (TeamDelays,
// *BaseDefenseCounts "h,m,e", ...) are indexed by the same house+0x184.]
inline int difficulty_class_index(AiDifficulty lobby) { return 2 - normal_difficulty_index(lobby); }

// Raw values of ONE rules difficulty group, filled by main.cpp from rulesmd
// via Rules::section_double [RE: ai-base.md §4]. Tests hand-build them from
// the kDifficulty* constants above.
struct DifficultyGroupRaw {
    double cost = 1.0;            // Cost=  (build cost multiplier)
    double build_time = 1.0;      // BuildTime= (build time multiplier)
    double build_delay = 0.03;    // BuildDelay= (MINUTES between AI build passes)
    bool build_slowdown = false;  // BuildSlowdown= (recorded; later sub-phases)
    double repair_delay = 0.02;   // RepairDelay= minutes (struct +0x38; house+0x1c0)
    double firepower = 1.0;       // FirePower= (struct +0x00; house+0x188)
    double rof = 1.0;             // ROF= (struct +0x20; house+0x1a8)
};

// Resolved per-house difficulty handicaps [RE: ai-base.md §4].
struct DifficultyMods {
    double cost_mul = 1.0;        // multiplies the credits charged for a build
    double build_time_mul = 1.0;  // multiplies build TIME (applied to BuildSpeed)
    double build_delay_min = 0.03;  // MINUTES between AI build passes (difficulty BuildDelay)
    bool build_slowdown = false;  // recorded; not consumed in AI-1
    int defense_target = 0;       // planned base-defense count (per difficulty)
    // [G-63] combat handicaps copied by AssignHandicap. CONSUMED in gamemd:
    // ROF multiplies the rearm delay (FMUL [house+0x1a8] in FUN_006fc460) and
    // FirePower the weapon damage (FLD [house+0x188] in FUN_006fd040/6fd210).
    // Armor (+0x1a0) / Groundspeed (+0x190) are stored but no reader was
    // located in this build -> not applied [RE-FALLBACK, docs/re/ai-base.md §4].
    double firepower_mul = 1.0;
    double rof_mul = 1.0;
    double repair_delay_min = 0.02;  // DifficultyClass RepairDelay (minutes) [G-65]
};

// The [AI]-section knobs, defaulting to the recovered constants (main.cpp
// re-reads them from the real rulesmd; tests use the defaults)
// [RE-GATE APPLIED: ai-base.md §2].
struct AiRules {
    double power_surplus = kAiPowerSurplus;
    double power_emergency = kAiPowerEmergency;  // recorded; sell trigger, unused in AI-1
    int base_size_add = kAiBaseSizeAdd;
    int base_spacing = kAiBaseSpacing;
    double refinery_ratio = kAiRefineryRatio;
    int refinery_limit = kAiRefineryLimit;
    double war_ratio = kAiWarRatio;
    int war_limit = kAiWarLimit;
    double defense_ratio = kAiDefenseRatio;
    int defense_limit = kAiDefenseLimit;
    double credit_reserve = kAiCreditReserve;
    double condition_red = 0.25;   // [AudioVisual] ConditionRed= (Rules+0x1708): AI sell threshold [G-65]
    // [G-66] superweapon target values ([General] AIIonCannon*Value= CSVs,
    // resolved at the house's DifficultyClass index by main.cpp). The SW AI
    // (FUN_00509710 -> FUN_0050ca10) scores every object of the house's enemy
    // and fires at a random one of the highest-valued.
    int sw_value_conyard = 100, sw_value_factory = 100, sw_value_power = 100,
        sw_value_tech = 100, sw_value_defense = 35, sw_value_harvester = 1,
        sw_value_building = 4, sw_value_unit = 2;
    // W6-02-09 [RE: FUN_0050ca10 -- Rules+0x1208 Engineer, +0x1224 Thief,
    // +0x125c MCV, +0x1278 APC (Passengers > 0); rulesmd 1,1,1 each].
    int sw_value_engineer = 1, sw_value_thief = 1, sw_value_mcv = 1, sw_value_apc = 1;
    // W6-03-06 [RE: FUN_005052d0 / FUN_004fe200] the skirmish plan knobs,
    // resolved at house+0x184 by make_ai_slot_config. -1 (every synthetic and
    // campaign config) keeps the legacy deterministic plan.
    int extra_refineries = -1;   // [General] AIExtraRefineries=2,1,0 (Rules+0x1378)
    int pick_wall_pct = -1;      // [General] AIPickWallDefensePercent=50,25,10 (Rules+0xdd8)
    // W6-03-05 [RE: FUN_0041f0f0 / FUN_006ef1d0 -- Rules+0xd70]: [General]
    // AIMinorSuperReadyPercent=.7, the Curtain/Sphere charge the AITriggers
    // and script actions 55-57 wait for.
    double minor_super_ready = 0.7;
};

// mods for difficulty d: values from groups[d], defense_target from
// kAiDefenseTarget[d] [RE-GATE APPLIED: ai-base.md §2/§4]. The BaseSizeAdd
// cap is NOT per-difficulty — next_build gates tail expansion at
// view.rival_base_size + ai.base_size_add [RE: ai-base.md §2].
DifficultyMods difficulty_mods(AiDifficulty d, const DifficultyGroupRaw (&groups)[3]);

// One planned base node — mirrors the original's 0x10-byte node in the
// house+0x5704 vector ([+0] type/marker, [+4] placed cell — 0 until placed,
// [+8] flags) [RE: ai-base.md §3/§5]: intended type + cell, the World
// building index while standing, and the destroyed-awaiting-rebuild flag
// (set by World when the placed building dies; the cell is RETAINED).
struct PlannedNode {
    int id = -1;          // STABLE node identity (assigned from AiBaseState::
                          // next_node_id at creation; never reused). The
                          // caller keys pending[] on this, NOT the vector
                          // index, so a mid-plan insert/erase never aliases a
                          // node in production to the wrong entry.
    int type = -1;        // roster index
    int cx = 0, cy = 0;   // foundation origin cell; (0,0) = not yet chosen [RE: ai-base.md §3 — cell is 0 until placed]
    int building = -1;    // World building index once placed (-1 = not placed)
    bool dead = false;    // placed building destroyed -> rebuild target (cell kept)
};

// Per-house planner state. `plan` is the PRECOMPUTED ORDERED base-node plan
// (built lazily by next_build when empty — FUN_0050a3e0 builds iff count==0)
// [RE: ai-base.md §2/§3]. `pending[q]` is the plan index the building
// currently in queue q (0=Structure, 1=Defense) will fill on placement,
// keyed by the node's STABLE id (see PlannedNode::id / next_node_id) so a
// power-preemption insert ahead of it can never re-point pending[q] at the
// wrong node. `rng` is the engine-local xorshift32 the economy/production phases
// adjudicated [RE-FALLBACK: ai-base.md deviations — stands in for the
// lockstep game RNG the original consults (build-tick jitter, node-cell
// jitter); deterministic, NEVER wall-clock].
struct AiBaseState {
    AiDifficulty difficulty = AiDifficulty::Normal;
    double build_clock = 0.0;  // secs since the last build pass (kept in seconds, Net L0 note in world.cpp)
    double build_gate = 0.0;   // secs the next pass waits for; 0 = fire
                               // immediately. Re-armed after each pass to
                               // max(~7 s tick jitter, difficulty BuildDelay)
                               // [RE-GATE APPLIED: ai-base.md §1 — the tick
                               // self-reschedules 0x69 + rand(1,7) = 105–112
                               // frames; BuildDelay is minutes, RE-FALLBACK
                               // secs = min x 60]
    std::vector<PlannedNode> plan;
    std::uint32_t rng = 0x2545F491u;  // deterministic fixed seed
    // [G-65] the house repair window (house+0x245 "a repair was started" +
    // the +0x280 timer, FUN_00450620): secs since the last AI repair start.
    double repair_clock = 0.0;   // frames (Net L0)
    int pending[2] = {-1, -1};        // STABLE node id in production per queue
                                      // (0=Structure, 1=Defense); -1 = idle
    int next_node_id = 0;             // monotonic id source; every PlannedNode
                                      // (plan build, preemption insert, and
                                      // economy-expansion append) draws from
                                      // it so ids never collide or reuse
    // W6-03-08 [RE: FUN_004fdb30 sets house+0x564c (building) / +0x5650
    // (unit) to the recovery target and abandons other production]: the
    // refinery (roster index) / harvester (unit-roster index) the house is
    // raising money for; -1 = none.
    int raise_building = -1;
    int raise_unit = -1;
};

// xorshift32 step (same algorithm family the economy phase adjudicated).
std::uint32_t ai_rand(std::uint32_t& s);

// Read-only snapshot of one house's base, filled by the caller (World).
struct BaseBuildingView {
    int type = -1;
    int cx = 0, cy = 0, fw = 1, fh = 1;
    bool operational = false;  // alive && Idle (fully built)
};
struct BaseView {
    std::vector<BaseBuildingView> own;  // this house's ALIVE buildings
    int center_x = 0, center_y = 0;     // base center = ConYard origin cell
    int threat_x = 0, threat_y = 0;     // defense bias target (enemy centroid)
    double credits = 0.0;
    int power_output = 0;
    int power_drain = 0;
    int harvesters = 0;                 // own alive harvesters
    int rival_base_size = 0;            // largest OTHER house's alive-building
                                        // count — BaseSizeAdd cap basis: AI
                                        // base <= rival + BaseSizeAdd(3)
                                        // [RE: ai-base.md §2]
};

// Roster indices of the AI's build set, resolved once by main.cpp
// [RE-GATE APPLIED: ai-base.md §2 — the recovered [AI] build lists:
// BuildConst/BuildPower/BuildRefinery/BuildBarracks/BuildWeapons/BuildRadar
// + the per-Side SovietBaseDefenses + ConcreteWalls. The [AI]
// BuildDefense/BuildAA/BuildPDefense/BuildHelipad lists are DEAD ("do
// nothing") — defenses come from the Side list + per-difficulty counts].
// -1 / empty = unavailable in this install (art missing): the planner SKIPS
// that node (art-guard-drop, never a crash).
struct AiCatalog {
    int conyard = -1;      // BuildConst -> NACNST (Soviet)
    int power = -1;        // BuildPower -> NAPOWR
    int refinery = -1;     // BuildRefinery -> NAREFN
    int barracks = -1;     // BuildBarracks -> NAHAND
    int war_factory = -1;  // BuildWeapons -> NAWEAP
    int radar = -1;        // BuildRadar -> NARADR (the prereq-sorted remainder in AI-1)
    std::vector<int> defenses;  // SovietBaseDefenses = NALASR,NABNKR,TESLA,NAFLAK [RE: ai-base.md §2]
    int wall = -1;         // ConcreteWalls first Soviet-ownable -> NAWALL [RE: ai-base.md §2]
    // [G-66] [AI] BuildTech= ("should build on each of these") -> NATECH/GATECH,
    // and the side's AIBuildThis=yes SuperWeapon= structures (NAMISL/NAIRON,
    // GAWEAT/GACSPH), planned at the tail behind their tech prerequisite.
    int tech = -1;
    std::vector<int> superweapons;
    // W2-AN-16 [rules [AI] BuildNavalYard=NAYARD,GAYARD; [General]
    // AINavalYardAdjacency=20 "distance in cells the Computer can place his
    // Naval Yard from his Con Yard"]. -1 = no yard (no water near the base:
    // the node is dropped when no water spot exists).
    int naval_yard = -1;
    int naval_yard_adjacency = 20;
};

// next_build's answer. type >= 0: start this roster type now, filling
// plan[node] (a rebuild node reuses its stored cell; a fresh node's cell is
// chosen by the caller via find_build_location and stored). wait: the head
// unsatisfied node is unaffordable at cost*cost_mul + CreditReserve -> bank
// income, start NOTHING cheaper [RE: ai-base.md §2/§5].
struct BuildDecision {
    int type = -1;
    int node = -1;   // plan index this start fills (>= 0 whenever type >= 0)
    bool wait = false;
    int wait_type = -1;   // W2-SK-06: the head type being banked for (wait == true)
};

// Build the PRECOMPUTED ORDERED base-node plan [RE-GATE APPLIED: ai-base.md
// §2a — FUN_005052d0 is a Kahn-style TOPOLOGICAL PREREQ-SORT]. ConYard +
// Power append first; Barracks + War Factory are HOISTED to the front of the
// candidate set as a PREFERENCE the prereq-sort OVERRIDES when a prereq is
// unmet, so a prerequisite building is always emitted BEFORE the node that
// depends on it: the Refinery (needs only Power) lands BEFORE the War Factory
// (needs PROC) AND the Radar (needs NAREFN); Barracks (no refinery prereq)
// keeps its hoisted slot. Effective order: ConYard -> Power -> Barracks ->
// Refinery -> War Factory -> Radar -> extra Refineries (x RefineryLimit) ->
// base defenses (x mods.defense_target, cycling the side list) -> walls
// (ConcreteWalls, one node per planned defense). NB the original rings wall
// cells around each defense's foundation perimeter (FUN_0050c160), but in
// THIS engine a wall node is NOT in catalog.defenses, so it is placed by the
// GENERIC center-out scan (find_build_location is_defense=false) like any
// non-defense node — a [RE-FALLBACK] deviation, not the perimeter ring. A
// !progress FORCE-EMIT
// cycle-breaker guarantees termination on an unsatisfiable/cyclic prereq set.
// Catalog gaps (-1) are skipped.
//
// W6-03-06: with `rng` and ai.extra_refineries >= 0 (a skirmish house) the
// plan follows FUN_005052d0 instead: ONE refinery in the topo sort, then
// AIExtraRefineries more at rand(firstRefinery, n-1)+1, then the per-difficulty
// base-defence placeholders each at rand(3, n-1)+1 -- spread through the
// whole build-out rather than a tail block. A placeholder resolves (in plan
// order) like FUN_004fe200's -1 node: with rand(0,99) < AIPickWallDefensePercent
// and a defence ahead of it, a wall node; else a defence whose prereqs the
// plan before it meets (none -> the node is dropped).
std::vector<PlannedNode> build_base_plan(const BaseView& view, const AiCatalog& cat,
                                         const std::vector<BuildingTypeDef>& roster,
                                         const AiRules& ai, const DifficultyMods& mods,
                                         std::uint32_t* rng = nullptr);

// Walk the ordered plan ONE structure at a time [RE-GATE APPLIED: ai-base.md
// §1/§2/§2a — FUN_004fe200 is a HEAD-ONLY dispatcher; the single
// Structure-queue slot maps the house+0x564c one-at-a-time gate]: lazily
// build the plan when empty, then return the first UNSATISFIED node (its
// building doesn't currently exist — dead nodes are unsatisfied again, at
// their original list position, so rebuild-before-tail-expansion emerges
// [RE: ai-base.md §5]). It NEVER skips a prereq-blocked head and has NO
// find-next-buildable scan — the topologically prereq-sorted plan
// (build_base_plan) guarantees the head is buildable once its predecessors
// stand [RE: ai-base.md §2a]. Two dynamic
// overrides: (a) POWER PREEMPTION — before a power-draining node that would
// push drain > output - PowerSurplus(50), INSERT a Power node ahead of it;
// (b) ECONOMY EXPANSION — separately APPEND an extra refinery/war-factory
// node when affordable and under its Ratio/Limit (FUN_004fdb30).
// PowerSurplus=50 (reserve/top-up), the drainer-preemption gate, and
// PowerEmergency=0.75 (a SELL trigger, unused in AI-1) are THREE DISTINCT
// rules — never conflated [RE: ai-base.md §2]. Affordability gates the
// START: credits >= cost*cost_mul + CreditReserve, else wait/bank — never a
// cheaper substitute. Prereq/tech gating stays with the CALLER's shared
// can_start (the same player path, ai-base.md §6); the build clock is gated
// by the CALLER (World owns dt).
BuildDecision next_build(AiBaseState& state, const BaseView& view,
                         const AiCatalog& cat,
                         const std::vector<BuildingTypeDef>& roster,
                         const AiRules& ai, const DifficultyMods& mods);

// Deterministic placement scan [RE-GATE APPLIED: ai-base.md §3 — the exact
// outward scan was NOT pinned; RE-FALLBACK (calibrated, keep): nearest-first
// Chebyshev rings from the base center, row-major within a ring; defenses
// probe each ring's cells nearest the threat first (defense/wall cells
// derive from foundation-perimeter rings around the base). The AI BYPASSES
// the player's Adjacent= proximity check (FUN_004a8dc0 early-returns for
// non-local-human houses), so `can_place(cx, cy)` is the shared PER-CELL
// buildable/unoccupied grid check over the full foundation with the
// AIBaseSpacing(1) gap — bound by the caller (placement_check with
// bypass_adjacency=true, foundation expanded by kAiBaseSpacing). BaseBias
// plays NO role here (it is a targeting multiplier [RE: ai-base.md §2]).
// Returns the first accepted foundation origin, or {-1, -1} when no cell
// within kAiMaxScanRadius passes (boxed in -> the caller holds and retries).
Cellxy find_build_location(const BaseView& view, bool is_defense,
                           const std::function<bool(int, int)>& can_place);

}  // namespace ra2engine

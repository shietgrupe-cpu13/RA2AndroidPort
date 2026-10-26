#include "ai_base.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ra2engine {

std::uint32_t ai_rand(std::uint32_t& s) {
    // xorshift32 [RE-FALLBACK: ai-base.md deviations — engine-local RNG for
    // any adjudicated tie-break; deterministic, never wall-clock].
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

DifficultyMods difficulty_mods(AiDifficulty d, const DifficultyGroupRaw (&groups)[3]) {
    // `d` is the LOBBY choice; the DifficultyClass is groups[2 - d] (the
    // hardest AI runs on [Easy]) [G-63; RE: FUN_004f6dd0, ai-base.md §4].
    const int i = normal_difficulty_index(d);
    const int c = difficulty_class_index(d);
    DifficultyMods m;
    m.cost_mul = groups[c].cost;
    // W6-02-04 [RE: house+0x1B8 BuildTime is written only by AssignHandicap
    // FUN_004F6DD0 @0x4F6FFA and read only by the house CRC (0x502C31);
    // Time_To_Build FUN_006F3D00 uses only the country multiplier
    // FUN_0050BEC0]: the BuildTime handicap is never applied.
    m.build_time_mul = 1.0;
    m.build_delay_min = groups[c].build_delay;   // minutes
    m.build_slowdown = groups[c].build_slowdown;
    m.firepower_mul = groups[c].firepower;
    m.rof_mul = groups[c].rof;
    m.repair_delay_min = groups[c].repair_delay;
    // kAiDefenseTarget is already stored lobby-ordered ({6,22,25} = the file's
    // "25,22,6 ;h,m,e" reversed), so it keeps the lobby index.
    m.defense_target = kAiDefenseTarget[i];
    return m;
}

namespace {
long long dist2(int ax, int ay, int bx, int by) {
    const long long dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}
}  // namespace

Cellxy find_build_location(const BaseView& view, bool is_defense,
                           const std::function<bool(int, int)>& can_place) {
    // [RE-GATE APPLIED: ai-base.md §3 — the exact outward scan was NOT
    // pinned; this nearest-first Chebyshev ring from the base center,
    // row-major (dy then dx ascending) within a ring, stands as the
    // CALIBRATED RE-FALLBACK. Deterministic; foundation/overlap enforced by
    // can_place — the shared per-cell buildable/unoccupied grid check with
    // the AIBaseSpacing(1) gap. The AI BYPASSES the Adjacent= proximity
    // check (FUN_004a8dc0 early-return for non-local-human houses), so
    // can_place carries NO adjacency requirement. Defenses probe each ring's
    // cells nearest the THREAT first (perimeter-facing bias; defense/wall
    // cells derive from foundation-perimeter rings around the base) with the
    // row-major order as the stable tie-break. BaseBias is NOT used here —
    // it is a threat-TARGETING multiplier [RE: ai-base.md §2].]
    const int ox = view.center_x, oy = view.center_y;
    for (int r = 0; r <= kAiMaxScanRadius; ++r) {
        std::vector<Cellxy> ring;
        ring.reserve(r == 0 ? 1 : 8 * r);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                ring.push_back(Cellxy{ox + dx, oy + dy});
            }
        if (is_defense) {
            std::stable_sort(ring.begin(), ring.end(), [&](const Cellxy& a, const Cellxy& b) {
                return dist2(a.x, a.y, view.threat_x, view.threat_y) <
                       dist2(b.x, b.y, view.threat_x, view.threat_y);
            });
        }
        for (const Cellxy& c : ring)
            if (can_place(c.x, c.y)) return c;
    }
    return Cellxy{-1, -1};  // boxed in / nothing within the cap -> caller holds
}

namespace {
int count_alive(const BaseView& v, int type) {
    if (type < 0) return 0;
    int n = 0;
    for (const BaseBuildingView& b : v.own)
        if (b.type == type) ++n;
    return n;
}
// Affordability [RE-GATE APPLIED: ai-base.md §2 — the START is gated on
// credits >= cost x difficulty cost_mul + CreditReserve(100, kept banked);
// unaffordable -> WAIT/bank, never a cheaper substitute].
BuildDecision pick_node(const std::vector<PlannedNode>& plan, int i,
                        const BaseView& v,
                        const std::vector<BuildingTypeDef>& roster,
                        const AiRules& ai, const DifficultyMods& m) {
    BuildDecision d;
    const int type = plan[i].type;
    if (type < 0 || type >= (int)roster.size()) return d;  // marker/gap: no-op
    if (v.credits < roster[type].cost * m.cost_mul + ai.credit_reserve) {
        d.wait = true;  // bank income [RE: ai-base.md §2 — CreditReserve]
        d.wait_type = type;
        return d;
    }
    d.type = type;
    d.node = i;
    return d;
}
// Plan-so-far prerequisite check [RE-GATE APPLIED: ai-base.md §2a —
// FUN_00505180 tests a type's prereq tokens against a SUPPLIED building
// list (here: the plan under construction), NOT the house's standing
// buildings. Engine mapping of the EXISTING prereq semantics
// (BuildingTypeDef::prerequisite, construction.md §10): a plain token
// matches a plan node whose roster id equals it; a GROUP token maps to the
// catalog slot resolved from the SAME [AI] build list the original's
// negative token expands to (-1 Power/BuildPower, -2 Factory/BuildWeapons,
// -3 Barracks/BuildBarracks, -4 Radar/BuildRadar, -5 Tech/BuildTech,
// -6 PROC/BuildRefinery — ai-base.md §2a Evidence 1, so the AiCatalog IS
// the recovered group table). An unresolvable token (catalog gap, TECH in
// AI-1) is UNMET — the cycle-breaker below eventually force-emits.]
bool prereqs_in_plan(int type, const std::vector<PlannedNode>& plan,
                     const std::vector<BuildingTypeDef>& roster,
                     const AiCatalog& cat) {
    for (const std::string& tok : roster[type].prerequisite) {
        int want = -1;  // roster index a group token demands; -1 = plain token
        if (tok == "POWER") want = cat.power;
        else if (tok == "FACTORY") want = cat.war_factory;
        else if (tok == "BARRACKS") want = cat.barracks;
        else if (tok == "RADAR") want = cat.radar;
        else if (tok == "TECH") want = cat.tech;
        else if (tok == "PROC") want = cat.refinery;
        bool met = false;
        for (const PlannedNode& n : plan) {
            if (n.type < 0) continue;
            if (want >= 0 ? n.type == want : roster[n.type].id == tok) {
                met = true;
                break;
            }
        }
        if (!met) return false;
    }
    return true;
}

// rand(lo, hi) inclusive over the house's seeded xorshift32 [RE: FUN_0065c220;
// RE-FALLBACK: ai_rand stands in for the lockstep RNG].
int rand_in(std::uint32_t& rng, int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(ai_rand(rng) % (std::uint32_t)(hi - lo + 1));
}

// W6-03-06 [RE: FUN_005052d0 tail + FUN_004fe200 -1 dispatch; see the
// header]. Marker -1 = a base-defence placeholder until resolved below.
void spread_plan_tail(std::vector<PlannedNode>& plan, int& next_id, const AiCatalog& cat,
                      const std::vector<BuildingTypeDef>& roster, const AiRules& ai, int defs,
                      std::uint32_t& rng) {
    auto make = [&](int t) { PlannedNode n; n.id = next_id++; n.type = t; return n; };
    // Extra refineries: rand(first refinery, n-1) + 1, AIExtraRefineries times.
    int fr = -1;
    for (int i = 0; i < (int)plan.size(); ++i)
        if (cat.refinery >= 0 && plan[i].type == cat.refinery) { fr = i; break; }
    if (fr >= 0)
        for (int k = 0; k < ai.extra_refineries; ++k) {
            const int pos = rand_in(rng, fr, (int)plan.size() - 1);
            plan.insert(plan.begin() + pos + 1, make(cat.refinery));
        }
    // Base-defence placeholders: rand(3, n-1) + 1 each.
    if (cat.defenses.empty() || (int)plan.size() < 4) return;
    for (int k = 0; k < defs; ++k) {
        const int pos = rand_in(rng, 3, (int)plan.size() - 1);
        plan.insert(plan.begin() + pos + 1, make(-1));
    }
    // Resolve each placeholder against the plan before it.
    std::vector<PlannedNode> out;
    int walls_owed = 0, cycle = 0;
    for (const PlannedNode& n : plan) {
        if (n.type != -1) { out.push_back(n); continue; }
        PlannedNode r = n;
        if (cat.wall >= 0 && walls_owed > 0 && ai.pick_wall_pct > 0 &&
            rand_in(rng, 0, 99) < ai.pick_wall_pct && prereqs_in_plan(cat.wall, out, roster, cat)) {
            r.type = cat.wall;   // FUN_0050c160: walls for a defence that still needs them
            --walls_owed;
            out.push_back(r);
            continue;
        }
        std::vector<int> ok;   // FUN_00506d10: a defence this house can build here
        for (int t : cat.defenses)
            if (t >= 0 && t < (int)roster.size() && prereqs_in_plan(t, out, roster, cat)) ok.push_back(t);
        if (ok.empty()) continue;   // nothing buildable: the node is removed
        r.type = ok[(std::size_t)(cycle++ % (int)ok.size())];   // [RE-FALLBACK: AIForcePrediction pick]
        ++walls_owed;
        out.push_back(r);
    }
    plan.swap(out);
}
}  // namespace

std::vector<PlannedNode> build_base_plan(const BaseView& view, const AiCatalog& cat,
                                         const std::vector<BuildingTypeDef>& roster,
                                         const AiRules& ai, const DifficultyMods& mods,
                                         std::uint32_t* rng) {
    const bool spread = rng != nullptr && ai.extra_refineries >= 0;   // W6-03-06
    // [RE-GATE APPLIED: ai-base.md §2a — FUN_005052d0 is a Kahn-style
    // TOPOLOGICAL PREREQ-SORT, not a fixed-order append. ConYard + Power
    // are appended directly (§2 steps 1-2); Barracks/War Factory are
    // HOISTED to candidate indices 0/1 as a PREFERENCE ONLY (§2 steps 3-4)
    // — the sort OVERRIDES the hoist when a prereq is unmet, which is
    // exactly how the Refinery (needs only Power) lands BEFORE the War
    // Factory (needs PROC) and the Radar (needs NAREFN). A catalog gap
    // (-1: art missing / not in this install) is SKIPPED — art-guard-drop,
    // never a crash. `view` stays reserved for later sub-phases.]
    (void)view;
    std::vector<PlannedNode> plan;
    int next_id = 0;  // contiguous ids 0..N-1 for the static plan; next_build
                      // continues the per-state counter past the max (below)
    auto append = [&](int t) {
        PlannedNode n;
        n.id = next_id++;
        n.type = t;
        plan.push_back(n);
    };
    // 1-2) ConYard + Power append DIRECTLY (not topo-gated) [RE: §2].
    if (cat.conyard >= 0 && cat.conyard < (int)roster.size()) append(cat.conyard);
    if (cat.power >= 0 && cat.power < (int)roster.size()) append(cat.power);
    // 3-4) Candidate multiset in PREFERENCE order: Barracks + War Factory
    // hoisted to the front [RE: §2 steps 3-4], then the prereq-sorted
    // remainder (AI-1: radar), Refineries x RefineryLimit(4), defenses x
    // per-difficulty count (cycling the side list deterministically — stands
    // in for the AIForcePrediction weighting [RE-FALLBACK: ai-base.md §2]),
    // walls (ConcreteWalls) at the natural tail — one wall node per planned
    // defense. NB the original rings wall cells on the defense foundation
    // perimeter (FUN_0050c160); here a wall node is NOT a catalog.defenses
    // entry, so it is placed by the GENERIC center-out scan
    // (find_build_location is_defense=false), not the perimeter ring
    // [RE: §2/§3; RE-FALLBACK: one wall node per defense in AI-1].
    std::vector<int> cand;
    auto add_cand = [&](int t) {
        if (t >= 0 && t < (int)roster.size()) cand.push_back(t);
    };
    add_cand(cat.barracks);
    add_cand(cat.war_factory);
    add_cand(cat.radar);
    for (int i = 0; i < (spread ? 1 : ai.refinery_limit); ++i) add_cand(cat.refinery);
    // [G-66] tech centre + superweapon structures [RE-FALLBACK: position --
    // W2-AN-21 moves them (and the W2-AN-16 Naval Yard) from the plan tail to
    // right after the refineries, ahead of the up-to-25 defenses and their
    // walls: at the tail a Brutal AI met them only after ~40 minutes, while
    // the original AI fields its tech centre and superweapons mid-game. The
    // topo loop still orders them behind their prerequisites.]
    add_cand(cat.naval_yard);   // W2-AN-16: dropped at placement when no water is near
    add_cand(cat.tech);
    for (int t : cat.superweapons) add_cand(t);
    const int defs = std::min(mods.defense_target, ai.defense_limit);
    if (!spread && !cat.defenses.empty())
        for (int i = 0; i < defs; ++i)
            add_cand(cat.defenses[i % (int)cat.defenses.size()]);
    if (!spread && cat.wall >= 0 && !cat.defenses.empty())
        for (int i = 0; i < defs; ++i) add_cand(cat.wall);
    // 5) Kahn-style topological loop [RE-GATE APPLIED: ai-base.md §2a —
    // FUN_005052d0's main loop]: emit the FIRST candidate whose prereqs are
    // already met BY THE PLAN-SO-FAR (prereqs_in_plan == the original's
    // FUN_00505180 fed &outputPlanVector), then rescan from the front so
    // the hoisted candidates are always retried first. Deterministic — no
    // RNG: extra refineries fall in right after the prereq-sorted core
    // instead of the original's RNG-spaced inserts, and defenses stay
    // consecutive [RE-FALLBACK: ai-base.md deviations — engine-deterministic
    // stand-in for the lockstep-RNG positions].
    while (!cand.empty()) {
        bool progress = false;
        for (std::size_t i = 0; i < cand.size(); ++i) {
            if (!prereqs_in_plan(cand[i], plan, roster, cat)) continue;
            append(cand[i]);
            cand.erase(cand.begin() + i);
            progress = true;
            break;  // rescan from the front: the hoist preference
        }
        if (!progress) {
            // FORCE-EMIT cycle-breaker [RE-GATE APPLIED: ai-base.md §2a —
            // the !progress branch force-appends an unused candidate so the
            // loop ALWAYS terminates, even on an unsatisfiable/cyclic
            // prereq set; matching FUN_005052d0, the LAST unused candidate
            // is taken — deterministic, no RNG].
            append(cand.back());
            cand.pop_back();
        }
    }
    if (spread) spread_plan_tail(plan, next_id, cat, roster, ai, defs, *rng);
    // NOTE: the static plan is NOT trimmed by BaseSizeAdd — the cap gates
    // DYNAMIC expansion (next_build) at rival + BaseSizeAdd(3) [RE: §2].
    return plan;
}

BuildDecision next_build(AiBaseState& state, const BaseView& view,
                         const AiCatalog& cat,
                         const std::vector<BuildingTypeDef>& roster,
                         const AiRules& ai, const DifficultyMods& mods) {
    // Lazy plan build — the original builds the node vector iff empty
    // (FUN_0050a3e0 -> FUN_005052d0) [RE: ai-base.md §2/§3].
    if (state.plan.empty()) {
        state.plan = build_base_plan(view, cat, roster, ai, mods, &state.rng);
        // Continue the stable-id counter past the ids build_base_plan assigned
        // so every later insert/append draws a fresh, never-reused id.
        state.next_node_id = 0;
        for (const PlannedNode& n : state.plan)
            if (n.id + 1 > state.next_node_id) state.next_node_id = n.id + 1;
    }

    // PLAN WALK [RE-GATE APPLIED: ai-base.md §1/§2/§2a/§5 — FUN_004fe200
    // is a HEAD-ONLY dispatcher]: the first UNSATISFIED node in plan order,
    // ONE structure at a time (the single Structure-queue slot /
    // house+0x564c gate lives with the caller). Satisfaction is
    // existence-based: each node is covered by one standing own building of
    // its type; a DEAD node is unsatisfied again at its ORIGINAL position
    // -> rebuild-before-tail-expansion [RE: §5]. The walk NEVER skips a
    // prereq-blocked head and has NO find-next-buildable scan —
    // FUN_004fe200 peeks the head node only; the topologically
    // prereq-sorted plan (build_base_plan) guarantees the head is buildable
    // once its predecessors stand [RE: §2a]. The ONLY thing ever put ahead
    // of the head is the power-preemption INSERT below.
    std::vector<int> claimed(roster.size(), 0);
    for (int i = 0; i < (int)state.plan.size(); ++i) {
        const PlannedNode& n = state.plan[i];
        if (n.type < 0 || n.type >= (int)roster.size()) continue;  // gap/marker
        if (!n.dead && claimed[n.type] < count_alive(view, n.type)) {
            ++claimed[n.type];  // satisfied by a standing building
            continue;
        }
        // POWER PREEMPTION [RE-GATE APPLIED: ai-base.md §1/§2 —
        // FUN_004fe200 step 4]: before a drainer that would push
        // drain > output - PowerSurplus(50), INSERT a Power node ahead of
        // it. PowerSurplus (the reserve), this gate, and
        // PowerEmergency=0.75 (a SELL trigger — recorded in kAiPowerEmergency,
        // NOT consumed by AI-1) are THREE DISTINCT rules [RE: §2].
        const BuildingTypeDef& t = roster[n.type];
        if (t.power < 0 && cat.power >= 0 && n.type != cat.power &&
            view.power_drain + (-t.power) >
                view.power_output - om::ftol(ai.power_surplus)) {
            PlannedNode p;
            p.id = state.next_node_id++;  // stable id -> pending[] never aliases
            p.type = cat.power;
            state.plan.insert(state.plan.begin() + i, p);
            return pick_node(state.plan, i, view, roster, ai, mods);
        }
        return pick_node(state.plan, i, view, roster, ai, mods);
    }

    // ECONOMY EXPANSION [RE-GATE APPLIED: ai-base.md §2 — FUN_004fdb30]: a
    // SEPARATE trigger once the plan is satisfied — APPEND an extra
    // refinery (then war-factory) node when under its Ratio/Limit and
    // affordable. Opportunistic: unaffordable -> no wait, just bank.
    // BaseSizeAdd caps the whole base at rival + BaseSizeAdd(3) [RE: §2].
    if ((int)view.own.size() >= view.rival_base_size + ai.base_size_add)
        return BuildDecision{};  // base big enough: build nothing
    const int expand[2] = {cat.refinery, cat.war_factory};
    const int limit[2] = {ai.refinery_limit, ai.war_limit};
    for (int e = 0; e < 2; ++e) {
        const int t = expand[e];
        // NOTE (MINOR-4): the refinery branch (e==0) is effectively
        // UNREACHABLE — build_base_plan already emits refinery_limit refinery
        // nodes, so once they stand count_alive(refinery) >= refinery_limit
        // and this `continue` always fires. Only the war-factory branch
        // (static plan holds ONE, war_limit==2) ever appends here.
        if (t < 0 || count_alive(view, t) >= limit[e]) continue;
        if (view.credits < roster[t].cost * mods.cost_mul + ai.credit_reserve)
            continue;  // opportunistic — no wait
        PlannedNode n;
        n.id = state.next_node_id++;  // stable id for the appended expansion node
        n.type = t;
        state.plan.push_back(n);
        const int ni = (int)state.plan.size() - 1;
        // Route the inserted economy node through the SAME power-preemption
        // gate a normal head node passes [RE: ai-base.md §2 — FUN_004fdb30
        // inserts the node then dispatches through FUN_004fe200's power gate]:
        // if starting this drainer would push drain > output - PowerSurplus(50),
        // INSERT a Power node AHEAD of it first (MINOR-3).
        const BuildingTypeDef& et = roster[t];
        if (et.power < 0 && cat.power >= 0 && t != cat.power &&
            view.power_drain + (-et.power) >
                view.power_output - om::ftol(ai.power_surplus)) {
            PlannedNode p;
            p.id = state.next_node_id++;  // stable id -> pending[] never aliases
            p.type = cat.power;
            state.plan.insert(state.plan.begin() + ni, p);
            return pick_node(state.plan, ni, view, roster, ai, mods);
        }
        BuildDecision d;
        d.type = t;
        d.node = ni;
        return d;
    }
    return BuildDecision{};  // plan satisfied, limits met: build nothing
}

}  // namespace ra2engine

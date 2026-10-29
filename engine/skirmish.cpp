#include "skirmish.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include "ra2assets/rules.hpp"

namespace ra2engine {

SkirmishConfig default_skirmish_config(const ra2assets::Rules& rules) {
    SkirmishConfig cfg;
    cfg.starting_cash = (double)rules.mp_money();   // shipped 10000
    cfg.slots = {
        SkirmishSlot{World::Side::Allied,  0, "Americans", 0, /*comb*/true,  /*ai*/false, AiDifficulty::Normal},
        SkirmishSlot{World::Side::Soviet,  1, "Russians",  1, /*comb*/true,  /*ai*/true,  AiDifficulty::Normal},
        SkirmishSlot{World::Side::Neutral, 2, "Civilian",  2, /*comb*/false, /*ai*/false, AiDifficulty::Normal},
    };
    return cfg;
}

void configure_skirmish_houses(World& world, const SkirmishConfig& cfg) {
    for (int i = 0; i < (int)cfg.slots.size(); ++i) {
        const SkirmishSlot& s = cfg.slots[i];
        world.set_house(i, World::HouseRec{s.side, s.color_ix, s.country, s.alliance_team, s.combatant});
    }
    world.enable_shroud(cfg.shroud);
    // Standard skirmish uses both unexplored shroud and dimmed explored fog.
    // Without FogOfWar, a cell remains fully bright after its scout leaves,
    // leaving the player with only the solid-black unexplored region.
    world.set_fog_of_war(true);
    world.enable_ally_reveal(true);                 // rules AllyReveal=yes
    world.enable_victory(true, cfg.short_game);
    for (int i = 0; i < (int)cfg.slots.size(); ++i) {
        if (!cfg.slots[i].combatant) continue;
        world.grant_credits_for_test(i, cfg.starting_cash);   // deterministic per-house credit seed
        if (cfg.slots[i].is_ai) {
            AiHouseConfig ai;                       // minimal: difficulty only (roster filled by launch_skirmish)
            ai.difficulty = cfg.slots[i].diff;
            world.enable_ai_house(i, ai);
        }
    }
    apply_computer_alliance(world, cfg);
}

// W6-a [RE: FUN_00501460, called when a computer house sets up its base
// (FUN_0050a3e0 @0x50A5D4 on its ConYard, FUN_00504fa0 on its MCV deploy),
// when a computer house is defeated with Paranoid=yes (FUN_004fbed0) and when
// a human allies (FUN_004f9990)]: if Rules+0x14b5 [MultiplayerDialogSettings]
// AlliesAllowed (rulesmd says no, but the Battle mode INI MPBattleMD.ini --
// the only mode that allows AI players, mpmodesmd.ini -- overrides it to yes)
// and no lobby team was chosen (Scenario+0x11e0, set by FUN_00687950 when any
// slot's team != -1), every living computer house allies with every other
// computer house and is the enemy of every human. So a plain skirmish is
// the human against an alliance of all the AIs; with lobby teams the teams
// stand as chosen. Applied once at launch (every AI unfolds its MCV on its
// first frames). [Harness deviation: with no human combatant -- the
// --ai-vs-ai soak, not a mode the original offers -- the AIs stay apart so
// they still fight.]
void apply_computer_alliance(World& world, const SkirmishConfig& cfg) {
    bool teams = false, human = false;
    for (const SkirmishSlot& s : cfg.slots) {
        if (!s.combatant) continue;
        if (s.alliance_team >= 0) teams = true;
        if (!s.is_ai) human = true;
    }
    if (teams || !human) return;
    const int n = (int)cfg.slots.size();
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (cfg.slots[i].combatant && cfg.slots[j].combatant && cfg.slots[i].is_ai &&
                cfg.slots[j].is_ai)
                world.set_pair_alliance(i, j, true);
}

}  // namespace ra2engine

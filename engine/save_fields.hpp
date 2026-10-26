#pragma once
// P7 Save / Load: THE field lists. One visitor per struct, one line per field,
// shared by save (Writer), load (Reader) and the coverage tripwire (Counter).
//
// ADDING STATE (P4/P5/P6/P10 and later):
//   1. add `io(ar, x.your_field);` to the struct's visitor below (or to
//      SaveAccess::world for a new World member),
//   2. bump save::kFormatVersion in savegame.hpp,
//   3. if you added a NEW struct type, add a `fields` overload for it and a
//      CHECK_ARITY line in tests/test_saveload.cpp.
// tests/test_saveload.cpp FAILS (naming the struct) when a struct gains a
// member that its visitor does not visit.

#include "map_trigger.hpp"
#include "savegame.hpp"
#include "campaign.hpp"   // P10 CampaignHostSave
#include "skirmish.hpp"
#include "world.hpp"

namespace ra2engine {

template<class Ar> void fields(Ar& ar,DrainType& v) {
    save::io(ar,v.enabled);save::io(ar,v.refinery);save::io(ar,v.power);
}
template<class Ar> void fields(Ar& ar,DrainLink& v) {
    save::io(ar,v.unit);save::io(ar,v.building);save::io(ar,v.source_house);
    save::io(ar,v.target_house);save::io(ar,v.frames);
}
template<class Ar> void fields(Ar& ar,DrainState& v) {
    save::io(ar,v.enabled);save::io(ar,v.amount);save::io(ar,v.delay);
    save::io(ar,v.sources);save::io(ar,v.targets);save::io(ar,v.links);
}

// The single friend every class with private save-state names. Static members
// only; visitors for private members and private nested types live here.
struct SaveAccess {
    // R-181: after a load, every cross-reference index (unit -> unit /
    // building, building -> type, house numbers) and every grid size must be
    // in range. The CRC-32 trailer catches random corruption; this catches a
    // well-formed file whose numbers would index out of bounds (a hand-edited
    // or mismatched save). Defined in savegame.cpp.
    static bool valid_world(const World& w, std::string* why = nullptr);
    template <class Ar> static void occupancy(Ar& ar, Occupancy& o) {
        save::io(ar, o.w_);
        save::io(ar, o.h_);
        save::io(ar, o.owner_);
        save::io(ar, o.spots_);   // v20: W5-B infantry sub-cell spots
        save::io(ar, o.inf_);
        save::io(ar, o.owner_d_);   // v42: w8fix-layer deck plane
        save::io(ar, o.spots_d_);
        save::io(ar, o.lay_);
        // W9-02-01: the per-unit claim index is derived -- rebuilt from the
        // planes on the first release_all_of after a load.
        if constexpr (Ar::kLoading) {
            o.claims_.clear();
            o.claims_dirty_ = true;
        }
    }
    template <class Ar> static void ore_field(Ar& ar, OreField& f) {
        save::io(ar, f.w_);
        save::io(ar, f.h_);
        save::io(ar, f.kind_);
        save::io(ar, f.density_);
        save::io(ar, f.variant_);
        save::io(ar, f.timer_);
        save::io(ar, f.mines_);   // W2-SK-01 ore mines (v13)
        save::io(ar, f.growth_frames_);
        save::io(ar, f.spread_frames_);
        save::io(ar, f.spread_timer_);
    }
    template <class Ar> static void queues(Ar& ar, ProductionQueues& q) { save::io(ar, q.q_); }
    template <class Ar> static void rad_field(Ar& ar, RadField& f) {   // P4 radiation
        save::io(ar, f.cell_);
        save::io(ar, f.sites_);
        save::io(ar, f.w_);
        save::io(ar, f.h_);
    }
    template <class Ar> static void game_random(Ar& ar, GameRandom& g) {   // P5 crate RNG
        save::io(ar, g.table_);
        save::io(ar, g.idx1_);
        save::io(ar, g.idx2_);
    }

    // --- World private nested types ---
    template <class Ar> static void house_ai(Ar& ar, World::HouseAiRec& r) {
        save::io(ar, r.is_ai);
        save::io(ar, r.cfg);
        save::io(ar, r.state);
        save::io(ar, r.team_state);
        save::io(ar, r.trigger_state);
        save::io(ar, r.enemy_house);   // W2-SK-08 (v13)
        save::io(ar, r.anger);         // W6-03-04 (v25)
        save::io(ar, r.hate_frames);   // W6-03-04 (v25)
    }
    template <class Ar> static void house_sw(Ar& ar, World::HouseSuperweapon& s) {
        save::io(ar, s.has_sw);
        save::io(ar, s.sw);
        save::io(ar, s.eff_damage);
        save::io(ar, s.eff_cell_spread);
        save::io(ar, s.eff_percent_at_max);
        save::io(ar, s.eff_verses);
        save::io(ar, s.is_powered);   // gap owner A (G-29)
    }
    template <class Ar> static void an_world(Ar& ar, World::AnWorld& a) {   // W2-AN (v9)
        save::io(ar, a.any);
        save::io(ar, a.sw);
        save::io(ar, a.strikes);
        save::io(ar, a.events);
        save::io(ar, a.event_seq);
        save::io(ar, a.types);
        save::io(ar, a.dock_offsets);
        save::io(ar, a.rng);
        save::io(ar, a.frame);
        save::io(ar, a.sw_btype);
        save::io(ar, a.repair_btype);
        save::io(ar, a.airfield_btype);
        save::io(ar, a.nuke_set);
        save::io(ar, a.nuke_damage);
        save::io(ar, a.nuke_spread);
        save::io(ar, a.nuke_pmax);
        save::io(ar, a.nuke_verses);
        save::io(ar, a.waves);   // v35: W7-06-10
    }
    template <class Ar> static void tech_income(Ar& ar, World::TechIncome& t) {
        save::io(ar, t.building);
        save::io(ar, t.amount);
        save::io(ar, t.delay);
        save::io(ar, t.frames);
    }
    template <class Ar> static void harvest_event(Ar& ar, World::PendingHarvestEvent& e) {
        save::io(ar, e.kind);
        save::io(ar, e.unit);
    }
    using HouseAiRec = World::HouseAiRec;
    using HouseSuperweapon = World::HouseSuperweapon;
    using AnWorld = World::AnWorld;   // W2-AN
    using TechIncome = World::TechIncome;
    using PendingHarvestEvent = World::PendingHarvestEvent;

    // --- World: every member except map_ (the caller-owned MapFile) ---
    template <class Ar> static void world(Ar& ar, World& w) {
        save::io(ar, w.ally_reveal_); save::mark(ar, "ally_reveal_");
        // Projectile phase (P2)
        save::io(ar, w.projectiles_enabled_); save::mark(ar, "projectiles_enabled_");
        save::io(ar, w.proj_rules_); save::mark(ar, "proj_rules_");
        save::io(ar, w.proj_rng_); save::mark(ar, "proj_rng_");
        save::io(ar, w.projectiles_); save::mark(ar, "projectiles_");
        save::io(ar, w.proj_events_); save::mark(ar, "proj_events_");
        save::io(ar, w.proj_accum_); save::mark(ar, "proj_accum_");
        save::io(ar, w.proj_frame_); save::mark(ar, "proj_frame_");
        save::io(ar, w.proj_seq_); save::mark(ar, "proj_seq_");   // W4 (v15)
        // AI-1/2/3
        save::io(ar, w.ai_houses_); save::mark(ar, "ai_houses_");
        save::io(ar, w.ai_any_); save::mark(ar, "ai_any_");
        // L2 per-house economy (save v32): wallets + queues of every player
        // and AI house, the human mask, the latched outcomes, the cheer flags.
        save::io(ar, w.econ_); save::mark(ar, "econ_");
        // W9-05: the per-house production rules are not saved (the host
        // re-installs them), but both peers must have built them alike: the
        // digest taken at install time is hashed.
        save::io_hash_only(ar, w.econ_rules_set_);
        save::io_hash_only(ar, w.econ_rules_digest_); save::mark(ar, "econ_rules_digest_");
        save::io(ar, w.human_mask_); save::mark(ar, "human_mask_");
        save::io(ar, w.house_outcome_); save::mark(ar, "house_outcome_");
        save::io(ar, w.victory_cheered_); save::mark(ar, "victory_cheered_");
        // CB-4 superweapons
        save::io(ar, w.sw_houses_); save::mark(ar, "sw_houses_");
        save::io(ar, w.sw_any_); save::mark(ar, "sw_any_");
        // W2-AN (save v9): air/naval/SW v2 state + the amphibious scratch grid
        save::io(ar, w.an_); save::mark(ar, "an_");
        save::io_unhashed(ar, w.amphib_passable_); save::mark(ar, "amphib_passable_");   // W9-05: derived (amphib_grid)
        // Capture
        save::io(ar, w.capture_any_); save::mark(ar, "capture_any_");
        save::io(ar, w.tech_income_); save::mark(ar, "tech_income_");
        save::io(ar, w.blackout_frames_); save::mark(ar, "blackout_frames_");
        save::io(ar, w.produced_veteran_); save::mark(ar, "produced_veteran_");
        // Shroud
        save::io(ar, w.explored_); save::mark(ar, "explored_");
        save::io(ar, w.visible_); save::mark(ar, "visible_");
        save::io(ar, w.shroud_any_); save::mark(ar, "shroud_any_");
        save::io(ar, w.fog_of_war_); save::mark(ar, "fog_of_war_");   // v9: W2-GA-03 FogOfWar flag
        save::io(ar, w.map_reveal_); save::mark(ar, "map_reveal_");
        // Multi-house / victory
        save::io(ar, w.houses_); save::mark(ar, "houses_");
        save::io(ar, w.houses_configured_); save::mark(ar, "houses_configured_");
        save::io(ar, w.ally_pairs_); save::mark(ar, "ally_pairs_");       // W2-CA-10 (save v12)
        save::io(ar, w.victory_enabled_); save::mark(ar, "victory_enabled_");
        save::io(ar, w.short_game_); save::mark(ar, "short_game_");
        save::io(ar, w.house_defeated_); save::mark(ar, "house_defeated_");
        save::io(ar, w.winning_team_); save::mark(ar, "winning_team_");
        // W9-05 (save v46): diplomacy rules, the game-over latch, observer reveal.
        save::io(ar, w.allies_allowed_); save::mark(ar, "allies_allowed_");
        save::io(ar, w.ally_change_allowed_); save::mark(ar, "ally_change_allowed_");
        save::io(ar, w.game_over_); save::mark(ar, "game_over_");
        save::io(ar, w.observer_reveal_); save::mark(ar, "observer_reveal_");
        save::io(ar, w.victory_defeats_seen_); save::mark(ar, "victory_defeats_seen_");
        // Cargo / flight
        save::io(ar, w.cargo_any_); save::mark(ar, "cargo_any_");
        save::io(ar, w.cargo_events_); save::mark(ar, "cargo_events_");
        save::io(ar, w.player_orders_); save::mark(ar, "player_orders_");   // W4-SQ (save v16)
        save::io(ar, w.passive_houses_); save::mark(ar, "passive_houses_");  // W4-SQ-02 (save v16)
        save::io(ar, w.playfield_); save::mark(ar, "playfield_");       // W6 fix #2 (save v25)
        save::io(ar, w.handicap_set_); save::mark(ar, "handicap_set_");
        save::io(ar, w.handicap_fp_); save::mark(ar, "handicap_fp_");
        save::io(ar, w.handicap_rof_); save::mark(ar, "handicap_rof_");
        save::io(ar, w.house_repair_delay_); save::mark(ar, "house_repair_delay_");   // fv7c (save v40)
        save::io(ar, w.house_repair_clock_); save::mark(ar, "house_repair_clock_");
        save::io(ar, w.ai_demand_ids_); save::mark(ar, "ai_demand_ids_");   // W4-SQ-12 (save v16)
        save::io(ar, w.ai_demand_counts_); save::mark(ar, "ai_demand_counts_");
        save::io(ar, w.aircraft_any_); save::mark(ar, "aircraft_any_");
        // Core
        save::io(ar, w.units_); save::mark(ar, "units_");
        save::io(ar, w.buildings_); save::mark(ar, "buildings_");
        save::io(ar, w.passable_); save::mark(ar, "passable_");
        save::io(ar, w.water_passable_); save::mark(ar, "water_passable_");
        save::io(ar, w.beach_); save::mark(ar, "beach_");   // fv7c (save v40)
        save::io(ar, w.naval_any_); save::mark(ar, "naval_any_");
        save::io(ar, w.occ_); save::mark(ar, "occ_");
        save::io(ar, w.grid_w_); save::mark(ar, "grid_w_");
        save::io(ar, w.grid_h_); save::mark(ar, "grid_h_");
        save::io(ar, w.move_cps_); save::mark(ar, "move_cps_");
        save::io(ar, w.anims_); save::mark(ar, "anims_");
        save::io(ar, w.inf_anims_); save::mark(ar, "inf_anims_"); save::io(ar, w.inf_human_); save::io(ar, w.inf_idle_freq_);   // W3-B
        save::io(ar, w.inf_tables_); save::mark(ar, "inf_tables_");
        // W7-12-08 / W7-12-07 (v35): InfDeath of the rad / nuke / storm kills
        // and the per-tick fragment feed (presentation only).
        save::io(ar, w.rad_inf_death_); save::mark(ar, "rad_inf_death_");
        save::io(ar, w.nuke_inf_death_); save::mark(ar, "nuke_inf_death_");
        save::io(ar, w.storm_inf_death_); save::mark(ar, "storm_inf_death_");
        save::io(ar, w.frag_events_); save::mark(ar, "frag_events_");
        // Economy
        save::io(ar, w.ore_); save::mark(ar, "ore_");
        save::io(ar, w.ore_rng_); save::mark(ar, "ore_rng_");
        save::io(ar, w.types_); save::mark(ar, "types_");
        save::io(ar, w.refinery_types_); save::mark(ar, "refinery_types_");
        save::io(ar, w.lookup_types_); save::mark(ar, "lookup_types_");
        save::io(ar, w.ore_value_ore_); save::mark(ar, "ore_value_ore_");
        save::io(ar, w.ore_value_gems_); save::mark(ar, "ore_value_gems_");
        save::io(ar, w.growth_interval_); save::mark(ar, "growth_interval_");
        save::io(ar, w.growth_pct_); save::mark(ar, "growth_pct_");
        save::io(ar, w.spread_pct_); save::mark(ar, "spread_pct_");
        save::io(ar, w.purifier_bonus_); save::mark(ar, "purifier_bonus_");   // gap owner A (G-27)
        save::io(ar, w.harvest_scan_); save::mark(ar, "harvest_scan_");     // W2-SK-02 (v13)
        save::io(ar, w.harvest_income_); save::mark(ar, "harvest_income_");   // W2-SK income witness (v13)
        save::io(ar, w.house_credits_); save::mark(ar, "house_credits_");
        save::io(ar, w.harvest_events_); save::mark(ar, "harvest_events_");
        // Repair & Sell
        save::io(ar, w.repair_rules_); save::mark(ar, "repair_rules_");
        save::io(ar, w.repair_sell_any_); save::mark(ar, "repair_sell_any_");
        save::io(ar, w.repair_frame_); save::mark(ar, "repair_frame_");
        save::io(ar, w.survivor_crew_); save::mark(ar, "survivor_crew_");
        save::io(ar, w.survivor_crew_set_); save::mark(ar, "survivor_crew_set_");
        // P5 Veterancy + crates
        save::io(ar, w.vet_any_); save::mark(ar, "vet_any_");
        save::io(ar, w.vet_rules_); save::mark(ar, "vet_rules_");
        save::io(ar, w.vet_building_costs_); save::mark(ar, "vet_building_costs_");
        save::io(ar, w.vet_frame_); save::mark(ar, "vet_frame_");
        save::io(ar, w.vet_events_); save::mark(ar, "vet_events_");
        save::io(ar, w.crates_any_); save::mark(ar, "crates_any_");
        save::io(ar, w.crate_rules_); save::mark(ar, "crate_rules_");
        save::io(ar, w.crate_rng_); save::mark(ar, "crate_rng_");
        save::io(ar, w.crate_pool_); save::mark(ar, "crate_pool_");
        save::io(ar, w.crates_); save::mark(ar, "crates_");
        save::io(ar, w.crate_events_); save::mark(ar, "crate_events_");
        // P4 abilities
        save::io(ar, w.abilities_any_); save::mark(ar, "abilities_any_");
        save::io(ar, w.ability_frame_); save::mark(ar, "ability_frame_");
        save::io(ar, w.ability_rules_); save::mark(ar, "ability_rules_");
        save::io(ar, w.ability_profiles_); save::mark(ar, "ability_profiles_");
        save::io(ar, w.rad_); save::mark(ar, "rad_");
        save::io(ar, w.no_c4_types_); save::mark(ar, "no_c4_types_");
        save::io(ar, w.side_disguise_); save::mark(ar, "side_disguise_");   // v35: wave-7 W7-01-03
        save::io(ar, w.infantry_blink_disguise_); save::mark(ar, "infantry_blink_disguise_");   // v35: W7-03-04
        save::io(ar, w.ability_events_); save::mark(ar, "ability_events_");
        save::io(ar, w.fire_events_); save::mark(ar, "fire_events_");   // Gap fix C
        save::io(ar, w.combat_rng_); save::mark(ar, "combat_rng_");
        // gF MCV deploy
        save::io(ar, w.deploy_any_); save::mark(ar, "deploy_any_");
        save::io(ar, w.deploy_events_); save::mark(ar, "deploy_events_");
        save::io(ar, w.buildup_totals_); save::mark(ar, "buildup_totals_");
        save::io(ar, w.undeploy_types_); save::mark(ar, "undeploy_types_");
        save::io(ar, w.undeploy_units_); save::mark(ar, "undeploy_units_");
        save::io(ar, w.mcv_redeploys_); save::mark(ar, "mcv_redeploys_");
        save::io(ar, w.skirmish_session_); save::mark(ar, "skirmish_session_");
        save::io(ar, w.session_seed_); save::mark(ar, "session_seed_");   // Net L0 (v34)
        // G16 world depth (agent G)
        save::io(ar, w.depth_rules_); save::mark(ar, "depth_rules_");
        save::io(ar, w.garrison_any_); save::mark(ar, "garrison_any_");
        save::io(ar, w.garrison_shots_); save::mark(ar, "garrison_shots_");
        save::io(ar, w.garrison_events_); save::mark(ar, "garrison_events_");
        save::io(ar, w.entered_events_); save::mark(ar, "entered_events_");   // W2-CA-01 (save v12)
        save::io(ar, w.track_entered_); save::mark(ar, "track_entered_");
        save::io(ar, w.bridge_ovl_); save::mark(ar, "bridge_ovl_");
        save::io(ar, w.bridge_tile_land_); save::mark(ar, "bridge_tile_land_");
        save::io(ar, w.bridge_version_); save::mark(ar, "bridge_version_");
        save::io(ar, w.bridge_rng_); save::mark(ar, "bridge_rng_");
        save::io(ar, w.hbridge_); save::mark(ar, "hbridge_");   // W4-SQ-10 (save v16)
        save::io_unhashed(ar, w.deck_land_); save::mark(ar, "deck_land_");   // R-210 derived scratch (save v30)
        save::io_unhashed(ar, w.deck_amph_); save::mark(ar, "deck_amph_");   // W9-05: deck_grid() rebuilds it
        save::io_unhashed(ar, w.deck_ver_); save::mark(ar, "deck_ver_");
        save::io(ar, w.w5_); save::mark(ar, "w5_");   // wave-5 abilities (agent C)
        save::io_unhashed(ar, w.w5_hide_); save::mark(ar, "w5_hide_");   // wave-5 gap scratch (the amphib_passable_ precedent)
        save::io_unhashed(ar, w.w5_own_); save::mark(ar, "w5_own_");     // W9-05: rebuilt by w5_gap_pass(0)
        // W5-B R-062 land speed (save v20)
        save::io(ar, w.land_speed_armed_); save::mark(ar, "land_speed_armed_");
        save::io(ar, w.speed_land_); save::mark(ar, "speed_land_");
        save::io(ar, w.ramp_); save::mark(ar, "ramp_");
        save::io(ar, w.build_static_); save::mark(ar, "build_static_");   // W8-03-01 (save v42)
        save::io(ar, w.level_grid_); save::mark(ar, "level_grid_");
        save::io(ar, w.land_rules_); save::mark(ar, "land_rules_");
        save::io(ar, w.loco_rules_); save::mark(ar, "loco_rules_");   // W7 locomotion (v35)
        save::io(ar, w.crush_rules_); save::mark(ar, "crush_rules_");   // W5-B R-050 (v20)
        save::io(ar, w.elev_rules_); save::mark(ar, "elev_rules_");    // W5-B R-063 (v20)
        save::io(ar, w.w7_rules_); save::mark(ar, "w7_rules_");    // w7fix-combat (v35)
        save::io(ar, w.house_iq_); save::mark(ar, "house_iq_");
        save::io(ar, w.w7_hits_); save::mark(ar, "w7_hits_");
        save::io(ar, w.campaign_team_flags_); save::mark(ar, "campaign_team_flags_");   // fv7c (save v40)
        save::io(ar, w.wall_leak_); save::mark(ar, "wall_leak_");   // W5-A diagonal wall gaps (save v21)
        save::io(ar, w.crush_wall_); save::mark(ar, "crush_wall_");  // P4 crushable / crushed walls (save v23)
        save::io(ar, w.wall_ov_type_); save::mark(ar, "wall_ov_type_");    // W6-04-05 map wall overlays (v25; re-registered on load)
        save::io(ar, w.wall_ov_types_); save::mark(ar, "wall_ov_types_");
        save::io(ar, w.wall_ov_stage_); save::mark(ar, "wall_ov_stage_");   // W6-04-05 their damage stages
        save::io(ar, w.w6_wall_kills_); save::mark(ar, "w6_wall_kills_");   // W6-04-05 walls destroyed this frame (opened next step)
        save::io_unhashed(ar, w.crush_grid_); save::mark(ar, "crush_grid_");  // P4 derived scratch (the amphib_passable_ precedent)
        save::io_unhashed(ar, w.crush_leak_); save::mark(ar, "crush_leak_");  // W9-05: crush_grid()/crush_leak() rebuild them
        save::io(ar, w.sw_quarry_); save::mark(ar, "sw_quarry_");   // W5-A AI SW PreferredTarget (save v21)
        // v56 append-only extension. An old v55 World ends precisely here;
        // keep freshly configured rules, but never retain links from a prior
        // session when migrating it. Other section layouts are unchanged.
        if constexpr(Ar::kLoading) {
            if(ar.at_end())w.drain_.links.clear();
            else save::io(ar,w.drain_);
        } else if(w.drain_.enabled)save::io(ar,w.drain_);
        save::mark(ar,"drain_");
    }

    // --- MapTriggerSystem: every member except d_ (the parsed map data, a
    // reference re-derived from the map on load) ---
    using Trig = MapTriggerSystem::Trig;
    using Tag = MapTriggerSystem::Tag;
    template <class Ar> static void trig(Ar& ar, Trig& t) {
        save::io(ar, t.type);
        save::io(ar, t.house_override);
        save::io(ar, t.destroyed);
        save::io(ar, t.t_start);
        save::io(ar, t.t_left);
        save::io(ar, t.bits);
        save::io(ar, t.enabled);
    }
    template <class Ar> static void tag(Ar& ar, Tag& t) {
        save::io(ar, t.type);
        save::io(ar, t.trigs);
        save::io(ar, t.instance_count);
        save::io(ar, t.destroyed);
        save::io(ar, t.executing);
        save::io(ar, t.pending_delete);
    }
    template <class Ar> static void triggers(Ar& ar, MapTriggerSystem& s) {
        save::io(ar, s.difficulty_);
        save::io(ar, s.rng_);
        save::io(ar, s.frame_);
        save::io(ar, s.globals_);
        save::io(ar, s.locals_);
        save::io(ar, s.vars_changed_);
        save::io(ar, s.mt_start_);
        save::io(ar, s.mt_left_);
        save::io(ar, s.outcome_);
        save::io(ar, s.tags_);
        save::io(ar, s.tag_of_type_);
        save::io(ar, s.map_list_);
        save::io(ar, s.logic_list_);
        save::io(ar, s.house_lists_);
        save::io(ar, s.ev_house_);
        save::io(ar, s.ev_base_);
        save::io(ar, s.house_flags_);
        save::io(ar, s.log_);
        save::io(ar, s.fired_);
        save::io(ar, s.logic_fired_);
        save::io(ar, s.house_fired_);
        save::io(ar, s.variable_fired_);
        save::io(ar, s.raise_depth_kind_);
        save::io(ar, s.force_depth_);
        save::io(ar, s.cur_trigger_);
        save::io(ar, s.campaign_mode_);   // P10
        save::io(ar, s.semi_detached_);   // P10
        save::io(ar, s.timer_text_);      // W6-05-04 (save v25)
    }
    static std::size_t trig_arity() { return save::aggregate_arity<Trig>(); }
    static std::size_t tag_arity() { return save::aggregate_arity<Tag>(); }
    static std::size_t house_ai_arity() { return save::aggregate_arity<HouseAiRec>(); }
    static std::size_t house_sw_arity() { return save::aggregate_arity<HouseSuperweapon>(); }
    static std::size_t tech_income_arity() { return save::aggregate_arity<TechIncome>(); }
    static std::size_t harvest_event_arity() { return save::aggregate_arity<PendingHarvestEvent>(); }
};

namespace save {

// Private-state forwarding (ADL on Ar finds these).
template <class Ar> void fields(Ar& ar, Occupancy& v) { SaveAccess::occupancy(ar, v); }
template <class Ar> void fields(Ar& ar, OreField& v) { SaveAccess::ore_field(ar, v); }
template <class Ar> void fields(Ar& ar, ProductionQueues& v) { SaveAccess::queues(ar, v); }
template <class Ar> void fields(Ar& ar, GameRandom& v) { SaveAccess::game_random(ar, v); }
template <class Ar> void fields(Ar& ar, RadField& v) { SaveAccess::rad_field(ar, v); }
template <class Ar> void fields(Ar& ar, SaveAccess::HouseAiRec& v) { SaveAccess::house_ai(ar, v); }
template <class Ar> void fields(Ar& ar, SaveAccess::HouseSuperweapon& v) { SaveAccess::house_sw(ar, v); }
template <class Ar> void fields(Ar& ar, SaveAccess::AnWorld& v) { SaveAccess::an_world(ar, v); }   // W2-AN
template <class Ar> void fields(Ar& ar, SaveAccess::TechIncome& v) { SaveAccess::tech_income(ar, v); }
template <class Ar> void fields(Ar& ar, SaveAccess::PendingHarvestEvent& v) { SaveAccess::harvest_event(ar, v); }
template <class Ar> void fields(Ar& ar, SaveAccess::Trig& v) { SaveAccess::trig(ar, v); }
template <class Ar> void fields(Ar& ar, SaveAccess::Tag& v) { SaveAccess::tag(ar, v); }

// --- small leaf structs ---
template <class Ar> void fields(Ar& ar, SwParams& v) {   // W2-AN (save v9)
    io(ar, v.nuke_up_frames); io(ar, v.nuke_down_frames); io(ar, v.nuke_rad_level);
    io(ar, v.iron_frames); io(ar, v.chrono_delay_frames); io(ar, v.storm_deferment);
    io(ar, v.storm_damage); io(ar, v.storm_duration); io(ar, v.storm_hit_delay);
    io(ar, v.storm_scatter_delay); io(ar, v.storm_cell_spread); io(ar, v.storm_separation);
    io(ar, v.storm_wh_spread); io(ar, v.storm_wh_pmax); io(ar, v.storm_verses);
    io(ar, v.amer_para_inf); io(ar, v.amer_para_num); io(ar, v.ally_para_inf);
    io(ar, v.ally_para_num); io(ar, v.sov_para_inf); io(ar, v.sov_para_num);
    io(ar, v.paradrop_radius); io(ar, v.cloak_delay_frames); io(ar, v.ship_sinking_weight);
    io(ar, v.reload_frames); io(ar, v.flight_level); io(ar, v.rockets);   // W4 (v15)
    io(ar, v.repair_step);
    io(ar, v.repair_rate_frames); io(ar, v.repair_percent);
    io(ar, v.parachute_fall);   // W4-SQ-06 (save v16)
    io(ar, v.storm_wall_flags); io(ar, v.nuke_wall_flags);   // v25: W6-04-04
    io(ar, v.c4_verses); io(ar, v.c4_spread); io(ar, v.c4_pmax);   // v35: W7-05-09
}
template <class Ar> void fields(Ar& ar, SwStrike& v) {   // W2-AN (save v9)
    io(ar, v.kind); io(ar, v.house); io(ar, v.x); io(ar, v.y); io(ar, v.ox); io(ar, v.oy);
    io(ar, v.frame); io(ar, v.hit_timer); io(ar, v.scatter_timer); io(ar, v.rng);
    io(ar, v.recent); io(ar, v.done);
}
template <class Ar> void fields(Ar& ar, SwEvent& v) {   // W2-AN (save v9)
    io(ar, v.seq); io(ar, v.kind); io(ar, v.house); io(ar, v.type); io(ar, v.x); io(ar, v.y);
    io(ar, v.x2); io(ar, v.y2);
}
template <class Ar> void fields(Ar& ar, AnType& v) {   // W2-AN (save v9)
    io(ar, v.naval_targeting); io(ar, v.land_targeting); io(ar, v.no_passive_acquire);
    io(ar, v.organic); io(ar, v.teleporter); io(ar, v.underwater); io(ar, v.cloakable);
    io(ar, v.sensors); io(ar, v.sensors_sight); io(ar, v.crashable); io(ar, v.weight);
    io(ar, v.min_range); io(ar, v.fixed_wing); io(ar, v.ammo); io(ar, v.fly_speed_cps);
    io(ar, v.spawned); io(ar, v.missile); io(ar, v.missile_damage); io(ar, v.spawner_slot);
    io(ar, v.spawns); io(ar, v.spawns_number); io(ar, v.spawn_regen_frames);
    io(ar, v.spawn_reload_frames); io(ar, v.landable); io(ar, v.missile_spread);
    io(ar, v.rocket_kind); io(ar, v.rocket_speed); io(ar, v.no_shadow);   // W4 (v15)
    io(ar, v.jj_accel); io(ar, v.strafe); io(ar, v.fighter);              // W7 (v35)
    io(ar, v.slowdown_distance); io(ar, v.cell_rangefinding);   // W7-05-11
    io(ar, v.airport_bound); io(ar, v.crash_weapon); io(ar, v.spawner_range);   // v35: wave 7
    io(ar, v.sonic_ambient); io(ar, v.sonic_ambient_elite); io(ar, v.distributed_fire);
    io(ar, v.radial_segments); io(ar, v.radar_invisible); io(ar, v.hover_attack);
}
template <class Ar> void fields(Ar& ar, AnUnit& v) {   // W2-AN (save v9)
    io(ar, v.t); io(ar, v.in_water); io(ar, v.submerged); io(ar, v.surface_left_frames);
    io(ar, v.detected_mask); io(ar, v.sinking); io(ar, v.iron_left_frames); io(ar, v.chrono_left_frames);
    io(ar, v.crashing); io(ar, v.ammo); io(ar, v.sortie); io(ar, v.home); io(ar, v.pad);
    io(ar, v.reload_timer); io(ar, v.attack_unit); io(ar, v.attack_building);
    io(ar, v.attack_x); io(ar, v.attack_y); io(ar, v.pass_cool); io(ar, v.spawn_ready);
    io(ar, v.spawn_out); io(ar, v.spawn_regen_timer); io(ar, v.spawn_reload_timer);
    io(ar, v.spawn_pending); io(ar, v.spawn_fire_unit); io(ar, v.spawn_fire_building);
    io(ar, v.owner); io(ar, v.landing); io(ar, v.drop_left); io(ar, v.drop_type);
    io(ar, v.drop_x); io(ar, v.drop_y); io(ar, v.exit_x); io(ar, v.exit_y);
    io(ar, v.rocket_elite); io(ar, v.rocket_end); io(ar, v.rocket);   // W4 (v15)
    io(ar, v.para_alt);   // W4-SQ-06 (save v16)
    io(ar, v.jj_speed); io(ar, v.fly_throttle); io(ar, v.strafe_left);   // W7 (v35)
    io(ar, v.sp_state); io(ar, v.sp_timer); io(ar, v.sp_unit); io(ar, v.sp_tick);   // v35: wave 7
    io(ar, v.sp_launch); io(ar, v.sp_status); io(ar, v.sp_tgt_unit); io(ar, v.sp_tgt_bld);
    io(ar, v.sp_next_unit); io(ar, v.sp_next_bld); io(ar, v.launch_hold); io(ar, v.loaner);
    io(ar, v.crash_hp); io(ar, v.naval_cell); io(ar, v.shot_list); io(ar, v.radial_counter);
    io(ar, v.para_passes); io(ar, v.para_overfly); io(ar, v.para_timer);   // v42: W8-05-07
}
template <class Ar> void fields(Ar& ar, SonicWave& v) {   // v35: W7-06-10
    io(ar, v.firer); io(ar, v.target_unit); io(ar, v.target_bld); io(ar, v.sx); io(ar, v.sy);
    io(ar, v.tx); io(ar, v.ty); io(ar, v.head); io(ar, v.tail); io(ar, v.growing);
    io(ar, v.tail_moving); io(ar, v.damage); io(ar, v.verses); io(ar, v.sonic_wh);
}
template <class Ar> void fields(Ar& ar, RocketWarhead& v) {   // W4 (v15)
    io(ar, v.id); io(ar, v.spread); io(ar, v.pmax); io(ar, v.verses);
    io(ar, v.wall_flags);   // v25: W6-04-04
}
template <class Ar> void fields(Ar& ar, RocketBlock& v) {   // W4 (v15)
    io(ar, v.pause_frames); io(ar, v.tilt_frames); io(ar, v.pitch_initial); io(ar, v.pitch_final);
    io(ar, v.turn_rate); io(ar, v.raise_rate); io(ar, v.acceleration); io(ar, v.altitude);
    io(ar, v.damage); io(ar, v.elite_damage); io(ar, v.body_length); io(ar, v.lazy_curve);
    io(ar, v.warhead); io(ar, v.elite_warhead);
}
template <class Ar> void fields(Ar& ar, RocketParams& v) { io(ar, v.block); io(ar, v.type_id); }   // W4 (v15)
template <class Ar> void fields(Ar& ar, RocketFlight& v) {   // W4 (v15)
    io(ar, v.phase); io(ar, v.timer); io(ar, v.pitch); io(ar, v.speed); io(ar, v.horiz0);
    io(ar, v.x); io(ar, v.y); io(ar, v.z); io(ar, v.yaw); io(ar, v.frames);
    io(ar, v.tx); io(ar, v.ty); io(ar, v.tz);
}
template <class Ar> void fields(Ar& ar, Cellxy& v) { io(ar, v.x); io(ar, v.y); }
template <class Ar> void fields(Ar& ar, SeqEntry& v) { io(ar, v.first); io(ar, v.count); io(ar, v.fmult); }
template <class Ar> void fields(Ar& ar, UnitAnimSeq& v) {
    io(ar, v.stand); io(ar, v.walk); io(ar, v.fire); io(ar, v.die);
}
template <class Ar> void fields(Ar& ar, OreRng& v) { io(ar, v.s); }
template <class Ar> void fields(Ar& ar, HarvestScan& v) {   // W2-SK-02 (v13)
    io(ar, v.long_scan);
    io(ar, v.short_scan);
}
template <class Ar> void fields(Ar& ar, OreMine& v) {   // W2-SK-01 (v13)
    io(ar, v.x);
    io(ar, v.y);
    io(ar, v.half_frames);
    io(ar, v.anim_rate);
    io(ar, v.anim_prob);
    io(ar, v.stage);
    io(ar, v.rate);
    io(ar, v.timer);
    io(ar, v.spawned);
}
template <class Ar> void fields(Ar& ar, ProjRng& v) { io(ar, v.s); }
template <class Ar> void fields(Ar& ar, ProjectileRules& v) {
    io(ar, v.gravity); io(ar, v.ballistic_scatter);
    io(ar, v.rot_var); io(ar, v.safety_altitude);   // W4 (v15)
}
template <class Ar> void fields(Ar& ar, Superweapon& v) {
    io(ar, v.type); io(ar, v.recharge_secs); io(ar, v.timer); io(ar, v.ready);
}
template <class Ar> void fields(Ar& ar, RepairSellRules& v) {
    io(ar, v.repair_step);
    io(ar, v.repair_percent);
    io(ar, v.repair_rate);
    io(ar, v.refund_percent);
    io(ar, v.allied_survivor_divisor);
    io(ar, v.soviet_survivor_divisor);
    io(ar, v.third_survivor_divisor);
    io(ar, v.u_repair_rate);   // v35: W7-08-03
}
template <class Ar> void fields(Ar& ar, World::HouseRec& v) {
    io(ar, v.side); io(ar, v.color_ix); io(ar, v.country); io(ar, v.alliance_team); io(ar, v.combatant);
    io(ar, v.player_control);   // W2-CA-02 (save v12)
}
template <class Ar> void fields(Ar& ar, CargoEvent& v) {
    io(ar, v.kind); io(ar, v.unit); io(ar, v.transport); io(ar, v.x); io(ar, v.y);
}

// --- projectiles ---
template <class Ar> void fields(Ar& ar, ProjectileSpec& v) {
    io(ar,v.turret_offset);
    io(ar, v.weapon_id);
    io(ar, v.kind);
    io(ar, v.arcing);
    io(ar, v.inaccurate);
    io(ar, v.flak_scatter);
    io(ar, v.inviso);
    io(ar, v.floater);
    io(ar, v.rot);
    io(ar, v.speed_cap);
    io(ar, v.acceleration);
    io(ar, v.detonation_altitude);
    io(ar, v.weapon_range);
    io(ar, v.flh);
    io(ar, v.level); io(ar, v.airburst); io(ar, v.very_high); io(ar, v.course_lock);   // W4 (v15)
    io(ar, v.subject_to_cliffs);   // v20: W5-B
    io(ar, v.subject_to_elevation);
    io(ar, v.subject_to_walls);
}
template <class Ar> void fields(Ar& ar, Projectile& v) {
    io(ar, v.weapon_id);
    io(ar, v.kind);
    io(ar, v.attacker);
    io(ar, v.target_unit);
    io(ar, v.target_building);
    io(ar, v.x); io(ar, v.y); io(ar, v.z);
    io(ar, v.vx); io(ar, v.vy); io(ar, v.vz);
    io(ar, v.tx); io(ar, v.ty); io(ar, v.tz);
    io(ar, v.speed_cap);
    io(ar, v.acceleration);
    io(ar, v.detonation_altitude);
    io(ar, v.inaccurate);
    io(ar, v.floater);
    io(ar, v.slow);
    io(ar, v.launch_frame);
    io(ar, v.launch_speed);
    io(ar, v.max_z);
    io(ar, v.weapon_slot);   // Gap fix C
    io(ar, v.attacker_building);
    // W4 (v15): homing state
    io(ar, v.uid); io(ar, v.rot); io(ar, v.speed); io(ar, v.level); io(ar, v.airburst);
    io(ar, v.very_high); io(ar, v.course_lock); io(ar, v.lock_frames); io(ar, v.locked);
    io(ar, v.giveup_n); io(ar, v.giveup_acc); io(ar, v.prev_dist); io(ar, v.lost); io(ar, v.target_air);
}
// Gap fix C: one shot event (World::FireEvent).
template <class Ar> void fields(Ar& ar, World::FragEvent& v) {   // W7-12-07 (v35)
    io(ar, v.attacker); io(ar, v.x0); io(ar, v.y0); io(ar, v.level0); io(ar, v.target);
}
template <class Ar> void fields(Ar& ar, World::FireEvent& v) {
    io(ar, v.unit); io(ar, v.building); io(ar, v.target_unit); io(ar, v.target_building);
    io(ar, v.slot); io(ar, v.weapon);
    io(ar, v.cell_x); io(ar, v.cell_y);
}
// Gap fix C: one resolved weapon slot.
template <class Ar> void fields(Ar& ar, WeaponSlot& v) {
    io(ar, v.valid);
    io(ar, v.id);
    io(ar, v.damage);
    io(ar, v.rof);
    io(ar, v.range);
    io(ar, v.verses);
    io(ar, v.cell_spread);
    io(ar, v.percent_at_max);
    io(ar, v.affects_allies);   // v20: W5-B
    io(ar, v.proj);
    io(ar, v.aa);
    io(ar, v.ag);
    io(ar, v.omni_fire);
    io(ar, v.wall);   // G16
    io(ar, v.inf_death); io(ar, v.prone_damage);   // W3-B
    io(ar, v.burst);   // v25: W6-04-01
    io(ar, v.sonic);   // v25: W6-04-09
    io(ar, v.bomb_disarm);   // v31: warhead BombDisarm=
    io(ar, v.wall_flags);   // v25: W6-04-04
    io(ar, v.shrapnel_count); io(ar, v.shrapnel_damage); io(ar, v.shrapnel_range); io(ar, v.shrapnel_verses);   // v25: W6-04-02
    io(ar, v.min_range);   // v25: W6-04-22
    io(ar, v.never_use);   // v35: W7-01-01
}
// G16 world depth (agent G)
template <class Ar> void fields(Ar& ar, World::DepthRules& v) {
    io(ar, v.occupy_damage_mult); io(ar, v.occupy_rof_mult); io(ar, v.occupy_weapon_range);
    io(ar, v.bridge_strength); io(ar, v.bridge_destruction); io(ar, v.armed);
}
template <class Ar> void fields(Ar& ar, World::EnteredEvent& v) {   // W2-CA-01
    io(ar, v.kind); io(ar, v.unit); io(ar, v.building); io(ar, v.house); io(ar, v.consumed);
}
template <class Ar> void fields(Ar& ar, World::GarrisonEvent& v) {
    io(ar, v.kind); io(ar, v.building); io(ar, v.house);
}
template <class Ar> void fields(Ar& ar, ProjectileEvent& v) {
    io(ar, v.kind);
    io(ar, v.frame);
    io(ar, v.attacker);
    io(ar, v.x); io(ar, v.y); io(ar, v.z);
    io(ar, v.speed);
    io(ar, v.scatter_dx); io(ar, v.scatter_dy);
    io(ar, v.victim);
    io(ar, v.building);
    io(ar, v.damage);
    io(ar, v.flight_frames);
    io(ar, v.apex);
}

// --- P5 veterancy + crates ---
template <class Ar> void fields(Ar& ar, VeterancyRules& v) {
    io(ar, v.ratio);
    io(ar, v.combat);
    io(ar, v.speed);
    io(ar, v.sight);
    io(ar, v.armor);
    io(ar, v.rof);
    io(ar, v.cap);
    io(ar, v.repair_rate);
    io(ar, v.elite_flash);
}
template <class Ar> void fields(Ar& ar, WeaponStats& v) {
    io(ar, v.damage); io(ar, v.rof); io(ar, v.range); io(ar, v.verses);
    io(ar, v.omni_fire);   // v25: W6-04-06
    io(ar, v.cell_spread); io(ar, v.percent_at_max);
    io(ar, v.affects_allies);   // v20: W5-B
    io(ar, v.inf_death); io(ar, v.prone_damage);   // W3-B
    io(ar, v.burst);   // v25: W6-04-01
    io(ar, v.sonic);   // v25: W6-04-09
    io(ar, v.wall_flags);   // v25: W6-04-04
    io(ar, v.shrapnel_count); io(ar, v.shrapnel_damage); io(ar, v.shrapnel_range); io(ar, v.shrapnel_verses);   // v25: W6-04-02
}
// W3-B: an art Sequence= entry / a per-image infantry table.
template <class Ar> void fields(Ar& ar, InfSeqEntry& v) {
    io(ar, v.first); io(ar, v.count); io(ar, v.fmult); io(ar, v.facing); io(ar, v.has);
}
template <class Ar> void fields(Ar& ar, InfTable& v) {
    io(ar, v.e); io(ar, v.crawls); io(ar, v.fearless); io(ar, v.fraidycat); io(ar, v.not_human);
}
// W3-B: the infantry sequence machine state (inf_anim.hpp).
template <class Ar> void fields(Ar& ar, InfAnimState& v) {
    io(ar, v.seq); io(ar, v.stage); io(ar, v.stage_timer); io(ar, v.acc); io(ar, v.ticks);
    io(ar, v.idle_timer); io(ar, v.rng); io(ar, v.fear); io(ar, v.prone); io(ar, v.armed);
    io(ar, v.fearless); io(ar, v.fraidycat); io(ar, v.death); io(ar, v.vanish); io(ar, v.was_deployed);
    io(ar, v.prev_fire); io(ar, v.cheer);
    io(ar, v.erupt); io(ar, v.air);   // W7-02-05 / W7-01-21 (v35)
}
template <class Ar> void fields(Ar& ar, UnitVet& v) {
    io(ar, v.veterancy);
    io(ar, v.last_rank);
    io(ar, v.elite_flash);
    io(ar, v.firepower_mult);
    io(ar, v.armor_mult);
    io(ar, v.speed_mult);
    io(ar, v.recv);
    io(ar, v.vet_armor);
    io(ar, v.killed_by);
    io(ar, v.credited);
    io(ar, v.cost);
    io(ar, v.trainable);
    io(ar, v.dont_score);
    io(ar, v.self_healing);
    io(ar, v.vet_abilities);
    io(ar, v.elite_abilities);
    io(ar, v.rules_speed);
    io(ar, v.base);
    io(ar, v.has_elite);
    io(ar, v.elite);
    io(ar, v.base_speed_cps);
    io(ar, v.base_sight);
}
template <class Ar> void fields(Ar& ar, PowerupDef& v) {
    io(ar, v.weight); io(ar, v.anim); io(ar, v.naval); io(ar, v.arg);
}
template <class Ar> void fields(Ar& ar, CrateRules& v) {
    io(ar, v.minimum); io(ar, v.maximum); io(ar, v.radius_leptons); io(ar, v.regen); io(ar, v.powerups);
}
template <class Ar> void fields(Ar& ar, World::Crate& v) {
    io(ar, v.x); io(ar, v.y); io(ar, v.powerup); io(ar, v.start); io(ar, v.lifetime);
}
template <class Ar> void fields(Ar& ar, World::CrateEvent& v) {
    io(ar, v.picked); io(ar, v.applied); io(ar, v.house); io(ar, v.unit);
    io(ar, v.x); io(ar, v.y); io(ar, v.amount); io(ar, v.affected);
}
template <class Ar> void fields(Ar& ar, World::ElevationRules& v) {   // W5-B (v20)
    io(ar, v.armed); io(ar, v.increment); io(ar, v.bonus); io(ar, v.cap);
}
template <class Ar> void fields(Ar& ar, HitRec& v) {   // w7fix-combat (v35)
    io(ar, v.victim); io(ar, v.building); io(ar, v.attacker); io(ar, v.attacker_building);
    io(ar, v.attacker_house); io(ar, v.ax); io(ar, v.ay); io(ar, v.disguised); io(ar, v.incoming);
}
template <class Ar> void fields(Ar& ar, World::CombatW7Rules& v) {   // w7fix-combat (v35)
    io(ar, v.armed); io(ar, v.player_return_fire); io(ar, v.player_scatter); io(ar, v.iq_guard_area);
    io(ar, v.iq_auto_crush); io(ar, v.iq_scatter); io(ar, v.max_iq); io(ar, v.close_enough);
    io(ar, v.iq_repair_sell); io(ar, v.ai_credit_reserve);   // fv7c (save v40)
    io(ar, v.normal_targeting_delay); io(ar, v.guard_area_targeting_delay); io(ar, v.incoming);
    io(ar, v.my_eff); io(ar, v.tgt_eff); io(ar, v.tgt_special); io(ar, v.tgt_strength); io(ar, v.tgt_distance);
    io(ar, v.enemy_house_bonus); io(ar, v.call_for_help_budget); io(ar, v.base_defense_delay);
}
template <class Ar> void fields(Ar& ar, World::CrushRules& v) {   // W5-B (v20)
    io(ar, v.armed); io(ar, v.crush_leptons); io(ar, v.player_auto_crush); io(ar, v.ai_any_building);
}
template <class Ar> void fields(Ar& ar, World::LandSpeedRules& v) {   // W5-B (v20)
    for (auto& row : v.cost)
        for (float& c : row) io(ar, c);
    io(ar, v.tracked_up); io(ar, v.tracked_down); io(ar, v.wheeled_up); io(ar, v.wheeled_down);
    io(ar, v.condition_red);
}
template <class Ar> void fields(Ar& ar, LocoRules& v) {   // W7 locomotion (v35)
    io(ar, v.path_delay_frames); io(ar, v.blockage_path_delay); io(ar, v.close_enough);
    io(ar, v.condition_yellow); io(ar, v.hover_accel); io(ar, v.hover_brake); io(ar, v.hover_boost);
}
template <class Ar> void fields(Ar& ar, LocoType& v) {   // W7 locomotion (v35)
    io(ar, v.kind); io(ar, v.accelerates); io(ar, v.accel_factor); io(ar, v.decel_factor);
    io(ar, v.slowdown_distance);
}
template <class Ar> void fields(Ar& ar, UnitLoco& v) {   // W7 locomotion (v35)
    io(ar, v.t);
    io(ar, v.pct); io(ar, v.pct_desired); io(ar, v.accum);
    io(ar, v.trk); io(ar, v.trk_flags); io(ar, v.trk_idx); io(ar, v.trk_face);
    io(ar, v.trk_hx); io(ar, v.trk_hy); io(ar, v.trk_mx); io(ar, v.trk_my);
    io(ar, v.trk_two); io(ar, v.trk_in_mid); io(ar, v.trk_layer_mid); io(ar, v.trk_layer_head);
    io(ar, v.hover);
    io(ar, v.path_delay); io(ar, v.blockage); io(ar, v.temp_tries);
}
template <class Ar> void fields(Ar& ar, World::VetEvent& v) {
    io(ar, v.unit); io(ar, v.old_rank); io(ar, v.new_rank);
}

// --- P4 abilities ---
template <class Ar> void fields(Ar& ar, WeaponSpec& v) {
    io(ar, v.valid);
    io(ar, v.damage);
    io(ar, v.rof);
    io(ar, v.range);
    io(ar, v.verses);
    io(ar, v.cell_spread);
    io(ar, v.percent_at_max);
    io(ar, v.rad_level);
    io(ar, v.kind);
    io(ar, v.slot_set); io(ar, v.aa); io(ar, v.ag); io(ar, v.proj);   // wave-5 R-048
    io(ar, v.delay_kill); io(ar, v.delay_kill_frames); io(ar, v.delay_kill_at_max);   // P4 (v23)
    io(ar, v.burst);   // v25: W6-04-01
    io(ar, v.omni_fire);   // v25: W6-04-06
    io(ar, v.affects_allies); io(ar, v.prone_damage); io(ar, v.inf_death);   // v25: W6-04-03
    io(ar, v.wall_flags);   // v25: W6-04-04
}
template <class Ar> void fields(Ar& ar, AbilityProfile& v) {
    io(ar, v.fire_kind);
    io(ar, v.c4);
    io(ar, v.c4_range);
    io(ar, v.teleporter);
    io(ar, v.explodes);
    io(ar, v.death);
    io(ar, v.deployer);
    io(ar, v.deploy);
    io(ar, v.immune_radiation);
    io(ar, v.parasiteable);
    io(ar, v.warpable);
    io(ar, v.no_passive_acquire);
    io(ar, v.no_retaliate);
    io(ar, v.suppression_threshold);
    io(ar, v.ifv_mode);
    io(ar, v.gunner);
    io(ar, v.ifv_weapons);
    io(ar, v.ifv_weapons_elite);   // v25: W6-04-28
    io(ar, v.infantry);
}
template <class Ar> void fields(Ar& ar, AbilityRules& v) {
    io(ar, v.rad_duration_multiple);
    io(ar, v.rad_application_delay);
    io(ar, v.rad_level_max);
    io(ar, v.rad_level_delay);
    io(ar, v.rad_level_factor);
    io(ar, v.rad_verses);
    io(ar, v.chrono_distance_factor);
    io(ar, v.chrono_trigger);
    io(ar, v.chrono_minimum_delay);
    io(ar, v.chrono_range_minimum);
    io(ar, v.c4_delay_minutes);
    io(ar, v.ivan_damage);
    io(ar, v.ivan_timed_delay);
    io(ar, v.ivan_warhead);
}
template <class Ar> void fields(Ar& ar, RadSite& v) {
    io(ar, v.cx); io(ar, v.cy);
    io(ar, v.spread);
    io(ar, v.spread_leptons);
    io(ar, v.rad_level);
    io(ar, v.level_steps);
    io(ar, v.duration);
    io(ar, v.time_left);
    io(ar, v.timer_start);
    io(ar, v.timer_left);
    io(ar, v.alive);
}
template <class Ar> void fields(Ar& ar, World::DeployEvent& v) {   // gF
    io(ar, v.kind);
    io(ar, v.unit);
    io(ar, v.building);
    io(ar, v.house);
}
template <class Ar> void fields(Ar& ar, World::AbilityEvent& v) {
    io(ar, v.kind); io(ar, v.a); io(ar, v.b); io(ar, v.x); io(ar, v.y); io(ar, v.building);
}

// --- wave-5 abilities (agent C, abil_w5.hpp) ---
template <class Ar> void fields(Ar& ar, BldTypeW5& v) {
    io(ar, v.spy_purpose); io(ar, v.radar); io(ar, v.prism); io(ar, v.overpowerable);
    io(ar, v.gap_radius); io(ar, v.inf_self_heal); io(ar, v.unit_self_heal); io(ar, v.cloning);
    io(ar, v.psychic_radius); io(ar, v.sensors_sight); io(ar, v.delayed_fire_delay);
    io(ar, v.explodes); io(ar, v.delay_kill_eligible); io(ar, v.death);   // P4 (v23)
    io(ar, v.can_c4);   // v35: W7-01-22
}
template <class Ar> void fields(Ar& ar, UnitTypeW5& v) {
    io(ar, v.requires_stolen); io(ar, v.base_tech_level);
}
// W6 fix #2 (save v25)
template <class Ar> void fields(Ar& ar, UnitTypeTgt& v) {
    io(ar, v.rules_id); io(ar, v.insignificant); io(ar, v.legal_target); io(ar, v.immune_psionics);
    io(ar, v.guard_range);
    // w7fix-combat (save v35)
    io(ar, v.default_to_guard_area); io(ar, v.opportunity_fire); io(ar, v.selectable_combatant);
    io(ar, v.to_protect); io(ar, v.threat_posed); io(ar, v.special_threat_value);
    io(ar, v.attack_cursor_on_friendlies); io(ar, v.prevent_attack_move); io(ar, v.cost);
}
template <class Ar> void fields(Ar& ar, UnitTgt& v) {
    io(ar, v.t); io(ar, v.in_playfield); io(ar, v.area_guard);
    // w7fix-combat (save v35): the mission model
    io(ar, v.mission); io(ar, v.scan_timer); io(ar, v.opportunity); io(ar, v.overridden);
    io(ar, v.resume_x); io(ar, v.resume_y); io(ar, v.resume_area_guard); io(ar, v.home_x); io(ar, v.home_y);
    io(ar, v.amove_x); io(ar, v.amove_y); io(ar, v.help_frame); io(ar, v.team_aggressive); io(ar, v.team_suicide); io(ar, v.crush_warn);
    io(ar, v.w7_rof); io(ar, v.rof_raw); io(ar, v.house_rof); io(ar, v.house_fp); io(ar, v.rof_vet);
}
// --- wave-7 fix (units) (unit_w7.hpp), save v35 ---
template <class Ar> void fields(Ar& ar, UnitTypeW7& v) {
    io(ar, v.agent); io(ar, v.infiltrate); io(ar, v.engineer); io(ar, v.perma_disguise);
    io(ar, v.detect_disguise); io(ar, v.selectable_combatant); io(ar, v.move_to_shroud);
    io(ar, v.default_to_guard_area); io(ar, v.has_turret); io(ar, v.fake_blink); io(ar, v.fake_blink_elite);
    io(ar, v.deploy_area_fire); io(ar, v.undeploy_delay); io(ar, v.mc_capacity); io(ar, v.mc_infinite);
    io(ar, v.build_time_mult); io(ar, v.mind_control);
    io(ar,v.bunkerable); io(ar,v.magnetic);
    io(ar,v.overload_count); io(ar,v.overload_damage); io(ar,v.overload_frames);
}
template <class Ar> void fields(Ar& ar, UnitW7& v) {
    io(ar, v.t); io(ar, v.blink_left); io(ar, v.redisguise_left); io(ar, v.flash_seen); io(ar, v.hull_aiming);
    io(ar, v.paralysed_until); io(ar, v.host_raw_hit); io(ar, v.deploy_ready_at); io(ar, v.undeploy_at); io(ar, v.erased_by);
    io(ar, v.depot_target); io(ar, v.mc_controller); io(ar, v.mc_victims); io(ar, v.mc_shot);
    io(ar, v.overload_left); io(ar, v.mc_original_house); io(ar, v.slave_manager); io(ar, v.mc_building);
}
template <class Ar> void fields(Ar& ar, BldW7& v) {
    io(ar, v.mc_victims);
    io(ar, v.slaves_initialized); io(ar, v.slave_regen_left);
    io(ar, v.depot_next); io(ar, v.depot_client); io(ar, v.warp_unpowered); io(ar, v.garrison_target_building);
}
template <class Ar> void fields(Ar& ar, UnitAi& v) { io(ar, v.guard_since); }   // W7 (save v35)
template <class Ar> void fields(Ar& ar, BldTgt& v) {
    io(ar, v.in_playfield); io(ar, v.insignificant); io(ar, v.legal_target);
}
template <class Ar> void fields(Ar& ar, PlayfieldW6& v) {
    io(ar, v.campaign); io(ar, v.valid); io(ar, v.size_w); io(ar, v.x); io(ar, v.y); io(ar, v.w); io(ar, v.h);
}
template <class Ar> void fields(Ar& ar, BldW5& v) {
    io(ar, v.prism_stage); io(ar, v.delay); io(ar, v.supporting); io(ar, v.master);
    io(ar, v.overpowerers); io(ar, v.overpowered); io(ar, v.gap_on);
}
template <class Ar> void fields(Ar& ar, UnitProfileW5& v) {
    io(ar, v.assault); io(ar, v.assault_rof); io(ar, v.open_topped); io(ar, v.deploy_frames);
    io(ar, v.airstrike_team); io(ar, v.elite_airstrike_team); io(ar, v.airstrike_type);
    io(ar, v.airstrike_recharge); io(ar, v.elite_airstrike_recharge); io(ar, v.flare_range);
    io(ar, v.paralyzes); io(ar, v.culling);
}
template <class Ar> void fields(Ar& ar, UnitW5& v) {
    io(ar, v.bound); io(ar, v.p); io(ar, v.charging); io(ar, v.next_assault); io(ar, v.door_until);
    io(ar, v.defuse_unit); io(ar, v.defuse_bld); io(ar, v.healer); io(ar, v.heal_target); io(ar, v.mirage_tree);
    io(ar, v.base_flight_speed); io(ar, v.base_fly_cps); io(ar, v.last_hp);
    io(ar, v.airstrike_ready); io(ar, v.strike_of); io(ar, v.strike_bld);
    io(ar, v.c4_frame); io(ar, v.c4_by);
}
// --- wave-6 #4 units/weapons (unit_w6.hpp), save v25 ---
template <class Ar> void fields(Ar& ar, UnitTypeW6& v) {
    io(ar, v.burst); io(ar, v.omni_fire); io(ar, v.sonic);   // W6-04-09
    io(ar, v.bomb_disarm);   // v31: the primary warhead's BombDisarm=
    io(ar, v.open_transport_weapon); io(ar, v.air_range_bonus); io(ar, v.deployed_crushable);   // W6-04-15/16/29
    io(ar, v.can_approach_target);   // W6-04-30
    io(ar, v.omni_crusher); io(ar, v.omni_crush_resistant);   // W6-04-14
    io(ar, v.wall_flags);   // W6-04-04
    io(ar, v.decloak_to_fire);   // W6-04-25
    io(ar, v.powered_unit);   // W6-04-18
}
template <class Ar> void fields(Ar& ar, UnitW6& v) {
    io(ar, v.t); io(ar, v.burst); io(ar, v.burst_index); io(ar, v.omni_fire);
    io(ar, v.sonic); io(ar, v.sonic_hit); io(ar, v.heal_hit);   // W6-04-09
    io(ar, v.wall_flags);   // W6-04-04
    io(ar, v.unpowered);   // W6-04-18
    io(ar, v.hover_spin);   // W8-04-05 (v42)
}
template <class Ar> void fields(Ar& ar, BldW6& v) {
    io(ar, v.burst_index);
    io(ar, v.wall_stage);   // W6-04-04
    io(ar, v.def_target_building);   // W6-04-13
}
template <class Ar> void fields(Ar& ar, BldTypeW6& v) {
    io(ar, v.wall_levels);
    io(ar, v.powers_unit);   // W6-04-18
    io(ar, v.gate);          // R-210 (save v30)
}
template <class Ar> void fields(Ar& ar, WallOvType& v) {
    io(ar, v.strength); io(ar, v.levels); io(ar, v.wood);
}
template <class Ar> void fields(Ar& ar, RulesW5& v) {
    io(ar, v.prism_support_modifier); io(ar, v.prism_support_max); io(ar, v.prism_support_delay);
    io(ar, v.prism_support_duration); io(ar, v.self_heal_inf_frames); io(ar, v.self_heal_inf_amount);
    io(ar, v.self_heal_unit_frames); io(ar, v.self_heal_unit_amount);
    io(ar, v.open_topped_range_bonus); io(ar, v.open_topped_damage_mult); io(ar, v.mirage_trees);
}
template <class Ar> void fields(Ar& ar, WorldW5& v) {
    io(ar, v.stolen_tech); io(ar, v.armed); io(ar, v.rules); io(ar, v.profiles); io(ar, v.frame);
    io(ar, v.gap_created); io(ar, v.passenger_shots); io(ar, v.prism_supports); io(ar, v.defusals); io(ar, v.repairs);
    io(ar, v.ai_garrisons); io(ar, v.ai_tech_captures); io(ar, v.scatters); io(ar, v.airstrikes); io(ar, v.culls); io(ar, v.ship_c4);
}

// --- unit / building types ---
template <class Ar> void fields(Ar& ar, Gattling& v) {
    io(ar, v.weapons); io(ar, v.elite_weapons);
    io(ar, v.thresholds); io(ar, v.elite_thresholds);
    io(ar, v.rate_up); io(ar, v.rate_down); io(ar, v.charge);
}
template <class Ar> void fields(Ar& ar, UnitCombat& v) {
    io(ar, v.gattling);
    io(ar, v.strength);
    io(ar, v.inf_death); io(ar, v.prone_damage);   // W3-B
    io(ar, v.armor);
    io(ar, v.image);
    io(ar, v.damage);
    io(ar, v.rof);
    io(ar, v.range);
    io(ar, v.verses);
    io(ar, v.cell_spread);
    io(ar, v.percent_at_max);
    io(ar, v.affects_allies);   // v20: W5-B
    io(ar, v.proj);
    io(ar, v.rot);
    io(ar, v.turret);
    io(ar, v.speed_cps);
    io(ar, v.speed_type);   // v20: W5-B
    io(ar, v.auto_crush);
    io(ar, v.is_infantry);
    io(ar, v.land_speed_loco);
    io(ar, v.is_vehicle);
    io(ar, v.passengers);
    io(ar, v.size_limit);
    io(ar, v.size);
    io(ar, v.is_aircraft);
    io(ar, v.cruise_altitude);
    io(ar, v.climb_rate);
    io(ar, v.descend_rate);
    io(ar, v.flight_speed);
    io(ar, v.flight_turn);
    io(ar, v.balloon_hover);
    io(ar, v.omni_fire);
    io(ar, v.is_naval);
    io(ar, v.movement_domain);
    io(ar, v.veteran_rank);
    io(ar, v.sight);
    // P5
    io(ar, v.cost);
    io(ar, v.trainable);
    io(ar, v.dont_score);
    io(ar, v.self_healing);
    io(ar, v.crate_goodie);
    io(ar, v.vet_abilities);
    io(ar, v.elite_abilities);
    io(ar, v.rules_speed);
    io(ar, v.has_elite);
    io(ar, v.elite);
    // Gap fix C
    io(ar, v.secondary);
    io(ar, v.elite_secondary);
    io(ar, v.primary_aa);
    io(ar, v.primary_ag);
    io(ar, v.elite_proj);
    io(ar, v.has_elite_proj);
    io(ar, v.deploy_fire);
    io(ar, v.deploy_fire_slot);
    io(ar, v.crusher);
    io(ar, v.crushable);
    io(ar, v.deploys_into);   // gF
    io(ar, v.primary_wall);   // G16
    io(ar, v.occupier);
    io(ar, v.occupy_weapon);
    io(ar, v.elite_occupy_weapon);
    io(ar, v.shrapnel_count);        // W2-SK-22 (v13)
    io(ar, v.shrapnel_damage);
    io(ar, v.shrapnel_range);
    io(ar, v.shrapnel_verses);
    io(ar, v.disguise_when_still);
    io(ar, v.an);   // W2-AN (save v9)
    io(ar, v.w6);   // v25: wave-6 #4
    io(ar, v.tg);   // W6 fix #2 (save v29)
    io(ar, v.loco);   // W7 locomotion (save v35)
    io(ar, v.w7);   // v35: wave-7 fix (units)
}
template <class Ar> void fields(Ar& ar, UnitTypeDef& v) {
    io(ar, v.id);
    io(ar, v.cost);
    io(ar, v.tech_level);
    io(ar, v.is_vehicle);
    io(ar, v.chrono_miner);
    io(ar, v.prerequisite);
    io(ar, v.cameo);
    io(ar, v.combat);
    io(ar, v.naval);       // gap owner A (G-22/G-24)
    io(ar, v.aircraft);
    io(ar, v.harvester);
    io(ar, v.storage);
    io(ar, v.w5);   // wave-5 stolen tech (agent C)
}
// W9-05: never saved (re-installed by the host after a load) -- only hashed,
// once per install, as World::econ_rules_digest_ (save::econ_rules_digest).
template <class Ar> void fields(Ar& ar, PlayerEconRules& v) {
    io(ar, v.units);
    io(ar, v.unit_anims);
    io(ar, v.unit_has_anim);
    io(ar, v.factory_types);
    io(ar, v.groups);
    io(ar, v.water_bound);
    io(ar, v.wall);
    io(ar, v.wall_fill_range);
    io(ar, v.buildup_total);
    io(ar, v.base_normal);
    io(ar, v.build_off_ally);   // W9-05-09 (w9fix-match)
    io(ar, v.ally_eligible);
    io(ar, v.bld_allowed);
    io(ar, v.unit_build_limit);
    io(ar, v.build_speed);
    io(ar, v.low_power_mod);
    io(ar, v.multiple_factory);
    io(ar, v.wall_coefficient);
    io(ar, v.max_queued);
    io(ar, v.wall_rules);
}
template <class Ar> void fields(Ar& ar, BuildingTypeDef& v) {
    io(ar, v.gattling);
    io(ar, v.mind_control);
    io(ar,v.tank_bunker); io(ar,v.bunker_damage); io(ar,v.bunker_rof); io(ar,v.bunker_range);
    io(ar, v.slave_combat); io(ar, v.slave_count); io(ar, v.slave_regen_rate); io(ar, v.slave_storage);
    io(ar,v.slave_harvest_rate);io(ar,v.slave_reload_rate);
    io(ar, v.id);
    io(ar, v.fw); io(ar, v.fh);
    io(ar, v.strength);
    io(ar, v.armor_ix);
    io(ar, v.cost);
    io(ar, v.charge);
    io(ar, v.power);
    io(ar, v.adjacent);
    io(ar, v.tech_level);
    io(ar, v.conyard);
    io(ar, v.prerequisite);
    io(ar, v.image);
    io(ar, v.buildup);
    io(ar, v.cameo);
    io(ar, v.explosion);
    io(ar, v.produce_cash_startup);
    io(ar, v.produce_cash_amount);
    io(ar, v.produce_cash_delay);
    io(ar, v.sight);
    io(ar, v.grants_map_reveal);
    io(ar, v.unsellable);
    io(ar, v.click_repairable);
    io(ar, v.crewed);
    io(ar, v.deploy_facing);   // gF
    io(ar, v.ore_purifier);   // gap owner A (G-27)
    io(ar, v.capturable);     // W2-SK-12 (v13)
    io(ar, v.spyable);        // W2-SK-20 (v13)
    io(ar, v.unit_repair);    // W2-SK-19 (v13)
    io(ar, v.factory_kind);   // W2-SK-20 (v13)
    io(ar, v.soylent);
    // Gap fix C: defense weapons
    io(ar, v.weapon);
    io(ar, v.armed);
    io(ar, v.powered);
    io(ar, v.rot);
    io(ar, v.turret);
    io(ar, v.guard_range);
    io(ar, v.can_be_occupied);   // G16
    io(ar, v.can_occupy_fire);
    io(ar, v.max_occupants);
    io(ar, v.bridge_repair_hut);
    io(ar, v.w5);   // wave-5 abilities (agent C)
    io(ar, v.wall);            // W6-03-01 (v25)
    io(ar, v.insignificant);   // W6-03-01 (v25)
    io(ar, v.w6);   // v25: W6-04-04 wall damage levels
}

// --- the live objects ---
template <class Ar> void fields(Ar& ar, Unit& v) {
    io(ar, v.gattling);
    io(ar, v.cell_x); io(ar, v.cell_y); io(ar, v.level); io(ar, v.frame); io(ar, v.image);
    io(ar, v.path);
    io(ar, v.path_idx);
    io(ar, v.fx); io(ar, v.fy);
    io(ar, v.anim_time);
    io(ar, v.wait_time);
    io(ar, v.facing);
    // combat
    io(ar, v.house);
    io(ar, v.hp); io(ar, v.max_hp);
    io(ar, v.armor_ix);
    io(ar, v.w_damage);
    io(ar, v.w_rof);
    io(ar, v.w_range);
    io(ar, v.w_verses);
    io(ar, v.w_cell_spread);
    io(ar, v.w_percent_at_max);
    io(ar, v.w_affects_allies);   // v20: W5-B
    io(ar, v.w_mind_control);     // v24: campaign mind-control shot (re-armed each frame)
    io(ar, v.spot);
    io(ar, v.sub_x);
    io(ar, v.sub_y);
    io(ar, v.speed_type);
    io(ar, v.speed_mult);
    io(ar, v.auto_crush);   // W5-B R-050
    io(ar, v.crushing);
    io(ar, v.is_infantry);
    io(ar, v.land_speed_loco);
    io(ar, v.w_proj);
    io(ar, v.target);
    io(ar, v.target_building);
    io(ar, v.attack_order);
    io(ar, v.rearm_frames);
    io(ar, v.fire_flash);
    io(ar, v.w_inf_death); io(ar, v.w_prone_damage); io(ar, v.ia);   // W3-B
    io(ar, v.guard_x); io(ar, v.guard_y);
    io(ar, v.alive);
    io(ar, v.die_time);
    // vehicles
    io(ar, v.is_vehicle);
    io(ar, v.bunker_exit_host);
    io(ar, v.body_facing);
    io(ar, v.turret_facing);
    io(ar, v.rot_u16_per_sec);
    io(ar, v.speed_cps);
    // economy
    io(ar, v.is_harvester);
    io(ar, v.chrono_miner);
    io(ar, v.storage_bales);
    io(ar, v.bales_ore); io(ar, v.bales_gems);
    io(ar, v.harv);
    io(ar, v.harv_cell_x); io(ar, v.harv_cell_y);
    io(ar, v.has_harv_cell);
    io(ar, v.dock_refinery);
    io(ar, v.harv_timer);
    io(ar, v.manual_hold);
    io(ar, v.harv_skip_x);   // W2-SK-04 (v13)
    io(ar, v.harv_skip_y);
    // cargo
    io(ar, v.passengers_max);
    io(ar, v.size_limit);
    io(ar, v.size);
    io(ar, v.cargo);
    io(ar, v.cargo_of);
    io(ar, v.enter_target);
    io(ar, v.unload_pending);
    io(ar, v.eject_counter);
    // aircraft
    io(ar, v.is_aircraft);
    io(ar, v.altitude);
    io(ar, v.flight_state);
    io(ar, v.cruise_altitude);
    io(ar, v.climb_rate);
    io(ar, v.descend_rate);
    io(ar, v.flight_speed);
    io(ar, v.flight_turn);
    io(ar, v.balloon_hover);
    io(ar, v.omni_fire);
    io(ar, v.has_flight_goal);
    io(ar, v.flight_goal_x); io(ar, v.flight_goal_y);
    // naval
    io(ar, v.is_naval);
    io(ar, v.movement_domain);
    // capture / spy
    io(ar, v.capture_target);
    io(ar, v.infiltrate_target);
    io(ar, v.infiltrate_purpose);
    io(ar, v.infiltrate_steal_pct);
    io(ar, v.infiltrate_blackout);
    io(ar, v.veteran_rank);
    // shroud
    io(ar, v.sight);
    io(ar, v.seen_by_mask);
    // disguise
    io(ar, v.disguised);
    io(ar, v.disguised_as_type);
    io(ar, v.disguised_as_house);
    io(ar, v.detect_disguise);
    io(ar, v.vet);   // P5
    // P4 abilities
    io(ar, v.abil);
    io(ar, v.abil_bound);
    io(ar, v.frozen);
    io(ar, v.warp_until);
    io(ar, v.ability_fired_unit);
    io(ar, v.ability_fired_building);
    io(ar, v.parasite_on_me);
    io(ar, v.parasite_next_tick);
    io(ar, v.suppress_until);
    io(ar, v.last_hp);
    io(ar, v.temporal_target);
    io(ar, v.temporal_target_building);
    io(ar, v.warp_remaining);
    io(ar, v.bomb_frame);
    io(ar, v.bomb_owner);
    io(ar, v.bomb_house);
    io(ar, v.deployed);
    io(ar, v.deploy_next_fire);
    io(ar, v.ifv_current);
    io(ar, v.death_done);
    // Gap fix C: weapon slots / deploy-fire / crush
    io(ar, v.w2);
    io(ar, v.w2_elite);
    io(ar, v.p_aa); io(ar, v.p_ag);
    io(ar, v.p_proj);
    io(ar, v.p_proj_elite);
    io(ar, v.p_proj_elite_set);
    io(ar, v.w_active);
    io(ar, v.w_aa); io(ar, v.w_ag);
    io(ar, v.deploy_fire);
    io(ar, v.deploy_fire_slot);
    io(ar, v.crusher);
    io(ar, v.crushable);
    // gF MCV deploy
    io(ar, v.deploys_into);
    io(ar, v.deploy_mission);
    io(ar, v.deploy_goal_x); io(ar, v.deploy_goal_y);
    io(ar, v.deployed_away);
    // G16 world depth
    io(ar, v.p_wall);
    io(ar, v.occupier);
    io(ar, v.occupy_w);
    io(ar, v.occupy_w_elite);
    io(ar, v.garrison_of);
    io(ar, v.occupy_target);
    io(ar, v.attack_cell_x); io(ar, v.attack_cell_y);
    io(ar, v.cell_shot);
    io(ar, v.shrapnel_count);        // W2-SK-22 (v13)
    io(ar, v.shrapnel_damage);
    io(ar, v.shrapnel_range);
    io(ar, v.shrapnel_verses);
    io(ar, v.disguise_when_still);
    io(ar, v.still_frames);
    io(ar, v.an);   // W2-AN (save v9)
    io(ar, v.w5);   // wave-5 abilities (agent C)
    io(ar, v.w6);   // v25: wave-6 #4
    io(ar, v.tg);   // W6 fix #2 (save v29)
    io(ar, v.on_deck);   // R-210 (save v30)
    io(ar, v.loco);      // W7 locomotion (save v35)
    io(ar, v.w7);        // v35: wave-7 fix (units)
    io(ar, v.ai);        // W7-01-08 AI guard clock (save v35)
}
template <class Ar> void fields(Ar& ar, Building& v) {
    io(ar, v.gattling_charge);
    io(ar, v.ai_repairable); io(ar, v.has_been_captured);   // fv7c (save v40)
    io(ar, v.type);
    io(ar, v.cx); io(ar, v.cy);
    io(ar, v.fw); io(ar, v.fh);
    io(ar, v.house);
    io(ar, v.hp); io(ar, v.max_hp);
    io(ar, v.armor_ix);
    io(ar, v.sight);
    io(ar, v.grants_map_reveal);
    io(ar, v.state);
    io(ar, v.buildup_time);
    io(ar, v.buildup_total);
    io(ar, v.die_time);
    io(ar, v.alive);
    io(ar, v.blocking_released);
    io(ar, v.primary);
    io(ar, v.rally_x); io(ar, v.rally_y);
    io(ar, v.has_rally);
    io(ar, v.grants_superweapon);
    io(ar, v.being_repaired);
    io(ar, v.selling);
    io(ar, v.sell_time);
    io(ar, v.bunker_exit_left);
    io(ar, v.attack_order_kind); io(ar, v.attack_order_target);
    io(ar, v.attack_order_house);
    io(ar, v.attack_order_x); io(ar, v.attack_order_y); io(ar, v.attack_order_force);
    io(ar, v.sold);
    // P5
    io(ar, v.vet_recv);
    io(ar, v.armor_mult);
    io(ar, v.killed_by);
    io(ar, v.vet_credited);
    // P4 abilities
    io(ar, v.c4_applied);
    io(ar, v.c4_frame);
    io(ar, v.c4_by);
    io(ar, v.bomb_frame);
    io(ar, v.bomb_owner);
    io(ar, v.bomb_house);
    io(ar, v.warp_remaining);
    io(ar, v.ability_death_done);
    // Gap fix C: defense combat state
    io(ar, v.def_target);
    io(ar, v.def_rearm_frames);
    io(ar, v.def_turret_facing);
    io(ar, v.def_slot);
    io(ar, v.def_fire_flash);
    io(ar, v.def_offline);
    // G16 garrison
    io(ar, v.occupants);
    io(ar, v.firing_occupant);
    io(ar, v.garrison_rearm_frames);
    io(ar, v.garrison_target);
    io(ar, v.iron_left_frames);   // W2-AN-04 (save v9)
    io(ar, v.w5);          // wave-5 abilities (agent C)
    io(ar, v.power_off);   // W5-E2 power-down (save v21)
    io(ar, v.w6);          // v25: wave-6 #4
    io(ar, v.tg);          // W6 fix #2 (save v29)
    io(ar, v.w7);          // v35: wave-7 fix (units)
}

// --- production ---
template <class Ar> void fields(Ar& ar, QueueSlot& v) {
    io(ar, v.type);
    io(ar, v.stage);
    io(ar, v.step_ticks);
    io(ar, v.frames_accum);
    io(ar, v.total_frames);
    io(ar, v.spent);
    io(ar, v.ready);
    io(ar, v.charge_basis);
}
template <class Ar> void fields(Ar& ar, ProductionState& v) { io(ar, v.credits); io(ar, v.slot); }
// L2 per-house economy (save v32).
template <class Ar> void fields(Ar& ar, HouseEcon& v) {
    io(ar, v.player);
    io(ar, v.credits);
    io(ar, v.queues);
    io(ar, v.backlog);
    io(ar, v.held);
}

// --- AI-1 ---
template <class Ar> void fields(Ar& ar, DifficultyMods& v) {
    io(ar, v.cost_mul); io(ar, v.build_time_mul); io(ar, v.build_delay_min);
    io(ar, v.build_slowdown); io(ar, v.defense_target);
    io(ar, v.firepower_mul); io(ar, v.rof_mul); io(ar, v.repair_delay_min);
}
template <class Ar> void fields(Ar& ar, AiRules& v) {
    io(ar, v.power_surplus);
    io(ar, v.power_emergency);
    io(ar, v.base_size_add);
    io(ar, v.base_spacing);
    io(ar, v.refinery_ratio);
    io(ar, v.refinery_limit);
    io(ar, v.war_ratio);
    io(ar, v.war_limit);
    io(ar, v.defense_ratio);
    io(ar, v.defense_limit);
    io(ar, v.credit_reserve);
    io(ar, v.condition_red);
    io(ar, v.sw_value_conyard); io(ar, v.sw_value_factory); io(ar, v.sw_value_power);
    io(ar, v.sw_value_tech); io(ar, v.sw_value_defense); io(ar, v.sw_value_harvester);
    io(ar, v.sw_value_building); io(ar, v.sw_value_unit);
    io(ar, v.sw_value_engineer); io(ar, v.sw_value_thief);   // W6-02-09 (v25)
    io(ar, v.sw_value_mcv); io(ar, v.sw_value_apc);
    io(ar, v.extra_refineries); io(ar, v.pick_wall_pct);   // W6-03-06 (v25)
    io(ar, v.minor_super_ready);   // W6-03-05 (v25)
}
template <class Ar> void fields(Ar& ar, PlannedNode& v) {
    io(ar, v.id); io(ar, v.type); io(ar, v.cx); io(ar, v.cy); io(ar, v.building); io(ar, v.dead);
}
template <class Ar> void fields(Ar& ar, AiBaseState& v) {
    io(ar, v.difficulty);
    io(ar, v.build_clock);
    io(ar, v.build_gate);
    io(ar, v.plan);
    io(ar, v.rng);
    io(ar, v.repair_clock);
    io(ar, v.pending);
    io(ar, v.next_node_id);
    io(ar, v.raise_building); io(ar, v.raise_unit);   // W6-03-08 (v25)
}
template <class Ar> void fields(Ar& ar, AiCatalog& v) {
    io(ar, v.conyard); io(ar, v.power); io(ar, v.refinery); io(ar, v.barracks);
    io(ar, v.war_factory); io(ar, v.radar); io(ar, v.defenses); io(ar, v.wall);
    io(ar, v.tech); io(ar, v.superweapons);
    io(ar, v.naval_yard); io(ar, v.naval_yard_adjacency);   // W2-AN-16 (save v9)
}
// --- AI-2 ---
template <class Ar> void fields(Ar& ar, TeamInstance& v) {
    io(ar, v.teamtype);
    io(ar, v.state);
    io(ar, v.members);
    io(ar, v.member_types);
    io(ar, v.script_pc);
    io(ar, v.script_timer);
    io(ar, v.recruit_timer);
    io(ar, v.gather_x); io(ar, v.gather_y);
    io(ar, v.move_issued);
    io(ar, v.stall_reissues);
    io(ar, v.best_focus_d2);
    io(ar, v.no_progress_secs);
    io(ar, v.completed_success);
}
template <class Ar> void fields(Ar& ar, AiTeamState& v) {
    io(ar, v.teams); io(ar, v.check_clock); io(ar, v.spawn_clock); io(ar, v.rng); io(ar, v.teams_created);
}
template <class Ar> void fields(Ar& ar, AiTeamRules& v) {
    io(ar, v.team_delay_frames);
    io(ar, v.autocreate_time_min);
    io(ar, v.total_team_cap);
    io(ar, v.dissolve_frames);
    io(ar, v.multiplay);
    io(ar, v.credit_reserve);
    io(ar, v.infantry_reserve);
    io(ar, v.build_slowdown);
}
// --- aimd ---
template <class Ar> void fields(Ar& ar, TaskForceMember& v) { io(ar, v.count); io(ar, v.unit); }
template <class Ar> void fields(Ar& ar, TaskForce& v) {
    io(ar, v.id); io(ar, v.name); io(ar, v.group); io(ar, v.members);
}
template <class Ar> void fields(Ar& ar, ScriptAction& v) { io(ar, v.action); io(ar, v.argument); }
template <class Ar> void fields(Ar& ar, ScriptType& v) { io(ar, v.id); io(ar, v.name); io(ar, v.actions); }
template <class Ar> void fields(Ar& ar, TeamType& v) {
    io(ar, v.id);
    io(ar, v.name);
    io(ar, v.house);
    io(ar, v.taskforce);
    io(ar, v.script);
    io(ar, v.priority);
    io(ar, v.max);
    io(ar, v.full);
    io(ar, v.autocreate);
    io(ar, v.aggressive);
    io(ar, v.reinforce);
    io(ar, v.suicide);
    io(ar, v.tech_level);
    io(ar, v.is_base_defense);
    io(ar, v.waypoint);
}
template <class Ar> void fields(Ar& ar, TriggerObject& v) { io(ar, v.kind); io(ar, v.index); }
template <class Ar> void fields(Ar& ar, TriggerComparator& v) { io(ar, v.value); io(ar, v.op); }
template <class Ar> void fields(Ar& ar, AITriggerType& v) {
    io(ar, v.id);
    io(ar, v.name);
    io(ar, v.team1); io(ar, v.team2);
    io(ar, v.owner_house);
    io(ar, v.tech_level);
    io(ar, v.condition_type);
    io(ar, v.condition_object);
    io(ar, v.comparator);
    io(ar, v.starting_weight);
    io(ar, v.min_weight);
    io(ar, v.max_weight);
    io(ar, v.is_for_skirmish);
    io(ar, v.side);
    io(ar, v.base_defense);
    io(ar, v.enabled);
    io(ar, v.is_enabled);
}
template <class Ar> void fields(Ar& ar, AimdData& v) {
    io(ar, v.taskforces); io(ar, v.scripts); io(ar, v.teams); io(ar, v.triggers);
}
// --- AI-3 ---
template <class Ar> void fields(Ar& ar, AiTriggerState& v) {
    io(ar, v.weights);
    io(ar, v.times_completed);
    io(ar, v.times_executed);
    io(ar, v.rng);
    io(ar, v.triggers_fired);
    io(ar, v.last_fired);
    io(ar, v.adjustments);
    io(ar, v.last_adjusted);
    io(ar, v.last_success);
    io(ar, v.last_old_weight);
    io(ar, v.last_new_weight);
}
// --- AI config (resolved by main.cpp, saved so load needs no re-resolution) ---
template <class Ar> void fields(Ar& ar, AiTeamConfig& v) {
    io(ar, v.enabled);
    io(ar, v.aimd);
    io(ar, v.unit_roster);
    io(ar, v.inf_factory_types);
    io(ar, v.veh_factory_types);
    io(ar, v.nav_factory_types);   // W2-AN-16 (save v9)
    io(ar, v.waypoints);
    io(ar, v.house_name);
    io(ar, v.building_type_roster);
    io(ar, v.building_class);
    io(ar, v.harvesters_per_refinery);
    io(ar, v.rules);
    io(ar, v.triggers_enabled);
    io(ar, v.trigger_side);
    io(ar, v.trigger_tech_level);
}
template <class Ar> void fields(Ar& ar, AiHouseConfig& v) {
    io(ar, v.difficulty);
    io(ar, v.catalog);
    io(ar, v.ai);
    io(ar, v.mods);
    io(ar, v.roster);
    io(ar, v.buildup_totals);
    io(ar, v.groups);
    io(ar, v.build_speed);
    io(ar, v.low_power_penalty);
    io(ar, v.start_credits);
    io(ar, v.team);
    io(ar, v.multiple_factory);   // W2-SK-06 (v13)
    io(ar, v.fire_superweapons);  // W5-A IQ gate (v22)
    io(ar, v.virtual_purifiers);  // W6-03-03 (v25)
    io(ar, v.hate_delay_frames);  // W6-03-04 (v25)
    io(ar, v.auto_deploy_frames); // W7-01-08 (v35)
    io(ar, v.roster_to_protect);  // W7-04-18 (v35)
}

// --- skirmish config (the 'CONF' section: rebuilds the scene's assets) ---
template <class Ar> void fields(Ar& ar, SkirmishSlot& v) {
    io(ar, v.side); io(ar, v.color_ix); io(ar, v.country); io(ar, v.alliance_team);
    io(ar, v.combatant); io(ar, v.is_ai); io(ar, v.diff);
}
template <class Ar> void fields(Ar& ar, SkirmishConfig& v) {
    io(ar, v.map_id);
    io(ar, v.map_file);
    io(ar, v.use_map_starts);
    io(ar, v.slots);
    io(ar, v.starting_cash);
    io(ar, v.unit_count);
    io(ar, v.tech_level);
    io(ar, v.shroud);
    io(ar, v.crates);
    io(ar, v.superweapons);
    io(ar, v.short_game);
    io(ar, v.campaign_map);          // P10: campaign mission identity ("" = skirmish)
    io(ar, v.campaign_side);
    io(ar, v.campaign_difficulty);
    io(ar, v.game_speed);            // skirmish page: lobby Game Speed (-1 = unset)
    io(ar, v.mcv_redeploys);         // skirmish page: MCV Repacks (-1 = unset)
    io(ar, v.session_seed);          // Net L0 (v34): the launcher's session seed
    io(ar, v.mode_rules);            // W9-03-04 game type rules overlay (v47)
    io(ar, v.build_off_ally);        // W9-05-09 (v46)
    io(ar, v.mp_mode);               // W9-05-03 (v46)
}

// --- P10 campaign runtime state (the 'HOST' section of a campaign save) ---
template <class Ar> void fields(Ar& ar, CampaignHostSave& v) {
    io(ar, v.u_tag); io(ar, v.u_cx); io(ar, v.u_cy); io(ar, v.u_hp); io(ar, v.u_team);
    io(ar, v.u_type); io(ar, v.u_flags);
    io(ar, v.u_mc_victim); io(ar, v.u_mc_orig);   // W2-CA-15
    io(ar, v.u_mission);                          // W4-SQ-05 (save v16)
    io(ar, v.b_tag); io(ar, v.b_hp); io(ar, v.b_type); io(ar, v.b_flags);
    io(ar, v.t_type); io(ar, v.t_script); io(ar, v.t_house); io(ar, v.t_pc);
    io(ar, v.t_goal_x); io(ar, v.t_goal_y); io(ar, v.t_members);
    io(ar, v.t_wait_until); io(ar, v.t_issued_frame); io(ar, v.t_created); io(ar, v.t_flags);
    io(ar, v.destroyed_units); io(ar, v.destroyed_buildings); io(ar, v.last_built);
    io(ar, v.obj_frames); io(ar, v.obj_texts);
    io(ar, v.end_outcome); io(ar, v.end_flagged_at); io(ar, v.frame); io(ar, v.accum);
    io(ar, v.logged); io(ar, v.teams_created); io(ar, v.raises);
    io(ar, v.b_off); io(ar, v.house_quarry); io(ar, v.house_fire_sale);   // W5-A (save v22)
    io(ar, v.left_teams); io(ar, v.local_override);
}

// --- GameScene / UI state (the 'SCNE' section) ---------------------------
// Everything GameScene holds OUTSIDE the World that the simulation or the
// resumed screen depends on. Transient input (drag box, armed repair/sell/
// placement cursor, hover) is NOT saved -- load returns with no armed mode,
// as the original's dialog does. Anyone adding a sim-relevant GameScene member
// adds it here + to GameScene::capture_save/apply_save (main.cpp).
struct SceneSave {
    int cam_x = 0, cam_y = 0;          // Camera
    int active_tab = 0;                // sidebar tab (0..3)
    // (L2, save v32: the wallet + queues moved into the World's HouseEcon.)
    double credits_shown = 0.0;        // the rolling credits display
    QueueCat place_cat = QueueCat::Structure;  // queue a pending placement drains
    std::vector<int> bstates;          // FreeUnit grand-opening tracker
};
template <class Ar> void fields(Ar& ar, SceneSave& v) {
    io(ar, v.cam_x); io(ar, v.cam_y);
    io(ar, v.active_tab);
    io(ar, v.credits_shown);
    io(ar, v.place_cat);
    io(ar, v.bstates);
}

// R-180: GameScene's build-queue backlog (QueueCat -> unit types waiting
// behind the slot, FIFO) and the categories on hold (G-07).
struct QueueBacklogSave {
    std::map<int, std::vector<int>> backlog;
    std::vector<int> held;
};
template <class Ar> void fields(Ar& ar, QueueBacklogSave& v) {
    io(ar, v.backlog);
    io(ar, v.held);
}

// --- header ---
template <class Ar> void fields(Ar& ar, SaveHeader& v) {
    io(ar, v.description);
    io(ar, v.kind);
    io(ar, v.map_file);
    io(ar, v.map_id);
    io(ar, v.map_w); io(ar, v.map_h); io(ar, v.map_cells);
    io(ar, v.frame);
    io(ar, v.unix_time);
}

// Members the visitor for T visits (the arity tripwire's right-hand side).
template <class T> std::size_t visited_fields() {
    Counter c;
    T t{};
    fields(c, t);
    return c.n;
}

// A generic blob helper: serialize any visitable value to bytes and back.
template <class T> std::vector<std::uint8_t> to_bytes(T& v) {
    Writer w;
    io(w, v);
    return std::move(w.buf);
}
template <class T> bool from_bytes(const std::vector<std::uint8_t>& b, T& v) {
    Reader r(b);
    io(r, v);
    return r.ok() && r.at_end();
}

}  // namespace save
}  // namespace ra2engine

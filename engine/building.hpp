#pragma once
#include "gattling.hpp"
#include "civilian_frames.hpp"
// Pure building core: type defs resolved from Rules/Art by the caller,
// foundation cell math, placement validity, damage/frame selection, and
// per-frame stepping (buildup progression, death anim timing). No SDL/OS.
// Calibration: docs/re/building-render.md (§ refs inline).
#include "weapon.hpp"  // Gap fix C: defense weapon slots
#include <string>
#include <vector>
#include "occupancy.hpp"
#include "pathfind.hpp"  // Cellxy
#include "abil_w5.hpp"   // wave-5 abilities (BldTypeW5 / BldW5)
#include "unit_w6.hpp"   // wave-6 #4 (BldW6)
#include "target_w6.hpp" // W6 fix #2 (BldTgt)
#include "unit_w7.hpp"   // wave-7 fix (units) (BldW7)
#include "unit_combat.hpp"

namespace ra2engine {

class OreField;   // ore.hpp (W8-03-01 BuildMask)

// One building type's stats, resolved ONCE by the caller (main.cpp from
// Rules/Art, or a test by hand). Index into the roster vector is the
// Building::type key used everywhere.
struct BuildingTypeDef {
    bool tank_bunker = false;
    double bunker_damage = 1.3, bunker_rof = 1.3, bunker_range = 2.0;
    bool mind_control = false;
    UnitCombat slave_combat;
    int slave_count = 0;
    int slave_regen_rate = 500;
    int slave_storage = 4;
    int slave_harvest_rate = 150; // SLAV HarvestRate, game frames per bale
    int slave_reload_rate = 25;  // manager SlaveReloadRate, game frames per bale
    std::string id;                  // "GAPOWR"
    int fw = 1, fh = 1;              // Art Foundation=WxH
    int strength = 1;
    int armor_ix = 0;                // armor_index(Rules armor)
    int cost = 0;
    // Credits actually deducted by production.cpp's per-stage charge math
    // [RE-GATE APPLIED: economy.md §6a]. The binary charges the
    // FreeUnit-subtracted cost while build TIME still uses raw `cost`
    // (GAREFN/NAREFN ship a free HARV/CMIN: charge = cost - free_unit_cost,
    // e.g. 2000-1400 = 600). Resolved ONCE by the caller: `charge = cost`
    // for every type with no FreeUnit; `charge = max(0, cost -
    // free_unit_cost)` for GAREFN/NAREFN. Left at the default 0 ("unset") by
    // every caller/test that predates this phase and never touches it --
    // production.cpp's start() treats charge == 0 as "use cost", so those
    // callers are byte-identical without modification (see QueueSlot::
    // charge_basis in production.hpp).
    int charge = 0;
    int power = 0;                   // + output, - drain
    int adjacent = 2;
    int tech_level = 0;
    bool conyard = false;
    std::vector<std::string> prerequisite;  // uppercased tokens (may be groups)
    std::string image;               // SHP image name (== id for all 8)
    std::string buildup;             // Art Buildup= ("" = place instantly Idle)
    std::string cameo;               // Art Cameo= ("" = never in sidebar)
    std::string explosion;           // Rules explosion_first(id)
    // Engineer/Spy Capture [RE: capture.md §D1]: neutral tech-structure periodic
    // income ([CAOILD] ProduceCashStartup=1000, ProduceCashAmount=20 every
    // ProduceCashDelay=100 frames). 0 for every non-tech building (the keys are
    // absent from their rules sections) -> byte-identical for all prior types.
    int produce_cash_startup = 0;    // Rules ProduceCashStartup (granted on capture-from-neutral)
    int produce_cash_amount = 0;     // Rules ProduceCashAmount (per-tick credit)
    int produce_cash_delay = 0;      // Rules ProduceCashDelay (frames between ticks)
    // Shroud/Fog phase [RE: shroud.md §2]: reveal radius in cells (clamped 10).
    int sight = 0;
    // Shroud/Fog phase [RE: shroud.md §7A]: this building type GRANTS its house a
    // whole-map reveal (SpySat/GPS, Allied GASPYSAT SpySat=yes). Inert default
    // (false) -> not read by world_hash. If GASPYSAT is absent from the roster
    // the mechanism ships inert until such a building exists (RE-GATE item 2).
    bool grants_map_reveal = false;
    // --- Repair & Sell phase [RE: repair-sell.md §A4/§B1/§B3/§B5] ---
    // Absent from every stock building section EXCEPT ClickRepairable, whose
    // recovered default is YES [§A4]. Resolved by load_building_assets.
    bool unsellable       = false;  // Unsellable=      [§B5] [RE-GATE APPLIED §D0: CAOILD
                                    // DOES set Unsellable=yes -- the hunt's "no stock
                                    // building sets it" was WRONG. Verified in rulesmd.]
    bool click_repairable = true;   // ClickRepairable= [§A4] DEFAULT YES
    bool crewed           = false;  // Crewed=          [§B3] gates survivor spawn
    // gF [RE: docs/re/mcv-deploy.md]: BuildingType DeployFacing= (+0xEDC,
    // ctor default 0x80, read as /32*32 by FUN_0045FE40) -- the facing an
    // MCV turns to before it unfolds into this type.
    int  deploy_facing    = 0x80;
    bool ore_purifier     = false;  // OrePurifier=     gap owner A (G-27) [RE: economy.md §2]
    // --- W2-SK-12/19/20 (save v13) ---
    bool capturable       = false;  // Capturable=  (rulesmd: absent = no; walls/defences no)
    bool spyable          = false;  // Spyable=     (absent = no)
    bool unit_repair      = false;  // UnitRepair=  (GADEPT/NADEPT service depot)
    int  factory_kind     = 0;      // Factory=: 0 none, 1 infantry, 2 vehicle, 3 naval, 4 aircraft
    int  soylent          = 0;      // Soylent=         [§B1] absolute refund override
                                    // [RE-GATE APPLIED §D0: GAREFN/NAREFN set Soylent=300,
                                    // so this override is load-bearing, not dead code.]
    // --- Gap fix C: base defenses [RE: docs/re/defenses.md] ---
    // Resolved by fill_building_weapons (engine/weapon_resolve.cpp). All inert
    // by default (armed=false -> World::step_building_combat skips the type).
    WeaponSlot weapon[2];           // Primary / Secondary (TechnoType slot rule)
    bool armed = false;             // weapon[0] valid with damage or range
    bool powered = false;           // Powered= (goes offline at low power)
    int rot = 0;                    // ROT= (turret slew rate)
    bool turret = false;            // Turret= (must face before firing)
    double guard_range = 0.0;       // GuardRange= (cells; 0 = weapon range)
    // --- G16 world depth (agent G) [RE: docs/re/world-depth.md] ---
    bool can_be_occupied = false;   // CanBeOccupied=     (BuildingType +0x157B)
    bool can_occupy_fire = false;   // CanOccupyFire=     (+0x157C)
    bool grinding = false;          // Grinding= (Yuri Grinder consumes friendly infantry/vehicles)
    bool bio_reactor = false;       // Yuri Bio Reactor's hardcoded garrison/power bonus
    int max_occupants = 0;          // MaxNumberOccupants= (+0x1580)
    bool bridge_repair_hut = false; // BridgeRepairHut=   (+0x16B6)
    // W6-03-01 [RE: rulesmd [OverlayTypes] 3=GAWALL / 27=NAWALL -- a placed
    // Wall=yes type becomes an OVERLAY in the original, never a BuildingClass,
    // so neither the ShortGame last-building count nor any TeamClass quarry
    // scan sees it; Insignificant=yes (ObjectType +0x232: walls, tech and
    // civilian structures, lamps) never keeps a house alive either].
    bool wall = false;              // Wall=
    bool insignificant = false;     // Insignificant=
    BldTypeW5 w5;                   // wave-5 abilities (abil_w5.hpp)
    BldTypeW6 w6;                   // wave-6 #4 (unit_w6.hpp): wall damage levels
    Gattling gattling;
};

// A placed building instance.
struct Building {
    int type = -1;                   // index into the roster
    int cx = 0, cy = 0;              // foundation ORIGIN cell [RE: building-render.md §2]
    int fw = 1, fh = 1;              // copied from the type at placement
    int house = 0;
    int hp = 1, max_hp = 1;
    int armor_ix = 0;                // armor_index(Rules armor); set by World::add_building
    // Shroud/Fog phase [RE: shroud.md §2]: reveal radius in cells (clamped 10).
    int sight = 0;
    // Shroud/Fog phase [RE: shroud.md §7A]: this building GRANTS its house a
    // whole-map reveal (SpySat/GPS). Copied from the type at placement. Inert
    // default (false) -> not read by world_hash, so every existing gate is
    // byte-identical; drives the map_reveal_ latch in step_visibility.
    bool grants_map_reveal = false;
    enum class State { Buildup, Idle, Dead };
    State state = State::Buildup;
    double buildup_time = 0.0;       // seconds elapsed in Buildup
    double buildup_total = 0.0;      // seconds Buildup lasts (0 = instant)
    double die_time = -1.0;          // >=0: death anim playing
    bool alive = true;               // tombstone after death anim (never erased)
    // World-owned bookkeeping (NOT touched by building.cpp): true once World
    // has restored passable_ for this building's foundation cells after it
    // died, so death-triggered unblocking happens exactly once.
    bool blocking_released = false;
    // --- Production phase: factory rally/primary [RE: production.md §1] ---
    // NOT read by world_hash (which hashes only type/cx/cy/hp/state), so
    // --demo-base stays hash=5273b5e71ca009e1 (same discipline Economy used
    // adding fields to Unit). Default-initialized; only unit-factory buildings
    // ever set them.
    bool primary = false;            // this house's primary factory of its category
    int rally_x = 0, rally_y = 0;    // rally-point cell (valid iff has_rally)
    bool has_rally = false;          // a rally point was set for this factory
    // --- Engineer/Spy Capture phase [RE: capture.md §A3] ---
    // True iff this building GRANTS its house a superweapon (a SW-building).
    // Inert default (false) -> not read by world_hash (which hashes only
    // type/cx/cy/hp/state), so every existing gate is byte-identical. Set by the
    // caller when it spawns a SW-granting building; capture_building moves the
    // house's SW grant when such a building is captured (transfer_superweapon).
    bool grants_superweapon = false;
    // --- Repair & Sell phase [RE: repair-sell.md §A2/§B2] ---
    // ALL FOUR are inert by default and are NOT read by world_hash (which folds
    // only type/cx/cy/hp/state), so every existing gate is byte-identical. They
    // are set ONLY by World::set_repair / World::begin_sell, which no existing
    // demo calls.
    bool   being_repaired = false;  // [§A2 bldg+0x6E8] the per-building wrench toggle
    // fv7c [RE: FUN_00450620 AI repair gate `HasBeenCaptured || +0x6CB`]:
    // AIRepairable (+0x6CB) -- [Structures] field 16 (reader FUN_0044F810),
    // true for an AI-placed building (FUN_00440500) and a non-human MCV deploy
    // (FUN_007383E0); has_been_captured is set by a capture.
    bool   ai_repairable = false;
    bool   has_been_captured = false;
    bool   selling        = false;  // [§B2] mission 0x13 (Selling) is running
    double sell_time      = 0.0;    // seconds elapsed in the MIRRORED buildup [§B2]
    int bunker_exit_left = -1; // lowering timer; -1 no player evacuation
    // Explicit human/network attack order, independent of automatic acquisition.
    int attack_order_kind = 0; // 0 none, 1 unit, 2 building, 3 ground cell
    int attack_order_target = -1;
    int attack_order_house = -1; // cancel if the structure changes hands
    int attack_order_x = 0, attack_order_y = 0;
    bool attack_order_force = false;
    bool   sold           = false;  // died via sell, not combat (suppresses the explosion)
    // --- P5 Veterancy + Crates [RE: veterancy.md V3/V6/C6] --- ALL inert:
    // vet_recv is set only by a vet-enabled World; killed_by is a plain stamp
    // combat.cpp writes at the kill (read only by the firewalled credit pass).
    bool   vet_recv       = false;  // receive-damage hook armed (ArmorMultiplier)
    double armor_mult     = 1.0;    // ArmorMultiplier (Armor crate)
    int    killed_by      = -1;     // unit index of the killer
    bool   vet_credited   = false;  // kill credit resolved
    // --- P4 Unit special abilities [RE: abilities.md §C2/§D/§E] ---
    // ALL inert by default and NOT read by world_hash; set only by the P4
    // ability pass (World::step_abilities), which no pre-P4 gate arms.
    bool c4_applied = false;         // [§D1 +0x6DF] C4 planted (also refuses Sell, §D3)
    long long c4_frame = -1;         // [§D1 +0x528] detonation frame
    int c4_by = -1;                  // [§D1 +0x540] the planting infantry (unit index)
    long long bomb_frame = -1;       // [§E] Ivan bomb DetonationFrame
    int bomb_owner = -1;             // the planting unit
    int bomb_house = -1;
    int warp_remaining = 0;          // [§C2] being erased by a legionnaire
    bool ability_death_done = false; // bomb chain handled on death
    // --- Gap fix C: base-defense combat state (World::step_building_combat).
    // Inert for unarmed types; NOT read by world_hash.
    int def_target = -1;             // unit index being engaged, -1 none
    int def_rearm_frames = 0;        // whole frames until the weapon may fire again (Net L0)
    double def_turret_facing = 32768.0;  // u16 turret facing (Turret=yes types)
    int def_slot = 0;                // weapon slot of the last shot
    double def_fire_flash = 0.0;     // seconds of muzzle flash left (renderer)
    bool def_offline = false;        // Powered=yes and the house is low on power
    // --- G16 garrison (agent G, world_depth.cpp) --- BuildingClass::Occupants
    // (+0x688) / FiringOccupantIndex (+0x69C). Inert; NOT read by world_hash.
    std::vector<int> occupants;      // unit indices, in entry order
    int firing_occupant = 0;
    int garrison_rearm_frames = 0;   // whole frames (Net L0)
    int garrison_target = -1;
    // W2-AN-04: Iron Curtain invulnerability left (seconds); 0 = none.
    int iron_left_frames = 0;        // Iron Curtain frames left (Net L0)
    // W5-E2 [RE: BuildingClass +0x660 HasPower; GoOffline FUN_00452350 /
    // GoOnline FUN_00452250]: powered down by the owner (TS-era power-toggle
    // mode, trigger TurnOff/TurnOn). A powered-down building draws no power
    // and does not work (defences go offline). Inert default (false).
    bool power_off = false;
    BldW5 w5;                        // wave-5 abilities (abil_w5.hpp)
    BldW6 w6;                        // wave-6 #4 units/weapons (unit_w6.hpp)
    BldTgt tg;                       // W6 fix #2: IsInPlayfield + Insignificant/LegalTarget (save v29)
    BldW7 w7;                        // wave-7 fix (units): unit_w7.hpp (save v35)
    int gattling_charge = 0;
};

// Foundation origin convention [RE: building-render.md §2, pre-hunt]: the
// origin cell (cx, cy) is the foundation's minimum-(x, y) corner in raw iso
// cell space; the building occupies (cx+i, cy+j) for i in [0,fw), j in [0,fh).
std::vector<Cellxy> foundation_cells(int cx, int cy, int fw, int fh);

// Per-cell placement feedback for the cursor grid: one flag per foundation
// cell in foundation_cells order; true = that cell individually OK
// (in-bounds + passable + unoccupied + no building). The ADJACENCY test is
// all-or-nothing and reported separately.
struct PlacementCheck {
    bool ok = false;                  // all cells OK AND adjacency satisfied
    bool adjacency = false;           // within Adjacent= range of an own building
    std::vector<bool> cell_ok;        // per foundation cell
};

// Validity [RE: construction.md §9, pre-hunt]: every foundation cell must be
// in-grid, passable (grid value 1), free in `occ` (no unit), and not overlap
// any ALIVE building's foundation. Adjacency: the candidate foundation rect
// expanded by `adjacent` cells (Chebyshev) must intersect the foundation
// rect of at least one alive, same-house, non-Dead building. `bypass_adjacency`
// (scripted spawns: the very first ConYard has no neighbors) skips ONLY the
// adjacency test, never the cell tests.
// W8-03-01 [RE: CellClass::CanThisExistHere FUN_0047C530, reached from the
// MCV deploy legality FUN_007154F0 and the placement test]: in play a
// building (speed type -1) is refused on a cell that
//   - carries ANY overlay (+0x44 != -1; ore, gems, rocks, crates, bridges...),
//     except a Wall=yes type (+0x16BF) over a Tiberium overlay
//     (FUN_005FD510 != -1), which is allowed on a flat non-bridge cell;
//   - has the bridge flag (+0x140 & 0x100) or flag 0x400;
//   - has a slope (+0x11C != 0);
//   - or whose LandType is not Buildable (the [Clear]..[Weeds] Buildable=
//     table at DAT_0089D9D0, stride 0x24).
// `build_static` is the per-cell static part (1 = LandType Buildable, flat,
// no non-ore overlay, not a bridge cell) built once from the map
// (buildable_static); the ore part is live (OreField). A WaterBound type uses
// the water grid and no mask (its own tile-set test). nullptr = the legacy
// passability-only test.
struct BuildMask {
    const std::vector<std::uint8_t>* build_static = nullptr;
    const OreField* ore = nullptr;   // live ore/gems: refuse a building, allow a wall
    bool wall = false;               // Wall=yes type: ore cells are allowed
};

// W9-05-09 [RE: FUN_004A8DC0 @0x4A8F0A]: with the session's BuildOffAlly on,
// an ALLIED building (its owner IsAlliedWith the placer, FUN_004F9870) whose
// type has EligibileForAllyBuilding=yes (BuildingType +0x1550; rulesmd: the
// ConYards GACNST / NACNST / YACNST / YACOMD) also extends the base.
// `allied_owner[h]` = 1 when house h is such an ally of the placer.
struct AllyBuild {
    const std::vector<char>* allied_owner = nullptr;   // per house
    const std::vector<char>* eligible_type = nullptr;  // per building roster index
};

PlacementCheck placement_check(int cx, int cy, int fw, int fh, int house, int adjacent,
                               const std::vector<std::uint8_t>& passable, int grid_w, int grid_h,
                               const Occupancy& occ,
                               const std::vector<Building>& buildings,
                               bool bypass_adjacency,
                               const std::vector<char>* base_normal_by_type = nullptr,
                               const BuildMask* mask = nullptr,
                               const AllyBuild* ally = nullptr);

// W8-03-01: the static buildable grid (see BuildMask). `land` is the final
// per-cell LandType (tile, then the overlay override of FUN_0047D1C0),
// `overlay_blocks` 1 where a non-ore overlay sits, `ramp` the TMP slope
// type, `bridge` 1 on a bridge cell, `buildable_land[12]` the rules
// [LandType] Buildable= flags. All grids are grid_w*grid_h (y*w+x).
std::vector<std::uint8_t> buildable_static(const std::vector<std::uint8_t>& land,
                                           const std::vector<std::uint8_t>& overlay_blocks,
                                           const std::vector<std::uint8_t>& ramp,
                                           const std::vector<std::uint8_t>& bridge,
                                           const bool buildable_land[12]);
// Gap owner A (G-28) [RE: construction.md §9 -- the neighbour must be own AND
// BaseNormal=yes; RE-E5 the +0x154F read is a pre-hunt citation]: when
// `base_normal_by_type` is given, an own building whose type has
// BaseNormal=no (walls, pillboxes, Naval Yard...) does not extend the base.
// nullptr (every legacy caller) = every own building counts.

// Frame selection for the MAIN building SHP [RE: building-render.md §4,
// pre-hunt]: image frames are the first half (second half = shadows, like
// unit SHPs); frame 0 = healthy, frame 1 = damaged, shown when
// hp/max_hp <= condition_yellow (0..1 fraction). Returns 0 when the SHP has
// fewer than 2 image frames. Only meaningful in State::Idle.
int building_frame(const Building& b, int shp_frame_count, double condition_yellow);

// Frame selection for the BUILDUP SHP during State::Buildup
// [RE-GATE APPLIED: building-render.md §5]: image frames = first half; the
// anim plays ONCE, one frame per fixed PERIOD, period = buildup_total /
// image_frames seconds (the original's per-frame delay is
// trunc(BuildupTime*900/image_frames) ticks; buildup_total is period *
// image_frames, so dividing recovers the period). frame =
// trunc(buildup_time / period), clamped to [0, image_frames-1].
int buildup_frame(const Building& b, int buildup_shp_frame_count);

// Death anim duration (the explosion SHP is drawn by main.cpp; this is the
// sim-side tombstone timer, matching the unit kDieAnimSecs pattern).
constexpr double kBuildingDieSecs = 1.0;

// Advance every building by dt: Buildup accumulates buildup_time and flips
// to Idle at buildup_total; Dead accumulates die_time and tombstones
// (alive=false) at kBuildingDieSecs. Idle does nothing. Pure; deterministic;
// ascending index. Returns indices that flipped Buildup->Idle this call (the
// caller re-computes power on those) -- Dead->tombstone needs no report (the
// caller released blocking when the building DIED, not when it tombstoned).
std::vector<int> step_buildings(std::vector<Building>& buildings, double dt);

// Kill helper shared by combat and tests: marks Dead + die_time=0 (idempotent
// -- already-Dead/dead buildings are untouched). hp clamped to 0.
void kill_building(Building& b);

}  // namespace ra2engine

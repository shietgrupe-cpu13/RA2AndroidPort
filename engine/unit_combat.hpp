#pragma once
#include <string>
#include <vector>

#include "projectile.hpp"  // ProjectileSpec (P2; pure, no World)
#include "veterancy.hpp"  // P5: AbilityMask / WeaponStats (pure, World-free)
#include "weapon.hpp"     // Gap fix C: WeaponSlot (pure)
#include "air_naval.hpp"  // W2-AN: AnType (pure)
#include "unit_w6.hpp"    // wave-6 #4: UnitTypeW6 (pure)
#include "target_w6.hpp"  // W6 fix #2 (missions): UnitTypeTgt (pure)
#include "locomotion.hpp" // W7: LocoType (pure)
#include "unit_w7.hpp"    // wave-7 fix (units): UnitTypeW7 (pure)

namespace ra2engine {

// CB-3 Naval [RE: naval.md §1]: the movement domain a unit's A* reads. Land
// (the DEFAULT -- every existing unit) resolves to the UNCHANGED passable_
// grid; Water resolves to the new water_passable_ grid. Land = 0 so a
// value-initialized/legacy Unit is Land (inert -- spec §8). Amphibious is
// reserved for a later phase (land<->water transition is out of scope).
//
// Defined HERE (not world.hpp) because UnitCombat below carries a
// MovementDomain field, and unit_combat.hpp is included by BOTH world.hpp and
// unit_type.hpp BEFORE world.hpp's own body -- so the enum must be visible at
// this point. world.hpp gets it transitively via its unit_combat.hpp include.
// W2-AN-10 [RE: naval.md §2 table; rules [LCRF]/[SAPC] MovementZone=Amphibious,
// Hover loco {4A582742}]: Amphibious = the Land OR the Water grid (beach
// crossing; the landing craft loads/unloads on the shore).
enum class MovementDomain { Land = 0, Water = 1, Amphibious = 2 };

// W4-MC-01 [RE: gamemd MovementZone name table (docs/re/movement-zones.md):
// Normal, Crusher, Destroyer, AmphibiousDestroyer, AmphibiousCrusher,
// Amphibious, Subterrannean, Infantry, InfantryDestroyer, Fly, Water,
// WaterBeach, CrusherAll]: the three Amphibious* zones path over land AND
// water; SpeedType=Amphibious (Tanya/SEAL/APC/...) is the matching cost row.
// Case-insensitive, surrounding blanks ignored ("[SAPC] MovementZone=Amphibious ").
inline bool is_amphibious_zone(const std::string& movement_zone, const std::string& speed_type) {
    auto norm = [](const std::string& v) {
        std::string o;
        for (char ch : v) {
            if (ch == ' ' || ch == 9 || ch == 13) continue;   // blank, tab, CR
            o += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
        }
        return o;
    };
    const std::string mz = norm(movement_zone);
    return mz == "amphibious" || mz == "amphibiousdestroyer" || mz == "amphibiouscrusher" ||
           norm(speed_type) == "amphibious";
}

// Per-unit combat stats resolved once by the caller (main.cpp, from Rules)
// and handed to World::add_combat_unit to fill in a spawned Unit's hp/armor/
// weapon fields. Mirrors the Rules-derived values Task 2 exposes (strength,
// armor, the primary weapon's damage/rof/range, and its warhead's verses)
// plus the SHP image name add_unit already takes directly.
struct UnitCombat {
    int strength = 1;
    std::string armor;
    std::string image;
    int damage = 0;
    int rof = 0;
    double range = 0.0;
    std::vector<int> verses;
    // --- CB-2 air-to-ground splash [RE-GATE APPLIED: aircraft.md §4 ->
    // combat.md §7] --- the primary weapon's warhead CellSpread / PercentAtMax
    // (BlimpBomb -> BlimpHE CellSpread=2 PercentAtMax=.5). Default 0 = no
    // spread (single-target). Fed to combat.cpp's apply_spread_damage, GATED on
    // cell_spread >= 1 so every existing sub-1-cell warhead keeps its EXACT
    // single-target path (the 9 gates stay byte-identical).
    double cell_spread = 0.0;      // warhead CellSpread (cells; radius = round(.))
    double percent_at_max = 1.0;   // warhead PercentAtMax (edge damage scale)
    bool affects_allies = true;    // W5-B R-060: warhead AffectsAllies=
    // W5-B R-062: SpeedType= as a GroundType Cost column (0 Foot, 1 Track,
    // 2 Wheel, 3 Hover, 4 Winged, 5 Float, 6 Amphibious, 7 FloatBeach); -1 =
    // absent (World defaults: infantry Foot, vehicles Track).
    int speed_type = -1;
    // --- Projectile phase (P2) [RE: projectile.md] --- the primary weapon's
    // projectile, resolved by main.cpp (resolve_projectile_spec + the art FLH).
    // Default kind=Instant is inert; consumed only behind World's
    // projectiles_enabled_ firewall.
    ProjectileSpec proj;
    // --- Voxel Vehicles phase (docs/re/voxel-render.md §5) ---
    int rot = 0;               // Rules ROT= (turn rate); 0 = no turning
    bool turret = false;       // has an independently-slewing turret
    double speed_cps = 0.0;    // per-unit speed in cells/sec; 0 = World move_cps
    bool is_vehicle = false;   // gates all vehicle-specific locomotion

    // --- CB-1 Transports/Cargo [RE: transport-cargo.md §1] ---
    // Resolved once from Rules by main.cpp like every existing stat; ALL
    // defaults inert (0 passengers = not a transport). Copied onto the Unit
    // at add_combat_unit.
    int passengers = 0;        // Rules Passengers= (capacity COUNT [RE-GATE
                               // APPLIED: transport-cargo.md §1])
    int size_limit = 0;        // Rules SizeLimit= (largest admissible Size --
                               // a per-passenger cap)
    int size = 1;              // Rules Size= (own bulk; pip weight only)
                               // [RE-GATE APPLIED: transport-cargo.md §1 --
                               // absent-key default 1 CONFIRMED]

    // --- CB-2 Aircraft/Flight [RE: aircraft.md] ---
    // Resolved once from Rules by main.cpp like every existing stat; ALL
    // defaults inert (is_aircraft=false = a ground unit). Copied onto the Unit
    // at add_combat_unit. Rates are per-SECOND (multiplied by dt in
    // step_flight -- the rot_u16_per_sec precedent), converted from the
    // recovered per-frame Jumpjet values at resolution time [RE-GATE APPLIED:
    // aircraft.md §1/§2/§5].
    bool is_aircraft = false;      // flight-locomotor type (Jumpjet CLSID)
    double cruise_altitude = 0.0;  // JumpjetHeight (leptons)
    double climb_rate = 0.0;       // JumpjetClimb (leptons/sec)
    double descend_rate = 0.0;     // JumpjetCrash (leptons/sec)
    double flight_speed = 0.0;     // cruise speed (cells/sec) from JumpjetSpeed
                                   // (fallback base Speed) [D2, aircraft.md §1]
    double flight_turn = 0.0;      // heading turn rate (u16/sec) from ROT x2 --
                                   // NOT JumpJetTurnRate [D1, aircraft.md §1]
    bool balloon_hover = false;    // BalloonHover: takes off once, never lands
    bool omni_fire = false;        // weapon OmniFire: fire without turning

    // --- CB-3 Naval/Water [RE: naval.md §1] ---
    // Resolved once from Rules by main.cpp like every existing stat; ALL
    // defaults inert (is_naval=false = a ground unit). Copied onto the Unit at
    // add_combat_unit. A ship reuses speed_cps/rot/damage/rof/range/verses (it
    // is a voxel vehicle, is_vehicle=true); naval adds only the domain flags.
    bool is_naval = false;                                // naval-locomotor type (CLSID)
    MovementDomain movement_domain = MovementDomain::Land;  // Water for a naval unit

    // --- Engineer/Spy Capture phase [RE-GATE APPLIED: capture.md §B5] ---
    // Veteran rank: 0 = none (byte-identical), 1 = veteran. rank>0 scales
    // firepower + HP at spawn (add_combat_unit) via veteran_firepower/veteran_hp;
    // combat.cpp is UNTOUCHED (a veteran just spawns with higher w_damage + hp).
    int veteran_rank = 0;

    // --- Shroud/Fog phase [RE: shroud.md §2] ---
    // Reveal radius in cells, from rules.sight(id) (hard-clamped to 10). Copied
    // onto Unit at add_combat_unit. Inert default 0 (a source with sight 0
    // reveals only its own cell / nothing, and is meaningless while the
    // visibility grids are empty). NOT read by world_hash.
    int sight = 0;

    // Slave workers use their own SLAV rules cadence rather than the generic
    // harvester fallback. These are immutable Rules-derived profile values,
    // not saved simulation state.
    int harvest_rate_micros = 1000000;
    int slave_reload_rate_micros = 133333;

    // --- P5 Veterancy + Crates [RE: veterancy.md V3/V4/V10/V11/V14/C8] ---
    // Resolved once by main.cpp's resolve_combat_unit like every stat above.
    // ALL defaults inert: World reads them only past the enable_veterancy
    // firewall (a hand-built UnitCombat in an existing gate never sets them,
    // and add_combat_unit merely copies them into Unit::vet).
    int cost = 0;                     // Cost= (experience value / owner cost)
    bool trainable = true;            // Trainable= (default YES, V14)
    bool dont_score = false;          // DontScore=
    bool self_healing = false;        // SelfHealing= (V11)
    bool crate_goodie = false;        // CrateGoodie= (default NO, C8)
    AbilityMask vet_abilities = 0;    // VeteranAbilities=
    AbilityMask elite_abilities = 0;  // EliteAbilities=
    int rules_speed = 0;              // integer Speed= (V8 domain)
    bool has_elite = false;           // ElitePrimary= defined (V10)
    WeaponStats elite;                // ElitePrimary's resolved stats

    // --- Gap fix C [RE: docs/re/weapons.md] --- resolved by main.cpp's
    // resolve_combat_unit from the TechnoType slot rule (Primary/Secondary or
    // Weapon1..N). All defaults inert: no secondary, primary ground-only.
    WeaponSlot secondary;             // slot 1 (Secondary=)
    WeaponSlot elite_secondary;       // elite slot 1 (EliteSecondary=)
    bool primary_aa = false;          // primary projectile AA=
    bool primary_ag = true;           // primary projectile AG=
    ProjectileSpec elite_proj;        // elite slot 0 projectile
    bool has_elite_proj = false;
    bool deploy_fire = false;         // Deployer+DeployFire infantry (GI/GGI), G-45
    int deploy_fire_slot = 1;         // DeployFireWeapon=
    bool crusher = false;             // Crusher=
    bool crushable = false;           // Crushable=
    bool auto_crush = false;          // W5-B R-050: TechnoType AutoCrush= (+0xD2D; HARV/CMIN yes)
    bool is_infantry = false;         // W5-B R-061: an [InfantryTypes] type (uses sub-cell spots)
    bool land_speed_loco = false;     // W5-B R-062: Drive/Ship locomotor (reads [LandType] speed)
    LocoType loco;                    // W7 locomotion: kind + acceleration keys (save v35)
    // gF [RE: docs/re/mcv-deploy.md]: DeploysInto= building id (AMCV->GACNST).
    std::string deploys_into;
    // --- G16 (agent G) [RE: docs/re/world-depth.md] ---
    bool primary_wall = false;        // primary warhead Wall= (bridge damage, G-49)
    bool occupier = false;            // Occupier= (may garrison CanBeOccupied buildings, G-48)
    WeaponSlot occupy_weapon;         // OccupyWeapon= (fired from inside a garrison)
    WeaponSlot elite_occupy_weapon;   // EliteOccupyWeapon=
    // --- W2-SK-22 (save v13) ---
    int shrapnel_count = 0;           // primary ShrapnelCount= (Prism Tank Comet: 5)
    int shrapnel_damage = 0;          // ShrapnelWeapon= Damage (CometFragment 30)
    double shrapnel_range = 0.0;      // ShrapnelWeapon= Range (cells)
    std::vector<int> shrapnel_verses; // ShrapnelWeapon= warhead Verses
    bool disguise_when_still = false; // CanDisguise= + DisguiseWhenStill= (Mirage Tank)
    AnType an;                        // W2-AN: naval/air/SW type data (air_naval.hpp)
    // W3-B (last: keeps the positional aggregate initialisers valid): the
    // primary warhead's InfDeath= (absent 0) and ProneDamage= (absent 1).
    int inf_death = 0;
    double prone_damage = 1.0;
    UnitTypeW6 w6;                    // wave-6 #4 type data (unit_w6.hpp)
    UnitTypeTgt tg;                   // W6 fix #2: rules id + targeting type data (target_w6.hpp)
    UnitTypeW7 w7;                    // wave-7 fix (units): unit_w7.hpp (save v35)
};

}  // namespace ra2engine

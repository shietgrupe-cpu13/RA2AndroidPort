#pragma once
// Wave-2 fix (W2-AN-*): aircraft / naval / superweapon per-unit state.
// AnType = the per-type rules data resolved once (main.cpp resolve_combat_unit)
// and carried in UnitCombat::an; AnUnit = the per-unit runtime state carried in
// Unit::an (it embeds the AnType it was spawned with). Plain aggregates, SDL-
// and World-free; the save visitors live in save_fields.hpp.
//
// Offsets below are the TechnoTypeClass::LoadFromINI stores recovered for this
// fix with a byte scan of gamemd.exe (push "<Key>" ... mov [esi+disp], al):
//   Organic +0xD97, Teleporter +0xCD4 (the Chrono Warp kill test),
//   Underwater +0xD69, Cloakable +0xCD0, Sensors +0xC9D, SensorsSight +0x5F0,
//   NavalTargeting +0x600, LandTargeting +0x604, CanPassiveAquire +0xD99,
//   Crashable +0xD95, BalloonHover +0xD6A.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ability_types.hpp"   // W7: WeaponSpec (the crash death weapon)
#include "rocket.hpp"   // W4: RocketLocomotion blocks + per-type warheads

namespace ra2engine {

// [rules.ini comment block above [E1], classic RA2]
//   NavalTargeting: 0 UNDERWATER_NEVER, 1 UNDERWATER_SECONDARY, 2 UNDERWATER_ONLY,
//                   3 ORGANIC_SECONDARY, 4 SEAL_SPECIAL, 5 NAVAL_ALL, 6 NAVAL_NONE,
//                   7 Boomer: primary torpedo on water; LandTargeting selects launcher.
//   LandTargeting:  0 LAND_OKAY, 1 LAND_NOT_OKAY   ("Defaults are 0's")
enum : int {
    kNavUnderwaterNever = 0,
    kNavUnderwaterSecondary = 1,
    kNavUnderwaterOnly = 2,
    kNavOrganicSecondary = 3,
    kNavSealSpecial = 4,
    kNavNavalAll = 5,
    kNavNavalNone = 6,
    kNavBoomer = 7,
};

struct AnType {
    int naval_targeting = 0;          // NavalTargeting=
    int land_targeting = 0;           // LandTargeting=
    bool no_passive_acquire = false;  // CanPassiveAquire=no (CARRIER, DRED)
    bool organic = false;             // Organic=
    bool teleporter = false;          // Teleporter=
    bool underwater = false;          // Underwater= (submersible)
    bool cloakable = false;           // Cloakable=
    bool sensors = false;             // Sensors=
    int sensors_sight = 0;            // SensorsSight= (cells)
    bool crashable = false;           // Crashable=
    double weight = 1.0;              // Weight= (ShipSinkingWeight test)
    double min_range = 0.0;           // primary weapon MinimumRange= (cells)
    // Fixed-wing aircraft (FlyLocomotion {4A582746}) -- ORCA/BEAG, spawned
    // HORNET/ASW, PDPLANE.
    bool fixed_wing = false;
    int ammo = 0;                     // Ammo=
    double fly_speed_cps = 0.0;       // Speed= -> cells/sec
    bool spawned = false;             // Spawned= (no player control)
    bool missile = false;             // MissileSpawn= (DMISL/V3ROCKET: a homing rocket)
    int missile_damage = 0;           // [General] DMislDamage / V3RocketDamage
    int rocket_kind = -1;             // W4: RocketKind block (FUN_00661D00 head), -1 = not a rocket
    int rocket_speed = 0;             // W4: Speed= -> min(255, Speed*256/100) leptons/frame (type+0x678)
    bool no_shadow = false;           // W4: NoShadow= (V3ROCKET / DMISL)
    // Spawner weapon (Spawner=yes): which slot (0 primary / 1 secondary, -1 none)
    // launches `spawns` instead of firing.
    int spawner_slot = -1;
    std::string spawns;               // Spawns= rules id
    int spawns_number = 0;            // SpawnsNumber=
    int spawn_regen_frames = 0;       // SpawnRegenRate= (frames to rebuild a lost spawn)
    int spawn_reload_frames = 0;      // SpawnReloadRate= (frames to rearm a returned spawn)
    // Landing-capable jumpjet (Night Hawk): !BalloonHover lands at its goal.
    bool landable = false;            // Landable=
    // Explicit death delivery for missiles / crashes: the warhead verses used by
    // a missile detonation (V3WH) and its splash.
    double missile_spread = 0.0;
    // W7-05-15 [RE: JumpjetLocomotionClass Link FUN_0054AA60]: JumpJetAccel
    // (TechnoType +0xD84, float, leptons/frame per frame); the absent-key
    // default is [JumpjetControls] Acceleration=2.
    double jj_accel = 2.0;
    // W7-05-02 [RE: AircraftClass Mission_Attack FUN_00417FE0]: Is_Strafe
    // FUN_0041B820 (primary projectile ROT <= 1 and not Inviso -- HornetBomb,
    // ASWBomb) and Is_Fighter FUN_0041B870 (Fighter=, AircraftType +0xE0E).
    bool strafe = false;
    bool fighter = false;
    // SlowdownDistance= (TechnoType +0x2F8, ctor default 500 @0x70FF52): the
    // FlyLocomotion approach throttle (FUN_004CD520).
    int slowdown_distance = 500;
    // W7-05-11 [rules [BlimpBomb] CellRangefinding=yes; WeaponType key]: the
    // primary weapon measures its range cell centre to cell centre.
    bool cell_rangefinding = false;
    // --- wave 7 (save v35) ---
    // W7-05-04 [RE: AircraftType +0xE0D, read by Enter_Idle_Mode FUN_004176F0]:
    // AirportBound=yes -- no dock found when the plane goes idle -> it crashes.
    bool airport_bound = false;
    // W7-05-09 [RE: FireDeathWeapon FUN_0070CAC0]: DeathWeapon (+0xD18), else
    // the Primary, its Damage x DeathWeaponDamageModifier (+0xD1C).
    WeaponSpec crash_weapon;
    // W7-06-03 [RE: SpawnManager AI FUN_006B68B0 status 0]: the spawner
    // weapon's range (InRange, vtbl+0x3AC -> FUN_006F6CB0).
    double spawner_range = 0.0;
    // W7-06-10 [RE: WeaponType IsSonic +0x130, AmbientDamage +0x98]: the
    // primary weapon's (and elite primary's) sonic wave damage per frame.
    int sonic_ambient = 0;
    int sonic_ambient_elite = 0;
    // W7-06-11 [RE: TechnoType DistributedFire +0x6B0, RadialFireSegments +0x6A4].
    bool distributed_fire = false;
    int radial_segments = 0;
    // W7-05-13 [RE: ObjectType RadarInvisible +0x22F; FootType HoverAttack +0x390].
    bool radar_invisible = false;
    bool hover_attack = false;
};

// W7-06-03/06 [RE: SpawnManagerClass, ctor FUN_006B6310, AI FUN_006B68B0]:
// one SpawnNode per SpawnsNumber. States as the original numbers them:
//   0 docked + armed, 1 rocket handed to the KamikazeTracker (pause + tilt),
//   2 launched (gathering by the owner / rocket on its way), 3 attacking,
//   4 returning, 6 reloading aboard (SpawnReloadRate), 7 dead (SpawnRegenRate).
enum : int {
    kSpawnDocked = 0, kSpawnRocketWait = 1, kSpawnLaunched = 2, kSpawnAttack = 3,
    kSpawnReturn = 4, kSpawnReload = 6, kSpawnDead = 7,
};

struct AnUnit {
    AnType t;
    // --- naval ---
    bool in_water = false;            // stands on a water cell (ships always)
    bool submerged = false;           // cloaked sub (Underwater+Cloakable)
    int surface_left_frames = 0;      // whole frames left surfaced after firing (CloakDelay; Net L0)
    std::uint32_t detected_mask = 0;  // bit h: house h's sonar sees this sub (fv7c: 32-bit, FUN_004F98B0 house masks)
    bool sinking = false;             // ShipSinkingWeight death (renderer)
    // --- superweapons ---
    int iron_left_frames = 0;         // Iron Curtain invulnerability left (whole frames; Net L0)
    int chrono_left_frames = 0;       // frozen after a Chrono Warp (whole frames; Net L0)
    // --- aircraft ---
    bool crashing = false;            // Crashable death: falling (renderer tilts)
    int ammo = 0;                     // fixed-wing ammo left
    int sortie = 0;                   // 0 idle/docked, 1 takeoff/outbound, 2 attack run,
                                      // 3 return to base, 4 landing, 5 parked away
    int home = -1;                    // airfield building index (-1 none)
    int pad = -1;                     // dock pad on `home`
    int reload_timer = 0;             // frames toward the next +1 ammo while docked
    int attack_unit = -1;             // sortie target (unit) ...
    int attack_building = -1;         // ... or building
    int attack_x = -1, attack_y = -1; // ... or a cell (move / strike point)
    double pass_cool = 0.0;           // seconds before the next attack pass may fire
    // --- spawner (carrier / dreadnought / destroyer ASW / V3) ---
    int spawn_ready = 0;              // spawns docked and armed
    int spawn_out = 0;                // spawns airborne
    int spawn_regen_timer = 0;        // frames toward rebuilding a lost spawn
    int spawn_reload_timer = 0;       // frames toward rearming a returned spawn
    int spawn_pending = 0;            // spawns returned, waiting to rearm
    int spawn_fire_unit = -1;         // a spawner shot this frame -> launch at ...
    int spawn_fire_building = -1;
    int owner = -1;                   // a spawned unit: its spawner unit index
    // --- Night Hawk / paradrop plane ---
    bool landing = false;             // descending to land at its goal
    int drop_left = 0;                // paradrop plane: paratroopers still aboard
    std::string drop_type;            // paradrop infantry rules id
    int drop_x = -1, drop_y = -1;     // drop site
    int exit_x = -1, exit_y = -1;     // map-edge exit after the drop
    // --- W4: RocketLocomotion (spawned V3/DMisl/CMisl) ---
    bool rocket_elite = false;        // locomotor +0x4D: the launcher was elite at launch
    int rocket_end = 0;               // 0 flying, 1 detonated (FUN_00662A70), 2 shot down (Strength 0)
    RocketFlight rocket;              // W4: the RocketLocomotion state (FUN_00661D00)
    // W4-SQ-06 (save v16): a paratrooper's height while it falls under its
    // parachute (cargo_of == kParachuteCargo); 0 on the ground.
    double para_alt = 0.0;
    // W8-05-07 (save v42) [RE: AircraftClass Mission_ParadropApproach
    // FUN_004158E0 / Mission_ParadropOverfly FUN_00415960 / FUN_00415C60]:
    // the paradrop plane's mission state. para_passes = +0x6D3 (5 from the
    // ctor FUN_00413D20 @0x413D74, -1 on each approach into ParadropRadius,
    // back to 5 on every jump; -1 here = gave up, leaving); para_overfly =
    // Overfly (else Approach); para_timer = frames until the mission runs
    // again (Approach returns 3, Overfly 5).
    int para_passes = 5;
    bool para_overfly = false;
    int para_timer = 0;
    // W7 (save v35): the locomotor speed state. jj_speed = JumpjetLocomotion
    // CurSpeed (loco +0x74, leptons/frame, FUN_0054CDA0); fly_throttle =
    // FlyLocomotion CurrentSpeed (loco +0x48, fraction of type+0x678,
    // FUN_004CD520); strafe_left = Mission_Attack strafe shots still to fire
    // (statuses 6..9, FUN_00417FE0).
    double jj_speed = 0.0;
    double fly_throttle = 0.0;
    int strafe_left = 0;
    // --- wave 7 (save v35) ---
    // W7-06-03/06/08 spawn manager: per node (state, frames left on the node
    // timer, spawned unit index), the manager's launch timer, its AI timer
    // (every 10 frames), status (+0x70: 0 idle, 1 launching, 2 out), target
    // (+0x68) and queued next target (+0x6C).
    std::vector<int> sp_state;
    std::vector<int> sp_timer;
    std::vector<int> sp_unit;
    int sp_tick = 0;
    int sp_launch = 0;
    int sp_status = 0;
    int sp_tgt_unit = -1, sp_tgt_bld = -1;
    int sp_next_unit = -1, sp_next_bld = -1;
    // W7-06-16 [RE: FUN_006B7400 from ShipLoco FUN_006A1310 @0x6A13E6]:
    // own rockets on the deck (pause + tilt) hold the launcher still.
    bool launch_hold = false;
    // W7-05-04 [RE: techno +0x3D4 set only by the reinforcement / superweapon
    // plane code 0x65D320..0x65E4F0]: a "loaner" retreats off the map instead
    // of crashing when it has nowhere to land.
    bool loaner = false;
    // W7-05-09 [RE: FlyLoco FUN_004CD520 @0x4CD6E8]: a plane that hit the
    // ground with Strength left (it crashed alive: no airfield, carrier lost)
    // -> its Strength as C4Warhead damage; 0 = it was shot down.
    int crash_hp = 0;
    // W7-06-02 [RE: GetFireError @0x6FBC86]: stands on a Water / Beach cell,
    // not airborne, not on a bridge (the target-side naval test).
    bool naval_cell = false;
    // W7-06-11 [RE: Fire FUN_006FD210 @0x6FED3C, FUN_007089D0]: targets this
    // DistributedFire unit already shot; the RadialFireSegments counter (+0x43C).
    std::vector<int> shot_list;
    int radial_counter = 0;
};

// W7-06-10 [RE: WaveClass ctor FUN_0075D770 (type 0 = sonic), AI
// FUN_0075FD70, sweep FUN_00761910 / FUN_0075FF10, cell damage
// FUN_0075E150]: one Dolphin sonic wave in flight.
struct SonicWave {
    int firer = -1;
    int target_unit = -1, target_bld = -1;
    double sx = 0, sy = 0, tx = 0, ty = 0;   // cells
    double head = 0.0, tail = 0.0;           // wave front / back along the line (0..1)
    bool growing = true;                     // +0x12C: the firer still holds its target
    bool tail_moving = false;                // +0x12D
    int damage = 0;                          // AmbientDamage
    std::vector<int> verses;                 // the primary weapon's warhead
    bool sonic_wh = false;                   // W6-04-09 Sonic warhead
};

// --- Superweapons v2 (W2-AN-04/05/06/17/20) -----------------------------------
// [rules.ini [General] + the [<Special>] sections, classic RA2]; resolved once
// by main.cpp (resolve_sw_params) and handed to World::set_sw_params.
struct SwParams {
    // Nuke [RE: superweapon.md §2 + this fix: the dispatcher FUN_006cba10 case 0
    // spawns the down missile 20000 leptons above the target with velocity
    // (0,0,-100) (DAT_007eca88 = -100.0) -> 200 frames down; the up missile
    // (NukeCarrier, GiantNukeUp Vertical, launch speed 1, Acceleration=1, cap
    // 255) passes DetonationAltitude=20000 after 200 frames (sum 1..200=20100)].
    int nuke_up_frames = 200;
    int nuke_down_frames = 200;
    int nuke_rad_level = 500;         // [NukePayload] RadLevel=
    // Iron Curtain [rules [General] IronCurtainDuration=750 frames].
    int iron_frames = 750;
    // Chronosphere [ChronoDelay=60].
    int chrono_delay_frames = 60;
    // Lightning Storm [LightningDeferment=250, Damage=250, StormDuration=180,
    // HitDelay=10, ScatterDelay=5, CellSpread=10, Separation=3, Warhead=IonWH].
    int storm_deferment = 250;
    int storm_damage = 250;
    int storm_duration = 180;
    int storm_hit_delay = 10;
    int storm_scatter_delay = 5;
    int storm_cell_spread = 10;
    int storm_separation = 3;
    double storm_wh_spread = 2.0;     // [IonWH] CellSpread
    double storm_wh_pmax = 0.5;       // [IonWH] PercentAtMax
    std::array<int, 11> storm_verses{};
    int storm_wall_flags = 0;         // W6-04-04: [IonWH] Wall/Wood
    int nuke_wall_flags = 0;          // W6-04-04: [NUKE] WallAbsoluteDestroyer/Wood
    // Chrono kill warhead [Rules+0xFA8; damage = the victim's full Strength].
    // Paradrops [AmerParaDropInf/Num, AllyParaDropInf/Num, SovParaDropInf/Num,
    // ParadropRadius=1024 leptons].
    std::string amer_para_inf = "E1";
    int amer_para_num = 8;
    std::string ally_para_inf = "E1";
    int ally_para_num = 6;
    std::string sov_para_inf = "E2";
    int sov_para_num = 9;
    int paradrop_radius = 1024;
    // W4-SQ-06 [rules [General] ParachuteMaxFallRate=-5]: a paratrooper under
    // its parachute sinks this many leptons per game frame (save v16).
    int parachute_fall = 5;
    // Naval / aircraft [General].
    int cloak_delay_frames = 18;      // CloakDelay=.02 min x 900
    double ship_sinking_weight = 3.0; // ShipSinkingWeight=
    int reload_frames = 270;          // ReloadRate=.3 min x 900, per ammo point
    int flight_level = 1500;          // FlightLevel= (leptons)
    // W4: the three RocketLocomotion blocks (V3 / DMisl / CMisl) with their
    // OWN warheads [FUN_00662A70: Rules+0xFB0..0xFC4] -- replaces the old
    // single V3Warhead every rocket exploded with.
    RocketParams rockets;
    // Naval Yard UnitRepair [RE-FALLBACK: the service-depot cadence --
    // [General] RepairStep=7 hp per RepairRate=.016 min (~14 frames), cost
    // RepairPercent=.15 of the unit cost pro rata].
    int repair_step = 7;
    int repair_rate_frames = 14;
    double repair_percent = 0.15;
    // W7-05-09 [RE: FlyLoco FUN_004CD520 @0x4CD7C9: Rules+0xFA8 =
    // [CombatDamage] C4Warhead]: an alive crash's warhead.
    std::vector<int> c4_verses = std::vector<int>(11, 100);
    double c4_spread = 0.0;
    double c4_pmax = 1.0;
};

// One superweapon effect in progress (a nuke in flight or a lightning storm).
struct SwStrike {
    int kind = 0;          // 1 = nuke, 2 = lightning storm, 3 = chrono freeze
    int house = -1;
    int x = 0, y = 0;      // target cell
    int ox = 0, oy = 0;    // origin (silo) cell
    int frame = 0;         // logic frames elapsed
    int hit_timer = 0, scatter_timer = 0;
    std::uint32_t rng = 1; // per-strike deterministic LCG (bolt scatter)
    std::vector<int> recent;   // recent bolt cells (x, y pairs) for LightningSeparation
    bool done = false;
};

// Presentation feed: appended by World, read (never drained) by PresGame by
// sequence number.
struct SwEvent {
    enum Kind : int {
        Granted = 1,     // a house gained a SW (enemy -> "detected" EVA)
        Ready = 2,       // charged
        Launched = 3,    // nuke left the silo (x,y = target, x2,y2 = silo)
        NukeFalling = 4, // the down missile appears over the target
        Detonated = 5,   // nuke impact
        Activated = 6,   // Iron Curtain / Chronosphere (x2,y2 = destination) / Storm / Paradrop
        Bolt = 7,        // one lightning bolt at (x, y)
        Dropped = 8,     // paratroopers landed at (x, y)
    };
    int seq = 0;
    int kind = 0;
    int house = -1;
    int type = 0;          // SuperweaponType as int
    int x = 0, y = 0, x2 = -1, y2 = -1;
};

// Pure targeting rule (W2-AN-14): may weapon slot `slot` of an attacker with
// type data `a` engage a victim in the given situation? Returns the slot to use
// (0/1), or -1. `want` = the slot the ordinary armor/AA choice picked (-1 =
// none), `has_secondary` = the attacker has a valid slot 1.
// W7-06-02/09 [RE: GetFireError @0x6FBC86, NavalTargeting switch FUN_006F2D80]:
// victim_in_water = the victim's cell is Water/Beach (not airborne, not on a
// bridge); victim_underwater = its TYPE is Underwater=yes; victim_cloaked = it
// is submerged now; victim_hover = SpeedType Hover.
int an_naval_slot(const AnType& a, int want, bool has_secondary, bool victim_in_water,
                  bool victim_underwater, bool victim_cloaked, bool victim_organic,
                  bool victim_hover, bool victim_airborne, bool victim_is_building);

}  // namespace ra2engine

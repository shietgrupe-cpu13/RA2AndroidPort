#pragma once
// Projectile flight core (P2) -- the pure, World-free half of BulletClass.
//
// Calibration authority: docs/re/projectile.md (the BulletClass/[Projectile]
// hunt) and its §12 implement-phase RE-GATE addendum. Every constant below
// cites it by section. This TU knows nothing about units, buildings or the
// map: it owns the launch solver (TechnoClass::Fire FUN_006FD210), the two
// per-frame integrators (BulletClass::Update FUN_004666D0), the scatter
// formulas (§5) and the deterministic projectile RNG (spec decision D3).
// World (engine/world.cpp) owns collision/arrival/detonation, which need the
// unit/building/map state.
//
// Pure: no SDL/OS deps; compiled into both ra2assets_tests and ra2engine.
//
// Units: positions are INTEGER leptons (256 per cell, §3) truncated with ftol
// each frame (FUN_004666D0 stores CoordStruct ints); velocities are doubles in
// leptons per LOGIC FRAME (15 Hz, no dt -- §6.1). The clone's lepton space is
// x = fx*256, y = fy*256 (a unit at integer fx sits on its cell's lepton
// centre -- the dist_leptons convention), so the cell of a lepton point is
// floor((l + 128) / 256) (spec D4).
#include <cstdint>
#include <string>

namespace ra2assets { class Rules; }

namespace ra2engine {

// How a weapon's projectile is delivered.
//  Instant   -- the pre-P2 instant-hit path (Inviso, homing, unresolved). This
//               is the inert DEFAULT: a value-initialised spec never flies.
//  Ballistic -- ROT==0, !Vertical, !Inviso: gravity integrator (§6.1).
//  Vertical  -- [Projectile] Vertical=yes: the falling bomb (§6.2).
//  Homing    -- W4: ROT>0 (not Inviso, not Vertical): the BulletClass::Update
//               homing branch (docs/gap/wave4/04-projectiles.md §1.1).
enum class ProjKind : std::uint8_t { Instant = 0, Ballistic = 1, Vertical = 2, Homing = 3 };

// One weapon's resolved projectile parameters (main.cpp resolves it from Rules
// once per unit type, like every other combat stat). ALL defaults are inert.
struct ProjectileSpec {
    ProjKind kind = ProjKind::Instant;
    bool arcing = false;        // [Projectile] Arcing (BulletType+0x29B)
    bool inaccurate = false;    // Inaccurate (+0x2A2): suppress snap-to-target (§5.4)
    bool flak_scatter = false;  // FlakScatter (+0x2A3)
    bool inviso = false;        // Inviso (+0x29E)
    bool floater = false;       // Floater (+0x295): half gravity
    int rot = 0;                // ROT (+0x2DC): >0 = homing (W4: flies, ProjKind::Homing)
    int speed_cap = 0;          // weapon Speed -> leptons/frame (weapon_speed_leptons)
    int acceleration = 0;       // Acceleration (+0x2D0); absent -> 3 (W4: the BulletTypeClass
                                // ctor stores 3 at +0x2D0 / +0x2E4, gamemd 0x46BB2D)
    int detonation_altitude = 0;  // DetonationAltitude (+0x2BC); 0 = absent -> no cap
    int weapon_range = 0;       // ftol(Range*256) leptons: the §5.2 divisor
    int flh[3] = {0, 0, 0};     // PrimaryFireFLH F,L,H (leptons) -- artmd.ini
    // W4 homing keys (FUN_005B1B20 arguments, FUN_004666D0 homing branch).
    bool level = false;         // Level (+0x29D): no pitch change (torpedoes)
    bool airburst = false;      // Airburst (+0x294)
    bool very_high = false;     // VeryHigh (+0x299)
    int course_lock = 0;        // CourseLockDuration (+0x2E0) frames
    // W5-B R-063/R-057 [RE: BulletTypeClass_ReadINI 0x46BE00]: +0x296
    // SubjectToCliffs, +0x297 SubjectToElevation, +0x298 SubjectToWalls.
    bool subject_to_cliffs = false;
    bool subject_to_elevation = false;
    bool subject_to_walls = false;
    int turret_offset = 0;     // art turret origin along body facing, in leptons
    std::string weapon_id;     // immutable launch weapon identity for rendering
    bool flies() const { return kind != ProjKind::Instant; }
};

// World-level projectile constants resolved from Rules (spec C0).
struct ProjectileRules {
    double gravity = 6.0;        // [AudioVisual] Gravity=6 (§0.1 -- NOT [General])
    int ballistic_scatter = 256; // [CombatDamage] BallisticScatter=1.0 -> ftol(1.0*256)
    double rot_var = 0.25;       // W4: [General] MissileROTVar (homing wobble)
    int safety_altitude = 750;   // W4: [General] MissileSafetyAltitude
};

// Deterministic projectile RNG (spec D3). The bit source is the project's
// xorshift32 (a [DEVIATION -- RNG stream]: the original draws from the single
// lockstep Scenario->Random); the RANGE MAPPING is FUN_0065C220's exactly
// (§12.6: swap, top-bit mask, reject > range -> inclusive and unbiased), so
// every distribution is the original's.
struct ProjRng {
    std::uint32_t s = 0x9E3779B9u;  // fixed seed: bit-reproducible runs
    std::uint32_t next();           // xorshift32 (never 0)
    int random(int lo, int hi);     // Randomizer::Random(lo, hi), inclusive
    // The uniform scatter angle shared by all three scatter sites (§5):
    // r = Random(0,0x7FFFFFFE); a16 = (int16)ftol((r/2^31*2pi - pi/2)*(-65534/2pi));
    // theta = (a16 - 0x3FFF)*(-2pi/65534).
    double angle();
};

struct Vec3i { int x = 0, y = 0, z = 0; };

// A projectile in flight. World owns the vector of these.
struct Projectile {
    ProjKind kind = ProjKind::Ballistic;
    int attacker = -1;          // firing unit index (stats read at detonation)
    int target_unit = -1;       // target unit index, or -1
    int terrain_origin_level = 0; // fixed launch terrain reference; World v61 tail
    int target_building = -1;   // target building index, or -1
    int x = 0, y = 0, z = 0;    // position, integer leptons
    double vx = 0.0, vy = 0.0, vz = 0.0;  // velocity, leptons/frame
    int tx = 0, ty = 0, tz = 0; // TRUE target coords at launch (§12.4)
    int speed_cap = 0;          // Vertical terminal speed (bullet+0x110)
    int acceleration = 0;
    int detonation_altitude = 0;
    bool inaccurate = false;
    bool floater = false;
    bool slow = false;          // |V| < 8.0 at the start of this frame (§7)
    int launch_frame = 0;
    int launch_speed = 0;       // diagnostics / witnesses
    int max_z = 0;              // apex (diagnostics / witnesses)
    int weapon_slot = 0;        // Gap fix C: firer's weapon slot at launch (0/1)
    int attacker_building = -1; // Gap fix C: a base defense fired it (attacker = -1)
    // --- W4 homing (ProjKind::Homing) state ---
    int uid = 0;                // bullet ID: the 15-frame MissileROTVar phase
    int rot = 0;                // Type->ROT
    double speed = 0.0;         // |V| (leptons/frame)
    bool level = false, airburst = false, very_high = false;
    int course_lock = 0;        // CourseLockDuration
    int lock_frames = 0;        // frames held (+0x108)
    bool locked = true;         // course lock flag (+0x105)
    int giveup_n = 0;           // +0x118: closing samples taken (to 60)
    double giveup_acc = 0.0;    // +0x120: closing-rate accumulator
    int prev_dist = -1;         // last returned distance
    bool lost = false;          // target gone: climb to MissileSafetyAltitude (air) / last point (ground)
    std::string weapon_id;     // copied at launch; never inferred from current shooter stage
    bool target_air = false;    // the target is an airborne aircraft (direct pitch mode)
};

// One observable projectile event (World::take_projectile_events). Feeds the
// --demo-projectile witnesses/hash and is available to audio/render; NOT read
// by world_hash.
struct ProjectileEvent {
    enum Kind : std::uint8_t { Launch = 0, Refused = 1, Detonate = 2, Removed = 3 };
    Kind kind = Launch;
    int frame = 0;            // World projectile frame counter (15 Hz)
    int attacker = -1;
    int x = 0, y = 0, z = 0;  // launch point (Launch/Refused) or impact (Detonate/Removed)
    int speed = 0;            // launch speed (Launch/Refused)
    int scatter_dx = 0, scatter_dy = 0;  // Launch: §5 aim offset
    int victim = -1;          // Detonate: single-target unit victim, or -1
    int building = -1;        // Detonate: building hit, or -1
    int damage = 0;           // Detonate: total HP removed (units + building)
    int flight_frames = 0;    // Detonate/Removed: frames since launch
    int apex = 0;             // Detonate/Removed: highest z reached
};

// --- conversions -------------------------------------------------------------
int ftol(double v);                       // truncate toward zero (FUN_007C4D00)
int lepton_to_cell(int l);                // floor((l + 128) / 256)  (spec D4)

// FUN_00474740: weapon Speed= -> leptons/frame = ftol(min(255, clamp(S,0,100)*256/100)).
int weapon_speed_leptons(int ini_speed);

// FUN_00771E90 -> FUN_0048AAA0 (§0.2): the ROT==0 launch speed,
// ftol(sqrt(dist * g * 1.2)) leptons/frame, recomputed per shot.
int arcing_launch_speed(int dist2d, double gravity);

// FUN_0048A8E0 (§12.1): the arcing-angle core. `lofted` selects the minus root
// (high arc). Returns false when the shot has no solution (Fire then destroys
// the bullet: the shot is refused, §4 step 7). theta in radians, >= 0 (acos).
bool arc_solve_core(bool lofted, int v, int d, int z, double g, double& theta);

// FUN_0048A7E0 (§12.1) for the LOW root (the clone never lofts -- spec Scope):
// core + the d+1 numeric-derivative sign test, then the 16-bit binary-angle
// quantisation Fire applies: p16 = ftol((theta - pi/2)*(-65534/2pi)),
// phi = (p16 - 0x3FFF)*(-2pi/65534). Returns false if no root.
bool arc_pitch(int v, int d, int z, double g, double& phi);

// Non-arcing pitch (§4 step 7): level, or atan2(|dz| - 20, horiz) when |dz| > 200,
// signed toward the target [PROBABLE sign -- the atan2 helper FUN_004CB2F0's
// output convention was not unwound], quantised like arc_pitch.
double straight_pitch(int horiz, int dz);

// The launch: Fire steps 2-8 (§4 + §12.3). `src` = muzzle (firer + FLH), `tgt`
// = the true target coords. Draws scatter from `rng` when Inaccurate && Arcing
// (§5.1/§5.2). ok=false -> the shot is refused (arcing solver had no root).
struct LaunchResult {
    bool ok = false;
    int speed = 0;              // launch speed after the d3/2 cap / Vertical override
    double vx = 0.0, vy = 0.0, vz = 0.0;
    int aim_dx = 0, aim_dy = 0; // the (possibly scattered) working delta
    int scatter_dx = 0, scatter_dy = 0;  // aim - true delta (0,0 when no scatter)
};
LaunchResult compute_launch(const ProjectileSpec& spec, const ProjectileRules& rules,
                            const Vec3i& src, const Vec3i& tgt, ProjRng& rng);

// One logic frame of the ballistic integrator (§6.1, CONFIRMED):
// slow = |V| < 8.0; V.z -= g (halved if floater); pos = ftol(pos + V).
void integrate_ballistic(Projectile& p, double gravity);

// One logic frame of the Vertical integrator (§6.2, CONFIRMED):
// s = ftol(|V|); if s < cap: V = normalize(V) * (s + Acceleration); pos = ftol(pos + V).
void integrate_vertical(Projectile& p);

// W4: one logic frame of the homing branch (FUN_004666D0 ROT>=1 + the
// FUN_005B1B20 steering), docs/gap/wave4/04-projectiles.md §1.1:
//  * speed: launch 1, |V| += Acceleration to the weapon Speed (CourseLock with
//    CourseLockDuration=0 alternates +1/+0 per frame until Speed > 39 or
//    reached), |V| -= Acceleration/2 above it;
//  * turn = ftol((V*sin(ph) + V + 1) * ROT), ph = ((uid + frame) % 15)/15 * 2pi
//    (V = [General] MissileROTVar), x1.5 inside 256 leptons, 0 while locked;
//    the limit is (byte)turn in 1/256-circle units;
//  * yaw turned at the target by <= limit; pitch: Level = none, an air target
//    = straight at it, else the terrain-following cruise (min(d_cells,5) x
//    LevelHeight above the floor, 10 levels for Airburst/VeryHigh, +-18 nudge
//    beyond 20 leptons, pitch toward climb/dive/level at limit/2);
//  * no target: pitch toward climb; detonate at MissileSafetyAltitude.
// `tgt` is the target point (z = height above the ground). Returns 0 = fly,
// 1 = detonate snapped onto the target, 2 = detonate where it is.
struct HomingRules {
    double rot_var = 0.25;          // [General] MissileROTVar (Rules+0x598)
    int safety_altitude = 750;      // [General] MissileSafetyAltitude (Rules+0x5A0)
    int ground_z = 0;              // terrain floor in this flight's fixed reference frame
};
int integrate_homing(Projectile& p, const Vec3i& tgt, int frame, const HomingRules& hr);

// --- rules resolution (Task 3) ---------------------------------------------
// The game's INI bool (WW INIClass::Get_Bool): strip an inline ';' comment,
// trim, then the FIRST character decides -- y/Y/t/T/1 true, n/N/f/F/0 false,
// anything else (incl. empty) -> fallback. (RC-5: Ini::parse now cuts the
// inline comment itself, so the strip here is a harmless no-op kept for
// callers that pass raw strings.)
bool ini_bool_value(const std::string& raw, bool fallback);

// One weapon's projectile parameters from Rules: [weapon] Projectile= ->
// that [Projectile] section's Arcing/Inviso/Inaccurate/FlakScatter/Floater/
// Vertical/ROT/Acceleration/DetonationAltitude, plus the weapon's Speed= (as the
// leptons/frame cap) and Range= (as leptons, the §5.2 divisor). Classification:
// Inviso -> Instant (projectile.md §6.3/§11.3: a 1-frame teleport, already
// faithful); ROT>0 -> Instant (homing DEFERRED -- spec Scope); Vertical ->
// Vertical; otherwise Ballistic. No weapon / no Projectile= -> Instant. FLH is
// art data and is filled by the caller.
ProjectileSpec resolve_projectile_spec(const ra2assets::Rules& rules, const std::string& weapon);

// [AudioVisual] Gravity (§0.1) and [CombatDamage] BallisticScatter (cells ->
// ftol(x*256) leptons, FUN_00474550). Absent keys fall back to this install's
// values (6, 1.0).
ProjectileRules resolve_projectile_rules(const ra2assets::Rules& rules);

// Termination constants (§7 + §12.2).
constexpr int kLevelHeight = 104;              // 0x89CDE0, CONFIRMED §12.2
constexpr int kArrivalHeight = 2 * kLevelHeight;     // arrival test: height < 208
constexpr int kBlockedHeight = -4 * kLevelHeight;    // IsBlocked: height <= -416
constexpr int kCollisionRadius = 0x7F;         // 127 leptons, 3-D (§5.4/§7)
constexpr int kSnapRadius = 32;                // Explode snap (§5.4)
constexpr int kGroundObjectBand = 150;         // 0x7E38D8 (§12.2)
constexpr double kMinSpeed = 8.0;              // 0x7E38E0
constexpr double kRestSpeed = 10.0;            // 0x7E34A8 (with height <= 9)

}  // namespace ra2engine

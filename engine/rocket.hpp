#pragma once
// RocketLocomotion (V3ROCKET / DMISL / YR CMISL) -- the pure, World-free half
// of the spawned rockets (wave-4 fix, docs/gap/wave4/04-projectiles.md §1.2 and
// 03-all01-opening.md §3.1).
//
// Calibration authority (gamemd.exe YR 1.001; RA2 game.exe has the same code):
//   * FUN_0066CF70 (RulesClass::General) reads three RocketStruct blocks, one per
//     rocket type: V3 at Rules+0x4B0, DMisl at +0x4E4, CMisl at +0x518. Every
//     field is string-anchored (0x670C47..0x670F9B): +0 PauseFrames, +4
//     TiltFrames, +8 PitchInitial, +C PitchFinal, +10 TurnRate, +14 RaiseRate,
//     +18 Acceleration, +1C Altitude, +20 Damage, +24 EliteDamage, +28
//     BodyLength, +2C LazyCurve, +30 Type.
//   * The block a rocket uses is picked from its OWN type: == V3RocketType
//     (Rules+0x4E0) -> V3, == CMislType (+0x548) -> CMisl, anything else ->
//     DMisl (FUN_00661D00 head, FUN_00662A70).
//   * FUN_00662A70 (explode) picks the WARHEAD per rocket type too: V3
//     Rules+0xFB0 / elite +0xFB8, DMisl +0xFB4 / elite +0xFBC, CMisl +0xFC0 /
//     elite +0xFC4 -- the [CombatDamage] V3Warhead / DMislWarhead / CMislWarhead
//     (+ the *EliteWarhead keys) read at 0x66BDDC/0x66BE1B. The elite flag is
//     the locomotor's +0x4D byte, set while the rocket sits on its launcher
//     from (SpawnOwner && FUN_0074EE30) -- the launcher's elite rank.
//   * FUN_00661D00 (Process) is the six-state machine ported by rocket_step.
//   * FUN_00661B30 is the nose-contact detonation test.
//
// Pure: no SDL/OS deps; compiled into both ra2assets_tests and ra2engine.
#include <array>
#include <string>

namespace ra2assets { class Rules; }

namespace ra2engine {

enum RocketKind : int { kRocketV3 = 0, kRocketDMisl = 1, kRocketCMisl = 2 };

// One explosion warhead as the rocket's area damage reads it.
struct RocketWarhead {
    std::string id;                     // [CombatDamage] *Warhead= value
    double spread = 1.0;                // CellSpread
    double pmax = 0.5;                  // PercentAtMax
    std::array<int, 11> verses{};       // Verses (percent), 100 when absent
    int wall_flags = 0;                 // W6-04-04: Wall/Wood/WallAbsoluteDestroyer
};

// One RocketStruct (Rules+0x4B0 / +0x4E4 / +0x518) + its two warheads.
struct RocketBlock {
    int pause_frames = 0;               // +0  PauseFrames
    int tilt_frames = 60;               // +4  TiltFrames
    double pitch_initial = 0.0;         // +8  PitchInitial (x 90 degrees)
    double pitch_final = 0.5;           // +C  PitchFinal   (x 90 degrees)
    double turn_rate = 0.05;            // +10 TurnRate (radians of pitch per frame)
    int raise_rate = 1;                 // +14 RaiseRate (CMisl only)
    double acceleration = 0.4;          // +18 Acceleration (leptons/frame^2)
    int altitude = 768;                 // +1C Altitude (leptons)
    int damage = 200;                   // +20 Damage
    int elite_damage = 400;             // +24 EliteDamage
    int body_length = 256;              // +28 BodyLength (leptons)
    bool lazy_curve = false;            // +2C LazyCurve
    RocketWarhead warhead;              // V3Warhead / DMislWarhead / CMislWarhead
    RocketWarhead elite_warhead;        // V3EliteWarhead / DMislEliteWarhead / CMislEliteWarhead
};

// The three blocks + the three type ids, resolved once from (overlaid) rules.
struct RocketParams {
    std::array<RocketBlock, 3> block{};
    std::array<std::string, 3> type_id{};   // V3RocketType / DMislType / CMislType
};

// Resolve the three blocks from [General] (+ the map overlay, e.g. ALL01's
// DMislAltitude=1550) and the warheads from [CombatDamage]. Absent keys fall
// back to the stock rulesmd values.
RocketParams resolve_rocket_params(const ra2assets::Rules& rules);

// Which block a rocket type uses (FUN_00661D00 head): V3 / CMisl by type id,
// everything else DMisl. Case-insensitive.
int rocket_kind_for(const RocketParams& p, const std::string& unit_id);

// The warhead an exploding rocket uses (FUN_00662A70).
inline const RocketWarhead& rocket_warhead(const RocketBlock& b, bool elite) {
    return elite ? b.elite_warhead : b.warhead;
}
inline int rocket_damage(const RocketBlock& b, bool elite) { return elite ? b.elite_damage : b.damage; }

// ---- the flight (FUN_00661D00 / FUN_00661B30) --------------------------------
// One rocket's RocketLocomotion state. Positions are leptons in the clone's
// plane (x = fx*256, y = fy*256); z is the height above the ground (the flat
// combat plane, projectile.md 11.6). Pitch is radians, + = nose up (the
// original keeps it as a float at locomotor +0x50, field x pi/2 = 0x7E1820).
enum RocketPhase : int {
    kRocketPause = 1,   // PauseFrames on the launcher (launch smoke every 24 frames)
    kRocketTilt = 2,    // TiltFrames: pitch PitchInitial -> PitchFinal, then leave the launcher
    kRocketClimb = 3,   // speed += Acceleration (cap: type Speed) until height >= Altitude
    kRocketCruise = 4,  // LazyCurve arc, or level-off by TurnRate until the dive cone
    kRocketDive = 5,    // pitch turns at the target by TurnRate per frame
};
struct RocketFlight {
    int phase = kRocketPause;
    int timer = 0;            // frames left in the pause / tilt
    double pitch = 0.0;       // radians, + = nose up
    double speed = 0.0;       // leptons per frame (locomotor +0x44, double)
    int horiz0 = 0;           // horizontal distance at the end of the climb (+0x54)
    double x = 0.0, y = 0.0, z = 0.0;   // leptons; z = height above the ground
    double yaw = 0.0;         // radians in the lepton plane: heading (cos yaw, sin yaw)
    int frames = 0;           // frames since launch (safety lifetime)
    double tx = 0.0, ty = 0.0, tz = 0.0;   // last known target point (leptons, tz = height)
};

// Launch on the launcher at (x, y, z) aimed at (tx, ty): the pause timer, the
// initial pitch, and the yaw toward the target [RE-FALLBACK: the spawn
// manager's facing hand-over (FUN_006B7230) was not traced -- the rocket
// leaves facing its target].
void rocket_launch(const RocketBlock& b, RocketFlight& f, double x, double y, double z, double tx,
                   double ty);

// What one rocket_step frame did.
enum RocketStepResult : int { kRocketFlying = 0, kRocketLeftLauncher = 1, kRocketDetonate = 2 };

// One logic frame of FUN_00661D00 for a rocket of Speed cap `speed_cap`
// (leptons/frame, the type's converted Speed= at +0x678) toward the target
// point (tx, ty, tz) (tz = its height above the ground). Returns
// kRocketDetonate when FUN_00661B30 fires (the nose reaches the target's
// height, or the rocket's height drops below 1); the caller explodes it at
// rocket_nose(). kRocketLeftLauncher on the frame the tilt ends.
int rocket_step(const RocketBlock& b, int speed_cap, RocketFlight& f, double tx, double ty, double tz,
                bool vertical_raise=false);

// The nose point (BodyLength ahead along pitch and yaw): FUN_00661B30's test
// point and FUN_00662A70's explosion point.
void rocket_nose(const RocketBlock& b, const RocketFlight& f, double& x, double& y, double& z);

}  // namespace ra2engine

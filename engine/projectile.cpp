#include "projectile.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "ra2assets/rules.hpp"

namespace ra2engine {

namespace {

constexpr double kPi = 3.14159265358979323846;
// 16-bit binary-angle conversions read out of the image (§1 constants table):
// 0x7E1818 = -65534/2pi (radians -> u16), 0x7E1810 = -2pi/65534 (u16 -> radians),
// 0x7E1820 = pi/2 (the pitch datum); 0x3FFF is the level datum.
constexpr double kRadToU16 = -65534.0 / (2.0 * kPi);
constexpr double kU16ToRad = -2.0 * kPi / 65534.0;
constexpr double kHalfPi = kPi / 2.0;
constexpr int kLevelDatum = 0x3FFF;

// Fire's pitch quantisation: radians -> 16-bit binary angle -> radians.
double quantise_pitch(double theta) {
    int p16 = ftol((theta - kHalfPi) * kRadToU16) & 0xFFFF;  // stored in a 16-bit word
    if (p16 >= 0x8000) p16 -= 0x10000;                        // (short) reinterpretation
    return static_cast<double>(p16 - kLevelDatum) * kU16ToRad;
}

}  // namespace

int ftol(double v) { return om::ftol(v); }   // Net L0: the original _ftol (FUN_007C4D00)

int lepton_to_cell(int l) {
    return om::ftol(std::floor((static_cast<double>(l) + 128.0) / 256.0));
}

int weapon_speed_leptons(int ini_speed) {
    // FUN_00474740 (§1/§6.5): min(255, clamp(Speed,0,100) * 256 / 100), ftol.
    const double s = std::clamp(static_cast<double>(ini_speed), 0.0, 100.0) * 256.0 / 100.0;
    return ftol(std::min(255.0, s));
}

int arcing_launch_speed(int dist2d, double gravity) {
    // FUN_0048AAA0 (§0.2): FILD d; FMUL g; FMUL 1.2 (DAT_007E4190); sqrt; ftol.
    const double v = static_cast<double>(dist2d) * gravity * 1.2;
    return v > 0.0 ? ftol(om::sqrt(v)) : 0;
}

bool arc_solve_core(bool lofted, int v, int d, int z, double g, double& theta) {
    // FUN_0048A8E0, unwound in §12.1.
    if (z > 0) {
        theta = kPi / 4.0;  // literal 0x3FE921D9F4D37C12
        return true;
    }
    const double vv = static_cast<double>(v) * v;
    const double dz = static_cast<double>(z);
    const double dd = static_cast<double>(d) * d;
    const double a = vv - dz * g;
    const double disc = vv * vv - 2.0 * vv * dz * g - g * g * dd;
    double den = (dz * dz) / dd + 1.0;
    den = den + den;
    if (!(disc >= 0.0) || den == 0.0) return false;
    const double root = om::sqrt(disc);
    const double t = (lofted ? (a - root) : (a + root)) / den;
    if (!(t >= 0.0) || v == 0) return false;
    double c = om::sqrt(t) / static_cast<double>(v);
    c = std::clamp(c, -1.0, 1.0);  // FUN_004CACD0 is a table acos; guard the domain
    theta = om::acos(c);
    return true;
}

bool arc_pitch(int v, int d, int z, double g, double& phi) {
    // FUN_0048A7E0 (§12.1), low root: core(d), then core(d+1) and negate when
    // the angle FALLS with distance (the downward branch acos cannot express).
    double th = 0.0;
    if (!arc_solve_core(false, v, d, z, g, th)) return false;
    double th1 = th;  // original: uninitialised stack slot if this call fails
    arc_solve_core(false, v, d + 1, z, g, th1);
    if (th1 < th) th = -th;
    phi = quantise_pitch(th);
    return true;
}

double straight_pitch(int horiz, int dz) {
    // Fire, non-arcing branch (§4 step 7): level datum unless |dz| > 200, then
    // atan2(|dz| - 20, horiz). The 20.0 is dropped only for a building target
    // within tolerance (not modelled). Sign toward the target: [PROBABLE].
    const int adz = std::abs(dz);
    if (adz <= 200) return quantise_pitch(0.0);
    const double h = static_cast<double>(std::max(horiz, 1));
    double th = om::atan2(static_cast<double>(adz) - 20.0, h);
    if (dz < 0) th = -th;
    return quantise_pitch(th);
}

std::uint32_t ProjRng::next() {
    std::uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) x = 0x9E3779B9u;
    s = x;
    return x;
}

int ProjRng::random(int lo, int hi) {
    // FUN_0065C220 (§12.6): exact range mapping over our bit source.
    if (lo == hi) return lo;
    if (hi < lo) std::swap(lo, hi);
    const std::uint32_t range = static_cast<std::uint32_t>(hi) - static_cast<std::uint32_t>(lo);
    int b = 31;
    while (b > 0 && !(range & (1u << b))) --b;
    const std::uint64_t mask = (std::uint64_t{1} << (b + 1)) - 1;
    std::uint32_t v;
    do {
        v = static_cast<std::uint32_t>(next() & mask);
    } while (v > range);
    return static_cast<int>(static_cast<std::uint32_t>(lo) + v);
}

double ProjRng::angle() {
    // §5: r = Random(0,0x7FFFFFFE); ang16 = ftol((r/2^31*2pi - pi/2) * (-65534/2pi))
    // (0x7E2570 = 1/2^31); theta = ((short)ang16 - 0x3FFF) * (-2pi/65534).
    const int r = random(0, 0x7FFFFFFE);
    const double a = (static_cast<double>(r) / 2147483648.0) * (2.0 * kPi);
    int a16 = ftol((a - kHalfPi) * kRadToU16) & 0xFFFF;
    if (a16 >= 0x8000) a16 -= 0x10000;
    return static_cast<double>(a16 - kLevelDatum) * kU16ToRad;
}

LaunchResult compute_launch(const ProjectileSpec& spec, const ProjectileRules& rules,
                            const Vec3i& src, const Vec3i& tgt, ProjRng& rng) {
    LaunchResult out;
    double g = rules.gravity;
    if (spec.floater) g = rules.gravity * 0.5;  // FUN_0048AC00 (§6.1)

    const int tdx = tgt.x - src.x;
    const int tdy = tgt.y - src.y;
    const int dz = tgt.z - src.z;

    // Step 2 (§0.2): GetSpeed(distance) -- 2-D muzzle->target distance, pre-scatter.
    const int d2 = ftol(om::sqrt(static_cast<double>(tdx) * tdx + static_cast<double>(tdy) * tdy));
    int speed = (spec.rot == 0) ? arcing_launch_speed(d2, g) : spec.speed_cap;

    // Step 4 (§5.1/§5.2): scatter perturbs the working delta. Radius drawn FIRST,
    // then the angle (the order the two FUN_0065C220 calls appear in Fire).
    int dx = tdx, dy = tdy;
    if (spec.inaccurate && spec.arcing) {
        double r = 0.0;
        if (!spec.flak_scatter || spec.inviso) {
            // §5.1: Random(BallisticScatter/2, BallisticScatter).
            r = static_cast<double>(rng.random(rules.ballistic_scatter / 2, rules.ballistic_scatter));
        } else {
            // §5.2 [FlakTProj]: Random(0, BS) * ftol(|delta|_3d) / WeaponRange (integer).
            const int dist = ftol(om::sqrt(static_cast<double>(dx) * dx +
                                            static_cast<double>(dy) * dy +
                                            static_cast<double>(dz) * dz));
            const int pick = rng.random(0, rules.ballistic_scatter);
            r = spec.weapon_range > 0
                    ? static_cast<double>((static_cast<long long>(pick) * dist) / spec.weapon_range)
                    : 0.0;
        }
        const double th = rng.angle();
        dx = ftol(static_cast<double>(dx) + om::cos(th) * r);
        dy = ftol(static_cast<double>(dy) - om::sin(th) * r);
    }
    out.aim_dx = dx;
    out.aim_dy = dy;
    out.scatter_dx = dx - tdx;
    out.scatter_dy = dy - tdy;

    // Step 3 (§4): never cross more than half the remaining distance in frame 1.
    const int d3 = ftol(om::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                                  static_cast<double>(dz) * dz));
    if (d3 / 2 < speed) speed = d3 / 2;
    // §12.3: Vertical (and homing) projectiles leave the launcher at 1 lept/frame.
    if (spec.kind == ProjKind::Vertical || spec.rot > 0) speed = 1;

    // Step 7: pitch.
    const int horiz = ftol(om::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy));
    double phi = 0.0;
    if (spec.arcing) {
        if (!arc_pitch(speed, horiz, dz, g, phi)) {
            out.ok = false;  // §4: "the bullet is destroyed and the shot does not happen"
            out.speed = speed;
            return out;
        }
    } else {
        phi = straight_pitch(horiz, dz);
    }

    // Steps 6/8: |V| = speed along the yaw, then pitched (V.xy *= cos, V.z = sin*|V|).
    double ux = 1.0, uy = 0.0;  // degenerate (target straight below): any yaw
    const double hl = om::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
    if (hl > 0.0) {
        ux = dx / hl;
        uy = dy / hl;
    }
    const double s = static_cast<double>(speed);
    out.vx = ux * s * om::cos(phi);
    out.vy = uy * s * om::cos(phi);
    out.vz = s * om::sin(phi);
    out.speed = speed;
    out.ok = true;
    return out;
}

void integrate_ballistic(Projectile& p, double gravity) {
    // FUN_004666D0 non-homing !Vertical branch (§6.1, dis_arcgrav_4673F2.txt).
    p.slow = om::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) < kMinSpeed;
    const double g = p.floater ? gravity * 0.5 : gravity;
    p.vz -= g;
    p.x = ftol(static_cast<double>(p.x) + p.vx);
    p.y = ftol(static_cast<double>(p.y) + p.vy);
    p.z = ftol(static_cast<double>(p.z) + p.vz);
    p.max_z = std::max(p.max_z, p.z);
}

void integrate_vertical(Projectile& p) {
    // FUN_004666D0 Vertical branch (§6.2).
    const double mag = om::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
    const int s = ftol(mag);
    if (s < p.speed_cap) {
        const int ns = p.acceleration + s;
        if (p.vx == 0.0 && p.vy == 0.0 && p.vz == 0.0) p.vx = 100.0;  // 0x40590000 = 100.0
        const double m = om::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
        const double f = static_cast<double>(ns) / m;
        p.vx *= f;
        p.vy *= f;
        p.vz *= f;
    }
    p.x = ftol(static_cast<double>(p.x) + p.vx);
    p.y = ftol(static_cast<double>(p.y) + p.vy);
    p.z = ftol(static_cast<double>(p.z) + p.vz);
    p.max_z = std::max(p.max_z, p.z);
}

namespace {
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kU16Rad = kTwoPi / 65536.0;   // one 16-bit facing unit
// Turn `a` toward `want` (radians) by at most `lim` (FUN_005B23C0 snap within
// the limit, else +-lim by the shorter way, FUN_005B2380).
double turn_toward_rad(double a, double want, double lim) {
    double d = std::remainder(want - a, kTwoPi);
    if (std::fabs(d) <= lim) return want;
    return a + (d > 0.0 ? lim : -lim);
}
}  // namespace

int integrate_homing(Projectile& p, const Vec3i& tgt, int frame, const HomingRules& hr) {
    // --- speed (0x466968..0x466AB5) ---
    const double cap = static_cast<double>(p.speed_cap);
    if (p.course_lock == 0) {
        if (p.speed_cap > 0x27 || cap <= p.speed + 0.5) p.locked = false;   // 0x7E0738 = 0.5
    } else if (p.lock_frames < p.course_lock && ++p.lock_frames >= p.course_lock) {
        p.locked = false;
    }
    if (p.course_lock > 0 && p.lock_frames >= p.course_lock) p.locked = false;
    int accel = p.acceleration;
    if (p.locked && p.course_lock == 0) accel = (frame % 2 == 0) ? 1 : 0;
    if (p.speed < cap) {
        p.speed = std::min(cap, p.speed + accel);
    } else if (p.speed > cap) {
        p.speed -= accel / 2;
        if (p.speed <= 0.0) p.speed = 0.0;
    }
    // --- turn rate with the MissileROTVar wobble (0x466B9F..0x466CF1) ---
    const double ph = static_cast<double>((p.uid + frame) % 15) * (1.0 / 15.0) * kTwoPi;
    int turn = ftol((hr.rot_var * om::sin(ph) + hr.rot_var + 1.0) * p.rot);
    const double dx = static_cast<double>(tgt.x) - p.x, dy = static_cast<double>(tgt.y) - p.y;
    const double dz = static_cast<double>(tgt.z) - p.z;
    const double d3 = om::sqrt(dx * dx + dy * dy + dz * dz);
    if (!p.lost && d3 < 256.0) turn = ftol(turn * 1.5);   // 0x7E38F0 inside one cell
    if (p.locked) turn = 0;
    const int lim16 = (turn & 0xFF) << 8;                 // MOV CH,AL
    const double lim = lim16 * kU16Rad;
    // --- FUN_005B1B20 steering ---
    const double hv = om::sqrt(p.vx * p.vx + p.vy * p.vy);
    double yaw = hv > 0.0 ? om::atan2(p.vy, p.vx) : om::atan2(dy, dx);
    double pitch = om::atan2(p.vz, hv);
    const double horiz = om::sqrt(dx * dx + dy * dy);
    if (p.lost) {
        if (!p.level) pitch = turn_toward_rad(pitch, kPi / 4.0, lim);   // toward climb 0x2000
    } else {
        if (horiz > 0.0) yaw = turn_toward_rad(yaw, om::atan2(dy, dx), lim);
        const int far = (p.very_high ? 6 : 3) * 256;
        if (p.level) {
            // Level=yes: every pitch change is skipped (torpedoes).
        } else if (!p.target_air && (lim16 >> 8) > 1 && (p.airburst || ftol(d3) > far)) {
            // Cruise: follow the ground at min(d_cells, 5) levels (10 for
            // Airburst/VeryHigh); floor = 0 on the flat combat plane.
            int levels = ftol(d3) / 256;
            if (p.airburst || p.very_high) levels = 10;
            else if (levels > 4) levels = 5;
            const int err = p.z - hr.ground_z - levels * kLevelHeight;
            if (err < -20) p.z += 18;
            else if (err > 20) p.z -= 18;
            double want = 0.0;                                // level 0x4000
            if (err < -kLevelHeight / 2) want = kPi / 4.0;    // climb 0x2000
            else if (err > kLevelHeight / 2) want = -kPi / 16.0;   // dive 0x4800
            pitch = turn_toward_rad(pitch, want, lim / 2.0);
        } else {
            // Direct: straight at the target (air targets / close in).
            pitch = turn_toward_rad(pitch, om::atan2(dz, std::max(horiz, 1.0)), lim);
        }
    }
    const double cp = om::cos(pitch);
    p.vx = p.speed * cp * om::cos(yaw);
    p.vy = p.speed * cp * om::sin(yaw);
    p.vz = p.level ? 0.0 : p.speed * om::sin(pitch);
    // The returned distance: z quartered (zeroed for Airburst).
    const double qz = p.airburst ? 0.0 : dz / 4.0;
    const int dist = ftol(om::sqrt(dx * dx + dy * dy + qz * qz));
    int result = 0;
    // [RE-FALLBACK: a Level bullet (torpedo) runs on the water line, z = 0 in
    // the clone's flat plane, so the height < 1 test is skipped for it.]
    if (!p.lost && (dist <= p.speed * 0.5 || (p.z < hr.ground_z+1 && !p.level)))
        result = ((p.z > hr.ground_z || p.level) && !p.airburst) ? 1 : 2;
    if (p.lost && p.target_air && p.z >= hr.ground_z+hr.safety_altitude) result = 2;   // Rules+0x5A0
    // Give-up: 60 frames of closing samples, then a decaying average
    // (0x7E38E8 = 0.98333); not closing (0 <= avg < 60) -> detonate.
    if (result == 0 && !p.locked && !p.lost && p.prev_dist >= 0) {
        const int delta = p.prev_dist - dist;
        if (p.giveup_n < 60) {
            ++p.giveup_n;
            p.giveup_acc += delta;
        } else {
            p.giveup_acc = p.giveup_acc * 0.98333333333333328 + delta;
            if (p.giveup_acc >= 0.0 && p.giveup_acc < 60.0 && !p.airburst && !p.very_high) result = 2;
        }
    }
    p.prev_dist = dist;
    if (result == 1) {
        p.x = tgt.x;
        p.y = tgt.y;
        p.z = tgt.z;
    } else if (result == 0) {
        p.x = ftol(static_cast<double>(p.x) + p.vx);
        p.y = ftol(static_cast<double>(p.y) + p.vy);
        p.z = ftol(static_cast<double>(p.z) + p.vz);
        if (p.z < hr.ground_z && !p.level) result = 2;   // into the ground
    }
    p.max_z = std::max(p.max_z, p.z);
    return result;
}

namespace {
std::string strip_comment_trim(const std::string& raw) {
    std::string v = raw.substr(0, raw.find(';'));
    const auto b = v.find_first_not_of(" 	");
    if (b == std::string::npos) return std::string();
    const auto e = v.find_last_not_of(" 	");
    return v.substr(b, e - b + 1);
}
}  // namespace

bool ini_bool_value(const std::string& raw, bool fallback) {
    const std::string v = strip_comment_trim(raw);
    if (v.empty()) return fallback;
    switch (v[0]) {
        case 'y': case 'Y': case 't': case 'T': case '1': return true;
        case 'n': case 'N': case 'f': case 'F': case '0': return false;
        default: return fallback;
    }
}

ProjectileSpec resolve_projectile_spec(const ra2assets::Rules& rules, const std::string& weapon) {
    ProjectileSpec spec;  // Instant (inert) unless proven otherwise
    spec.weapon_id=weapon;
    if (weapon.empty()) return spec;
    const std::string proj = strip_comment_trim(rules.weapon_projectile(weapon));
    if (proj.empty()) return spec;
    auto flag = [&](const char* key) {
        return ini_bool_value(rules.section_string(proj, key, ""), false);
    };
    spec.arcing = flag("Arcing");
    spec.inviso = flag("Inviso");
    spec.inaccurate = flag("Inaccurate");
    spec.flak_scatter = flag("FlakScatter");
    spec.floater = flag("Floater");
    const bool vertical = flag("Vertical");
    spec.rot = om::ftol(rules.section_double(proj, "ROT", 0.0));
    // W4: BulletTypeClass ctor default Acceleration = 3 (gamemd 0x46BB2D:
    // mov eax,3 / mov [esi+2D0h],eax).
    spec.acceleration = om::ftol(rules.section_double(proj, "Acceleration", 3.0));
    spec.level = flag("Level");
    spec.airburst = flag("Airburst");
    spec.very_high = flag("VeryHigh");
    spec.subject_to_cliffs = flag("SubjectToCliffs");         // W5-B
    spec.subject_to_elevation = flag("SubjectToElevation");
    spec.subject_to_walls = flag("SubjectToWalls");
    spec.course_lock = om::ftol(rules.section_double(proj, "CourseLockDuration", 0.0));
    spec.detonation_altitude =
        om::ftol(rules.section_double(proj, "DetonationAltitude", 0.0));
    spec.speed_cap = weapon_speed_leptons(rules.weapon_speed(weapon));
    spec.weapon_range = ftol(rules.weapon_range(weapon) * 256.0);
    if (spec.inviso)
        spec.kind = ProjKind::Instant;      // §11.3 Inviso
    else if (vertical)
        spec.kind = ProjKind::Vertical;     // §6.2
    else if (spec.rot > 0)
        spec.kind = ProjKind::Homing;       // W4: the homing branch (§1.1)
    else
        spec.kind = ProjKind::Ballistic;    // §6.1
    return spec;
}

ProjectileRules resolve_projectile_rules(const ra2assets::Rules& rules) {
    ProjectileRules r;
    // §0.1: Gravity lives in [AudioVisual] (Rules+0x16B8), NOT [General].
    r.gravity = rules.section_double("AudioVisual", "Gravity", 6.0);
    // §2: [CombatDamage] BallisticScatter (cells) -> FUN_00474550 ftol(x*256.0).
    r.ballistic_scatter = ftol(rules.section_double("CombatDamage", "BallisticScatter", 1.0) * 256.0);
    // W4: the homing wobble amplitude and the air-target safety climb
    // (Rules+0x598 / +0x5A0, string-anchored at 0x66E66C / 0x66E68C).
    r.rot_var = rules.general_double("MissileROTVar", 0.25);
    r.safety_altitude = om::ftol(rules.general_double("MissileSafetyAltitude", 750.0));
    return r;
}

}  // namespace ra2engine

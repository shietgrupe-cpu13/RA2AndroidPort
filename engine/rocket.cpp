#include "rocket.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim

#include <algorithm>
#include <cctype>
#include <cmath>

#include "ra2assets/rules.hpp"
#include "weapon_resolve.hpp"   // warhead_wall_flags (W6-04-04)

namespace ra2engine {

namespace {

int ftol_i(double v) { return om::ftol(v); }   // Net L0: the original _ftol (FUN_007C4D00)

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(std::string v) {
    const std::size_t c = v.find_first_of(";,");
    if (c != std::string::npos) v = v.substr(0, c);
    const auto b = v.find_first_not_of(" \t");
    if (b == std::string::npos) return std::string();
    const auto e = v.find_last_not_of(" \t");
    return v.substr(b, e - b + 1);
}

bool yes(const std::string& raw, bool fallback) {
    const std::string v = trimmed(raw);
    if (v.empty()) return fallback;
    switch (v[0]) {
        case 'y': case 'Y': case 't': case 'T': case '1': return true;
        case 'n': case 'N': case 'f': case 'F': case '0': return false;
        default: return fallback;
    }
}

// A [CombatDamage] warhead key ([General] tolerated: older overlays put it there).
std::string warhead_key(const ra2assets::Rules& rules, const char* key, const char* fallback) {
    std::string v = trimmed(rules.section_string("CombatDamage", key, ""));
    if (v.empty()) v = trimmed(rules.section_string("General", key, ""));
    return v.empty() ? std::string(fallback) : v;
}

RocketWarhead resolve_warhead(const ra2assets::Rules& rules, const std::string& id) {
    RocketWarhead w;
    w.id = id;
    w.spread = rules.section_double(id, "CellSpread", 0.0);
    w.pmax = rules.section_double(id, "PercentAtMax", 1.0);
    w.wall_flags = warhead_wall_flags(rules, id);   // W6-04-04
    w.verses.fill(100);
    const std::vector<int> v = rules.verses(id);
    for (std::size_t i = 0; i < v.size() && i < w.verses.size(); ++i) w.verses[i] = v[i];
    return w;
}

RocketBlock resolve_block(const ra2assets::Rules& rules, const std::string& pre, const RocketBlock& d,
                          const char* wh_key, const char* wh_def, const char* ewh_key,
                          const char* ewh_def) {
    RocketBlock b = d;
    auto num = [&](const char* k, double def) { return rules.general_double(pre + k, def); };
    b.pause_frames = om::ftol(num("PauseFrames", d.pause_frames));
    b.tilt_frames = om::ftol(num("TiltFrames", d.tilt_frames));
    b.pitch_initial = num("PitchInitial", d.pitch_initial);
    b.pitch_final = num("PitchFinal", d.pitch_final);
    b.turn_rate = num("TurnRate", d.turn_rate);
    b.raise_rate = om::ftol(num("RaiseRate", d.raise_rate));
    b.acceleration = num("Acceleration", d.acceleration);
    b.altitude = om::ftol(num("Altitude", d.altitude));
    b.damage = om::ftol(num("Damage", d.damage));
    b.elite_damage = om::ftol(num("EliteDamage", d.elite_damage));
    b.body_length = om::ftol(num("BodyLength", d.body_length));
    b.lazy_curve = yes(rules.section_string("General", pre + "LazyCurve", ""), d.lazy_curve);
    const std::string wh = warhead_key(rules, wh_key, wh_def);
    const std::string ewh = warhead_key(rules, ewh_key, ewh_def);
    b.warhead = resolve_warhead(rules, wh);
    // [RE-FALLBACK: an elite warhead id with no [section] in the rules (an RA2
    // rules.ini without the *Elite keys) keeps the normal warhead instead of
    // the engine's all-default WarheadType.]
    b.elite_warhead = rules.section_string(ewh, "Verses", "").empty() ? b.warhead : resolve_warhead(rules, ewh);
    return b;
}

}  // namespace

RocketParams resolve_rocket_params(const ra2assets::Rules& rules) {
    RocketParams p;
    // Stock rulesmd [General] values (the fallbacks for an absent key).
    RocketBlock v3;
    v3.pause_frames = 0; v3.tilt_frames = 60; v3.pitch_initial = 0.21; v3.pitch_final = 0.5;
    v3.turn_rate = 0.05; v3.raise_rate = 1; v3.acceleration = 0.4; v3.altitude = 768;
    v3.damage = 200; v3.elite_damage = 400; v3.body_length = 256; v3.lazy_curve = true;
    RocketBlock dm;
    dm.pause_frames = 20; dm.tilt_frames = 60; dm.pitch_initial = 0.0; dm.pitch_final = 0.5;
    dm.turn_rate = 0.08; dm.raise_rate = 1; dm.acceleration = 0.8; dm.altitude = 768;
    dm.damage = 300; dm.elite_damage = 600; dm.body_length = 128; dm.lazy_curve = false;
    RocketBlock cm;
    cm.pause_frames = 20; cm.tilt_frames = 100; cm.pitch_initial = 1.0; cm.pitch_final = 1.0;
    cm.turn_rate = 0.10; cm.raise_rate = 1; cm.acceleration = 1.0; cm.altitude = 768;
    cm.damage = 200; cm.elite_damage = 250; cm.body_length = 128; cm.lazy_curve = false;
    p.block[kRocketV3] = resolve_block(rules, "V3Rocket", v3, "V3Warhead", "V3WH", "V3EliteWarhead", "V3EWH");
    p.block[kRocketDMisl] =
        resolve_block(rules, "DMisl", dm, "DMislWarhead", "DMISLWH", "DMislEliteWarhead", "DMISLEWH");
    p.block[kRocketCMisl] =
        resolve_block(rules, "CMisl", cm, "CMislWarhead", "CMISLWH", "CMislEliteWarhead", "CMISLEWH");
    auto type = [&](const char* k, const char* def) {
        const std::string v = trimmed(rules.section_string("General", k, ""));
        return v.empty() ? std::string(def) : v;
    };
    p.type_id[kRocketV3] = type("V3RocketType", "V3ROCKET");
    p.type_id[kRocketDMisl] = type("DMislType", "DMISL");
    p.type_id[kRocketCMisl] = type("CMislType", "CMISL");
    return p;
}

namespace {
constexpr double kHalfPi = 1.57079632679489661923;   // 0x7E1820
}  // namespace

void rocket_launch(const RocketBlock& b, RocketFlight& f, double x, double y, double z, double tx,
                   double ty) {
    f = RocketFlight{};
    f.phase = kRocketPause;
    f.timer = std::max(0, b.pause_frames);
    f.pitch = b.pitch_initial * kHalfPi;
    f.speed = 0.0;
    f.x = x;
    f.y = y;
    f.z = z;
    const double dx = tx - x, dy = ty - y;
    f.yaw = (dx == 0.0 && dy == 0.0) ? 0.0 : om::atan2(dy, dx);
}

void rocket_nose(const RocketBlock& b, const RocketFlight& f, double& x, double& y, double& z) {
    const double c = om::cos(f.pitch);
    x = f.x + b.body_length * c * om::cos(f.yaw);
    y = f.y + b.body_length * c * om::sin(f.yaw);
    z = f.z + b.body_length * om::sin(f.pitch);
}

namespace {
// FUN_00661B30: the nose at or below the target's height, or the rocket's own
// height below 1 [the bridge-layer case is not modelled: the clone's combat
// plane is flat].
bool nose_contact(const RocketBlock& b, const RocketFlight& f, double tz) {
    double nx = 0, ny = 0, nz = 0;
    rocket_nose(b, f, nx, ny, nz);
    return ftol_i(nz) <= ftol_i(tz) || f.z < 1.0;
}
}  // namespace

int rocket_step(const RocketBlock& b, int speed_cap, RocketFlight& f, double tx, double ty, double tz,
                bool vertical_raise) {
    ++f.frames;
    int result = kRocketFlying;
    const double cap = static_cast<double>(std::max(1, speed_cap));
    auto accelerate = [&]() {
        f.speed = std::min(cap, f.speed + b.acceleration);   // +0x18, capped at type+0x678
    };
    switch (f.phase) {
        case kRocketPause:
            f.speed = 0.0;
            if (f.timer > 0) {
                --f.timer;
                if (f.timer > 0) break;
            }
            f.phase = kRocketTilt;
            f.timer = std::max(0, b.tilt_frames);
            if (f.timer > 0) break;
            [[fallthrough]];
        case kRocketTilt: {
            f.speed = 0.0;
            const double p0 = b.pitch_initial * kHalfPi, p1 = b.pitch_final * kHalfPi;
            if (f.timer > 0) {
                --f.timer;
                const int total = std::max(1, b.tilt_frames);
                const double t = static_cast<double>(total - f.timer) / total;
                if (f.timer > 0) {
                    // CMisl uses the configured RaiseRate during its launch
                    // interval, not the stationary V3/Dreadnought tilt arm.
                    if(vertical_raise){f.z+=b.raise_rate;break;}
                    f.pitch = p0 + (p1 - p0) * t;
                    break;
                }
            }
            // End of the tilt: PitchFinal, leave the launcher (launch anim).
            f.pitch = p1;
            f.phase = kRocketClimb;
            result = kRocketLeftLauncher;
            break;
        }
        case kRocketClimb:
            accelerate();
            if (static_cast<double>(b.altitude) <= f.z) {
                f.phase = kRocketCruise;
                f.horiz0 = ftol_i(om::hypot(tx - f.x, ty - f.y));
            }
            break;
        case kRocketCruise: {
            accelerate();
            const double horiz = om::hypot(tx - f.x, ty - f.y);
            if (!b.lazy_curve || f.horiz0 == 0) {
                // LazyCurve=no: level off by TurnRate, then dive once the
                // target is inside the 45-degree cone below [RE-FALLBACK: the
                // decompile's distance term is ambiguous; the horizontal
                // distance against the height above the target is the reading
                // that dives instead of overflying].
                if (f.pitch > 0.0) {
                    f.pitch -= b.turn_rate;
                    if (f.pitch < 0.0) f.pitch = 0.0;
                }
                if (ftol_i(horiz) <= ftol_i(f.z - tz)) f.phase = kRocketDive;
            } else {
                if (nose_contact(b, f, tz)) return kRocketDetonate;
                // LazyCurve=yes: blend the line of sight and PitchFinal by the
                // remaining horizontal fraction -- the big lazy arc.
                const double t = horiz / static_cast<double>(f.horiz0);
                const double los = horiz <= 0.0 ? -kHalfPi : om::atan((tz - f.z) / horiz);
                f.pitch = los * (1.0 - t) + b.pitch_final * kHalfPi * t;
            }
            if (horiz > 0.0) f.yaw = om::atan2(ty - f.y, tx - f.x);   // re-aimed every frame
            break;
        }
        case kRocketDive: {
            if (nose_contact(b, f, tz)) return kRocketDetonate;
            const double horiz = om::hypot(tx - f.x, ty - f.y);
            const double want = horiz <= 0.0 ? -kHalfPi : om::atan((tz - f.z) / horiz);   // 0x7EFC10
            const double d = want - f.pitch;
            if (std::fabs(d) <= b.turn_rate) f.pitch += d;
            else f.pitch += d > 0.0 ? b.turn_rate : -b.turn_rate;
            break;
        }
        default:
            break;
    }
    // Common tail: move speed along the pitch and the yaw.
    if (f.speed > 0.0) {
        const double c = om::cos(f.pitch);
        f.x += f.speed * c * om::cos(f.yaw);
        f.y += f.speed * c * om::sin(f.yaw);
        f.z += f.speed * om::sin(f.pitch);
    }
    return result;
}

int rocket_kind_for(const RocketParams& p, const std::string& unit_id) {
    const std::string u = upper(unit_id);
    if (u == upper(p.type_id[kRocketV3])) return kRocketV3;
    if (u == upper(p.type_id[kRocketCMisl])) return kRocketCMisl;
    return kRocketDMisl;
}

}  // namespace ra2engine

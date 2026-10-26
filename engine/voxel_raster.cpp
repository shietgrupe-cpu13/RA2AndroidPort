#include "voxel_raster.hpp"
#include "terrain_tilt.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cctype>

namespace ra2engine {
namespace {
#include "voxel_normals.inc"   // kNormals2[36][3], kNormals4[245][3] (voxel-render.md §3)

// The repo compiles with CMAKE_CXX_EXTENSIONS OFF (strict -std=c++17), so
// <cmath> M_PI is not guaranteed. Use a local constant rather than defining
// _USE_MATH_DEFINES globally.
constexpr double kPi = 3.14159265358979323846;

struct V3 { double x, y, z; };
std::size_t hva_section(const ra2assets::HvaModel& h, const std::string& name,
                        std::size_t fallback, std::size_t count) {
    auto equal=[](const std::string& a,const std::string& b) {
        if(a.size()!=b.size()) return false;
        for(std::size_t i=0;i<a.size();++i)
            if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i]))) return false;
        return true;
    };
    for(std::size_t i=0;i<h.section_names.size() && i<count;++i)
        if(equal(name,h.section_names[i])) return i;
    return std::min(fallback,count-1); // unnamed legacy fixtures retain index ordering
}

// hva 3x4 (rows) applied to p, translation column pre-scaled by `scale`
// [RE: voxel-render.md §1].
V3 apply_xform(const float t[3][4], float scale, const V3& p) {
    return {
        t[0][0] * p.x + t[0][1] * p.y + t[0][2] * p.z + t[0][3] * scale,
        t[1][0] * p.x + t[1][1] * p.y + t[1][2] * p.z + t[1][3] * scale,
        t[2][0] * p.x + t[2][1] * p.y + t[2][2] * p.z + t[2][3] * scale,
    };
}

V3 yaw_rotate(const V3& p, double c, double s) {
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

// W4: nose-up pitch about the model's +Y axis (the nose is +X), applied
// before the yaw. cp = 1, sp = 0 is the identity (every unpitched caller).
V3 pitch_rotate(const V3& p, double cp, double sp) {
    if (sp == 0.0) return p;
    return {p.x * cp - p.z * sp, p.y, p.x * sp + p.z * cp};
}

void project(const V3& w, double& sx, double& sy, double& depth) {
    // voxel-render.md §1: basis Rx(-60)*Rz(-45); depth = third row.
    sx = (w.x + w.y) * kIsoX;
    sy = (w.x - w.y) * kIsoY - w.z * kIsoZ;
    depth = 0.61237 * (w.x - w.y) + 0.5 * w.z;
}

// voxel-render.md §3: level = ftol((d + s) * 16), d = diffuse, s = specular
// h/(3-2h); L/H are the light dir already rotated into MODEL space by the
// caller (world-fixed light). Indices 253-255 -> hard-wired level 16 (a
// self-lit marker level; row 8 is the base/identity row). Clamp to
// section_count-1 (d+s can reach ~2.0).
int light_level(uint8_t normal_ix, uint8_t mode, uint32_t levels,
                const double L[3], const double H[3]) {
    if (normal_ix >= 253) return std::min<int>(16, static_cast<int>(levels) - 1);
    const float(*tab)[3] = (mode == 2) ? kNormals2 : kNormals4;
    const int tab_n = (mode == 2) ? 36 : 245;
    const int ix = std::min<int>(normal_ix, tab_n - 1);
    const double d = std::max(0.0, tab[ix][0] * L[0] + tab[ix][1] * L[1] + tab[ix][2] * L[2]);
    const double h = std::max(0.0, tab[ix][0] * H[0] + tab[ix][1] * H[1] + tab[ix][2] * H[2]);
    const double s = h / (3.0 - 2.0 * h);
    const int lv = static_cast<int>((d + s) * 16.0);  // ftol = truncate
    return std::clamp(lv, 0, static_cast<int>(levels) - 1);
}
}  // namespace

int facing_to_index(double facing_u16, int n) {
    const double per = 65536.0 / n;
    const int i = static_cast<int>(std::floor(facing_u16 / per + 0.5));
    return ((i % n) + n) % n;
}

double index_to_yaw(int i, int n) {
    // voxel-render.md §2 (AMENDED): yaw(i) = (i - n/4) * (-2*pi/n).
    // Index n/4 (facing 0x4000, screen East) is the unrotated model (+X
    // nose points down-right); increasing facing rotates CLOCKWISE on
    // screen. For n=32: yaw(8)=0, yaw(0)=+pi/2 (North).
    return (i - n / 4) * (-2.0 * kPi / n);
}

VoxelSprite rasterize_model(const ra2assets::VxlModel& m, const ra2assets::HvaModel& hva,
                            double yaw_rad, const ra2assets::Vpl& vpl,
                            const ra2assets::Palette& pal, int hva_frame, double pitch_rad, int ramp) {
    const double c = std::cos(yaw_rad), s = std::sin(yaw_rad);
    const double cp = pitch_rad == 0.0 ? 1.0 : std::cos(pitch_rad);
    const double sp = pitch_rad == 0.0 ? 0.0 : std::sin(pitch_rad);
    const auto tilt=terrain_tilt(ramp);
    auto oriented=[&](V3 p) {
        p=yaw_rotate(pitch_rotate(p,cp,sp),c,s);
        if(ramp==0) return p; // preserve flat-ground raster output exactly
        const auto q=tilt.apply(p.x,p.y,p.z);
        return V3{q[0],q[1],q[2]};
    };

    VoxelSprite out;
    if (m.sections.empty() || hva.frames.empty() || hva.frames[0].empty()) return out;
    // W3-B: the HVA frame (rotor / propeller anims); 0 = the old behaviour.
    const std::size_t hf = static_cast<std::size_t>(hva_frame < 0 ? 0 : hva_frame) % hva.frames.size();
    if (hva.frames[hf].empty()) return out;

    // Pass 1: bounds. Project each section's 8 transformed bbox corners.
    double mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
    for (size_t si = 0; si < m.sections.size(); ++si) {
        const auto& sec = m.sections[si];
        const auto& xf = hva.frames.at(hf).at(hva_section(hva,sec.name,si,hva.frames[hf].size()));
        for (int corner = 0; corner < 8; ++corner) {
            const V3 lo = {sec.min_bounds[0], sec.min_bounds[1], sec.min_bounds[2]};
            const V3 hi = {sec.max_bounds[0], sec.max_bounds[1], sec.max_bounds[2]};
            const V3 p = {(corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                          (corner & 4) ? hi.z : lo.z};
            double sx, sy, d;
            project(oriented(apply_xform(xf.transform, sec.scale, p)), sx, sy, d);
            mnx = std::min(mnx, sx); mxx = std::max(mxx, sx);
            mny = std::min(mny, sy); mxy = std::max(mxy, sy);
        }
    }
    if (mnx > mxx) return out;

    const int pad = 2;  // 1px pad + the voxel footprint below
    out.w = static_cast<int>(std::ceil(mxx - mnx)) + 2 * pad + 1;
    out.h = static_cast<int>(std::ceil(mxy - mny)) + 2 * pad + 1;
    out.anchor_x = static_cast<int>(std::round(-mnx)) + pad;
    out.anchor_y = static_cast<int>(std::round(-mny)) + pad;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    std::vector<double> zbuf(static_cast<size_t>(out.w) * out.h,
                             -std::numeric_limits<double>::infinity());

    // Light dirs rotated into model space ONCE per call (world-fixed light):
    const double lc = std::cos(-yaw_rad), ls = std::sin(-yaw_rad);
    const auto light=tilt.inverse(kLightWorld[0],kLightWorld[1],kLightWorld[2]);
    const double L[3] = {light[0] * lc - light[1] * ls,
                         light[0] * ls + light[1] * lc, light[2]};
    double H[3] = {L[0], L[1], L[2] + 1.0};
    const double hn = std::sqrt(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
    H[0] /= hn; H[1] /= hn; H[2] /= hn;

    // Pass 2: voxels.
    for (size_t si = 0; si < m.sections.size(); ++si) {
        const auto& sec = m.sections[si];
        const auto& xf = hva.frames.at(hf).at(hva_section(hva,sec.name,si,hva.frames[hf].size()));
        const double stepx = (sec.max_bounds[0] - sec.min_bounds[0]) / sec.size_x;
        const double stepy = (sec.max_bounds[1] - sec.min_bounds[1]) / sec.size_y;
        const double stepz = (sec.max_bounds[2] - sec.min_bounds[2]) / sec.size_z;
        // A normal's light level depends only on this call's orientation,
        // VPL level count and section normal mode. Reuse the exact arithmetic
        // result for repeated normals; no transforms or depth tests change.
        std::array<int,256> light_by_normal;
        light_by_normal.fill(-1);
        for (int z = 0; z < sec.size_z; ++z)
            for (int y = 0; y < sec.size_y; ++y)
                for (int x = 0; x < sec.size_x; ++x) {
                    const auto& v = sec.voxels[x + static_cast<size_t>(sec.size_x) *
                                                       (y + static_cast<size_t>(sec.size_y) * z)];
                    if (!v.present) continue;
                    // Corner-anchored (voxel-render.md §1): no +0.5.
                    const V3 local = {sec.min_bounds[0] + x * stepx,
                                      sec.min_bounds[1] + y * stepy,
                                      sec.min_bounds[2] + z * stepz};
                    double sx, sy, depth;
                    project(oriented(apply_xform(xf.transform, sec.scale, local)),
                            sx, sy, depth);
                    const int px = static_cast<int>(std::round(sx)) + out.anchor_x;
                    const int py = static_cast<int>(std::round(sy)) + out.anchor_y;
                    int& lv = light_by_normal[v.normal];
                    if(lv<0)lv=light_level(v.normal,sec.normals_mode,vpl.section_count,L,H);
                    const ra2assets::Rgba col = pal[vpl.tables[lv][v.color]];
                    // 2x1 footprint (voxel-render.md §1).
                    for (int dx = 0; dx < 2; ++dx) {
                        const int qx = px + dx, qy = py;
                        if (qx < 0 || qy < 0 || qx >= out.w || qy >= out.h) continue;
                        const size_t at = static_cast<size_t>(qy) * out.w + qx;
                        if (depth <= zbuf[at]) continue;
                        zbuf[at] = depth;
                        out.rgba[at * 4 + 0] = col.r;
                        out.rgba[at * 4 + 1] = col.g;
                        out.rgba[at * 4 + 2] = col.b;
                        out.rgba[at * 4 + 3] = 255;
                    }
                }
    }
    return out;
}

VoxelSprite rasterize_shadow(const ra2assets::VxlModel& m, const ra2assets::HvaModel& hva,
                             double yaw_rad, int alpha, int ramp) {
    const double c = std::cos(yaw_rad), s = std::sin(yaw_rad);
    const auto tilt=terrain_tilt(ramp);
    VoxelSprite out;
    if (m.sections.empty() || hva.frames.empty() || hva.frames[0].empty()) return out;
    // Collect the flattened projections first (bounds), then plot.
    std::vector<std::pair<int, int>> pts;
    double mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
    for (size_t si = 0; si < m.sections.size(); ++si) {
        const auto& sec = m.sections[si];
        const auto& xf = hva.frames.at(0).at(hva_section(hva,sec.name,si,hva.frames[0].size()));
        const double stepx = (sec.max_bounds[0] - sec.min_bounds[0]) / sec.size_x;
        const double stepy = (sec.max_bounds[1] - sec.min_bounds[1]) / sec.size_y;
        const double stepz = (sec.max_bounds[2] - sec.min_bounds[2]) / sec.size_z;
        for (int z = 0; z < sec.size_z; ++z)
            for (int y = 0; y < sec.size_y; ++y)
                for (int x = 0; x < sec.size_x; ++x) {
                    const auto& v = sec.voxels[x + static_cast<size_t>(sec.size_x) *
                                                       (y + static_cast<size_t>(sec.size_y) * z)];
                    if (!v.present) continue;
                    const V3 local = {sec.min_bounds[0] + x * stepx, sec.min_bounds[1] + y * stepy,
                                      sec.min_bounds[2] + z * stepz};
                    V3 w = yaw_rotate(apply_xform(xf.transform, sec.scale, local), c, s);
                    const auto tilted=tilt.apply(w.x,w.y,w.z);
                    w.x=tilted[0];w.y=tilted[1];
                    w.z = 0.0;   // flattened onto the ground plane
                    double sx, sy, d;
                    project(w, sx, sy, d);
                    pts.push_back({static_cast<int>(std::round(sx)), static_cast<int>(std::round(sy))});
                    mnx = std::min(mnx, sx); mxx = std::max(mxx, sx);
                    mny = std::min(mny, sy); mxy = std::max(mxy, sy);
                }
    }
    if (pts.empty()) return out;
    const int pad = 2;
    out.anchor_x = static_cast<int>(std::round(-mnx)) + pad;
    out.anchor_y = static_cast<int>(std::round(-mny)) + pad;
    out.w = static_cast<int>(std::ceil(mxx - mnx)) + 2 * pad + 2;
    out.h = static_cast<int>(std::ceil(mxy - mny)) + 2 * pad + 1;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    const uint8_t a = static_cast<uint8_t>(std::max(0, std::min(255, alpha)));
    for (const auto& p : pts)
        for (int dx = 0; dx < 2; ++dx) {   // the hull's 2x1 footprint
            const int qx = p.first + out.anchor_x + dx, qy = p.second + out.anchor_y;
            if (qx < 0 || qy < 0 || qx >= out.w || qy >= out.h) continue;
            out.rgba[(static_cast<size_t>(qy) * out.w + qx) * 4 + 3] = a;
        }
    return out;
}

}  // namespace ra2engine

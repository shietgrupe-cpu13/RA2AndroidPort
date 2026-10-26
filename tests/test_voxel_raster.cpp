#include "doctest.h"
#include "voxel_raster.hpp"
#include "terrain_tilt.hpp"
#include "voxel_animation_cache.hpp"
#include "ra2assets/vxl.hpp"
#include "ra2assets/hva.hpp"
#include "ra2assets/vpl.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("voxel lighting: memoization preserves the pre-change pixel corpus") {
    // Independently generated assets; goldens frozen from a4577e9 before
    // normal-light reuse. They establish output preservation, not PC parity.
    ra2assets::VxlModel model;ra2assets::VxlSection section{};
    section.name="body";section.size_x=section.size_y=40;section.size_z=24;section.scale=0.0625;
    section.min_bounds[0]=section.min_bounds[1]=-20;section.min_bounds[2]=0;
    section.max_bounds[0]=section.max_bounds[1]=20;section.max_bounds[2]=24;
    section.voxels.resize(40*40*24);
    for(std::size_t i=0;i<section.voxels.size();++i) {
        auto& v=section.voxels[i];v.present=i%7!=0;v.normal=static_cast<uint8_t>(i%256);v.color=static_cast<uint8_t>(1+i%255);
    }
    model.sections.push_back(section);
    ra2assets::HvaModel hva;hva.frame_count=2;hva.section_names={"body"};
    hva.frames.resize(2,std::vector<ra2assets::HvaSectionXform>(1));
    for(auto& f:hva.frames)f[0].transform[0][0]=f[0].transform[1][1]=f[0].transform[2][2]=1;
    hva.frames[1][0].transform[0][1]=0.125;hva.frames[1][0].transform[1][0]=-0.25;hva.frames[1][0].transform[2][3]=5;
    ra2assets::Vpl vpl;vpl.section_count=32;vpl.tables.resize(32);
    for(int l=0;l<32;++l)for(int c=0;c<256;++c)vpl.tables[l][c]=static_cast<uint8_t>((c+l*7)%256);
    ra2assets::Palette pal{};
    for(int c=0;c<256;++c)pal[c]={static_cast<uint8_t>(c),static_cast<uint8_t>(255-c),static_cast<uint8_t>((c*17)%256),255};
    const std::uint64_t expected[]={10648953186822083836ull,1542018878348848889ull,11643546570104464420ull,
        6498029822417324300ull,654116153814205069ull,6793730798480642762ull,5855072078042587445ull,
        8198710730002339777ull,5450322192246802704ull,11708068853678602759ull,6106300286908376974ull,
        8185425042783939714ull,17135073159603382555ull,15724758639675001596ull,2983200812574123843ull,
        582620714650974465ull,16313397727477027545ull,18430286955755373869ull,2497314096417208839ull,
        16565973376344325639ull};
    int index=0;
    for(int mode:{2,4})for(int frame:{0,1})for(int ramp:{0,1,5,13,1226}) {
        model.sections[0].normals_mode=mode;
        const auto image=ra2engine::rasterize_model(model,hva,ra2engine::index_to_yaw(3,32),vpl,pal,frame,0.12,ramp);
        std::uint64_t hash=14695981039346656037ull;for(auto byte:image.rgba)hash=(hash^byte)*1099511628211ull;
        INFO("normal mode="<<mode<<" frame="<<frame<<" ramp="<<ramp);CHECK(hash==expected[index++]);
    }
}

TEST_CASE("voxel animation cache: frame-boundary eviction preserves live part references") {
    ra2engine::VoxelAnimationCache cache(700);
    cache.begin_frame(1,1);
    ra2engine::VoxelSprite hull;hull.w=10;hull.rgba.assign(400,42);
    const auto* held=&cache.insert("hull",std::move(hull));
    ra2engine::VoxelSprite turret;turret.rgba.assign(400,17);
    cache.insert("turret",std::move(turret));
    CHECK(cache.bytes()>700);
    cache.begin_frame(1,1); // Another unit of the same type in this frame.
    CHECK(cache.find("hull")==held);CHECK(held->rgba[0]==42);
    CHECK(cache.evicted()==0);
    cache.begin_frame(1,2);
    CHECK(cache.evicted()==0); // Preserve the previous frame's working set.
    cache.begin_frame(1,3);
    CHECK(cache.bytes()<=700);CHECK(cache.evicted()>0);
    // A new renderer may restart frame numbering; it is still a new epoch.
    for(int frame=4;frame<100;++frame) {
        cache.begin_frame(1,frame);CHECK(cache.size()<=2);
        ra2engine::VoxelSprite s;s.rgba.assign(1024,1);
        cache.insert(std::to_string(frame),std::move(s));
    }
    cache.begin_frame(2,1);cache.begin_frame(2,2);CHECK(cache.bytes()<=700);
}

TEST_CASE("voxel animation cache: reuse retains a frequently drawn facing") {
    ra2engine::VoxelAnimationCache cache(700);
    cache.begin_frame(1,1);
    for(const char* key:{"old","hot"}) {
        ra2engine::VoxelSprite s;s.rgba.assign(200,1);cache.insert(key,std::move(s));
    }
    cache.begin_frame(1,2);REQUIRE(cache.find("hot")!=nullptr);
    ra2engine::VoxelSprite s;s.rgba.assign(200,1);cache.insert("new",std::move(s));
    cache.begin_frame(1,3);
    CHECK(cache.find("old")==nullptr);CHECK(cache.find("hot")!=nullptr);
}

TEST_CASE("voxel animation cache: a busy working set above budget does not churn") {
    ra2engine::VoxelAnimationCache cache(100);
    cache.begin_frame(1,1);
    for(int i=0;i<10;++i) {
        ra2engine::VoxelSprite s;s.rgba.assign(256,1);cache.insert(std::to_string(i),std::move(s));
    }
    for(int frame=2;frame<12;++frame) {
        cache.begin_frame(1,frame);
        for(int i=0;i<10;++i) REQUIRE(cache.find(std::to_string(i))!=nullptr);
        CHECK(cache.evicted()==0);
    }
    cache.begin_frame(1,12);cache.begin_frame(1,13);
    CHECK(cache.bytes()<=100);CHECK(cache.evicted()==10);
}

TEST_CASE("terrain tilt: ramp axes preserve length and invert without changing flat ground") {
    for(int ramp=0;ramp<=20;++ramp) {
        const auto t=ra2engine::terrain_tilt(ramp);
        const auto p=t.apply(3,4,12);
        CHECK(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]==doctest::Approx(169));
        const auto q=t.inverse(p[0],p[1],p[2]);
        CHECK(q[0]==doctest::Approx(3));
        CHECK(q[1]==doctest::Approx(4));
        CHECK(q[2]==doctest::Approx(12));
    }
    CHECK(ra2engine::terrain_tilt(1).apply(1,0,0)[2]>0);
    CHECK(ra2engine::terrain_tilt(3).apply(1,0,0)[2]<0);
    CHECK(ra2engine::terrain_tilt(2).apply(0,1,0)[2]<0);
    CHECK(ra2engine::terrain_tilt(4).apply(0,1,0)[2]>0);
    CHECK(ra2engine::terrain_tilt(255).apply(3,4,12)==std::array<double,3>{3,4,12});
}

using ra2assets::VxlModel;
using ra2assets::VxlSection;
using ra2assets::HvaModel;
using ra2assets::HvaSectionXform;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
// One 1x1x1-section model holding a single voxel with the given color/normal,
// identity transform, bounds centered on the origin (so the voxel center IS
// the model origin), normals_mode 2.
VxlModel single_voxel_model(uint8_t color, uint8_t normal) {
    VxlModel m;
    VxlSection s;
    s.name = "T";
    s.size_x = s.size_y = s.size_z = 1;
    s.scale = 1.0f;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) s.transform[r][c] = (r == c) ? 1.f : 0.f;
    for (int a = 0; a < 3; ++a) { s.min_bounds[a] = -0.5f; s.max_bounds[a] = 0.5f; }
    s.normals_mode = 2;
    s.voxels = { {color, normal, true} };
    m.sections.push_back(s);
    return m;
}
HvaModel identity_hva() {
    HvaModel h;
    h.frame_count = 1;
    h.section_names = {"T"};
    HvaSectionXform x{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) x.transform[r][c] = (r == c) ? 1.f : 0.f;
    h.frames = {{x}};
    return h;
}
// Identity-ish vpl: every table row is the identity map, palette gray ramp.
ra2assets::Vpl identity_vpl() {
    std::vector<uint8_t> raw(16 + 768 + 32 * 256, 0);
    raw[8] = 32;  // section_count
    for (int i = 0; i < 256; ++i) {
        raw[16 + i * 3 + 0] = raw[16 + i * 3 + 1] = raw[16 + i * 3 + 2] =
            static_cast<uint8_t>(i / 4);  // 6-bit gray ramp
        for (int t = 0; t < 32; ++t) raw[16 + 768 + t * 256 + i] = static_cast<uint8_t>(i);
    }
    return ra2assets::Vpl::load(raw.data(), raw.size());
}
ra2assets::Palette gray_pal() {
    // Palette::load expands <<2; feed the same gray ramp for consistency.
    uint8_t raw[768];
    for (int i = 0; i < 256; ++i) raw[i*3] = raw[i*3+1] = raw[i*3+2] = static_cast<uint8_t>(i / 4);
    return ra2assets::Palette::load(raw, 768);
}
int opaque_count(const ra2engine::VoxelSprite& s) {
    int n = 0;
    for (size_t i = 3; i < s.rgba.size(); i += 4) if (s.rgba[i] != 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("voxel_raster: HVA animation changes the drawn anchor and wraps exactly") {
    const auto model=single_voxel_model(100,0);auto hva=identity_hva();
    hva.frames.push_back(hva.frames[0]);hva.frames[1][0].transform[0][3]=8;
    hva.frame_count=2;
    const auto vpl=identity_vpl();const auto pal=gray_pal();
    const auto first=ra2engine::rasterize_model(model,hva,0,vpl,pal,0);
    const auto second=ra2engine::rasterize_model(model,hva,0,vpl,pal,1);
    const auto wrapped=ra2engine::rasterize_model(model,hva,0,vpl,pal,2);
    REQUIRE(opaque_count(first)>0);REQUIRE(opaque_count(second)>0);
    CHECK((first.anchor_x!=second.anchor_x || first.anchor_y!=second.anchor_y));
    CHECK(wrapped.anchor_x==first.anchor_x);CHECK(wrapped.anchor_y==first.anchor_y);
    CHECK(wrapped.rgba==first.rgba);
}

TEST_CASE("voxel shadow: terrain orientation changes silhouette without animating the shadow frame") {
 auto model=single_voxel_model(100,0);model.sections[0].min_bounds[2]=20;model.sections[0].max_bounds[2]=21;
 auto h=identity_hva();const auto flat=ra2engine::rasterize_shadow(model,h,0,128);
 const auto explicit_flat=ra2engine::rasterize_shadow(model,h,0,128,0);
 CHECK(flat.rgba==explicit_flat.rgba);CHECK(flat.anchor_x==explicit_flat.anchor_x);
 const auto slope=ra2engine::rasterize_shadow(model,h,0,128,1);
 CHECK(opaque_count(slope)>0);CHECK(slope.anchor_x!=flat.anchor_x);
 h.frames.push_back(h.frames[0]);h.frames[1][0].transform[0][3]=99;h.frame_count=2;
 const auto still=ra2engine::rasterize_shadow(model,h,0,128,1);
 CHECK(still.rgba==slope.rgba);CHECK(still.anchor_x==slope.anchor_x);
}

TEST_CASE("voxel_raster: single voxel lands on the anchor") {
    auto spr = ra2engine::rasterize_model(single_voxel_model(100, 0), identity_hva(),
                                          0.0, identity_vpl(), gray_pal());
    REQUIRE(spr.w > 0);
    REQUIRE(spr.h > 0);
    CHECK(opaque_count(spr) >= 1);
    // The voxel center is the model origin, so its pixels must cover the
    // anchor point (within the 1-voxel footprint).
    bool anchor_covered = false;
    for (int dy = -2; dy <= 2 && !anchor_covered; ++dy)
        for (int dx = -2; dx <= 2 && !anchor_covered; ++dx) {
            const int x = spr.anchor_x + dx, y = spr.anchor_y + dy;
            if (x < 0 || y < 0 || x >= spr.w || y >= spr.h) continue;
            if (spr.rgba[(static_cast<size_t>(y) * spr.w + x) * 4 + 3] != 0) anchor_covered = true;
        }
    CHECK(anchor_covered);
}

TEST_CASE("voxel_raster: terrain is applied after facing and flat sprites stay identical") {
    auto model=single_voxel_model(100,0);
    // An off-origin voxel makes the slope's height effect observable.
    model.sections[0].min_bounds[0]=20;
    model.sections[0].max_bounds[0]=21;
    const auto h=identity_hva();const auto v=identity_vpl();const auto pal=gray_pal();
    const auto flat=ra2engine::rasterize_model(model,h,0,v,pal);
    const auto explicit_flat=ra2engine::rasterize_model(model,h,0,v,pal,0,0,0);
    CHECK(flat.rgba==explicit_flat.rgba);
    CHECK(flat.anchor_y==explicit_flat.anchor_y);
    const auto uphill=ra2engine::rasterize_model(model,h,0,v,pal,0,0,1);
    const auto downhill=ra2engine::rasterize_model(model,h,0,v,pal,0,0,3);
    REQUIRE(opaque_count(uphill)>0);
    REQUIRE(opaque_count(downhill)>0);
    auto relative_y=[](const ra2engine::VoxelSprite& s) {
        double sum=0;int n=0;
        for(int y=0;y<s.h;++y) for(int x=0;x<s.w;++x)
            if(s.rgba[(y*s.w+x)*4+3]) {sum+=y-s.anchor_y;++n;}
        return sum/n;
    };
    CHECK(relative_y(uphill)<relative_y(flat));
    CHECK(relative_y(downhill)>relative_y(flat));
    // Turning perpendicular to the uphill axis removes that axis' rise.
    const auto side=ra2engine::rasterize_model(model,h,3.14159265358979323846/2,v,pal,0,0,1);
    const auto side_flat=ra2engine::rasterize_model(model,h,3.14159265358979323846/2,v,pal);
    CHECK(std::abs(relative_y(side)-relative_y(side_flat))<=1.0);
}

TEST_CASE("voxel_raster: yaw rotation moves an off-axis voxel as hand-computed") {
    // Two-voxel column along model +X: bounds [0,2] x [0,1] x [0,1], 2x1x1.
    // Corner-anchored positions (voxel-render.md §1): voxel (1,0,0) sits at
    // local (1, 0, 0) exactly -- pure +X, so its projected offset from the
    // anchor is sign-definite under the recovered basis (no y/z bleed).
    VxlModel m = single_voxel_model(100, 0);
    m.sections[0].size_x = 4;
    m.sections[0].min_bounds[0] = 0.0f; m.sections[0].max_bounds[0] = 4.0f;
    m.sections[0].min_bounds[1] = 0.0f; m.sections[0].max_bounds[1] = 1.0f;
    m.sections[0].min_bounds[2] = 0.0f; m.sections[0].max_bounds[2] = 1.0f;
    m.sections[0].voxels = { {0, 0, false}, {0, 0, false}, {0, 0, false},
                             {100, 0, true} };  // only x=3 present: local (3, 0, 0)
    auto at0 = ra2engine::rasterize_model(m, identity_hva(), 0.0, identity_vpl(), gray_pal());
    auto at180 = ra2engine::rasterize_model(m, identity_hva(), M_PI, identity_vpl(), gray_pal());
    // At yaw 0 the voxel is at world (3,0,0) -> sx = +2.12, sy = +1.06 px
    // (down-right, screen East -- whole-pixel offsets survive rounding); at
    // yaw pi it is (-3,0,0) -> sx = -2.12, sy = -1.06. Find each sprite's
    // opaque centroid relative to its anchor and check the sign flip.
    auto centroid_rel = [](const ra2engine::VoxelSprite& s, double& cx, double& cy) {
        double sx = 0, sy = 0; int n = 0;
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x)
                if (s.rgba[(static_cast<size_t>(y) * s.w + x) * 4 + 3] != 0) { sx += x; sy += y; ++n; }
        REQUIRE(n > 0);
        cx = sx / n - s.anchor_x;
        cy = sy / n - s.anchor_y;
    };
    double x0, y0, x1, y1;
    centroid_rel(at0, x0, y0);
    centroid_rel(at180, x1, y1);
    CHECK(x0 > 0.0);       // +X model axis projects right-and-down (diamond shear)
    CHECK(y0 > 0.0);
    CHECK(x1 < 0.0);       // opposite yaw mirrors both
    CHECK(y1 < 0.0);
}

TEST_CASE("voxel_raster: normal index selects the vpl row") {
    // A vpl whose table t maps EVERY color to t (so the output pixel value
    // reveals which lighting level was chosen). Combined with gray_pal (which
    // maps palette index j -> gray value j, because (j/4)<<2 == j for j%4==0),
    // the first opaque pixel's R channel equals level*8.
    std::vector<uint8_t> raw(16 + 768 + 32 * 256, 0);
    raw[8] = 32;
    for (int i = 0; i < 256; ++i) {
        raw[16 + i * 3] = raw[16 + i * 3 + 1] = raw[16 + i * 3 + 2] = static_cast<uint8_t>(i / 4);
        for (int t = 0; t < 32; ++t) raw[16 + 768 + t * 256 + i] = static_cast<uint8_t>(t * 8);
    }
    auto vpl = ra2assets::Vpl::load(raw.data(), raw.size());
    // Hand-computed lighting at yaw 0 (voxel-render.md §3). The world light is
    // rotated into model space: L_model = Rz(-0)*(-0.7071,-0.7071,0) =
    // (-0.7071,-0.7071,0); H_model = normalize(L+(0,0,1)) = (-0.5,-0.5,0.7071).
    // level = trunc((max(0,n.L) + s)*16), s = h/(3-2h), h = max(0,n.H), clamped
    // to [0, section_count-1]. Chosen indices 4 and 22 of kNormals2 (brief's
    // 0/18 both give level 0 -- both normals point away from the light):
    //
    //   n[4]  = (-0.171400994, -0.572709978, -0.80163902):
    //     d = 0.7071*(0.171400994+0.572709978) = 0.52616; h < 0 -> s = 0
    //     level = trunc(0.52616*16) = trunc(8.4186) = 8  -> gray 8*8 = 64
    //   n[22] = (-0.705051005, -0.687775016,  0.172828004):
    //     d = 0.7071*(0.705051005+0.687775016) = 0.98487
    //     h = 0.5*(1.392826) + 0.172828*0.70711 = 0.81863; s = h/(3-2h) = 0.60070
    //     level = trunc((0.98487+0.60070)*16) = trunc(25.3691) = 25 -> gray 200
    //
    // Both are comfortably clear of an integer truncation boundary (8.42, 25.37).
    auto a = ra2engine::rasterize_model(single_voxel_model(1, 4), identity_hva(), 0.0, vpl, gray_pal());
    auto b = ra2engine::rasterize_model(single_voxel_model(1, 22), identity_hva(), 0.0, vpl, gray_pal());
    auto first_opaque_r = [](const ra2engine::VoxelSprite& s) -> int {
        for (size_t i = 3; i < s.rgba.size(); i += 4) if (s.rgba[i] != 0) return s.rgba[i - 3];
        return -1;
    };
    const int ra = first_opaque_r(a), rb = first_opaque_r(b);
    REQUIRE(ra >= 0);
    REQUIRE(rb >= 0);
    CHECK(ra != rb);  // different normals -> different lighting rows
    CHECK(ra == 64);  // level 8
    CHECK(rb == 200); // level 25
}

TEST_CASE("voxel_raster: facing quantization round-trips") {
    const int n = 32;  // [RE: voxel-render.md §2]
    CHECK(ra2engine::facing_to_index(0.0, n) == 0);
    CHECK(ra2engine::facing_to_index(65536.0 / n * 5, n) == 5);
    CHECK(ra2engine::facing_to_index(65536.0 - 1.0, n) == 0);  // wraps to nearest
    for (int i = 0; i < n; ++i)
        CHECK(ra2engine::facing_to_index(65536.0 * i / n, n) == i);
}

// ---------------------------------------------------------------------------
// FNV-1a 32-bit over the sprite bytes (regression fingerprint).
// ---------------------------------------------------------------------------
static uint32_t fnv1a(const std::vector<uint8_t>& v) {
    uint32_t h = 2166136261u;
    for (uint8_t b : v) { h ^= b; h *= 16777619u; }
    return h;
}

TEST_CASE("voxel_raster: real htnk hull renders deterministically") {
    // Guarded (same mix-chain/guard pattern as test_vxl.cpp / test_vpl.cpp).
    // Skips cleanly when the install is absent.
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    if (!fs::exists(install / "ra2.mix")) {
        MESSAGE("RA2 install absent; skipped");
        return;
    }
    // Borrow the shared, load-once ra2.mix (asset_fixture) instead of
    // re-slurping the ~268 MB archive; open_nested still exercises the real
    // path. REQUIRE_FALSE kept (now on the shared bytes) so the count is unchanged.
    REQUIRE_FALSE(ra2fixture::ra2_bytes().empty());
    const ra2assets::MixFile& outer = ra2fixture::ra2_mix();
    // htnk.vxl + htnk.hva + voxels.vpl live in ra2.mix -> local.mix.
    ra2assets::MixFile local = outer.open_nested("local.mix");
    const ra2assets::MixEntry* ve = local.find("htnk.vxl");
    const ra2assets::MixEntry* he = local.find("htnk.hva");
    const ra2assets::MixEntry* pe = local.find("voxels.vpl");
    REQUIRE(ve != nullptr);
    REQUIRE(he != nullptr);
    REQUIRE(pe != nullptr);
    auto vxl_raw = local.read(*ve);
    auto hva_raw = local.read(*he);
    auto vpl_raw = local.read(*pe);
    VxlModel m = ra2assets::load_vxl(vxl_raw.data(), vxl_raw.size());
    HvaModel hva = ra2assets::load_hva(hva_raw.data(), hva_raw.size());
    ra2assets::Vpl vpl = ra2assets::Vpl::load(vpl_raw.data(), vpl_raw.size());
    // unittem.pal comes from ra2.mix -> cache.mix.
    ra2assets::MixFile cache = outer.open_nested("cache.mix");
    const ra2assets::MixEntry* pale = cache.find(std::string("unittem.pal"));
    REQUIRE(pale != nullptr);
    auto pal_raw = cache.read(*pale);
    ra2assets::Palette pal = ra2assets::Palette::load(pal_raw.data(), pal_raw.size());

    auto s1 = ra2engine::rasterize_model(m, hva, 0.0, vpl, pal);
    auto s2 = ra2engine::rasterize_model(m, hva, 0.0, vpl, pal);
    CHECK(s1.rgba == s2.rgba);              // byte-deterministic
    CHECK(opaque_count(s1) > 200);          // a 46x34x12 tank is not a speck
    CHECK(s1.w < 200);
    CHECK(s1.h < 200);                      // and not exploded
    // FNV-1a fingerprint (regression lock) -- pinned after visual calibration
    // vs the original (Task 6, docs/re/voxel-render.md §1-4: the recovered
    // projection/lighting/palette produce a coherent Rhino hull, verified on
    // screen). Re-pin ONLY with a new visual check. Observed sprite: w=61 h=43
    // opaque=1038 anchor=(27,24).
    CHECK(fnv1a(s1.rgba) == 1209756769u);  // 0x481b6c61
}

TEST_CASE("voxel_raster: reordered named HVA sections preserve model and shadow") {
    auto model=single_voxel_model(100,0);
    model.sections[0].name="BODY";
    model.sections.push_back(model.sections[0]);
    model.sections[1].name="ENGINE";model.sections[1].voxels[0].color=160;
    auto h=identity_hva();h.section_names={"BODY","ENGINE"};
    auto moved=h.frames[0][0];moved.transform[0][3]=4;moved.transform[2][3]=2;
    h.frames[0].push_back(moved);
    auto reversed=h;
    std::swap(reversed.section_names[0],reversed.section_names[1]);
    std::swap(reversed.frames[0][0],reversed.frames[0][1]);
    const auto a=ra2engine::rasterize_model(model,h,0,identity_vpl(),gray_pal());
    const auto b=ra2engine::rasterize_model(model,reversed,0,identity_vpl(),gray_pal());
    CHECK(a.w==b.w);CHECK(a.h==b.h);CHECK(a.anchor_x==b.anchor_x);CHECK(a.anchor_y==b.anchor_y);CHECK(a.rgba==b.rgba);
    const auto sa=ra2engine::rasterize_shadow(model,h,0,128);
    const auto sb=ra2engine::rasterize_shadow(model,reversed,0,128);
    CHECK(sa.w==sb.w);CHECK(sa.h==sb.h);CHECK(sa.rgba==sb.rgba);
}

TEST_CASE("terrain tilt: movement blends remain rigid and retain endpoint orientation") {
 for(int a=0;a<=16;++a)for(int b=0;b<=16;++b) {
  CHECK(ra2engine::terrain_tilt_transition(a,b,0)==a);
  CHECK(ra2engine::terrain_tilt_transition(a,b,1)==b);
  for(int step=1;step<8;++step) {
   const auto t=ra2engine::terrain_tilt(ra2engine::terrain_tilt_transition(a,b,step/8.0));
   const auto p=t.apply(3,4,12),q=t.inverse(p[0],p[1],p[2]);
   CHECK(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]==doctest::Approx(169));
   CHECK(q[0]==doctest::Approx(3));CHECK(q[1]==doctest::Approx(4));CHECK(q[2]==doctest::Approx(12));
  }
 }
}

// W3-FIX-A: the shared per-theater MIX search list (src/asset_fs.cpp).
#include "doctest.h"
#include "ra2assets/asset_fs.hpp"
#include "asset_fixture.hpp"
#include "ra2assets/pal.hpp"

#include <cstdio>
#include <string>
#include <vector>

using ra2assets::AssetFS;
using ra2assets::AssetMixSpec;
using ra2assets::asset_search_order;
using ra2assets::art_base_names;
using ra2assets::theater_chain;
TEST_CASE("Yuri wall override: expansion Citadel replaces the pink classic French placeholder") {
    if(!ra2fixture::assets_available() || !ra2fixture::ra2_available())return;
    std::string base=ra2fixture::install_base()+"/";AssetFS fs(base,theater_chain("TEMPERATE"));
    const ra2assets::MixFile *classic=nullptr,*expansion=nullptr;std::string a,b;
    const auto* old=fs.find("ggfwll.shp",&classic,&a,true);
    const auto* correct=fs.find("ggfwll.shp",&expansion,&b,false,true);
    REQUIRE(old);REQUIRE(correct);CHECK(a=="ra2.mix/generic.mix");CHECK(b=="ra2md.mix/genermd.mix");
    CHECK(old->size!=correct->size);CHECK(classic->read(*old)!=expansion->read(*correct));
}

namespace {
int index_of(const std::vector<AssetMixSpec>& v, const std::string& label) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].label() == label) return static_cast<int>(i);
    return -1;
}
bool install_present() {
    const std::string p = ra2fixture::install_base() + "\\ra2.mix";
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}
std::string dir() { return ra2fixture::install_base() + "\\"; }
}  // namespace

TEST_CASE("asset_search_order: RA2 game.exe load order, YR tail last") {
    const auto o = asset_search_order(theater_chain("URBAN"), {"expand01.mix", "ecache01.mix"});
    // Bootstrap: EXPAND, RA2, CACHE, LOCAL; then ECACHE; then CONQUER.
    CHECK(index_of(o, "expand01.mix") == 0);
    CHECK(index_of(o, "ra2.mix") == 1);
    CHECK(index_of(o, "ra2.mix/cache.mix") < index_of(o, "ra2.mix/local.mix"));
    CHECK(index_of(o, "ra2.mix/local.mix") < index_of(o, "ecache01.mix"));
    CHECK(index_of(o, "ecache01.mix") < index_of(o, "ra2.mix/conquer.mix"));
    // Init_Theater: GENERIC, <THEATER>, <EXT>, ISO<..>, ISOGEN.
    const int g = index_of(o, "ra2.mix/generic.mix"), t = index_of(o, "ra2.mix/urban.mix"),
              e = index_of(o, "ra2.mix/urb.mix"), i = index_of(o, "ra2.mix/isourb.mix"),
              ig = index_of(o, "ra2.mix/isogen.mix");
    CHECK(index_of(o, "ra2.mix/conquer.mix") < g);
    CHECK(g < t);
    CHECK(t < e);
    CHECK(e < i);
    CHECK(i < ig);
    // Every RA2 archive precedes every YR one.
    const int md = index_of(o, "ra2md.mix");
    REQUIRE(md > 0);
    CHECK(ig < md);
    CHECK(index_of(o, "ra2md.mix/conqmd.mix") > md);
    CHECK(index_of(o, "ra2md.mix/isourbmd.mix") > md);
    CHECK(index_of(o, "ra2md.mix/isogenmd.mix") > md);
}

TEST_CASE("asset_search_order: SNOW opens no GENERIC before the theater archives") {
    const auto o = asset_search_order(theater_chain("SNOW"), {});
    const int g = index_of(o, "ra2.mix/generic.mix");
    CHECK(g > index_of(o, "ra2.mix/isogen.mix"));   // [RE-FALLBACK] tail copy only
    CHECK(index_of(o, "ra2.mix/snow.mix") < index_of(o, "ra2.mix/sno.mix"));
    CHECK(index_of(o, "ra2.mix/sno.mix") < index_of(o, "ra2.mix/isosnow.mix"));
    CHECK(index_of(o, "ra2md.mix/isosnomd.mix") > 0);
}

TEST_CASE("art_base_names: NewTheater munge matches theater_candidates") {
    auto t = art_base_names("CANEWY04", 'U');
    REQUIRE(t.size() == 2);
    CHECK(t[0] == "cunewy04");
    CHECK(t[1] == "cgnewy04");
    auto s = art_base_names("GACNST", 'A');
    REQUIRE(s.size() == 2);
    CHECK(s[0] == "gacnst");
    CHECK(s[1] == "ggcnst");
    auto l = art_base_names("AMMOCRAT", 'A');
    REQUIRE(l.size() == 1);
    CHECK(l[0] == "ammocrat");
}

TEST_CASE("AssetFS: real install -- landmarks, snow art, patch art, YR civilians") {
    if (!install_present()) { MESSAGE("install absent; skipping"); return; }
    AssetFS urb(dir(), theater_chain("URBAN"));
    // Statue of Liberty / White House: cg* names live only in isogen.mix.
    CHECK(urb.where("cgnewy04.shp") == "ra2.mix/isogen.mix");
    CHECK(urb.where("cgwash01.shp") == "ra2.mix/isogen.mix");
    // Palette=lib -> lib<ext>.pal in cache.mix.
    CHECK(urb.where("liburb.pal") == "ra2.mix/cache.mix");
    // hillbtwn civilian art: YR-only, reached through the tail.
    CHECK(urb.where("cula07.shp").rfind("ra2md.mix", 0) == 0);
    CHECK(urb.find("cula07.shp", nullptr, nullptr, /*ra2_only=*/true) == nullptr);
    AssetFS sno(dir(), theater_chain("SNOW"));
    // Ammo crates (all10s): only in <ext>.mix.
    CHECK(sno.where("ammo01.shp") == "ra2.mix/sno.mix");
    // Snow cottage art (not the temperate twin).
    CHECK(sno.where("caeur1.shp") != "");
    // The 1.006 patch caches are on the list, ahead of conquer.mix.
    bool has_ecache = false;
    for (const auto& m : sno.mixes()) has_ecache |= m.first == "ecache01.mix";
    CHECK(has_ecache);
}

TEST_CASE("cameo archives are searchable without losing RA2-before-Yuri ordering") {
    const auto o = asset_search_order(theater_chain("TEMPERATE"), {"expand01.mix"});
    CHECK(index_of(o,"language.mix/cameo.mix") >= 0);
    CHECK(index_of(o,"cameo.mix") >= 0);
    CHECK(index_of(o,"langmd.mix/cameomd.mix") >= 0);
    CHECK(index_of(o,"cameomd.mix") >= 0);
    CHECK(index_of(o,"expand01.mix") < index_of(o,"language.mix/cameo.mix"));
    CHECK(index_of(o,"language.mix/cameo.mix") < index_of(o,"ra2md.mix"));
}

TEST_CASE("AssetFS: unit and overlay palettes resolve for every RA2 and Yuri theater") {
    if (!ra2fixture::assets_available() || !ra2fixture::ra2_available()) {
        MESSAGE("Game archives unavailable"); return;
    }
    for (const auto* name : {"TEMPERATE","SNOW","URBAN","NEWURBAN","DESERT","LUNAR"}) {
        CAPTURE(name);
        const auto* chain=theater_chain(name); REQUIRE(chain);
        AssetFS assets(dir(),chain);
        for (const auto* palette : {chain->unit_pal,chain->theater_pal,"cameo.pal"}) {
            CAPTURE(palette);
            std::vector<std::uint8_t> raw; REQUIRE(assets.read(palette,raw));
            CHECK(raw.size()==768);
            CHECK_NOTHROW(ra2assets::Palette::load(raw.data(),raw.size()));
        }
    }
}

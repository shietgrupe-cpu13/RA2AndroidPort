#include "doctest.h"
#include "maplist.hpp"
#include "ra2assets/mix.hpp"
#include "ra2assets/fileio.hpp"
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <string>

using ra2engine::MapEntry;
using ra2engine::build_map_catalog;
using ra2engine::load_yr_map_catalog;
using ra2assets::MixFile;

namespace {
std::vector<uint8_t> slurp_ml(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}
std::string install_dir() {
    const char* d = std::getenv("RA2_INSTALL_DIR");
    std::string s = d ? d : "D:\\Red Alert 2";
    if (!s.empty() && s.back() != '\\' && s.back() != '/') s.push_back('\\');
    return s;
}
} // namespace

// --- REAL ASSET (guarded): the catalog end to end ------------------------
TEST_CASE("maplist: YR catalog = 91 standard maps, CSF-named [RE: maps.md §2/§9.6/§9.7]") {
    const std::string dir = install_dir();
    if (slurp_ml(dir + "multimd.mix").empty()) { MESSAGE("skipped: no multimd.mix"); return; }
    if (slurp_ml(dir + "langmd.mix").empty())  { MESSAGE("skipped: no langmd.mix");  return; }

    const ra2engine::MapCatalog cat = load_yr_map_catalog(dir, "standard");
    CHECK(cat.entries.size() == 91);              // [RE: maps.md §9.6]

    // Every listed map must EXIST in the archive -- that is the whole point of
    // the mandatory existence filter (maps.md §2).
    for (const MapEntry& m : cat.entries) {
        CHECK(cat.maps.find(m.file_name) != nullptr);
        CHECK_FALSE(m.display_name.empty());
    }

    // CSF display names really resolved: at least one entry must differ from
    // its raw base name, and the known §9.7 pin must be present.
    int csf_named = 0;
    const MapEntry* mayday = nullptr;
    for (const MapEntry& m : cat.entries) {
        if (m.display_name != m.base_name) ++csf_named;
        if (m.file_name == "xmp31s2.map") mayday = &m;
    }
    CHECK(csf_named > 50);                        // the vast majority are named
    REQUIRE(mayday != nullptr);
    CHECK(mayday->display_name == "May Day (2)"); // [RE: maps.md §9.7]

    // Sorted by display name, case-insensitively.
    for (size_t i = 1; i < cat.entries.size(); ++i) {
        std::string a = cat.entries[i - 1].display_name, b = cat.entries[i].display_name;
        for (char& c : a) c = (char)std::tolower((unsigned char)c);
        for (char& c : b) c = (char)std::tolower((unsigned char)c);
        CHECK(a <= b);
    }
}

TEST_CASE("maplist: no mode filter yields the full active list (161)") {
    const std::string dir = install_dir();
    if (slurp_ml(dir + "multimd.mix").empty()) { MESSAGE("skipped: no multimd.mix"); return; }
    if (slurp_ml(dir + "langmd.mix").empty())  { MESSAGE("skipped: no langmd.mix");  return; }
    const ra2engine::MapCatalog all = load_yr_map_catalog(dir, "");
    CHECK(all.entries.size() == 161);             // [RE: maps.md §9.6]
}

// --- SYNTHETIC: the filters, with a hand-built archive-less path ----------
TEST_CASE("maplist: existence filter drops maps absent from the archive") {
    // A real MixFile is needed for find(); reuse multimd.mix but feed a pkt
    // naming one present map and one invented one.
    const std::string dir = install_dir();
    auto bytes = slurp_ml(dir + "multimd.mix");
    if (bytes.empty()) { MESSAGE("skipped: no multimd.mix"); return; }
    MixFile maps = MixFile::open(std::move(bytes));

    const std::string pkt =
        "[MultiMaps]\n"
        "1=XMP31S2\n"          // present
        "2=NOSUCHMAP\n"        // absent -> MUST be dropped
        "\n"
        "[XMP31S2]\nDescription=DESC:MP31U2\nGameMode=standard\nMinPlayers=2\nMaxPlayers=2\n"
        "[NOSUCHMAP]\nDescription=DESC:NOPE\nGameMode=standard\nMinPlayers=2\nMaxPlayers=8\n";

    const std::vector<MapEntry> got = build_map_catalog(pkt, maps, nullptr, "standard");
    REQUIRE(got.size() == 1);
    CHECK(got[0].file_name == "xmp31s2.map");
    CHECK(got[0].display_name == "XMP31S2");   // no CSF -> base_name fallback
}

TEST_CASE("maplist: mode filter keeps only the requested tag") {
    const std::string dir = install_dir();
    auto bytes = slurp_ml(dir + "multimd.mix");
    if (bytes.empty()) { MESSAGE("skipped: no multimd.mix"); return; }
    MixFile maps = MixFile::open(std::move(bytes));

    const std::string pkt =
        "[MultiMaps]\n1=XMP31S2\n2=XMP01T4\n\n"
        "[XMP31S2]\nGameMode=standard\n"
        "[XMP01T4]\nGameMode=meatgrind\n";      // not standard -> dropped

    CHECK(build_map_catalog(pkt, maps, nullptr, "standard").size() == 1);
    CHECK(build_map_catalog(pkt, maps, nullptr, "meatgrind").size() == 1);
    CHECK(build_map_catalog(pkt, maps, nullptr, "").size() == 2);   // "" keeps all
}

// --- W5-PRES R-105: lobby theater filter ---------------------------------
TEST_CASE("maplist: map_theater_of reads [Map] Theater= only") {
    CHECK(ra2engine::map_theater_of("[Basic]\nTheater=SNOW\n[Map]\r\n Theater = newurban ; c\r\nSize=0,0\n") == "NEWURBAN");
    CHECK(ra2engine::map_theater_of("[Basic]\nName=x\n").empty());
    CHECK(ra2engine::lobby_theater_supported("TEMPERATE"));
    CHECK(ra2engine::lobby_theater_supported("SNOW"));
    CHECK(ra2engine::lobby_theater_supported("URBAN"));
    CHECK(ra2engine::lobby_theater_supported(""));          // unreadable -> kept
    CHECK_FALSE(ra2engine::lobby_theater_supported("DESERT"));
    CHECK_FALSE(ra2engine::lobby_theater_supported("NEWURBAN"));
    CHECK_FALSE(ra2engine::lobby_theater_supported("LUNAR"));
}

TEST_CASE("maplist: the lobby hides the YR-only DESERT/NEWURBAN maps (real asset)") {
    const std::string dir = install_dir();
    if (slurp_ml(dir + "multimd.mix").empty()) { MESSAGE("skipped: no multimd.mix"); return; }
    ra2engine::MapCatalog cat = load_yr_map_catalog(dir, "standard");
    const std::size_t before = cat.entries.size();
    const int hidden = ra2engine::filter_lobby_theaters(cat);
    MESSAGE("hidden=" << hidden << " kept=" << cat.entries.size() << " first="
                      << (cat.entries.empty() ? std::string("-") : cat.entries[0].file_name));
    CHECK(hidden > 0);
    CHECK(cat.entries.size() + static_cast<std::size_t>(hidden) == before);
    bool mayday = false;
    for (const MapEntry& m : cat.entries) {
        CHECK(ra2engine::lobby_theater_supported(m.theater));
        CHECK_FALSE(m.theater.empty());
        if (m.file_name == "xmp31s2.map") mayday = true;
    }
    CHECK(mayday);   // a TEMPERATE map the play scripts use stays listed
}

TEST_CASE("addon maps: tokens reject paths and validation goes beyond the extension") {
 using namespace ra2engine;
 CHECK(addon_map_token("maps/Example.YRM"));CHECK(addon_map_token("maps/test.mpr"));
 for(const auto* name:{"../test.map","maps/../test.map","maps/a/b.map","maps/C:test.map","maps/test.exe","maps/.map"})
  CHECK_FALSE(addon_map_token(name));
 CHECK_THROWS(inspect_addon_map("[Map]\nSize=0,0,20,20\nTheater=TEMPERATE\n","maps/test.yrm"));
 CHECK_THROWS(inspect_addon_map("[Map]\nSize=0,0,999999,20\n[IsoMapPack5]\n1=AAAA\n","maps/test.yrm"));
 CHECK_THROWS(inspect_addon_map("[Map]\nSize=0,0,20,20\n[Triggers]\nFoo=1\n[IsoMapPack5]\n1=AAAA\n","maps/test.yrm"));
}
TEST_CASE("addon maps: retail terrain copied as loose YRM is read, validated and catalogued independently") {
 using namespace ra2engine;
 const auto dir=install_dir();if(slurp_ml(dir+"multimd.mix").empty()){MESSAGE("skipped: real maps unavailable");return;}
 auto cat=load_yr_map_catalog(dir,"standard");std::vector<std::uint8_t> bytes;
 for(const auto& entry:cat.entries)try {
  auto raw=read_catalog_map(cat,entry.file_name);
  (void)inspect_addon_map(std::string(raw.begin(),raw.end()),"maps/test.yrm");bytes=std::move(raw);break;
 } catch(const std::exception&) {}
 REQUIRE_FALSE(bytes.empty());
 const auto temp=std::filesystem::temp_directory_path()/
   ("ra2-addon-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directories(temp/"maps");
 struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code ec;
  std::filesystem::remove(p/"maps/test.yrm",ec);std::filesystem::remove(p/"maps/broken.yrm",ec);
  std::filesystem::remove(p/"maps",ec);std::filesystem::remove(p,ec);}} cleanup{temp};
 {std::ofstream out(temp/"maps/test.yrm",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
 {std::ofstream out(temp/"maps/broken.yrm");out<<"Not a game map";}
 CHECK(read_addon_map(temp.string(),"maps/test.yrm")==bytes);
 CHECK_THROWS(read_addon_map(temp.string(),"maps/../test.yrm"));
 const auto count=cat.entries.size();append_addon_maps(cat,temp.string(),"");
 REQUIRE(cat.entries.size()==count+1);REQUIRE(cat.rejected_addons.size()==1);
 const auto it=std::find_if(cat.entries.begin(),cat.entries.end(),[](const MapEntry& e){return e.file_name=="maps/test.yrm";});
 REQUIRE(it!=cat.entries.end());CHECK(it->max_players>=2);CHECK(it->max_players<=8);
 CHECK(read_catalog_map(cat,it->file_name)==bytes);
}

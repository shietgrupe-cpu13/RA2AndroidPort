#include "doctest.h"
#include "ra2assets/mod_profile.hpp"
#include "ra2assets/asset_fs.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"
#include "maplist.hpp"
#include "asset_fixture.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
namespace {
namespace fs=std::filesystem;
using namespace ra2assets;
struct ModFixture {
    fs::path dir=fs::temp_directory_path()/("ra2-mod-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ModFixture(){fs::create_directories(dir);}
    ~ModFixture(){activate_mod(nullptr);std::error_code ec;fs::remove_all(dir,ec);}
    void put(const std::string& name,const std::string& text){fs::create_directories((dir/name).parent_path());std::ofstream f(dir/name,std::ios::binary);f<<text;REQUIRE(bool(f));}
    void manifest(){put("mod.ini","[Mod]\nId=test-mod\nName=Test mod\nRequires=base\n");}
    std::shared_ptr<ModProfile> inspect(){return ModProfile::inspect(dir.u8string());}
};
std::string bytes(const std::vector<uint8_t>& b){return {b.begin(),b.end()};}
std::vector<uint8_t> bytes(const std::string& s){return {s.begin(),s.end()};}
std::string mix(const std::string& name,const std::string& content){
    std::string b;auto put=[&](uint32_t v,int n){for(int i=0;i<n;i++)b.push_back(static_cast<char>(v>>(i*8)));};
    put(1,2);put(static_cast<uint32_t>(content.size()),4);put(mix_id(name),4);put(0,4);put(static_cast<uint32_t>(content.size()),4);return b+content;
}
}
TEST_CASE("mod profiles: loose overrides reach Rules and art lookup; disable restores base") {
    ModFixture f;f.manifest();f.put("rulesmd.ini","[E1]\nCost=75\nStrength=150\n");f.put("assets/gi.shp","mod-art");
    auto p=f.inspect();REQUIRE(p->can_activate());CHECK(p->status()=="SUPPORTED");CHECK(p->fingerprint.size()==16);
    std::vector<uint8_t> data;REQUIRE(p->read("RULESMD.INI",data));
    const auto ini=Ini::parse(bytes(merge_mod_ini(bytes(std::string("[E1]\nCost=100\nStrength=100\nSpeed=4\n")),data)));
    const auto rules=Rules::load(ini);CHECK(rules.cost("E1")==75);CHECK(rules.strength("E1")==150);CHECK(rules.speed("E1")==4);
    f.put("base/ra2.mix",mix("gi.shp","base-art"));
    AssetFS base((f.dir/"base").u8string()+"/",nullptr),modded((f.dir/"base").u8string()+"/",nullptr,p);
    REQUIRE(base.read("gi.shp",data));CHECK(bytes(data)=="base-art");
    const MixFile* owner=nullptr;REQUIRE(modded.find("GI.SHP",&owner));CHECK(bytes(owner->read(*modded.find("GI.SHP")))=="mod-art");
    REQUIRE(modded.read("gi.shp",data));CHECK(bytes(data)=="mod-art");
    CHECK(modded.where("gi.shp").find("mod:test-mod/")==0);
    CHECK_FALSE(p->read("../rulesmd.ini",data));CHECK_FALSE(p->read("C:\\rulesmd.ini",data));
    activate_mod(p);CHECK(active_mod()->id=="test-mod");activate_mod(nullptr);CHECK_FALSE(active_mod());
    REQUIRE(base.read("gi.shp",data));CHECK(bytes(data)=="base-art");
}
TEST_CASE("mod profiles: archive priority and loose priority are explicit") {
    ModFixture f;f.manifest();f.put("expand99.mix",mix("unit.shp","new"));f.put("expand01.mix",mix("unit.shp","old"));
    auto p=f.inspect();REQUIRE(p->can_activate());std::vector<uint8_t> data;REQUIRE(p->read("unit.shp",data));CHECK(bytes(data)=="new");
    f.put("unit.shp","loose");auto q=f.inspect();REQUIRE(q->read("unit.shp",data));CHECK(bytes(data)=="loose");
}
TEST_CASE("mod profiles: content identity is path-independent and changes with bytes") {
    ModFixture a,b;a.manifest();b.manifest();a.put("rulesmd.ini","[E1]\nCost=75\n");b.put("rulesmd.ini","[E1]\nCost=75\n");
    const auto first=a.inspect();CHECK(first->fingerprint==b.inspect()->fingerprint);
    b.put("rulesmd.ini","[E1]\nCost=76\n");CHECK(first->fingerprint!=b.inspect()->fingerprint);
}
TEST_CASE("mod profiles: missing manifest, extensions and malformed archives block activation") {
    ModFixture f;CHECK_FALSE(f.inspect()->can_activate());f.manifest();f.put("Ares.dll","not executable");
    auto p=f.inspect();CHECK_FALSE(p->can_activate());CHECK(p->report().find("windows-extension")!=std::string::npos);CHECK_THROWS(activate_mod(p));
    fs::remove(f.dir/"Ares.dll");f.put("rulesmd.ini","[E1]\nAttachEffect.Duration=20\n");CHECK_FALSE(f.inspect()->can_activate());
    fs::remove(f.dir/"rulesmd.ini");f.put("bad.mix","malformed");CHECK_FALSE(f.inspect()->can_activate());
}
TEST_CASE("mod profiles: unverified tags are reported instead of certified") {
    ModFixture f;f.manifest();f.put("rulesmd.ini","[E1]\nNeverImplementedTag=yes\n");auto p=f.inspect();
    REQUIRE(p->can_activate());CHECK(p->status()=="PARTIAL");CHECK(p->report().find("NEVERIMPLEMENTEDTAG")!=std::string::npos);
    CHECK_FALSE(safe_mod_id("../base"));CHECK_FALSE(safe_mod_id("Bad-ID"));CHECK(safe_mod_id("small-mod_1"));
}
TEST_CASE("mod profiles: merged registries retain base ordering and replace existing keys") {
    auto merged=merge_mod_ini(bytes(std::string("[InfantryTypes]\n1=E1\n2=E2\n[E1]\nCost=100\nCost=110\n")),
                              bytes(std::string("[InfantryTypes]\n2=INIT\n3=BRUTE\n[E1]\nCost=75\n")));
    auto ini=Ini::parse(bytes(merged));REQUIRE(ini.section("InfantryTypes").size()==3);
    CHECK(ini.section("InfantryTypes")[0].second=="E1");CHECK(ini.section("InfantryTypes")[1].second=="INIT");
    CHECK(ini.section("InfantryTypes")[2].second=="BRUTE");CHECK(ini.section("E1").size()==1);CHECK(ini.get_int("E1","Cost")==75);
    const auto preserved=Ini::parse(bytes(merge_mod_ini(bytes(std::string("[Registry]\n1=A\n1=B\n")),bytes(std::string("[E1]\nCost=75\n")))));
    CHECK(preserved.section("Registry").size()==2); // untouched base registries retain duplicates
}
TEST_CASE("mod profiles: mod map tokens are distinct and fail closed when disabled") {
    ModFixture f;f.manifest();f.put("maps/demo.map","not a valid map");auto p=f.inspect();REQUIRE(p->can_activate());
    CHECK(p->maps()==std::vector<std::string>{"demo.map"});CHECK(p->map_path("../demo.map").empty());
    CHECK(ra2engine::addon_map_token("modmaps/demo.map"));CHECK_FALSE(ra2engine::addon_map_token("modmaps/../demo.map"));
    CHECK_THROWS(ra2engine::read_addon_map(f.dir.u8string(),"modmaps/demo.map"));
    activate_mod(p);ra2engine::MapCatalog catalog;ra2engine::append_addon_maps(catalog,f.dir.u8string(),"");
    CHECK(catalog.entries.empty());CHECK(catalog.rejected_addons.size()==2); // same invalid map in base and mod namespaces
}
TEST_CASE("mod profiles: accepted mod map loads through the catalog and disappears on disable") {
    if(!ra2fixture::assets_available()){MESSAGE("skipped: retail map fixture unavailable");return;}
    const auto base=ra2fixture::install_base()+"/";
    auto stock=ra2engine::load_yr_map_catalog(base,"standard");std::vector<uint8_t> data;
    for(const auto& entry:stock.entries)try{
        auto b=ra2engine::read_catalog_map(stock,entry.file_name);
        ra2engine::inspect_addon_map(bytes(b),"modmaps/demo.yrm");data=std::move(b);break;
    }catch(const std::exception&){}
    REQUIRE_FALSE(data.empty());ModFixture f;f.manifest();f.put("maps/demo.yrm",bytes(data));auto p=f.inspect();REQUIRE(p->can_activate());
    activate_mod(p);ra2engine::MapCatalog catalog;
    ra2engine::append_addon_maps(catalog,(f.dir/"no-base-maps").u8string(),"");
    REQUIRE(catalog.entries.size()==1);CHECK(catalog.entries[0].file_name=="modmaps/demo.yrm");
    CHECK(ra2engine::read_catalog_map(catalog,catalog.entries[0].file_name)==data);
    activate_mod(nullptr);CHECK_THROWS(ra2engine::read_catalog_map(catalog,"modmaps/demo.yrm"));
}

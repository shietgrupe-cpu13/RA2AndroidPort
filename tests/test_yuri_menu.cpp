#include "doctest.h"
#include "faction_support.hpp"
#include "menu_readability.hpp"
#include "ra2assets/rules.hpp"
#include "skirmish_layout.hpp"
#include "faction_art.hpp"
#include "asset_fixture.hpp"
#include "ra2assets/pcx.hpp"
#include "ra2assets/shp.hpp"
#include "sidebar_assets.hpp"
TEST_CASE("Yuri sidebar: stock expansion overrides inherit complete Soviet chrome") {
 if(!ra2fixture::assets_available() || !ra2fixture::ra2_available())return;
 const auto base=ra2fixture::mix_by_name("ra2.mix").open_nested("sidec02.mix");
 const auto overlay=ra2engine::yuri_sidebar_overrides(ra2fixture::mix_by_name("ra2md.mix"));
 // The original resolver incorrectly required a complete sidec03 archive.
 CHECK(overlay.find(std::string("top.shp"))==nullptr);
 for(const auto* name:{"top.shp","side1.shp","side2.shp","tab00.shp","tab01.shp","tab02.shp","tab03.shp","powerp.shp","credits.shp","gclock2.shp"}) {
  const auto* e=base.find(std::string(name));REQUIRE(e);
  CHECK(ra2assets::ShpFile::load(base.read(*e)).count()>0);
 }
 const auto* radar=overlay.find(std::string("radary.shp"));REQUIRE(radar);
 CHECK(ra2assets::ShpFile::load(overlay.read(*radar)).count()>0);
 const auto* palette=base.find(std::string("sidebar.pal"));REQUIRE(palette);CHECK(palette->size==768);
}
TEST_CASE("Yuri presentation: retail flag loading image and palette decode from verified archives") {
 if(!ra2fixture::assets_available() || !ra2fixture::ra2_available())return;
 const auto local=ra2fixture::mix_by_name("ra2md.mix").open_nested("localmd.mix");
 const auto* flag=local.find(std::string("yrii.pcx"));REQUIRE(flag);
 const auto pcx=ra2assets::decode_pcx(local.read(*flag));CHECK(pcx.w>0);CHECK(pcx.h>0);
 const auto load=ra2fixture::mix_by_name("ra2md.mix").open_nested("loadmd.mix");
 const auto* image=load.find(std::string("ls800yuri.shp"));REQUIRE(image);
 const auto shp=ra2assets::ShpFile::load(load.read(*image));CHECK(shp.count()>0);CHECK(shp.width()==800);
 const auto* palette=load.find(ra2engine::loading_country_palette("YuriCountry",false));REQUIRE(palette);
 const auto bytes=load.read(*palette);CHECK(bytes.size()==768);
 const auto pal=ra2assets::Palette::load(bytes.data(),bytes.size());
 const auto pixels=ra2assets::ShpFile::palettize(shp.decode_indexed(0),pal);
 REQUIRE(pixels.size()==800*600);
 // Authentic loading panel interior is black; the old MPLS mapping made it white.
 const auto panel=pixels[480*800+600];CHECK(panel.r==0);CHECK(panel.g==0);CHECK(panel.b==0);
}
TEST_CASE("loading presentation: every playable country has a paired retail loading palette") {
 using namespace ra2engine;
 CHECK(loading_country_palette("YuriCountry",false)=="mpyls.pal");
 if(!ra2fixture::assets_available())return;
 const auto load=ra2fixture::mix_by_name("ra2md.mix").open_nested("loadmd.mix");
 for(const auto* country:{"Americans","British","French","Germans","Alliance","Koreans","Russians","Confederation","Africans","Arabs","YuriCountry"}) {
  const auto* image=load.find("ls800"+loading_country_art(country,false)+".shp");
  const auto* palette=load.find(loading_country_palette(country,false));
  REQUIRE(image);REQUIRE(palette);CHECK(palette->size==768);
 }
}
TEST_CASE("Yuri presentation: expansion loading image and harvesting-role warning are distinct") {
 using namespace ra2engine;
 CHECK(loading_country_art("YuriCountry",false)=="yuri");
 CHECK(loading_country_art("YuriCountry",true)=="yuri");
 CHECK(loading_country_art("Americans",false)=="ustates");
 CHECK(loading_country_art("Russians",true)=="russia");
 CHECK(std::string(attacked_building_eva(true))=="EVA_OreMinerUnderAttack");
 CHECK(std::string(attacked_building_eva(false))=="EVA_OurBaseIsUnderAttack");
}
TEST_CASE("Yuri country remains a distinct side with its own default") {
 using namespace ra2engine;
 CHECK(faction_side("YuriCountry","ThirdSide")==World::Side::Yuri);
 CHECK(faction_side("YuriCountry","")==World::Side::Yuri);
 CHECK(faction_side("Russians","Nod")==World::Side::Soviet);
 CHECK(std::string(faction_country(World::Side::Yuri))=="YuriCountry");
 CHECK(std::string(faction_rules_side(World::Side::Yuri))=="THIRDSIDE");
 auto ini=ra2assets::Ini::parse("[BuildingTypes]\n0=GACNST\n7=YACNST\n12=YAPOWR\n");
 const auto rules=ra2assets::Rules::load(ini);const auto types=rules.listed_types("BuildingTypes");
 CHECK(types.size()==3);
 CHECK(std::find(types.begin(),types.end(),"YACNST")!=types.end());
 CHECK(rules.listed_types("missing").empty());
}
TEST_CASE("touch menu bitmap text uses whole pixels instead of uneven fractional strokes") {
 using namespace ra2engine;
 const double touch=menu_font_scale(1.5,12,true);
 CHECK(touch==1);
 CHECK(menu_font_scale(2.5,12,true)==2);
 CHECK(menu_font_scale(0.75,12,true)==1);
 CHECK(touch*16<=1.5*19); // Fits the 19-logical-pixel country combobox.
 CHECK(menu_font_scale(1.5,14,true)==touch); // fit() and drawing agree.
}
TEST_CASE("Skirmish layout: flags and spawn badges stay inside their image bounds") {
 using namespace ra2engine;
 const auto flag=fit_skirmish_image(64,48,{277,30,32,19});
 CHECK(flag.w<=32);CHECK(flag.h<=19);CHECK(flag.x>=277);CHECK(flag.y>=30);
 for(const auto& box:{SbRect{200,240,328,180},SbRect{0,0,1,1},SbRect{4,7,15,17}}) {
  for(int x:{box.x-100,box.x,box.x+box.w+100})for(int y:{box.y-100,box.y,box.y+box.h+100}) {
   const auto mark=skirmish_marker(x,y,box);
   CHECK(mark.x>=box.x);CHECK(mark.y>=box.y);
   CHECK(mark.x+mark.w<=box.x+box.w);CHECK(mark.y+mark.h<=box.y+box.h);
  }
 }
 CHECK(fit_skirmish_image(0,0,{0,0,32,19}).w==0);
}

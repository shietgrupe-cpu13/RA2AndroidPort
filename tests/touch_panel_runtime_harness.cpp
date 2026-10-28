// Standalone integration harness; includes the production touch_bar.inc.
// Build with engine/include and SDL2 include paths plus touch_gestures.cpp.
// SDL drawing is stubbed; this checks event routing, not rendered appearance.
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <SDL.h>
#undef main
#include "input.hpp"
#include "touch_gestures.hpp"
#include "touch_bar_layout.hpp"
#include "texture_cache.hpp"
#include "building.hpp"
#include "touch_theme.hpp"
#include "ra2assets/shp.hpp"
using ra2engine::InputEvent;
extern "C" {
int SDL_SetRenderDrawBlendMode(SDL_Renderer*,SDL_BlendMode){return 0;}
int SDL_SetRenderDrawColor(SDL_Renderer*,Uint8,Uint8,Uint8,Uint8){return 0;}
int SDL_RenderFillRect(SDL_Renderer*,const SDL_Rect*){return 0;}
int SDL_RenderDrawRect(SDL_Renderer*,const SDL_Rect*){return 0;}
int SDL_RenderDrawLine(SDL_Renderer*,int,int,int,int){return 0;}
}
void draw_text(SDL_Renderer*,const std::string&,int,int,int,int,int,int){}
void blit_shp(SDL_Renderer*,ra2engine::TextureCache&,const std::string&,const ra2assets::ShpFile&,int,const ra2assets::Palette&,int,int){}
struct FakeWorld {
 std::vector<ra2engine::Unit> us;
 std::vector<ra2engine::Building> bs;
 const auto& units()const{return us;} const auto& buildings()const{return bs;}
 bool controllable(int h)const{return h==0;}
 bool power_ready=false;
 bool sw_ready(int,ra2engine::SuperweaponType)const{return power_ready;}
 bool packable=false;
 bool can_pack_slave_miner(int)const{return packable;}
 bool can_pack_conyard(int)const{return false;}
 bool building_can_attack(int)const{return false;}
 ra2engine::World::Side house_side(int)const{return ra2engine::World::Side::Yuri;}
};
struct FakeInput {
 ra2engine::Selection sel; int building=-1,stops=0,group=0,created=0;
 auto& selection(){return sel;} const auto& selection()const{return sel;}
 int selected_building()const{return building;}
 bool planning=false;
 bool planning_mode()const{return planning;}
 std::size_t planned_moves()const{return 0;}
 void toggle_planning(){planning=!planning;}
 void type_select(){} void stop_selected(){++stops;} void guard_selected(){} void scatter_selected(){}
 void group_create(int n){created=n;} void group_select(int n,uint32_t){group=n;}
 bool placing=false;
 bool placement_active()const{return placing;} void placement_cell(int& x,int& y){x=5;y=5;} void handle(const InputEvent&){}
};
struct Scene {
 FakeWorld world_; FakeInput input_; int local_house_=0;
 struct Menu{bool open_=false;} menu_;
 struct Dialog{bool is_open()const{return false;}}sld_;
 struct Abort{bool open()const{return false;}}gf_abort_;
 bool input_locked_=false,campaign_=false,show_briefing_=false,sidebar_up_=false,net_chat_open_=false;
 struct Layout{bool contains(int,int)const{return false;}}lay_;
 struct Cam{double x=0,y=0;}cam_;
 double view_cam_[3][2]{}; bool view_set_[3]{};
 int kWinW=1280,kWinH=548,playfield_w_=800,deploys=0,armed=0,yuri_deploys=0;
 bool touch_yuri_deploy_available(bool)const{return true;}
 void touch_yuri_deploy(bool){++yuri_deploys;}
 void w2_arm_sw(ra2engine::SuperweaponType t){armed=int(t);}
 struct Def{std::string id;ra2engine::UnitCombat combat;};
 struct Roster{std::vector<Def> defs;std::vector<char>has_cameo;std::vector<ra2assets::ShpFile>cameos;}ur_;
 struct Assets{ra2assets::Palette cameo_pal;}ba_;
 void center_base(){}void hotkey_radar_event(uint32_t){}void cycle_object(int){}void hotkey_follow(){}void deploy_selected(){++deploys;}
 #include "touch_bar.inc"
};
InputEvent event(InputEvent::Kind kind,int x,int y){InputEvent e;e.kind=kind;e.touch=true;e.x=x;e.y=y;return e;}
int main(int argc,char** argv){
 if(argc>1 && std::string(argv[1])=="--free") {
  Scene s;s.touch_ui_=true; // Old folded preference must not hide free buttons.
  assert(s.touch_free_layout());assert(s.touch_panel_collapsed_);assert(s.touch_btn_count()==3);
  assert(s.touch_panel_rect().w==0 && s.touch_panel_rect().h==0);
  auto r=s.touch_btn_rect(0);assert(r.x+r.w==s.kWinW && r.y==0);assert(r.h==22);
  assert(!s.touch_panel_toggle_at(10,s.kWinH-10));
  assert(!s.touch_panel_contains(10,s.kWinH-10)); // Old panel space belongs to the map.
  assert(s.touch_panel_contains(r.x+2,r.y+2)); // Beyond the old playfield/dock.
  ra2engine::Unit u;u.house=0;u.alive=true;s.world_.us.push_back(u);s.input_.sel.set(0);
  auto down=event(InputEvent::PointerDown,r.x+2,r.y+2),up=down;up.kind=InputEvent::PointerUp;
  assert(s.touch_bar_handle(down));assert(s.touch_bar_handle(up));assert(s.input_.stops==1);
  auto deploy=s.touch_btn_rect(2);assert(deploy.y+deploy.h==s.kWinH);
  s.kWinW=1920;s.kWinH=1080;auto corner=s.touch_btn_rect(0);assert(corner.x+corner.w==1920);
  std::cout<<"Free HUD: no dock, compact per-button sizing, screen corners and real command routing passed\n";return 0;
 }
 if(argc>1) {
  Scene s;s.touch_ui_=true;s.touch_panel_collapsed_=false;
  if(std::string(argv[1])=="--empty") {assert(s.touch_buttons().empty());assert(s.touch_btn_count()==0);return 0;}
  assert(s.touch_buttons().size()==5);
  auto r=s.touch_btn_rect(0);assert(r.x>500);assert(s.touch_panel_contains(r.x+2,r.y+2));
  auto pan=event(InputEvent::Pan,20,20);pan.x0=r.x+2;pan.y0=r.y+2;assert(s.touch_bar_handle(pan));
  assert(s.touch_action_id("GROUP0")==s.TB_GROUP1+9);
  assert(!s.touch_button_enabled(s.TB_DOMINATOR));s.world_.power_ready=true;
  assert(s.touch_button_enabled(s.TB_DOMINATOR));s.touch_command(s.TB_DOMINATOR,false,0);
  assert(s.armed==int(ra2engine::SuperweaponType::PsychicDominator));
  s.touch_command(s.TB_PSI,false,0);s.touch_command(s.TB_MINER,false,0);assert(s.yuri_deploys==2);
  std::cout<<"Floating capture, schema 2, all-hidden layout and Yuri shortcuts passed\n";return 0;
 }

 Scene s;s.touch_ui_=true;
 assert(s.touch_panel_collapsed_);s.touch_panel_collapsed_=false;
 assert(s.touch_bar_visible());s.net_chat_open_=true;assert(!s.touch_bar_visible());
 s.net_chat_open_=false;assert(s.touch_bar_visible());
 s.touch_command(s.TB_MOVE,false,0);
 for(auto kind:{InputEvent::PointerDown,InputEvent::PointerMove,InputEvent::PointerUp}) {
  auto e=event(kind,500,200);assert(!s.touch_bar_handle(e));
  assert(e.ctrl && e.shift && !e.alt);
 }
 assert(!s.touch_move_);
 s.touch_command(s.TB_ROUTE,false,0);assert(s.input_.planning_mode());
 s.touch_command(s.TB_ROUTE,false,0);assert(!s.input_.planning_mode());
 s.input_.placing=true;
 auto preview=event(InputEvent::PointerDown,500,200);assert(s.touch_bar_handle(preview));
 auto cancel=event(InputEvent::PointerUp,500,200);cancel.button=InputEvent::Right;
 assert(!s.touch_bar_handle(cancel));assert(!s.touch_place_swallow_up_);
 s.input_.placing=false;
 s.touch_add_=true;s.touch_box_=true;s.touch_command(s.TB_FIRE,false,0);
 assert(s.touch_fire_);assert(!s.touch_add_);assert(!s.touch_box_);
 auto fireDown=event(InputEvent::PointerDown,500,200);
 assert(!s.touch_bar_handle(fireDown));assert(fireDown.ctrl);
 auto fireMove=event(InputEvent::PointerMove,500,200);
 assert(!s.touch_bar_handle(fireMove));assert(fireMove.ctrl);
 auto fireUp=event(InputEvent::PointerUp,500,200);
 assert(!s.touch_bar_handle(fireUp));assert(fireUp.ctrl);assert(!s.touch_fire_);

 assert(!s.touch_button_enabled(s.TB_DEPLOY));
 ra2engine::Unit gi;gi.house=0;gi.alive=true;gi.deploy_fire=true;s.world_.us.push_back(gi);s.input_.sel.set(0);
 assert(s.touch_button_enabled(s.TB_DEPLOY));assert(std::string(s.touch_deploy_label())=="DEPLOY");
 s.world_.us[0].deployed=true;assert(std::string(s.touch_deploy_label())=="UNDEPLOY");
 s.world_.us[0].deploy_fire=false;s.world_.us[0].passengers_max=5;assert(!s.touch_button_enabled(s.TB_DEPLOY));
 s.world_.us[0].cargo.push_back(1);assert(std::string(s.touch_deploy_label())=="UNLOAD");assert(s.touch_button_enabled(s.TB_DEPLOY));
 s.world_.us[0].house=1;assert(!s.touch_button_enabled(s.TB_DEPLOY));assert(!s.touch_button_enabled(s.TB_STOP));
 s.world_.us[0].house=0;s.world_.us[0].die_time=0;assert(!s.touch_button_enabled(s.TB_DEPLOY));s.world_.us[0].die_time=-1;
 auto r=s.touch_btn_rect(0);auto d=event(InputEvent::PointerDown,r.x+4,r.y+4);auto u=d;u.kind=InputEvent::PointerUp;
 assert(s.touch_bar_handle(d));assert(s.touch_bar_handle(u));assert(s.input_.stops==1);
 // A disabled command consumes its tap, issuing nothing.
 s.input_.sel.clear();assert(s.touch_bar_handle(d));assert(s.touch_bar_handle(u));assert(s.input_.stops==1);
 s.world_.bs.emplace_back();s.world_.bs[0].house=0;s.input_.building=0;s.world_.packable=true;
 assert(s.touch_button_enabled(s.TB_DEPLOY));assert(std::string(s.touch_deploy_label())=="UNDEPLOY");
 s.world_.bs[0].house=1;assert(!s.touch_button_enabled(s.TB_DEPLOY));
 s.world_.bs.clear();s.input_.building=-1;s.world_.packable=false;
 // Slide-off must consume release and not fire.
 s.input_.sel.set(0);assert(s.touch_bar_handle(d));u.x=500;assert(s.touch_bar_handle(u));assert(s.input_.stops==1);
 // Map-origin selection release over the panel must reach the map input.
 s.touch_down_btn_=-1;s.touch_panel_capture_=false;u.x=r.x+4;assert(!s.touch_bar_handle(u));
 // The header and gaps consume complete taps; drags don't scroll terrain.
 auto p=s.touch_panel_rect();d=event(InputEvent::PointerDown,p.x+3,p.y+3);assert(s.touch_bar_handle(d));
 auto mv=d;mv.kind=InputEvent::PointerMove;mv.x=600;assert(s.touch_bar_handle(mv));
 u=mv;u.kind=InputEvent::PointerUp;assert(s.touch_bar_handle(u));assert(!s.touch_panel_capture_);
 auto pan=event(InputEvent::Pan,600,250);pan.x0=p.x+3;pan.y0=p.y+3;assert(s.touch_bar_handle(pan));
 // Group holds assign instead of invoking any deploy command.
 r=s.touch_btn_rect(8);d=event(InputEvent::PointerDown,r.x+3,r.y+3);u=d;u.kind=InputEvent::PointerUp;u.button=InputEvent::Right;
 assert(s.touch_bar_handle(d));assert(s.touch_bar_handle(u));assert(s.input_.created==1);assert(s.deploys==0);
 // Arrow toggles once per complete tap; old panel space becomes battlefield.
 auto a=s.touch_panel_toggle_rect();d=event(InputEvent::PointerDown,a.x+5,a.y+5);u=d;u.kind=InputEvent::PointerUp;
 assert(s.touch_bar_handle(d));assert(!s.touch_panel_collapsed_);assert(s.touch_bar_handle(u));
 assert(s.touch_panel_collapsed_);assert(s.touch_btn_count()==0);assert(s.touch_btn_at(r.x,r.y)==-1);
 assert(!s.touch_panel_contains(p.x+5,p.y+5));
 a=s.touch_panel_toggle_rect();d=event(InputEvent::PointerDown,a.x+5,a.y+5);u=d;u.kind=InputEvent::PointerUp;
 assert(s.touch_bar_handle(d));assert(s.touch_bar_handle(u));assert(!s.touch_panel_collapsed_);assert(s.touch_btn_count()==13);
 // Releasing outside the arrow does not hide the panel.
 a=s.touch_panel_toggle_rect();d=event(InputEvent::PointerDown,a.x+5,a.y+5);u=d;u.kind=InputEvent::PointerUp;u.x=600;
 assert(s.touch_bar_handle(d));assert(s.touch_bar_handle(u));assert(!s.touch_panel_collapsed_);
 s.menu_.open_=true;assert(s.touch_btn_at(r.x,r.y)==-1);
 std::cout<<"Panel integration checks passed: capabilities, labels, disabled actions, capture, slide-off, map release, group hold and menu suppression.\n";
}

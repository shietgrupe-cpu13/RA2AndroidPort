// Bottom-left unit command panel. Commands use existing RA2 order handlers;
// group taps select, group holds assign, battlefield double-tap selects type.
// Icons are original SDL primitives and never depend on retail icon sheets.
struct TouchBtn { const char* label; int id; };
static bool touch_free_layout() {
    const char* mode=std::getenv("RA2_TOUCH_FREE_LAYOUT");
    return mode && std::string(mode)=="1";
}
ra2engine::TouchTheme touch_skin() const {
    return ra2engine::touch_theme(static_cast<int>(world_.house_side(local_house_)));
}
void touch_colour(SDL_Renderer* ren, ra2assets::Rgba c) const {
    SDL_SetRenderDrawColor(ren,c.r,c.g,c.b,c.a);
}
void touch_metal(SDL_Renderer* ren,const SDL_Rect& r,ra2assets::Rgba c) const {
    static const float opacity_mult = [] {
        if (const char* v = std::getenv("RA2_TOUCH_OPACITY")) {
            int op = std::atoi(v);
            if (op >= 20 && op <= 100) return op / 100.0f;
        }
        return 1.0f;
    }();
    if (opacity_mult < 0.99f) {
        c.a = static_cast<uint8_t>(std::clamp(static_cast<int>(c.a * opacity_mult), 20, 255));
    }
    touch_colour(ren,c);SDL_RenderFillRect(ren,&r);
    SDL_SetRenderDrawColor(ren,std::min(255,c.r+28),std::min(255,c.g+28),std::min(255,c.b+28),c.a);
    SDL_RenderDrawLine(ren,r.x+1,r.y+1,r.x+r.w-2,r.y+1);
    SDL_RenderDrawLine(ren,r.x+1,r.y+1,r.x+1,r.y+r.h-2);
    SDL_SetRenderDrawColor(ren,7,8,11,static_cast<uint8_t>(std::clamp(static_cast<int>(220 * opacity_mult), 20, 255)));
    SDL_RenderDrawLine(ren,r.x+1,r.y+r.h-2,r.x+r.w-2,r.y+r.h-2);
}
enum : int { TB_TOGGLE = 1, TB_ADD, TB_FIRE, TB_MOVE, TB_STOP, TB_GUARD, TB_SCATTER, TB_DEPLOY, TB_BRIEF,
             TB_BOX, TB_MORE, TB_TYPE, TB_HOME, TB_EVENT, TB_NEXT, TB_PREV, TB_FOLLOW, TB_ROUTE,
             TB_PSI = 30, TB_MINER, TB_REVEAL, TB_DOMINATOR, TB_MUTATOR,
             TB_VIEW1 = 90, TB_GROUP1 = 100 };
static int touch_action_id(const std::string& key) {
    const char* keys[]={"STOP","GUARD","DEPLOY","SCATTER","FIRE","MOVE","ADD","BOX","ROUTE","HOME",
        "NEXT","PREV","TYPE","EVENT","FOLLOW","BRIEF","MORE","PSI","MINER","REVEAL","DOMINATOR","MUTATOR"};
    const int ids[]={TB_STOP,TB_GUARD,TB_DEPLOY,TB_SCATTER,TB_FIRE,TB_MOVE,TB_ADD,TB_BOX,TB_ROUTE,TB_HOME,
        TB_NEXT,TB_PREV,TB_TYPE,TB_EVENT,TB_FOLLOW,TB_BRIEF,TB_MORE,TB_PSI,TB_MINER,TB_REVEAL,TB_DOMINATOR,TB_MUTATOR};
    for(int i=0;i<22;++i)if(key==keys[i])return ids[i];
    if(key.size()==6 && key.rfind("GROUP",0)==0 && key[5]>='0' && key[5]<='9')
        return TB_GROUP1+(key[5]=='0'?9:key[5]-'1');
    return -1;
}
static const char* touch_action_label(int id) {
    switch(id) {
        case TB_ADD:return "+SELECT";case TB_MOVE:return "ATK MOVE";case TB_ROUTE:return "ROUTE Z";
        case TB_PSI:return "PSI WAVE";case TB_MINER:return "MINER";case TB_REVEAL:return "REVEAL";
        case TB_DOMINATOR:return "DOMINATE";case TB_MUTATOR:return "MUTATE";
        case TB_GROUP1:return "1";case TB_GROUP1+1:return "2";case TB_GROUP1+2:return "3";
        case TB_GROUP1+3:return "4";case TB_GROUP1+4:return "5";case TB_GROUP1+5:return "6";
        case TB_GROUP1+6:return "7";case TB_GROUP1+7:return "8";case TB_GROUP1+8:return "9";case TB_GROUP1+9:return "0";
        case TB_STOP:return "STOP";case TB_GUARD:return "GUARD";case TB_DEPLOY:return "DEPLOY";case TB_SCATTER:return "SCATTER";
        case TB_FIRE:return "FIRE";case TB_BOX:return "BOX";case TB_HOME:return "HOME";case TB_NEXT:return "NEXT";
        case TB_PREV:return "PREV";case TB_TYPE:return "TYPE";case TB_EVENT:return "EVENT";
        case TB_FOLLOW:return "FOLLOW";case TB_BRIEF:return "BRIEF";default:return "MORE";
    }
}
static ra2engine::SuperweaponType touch_yuri_power(int id) {
    using T=ra2engine::SuperweaponType;
    return id==TB_REVEAL?T::PsychicReveal:id==TB_DOMINATOR?T::PsychicDominator:id==TB_MUTATOR?T::GeneticConverter:T::None;
}
// Query live selection without issuing simulation orders.
const char* touch_deploy_label() const {
    const char* result = nullptr;
    auto add = [&](const char* kind) { if (!result) result = kind; else if (std::string(result) != kind) result = "ACTION"; };
    for (int i : input_.selection().ids()) {
        if (i < 0 || i >= static_cast<int>(world_.units().size())) continue;
        const auto& u = world_.units()[i];
        if (!u.alive || u.die_time >= 0 || u.cargo_of >= 0 || !world_.controllable(u.house)) continue;
        if (!u.cargo.empty()) add("UNLOAD");
        else if (u.deploys_into >= 0 || u.deploy_fire || (u.abil.deployer && u.abil.deploy.valid))
            add(u.deployed ? "UNDEPLOY" : "DEPLOY");
    }
    const int sb = input_.selected_building();
    if (sb >= 0 && sb < static_cast<int>(world_.buildings().size())) {
        const auto& b = world_.buildings()[sb];
        if (b.house == local_house_) {
            if (!b.occupants.empty()) add("UNLOAD");
            else if (world_.can_pack_slave_miner(sb) || world_.can_pack_conyard(sb)) add("UNDEPLOY");
        }
    }
    return result ? result : "DEPLOY";
}
bool touch_button_enabled(int id) const {
    if(id==TB_PSI || id==TB_MINER)return touch_yuri_deploy_available(id==TB_MINER);
    const auto power=touch_yuri_power(id);
    if(power!=ra2engine::SuperweaponType::None)return world_.sw_ready(local_house_,power);
    if (id == TB_DEPLOY) {
        for (int i : input_.selection().ids()) {
            if (i < 0 || i >= static_cast<int>(world_.units().size())) continue;
            const auto& u = world_.units()[i];
            if (u.alive && u.die_time < 0 && u.cargo_of < 0 && world_.controllable(u.house) &&
                (!u.cargo.empty() || u.deploys_into >= 0 || u.deploy_fire || (u.abil.deployer && u.abil.deploy.valid))) return true;
        }
        const int b = input_.selected_building();
        return b >= 0 && b < static_cast<int>(world_.buildings().size()) &&
            world_.buildings()[b].house == local_house_ &&
            (!world_.buildings()[b].occupants.empty() || world_.can_pack_slave_miner(b) || world_.can_pack_conyard(b));
    }
    if (id == TB_STOP || id == TB_GUARD || id == TB_SCATTER || id == TB_FIRE || id == TB_MOVE) {
        if((id==TB_STOP || id==TB_FIRE) && world_.building_can_attack(input_.selected_building()))return true;
        for (int i : input_.selection().ids()) {
            if (i < 0 || i >= static_cast<int>(world_.units().size())) continue;
            const auto& u = world_.units()[i];
            if (u.alive && u.die_time < 0 && u.cargo_of < 0 && world_.controllable(u.house)) return true;
        }
        return false;
    }
    return true;
}
SDL_Rect touch_panel_rect() const {
    if(touch_free_layout())return SDL_Rect{0,0,0,0};
    if (touch_panel_collapsed_) return touch_panel_toggle_rect();
    const int m=ra2engine::TouchBarLayout::scaled(8);
    return touch_dock_offset(SDL_Rect{m,touch_bar_top(),std::min(ra2engine::TouchBarLayout::scaled(300),playfield_w_-2*m),kWinH-m-touch_bar_top()});
}
SDL_Rect touch_panel_toggle_rect() const {
    const auto r = ra2engine::TouchBarLayout::toggle(playfield_w_,kWinH,
        touch_dock_count(),touch_panel_collapsed_);
    return touch_dock_offset(SDL_Rect{r.x,r.y,r.w,r.h});
}
bool touch_panel_toggle_at(int x,int y) const {
    const auto r = touch_panel_toggle_rect();
    return !touch_free_layout() && touch_bar_visible() && x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
}
void draw_touch_panel_toggle(SDL_Renderer* ren) {
    const auto r = touch_panel_toggle_rect();
    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);
    const bool armed = touch_add_ || touch_fire_ || touch_move_ || touch_box_;
    const auto skin = touch_skin();
    touch_metal(ren,r,armed ? skin.active : skin.panel);
    touch_colour(ren,skin.accent);SDL_RenderDrawRect(ren,&r);
    const int x=r.x+r.w/2,y=r.y+r.h/2,d=touch_panel_collapsed_ ? 1 : -1;
    SDL_RenderDrawLine(ren,x-d*5,y-10,x+d*5,y);
    SDL_RenderDrawLine(ren,x+d*5,y,x-d*5,y+10);
}
bool touch_panel_contains(int x, int y) const {
    const auto r = touch_panel_rect();
    return touch_bar_visible() && ((x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h) || touch_btn_at(x,y)>=0);
}
void draw_touch_icon(SDL_Renderer* ren, int id, const SDL_Rect& r, bool enabled) {
    touch_colour(ren,enabled ? touch_skin().accent : ra2assets::Rgba{112,112,119,255});
    const int x = r.x+r.w/2, y = r.y+20;
    auto line = [&](int a,int b,int c,int d){ SDL_RenderDrawLine(ren,x+a,y+b,x+c,y+d); };
    if (id == TB_STOP) { SDL_Rect q{x-9,y-9,18,18}; SDL_RenderDrawRect(ren,&q); }
    else if (id == TB_GUARD) { line(-10,-9,0,-13);line(0,-13,10,-9);line(10,-9,8,5);line(8,5,0,12);line(0,12,-8,5);line(-8,5,-10,-9); }
    else if (id == TB_DEPLOY) { line(0,-12,0,7);line(-7,0,0,7);line(0,7,7,0);line(-11,7,-11,12);line(-11,12,11,12);line(11,12,11,7); }
    else if (id == TB_FIRE || id == TB_EVENT) { SDL_Rect q{x-7,y-7,14,14}; SDL_RenderDrawRect(ren,&q);line(-13,0,13,0);line(0,-13,0,13); }
    else if (id == TB_MOVE) {line(-12,0,12,0);line(4,-8,12,0);line(12,0,4,8);}
    else if (id == TB_ADD) {line(-11,0,11,0);line(0,-11,0,11);}
    else if (id == TB_SCATTER) {for (int d : {-1,1}) {line(0,0,d*11,d*11);line(d*11,d*11,d*4,d*11);line(d*11,d*11,d*11,d*4);line(0,0,d*11,-d*11);}}
    else if (id == TB_HOME) {line(-12,0,0,-12);line(0,-12,12,0);line(-8,-4,-8,10);line(-8,10,8,10);line(8,10,8,-4);}
    else if (id == TB_BOX || id == TB_TYPE) {SDL_Rect q{x-11,y-10,22,20};SDL_RenderDrawRect(ren,&q);line(-5,-4,5,4);}
    else if (id == TB_TOGGLE || id == TB_PREV) {line(7,-10,-5,0);line(-5,0,7,10);}
    else if (id == TB_NEXT) {line(-7,-10,5,0);line(5,0,-7,10);}
    else {for (int d : {-7,0,7}) line(-11,d,11,d);}
}
void draw_touch_selection(SDL_Renderer* ren, ra2engine::TextureCache& tc) {
    const auto panel = touch_panel_rect();
    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);
    const auto skin = touch_skin();
    touch_metal(ren,panel,skin.panel);
    touch_colour(ren,skin.border); SDL_RenderDrawRect(ren,&panel);
    touch_colour(ren,skin.accent);
    SDL_RenderDrawLine(ren,panel.x+1,panel.y+1,panel.x+panel.w-2,panel.y+1);
    const int first = input_.selection().lowest();
    std::string title = input_.planning_mode() ? "ROUTE Z: TAP POINTS" : "NO UNIT SELECTED";
    bool pictured = false;
    if (first >= 0 && first < static_cast<int>(world_.units().size())) {
        const auto& u = world_.units()[first]; title = u.image;
        for (size_t i=0;i<ur_.defs.size();++i) {
            if (ur_.defs[i].combat.image != u.image) continue;
            title = ur_.defs[i].id;
            if (i < ur_.has_cameo.size() && ur_.has_cameo[i]) {
                blit_shp(ren,tc,"panel:"+title,ur_.cameos[i],0,ba_.cameo_pal,panel.x+2,panel.y+2);
                pictured = true;
            }
            break;
        }
    } else if (input_.selected_building() >= 0) title = "BUILDING SELECTED";
    if (!pictured) {
        SDL_Rect p{panel.x+4,panel.y+5,54,40}; SDL_SetRenderDrawColor(ren,70,87,72,255);SDL_RenderDrawRect(ren,&p);
        draw_text(ren,"UNIT",p.x+8,p.y+16,1,170,190,150);
    }
    // Leave the fold arrow clear even with a long rules id.
    if(input_.planning_mode())title="ROUTE Z: "+std::to_string(input_.planned_moves())+" / TAP Z TO GO";
    title = title.substr(0,24);
    draw_text(ren,title,panel.x+66,panel.y+8,1,skin.text.r,skin.text.g,skin.text.b);
    int hp=0,maximum=0,count=0;
    for (int id : input_.selection().ids()) {
        if (id < 0 || id >= static_cast<int>(world_.units().size())) continue;
        const auto& u=world_.units()[id];
        if (!u.alive || u.die_time>=0) continue;
        hp+=std::max(0,u.hp); maximum+=std::max(1,u.max_hp); ++count;
    }
    const int b=input_.selected_building();
    if (!count && b>=0 && b<static_cast<int>(world_.buildings().size())) {
        const auto& building=world_.buildings()[b]; hp=std::max(0,building.hp);maximum=std::max(1,building.max_hp);
    }
    if (maximum>0) {
        SDL_Rect track{panel.x+66,panel.y+23,std::max(1,panel.w-122),5};
        SDL_SetRenderDrawColor(ren,8,9,12,255);SDL_RenderFillRect(ren,&track);
        track.w=std::clamp(static_cast<int>(static_cast<double>(track.w)*hp/maximum),0,track.w);
        SDL_SetRenderDrawColor(ren,hp*2<maximum?220:112,hp*2<maximum?85:205,100,255);SDL_RenderFillRect(ren,&track);
    }
    const std::string detail=maximum>0 ? std::to_string(hp)+" / "+std::to_string(maximum)+" HP" : "TAP GROUP / HOLD TO ASSIGN";
    draw_text(ren,detail,panel.x+66,panel.y+35,1,skin.text.r,skin.text.g,skin.text.b);
}
int touch_bar_top() const {
    if (touch_panel_collapsed_) return touch_panel_toggle_rect().y;
    return ra2engine::TouchBarLayout::top(kWinH, touch_dock_count());
}
const std::vector<TouchBtn>& touch_buttons() const {
    static const std::vector<TouchBtn> all = [] {
        if (const char* m = std::getenv("RA2_TOUCH_MAPPING")) {
            if (m[0] != '\0') {
                std::vector<TouchBtn> custom;
                std::string s(m);
                std::size_t start = 0;
                while (start < s.size()) {
                    std::size_t end = s.find(',', start);
                    if (end == std::string::npos) end = s.size();
                    std::string token = s.substr(start, end - start);
                    start = end + 1;
                    const int id=touch_action_id(token);
                    if(id>=0 && std::none_of(custom.begin(),custom.end(),[&](const TouchBtn& b){return b.id==id;}))
                        custom.push_back({touch_action_label(id),id});
                }
                const char* version=std::getenv("RA2_TOUCH_MAPPING_VERSION");
                if(version && std::string(version)=="2")return custom; // NONE intentionally hides every action.
                if (!custom.empty()) {
                    custom.push_back({"MORE", TB_MORE});
                    for(int i=0;i<5;++i)custom.push_back({touch_action_label(TB_GROUP1+i),TB_GROUP1+i});
                    return custom;
                }
            }
        }
        int mask=511;
        if(const char* v=std::getenv("RA2_TOUCH_ACTIONS"))mask=std::clamp(std::atoi(v),0,1023);
        const TouchBtn actions[]={{"STOP",TB_STOP},{"GUARD",TB_GUARD},{"DEPLOY",TB_DEPLOY},
            {"SCATTER",TB_SCATTER},{"FIRE",TB_FIRE},{"ATK MOVE",TB_MOVE},{"+SELECT",TB_ADD},
            {"TYPE",TB_TYPE},{"ROUTE Z",TB_ROUTE}};
        std::vector<TouchBtn> out;
        for(int i=0;i<9;++i)if(mask&(1<<i))out.push_back(actions[i]);
        out.push_back({"MORE",TB_MORE});
        for(int i=0;i<5;++i)out.push_back({i==0?"1":i==1?"2":i==2?"3":i==3?"4":"5",TB_GROUP1+i});
        return out;
    }();
    // W6-01-09: page 2 -- the KeyboardMD.ini commands with no other touch path.
    static const std::vector<TouchBtn> more = {
        {"BACK", TB_TOGGLE}, {"TYPE", TB_TYPE}, {"HOME", TB_HOME}, {"EVT", TB_EVENT},
        {"NEXT", TB_NEXT}, {"PREV", TB_PREV}, {"FOLW", TB_FOLLOW}, {"6", TB_GROUP1 + 5},
        {"7", TB_GROUP1 + 6}, {"8", TB_GROUP1 + 7}, {"9", TB_GROUP1 + 8}, {"0", TB_GROUP1 + 9},
        {"ROUTE Z", TB_ROUTE}, {"V1", TB_VIEW1}, {"V2", TB_VIEW1 + 1}, {"V3", TB_VIEW1 + 2}, {"BOX", TB_BOX}, {"BRF", TB_BRIEF}};
    if (touch_page2_) return more;
    return all;
}
struct CustomTouchPos { int id; float x_pct; float y_pct; float size=100; };
static const std::vector<CustomTouchPos>& touch_custom_positions() {
    static const std::vector<CustomTouchPos> pos = [] {
        std::vector<CustomTouchPos> out;
        if (const char* env = std::getenv("RA2_TOUCH_POSITIONS")) {
            std::string s(env);
            std::size_t start = 0;
            while (start < s.size()) {
                std::size_t end = s.find(';', start);
                if (end == std::string::npos) end = s.size();
                std::string token = s.substr(start, end - start);
                start = end + 1;
                std::size_t colon = token.find(':');
                std::size_t comma = token.find(',');
                if (colon != std::string::npos && comma != std::string::npos && comma > colon) {
                    std::string key = token.substr(0, colon);
                    float x = static_cast<float>(std::atof(token.substr(colon + 1, comma - colon - 1).c_str()));
                    float y = static_cast<float>(std::atof(token.substr(comma + 1).c_str()));
                    const auto size_at=token.find(',',comma+1);
                    float size=size_at==std::string::npos ? 100.0f : std::atof(token.substr(size_at+1).c_str());
                    if(!std::isfinite(size))size=100;
                    const int id=touch_action_id(key);
                    if(id>=0 && std::isfinite(x) && std::isfinite(y))
                        out.push_back({id,std::clamp(x,0.0f,100.0f),std::clamp(y,0.0f,100.0f),std::clamp(size,40.0f,180.0f)});
                }
            }
        }
        return out;
    }();
    return pos;
}

bool touch_is_floating(int id) const {
    if(touch_free_layout())return true;
    for(const auto& p:touch_custom_positions())if(p.id==id)return true;return false;
}
int touch_dock_count() const {
    int n=0;for(const auto& b:touch_buttons())if(!touch_is_floating(b.id))++n;return n;
}
SDL_Rect touch_dock_offset(SDL_Rect r) const {
    const auto* dock=std::getenv("RA2_TOUCH_DOCK");
    if(dock && std::string(dock)=="bottom_right") {
        const int m=ra2engine::TouchBarLayout::scaled(8),pw=std::min(ra2engine::TouchBarLayout::scaled(300),playfield_w_-2*m);
        r.x+=std::max(0,playfield_w_-2*m-pw);
    }
    if(dock && std::string(dock)=="left_side" && !touch_panel_collapsed_)
        r.y-=std::max(0,ra2engine::TouchBarLayout::top(kWinH,touch_dock_count())/2);
    return r;
}
bool touch_bar_visible() const {
    return touch_ui_ && !menu_.open_ && !sld_.is_open() && !gf_abort_.open() && !input_locked_ && !net_chat_open_;
}
// Primary row sits at the bottom; extra shortcuts expand upwards or at custom free positions.
SDL_Rect touch_btn_rect(int i) const {
    const auto& btns = touch_buttons();
    if (i >= 0 && i < static_cast<int>(btns.size())) {
        int id = btns[i].id;
        for (const auto& cp : touch_custom_positions()) {
            if (cp.id == id) {
                if(touch_free_layout()) {
                    const auto r=ra2engine::TouchBarLayout::floating(kWinW,kWinH,cp.x_pct,cp.y_pct,cp.size);
                    return SDL_Rect{r.x,r.y,r.w,r.h};
                }
                const auto base = ra2engine::TouchBarLayout::button(playfield_w_, kWinH, touch_btn_count(), i);
                int total_w = kWinW;
                int x = std::clamp(static_cast<int>(cp.x_pct * static_cast<float>(total_w - base.w) / 100.0f), 0, std::max(0, total_w - base.w));
                int y = std::clamp(static_cast<int>(cp.y_pct * static_cast<float>(kWinH - base.h) / 100.0f), 0, std::max(0, kWinH - base.h));
                return SDL_Rect{x, y, base.w, base.h};
            }
        }
    }
    if(touch_free_layout()) {
        // Extra MORE-page commands also remain independent floating buttons.
        const auto r=ra2engine::TouchBarLayout::floating(kWinW,kWinH,10.0f+(i%6)*16.0f,20.0f+(i/6)*24.0f);
        return SDL_Rect{r.x,r.y,r.w,r.h};
    }
    int dock_index=0;for(int j=0;j<i;++j)if(!touch_is_floating(btns[j].id))++dock_index;
    const auto r = ra2engine::TouchBarLayout::button(playfield_w_, kWinH, touch_dock_count(), dock_index);
    return touch_dock_offset(SDL_Rect{r.x, r.y, r.w, r.h});
}
int touch_btn_count() const {
    return touch_panel_collapsed_ && !touch_free_layout() ? 0 : static_cast<int>(touch_buttons().size());
}
int touch_btn_at(int x, int y) const {
    if (!touch_bar_visible()) return -1;
    const int n = touch_btn_count();
    for (int i = n-1; i >= 0; --i) {
        const SDL_Rect r = touch_btn_rect(i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h && r.y + r.h <= kWinH) return i;
    }
    return -1;
}
void touch_command(int id, bool long_press, uint32_t t_ms) {
    bool one_shot = true;
    switch (id) {
        case TB_TOGGLE: touch_page2_ = false; one_shot = false; break;
        case TB_MORE: touch_page2_ = !touch_page2_; one_shot = false; break;   // W6-01-09
        case TB_BOX:   // W6-01-11: sticky band-box drag
            touch_box_ = !touch_box_;
            ra2engine::TouchGestures::set_box_mode(touch_box_);
            one_shot = false;
            break;
        case TB_TYPE: input_.type_select(); break;          // TypeSelect FUN_00731A20
        case TB_HOME: center_base(); break;                 // CenterBase
        case TB_EVENT: hotkey_radar_event(t_ms); break;     // CenterOnRadarEvent
        case TB_NEXT: cycle_object(+1); one_shot = false; break;   // NextObject (repeatable)
        case TB_PREV: cycle_object(-1); one_shot = false; break;   // PreviousObject
        case TB_FOLLOW: hotkey_follow(); break;              // Follow
        case TB_ADD: touch_add_ = !touch_add_; one_shot = false; break;
        case TB_FIRE:
            touch_fire_ = !touch_fire_; touch_move_ = false;
            if (touch_fire_) { touch_add_ = false; touch_box_ = false; }
            one_shot = false; break;
        case TB_MOVE:
            touch_move_ = !touch_move_; touch_fire_ = false;
            if (touch_move_) touch_box_ = false;
            one_shot = false; break;
        case TB_ROUTE: input_.toggle_planning(); one_shot=false; break;
        case TB_STOP: input_.stop_selected(); break;
        case TB_GUARD: input_.guard_selected(); break;
        case TB_SCATTER: input_.scatter_selected(); break;
        case TB_DEPLOY: deploy_selected(); break;
        case TB_PSI: touch_yuri_deploy(false); break;
        case TB_MINER: touch_yuri_deploy(true); break;
        case TB_REVEAL:case TB_DOMINATOR:case TB_MUTATOR:
            if(world_.sw_ready(local_house_,touch_yuri_power(id)))w2_arm_sw(touch_yuri_power(id));
            break;
        case TB_BRIEF: show_briefing_ = !show_briefing_; break;
        default:
            if (id >= TB_VIEW1 && id < TB_VIEW1 + 3) {   // W6-01-09: View / SetView 1-3
                const int v = id - TB_VIEW1;
                if (long_press) {
                    view_cam_[v][0] = cam_.x;
                    view_cam_[v][1] = cam_.y;
                    view_set_[v] = true;
                } else if (view_set_[v]) {
                    cam_.x = view_cam_[v][0];
                    cam_.y = view_cam_[v][1];
                }
            }
            if (id >= TB_GROUP1 && id < TB_GROUP1 + 10) {
                const int n = id - TB_GROUP1 + 1;
                if (long_press) input_.group_create(n);
                else input_.group_select(n, t_ms);
            }
            break;
    }
    if (one_shot) { touch_page2_ = false; } // Keep primary shortcuts visible.
}
// Consumes taps on the bar; stamps the modifier toggles on other touch
// pointer events (so Input sees Shift / Ctrl / Alt exactly as from a
// keyboard). Returns true when the event was consumed.
bool touch_bar_handle(ra2engine::InputEvent& ev) {
    if (ev.touch) touch_ui_ = true;
    // A drag that starts on the dock does not scroll the map.
    if (ev.kind == ra2engine::InputEvent::Pan && ev.touch && touch_panel_contains(ev.x0, ev.y0)) return true;
    const bool ptr = ev.kind == ra2engine::InputEvent::PointerDown || ev.kind == ra2engine::InputEvent::PointerUp;
    if (!ptr) {
        if (ev.kind == ra2engine::InputEvent::PointerMove && !touch_panel_capture_)
            touch_apply_modifiers(ev);
        return touch_panel_capture_ && ev.kind == ra2engine::InputEvent::PointerMove;
    }
    if (ev.kind == ra2engine::InputEvent::PointerDown) touch_panel_capture_ = touch_panel_contains(ev.x,ev.y);
    // A map-origin selection drag must still receive its release over the panel.
    if (ev.kind == ra2engine::InputEvent::PointerUp && !touch_panel_capture_ && touch_down_btn_ < 0 &&
        !input_.placement_active()) {
        touch_apply_modifiers(ev);
        return false;
    }
    if (ev.kind == ra2engine::InputEvent::PointerDown && touch_panel_toggle_at(ev.x,ev.y)) {
        touch_down_btn_ = -2; // Capture the arrow; release must land on it too.
        return true;
    }
    if (ev.kind == ra2engine::InputEvent::PointerUp && touch_down_btn_ == -2) {
        if (touch_panel_toggle_at(ev.x,ev.y)) touch_panel_collapsed_ = !touch_panel_collapsed_;
        touch_down_btn_ = -1;
        touch_panel_capture_ = false;
        return true;
    }
    const int bi = touch_btn_at(ev.x, ev.y);
    if (bi >= 0) {
        if (ev.kind == ra2engine::InputEvent::PointerDown) {
            touch_down_btn_ = bi;
        } else if (touch_down_btn_ == bi) {
            if (touch_button_enabled(touch_buttons()[static_cast<size_t>(bi)].id))
            touch_command(touch_buttons()[static_cast<size_t>(bi)].id,
                          ev.button == ra2engine::InputEvent::Right, ev.t_ms);   // hold = right
            touch_down_btn_ = -1; touch_panel_capture_ = false;
        }
        return true;
    }
    if (ev.kind == ra2engine::InputEvent::PointerUp && touch_down_btn_ >= 0) {   // slid off a button
        touch_down_btn_ = -1; touch_panel_capture_ = false;
        return true;
    }
    const bool panel_up = ev.kind == ra2engine::InputEvent::PointerUp && touch_panel_capture_;
    if (ev.kind == ra2engine::InputEvent::PointerUp) touch_panel_capture_ = false;
    if (panel_up || touch_panel_contains(ev.x,ev.y)) return true;
    if (!ev.touch) return false;
    // W2-AR-16c: placing a building by touch -- the first tap shows the
    // footprint there, a second tap on the same cell places it.
    if (input_.placement_active() && !(sidebar_up_ && lay_.contains(ev.x, ev.y))) {
        if(ev.button==ra2engine::InputEvent::Right) {
            // A cancel must reach Input even after a preview-only placement tap.
            touch_place_swallow_up_=false;
            touch_place_cx_=touch_place_cy_=INT_MIN;
            return false;
        }
        ra2engine::InputEvent mv = ev;
        mv.kind = ra2engine::InputEvent::PointerMove;
        input_.handle(mv);
        int cx = 0, cy = 0;
        input_.placement_cell(cx, cy);
        if (ev.kind == ra2engine::InputEvent::PointerDown && ev.button == ra2engine::InputEvent::Left) {
            if (cx != touch_place_cx_ || cy != touch_place_cy_) {
                touch_place_cx_ = cx;
                touch_place_cy_ = cy;
                touch_place_swallow_up_ = true;
                return true;   // preview only
            }
        } else if (touch_place_swallow_up_) {
            touch_place_swallow_up_ = false;
            return true;
        }
    } else {
        touch_place_cx_ = touch_place_cy_ = INT_MIN;
    }
    touch_apply_modifiers(ev);
    return false;
}
void touch_apply_modifiers(ra2engine::InputEvent& ev) {
    if (!ev.touch) return;
    // Apply on move AND release: Input refreshes Ctrl on every pointer
    // event and commits the command on release, not on press.
    if (!(sidebar_up_ && lay_.contains(ev.x, ev.y))) {
        ev.shift = ev.shift || touch_add_ || touch_move_;
        ev.ctrl = ev.ctrl || touch_fire_ || touch_move_;
        // ATK MOVE uses the existing Ctrl+Shift attack-move command, not Alt move.
        // FIRE / ATK MOVE are one-shot: spent by a left PointerUp.
        if (ev.kind == ra2engine::InputEvent::PointerUp && ev.button == ra2engine::InputEvent::Left &&
            (touch_fire_ || touch_move_)) {
            touch_fire_ = touch_move_ = false;
        }
    }
}
// The dock stays where the player left it; commands collapse the extra row.
void touch_bar_tick(double dt) {
    (void)dt;
    ra2engine::TouchGestures::set_box_mode(touch_box_ && touch_bar_visible());
}
void draw_touch_bar(SDL_Renderer* ren, ra2engine::TextureCache& tc) {
    if (!touch_bar_visible()) return;
    if(!touch_free_layout()) {
        if (touch_panel_collapsed_) { draw_touch_panel_toggle(ren); return; }
        draw_touch_selection(ren, tc);
        draw_touch_panel_toggle(ren);
    }
    const auto& bs = touch_buttons();
    const int n = touch_btn_count();
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < n; ++i) {
        const SDL_Rect r = touch_btn_rect(i);
        if (r.y + r.h > kWinH || r.y<0) continue;
        const int id = bs[static_cast<size_t>(i)].id;
        const bool armed_mod = touch_add_ || touch_fire_ || touch_move_ || touch_box_;
        const bool on = (id == TB_ADD && touch_add_) || (id == TB_FIRE && touch_fire_) ||
                        (id == TB_MOVE && touch_move_) || (id == TB_TOGGLE && (touch_bar_open_ || armed_mod)) ||
                        (id == TB_BRIEF && show_briefing_) || (id == TB_BOX && touch_box_) ||
                        (id == TB_ROUTE && input_.planning_mode()) || (id == TB_MORE && touch_page2_);
        const auto skin=touch_skin();
        touch_metal(ren,r,on ? skin.active : skin.panel);
        touch_colour(ren,on ? skin.accent : skin.border);
        SDL_RenderDrawRect(ren, &r);
        const bool enabled = touch_button_enabled(id);
        if (!enabled) {
            SDL_SetRenderDrawColor(ren, 12, 16, 18, 180);
            SDL_RenderFillRect(ren, &r);
        }
        std::string label = id == TB_DEPLOY ? touch_deploy_label() : bs[static_cast<size_t>(i)].label;
        if (id == TB_TOGGLE && !touch_bar_open_ && armed_mod)   // closed: show what is armed
            label = touch_add_ ? "+SEL" : touch_fire_ ? "FIRE" : touch_move_ ? "ATK MOVE" : "BOX";
        const bool has_icon = !touch_free_layout() && id < TB_PSI && id != TB_BRIEF;
        if (has_icon) draw_touch_icon(ren, id, r, enabled);
        const int scale = label.size() <= 2 ? 2 : 1;
        const int lw = static_cast<int>(label.size()) * 6 * scale - scale;
        const int ly = has_icon ? r.y + r.h - 14 : r.y + (r.h - 7*scale)/2;
        if(touch_free_layout()) {
            const float font=std::min(r.h/14.0f,r.w/float(label.size()*6+8));
            float sx=1,sy=1;SDL_RenderGetScale(ren,&sx,&sy);SDL_RenderSetScale(ren,sx*font,sy*font);
            draw_text(ren,label,int((r.x+r.w/2)/font)-int(label.size()*6-1)/2,int((r.y+r.h/2)/font)-3,1,
                enabled?235:115,enabled?225:120,enabled?200:115);
            SDL_RenderSetScale(ren,sx,sy);
        } else draw_text(ren, label, r.x + (r.w-lw)/2, ly, scale,
                  enabled ? 235 : 115, enabled ? 225 : 120, enabled ? 200 : 115);
    }
}
#ifdef __ANDROID__
bool touch_ui_ = true;    // a phone: the dock is there from the first frame
#else
bool touch_ui_ = false;   // desktop: appears after the first touch event
#endif
bool touch_bar_open_ = true; // Compatibility state for diagnostics.
bool touch_add_ = false, touch_fire_ = false, touch_move_ = false;
bool touch_box_ = false;     // W6-01-11 sticky BOX (TouchGestures::set_box_mode)
bool touch_page2_ = false;   // W6-01-09 the MORE page
int touch_down_btn_ = -1;
int touch_place_cx_ = INT_MIN, touch_place_cy_ = INT_MIN;
bool touch_place_swallow_up_ = false;

bool touch_panel_capture_ = false;

bool touch_panel_collapsed_ = !std::getenv("RA2_TOUCH_COLLAPSED") || std::string(std::getenv("RA2_TOUCH_COLLAPSED"))!="0"; // Start unobtrusively; the arrow expands it.

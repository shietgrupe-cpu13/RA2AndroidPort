// pres_turret.inc -- W2-GA-13 base-defence turrets (Patriot SAM, Flak Cannon,
// Sentry Gun, Grand Cannon ...). Included by main.cpp before render_world;
// GameScene loads it and publishes g_turrets (null in every gate).
//
// [RE: rulesmd BuildingTypes Turret=yes, TurretAnim=<vxl>, TurretAnimIsVoxel,
// TurretAnimX/Y (screen px from the building's draw point = origin-cell top vertex), TurretAnimZAdjust
// (depth only)]: the turret voxel (+ optional <name>barl) is rasterised per
// facing in each house colour and drawn over the building,
// facing the sim's slewed turret (Building::def_turret_facing, ROT per
// step_building_combat) [W5-PRES R-121; the idle behaviour: docs/re/
// w5-presentation.md §5].

class PresTurrets {
public:
    struct Type {
        int x = 0, y = 0;
        std::shared_ptr<VehicleAssets::VoxelSrc> src;
        bool has_barrel = false;
        mutable ra2engine::VoxelAnimationCache animation_sprites{2u << 20};
        mutable std::map<int, std::shared_ptr<std::array<ra2engine::FacingSet, 2>>> sets;   // colour key
    };

    void load(const ra2assets::MixFile& ra2, const std::string& dir, const Ini& rules_ini,
              const BuildingAssets& ba, const ra2assets::Palette& unit_pal) {
        types_.clear();
        std::unique_ptr<MixFile> local, md_local;
        try { local = std::make_unique<MixFile>(ra2.open_nested("local.mix")); } catch (...) {}
        try { md_local = std::make_unique<MixFile>(MixFile::open_path(dir + "ra2md.mix").open_nested("localmd.mix")); } catch (...) {}
        auto part = [&](const std::string& base, ra2assets::VxlModel& m, ra2assets::HvaModel& h) {
            for (MixFile* src : {local.get(), md_local.get()}) {
                if (!src) continue;
                const MixEntry* ve = src->find(base + ".vxl");
                const MixEntry* he = src->find(base + ".hva");
                if (!ve || !he) continue;
                const auto vr = src->read(*ve);
                const auto hr = src->read(*he);
                m = ra2assets::load_vxl(vr.data(), vr.size());
                h = ra2assets::load_hva(hr.data(), hr.size());
                return true;
            }
            return false;
        };
        ra2assets::Vpl vpl;
        bool have_vpl = false;
        if (local)
            if (const MixEntry* pe = local->find(std::string("voxels.vpl"))) {
                const auto raw = local->read(*pe);
                vpl = ra2assets::Vpl::load(raw.data(), raw.size());
                have_vpl = true;
            }
        if (!have_vpl) return;
        for (const ra2engine::BuildingTypeDef& d : ba.roster) {
            const std::string anim = pres_clean(rules_ini.get(d.id, "TurretAnim"));
            if (anim.empty()) continue;
            if (pres_lower(pres_clean(rules_ini.get(d.id, "TurretAnimIsVoxel"))).rfind("t", 0) != 0) continue;
            auto src = std::make_shared<VehicleAssets::VoxelSrc>();
            if (!part(pres_lower(anim), src->hull, src->hull_h)) {
                std::cerr << "[turret] " << d.id << " TurretAnim=" << anim << " voxel missing\n";
                continue;
            }
            Type t;
            t.has_barrel = part(pres_lower(anim) + "barl", src->tur, src->tur_h);
            src->vpl = vpl;
            src->pal = unit_pal;
            t.src = src;
            t.x = std::atoi(pres_clean(rules_ini.get(d.id, "TurretAnimX")).c_str());
            t.y = std::atoi(pres_clean(rules_ini.get(d.id, "TurretAnimY")).c_str());
            types_[d.id] = t;
        }
        std::cerr << "[turret] voxel turrets=" << types_.size() << "\n";
    }

    // Draws the turret of building `b` (type id `id`) after its body.
    void draw(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w,
              const ra2engine::Building& b, const std::string& id, int level, const ra2engine::Camera& cam,
              int W, int H) const {
        auto it = types_.find(id);
        if (it == types_.end() || b.state != ra2engine::Building::State::Idle) return;
        const Type& t = it->second;
        // W5-PRES R-121: the SIM's turret facing (Building::def_turret_facing,
        // slewed at the type's ROT by step_building_combat, voxel-render.md §5)
        // instead of snapping to the target direction.
        const auto index=static_cast<std::size_t>(&b-w.buildings().data());
        const double f = g_building_motion.turret(index,b,g_pres_render_fraction);
        const int key = ra2engine::house_draw_key(b.house);
        auto& slot = t.sets[key];
        if (!slot) {
            slot = std::make_shared<std::array<ra2engine::FacingSet, 2>>();
            const ra2assets::Palette hp = key >= 10 ? ra2engine::pal_for_color(t.src->pal, key - 10)
                                                    : ra2engine::house_draw_palette(t.src->pal, b.house);
            (*slot)[0] = ra2engine::build_facing_set(t.src->hull, t.src->hull_h, ra2engine::kVehicleFacings,
                                                     t.src->vpl, hp);
            if (t.has_barrel)
                (*slot)[1] = ra2engine::build_facing_set(t.src->tur, t.src->tur_h, ra2engine::kVehicleFacings,
                                                         t.src->vpl, hp);
        }
        const int fi = ra2engine::facing_to_index(f, ra2engine::kVehicleFacings);
        // W4-FIX-POS: TurretAnimX/Y are pixel offsets from the BUILDING DRAW
        // POINT (the origin cell's top vertex, building_draw_point) -- cell
        // (cx-0.5, cy-0.5)'s centre in draw_voxel_sprite's frame [RE: FUN_00459ee0;
        // GTGCAN TurretAnimY=28 ~ its foundation centre, 30 px below the vertex].
        const double dx = b.cx - 0.5, dy = b.cy - 0.5;
        ra2engine::Camera c2 = cam;
        c2.x += t.x;
        c2.y += t.y;
        const std::string k = "turret:" + id + ":" + std::to_string(key) + ":" + std::to_string(fi);
        t.animation_sprites.begin_frame(tc.instance_id(),tc.frame_id());
        const bool gatling = b.type>=0 && b.type<static_cast<int>(w.btypes().size()) && w.btypes()[b.type].gattling.enabled();
        const int stage = gatling ? w.btypes()[b.type].gattling.stage(false,b.gattling_charge) : 0;
        const long clock = gatling && b.gattling_charge>0 ? g_pres_tick/std::max(1,4-stage) : 0;
        auto draw_part = [&](int part) {
            const auto& model=part==0?t.src->hull:t.src->tur;
            const auto& hva=part==0?t.src->hull_h:t.src->tur_h;
            const int frame=ra2engine::hva_animation_frame(static_cast<int>(clock),hva.frames.size());
            const std::string name=k+(part?":b":"")+":f"+std::to_string(frame);
            const ra2engine::VoxelSprite* sprite=&(*slot)[part].sprites[static_cast<std::size_t>(fi)];
            if(frame) {
                sprite=t.animation_sprites.find(name);
                if(!sprite) {
                    const auto pal=key>=10?ra2engine::pal_for_color(t.src->pal,key-10):ra2engine::house_draw_palette(t.src->pal,b.house);
                    sprite=&t.animation_sprites.insert(name,ra2engine::rasterize_model(model,hva,
                        ra2engine::index_to_yaw(fi,ra2engine::kVehicleFacings),t.src->vpl,pal,frame));
                }
            }
            ra2engine::draw_voxel_sprite(ren,tc,name,*sprite,dx,dy,level,c2,W,H,0);
        };
        draw_part(0);
        if(t.has_barrel)draw_part(1);
    }
    std::size_t count() const { return types_.size(); }

private:
    std::map<std::string, Type> types_;
};

const PresTurrets* g_turrets = nullptr;   // GameScene only

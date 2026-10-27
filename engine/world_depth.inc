// G16 world depth (agent G) -- the main.cpp side: terrain/bridge arming of the
// real World, the garrison/bridge-hut building types that the skirmish map
// dressing must place as real World buildings, and the OverlayTypes runtime
// index. docs/re/world-depth.md. Interactive game only (GameScene / the real
// map worlds); every gate keeps its World.

// [OverlayTypes] names by RUNTIME index (order of appearance; the ini's own
// numbers skip 40/41/183 and start at 1) [RE: world-depth.md §B1].
std::vector<std::string> overlay_type_names(const Ini& rules_ini) {
    return ra2engine::overlay_names_in_order(rules_ini.section("OverlayTypes"));
}

// The real map's terrain: tile land types, then the low-bridge overlay
// override (LOBRDG*/LOBRDB* intact or damaged: Land=Road with
// NoUseTileLandType=true -- rulesmd; destroyed pieces keep the tile's land
// type), then World::set_land_terrain + the bridge state. Replaces the bare
// set_land_terrain(resolve_land_types(...)) of launch_skirmish and the
// campaign world. A map without low bridges gets exactly the old grids.
void depth_set_terrain(ra2engine::World& world, Assets& a) {
    const int gw = world.grid_w(), gh = world.grid_h();
    std::vector<ra2assets::LandType> lt = resolve_land_types(a.map, a.theater, gw, gh);
    const std::vector<ra2assets::LandType> tile = lt;
    std::vector<int> ovl(static_cast<std::size_t>(gw) * gh, -1);
    bool any = false;
    if (a.map.has_overlay())
        for (const ra2assets::MapCell& c : a.map.cells()) {
            if (c.x < 0 || c.y < 0 || c.x >= gw || c.y >= gh) continue;
            const int id = a.map.overlay_at(c.x, c.y);
            if (!ra2engine::is_low_bridge_overlay(id)) continue;
            const std::size_t i = static_cast<std::size_t>(c.y) * gw + c.x;
            ovl[i] = id;
            any = true;
            if (!ra2engine::low_bridge_destroyed(id)) lt[i] = ra2assets::LandType::Road;
        }
    place_overlay_terrain(world, a, lt);   // W8-03-02 rocks/fences/tracks + W8-03-01 buildable grid (w8fix-place)
    world.set_land_terrain(lt);
    if (any) world.set_bridges(std::move(ovl), tile);
    // W4-SQ-10: high-bridge decks (BRIDGE1/2 = runtime 0x18/0x19, BRIDGEB1/B2
    // = 0xED/0xEE) carry ground units over the river; the cells under them keep
    // their water for ships [RE: world-depth.md B1 anchors].
    if (a.map.has_overlay()) {
        std::vector<std::uint8_t> hb(static_cast<std::size_t>(gw) * gh, 0);
        std::vector<std::uint8_t> real_cell(hb.size(), 0);
        for (const ra2assets::MapCell& c : a.map.cells())
            if (c.x >= 0 && c.y >= 0 && c.x < gw && c.y < gh)
                real_cell[static_cast<std::size_t>(c.y) * gw + c.x] = 1;
        bool any_high = false;
        for (const ra2assets::MapCell& c : a.map.cells()) {
            if (c.x < 0 || c.y < 0 || c.x >= gw || c.y >= gh) continue;
            const int id = a.map.overlay_at(c.x, c.y);
            int xs[4], ys[4];
            if (!ra2engine::high_bridge_section_cells(id, c.x, c.y, xs, ys)) continue;
            for (int k = 0; k < 4; ++k) {
                if (xs[k] < 0 || ys[k] < 0 || xs[k] >= gw || ys[k] >= gh) continue;
                const std::size_t i = static_cast<std::size_t>(ys[k]) * gw + xs[k];
                if (!real_cell[i]) continue;   // never open cells outside the map diamond
                const ra2assets::LandType t = tile[i];
                hb[i] = (t == ra2assets::LandType::Water || t == ra2assets::LandType::Rock ||
                         t == ra2assets::LandType::Wall) ? 1 : 2;
                any_high = true;
            }
        }
        if (any_high) world.set_high_bridges(std::move(hb));
    }
}

// The real game's garrison / bridge rules (GameScene ctor; never a gate).
void depth_arm_rules(ra2engine::World& world, const Rules& rules) {
    ra2engine::World::DepthRules r;
    r.occupy_damage_mult = rules.section_double("CombatDamage", "OccupyDamageMultiplier", 1.0);
    r.occupy_rof_mult = rules.section_double("CombatDamage", "OccupyROFMultiplier", 1.0);
    r.occupy_weapon_range = static_cast<int>(rules.section_double("CombatDamage", "OccupyWeaponRange", 0.0));
    r.bridge_strength = static_cast<int>(rules.section_double("CombatDamage", "BridgeStrength", 1500.0));
    r.bridge_destruction = rules.section_flag("CombatDamage", "DestroyableBridges", true) &&
                           rules.section_flag("MultiplayerDialogSettings", "BridgeDestruction", true);
    r.armed = true;
    world.set_depth_rules(r);
    // Wave-7 fix (units): W7-01-03 the PermaDisguise look per side ([General]
    // AlliedDisguise / SovietDisguise / ThirdDisguise -> their SHP image) and
    // W7-03-04 InfantryBlinkDisguiseTime.
    const char* keys[3] = {"AlliedDisguise", "SovietDisguise", "ThirdDisguise"};
    const char* defs[3] = {"E1", "E2", "INIT"};
    for (int s = 0; s < 3; ++s) {
        const std::string id = rules.general_string(keys[s], defs[s]);
        const std::string img = rules.image(id);
        world.set_side_disguise(s, img.empty() ? id : img);
    }
    world.set_infantry_blink_disguise(
        static_cast<int>(rules.general_double("InfantryBlinkDisguiseTime", 20.0)));
}

// Is `id` a building the World must own for G16 (garrisonable or a bridge
// repair hut) although it is not in the playable roster?
bool depth_world_building(const Rules& rules, const std::string& id) {
    return rules.section_flag(id, "CanBeOccupied", false) || rules.section_flag(id, "BridgeRepairHut", false) ||
           // Wave-5 (R-056 / item 3): an engineer-capturable tech structure
           // (Capturable=yes NeedsEngineer=yes: Tech Airport, Hospital,
           // Machine Shop, Power Plant, Outpost, Secret Lab...) is a real
           // neutral World building, not map dressing.
           (rules.section_flag(id, "Capturable", false) && rules.section_flag(id, "NeedsEngineer", false));
}

// Roster index of building `id`, appending it (def + art) when absent -- the
// camp::Types::building recipe (campaign_runtime.inc) for the skirmish map
// dressing's civilian garrisons and bridge huts.
int depth_register_building(BuildingAssets& ba, const ra2assets::MixFile& ra2, const Rules& rules,
                            const Art& art, const std::string& id_in) {
    std::string id = id_in;
    for (char& ch : id) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (std::size_t i = 0; i < ba.roster.size(); ++i)
        if (ba.roster[i].id == id) return static_cast<int>(i);
    ra2engine::BuildingTypeDef d;
    d.id = id;
    const std::string art_id = rules.image(id);
    d.fw = d.fh = 1;
    art.foundation(art_id, d.fw, d.fh);
    d.strength = std::max(1, rules.strength(id));
    d.sight = rules.sight(id);
    d.armor_ix = ra2engine::armor_index(rules.armor(id));
    d.cost = rules.cost(id);
    d.charge = d.cost;
    d.power = rules.power(id);
    d.adjacent = rules.adjacent(id);
    d.tech_level = rules.tech_level(id);
    d.image = art_id;
    d.cameo = art.cameo(art_id);
    d.explosion = rules.explosion_first(id);
    d.unsellable = rules.section_flag(id, "Unsellable", false);
    d.click_repairable = rules.section_flag(id, "ClickRepairable", true);
    // W2-SK-12/19/20: the same Capturable/Spyable/UnitRepair flags as the roster.
    d.capturable = rules.section_flag(id, "Capturable", false);
    d.spyable = rules.section_flag(id, "Spyable", false);
    d.unit_repair = rules.section_flag(id, "UnitRepair", false);
    BuildingArt slot;
    (void)ra2;
    slot.ok = asset_building_shp(art_id, slot.shp);   // W3-FIX-A: global search list
    if (slot.ok) building_art_extras(slot, art, rules, id, art_id);
    ra2engine::fill_building_weapons(rules, id, d);   // also the G16 garrison/hut keys
    d.wall = rules.section_flag(id, "Wall", false);                 // W6-03-01 flag
    d.w6.wall_levels = d.wall ? art.damage_levels(art_id) : 0;   // W6-04-04
    ba.roster.push_back(d);
    ba.art.push_back(std::move(slot));
    ba.defense.push_back(0);
    ba.wall.push_back(0);
    ba.naval.push_back(0);
    ba.water_bound.push_back(0);
    ba.factory_kind.push_back("");
    ba.wall_fill_range.push_back(0);
    ba.dock_offsets.emplace_back();
    return static_cast<int>(ba.roster.size()) - 1;
}

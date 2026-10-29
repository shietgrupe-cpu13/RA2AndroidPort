#include "weapon_resolve.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include "abil_w5_rules.hpp"   // wave-5 (agent C)

#include "ra2assets/rules.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ra2engine {

WeaponSlot resolve_weapon_slot(const ra2assets::Rules& rules, const std::string& weapon) {
    WeaponSlot s;
    if (weapon.empty()) return s;
    // A weapon section always carries at least one of these keys.
    if (rules.section_string(weapon, "Damage", "").empty() &&
        rules.section_string(weapon, "ROF", "").empty() &&
        rules.section_string(weapon, "Range", "").empty())
        return s;
    s.valid = true;
    s.id = weapon;
    s.damage = rules.weapon_damage(weapon);
    s.rof = rules.weapon_rof(weapon);
    s.range = rules.weapon_range(weapon);
    const std::string wh = rules.weapon_warhead(weapon);
    s.verses = rules.verses(wh);
    s.cell_spread = rules.section_double(wh, "CellSpread", 0.0);
    s.percent_at_max = rules.section_double(wh, "PercentAtMax", 1.0);
    s.affects_allies = rules.section_flag(wh, "AffectsAllies", true);   // W5-B R-060
    s.never_use = rules.section_flag(weapon, "NeverUse", false);         // W7-01-01
    s.proj = resolve_projectile_spec(rules, weapon);
    // BulletType AA=/AG= [RE-FALLBACK defaults AA=no, AG=yes: the BulletType
    // loader sits in an unanalysed gap (voxel-render.md §7); these are the
    // ModEnc defaults and every stock AA projectile states AA=yes explicitly].
    const std::string proj = rules.weapon_projectile(weapon);
    s.aa = rules.section_flag(proj, "AA", false);
    s.ag = rules.section_flag(proj, "AG", true);
    s.omni_fire = rules.weapon_omnifire(weapon);
    s.wall = rules.section_flag(wh, "Wall", false);   // G16: WarheadType +0x144
    s.inf_death = om::ftol(rules.section_double(wh, "InfDeath", 0.0));   // W3-B
    s.prone_damage = warhead_prone_damage(rules, wh);
    // W6-04-01 [RE: WeaponTypeClass::LoadFromINI Burst -> +0x9C, default 1]
    s.burst = std::max(1, om::ftol(rules.section_double(weapon, "Burst", 1.0)));
    s.sonic = rules.section_flag(wh, "Sonic", false);   // W6-04-09 (WarheadType +0x14B)
    s.bomb_disarm = rules.section_flag(wh, "BombDisarm", false);   // WarheadType +0x16E (DefuseKit)
    s.wall_flags = warhead_wall_flags(rules, wh);         // W6-04-04
    s.min_range = rules.section_double(weapon, "MinimumRange", 0.0);   // W6-04-22 (+0xB8)
    // W6-04-02: ShrapnelWeapon/ShrapnelCount are BulletType keys (the
    // projectile section), not WeaponType keys.
    const std::string sw = rules.section_string(proj, "ShrapnelWeapon", "");
    const int sc = om::ftol(rules.section_double(proj, "ShrapnelCount", 0.0));
    if (!sw.empty() && sc > 0) {
        s.shrapnel_count = sc;
        s.shrapnel_damage = rules.weapon_damage(sw);
        s.shrapnel_range = rules.weapon_range(sw);
        s.shrapnel_verses = rules.verses(rules.weapon_warhead(sw));
    }
    return s;
}

// W6-04-04 [RE: FUN_00489190 @0x48958B: WallAbsoluteDestroyer +0x145, Wall
// +0x144, Wood +0x147]: the warhead's wall keys as W6WallFlags.
int warhead_wall_flags(const ra2assets::Rules& rules, const std::string& wh) {
    if (wh.empty()) return 0;
    int f = 0;
    if (rules.section_flag(wh, "Wall", false)) f |= kW6Wall;
    if (rules.section_flag(wh, "Wood", false)) f |= kW6Wood;
    if (rules.section_flag(wh, "WallAbsoluteDestroyer", false)) f |= kW6WallAbs;
    return f;
}

// W3-B: ProneDamage= "50%" (or a fraction) -> multiplier; absent -> 1.0.
double warhead_prone_damage(const ra2assets::Rules& rules, const std::string& wh) {
    std::string v = rules.section_string(wh, "ProneDamage", "");
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v = v.substr(0, sc);
    if (v.empty()) return 1.0;
    const double d = std::atof(v.c_str());
    return v.find('%') != std::string::npos ? d / 100.0 : d;
}

std::string type_weapon_id(const ra2assets::Rules& rules, const std::string& type_id, int i,
                           bool elite) {
    const int turret_count = om::ftol(rules.section_double(type_id, "TurretCount", 0.0));
    const int weapon_count = om::ftol(rules.section_double(type_id, "WeaponCount", 0.0));
    const bool numbered_gatling=rules.section_flag(type_id,"IsGattling",false) && i<weapon_count &&
        !rules.section_string(type_id,"Weapon"+std::to_string(i+1),"").empty();
    if (turret_count < 1 && !numbered_gatling) {
        if (i < 0 || i >= 2) return "";
        if (rules.section_flag(type_id, "ClearAllWeapons", false)) return "";
        static const char* kNormal[2] = {"Primary", "Secondary"};
        static const char* kElite[2] = {"ElitePrimary", "EliteSecondary"};
        return rules.section_string(type_id, elite ? kElite[i] : kNormal[i], "");
    }
    if (i >= weapon_count) return "";
    return rules.section_string(type_id, (elite ? "EliteWeapon" : "Weapon") + std::to_string(i + 1),
                                "");
}

TypeWeapons resolve_type_weapons(const ra2assets::Rules& rules, const std::string& type_id) {
    TypeWeapons t;
    for (int i = 0; i < 2; ++i) {
        t.slot[i] = resolve_weapon_slot(rules, type_weapon_id(rules, type_id, i, false));
        t.elite[i] = resolve_weapon_slot(rules, type_weapon_id(rules, type_id, i, true));
    }
    // A multi-turret type (TurretCount >= 1: SREF IsChargeTurret, FV Gunner)
    // fires the weapon of its CURRENT turret (IFV mode / charge stage), not a
    // per-target Primary/Secondary pick -- slot 1 there is "Weapon2" (the FV's
    // RepairBullet), which must never be chosen as a secondary. The IFV swap is
    // P4's step_abilities (ifv_weapons); only slot 0 is the default turret.
    if (om::ftol(rules.section_double(type_id, "TurretCount", 0.0)) >= 1 &&
        !rules.section_flag(type_id,"IsGattling",false)) {
        t.slot[1] = WeaponSlot{};
        t.elite[1] = WeaponSlot{};
    }
    t.deploy_fire_weapon =
        om::ftol(rules.section_double(type_id, "DeployFireWeapon", 1.0));
    return t;
}

static Gattling resolve_gattling(const ra2assets::Rules& rules, const std::string& id) {
    Gattling g;
    if (!rules.section_flag(id, "IsGattling", false)) return g;
    const int stages = std::clamp(om::ftol(rules.section_double(id, "WeaponStages", 0)), 0, 16);
    int last = 0, elite_last = 0;
    for (int i = 0; i < stages; ++i) {
        const std::string n = std::to_string(i+1);
        const int limit = std::clamp(om::ftol(rules.section_double(id, "Stage"+n, last+1)), last+1, 1000000);
        const int elimit = std::clamp(om::ftol(rules.section_double(id, "EliteStage"+n, limit)), elite_last+1, 1000000);
        g.thresholds.push_back(limit); g.elite_thresholds.push_back(elimit);
        last = limit; elite_last = elimit;
        for (int slot = 0; slot < 2; ++slot) {
            g.weapons.push_back(resolve_weapon_slot(rules, type_weapon_id(rules,id,2*i+slot,false)));
            g.elite_weapons.push_back(resolve_weapon_slot(rules, type_weapon_id(rules,id,2*i+slot,true)));
        }
    }
    g.rate_up = std::clamp(om::ftol(rules.section_double(id,"RateUp",1)),0,1000000);
    g.rate_down = std::clamp(om::ftol(rules.section_double(id,"RateDown",50)),0,1000000);
    return g;
}

void fill_unit_combat_extras(const ra2assets::Rules& rules, const std::string& type_id,
                             const TypeWeapons& tw, UnitCombat& c) {
    c.gattling = resolve_gattling(rules, type_id);
    c.primary_aa = tw.slot[0].aa;
    c.primary_ag = tw.slot[0].ag;
    c.secondary = tw.slot[1];
    c.elite_secondary = tw.elite[1];
    for (int k = 0; k < 3; ++k) {
        c.secondary.proj.flh[k] = c.proj.flh[k];
        c.elite_secondary.proj.flh[k] = c.proj.flh[k];
    }
    // DeployFire (G-45) [RE: docs/re/weapons.md §3 -- DeployFireWeapon (def 1)
    // read into type+0x6A8 by FUN_00711510]: the deployed infantry fires that
    // slot through the normal target path. A RadLevel deploy weapon (DESO
    // RadEruptionWeapon) is the P4 eruption ability instead.
    const bool deployer = (rules.section_flag(type_id, "Deployer", false) ||
                           rules.section_flag(type_id, "IsSimpleDeployer", false)) &&
                          rules.section_flag(type_id, "DeployFire", false);
    if (deployer) {
        const int slot = tw.deploy_fire_weapon == 0 ? 0 : 1;
        const WeaponSlot& dw = tw.slot[slot];
        const bool rad = dw.valid && rules.section_double(dw.id, "RadLevel", 0.0) > 0.0;
        if (dw.valid && !rad) {
            c.deploy_fire = true;
            c.deploy_fire_slot = slot;
        }
    }
    c.crusher = rules.section_flag(type_id, "Crusher", false);
    c.auto_crush = rules.section_flag(type_id, "AutoCrush", false);   // W5-B R-050
    c.is_infantry = rules.is_infantry(type_id);                         // W5-B R-061 (sub-cells)
    {   // W5-B R-062: only the Drive {4A582741} and Ship {2BEA74E1} locomotors
        // read the GroundType speed table (FUN_004B2550 / FUN_006A1310); Hover,
        // Walk, Mech, Jumpjet, Teleport, Rocket... do not. Absent -> Drive.
        std::string loco = rules.section_string(type_id, "Locomotor", "");
        for (char& ch : loco) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        c.land_speed_loco = loco.empty() || loco.rfind("{4A582741", 0) == 0 || loco.rfind("{2BEA74E1", 0) == 0;
        // W7 locomotion: the locomotor kind and the Drive/Ship acceleration
        // keys [RE: TechnoType INI FUN_00711510 -- Accelerates +0xDBD (yes),
        // AccelerationFactor +0x308 (.03), DeaccelerationFactor +0x300 (.002),
        // SlowdownDistance +0x2F8 (500 leptons)].
        c.loco.kind = static_cast<int>(loco_kind_from_clsid(loco.c_str(), c.is_infantry));
        c.loco.accelerates = rules.section_flag(type_id, "Accelerates", true);
        c.loco.accel_factor = rules.section_double(type_id, "AccelerationFactor", 0.03);
        c.loco.decel_factor = rules.section_double(type_id, "DeaccelerationFactor", 0.002);
        c.loco.slowdown_distance = static_cast<int>(rules.section_double(type_id, "SlowdownDistance", 500.0));
    }
    // Crushable defaults YES for infantry (Conscripts carry no key and are
    // crushed in the original), NO otherwise [RE-FALLBACK: InfantryTypeClass
    // ctor default not read; rulesmd sets Crushable=yes/no explicitly on the
    // exceptions, e.g. DESO Crushable=no].
    c.crushable = rules.section_flag(type_id, "Crushable", rules.is_infantry(type_id));
    // G16 (agent G) [RE: world-depth.md §A]: InfantryType Occupier (+0xEB4),
    // OccupyWeapon (+0xE04) / EliteOccupyWeapon (+0xE20); primary warhead Wall=.
    c.primary_wall = tw.slot[0].wall;
    c.occupier = rules.section_flag(type_id, "Occupier", false);
    c.occupy_weapon = resolve_weapon_slot(rules, rules.section_string(type_id, "OccupyWeapon", ""));
    c.elite_occupy_weapon =
        resolve_weapon_slot(rules, rules.section_string(type_id, "EliteOccupyWeapon", ""));
    // OccupyWeapon "Defaults to 0 (Primary)" (rulesmd comment; GetWeapon falls
    // back to the occupant's weapon 0 when the slot is empty, FUN_004526E0).
    if (!c.occupy_weapon.valid) c.occupy_weapon = tw.slot[0];
    if (!c.elite_occupy_weapon.valid) c.elite_occupy_weapon = c.occupy_weapon;
    fill_unit_w7(rules, type_id, tw, c.w7);
    if (c.w7.bunkerable) {
        c.occupy_weapon=tw.slot[0];
        c.elite_occupy_weapon=tw.elite[0].valid ? tw.elite[0] : tw.slot[0];
    }
}

// Wave-7 fix (units): the type keys unit_w7.hpp documents, each with the
// TechnoTypeClass::LoadFromINI (FUN_00711510) / WeaponType loader address
// recorded in docs/gap/wave7.
void fill_unit_w7(const ra2assets::Rules& rules, const std::string& type_id, const TypeWeapons& tw,
                  UnitTypeW7& w) {
    w.agent = rules.section_flag(type_id, "Agent", false);
    w.infiltrate = rules.section_flag(type_id, "Infiltrate", false);
    w.engineer = rules.section_flag(type_id, "Engineer", false);
    w.perma_disguise = rules.section_flag(type_id, "CanDisguise", false) &&
                       rules.section_flag(type_id, "PermaDisguise", false);
    w.detect_disguise = rules.section_flag(type_id, "DetectDisguise", false);
    w.selectable_combatant = rules.section_flag(type_id, "IsSelectableCombatant", false);
    w.move_to_shroud = rules.section_flag(type_id, "MoveToShroud", true);
    w.default_to_guard_area = rules.section_flag(type_id, "DefaultToGuardArea", false);
    w.has_turret = rules.section_flag(type_id, "Turret", false);
    w.bunkerable = rules.is_vehicle(type_id) && w.has_turret && rules.section_flag(type_id,"Bunkerable",true);
    w.magnetic = tw.slot[0].valid && rules.section_flag(rules.weapon_warhead(tw.slot[0].id),"IsLocomotor",false);
    if (tw.slot[0].valid)
        w.fake_blink = om::ftol(rules.section_double(tw.slot[0].id, "DisguiseFakeBlinkTime", 0.0));
    w.fake_blink_elite = tw.elite[0].valid
                             ? om::ftol(rules.section_double(tw.elite[0].id, "DisguiseFakeBlinkTime", 0.0))
                             : w.fake_blink;
    // [RE: InfantryClass Mission_Unload 0x51F3F0] an AreaFire deploy weapon.
    {
        const int ds = tw.deploy_fire_weapon == 0 ? 0 : 1;
        if (rules.section_flag(type_id, "Deployer", false) && rules.section_flag(type_id, "DeployFire", false) &&
            tw.slot[ds].valid && rules.section_flag(tw.slot[ds].id, "AreaFire", false))
            w.deploy_area_fire = true;
    }
    w.undeploy_delay = om::ftol(rules.section_double(type_id, "UndeployDelay", -1.0));
    w.build_time_mult = rules.section_double(type_id, "BuildTimeMultiplier", 1.0);   // W7-04-03
    // [RE: CaptureManager ctor 0x4716F0 via 0x6F34A0]: Max = Damage, Infinite
    // = InfiniteMindControl -- only for a primary whose warhead has MindControl.
    if (tw.slot[0].valid &&
        rules.section_flag(rules.section_string(tw.slot[0].id, "Warhead", ""), "MindControl", false)) {
        w.mc_capacity = std::max(1, om::ftol(rules.section_double(tw.slot[0].id, "Damage", 1.0)));
        w.mind_control = true;
        w.mc_infinite = rules.section_flag(tw.slot[0].id, "InfiniteMindControl", false);
        auto integers = [&](const char* key) {
            std::vector<int> result; std::string text=rules.section_string("CombatDamage",key,"");
            for (char& ch : text) if (ch==',') ch=' ';
            std::istringstream stream(text); int value;
            while (stream >> value) result.push_back(value);
            return result;
        };
        w.overload_count=integers("OverloadCount"); w.overload_damage=integers("OverloadDamage");
        w.overload_frames=integers("OverloadFrames");
    }
}

void fill_building_weapons(const ra2assets::Rules& rules, const std::string& type_id,
                           BuildingTypeDef& d) {
    d.gattling = resolve_gattling(rules, type_id);
    const TypeWeapons tw = resolve_type_weapons(rules, type_id);
    d.weapon[0] = tw.slot[0];
    d.weapon[1] = tw.slot[1];
    d.mind_control = tw.slot[0].valid && rules.section_flag(
        rules.section_string(tw.slot[0].id,"Warhead",""),"MindControl",false);
    d.armed = tw.slot[0].valid && tw.slot[0].range > 0.0 && tw.slot[0].damage != 0;
    d.powered = rules.section_flag(type_id, "Powered", false);
    d.rot = rules.rot(type_id);
    d.turret = rules.has_turret(type_id);
    d.guard_range = rules.section_double(type_id, "GuardRange", 0.0);
    // G16 (agent G) [RE: world-depth.md §A1, BuildingTypeClass::LoadFromINI
    // FUN_0045FE40]: the garrison / bridge-hut keys.
    d.tank_bunker = rules.section_flag(type_id,"Bunker",false);
    d.bunker_damage=rules.section_double("CombatDamage","BunkerDamageMultiplier",1.3);
    d.bunker_rof=rules.section_double("CombatDamage","BunkerROFMultiplier",1.3);
    d.bunker_range=rules.section_double("CombatDamage","BunkerWeaponRangeBonus",2.0);
    d.can_be_occupied = d.tank_bunker || rules.section_flag(type_id, "CanBeOccupied", false);
    d.grinding = rules.section_flag(type_id, "Grinding", false);
    d.bio_reactor = type_id == "YAPOWR";
    d.can_occupy_fire = d.tank_bunker || rules.section_flag(type_id, "CanOccupyFire", false);
    d.max_occupants = d.tank_bunker ? 1 : om::ftol(rules.section_double(type_id, "MaxNumberOccupants", 0.0));
    if (d.bio_reactor) {
        d.can_be_occupied = true;
        d.max_occupants = 5;
    }
    d.bridge_repair_hut = rules.section_flag(type_id, "BridgeRepairHut", false);
    d.w6.gate = rules.section_flag(type_id, "Gate", false);   // R-210
    fill_building_abilities(rules, type_id, d);   // wave-5 (agent C): abil_w5_rules.cpp
}

}  // namespace ra2engine

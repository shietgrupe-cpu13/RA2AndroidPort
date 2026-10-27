#include "superweapon.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

namespace ra2engine {

const char* superweapon_type_name(SuperweaponType t) {
    switch (t) {
        case SuperweaponType::None:             return "None";
        case SuperweaponType::Nuke:             return "Nuke";
        case SuperweaponType::IronCurtain:      return "IronCurtain";
        case SuperweaponType::LightningStorm:   return "LightningStorm";
        case SuperweaponType::ChronoSphere:     return "ChronoSphere";
        case SuperweaponType::ChronoWarp:       return "ChronoWarp";
        case SuperweaponType::ParaDrop:         return "ParaDrop";
        case SuperweaponType::AmerParaDrop:     return "AmerParaDrop";
        case SuperweaponType::PsychicDominator: return "Psychic Dominator";
        case SuperweaponType::SpyPlane:         return "SpyPlane";
        case SuperweaponType::GeneticConverter: return "Genetic Mutator";
        case SuperweaponType::ForceShield:      return "ForceShield";
        case SuperweaponType::PsychicReveal:    return "Psychic Sensor";
    }
    return "None";
}

// [RE-GATE APPLIED: superweapon.md §4] the recovered [SuperWeaponTypes] roster:
// 1=NukeSpecial, 2=IronCurtainSpecial, 3=LightningStormSpecial,
// 4=ChronoSphereSpecial, 5=ChronoWarpSpecial, 6=ParaDropSpecial,
// 7=AmericanParaDropSpecial, 8=PsychicDominatorSpecial, 9=SpyPlaneSpecial,
// 10=GeneticConverterSpecial, 11=ForceShieldSpecial, 12=PsychicRevealSpecial.
// id 6 (ParaDrop) and id 7 (AmerParaDrop) are DISTINCT SW kinds (§4 delta); id 9
// is SpyPlane. Only id 1 (Nuke) is active.
SuperweaponType superweapon_type_from_id(int id) {
    switch (id) {
        case 1:  return SuperweaponType::Nuke;
        case 2:  return SuperweaponType::IronCurtain;
        case 3:  return SuperweaponType::LightningStorm;
        case 4:  return SuperweaponType::ChronoSphere;
        case 5:  return SuperweaponType::ChronoWarp;
        case 6:  return SuperweaponType::ParaDrop;           // ParaDropSpecial
        case 7:  return SuperweaponType::AmerParaDrop;       // AmericanParaDropSpecial (DISTINCT, §4)
        case 8:  return SuperweaponType::PsychicDominator;
        case 9:  return SuperweaponType::SpyPlane;           // SpyPlaneSpecial
        case 10: return SuperweaponType::GeneticConverter;
        case 11: return SuperweaponType::ForceShield;
        case 12: return SuperweaponType::PsychicReveal;
        default: return SuperweaponType::None;
    }
}

}  // namespace ra2engine

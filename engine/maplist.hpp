#pragma once
#include <string>
#include <vector>
#include "ra2assets/mix.hpp"
#include "ra2assets/csf.hpp"

namespace ra2engine {

// One selectable skirmish map [RE: maps.md §2/§3].
struct MapEntry {
    std::string file_name;     // "xmp01t4.map"          -- the multimd.mix entry
    std::string base_name;     // "XMP01T4"              -- the pkt index value
    std::string display_name;  // "South Pacific (2-4)"  -- CSF; base_name fallback
    std::string csf_label;     // "DESC:MP01DU"
    std::vector<std::string> game_modes;
    int min_players = 0;
    int max_players = 0;
    std::string theater;       // W5-PRES R-105: [Map] Theater= (upper case), filled by filter_lobby_theaters
    bool addon = false;        // loose map imported into the install's maps/ directory
};

enum class MapCatalogFilter { All, Standard, Addon };
std::vector<int> map_catalog_indices(const std::vector<MapEntry>& entries, MapCatalogFilter filter);
std::vector<int> map_catalog_search_indices(const std::vector<MapEntry>& entries,
                                           MapCatalogFilter filter, const std::string& query);

// Build the skirmish map list. Steps, in EXACTLY this order [RE: maps.md §2]:
//   1. PktFile::parse(pkt_text)                              -- the manifest
//   2. EXISTENCE FILTER: drop entries absent from `maps`     -- MANDATORY:
//      the RA2 pkt lists 148 maps of which 58 are ABSENT from MULTI.MIX, so a
//      pkt-only list offers unloadable maps. (The YR list happens to be
//      complete on this install -- 161/161 present, re-measured at §9.6 -- but
//      the filter is the recovered original behavior and the only
//      partial-install safety net.)
//   3. MODE FILTER: keep has_mode(mode_tag); "" keeps everything. The skirmish
//      list is "standard" -- mpmodesmd.ini's Battle mode, "the only one that
//      allows AI players" (91 of the 161 active maps).
//   4. DISPLAY NAME: Description= is a CSF LABEL, resolved case-insensitively
//      in ra2md.csf. No stock map has [Basic] Name= (0 of 281), so a missing
//      label falls back to base_name.
//   5. Stable sort by display_name (case-insensitive), file_name as tie-break.
// `csf` may be null (then every display_name is the base_name).
std::vector<MapEntry> build_map_catalog(const std::string& pkt_text,
                                        const ra2assets::MixFile& maps,
                                        const ra2assets::CsfStrings* csf,
                                        const std::string& mode_tag = "standard");

// The loaded YR catalog + the archive its maps live in (kept open so the
// caller can read a selected map without re-slurping 31 MB).
struct MapCatalog {
    std::vector<MapEntry>  entries;
    ra2assets::MixFile     maps;      // multimd.mix
    std::string directory;
    std::vector<std::string> rejected_addons;
};

// Open <dir>/multimd.mix + <dir>/langmd.mix, read missionsmd.pkt + ra2md.csf,
// and build the catalog [RE: maps.md §1/§2]. Throws std::runtime_error if an
// archive or the manifest is missing -- callers guard and fall back to the
// engine's default map. NEVER called from load_assets or any gate path
// (maps.md §7 rule 5: no unconditional mix slurps at startup).
MapCatalog load_yr_map_catalog(const std::string& dir, const std::string& mode_tag = "standard");

// W5-PRES R-105 [presentation choice, not original behaviour]: the clone
// draws only the RA2 theaters (TEMPERATE/SNOW/URBAN) with their own art; the
// YR-only DESERT / NEWURBAN / LUNAR maps would render with temperate art, so
// the skirmish lobby hides them. Reads each map's [Map] Theater= from
// `cat.maps`, records it in MapEntry::theater and drops the YR-only ones.
// Returns the number removed. A map whose theater cannot be read is kept.
// Addon maps live in an isolated maps/ directory. Tokens contain a leaf name,
// never an arbitrary path, and remain relative in saves and network manifests.
bool addon_map_token(const std::string& token);
MapEntry inspect_addon_map(const std::string& text,const std::string& token);
std::vector<std::uint8_t> read_addon_map(const std::string& dir,const std::string& token);
std::vector<std::uint8_t> read_catalog_map(const MapCatalog& catalog,const std::string& token);
void append_addon_maps(MapCatalog& catalog,const std::string& dir,const std::string& mode_tag);
std::string map_theater_of(const std::string& map_text);    // "" when absent
bool lobby_theater_supported(const std::string& theater);    // TEMPERATE/SNOW/URBAN (or unknown)
int filter_lobby_theaters(MapCatalog& cat);

}  // namespace ra2engine

#pragma once
// W3-FIX-A: one ordered MIX search list per theater, shared by every art
// loader (buildings, units, voxels, terrain, overlays, smudges, anims,
// campaign extras, palettes).
//
// The original resolves every art file name with ONE global lookup over all
// the MIX archives it has opened, first-opened first (MixFileClass list,
// see docs/re/asset-search.md for the recovered load order). The clone used
// to open 1-3 hand-picked nested mixes per loader, so files living only in
// isogen.mix, <ext>.mix (tem/sno/urb), the RA2 1.006 patch caches
// (expand01/ecache01) or the YR overlays were never found (W3 audit 03 §1.2).
//
// AssetFS reproduces the original's single global lookup. The ORDER is the
// RA2 game.exe load order (RA2 data wins for an RA2 game), followed by the
// YR ra2md.mix archives as a fallback tail [RE-FALLBACK: the RA2 original
// has no YR data; YR-only art -- e.g. the civilian buildings of the default
// lobby map hillbtwn -- is reachable only through this tail].
#include "ra2assets/mix.hpp"
#include "ra2assets/theater_name.hpp"
#include "ra2assets/mod_profile.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ra2assets {

// One archive of the search list: `top` is an install-dir file name; `nested`
// is empty for the top-level archive itself, else a mix inside `top`.
struct AssetMixSpec {
    std::string top;
    std::string nested;
    std::string label() const { return nested.empty() ? top : top + "/" + nested; }
};

// The ordered archive list for a theater (nullptr = TEMPERATE). Pure: it
// does not touch the disk. `patch_tops` are the top-level patch archives the
// install has (expand01.mix, ecache01.mix, ...), already in load order.
std::vector<AssetMixSpec> asset_search_order(const TheaterChain* chain,
                                             const std::vector<std::string>& patch_tops);

class AssetFS {
public:
    // Opens every archive of asset_search_order() that exists under
    // `install_dir` (which must end with a path separator). Missing archives
    // are skipped silently; nothing throws.
    AssetFS(const std::string& install_dir, const TheaterChain* chain,
            std::shared_ptr<const ModProfile> mod = nullptr);

    // First archive (in search order) holding `name` (case-insensitive).
    // Returns nullptr when absent everywhere; `label` gets the archive label.
    // `ra2_only` stops before the YR tail (the RA2 game.exe archives only).
    // `prefer_yr` searches that tail first, with classic fallback; ignored
    // when ra2_only is set. Opt in only for expansion artwork overrides.
    const MixEntry* find(const std::string& name, const MixFile** owner = nullptr,
                         std::string* label = nullptr, bool ra2_only = false,
                         bool prefer_yr = false) const;
    bool has(const std::string& name) const { return find(name) != nullptr; }
    // Reads `name`; returns false (out untouched) when absent.
    bool read(const std::string& name, std::vector<uint8_t>& out) const;
    // The archive label holding `name`, or "" when absent (diagnostics).
    std::string where(const std::string& name) const;

    const TheaterChain& chain() const { return *chain_; }
    // Opened archives, in search order.
    const std::vector<std::pair<std::string, std::shared_ptr<MixFile>>>& mixes() const { return mixes_; }

private:
    std::shared_ptr<const ModProfile> mod_;
    const TheaterChain* chain_ = nullptr;
    std::vector<std::pair<std::string, std::shared_ptr<MixFile>>> mixes_;
    std::size_t yr_begin_ = 0;   // index of the first YR (ra2md.mix) archive
};

// The candidate BASE NAMES (no extension, lower case) the original tries for
// one art image, in order [RE: building-render.md §1, FUN_005F8860/
// FUN_005F8EA0/FUN_005F8F00]: the NewTheater name pattern {g,n,c,y}{a,t}
// gives [themed (name[1] = theater letter), generic (name[1] = 'g')]; any
// other name is used literally. Same names, same order as
// theater_candidates() -- only the archive walk differs.
std::vector<std::string> art_base_names(const std::string& image, char theater_letter);

// First hit of `bases` x `exts` (ext-major: every base with exts[0], then
// every base with exts[1], ...) over the global search list. Returns the
// file name found ("" when none) and fills `data`.
std::string find_art(const AssetFS& fs, const std::vector<std::string>& bases,
                     const std::vector<std::string>& exts, std::vector<uint8_t>* data = nullptr,
                     bool ra2_only = false);

}  // namespace ra2assets

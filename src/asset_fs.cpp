#include "ra2assets/asset_fs.hpp"
#include "ra2assets/asset_trace.hpp"

#include <cctype>
#include <cstdio>

namespace ra2assets {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

// [RE: docs/re/asset-search.md] RA2 game.exe MixFileClass list order (the
// ctor FUN_005952f0 appends at the tail; MixFileClass::Offset FUN_00595aa0
// walks head -> tail, first hit wins):
//   Init_Bootstrap_Mixfiles FUN_00515c90: EXPAND99..EXPAND00, RA2, CACHE, LOCAL
//   FUN_00515af0:                        ECACHE*.MIX, ELOCAL*.MIX
//   Init_Secondary_Mixfiles FUN_00515e70: CONQUER, CAMEO, MAPS*, MULTI, THEME, MOVIES
//   Init_Theater FUN_00519e70:           GENERIC (not loaded for SNOW),
//                                        <THEATER>.MIX, <EXT>.MIX, ISO<..>.MIX, ISOGEN
//   FUN_0051a2b0 (scenario start):       NEUTRAL
// then the YR tail [RE-FALLBACK], in gamemd's relative order (FUN_005301a0 +
// its Init_Theater "%sMD.MIX"): md patches, CACHEMD, LOCALMD, CONQMD, GENERMD,
// ISOGENMD, CAMEOMD, then the theater md overlays.
std::vector<AssetMixSpec> asset_search_order(const TheaterChain* chain,
                                             const std::vector<std::string>& patch_tops) {
    const TheaterChain* c = chain ? chain : theater_chain("TEMPERATE");
    // The YR-only theaters (DESERT/NEWURBAN/LUNAR) draw with the temperate
    // art (main.cpp art_chain(), W2-GA-09); mirror that here.
    if (c->yr_only) c = theater_chain("TEMPERATE");
    const bool snow = std::string(c->name) == "SNOW";
    std::vector<AssetMixSpec> out;
    auto top = [&](const std::string& t) { out.push_back({t, ""}); };
    auto ra2 = [&](const std::string& n) { out.push_back({"ra2.mix", n}); };
    auto md = [&](const std::string& n) { out.push_back({"ra2md.mix", n}); };
    // --- RA2 game.exe order ---
    for (const std::string& p : patch_tops)
        if (lower(p).rfind("expand", 0) == 0) top(p);
    top("ra2.mix");
    ra2("cache.mix");
    ra2("local.mix");
    for (const std::string& p : patch_tops)
        if (lower(p).rfind("ecache", 0) == 0 || lower(p).rfind("elocal", 0) == 0) top(p);
    ra2("conquer.mix");
    if (!snow) ra2("generic.mix");
    ra2(c->art_mix);                                   // TEMPERAT / SNOW / URBAN
    ra2(std::string(c->ext) + ".mix");                 // TEM / SNO / URB
    ra2(c->iso_mix);                                   // ISOTEMP / ISOSNOW / ISOURB
    ra2("isogen.mix");
    ra2("neutral.mix");
    out.push_back({"language.mix", "cameo.mix"});
    out.push_back({"cameo.mix", ""});
    // [RE-FALLBACK] SNOW: Init_Theater never opens GENERIC.MIX for a snow
    // scenario that is the session's first (prev theater -1); a session that
    // played a non-snow map first still has it open. The clone keeps it as
    // the last RA2 archive so a 'g'-only name cannot vanish on snow.
    if (snow) ra2("generic.mix");
    // --- YR tail [RE-FALLBACK: YR-only art, e.g. hillbtwn civilians] ---
    top("ra2md.mix");
    for (const char* n : {"expandmd01.mix", "expandmd02.mix", "expandmd03.mix", "expandmd04.mix",
                          "expandmd05.mix", "expandmd99.mix"})
        md(n);
    md("cachemd.mix");
    md("localmd.mix");
    md("conqmd.mix");
    md("genermd.mix");
    md("isogenmd.mix");
    md("cameomd.mix");
    out.push_back({"langmd.mix", "cameomd.mix"});
    out.push_back({"language.mix", "cameomd.mix"});
    out.push_back({"cameomd.mix", ""});
    // Theater md overlays: "%sMD.MIX" over the theater names.
    const std::string an = lower(c->art_mix);          // "temperat.mix"
    md(an.substr(0, an.size() - 4) + "md.mix");        // temperatmd.mix / snowmd.mix / urbanmd.mix
    const std::string in = lower(c->iso_pal);          // "isotem.pal" -> isotemmd.mix
    md(in.substr(0, in.size() - 4) + "md.mix");
    return out;
}

AssetFS::AssetFS(const std::string& install_dir, const TheaterChain* chain, std::shared_ptr<const ModProfile> mod)
    : mod_(std::move(mod)), chain_(chain ? chain : theater_chain("TEMPERATE")) {
    if (chain_->yr_only) chain_ = theater_chain("TEMPERATE");
    // Top-level patch archives present on the install, in the original's
    // order: EXPAND99 .. EXPAND00 (descending), then ECACHE*/ELOCAL*.
    std::vector<std::string> patches;
    auto exists = [&](const std::string& f) {
        if (std::FILE* fp = std::fopen((install_dir + f).c_str(), "rb")) { std::fclose(fp); return true; }
        return false;
    };
    char buf[32];
    for (int i = 99; i >= 0; --i) {
        std::snprintf(buf, sizeof buf, "expand%02d.mix", i);
        if (exists(buf)) patches.push_back(buf);
    }
    for (int i = 0; i <= 99; ++i) {
        std::snprintf(buf, sizeof buf, "ecache%02d.mix", i);
        if (exists(buf)) patches.push_back(buf);
    }
    for (int i = 0; i <= 99; ++i) {
        std::snprintf(buf, sizeof buf, "elocal%02d.mix", i);
        if (exists(buf)) patches.push_back(buf);
    }
    std::vector<std::pair<std::string, std::shared_ptr<MixFile>>> tops;
    auto get_top = [&](const std::string& t) -> std::shared_ptr<MixFile> {
        for (auto& p : tops)
            if (p.first == t) return p.second;
        std::shared_ptr<MixFile> m;
        try { m = std::make_shared<MixFile>(MixFile::open_path(install_dir + t)); } catch (...) {}
        tops.push_back({t, m});
        return m;
    };
    yr_begin_ = static_cast<std::size_t>(-1);
    for (const AssetMixSpec& s : asset_search_order(chain_, patches)) {
        if (s.top == "ra2md.mix" && yr_begin_ == static_cast<std::size_t>(-1)) yr_begin_ = mixes_.size();
        std::shared_ptr<MixFile> t = get_top(s.top);
        if (!t) continue;
        if (s.nested.empty()) { mixes_.push_back({s.label(), t}); continue; }
        if (!t->find(s.nested)) continue;
        try {
            mixes_.push_back({s.label(), std::make_shared<MixFile>(t->open_nested(s.nested))});
        } catch (...) {}
    }
    if (yr_begin_ == static_cast<std::size_t>(-1)) yr_begin_ = mixes_.size();
}

const MixEntry* AssetFS::find(const std::string& name, const MixFile** owner, std::string* label,
                              bool ra2_only, bool prefer_yr) const {
    if (name.empty()) return nullptr;
    if (mod_) if (const auto* e=mod_->find(name,owner,label)) return e;
    const uint32_t id = mix_id(name);
    const std::size_t end = ra2_only ? yr_begin_ : mixes_.size();
    // Opt-in YR override: retain relative archive order in each group and
    // fall back to classic assets when the expansion has no replacement.
    for (std::size_t n = 0; n < end; ++n) {
        const std::size_t k = prefer_yr && !ra2_only
            ? (n < end - yr_begin_ ? yr_begin_ + n : n - (end - yr_begin_)) : n;
        const auto& m = mixes_[k];
        if (const MixEntry* e = m.second->find(id)) {
            if (owner) *owner = m.second.get();
            if (label) *label = m.first;
            trace_asset("resolve",name,m.first,e->size);
            return e;
        }
    }
    return nullptr;
}

bool AssetFS::read(const std::string& name, std::vector<uint8_t>& out) const {
    const MixFile* m = nullptr;
    const MixEntry* e = find(name, &m);
    if (!e) return false;
    out = m->read(*e);
    return true;
}

std::string AssetFS::where(const std::string& name) const {
    std::string l;
    return find(name, nullptr, &l) ? l : std::string();
}

std::vector<std::string> art_base_names(const std::string& image, char theater_letter) {
    std::vector<std::string> out;
    if (image.empty()) return out;
    const std::string base = lower(image);
    const bool munge = base.size() >= 2 &&
                       (base[0] == 'g' || base[0] == 'n' || base[0] == 'c' || base[0] == 'y') &&
                       (base[1] == 'a' || base[1] == 't');
    if (munge) {
        std::string themed = base;
        themed[1] = static_cast<char>(std::tolower(static_cast<unsigned char>(theater_letter)));
        std::string generic = base;
        generic[1] = 'g';
        out.push_back(themed);
        if (generic != themed) out.push_back(generic);
    } else {
        out.push_back(base);
    }
    return out;
}

std::string find_art(const AssetFS& fs, const std::vector<std::string>& bases,
                     const std::vector<std::string>& exts, std::vector<uint8_t>* data, bool ra2_only) {
    for (const std::string& ext : exts)
        for (const std::string& b : bases) {
            const std::string f = b + ext;
            const MixFile* m = nullptr;
            if (const MixEntry* e = fs.find(f, &m, nullptr, ra2_only)) {
                if (data) *data = m->read(*e);
                return f;
            }
        }
    return std::string();
}

}  // namespace ra2assets

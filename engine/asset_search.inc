// asset_search.inc -- W3-FIX-A: every engine art loader resolves files through
// ONE ordered MIX search list per theater (ra2assets::AssetFS, the original's
// global MixFileClass lookup -- docs/re/asset-search.md). Included by main.cpp
// right after art_chain(); the list follows art_chain() (the map's theater in
// the real game, TEMPERATE in every gate).

// (ra2assets/asset_fs.hpp is included at the top of main.cpp)

// The search list for the current art_chain(), opened once per theater.
inline const ra2assets::AssetFS& asset_fs() {
    static std::map<std::string, std::unique_ptr<ra2assets::AssetFS>> cache;
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    const ra2assets::TheaterChain& ch = art_chain();
    auto it = cache.find(ch.name);
    if (it == cache.end())
        it = cache.emplace(ch.name, std::make_unique<ra2assets::AssetFS>(env_install_dir(), &ch, ra2assets::active_mod())).first;
    return *it->second;
}

// Reads `file` from the global list; false when absent everywhere.
inline bool asset_read(const std::string& file, std::vector<uint8_t>& out) {
    return asset_fs().read(file, out);
}

// Required theater palettes use the same patch/base/Yuri search as artwork.
// Expansion theaters keep their unit palettes in cachemd.mix, not cache.mix.
inline ra2assets::Palette asset_required_palette(const std::string& name) {
    std::vector<uint8_t> raw;
    if (!asset_read(name, raw))
        throw std::runtime_error("Required palette " + name +
            " not found in imported base, patch or Yuri game archives");
    return ra2assets::Palette::load(raw.data(), raw.size());
}

// Decodes the first SHP found for `bases` x `exts` (ext-major). Returns the
// file name ("" when none) and fills `out`.
inline std::string asset_find_shp(const std::vector<std::string>& bases, const std::vector<std::string>& exts,
                                  ra2assets::ShpFile& out, bool ra2_only = false) {
    std::vector<uint8_t> raw;
    const std::string f = ra2assets::find_art(asset_fs(), bases, exts, &raw, ra2_only);
    if (f.empty()) return f;
    try {
        out = ra2assets::ShpFile::load(raw);
    } catch (const std::exception&) {
        return std::string();
    }
    return f;
}

// A building / NewTheater image: [themed, 'g'] (or literal) + ".shp".
// `ra2_only` (the playable roster): RA2 archives only -- a YR-only roster
// building (NABNKR, NAINDP) stays art-absent and therefore unbuildable, as
// before; map objects and dressing take the YR tail too.
inline bool asset_building_shp(const std::string& image, ra2assets::ShpFile& out, bool ra2_only = false) {
    auto bases = ra2assets::art_base_names(image, art_chain().letter);
    std::string literal = image;
    for (char& c : literal) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (std::find(bases.begin(),bases.end(),literal) == bases.end()) bases.push_back(literal);
    // GAFWLL was an unused French-wall placeholder in RA2; YR replaces it
    // with Yuri's Citadel wall under the same generic filename. This explicit
    // override must precede the classic-only roster probe as well.
    if(literal=="gafwll")for(const auto& base:bases) {
        const ra2assets::MixFile* owner=nullptr;std::string where;
        const auto* e=asset_fs().find(base+".shp",&owner,&where,false,true);
        // Try every YR candidate before accepting a classic theater fallback.
        if(e && where.rfind("ra2md.mix",0)==0) {
            try {out=ra2assets::ShpFile::load(owner->read(*e));return true;}
            catch(const std::exception&) {}
        }
    }
    return !asset_find_shp(bases, {".shp"}, out, ra2_only).empty();
}

// Map-dressing / anim art: `Theater=yes` names take the theater extension
// first; then the NewTheater names as .shp; then the literal name as .shp.
inline std::string asset_art_shp(const std::string& image, bool theater_ext, ra2assets::ShpFile& out) {
    std::vector<std::string> bases = ra2assets::art_base_names(image, art_chain().letter);
    std::string lit = image;
    for (char& c : lit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (std::find(bases.begin(), bases.end(), lit) == bases.end()) bases.push_back(lit);
    std::vector<std::string> exts;
    if (theater_ext) exts.push_back(std::string(".") + art_chain().ext);
    exts.push_back(".shp");
    return asset_find_shp(bases, exts, out);
}

// Art `Palette=<name>` [RE: asset-search.md §2, FUN_00604540]: the file is
// <name><theater ext>.pal ("lib" -> "liburb.pal"). nullptr when absent.
inline std::shared_ptr<ra2assets::Palette> asset_art_palette(const std::string& name) {
    if (name.empty()) return nullptr;
    std::string f = name;
    for (char& c : f) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    f += std::string(art_chain().ext) + ".pal";
    std::vector<uint8_t> raw;
    if (!asset_read(f, raw)) return nullptr;
    try {
        return std::make_shared<ra2assets::Palette>(ra2assets::Palette::load(raw.data(), raw.size()));
    } catch (const std::exception&) {
        return nullptr;
    }
}

// Missing legacy cameos may live in patch or Yuri archives. Keep the legacy
// hit unchanged, then use the shared ordered search for absent icons.
inline bool asset_cameo_fallback(const std::string& name, ra2assets::ShpFile& out) {
    std::string file = name;
    for (char& c : file) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (file.size() < 4 || file.substr(file.size()-4) != ".shp") file += ".shp";
    std::vector<uint8_t> raw;
    if (!asset_read(file, raw)) {
        std::cerr << "[cameo] missing " << file << " in imported archives\n";
        return false;
    }
    try {
        out = ra2assets::ShpFile::load(raw);
        std::cerr << "[cameo] fallback " << file << " from " << asset_fs().where(file) << "\n";
        return out.count() > 0;
    } catch (const std::exception& ex) {
        std::cerr << "[cameo] invalid " << file << ": " << ex.what() << "\n";
        return false;
    }
}

#include "maplist.hpp"
#include "ra2assets/pkt.hpp"
#include "ra2assets/fileio.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <cstdio>
#include "ra2assets/ini.hpp"
#include "ra2assets/map.hpp"

namespace ra2engine {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// CSF values are UTF-16. Every stock map name is ASCII; narrow to char and
// replace any non-ASCII code unit with '?' rather than mangling the string.
std::string narrow(const std::u16string& u) {
    std::string out;
    out.reserve(u.size());
    for (char16_t c : u) out.push_back(c < 0x80 ? (char)c : '?');
    return out;
}

}  // namespace

std::vector<MapEntry> build_map_catalog(const std::string& pkt_text,
                                        const ra2assets::MixFile& maps,
                                        const ra2assets::CsfStrings* csf,
                                        const std::string& mode_tag) {
    std::vector<MapEntry> out;
    const ra2assets::PktFile pkt = ra2assets::PktFile::parse(pkt_text);   // step 1
    for (const ra2assets::PktMapEntry& p : pkt.entries()) {
        if (maps.find(p.file_name) == nullptr) continue;                  // step 2
        if (!mode_tag.empty() && !p.has_mode(mode_tag)) continue;         // step 3
        MapEntry e;
        e.file_name   = p.file_name;
        e.base_name   = p.base_name;
        e.csf_label   = p.description;
        e.game_modes  = p.game_modes;
        e.min_players = p.min_players;
        e.max_players = p.max_players;
        e.display_name = p.base_name;                                     // step 4
        if (csf && !p.description.empty()) {
            if (const std::u16string* v = csf->find(p.description)) {
                const std::string n = narrow(*v);
                if (!n.empty()) e.display_name = n;
            }
        }
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(),                              // step 5
                     [](const MapEntry& a, const MapEntry& b) {
                         const std::string la = lower(a.display_name), lb = lower(b.display_name);
                         if (la != lb) return la < lb;
                         return a.file_name < b.file_name;
                     });
    return out;
}

MapCatalog load_yr_map_catalog(const std::string& dir, const std::string& mode_tag) {
    // maps.md §1: the YR multiplayer maps live in multimd.mix; the manifest
    // (missionsmd.pkt) and the string table (ra2md.csf) live in langmd.mix.
    ra2assets::MixFile maps = ra2assets::MixFile::open_path(dir + "multimd.mix");
    ra2assets::MixFile lang = ra2assets::MixFile::open_path(dir + "langmd.mix");

    const ra2assets::MixEntry* pe = lang.find(std::string("missionsmd.pkt"));
    if (!pe) throw std::runtime_error("missionsmd.pkt not found in langmd.mix");
    const auto praw = lang.read(*pe);
    const std::string pkt_text(praw.begin(), praw.end());

    // CSF is OPTIONAL: without it every display_name falls back to the pkt
    // base name, which still yields a usable (if ugly) list.
    ra2assets::CsfStrings csf;
    bool have_csf = false;
    if (const ra2assets::MixEntry* ce = lang.find(std::string("ra2md.csf"))) {
        try {
            const auto craw = lang.read(*ce);
            csf = ra2assets::CsfStrings::load(craw.data(), craw.size());
            have_csf = true;
        } catch (const std::exception&) { have_csf = false; }
    }

    MapCatalog cat{build_map_catalog(pkt_text, maps, have_csf ? &csf : nullptr, mode_tag),
                   std::move(maps)};
    cat.directory=dir;
    append_addon_maps(cat,dir,mode_tag);
    return cat;
}

std::string map_theater_of(const std::string& text) {
    // The [Map] section's Theater= key (INI rules: case-insensitive section and
    // key, ';' comments, surrounding blanks ignored).
    bool in_map = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        const std::size_t sc = line.find(';');
        if (sc != std::string::npos) line.resize(sc);
        auto trim = [](std::string v) {
            const char* ws = " \t\r";
            const std::size_t a = v.find_first_not_of(ws);
            if (a == std::string::npos) return std::string();
            return v.substr(a, v.find_last_not_of(ws) - a + 1);
        };
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '[') {
            in_map = lower(line) == "[map]";
            continue;
        }
        if (!in_map) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (lower(trim(line.substr(0, eq))) != "theater") continue;
        std::string v = trim(line.substr(eq + 1));
        for (char& c : v) c = (char)std::toupper((unsigned char)c);
        return v;
    }
    return std::string();
}

bool lobby_theater_supported(const std::string& t) {
    return t != "DESERT" && t != "NEWURBAN" && t != "LUNAR";
}

int filter_lobby_theaters(MapCatalog& cat) {
    int removed = 0;
    std::vector<MapEntry> keep;
    keep.reserve(cat.entries.size());
    for (MapEntry& e : cat.entries) {
        try {
            const auto raw=read_catalog_map(cat,e.file_name);
            e.theater=map_theater_of(std::string(raw.begin(),raw.end()));
        } catch(const std::exception&) {++removed;continue;}
        if (!lobby_theater_supported(e.theater)) { ++removed; continue; }
        keep.push_back(std::move(e));
    }
    cat.entries.swap(keep);
    return removed;
}


bool addon_map_token(const std::string& token) {
    if(token.rfind("maps/",0)!=0 || token.size()>205)return false;
    const auto leaf=token.substr(5);
    if(leaf.empty() || leaf.front()=='.' || leaf.find_first_of("/\\:")!=std::string::npos)return false;
    for(unsigned char c:leaf)if(c<32 || c==127)return false;
    const auto dot=leaf.find_last_of('.');
    const auto ext=dot==std::string::npos?"":lower(leaf.substr(dot));
    return ext==".map" || ext==".yrm" || ext==".mpr";
}
MapEntry inspect_addon_map(const std::string& text,const std::string& token) {
    if(!addon_map_token(token) || text.size()>16*1024*1024)throw std::runtime_error("invalid map name or size");
    const auto ini=ra2assets::Ini::parse(text);
    if(!ini.has("Map") || ini.section("IsoMapPack5").empty())throw std::runtime_error("missing packed terrain");
    // Plain maps and supported INI overrides use the existing rules parser.
    // Scripted missions need CampaignRuntime, which skirmish does not run.
    for(const char* section:{"TRIGGERS","EVENTS","ACTIONS","TAGS","CELLTAGS",
        "VARIABLENAMES","TASKFORCES","SCRIPTTYPES","TEAMTYPES","AITRIGGERTYPES","TUBES"})
        if(!ini.section(section).empty())throw std::runtime_error(std::string("requires unsupported map scripting: ")+section);
    for(const auto& flag:ini.section("AITriggerTypesEnable"))
        if(lower(flag.second)!="yes" && flag.second!="1" && lower(flag.second)!="true")
            throw std::runtime_error("requires map-specific AI trigger filtering");
    int x=0,y=0,w=0,h=0;
    if(std::sscanf(ini.get("Map","Size").c_str(),"%d,%d,%d,%d",&x,&y,&w,&h)!=4 ||
       x!=0 || y!=0 || w<1 || h<1 || w+h>512)throw std::runtime_error("invalid map dimensions");
    const auto theater=map_theater_of(text);
    if(theater!="TEMPERATE" && theater!="SNOW" && theater!="URBAN")
        throw std::runtime_error("unsupported theater "+theater);
    const auto map=ra2assets::MapFile::parse(text);
    const auto starts=map.start_waypoints();
    if(starts.size()<2 || starts.size()>8 || map.cells().empty())throw std::runtime_error("needs 2-8 valid starts and terrain");
    for(std::size_t i=0;i<starts.size();++i)
        if(starts[i].index!=static_cast<int>(i) || !map.at(starts[i].x,starts[i].y))
            throw std::runtime_error("invalid or noncontiguous player starts");
    MapEntry e;e.file_name=token;e.base_name=token.substr(5);e.theater=theater;
    e.display_name=ini.get("Basic","Name",e.base_name);e.min_players=2;e.max_players=static_cast<int>(starts.size());
    std::string modes=lower(ini.get("Basic","GameMode","standard"));std::istringstream stream(modes);std::string mode;
    while(std::getline(stream,mode,',')) {
        const auto a=mode.find_first_not_of(" \t"),b=mode.find_last_not_of(" \t");
        if(a!=std::string::npos)e.game_modes.push_back(mode.substr(a,b-a+1));
    }
    return e;
}
std::vector<std::uint8_t> read_addon_map(const std::string& dir,const std::string& token) {
    if(!addon_map_token(token))throw std::runtime_error("invalid addon map token");
    const auto path=std::filesystem::path(dir)/token;
    if(std::filesystem::is_symlink(path.parent_path()) || std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path) ||
       std::filesystem::file_size(path)>16*1024*1024)throw std::runtime_error("invalid addon map file");
    auto bytes=ra2assets::read_file(path.string());
    (void)inspect_addon_map(std::string(bytes.begin(),bytes.end()),token);
    return bytes;
}
std::vector<std::uint8_t> read_catalog_map(const MapCatalog& cat,const std::string& token) {
    if(addon_map_token(token))return read_addon_map(cat.directory,token);
    const auto* e=cat.maps.find(token);
    if(!e)throw std::runtime_error("map not present in archive");
    return cat.maps.read(*e);
}
void append_addon_maps(MapCatalog& cat,const std::string& dir,const std::string& mode_tag) {
    cat.directory=dir;const auto path=std::filesystem::path(dir)/"maps";
    std::error_code ec;if(!std::filesystem::is_directory(path,ec))return;
    if(std::filesystem::is_symlink(path,ec))return;
    std::vector<std::string> tokens;
    for(std::filesystem::directory_iterator it(path,ec),end;!ec && it!=end;it.increment(ec)) {
        const auto token="maps/"+it->path().filename().string();
        if(addon_map_token(token))tokens.push_back(token);
    }
    std::sort(tokens.begin(),tokens.end());
    for(const auto& token:tokens)try {
        const auto bytes=read_addon_map(dir,token);
        auto entry=inspect_addon_map(std::string(bytes.begin(),bytes.end()),token);
        if(!mode_tag.empty() && std::find(entry.game_modes.begin(),entry.game_modes.end(),mode_tag)==entry.game_modes.end())continue;
        cat.entries.push_back(std::move(entry));
    } catch(const std::exception& ex) {cat.rejected_addons.push_back(token+": "+ex.what());}
    std::stable_sort(cat.entries.begin(),cat.entries.end(),[](const MapEntry& a,const MapEntry& b) {
        const auto an=lower(a.display_name),bn=lower(b.display_name);
        return an==bn ? a.file_name<b.file_name:an<bn;
    });
}
}  // namespace ra2engine

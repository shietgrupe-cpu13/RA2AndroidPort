#include "ra2assets/mod_profile.hpp"
#include "ra2assets/ini.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ra2assets {
namespace {
namespace fs = std::filesystem;
std::shared_ptr<const ModProfile> current;
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool safe_leaf(const std::string& s) {
    return !s.empty() && s != "." && s != ".." && s.find_first_of("/\\:") == std::string::npos;
}
std::string text(const std::vector<uint8_t>& b) { return std::string(b.begin(), b.end()); }
void hash_bytes(uint64_t& h, const char* p, std::size_t n) {
    for (std::size_t i=0;i<n;++i) { h ^= static_cast<unsigned char>(p[i]); h *= 0x100000001b3ull; }
}
const std::set<std::string> known_keys = {
    "COST","STRENGTH","SPEED","SIGHT","POINTS","BUILDTIMEMULTIPLIER","SOYLENT",
    "AMMO","RELOAD","ROT","GUARDRANGE","NAME","UINAME","IMAGE","CAMEO","ALTCAMEO",
    "DAMAGE","ROF","RANGE","MINIMUMRANGE","BRIGHT","REPORT","ANIM","ELITE",
    "ARMOR","PRIMARY","SECONDARY","ELITEPRIMARY","ELITESECONDARY","OWNER","TECHLEVEL",
    "PREREQUISITE","SOUND","VOLUME","PRIORITY"
};
const std::set<std::string> config_names = {"rulesmd.ini","artmd.ini","aimd.ini","soundmd.ini","evamd.ini"};
}
bool safe_mod_id(const std::string& s) {
    if (s.empty() || s.size()>48 || !std::isalnum(static_cast<unsigned char>(s[0]))) return false;
    for (unsigned char c : s) if (!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-' || c=='_')) return false;
    return true;
}
bool ModProfile::can_activate() const {
    return std::none_of(findings.begin(),findings.end(),[](const ModFinding& f){return f.blocked;});
}
std::string ModProfile::status() const {
    return !can_activate()?"BLOCKED":findings.empty()?"SUPPORTED":"PARTIAL";
}
std::string ModProfile::report() const {
    std::ostringstream o;
    o << status() << '\n' << "Name: " << name << "\nID: " << id << "\nContent: " << fingerprint
      << "\nFiles: " << files_.size() << "\nArchives: " << archives_.size() << '\n';
    for (const auto& f:findings) o << (f.blocked?"BLOCKED ":"NOTICE ") << f.code << ": " << f.detail << '\n';
    o << "\nData overlays only. This report is not gameplay certification. Windows programs are never executed.\n";
    return o.str();
}
std::shared_ptr<ModProfile> ModProfile::inspect(const std::string& directory) {
    auto p=std::make_shared<ModProfile>();
    p->name=fs::u8path(directory).filename().u8string();
    auto issue=[&](bool blocked,const std::string& code,const std::string& detail) {
        if (p->findings.size()<128) p->findings.push_back({blocked,code,detail});
        else if (blocked && p->can_activate()) p->findings.push_back({true,"too-many-findings","Additional blocking requirements found"});
    };
    try {
        const auto root=fs::u8path(directory);
        if (fs::is_symlink(root) || !fs::is_directory(root)) throw std::runtime_error("Mod folder is missing or is a symbolic link");
        p->root=fs::canonical(root).u8string();
        std::size_t count=0;
        for (fs::recursive_directory_iterator it(root),end;it!=end;++it) {
            if (++count>16384) throw std::runtime_error("Mod folder exceeds the 16384-entry inspection limit");
            if (it.depth()>12) throw std::runtime_error("Mod folder is too deeply nested");
            if (it->is_symlink()) { issue(true,"linked-path",fs::relative(it->path(),root).u8string());it.disable_recursion_pending();continue; }
            if (!it->is_regular_file()) continue;
            const auto rel=lower(it->path().lexically_relative(root).generic_u8string());
            if (!p->files_.emplace(rel,it->path().u8string()).second) issue(true,"ambiguous-name",rel);
            const auto ext=lower(it->path().extension().u8string());
            const auto leaf=lower(it->path().filename().u8string());
            if (ext==".dll" || ext==".exe" || ext==".bat" || ext==".cmd" || ext==".ps1" || leaf=="syringe.exe")
                issue(true,"windows-extension",rel+" is a Windows executable or extension; it cannot be loaded by the native port");
        }
        // A manifest is mandatory for activation, but unprepared folders can still be audited.
        auto mf=p->files_.find("mod.ini");
        if (mf==p->files_.end()) issue(true,"missing-manifest","Add mod.ini with [Mod] Id, Name and Requires=base after reviewing the report");
        else {
            if (fs::file_size(fs::u8path(mf->second))>65536) throw std::runtime_error("mod.ini exceeds 64 KiB");
            std::ifstream f(fs::u8path(mf->second)); std::ostringstream s;s<<f.rdbuf();
            const auto manifest=Ini::parse(s.str());
            p->id=manifest.get("Mod","Id");p->name=manifest.get("Mod","Name",p->name);
            if (!safe_mod_id(p->id)) issue(true,"invalid-id","Id must contain lowercase letters, digits, hyphens or underscores (max 48)");
            if (lower(manifest.get("Mod","Requires"))!="base") issue(true,"engine-requirement","Only Requires=base is supported; Ares/Phobos behavior must be implemented separately");
        }
        // Flat root archives have explicit descending name priority. Base archives are never modified.
        for (auto it=p->files_.rbegin();it!=p->files_.rend();++it) {
            const auto rel=it->first;
            if (rel.find('/')!=std::string::npos || fs::u8path(rel).extension()!=".mix") continue;
            try {
                auto mix=std::make_shared<MixFile>(MixFile::open_path(it->second));
                p->archives_.push_back({rel,mix});
                for (const char* child:{"localmd.mix","cachemd.mix","conqmd.mix","cameomd.mix"})
                    if (mix->find(child)) p->archives_.push_back({rel+"/"+child,std::make_shared<MixFile>(mix->open_nested(child))});
            } catch (const std::exception& e) {issue(true,"invalid-archive",rel+": "+e.what());}
        }
        for (const auto& file:p->files_) {
            const auto rel=file.first;const auto ext=fs::u8path(rel).extension().u8string();
            const bool asset=rel.find('/')==std::string::npos || rel.rfind("assets/",0)==0 || rel.rfind("ini/",0)==0;
            if (!asset || ext==".mix" || rel=="mod.ini") continue;
            const auto leaf=lower(fs::u8path(rel).filename().u8string());
            if (ext==".ini" || ext==".shp" || ext==".vxl" || ext==".hva" || ext==".pal" ||
                ext==".tem" || ext==".sno" || ext==".urb") {
                if (rel.find('/')!=std::string::npos && rel.substr(rel.find('/')+1).find('/')!=std::string::npos) continue;
                if (fs::file_size(fs::u8path(file.second))>64*1024*1024) {issue(true,"oversize-asset",rel+" exceeds 64 MiB");continue;}
                if (!p->loose_files_.emplace(leaf,file.second).second)
                    issue(true,"ambiguous-asset",leaf+" is supplied in more than one loose folder");
            }
            if (ext==".bag" || ext==".idx" || ext==".csf" || ext==".wav" || ext==".aud" || ext==".bik")
                issue(false,"unwired-resource",rel+": custom audio, video and string-table loading are not wired in this milestone");
            if (ext==".tem" || ext==".sno" || ext==".urb")
                issue(false,"theater-resource",rel+": theater artwork can resolve, but custom TMP template loading is not wired");
        }
        if (!p->archives_.empty()) issue(false,"archive-scope","Archive art is resolved as data; custom audio banks, string tables, theater tiles and campaign packaging are not yet fully integrated");
        const std::set<std::string> audit_configs={"rulesmd.ini","artmd.ini","aimd.ini","soundmd.ini","evamd.ini",
            "rulesmo.ini","artmo.ini","aimo.ini","soundmo.ini","evamo.ini"};
        for (const auto& cn:audit_configs) {
            std::vector<uint8_t> bytes;
            if (!p->read(cn,bytes)) continue;
            if (!config_names.count(cn)) issue(true,"renamed-config",cn+" found: this mod uses a nonstandard config name and needs an explicit adapter");
            if (bytes.size()>8*1024*1024) {issue(true,"oversize-config",cn);continue;}
            if (lower(text(bytes)).find("#include")!=std::string::npos)
                issue(true,"config-includes",cn+": included configuration files require a resolver that is not implemented yet");
            const auto ini=Ini::parse(text(bytes));std::set<std::string> unknown,extensions;
            for (const auto& section:ini.section_names()) for (const auto& kv:ini.section(section)) {
                const auto k=kv.first;
                // Registry IDs and packed script row numbers are data, not capability names.
                if (std::all_of(k.begin(),k.end(),[](unsigned char c){return c>='0'&&c<='9';}) ||
                    section=="AITRIGGERTYPES" || section=="TEAMTYPES" || section=="SCRIPTTYPES" || section=="TASKFORCES") continue;
                const auto dot=k.find('.');
                if (dot!=std::string::npos) extensions.insert(k.substr(0,dot)+".*");
                else if (k=="ATTACHEFFECT" || k=="CUSTOMPALETTE") extensions.insert(k);
                else if (!known_keys.count(k)) unknown.insert(k);
            }
            if(!extensions.empty()) {
                std::string list;for(const auto& k:extensions){if(!list.empty())list+=", ";list+=k;}
                issue(true,"extension-key",cn+": extension families require implementation review: "+list);
            }
            if (!unknown.empty()) {
                std::string sample;for (const auto& k:unknown) {if(sample.size()>800)break;if(!sample.empty())sample+=", ";sample+=k;}
                issue(false,"unverified-keys",cn+": "+std::to_string(unknown.size())+" distinct keys outside the initial verified subset: "+sample);
            }
        }
        if (!p->maps().empty()) issue(false,"map-validation","Maps are checked by the existing custom-map validator; scripts and additional theaters remain restricted");
        // Hash actual content, with stable relative names. Never hash or depend on the source path.
        if (p->can_activate()) {
            uint64_t h=0xcbf29ce484222325ull;std::array<char,65536> buffer{};
            for (const auto& file:p->files_) {
                hash_bytes(h,file.first.c_str(),file.first.size()+1);
                std::ifstream in(fs::u8path(file.second),std::ios::binary);
                if(!in)throw std::runtime_error("Cannot read "+file.first);
                while(in) {in.read(buffer.data(),buffer.size());hash_bytes(h,buffer.data(),static_cast<std::size_t>(in.gcount()));}
                if(!in.eof())throw std::runtime_error("Read failed: "+file.first);
                hash_bytes(h,"\0",1);
            }
            std::ostringstream hs;hs<<std::hex<<std::setw(16)<<std::setfill('0')<<h;p->fingerprint=hs.str();
        }
    } catch (const std::exception& e) {issue(true,"inspection-failed",e.what());}
    return p;
}
const MixEntry* ModProfile::find(const std::string& name,const MixFile** owner,std::string* source) const {
    if (!safe_leaf(name)) return nullptr;
    const auto key=lower(name);const auto loose=loose_files_.find(key);
    auto hit=[&](const std::string& label,const std::shared_ptr<MixFile>& mix)->const MixEntry* {
        const auto* e=mix->find(key);if (!e) return nullptr;
        if (e->size>64*1024*1024) throw std::runtime_error("Mod resource exceeds 64 MiB: "+name);
        if(owner)*owner=mix.get();if(source)*source="mod:"+id+"/"+label;return e;
    };
    if (loose!=loose_files_.end()) {
        std::lock_guard<std::mutex> lock(loose_mutex_);
        auto opened=loose_.find(key);
        if(opened==loose_.end())opened=loose_.emplace(key,std::make_shared<MixFile>(MixFile::open_loose_path(key,loose->second))).first;
        return hit(key,opened->second);
    }
    for (const auto& archive:archives_) if (const auto* e=hit(archive.first,archive.second))return e;
    return nullptr;
}
bool ModProfile::read(const std::string& name,std::vector<uint8_t>& out) const {
    const MixFile* owner=nullptr;const auto* e=find(name,&owner);if(!e)return false;out=owner->read(*e);return true;
}
std::vector<std::string> ModProfile::maps() const {
    std::vector<std::string> out;
    for(const auto& f:files_)if(f.first.rfind("maps/",0)==0 && safe_leaf(f.first.substr(5))) {
        const auto ext=fs::u8path(f.first).extension().u8string();
        if(ext==".map" || ext==".mpr" || ext==".yrm")out.push_back(f.first.substr(5));
    }
    return out;
}
std::string ModProfile::map_path(const std::string& leaf) const {
    if(!safe_leaf(leaf))return {};const auto it=files_.find("maps/"+lower(leaf));return it==files_.end()?"":it->second;
}
std::vector<uint8_t> merge_mod_ini(const std::vector<uint8_t>& base,const std::vector<uint8_t>& patch) {
    const auto original=Ini::parse(text(base)), overlay=Ini::parse(text(patch));
    std::ostringstream result;
    auto sections=original.section_names();
    for(const auto& s:overlay.section_names())if(!original.has(s))sections.push_back(s);
    for(const auto& s:sections) {
        result<<'['<<s<<"]\n";std::set<std::string> seen;
        // Replace in-place to preserve registry order and first-key readers.
        for(const auto& kv:original.section(s)) {
            const auto replacement=overlay.get(s,kv.first);
            const bool first=seen.insert(kv.first).second;
            if(replacement.empty()) result<<kv.first<<'='<<kv.second<<'\n';
            else if(first) result<<kv.first<<'='<<replacement<<'\n';
        }
        for(const auto& kv:overlay.section(s))if(seen.insert(kv.first).second)
            result<<kv.first<<'='<<overlay.get(s,kv.first)<<'\n';
    }
    const auto out=result.str();return {out.begin(),out.end()};
}
void activate_mod(std::shared_ptr<const ModProfile> profile) {
    if(profile && !profile->can_activate())throw std::runtime_error(profile->report());current=std::move(profile);
}
std::shared_ptr<const ModProfile> active_mod() { return current; }
}

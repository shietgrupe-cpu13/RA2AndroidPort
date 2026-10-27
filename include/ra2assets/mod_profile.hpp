#pragma once
#include "ra2assets/mix.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ra2assets {
struct ModFinding { bool blocked = false; std::string code, detail; };
// A read-only, process-lifetime overlay. No executable or DLL is ever loaded.
class ModProfile {
public:
    static std::shared_ptr<ModProfile> inspect(const std::string& directory);
    bool can_activate() const;
    std::string status() const;
    std::string report() const;
    const MixEntry* find(const std::string& name, const MixFile** owner = nullptr,
                         std::string* source = nullptr) const;
    bool read(const std::string& name, std::vector<uint8_t>& out) const;
    std::string map_path(const std::string& leaf) const;
    std::vector<std::string> maps() const;
    std::string root, id, name, fingerprint;
    std::vector<ModFinding> findings;
private:
    std::map<std::string, std::string> files_;
    std::vector<std::pair<std::string, std::shared_ptr<MixFile>>> archives_;
    // Loose files are exposed through the same lazy reader as archive members.
    std::map<std::string, std::string> loose_files_;
    mutable std::map<std::string, std::shared_ptr<MixFile>> loose_;
    mutable std::mutex loose_mutex_;
};
bool safe_mod_id(const std::string& value);
// INI patch: existing entries keep order; a later duplicate key wins.
std::vector<uint8_t> merge_mod_ini(const std::vector<uint8_t>& base, const std::vector<uint8_t>& patch);
void activate_mod(std::shared_ptr<const ModProfile> profile);
std::shared_ptr<const ModProfile> active_mod();
} // namespace ra2assets

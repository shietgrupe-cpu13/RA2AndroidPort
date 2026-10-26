// P7 Save / Load [RE: docs/re/saveload.md]. See savegame.hpp for the format.
#include "savegame.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <set>

#include "save_fields.hpp"

namespace ra2engine {
namespace save {

namespace {
constexpr char kMagic[8] = {'R', 'A', '2', 'S', 'A', 'V', 'E', '\0'};

template <class T> void put(Writer& w, T v) { io(w, v); }
}  // namespace

std::vector<std::uint8_t> save_world(const World& w) {
    Writer wr;
    // The Writer only reads; const_cast lets save and load share one visitor.
    SaveAccess::world(wr, const_cast<World&>(w));
    return std::move(wr.buf);
}

std::vector<std::pair<std::string, std::uint32_t>> world_field_hashes(const World& w) {
    FieldMarks marks;
    field_marks() = &marks;
    SyncWriter sw;   // W9-05: the sync-hash policy (derived caches hash as empty)
    SaveAccess::world(sw, const_cast<World&>(w));
    const std::vector<std::uint8_t> b = std::move(sw.buf);
    field_marks() = nullptr;
    std::vector<std::pair<std::string, std::uint32_t>> out;
    std::size_t from = 0;
    for (const auto& m : marks.v) {
        std::uint32_t h = 2166136261u;
        for (std::size_t i = from; i < m.second && i < b.size(); ++i) h = (h ^ b[i]) * 16777619u;
        out.emplace_back(m.first, h);
        from = m.second;
    }
    return out;
}

std::uint64_t world_sync_hash(const World& w, const MapTriggerSystem* triggers) {
    SyncHasher h;
    SaveAccess::world(h, const_cast<World&>(w));
    if (triggers) SaveAccess::triggers(h, const_cast<MapTriggerSystem&>(*triggers));
    return h.h;
}

std::uint64_t econ_rules_digest(const PlayerEconRules& r) {
    SyncHasher h;
    io(h, const_cast<PlayerEconRules&>(r));
    return h.h;
}

std::vector<std::pair<std::string, std::uint64_t>> econ_rules_field_digests(const PlayerEconRules& r0) {
    PlayerEconRules& r = const_cast<PlayerEconRules&>(r0);
    std::vector<std::pair<std::string, std::uint64_t>> out;
    auto d = [&out](const char* n, auto& v) {
        SyncHasher h;
        io(h, v);
        out.emplace_back(n, h.h);
    };
    d("units", r.units); d("unit_anims", r.unit_anims); d("unit_has_anim", r.unit_has_anim);
    d("factory_types", r.factory_types); d("groups", r.groups); d("water_bound", r.water_bound);
    d("wall", r.wall); d("wall_fill_range", r.wall_fill_range); d("buildup_total", r.buildup_total);
    d("base_normal", r.base_normal); d("build_off_ally", r.build_off_ally); d("ally_eligible", r.ally_eligible); d("bld_allowed", r.bld_allowed); d("unit_build_limit", r.unit_build_limit);
    d("build_speed", r.build_speed); d("low_power_mod", r.low_power_mod); d("multiple_factory", r.multiple_factory);
    d("wall_coefficient", r.wall_coefficient); d("max_queued", r.max_queued); d("wall_rules", r.wall_rules);
    for (std::size_t i = 0; i < r.units.size(); ++i) {
        SyncHasher h;
        io(h, r.units[i]);
        out.emplace_back("unit " + r.units[i].id, h.h);
    }
    return out;
}

bool load_world(World& w, const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);
    SaveAccess::world(r, w);
    if (!r.ok() || !r.at_end()) return false;
    std::string why;
    if (!SaveAccess::valid_world(w, &why)) {
        std::fprintf(stderr, "[saveload] load_world: index out of range: %s\n", why.c_str());
        return false;
    }
    return true;
}

std::vector<std::uint8_t> save_triggers(const MapTriggerSystem& s) {
    Writer wr;
    SaveAccess::triggers(wr, const_cast<MapTriggerSystem&>(s));
    return std::move(wr.buf);
}

bool load_triggers(MapTriggerSystem& s, const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);
    SaveAccess::triggers(r, s);
    return r.ok() && r.at_end();
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes, std::uint64_t h) {
    for (std::uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

// --- container -----------------------------------------------------------------
// v10 layout: magic, version, header block, section count, sections, then a
// 4-byte little-endian CRC-32 of every byte before it (W2-AR-20: a flipped
// byte anywhere is refused instead of loading silently altered state).
std::vector<std::uint8_t> encode_save(const SaveFile& f) {
    Writer w;
    w.bytes(kMagic, 8);
    std::uint32_t version = kFormatVersion;
    w.bytes(&version, 4);
    SaveHeader h = f.header;
    h.description = clean_description(h.description);
    std::vector<std::uint8_t> hb = to_bytes(h);
    io(w, hb);
    std::uint64_t n = f.sections.size();
    io(w, n);
    for (const auto& kv : f.sections) {
        std::uint32_t t = kv.first;
        w.bytes(&t, 4);
        std::vector<std::uint8_t> body = kv.second;
        io(w, body);
    }
    const std::uint32_t crc = crc32(w.buf.data(), w.buf.size());
    for (int i = 0; i < 4; ++i) w.buf.push_back(static_cast<std::uint8_t>(crc >> (8 * i)));
    return std::move(w.buf);
}

bool decode_save(const std::vector<std::uint8_t>& bytes, SaveFile& out, bool header_only) {
    out = SaveFile{};
    Reader r(bytes);
    char magic[8];
    r.bytes(magic, 8);
    if (!r.ok() || std::memcmp(magic, kMagic, 8) != 0) return false;
    std::uint32_t version = 0;
    r.bytes(&version, 4);
    if (!r.ok()) return false;
    std::vector<std::uint8_t> hb;
    io(r, hb);
    if (!r.ok()) return false;
    {
        // The header block is length-prefixed, so it is skippable even when its
        // layout is from another version; a wrong-version header that fails to
        // parse just shows an empty description.
        SaveHeader h;
        Reader hr(hb);
        io(hr, h);
        if (hr.ok()) out.header = h;
        out.header.version = version;
    }
    if (!supported_version(version) || header_only) return true;
    // W2-AR-20: verify the CRC-32 trailer, then parse the body without it.
    if (bytes.size() < 4 + 12) return false;
    const std::size_t body_n = bytes.size() - 4;
    std::uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) stored |= static_cast<std::uint32_t>(bytes[body_n + i]) << (8 * i);
    if (crc32(bytes.data(), body_n) != stored) return false;
    const std::vector<std::uint8_t> body_bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(body_n));
    Reader br(body_bytes);
    {
        char m[8];
        std::uint32_t v = 0;
        std::vector<std::uint8_t> skip;
        br.bytes(m, 8);
        br.bytes(&v, 4);
        io(br, skip);
    }
    std::uint64_t n = 0;
    io(br, n);
    for (std::uint64_t i = 0; i < n && br.ok(); ++i) {
        std::uint32_t t = 0;
        br.bytes(&t, 4);
        std::vector<std::uint8_t> body;
        io(br, body);
        if (br.ok()) out.sections[t] = std::move(body);
    }
    return br.ok() && br.at_end();
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    // Write-then-rename so a crash mid-save never leaves a torn file in place
    // of a good one.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

bool read_file_bytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(n));
    if (n) f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<bool>(f);
}

// --- slots ---------------------------------------------------------------------
namespace {
std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}
}  // namespace

std::vector<SlotEntry> list_saves(const std::string& dir) {
    std::vector<SlotEntry> out;
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
        if (!de.is_regular_file(ec)) continue;
        const std::string name = de.path().filename().string();
        if (name.size() < 4 || upper(name.substr(name.size() - 4)) != ".SAV") continue;
        std::vector<std::uint8_t> bytes;
        SlotEntry e;
        e.path = de.path().string();
        e.file_name = name;
        SaveFile f;
        if (!read_file_bytes(de.path().string(), bytes) || !decode_save(bytes, f, /*header_only=*/true)) {
            e.damaged = true;   // W2-AR-21: listed as damaged, not hidden
        } else {
            e.header = f.header;
            e.wrong_version = (!supported_version(f.header.version));
            // Current-version files are fully checked (CRC + framing) so a
            // corrupt one is flagged in the list, not discovered on load.
            SaveFile full;
            if (!e.wrong_version && !decode_save(bytes, full)) e.damaged = true;
        }
        const auto t = std::filesystem::last_write_time(de.path(), ec);
        e.mtime = ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
        out.push_back(std::move(e));
    }
    // FileEntryClass::Compare: newest first; name breaks ties deterministically.
    std::sort(out.begin(), out.end(), [](const SlotEntry& a, const SlotEntry& b) {
        if (a.mtime != b.mtime) return a.mtime > b.mtime;
        return a.file_name < b.file_name;
    });
    return out;
}

std::string new_save_name(const std::string& dir, std::uint32_t seed) {
    // SAVE%04lX.%3s with rand() retried until free (saveload.md §2). xorshift32
    // stands in for the CRT rand(); seed 0 is remapped (xorshift fixed point).
    std::uint32_t s = seed ? seed : 0x5A17u;
    for (int tries = 0; tries < 70000; ++tries) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        char buf[16];
        std::snprintf(buf, sizeof buf, "SAVE%04X.SAV", static_cast<unsigned>(s & 0xFFFFu));
        std::error_code ec;
        const std::filesystem::path p = std::filesystem::path(dir) / buf;
        if (!std::filesystem::exists(p, ec)) return buf;
    }
    return "SAVE0000.SAV";
}

bool delete_save(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec) && !ec;
}

std::string clean_description(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
        if (out.size() >= kMaxDescription) break;
    }
    return out;
}

}  // namespace save

// R-181: index range checks after a load (see save_fields.hpp).
bool SaveAccess::valid_world(const World& w, std::string* why) {
    auto bad = [&](const std::string& m) {
        if (why) *why = m;
        return false;
    };
    // [RE-FALLBACK: generous engine limits, not original ones] 64 houses
    // (the original's HouseClass array holds 20 + the specials), maps up to
    // 512x512 cells, building foundations up to 8x8.
    constexpr int kMaxHouses = 64, kMaxGrid = 512, kMaxFoundation = 8;
    const int gw = w.grid_w_, gh = w.grid_h_;
    if (gw < 0 || gh < 0 || gw > kMaxGrid || gh > kMaxGrid) return bad("grid size");
    const std::size_t cells = static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
    if (!w.passable_.empty() && w.passable_.size() != cells) return bad("passable grid");
    if (!w.water_passable_.empty() && w.water_passable_.size() != cells) return bad("water grid");
    if (w.houses_.size() > static_cast<std::size_t>(kMaxHouses)) return bad("house table");
    {   // w8fix-layer: the occupancy planes match its grid
        const Occupancy& o = w.occ_;
        const std::size_t oc = static_cast<std::size_t>(o.w_) * static_cast<std::size_t>(o.h_);
        if (o.w_ < 0 || o.h_ < 0 || o.owner_.size() != oc ||
            (!o.spots_.empty() && o.spots_.size() != oc * Occupancy::kSpots) ||
            (!o.owner_d_.empty() && o.owner_d_.size() != oc) ||
            (o.owner_d_.empty() ? !o.spots_d_.empty() : o.spots_d_.size() != o.spots_.size()))
            return bad("occupancy grid");
    }
    const int nu = static_cast<int>(w.units_.size());
    const int nb = static_cast<int>(w.buildings_.size());
    const int nt = static_cast<int>(w.types_.empty() ? w.lookup_types_.size() : w.types_.size());
    auto unit_ix = [&](int i) { return i >= -1 && i < nu; };
    auto bld_ix = [&](int i) { return i >= -1 && i < nb; };
    auto house_ix = [&](int h) { return h >= 0 && h < kMaxHouses; };
    if(w.drain_.delay<1 || w.drain_.amount<0 || w.drain_.links.size()>w.units_.size())return bad("drain configuration");
    std::set<int> drain_sources;
    for(const auto& d:w.drain_.links)
        if(d.unit<0 || d.unit>=nu || d.building<0 || d.building>=nb ||
           !house_ix(d.source_house) || !house_ix(d.target_house) || d.frames<0 ||
           d.frames>=w.drain_.delay || !drain_sources.insert(d.unit).second)return bad("drain link");
    for(const auto& b:w.buildings_)if(b.bunker_exit_left < -1 || b.bunker_exit_left>18)
        return bad("bunker lowering timer");
    for (int i = 0; i < nu; ++i) {
        const Unit& u = w.units_[static_cast<std::size_t>(i)];
        const std::string at = "unit " + std::to_string(i) + " ";
        if (!house_ix(u.house)) return bad(at + "house");
        // W5-E / W8-06-01: cargo_of also holds the garrison and the parachute
        // sentinels (a garrisoned infantry, a paratrooper under its chute).
        const bool cargo_ok = unit_ix(u.cargo_of) || u.cargo_of == kGarrisonCargo || u.cargo_of == kParachuteCargo;
        if (!unit_ix(u.target) || !cargo_ok ||
            !unit_ix(u.enter_target) ||
            !unit_ix(u.parasite_on_me) || !unit_ix(u.temporal_target) || !unit_ix(u.bomb_owner) ||
            !unit_ix(u.ability_fired_unit))
            return bad(at + "unit reference");
        if (!bld_ix(u.target_building) || !bld_ix(u.dock_refinery) || !bld_ix(u.capture_target) ||
            !bld_ix(u.infiltrate_target) || !bld_ix(u.garrison_of) || !bld_ix(u.occupy_target) || !bld_ix(u.bunker_exit_host) ||
            !bld_ix(u.temporal_target_building) || !bld_ix(u.ability_fired_building))
            return bad(at + "building reference");
        if (u.bomb_house < -1 || u.bomb_house >= kMaxHouses) return bad(at + "bomb house");
        // W8-06-01 audit: the lists and air/naval links later code indexes
        // with (a transport's cargo -- a transport inside a transport holds
        // unit indices both ways --, a spawn's owner, the spawn-manager nodes,
        // an aircraft's airfield). None of them holds a sentinel.
        for (int c : u.cargo)
            if (!unit_ix(c)) return bad(at + "cargo list");
        if (!unit_ix(u.an.owner) || !unit_ix(u.an.attack_unit) || !bld_ix(u.an.home) ||
            !bld_ix(u.an.attack_building))
            return bad(at + "air/naval reference");
        for (int s : u.an.sp_unit)
            if (!unit_ix(s)) return bad(at + "spawn node");
    }
    for (int i = 0; i < nb; ++i) {
        const Building& b = w.buildings_[static_cast<std::size_t>(i)];
        const std::string at = "building " + std::to_string(i) + " ";
        if (!house_ix(b.house)) return bad(at + "house");
        if (b.type < -1 || (nt > 0 && b.type >= nt)) return bad(at + "type");
        if (b.fw < 1 || b.fh < 1 || b.fw > kMaxFoundation || b.fh > kMaxFoundation) return bad(at + "foundation");
        if (gw > 0 && gh > 0 && (b.cx < 0 || b.cy < 0 || b.cx >= gw || b.cy >= gh)) return bad(at + "cell");
        if (!unit_ix(b.killed_by) || !unit_ix(b.c4_by) || !unit_ix(b.bomb_owner) || !unit_ix(b.def_target))
            return bad(at + "unit reference");
        if (b.bomb_house < -1 || b.bomb_house >= kMaxHouses) return bad(at + "bomb house");
        if(b.attack_order_kind<0 || b.attack_order_kind>3) return bad(at+"attack order kind");
        if(b.attack_order_house < -1 || (b.attack_order_house>=0 && !house_ix(b.attack_order_house)))return bad(at+"attack order owner");
        if((b.attack_order_kind==1 && (b.attack_order_target<0 || !unit_ix(b.attack_order_target))) ||
           (b.attack_order_kind==2 && (b.attack_order_target<0 || !bld_ix(b.attack_order_target))) ||
           (b.attack_order_kind==3 && (b.attack_order_x<0 || b.attack_order_y<0 || b.attack_order_x>=gw || b.attack_order_y>=gh)))
            return bad(at+"attack order target");
        for (int o : b.occupants)   // W8-06-01 audit: the garrison list
            if (!unit_ix(o)) return bad(at + "occupant list");
    }
    return true;
}
}  // namespace ra2engine

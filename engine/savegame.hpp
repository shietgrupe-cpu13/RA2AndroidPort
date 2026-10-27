#pragma once
// P7 Save / Load [RE: docs/re/saveload.md; spec 2026-10-06-saveload-design.md].
//
// The archive core: a Writer and a Reader that share ONE visitor per struct
// (engine/save_fields.hpp), plus the save-file container (header + tagged
// sections) and the slot helpers (SAVE%04X.SAV naming, newest-first listing).
//
// FORMAT DECISION: the original's save is an OLE compound file of raw class
// memory images (saveload.md §4) -- not reproducible. We keep its naming, its
// header-first listing and its UI flow, and use our own versioned binary
// payload. Every scalar is little-endian fixed width: bool/int8/uint8 -> 1
// byte; every other integral (int, long, size_t, ...) -> 8 bytes (long differs
// between Win64 and arm64); double -> its 8-byte bit pattern; enums -> 8-byte
// signed underlying value. Bit-exact doubles are what make load(save(x))
// continue bit-identically.
//
// SDL-free; compiled into both ra2engine and ra2assets_tests.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "orig_math.hpp"   // W9-05-13 om::canonical_bits

namespace ra2engine {

class World;
class MapTriggerSystem;
struct PlayerEconRules;

namespace save {

// Bump on ANY change to what is serialized (a field added to a visitor, a
// section's layout). Unsupported versions are listed but refused on load.
// Version56 explicitly migrates55; other older layouts remain unsupported.
constexpr std::uint32_t kFormatVersion = 61; // appended flight terrain origins; v60 waypoint routes and launcher profiles; retained DiskLaser charge/discharge. // DEBUG-AI mode; unchanged v57 field layout. // 56: appended Disc drain state, with v55 read migration. // 55: explicit building attack orders. // 54: saved Tank Bunker lowering and physical exit passage. // 53: Gatling weapon stages and charge. // 52: rules-driven slave harvesting/reload intervals.   // 50: packed slave worker mobile-manager links; 49: building mind control; 48: skirmish mind-control ownership and slave manager/worker state; 47: w9fix-menus SkirmishConfig::mode_rules (game type rules overlay) on top of 46; 46: w9fix-match -- World diplomacy rules (allies_allowed_/ally_change_allowed_), game_over_, observer_reveal_, victory_defeats_seen_ on top of 45; 45: w8fix-place -- World::build_static_ (placement BuildMask) on top of 44; 44: w8fix-save -- AnUnit para_passes/para_overfly/para_timer (paradrop Approach/Overfly) on top of 43; 43: w8fix-layer -- Occupancy deck plane (owner_d_/spots_d_) + per-unit layer lay_, on top of 42; 42: wave-8 fix (w8fix-misc: UnitW6::hover_spin, the Robot Tank power-off spin); 40: fv7c -- per-house masks 32-bit (Unit::seen_by_mask, AnUnit::detected_mask, World w5_hide_/w5_own_; were uint8_t, capped at 8 houses) + UnitW7::missile_holds removed (dead since the spawn-manager merge); 39 (+ w7fix-move: UnitCombat/Unit loco -- locomotor kind, acceleration keys, speed percentage, Drive/Ship track, hover ramp, blocked-move timers; World::loco_rules_; AnType jj_accel/strafe/fighter/slowdown_distance, AnUnit jj_speed/fly_throttle/strafe_left); 39: w7fix-combat mission model (UnitTgt mission/scan timer/override/home/attack-move/help/crush warn/ROF parts, UnitTypeTgt W7 keys, World combat-W7 rules + house IQ + hit log) on top of 38; 38: wave-7 presentation (InfAnimState erupt/air; World rad/nuke/storm_inf_death_ + frag_events_) on top of 37; 37: wave-7 AI (Unit::ai guard clock, AiHouseConfig auto_deploy_frames / roster_to_protect) on top of 36; 36: wave-7 units + air/naval merged (35 + AnType crash weapon / spawner range / sonic / DistributedFire / RadarInvisible / HoverAttack / AirportBound, AnUnit spawn-manager nodes + launch hold + loaner + crash_hp + naval_cell + shot list, AnWorld sonic waves, SwParams C4Warhead); 35: wave-7 fix (units) -- UnitTypeW7 / UnitW7 / BldW7 (unit_w7.hpp: role flags, Mirage blink, drone paralysis, deploy timers, depot timer, mind-control capacity); 34: Net L0 whole-frame int timers (Unit::rearm_frames, Building def/garrison_rearm_frames + iron_left_frames, AnUnit surface/iron/chrono_left_frames were double seconds) + World::session_seed_ / SkirmishConfig::session_seed (Net L0 session seed); 33: NET L1 -- Unit::selected removed (the selection is local UI state, commands carry unit ids); 32: L2 per-house economy (World::econ_ wallets + six queues + backlog/hold for player AND AI houses, human mask, latched outcomes, cheer flags; HouseAiRec loses credits/queues; wallet_hint_ removed; SceneSave loses the wallet/queues; the QBKL section is no longer written); 31: warhead BombDisarm= (WeaponSlot + UnitTypeW6 bomb_disarm, GetFireError gate); 30: R-210 bridges and cliffs (Unit::on_deck, BldTypeW6::gate, World deck grids); 29: W6 fix #2 (missions): Unit/UnitCombat tg (rules id, Insignificant/LegalTarget/ImmuneToPsionics/GuardRange, IsInPlayfield, area guard), Building tg, World playfield/usable area + per-house handicap; 28: wave-6 #4 units/weapons (WeaponSlot/WeaponStats/WeaponSpec Burst, UnitCombat/Unit/Building w6: burst index + type data); walls' damage stages, map wall overlays, warhead wall keys, IFV elite weapons) merged with 27; 27: W6-05-04 MapTriggerSystem::timer_text_ (action 103 TimerText label); 26: W6 skirmish AI (BuildingTypeDef wall/insignificant, HouseAiRec anger table + hate-delay clock, AiHouseConfig virtual purifiers) merged with 25; 25: W6-06-04 campaign trigger particle systems section PFX; 24: Unit::w_mind_control (campaign mind-control shot captures, no damage); 23: P4 building death weapons (BldTypeW5 explodes/delay_kill_eligible/death) + WeaponSpec delay kill; 22: W5-A campaign host state (building TurnOff/On, AI SW quarry, FireSale houses, teams that left the map, ResizeView area) + World wall leak mask / SW quarry + AiHouseConfig fire_superweapons; 21: W5-E2 Building::power_off; 20: W5-B infantry sub-cell spots (Occupancy spots_/inf_) + Unit spot/sub offset/speed/crush/is_infantry/land-speed loco + land speed/crush/elevation rules + AffectsAllies + projectile SubjectTo*; 19: W5-E audio section AUDI (music track / on); 18: wave-5 abilities (stolen tech, prism/tesla/gap, tech heal, OpenTopped, DeployTime, defuse, IFV swap, Boris, squid, ship C4, vet abilities; BldTypeW5/UnitTypeW5/BldW5/UnitW5/WorldW5 + WeaponSpec projectile); 17: W5-F build-queue backlog/hold section QBKL + load_world index range checks; 16: W4-SQ paradrop fall (AnUnit para_alt, SwParams parachute_fall) + team airdrop flag + player-order queue + passive houses; 15: W4 per-type rocket blocks/warheads + RocketLocomotion flight + homing projectiles; 14: W3-B infantry sequence/fear state + warhead InfDeath/ProneDamage; 13: W2-SK ore mines + harvester/AI/spy/depot/unit-special state; 12: w2fix-campaign PlayerControl/pair alliances/entered events/mind control; 11: W2-AR-20 CRC-32 trailer over the whole file; 10: W2-AN air/naval/SW state; 9: W2-GA-03 FogOfWar flag; 2: P10 campaign; 3: gap-A queues/roster flags; 4: gap-C weapon/defense; 5: gap-D AI; 6: gap-D AI + gF MCV deploy combined; 7: G16 garrison/bridge state; 8: skirmish-page game_speed/mcv_redeploys in the config

inline bool supported_version(std::uint32_t v) { return v == kFormatVersion || v == 60 || v == 59 || v == 58 || v == 57 || v == 56 || v == 55; }
// --- archives ---------------------------------------------------------------
// BasicWriter<false> = Writer (the save file); BasicWriter<true> = SyncWriter,
// the same bytes under the SYNC-HASH POLICY below (the desync dump's
// per-member hashes, world_field_hashes).
template <bool SyncHash> class BasicWriter {
public:
    static constexpr bool kLoading = false;
    static constexpr bool kSyncHash = SyncHash;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    bool ok() const { return true; }
    std::vector<std::uint8_t> buf;
};
using Writer = BasicWriter<false>;
using SyncWriter = BasicWriter<true>;

// W9-05 sync hash: the streaming archive of the lockstep state hash. Folds
// every byte straight into FNV-1a 64 (no buffer), under the sync-hash policy.
class SyncHasher {
public:
    static constexpr bool kLoading = false;
    static constexpr bool kSyncHash = true;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 0x100000001b3ull;
        }
    }
    bool ok() const { return true; }
    std::uint64_t h = 0xcbf29ce484222325ull;
};

// --- W9-05: the SYNC-HASH POLICY ----------------------------------------------
// One rule set for every hash two peers compare (the full-World debug hash,
// the desync dump's per-member hashes, and any cheaper frame CRC built on
// these visitors): an archive with `kSyncHash == true` is a hash. The SAVE
// bytes never change; only these two visitor helpers branch on it:
//   io_unhashed(ar, x)  -- a DERIVED cache that a const accessor rebuilds
//       lazily (amphib_grid(), crush_grid(), deck_grid(), the gap scratch).
//       Whether a presentation-side call refreshed it on ONE peer must not
//       read as a desync: saved (load stays bit-identical), never hashed.
//   io_hash_only(ar, x) -- state the save does not carry (re-installed after
//       a load) that both peers must nevertheless have built identically
//       (the per-house production rules digest): hashed, never saved.
// tests/test_saveload.cpp pins the exact list of unhashed World members
// (kUnhashedWorldMembers there), so a new member cannot be silently unhashed.
namespace detail {
template <class Ar, class = void> struct sync_hash_ar : std::false_type {};
template <class Ar>
struct sync_hash_ar<Ar, std::void_t<decltype(Ar::kSyncHash)>> : std::bool_constant<Ar::kSyncHash> {};
}  // namespace detail
template <class Ar> inline constexpr bool kHashing = detail::sync_hash_ar<Ar>::value;

// NET-PLAY: optional per-member marks while a World is written, so a desync
// report can name the World member whose bytes differ between two machines
// (world_field_hashes). Off (nullptr) except inside world_field_hashes.
struct FieldMarks { std::vector<std::pair<const char*, std::size_t>> v; };
inline FieldMarks*& field_marks() { static FieldMarks* m = nullptr; return m; }
template <class Ar> inline void mark(Ar&, const char*) {}
template <bool H> inline void mark(BasicWriter<H>& w, const char* name) {
    if (FieldMarks* m = field_marks()) m->v.emplace_back(name, w.buf.size());
}

class Reader {
public:
    static constexpr bool kLoading = true;
    Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
    explicit Reader(const std::vector<std::uint8_t>& v) : p_(v.data()), n_(v.size()) {}
    // Copies n bytes out; on underflow zero-fills, sets the fail flag, and
    // every later read also fails (a truncated/corrupt file never crashes).
    void bytes(void* out, std::size_t n) {
        if (!ok_ || n > n_ - pos_) {
            ok_ = false;
            std::memset(out, 0, n);
            return;
        }
        std::memcpy(out, p_ + pos_, n);
        pos_ += n;
    }
    bool ok() const { return ok_; }
    void fail() { ok_ = false; }
    std::size_t remaining() const { return ok_ ? n_ - pos_ : 0; }
    bool at_end() const { return pos_ == n_; }

private:
    const std::uint8_t* p_;
    std::size_t n_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

// Field-coverage counter: io(Counter&, x) counts one per visited member (a C
// array counts its element total, matching brace-elision aggregate arity) and
// never recurses. Used by the arity tripwire test.
class Counter {
public:
    static constexpr bool kLoading = false;
    std::size_t n = 0;
};

// --- aggregate arity (the brace-init member count) -------------------------
namespace detail {
struct AnyRef {
    template <class T> constexpr operator T&() const noexcept;
    template <class T> constexpr operator T&&() const noexcept;
};
template <class T, class Seq, class = void> struct init_n : std::false_type {};
template <class T, std::size_t... I>
struct init_n<T, std::index_sequence<I...>,
              std::void_t<decltype(T{(void(I), AnyRef{})...})>> : std::true_type {};
template <class T> struct is_vector : std::false_type {};
template <class T, class A> struct is_vector<std::vector<T, A>> : std::true_type {};
template <class T> struct is_std_array : std::false_type {};
template <class T, std::size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};
template <class T> struct is_map : std::false_type {};
template <class K, class V, class C, class A> struct is_map<std::map<K, V, C, A>> : std::true_type {};
template <class T> struct is_pair : std::false_type {};
template <class A, class B> struct is_pair<std::pair<A, B>> : std::true_type {};
template <class T> constexpr std::size_t total_extent() {
    if constexpr (std::is_array_v<T>) return std::extent_v<T> * total_extent<std::remove_extent_t<T>>();
    else return 1;
}
}  // namespace detail

// Number of members of aggregate T as the compiler sees them (C-array members
// count their elements -- brace elision). 9999 = not an aggregate / >200.
template <class T, std::size_t N = 0>
constexpr std::size_t aggregate_arity() {
    if constexpr (N > 200) return 9999;
    else if constexpr (!detail::init_n<T, std::make_index_sequence<N + 1>>::value) return N;
    else return aggregate_arity<T, N + 1>();
}

// --- the generic io ---------------------------------------------------------
template <class Ar, class T> void io(Ar& ar, T& v);

template <class Ar, class T> void io_scalar(Ar& ar, T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        std::uint8_t b = v ? 1 : 0;
        if constexpr (Ar::kLoading) { ar.bytes(&b, 1); v = (b != 0); }
        else ar.bytes(&b, 1);
    } else if constexpr (std::is_enum_v<T>) {
        std::int64_t x = static_cast<std::int64_t>(v);
        if constexpr (Ar::kLoading) { ar.bytes(&x, 8); v = static_cast<T>(x); }
        else ar.bytes(&x, 8);
    } else if constexpr (std::is_floating_point_v<T>) {
        static_assert(sizeof(double) == 8, "IEEE double expected");
        double d = static_cast<double>(v);
        if constexpr (Ar::kLoading) { ar.bytes(&d, 8); v = static_cast<T>(d); }
        else {
            // W9-05-13: a NaN's sign / payload bits are not portable (an SSE
            // default NaN is 0xFFF8..., an arm64 one 0x7FF8...): every NaN is
            // written (and hashed) as the one quiet NaN 0x7FF8000000000000.
            // Any NaN behaves the same in IEEE arithmetic and comparisons, so
            // a load still continues bit-identically.
            // (om::canonical_bits: the same rule net_frame_crc folds.)
            const std::uint64_t q = om::canonical_bits(d);
            ar.bytes(&q, 8);
        }
    } else if constexpr (sizeof(T) == 1) {
        if constexpr (Ar::kLoading) ar.bytes(&v, 1);
        else ar.bytes(&v, 1);
    } else if constexpr (std::is_signed_v<T>) {
        std::int64_t x = static_cast<std::int64_t>(v);
        if constexpr (Ar::kLoading) { ar.bytes(&x, 8); v = static_cast<T>(x); }
        else ar.bytes(&x, 8);
    } else {
        std::uint64_t x = static_cast<std::uint64_t>(v);
        if constexpr (Ar::kLoading) { ar.bytes(&x, 8); v = static_cast<T>(x); }
        else ar.bytes(&x, 8);
    }
}

// Element count prefix; on load, refuses counts that cannot fit in what is
// left (each element is >= 1 byte) so a corrupt count never allocates wildly.
template <class Ar> std::size_t io_count(Ar& ar, std::size_t n) {
    std::uint64_t c = n;
    if constexpr (Ar::kLoading) {
        ar.bytes(&c, 8);
        if (c > ar.remaining()) { ar.fail(); return 0; }
    } else {
        ar.bytes(&c, 8);
    }
    return static_cast<std::size_t>(c);
}

template <class Ar, class T> void io(Ar& ar, T& v) {
    if constexpr (std::is_same_v<Ar, Counter>) {
        ar.n += detail::total_extent<T>();
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
        io_scalar(ar, v);
    } else if constexpr (std::is_array_v<T>) {
        for (auto& e : v) io(ar, e);
    } else if constexpr (std::is_same_v<T, std::string>) {
        const std::size_t n = io_count(ar, v.size());
        if constexpr (Ar::kLoading) v.assign(n, '\0');
        if (n) ar.bytes(&v[0], n);
    } else if constexpr (detail::is_vector<T>::value) {
        using E = typename T::value_type;
        const std::size_t n = io_count(ar, v.size());
        if constexpr (Ar::kLoading) { v.clear(); v.resize(n); }
        if constexpr (std::is_same_v<E, bool>) {
            for (std::size_t i = 0; i < n; ++i) { bool b = v[i]; io(ar, b); v[i] = b; }
        } else if constexpr (std::is_same_v<E, std::uint8_t> || std::is_same_v<E, std::int8_t> ||
                             std::is_same_v<E, char>) {
            if (n) ar.bytes(v.data(), n);
        } else {
            for (std::size_t i = 0; i < n && ar.ok(); ++i) io(ar, v[i]);
        }
    } else if constexpr (detail::is_std_array<T>::value) {
        for (auto& e : v) io(ar, e);
    } else if constexpr (detail::is_map<T>::value) {
        const std::size_t n = io_count(ar, v.size());
        if constexpr (Ar::kLoading) {
            v.clear();
            for (std::size_t i = 0; i < n && ar.ok(); ++i) {
                typename T::key_type k{};
                typename T::mapped_type m{};
                io(ar, k);
                io(ar, m);
                v.emplace(std::move(k), std::move(m));
            }
        } else {
            for (auto& kv : v) {
                auto k = kv.first;
                io(ar, k);
                io(ar, kv.second);
            }
        }
    } else if constexpr (detail::is_pair<T>::value) {
        io(ar, v.first);
        io(ar, v.second);
    } else {
        fields(ar, v);   // the per-struct visitor (save_fields.hpp), found by ADL on Ar
    }
}

// The sync-hash policy helpers (see "W9-05: the SYNC-HASH POLICY" above).
template <class Ar, class T> void io_unhashed(Ar& ar, T& v) {
    if constexpr (!kHashing<Ar>) io(ar, v);
}
template <class Ar, class T> void io_hash_only(Ar& ar, T& v) {
    if constexpr (kHashing<Ar>) io(ar, v);
}

// --- World / map-trigger entry points (savegame.cpp) ------------------------
// save_world never mutates the World (the writer only reads) -- serialization
// is read-only w.r.t. simulation, so every byte-identity gate is untouched.
std::vector<std::uint8_t> save_world(const World& w);
// NET-PLAY: FNV-1a of each World member's save bytes, in visit order
// ("name" -> 32-bit hash); two machines' lists differ at the desynced member.
// Both follow the sync-hash policy (derived caches left out).
std::vector<std::pair<std::string, std::uint32_t>> world_field_hashes(const World& w);
// W9-05: the full-state sync hash of a World (+ the map-trigger runtime when
// `triggers` is set: campaign / co-op), FNV-1a 64 streamed over the visitors
// under the sync-hash policy -- no 8 MB buffer.
std::uint64_t world_sync_hash(const World& w, const MapTriggerSystem* triggers = nullptr);
// W9-05: the digest World::set_player_econ_rules keeps per house (hashed in
// place of the unsaved rules themselves).
std::uint64_t econ_rules_digest(const PlayerEconRules& r);
// The same, per field (and per unit def): the desync dump names what differs.
std::vector<std::pair<std::string, std::uint64_t>> econ_rules_field_digests(const PlayerEconRules& r);
// Overwrites EVERY serialized member of `w` (all but the MapFile pointer).
// `w` must have been constructed from the same map. Returns false on a
// truncated/corrupt blob (w is then in an unspecified-but-valid state).
bool load_world(World& w, const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> save_triggers(const MapTriggerSystem& s);
bool load_triggers(MapTriggerSystem& s, const std::vector<std::uint8_t>& bytes);

// FNV-1a 64 over a byte blob (the gate's full-state hash).
std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes,
                    std::uint64_t h = 0xcbf29ce484222325ull);

// --- the file container -----------------------------------------------------
enum class ScenarioKind : std::int32_t { Skirmish = 0, Campaign = 1, Demo = 2 };

struct SaveHeader {
    std::uint32_t version = kFormatVersion;  // read back from the file
    std::string description;                 // <= 80 chars (saveload.md §2)
    ScenarioKind kind = ScenarioKind::Skirmish;
    std::string map_file;                    // multiplayer map file ("" = default)
    std::string map_id;                      // scenario/map identity
    int map_w = 0, map_h = 0, map_cells = 0; // map signature (mismatch -> refuse)
    std::int64_t frame = 0;                  // game logic frame at save time
    std::int64_t unix_time = 0;              // wall clock at save (display only)
};

constexpr std::uint32_t tag(char a, char b, char c, char d) {
    return std::uint32_t(std::uint8_t(a)) | std::uint32_t(std::uint8_t(b)) << 8 |
           std::uint32_t(std::uint8_t(c)) << 16 | std::uint32_t(std::uint8_t(d)) << 24;
}
constexpr std::uint32_t kTagConfig = tag('C', 'O', 'N', 'F');
constexpr std::uint32_t kTagWorld = tag('W', 'R', 'L', 'D');
constexpr std::uint32_t kTagTriggers = tag('T', 'R', 'I', 'G');
constexpr std::uint32_t kTagScene = tag('S', 'C', 'N', 'E');
constexpr std::uint32_t kTagHost = tag('H', 'O', 'S', 'T');
// R-180 (v16): the sidebar's waiting unit copies + the queues on hold
// (save_fields.hpp QueueBacklogSave). Optional: absent = nothing waiting.
constexpr std::uint32_t kTagBacklog = tag('Q', 'B', 'K', 'L');
// W5-E: the presentation audio state -- "<theme section>\n<1|0 music on>\n".
constexpr std::uint32_t kTagAudio = tag('A', 'U', 'D', 'I');
// W6-06-04 (v25): the particle systems campaign triggers started (action 88
// ParticleAnim) that are still alive -- one "<system> <x> <y> <z>\n" line
// each, world leptons. Optional: absent = none (skirmish never writes it).
constexpr std::uint32_t kTagParticles = tag('P', 'F', 'X', ' ');

struct SaveFile {
    SaveHeader header;
    std::map<std::uint32_t, std::vector<std::uint8_t>> sections;
    const std::vector<std::uint8_t>* section(std::uint32_t t) const {
        auto it = sections.find(t);
        return it == sections.end() ? nullptr : &it->second;
    }
};

constexpr std::size_t kMaxDescription = 80;   // 0x50, saveload.md §2

std::vector<std::uint8_t> encode_save(const SaveFile& f);
// Parses magic + version + header (+ sections unless header_only). Returns
// false for a non-save / truncated file. A WRONG-VERSION file parses its
// magic+version only: header.version != kFormatVersion and sections empty.
bool decode_save(const std::vector<std::uint8_t>& bytes, SaveFile& out, bool header_only = false);

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes);
bool read_file_bytes(const std::string& path, std::vector<std::uint8_t>& out);

// --- slots (saveload.md §2) ---------------------------------------------------
struct SlotEntry {
    std::string path;          // full path
    std::string file_name;     // SAVE1A2B.SAV
    SaveHeader header;
    bool wrong_version = false;
    // W2-AR-20/-21: the file is a *.SAV but unreadable -- bad magic, torn, or
    // its CRC-32 trailer does not match (listed, never loaded).
    bool damaged = false;
    std::int64_t mtime = 0;    // file modification time (sort key)
};
// W2-AR-20: CRC-32 (IEEE 802.3, reflected 0xEDB88320) -- the save trailer.
std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0);
// Every *.SAV in `dir`, newest first (FileEntryClass::Compare); unreadable
// ones are flagged `damaged` (W2-AR-21) instead of silently left out.
std::vector<SlotEntry> list_saves(const std::string& dir);
// "SAVE%04X.SAV" with a pseudo-random 16-bit number (seeded by `seed`),
// retried until no such file exists in `dir`.
std::string new_save_name(const std::string& dir, std::uint32_t seed);
bool delete_save(const std::string& path);
// Clamp to 80 chars; strips control chars. Empty => refused by the caller.
std::string clean_description(const std::string& s);

}  // namespace save
}  // namespace ra2engine

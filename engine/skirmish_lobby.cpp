#include "skirmish_lobby.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace ra2engine {

namespace {

constexpr const char* kSk = "Skirmish";
constexpr const char* kMp = "MultiPlayer";

// The 0x006388A0-style Random(lo,hi) contract over a small LCG (same
// constants as gf_start.inc's GfRng so a seed means the same everywhere).
struct LobbyRng {
    std::uint32_t s;
    explicit LobbyRng(std::uint32_t seed) : s(seed ? seed : 0x2545F491u) {}
    int range(int lo, int hi) {
        s = s * 1664525u + 1013904223u;
        if (hi <= lo) return lo;
        return lo + static_cast<int>((s >> 8) % static_cast<std::uint32_t>(hi - lo + 1));
    }
};

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

bool valid_row(int row) { return row >= 0 && row < kLobbyRows; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int country_index(const std::vector<std::string>& names, const std::string& v) {
    const std::string l = lower(v);
    for (std::size_t i = 0; i < names.size(); ++i)
        if (lower(names[i]) == l) return static_cast<int>(i);
    return -1;
}

// "a,b,c" -> three ints (FUN_0046C880: missing fields keep their defaults).
void parse_triple(const std::string& v, int& a, int& b, int& c) {
    int* out[3] = {&a, &b, &c};
    std::size_t i = 0;
    for (int k = 0; k < 3 && i <= v.size(); ++k) {
        const std::size_t comma = v.find(',', i);
        const std::string f = v.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
        bool any = false;
        for (char ch : f) if (!std::isspace(static_cast<unsigned char>(ch))) any = true;
        if (any) *out[k] = ww_parse_int(f, *out[k]);
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
}

}  // namespace

// ------------------------------------------------------------- mappings --

int lobby_ai_to_slot_type(int ai) {
    switch (ai) {
        case kLobbyAiBrutal: return 4;
        case kLobbyAiMedium: return 5;
        case kLobbyAiEasy:   return 6;
        case kLobbyAiDebug:  return 7; // engine-local extension, not a retail slot type
        default:             return 1;
    }
}

int lobby_slot_type_to_ai(int type) {
    switch (type) {
        case 4:  return kLobbyAiBrutal;
        case 5:  return kLobbyAiMedium;
        case 6:  return kLobbyAiEasy;
        case 7:  return kLobbyAiDebug;
        default: return kLobbyAiNone;
    }
}

AiDifficulty lobby_ai_difficulty(int ai) {
    switch (ai) {
        case kLobbyAiBrutal: return AiDifficulty::Difficult;
        case kLobbyAiEasy:   return AiDifficulty::Easy;
        case kLobbyAiDebug:  return AiDifficulty::Debug;
        default:             return AiDifficulty::Normal;
    }
}

int lobby_color_to_house(int c) {
    static const int k[kLobbyColors] = {0, 1, 5, 7, 2, 6, 4, 3};
    return (c >= 0 && c < kLobbyColors) ? k[c] : 0;
}

void lobby_color_rgb(int c, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    static const std::uint8_t k[kLobbyColors][3] = {
        {221, 226, 13}, {255, 25, 25}, {42, 116, 226}, {62, 209, 46},
        {255, 160, 25}, {50, 215, 230}, {149, 40, 189}, {255, 154, 235}};
    if (c < 0 || c >= kLobbyColors) { r = g = b = 96; return; }
    r = k[c][0]; g = k[c][1]; b = k[c][2];
}

// ------------------------------------------------------------ defaults --

LobbyState lobby_first_run(const LobbyRulesInfo& ru) {
    LobbyState st;
    st.game_speed = clampi(ru.game_speed, 0, 6);
    st.credits = clampi(ru.money, ru.min_money, ru.max_money);
    st.unit_count = clampi(ru.unit_count, ru.min_units, ru.max_units);
    st.short_game = ru.short_game;
    st.superweapons = ru.superweapons;
    st.build_off_ally = ru.build_off_ally;
    st.mcv_repacks = ru.mcv_repacks;
    st.crates = ru.crates;
    st.rows[0].country = 0;          // [RE-FALLBACK] America
    st.rows[0].color = 0;            // [RE-FALLBACK] Gold
    st.rows[1].ai = kLobbyAiEasy;    // FUN_0066F1F0(..., 1, 6): Slot01 = Easy
    return st;
}

// --------------------------------------------------------------- INI ---

std::string lobby_handle_encode(const std::string& name) {
    std::string out;
    char b[8];
    for (unsigned char ch : name) {
        std::snprintf(b, sizeof b, "%x,", static_cast<unsigned>(ch));
        out += b;
    }
    return out;
}

std::string lobby_handle_decode(const std::string& v) {
    std::string out;
    std::size_t i = 0;
    while (i < v.size()) {
        std::size_t comma = v.find(',', i);
        if (comma == std::string::npos) comma = v.size();
        std::string f = v.substr(i, comma - i);
        f.erase(std::remove_if(f.begin(), f.end(),
                               [](char c) { return std::isspace(static_cast<unsigned char>(c)); }),
                f.end());
        if (!f.empty()) {
            const long ch = std::strtol(f.c_str(), nullptr, 16);
            if (ch > 0 && ch < 256) out += static_cast<char>(ch);
        }
        i = comma + 1;
    }
    if (out.size() > static_cast<std::size_t>(kLobbyNameMax)) out.resize(kLobbyNameMax);
    return out;
}

LobbyState lobby_load(const IniDoc& d, const LobbyState& base, const LobbyRulesInfo& ru,
                      const std::vector<std::string>& countries) {
    LobbyState st = base;
    const int nc = static_cast<int>(countries.size());
    auto rd_int = [&](const char* s, const char* k, int def) {
        const auto v = d.get(s, k);
        return v ? ww_parse_int(*v, def) : def;
    };
    auto rd_bool = [&](const char* s, const char* k, bool def) {
        const auto v = d.get(s, k);
        return v ? ww_parse_bool(*v, def) : def;
    };
    if (const auto h = d.get(kMp, "Handle")) st.name = lobby_handle_decode(*h);
    // Human colour/country (0x0066F3A0): ColorEx/SideEx == -2 means Random.
    if (rd_int(kMp, "ColorEx", -1) == kLobbyRandom) st.rows[0].color = kLobbyRandom;
    else st.rows[0].color = rd_int(kMp, "Color", st.rows[0].color);
    if (rd_int(kMp, "SideEx", -1) == kLobbyRandom) {
        st.rows[0].country = kLobbyRandom;
    } else if (const auto s = d.get(kMp, "Side")) {
        const int ix = country_index(countries, *s);
        st.rows[0].country = ix >= 0 ? ix : ww_parse_int(*s, st.rows[0].country);
    }
    st.game_mode = rd_int(kSk, "GameMode", st.game_mode);
    st.scen_index = rd_int(kSk, "ScenIndex", st.scen_index);
    if (const auto f = d.get(kSk, "ScenFile")) st.scen_file = *f;
    st.game_speed = clampi(rd_int(kSk, "GameSpeed", st.game_speed), 0, 6);
    st.credits = clampi(rd_int(kSk, "Credits", st.credits), ru.min_money, ru.max_money);
    st.unit_count = clampi(rd_int(kSk, "UnitCount", st.unit_count), ru.min_units, ru.max_units);
    st.short_game = rd_bool(kSk, "ShortGame", st.short_game);
    st.superweapons = rd_bool(kSk, "SuperWeaponsAllowed", st.superweapons);
    st.build_off_ally = rd_bool(kSk, "BuildOffAlly", st.build_off_ally);
    st.mcv_repacks = rd_bool(kSk, "MCVRepacks", st.mcv_repacks);
    st.crates = rd_bool(kSk, "CratesAppear", st.crates);
    for (int row = 1; row < kLobbyRows; ++row) {
        char key[16];
        std::snprintf(key, sizeof key, "Slot%02d", row);
        const auto v = d.get(kSk, key);
        if (!v) continue;
        int type = lobby_ai_to_slot_type(st.rows[row].ai), c = kLobbyRandom, col = kLobbyRandom;
        parse_triple(*v, type, c, col);
        st.rows[row].ai = lobby_slot_type_to_ai(type);
        st.rows[row].country = c;
        st.rows[row].color = col;
    }
    for(int row=0;row<kLobbyRows;++row) {
        st.rows[row].spawn=rd_int(kSk,("Spawn"+std::to_string(row)).c_str(),-1);
        if(st.rows[row].spawn<0 || st.rows[row].spawn>=8)st.rows[row].spawn=-1;
        const std::string key="Team"+std::to_string(row);
        st.rows[row].team=rd_int(kSk,key.c_str(),-1);
        if(st.rows[row].team<0 || st.rows[row].team>=kLobbyTeams) st.rows[row].team=-1;
        if(row>0 && st.rows[row].ai==kLobbyAiNone) st.rows[row].team=-1;
    }
    // Sanitize: out-of-range -> Random; None rows hold -2/-2; duplicate
    // colours keep the first (lowest row) owner.
    bool used[kLobbyColors] = {};
    for (int row = 0; row < kLobbyRows; ++row) {
        LobbyRow& r = st.rows[row];
        if (row > 0 && r.ai == kLobbyAiNone) { r.country = kLobbyRandom; r.color = kLobbyRandom; continue; }
        if (r.country != kLobbyRandom && (r.country < 0 || r.country >= nc)) r.country = kLobbyRandom;
        if (r.color != kLobbyRandom && (r.color < 0 || r.color >= kLobbyColors)) r.color = kLobbyRandom;
        if (r.color >= 0) {
            if (used[r.color]) r.color = kLobbyRandom;
            else used[r.color] = true;
        }
    }
    if (st.name.size() > static_cast<std::size_t>(kLobbyNameMax)) st.name.resize(kLobbyNameMax);
    return st;
}

void lobby_store(const LobbyState& st, IniDoc& d, const std::vector<std::string>& countries) {
    const LobbyRow& h = st.rows[0];
    d.set(kMp, "Handle", lobby_handle_encode(st.name));
    // FUN_00672AE0 / FUN_00672A30 with "do not resolve": Random stores 0 + Ex=-2.
    d.set(kMp, "Color", ww_format_int(h.color >= 0 ? h.color : 0));
    d.set(kMp, "ColorEx", ww_format_int(h.color >= 0 ? -1 : kLobbyRandom));
    const int side_ix = h.country >= 0 ? h.country : 0;
    d.set(kMp, "Side", side_ix < static_cast<int>(countries.size())
                           ? countries[static_cast<std::size_t>(side_ix)]
                           : ww_format_int(side_ix));
    d.set(kMp, "SideEx", ww_format_int(h.country >= 0 ? -1 : kLobbyRandom));
    d.set(kMp, "GameMode", ww_format_int(st.game_mode));
    d.set(kSk, "GameMode", ww_format_int(st.game_mode));
    d.set(kSk, "ScenIndex", ww_format_int(st.scen_index));
    d.set(kSk, "GameSpeed", ww_format_int(st.game_speed));
    d.set(kSk, "Credits", ww_format_int(st.credits));
    d.set(kSk, "UnitCount", ww_format_int(st.unit_count));
    d.set(kSk, "ShortGame", ww_format_bool(st.short_game));
    d.set(kSk, "SuperWeaponsAllowed", ww_format_bool(st.superweapons));
    d.set(kSk, "BuildOffAlly", ww_format_bool(st.build_off_ally));
    d.set(kSk, "MCVRepacks", ww_format_bool(st.mcv_repacks));
    d.set(kSk, "CratesAppear", ww_format_bool(st.crates));
    for (int row = 1; row < kLobbyRows; ++row) {
        char key[16], val[48];
        std::snprintf(key, sizeof key, "Slot%02d", row);
        const LobbyRow& r = st.rows[row];
        std::snprintf(val, sizeof val, "%d,%d,%d", lobby_ai_to_slot_type(r.ai), r.country, r.color);
        d.set(kSk, key, val);
    }
    for(int row=0;row<kLobbyRows;++row)
        d.set(kSk,"Team"+std::to_string(row),ww_format_int(lobby_row_active(st,row) ? st.rows[row].team : -1));
    for(int row=0;row<kLobbyRows;++row)d.set(kSk,"Spawn"+std::to_string(row),ww_format_int(st.rows[row].spawn));
    if (!st.scen_file.empty()) d.set(kSk, "ScenFile", st.scen_file);
}

// ----------------------------------------------------------- row model --

bool lobby_row_active(const LobbyState& st, int row) {
    if (!valid_row(row)) return false;
    return row == 0 || st.rows[row].ai != kLobbyAiNone;
}

int lobby_ai_count(const LobbyState& st) {
    int n = 0;
    for (int row = 1; row < kLobbyRows; ++row)
        if (st.rows[row].ai >= kLobbyAiBrutal && st.rows[row].ai <= kLobbyAiDebug) ++n;
    return n;
}

int lobby_visible_rows(int map_max) { return clampi(map_max, 1, kLobbyRows); }

int lobby_color_owner(const LobbyState& st, int color) {
    if (color < 0 || color >= kLobbyColors) return -1;
    for (int row = 0; row < kLobbyRows; ++row)
        if (lobby_row_active(st, row) && st.rows[row].color == color) return row;
    return -1;
}

std::vector<int> lobby_color_choices(const LobbyState& st, int row) {
    std::vector<int> out{kLobbyRandom};
    for (int c = 0; c < kLobbyColors; ++c) {
        const int owner = lobby_color_owner(st, c);
        if (owner < 0 || owner == row) out.push_back(c);
    }
    return out;
}

void lobby_set_ai(LobbyState& st, int row, int ai) {
    if (row <= 0 || row >= kLobbyRows) return;
    if (ai < kLobbyAiNone || ai > kLobbyAiDebug) ai = kLobbyAiNone;
    st.rows[row].ai = ai;
    if (ai == kLobbyAiNone) {
        st.rows[row].country = kLobbyRandom;
        st.rows[row].color = kLobbyRandom;
        st.rows[row].team = -1;
    }
}

bool lobby_set_color(LobbyState& st, int row, int color) {
    if (!lobby_row_active(st, row)) return false;
    if (color == kLobbyRandom) { st.rows[row].color = kLobbyRandom; return true; }
    if (color < 0 || color >= kLobbyColors) return false;
    const int owner = lobby_color_owner(st, color);
    if (owner >= 0 && owner != row) return false;
    st.rows[row].color = color;
    return true;
}

void lobby_set_country(LobbyState& st, int row, int country) {
    if (!lobby_row_active(st, row)) return;
    st.rows[row].country = country < 0 ? kLobbyRandom : country;
}

void lobby_apply_max_players(LobbyState& st, int map_max) {
    const int vis = lobby_visible_rows(map_max);
    for (int row = std::max(1, vis); row < kLobbyRows; ++row) lobby_set_ai(st, row, kLobbyAiNone);
}

LobbyCheck lobby_validate(const LobbyState& st, int map_max) {
    const int players = lobby_ai_count(st) + 1;
    if (map_max < players) return LobbyCheck::ScenarioTooSmall;
    if (players < 2) return LobbyCheck::NeedTwoPlayers;
    bool enemy=st.rows[0].team<0 || st.rows[0].team>=kLobbyTeams;
    for(int row=1;row<kLobbyRows;++row)
        if(lobby_row_active(st,row) && st.rows[row].team!=st.rows[0].team) enemy=true;
    if(!enemy) return LobbyCheck::NeedEnemy;
    return LobbyCheck::Ok;
}

// ----------------------------------------------------------- converter --

std::vector<LobbyResolved> lobby_resolve(const LobbyState& st, const LobbyConvertCtx& ctx) {
    LobbyRng rng(ctx.seed);
    const int nc = std::max(1, static_cast<int>(ctx.countries.size()));
    std::vector<LobbyResolved> out;
    // Colours already claimed explicitly by any active row.
    bool owned[kLobbyColors] = {};
    for (int row = 0; row < kLobbyRows; ++row)
        if (lobby_row_active(st, row) && st.rows[row].color >= 0 && st.rows[row].color < kLobbyColors)
            owned[st.rows[row].color] = true;
    for (int row = 0; row < kLobbyRows; ++row) {
        if (!lobby_row_active(st, row)) continue;
        const LobbyRow& r = st.rows[row];
        LobbyResolved z;
        z.row = row;
        if(ctx.country_allowed) {
            std::vector<int> allowed;
            for(int c=0;c<static_cast<int>(ctx.countries.size());++c)
                if(ctx.country_allowed(row,ctx.countries[c])) allowed.push_back(c);
            if(allowed.empty()) throw std::runtime_error("No supported country for lobby row");
            z.country=std::find(allowed.begin(),allowed.end(),r.country)!=allowed.end() ? r.country :
                allowed[rng.range(0,static_cast<int>(allowed.size())-1)];
        } else z.country = (r.country >= 0 && r.country < nc) ? r.country : rng.range(0, nc - 1);
        if (r.color >= 0 && r.color < kLobbyColors) {
            z.color = r.color;
        } else {
            bool any_free = false;
            for (bool o : owned) any_free = any_free || !o;
            int c = rng.range(0, kLobbyColors - 1);
            while (any_free && owned[c]) c = rng.range(0, kLobbyColors - 1);
            z.color = c;
            owned[c] = true;
        }
        out.push_back(z);
    }
    return out;
}

SkirmishConfig lobby_to_config(const LobbyState& st, const LobbyConvertCtx& ctx) {
    SkirmishConfig cfg;
    cfg.map_file = ctx.map_file;
    cfg.use_map_starts = !ctx.map_file.empty();
    cfg.starting_cash = static_cast<double>(st.credits);
    cfg.unit_count = st.unit_count;
    cfg.tech_level = ctx.tech_level;
    cfg.crates = st.crates;
    cfg.superweapons = st.superweapons;
    cfg.short_game = st.short_game;
    cfg.game_speed = st.game_speed;
    cfg.mcv_redeploys = st.mcv_repacks ? 1 : 0;
    for (const LobbyResolved& z : lobby_resolve(st, ctx)) {
        SkirmishSlot s;
        s.country = z.country < static_cast<int>(ctx.countries.size())
                        ? ctx.countries[static_cast<std::size_t>(z.country)] : std::string();
        s.side = ctx.side_of ? ctx.side_of(s.country) : World::Side::Allied;
        s.color_ix = lobby_color_to_house(z.color);
        const int team=st.rows[z.row].team;
        s.alliance_team = team>=0 && team<kLobbyTeams ? team : -1;
        s.combatant = true;
        s.is_ai = z.row != 0;
        s.diff = s.is_ai ? lobby_ai_difficulty(st.rows[z.row].ai) : AiDifficulty::Normal;
        cfg.slots.push_back(s);
    }
    // The hidden Neutral civilian house every skirmish World carries.
    cfg.slots.push_back(SkirmishSlot{World::Side::Neutral, 2, "Civilian", -1, false, false,
                                     AiDifficulty::Normal});
    return cfg;
}

}  // namespace ra2engine

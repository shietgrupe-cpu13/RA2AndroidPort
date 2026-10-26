#pragma once
// The Skirmish setup page (dialog 0x102) as a PURE state model: the 8 fixed
// player rows, the game options, the RA2.INI [Skirmish]/[MultiPlayer]
// persistence, the start-time validation and the converter into the
// launcher's SkirmishConfig. No SDL, no assets: compiled into both ra2engine
// and ra2assets_tests (same pattern as settings.cpp / skirmish.cpp).
//
// Calibration authority: docs/gap/07-skirmish-page.md §1 (layout + behaviour)
// and the game.exe 1.006 decompiles it cites:
//   0x00683FC0  init: AI combo item data, INI slot type -> combo value
//   0x006829F0  Start/Back commit: combo value -> slot type, GameSpeed = 6-pos
//               written to OptionsClass+0 (0x00A40B18) as well, validation
//   0x0066F1F0  [Skirmish] read: keys, defaults, SlotNN = type,country,color
//               (Slot01 defaults to 6 = Easy, Slot02..07 to 1 = None)
//   0x0066F3A0 / 0x00670380  [MultiPlayer] Handle/Color/ColorEx/Side/SideEx
//   0x00672A30 / 0x00672AE0  human Side/Color: -2 stores SideEx/ColorEx = -2
//   0x00672BB0  Random resolution at launch (country: uniform over Multiplay
//               countries; colour: uniform 0..7 re-rolled while owned)
//   0x004D2080 / 0x004D2700  exclusive colour ownership (table 0x008660E0)
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "settings.hpp"
#include "skirmish.hpp"

namespace ra2engine {

constexpr int kLobbyRows = 8;        // row 0 = the human (name edit), rows 1..7 = AI combos
constexpr int kLobbyRandom = -2;     // country/colour "Random (???)" / "???"
constexpr int kLobbyTeams = 11;
inline std::string lobby_team_label(int team) { return team>=0 && team<kLobbyTeams?std::string(1,char('A'+team)):"-"; }
constexpr int kLobbyColors = 8;      // Gold Red Blue Green Orange Sky Purple Pink

// AI combo item data (FUN_00683FC0): None -1, Brutal 0, Medium 1, Easy 2.
enum : int { kLobbyAiNone = -1, kLobbyAiBrutal = 0, kLobbyAiMedium = 1, kLobbyAiEasy = 2, kLobbyAiDebug = 3 };

// [Skirmish] SlotNN type <-> combo value (0x683FC0 / 0x6829F0):
// None 1, Brutal 4, Medium 5, Easy 6. Unknown types read as None.
int lobby_ai_to_slot_type(int ai);
int lobby_slot_type_to_ai(int type);

// The combo value is house+0x184 (the DifficultyClass index, 0 = hardest);
// AiDifficulty is the lobby choice with class = 2 - choice (G-63), so the
// mapping is choice = 2 - value: Brutal -> Difficult, Medium -> Normal,
// Easy -> Easy.
AiDifficulty lobby_ai_difficulty(int ai);

// Lobby colour index (0x7E6D94 order: Gold, Red, Blue, Green, Orange, Sky,
// Purple, Pink) -> the clone's [Colors] house colour index (world.cpp:
// Gold, DarkRed, Orange, Magenta, Purple, DarkBlue, DarkSky, DarkGreen).
// [ANCHORED by RGB/name, SK-11]
int lobby_color_to_house(int lobby_color);
// RGB of the lobby swatch (CONFIRMED dword table 0x7E6D94).
void lobby_color_rgb(int lobby_color, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b);

struct LobbyRow {
    int ai = kLobbyAiNone;          // rows 1..7 only; row 0 is always the human
    int country = kLobbyRandom;     // index into the country list, or -2
    int color = kLobbyRandom;       // lobby colour 0..7, or -2
    int spawn = -1; // explicit map waypoint index, -1 random
    int team = -1;                  // -1 free-for-all; 0..10 teams (launcher extension)
};

struct LobbyState {
    std::string name;               // [MultiPlayer] Handle, max 19 chars (EM_LIMITTEXT 0x13)
    LobbyRow rows[kLobbyRows];
    int  game_mode = 1;             // [Skirmish] GameMode (1 = Battle)
    int  scen_index = 0;            // [Skirmish] ScenIndex
    std::string scen_file;          // clone key [Skirmish] ScenFile (round-trip aid, Q8)
    int  game_speed = 1;            // 0 fastest .. 6 slowest (slider pos = 6 - speed)
    int  credits = 10000;
    int  unit_count = 10;
    bool short_game = true;
    bool superweapons = true;
    bool build_off_ally = true;     // greyed in RA2: persisted untouched
    bool mcv_repacks = true;
    bool crates = true;
};

// The rules side of the page: [MultiplayerDialogSettings] defaults + slider
// ranges (read by main.cpp from rulesmd; the pure layer never sees Rules).
struct LobbyRulesInfo {
    int  game_speed = 1;
    int  money = 10000, min_money = 5000, max_money = 10000, money_step = 100;
    int  unit_count = 10, min_units = 0, max_units = 10;
    bool short_game = true, superweapons = true, build_off_ally = true;
    bool mcv_repacks = true, crates = true;
    int  tech_level = 10;
};

constexpr int kLobbyNameMax = 19;

// First-run page: rules defaults, Slot01 = Easy / Random / Random (the INI
// reader's own default, FUN_0066F1F0(..., 1, 6)), every other AI row None.
// [RE-FALLBACK: the human's first-run country/colour (no [MultiPlayer] keys)
// are America / Gold.]
LobbyState lobby_first_run(const LobbyRulesInfo& rules);

// RA2.INI. Absent keys keep `base` (normally lobby_first_run). Values are
// clamped to the rules ranges; colours/countries out of range read as -2;
// duplicate colours are released (the page never shows two owners).
// `country_names` is the country list the indices refer to (Side= is stored
// by name).
LobbyState lobby_load(const IniDoc& ini, const LobbyState& base, const LobbyRulesInfo& rules,
                      const std::vector<std::string>& country_names);
void lobby_store(const LobbyState& st, IniDoc& ini, const std::vector<std::string>& country_names);

// Hex-byte Handle encoding ("Tanya" <-> "54,61,6e,79,61,").
std::string lobby_handle_encode(const std::string& name);
std::string lobby_handle_decode(const std::string& v);

// --- row model ---
bool lobby_row_active(const LobbyState& st, int row);       // row 0, or an AI row not None
int  lobby_ai_count(const LobbyState& st);                  // AI rows in {0,1,2}
int  lobby_visible_rows(int map_max_players);               // clamp(max, 1..8)
// Owner row of a lobby colour (-1 = free). Only active rows own colours.
int  lobby_color_owner(const LobbyState& st, int color);
// The colour combo of `row`: "???" (-2) then the colours not owned by any
// OTHER row, in lobby order (FUN_004D2080).
std::vector<int> lobby_color_choices(const LobbyState& st, int row);
// "None" releases the row (country/colour back to -2, controls disabled);
// any AI level re-enables it (keeping -2 values).
void lobby_set_ai(LobbyState& st, int row, int ai);
// Claim a colour; refused (false) when another row owns it.
bool lobby_set_color(LobbyState& st, int row, int color);
void lobby_set_country(LobbyState& st, int row, int country);
// A map change: rows >= the map's max are closed (None) and hidden.
void lobby_apply_max_players(LobbyState& st, int map_max_players);

// Start validation (0x6829F0): map max < AI+1 -> TXT_SCENARIO_TOO_SMALL;
// AI+1 < 2 -> TXT_NEED_AT_LEAST_TWO_PLAYERS.
enum class LobbyCheck { Ok, ScenarioTooSmall, NeedTwoPlayers, NeedEnemy };
LobbyCheck lobby_validate(const LobbyState& st, int map_max_players);

// The converter's environment.
struct LobbyConvertCtx {
    std::vector<std::string> countries;                       // index -> rules country id
    std::function<World::Side(const std::string&)> side_of;  // rules Side= of a country
    std::uint32_t seed = 1;                                   // Random resolution RNG
    int tech_level = 10;                                      // rules TechLevel (no page control)
    std::string map_file;                                     // "" = engine default map
    std::function<bool(int,const std::string&)> country_allowed; // optional per-row capability filter
};

// The resolved per-row picks (exposed for tests / the probe).
struct LobbyResolved {
    int row = 0;
    int country = 0;    // index into ctx.countries
    int color = 0;      // lobby colour 0..7
};

// Random resolution (FUN_00672BB0): the human first, then AI rows in order;
// country -2 -> uniform over the list; colour -2 -> uniform 0..7 re-rolled
// while any player (human, explicit or already resolved) owns it.
std::vector<LobbyResolved> lobby_resolve(const LobbyState& st, const LobbyConvertCtx& ctx);

// LobbyState -> SkirmishConfig: slot 0 = the human, then one AI slot per
// active row (row order), then the hidden Neutral civilian slot the game
// depends on (default_skirmish_config's third house). Team assignments are
// a launcher extension; -1 retains free-for-all. use_map_starts,
// tech_level from the rules, game_speed / mcv_redeploys from the page.
SkirmishConfig lobby_to_config(const LobbyState& st, const LobbyConvertCtx& ctx);

}  // namespace ra2engine

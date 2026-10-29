#pragma once
// P8 Options + persistence: the pure (SDL-free) settings layer, compiled into
// both ra2engine and ra2assets_tests from this single source (same pattern as
// repair_sell.hpp / audio_mixer.hpp).
//
// Calibration authority: docs/re/options.md
//   §3  OptionsClass @0x00A40B18 -- the RA2.INI [Options]/[Video]/[Audio] keys,
//       their ctor defaults (0x005D8840), the load clamps (FUN_005d8b00), the
//       save list/order (FUN_005d91b0) and the Westwood INIClass value
//       semantics (read: FUN_0050d7f0 / FUN_0050f410 / FUN_0050e3a0;
//       write: "%d" / "yes"|"no" / "%f").
//   §2  the options page's slider <-> value math (DLGPROC 0x00542590 + the
//       commit routine FUN_00542280).
//   §4  the skinned trackbar geometry (subclass proc 0x005FB270).
//   §5  the edge-scroll distance table (0x7F2BE8) and the linear volume law.
#include <optional>
#include <string>
#include <vector>

namespace ra2engine {

// A case-preserving, order-preserving INI document. The repo's ra2assets::Ini
// upper-cases names and is read-only, so it cannot rewrite a user's RA2.INI
// without destroying it; this model keeps every line verbatim (comments, blank
// lines, foreign sections such as [MultiPlayer]/[Skirmish], line endings) so
// that serialize(parse(t)) == t, and set() touches only the one key it writes.
class IniDoc {
public:
    static IniDoc parse(const std::string& text);
    std::string serialize() const;

    // Value of the last occurrence of `key` within `section` (both matched
    // case-insensitively; a section may appear more than once); the value is
    // trimmed and cut at a ';' comment. nullopt when absent.
    std::optional<std::string> get(const std::string& section, const std::string& key) const;

    // Rewrite the existing key's value in place (keeping its spelling), else
    // append `key=value` after the section's last non-blank line, else append
    // a new section at the end (separated by a blank line). New lines use the
    // document's line ending (CRLF for an empty document, like RA2.INI).
    void set(const std::string& section, const std::string& key, const std::string& value);

private:
    struct Line {
        std::string text;   // without the line ending
        std::string eol;    // "\r\n", "\n" or "" (last line, no newline)
    };
    std::vector<Line> lines_;
    std::string eol_() const;
};

// Westwood INIClass value semantics (options.md §3).
int         ww_parse_int(const std::string& v, int def);        // $hex, ...h hex, else atoi
bool        ww_parse_bool(const std::string& v, bool def);      // 1/T/Y, 0/F/N, else def
double      ww_parse_double(const std::string& v, double def);  // %f; '%' -> x0.01
std::string ww_format_int(int v);                               // "%d"
std::string ww_format_bool(bool v);                             // "yes" / "no"
std::string ww_format_double(double v);                         // "%f"

// OptionsClass. Defaults = the ctor at 0x005D8840 (options.md §3).
struct Settings {
    // [Options]
    int  game_speed = 3;
    int  difficulty = 1;
    int  scroll_method = 0;
    int  scroll_rate = 3;          // 0 = fastest .. 6 = slowest
    bool auto_scroll = true;
    int  detail_level = 2;
    bool sidebar_cameo_text = true;
    int sidebar_scale = 100;
    int sidebar_cells = 0; // Auto, or an even number of cameo boxes (6..60).
    bool unit_action_lines = true; // "Target Lines"
    bool show_hidden = false;
    bool tooltips = true;
    bool autosave_enabled = true;
    int autosave_interval_minutes = 5;
    // [Video]
    int  screen_width = -1;
    int  screen_height = -1;
    bool stretch_movies = false;
    bool display_override = false;
    bool widescreen = true;
    int hud_text_size = 100;       // in-game game.fnt scale percent; independent of display resolution
    bool allow_hires_modes = false;   // load-only (never saved by the original)
    bool allow_vram_sidebar = false;  // load-only
    // [Audio]
    float sound_volume = 0.7f;
    float voice_volume = 1.0f;
    float score_volume = 0.5f;
    bool  is_score_repeat = false;
    bool  is_score_shuffle = false;
    int   sound_latency = 9;
    bool  in_game_music = true;

    bool operator==(const Settings& o) const;
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

// FUN_005d8b00: absent keys keep the defaults; clamps Difficulty [0,4],
// DetailLevel [0,2], volumes min(v, 1.0) (no lower clamp).
Settings load_settings(const IniDoc& ini);
// FUN_005d91b0: writes exactly the original's save list, in its order.
void store_settings(const Settings& s, IniDoc& ini);

// --- options page control math (options.md §2) ---
int   volume_to_slider(float v);        // ftol(v*10 + 0.5), clamped 0..10
float slider_to_volume(int pos);        // pos * 0.1
int   scroll_rate_to_slider(int rate);  // 6 - rate
int   slider_to_scroll_rate(int pos);   // 6 - pos
int   detail_to_slider(int detail);     // detail != 0
int   slider_to_detail(int pos);        // pos ? 2 : 0

// --- skinned trackbar geometry (options.md §4, proc 0x5FB270). Positions are
// relative to the range minimum (all options sliders have min 0). ---
constexpr int kTrackbarReadoutW = 50;   // 0x32: the numeric readout box
constexpr int kTrackbarThumbW = 12;     // trakgrip.pcx
int trackbar_usable_w(int client_w, int readout_w);
int trackbar_thumb_x(int pos, int range, int client_w, int readout_w);   // client x of thumb
int trackbar_pos_from_x(int x, int range, int client_w, int readout_w);  // click/drag mapping

// --- edge scroll (options.md §5, FUN_00669ec0, table 0x7F2BE8) ---
int edge_scroll_distance(int scroll_rate, int accel, double scroll_multiplier);
int edge_scroll_accel_cap(int scroll_rate);   // accel is pinned at 7 - rate

// --- volume law (options.md §5): linear, permille for the Mixer bus gain ---
int volume_gain_permille(float v);

}  // namespace ra2engine

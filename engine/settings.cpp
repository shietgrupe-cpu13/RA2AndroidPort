#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ra2engine {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// "[Name]" -> true + name (trimmed). Anything else -> false.
bool section_header(const std::string& line, std::string& name) {
    const std::string t = trim(line);
    if (t.size() < 2 || t.front() != '[') return false;
    const size_t close = t.find(']');
    if (close == std::string::npos) return false;
    name = trim(t.substr(1, close - 1));
    return true;
}

// "key=value" (not a comment) -> true + trimmed key + raw value text.
bool key_line(const std::string& line, std::string& key, std::string& value) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') return false;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = trim(line.substr(0, eq));
    if (key.empty()) return false;
    value = line.substr(eq + 1);
    return true;
}

std::string clean_value(const std::string& raw) {
    const size_t semi = raw.find(';');
    return trim(semi == std::string::npos ? raw : raw.substr(0, semi));
}

}  // namespace

// ------------------------------------------------------------------ IniDoc --

IniDoc IniDoc::parse(const std::string& text) {
    IniDoc d;
    size_t i = 0;
    while (i < text.size()) {
        const size_t nl = text.find('\n', i);
        Line ln;
        if (nl == std::string::npos) {
            ln.text = text.substr(i);
            i = text.size();
        } else {
            size_t end = nl;
            if (end > i && text[end - 1] == '\r') {
                --end;
                ln.eol = "\r\n";
            } else {
                ln.eol = "\n";
            }
            ln.text = text.substr(i, end - i);
            i = nl + 1;
        }
        d.lines_.push_back(std::move(ln));
    }
    return d;
}

std::string IniDoc::serialize() const {
    std::string out;
    for (const Line& l : lines_) out += l.text + l.eol;
    return out;
}

std::string IniDoc::eol_() const {
    for (const Line& l : lines_)
        if (!l.eol.empty()) return l.eol;
    return "\r\n";   // RA2.INI is a Windows CRLF file
}

std::optional<std::string> IniDoc::get(const std::string& section, const std::string& key) const {
    std::optional<std::string> found;
    bool in = false;
    std::string name, k, v;
    for (const Line& l : lines_) {
        if (section_header(l.text, name)) { in = iequals(name, section); continue; }
        if (in && key_line(l.text, k, v) && iequals(k, key)) found = clean_value(v);
    }
    return found;
}

void IniDoc::set(const std::string& section, const std::string& key, const std::string& value) {
    const std::string eol = eol_();
    bool in = false;
    long key_ix = -1, last_content_ix = -1;
    bool section_seen = false;
    std::string name, k, v;
    for (size_t i = 0; i < lines_.size(); ++i) {
        const std::string& t = lines_[i].text;
        if (section_header(t, name)) {
            in = iequals(name, section);
            if (in) { section_seen = true; last_content_ix = static_cast<long>(i); }
            continue;
        }
        if (!in) continue;
        if (!trim(t).empty()) last_content_ix = static_cast<long>(i);
        if (key_line(t, k, v) && iequals(k, key)) key_ix = static_cast<long>(i);
    }
    if (key_ix >= 0) {   // rewrite in place, keeping the original key spelling
        std::string& t = lines_[static_cast<size_t>(key_ix)].text;
        t = t.substr(0, t.find('=') + 1) + value;
        return;
    }
    if (section_seen) {  // append after the section's last non-blank line
        Line& prev = lines_[static_cast<size_t>(last_content_ix)];
        if (prev.eol.empty()) prev.eol = eol;
        lines_.insert(lines_.begin() + last_content_ix + 1, Line{key + "=" + value, eol});
        return;
    }
    // New section at the end, separated from existing content by a blank line.
    if (!lines_.empty()) {
        if (lines_.back().eol.empty()) lines_.back().eol = eol;
        if (!trim(lines_.back().text).empty()) lines_.push_back(Line{"", eol});
    }
    lines_.push_back(Line{"[" + section + "]", eol});
    lines_.push_back(Line{key + "=" + value, eol});
}

// --------------------------------------------------- Westwood value semantics --

int ww_parse_int(const std::string& v, int def) {
    if (v.empty()) return def;
    unsigned int h = 0;
    if (v[0] == '$') return std::sscanf(v.c_str(), "$%x", &h) == 1 ? static_cast<int>(h) : def;
    if (std::tolower(static_cast<unsigned char>(v.back())) == 'h')
        return std::sscanf(v.c_str(), "%x", &h) == 1 ? static_cast<int>(h) : def;
    return std::atoi(v.c_str());
}

bool ww_parse_bool(const std::string& v, bool def) {
    if (v.empty()) return def;
    switch (std::toupper(static_cast<unsigned char>(v[0]))) {
        case '1': case 'T': case 'Y': return true;
        case '0': case 'F': case 'N': return false;
        default: return def;
    }
}

double ww_parse_double(const std::string& v, double def) {
    float f = 0.0f;
    if (std::sscanf(v.c_str(), "%f", &f) != 1) return def;
    double d = static_cast<double>(f);
    if (v.find('%') != std::string::npos) d *= 0.01;
    return d;
}

std::string ww_format_int(int v) {
    char b[32];
    std::snprintf(b, sizeof b, "%d", v);
    return b;
}

std::string ww_format_bool(bool v) { return v ? "yes" : "no"; }

std::string ww_format_double(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%f", v);
    return b;
}

// ---------------------------------------------------------------- Settings --

bool Settings::operator==(const Settings& o) const {
    return game_speed == o.game_speed && difficulty == o.difficulty &&
           scroll_method == o.scroll_method && scroll_rate == o.scroll_rate &&
           auto_scroll == o.auto_scroll && detail_level == o.detail_level &&
           sidebar_cameo_text == o.sidebar_cameo_text &&
           unit_action_lines == o.unit_action_lines && show_hidden == o.show_hidden &&
           tooltips == o.tooltips && autosave_enabled == o.autosave_enabled &&
           autosave_interval_minutes == o.autosave_interval_minutes && screen_width == o.screen_width &&
           screen_height == o.screen_height && stretch_movies == o.stretch_movies &&
           sidebar_scale == o.sidebar_scale && sidebar_cells == o.sidebar_cells &&
           display_override == o.display_override && widescreen == o.widescreen &&
           hud_text_size == o.hud_text_size &&
           allow_hires_modes == o.allow_hires_modes &&
           allow_vram_sidebar == o.allow_vram_sidebar && sound_volume == o.sound_volume &&
           voice_volume == o.voice_volume && score_volume == o.score_volume &&
           is_score_repeat == o.is_score_repeat && is_score_shuffle == o.is_score_shuffle &&
           sound_latency == o.sound_latency && in_game_music == o.in_game_music;
}

namespace {
constexpr const char* kOpt = "Options";
constexpr const char* kVid = "Video";
constexpr const char* kAud = "Audio";

int rd_int(const IniDoc& d, const char* s, const char* k, int def) {
    const auto v = d.get(s, k);
    return v ? ww_parse_int(*v, def) : def;
}
bool rd_bool(const IniDoc& d, const char* s, const char* k, bool def) {
    const auto v = d.get(s, k);
    return v ? ww_parse_bool(*v, def) : def;
}
float rd_vol(const IniDoc& d, const char* s, const char* k, float def) {
    const auto v = d.get(s, k);
    // Stored as a float (fld/fst of a float member) and clamped by
    // `if (1.0 <= v) v = 1.0` -- no lower clamp (FUN_005d8b00).
    float f = v ? static_cast<float>(ww_parse_double(*v, def)) : def;
    // W6-06-12: a non-finite value ("nan", "inf") would reach the mixer as
    // its gain; MSVC6's atof gives 0 for "nan" and the clamp maps +inf to 1,
    // but a NaN passes both compares. Reject it: the default.
    if (!std::isfinite(f)) f = def;
    if (f >= 1.0f) f = 1.0f;
    return f;
}
// The original's two-step clamp: `if (x >= hi) x = hi;` then `x <= 0 -> 0`.
int clamp_hi_then_lo(int x, int hi) {
    if (x >= hi) x = hi;
    if (x <= 0) x = 0;
    return x;
}
}  // namespace

Settings load_settings(const IniDoc& d) {
    Settings s;
    s.game_speed = rd_int(d, kOpt, "GameSpeed", s.game_speed);
    s.difficulty = clamp_hi_then_lo(rd_int(d, kOpt, "Difficulty", s.difficulty), 4);
    s.scroll_method = rd_int(d, kOpt, "ScrollMethod", s.scroll_method);
    s.scroll_rate = rd_int(d, kOpt, "ScrollRate", s.scroll_rate);
    s.auto_scroll = rd_bool(d, kOpt, "AutoScroll", s.auto_scroll);
    s.detail_level = clamp_hi_then_lo(rd_int(d, kOpt, "DetailLevel", s.detail_level), 2);
    s.sidebar_cameo_text = rd_bool(d, kOpt, "SidebarCameoText", s.sidebar_cameo_text);
    s.unit_action_lines = rd_bool(d, kOpt, "UnitActionLines", s.unit_action_lines);
    s.show_hidden = rd_bool(d, kOpt, "ShowHidden", s.show_hidden);
    s.tooltips = rd_bool(d, kOpt, "ToolTips", s.tooltips);
    s.autosave_enabled = rd_bool(d, kOpt, "AutoSave", s.autosave_enabled);
    {
        const int minutes = rd_int(d, kOpt, "AutoSaveInterval", s.autosave_interval_minutes);
        static constexpr int intervals[] = {1, 3, 5, 10, 15, 30};
        for (int value : intervals) if (value == minutes) s.autosave_interval_minutes = value;
    }
    s.screen_width = rd_int(d, kVid, "ScreenWidth", s.screen_width);
    s.screen_height = rd_int(d, kVid, "ScreenHeight", s.screen_height);
    s.stretch_movies = rd_bool(d, kVid, "StretchMovies", s.stretch_movies);
    s.display_override = rd_bool(d, kVid, "DisplayOverride", s.display_override);
    s.widescreen = rd_bool(d, kVid, "Widescreen", s.widescreen);
    s.hud_text_size = std::clamp(rd_int(d, kVid, "HUDTextSize", s.hud_text_size), 100, 160);
    s.sidebar_scale = std::clamp(rd_int(d,kVid,"SidebarScale",100),60,125);
    const int cells=rd_int(d,kVid,"SidebarCells",0);
    s.sidebar_cells=cells<=0 ? 0 : std::clamp(cells/2*2,6,60);
    s.allow_hires_modes = rd_bool(d, kVid, "AllowHiResModes", s.allow_hires_modes);
    s.allow_vram_sidebar = rd_bool(d, kVid, "AllowVRAMSidebar", s.allow_vram_sidebar);
    s.sound_volume = rd_vol(d, kAud, "SoundVolume", s.sound_volume);
    s.voice_volume = rd_vol(d, kAud, "VoiceVolume", s.voice_volume);
    s.score_volume = rd_vol(d, kAud, "ScoreVolume", s.score_volume);
    s.is_score_repeat = rd_bool(d, kAud, "IsScoreRepeat", s.is_score_repeat);
    s.is_score_shuffle = rd_bool(d, kAud, "IsScoreShuffle", s.is_score_shuffle);
    s.sound_latency = rd_int(d, kAud, "SoundLatency", s.sound_latency);
    s.in_game_music = rd_bool(d, kAud, "InGameMusic", s.in_game_music);
    return s;
}

void store_settings(const Settings& s, IniDoc& d) {
    // Exactly FUN_005d91b0's list and order (AllowHiResModes/AllowVRAMSidebar
    // are load-only and never written).
    d.set(kOpt, "GameSpeed", ww_format_int(s.game_speed));
    d.set(kOpt, "Difficulty", ww_format_int(s.difficulty));
    d.set(kOpt, "ScrollMethod", ww_format_int(s.scroll_method));
    d.set(kOpt, "ScrollRate", ww_format_int(s.scroll_rate));
    d.set(kOpt, "AutoScroll", ww_format_bool(s.auto_scroll));
    d.set(kOpt, "DetailLevel", ww_format_int(s.detail_level));
    d.set(kOpt, "SidebarCameoText", ww_format_bool(s.sidebar_cameo_text));
    d.set(kOpt, "UnitActionLines", ww_format_bool(s.unit_action_lines));
    d.set(kOpt, "ShowHidden", ww_format_bool(s.show_hidden));
    d.set(kOpt, "ToolTips", ww_format_bool(s.tooltips));
    d.set(kOpt, "AutoSave", ww_format_bool(s.autosave_enabled));
    d.set(kOpt, "AutoSaveInterval", ww_format_int(s.autosave_interval_minutes));
    d.set(kVid, "ScreenWidth", ww_format_int(s.screen_width));
    d.set(kVid, "ScreenHeight", ww_format_int(s.screen_height));
    d.set(kVid, "StretchMovies", ww_format_bool(s.stretch_movies));
    // Keep legacy files byte-identical until the new controls are used.
    if (s.display_override || d.get(kVid, "DisplayOverride"))
        d.set(kVid, "DisplayOverride", ww_format_bool(s.display_override));
    if(s.sidebar_scale!=100 || d.get(kVid,"SidebarScale"))
        d.set(kVid,"SidebarScale",ww_format_int(s.sidebar_scale));
    if(s.sidebar_cells!=0 || d.get(kVid,"SidebarCells"))
        d.set(kVid,"SidebarCells",ww_format_int(s.sidebar_cells));
    if (!s.widescreen || d.get(kVid, "Widescreen"))
        d.set(kVid, "Widescreen", ww_format_bool(s.widescreen));
    if (s.hud_text_size != 100 || d.get(kVid, "HUDTextSize"))
        d.set(kVid, "HUDTextSize", ww_format_int(std::clamp(s.hud_text_size, 100, 160)));
    d.set(kAud, "SoundVolume", ww_format_double(s.sound_volume));
    d.set(kAud, "VoiceVolume", ww_format_double(s.voice_volume));
    d.set(kAud, "ScoreVolume", ww_format_double(s.score_volume));
    d.set(kAud, "IsScoreRepeat", ww_format_bool(s.is_score_repeat));
    d.set(kAud, "IsScoreShuffle", ww_format_bool(s.is_score_shuffle));
    d.set(kAud, "SoundLatency", ww_format_int(s.sound_latency));
    d.set(kAud, "InGameMusic", ww_format_bool(s.in_game_music));
}

// ------------------------------------------------------------- control math --

int volume_to_slider(float v) {
    // ftol(v * 10.0 + 0.5) (consts 0x79D130 / 0x79A728); our trackbar clamps.
    const int p = static_cast<int>(static_cast<double>(v) * 10.0 + 0.5);
    return std::max(0, std::min(10, p));
}
float slider_to_volume(int pos) { return static_cast<float>(pos * 0.1); }
int scroll_rate_to_slider(int rate) { return 6 - rate; }
int slider_to_scroll_rate(int pos) { return 6 - pos; }
int detail_to_slider(int detail) { return detail != 0 ? 1 : 0; }
int slider_to_detail(int pos) { return pos != 0 ? 2 : 0; }

int trackbar_usable_w(int client_w, int readout_w) {
    return std::max(1, client_w - readout_w - 13);
}

int trackbar_thumb_x(int pos, int range, int client_w, int readout_w) {
    if (range <= 0) return 1;
    pos = std::max(0, std::min(range, pos));
    return 1 + trackbar_usable_w(client_w, readout_w) * pos / range;
}

int trackbar_pos_from_x(int x, int range, int client_w, int readout_w) {
    const int usable = trackbar_usable_w(client_w, readout_w);
    int ix = x - 6;
    if (ix < 1) ix = 1;
    const int hi = client_w - readout_w - 12;
    if (ix > hi) ix = hi;
    int p = (ix - 1) * (range + 1) / usable;
    if (p >= range) p = range;
    return std::max(0, p);
}

namespace {
// 0x7F2BE8: the edge-scroll distance table (options.md §5).
constexpr int kEdgeScrollTable[9] = {448, 384, 320, 256, 192, 128, 64, 32, 16};
}

int edge_scroll_distance(int scroll_rate, int accel, double scroll_multiplier) {
    const int rate = std::max(0, std::min(6, scroll_rate));
    int idx = 8 - accel;
    if (idx < rate + 1) idx = rate + 1;
    if (idx > 8) idx = 8;
    return static_cast<int>(kEdgeScrollTable[idx] * scroll_multiplier);
}

int edge_scroll_accel_cap(int scroll_rate) {
    const int rate = std::max(0, std::min(6, scroll_rate));
    return 8 - (rate + 1);
}

int volume_gain_permille(float v) {
    const long g = std::lround(static_cast<double>(v) * 1000.0);
    return static_cast<int>(std::max(0L, std::min(1000L, g)));
}

}  // namespace ra2engine

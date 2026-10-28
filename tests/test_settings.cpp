// P8 Options + persistence: the pure settings layer (engine/settings.{hpp,cpp}).
// Authority: docs/re/options.md (§2 control math, §3 keys/defaults/value
// semantics, §4 trackbar geometry, §5 edge-scroll table + gain law).
#include "doctest.h"
#include "settings.hpp"

#include <string>

using namespace ra2engine;

namespace {
// A trimmed copy of this install's real RA2.INI shape: CRLF, foreign sections
// before/after the ones OptionsClass owns, a comment, and the original's own
// value spellings ("0.700000", "yes").
const char* kInstallStyle =
    "[MultiPlayer]\r\n"
    "PreferredGameTypeID=1\r\n"
    "Handle=54,61,6e,79,61,\r\n"
    "\r\n"
    "; a user comment survives\r\n"
    "[Options]\r\n"
    "GameSpeed=1\r\n"
    "Difficulty=0\r\n"
    "ScrollMethod=0\r\n"
    "ScrollRate=0\r\n"
    "AutoScroll=yes\r\n"
    "DetailLevel=2\r\n"
    "SidebarCameoText=yes\r\n"
    "UnitActionLines=yes\r\n"
    "ShowHidden=yes\r\n"
    "ToolTips=yes\r\n"
    "\r\n"
    "[Video]\r\n"
    "AllowHiResModes=no\r\n"
    "ScreenWidth=1280\r\n"
    "ScreenHeight=800\r\n"
    "StretchMovies=no\r\n"
    "\r\n"
    "[Audio]\r\n"
    "SoundVolume=0.700000\r\n"
    "VoiceVolume=0.900000\r\n"
    "ScoreVolume=0.400000\r\n"
    "IsScoreRepeat=yes\r\n"
    "IsScoreShuffle=no\r\n"
    "SoundLatency=9\r\n"
    "InGameMusic=no\r\n"
    "\r\n"
    "[Skirmish]\r\n"
    "Credits=10000\r\n"
    "Slot01=6,4,1\r\n";
}  // namespace

// ---------------------------------------------------------------- IniDoc ----

TEST_CASE("settings: IniDoc parse->serialize is byte-identical (CRLF, comments, foreign sections)") {
    const std::string text = kInstallStyle;
    CHECK(IniDoc::parse(text).serialize() == text);
}

TEST_CASE("settings: IniDoc round-trips LF text, blank lines and a missing final newline") {
    const std::string a = "[A]\nx=1\n\n  ; c\n[B]\ny = two words \nz=3";
    CHECK(IniDoc::parse(a).serialize() == a);
    const std::string empty;
    CHECK(IniDoc::parse(empty).serialize() == empty);
}

TEST_CASE("settings: IniDoc lookup is case-insensitive and strips inline ';' comments") {
    IniDoc d = IniDoc::parse("[General]\nScrollMultiplier=.07     ; multiplier\n");
    REQUIRE(d.get("general", "SCROLLMULTIPLIER"));
    CHECK(*d.get("general", "SCROLLMULTIPLIER") == ".07");
    CHECK_FALSE(d.get("General", "Nope"));
    CHECK_FALSE(d.get("Nope", "ScrollMultiplier"));
}

TEST_CASE("settings: IniDoc set rewrites in place, appends key, appends section") {
    IniDoc d = IniDoc::parse("[A]\r\nx=1\r\n\r\n[B]\r\ny=2\r\n");
    d.set("a", "X", "9");                      // in place, original key spelling kept
    d.set("A", "w", "new");                    // appended after A's last key, before blank
    d.set("C", "k", "v");                      // new section at the end
    CHECK(d.serialize() == "[A]\r\nx=9\r\nw=new\r\n\r\n[B]\r\ny=2\r\n\r\n[C]\r\nk=v\r\n");
}

TEST_CASE("settings: IniDoc set on an empty doc creates a CRLF section") {
    IniDoc d = IniDoc::parse("");
    d.set("Options", "GameSpeed", "3");
    CHECK(d.serialize() == "[Options]\r\nGameSpeed=3\r\n");
}

// ------------------------------------------------- Westwood value parsing ----

TEST_CASE("settings: ww_parse_int - decimal, $hex, trailing-h hex, empty -> default") {
    CHECK(ww_parse_int("42", 7) == 42);
    CHECK(ww_parse_int("-1", 7) == -1);
    CHECK(ww_parse_int("$1F", 7) == 31);
    CHECK(ww_parse_int("1Fh", 7) == 31);
    CHECK(ww_parse_int("10H", 7) == 16);
    CHECK(ww_parse_int("", 7) == 7);
}

TEST_CASE("settings: ww_parse_bool - first char 1/T/Y true, 0/F/N false, else default") {
    CHECK(ww_parse_bool("yes", false));
    CHECK(ww_parse_bool("True", false));
    CHECK(ww_parse_bool("1", false));
    CHECK_FALSE(ww_parse_bool("no", true));
    CHECK_FALSE(ww_parse_bool("false", true));
    CHECK_FALSE(ww_parse_bool("0", true));
    CHECK(ww_parse_bool("maybe", true));
    CHECK_FALSE(ww_parse_bool("maybe", false));
    CHECK(ww_parse_bool("", true));
}

TEST_CASE("settings: ww_parse_double - %f, '%' scales by 0.01, junk -> default") {
    CHECK(ww_parse_double("0.700000", 0.0) == doctest::Approx(0.7));
    CHECK(ww_parse_double(".07", 0.0) == doctest::Approx(0.07));
    CHECK(ww_parse_double("50%", 0.0) == doctest::Approx(0.5));
    CHECK(ww_parse_double("abc", 0.25) == doctest::Approx(0.25));
}

TEST_CASE("settings: writers match the original %d / yes|no / %f") {
    CHECK(ww_format_int(-1) == "-1");
    CHECK(ww_format_bool(true) == "yes");
    CHECK(ww_format_bool(false) == "no");
    CHECK(ww_format_double(0.7f) == "0.700000");
    CHECK(ww_format_double(1.0f) == "1.000000");
}

// ------------------------------------------------------- Settings model ----

TEST_CASE("settings: defaults equal the OptionsClass ctor (0x5D8840)") {
    const Settings s;
    CHECK(s.game_speed == 3);
    CHECK(s.difficulty == 1);
    CHECK(s.scroll_method == 0);
    CHECK(s.scroll_rate == 3);
    CHECK(s.auto_scroll);
    CHECK(s.detail_level == 2);
    CHECK(s.sidebar_cameo_text);
    CHECK(s.unit_action_lines);
    CHECK_FALSE(s.show_hidden);
    CHECK(s.tooltips);
    CHECK(s.screen_width == -1);
    CHECK(s.screen_height == -1);
    CHECK_FALSE(s.stretch_movies);
    CHECK_FALSE(s.allow_hires_modes);
    CHECK_FALSE(s.allow_vram_sidebar);
    CHECK(s.sound_volume == 0.7f);
    CHECK(s.voice_volume == 1.0f);
    CHECK(s.score_volume == 0.5f);
    CHECK_FALSE(s.is_score_repeat);
    CHECK_FALSE(s.is_score_shuffle);
    CHECK(s.sound_latency == 9);
    CHECK(s.in_game_music);
    // An empty file loads as the defaults.
    CHECK(load_settings(IniDoc::parse("")) == Settings{});
}

TEST_CASE("settings: load reads the install-style RA2.INI") {
    const Settings s = load_settings(IniDoc::parse(kInstallStyle));
    CHECK(s.game_speed == 1);
    CHECK(s.difficulty == 0);
    CHECK(s.scroll_rate == 0);
    CHECK(s.show_hidden);
    CHECK(s.screen_width == 1280);
    CHECK(s.screen_height == 800);
    CHECK(s.sound_volume == 0.7f);
    CHECK(s.voice_volume == 0.9f);
    CHECK(s.score_volume == 0.4f);
    CHECK(s.is_score_repeat);
    CHECK_FALSE(s.in_game_music);
}

TEST_CASE("settings: load applies the original clamps (FUN_005d8b00)") {
    Settings s = load_settings(IniDoc::parse(
        "[Options]\nDifficulty=9\nDetailLevel=5\n[Audio]\nSoundVolume=1.7\nVoiceVolume=-0.5\n"));
    CHECK(s.difficulty == 4);         // CMP EAX,4 / JL / MOV EAX,4
    CHECK(s.detail_level == 2);
    CHECK(s.sound_volume == 1.0f);    // min(v, 1.0)
    CHECK(s.voice_volume == -0.5f);   // no lower clamp in the original
    s = load_settings(IniDoc::parse("[Options]\nDifficulty=-3\nDetailLevel=-1\n"));
    CHECK(s.difficulty == 0);
    CHECK(s.detail_level == 0);
}

TEST_CASE("settings: a non-finite volume falls back to the default (W6-06-12)") {
    const Settings d;
    Settings s = load_settings(IniDoc::parse("[Audio]\nSoundVolume=nan\nVoiceVolume=-nan\nScoreVolume=inf\n"));
    CHECK(s.sound_volume == d.sound_volume);
    CHECK(s.voice_volume == d.voice_volume);
    CHECK(s.score_volume == d.score_volume);
}

TEST_CASE("settings: store->load round-trips every persisted field") {
    Settings a;
    a.game_speed = 5; a.difficulty = 2; a.scroll_method = 1; a.scroll_rate = 6;
    a.auto_scroll = false; a.detail_level = 0; a.sidebar_cameo_text = false;
    a.unit_action_lines = false; a.show_hidden = true; a.tooltips = false;
    a.screen_width = 1024; a.screen_height = 768; a.stretch_movies = true;
    a.sound_volume = 0.3f; a.voice_volume = 0.8f; a.score_volume = 0.0f;
    a.is_score_repeat = true; a.is_score_shuffle = true; a.sound_latency = 4;
    a.in_game_music = false;
    IniDoc d = IniDoc::parse("");
    store_settings(a, d);
    const Settings b = load_settings(IniDoc::parse(d.serialize()));
    CHECK(b == a);
}

TEST_CASE("settings: store writes the FUN_005d91b0 key list, in order, and never the load-only keys") {
    IniDoc d = IniDoc::parse("");
    store_settings(Settings{}, d);
    CHECK(d.serialize() ==
          "[Options]\r\nGameSpeed=3\r\nDifficulty=1\r\nScrollMethod=0\r\nScrollRate=3\r\n"
          "AutoScroll=yes\r\nDetailLevel=2\r\nSidebarCameoText=yes\r\nUnitActionLines=yes\r\n"
          "ShowHidden=no\r\nToolTips=yes\r\n\r\n"
          "[Video]\r\nScreenWidth=-1\r\nScreenHeight=-1\r\nStretchMovies=no\r\n\r\n"
          "[Audio]\r\nSoundVolume=0.700000\r\nVoiceVolume=1.000000\r\nScoreVolume=0.500000\r\n"
          "IsScoreRepeat=no\r\nIsScoreShuffle=no\r\nSoundLatency=9\r\nInGameMusic=yes\r\n");
}

TEST_CASE("settings: storing the loaded values back leaves the install-style file byte-identical") {
    const std::string text = kInstallStyle;
    IniDoc d = IniDoc::parse(text);
    store_settings(load_settings(d), d);
    CHECK(d.serialize() == text);     // foreign sections, comment, AllowHiResModes untouched
}

// ------------------------------------------------------- control math ----

TEST_CASE("settings: slider <-> value mappings (DLGPROC 0x542590 / FUN_00542280)") {
    CHECK(volume_to_slider(0.7f) == 7);
    CHECK(volume_to_slider(0.75f) == 8);
    CHECK(volume_to_slider(0.0f) == 0);
    CHECK(volume_to_slider(1.0f) == 10);
    CHECK(volume_to_slider(-0.5f) == 0);
    CHECK(slider_to_volume(7) == doctest::Approx(0.7f));
    CHECK(scroll_rate_to_slider(3) == 3);
    CHECK(scroll_rate_to_slider(0) == 6);
    CHECK(slider_to_scroll_rate(6) == 0);
    CHECK(detail_to_slider(1) == 1);
    CHECK(detail_to_slider(0) == 0);
    CHECK(slider_to_detail(1) == 2);   // DetailLevel=1 round-trips to 2 (lossy, faithful)
    CHECK(slider_to_detail(0) == 0);
}

TEST_CASE("settings: trackbar geometry + click mapping (skin proc 0x5FB270)") {
    // Volume slider: 127 px client, 50 px readout -> usable 64.
    CHECK(trackbar_usable_w(127, 50) == 64);
    CHECK(trackbar_thumb_x(0, 10, 127, 50) == 1);
    CHECK(trackbar_thumb_x(10, 10, 127, 50) == 65);
    CHECK(trackbar_thumb_x(5, 10, 127, 50) == 33);
    // Scroll slider: 180 px, no readout -> usable 167; range 6.
    CHECK(trackbar_usable_w(180, 0) == 167);
    CHECK(trackbar_pos_from_x(0, 6, 180, 0) == 0);       // x-6 clamps to 1 -> 0
    CHECK(trackbar_pos_from_x(500, 6, 180, 0) == 6);     // clamps to range
    CHECK(trackbar_pos_from_x(91, 6, 180, 0) == 3);      // (85-1)*7/167 = 3
    CHECK(trackbar_pos_from_x(40, 10, 127, 50) == 5);    // (34-1)*11/64 = 5
}

TEST_CASE("settings: edge-scroll distance from the recovered 0x7F2BE8 table") {
    CHECK(edge_scroll_distance(3, 0, 0.07) == 1);     // 16*.07 -> starts slow
    CHECK(edge_scroll_distance(3, 1, 0.07) == 2);     // 32*.07
    CHECK(edge_scroll_distance(3, 4, 0.07) == 13);    // capped at idx rate+1 = 4 -> 192*.07
    CHECK(edge_scroll_distance(3, 99, 0.07) == 13);
    CHECK(edge_scroll_distance(0, 99, 0.07) == 26);   // 384*.07
    CHECK(edge_scroll_distance(6, 99, 0.07) == 2);    // 32*.07
    CHECK(edge_scroll_accel_cap(3) == 4);
    CHECK(edge_scroll_accel_cap(0) == 7);
    CHECK(edge_scroll_accel_cap(6) == 1);
}

TEST_CASE("settings: bus gain is linear in the stored volume") {
    CHECK(volume_gain_permille(1.0f) == 1000);
    CHECK(volume_gain_permille(0.7f) == 700);
    CHECK(volume_gain_permille(0.0f) == 0);
    CHECK(volume_gain_permille(-1.0f) == 0);
    CHECK(volume_gain_permille(2.0f) == 1000);
}

TEST_CASE("settings: live display overrides persist through enabling and disabling") {
    auto doc=IniDoc::parse("[Video]\nScreenWidth=1280\nScreenHeight=720\n");
    auto s=load_settings(doc);
    CHECK_FALSE(s.display_override); CHECK(s.widescreen);
    s.display_override=true; s.widescreen=false; store_settings(s,doc);
    CHECK(load_settings(doc)==s);
    s.display_override=false; s.widescreen=true; store_settings(s,doc);
    CHECK(load_settings(doc)==s);
}

TEST_CASE("settings: sidebar preferences persist and malformed box counts are clamped") {
    auto d=IniDoc::parse("[Video]\nSidebarScale=75\nSidebarCells=12\n");
    auto s=load_settings(d);CHECK(s.sidebar_scale==75);CHECK(s.sidebar_cells==12);
    s.sidebar_scale=60;s.sidebar_cells=0;store_settings(s,d);CHECK(load_settings(d)==s);
    CHECK(load_settings(IniDoc::parse("[Video]\nSidebarScale=200\nSidebarCells=9\n")).sidebar_cells==8);
    CHECK(load_settings(IniDoc::parse("[Video]\nSidebarScale=200\nSidebarCells=9\n")).sidebar_scale==125);
}

#include "doctest.h"
#include "audio_mixer.hpp"
#include <vector>

using namespace ra2engine;

namespace {
// bank with two tiny clips: A = {1000,1000,1000,1000}, B = {-300,-300}
SoundBank two_clip_bank(int& a, int& b) {
    SoundBank bank;
    a = bank.add({1000, 1000, 1000, 1000});
    b = bank.add({-300, -300});
    return bank;
}
MixerSound plain(int clip, int volume = 100) {
    MixerSound s;
    s.variants = {clip};
    s.volume = volume;
    s.limit = 0;
    return s;   // plays at dist 0 -> full gain
}
}  // namespace

TEST_CASE("mixer: two overlapping clips sum sample-by-sample") {
    int a, b;
    SoundBank bank = two_clip_bank(a, b);
    Mixer m(bank, 1);
    m.register_sound("A", plain(a));
    m.register_sound("B", plain(b));
    CHECK(m.play("A"));
    CHECK(m.play("B"));
    int16_t out[6] = {};
    m.render(out, 6);
    CHECK(out[0] == 700);   // 1000 - 300
    CHECK(out[1] == 700);
    CHECK(out[2] == 1000);  // B finished
    CHECK(out[3] == 1000);
    CHECK(out[4] == 0);     // both finished
    CHECK(m.active_voices() == 0);
}

TEST_CASE("mixer: summing clamps at int16 extremes") {
    SoundBank bank;
    int loud = bank.add({30000, -30000});
    Mixer m(bank, 1);
    m.register_sound("L", plain(loud));
    CHECK(m.play("L"));
    CHECK(m.play("L"));
    int16_t out[2] = {};
    m.render(out, 2);
    CHECK(out[0] == 32767);    // 60000 clamped
    CHECK(out[1] == -32768);   // -60000 clamped
}

TEST_CASE("mixer: volume scales linearly (permille math)") {
    SoundBank bank;
    int c = bank.add({10000});
    Mixer m(bank, 1);
    m.register_sound("H", plain(c, 65));   // GIAttack's real volume
    CHECK(m.play("H"));
    int16_t out[1] = {};
    m.render(out, 1);
    CHECK(out[0] == 6500);
}

TEST_CASE("mixer: Limit rejects the (limit+1)-th instance; interrupt steals instead") {
    SoundBank bank;
    int c = bank.add(std::vector<int16_t>(100, 100));
    Mixer m(bank, 1);
    MixerSound s = plain(c);
    s.limit = 3;                       // ConscriptAttack's real limit
    m.register_sound("S", s);
    CHECK(m.play("S")); CHECK(m.play("S")); CHECK(m.play("S"));
    CHECK_FALSE(m.play("S"));          // 4th rejected (documented approx of vanilla cull-to-cap)
    CHECK(m.active_voices() == 3);
    MixerSound si = s;
    si.interrupt = true;
    Mixer m2(bank, 1);
    m2.register_sound("S", si);
    CHECK(m2.play("S")); CHECK(m2.play("S")); CHECK(m2.play("S"));
    CHECK(m2.play("S"));               // interrupt: steals oldest same-key (RE doc, Medium)
    CHECK(m2.active_voices() == 3);
}

TEST_CASE("mixer: full voice pool DROPS the new sound (vanilla: no priority steal)") {
    // docs/re/audio-playback.md FUN_00405190: the SFX allocator reclaims
    // finished voices only, then fails -- it never evicts a playing voice.
    SoundBank bank;
    int c = bank.add(std::vector<int16_t>(100, 100));
    Mixer m(bank, 1);
    MixerSound s = plain(c);
    s.priority = 4;                    // even critical does not steal
    m.register_sound("S", s);
    for (int i = 0; i < Mixer::kMaxVoices; ++i) REQUIRE(m.play("S"));
    CHECK_FALSE(m.play("S"));          // pool full -> dropped
    CHECK(m.active_voices() == Mixer::kMaxVoices);
}

TEST_CASE("mixer: priority_from_string uses the real 5-level scale") {
    CHECK(priority_from_string("lowest") == 0);
    CHECK(priority_from_string("low") == 1);
    CHECK(priority_from_string("normal") == 2);
    CHECK(priority_from_string("high") == 3);
    CHECK(priority_from_string("critical") == 4);
    CHECK(priority_from_string("bogus") == 2);
}

TEST_CASE("mixer: attenuation linear (CONFIRMED) with GLOBAL MinVolume floor") {
    MixerSound s;
    s.volume = 100; s.range = 10.0; s.min_volume = 50;
    CHECK(Mixer::gain_permille(s, 0.0) == 1000);
    CHECK(Mixer::gain_permille(s, 5.0) == 500);
    CHECK(Mixer::gain_permille(s, 10.0) == 0);
    CHECK(Mixer::gain_permille(s, 25.0) == 0);
    s.global = true;                   // Type=GLOBAL: floored, never silent
    CHECK(Mixer::gain_permille(s, 25.0) == 500);   // max(gain, min_volume*10)
    CHECK(Mixer::gain_permille(s, 0.0) == 1000);   // floor never raises full
}

TEST_CASE("mixer: seeded variant selection is deterministic") {
    SoundBank bank;
    int c0 = bank.add({1});
    int c1 = bank.add({2});
    int c2 = bank.add({3});
    MixerSound s;
    s.variants = {c0, c1, c2};
    s.limit = 0; s.volume = 100;
    Mixer a(bank, 42), b(bank, 42);
    a.register_sound("V", s);
    b.register_sound("V", s);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(a.play("V")); REQUIRE(b.play("V"));
        int16_t oa[1] = {}, ob[1] = {};
        a.render(oa, 1); b.render(ob, 1);
        CHECK(oa[0] == ob[0]);   // same seed -> same variant sequence
    }
}

TEST_CASE("mixer: EVA queues one-at-a-time in FIFO order") {
    SoundBank bank;
    int e1 = bank.add({11, 11});
    int e2 = bank.add({22, 22});
    Mixer m(bank, 1);
    m.play_eva(e1);
    m.play_eva(e2);
    CHECK(m.eva_active());
    CHECK(m.eva_queued() == 1);
    int16_t out[2] = {};
    m.render(out, 2);
    CHECK(out[0] == 11);           // e1 plays alone first
    m.render(out, 2);
    CHECK(out[0] == 22);           // then e2
    CHECK(m.eva_queued() == 0);
    m.render(out, 2);
    CHECK_FALSE(m.eva_active());
    CHECK(out[0] == 0);
}

TEST_CASE("mixer: stop_eva flushes the playing line and its queue (G-80 INTERRUPT)") {
    SoundBank bank;
    int e1 = bank.add({11, 11, 11});
    int e2 = bank.add({22, 22});
    Mixer m(bank, 1);
    m.play_eva(e1);
    m.play_eva(e2);
    m.stop_eva();
    CHECK_FALSE(m.eva_active());
    CHECK(m.eva_queued() == 0);
    int16_t out[1] = {};
    m.render(out, 1);
    CHECK(out[0] == 0);
}

TEST_CASE("mixer: playing/stop by key (G-82 music track change)") {
    SoundBank bank;
    MixerSound s;
    s.variants.push_back(bank.add(std::vector<int16_t>(100, 5)));
    s.global = true;
    s.min_volume = 100;
    Mixer m(bank, 1);
    m.register_sound("music:A", s);
    CHECK_FALSE(m.playing("music:A"));
    REQUIRE(m.play("music:A"));
    CHECK(m.playing("music:A"));
    m.stop("music:A");
    CHECK_FALSE(m.playing("music:A"));
    CHECK(m.active_voices() == 0);
}

TEST_CASE("mixer: unknown key or empty variants -> false, no crash") {
    SoundBank bank;
    Mixer m(bank, 1);
    CHECK_FALSE(m.play("nope"));
    MixerSound empty;
    m.register_sound("E", empty);
    CHECK_FALSE(m.play("E"));
}

// --- P8 Options: per-bus volume (docs/re/options.md §5: Sound / Voice / Score
// buses, linear gain). Default gains are identity so nothing pre-P8 changes. ---

TEST_CASE("mixer bus: default gains are identity (pre-P8 output unchanged)") {
    int a, b;
    SoundBank bank = two_clip_bank(a, b);
    Mixer m(bank, 1);
    CHECK(m.bus_gain(Mixer::Bus::Sound) == 1000);
    CHECK(m.bus_gain(Mixer::Bus::Voice) == 1000);
    CHECK(m.bus_gain(Mixer::Bus::Score) == 1000);
    m.register_sound("A", plain(a));
    CHECK(m.play("A"));
    m.play_eva(b);
    int16_t out[2] = {};
    m.render(out, 2);
    CHECK(out[0] == 700);   // 1000 + (-300), exactly as before buses existed
}

TEST_CASE("mixer bus: a Score-bus gain scales only Score voices") {
    int a, b;
    SoundBank bank = two_clip_bank(a, b);
    Mixer m(bank, 1);
    MixerSound music = plain(a);
    music.bus = Mixer::Bus::Score;
    m.register_sound("music", music);
    m.register_sound("sfx", plain(b));
    m.set_bus_gain(Mixer::Bus::Score, 500);
    CHECK(m.play("music"));
    CHECK(m.play("sfx"));
    int16_t out[3] = {};
    m.render(out, 3);
    CHECK(out[0] == 200);   // 1000*0.5 + (-300)
    CHECK(out[2] == 500);   // sfx finished; music still halved
}

TEST_CASE("mixer bus: EVA follows the Voice bus; gain 0 silences; gain clamps") {
    int a, b;
    SoundBank bank = two_clip_bank(a, b);
    Mixer m(bank, 1);
    m.set_bus_gain(Mixer::Bus::Voice, 250);
    m.play_eva(a);
    int16_t out[1] = {};
    m.render(out, 1);
    CHECK(out[0] == 250);
    m.set_bus_gain(Mixer::Bus::Voice, 0);
    m.render(out, 1);
    CHECK(out[0] == 0);
    m.set_bus_gain(Mixer::Bus::Sound, 5000);
    CHECK(m.bus_gain(Mixer::Bus::Sound) == 1000);
    m.set_bus_gain(Mixer::Bus::Sound, -5);
    CHECK(m.bus_gain(Mixer::Bus::Sound) == 0);
}

// ---- W2-GA-22/23/24 -------------------------------------------------------
TEST_CASE("mixer: render_stereo pans by the RE-Q11 balance law, EVA centred") {
    SoundBank bank;
    const int c = bank.add(std::vector<int16_t>(4, 8000));
    Mixer m(bank, 1);
    MixerSound s = plain(c);
    m.register_sound("P", s);
    CHECK(m.play_at("P", 0.0, 4096) != 0);   // left screen edge
    int16_t lr[2] = {};
    m.render_stereo(lr, 1);
    CHECK(lr[0] == 8000);                     // left full
    CHECK(lr[1] == 4000);                     // right half
}

TEST_CASE("mixer: update_voice re-attenuates and stops a voice that leaves range") {
    SoundBank bank;
    const int c = bank.add(std::vector<int16_t>(100, 1000));
    Mixer m(bank, 1);
    MixerSound s = plain(c);
    s.range = 10.0;
    m.register_sound("R", s);
    const std::uint32_t h = m.play_at("R", 0.0, 8192);
    REQUIRE(h != 0);
    CHECK(m.update_voice(h, 5.0, 8192));
    int16_t out[1] = {};
    m.render(out, 1);
    CHECK(out[0] == 500);                     // half gain at half range
    CHECK_FALSE(m.update_voice(h, 12.0, 8192));   // out of range -> stopped
    CHECK_FALSE(m.voice_alive(h));
}

TEST_CASE("mixer: FShift changes the playback rate; LOOP repeats until stopped") {
    SoundBank bank;
    const int c = bank.add({1, 2, 3, 4});
    Mixer m(bank, 1);
    MixerSound s = plain(c);
    s.fshift_lo = s.fshift_hi = 100;          // 200 % pitch
    m.register_sound("F", s);
    CHECK(m.play("F"));
    int16_t out[3] = {};
    m.render(out, 3);
    CHECK(out[0] == 1);
    CHECK(out[1] == 3);
    CHECK(out[2] == 0);                       // finished after 2 frames
    MixerSound l = plain(c);
    l.loop = true;
    m.register_sound("L", l);
    const std::uint32_t h = m.play_at("L", 0.0, 8192);
    int16_t o2[6] = {};
    m.render(o2, 6);
    CHECK(o2[4] == 1);                        // wrapped to the start
    CHECK(o2[5] == 2);
    m.stop_voice(h);
    m.render(o2, 1);
    CHECK(o2[0] == 0);
}

TEST_CASE("mixer: render_stereo soft-limits instead of hard clipping, streams music") {
    struct Const : MusicSource {
        std::size_t read(int16_t* lr, std::size_t n) override {
            for (std::size_t i = 0; i < 2 * n; ++i) lr[i] = 1000;
            return n;
        }
    };
    SoundBank bank;
    const int loud = bank.add({30000});
    Mixer m(bank, 1);
    m.register_sound("X", plain(loud));
    CHECK(m.play("X"));
    CHECK(m.play("X"));
    m.set_music(std::make_shared<Const>());
    int16_t lr[2] = {};
    m.render_stereo(lr, 1);
    CHECK(lr[0] > 30000);
    CHECK(lr[0] < 32767);                     // 61000 compressed, not clamped flat
    int16_t lr2[2] = {};
    m.render_stereo(lr2, 1);
    CHECK(lr2[0] == 1000);                    // only the music remains
}

#include "music_stream.hpp"
#include "ra2assets/mix.hpp"
#include "ra2assets/wav.hpp"
#include "asset_fixture.hpp"

// W2-GA-22: a streamed track yields the same audio as the whole-file decode
// (here: a 22050 Hz stereo theme passes through untouched), off-thread.
TEST_CASE("mixer: MusicStream streams theme.mix grinder.wav identically to the whole decode") {
    std::string path = ra2fixture::install_base();
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += "/";
    path += "theme.mix";
    ra2assets::MixFile theme;
    try { theme = ra2assets::MixFile::open_path(path); } catch (const std::exception&) {
        MESSAGE("theme.mix absent; skipped");
        return;
    }
    const ra2assets::MixEntry* e = theme.find(std::string("grinder.wav"));
    REQUIRE(e);
    const std::vector<uint8_t> bytes = theme.read(*e);
    ra2assets::WavInfo info;
    const std::vector<int16_t> whole = ra2assets::load_wav_ima(bytes.data(), bytes.size(), info);
    REQUIRE(info.sample_rate == 22050);
    REQUIRE(info.channels == 2);
    MusicStream ms([bytes]() { return bytes; });
    REQUIRE(ms.wait_ready(5000));
    CHECK(ms.length_secs() > 60.0);
    std::vector<int16_t> got;
    int16_t buf[2048];
    for (int spins = 0; spins < 2000000 && !ms.finished(); ++spins) {
        const std::size_t n = ms.read(buf, 1024);
        got.insert(got.end(), buf, buf + n * 2);
        if (!n) std::this_thread::yield();
    }
    CHECK(ms.finished());
    CHECK_FALSE(ms.failed());
    CHECK(got == whole);
}

TEST_CASE("mixer: MusicStream marks a zero-filled entry failed (the thememd.mix placeholders)") {
    MusicStream ms([]() { return std::vector<uint8_t>(4096, 0); });
    CHECK_FALSE(ms.wait_ready(2000));
    CHECK(ms.failed());
    CHECK(ms.finished());
}

// W5-E [RE: FUN_004055c0]: PREDELAY / AMBIENT wait RandomRanged(lo, max) ms
// (lo = 33 for AMBIENT) before the voice sounds; the voice holds its slot.
TEST_CASE("mixer: PREDELAY delays the start by Delay ms (22050 Hz samples)") {
    SoundBank bank;
    int c = bank.add({5000, 5000});
    Mixer m(bank, 1);
    MixerSound s = plain(c);
    s.predelay = true;
    s.delay_lo_ms = 100;
    s.delay_hi_ms = 100;   // fixed 100 ms = 2205 samples
    m.register_sound("D", s);
    REQUIRE(m.play("D"));
    std::vector<int16_t> out(2210, 0);
    m.render(out.data(), out.size());
    CHECK(out[0] == 0);
    CHECK(out[2204] == 0);
    CHECK(out[2205] == 5000);
    CHECK(out[2206] == 5000);
    // A draw under 33 ms starts at once.
    MixerSound z = plain(c);
    z.predelay = true;
    z.delay_lo_ms = 0;
    z.delay_hi_ms = 20;
    m.register_sound("Z", z);
    REQUIRE(m.play("Z"));
    int16_t o2[1] = {};
    m.render(o2, 1);
    CHECK(o2[0] == 5000);
}

TEST_CASE("mixer: play_scaled scales the gain; volume under 0.05 is inaudible") {
    SoundBank bank;
    int c = bank.add({10000});
    Mixer m(bank, 1);
    m.register_sound("S", plain(c));
    REQUIRE(m.play_scaled("S", 0.5));
    int16_t out[1] = {};
    m.render(out, 1);
    CHECK(out[0] == 5000);
    // [RE: FUN_0074f8e0 constant 0.05]
    MixerSound r = plain(c);
    r.range = 10.0;
    CHECK(Mixer::gain_permille(r, 9.6) == 0);   // 0.04
    CHECK(Mixer::gain_permille(r, 8.0) >= 199);   // 0.2 (float truncation)
    CHECK(m.sound("S") != nullptr);
    CHECK(m.sound("nope") == nullptr);
}

TEST_CASE("mixer: pause freezes voices and music in mono and stereo") {
    struct CountingMusic : MusicSource {
        int reads=0;
        std::size_t read(int16_t* out,std::size_t n) override {
            ++reads;std::fill_n(out,n*2,int16_t{100});return n;
        }
    };
    for(bool stereo:{false,true}) {
        SoundBank bank;int clip=bank.add({1000,2000,3000});
        Mixer m(bank,1);m.register_sound("A",plain(clip));REQUIRE(m.play("A"));
        auto music=std::make_shared<CountingMusic>();m.set_music(music);
        int16_t out[8]={};
        auto render=[&](std::size_t n){if(stereo)m.render_stereo(out,n);else m.render(out,n);};
        render(1);CHECK(out[0]==1100);const int reads=music->reads;
        m.set_paused(true);CHECK(m.paused());std::fill_n(out,8,int16_t{42});render(4);
        for(int i=0;i<(stereo?8:4);++i)CHECK(out[i]==0);
        CHECK(music->reads==reads);CHECK(m.active_voices()==1);
        m.set_paused(false);render(1);CHECK(out[0]==2100);
    }
}

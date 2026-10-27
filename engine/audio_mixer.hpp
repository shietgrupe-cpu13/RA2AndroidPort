#pragma once
// Pure software mixer: no SDL/OS/file deps (same pattern as combat.hpp/
// occupancy.hpp), so it is unit-testable and compiled into both
// ra2assets_tests and ra2engine from a single source of truth (see
// CMakeLists.txt / cmake/ra2_sources.cmake). All PCM is 22050 Hz mono S16.
//
// Calibration authority: docs/re/audio-playback.md ("Decisions for
// ra2engine" section). That doc AMENDS the original plan after a Ghidra
// hunt; the semantics implemented here are the amended ones, not the
// plan's original guesses:
//   - Priority is the real 5-level scale (LOWEST=0..CRITICAL=4, unknown
//     string -> NORMAL=2), not the plan's 4-level 0..3.
//   - `Limit` is enforced by REJECTING the (Limit+1)-th play() at trigger
//     time (a documented approximation of vanilla's start-then-cull-to-cap
//     voice stealing, FUN_004041d0) -- both hold the same <=Limit bound.
//   - `interrupt` (Control=INTERRUPT) steals the oldest still-playing
//     voice of the SAME key rather than rejecting (FUN_004041d0, Medium).
//   - A full voice pool DROPS the new sound; vanilla's SFX allocator
//     (FUN_00405190) reclaims only finished voices and never evicts a
//     playing one by priority, no matter how high the new sound's
//     priority is (Medium).
//   - Attenuation is CONFIRMED linear, `gain = 1 - dist/range`, full at
//     dist=0, silent at dist>=range (FUN_0074f8e0, High). `Type=GLOBAL`
//     (MixerSound::global) floors the result at `min_volume` (scaled to
//     permille) instead of letting it reach silence -- MinVolume is
//     otherwise ignored (GLOBAL-only branch). The caller supplies
//     dist_cells in CELLS; the game's own metric is Chebyshev screen
//     pixels (`max(dx, 2|dy|)`) divided by 60 px/cell -- Task 6 computes
//     that projection and passes the resulting cell distance in here.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ra2engine {

// [P8 / docs/re/options.md §5] The original routes every sound through one of
// three volume buses set from RA2.INI [Audio]: SoundVolume -> the SFX voice
// pool (DAT_00831458), VoiceVolume -> the speech/EVA streamer (DAT_00831450),
// ScoreVolume -> the music/theme player (DAT_00831454). Linear gain.
enum class AudioBus { Sound = 0, Voice = 1, Score = 2 };

// One [SoundList] event's mixer-relevant fields (Task 6 fills these from a
// parsed SoundEntry). `variants` are SoundBank clip ids; a sound with no
// variants can never play (register_sound accepts it, play() rejects it).
struct MixerSound {
    std::vector<int> variants;   // SoundBank clip ids (>=1 to be playable)
    int volume = 80;             // 0..100
    // 5-level scale (docs/re/audio-playback.md table @0x815018): lowest=0,
    // low=1, normal=2, high=3, critical=4. Stored verbatim from
    // priority_from_string; the mixer itself never evicts by priority (see
    // the full-pool DROP rule above), so this is carried on Voice mostly
    // for a future consumer, not consulted by play()/render() today.
    int priority = 1;
    int limit = 5;                // max simultaneous instances; <=0 unlimited
    bool interrupt = false;       // Control=interrupt: steal oldest same-key voice
    bool global = false;          // Type contains GLOBAL: MinVolume floor (never silent)
    double range = 10.0;          // cells
    int min_volume = 50;          // 0..100; GLOBAL-only floor
    AudioBus bus = AudioBus::Sound;   // [P8] volume bus (SFX by default)
    // W2-GA-24 [RE: audio-playback.md §2/§4, FUN_004048b0 / FUN_004055c0]:
    // pitch % = 100 + RandomRanged(fshift_lo, fshift_hi); volume % -=
    // RandomRanged(0, vshift). Control ALL plays every body sample in order,
    // ATTACK/DECAY reserve the first/last variant as a fixed intro/outro, LOOP
    // repeats the body until stopped. Audio-local RNG (never the sim's).
    int vshift = 0;
    int fshift_lo = 0, fshift_hi = 0;
    bool all = false;
    bool attack = false, decay = false;
    int attack_n = 0, decay_n = 0;   // W7-12-24 soundmd Attack= / Decay= counts (0 -> 1 with the Control bit)
    bool loop = false;
    // W5-E [RE: FUN_004055c0 voice start, audio-playback.md §4]: Control
    // PREDELAY (0x8) or AMBIENT (0x80) waits RandomRanged(lo, Delay max) ms
    // before the voice sounds -- lo = 33 for AMBIENT, else Delay min -- and
    // only when the draw is >= 33 ms. A LOOP voice re-draws it before every
    // pass (the start state is re-entered) [Medium: the re-entry edge].
    bool predelay = false;
    bool ambient = false;
    int delay_lo_ms = 0, delay_hi_ms = 0;
    // W5-E [RE: FUN_0074f8e0]: Type=LOCAL measures from the view centre
    // (no on-screen dead zone); Type=SHROUD is silent unless the source cell
    // is revealed (the caller passes the shroud state).
    bool local = false;
    bool shroud = false;
};

// W2-GA-22: a streamed stereo music source (22050 Hz interleaved L,R). The
// mixer pulls from it on the audio thread; implementations must be
// thread-safe. read() returns the frames written (the rest of `out` is left
// untouched -- the mixer zero-fills).
class MusicSource {
public:
    virtual ~MusicSource() = default;
    virtual std::size_t read(int16_t* lr, std::size_t frames) = 0;
};

// AMENDED: real 5-level scale (docs/re/audio-playback.md, table @0x815018):
// lowest=0 low=1 normal=2 high=3 critical=4, unknown -> 2 (NORMAL, the
// table's own default). Callers are expected to pass an already-lowercased
// token (Task 2's Sounds reader lowercases Priority at parse time).
int priority_from_string(const std::string& p);

// Owns decoded PCM clips (22050 Hz mono S16) by integer id. Pure storage --
// no decoding, no file I/O; Task 6 fills it from already-decoded audio.
class SoundBank {
public:
    int add(std::vector<int16_t> pcm);            // -> clip id
    const std::vector<int16_t>& pcm(int id) const;
    int count() const;

private:
    friend class Mixer;
    std::vector<std::vector<int16_t>> clips_;
    // W2-GA-P4: clips may be added from the game thread while the audio
    // thread renders; the mixer holds this lock for a whole render call.
    std::unique_ptr<std::mutex> mu_ = std::make_unique<std::mutex>();
};

// The mixer proper: keyed sound registry, a bounded voice pool, a
// single-active-plus-FIFO EVA speech slot, and a sample-accurate renderer.
class Mixer {
public:
    static constexpr int kMaxVoices = 16;
    // Freeze sample cursors, queued speech and streamed music while a modal game page is open.
    void set_paused(bool paused);
    bool paused() const;
    using Bus = AudioBus;

    // [P8] Per-bus gain in permille (0..1000, clamped). Defaults to 1000 on
    // every bus = identity: render() takes the exact pre-P8 arithmetic path
    // for an identity bus, so no gate/test output moves unless the
    // interactive Options settings were applied. The EVA slot mixes on
    // Bus::Voice.
    void set_bus_gain(Bus bus, int permille);
    int  bus_gain(Bus bus) const;

    // `bank` must outlive the Mixer (stored by reference; Task 6 owns both
    // with the same lifetime, same pattern as Occupancy/World elsewhere).
    // `seed` initializes the audio-local xorshift32 RNG used for variant
    // selection -- deliberately NOT the sim's Randomizer (docs/re/
    // audio-playback.md's RNG-determinism caveat: audio must never perturb
    // sim RNG state so --demo-combat stays byte-identical).
    Mixer(const SoundBank& bank, uint32_t seed);

    // Registers/replaces the MixerSound for `key` (e.g. "GIAttack").
    void register_sound(const std::string& key, const MixerSound& s);

    // Triggers one instance of `key` at `dist_cells` (0.0 = full gain
    // subject only to the GLOBAL floor). Returns false when: the key is
    // unknown, it has no variants, it is inaudible (gain_permille <= 0,
    // so no voice is spent on a silent play), Limit is already at cap and
    // `interrupt` is false, or the pool is full (kMaxVoices) with no
    // Limit-driven steal to make room -- see the class doc comment above
    // for which of these is a documented approximation of vanilla.
    bool play(const std::string& key, double dist_cells = 0.0);
    // W5-E: a non-positional play scaled by `volume` (0..1) -- the original's
    // FUN_0074F740(snd, volume) (e.g. the credit ticks at 0.5).
    bool play_scaled(const std::string& key, double volume);
    // W5-E: the registered event (nullptr if unknown) -- the presentation
    // reads Type=LOCAL/SHROUD from it.
    const MixerSound* sound(const std::string& key) const;

    // W2-GA-23 [RE: audio-playback.md §3, RE-Q11 FUN_0074f8e0]: like play()
    // but positional -- `pan` 0..16384 (8192 centre) -- and returns a voice
    // handle (0 = not started) the caller re-attenuates every frame with
    // update_voice (the original re-evaluates live voices and STOPS one that
    // leaves range).
    std::uint32_t play_at(const std::string& key, double dist_cells, int pan);
    // Re-evaluates a live voice's gain/pan; false (and the voice is stopped)
    // once it is inaudible or already finished.
    bool update_voice(std::uint32_t handle, double dist_cells, int pan);
    void stop_voice(std::uint32_t handle);
    bool voice_alive(std::uint32_t handle) const;

    // W2-GA-22: the streamed score (Score bus), mixed by render_stereo (and
    // downmixed by render). nullptr = none.
    void set_music(std::shared_ptr<MusicSource> src);
    std::shared_ptr<MusicSource> music() const;   // W5-E2: the movie player restores it

    // Queues an EVA line by SoundBank clip id: full volume, no gain
    // scaling, one active at a time (FIFO behind it). A new call starts
    // playing immediately iff no EVA line is currently active.
    void play_eva(int clip_id);
    // [G-80] EVA Type=INTERRUPT flush (audio-playback.md §8): stop the playing
    // speech and drop everything queued behind it.
    void stop_eva();
    // [G-82] Stop every voice playing `key` (music track change).
    void stop(const std::string& key);
    // [G-82] True while any voice of `key` is still playing.
    bool playing(const std::string& key) const;

    // Renders `frames` samples into `out` (must hold >= frames int16_t):
    // sums every active voice's contribution plus the active EVA line,
    // clamps to the int16 range, and advances all playback positions,
    // retiring any voice/EVA clip that finishes during this call.
    void render(int16_t* out, std::size_t frames);
    // W2-GA-23: the stereo sink path -- interleaved L,R; per-voice pan (linear
    // balance law around 8192), streamed music, and a soft limiter instead of
    // a hard clamp (headroom over the original's hardware mixer saturation).
    void render_stereo(int16_t* lr, std::size_t frames);

    int active_voices() const;          // non-EVA voices currently playing
    bool eva_active() const;
    std::size_t eva_queued() const;     // waiting behind the active EVA line

    // CONFIRMED linear falloff (docs/re/audio-playback.md, FUN_0074f8e0,
    // High): gain = 1 - dist/range, full at 0, silent at range. Type=GLOBAL
    // events are floored at min_volume (never silent) instead. The caller
    // supplies dist in CELLS (the game divides its pixel metric by 60 --
    // Task 6 computes the exact Chebyshev metric). Returns 0..1000.
    static int gain_permille(const MixerSound& s, double dist_cells);

private:
    // One live (non-EVA) voice. `vol_x_gain` is volume(0..100) *
    // gain_permille(0..1000), precomputed at play() time (attenuation is
    // NOT re-evaluated per frame here -- docs/re/audio-playback.md notes
    // vanilla re-attenuates live voices every frame; this pure mixer keeps
    // the plan's simpler latch-at-trigger model, a documented
    // simplification Task 6 may revisit). `age` breaks ties for
    // interrupt's oldest-same-key steal.
    struct Voice {
        std::string key;
        int clip;
        std::size_t pos = 0;
        int vol_x_gain;      // volume * gain_permille, 0..100000
        int priority;
        std::uint64_t age;
        Bus bus = Bus::Sound;   // [P8] latched from MixerSound::bus at play()
        // W2-GA-23/24 (defaults keep the pre-W2 arithmetic exactly):
        int volume = 100;            // after VShift
        int pan = 8192;
        std::uint32_t handle = 0;
        std::uint32_t step = 65536;  // 16.16 playback rate (FShift)
        std::uint32_t frac = 0;      // 16.16 fraction of pos
        std::vector<int> seq;        // follow-on clips (ALL / DECAY), in order
        std::size_t seq_ix = 0;
        int loop_from = -1;          // LOOP: body clip to repeat
        bool done = false;
        std::size_t delay = 0;       // W5-E predelay samples still to wait
        bool predelay = false, ambient = false;
        int delay_lo = 0, delay_hi = 0;
        std::size_t loop_start = 0;  // W7-12-24: LOOP+ALL restarts here (after the intro)
    };

    std::uint32_t next_rand_();
    int ranged_(int lo, int hi);
    void kill_oldest_with_key_(const std::string& key);
    std::uint32_t start_(const std::string& key, double dist_cells, int pan, double scale = 1.0);
    std::size_t draw_delay_(bool ambient, int lo, int hi);
    // Advances one voice by one output frame; false once it has finished.
    bool next_sample_(Voice& v, int& sample);
    void retire_();

    bool paused_ = false;
    const SoundBank& bank_;
    std::map<std::string, MixerSound> sounds_;
    std::vector<Voice> voices_;

    int eva_clip_ = -1;
    std::size_t eva_pos_ = 0;
    std::deque<int> eva_queue_;

    std::uint32_t rng_;
    std::uint64_t age_counter_ = 0;
    int bus_gain_[3] = {1000, 1000, 1000};   // [P8] Sound / Voice / Score permille
    std::uint32_t next_handle_ = 1;
    std::shared_ptr<MusicSource> music_;
    std::vector<int16_t> music_buf_;
    // W2-GA-P4: every public entry point locks this (the SDL audio thread
    // renders while the game thread triggers sounds).
    mutable std::recursive_mutex mu_;
};

}  // namespace ra2engine

#include "audio_mixer.hpp"

#include <algorithm>
#include <cmath>

namespace ra2engine {

int priority_from_string(const std::string& p) {
    // docs/re/audio-playback.md table @0x815018 (High -- read verbatim from
    // .rdata): LOWEST=0, LOW=1, NORMAL=2, HIGH=3, CRITICAL=4, default 2.
    if (p == "lowest") return 0;
    if (p == "low") return 1;
    if (p == "normal") return 2;
    if (p == "high") return 3;
    if (p == "critical") return 4;
    return 2;  // unknown -> NORMAL, the table's own default
}

int SoundBank::add(std::vector<int16_t> pcm) {
    std::lock_guard<std::mutex> lk(*mu_);
    clips_.push_back(std::move(pcm));
    return static_cast<int>(clips_.size()) - 1;
}

const std::vector<int16_t>& SoundBank::pcm(int id) const {
    return clips_.at(static_cast<std::size_t>(id));
}

int SoundBank::count() const {
    std::lock_guard<std::mutex> lk(*mu_);
    return static_cast<int>(clips_.size());
}

// CONFIRMED linear falloff (docs/re/audio-playback.md FUN_0074f8e0, High):
// gain = 1 - dist/range; Type=GLOBAL floors the result at MinVolume (the
// max(vol,MinVolume) branch is GLOBAL-only in gamemd; MinVolume is ignored
// for every other Type). `range <= 0` is treated as "no falloff" (full
// gain) -- a defensive fallback for a malformed/zero Range rather than a
// divide-by-zero.
int Mixer::gain_permille(const MixerSound& s, double dist_cells) {
    int g = 1000;
    if (s.range > 0.0) {
        const double f = 1.0 - dist_cells / s.range;
        g = f <= 0.0 ? 0 : (f >= 1.0 ? 1000 : static_cast<int>(f * 1000.0));
    }
    if (s.global) g = std::max(g, s.min_volume * 10);
    // W5-E [RE: FUN_0074f8e0 tail, constant @0x7E7AE8 = 0.05]: a volume under
    // 0.05 is inaudible (the voice is not started / is stopped).
    return g < 50 ? 0 : g;
}

Mixer::Mixer(const SoundBank& bank, uint32_t seed)
    : bank_(bank), rng_(seed != 0 ? seed : 1u) {}
// xorshift32 with a zero state never leaves zero, so a caller-supplied seed
// of 0 is nudged to 1 -- audio-local RNG, deliberately never the sim's
// Randomizer (docs/re/audio-playback.md's RNG-determinism caveat).

void Mixer::register_sound(const std::string& key, const MixerSound& s) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    sounds_[key] = s;
}

std::uint32_t Mixer::next_rand_() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

// RandomRanged(lo, hi) on the audio-local RNG; no draw for an empty range,
// so sounds without VShift/FShift consume exactly the pre-W2 RNG sequence.
int Mixer::ranged_(int lo, int hi) {
    if (hi < lo) std::swap(lo, hi);
    if (hi == lo) return lo;
    return lo + static_cast<int>(next_rand_() % static_cast<std::uint32_t>(hi - lo + 1));
}

void Mixer::kill_oldest_with_key_(const std::string& key) {
    auto oldest = voices_.end();
    for (auto it = voices_.begin(); it != voices_.end(); ++it) {
        if (it->key != key || it->done) continue;
        if (oldest == voices_.end() || it->age < oldest->age) oldest = it;
    }
    if (oldest != voices_.end()) voices_.erase(oldest);
}

bool Mixer::play(const std::string& key, double dist_cells) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return start_(key, dist_cells, 8192) != 0;
}

bool Mixer::play_scaled(const std::string& key, double volume) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return start_(key, 0.0, 8192, volume) != 0;
}

const MixerSound* Mixer::sound(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = sounds_.find(key);
    return it == sounds_.end() ? nullptr : &it->second;
}

// [RE: FUN_004055c0] RandomRanged(AMBIENT ? 33 : Delay min, Delay max) ms; a
// draw under 33 ms starts at once. Audio-local RNG.
std::size_t Mixer::draw_delay_(bool ambient, int lo, int hi) {
    const int ms = ranged_(ambient ? 33 : lo, hi);
    if (ms < 33) return 0;
    return static_cast<std::size_t>(ms) * 22050u / 1000u;
}

std::uint32_t Mixer::play_at(const std::string& key, double dist_cells, int pan) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return start_(key, dist_cells, pan);
}

std::uint32_t Mixer::start_(const std::string& key, double dist_cells, int pan, double scale) {
    auto it = sounds_.find(key);
    if (it == sounds_.end() || it->second.variants.empty()) return 0;
    const MixerSound& s = it->second;
    int gain = gain_permille(s, dist_cells);
    if (scale != 1.0) gain = static_cast<int>(gain * std::max(0.0, std::min(1.0, scale)));
    if (gain <= 0) return 0;  // inaudible: don't burn a voice on silence

    int same = 0;
    for (const Voice& v : voices_)
        if (v.key == key && !v.done) ++same;
    int live = 0;
    for (const Voice& v : voices_)
        if (!v.done) ++live;

    if (s.limit > 0 && same >= s.limit) {
        // Reject-at-trigger: a documented approximation of vanilla's
        // start-then-cull-to-cap (docs/re/audio-playback.md FUN_004041d0,
        // "Decisions" section) -- both hold the same <=Limit invariant.
        if (!s.interrupt) return 0;
        kill_oldest_with_key_(key);  // interrupt: steal same-event channel (Medium)
    } else if (live >= kMaxVoices) {
        // Vanilla drops the new sound when the pool is full -- it never
        // evicts a playing voice, regardless of priority (docs/re/
        // audio-playback.md FUN_00405190, "Decisions" section).
        return 0;
    }

    // [RE: FUN_004048b0] Attack (first A) + body + Decay (last D) partition.
    // W7-12-24 [RE: FUN_0074F260 Attack= / Decay= counts; FUN_00406570 forces
    // 1 only when the Control bit is set and the count is 0]: the intro is a
    // random one of the first A samples, the outro one of the last D.
    const int count = static_cast<int>(s.variants.size());
    int a = !s.attack ? 0 : std::max(1, s.attack_n);
    int d = !s.decay ? 0 : std::max(1, s.decay_n);
    if (a >= count) a = count >= 2 ? count - 1 : 0;
    if (d > count - a - 1) d = std::max(0, count - a - 1);
    std::vector<int> seq;
    if (a) seq.push_back(s.variants[static_cast<std::size_t>(a == 1 ? 0 : ranged_(0, a - 1))]);
    int body = -1;
    if (s.all) {
        for (int i = a; i < count - d; ++i) seq.push_back(s.variants[static_cast<std::size_t>(i)]);
    } else if (a == 0 && d == 0) {
        body = s.variants[next_rand_() % s.variants.size()];   // the pre-W2 pick, unchanged
        seq.push_back(body);
    } else {
        body = s.variants[static_cast<std::size_t>(ranged_(a, count - d - 1))];
        seq.push_back(body);
    }
    // W7-12-24: a LOOP+ALL event repeats only its body (the outro on stop
    // was not traced, [SUSPECTED]); others end on the outro.
    if (d && !(s.loop && s.all)) seq.push_back(s.variants[static_cast<std::size_t>(d == 1 ? count - 1 : ranged_(count - d, count - 1))]);

    Voice v{key, seq.front(), 0, 0, s.priority, age_counter_++, s.bus,
            100, 8192, 0, 65536, 0, {}, 0, -1, false, 0, false, false, 0, 0};   // the member defaults, spelled out (-Wextra)
    // [RE: FUN_004055c0] VShift volume drop + FShift pitch.
    v.volume = s.vshift > 0 ? std::max(0, s.volume - s.volume * ranged_(0, std::min(100, s.vshift)) / 100)
                            : s.volume;
    v.vol_x_gain = v.volume * gain;
    if (s.fshift_lo != 0 || s.fshift_hi != 0) {
        const int pct = std::max(10, 100 + ranged_(s.fshift_lo, s.fshift_hi));
        v.step = static_cast<std::uint32_t>(pct * 65536 / 100);
    }
    v.pan = std::max(0, std::min(16384, pan));
    // v.clip = seq[0]; the rest follow in order. LOOP repeats the body clip
    // (or, with ALL, the whole sequence) until the voice is stopped.
    v.seq = seq;
    v.seq_ix = 1;
    v.loop_from = !s.loop ? -1 : (s.all ? -2 : (body >= 0 ? body : seq.front()));
    v.loop_start = static_cast<std::size_t>(a > 0 && seq.size() > 1 ? 1 : 0);
    if (s.predelay || s.ambient) {   // W5-E predelay
        v.predelay = s.predelay;
        v.ambient = s.ambient;
        v.delay_lo = s.delay_lo_ms;
        v.delay_hi = s.delay_hi_ms;
        v.delay = draw_delay_(s.ambient, s.delay_lo_ms, s.delay_hi_ms);
    }
    v.handle = next_handle_++;
    if (next_handle_ == 0) next_handle_ = 1;
    voices_.push_back(std::move(v));
    return voices_.back().handle;
}

bool Mixer::update_voice(std::uint32_t handle, double dist_cells, int pan) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    for (Voice& v : voices_) {
        if (v.handle != handle || v.done) continue;
        auto it = sounds_.find(v.key);
        const int gain = it == sounds_.end() ? 0 : gain_permille(it->second, dist_cells);
        if (gain <= 0) { v.done = true; return false; }   // left range: stopped
        v.vol_x_gain = v.volume * gain;
        v.pan = std::max(0, std::min(16384, pan));
        return true;
    }
    return false;
}

void Mixer::stop_voice(std::uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    for (Voice& v : voices_)
        if (v.handle == handle) v.done = true;
}

bool Mixer::voice_alive(std::uint32_t handle) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    for (const Voice& v : voices_)
        if (v.handle == handle && !v.done) return true;
    return false;
}

std::shared_ptr<MusicSource> Mixer::music() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return music_;
}

void Mixer::set_music(std::shared_ptr<MusicSource> src) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    music_ = std::move(src);
}

void Mixer::set_bus_gain(Bus bus, int permille) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    bus_gain_[static_cast<int>(bus)] = std::max(0, std::min(1000, permille));
}

int Mixer::bus_gain(Bus bus) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return bus_gain_[static_cast<int>(bus)];
}

void Mixer::play_eva(int clip_id) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (eva_clip_ == -1) {
        eva_clip_ = clip_id;
        eva_pos_ = 0;
    } else {
        eva_queue_.push_back(clip_id);
    }
}

void Mixer::stop_eva() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    eva_clip_ = -1;
    eva_pos_ = 0;
    eva_queue_.clear();
}

void Mixer::stop(const std::string& key) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.key == key; }),
                  voices_.end());
}

bool Mixer::playing(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    for (const Voice& v : voices_)
        if (v.key == key && !v.done) return true;
    return false;
}

int Mixer::active_voices() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    int n = 0;
    for (const Voice& v : voices_)
        if (!v.done) ++n;
    return n;
}
bool Mixer::eva_active() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return eva_clip_ != -1;
}
std::size_t Mixer::eva_queued() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return eva_queue_.size();
}

// One output frame of one voice: the current sample, then advance (at the
// FShift rate) through the clip, its follow-on sequence, and LOOP.
bool Mixer::next_sample_(Voice& v, int& sample) {
    if (v.delay > 0 && !v.done) {   // W5-E: predelay silence (the voice holds its slot)
        --v.delay;
        sample = 0;
        return true;
    }
    for (int guard = 0; guard < 4; ++guard) {
        if (v.done) return false;
        const std::vector<int16_t>& pcm = bank_.pcm(v.clip);
        if (v.pos < pcm.size()) {
            sample = pcm[v.pos];
            if (v.step == 65536) {
                ++v.pos;
            } else {
                v.frac += v.step;
                v.pos += v.frac >> 16;
                v.frac &= 0xFFFFu;
            }
            if (v.pos >= pcm.size() && v.seq_ix >= v.seq.size() && v.loop_from == -1) v.done = true;
            return true;
        }
        // Clip exhausted: next in sequence, else loop, else finished.
        v.pos = 0;
        v.frac = 0;
        if (v.seq_ix < v.seq.size()) {
            v.clip = v.seq[v.seq_ix++];
        } else if (v.loop_from >= 0) {
            v.clip = v.loop_from;
            if (v.predelay || v.ambient) {   // W5-E: each pass waits its own predelay
                v.delay = draw_delay_(v.ambient, v.delay_lo, v.delay_hi);
                if (v.delay > 0) { --v.delay; sample = 0; return true; }
            }
        } else if (v.loop_from == -2 && !v.seq.empty()) {
            v.seq_ix = std::min(v.loop_start, v.seq.size() - 1);   // W7-12-24: the body only
            v.clip = v.seq[v.seq_ix++];
            if (v.predelay || v.ambient) {
                v.delay = draw_delay_(v.ambient, v.delay_lo, v.delay_hi);
                if (v.delay > 0) { --v.delay; sample = 0; return true; }
            }
        } else {
            v.done = true;
            return false;
        }
    }
    v.done = true;
    return false;
}

void Mixer::retire_() {
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.done; }),
                  voices_.end());
}

void Mixer::set_paused(bool paused) { std::lock_guard<std::recursive_mutex> lock(mu_); paused_=paused; }
bool Mixer::paused() const { std::lock_guard<std::recursive_mutex> lock(mu_); return paused_; }

void Mixer::render(int16_t* out, std::size_t frames) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (paused_) { std::fill_n(out, frames, int16_t{0}); return; }
    std::lock_guard<std::mutex> bl(*bank_.mu_);
    if (music_ && music_buf_.size() < frames * 2) music_buf_.resize(frames * 2);
    const std::size_t mframes = music_ ? music_->read(music_buf_.data(), frames) : 0;
    for (std::size_t f = 0; f < frames; ++f) {
        std::int64_t acc = 0;

        for (Voice& v : voices_) {
            int s = 0;
            if (!next_sample_(v, s)) continue;
            // int64 intermediate: pcm(<=32767) * vol_x_gain(<=100000)
            // overflows int32, so this must not be done in 32-bit.
            std::int64_t c = static_cast<std::int64_t>(s) * v.vol_x_gain / 100000;
            // [P8] bus volume; identity (1000) keeps the exact pre-P8 value.
            const int bg = bus_gain_[static_cast<int>(v.bus)];
            if (bg != 1000) c = c * bg / 1000;
            acc += c;
        }

        if (eva_clip_ != -1) {
            const std::vector<int16_t>& pcm = bank_.pcm(eva_clip_);
            if (eva_pos_ < pcm.size()) {
                // EVA: full volume, no attenuation; [P8] scaled by the Voice
                // bus only when it is not identity.
                std::int64_t c = pcm[eva_pos_];
                const int vg = bus_gain_[static_cast<int>(Bus::Voice)];
                if (vg != 1000) c = c * vg / 1000;
                acc += c;
                ++eva_pos_;
            }
            if (eva_pos_ >= pcm.size()) {
                if (!eva_queue_.empty()) {
                    eva_clip_ = eva_queue_.front();
                    eva_queue_.pop_front();
                    eva_pos_ = 0;
                } else {
                    eva_clip_ = -1;
                    eva_pos_ = 0;
                }
            }
        }
        if (f < mframes) {
            std::int64_t c = (static_cast<int>(music_buf_[2 * f]) + music_buf_[2 * f + 1]) / 2;
            const int sg = bus_gain_[static_cast<int>(Bus::Score)];
            if (sg != 1000) c = c * sg / 1000;
            acc += c;
        }

        if (acc > 32767) acc = 32767;
        if (acc < -32768) acc = -32768;
        out[f] = static_cast<int16_t>(acc);
    }
    // W2-GA-23: finished voices are retired once per buffer, not per sample.
    retire_();
}

namespace {
// Soft limiter: linear below 3/4 full scale, tanh knee above, never clips.
inline int16_t soft_limit(float x) {
    const float t = 0.75f * 32767.0f;
    const float a = std::fabs(x);
    if (a > t) {
        const float k = 32767.0f - t;
        const float y = t + k * std::tanh((a - t) / k);
        x = x < 0 ? -y : y;
    }
    if (x > 32767.0f) x = 32767.0f;
    if (x < -32768.0f) x = -32768.0f;
    return static_cast<int16_t>(std::lrint(x));
}
}  // namespace

void Mixer::render_stereo(int16_t* lr, std::size_t frames) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (paused_) { std::fill_n(lr, frames * 2, int16_t{0}); return; }
    std::lock_guard<std::mutex> bl(*bank_.mu_);
    if (music_ && music_buf_.size() < frames * 2) music_buf_.resize(frames * 2);
    const std::size_t mframes = music_ ? music_->read(music_buf_.data(), frames) : 0;
    const float sg = bus_gain_[static_cast<int>(Bus::Score)] / 1000.0f;
    const float vg = bus_gain_[static_cast<int>(Bus::Voice)] / 1000.0f;
    for (std::size_t f = 0; f < frames; ++f) {
        float l = 0.f, r = 0.f;
        for (Voice& v : voices_) {
            int s = 0;
            if (!next_sample_(v, s)) continue;
            const float c = s * (v.vol_x_gain / 100000.0f) * (bus_gain_[static_cast<int>(v.bus)] / 1000.0f);
            // Balance law around the RE-Q11 pan (8192 = centre, 4096/12288 at
            // the screen edges, 0/16384 one screen-width off-centre).
            const float pl = std::min(1.0f, (16384 - v.pan) / 8192.0f);
            const float pr = std::min(1.0f, v.pan / 8192.0f);
            l += c * pl;
            r += c * pr;
        }
        if (eva_clip_ != -1) {
            const std::vector<int16_t>& pcm = bank_.pcm(eva_clip_);
            if (eva_pos_ < pcm.size()) {
                const float c = pcm[eva_pos_] * vg;   // EVA is not positional: centred
                l += c;
                r += c;
                ++eva_pos_;
            }
            if (eva_pos_ >= pcm.size()) {
                if (!eva_queue_.empty()) {
                    eva_clip_ = eva_queue_.front();
                    eva_queue_.pop_front();
                } else {
                    eva_clip_ = -1;
                }
                eva_pos_ = 0;
            }
        }
        if (f < mframes) {
            l += music_buf_[2 * f] * sg;
            r += music_buf_[2 * f + 1] * sg;
        }
        lr[2 * f] = soft_limit(l);
        lr[2 * f + 1] = soft_limit(r);
    }
    retire_();
}

}  // namespace ra2engine

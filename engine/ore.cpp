#include "ore.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <cmath>

namespace ra2engine {

namespace {
// [RE-GATE APPLIED x2: economy.md §5, RESOLVED] Overlay bases, order-of-
// appearance positions in [OverlayTypes]: GEM01=27, TIB01=102, TIB2_01=127,
// TIB3_01=147. Each ORE tiberium's block is NumImages=12 flat density frames
// followed by NumSlopes=8 slope frames (FUN_005FD510's two-range test) --
// width 20, [base, base+20). GEM's block has only the 12 flat images (no
// slope range: "GEM has only the 12 flat images -- the slope range is
// empty/unused for gems") -- width 12, [base, base+12).
struct TibBase {
    int base;
    int width;
    OreKind kind;
};

constexpr TibBase kBases[] = {
    {27, 12, OreKind::Gems},    // GEM01..    [27,39)
    {102, 20, OreKind::Ore},    // TIB01..    [102,122)  (Riparius)
    {127, 20, OreKind::Ore},    // TIB2_01..  [127,147)
    {147, 20, OreKind::Ore},    // TIB3_01..  [147,167)
};
}  // namespace

OreKind overlay_ore_kind(int overlay_index) {
    for (const auto& b : kBases) {
        if (overlay_index >= b.base && overlay_index < b.base + b.width) {
            return b.kind;
        }
    }
    return OreKind::None;
}

int overlay_ore_variant(int overlay_index) {
    for (const auto& b : kBases) {
        if (overlay_index >= b.base && overlay_index < b.base + b.width) {
            int off = overlay_index - b.base;
            // [RE-GATE APPLIED: economy.md §5] slope frames (off >= 12) are
            // unused this phase (slope art not modeled) -- clamp to variant 0
            // rather than an out-of-range flat index.
            return off < 12 ? off : 0;
        }
    }
    return -1;
}

std::uint32_t OreRng::next() {
    if (s == 0) s = 1;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int OreRng::below(int n) {
    return (int)(next() % (std::uint32_t)n);
}

OreField::OreField(int w, int h)
    : w_(w), h_(h), kind_((size_t)w * h, (std::uint8_t)OreKind::None),
      density_((size_t)w * h, 0), variant_((size_t)w * h, 0) {}

OreField OreField::from_map(const ra2assets::MapFile& map, int grid_w, int grid_h) {
    OreField f(grid_w, grid_h);
    for (const auto& c : map.cells()) {
        if (!f.in(c.x, c.y)) continue;
        int overlay = map.overlay_at(c.x, c.y);
        OreKind kind = overlay_ore_kind(overlay);
        if (kind == OreKind::None) continue;
        int variant = overlay_ore_variant(overlay);
        int density = map.overlay_data_at(c.x, c.y);
        density = std::max(0, std::min(density, kMaxOreDensity));
        f.set(c.x, c.y, kind, density, variant);
    }
    return f;
}

int OreField::w() const { return w_; }
int OreField::h() const { return h_; }

bool OreField::in(int x, int y) const {
    return x >= 0 && y >= 0 && x < w_ && y < h_;
}

OreKind OreField::kind_at(int x, int y) const {
    if (!in(x, y)) return OreKind::None;
    return (OreKind)kind_[(size_t)x + (size_t)y * w_];
}

int OreField::density_at(int x, int y) const {
    if (!in(x, y)) return 0;
    if (kind_at(x, y) == OreKind::None) return 0;
    return density_[(size_t)x + (size_t)y * w_];
}

int OreField::variant_at(int x, int y) const {
    if (!in(x, y)) return 0;
    return variant_[(size_t)x + (size_t)y * w_];
}

bool OreField::has_ore(int x, int y) const {
    return kind_at(x, y) != OreKind::None;
}

void OreField::set(int x, int y, OreKind k, int density, int variant) {
    if (!in(x, y)) return;
    size_t i = (size_t)x + (size_t)y * w_;
    kind_[i] = (std::uint8_t)k;
    density_[i] = (std::uint8_t)std::max(0, std::min(density, kMaxOreDensity));
    variant_[i] = (std::uint8_t)variant;
}

OreKind OreField::take_bale(int x, int y) {
    if (!in(x, y)) return OreKind::None;
    size_t i = (size_t)x + (size_t)y * w_;
    OreKind k = (OreKind)kind_[i];
    if (k == OreKind::None) return OreKind::None;
    // [RE-GATE: economy.md §1] one gulp removes one density step; the cell
    // empties (kind -> None) when density hits 0.
    if (density_[i] > 0) --density_[i];
    if (density_[i] == 0) {
        kind_[i] = (std::uint8_t)OreKind::None;
    }
    return k;
}

void OreField::step_original(double dt, double growth_pct, double spread_pct,
                             const std::vector<std::uint8_t>& passable, OreRng& rng,
                             const Occupancy* occ) {
    static const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    auto can_hold = [&](int x, int y) {   // FUN_004837F0
        return in(x, y) && kind_at(x, y) == OreKind::None && !is_mine(x, y) &&
               passable[(size_t)x + (size_t)y * w_] != 0;
    };
    // Net L0: the original's growth / spread clocks count FRAMES; dt*15 is
    // exactly 1 per logic tick, so these sums stay exact integers (a running
    // sum of 1/15 s drifted against growth_frames/15).
    timer_ += dt * 15.0;
    const double g_frames = growth_frames_;
    while (growth_frames_ > 0 && timer_ >= g_frames) {
        timer_ -= g_frames;
        if (growth_pct < 1e-5) continue;
        std::vector<int> cand;
        for (int i = 0; i < w_ * h_; ++i)
            if (kind_[i] == (std::uint8_t)OreKind::Ore && density_[i] < kMaxOreDensity) cand.push_back(i);
        if (cand.empty()) continue;
        const int cap = std::max(5, std::min(50, om::ftol((cand.size() * growth_pct))));
        int n = rng.below(cap) + 1;
        for (; n > 0 && !cand.empty(); --n) {
            const int j = rng.below((int)cand.size());
            ++density_[cand[(size_t)j]];
            cand.erase(cand.begin() + j);
        }
    }
    spread_timer_ += dt * 15.0;
    const double s_frames = spread_frames_;
    while (spread_frames_ > 0 && spread_timer_ >= s_frames) {
        spread_timer_ -= s_frames;
        if (spread_pct < 1e-5) continue;
        std::vector<int> cand;
        for (int i = 0; i < w_ * h_; ++i) {
            if (kind_[i] != (std::uint8_t)OreKind::Ore || density_[i] == 0) continue;   // density > tib/2
            const int x = i % w_, y = i / w_;
            if (occ && !occ->is_free(x, y)) continue;   // an object on the cell
            bool open = false;
            for (int d = 0; d < 8 && !open; ++d) open = can_hold(x + dx[d], y + dy[d]);
            if (open) cand.push_back(i);
        }
        if (cand.empty()) continue;
        const int cap = std::max(5, std::min(25, om::ftol((cand.size() * spread_pct))));
        int n = rng.below(cap) + 1;
        for (; n > 0 && !cand.empty(); --n) {
            const int j = rng.below((int)cand.size());
            const int i = cand[(size_t)j];
            cand.erase(cand.begin() + j);
            const int sx = i % w_, sy = i / w_;
            const int start = rng.below(8);
            for (int d = 0; d < 8; ++d) {
                const int nx = sx + dx[(start + d) & 7], ny = sy + dy[(start + d) & 7];
                if (!can_hold(nx, ny)) continue;
                set(nx, ny, (OreKind)kind_[i], kMineSpawnDensity, rng.below(12));
                break;
            }
        }
    }
}

void OreField::step(double dt, double interval_secs, double growth_pct, double spread_pct,
                     const std::vector<std::uint8_t>& passable, OreRng& rng, const Occupancy* occ) {
    if (growth_frames_ > 0 || spread_frames_ > 0) {
        step_original(dt, growth_pct, spread_pct, passable, rng, occ);
        return;
    }
    // The legacy seconds-based owner has no timer to advance when disabled.
    // Zero/negative intervals otherwise spin forever without consuming time.
    if (!(interval_secs > 0.0) || !std::isfinite(interval_secs)) return;
    timer_ += dt;
    while (timer_ >= interval_secs) {
        timer_ -= interval_secs;
        // -- growth [RE-GATE: economy.md §4] --
        std::vector<int> cand;
        for (int i = 0; i < w_ * h_; ++i)
            if (kind_[i] == (std::uint8_t)OreKind::Ore && density_[i] < kMaxOreDensity)
                cand.push_back(i);
        if (!cand.empty() && growth_pct > 0.0) {
            int n = std::max(1, om::ftol((cand.size() * growth_pct)));
            for (int k = 0; k < n && !cand.empty(); ++k) {
                int j = rng.below((int)cand.size());
                ++density_[cand[j]];
                cand.erase(cand.begin() + j);
            }
        }
        // -- spread [RE-GATE: economy.md §4] --
        std::vector<int> src;
        for (int i = 0; i < w_ * h_; ++i)
            if (kind_[i] == (std::uint8_t)OreKind::Ore && density_[i] == kMaxOreDensity)
                src.push_back(i);
        if (!src.empty() && spread_pct > 0.0) {
            int n = std::max(1, om::ftol((src.size() * spread_pct)));
            for (int k = 0; k < n; ++k) {
                int i = src[rng.below((int)src.size())];
                int sx = i % w_, sy = i / w_;
                static const int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
                static const int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
                int start = rng.below(8);
                for (int d = 0; d < 8; ++d) {
                    int nx = sx + dx[(start + d) % 8], ny = sy + dy[(start + d) % 8];
                    if (!in(nx, ny) || kind_at(nx, ny) != OreKind::None) continue;
                    if (!passable[(size_t)nx + (size_t)ny * w_]) continue;
                    set(nx, ny, OreKind::Ore, 1, variant_at(sx, sy));
                    break;
                }
            }
        }
    }
}

double OreField::timer() const { return timer_; }

void OreField::add_mine(int x, int y, int half_frames, int anim_rate, double anim_prob) {
    if (!in(x, y) || is_mine(x, y)) return;
    OreMine m;
    m.x = x;
    m.y = y;
    m.half_frames = std::max(1, half_frames);
    m.anim_rate = std::max(1, anim_rate);
    m.anim_prob = anim_prob;
    mines_.push_back(m);
}

bool OreField::is_mine(int x, int y) const {
    for (const OreMine& m : mines_)
        if (m.x == x && m.y == y) return true;
    return false;
}

int OreField::step_mines(int frames, const std::vector<std::uint8_t>& passable, OreRng& rng) {
    // DAT_0089E5F8 neighbour order [RE-FALLBACK: the table's values are the
    // harvest.cpp ring, N,NE,E,SE,S,SW,W,NW].
    static const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    int spawned = 0;
    for (int f = 0; f < frames; ++f) {
        for (OreMine& m : mines_) {
            // TerrainClass::AI: not animating -> roll AnimationProbability.
            if (m.rate == 0 && (double)rng.below(1000000) * 1e-6 < m.anim_prob) {
                m.stage = 0;
                m.rate = m.anim_rate;
                m.timer = m.anim_rate;
            }
            if (m.rate == 0) continue;
            if (--m.timer > 0) continue;
            ++m.stage;
            m.timer = m.rate;
            if (m.stage != m.half_frames) continue;
            // Animation end -> stop + CellClass forced spread (one cell).
            m.stage = 0;
            m.rate = 0;
            const OreKind k = kind_at(m.x, m.y) == OreKind::None ? OreKind::Ore : kind_at(m.x, m.y);
            const int start = rng.below(8);
            for (int d = 0; d < 8; ++d) {
                const int nx = m.x + dx[(start + d) & 7], ny = m.y + dy[(start + d) & 7];
                if (!in(nx, ny) || kind_at(nx, ny) != OreKind::None || is_mine(nx, ny)) continue;
                if (!passable[(size_t)nx + (size_t)ny * w_]) continue;
                set(nx, ny, k, kMineSpawnDensity, rng.below(12));   // FUN_0065C220(0,11) art
                ++m.spawned;
                ++spawned;
                break;
            }
        }
    }
    return spawned;
}

}  // namespace ra2engine

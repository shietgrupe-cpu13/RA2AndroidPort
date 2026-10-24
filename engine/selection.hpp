#pragma once
// NET L1: the local player's unit selection -- UI state, NOT simulation state.
// It used to be Unit::selected (a World field, saved and hashed); in a lockstep
// game every peer has its own selection, so it lives with the local Input and
// orders carry explicit unit ids (engine/command.hpp).
//
// prune() drops units no longer available to player commands, including
// carried/consumed units, launched aircraft, and enslaved workers.

#include <cstdint>
#include <vector>

#include "world.hpp"

namespace ra2engine {

class Selection {
public:
    bool has(int i) const { return i >= 0 && i < static_cast<int>(f_.size()) && f_[static_cast<std::size_t>(i)]; }
    void set(int i, bool on = true) {
        if (i < 0) return;
        if (i >= static_cast<int>(f_.size())) {
            if (!on) return;
            f_.resize(static_cast<std::size_t>(i) + 1, 0);
        }
        f_[static_cast<std::size_t>(i)] = on ? 1 : 0;
    }
    void toggle(int i) { set(i, !has(i)); }
    void clear() { f_.clear(); }
    bool empty() const {
        for (std::uint8_t b : f_)
            if (b) return false;
        return true;
    }
    int count() const {
        int n = 0;
        for (std::uint8_t b : f_) n += b ? 1 : 0;
        return n;
    }
    // Selected unit ids, ascending (the order every group order iterates in).
    std::vector<int> ids() const {
        std::vector<int> v;
        for (std::size_t i = 0; i < f_.size(); ++i)
            if (f_[i]) v.push_back(static_cast<int>(i));
        return v;
    }
    // Lowest selected index, or -1.
    int lowest() const {
        for (std::size_t i = 0; i < f_.size(); ++i)
            if (f_[i]) return static_cast<int>(i);
        return -1;
    }
    // Whether the sim removed unit u from what a player can hold selected.
    static bool unselectable(const Unit& u) {
        return u.cargo_of >= 0 || u.w7.slave_manager != -1 ||
               (!u.alive && u.die_time < 0.0) || u.an.t.spawned;
    }
    void prune(const World& w) {
        const std::vector<Unit>& us = w.units();
        if (f_.size() > us.size()) f_.resize(us.size());
        for (std::size_t i = 0; i < f_.size(); ++i)
            if (f_[i] && unselectable(us[i])) f_[i] = 0;
    }
    bool operator==(const Selection& o) const { return ids() == o.ids(); }

private:
    std::vector<std::uint8_t> f_;
};

}  // namespace ra2engine

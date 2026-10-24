#include "input.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>

#include "flight.hpp"         // altitude_to_screen_dy (CB-2 raised-hitbox pick)
#include "iso_project.hpp"    // screen_to_cell
#include "ra2assets/map.hpp"  // MapCell (level lookup for the level-aware ground target)

namespace ra2engine {

static constexpr int kTileW = 60;
static constexpr int kTileH = 30;
static constexpr int kLevelPx = 15;
static constexpr int kMaxPickLevel = 18;   // R-210: highest standing height picked (Level 14 + a deck)

// Selection hit box around a unit's on-screen ground point (its feet): about
// half a tile to each side, most of a sprite height up, a little below.
// Restored from the Phase-2 tap hit-test: picking must be done in SCREEN
// space against the LEVEL-AWARE ground point the sprite is actually drawn at
// -- a bare screen_to_cell inversion is level-blind and resolves a full cell
// off for a unit standing on level-2 terrain.
static constexpr int kHitHalfW = 32;
static constexpr int kHitUp = 46;
static constexpr int kHitDown = 16;

// Pointer travel (window px) past which a left press becomes a selection-box
// drag rather than a click.
static constexpr int kDragThreshold = 6;
// Double-click window (ms) and max pixel slop between the two clicks.
static constexpr uint32_t kDblMs = 400;
static constexpr int kDblSlop = 6;

// W7-01-04 / W7-01-33: the first selected own engineer (Engineer= type
// flag), whatever the selection size [RE: Mouse_Left_Release FUN_004AE660
// gives every selected object its own What_Action], or -1.
static int sel_engineer_idx(const World& world, const Selection& sel) {
    for (int i : sel.ids()) {
        if (i >= static_cast<int>(world.units().size())) break;
        const Unit& u = world.units()[i];
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || !world.controllable_unit(i)) continue;
        if (world.unit_is_engineer(u)) return i;
    }
    return -1;
}

// Would World::order_defuse accept (eng, unit_target, b_target)? (its own
// guards, without the mutation -- the gesture decides before the command is
// queued, the apply re-checks).
static bool defuse_possible(const World& w, int eng, int unit_target, int b_target) {
    if (eng < 0 || eng >= static_cast<int>(w.units().size())) return false;
    const Unit& e = w.units()[static_cast<std::size_t>(eng)];
    if (!e.alive || e.die_time >= 0.0) return false;
    if (unit_target >= 0 && (unit_target >= static_cast<int>(w.units().size()) ||
                             w.units()[static_cast<std::size_t>(unit_target)].bomb_frame < 0))
        return false;
    if (b_target >= 0 && (b_target >= static_cast<int>(w.buildings().size()) ||
                          w.buildings()[static_cast<std::size_t>(b_target)].bomb_frame < 0))
        return false;
    return unit_target >= 0 || b_target >= 0;
}

void Input::emit(Command c) {
    // NET L1: every order leaves Input as a Command. In the game the sink is
    // the scene's scheduler (applied at the next tick start); without one
    // (unit tests, tools) it is applied to the World at once.
    if (sink_) {
        sink_(std::move(c));
        return;
    }
    struct RulesHost : CommandHost {
        const OrderRules* r;
        explicit RulesHost(const OrderRules* rr) : r(rr) {}
        const OrderRules* order_rules() const override { return r; }
    } host(&rules_);
    c.house = static_cast<std::int8_t>(world_.order_issuer());
    apply_command(world_, c, &host);
}

Input::Input(World& world, Camera& camera, int view_w, int view_h)
    : world_(world), cam_(camera), view_w_(view_w), view_h_(view_h) {
    int mnx, mny, mxx, mxy;
    if (terrain_screen_bounds(world_.map(), mnx, mny, mxx, mxy)) {
        const int margin_x = view_w_ / 2;
        const int margin_y = view_h_ / 2;
        min_cam_x_ = -margin_x - mxx;
        max_cam_x_ = view_w_ + margin_x - mnx;
        min_cam_y_ = -margin_y - mxy;
        max_cam_y_ = view_h_ + margin_y - mny;
    } else {
        min_cam_x_ = INT_MIN; max_cam_x_ = INT_MAX;
        min_cam_y_ = INT_MIN; max_cam_y_ = INT_MAX;
    }
}

void Input::clamp_camera() {
    if (cam_.x < min_cam_x_) cam_.x = min_cam_x_;
    else if (cam_.x > max_cam_x_) cam_.x = max_cam_x_;
    if (cam_.y < min_cam_y_) cam_.y = min_cam_y_;
    else if (cam_.y > max_cam_y_) cam_.y = max_cam_y_;
}

void Input::pan(int dx, int dy) {
    cam_.x += dx;
    cam_.y += dy;
    clamp_camera();
}

void Input::unit_ground_px(const Unit& u, int& gx, int& gy) const {
    const double hw = kTileW / 2.0;
    const double hh = kTileH / 2.0;
    // W5-B R-061: the infantry sub-cell spot offset (as draw_unit).
    const double fx = u.fx + u.sub_x / 256.0, fy = u.fy + u.sub_y / 256.0;
    gx = static_cast<int>(std::lround((fx - fy) * hw)) + kTileW / 2 + cam_.x;
    gy = static_cast<int>(std::lround((fx + fy) * hh - u.level * kLevelPx)) + kTileH / 2 +
         cam_.y;
}

int Input::unit_under(int px, int py) const {
    // Screen-space pick: test the click against every unit's hit box around
    // its level-aware ground point (unit_ground_px -- identical anchor to
    // draw_unit). Among units whose box contains the click, the NEAREST
    // ground point wins; ties break to the lowest index (deterministic).
    // Corpses are unpickable ANYWHERE this is used -- including the
    // right-click attack path -- so the alive/die_time check lives here once
    // rather than being repeated by every caller.
    const std::vector<Unit>& us = world_.units();
    int best = -1;
    long best_d2 = 0;
    for (std::size_t i = 0; i < us.size(); ++i) {
        const Unit& u = us[i];
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;  // dead/dying/
                                                     // CARRIED: unpickable (CB-1)
        int gx, gy;
        unit_ground_px(u, gx, gy);
        // CB-2 Aircraft [RE: aircraft.md §3]: an airborne unit is drawn RAISED
        // by altitude_to_screen_dy, so its hit box lives at the raised screen
        // position; shift the ground point up to match. A grounded aircraft
        // (altitude 0) picks normally. Interactive-only -- the scripted demos
        // don't hit-test, so this never affects a gate.
        if (u.is_aircraft && u.altitude > 0.0)
            gy -= ra2engine::altitude_to_screen_dy(u.altitude);
        if (px < gx - kHitHalfW || px > gx + kHitHalfW) continue;
        if (py < gy - kHitUp || py > gy + kHitDown) continue;
        const long dx = px - gx, dy = py - gy;
        const long d2 = dx * dx + dy * dy;
        if (best < 0 || d2 < best_d2) {
            best = static_cast<int>(i);
            best_d2 = d2;
        }
    }
    return best;
}

void Input::resolve_ground_cell(int px, int py, int& cx, int& cy) const {
    // R-210 [RE-FALLBACK: the TS-lineage click-cell calculation was not traced]:
    // the visible surface under the pointer. Two cells project onto the same
    // pixel when one stands k levels above the other k cells further forward
    // (a cliff top over the river below it); the higher one is drawn in
    // front. Scan the heights top-down and take the first cell whose own
    // standing height (Level, + 4 on an intact high-bridge deck) is that
    // height -- the front-most surface. The old two-pass inversion picked the
    // river cell BEHIND a cliff edge, so a click on the edge ordered a drop.
    for (int h = kMaxPickLevel; h >= 1; --h) {
        int hx = 0, hy = 0;
        screen_to_cell(px - cam_.x - kTileW / 2, py - cam_.y - kTileH / 2 + h * kLevelPx, kTileW, kTileH, hx, hy);
        const ra2assets::MapCell* c = world_.map().at(hx, hy);
        if (!c) continue;
        const bool deck = world_.high_bridge_cell(hx, hy) && !world_.high_bridge_destroyed(hx, hy);
        if (c->level == h || (deck && c->level + 4 == h)) {
            cx = hx;
            cy = hy;
            return;
        }
    }
    // LEVEL-AWARE two-pass inversion, identical to the right-click move path
    // (see handle()'s PointerDown/Right branch for the full rationale). Pass 1
    // inverts at level 0; if the resolved cell's real terrain level > 0, add
    // the raise back to py and re-invert once.
    int cx0 = 0, cy0 = 0;
    screen_to_cell(px - cam_.x - kTileW / 2, py - cam_.y - kTileH / 2, kTileW, kTileH, cx0, cy0);
    const ra2assets::MapCell* cell0 = world_.map().at(cx0, cy0);
    const int level = cell0 ? cell0->level : 0;
    cx = cx0;
    cy = cy0;
    if (level > 0) {
        screen_to_cell(px - cam_.x - kTileW / 2, py - cam_.y - kTileH / 2 + level * kLevelPx,
                       kTileW, kTileH, cx, cy);
    }
}

void Input::begin_placement(int roster_type, int fw, int fh) {
    place_type_ = roster_type;
    place_fw_ = fw > 0 ? fw : 1;
    place_fh_ = fh > 0 ? fh : 1;
    // Any in-flight left gesture is abandoned when placement begins.
    l_down_ = false;
    dragging_ = false;
}

void Input::cancel_placement() {
    place_type_ = -1;
    place_fw_ = place_fh_ = 1;
}

namespace {
// The shared body of SetRepairMode/SetSellMode [RE: repair-sell.md §C1] --
// both setters are the same shape in the binary.
Input::BaseMode next_mode(Input::BaseMode cur, Input::BaseMode want, int control) {
    const bool on = (cur == want);
    bool next_on = on;
    if (control < 0) next_on = !on;
    else if (control == 0) next_on = false;
    else next_on = true;
    if (next_on) return want;
    return on ? Input::BaseMode::None : cur;   // turning `want` off leaves a sibling alone
}
}  // namespace

void Input::set_repair_mode(int control) { set_mode_(BaseMode::Repair, control); }
void Input::set_sell_mode(int control) { set_mode_(BaseMode::Sell, control); }
void Input::set_power_mode(int control) { set_mode_(BaseMode::PowerToggle, control); }

void Input::set_mode_(BaseMode want, int control) {
    const BaseMode next = next_mode(mode_, want, control);
    if (next == mode_) return;                       // no-op if unchanged
    // PLACEMENT WINS: every setter early-returns while PendingObject != 0.
    if (placement_active()) return;
    // Every setter clears the other modes.
    sw_targeting_ = false;
    if (next == BaseMode::None) {
        mode_ = BaseMode::None;
        return;
    }
    // Arming requires at least one owned building (§D5); otherwise the button
    // is dead.
    if (owned_buildings_ <= 0) {
        mode_ = BaseMode::None;
        return;
    }
    mode_ = next;
    // Reset_Cursor (FUN_0048DBA0): arming a mode DESELECTS EVERYTHING.
    sel_.clear();
    selected_building_ = -1;
    l_down_ = false;
    dragging_ = false;
}

bool Input::cancel_one_mode() {
    // [RE: repair-sell.md §C1] EXACTLY ONE thing per press, in priority order.
    if (placement_active()) {
        actions_.place_cancelled = true;
        cancel_placement();
        return true;
    }
    if (mode_ == BaseMode::Repair) { mode_ = BaseMode::None; return true; }
    if (mode_ == BaseMode::Sell) { mode_ = BaseMode::None; return true; }
    if (mode_ == BaseMode::PowerToggle) { mode_ = BaseMode::None; return true; }   // W5-E2
    if (sw_targeting_) { sw_targeting_ = false; return true; }
    // (PlanningMode, PlaceBeaconMode would sit here -- not modeled.)
    return false;
}

int Input::building_art_at(int px, int py) const {
    if (!art_hit_) return -1;
    const std::vector<Building>& bs = world_.buildings();
    int best = -1;
    double best_key = 0.0;
    for (int i = 0; i < static_cast<int>(bs.size()); ++i) {
        const Building& b = bs[i];
        if (!b.alive || b.state == Building::State::Dead) continue;
        if (!art_hit_(i, px, py)) continue;
        // render_world's building depth key: the front-most (largest key,
        // later index on a tie -- drawn last) wins.
        const double key = (b.cx + (b.fw - 1) / 2.0) + (b.cy + (b.fh - 1) / 2.0);
        if (best < 0 || key >= best_key) {
            best = i;
            best_key = key;
        }
    }
    return best;
}

int Input::pick_building(int px, int py, bool touch) const {
    if (touch) {   // W6-01-01 [engine deviation]: a finger picks by the drawn art
        const int b = building_art_at(px, py);
        if (b >= 0) return b;
    }
    // [RE: FUN_006d98f0] the drawn-object list: each alive building is
    // registered at the screen point of its Location (the origin cell's
    // centre at its ground level, FUN_006d9250); the nearest one with
    // dx^2 + 0.5*dy^2 < 200 wins.
    const std::vector<Building>& bs = world_.buildings();
    int best = -1;
    double best_d = 200.0;
    for (int i = 0; i < static_cast<int>(bs.size()); ++i) {
        const Building& b = bs[i];
        if (!b.alive || b.state == Building::State::Dead) continue;
        const ra2assets::MapCell* mc = world_.map().at(b.cx, b.cy);
        const int level = mc ? mc->level : 0;
        const int hx = (b.cx - b.cy) * (kTileW / 2) + kTileW / 2 + cam_.x;
        const int hy = (b.cx + b.cy) * (kTileH / 2) + kTileH / 2 - level * kLevelPx + cam_.y;
        const double dx = px - hx, dy = py - hy;
        const double d = dx * dx + 0.5 * dy * dy;   // [RE: 0x7e0738 = 0.5]
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best >= 0) return best;
    int cx = 0, cy = 0;
    resolve_ground_cell(px, py, cx, cy);
    return world_.building_at_cell(cx, cy);   // already skips !alive / Dead
}

bool Input::unit_body_hit(int idx, int px, int py) const {
    const std::vector<Unit>& us = world_.units();
    if (idx < 0 || idx >= static_cast<int>(us.size())) return false;
    const Unit& u = us[idx];
    int gx, gy;
    unit_ground_px(u, gx, gy);
    if (u.is_aircraft && u.altitude > 0.0) gy -= ra2engine::altitude_to_screen_dy(u.altitude);
    // The original's drawn-object hotspot (FUN_006d98f0: dx^2 + 0.5*dy^2 <
    // 200). [RE-FALLBACK: the point FUN_006d9460 registers for a foot unit
    // (vtbl+0x1C0) was not traced; the body centre is taken 10 px above the
    // ground point, where the sprite's torso / hull is drawn.]
    const double dx = px - gx, dy = py - (gy - 10);
    return dx * dx + 0.5 * dy * dy < 200.0;
}

void Input::placement_cell(int& cx, int& cy) const {
    resolve_ground_cell(cur_x_, cur_y_, cx, cy);
    cx -= place_fw_ / 2;
    cy -= place_fh_ / 2;
}

int Input::lowest_selected() const { return sel_.lowest(); }

bool Input::selection_box(int& x0, int& y0, int& x1, int& y1) const {
    if (!(l_down_ && dragging_)) return false;
    x0 = std::min(down_x_, cur_x_);
    x1 = std::max(down_x_, cur_x_);
    y0 = std::min(down_y_, cur_y_);
    y1 = std::max(down_y_, cur_y_);
    return true;
}

bool Input::commit_factory_click(int px, int py) {
    // Resolve the clicked ground cell, then the building (if any) on it. The
    // factory gesture is OWN-HOUSE GATED [Task 5 review]: only an alive,
    // house-0 building whose type is a unit factory can ever be selected,
    // rallied, or primed -- an enemy factory click falls through to normal
    // selection (and clears any prior factory selection).
    const int b = pick_building(px, py, ev_touch_);   // W6-01-01
    if (b >= 0 && world_.buildings()[b].house == world_.order_issuer() && world_.buildings()[b].alive &&
        is_unit_factory_type(world_.buildings()[b].type)) {
        if (selected_building_ == b) {
            // Re-click the selected factory -> set primary [RE: production.md
            // §1.2, RE-FALLBACK: original toggles by right-click; our right-
            // click sets the rally, so LEFT re-click primes]. Resolve the
            // building's category so set_primary clears only its sibling group
            // (OrderRules::primary_group, at the PRIMARY event's apply).
            const bool was_primary = world_.buildings()[b].primary;
            emit(make_building_event(EventType::Primary, b));
            if (!was_primary) {   // W5-E EVA_PrimaryBuildingSelected
                actions_.primary_set = true;
                actions_.factory = b;
            }
        } else {
            selected_building_ = b;
            sel_.clear();  // factory selection clears the unit selection
        }
        return true;
    }
    selected_building_ = -1;  // click NOT on an own factory: drop the selection
    return false;
}

void Input::commit_click(int px, int py, bool shift) {
    // Left-click never selects a hostile: a pick that lands on a house != 0
    // unit is treated exactly like a pick on empty ground (falls through to
    // the clear-selection branch below).
    int idx = unit_under(px, py);
    if (idx >= 0 && !world_.controllable_unit(idx)) idx = -1;
    if (idx >= 0) {
        if (shift) {
            sel_.toggle(idx);  // toggle
        } else {
            sel_.clear();
            sel_.set(idx);
        }
    } else if (!shift) {
        sel_.clear();  // click on empty ground (or a hostile) clears (Shift+empty = no-op)
    }
    const int lo = lowest_selected();
    if (lo >= 0) { actions_.selected = true; actions_.actor = lo; }
}

bool Input::box_has_own_unit() const {
    const int x0 = std::min(down_x_, cur_x_), x1 = std::max(down_x_, cur_x_);
    const int y0 = std::min(down_y_, cur_y_), y1 = std::max(down_y_, cur_y_);
    const auto& units = world_.units();
    for (std::size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (!world_.controllable_unit(static_cast<int>(i)) ||
            !u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
        int gx, gy;
        unit_ground_px(u, gx, gy);
        if (gx >= x0 && gx <= x1 && gy >= y0 && gy <= y1) return true;
    }
    return false;
}

void Input::commit_box(bool shift) {
    int x0, y0, x1, y1;
    x0 = std::min(down_x_, cur_x_); x1 = std::max(down_x_, cur_x_);
    y0 = std::min(down_y_, cur_y_); y1 = std::max(down_y_, cur_y_);
    if (!shift) sel_.clear();  // plain box replaces; Shift+box adds
    for (std::size_t i = 0; i < world_.units().size(); ++i) {
        const Unit& u = world_.units()[i];
        // A box drag never selects hostiles, corpses, or CARRIED units --
        // house != 0, dead/dying, and cargo_of >= 0 (aboard a transport,
        // off-map [CB-1]) are skipped even if their ground point falls
        // inside the dragged rectangle.
        if (!world_.controllable_unit(static_cast<int>(i)) ||
            !u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
        int gx, gy;
        unit_ground_px(u, gx, gy);
        if (gx >= x0 && gx <= x1 && gy >= y0 && gy <= y1) sel_.set(static_cast<int>(i));
    }
    const int lo = lowest_selected();
    if (lo >= 0) { actions_.selected = true; actions_.actor = lo; }
}

void Input::commit_double(int px, int py) {
    // Same house-0-only rule as commit_click: a double-click landing on a
    // hostile does nothing (no type-select, no selection change).
    const int idx = unit_under(px, py);
    if (idx < 0 || !world_.controllable_unit(idx)) return;
    const std::string type = world_.units()[idx].image;
    sel_.clear();
    for (std::size_t i = 0; i < world_.units().size(); ++i) {
        const Unit& u = world_.units()[i];
        // The "same type" scan is also house-0-only and skips corpses AND
        // CARRIED units (a hostile, a dead unit, or one aboard a transport
        // (cargo_of >= 0 [CB-1]) sharing the clicked unit's image must never
        // join the selection).
        if (!world_.controllable_unit(static_cast<int>(i)) || u.image != type || !u.alive || u.die_time >= 0.0 ||
            u.cargo_of >= 0)
            continue;
        int gx, gy;
        unit_ground_px(u, gx, gy);
        if (gx >= 0 && gx < view_w_ && gy >= 0 && gy < view_h_) sel_.set(static_cast<int>(i));  // on-screen
    }
    const int lo = lowest_selected();
    if (lo >= 0) { actions_.selected = true; actions_.actor = lo; }
}


// G-05: the order half of a click -- formerly the Legacy right-click body,
// now shared by Legacy right-down and Original left-up. G-06: the attack
// gates use World::is_enemy(0, house) (allies/neutrals are not attack targets).
void Input::issue_order(int px, int py) {
    // NET L1: each branch queues ONE command (MEGAMISSION / ARCHIVE) whose
    // Whom is the selection; the World mutation happens at its apply. W4-SQ:
    // the apply marks the ordered units ("a player order leaves the script
    // team", formerly mark_player_order here).
    const std::vector<int> sel = sel_.ids();
    InputEvent ev;   // the body below reads ev.x / ev.y
    actions_.order = InputActions::Order::Move;   // W5-E: per-order voice kind
    actions_.target_building = -1;
    ev.x = px;
    ev.y = py;
    // Rally-point set [RE: production.md §1.4]: with an own unit
    // factory selected, a right-click sets its rally cell and
    // consumes the event (no attack/move). Resolved BEFORE the
    // harvester/attack path so a selected factory always takes
    // precedence. A factory that died since selection must NOT keep
    // swallowing right-clicks: if the selection is stale (out of
    // range or dead/tombstoned), clear it and fall through so the
    // click resumes normal attack/move behavior.
    if (selected_building_ >= 0) {
        const auto& blds = world_.buildings();
        if (selected_building_ >= static_cast<int>(blds.size()) ||
            !blds[selected_building_].alive) {
            selected_building_ = -1;   // stale selection: don't consume
        } else if (world_.garrison_firing(selected_building_) && sel.empty()) {
            // W7-02-10: a selected own firing garrison takes an attack order.
            const int tu = unit_under(ev.x, ev.y);
            const int tb = pick_building(ev.x, ev.y, ev_touch_);
            const int gh = blds[selected_building_].house;
            Command c = make_building_event(EventType::MegaMission, selected_building_);
            c.mission = Mission::Attack;
            if (tu >= 0 && !world_.are_allied(gh, world_.units()[tu].house)) c.target = Target::unit(world_, tu);
            else if (tb >= 0 && tb != selected_building_ && !world_.are_allied(gh, blds[tb].house)) c.target = Target::building(tb);
            else return;
            emit(std::move(c));
            actions_.attack_ordered = true;
            return;
        } else if (is_unit_factory_type(blds[selected_building_].type)) {   // G-10: rally = factories only
            int rcx = 0, rcy = 0;
            resolve_ground_cell(ev.x, ev.y, rcx, rcy);
            Command c = make_building_event(EventType::Archive, selected_building_);   // ArchiveTarget
            c.target = Target::cell(rcx, rcy);
            emit(std::move(c));
            actions_.rally_set = true;   // W5-E EVA_NewRallyPointEstablished
            actions_.factory = selected_building_;
            return;
        }
    }
    // Right-click: attack a hostile under the cursor if the pick
    // lands on one (house != 0) -- a right-click on a FRIENDLY
    // unit is NOT an attack order and falls through to the
    // ground-move branch below (RA2 behavior: there is no
    // "attack own unit" order, so it's treated like clicking the
    // ground under that unit).
    const int idx = unit_under(ev.x, ev.y);
    // Attack requires BOTH a hostile under the cursor AND a live
    // selection (spec gate); otherwise fall through to a ground move
    // so an unselected right-click behaves identically everywhere.
    bool any_selected = false;
    for (int si : sel)
        if (si < static_cast<int>(world_.units().size()) && world_.units()[si].alive &&
            world_.units()[si].die_time < 0.0) { any_selected = true; break; }
    if (idx >= 0 && world_.is_enemy(world_.order_issuer(), world_.units()[idx].house) && any_selected) {
        // Capture actor BEFORE the order mutates any state.
        const int actor = lowest_selected();
        emit(make_mega(world_, sel, Mission::Attack, Target::unit(world_, idx), Target{}));
        actions_.attack_ordered = true;
        actions_.actor = actor;
    } else {
        // Group-move to the clicked ground cell (issued on down).
        // The half-tile offset before screen_to_cell maps the
        // click to the cell whose visible diamond was clicked --
        // LEVEL-AWARE, two-pass: raised terrain projects its
        // diamond kLevelPx*level higher on screen than the
        // level-0 projection (cell_to_screen subtracts
        // level*kLevelPx from py), so a single level-blind
        // inversion resolves 1-2 cells short of the tile actually
        // drawn under the cursor. Pass 1 inverts at level 0 to get
        // a first-guess cell; looking up THAT cell's real level
        // and re-inverting with the raise added back to py
        // converges on the true cell on smooth terrain (one
        // refinement step, not a fixed-point loop -- see
        // task-6-report.md).
        int cx = 0, cy = 0;
        resolve_ground_cell(ev.x, ev.y, cx, cy);
        // W6-01-01: every building target below is the click's building pick
        // (the original's object pick; on touch the drawn art first).
        const int pick_b = pick_building(ev.x, ev.y, ev_touch_);
        // Harvester orders [RE: economy.md §1], resolved BEFORE the
        // attack-building branch: with a house-0 harvester in the
        // selection, a right-click on an ore cell issues a harvest
        // order and one on an own alive REFINERY (is_refinery_type
        // -- the roster list set_refinery_types installed; any
        // other own building stays a plain ground move) sends the
        // miner(s) home. order_harvest/order_return only touch
        // selected house-0 harvesters, so non-harvester
        // right-clicks fall straight through to attack/move below.
        bool harv_selected = false;
        for (int si : sel) {
            if (si >= static_cast<int>(world_.units().size())) break;
            const Unit& su = world_.units()[si];
            if (su.is_harvester && world_.controllable_unit(si) && su.alive && su.die_time < 0.0) {
                harv_selected = true;
                break;
            }
        }
        const int here_b = pick_b;
        if (harv_selected && world_.ore().has_ore(cx, cy)) {
            // MIXED selection (fix round): co-selected NON-harvester
            // units are not harvest-capable -- they get the normal
            // group move to the same cell (each unit interprets the
            // click per its own logic). The harvesters are dropped
            // from the selection for the move call so
            // order_move_selected can't clobber the harvest mission
            // just issued (it would set manual_hold + repath), then
            // restored -- the player's selection is untouched.
            std::vector<int> harv_idx, others;
            for (int i : sel) {
                if (i >= static_cast<int>(world_.units().size())) break;
                const Unit& su = world_.units()[i];
                // CB-1: also skip CARRIED units (cargo_of >= 0, aboard a
                // transport / off-map) -- a widening of the corpse/
                // validity skip, the fourth carry-guard site.
                if (!su.alive || su.die_time >= 0.0 || su.cargo_of >= 0)
                    continue;
                if (su.is_harvester && world_.controllable_unit(i))
                    harv_idx.push_back(i);
                else
                    others.push_back(i);
            }
            // The miners get Mission_Harvest on the ore cell; the rest of a
            // MIXED selection a plain move there (two MEGAMISSION runs).
            emit(make_mega(world_, harv_idx, Mission::Harvest, Target::cell(cx, cy), Target{}));
            if (!others.empty()) {
                const int actor = lowest_selected();
                emit(make_mega(world_, others, Mission::Move, Target{}, Target::cell(cx, cy)));
                if (actor >= 0) {
                    actions_.move_ordered = true;
                    actions_.actor = actor;
                }
            }
            // W5-E: the miners' Mission_Harvest acknowledgement (VoiceHarvest).
            if (!harv_idx.empty()) {
                actions_.move_ordered = true;
                actions_.actor = static_cast<int>(harv_idx.front());
                actions_.order = InputActions::Order::Harvest;
            }
            return;  // handled: no attack for this right-click
        }
        if (harv_selected && here_b >= 0 && world_.buildings()[here_b].house == world_.order_issuer() &&
            world_.buildings()[here_b].alive &&
            is_refinery_type(world_.buildings()[here_b].type)) {
            const int actor = lowest_selected();
            emit(make_mega(world_, sel, Mission::Enter, Target::building(here_b), Target{}));   // dock
            if (actor >= 0) {   // W5-E: dock = Mission_Enter (VoiceEnter)
                actions_.move_ordered = true;
                actions_.actor = actor;
                actions_.order = InputActions::Order::Enter;
            }
            return;  // handled
        }
        // CB-1 enter-transport gesture [RE: transport-cargo.md
        // §2/§3; spec §3]: a right-click on an OWN, alive,
        // non-dying TRANSPORT (passengers_max > 0 -- the Unit
        // carries the roster knowledge, the same way the
        // refinery-return gesture reads roster types) with a
        // selection containing at least one eligible
        // NON-transport unit issues the enter order instead of a
        // group move. Resolved AFTER the harvester branches (ore/
        // refinery win for miners) and BEFORE attack-building/
        // move. The original's "enter" cursor is cosmetic; ours
        // stays default (noted divergence, no new UI systems).
        // Wave-5 (R-052): an allied house's transport takes the player's
        // infantry too (the original's IsAlliedWith enter test).
        // Wave-5 (R-046): a single selected engineer right-clicking a unit
        // that carries an Ivan bomb walks over and disarms it (DefuseKit).
        if (idx >= 0 && world_.units()[idx].bomb_frame >= 0) {
            const int eng = sel_engineer_idx(world_, sel_);
            if (eng >= 0 && defuse_possible(world_, eng, idx, -1)) {
                emit(make_mega(world_, {eng}, Mission::Capture, Target::unit(world_, idx), Target{}));
                actions_.move_ordered = true;
                actions_.actor = eng;
                return;
            }
        }
        if (idx >= 0 && (world_.controllable(world_.units()[idx].house) ||
                         world_.are_allied(world_.order_issuer(), world_.units()[idx].house)) &&
            world_.units()[idx].passengers_max > 0) {
            bool eligible = false;
            for (int si : sel) {
                if (si >= static_cast<int>(world_.units().size())) break;
                const Unit& su = world_.units()[si];
                if (!world_.controllable_unit(si) || !su.alive || su.die_time >= 0.0 || si == idx)
                    continue;
                if (su.passengers_max > 0) continue;  // transports
                eligible = true;                      // do not ride
                break;
            }
            if (eligible) {
                const int actor = lowest_selected();
                emit(make_mega(world_, sel, Mission::Enter, Target::unit(world_, idx), Target{}));
                actions_.move_ordered = true;   // move-ack voice
                actions_.actor = actor;
                actions_.order = InputActions::Order::Enter;   // W5-E VoiceEnter
                return;  // handled: no attack/move for this click
            }
        }
        // Spy infiltration gesture [RE: capture.md §B CONFIRMED]: a
        // SINGLE selected SPY right-clicking an ENEMY/NEUTRAL building
        // ORDERS it to move adjacent and then infiltrate it --
        // refinery cash-steal, power blackout, SW reset, factory
        // veteran-flag, or (radar/lab) inert -- and the spy is
        // CONSUMED on the successful infiltration ON ARRIVAL, NOT on
        // the click. Like the engineer sibling, the act
        // (infiltrate_building + consume_unit) is deferred to
        // step_capture when the spy reaches adjacency -- a spy no
        // longer infiltrates a building across the map on a single
        // click. The one wrinkle: the InfiltratePurpose is resolved
        // from the building's roster type HERE (set_infiltrate_params;
        // World is asset-free) and stored PRE-RESOLVED on the unit, so
        // step_capture acts without the roster. Resolved BEFORE the
        // engineer/attack/move branches so a spy never issues a plain
        // attack. An OWN building is not a spy target so the gesture
        // falls through to normal handling. Interactive-only: no demo
        // selects a spy, so every gate is byte-identical.
        // G16 (agent G) [RE: world-depth.md §A2]: selected Occupier infantry
        // clicking a building they may garrison walk in (InfantryClass
        // Mission_Enter -> Occupy on arrival).
        {
            const int gb = pick_b;
            if (gb >= 0) {
                const int actor = lowest_selected();
                if (world_.group_occupy_count(world_.order_issuer(), sel, gb) > 0) {
                    emit(make_mega(world_, sel, Mission::Enter, Target::building(gb), Target{}));
                    actions_.move_ordered = true;
                    actions_.actor = actor;
                    actions_.order = InputActions::Order::Enter;   // W5-E VoiceEnter
                    return;
                }
            }
        }
        // W7-08-01 / W7-04-06 [RE: Mission_Repair 0x44B780]: damaged own
        // vehicles clicked onto an own UnitRepair building dock for service.
        if (pick_b >= 0 && world_.depot_wants(world_.order_issuer(), sel, pick_b)) {
            const int actor = lowest_selected();
            emit(make_mega(world_, sel, Mission::Enter, Target::building(pick_b), Target{}));
            actions_.move_ordered = true;
            actions_.actor = actor;
            actions_.order = InputActions::Order::Enter;   // VoiceEnter
            return;
        }
        // W7-01-04 [RE: Mouse_Left_Release FUN_004AE660 -> What_Action +
        // Active_Click_With per selected object]: every selected spy /
        // engineer acts on the building by its own rule; the rest of a mixed
        // selection attacks an enemy building (below) or moves. W7-01-33: the
        // roles come from the Agent / Infiltrate / Engineer type flags.
        if (pick_b >= 0) {
            std::vector<int> spies, engs, rest;
            for (int i : sel) {
                if (i < 0 || i >= static_cast<int>(world_.units().size())) continue;
                const Unit& su = world_.units()[i];
                if (!su.alive || su.die_time >= 0.0 || su.cargo_of >= 0 || !world_.controllable_unit(i)) continue;
                if (world_.unit_is_spy(su) && world_.spy_can_infiltrate(i, pick_b)) spies.push_back(i);   // W7-01-14
                else if (world_.unit_is_engineer(su) &&
                         world_.engineer_action(i, pick_b) != World::EngineerAct::None) engs.push_back(i);
                else rest.push_back(i);
            }
            if (!spies.empty() || !engs.empty()) {
                if (!spies.empty()) {
                    // The purpose / steal / blackout are resolved at the apply
                    // from the same OrderRules (Mission_Sabotage).
                    emit(make_mega(world_, spies, Mission::Sabotage, Target::building(pick_b), Target{}));
                    actions_.order = InputActions::Order::Enter;   // W5-E [RE-FALLBACK: spy mission]
                }
                if (!engs.empty()) {
                    emit(make_mega(world_, engs, Mission::Capture, Target::building(pick_b), Target{}));
                    actions_.order = InputActions::Order::Capture;   // W5-E VoiceCapture
                }
                actions_.move_ordered = true;   // move-ack voice
                actions_.actor = !engs.empty() ? engs.front() : spies.front();
                if (!rest.empty() && world_.is_enemy(world_.order_issuer(), world_.buildings()[pick_b].house))
                    emit(make_mega(world_, rest, Mission::Attack, Target::building(pick_b), Target{}));
                return;                          // handled
            }
        }
        // Interactive attack-building (Task 4 machinery wired to the
        // player here): if the resolved ground cell lands on a
        // HOSTILE building's foundation it's an attack order, not a
        // move -- mirrors the hostile-unit gate above (house != 0 +
        // a live selection). building_at_cell returns only alive,
        // non-Dead foundations and order_attack_building re-guards
        // house-0/dead/out-of-range internally (world.cpp:464), so
        // the lone house != 0 test here is just the input-level
        // friendly-vs-hostile decision: a click on an own/friendly
        // building is NOT an attack and falls through to the move
        // below, exactly like a friendly-unit right-click.
        const int b_idx = pick_b;
        if (b_idx >= 0 && world_.is_enemy(world_.order_issuer(), world_.buildings()[b_idx].house) && any_selected) {
            // Capture actor BEFORE the order mutates any state.
            const int actor = lowest_selected();
            emit(make_mega(world_, sel, Mission::Attack, Target::building(b_idx), Target{}));
            actions_.attack_ordered = true;
            actions_.actor = actor;
            actions_.target_building = b_idx;   // W5-E: C4 carriers -> Sabotage voice
        } else {
            // Group-move to the clicked ground cell (issued on
            // down). Capture actor BEFORE the order mutates state;
            // the move flag is set only when the selection is
            // non-empty (an empty-selection right-click is a no-op
            // move).
            const int actor = lowest_selected();
            if (!sel.empty()) emit(make_mega(world_, sel, Mission::Move, Target{}, Target::cell(cx, cy)));
            if (actor >= 0) {
                actions_.move_ordered = true;
                actions_.actor = actor;
            }
        }
    }
}

void Input::handle(const InputEvent& ev) {
    sel_.prune(world_);   // NET L1: the sim may have boarded / consumed selected units
    // NOTE: actions_ is deliberately NOT touched here -- it ACCUMULATES

    // across handle() calls (the commit/order sites below OR their flags in)
    // and is cleared only by take_actions(), the game loop's once-per-frame
    // consume point. See InputActions in input.hpp for the rationale.

    // G-05: the original's dispatch (input_original.cpp). Legacy falls through.
    if (scheme_ == Scheme::Original) {
        handle_original(ev);
        return;
    }

    // CB-4 Superweapons [spec §4]: while SW-targeting is armed, a left Down+Up
    // (no drag) commits the target cell -- resolved via the SAME level-aware
    // screen->cell pick the placement path uses (resolve_ground_cell) -- and
    // disarms; a right-click cancels; PointerMove just tracks the cursor.
    // Checked BEFORE placement/selection so an armed SW consumes the click
    // first. UI state only -- the sim is untouched until main.cpp calls
    // World::fire_superweapon. No existing flow arms this, so this branch is
    // never entered in any existing demo. Quit still propagates.
    if (sw_targeting_) {
        switch (ev.kind) {
            case InputEvent::Quit:
                quit_ = true;
                break;
            case InputEvent::PointerMove:
                cur_x_ = ev.x;
                cur_y_ = ev.y;
                break;
            case InputEvent::PointerDown:
                if (ev.button == InputEvent::Right) {
                    sw_targeting_ = false;   // cancel
                } else {
                    l_down_ = true;
                    dragging_ = false;
                    down_x_ = cur_x_ = ev.x;
                    down_y_ = cur_y_ = ev.y;
                }
                break;
            case InputEvent::PointerUp:
                if (ev.button == InputEvent::Left && l_down_ && !dragging_) {
                    int cx = 0, cy = 0;
                    resolve_ground_cell(ev.x, ev.y, cx, cy);
                    actions_.sw_fire_confirmed = true;
                    actions_.sw_cx = cx;
                    actions_.sw_cy = cy;
                    sw_targeting_ = false;   // one-shot
                }
                l_down_ = false;
                dragging_ = false;
                break;
            default:
                break;
        }
        return;
    }

    // Placement mode (Task 7a) intercepts the whole gesture model: a left
    // Down+Up (no drag) confirms at the hovered foundation origin; a right
    // click cancels; PointerMove just tracks the cursor (so placement_cell
    // follows it); everything else is swallowed so no selection/attack/move
    // gesture leaks through while placing. Quit still propagates.
    if (placement_active()) {
        switch (ev.kind) {
            case InputEvent::Quit:
                quit_ = true;
                break;
            case InputEvent::PointerMove:
                cur_x_ = ev.x;
                cur_y_ = ev.y;
                break;
            case InputEvent::PointerDown:
                if (ev.button == InputEvent::Right) {
                    actions_.place_cancelled = true;
                    cancel_placement();
                } else {
                    l_down_ = true;
                    dragging_ = false;
                    down_x_ = cur_x_ = ev.x;
                    down_y_ = cur_y_ = ev.y;
                }
                break;
            case InputEvent::PointerUp:
                if (ev.button == InputEvent::Left && l_down_ && !dragging_) {
                    int cx = 0, cy = 0;
                    resolve_ground_cell(ev.x, ev.y, cx, cy);
                    actions_.place_confirmed = true;
                    actions_.place_cx = cx - place_fw_ / 2;
                    actions_.place_cy = cy - place_fh_ / 2;
                }
                l_down_ = false;
                dragging_ = false;
                break;
            default:
                break;
        }
        return;
    }

    // Repair / Sell cursor mode [RE: repair-sell.md §C1]. While armed, a left
    // Down+Up (no drag) on an OWN alive building reports the click (main.cpp
    // routes it to World::toggle_repair / World::begin_sell); a right-click
    // cancels ONE mode; PointerMove just tracks the cursor. The whole gesture
    // model is swallowed so no selection/attack/move leaks through. Quit still
    // propagates. NOT one-shot (unlike sw_targeting_): it stays armed after a
    // click, so several buildings can be repaired/sold in a row. Placed AFTER
    // the placement block because placement WINS (set_mode_ also refuses while
    // placing, so the two can never both be active).
    if (mode_ != BaseMode::None) {
        switch (ev.kind) {
            case InputEvent::Quit:
                quit_ = true;
                break;
            case InputEvent::PointerMove:
                cur_x_ = ev.x;
                cur_y_ = ev.y;
                break;
            case InputEvent::PointerDown:
                if (ev.button == InputEvent::Right) {
                    cancel_one_mode();
                } else {
                    l_down_ = true;
                    dragging_ = false;
                    down_x_ = cur_x_ = ev.x;
                    down_y_ = cur_y_ = ev.y;
                }
                break;
            case InputEvent::PointerUp:
                if (ev.button == InputEvent::Left && l_down_ && !dragging_) {
                    const int bi = building_under(ev.x, ev.y);
                    // OWN house only [RE: §C1 -- the SELL/REPAIR event verifies
                    // ownership]. A click on nothing, or on an enemy building,
                    // is swallowed with NO report.
                    if (bi >= 0 && world_.buildings()[bi].house == world_.order_issuer()) {
                        if (mode_ == BaseMode::Repair) {
                            actions_.repair_clicked = true;
                            actions_.repair_building = bi;
                        } else if (mode_ == BaseMode::PowerToggle) {   // W5-E2
                            actions_.power_clicked = true;
                            actions_.power_building = bi;
                        } else {
                            actions_.sell_clicked = true;
                            actions_.sell_building = bi;
                        }
                    }
                }
                l_down_ = false;
                dragging_ = false;
                break;
            default:
                break;
        }
        return;
    }

    switch (ev.kind) {
        case InputEvent::Quit:
            quit_ = true;
            break;

        case InputEvent::PointerDown:
            if (ev.button == InputEvent::Right) {
                issue_order(ev.x, ev.y);   // G-05: the shared order body (see issue_order)
            } else {
                l_down_ = true;
                dragging_ = false;
                down_shift_ = ev.shift;
                down_x_ = cur_x_ = ev.x;
                down_y_ = cur_y_ = ev.y;
            }
            break;

        case InputEvent::PointerMove:
            cur_x_ = ev.x;
            cur_y_ = ev.y;
            if (l_down_ && !dragging_ &&
                (std::abs(ev.x - down_x_) > kDragThreshold ||
                 std::abs(ev.y - down_y_) > kDragThreshold)) {
                dragging_ = true;
            }
            break;

        case InputEvent::PointerUp:
            if (ev.button == InputEvent::Right) break;
            if (l_down_) {
                if (dragging_) {
                    commit_box(down_shift_);
                } else if (commit_factory_click(ev.x, ev.y)) {
                    // Handled as a factory select/primary [RE: production.md
                    // §1.2] -- takes precedence over unit selection AND the
                    // double-click type-select; reset the double-click timer so
                    // a following click on a unit isn't spuriously a "double".
                    last_click_t_ = 0;
                } else {
                    const bool is_double =
                        (ev.t_ms - last_click_t_) <= kDblMs &&
                        std::abs(ev.x - last_click_x_) <= kDblSlop &&
                        std::abs(ev.y - last_click_y_) <= kDblSlop;
                    if (is_double) {
                        commit_double(ev.x, ev.y);
                        last_click_t_ = 0;  // consume, so a triple-click isn't a 2nd double
                    } else {
                        commit_click(ev.x, ev.y, down_shift_);
                        last_click_t_ = ev.t_ms;
                        last_click_x_ = ev.x;
                        last_click_y_ = ev.y;
                    }
                }
            }
            l_down_ = false;
            dragging_ = false;
            break;

        default:
            break;
    }
}

}  // namespace ra2engine

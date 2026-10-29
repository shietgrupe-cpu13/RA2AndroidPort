// pres_effects.inc -- W3-FX: the effects the original draws that are not
// AnimList one-shots: particle systems (damage smoke, sparks, trigger smoke),
// Bouncer debris (SHP + voxel), anim/projectile trailers, the V3 rocket trail,
// projectile line trails, ship wakes, ore twinkles and object ambient sounds.
// Included by main.cpp right after pres_fx.inc (it uses PresFx's anim types,
// palette and blitter). Driven ONCE PER GAME FRAME (World::vet_frame
// advances), never by wall time.
//
// Determinism: everything here is presentation. It observes World state and
// owns its own RNG; nothing it does is read by the simulation, a gate hash or
// a save (the effects are rebuilt from World state after a load: damage smoke
// re-arms on the next health-condition change). [RE-FALLBACK, deliberate:
// the original's particles and debris draw from the lockstep RNG and two
// kinds can hurt -- Gas particles (Damage=, only the unused RA2 weapons
// Vortex/ChemLauncher make them) and Bouncer debris with Damage= on impact
// (DBRIS*LG 20 HE, PIECE 5) -- the clone keeps both cosmetic so the World
// stays independent of the draw layer.]
//
// Recovered behaviour (gamemd.exe, docs/re/effects.md):
//  * ParticleSystemClass::Update FUN_0062f800 switches on BehavesLike
//    (0 Smoke FUN_0062e7e0, 1 Gas FUN_0062e170, 2 Fire FUN_0062f440, 3 Spark
//    FUN_0062e2e0, 4 Railgun FUN_0062ecd0), then Lifetime--; at 0 the system
//    is removed; a system marked TimeToDie goes once its particles are gone.
//  * Smoke system: follows its owner (+SpawnDistanceToOwner); every
//    (int)Accum frames (Accum starts at SpawnFrames, += Slowdown per frame,
//    TimeToDie once > SpawnCutoff) it spawns HoldsWhat at Location +
//    (Random % (SpawnRadius+1), same, +10); past SpawnTranslucencyCutoff a new
//    puff is 25 % more transparent; its Speed drops by (Accum - SpawnFrames).
//  * ParticleClass ctor FUN_0062b080: Speed = Velocity, RemainingEC = MaxEC +
//    Random % MaxEC, state = StartStateAI, translucency = Translucency.
//    Particle AI FUN_0062c8e0: behaviour step, RemainingEC--, 0 -> delete.
//  * Smoke particle FUN_0062b7f0: on even frames a 1-in-8 random walk of a
//    +-2 lepton drift; the state (= image frame) advances every
//    StateAIAdvance(+0/1) frames; EndStateAI with DeleteOnStateLimit deletes.
//  * Spark system: while SparkSpawnFrames > 0, with SpawnSparkPercentage (or
//    on the last frame) a burst of Cap/2 + Random % (Cap/2) sparks with
//    velocity (Random % XVelocity, Random % YVelocity, MinZVelocity + Random %
//    ZVelocityRange); spark particles fall under [General] Gravity and walk
//    their ColorList at ColorSpeed.
//  * Damage smoke, TechnoClass::ReceiveDamage FUN_00700da0: when the health
//    ratio is at or below ConditionYellow and the damage changed the
//    condition (NowYellow / NowRed), and no smoke system is attached, one
//    random BehavesLike=Smoke entry of DamageParticleSystems is created at
//    Location + DamageSmokeOffset, owned (followed) by the object; above
//    ConditionYellow the attached system is removed. Spark entries are only
//    used by DamageSparks types (TechnoClass::AI, cyborgs), none in RA2.
//  * Debris, same function on death: unless low over water, n = Random(
//    MinDebris, MaxDebris-1); DebrisTypes voxels (each takes Random %
//    (DebrisMaximums[i]+1) of n, cycling) else DebrisAnims, else (no types)
//    [General] MetallicDebris -- anims at Location + 20.
//  * Bouncer anims (AnimClass ctor FUN_00421ec0 + BounceClass FUN_00439760 /
//    FUN_00439a80): velocity (Random % 2*MaxXYVel - MaxXYVel twice, MinZVel +
//    Random % (MaxZVel-MinZVel)), gravity 1.4 leptons/frame^2, ground hit
//    reflects and scales by Elasticity, too slow -> Impact -> ExpireAnim.
//    AnimClass::AI FUN_00423ae0 spawns TrailerAnim every TrailerSeperation
//    frames.
//  * Projectile Trailer= (BulletTypeClass art, FUN_0046bf4c): BulletClass::AI
//    FUN_004666d0 spawns it every SpawnDelay (default 3) frames.
//  * V3 rocket, RocketLocomotionClass FUN_00661d00: "V3TAKOFF" every 24
//    frames while launching, then "V3TRAIL" every 3 frames in flight.
//  * Wake, the ship locomotors (FUN_0069f2a0 & co.): every 8 frames while
//    moving on a water cell, [General] Wake= at the unit.
//  * OreTwinkle, scenario start FUN_00684670: each ore cell gets
//    [General] OreTwinkle with chance 1/OreTwinkleChance; the anim
//    (TWNK1: LoopCount=-1, RandomLoopDelay) loops for the whole game and is
//    hidden once the cell has no ore (HideIfNoOre).

constexpr double kFxLepPx = 36.74 / 256.0;   // leptons of height -> screen px (flight.cpp)

// W3-FX trigger flashes, keyed by World object index, valid until a World
// frame (vet_frame). 104 FlashTeam / 131 FlashBuildingsOfType [RE:
// FUN_006f1490 -> ObjectClass::Flash(Value frames)].
std::map<int, long> g_flash_units, g_flash_buildings;

// W3-FX radar events for the minimap (pres_radar.inc), refreshed by
// Presentation::update: cell, frames since creation, the type's visibility
// frames [RE: presentation.md §1.2 table 0x007EF998 / [General]
// RadarEventVisibilityDurations] and [AudioVisual] RadarEventMinRadius /
// RadarEventSpeed / RadarEventRotationSpeed / RadarEventColorSpeed.
struct RadarPingDraw { double x = 0, y = 0; long age = 0; int vis = 0; };
std::vector<RadarPingDraw> g_radar_pings;
double g_radar_min_r = 8.0, g_radar_speed = 1.2, g_radar_rot = 0.05, g_radar_col = 0.1;

struct FxPtType {        // [Particles] entry (ParticleTypeClass)
    std::string name;
    const FxAnimType* img = nullptr;      // Image= (anim.pal SHP)
    int behaves = 0;                      // 0 Smoke 1 Gas 2 Fire 3 Spark 4 Railgun
    int max_ec = 0, max_dc = 0;
    double velocity = 0.0, deacc = 0.0;
    int translucency = 0;
    int start_state = 0, end_state = 0, state_adv = 1;
    bool delete_on_limit = false;
    int start_frame = 0, loop_frames = 0;
    int xvel = 0, yvel = 0, min_zvel = 0, zvel_range = 0;
    double color_speed = 0.0;
    std::vector<SDL_Color> colors;
    std::string next;
    double next_dx = 0.0, next_dy = 0.0, next_dz = 0.0;
    int wind = 0;
};

struct FxPsType {        // [ParticleSystems] entry (ParticleSystemTypeClass)
    std::string name;
    std::string holds;
    bool spawns = false;
    int spawn_frames = 0;
    double slowdown = 0.0;
    int cap = 0, radius = 0;
    double cutoff = 0.0, trans_cutoff = 0.0;
    int behaves = 0;
    int lifetime = -1;
    int spark_frames = 0;
    double spark_pct = 0.0;
    double per_coord = 0.0, spiral_r = 0.0, spiral_d = 0.0;
    bool laser = false;
    SDL_Color laser_color{255, 255, 255, 255};
};

class PresEffects {
public:
    // Owner of an attached system: 0 none, 1 unit index, 2 building index.
    struct Owner {
        int kind, index;
        Owner(int k = 0, int i = -1) : kind(k), index(i) {}
    };

    void init(PresFx& fx, const Ini& rules, const ra2assets::MixFile& ra2, const std::string& dir) {
        fx_ = &fx;
        rules_ = &rules;
        ra2_ = &ra2;
        // The RA2 rules.ini (expand01 patch copy): the effect keys are read
        // from it first (see rget).
        try {
            ra2assets::MixFile ex = ra2assets::MixFile::open_path(dir + "expand01.mix");
            if (const ra2assets::MixEntry* e = ex.find(std::string("rules.ini"))) {
                const auto raw = ex.read(*e);
                ra2_rules_ = Ini::parse(std::string(raw.begin(), raw.end()));
            }
        } catch (...) {}
        auto gen = [&](const char* k) {
            std::string v = pres_clean(rules.get("General", k));
            if (v.empty()) v = pres_clean(rules.get("AudioVisual", k));
            if (v.empty()) v = pres_clean(rules.get("CombatDamage", k));
            return v;
        };
        gravity_ = gen("Gravity").empty() ? 6.0 : std::atof(gen("Gravity").c_str());
        auto pct = [&](const char* k, double d) {
            const std::string v = pres_clean(rules.get("AudioVisual", k));
            return v.empty() ? d : std::atof(v.c_str()) / (v.find('%') != std::string::npos ? 100.0 : 1.0);
        };
        yellow_ = pct("ConditionYellow", 0.5);
        red_ = pct("ConditionRed", 0.25);
        wake_ = gen("Wake");
        twinkle_ = gen("OreTwinkle");
        twinkle_chance_ = std::max(1, std::atoi(gen("OreTwinkleChance").c_str()));
        metallic_ = split(gen("MetallicDebris"));
        splash_ = split(gen("SplashList"));
        {   // voxels.vpl through the shared search list (W3-FIX-A AssetFS)
            std::vector<uint8_t> raw;
            if (asset_read("voxels.vpl", raw)) {
                try {
                    vpl_ = ra2assets::Vpl::load(raw.data(), raw.size());
                    has_vpl_ = true;
                } catch (...) {}
            }
        }
    }
    void set_unit_palette(const ra2assets::Palette* p) { unit_pal_ = p; }
    const std::vector<std::string>& splash_list() const { return splash_; }

    // ---- per game frame ----------------------------------------------------
    // Steps every effect once per World frame since the last call (at most 8,
    // so a long pause never floods): the per-frame spawners (wakes, rocket and
    // projectile trails) then the effect AI. The damage-smoke edges and the
    // ore twinkles are observed once per call.
    void update(const ra2engine::World& w, const BuildingAssets& ba) {
        world_ = &w;
        ba_ = &ba;
        const long now = static_cast<long>(w.vet_frame());
        if (last_frame_ < 0) last_frame_ = now - 1;
        const int n = static_cast<int>(std::clamp<long>(now - last_frame_, 0, 8));
        last_frame_ = now;
        if (n == 0) return;
        seed_twinkles(w);
        observe_damage(w, ba);
        for (int k = 0; k < n; ++k) {
            observe_units(w);
            observe_projectiles(w, ba);
            tick(w, ba);
        }
    }

    // ---- spawners -------------------------------------------------------------
    // A particle system of type `name` at world leptons (x, y, z = absolute
    // height); attached to `owner` when given. Returns its id (0 = none).
    int spawn_system(const std::string& name, double x, double y, double z, Owner owner = {},
                     double tx = 0, double ty = 0, double tz = 0) {
        const FxPsType* t = ps_type(name);
        if (!t) return 0;
        Sys s;
        s.id = ++next_id_;
        s.t = t;
        s.x = x; s.y = y; s.z = z;
        s.tx = tx; s.ty = ty; s.tz = tz;
        s.owner = owner;
        s.accum = t->spawn_frames;
        s.lifetime = t->lifetime;
        s.spark_frames = t->spark_frames;
        if (owner.kind != 0) {
            double ox = 0, oy = 0, oz = 0;
            if (owner_pos(owner, ox, oy, oz)) { s.offx = x - ox; s.offy = y - oy; s.offz = z - oz; }
        }
        if (t->behaves == 4) railgun_burst(s);
        systems_.push_back(std::move(s));
        pres_log(frame_, "fx particles " + t->name);
        return systems_.back().id;
    }
    // Global/unmanaged particles emitted by AnimType::SpawnsParticle. The
    // setting names a ParticleType, not a ParticleSystemType; vanilla keeps
    // these in its hidden GasCloudSys so their own BehavesLike/MaxEC apply.
    void spawn_unmanaged_particle(const std::string& name, int count, double x, double y, double z) {
        const int n = std::clamp(count, 0, 512);
        if (n == 0 || !pt_type(name)) return;
        Sys s;
        s.id = ++next_id_;
        s.t = &unmanaged_particle_owner_;
        s.x = x; s.y = y; s.z = z;
        s.dying = true;
        for (int i = 0; i < n; ++i) {
            Part p = make_part(name, x, y, z);
            if (p.t) s.parts.push_back(std::move(p));
        }
        if (!s.parts.empty()) systems_.push_back(std::move(s));
    }
    void kill_system(int id) {
        for (Sys& s : systems_)
            if (s.id == id) s.dying = true;
    }
    bool system_alive(int id) const {
        for (const Sys& s : systems_)
            if (s.id == id) return true;
        return false;
    }
    std::size_t system_count() const { return systems_.size(); }
    std::size_t particle_count() const {
        std::size_t n = 0;
        for (const Sys& s : systems_) n += s.parts.size();
        return n;
    }
    std::size_t debris_count() const { return bouncers_.size() + voxels_.size(); }
    std::size_t line_count() const { return lines_.size(); }

    // Debris of a destroyed techno (`id` rules section) at world leptons.
    void spawn_debris(const std::string& id, double x, double y, double z, bool on_water) {
        if (on_water) return;
        const int mx = num(id, "MaxDebris", 0);
        if (mx <= 0) return;
        const int mn = std::clamp(num(id, "MinDebris", 0), 0, mx);
        int n = mn + static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, mx - mn)));
        if (mx - 1 < mn) n = mn;
        const std::vector<std::string> types = split(rget(id, "DebrisTypes"));
        const std::vector<std::string> maxs = split(rget(id, "DebrisMaximums"));
        int spawned = 0;
        if (!types.empty()) {
            std::size_t i = 0;
            int guard = 0;
            while (n > 0 && guard++ < 64) {
                const int cap = i < maxs.size() ? std::atoi(maxs[i].c_str()) : 0;
                int k = static_cast<int>(rnd() % static_cast<std::uint32_t>(cap + 1));
                k = std::min(k, n);
                for (int j = 0; j < k; ++j) spawned += spawn_voxel_debris(types[i], x, y, z) ? 1 : 0;
                n -= k;
                if (++i >= types.size()) i = 0;
            }
        }
        const std::vector<std::string> anims = split(rget(id, "DebrisAnims"));
        const std::vector<std::string>& list = !anims.empty() ? anims : metallic_;
        if ((anims.empty() && types.empty()) || !anims.empty())
            for (; n > 0 && !list.empty(); --n)
                spawned += spawn_bouncer(list[rnd() % list.size()], x, y, z + 20.0) ? 1 : 0;
        if (spawned) pres_log(frame_, "fx debris " + id + " x" + std::to_string(spawned));
    }
    // A Bouncer=yes anim (or a plain anim when not a bouncer) at leptons.
    bool spawn_bouncer(const std::string& name, double x, double y, double z) {
        const FxAnimType* t = fx_->type(name);
        if (!t) return false;
        const std::string key = pres_upper(name);
        // W5-PRES R-123 [RE: ctor FUN_00421ec0]: IsMeteor=yes runs the same
        // bounce physics as Bouncer=yes.
        if (!yes(aget(key, "Bouncer")) && !yes(aget(key, "IsMeteor"))) {
            fx_->spawn(name, x / 256.0, y / 256.0, 0, z * kFxLepPx);
            return true;
        }
        Bouncer b;
        b.t = t;
        b.x = x; b.y = y; b.z = z;
        const double xy = adbl(key, "MaxXYVel", 15.0);
        const double zmin = adbl(key, "MinZVel", 20.0);
        const double zmax = adbl(key, "MaxZVel", zmin * 1.5);
        auto sym = [&](double r) {
            const int span = std::max(1, static_cast<int>(2 * r));
            return static_cast<double>(rnd() % static_cast<std::uint32_t>(span)) - r;
        };
        b.vx = sym(xy);
        b.vy = sym(xy);
        b.vz = zmin + static_cast<double>(rnd() % static_cast<std::uint32_t>(std::max(1, static_cast<int>(zmax - zmin))));
        b.elasticity = adbl(key, "Elasticity", 0.0);
        b.trailer = aget(key, "TrailerAnim");
        b.trailer_sep = std::max(1, aint(key, "TrailerSeperation", 3));
        b.expire = aget(key, "ExpireAnim");
        // W5-PRES R-123 [RE: AI FUN_00423ae0 impact]: Spawns= x
        // (Random(0,SpawnCount) + Random(0,SpawnCount)) on a land impact.
        b.spawns = aget(key, "Spawns");
        b.spawn_count = std::max(0, aint(key, "SpawnCount", 0));
        // RandomRate=a,b [RE: reader FUN_00427c80, W5-PRES R-123]: 900/a, 900/b
        // with `if (max < min) min = max` -- every artmd entry (a < b) is a
        // fixed 900/b (the type's rand_min == rand_max).
        b.delay = std::max(1, t->rand_max > 0 ? t->rand_min : t->delay);
        bouncers_.push_back(b);
        return true;
    }
    // [VoxelAnims] debris (TIRE / AXLE / PIECE): the same bounce, drawn as
    // the voxel spinning about its vertical axis [RE-FALLBACK: the original
    // tumbles it through a quaternion; the clone's rasterizer takes a yaw].
    bool spawn_voxel_debris(const std::string& name, double x, double y, double z) {
        const VoxelDef* d = voxel_def(name);
        if (!d) return false;
        VoxelDebris v;
        v.d = d;
        v.x = x; v.y = y; v.z = z + 20.0;
        auto sym = [&](double r) {
            const int span = std::max(1, static_cast<int>(2 * r));
            return static_cast<double>(rnd() % static_cast<std::uint32_t>(span)) - r;
        };
        v.vx = sym(d->max_xy);
        v.vy = sym(d->max_xy);
        v.vz = d->min_z + static_cast<double>(rnd() % static_cast<std::uint32_t>(std::max(1, static_cast<int>(d->max_z - d->min_z))));
        v.spin = d->min_ang + static_cast<double>(rnd() % 1000) / 1000.0 * (d->max_ang - d->min_ang);
        v.life = d->duration > 0 ? d->duration : 150;
        voxels_.push_back(v);
        return true;
    }

    // ---- per-object observers (Presentation calls these) ------------------
    // Health-condition edges drive the damage smoke of units and buildings.
    // W6-06-04: a loaded game starts with no particle systems (they are not
    // in the save). The next observe_damage treats every object as if it had
    // been healthy, so each damaged unit / building gets its damage smoke
    // back (the systems the original's full save would have restored).
    void after_load() { seed_damage_ = true; }
    void observe_damage(const ra2engine::World& w, const BuildingAssets& ba) {
        const bool seed = seed_damage_;
        seed_damage_ = false;
        const auto& us = w.units();
        if (ucond_.size() < us.size()) { ucond_.resize(us.size(), -1); usmoke_.resize(us.size(), 0); }
        for (std::size_t i = 0; i < us.size(); ++i) {
            const ra2engine::Unit& u = us[i];
            const bool live = u.alive && u.die_time < 0.0 && u.cargo_of < 0;
            const int cond = !live ? 3 : condition(u.hp, u.max_hp);
            int& prev = ucond_[i];
            if (seed && prev < 0 && live) prev = 0;
            if (prev >= 0 && cond != prev) {
                if (cond == 3 || cond == 0) {   // dead / healthy again
                    if (usmoke_[i]) kill_system(usmoke_[i]);
                    usmoke_[i] = 0;
                } else if (cond > prev && (!usmoke_[i] || !system_alive(usmoke_[i]))) {
                    const double z = u.level * 104.0 + (u.is_aircraft ? u.altitude : 0.0);
                    usmoke_[i] = damage_smoke(pres_type_id(u), u.fx * 256.0, u.fy * 256.0, z,
                                              Owner{1, static_cast<int>(i)});
                }
            }
            prev = cond;
        }
        const auto& bs = w.buildings();
        if (bcond_.size() < bs.size()) { bcond_.resize(bs.size(), -1); bsmoke_.resize(bs.size(), 0); }
        for (std::size_t i = 0; i < bs.size(); ++i) {
            const ra2engine::Building& b = bs[i];
            const bool live = b.alive && b.state != ra2engine::Building::State::Dead;
            const int cond = !live ? 3 : condition(b.hp, b.max_hp);
            int& prev = bcond_[i];
            if (seed && prev < 0 && live) prev = 0;
            if (prev >= 0 && cond != prev) {
                if (cond == 3 || cond == 0) {
                    if (bsmoke_[i]) kill_system(bsmoke_[i]);
                    bsmoke_[i] = 0;
                } else if (cond > prev && (!bsmoke_[i] || !system_alive(bsmoke_[i])) &&
                           b.type >= 0 && b.type < static_cast<int>(ba.roster.size())) {
                    double x = 0, y = 0, z = 0;
                    building_pos(b, x, y, z);
                    bsmoke_[i] = damage_smoke(ba.roster[static_cast<std::size_t>(b.type)].id, x, y, z,
                                              Owner{2, static_cast<int>(i)});
                }
            }
            prev = cond;
        }
    }
    // Ore twinkles, once the World's ore exists (FUN_00684670).
    void seed_twinkles(const ra2engine::World& w) {
        if (twinkle_seeded_ || twinkle_.empty()) return;
        const ra2engine::OreField& ore = w.ore();
        bool any = false;
        for (int y = 0; y < w.grid_h(); ++y)
            for (int x = 0; x < w.grid_w(); ++x) {
                if (!ore.has_ore(x, y)) continue;
                any = true;
                if (rnd() % static_cast<std::uint32_t>(twinkle_chance_) != 0) continue;
                Twinkle t;
                t.x = x; t.y = y;
                t.wait = static_cast<int>(rnd() % 300);
                twinkles_.push_back(t);
            }
        if (!any) return;
        twinkle_seeded_ = true;
        twinkle_t_ = fx_->type(twinkle_);
        const std::vector<std::string> d = split(aget(pres_upper(twinkle_), "RandomLoopDelay"));
        if (d.size() >= 2) { tw_delay0_ = std::atoi(d[0].c_str()); tw_delay1_ = std::max(tw_delay0_, std::atoi(d[1].c_str())); }
        pres_log(frame_, "fx ore twinkles " + std::to_string(twinkles_.size()));
    }
    std::size_t twinkle_count() const { return twinkles_.size(); }

    // ---- draw -------------------------------------------------------------
    // Ore twinkles (they loop with RandomLoopDelay pauses and HideIfNoOre, so
    // they are kept here rather than as one-shot PresFx anims), drawn after
    // the ore and smudges and before every object.
    void draw_ground(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::Camera& cam, int W,
                     int H) {
        if (!fx_) return;
        for (const Ground& g : grounds_) {
            const int fr = g.t->frame_at(g.age);
            if (fr < 0) continue;
            int sx = 0, sy = 0;
            to_screen(cam, g.x, g.y, g.z, sx, sy);
            fx_->blit(ren, tc, *g.t->shp, "fx:" + g.t->name, fr, sx, sy, g.t->translucent, W, H);
        }
        if (twinkle_t_ && world_) {
            const ra2engine::OreField& ore = world_->ore();
            for (const Twinkle& t : twinkles_) {
                if (t.wait > 0 || !ore.has_ore(t.x, t.y)) continue;   // HideIfNoOre
                if (!world_->explored_at(g_local_house, t.x, t.y)) continue;
                const int fr = PresFx::loop_frame(*twinkle_t_, t.age);
                const ra2assets::MapCell* mc = world_->map().at(t.x, t.y);
                const double z = (mc ? mc->level : 0) * 104.0;
                int sx = 0, sy = 0;
                to_screen(cam, t.x * 256.0, t.y * 256.0, z, sx, sy);
                fx_->blit(ren, tc, *twinkle_t_->shp, "fx:" + twinkle_t_->name, fr, sx, sy,
                          twinkle_t_->translucent, W, H);
            }
        }
    }
    // Air layer: particles, debris, line trails (after every object).
    void draw(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::Camera& cam, int W, int H) {
        if (!fx_) return;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        // Line trails (UseLineTrail): a fading polyline per live projectile.
        for (const auto& kv : lines_) {
            const LineTrail& lt = kv.second;
            for (std::size_t k = 1; k < lt.pts.size(); ++k) {
                const LinePt& a = lt.pts[k - 1];
                const LinePt& b = lt.pts[k];
                const int alpha = std::clamp(b.intensity, 0, 255);
                if (alpha <= 0) continue;
                int ax, ay, bx, by;
                to_screen(cam, a.x, a.y, a.z, ax, ay);
                to_screen(cam, b.x, b.y, b.z, bx, by);
                SDL_SetRenderDrawColor(ren, lt.color.r, lt.color.g, lt.color.b, static_cast<Uint8>(alpha));
                plot_line(ren, ax, ay, bx, by);
            }
        }
        for (const Sys& s : systems_) {
            if (s.t->laser && s.laser_left > 0) {
                int ax, ay, bx, by;
                to_screen(cam, s.x, s.y, s.z, ax, ay);
                to_screen(cam, s.tx, s.ty, s.tz, bx, by);
                const Uint8 a = static_cast<Uint8>(std::clamp(s.laser_left * 32, 0, 255));
                SDL_SetRenderDrawColor(ren, s.t->laser_color.r, s.t->laser_color.g, s.t->laser_color.b, a);
                plot_line(ren, ax, ay, bx, by);
            }
            for (const Part& p : s.parts) {
                int sx = 0, sy = 0;
                to_screen(cam, p.x, p.y, p.z, sx, sy);
                if (sx < -64 || sy < -64 || sx > W + 64 || sy > H + 64) continue;
                if (p.t->behaves == 3 || p.t->behaves == 4 || !p.t->img) {   // spark / railgun dot
                    const SDL_Color c = color_at(*p.t, p.color_t);
                    if (c.r == 0 && c.g == 0 && c.b == 0) continue;
                    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
                    SDL_Rect r{sx - 1, sy - 1, 2, 2};
                    SDL_RenderFillRect(ren, &r);
                    continue;
                }
                const FxAnimType& img = *p.t->img;
                int fr = p.state;
                if (p.t->behaves == 1 && p.t->loop_frames > 0) fr = p.t->start_frame + p.state % p.t->loop_frames;
                fr = std::clamp(fr, 0, std::max(0, img.frames - 1));
                blit_alpha(ren, tc, img, fr, sx, sy, alpha_of(p.trans), W, H);
            }
        }
        for (const Bouncer& b : bouncers_) {
            int sx = 0, sy = 0;
            to_screen(cam, b.x, b.y, b.z, sx, sy);
            const int fr = PresFx::loop_frame(*b.t, b.age * std::max(1, b.t->delay) / std::max(1, b.delay));
            fx_->blit(ren, tc, *b.t->shp, "fx:" + b.t->name, fr, sx, sy, b.t->translucent, W, H);
        }
        // W5-PRES R-120: flashing objects are drawn brightened per object
        // (pres_state.inc ObjTint) -- the old additive-diamond stand-in is off.
        if (unit_pal_ && has_vpl_)
            for (const VoxelDebris& v : voxels_) {
                int sx = 0, sy = 0;
                to_screen(cam, v.x, v.y, v.z, sx, sy);
                const int yaw_ix = (static_cast<int>(v.angle / 360.0 * kVoxYaws) % kVoxYaws + kVoxYaws) % kVoxYaws;
                const ra2engine::VoxelSprite* sp = voxel_sprite(*v.d, yaw_ix);
                if (!sp || sp->w <= 0) continue;
                const std::string key = "vxdeb:" + v.d->name + ":" + std::to_string(yaw_ix);
                SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
                if (!tex) tex = static_cast<SDL_Texture*>(tc.get_rgba(key, sp->rgba.data(), sp->w, sp->h));
                if (!tex) continue;
                SDL_Rect dst{sx - sp->anchor_x, sy - sp->anchor_y, sp->w, sp->h};
                SDL_RenderCopy(ren, tex, nullptr, &dst);
            }
    }

    // ---- per-frame hooks fed by Presentation ------------------------------
    // Projectile trailers + line trails; the V3 rocket trail; ship wakes.
    void observe_projectiles(const ra2engine::World& w, const BuildingAssets& ba) {
        proj_ba_ = &ba;
        std::map<long long, bool> seen;
        for (const ra2engine::Projectile& p : w.projectiles()) {
            const std::string wid=projectile_weapon(w,p);
            if (wid.empty()) continue;
            const ProjArt& pa = proj_art(wid);
            const double z = p.terrain_origin_level * 104.0 + p.z;
            if (!pa.trailer.empty() && frame_ % std::max(1, pa.spawn_delay) == 0)
                fx_->spawn(pa.trailer, p.x / 256.0, p.y / 256.0, 0, z * kFxLepPx);
            if (pa.line) {
                const long long key = (static_cast<long long>(p.attacker + 2) << 40) ^
                                      (static_cast<long long>(p.attacker_building + 2) << 20) ^ p.launch_frame;
                seen[key] = true;
                LineTrail& lt = lines_[key];
                lt.color = pa.line_color;
                lt.decrement = pa.line_decrement;
                lt.live = true;
                lt.pts.push_back(LinePt{static_cast<double>(p.x), static_cast<double>(p.y), z, 255});
                if (lt.pts.size() > 32) lt.pts.erase(lt.pts.begin());
            }
        }
        for (auto& kv : lines_)
            if (!seen.count(kv.first)) kv.second.live = false;
        // Nuclear carriers are timed strikes, not members of projectiles().
        // Observe their existing clock here, once per logic tick, so the
        // retail NKMSLUP Trailer= is not skipped or emitted per render frame.
        for(const auto& strike:w.sw_strikes()) {
            if(strike.kind!=1 || strike.done || strike.frame>=w.sw_params().nuke_up_frames ||
               !w.object_visible_at(g_local_house,strike.ox,strike.oy))continue;
            const auto& pa=proj_art("NukeCarrier");
            if(!pa.trailer.empty() && frame_%std::max(1,pa.spawn_delay)==0) {
                const double z=w.cell_level(strike.ox,strike.oy)*104.0+
                    ra2engine::nuke_visual_height(strike.frame,w.sw_params().nuke_up_frames,w.sw_params().nuke_down_frames);
                fx_->spawn(pa.trailer,strike.ox,strike.oy,0,z*kFxLepPx);
            }
        }
    }
    void observe_units(const ra2engine::World& w) {
        const auto& us = w.units();
        for (std::size_t i = 0; i < us.size(); ++i) {
            const ra2engine::Unit& u = us[i];
            const bool active=u.alive && u.die_time<0.0 && u.cargo_of<0;
            const bool launch_edge=rocket_launches_.observe(i,active,u.an.rocket.phase>=ra2engine::kRocketClimb);
            if(!active)continue;
            // Ship wake: every 8 frames while moving on water.
            // W7-06-04 [RE: ShipLocomotion Process FUN_0069F2A0 @0x69F4C9 --
            // Type->Underwater (+0xD69) never spawns a wake; nor on a bridge]:
            // subs, dolphins and squids leave none.
            if (!wake_.empty() && u.is_naval && !u.an.t.underwater && !u.on_deck && u.moving() && frame_ % 8 == 0 &&
                on_water(w, u.cell_x, u.cell_y) && w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) {
                // WAKE1 is Flat / Layer=ground: PresFx draws it in its ground
                // pass under the ships (agent B's shared anim drawing).
                if (fx_->spawn(wake_, u.fx, u.fy, u.level, 0.0)) ++wakes_;
            }
            // W8-04-04 Hover wake (LCRF / SAPC / ROBO / YHVR) [RE:
            // HoverLocomotionClass::Process FUN_00514130 @0x514841]: while
            // Is_Moving_Now (vt+0x80), every 10 frames, not OnBridge (+0x8C),
            // cell LandType Water: [General] Wake= at the unit.
            else if (!wake_.empty() && !u.is_naval && ra2engine::unit_loco_kind(u) == ra2engine::LocoKind::Hover &&
                     !u.on_deck && u.moving() && frame_ % 10 == 0 && on_water(w, u.cell_x, u.cell_y) &&
                     w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) {
                if (fx_->spawn(wake_, u.fx, u.fy, u.level, 0.0)) ++wakes_;
            }
            // V3 / Dreadnought rocket (RocketLocomotion, FUN_00661D00): the
            // launch anim when the tilt ends and it leaves the launcher, then
            // the trail every 3 frames while it flies.
            if ((u.an.t.missile || pres_upper(u.image) == v3_image() || pres_upper(u.image) == dmisl_image()) &&
                w.object_visible_at(g_local_house,u.cell_x,u.cell_y)) {
                const double z = u.an.t.missile ? u.an.rocket.terrain_origin_level*104.0+u.an.rocket.z : u.level*104.0+u.altitude;
                if((u.an.rocket.phase==ra2engine::kRocketPause || (u.an.t.rocket_kind==ra2engine::kRocketCMisl && u.an.rocket.phase==ra2engine::kRocketTilt)) && (u.an.rocket.frames%24)==1) {
                    if(fx_->spawn("V3TAKOFF",u.fx,u.fy,0,std::max(0.0,z-10.0)*kFxLepPx))++v3_puffs_;
                }
                if (u.an.rocket.phase >= ra2engine::kRocketClimb) {
                    if (launch_edge) {
                        if (fx_->spawn("V3TAKOFF", u.fx, u.fy, 0, std::max(0.0, z - 10.0) * kFxLepPx)) ++v3_puffs_;
                    } else if (frame_ % 3 == 0) {
                        if (fx_->spawn("V3TRAIL", u.fx, u.fy, 0, z * kFxLepPx)) ++v3_puffs_;
                    }
                }
            }
        }
    }
    // A weapon whose shot the clone resolves instantly (no flying
    // projectile) but whose projectile art draws a line trail (UseLineTrail,
    // e.g. the IFV's HoverMissile -> DRAGON): the trail is laid along the
    // shot, newest at the target, and fades at LineTrailColorDecrement.
    // [RE-FALLBACK: the original lays it point by point as the missile flies.]
    void instant_line(const std::string& weapon, double x0, double y0, double z0, double x1, double y1,
                      double z1) {
        if (weapon.empty()) return;
        const ProjArt& pa = proj_art(weapon);
        if (!pa.line) return;
        LineTrail lt;
        lt.color = pa.line_color;
        lt.decrement = pa.line_decrement;
        lt.live = false;
        const int n = 12;
        for (int k = 0; k <= n; ++k) {
            const double f = static_cast<double>(k) / n;
            const double arc = std::sin(f * 3.14159265) * 64.0;   // a shallow missile arc
            lt.pts.push_back(LinePt{x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, z0 + (z1 - z0) * f + arc,
                                    255 - (n - k) * lt.decrement});
        }
        lines_[--instant_key_] = lt;
    }
    std::string unit_weapon(const std::string& image, int slot) { return weapon_of(unit_id(image), slot); }
    // A rules key with the effects' RA2-first section lookup (AmbientSound, ...).
    std::string rules_key(const std::string& s, const std::string& k) const { return rget(s, k); }
    ra2engine::RocketLaunchTracker rocket_launches_;   // W4: rockets that showed their launch anim
    // W4 (W4-PJ-02/03/05/06/07): the projectile in flight, drawn from the
    // weapon that FIRED it (unit slot or base defence), its art [Image]:
    // Voxel=yes bullets (ZBOMB, SUBT, the Grand Cannon shell) as a voxel at
    // the velocity's yaw and pitch, else the SHP, by facing when the ART
    // section says Rotates=yes (BulletTypeClass reads Rotates from ArtINI,
    // 0x46C180 / 0x886100). Returns false -> the caller's placeholder.
    std::string projectile_weapon(const ra2engine::World& w, const ra2engine::Projectile& p) {
        if(!p.weapon_id.empty()) return p.weapon_id;
        if (p.attacker >= 0 && p.attacker < static_cast<int>(w.units().size())) {
            const ra2engine::Unit& u = w.units()[static_cast<std::size_t>(p.attacker)];
            return weapon_of(pres_type_id(u), p.weapon_slot);
        }
        if (proj_ba_ && p.attacker_building >= 0 && p.attacker_building < static_cast<int>(w.buildings().size())) {
            const ra2engine::Building& b = w.buildings()[static_cast<std::size_t>(p.attacker_building)];
            if (b.type >= 0 && b.type < static_cast<int>(proj_ba_->roster.size()))
                return weapon_of(proj_ba_->roster[static_cast<std::size_t>(b.type)].id, p.weapon_slot);
        }
        return std::string();
    }
    auto& proj_draw(const std::string& weapon) {
        auto it = proj_draw_.find(weapon);
        if (it != proj_draw_.end()) return it->second;
        ProjDraw d{};
        const std::string pj = rget(weapon, "Projectile");
        std::string img = pj.empty() ? std::string() : rget(pj, "Image");
        if (img.empty()) img = pj;
        d.image = pres_upper(img);
        if (!d.image.empty() && pres_lower(d.image) != "none") {
            d.voxel = yes(aget(d.image, "Voxel"));
            d.rotates = yes(aget(d.image, "Rotates"));
            // Art AnimPalette (def=no): a bullet SHP draws in the unit
            // palette unless it asks for anim.pal (the 120MM shell's indices
            // 137..143 are dull grey-brown in unittem.pal, garbage in anim.pal).
            d.anim_pal = yes(aget(d.image, "AnimPalette"));
            // [Projectile] Shadow= (def=yes): the ground shadow under the shell.
            const std::string sh = rget(pj, "Shadow");
            d.shadow = sh.empty() ? true : yes(sh);
            if (d.voxel) d.vox = voxel_def(d.image);
            if (!d.vox) d.shp = fx_->projectile_shp(d.image);
        }
        return proj_draw_.emplace(weapon, std::move(d)).first->second;
    }
    bool draw_projectile(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w,
                         const ra2engine::Projectile& p, int gx, int sy, int gy, int W, int H) {
        const std::string wid = projectile_weapon(w, p);
        if (wid.empty()) return false;
        auto& d = proj_draw(wid);
        if (d.vox && unit_pal_ && has_vpl_) {
            const double hv = std::sqrt(p.vx * p.vx + p.vy * p.vy);
            // Model yaw in the voxel convention (index_to_yaw: +X = facing 0x4000
            // = screen east): the lepton-plane heading atan2(vy, vx) maps to
            // facing_u16_from_delta(vx, vy).
            const double fu16 = ra2engine::facing_u16_from_delta(p.vx, p.vy);
            const int yi = ra2engine::facing_to_index(fu16, 32);
            const double pitch = std::atan2(p.vz, std::max(hv, 1e-6));
            const int pi = static_cast<int>(std::lround(pitch * 180.0 / 3.14159265358979 / 15.0));
            const int key = yi * 64 + (pi + 16);
            auto it = d.sprites.find(key);
            if (it == d.sprites.end()) {
                ra2engine::VoxelSprite sp;
                try {
                    sp = ra2engine::rasterize_model(d.vox->vxl, d.vox->hva, ra2engine::index_to_yaw(yi, 32), vpl_,
                                                    *unit_pal_, 0, pi * 15.0 * 3.14159265358979 / 180.0);
                } catch (...) {}
                it = d.sprites.emplace(key, std::move(sp)).first;
            }
            const ra2engine::VoxelSprite& sp = it->second;
            if (sp.w <= 0) return false;
            const std::string tk = "pvx:" + d.image + ":" + std::to_string(key);
            SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(tk));
            if (!tex) tex = static_cast<SDL_Texture*>(tc.get_rgba(tk, sp.rgba.data(), sp.w, sp.h));
            if (!tex) return false;
            SDL_Rect dst{gx - sp.anchor_x, sy - sp.anchor_y, sp.w, sp.h};
            if (dst.x > W || dst.y > H || dst.x + dst.w < 0 || dst.y + dst.h < 0) return true;
            SDL_RenderCopy(ren, tex, nullptr, &dst);
            return true;
        }
        if (!d.shp || d.shp->count() <= 0) return false;
        if (d.shadow && gy > sy + 2) {   // Shadow=yes: a dark blob on the ground below
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 110);
            SDL_Rect r{gx - 2, gy - 1, 4, 2};
            SDL_RenderFillRect(ren, &r);
        }
        int fr = 0;
        if (d.rotates && d.shp->count() > 1) {
            // Facing frames: 0 = north, clockwise (TS/RA2 bullet SHP convention)
            // [RE-FALLBACK: RE-P5 open -- rotating-bullet frame order]; the
            // screen-space velocity (voxel-render.md §6).
            const double sdx = (p.vx - p.vy) * 30.0, sdy = (p.vx + p.vy) * 15.0 - p.vz * 36.74;
            const double ang = std::atan2(sdx, -sdy);
            const int n = d.shp->count();
            fr = static_cast<int>(std::lround((ang < 0 ? ang + 2 * 3.14159265358979 : ang) /
                                              (2 * 3.14159265358979) * n)) % n;
        }
        if (!d.anim_pal && unit_pal_)
            fx_->blit(ren, tc, *d.shp, "projU:" + d.image, fr, gx, sy, 255, W, H, unit_pal_);
        else
            fx_->blit(ren, tc, *d.shp, "proj:" + d.image, fr, gx, sy, 255, W, H);
        return true;
    }

    long wakes_spawned() const { return wakes_; }
    long v3_puffs() const { return v3_puffs_; }
    void note_splash() { ++splashes_; }
    long splashes() const { return splashes_; }
    void note_unit_id(const std::string& image, const std::string& id) {
        if (!image.empty() && !id.empty() && !unit_ids_.count(image)) unit_ids_[image] = id;
    }
    std::string unit_id(const std::string& image) const {
        auto it = unit_ids_.find(image);
        return it == unit_ids_.end() ? image : it->second;
    }

private:
    struct Part {
        const FxPtType* t = nullptr;
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0, vz = 0;
        double speed = 0;
        int dx = 0, dy = 0;
        int ec = 0;
        int age = 0;
        int state = 0;
        int trans = 0;
        double color_t = 0;
        bool dead = false;
    };
    struct Sys {
        int id = 0;
        const FxPsType* t = nullptr;
        double x = 0, y = 0, z = 0, tx = 0, ty = 0, tz = 0;
        double offx = 0, offy = 0, offz = 0;
        Owner owner;
        double accum = 0;
        int lifetime = -1;
        int spark_frames = 0;
        int laser_left = 0;
        bool dying = false;
        std::vector<Part> parts;
    };
    struct Bouncer {
        const FxAnimType* t = nullptr;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        double elasticity = 0;
        std::string trailer, expire;
        std::string spawns;      // W5-PRES R-123 Spawns= / SpawnCount=
        int spawn_count = 0;
        int trailer_sep = 3;
        int delay = 1;
        long age = 0;
        bool done = false;
    };
    struct VoxelDef {
        std::string name;
        ra2assets::VxlModel vxl;
        ra2assets::HvaModel hva;
        double elasticity = 0, min_ang = 5, max_ang = 9, min_z = 24, max_z = 28, max_xy = 15;
        int duration = 0;
        std::string expire;
        std::vector<ra2engine::VoxelSprite> yaws;   // lazily rasterized
        std::vector<bool> made;
    };
    struct VoxelDebris {
        const VoxelDef* d = nullptr;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        double angle = 0, spin = 0;
        int life = 0;
        bool done = false;
    };
    struct Ground { const FxAnimType* t; double x, y, z; long age; };
    struct Twinkle { int x = 0, y = 0; long age = 0; int wait = 0; };
    struct LinePt { double x, y, z; int intensity; };
    struct LineTrail { std::vector<LinePt> pts; SDL_Color color{255, 255, 255, 255}; int decrement = 16; bool live = true; };
    struct ProjArt { std::string trailer; int spawn_delay = 3; bool line = false; SDL_Color line_color{255, 255, 255, 255}; int line_decrement = 16; };
    struct ProjDraw {
        std::string image;
        bool voxel = false, rotates = false, anim_pal = false, shadow = true;
        const VoxelDef* vox = nullptr;
        std::shared_ptr<ra2assets::ShpFile> shp;
        std::map<int, ra2engine::VoxelSprite> sprites;   // yaw x pitch -> sprite
    };
    const BuildingAssets* proj_ba_ = nullptr;
    std::map<std::string, ProjDraw> proj_draw_;
    static constexpr int kVoxYaws = 16;

    // ---- one game frame ----------------------------------------------------
    void tick(const ra2engine::World& w, const BuildingAssets& ba) {
        ++frame_;
        (void)ba;
        for (Sys& s : systems_) step_system(w, s);
        systems_.erase(std::remove_if(systems_.begin(), systems_.end(),
                                      [](const Sys& s) { return s.dying && s.parts.empty(); }),
                       systems_.end());
        for (Bouncer& b : bouncers_) step_bouncer(w, b);
        bouncers_.erase(std::remove_if(bouncers_.begin(), bouncers_.end(), [](const Bouncer& b) { return b.done; }),
                        bouncers_.end());
        for (VoxelDebris& v : voxels_) step_voxel(w, v);
        voxels_.erase(std::remove_if(voxels_.begin(), voxels_.end(), [](const VoxelDebris& v) { return v.done; }),
                      voxels_.end());
        for (Ground& g : grounds_) ++g.age;
        grounds_.erase(std::remove_if(grounds_.begin(), grounds_.end(),
                                      [](const Ground& g) { return g.t->frame_at(g.age) < 0; }),
                       grounds_.end());
        for (Twinkle& t : twinkles_) {
            if (t.wait > 0) { --t.wait; continue; }
            ++t.age;
            if (twinkle_t_ && twinkle_t_->delay > 0 &&
                t.age >= static_cast<long>(twinkle_t_->end - twinkle_t_->start) * twinkle_t_->delay) {
                // One pass done: RandomLoopDelay pause before the next.
                t.age = 0;
                t.wait = tw_delay0_ + static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, tw_delay1_ - tw_delay0_ + 1)));
            }
        }
        for (auto it = lines_.begin(); it != lines_.end();) {
            LineTrail& lt = it->second;
            for (LinePt& p : lt.pts) p.intensity -= lt.decrement;
            while (!lt.pts.empty() && lt.pts.front().intensity <= 0) lt.pts.erase(lt.pts.begin());
            if (!lt.live && lt.pts.empty()) it = lines_.erase(it);
            else ++it;
        }
    }

    void step_system(const ra2engine::World& w, Sys& s) {
        if (s.owner.kind != 0) {
            double ox = 0, oy = 0, oz = 0;
            if (owner_pos(s.owner, ox, oy, oz)) {
                s.x = ox + s.offx; s.y = oy + s.offy; s.z = oz + s.offz;
            } else {
                s.owner = Owner{};
                s.dying = true;   // the owner is gone: no new puffs
            }
        }
        std::vector<Part> successors;
        successors.reserve(std::min<std::size_t>(s.parts.size() * 2, 512));
        for (Part& p : s.parts) {
            const bool was_alive = !p.dead;
            step_part(w, s, p);
            // Yuri's Revenge NextParticle emits two successors as a particle
            // expires. Defer insertion until after this loop so vector growth
            // cannot invalidate the current particle reference.
            if (was_alive && p.dead && p.t && !p.t->next.empty() && successors.size() < 512) {
                for (int i = 0; i < 2 && successors.size() < 512; ++i) {
                    Part q = make_part(p.t->next, p.x + p.t->next_dx,
                                       p.y + p.t->next_dy, p.z + p.t->next_dz);
                    if (!q.t) continue;
                    if (rnd() % 6 != 0) q.trans = std::min(100, p.trans + 25);
                    successors.push_back(std::move(q));
                }
            }
        }
        s.parts.erase(std::remove_if(s.parts.begin(), s.parts.end(), [](const Part& p) { return p.dead; }),
                      s.parts.end());
        s.parts.insert(s.parts.end(), successors.begin(), successors.end());
        const FxPsType& t = *s.t;
        if (t.behaves == 3) {   // Spark
            if (s.spark_frames > 0 && !s.dying) {
                const double r01 = static_cast<double>(rnd() % 10000) / 10000.0;
                if (s.spark_frames == 1 || r01 <= t.spark_pct) {
                    const int half = std::max(1, t.cap / 2);
                    const int n = half + static_cast<int>(rnd() % static_cast<std::uint32_t>(half));
                    for (int k = 0; k < n; ++k) spawn_spark(s);
                }
                if (--s.spark_frames < 1) s.dying = true;
            }
        } else if (t.behaves == 4) {   // Railgun: one burst at creation
            if (s.laser_left > 0) --s.laser_left;
            s.dying = true;
        } else if (t.spawns && !s.dying) {   // Smoke / Gas / Fire
            const int every = std::max(1, static_cast<int>(s.accum));
            if (frame_ % every == 0 && static_cast<int>(s.parts.size()) < std::max(1, t.cap)) {
                const int r = t.radius;
                auto off = [&]() { return r > 0 ? static_cast<double>(static_cast<int>(rnd() % (2 * r + 1)) - r) : 0.0; };
                Part p = make_part(t.holds, s.x + off(), s.y + off(), s.z + 10.0);
                if (p.t) {
                    if (s.accum > t.trans_cutoff) p.trans += 25;
                    p.speed -= (s.accum - t.spawn_frames);
                    if (p.speed < 0.5) p.speed = 2.0;
                    s.parts.push_back(p);
                }
            }
            s.accum += t.slowdown;
            if (s.accum > t.cutoff) s.dying = true;
        } else if (!t.spawns && s.parts.empty() && !s.dying) {
            // A non-spawning smoke/gas system holds one particle (GasCloudSys).
            Part p = make_part(t.holds, s.x, s.y, s.z);
            if (p.t) s.parts.push_back(p);
            s.dying = true;
        }
        if (s.lifetime > 0 && --s.lifetime == 0) {
            s.dying = true;
            s.parts.clear();
        }
    }

    Part make_part(const std::string& name, double x, double y, double z) {
        Part p;
        p.t = pt_type(name);
        if (!p.t) return p;
        p.x = x; p.y = y; p.z = z;
        p.speed = p.t->velocity;
        p.ec = p.t->max_ec + (p.t->max_ec > 0 ? static_cast<int>(rnd() % static_cast<std::uint32_t>(p.t->max_ec)) : 0);
        p.state = p.t->start_state;
        p.trans = p.t->translucency;
        return p;
    }
    void spawn_spark(Sys& s) {
        Part p = make_part(s.t->holds, s.x, s.y, s.z);
        if (!p.t) return;
        auto sr = [&](int m) { return m > 0 ? static_cast<double>(static_cast<int>(rnd() % (2 * m + 1)) - m) : 0.0; };
        p.vx = sr(p.t->xvel);
        p.vy = sr(p.t->yvel);
        p.vz = p.t->min_zvel + (p.t->zvel_range > 0 ? static_cast<double>(rnd() % static_cast<std::uint32_t>(p.t->zvel_range)) : 0.0);
        s.parts.push_back(p);
    }
    // Railgun: particles along the line at ParticlesPerCoord, on a spiral of
    // SpiralRadius [RE-FALLBACK: FUN_0062ecd0's perturbation terms not unwound].
    void railgun_burst(Sys& s) {
        const double dx = s.tx - s.x, dy = s.ty - s.y, dz = s.tz - s.z;
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        const int n = std::min(400, static_cast<int>(len * s.t->per_coord));
        for (int k = 0; k < n; ++k) {
            const double f = n > 1 ? static_cast<double>(k) / (n - 1) : 0.0;
            const double a = k * s.t->spiral_d * 50.0;
            Part p = make_part(s.t->holds, s.x + dx * f + std::cos(a) * s.t->spiral_r,
                               s.y + dy * f + std::sin(a) * s.t->spiral_r, s.z + dz * f);
            if (p.t) s.parts.push_back(p);
        }
        s.laser_left = s.t->laser ? 8 : 0;
    }

    void step_part(const ra2engine::World& w, Sys& s, Part& p) {
        (void)s;
        ++p.age;
        const FxPtType& t = *p.t;
        if (t.behaves == 3) {   // Spark: ballistic under Gravity, dies on the ground
            p.vz -= gravity_;
            p.x += p.vx; p.y += p.vy; p.z += p.vz;
            p.color_t += t.color_speed;
            if (p.z <= ground_z(w, p.x, p.y)) p.dead = true;
            if (t.colors.size() > 1 && p.color_t >= static_cast<double>(t.colors.size() - 1)) p.dead = true;
        } else if (t.behaves == 4) {   // Railgun dot: fade along the ColorList
            p.color_t += t.color_speed;
            if (t.colors.size() > 1 && p.color_t >= static_cast<double>(t.colors.size() - 1)) p.dead = true;
        } else {   // Smoke / Gas / Fire
            if ((frame_ & 1) == 0 && rnd() % 8 == 0) {
                const int d = static_cast<int>(rnd() % 3) - 1;
                if (rnd() & 1) p.dx = std::clamp(p.dx + d, -2, 2);
                else p.dy = std::clamp(p.dy + d, -2, 2);
            }
            if (frame_ & 1) {
                // [RE-FALLBACK: the velocity matrix of FUN_0062b7f0 is not
                // unwound -- the puff rises by Speed every other frame.]
                p.z += p.speed;
                p.x += p.dx; p.y += p.dy;
                if (t.wind > 0) p.x += t.wind;
            }
            p.speed = std::max(1.0, p.speed - t.deacc);
            const int adv = std::max(1, t.state_adv) + static_cast<int>((p.age >> 3) & 1);
            if (p.age % adv == 0) ++p.state;
            if (t.end_state > 0 && p.state >= t.end_state) {
                if (t.delete_on_limit) p.dead = true;
                else p.state = 0;
            }
        }
        if (--p.ec <= 0) p.dead = true;
    }

    void step_bouncer(const ra2engine::World& w, Bouncer& b) {
        ++b.age;
        // [RE: FUN_00423ae0] TrailerAnim every TrailerSeperation frames.
        if (!b.trailer.empty() && frame_ % b.trailer_sep == 0)
            fx_->spawn(b.trailer, b.x / 256.0, b.y / 256.0, 0, b.z * kFxLepPx);
        b.vz -= 1.4;   // [RE: FUN_00421ec0 -> Bounce.Initialize gravity 1.4]
        b.x += b.vx; b.y += b.vy; b.z += b.vz;
        const double gz = ground_z(w, b.x, b.y);
        if (b.z <= gz) {
            b.z = gz;
            const double sp = std::sqrt(b.vx * b.vx + b.vy * b.vy + b.vz * b.vz) * b.elasticity;
            if (b.elasticity <= 0.0 || sp < 4.0) {   // Impact [RE-FALLBACK: threshold]
                if (!b.expire.empty()) fx_->spawn(b.expire, b.x / 256.0, b.y / 256.0, 0, b.z * kFxLepPx);
                if (!b.spawns.empty() && b.spawn_count > 0) {
                    const std::uint32_t m = static_cast<std::uint32_t>(b.spawn_count + 1);
                    const int n = static_cast<int>(rnd() % m + rnd() % m);
                    for (int k = 0; k < n; ++k) fx_->spawn(b.spawns, b.x / 256.0, b.y / 256.0, 0, b.z * kFxLepPx);
                }
                b.done = true;
                return;
            }
            b.vz = -b.vz * b.elasticity;
            b.vx *= b.elasticity;
            b.vy *= b.elasticity;
        }
        if (b.age > 600) b.done = true;
    }
    void step_voxel(const ra2engine::World& w, VoxelDebris& v) {
        v.angle += v.spin * 8.0;
        v.vz -= 1.4;
        v.x += v.vx; v.y += v.vy; v.z += v.vz;
        const double gz = ground_z(w, v.x, v.y);
        if (v.z <= gz) {
            v.z = gz;
            const double e = v.d->elasticity;
            const double sp = std::sqrt(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz) * e;
            if (e <= 0.0 || sp < 4.0) {
                if (!v.d->expire.empty()) fx_->spawn(v.d->expire, v.x / 256.0, v.y / 256.0, 0, v.z * kFxLepPx);
                v.vx = v.vy = v.vz = 0;
                v.spin = 0;
            } else {
                v.vz = -v.vz * e;
                v.vx *= e;
                v.vy *= e;
            }
        }
        if (--v.life <= 0) v.done = true;   // Duration
    }

    // Flashing objects [RE-FALLBACK: the original redraws a flashing object
    // with its brightened drawer on alternate frames; the clone pulses an
    // additive white diamond over the footprint on alternate 2-frame beats].
    void draw_flashes(SDL_Renderer* ren, const ra2engine::Camera& cam) {
        if (!world_ || (g_flash_units.empty() && g_flash_buildings.empty())) return;
        const long now = static_cast<long>(world_->vet_frame());
        if ((now / 2) % 2) return;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
        auto diamond = [&](double x, double y, double z, int hw, int hh) {
            int sx = 0, sy = 0;
            to_screen(cam, x, y, z, sx, sy);
            for (int r = 0; r < hh; ++r) {
                const int w = hw * (hh - r) / hh;
                SDL_Rect a{sx - w, sy - r, 2 * w + 1, 1}, b{sx - w, sy + r, 2 * w + 1, 1};
                SDL_RenderFillRect(ren, &a);
                if (r) SDL_RenderFillRect(ren, &b);
            }
        };
        for (const auto& kv : g_flash_units) {
            if (kv.second < now || kv.first < 0 || kv.first >= static_cast<int>(world_->units().size())) continue;
            const ra2engine::Unit& u = world_->units()[static_cast<std::size_t>(kv.first)];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            if (!world_->object_visible_at(g_local_house, u.cell_x, u.cell_y)) continue;
            diamond(u.fx * 256.0, u.fy * 256.0, u.level * 104.0 + (u.is_vehicle ? 60.0 : 90.0),
                    u.is_vehicle ? 20 : 9, u.is_vehicle ? 12 : 14);
        }
        for (const auto& kv : g_flash_buildings) {
            if (kv.second < now || kv.first < 0 || kv.first >= static_cast<int>(world_->buildings().size())) continue;
            const ra2engine::Building& b = world_->buildings()[static_cast<std::size_t>(kv.first)];
            if (!b.alive || b.state == ra2engine::Building::State::Dead) continue;
            double x = 0, y = 0, z = 0;
            building_pos(b, x, y, z);
            diamond(x, y, z + 40.0, 15 * (b.fw + b.fh), 15 * (b.fw + b.fh) / 2);
        }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    }

    // ---- helpers ---------------------------------------------------------------
    // A 2-px line from filled rects (the shot renderer drops RenderDrawLine
    // primitives on some targets; rects always land).
    static void plot_line(SDL_Renderer* ren, int x0, int y0, int x1, int y1) {
        const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        for (int i = 0; i <= n; ++i) {
            const int x = n ? x0 + (x1 - x0) * i / n : x0, y = n ? y0 + (y1 - y0) * i / n : y0;
            SDL_Rect r{x, y, 2, 2};
            SDL_RenderFillRect(ren, &r);
        }
    }
    int condition(int hp, int max_hp) const {
        const double r = max_hp > 0 ? static_cast<double>(hp) / max_hp : 1.0;
        return r <= red_ ? 2 : r <= yellow_ ? 1 : 0;
    }
    int damage_smoke(const std::string& id, double x, double y, double z, Owner owner) {
        std::vector<std::string> smoke;
        for (const std::string& n : split(rget(id, "DamageParticleSystems"))) {
            const FxPsType* t = ps_type(n);
            if (t && t->behaves == 0) smoke.push_back(n);   // BehavesLike=Smoke only
        }
        if (smoke.empty()) return 0;
        const std::vector<std::string> off = split(rget(id, "DamageSmokeOffset"));
        if (off.size() >= 3) {
            x += std::atof(off[0].c_str());
            y += std::atof(off[1].c_str());
            z += std::atof(off[2].c_str());
        }
        return spawn_system(smoke[rnd() % smoke.size()], x, y, z, owner);
    }
    bool owner_pos(const Owner& o, double& x, double& y, double& z) const {
        if (!world_) return false;
        if (o.kind == 1) {
            if (o.index < 0 || o.index >= static_cast<int>(world_->units().size())) return false;
            const ra2engine::Unit& u = world_->units()[static_cast<std::size_t>(o.index)];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) return false;
            x = u.fx * 256.0; y = u.fy * 256.0;
            z = u.level * 104.0 + (u.is_aircraft ? u.altitude : 0.0);
            return true;
        }
        if (o.kind == 2) {
            if (o.index < 0 || o.index >= static_cast<int>(world_->buildings().size())) return false;
            const ra2engine::Building& b = world_->buildings()[static_cast<std::size_t>(o.index)];
            if (!b.alive || b.state == ra2engine::Building::State::Dead) return false;
            building_pos(b, x, y, z);
            return true;
        }
        return false;
    }
    void building_pos(const ra2engine::Building& b, double& x, double& y, double& z) const {
        x = (b.cx + (b.fw - 1) / 2.0) * 256.0;   // the clone's footprint centre convention
        y = (b.cy + (b.fh - 1) / 2.0) * 256.0;
        const ra2assets::MapCell* mc = world_ ? world_->map().at(b.cx, b.cy) : nullptr;
        z = (mc ? mc->level : 0) * 104.0;
    }
    double ground_z(const ra2engine::World& w, double x, double y) const {
        const ra2assets::MapCell* mc = w.map().at(ra2engine::lepton_to_cell(static_cast<int>(x)),
                                                  ra2engine::lepton_to_cell(static_cast<int>(y)));
        return (mc ? mc->level : 0) * 104.0;
    }
    static bool on_water(const ra2engine::World& w, int x, int y) {
        const auto& wg = w.grid_for(ra2engine::MovementDomain::Water);
        if (wg.empty() || x < 0 || y < 0 || x >= w.grid_w() || y >= w.grid_h()) return false;
        return wg[static_cast<std::size_t>(y) * w.grid_w() + x] != 0;
    }
    static void to_screen(const ra2engine::Camera& cam, double x, double y, double z, int& sx, int& sy) {
        const double fx = x / 256.0, fy = y / 256.0;
        sx = static_cast<int>(std::lround((fx - fy) * 30.0)) + cam.x + 30;
        sy = static_cast<int>(std::lround((fx + fy) * 15.0 - z * kFxLepPx)) + cam.y + 15;
    }
    static int alpha_of(int translucency) {
        return std::clamp(255 - translucency * 255 / 100, 24, 255);
    }
    void blit_alpha(SDL_Renderer* ren, ra2engine::TextureCache& tc, const FxAnimType& img, int fr, int cx,
                    int cy, int alpha, int W, int H) {
        const ra2assets::ShpFile& shp = *img.shp;
        if (fr < 0 || fr >= shp.count()) return;
        const ra2assets::ShpFile::Frame f = shp.frame_info(fr);
        if (f.w <= 0 || f.h <= 0) return;
        SDL_Rect dst{cx - shp.width() / 2 + f.x, cy - shp.height() / 2 + f.y, f.w, f.h};
        if (dst.x + dst.w <= 0 || dst.y + dst.h <= 0 || dst.x >= W || dst.y >= H) return;
        const std::string key = "fx:" + img.name + ":" + std::to_string(fr);
        SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
        if (!tex) {
            const std::vector<uint8_t> idx = shp.decode_indexed(fr);
            tex = static_cast<SDL_Texture*>(tc.get(key, idx.data(), f.w, f.h, fx_->pal()));
        }
        if (!tex) return;
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alpha));
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
    }
    static SDL_Color color_at(const FxPtType& t, double ct) {
        if (t.colors.empty()) return SDL_Color{255, 255, 200, 255};
        const int n = static_cast<int>(t.colors.size());
        const int i = std::clamp(static_cast<int>(ct), 0, n - 1);
        const int j = std::min(n - 1, i + 1);
        const double f = std::clamp(ct - i, 0.0, 1.0);
        auto lerp = [&](Uint8 a, Uint8 b) { return static_cast<Uint8>(a + (b - a) * f); };
        return SDL_Color{lerp(t.colors[i].r, t.colors[j].r), lerp(t.colors[i].g, t.colors[j].g),
                         lerp(t.colors[i].b, t.colors[j].b), 255};
    }

    // Types (lazy, from rules).
    const FxPsType* ps_type(const std::string& name) {
        const std::string key = pres_upper(name);
        if (key.empty()) return nullptr;
        auto it = ps_types_.find(key);
        if (it != ps_types_.end()) return it->second.get();
        std::unique_ptr<FxPsType> t;
        if (has_sec(key) && !rget(key, "HoldsWhat").empty()) {
            t = std::make_unique<FxPsType>();
            t->name = key;
            t->holds = pres_upper(rget(key, "HoldsWhat"));
            t->spawns = yes(rget(key, "Spawns"));
            t->spawn_frames = num(key, "SpawnFrames", 1);
            t->slowdown = dbl(key, "Slowdown", 0.0);
            t->cap = num(key, "ParticleCap", 10);
            t->radius = num(key, "SpawnRadius", 0);
            t->cutoff = dbl(key, "SpawnCutoff", 30.0);
            t->trans_cutoff = dbl(key, "SpawnTranslucencyCutoff", 30.0);
            t->behaves = behaves(rget(key, "BehavesLike"));
            t->lifetime = num(key, "Lifetime", -1);
            t->spark_frames = num(key, "SparkSpawnFrames", 0);
            t->spark_pct = dbl(key, "SpawnSparkPercentage", 1.0);
            t->per_coord = dbl(key, "ParticlesPerCoord", 0.1);
            t->spiral_r = dbl(key, "SpiralRadius", 0.0);
            t->spiral_d = dbl(key, "SpiralDeltaPerCoord", 0.0);
            t->laser = yes(rget(key, "Laser"));
            const std::vector<std::string> lc = split(rget(key, "LaserColor"));
            if (lc.size() >= 3)
                t->laser_color = SDL_Color{static_cast<Uint8>(std::atoi(lc[0].c_str())),
                                           static_cast<Uint8>(std::atoi(lc[1].c_str())),
                                           static_cast<Uint8>(std::atoi(lc[2].c_str())), 255};
        }
        const FxPsType* raw = t.get();
        ps_types_[key] = std::move(t);
        return raw;
    }
    const FxPtType* pt_type(const std::string& name) {
        const std::string key = pres_upper(name);
        if (key.empty()) return nullptr;
        auto it = pt_types_.find(key);
        if (it != pt_types_.end()) return it->second.get();
        std::unique_ptr<FxPtType> t;
        if (has_sec(key)) {
            t = std::make_unique<FxPtType>();
            t->name = key;
            const std::string img = rget(key, "Image");
            if (!img.empty()) t->img = fx_->type(img);
            t->behaves = behaves(rget(key, "BehavesLike"));
            t->max_ec = num(key, "MaxEC", 100);
            t->max_dc = num(key, "MaxDC", 0);
            t->velocity = dbl(key, "Velocity", 0.0);
            t->deacc = dbl(key, "Deacc", 0.0);
            t->translucency = num(key, "Translucency", 0);
            t->start_state = num(key, "StartStateAI", 0);
            t->end_state = num(key, "EndStateAI", 0);
            t->state_adv = num(key, "StateAIAdvance", 1);
            t->delete_on_limit = yes(rget(key, "DeleteOnStateLimit"));
            t->start_frame = num(key, "StartFrame", 0);
            t->loop_frames = num(key, "NumLoopFrames", 0);
            t->xvel = num(key, "XVelocity", 0);
            t->yvel = num(key, "YVelocity", 0);
            t->min_zvel = num(key, "MinZVelocity", 0);
            t->zvel_range = num(key, "ZVelocityRange", 0);
            t->color_speed = dbl(key, "ColorSpeed", 0.1);
            t->next = rget(key, "NextParticle");
            const std::vector<std::string> next_offset = split(rget(key, "NextParticleOffset"));
            if (next_offset.size() >= 3) {
                t->next_dx = std::atof(next_offset[0].c_str());
                t->next_dy = std::atof(next_offset[1].c_str());
                t->next_dz = std::atof(next_offset[2].c_str());
            }
            t->wind = num(key, "WindEffect", 0);
            // ColorList=(r,g,b),(r,g,b),...
            const std::string cl = rget(key, "ColorList");
            int vals[3] = {0, 0, 0}, nv = 0, cur = 0;
            bool in = false, have = false;
            for (char ch : cl) {
                if (ch == '(') { in = true; nv = 0; cur = 0; have = false; continue; }
                if (!in) continue;
                if (ch >= '0' && ch <= '9') { cur = cur * 10 + (ch - '0'); have = true; continue; }
                if (ch == ',' || ch == ')') {
                    if (nv < 3) vals[nv++] = have ? cur : 0;
                    cur = 0; have = false;
                    if (ch == ')') {
                        in = false;
                        t->colors.push_back(SDL_Color{static_cast<Uint8>(vals[0]), static_cast<Uint8>(vals[1]),
                                                      static_cast<Uint8>(vals[2]), 255});
                    }
                }
            }
        }
        const FxPtType* raw = t.get();
        pt_types_[key] = std::move(t);
        return raw;
    }
    const VoxelDef* voxel_def(const std::string& name) {
        const std::string key = pres_upper(name);
        auto it = vox_defs_.find(key);
        if (it != vox_defs_.end()) return it->second.get();
        std::unique_ptr<VoxelDef> d;
        {
            const std::string base = pres_lower(key);
            std::vector<uint8_t> vr, hr;
            if (asset_read(base + ".vxl", vr) && asset_read(base + ".hva", hr)) {
                try {
                    d = std::make_unique<VoxelDef>();
                    d->name = key;
                    d->vxl = ra2assets::load_vxl(vr.data(), vr.size());
                    d->hva = ra2assets::load_hva(hr.data(), hr.size());
                    d->elasticity = dbl(key, "Elasticity", 0.0);
                    d->min_ang = dbl(key, "MinAngularVelocity", 5.0);
                    d->max_ang = dbl(key, "MaxAngularVelocity", 9.0);
                    d->min_z = dbl(key, "MinZVel", 24.0);
                    d->max_z = dbl(key, "MaxZVel", 28.0);
                    d->max_xy = dbl(key, "MaxXYVel", 15.0);
                    d->duration = num(key, "Duration", 0);
                    d->expire = rget(key, "ExpireAnim");
                    d->yaws.resize(kVoxYaws);
                    d->made.assign(kVoxYaws, false);
                } catch (...) {
                    d.reset();
                }
            }
        }
        const VoxelDef* raw = d.get();
        vox_defs_[key] = std::move(d);
        return raw;
    }
    const ra2engine::VoxelSprite* voxel_sprite(const VoxelDef& d, int yaw_ix) {
        VoxelDef& m = const_cast<VoxelDef&>(d);
        if (yaw_ix < 0 || yaw_ix >= kVoxYaws || !unit_pal_) return nullptr;
        if (!m.made[static_cast<std::size_t>(yaw_ix)]) {
            m.made[static_cast<std::size_t>(yaw_ix)] = true;
            try {
                m.yaws[static_cast<std::size_t>(yaw_ix)] = ra2engine::rasterize_model(
                    m.vxl, m.hva, ra2engine::index_to_yaw(yaw_ix, kVoxYaws), vpl_, *unit_pal_);
            } catch (...) {}
        }
        return &m.yaws[static_cast<std::size_t>(yaw_ix)];
    }
    const ProjArt& proj_art(const std::string& weapon) {
        auto it = proj_art_.find(weapon);
        if (it != proj_art_.end()) return it->second;
        ProjArt a;
        const std::string pj = rget(weapon, "Projectile");
        std::string img = pj.empty() ? std::string() : rget(pj, "Image");
        if (img.empty()) img = pj;
        const std::string key = pres_upper(img);
        if (!key.empty() && pres_lower(key) != "none") {
            a.trailer = aget(key, "Trailer");
            a.spawn_delay = aint(key, "SpawnDelay", 3);
            a.line = yes(aget(key, "UseLineTrail"));
            const std::vector<std::string> c = split(aget(key, "LineTrailColor"));
            if (c.size() >= 3)
                a.line_color = SDL_Color{static_cast<Uint8>(std::atoi(c[0].c_str())),
                                         static_cast<Uint8>(std::atoi(c[1].c_str())),
                                         static_cast<Uint8>(std::atoi(c[2].c_str())), 255};
            a.line_decrement = std::max(1, aint(key, "LineTrailColorDecrement", 16));
        }
        return proj_art_.emplace(weapon, a).first->second;
    }
    std::string weapon_of(const std::string& id, int slot) {
        std::string w = rget(id, slot == 1 ? "Secondary" : "Primary");
        if (w.empty()) w = rget(id, slot == 1 ? "Weapon2" : "Weapon1");
        return w;
    }
    std::string dmisl_image() {   // the Dreadnought missile flies the same RocketLocomotion
        if (dmisl_.empty()) {
            std::string id = pres_upper(pres_clean(rules_ ? rules_->get("General", "DMislType") : std::string()));
            if (id.empty()) id = "DMISL";
            std::string img = pres_upper(rget(id, "Image"));
            dmisl_ = img.empty() ? id : img;
        }
        return dmisl_;
    }
    std::string v3_image() {
        if (v3_.empty()) {
            std::string id = pres_upper(pres_clean(rules_ ? rules_->get("General", "V3RocketType") : std::string()));
            if (id.empty()) id = "V3ROCKET";
            std::string img = pres_upper(rget(id, "Image"));
            v3_ = img.empty() ? id : img;
        }
        return v3_;
    }
    static int behaves(const std::string& v) {
        const std::string s = pres_lower(v);
        if (s == "gas") return 1;
        if (s == "fire") return 2;
        if (s == "spark") return 3;
        if (s == "railgun") return 4;
        return 0;
    }
    static bool yes(const std::string& v) {
        const std::string s = pres_lower(v);
        return !s.empty() && (s[0] == 'y' || s[0] == 't' || s[0] == '1');
    }
    // The original is RA2: a section RA2 rules.ini has is read from it (YR's
    // rulesmd comments out e.g. GACNST DamageParticleSystems and drops
    // LargeRedSmoke); YR-only sections fall back to rulesmd.
    bool has_sec(const std::string& s) const { return ra2_rules_.has(s) || (rules_ && rules_->has(s)); }
    std::string rget(const std::string& s, const std::string& k) const {
        if (ra2_rules_.has(s)) return pres_clean(ra2_rules_.get(s, k));
        return rules_ ? pres_clean(rules_->get(s, k)) : std::string();
    }
    std::string aget(const std::string& s, const std::string& k) const {
        return fx_ ? fx_->art_value(s, k) : std::string();
    }
    int num(const std::string& s, const std::string& k, int d) const {
        const std::string v = rget(s, k);
        return v.empty() ? d : std::atoi(v.c_str());
    }
    double dbl(const std::string& s, const std::string& k, double d) const {
        const std::string v = rget(s, k);
        return v.empty() ? d : std::atof(v.c_str());
    }
    int aint(const std::string& s, const std::string& k, int d) const {
        const std::string v = aget(s, k);
        return v.empty() ? d : std::atoi(v.c_str());
    }
    double adbl(const std::string& s, const std::string& k, double d) const {
        const std::string v = aget(s, k);
        return v.empty() ? d : std::atof(v.c_str());
    }
    static std::vector<std::string> split(const std::string& v) {
        std::vector<std::string> out;
        std::stringstream ss(v);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok = pres_clean(tok);
            if (!tok.empty()) out.push_back(tok);
        }
        return out;
    }
    std::uint32_t rnd() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return rng_ >> 8;
    }

    PresFx* fx_ = nullptr;
    const Ini* rules_ = nullptr;
    Ini ra2_rules_ = Ini::parse("");
    const ra2assets::MixFile* ra2_ = nullptr;
    ra2assets::Vpl vpl_;
    bool has_vpl_ = false;
    const ra2assets::Palette* unit_pal_ = nullptr;
    const ra2engine::World* world_ = nullptr;
    const BuildingAssets* ba_ = nullptr;
    long frame_ = 0, last_frame_ = -1;
    std::uint32_t rng_ = 0x3FA7C0DEu;
    double gravity_ = 6.0, yellow_ = 0.5, red_ = 0.25;
    std::string wake_, twinkle_, v3_, dmisl_;
    int twinkle_chance_ = 30;
    std::vector<std::string> metallic_, splash_;
    int next_id_ = 0;
    FxPsType unmanaged_particle_owner_{};
    std::vector<Sys> systems_;
    std::vector<Bouncer> bouncers_;
    std::vector<VoxelDebris> voxels_;
    std::vector<Ground> grounds_;
    std::vector<Twinkle> twinkles_;
    bool twinkle_seeded_ = false;
    const FxAnimType* twinkle_t_ = nullptr;
    int tw_delay0_ = 120, tw_delay1_ = 300;
    std::map<long long, LineTrail> lines_;
    std::map<std::string, ProjArt> proj_art_;
    std::map<std::string, std::unique_ptr<FxPsType>> ps_types_;
    std::map<std::string, std::unique_ptr<FxPtType>> pt_types_;
    std::map<std::string, std::unique_ptr<VoxelDef>> vox_defs_;
    std::map<std::string, std::string> unit_ids_;
    std::vector<int> ucond_, bcond_, usmoke_, bsmoke_;
    bool seed_damage_ = false;   // W6-06-04: the next observe_damage follows a load
    long wakes_ = 0, v3_puffs_ = 0, splashes_ = 0;
    long long instant_key_ = 0;
};

PresEffects* g_fx3 = nullptr;   // the live game's W3 effects (GameScene); null in gates

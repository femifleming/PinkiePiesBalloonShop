#pragma once
#include "Ambi.h"
#include <array>

namespace fp
{
//==============================================================================
// How the grains fly round inside the sphere. Every grain has its own place and its own way of
// moving; all of it stays inside the ball (|p| <= 1), not just on its surface.
//
//   still    each grain stays where it was put (drag it in the editor)
//   orbit    circles round you, each on its own tilted ring, speed and phase (an LFO in 3-D)
//   lorenz   the Lorenz attractor (sigma 10, rho 28, beta 8/3), one butterfly per grain, each
//            started a hair apart so they come apart over time
//   flock    boids (Reynolds 1987): keep apart, line up, stay together, and stay in the ball
//   random   sample and hold with glides: a new random place every so often, eased into
//
// 'spread' is how far out they go (0 the middle, 1 the sphere's skin), 'speed' how fast.
class Flight
{
public:
    static constexpr int kMax = 16;
    enum Mode { still = 0, orbit, lorenz, flock, random, numModes };
    using Vec3 = ambi::Vec3;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        rng.seed (0xB0B5);
        for (int v = 0; v < kMax; ++v)
        {
            auto& s = st[(size_t) v];
            s.pos = s.home = defaultHome (v, 0.7f);
            s.vel = { rng.bi() * 0.1f, rng.bi() * 0.1f, rng.bi() * 0.1f };
            s.axis = Vec3 { rng.bi() * 0.6f, rng.bi() * 0.6f, 1.0f }.normalised();
            s.phase = 2.3999632f * (float) v;   // the golden angle: evenly round however many there are
            s.rate = 1.0f + 0.35f * rng.bi();
            s.lz = { 6.0f * rng.bi(), 8.0f * rng.bi(), 22.0f + 6.0f * rng.bi() };   // each butterfly starts somewhere else on the attractor
            s.target = s.pos;
            s.holdLeft = rng.range (0.2f, 2.0f);
        }
    }

    // where grain v starts: the first straight ahead, the rest spread round you
    static Vec3 defaultHome (int v, float r)
    {
        if (v == 0) return { r, 0.0f, 0.0f };
        const double golden = kPi * (3.0 - std::sqrt (5.0));
        const double z = 0.6 * (1.0 - 2.0 * (v - 0.5) / (kMax - 1)), rr = std::sqrt (1.0 - z * z), a = golden * v;
        return Vec3 { (float) (rr * std::cos (a)), (float) (rr * std::sin (a)), (float) z } * r;
    }

    // seconds of flight for the grains 0..count-1
    void step (double seconds, int mode, float speed, float spread, int count) noexcept
    {
        const float sp = std::clamp (spread, 0.0f, 1.0f);
        const float sf = 0.05f * std::pow (60.0f, std::clamp (speed, 0.0f, 1.0f));   // 0.05 .. 3 (a rough "times per second")
        count = std::clamp (count, 1, kMax);
        double left = seconds;
        while (left > 1.0e-9)
        {
            const float dt = (float) std::min (left, 1.0 / 200.0);
            left -= dt;
            for (int v = 0; v < count; ++v) one (v, dt, mode, sf, sp, count);
            t += dt;
        }
    }

    Vec3 position (int v) const noexcept { return st[(size_t) std::clamp (v, 0, kMax - 1)].pos; }

    // the editor drags a grain: it goes there (and, standing still, stays there)
    void place (int v, Vec3 p) noexcept
    {
        auto& s = st[(size_t) std::clamp (v, 0, kMax - 1)];
        p = clampBall (p, 1.0f);
        s.pos = s.home = s.target = p;
        s.vel = {};
    }
    Vec3 home (int v) const noexcept { return st[(size_t) std::clamp (v, 0, kMax - 1)].home; }
    void setHome (int v, Vec3 p) noexcept { auto& s = st[(size_t) std::clamp (v, 0, kMax - 1)]; s.home = clampBall (p, 1.0f); }

    static Vec3 clampBall (Vec3 p, float r) noexcept
    {
        const float l = p.length();
        return l > r ? p * (r / l) : p;
    }

private:
    struct State
    {
        Vec3 pos, vel, home, axis, lz, target;
        float phase = 0.0f, rate = 1.0f, holdLeft = 1.0f;
    };

    void one (int v, float dt, int mode, float sf, float sp, int count) noexcept
    {
        auto& s = st[(size_t) v];
        auto glide = [&] (Vec3 to, float k)
        {
            // a critically damped spring towards 'to': no corners, no overshoot
            const float w = k;
            const Vec3 acc = (to - s.pos) * (w * w) - s.vel * (2.0f * w);
            s.vel += acc * dt;
            s.pos += s.vel * dt;
        };
        switch (mode)
        {
            case still:
                glide (s.home, 6.0f);
                break;
            case orbit:
            {
                s.phase += (float) kTwoPi * sf * 0.25f * s.rate * dt;
                if (s.phase > (float) kTwoPi) s.phase -= (float) kTwoPi;
                // a ring round 'axis': two directions at right angles to it
                const Vec3 a = s.axis;
                Vec3 u = std::abs (a.x) < 0.9f ? Vec3 { 1, 0, 0 } : Vec3 { 0, 1, 0 };
                u = (u - a * u.dot (a)).normalised();
                const Vec3 w { a.y * u.z - a.z * u.y, a.z * u.x - a.x * u.z, a.x * u.y - a.y * u.x };
                const float r = sp * (0.75f + 0.25f * std::sin (s.phase * 0.37f + (float) v));
                glide ((u * std::cos (s.phase) + w * std::sin (s.phase)) * r, 12.0f);
                break;
            }
            case lorenz:
            {
                // integrate the attractor at a pace set by speed, then fit its butterfly in the ball
                const float h = std::min (0.02f, dt * sf * 0.6f);
                const int sub = std::max (1, (int) std::ceil (dt * sf * 0.6f / std::max (1.0e-6f, h)));
                for (int k = 0; k < sub; ++k)
                {
                    const Vec3 q = s.lz;
                    const Vec3 d { 10.0f * (q.y - q.x), q.x * (28.0f - q.z) - q.y, q.x * q.y - (8.0f / 3.0f) * q.z };
                    s.lz += d * h;
                }
                const Vec3 to { s.lz.x / 20.0f, s.lz.y / 27.0f, (s.lz.z - 24.0f) / 22.0f };
                glide (clampBall (to, 1.0f) * sp, 14.0f);
                break;
            }
            case flock:
            {
                // the three rules over the flock, plus a wall at the spread and a little wandering
                Vec3 centre, heading, apart;
                int n = 0;
                for (int o = 0; o < count; ++o)
                {
                    if (o == v) continue;
                    const auto& q = st[(size_t) o];
                    centre += q.pos; heading += q.vel; ++n;
                    const Vec3 d = s.pos - q.pos;
                    const float dist = d.length();
                    if (dist < 0.42f) apart += d * ((0.42f - dist) / std::max (0.02f, dist));
                }
                Vec3 acc;
                if (n > 0)
                {
                    centre *= 1.0f / (float) n; heading *= 1.0f / (float) n;
                    acc += (centre - s.pos) * 0.35f + (heading - s.vel) * 0.7f + apart * 9.0f;
                }
                acc += Vec3 { rng.bi(), rng.bi(), rng.bi() } * 2.4f;
                const float r = s.pos.length(), wall = std::max (0.15f, sp);
                if (r > wall) acc -= s.pos * (8.0f * (r - wall) / std::max (0.01f, r));
                acc += (s.home - s.pos) * 0.05f;
                const float maxV = 0.15f + 1.1f * sf;
                s.vel += acc * (dt * (0.3f + sf));
                const float spd = s.vel.length();
                if (spd > maxV) s.vel *= maxV / spd;
                s.pos += s.vel * dt;
                break;
            }
            case random:
            default:
            {
                s.holdLeft -= dt * sf;
                if (s.holdLeft <= 0.0f)
                {
                    s.holdLeft = rng.range (0.4f, 2.5f);
                    Vec3 p;
                    do p = { rng.bi(), rng.bi(), rng.bi() }; while (p.length() > 1.0f);
                    s.target = p * sp;
                }
                glide (s.target, 1.5f + 3.0f * sf);
                break;
            }
        }
        s.pos = clampBall (s.pos, 1.0f);
        if (! std::isfinite (s.pos.x) || ! std::isfinite (s.pos.y) || ! std::isfinite (s.pos.z)) { s.pos = s.home; s.vel = {}; }
    }

    double sr = 48000.0, t = 0.0;
    Rng rng;
    std::array<State, kMax> st {};
};

//==============================================================================
// The weather, inside the sphere: while a kind of weather is on, a few patches of it (clouds,
// rain clouds, snow flurries, heat haze) drift slowly through the ball. A grain inside a patch is
// "rained on": its settings wander off at random (all weather does this), the snow freezes its
// spectrum while it's in there, and the heat smears it.
class SkyPatches
{
public:
    enum Kind { cloud = 0, rain, snow, heat, numKinds };
    static constexpr int perKind = 3, kNum = numKinds * perKind;
    using Vec3 = ambi::Vec3;

    struct Patch { Vec3 pos, vel; float radius = 0.45f, amount = 0.0f; };

    void prepare (uint64_t seed)
    {
        rng.seed (seed);
        for (int i = 0; i < kNum; ++i)
        {
            auto& p = patches[(size_t) i];
            do p.pos = { rng.bi(), rng.bi(), rng.bi() }; while (p.pos.length() > 0.9f);
            p.vel = Vec3 { rng.bi(), rng.bi(), rng.bi() * 0.4f } * 0.08f;
            p.radius = 0.38f + 0.14f * rng.uni();
            p.amount = 0.0f;
        }
    }

    // on: which kinds of weather are on. The patches ease in and out over a couple of seconds.
    void step (float dt, const bool on[numKinds]) noexcept
    {
        for (int i = 0; i < kNum; ++i)
        {
            auto& p = patches[(size_t) i];
            const int kind = i / perKind;
            const float target = on[kind] ? (i % perKind == 2 && kind != cloud ? 0.0f : 1.0f) : 0.0f;   // clouds come three at a time, the rest two
            p.amount += (target - p.amount) * std::min (1.0f, dt * 0.6f);
            if (target == 0.0f && p.amount < 1.0e-3f) p.amount = 0.0f;
            // a slow drift that turns now and then, and bounces softly off the inside of the ball
            p.vel += Vec3 { rng.bi(), rng.bi(), rng.bi() * 0.5f } * (0.05f * dt);
            const float sp = p.vel.length();
            if (sp > 0.12f) p.vel *= 0.12f / sp;
            p.pos += p.vel * dt;
            const float r = p.pos.length();
            if (r > 0.85f) { p.vel -= p.pos * (2.0f * p.vel.dot (p.pos) / std::max (1.0e-6f, r * r)); p.pos *= 0.85f / r; }
        }
    }

    // how deep inside patches of this kind a point is (0..1)
    float exposure (Vec3 at, int kind) const noexcept
    {
        float e = 0.0f;
        for (int k = 0; k < perKind; ++k)
        {
            const auto& p = patches[(size_t) (kind * perKind + k)];
            if (p.amount <= 0.0f) continue;
            const float d = (at - p.pos).length() / p.radius;
            if (d < 1.0f) e = std::max (e, p.amount * smoothstep (1.0f - d));
        }
        return e;
    }

    std::array<Patch, kNum> patches {};
private:
    Rng rng;
};
} // namespace fp

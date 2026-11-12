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
//   spiral   a helix round you, climbing to the top of the sphere and winding back down
//   lissajous a 3-D Lissajous figure (different whole-number rates on each axis), its own per grain
//   rossler  the Rossler attractor (a = b = 0.2, c = 5.7): long lazy loops with a sudden fold upwards
//   breathe  each grain on its own bearing, swelling out to the edge and back in to you
//   fall     like rain or snow: drifting down from overhead, past you, and starting again at the top
//   pingpong jumping between opposite sides of you, gliding quickly across
//
// After Bernhard Leitner, who since 1968 has built rooms out of sound moving along lines of
// loudspeakers (the Soundcube, the Sound Tube, the Sound Chair, Le Cylindre Sonore):
//   line     a line of sound through the room and through you: each grain runs along its own
//            straight line from wall to wall and back, at an even pace
//   pendulum a swinging space: each grain hangs from a point overhead and swings through under you,
//            slow at the ends and quick in the middle, as a pendulum is
//   column   a vertical space: each grain climbs and sinks along its own column standing round you,
//            winding a little round it as it goes
//   soundcube his Soundcube (1969): a grid of nine loudspeakers on each wall of a cube. The grains do
//            not fly; they step from one loudspeaker to the next, drawing a ring round the cube, then
//            another ring higher or lower, the way his scores draw lines and planes in a room
//
// After Iannis Xenakis:
//   sonotron Terretektorh's "accelerator of sound particles" and the turning drums that end
//            Persephassa: everyone on one ring round you, turning faster and faster along a
//            logarithmic spiral as the ring widens, then starting again the other way
//   paraboloid the Philips Pavilion and the string glissandi of Metastaseis: every grain glides
//            along its own straight line, and the lines together rule a hyperbolic paraboloid (a saddle)
//   gas      Pithoprakta: the grains are the molecules of a gas. Each flies straight, its speed drawn
//            from the Maxwell-Boltzmann distribution, and takes a new one when it hits the glass
//   nomos    Nomos Alpha: the grains sit on the eight corners of a cube, and the cube turns from one
//            of its 24 rotations to the next, each the product of the two before it
//   screens  Analogique A and B: the sphere is cut into eight cells (screens), and every moment each
//            grain moves on by a Markov chain, using his two transition tables (0.2 / 0.8 and
//            0.85 / 0.4); which table an axis uses depends on where the grain is on the other two
//
// The wind pushes them all the same way while it blows, and a twister takes them up into its spiral.
//
// 'spread' is how far out they go (0 the middle, 1 the sphere's skin), 'speed' how fast.
class Flight
{
public:
    static constexpr int kMax = 16;
    enum Mode { still = 0, orbit, lorenz, flock, random, spiral, lissajous, rossler, breathe, fall, pingpong,
                line, pendulum, column, soundcube, sonotron, paraboloid, gas, nomos, screens, numModes };
    static const char* const* modeNames() noexcept
    {
        static const char* const n[numModes] = { "still", "orbit", "lorenz", "flock", "random", "spiral", "lissajous", "rossler", "breathe", "fall", "ping-pong",
                                                 "line", "pendulum", "column", "soundcube", "sonotron", "paraboloid", "gas", "nomos", "screens" };
        return n;
    }
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
            s.rs = { 4.0f * rng.bi(), 4.0f * rng.bi(), 0.5f + rng.uni() };       // and each Rossler loop
            s.dir = Vec3 { rng.bi(), rng.bi(), rng.bi() * 0.6f }.normalised();
            s.target = s.pos;
            s.holdLeft = rng.range (0.2f, 2.0f);
            s.shown = s.pos;
            s.cell = v & 7;
            s.level = v % 3;
            s.step = (v * 5) % 12;
        }
        blow = blowTarget = {};
        twAmt = twAngle = twProgress = 0.0f;
        spinTau = spinAng = 0.0f; spinDir = 1.0f;
        nomA = rotZ(); nomB = rotDiag(); nomLeft = 0.0f;
    }

    // the wind: which way it pushes (a vector, its length how hard: 0..1)
    void setWind (Vec3 push) noexcept { blowTarget = push; }
    // a twister: how far it has the grains (0..1), how far round it has turned, and how far through it is
    void setTwister (float amount, float angle, float progress) noexcept { twAmt = amount; twAngle = angle; twProgress = progress; }

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
            shared (dt, mode, sf);
            for (int v = 0; v < count; ++v) one (v, dt, mode, sf, sp, count);
            t += dt;
            blow += (blowTarget - blow) * std::min (1.0f, dt / 0.6f);
        }
        // where each one is heard and shown: its own flight, pushed by the wind, taken up by a twister
        for (int v = 0; v < kMax; ++v)
        {
            auto& s = st[(size_t) v];
            Vec3 p = s.pos + blow * (0.55f * (1.0f - 0.4f * s.pos.length()));
            if (twAmt > 0.0f)
            {
                const float up = twProgress * 1.5f + 0.11f * (float) v;
                const float z = -0.7f + 1.5f * (up - std::floor (up));
                const float r = 0.15f + 0.5f * (z + 0.7f) / 1.5f;                // a funnel, wider at the top
                const float a = 0.35f * twAngle * (0.8f + 0.2f * s.rate) + 2.3999632f * (float) v;
                const Vec3 q { r * std::cos (a), r * std::sin (a), z };
                p = p + (q - p) * smoothstep (twAmt);
            }
            s.shown = clampBall (p, 1.0f);
        }
    }

    Vec3 position (int v) const noexcept { return st[(size_t) std::clamp (v, 0, kMax - 1)].shown; }

    // the editor drags a grain: it goes there (and, standing still, stays there)
    void place (int v, Vec3 p) noexcept
    {
        auto& s = st[(size_t) std::clamp (v, 0, kMax - 1)];
        p = clampBall (p, 1.0f);
        s.pos = s.home = s.target = s.shown = p;
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
        Vec3 pos, vel, home, axis, lz, target, rs, dir, shown;
        float phase = 0.0f, rate = 1.0f, holdLeft = 1.0f;
        int cell = 0, level = 1, step = 0;   // screens: which of the eight cells; soundcube: which ring and which loudspeaker on it
    };

    // a rotation of the cube: where each axis goes (a signed permutation matrix)
    struct Rot
    {
        int m[3][3] {};
        Vec3 apply (Vec3 p) const noexcept
        {
            return { (float) m[0][0] * p.x + (float) m[0][1] * p.y + (float) m[0][2] * p.z,
                     (float) m[1][0] * p.x + (float) m[1][1] * p.y + (float) m[1][2] * p.z,
                     (float) m[2][0] * p.x + (float) m[2][1] * p.y + (float) m[2][2] * p.z };
        }
        Rot times (const Rot& o) const noexcept
        {
            Rot r;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * o.m[k][j];
            return r;
        }
    };
    static Rot rotZ() noexcept    { Rot r; r.m[0][1] = -1; r.m[1][0] = 1; r.m[2][2] = 1; return r; }   // a quarter turn round the vertical
    static Rot rotDiag() noexcept { Rot r; r.m[1][0] = 1; r.m[2][1] = 1; r.m[0][2] = 1; return r; }    // a third of a turn round a diagonal

    float gauss() noexcept   // (Box-Muller)
    {
        const float u = std::max (1.0e-6f, rng.uni()), v = rng.uni();
        return std::sqrt (-2.0f * std::log (u)) * std::cos ((float) kTwoPi * v);
    }
    static float tri (float phase) noexcept   // -1 .. 1 .. -1, at an even pace
    {
        const float u = phase - std::floor (phase);
        return u < 0.5f ? 4.0f * u - 1.0f : 3.0f - 4.0f * u;
    }

    // what the grains of a mode share: the sonotron's one accelerating turn, the cube's next rotation
    void shared (float dt, int mode, float sf) noexcept
    {
        if (mode == sonotron)
        {
            spinTau += dt * sf * 0.09f;
            if (spinTau >= 1.0f) { spinTau = 0.0f; spinDir = -spinDir; }
            spinAng += spinDir * (float) kTwoPi * 0.06f * std::exp (3.4f * spinTau) * (0.4f + sf) * dt;   // a logarithmic spiral: its turning grows as e^(k t)
        }
        else if (mode == nomos)
        {
            nomLeft -= dt * sf;
            if (nomLeft <= 0.0f)
            {
                nomLeft = 1.6f;
                const Rot next = nomB.times (nomA);   // each rotation is the product of the two before it
                nomA = nomB; nomB = next;
            }
        }
    }

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
            case spiral:
            {
                s.phase += (float) kTwoPi * sf * 0.3f * s.rate * dt;
                const float climb = std::sin (s.phase * 0.12f + (float) v);          // up and down the helix
                const float r = sp * std::sqrt (std::max (0.05f, 1.0f - 0.8f * climb * climb));
                const float a = s.phase + (float) v * 0.9f;
                glide ({ r * std::cos (a), r * std::sin (a), climb * sp * 0.85f }, 10.0f);
                break;
            }
            case lissajous:
            {
                s.phase += sf * 0.5f * dt;
                const float fx = 1.0f + (float) (v % 3), fy = 2.0f + (float) ((v + 1) % 3), fz = 3.0f + (float) (v % 2);
                const float t0 = s.phase;
                glide (Vec3 { std::sin (fx * t0 + (float) v), std::sin (fy * t0 + 0.7f * (float) v), 0.7f * std::sin (fz * t0 + 1.3f * (float) v) } * (sp * 0.95f), 10.0f);
                break;
            }
            case rossler:
            {
                const float h = std::min (0.02f, dt * sf * 2.0f);
                const int sub = std::max (1, (int) std::ceil (dt * sf * 2.0f / std::max (1.0e-6f, h)));
                for (int k = 0; k < sub; ++k)
                {
                    const Vec3 q = s.rs;
                    const Vec3 d { -q.y - q.z, q.x + 0.2f * q.y, 0.2f + q.z * (q.x - 5.7f) };
                    s.rs += d * h;
                }
                if (! std::isfinite (s.rs.x) || std::abs (s.rs.x) > 100.0f) s.rs = { 1.0f, 1.0f, 0.5f };
                const Vec3 to { s.rs.x / 11.0f, s.rs.y / 11.0f, s.rs.z / 12.0f - 0.4f };
                glide (clampBall (to, 1.0f) * sp, 14.0f);
                break;
            }
            case breathe:
            {
                s.phase += (float) kTwoPi * sf * 0.15f * s.rate * dt;
                const float r = sp * (0.15f + 0.85f * (0.5f + 0.5f * std::sin (s.phase + (float) v * 1.1f)));
                glide (s.dir * r, 6.0f);
                break;
            }
            case fall:
            {
                // down through the sphere at its own speed and place, then back to the top
                s.target.z -= dt * sf * 0.35f * (0.7f + 0.3f * s.rate);
                if (s.target.z < -sp)
                {
                    const float a = rng.uni() * (float) kTwoPi, r = sp * std::sqrt (rng.uni()) * 0.8f;
                    s.target = { r * std::cos (a), r * std::sin (a), sp };
                    s.pos = s.target; s.vel = {};
                }
                s.target.x += dt * 0.05f * std::sin (s.phase += dt * 0.7f);   // a little sway
                glide (s.target, 12.0f);
                break;
            }
            case pingpong:
            {
                s.holdLeft -= dt * sf;
                if (s.holdLeft <= 0.0f)
                {
                    s.holdLeft = rng.range (0.5f, 1.5f);
                    s.dir = s.dir * -1.0f + Vec3 { rng.bi(), rng.bi(), rng.bi() * 0.5f } * 0.25f;
                    s.dir = s.dir.normalised();
                }
                glide (s.dir * sp, 9.0f + 6.0f * sf);
                break;
            }
            case line:
            {
                // from one wall to the other and back, through the room and close by you
                s.phase += sf * 0.12f * s.rate * dt;
                const Vec3 a = s.dir * sp;
                const Vec3 b = (s.dir * -1.0f + s.axis * 0.35f).normalised() * sp;
                const float u = 0.5f + 0.5f * tri (s.phase + 0.137f * (float) v);
                glide (a + (b - a) * u, 16.0f);
                break;
            }
            case pendulum:
            {
                // hung from overhead, swinging through under you in its own plane
                s.phase += (float) kTwoPi * sf * 0.11f * s.rate * dt;
                const float az = 2.3999632f * (float) v;
                const float th = 1.15f * std::sin (s.phase + 0.5f * (float) v);
                const float L = 1.5f * sp;
                glide ({ L * std::sin (th) * std::cos (az), L * std::sin (th) * std::sin (az), sp * 0.95f - L * std::cos (th) }, 16.0f);
                break;
            }
            case column:
            {
                // up and down its own column, winding round it a little
                s.phase += sf * 0.1f * s.rate * dt;
                const float z = 0.9f * sp * tri (s.phase + 0.21f * (float) v);
                const float az = 2.3999632f * (float) v + 0.9f * std::sin ((float) kTwoPi * s.phase * 0.5f);
                const float r = sp * (0.3f + 0.25f * (float) (v % 3) * 0.5f);
                glide ({ r * std::cos (az), r * std::sin (az), z }, 14.0f);
                break;
            }
            case soundcube:
            {
                // one loudspeaker to the next round a ring of twelve (three on each of four walls), then a new ring
                s.holdLeft -= dt * sf * 2.2f;
                if (s.holdLeft <= 0.0f)
                {
                    s.holdLeft = 0.5f;
                    if (++s.step >= 12) { s.step = 0; s.level = rng.integer (3); }
                }
                const int face = s.step / 3;
                const float tq = 0.6f * (float) (s.step % 3 - 1), b = 0.6f * (float) (s.level - 1);
                Vec3 q = face == 0 ? Vec3 { 1.0f, tq, b } : face == 1 ? Vec3 { -tq, 1.0f, b } : face == 2 ? Vec3 { -1.0f, -tq, b } : Vec3 { tq, -1.0f, b };
                if (v % 3 == 1) q = { q.z, q.y, q.x };        // (every third grain draws its rings round another axis)
                else if (v % 3 == 2) q = { q.x, q.z, q.y };
                glide (q * (sp / 1.32f), 22.0f);              // a quick step: the sound hands over, it does not travel
                break;
            }
            case sonotron:
            {
                const float a = spinAng + (float) kTwoPi * (float) v / (float) count;
                const float r = sp * (0.4f + 0.6f * spinTau);
                glide ({ r * std::cos (a), r * std::sin (a), 0.12f * sp * std::sin (a * 0.5f + (float) v) }, 14.0f);
                break;
            }
            case paraboloid:
            {
                // z = x y: hold x and the point runs along one straight line of it; hold y and it runs along one of the other family
                s.phase += sf * 0.13f * s.rate * dt;
                const float fixed = count > 1 ? -0.9f + 1.8f * (float) (v / 2) / (float) std::max (1, (count - 1) / 2) : 0.0f;
                const float run = tri (s.phase + 0.19f * (float) v);
                const Vec3 q = (v & 1) ? Vec3 { run, fixed, run * fixed } : Vec3 { fixed, run, fixed * run };
                glide (q * (sp * 0.68f), 16.0f);
                break;
            }
            case gas:
            {
                // straight on until the glass, then away again at a new speed out of the distribution
                const float sigma = 0.22f * (0.3f + sf);
                if (const float speed = s.vel.length(); speed < 0.02f || speed > 6.0f * sigma)   // (coming from another flight: a first speed)
                {
                    const float gx = gauss(), gy = gauss(), gz = gauss();
                    s.vel = Vec3 { gx, gy, gz } * sigma;
                }
                s.pos += s.vel * dt;
                const float r = s.pos.length();
                if (r > sp && s.pos.dot (s.vel) > 0.0f)
                {
                    const Vec3 nrm = s.pos * (1.0f / r);
                    Vec3 d = s.vel - nrm * (2.0f * s.vel.dot (nrm));
                    const float gx = gauss(), gy = gauss(), gz = gauss();
                    const float fresh = std::max (0.03f, sigma * std::sqrt (gx * gx + gy * gy + gz * gz));
                    s.vel = d.normalised() * fresh;
                    s.pos = nrm * sp;
                }
                break;
            }
            case nomos:
            {
                // its corner of the cube, wherever the cube's latest rotation has put it (the second eight on a smaller cube inside)
                const int c = v & 7;
                const Vec3 corner { (c & 1) ? 1.0f : -1.0f, (c & 2) ? 1.0f : -1.0f, (c & 4) ? 1.0f : -1.0f };
                glide (nomB.apply (corner) * (sp * (v < 8 ? 0.5774f : 0.3f)), 2.0f + 3.0f * sf);
                break;
            }
            case screens:
            {
                s.holdLeft -= dt * sf;
                if (s.holdLeft <= 0.0f)
                {
                    s.holdLeft = rng.range (0.5f, 1.1f);
                    // three two-state chains (left/right, back/front, down/up). Table A: stay 0.2, cross 0.8.
                    // Table B: from 0 stay 0.85, from 1 stay 0.6. An axis uses A when the other two agree.
                    int bit[3] = { s.cell & 1, (s.cell >> 1) & 1, (s.cell >> 2) & 1 }, nextBit[3];
                    for (int a = 0; a < 3; ++a)
                    {
                        const bool tableA = bit[(a + 1) % 3] == bit[(a + 2) % 3];
                        const float stay = tableA ? 0.2f : (bit[a] == 0 ? 0.85f : 0.6f);
                        nextBit[a] = rng.uni() < stay ? bit[a] : 1 - bit[a];
                    }
                    s.cell = nextBit[0] | (nextBit[1] << 1) | (nextBit[2] << 2);
                    // somewhere inside that cell
                    Vec3 p;
                    do p = { rng.uni(), rng.uni(), rng.uni() }; while (p.length() > 1.0f || p.length() < 0.25f);
                    s.target = Vec3 { nextBit[0] ? p.x : -p.x, nextBit[1] ? p.y : -p.y, nextBit[2] ? p.z : -p.z } * sp;
                }
                glide (s.target, 2.5f + 4.0f * sf);
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
    Vec3 blow, blowTarget;
    float twAmt = 0.0f, twAngle = 0.0f, twProgress = 0.0f;
    float spinTau = 0.0f, spinAng = 0.0f, spinDir = 1.0f;
    Rot nomA, nomB;
    float nomLeft = 0.0f;
};

//==============================================================================
// The weather, inside the sphere: while a kind of weather is on, a few patches of it (clouds,
// rain clouds, snow flurries, heat haze, fog banks) drift slowly through the ball. A grain inside a patch is
// "rained on": its settings wander off at random (all weather does this), the snow freezes its
// spectrum while it's in there, the heat smears it, and in a fog bank it gets hard to say where it is.
class SkyPatches
{
public:
    enum Kind { cloud = 0, rain, snow, heat, fog, numKinds };
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

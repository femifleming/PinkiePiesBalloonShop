#pragma once
#include "Ambi.h"
#include <array>
#include <vector>

namespace fp
{
//==============================================================================
// The sphere's granulator, after the IEM GranularEncoder: many short, overlapping mono grains,
// each one born at its own direction and encoded there once, so a cloud of grains is a cloud of
// points you can hear around you, not one smeared sound.
//
//  * An 8 s stereo circular buffer, written in three bands: the input as it is, and low-passed
//    copies for grains an octave or two up, so they don't alias. (IEM reads only down in pitch;
//    here grains can go up too, started far enough back that they never catch the write head.)
//  * Each grain takes one channel of the input (left or right, at random: the stereo stays alive
//    as different grains), reads it with 4-point Hermite interpolation, and goes through its window
//    (the shop's grain shapes; bell is a Hann window like IEM's).
//  * Where: each grain picks one of the flying places and a random direction inside a cap round it
//    (uniform over the cap's area, IEM's shape 0). Spread 0 puts every grain exactly at its place;
//    spread 1 lets them land anywhere. The grain's distance is its place's distance.
//  * When: a new grain every length / overlap, jittered; length, read position and pitch are drawn
//    afresh for every grain within the deviation, so no two are alike.
//  * Level: sqrt(1 / (overlap x window power)), IEM's rule for uncorrelated grains.
//  * Freeze: the buffer stops being written (its writing fades over 5 ms); grains keep reading it.
class SpatialGranulator
{
public:
    using Vec3 = ambi::Vec3;
    static constexpr int kMaxGrains = 192, kMaxPlaces = 16;

    struct Settings
    {
        float lengthMs = 120.0f, overlap = 20.0f, posS = 0.0f, posRandS = 0.5f;
        float semis = 0.0f;            // base pitch (the octave balloon)
        bool randomOctave = false;
        float reverse = 0.0f;          // chance a grain plays backwards
        int shape = 2;
        const ChalkShape* chalk = nullptr;
        float spread = 0.3f;           // 0: exactly at its place .. 1: anywhere on the sphere
        float deviation = 0.3f;        // how much each grain's length, start and pitch wander
        bool freeze = false;
        float radiusM = 12.0f;         // how many metres the sphere's radius is (for the distance cues)
        int numPlaces = 1;
        std::array<Vec3, kMaxPlaces> places {};
    };

    void prepare (double sampleRate, int maxBlock, uint64_t seed = 0x6A11)
    {
        sr = sampleRate;
        N = (int) (8.0 * sr);
        for (auto& band : buf) for (auto& c : band) c.assign ((size_t) N, 0.0f);
        for (int c = 0; c < 2; ++c) { aa1[c].setup (sr, sr * 0.22); aa2[c].setup (sr, sr * 0.11); }
        tmp.assign ((size_t) std::max (1, maxBlock), 0.0f);
        rng.seed (seed);
        writeStep = (float) (1.0 / (0.005 * sr));
        reset();
    }

    void reset()
    {
        for (auto& band : buf) for (auto& c : band) std::fill (c.begin(), c.end(), 0.0f);
        for (int c = 0; c < 2; ++c) { aa1[c].reset(); aa2[c].reset(); }
        for (auto& g : grains) g.active = false;
        w = 0; validLen = 0; untilNext = 0.0; writeGain = 1.0f; octHold = 0; octNow = 0;
    }

    // in: the input block; out: 16 channels the grains are added into, at 'level' (0..1+)
    void process (const float* inL, const float* inR, float* const* out, int n, const Settings& s, float level) noexcept
    {
        if ((int) tmp.size() < n) tmp.resize ((size_t) n);
        const float overlap = std::clamp (s.overlap, 1.0f, 96.0f);
        const double lenS = std::clamp ((double) s.lengthMs * 0.001, 0.005, 2.0);
        const float norm = std::min (1.0f, std::sqrt (1.0f / (overlap * std::max (0.05f, grainShapePower (s.shape, s.chalk))))) * 1.75f;   // (IEM's 1.41, plus 2 dB: the cloud alone sits level with the dry sound)

        // record (unless frozen: then the writing fades out and the buffer holds still)
        const float wTarget = s.freeze ? 0.0f : 1.0f;
        const int blockStartW = w;
        for (int i = 0; i < n; ++i)
        {
            writeGain += std::clamp (wTarget - writeGain, -writeStep, writeStep);
            if (writeGain <= 0.0f) continue;   // frozen: the head stays where it stopped
            const float in[2] = { inL[i], inR[i] };
            for (int c = 0; c < 2; ++c)
            {
                const float x = in[c];
                const float b1 = aa1[c].process (x), b2 = aa2[c].process (x);
                const float keep = 1.0f - writeGain;
                auto put = [&] (std::vector<float>& v, float val) { v[(size_t) w] = keep * v[(size_t) w] + writeGain * val; };
                put (buf[0][(size_t) c], x); put (buf[1][(size_t) c], b1); put (buf[2][(size_t) c], b2);
            }
            w = (w + 1 == N) ? 0 : w + 1;
            validLen = std::min (N, validLen + 1);
        }
        const bool live = writeGain > 0.0f;
        const int headMoved = (w - blockStartW + N) % N;   // how far the write head went this block

        // new grains, at their sample offsets inside this block
        for (int i = 0; i < n; ++i)
        {
            untilNext -= 1.0;
            if (untilNext > 0.0) continue;
            const double gap = lenS / overlap * sr;
            untilNext += gap * (1.0 + 0.5 * (double) s.deviation * (double) rng.bi());
            if (untilNext < 1.0) untilNext = 1.0;
            spawn (s, i, lenS, live, headMoved, n);
        }

        // render: each grain into a mono strip, then into the 16 channels at its own gains
        for (auto& g : grains)
        {
            if (! g.active) continue;
            if (g.startAt >= n) { g.startAt -= n; continue; }   // still on its way (the sound takes time to arrive)
            const int from = g.startAt;
            g.startAt = 0;
            const auto& src = buf[(size_t) g.band][(size_t) g.ch];
            int i = from;
            for (; i < n && g.age < g.len; ++i, ++g.age)
            {
                const float t = (float) g.age / (float) g.len;
                const float env = grainShape (g.shape, t, g.chalk);
                g.lp += g.a * (read (src, g.pos) - g.lp);   // the air between
                tmp[(size_t) i] = env * g.lp;
                g.pos += g.rate;
                if (g.pos >= (double) N) g.pos -= (double) N; else if (g.pos < 0.0) g.pos += (double) N;
            }
            const int end = i;
            g.lp = flushDenormal (g.lp);
            if (g.age >= g.len) g.active = false;
            if (end <= from) continue;
            for (int c = 0; c < ambi::kCh; ++c)
            {
                const float k = g.coef[(size_t) c] * norm * level;
                if (k == 0.0f) continue;
                float* o = out[c] + from;
                const float* x = tmp.data() + from;
                for (int j = 0; j < end - from; ++j) o[j] += k * x[j];
            }
        }
    }

    int activeGrains() const noexcept { int k = 0; for (auto& g : grains) k += g.active ? 1 : 0; return k; }

private:
    struct Grain
    {
        bool active = false;
        int band = 0, ch = 0, len = 1, age = 0, startAt = 0, shape = 2;
        double pos = 0.0, rate = 1.0;
        float lp = 0.0f, a = 1.0f;
        const ChalkShape* chalk = nullptr;
        ambi::Coefs coef {};
    };

    float read (const std::vector<float>& d, double p) const noexcept
    {
        const int i = (int) p;
        const float f = (float) (p - (double) i);
        auto at = [&] (int k) { k += i; if (k < 0) k += N; else if (k >= N) k -= N; return d[(size_t) k]; };
        return hermite (at (-1), at (0), at (1), at (2), f);
    }

    void spawn (const Settings& s, int offset, double lenS, bool live, int headMoved, int n) noexcept
    {
        Grain* g = nullptr;
        for (auto& c : grains) if (! c.active) { g = &c; break; }
        if (g == nullptr || validLen < (int) (0.05 * sr)) return;

        // pitch: the octave (now and then a random one), a few cents of deviation, maybe backwards
        if (s.randomOctave && --octHold <= 0) { octHold = 3 + (int) (rng.uni() * 12.0f); octNow = rng.integer (3) - 1; }
        const float semis = s.semis + (s.randomOctave ? 12.0f * (float) octNow : 0.0f) + 0.3f * s.deviation * rng.bi();
        double rate = std::exp2 ((double) semis / 12.0);
        const bool backwards = rng.uni() < s.reverse;
        if (backwards) rate = -rate;

        double L = lenS * (1.0 + 0.6 * (double) s.deviation * (double) rng.bi()) * sr;
        L = std::clamp (L, 0.004 * sr, 2.0 * sr);

        // where on the tape: 'pos' seconds back, plus a random amount up to posRand, and always far
        // enough from the write head (and inside what's been recorded) for the whole grain
        const double headSpeed = live ? 1.0 : 0.0;
        const double drift = (headSpeed - rate) * L;   // how far its age moves during the grain
        const double margin = 8.0 + (double) n;        // (spawned mid-block: the head runs on to the block's end)
        const double lo = margin + std::max (0.0, -drift);
        const double hi = (double) validLen - margin - std::max (0.0, drift);
        if (hi <= lo) return;
        double age0 = ((double) s.posS + (double) s.posRandS * (double) rng.uni()) * sr;
        age0 = std::clamp (age0, lo, hi);
        // the head at this grain's first sample
        // (the block is written before the grains read it, so the head was 'headMoved' samples back at the block's start)
        const double head = (double) ((w - headMoved + (headMoved == n ? offset : 0) + 2 * N) % N);

        g->active = true;
        g->ch = rng.uni() < 0.5f ? 0 : 1;
        const double ar = std::abs (rate);
        g->band = ar > 3.2 ? 2 : (ar > 1.6 ? 1 : 0);
        g->rate = rate;
        g->len = (int) L;
        g->age = 0;
        g->shape = s.shape;
        g->chalk = s.chalk;
        g->pos = head - age0;
        while (g->pos < 0.0) g->pos += (double) N;
        while (g->pos >= (double) N) g->pos -= (double) N;

        // where: a place, then a direction uniformly inside a cap round it
        const int np = std::clamp (s.numPlaces, 1, kMaxPlaces);
        const Vec3 place = s.places[(size_t) rng.integer (np)];
        const float dist = std::clamp (place.length(), 0.0f, 1.0f);
        const Vec3 c = place.normalised();
        const float cap = std::clamp (s.spread, 0.0f, 1.0f) * (float) kPi;
        const float cosT = 1.0f - rng.uni() * (1.0f - std::cos (cap));
        const float sinT = std::sqrt (std::max (0.0f, 1.0f - cosT * cosT));
        const float phi = rng.uni() * (float) kTwoPi;
        Vec3 u = std::abs (c.x) < 0.9f ? Vec3 { 1, 0, 0 } : Vec3 { 0, 1, 0 };
        u = (u - c * u.dot (c)).normalised();
        const Vec3 v { c.y * u.z - c.z * u.y, c.z * u.x - c.x * u.z, c.x * u.y - c.y * u.x };
        const Vec3 dir = c * cosT + u * (sinT * std::cos (phi)) + v * (sinT * std::sin (phi));
        // its distance (as the DistanceSource does for whole sounds): 1 / d from half the radius, the air's
        // lowpass, and it arrives d / 343 s later
        const float dm = std::max (0.3f, dist * s.radiusM);
        const float gainD = std::min (4.0f, std::max (0.5f, 0.5f * s.radiusM) / dm);
        g->coef = ambi::encodeNear (dir * std::max (dist, 1.0e-3f));
        for (auto& k : g->coef) k *= gainD;
        const float air = std::min (20000.0f / (1.0f + dm / 25.0f), (float) sr * 0.45f);
        g->a = 1.0f - std::exp (-(float) kTwoPi * air / (float) sr);
        g->lp = 0.0f;
        g->startAt = offset + (int) (dm / 343.0f * (float) sr);
    }

    double sr = 48000.0, untilNext = 0.0;
    int N = 384000, w = 0, validLen = 0, octHold = 0, octNow = 0;
    std::array<std::array<std::vector<float>, 2>, 3> buf;   // [band][channel]
    Butter8LP aa1[2], aa2[2];
    std::array<Grain, kMaxGrains> grains {};
    std::vector<float> tmp;
    float writeGain = 1.0f, writeStep = 0.004f;
    Rng rng;
};
} // namespace fp

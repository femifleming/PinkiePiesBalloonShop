#pragma once
#include "Utils.h"
#include <array>

namespace fp
{
//==============================================================================
// Live granular shifter.
//
// * Records continuously into 4-second stereo "tapes". There are two tapes so a
//   freeze can be captured on one while live grains keep playing from the other;
//   the frozen cloud fades in (equal-power) over the Freeze Fade time.
// * Each tape is written in 3 bands (full, and anti-aliased copies for +1 and +2
//   octaves) so octave-up grains don't alias.
// * Sawtooth grain envelope: linear ramp up, with a short raised-cosine drop at
//   the end so it never clicks.
// * Grain size, buffer position and (optionally) octave are driven by random
//   walkers with random timing and random slew. Grains never cross the record
//   head or a freeze seam, so there are no discontinuities.
class GrainShifter
{
public:
    void prepare (double sampleRate, uint64_t seed = 0x51ED)
    {
        sr = sampleRate;
        N  = (int) (4.0 * sr);
        rng.seed (seed);

        for (auto& t : tapes)
        {
            for (auto& b : t.data) for (auto& c : b) c.assign ((size_t) N, 0.0f);
            t.recording = true; t.validLen = 0; t.heldAt = 0;
        }
        for (int c = 0; c < 2; ++c)
        {
            aa1[c].setup (sr, sr * 0.22);
            aa2[c].setup (sr, sr * 0.11);
        }

        sizeWalk.prepare (sr, rng, 0.15f, 2.2f, 0.02f, 2.5f);
        posWalk.prepare  (sr, rng, 0.10f, 2.0f, 0.02f, 3.0f);
        margin = 16.0;
        reset();
    }

    void reset()
    {
        for (auto& t : tapes)
        {
            for (auto& b : t.data) for (auto& c : b) std::fill (c.begin(), c.end(), 0.0f);
            t.recording = true; t.validLen = 0;
        }
        for (auto& g : grains) g.active = false;
        for (int c = 0; c < 2; ++c) { aa1[c].reset(); aa2[c].reset(); }
        w = 0; frozen = false; heldTape = -1; x = 0.0f;
        nextSpawn[0] = nextSpawn[1] = 0;
        oct.cur = oct.prev = 0; oct.q = 1.0f;
    }

    const ChalkShape* chalk = nullptr;   // the chalkboard grain shape (owned by the processor)

    // --- controls (call once per block) --------------------------------------
    void setFadeTime (float seconds) noexcept { xInc = 1.0f / (float) std::max (1.0, seconds * sr); }
    // size = centre grain length (it still wanders around it), density = grains overlapping,
    // shape = envelope, pan = random panning amount, reverse = chance a grain plays backwards
    void setGrainParams (float sizeMs, float densityOverlap, int shapeIndex, float panAmount, float reverseChance) noexcept
    {
        sizeSec = std::clamp (sizeMs, 5.0f, 3000.0f) * 0.001f;
        density = std::clamp ((double) densityOverlap, 0.5, 32.0);
        shape = std::clamp (shapeIndex, 0, 6);
        panAmt = std::clamp (panAmount, 0.0f, 1.0f);
        reverseAmt = std::clamp (reverseChance, 0.0f, 1.0f);
    }

    // where in the last 4 s the grains read from (0 = just now, 1 = four seconds ago), and how far
    // they wander around that spot (0 = always there, 1 = all over the tape, as before)
    void setPosition (float pos, float randomAmount) noexcept { posC = std::clamp (pos, 0.0f, 1.0f); posRand = std::clamp (randomAmount, 0.0f, 1.0f); }

    void setOctave (int o, bool randomMode) noexcept { oct.manual = std::clamp (o, -2, 2); oct.random = randomMode; }

    void setFreeze (bool on) noexcept
    {
        if (on == frozen) return;
        frozen = on;
        if (on && heldTape < 0)
        {
            // hold the recording tape that has the most audio in it
            heldTape = tapes[0].validLen >= tapes[1].validLen ? 0 : 1;
            auto& t = tapes[(size_t) heldTape];
            t.recording = false;
            t.heldAt = w;
            // any live grain on that tape that would run past the new seam fades out quickly
            for (auto& g : grains)
                if (g.active && g.tape == heldTape)
                {
                    const double age = wrapAge ((double) w - g.pos);
                    const double endAge = age - g.rate * (double) (g.len - g.age);
                    if (endAge < margin || endAge > (double) t.validLen - margin) g.startKill (sr);
                }
        }
    }

    bool isFrozen() const noexcept { return frozen; }
    float getFreezeAmount() const noexcept { return x; }

    // --- audio ---------------------------------------------------------------
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        // record
        const float in[2] = { inL, inR };
        float b1[2], b2[2];
        for (int c = 0; c < 2; ++c) { b1[c] = aa1[c].process (in[c]); b2[c] = aa2[c].process (in[c]); }
        for (auto& t : tapes)
            if (t.recording)
            {
                for (int c = 0; c < 2; ++c)
                {
                    t.data[0][(size_t) c][(size_t) w] = in[c];
                    t.data[1][(size_t) c][(size_t) w] = b1[c];
                    t.data[2][(size_t) c][(size_t) w] = b2[c];
                }
                t.validLen = std::min (N, t.validLen + 1);
            }
        w = (w + 1 == N) ? 0 : w + 1;

        // freeze crossfade
        if (frozen) x = std::min (1.0f, x + xInc);
        else        x = std::max (0.0f, x - xInc);

        if (! frozen && x <= 0.0f && heldTape >= 0)
        {
            for (auto& g : grains) if (g.active && ! g.live) g.active = false;
            auto& t = tapes[(size_t) heldTape];
            t.recording = true; t.validLen = 0;
            heldTape = -1;
        }

        const float layerGain[2] = { std::cos (x * (float) kPi * 0.5f), std::sin (x * (float) kPi * 0.5f) };

        // random modulators
        sizeWalk.next();
        posWalk.next();
        oct.tick (rng, sr);

        // schedulers: [0] live cloud, [1] frozen cloud
        for (int layer = 0; layer < 2; ++layer)
        {
            if (--nextSpawn[layer] > 0) continue;
            nextSpawn[layer] = (int) (0.005 * sr); // retry soon if we can't spawn
            if (layerGain[layer] < 1.0e-4f) continue;
            if (layer == 1 && heldTape < 0) continue;
            spawn (layer == 0);
        }

        // render
        float accL = 0.0f, accR = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const auto& t = tapes[(size_t) g.tape];
            const float env = g.envelope() * layerGain[g.live ? 0 : 1];
            const float l = readTape (t, g.band, 0, g.pos);
            const float r = readTape (t, g.band, 1, g.pos);
            accL += env * g.gl * l;
            accR += env * g.gr * r;

            g.pos += g.rate;
            if (g.pos >= (double) N) g.pos -= (double) N;
            else if (g.pos < 0.0) g.pos += (double) N;
            if (++g.age >= g.len || g.killDone()) g.active = false;
        }
        const float targetGain = std::min (4.0f, 1.3f / std::sqrt (std::max (0.05f, (float) density * grainShapePower (shape, chalk))));
        gainSm += 0.0005f * (targetGain - gainSm);
        outL = accL * gainSm;
        outR = accR * gainSm;
    }

private:
    struct Tape
    {
        std::array<std::array<std::vector<float>, 2>, 3> data; // [band][channel]
        bool recording = true;
        int validLen = 0, heldAt = 0;
    };

    struct Grain
    {
        bool active = false, live = true;
        int tape = 0, band = 0, len = 1, age = 0, fallLen = 1;
        double pos = 0.0, rate = 1.0;
        float gl = 1.0f, gr = 1.0f;
        float kill = 1.0f, killStep = 0.0f;
        int shape = 0;
        const ChalkShape* chalk = nullptr;

        float envelope() noexcept
        {
            float e = grainShape (shape, (float) age / (float) len, chalk);
            if (killStep > 0.0f) { kill = std::max (0.0f, kill - killStep); e *= kill; }
            return e;
        }
        void startKill (double sr) { if (killStep == 0.0f) killStep = 1.0f / (float) (0.006 * sr); }
        bool killDone() const noexcept { return killStep > 0.0f && kill <= 0.0f; }
    };

    struct OctaveSelector
    {
        int cur = 0, prev = 0, manual = 0, hold = 0;
        bool random = false;
        float q = 1.0f, qInc = 0.0f;

        void moveTo (int o, float slewSec, double sr)
        {
            if (o == cur) return;
            prev = (q >= 0.5f) ? cur : prev;
            cur  = o;
            q    = 0.0f;
            qInc = 1.0f / (float) std::max (1.0, slewSec * sr);
        }

        void tick (Rng& rng, double sr) noexcept
        {
            if (q < 1.0f) q = std::min (1.0f, q + qInc);
            if (random)
            {
                if (--hold <= 0)
                {
                    hold = (int) (sr * rng.range (0.4f, 5.0f));                  // random timing
                    const int o = std::clamp (manual + rng.integer (3) - 1, -2, 2);
                    moveTo (o, 0.04f * std::pow (60.0f, rng.uni()), sr);        // random slew 40ms..2.4s
                }
            }
            else
            {
                moveTo (manual, 0.12f, sr);
            }
        }

        // slew between octaves = probability that new grains use the new octave
        int pick (Rng& rng) noexcept { return rng.uni() < smoothstep (q) ? cur : prev; }
    };

    double wrapAge (double a) const noexcept
    {
        while (a < 0.0) a += (double) N;
        while (a >= (double) N) a -= (double) N;
        return a;
    }

    float readTape (const Tape& t, int band, int c, double pos) const noexcept
    {
        const auto& d = t.data[(size_t) band][(size_t) c];
        const int i = (int) pos;
        const float f = (float) (pos - (double) i);
        auto at = [&] (int k) { k += i; if (k < 0) k += N; else if (k >= N) k -= N; return d[(size_t) k]; };
        return hermite (at (-1), at (0), at (1), at (2), f);
    }

    void spawn (bool live) noexcept
    {
        int ti;
        if (live)
        {
            ti = -1;
            for (int i = 0; i < 2; ++i)
                if (tapes[(size_t) i].recording && (ti < 0 || tapes[(size_t) i].validLen > tapes[(size_t) ti].validLen))
                    ti = i;
            if (ti < 0) return;
        }
        else ti = heldTape;

        const auto& t = tapes[(size_t) ti];
        if (t.validLen < (int) (0.05 * sr)) return;

        Grain* g = nullptr;
        for (auto& c : grains) if (! c.active) { g = &c; break; }
        if (g == nullptr) return;

        const int o = std::clamp (oct.pick (rng), -2, 2);
        const bool backwards = rng.uni() < reverseAmt;            // random reverse
        const double rate = std::exp2 ((double) o) * (backwards ? -1.0 : 1.0);

        // grain length: wanders up to 1.5 octaves either side of the size setting
        double lenS = (double) sizeSec * std::pow (2.0, ((double) sizeWalk.value - 0.5) * 3.0) * rng.range (0.85f, 1.15f);
        double L = lenS * sr;

        const double headSpeed = t.recording ? 1.0 : 0.0;
        const double drift = (headSpeed - rate) * L;   // how far the age moves during the grain
        const double usable = (double) t.validLen - 2.0 * margin;
        if (std::abs (drift) > usable * 0.9)
            L = usable * 0.9 / std::max (1.0e-6, std::abs (headSpeed - rate));
        if (L < 0.010 * sr) return;
        const double d2 = (headSpeed - rate) * L;

        const double lo = margin + std::max (0.0, -d2);
        const double hi = (double) t.validLen - margin - std::max (0.0, d2);
        if (hi <= lo) return;

        const double v = std::clamp ((double) posC + (double) posRand * (((double) posWalk.value - 0.5) + 0.1 * (double) rng.bi()), 0.0, 1.0);
        const double age0 = lo + v * (hi - lo);
        const double head = t.recording ? (double) w : (double) t.heldAt;

        g->active  = true;
        g->live    = live;
        g->tape    = ti;
        g->band    = o >= 2 ? 2 : (o == 1 ? 1 : 0);
        g->rate    = rate;
        g->len     = (int) L;
        g->age     = 0;
        g->fallLen = std::clamp ((int) (L * 0.12), (int) (0.004 * sr), std::max (2, (int) L - 1));
        g->pos     = wrapAge (head - age0);
        g->kill    = 1.0f; g->killStep = 0.0f;
        g->shape   = shape;
        g->chalk   = chalk;
        const float pan = rng.bi() * panAmt;
        g->gl = 1.0f - std::max (0.0f, pan);
        g->gr = 1.0f + std::min (0.0f, pan);

        // relatively dense: ~7 overlapping grains, with jittered spacing
        nextSpawn[live ? 0 : 1] = std::max (1, (int) (L / density * rng.range (0.6f, 1.4f)));
    }

    double sr = 48000.0, margin = 16.0;
    int N = 192000, w = 0;
    std::array<Tape, 2> tapes;
    std::array<Grain, 64> grains;
    Butter8LP aa1[2], aa2[2];
    Rng rng;
    RandomWalk sizeWalk, posWalk;
    OctaveSelector oct;
    bool frozen = false;
    int heldTape = -1;
    float x = 0.0f, xInc = 1.0f / 48000.0f;
    int nextSpawn[2] {};
    double density = 7.0;
    float sizeSec = 0.12f, panAmt = 0.45f, reverseAmt = 0.0f, gainSm = 0.85f, posC = 0.5f, posRand = 1.0f;
    int shape = 0;
};
} // namespace fp

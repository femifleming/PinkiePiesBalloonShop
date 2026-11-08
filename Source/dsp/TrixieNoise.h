#pragma once
#include "Utils.h"
#include <atomic>

namespace fp
{
//==============================================================================
// Trixie's noise: when she feels like it, she plays something strange into the plugin's input
// while she's in the shop, so it goes through all the effects like your own sound would.
//
// Every visit rolls a new recipe from a seed:
//   an FM voice whose pitch hops around (sample-and-hold, 2 to 22 hops a second, sometimes
//   gliding, sometimes jumping), with an odd modulator ratio and a wobbling index
//   a band of noise through a resonant filter that sweeps on its own
//   a crusher (sample rate held down, bits taken away) on the lot, some visits more than others
//   an auto-pan wandering it across the stereo field
// and every time she zaps a balloon, a falling FM chirp. It fades in and out around her visit.
class TrixieNoise
{
public:
    void prepare (double sampleRate) { sr = sampleRate; active = false; for (auto& c : chirps) c.on = false; lastStart = startCount.load(); lastZap = zapCount.load(); }

    // the editor: start a noise (seed picks the recipe), and a chirp for each zap
    void request (uint32_t seed, float seconds) noexcept { reqSeed.store (seed); reqSeconds.store (seconds); startCount.fetch_add (1); }
    void zap() noexcept { zapCount.fetch_add (1); }
    bool isPlaying() const noexcept { return playing.load(); }

    void process (float* L, float* R, int n) noexcept
    {
        if (const int s = startCount.load(); s != lastStart) { lastStart = s; begin (reqSeed.load(), reqSeconds.load()); }
        while (lastZap != zapCount.load()) { ++lastZap; if (active) chirp(); }
        bool anyChirp = false;
        for (auto& c : chirps) anyChirp |= c.on;
        if (! active && ! anyChirp) { playing.store (false); return; }
        playing.store (true);

        for (int i = 0; i < n; ++i)
        {
            float y = 0.0f, pan = 0.5f;
            if (active)
            {
                // the envelope: in over 0.4 s, out over the last 0.8 s
                const float t = (float) age / (float) sr;
                const float env = std::min (1.0f, t / 0.4f) * std::clamp ((dur - t) / 0.8f, 0.0f, 1.0f);
                if (++age >= (int) (dur * sr)) active = false;

                // the hopping pitch
                if (--holdLeft <= 0)
                {
                    holdLeft = std::max (1, (int) (sr / (double) hopRate * (double) rng.range (0.6f, 1.4f)));
                    target = lo * std::pow (hi / lo, rng.uni());
                }
                freq += glide * (target - freq);
                // FM with a wobbling index
                lfoPh += (double) lfoRate / sr; if (lfoPh > 1.0) lfoPh -= 1.0;
                const float index = idxLo + (idxHi - idxLo) * (0.5f + 0.5f * (float) std::sin (kTwoPi * lfoPh));
                modPh += (double) (freq * ratio) / sr; if (modPh > 1.0) modPh -= 1.0;
                carPh += (double) freq / sr + (double) index * std::sin (kTwoPi * modPh) * (double) freq / sr * 0.15;
                carPh -= std::floor (carPh);
                const float fm = (float) std::sin (kTwoPi * carPh);
                // the swept noise band (a resonant state-variable bandpass)
                sweepPh += (double) sweepRate / sr; if (sweepPh > 1.0) sweepPh -= 1.0;
                const float fc = 200.0f * std::pow (40.0f, 0.5f + 0.5f * (float) std::sin (kTwoPi * sweepPh));
                const float f = 2.0f * std::sin ((float) kPi * std::min (fc, (float) sr * 0.2f) / (float) sr);
                bpLow += f * bpBand;
                const float high = rng.bi() - bpLow - q * bpBand;
                bpBand = flushDenormal (bpBand + f * high);
                bpLow = flushDenormal (bpLow);
                float v = fmLevel * fm + noiseLevel * bpBand;
                // crush it: hold the sample, take bits away
                if (--crushLeft <= 0) { crushLeft = crushHold; held = v; }
                v = held;
                if (bits < 16.0f) { const float stepsPerUnit = std::exp2 (bits - 1.0f); v = std::round (v * stepsPerUnit) / stepsPerUnit; }
                y = 0.16f * env * softClip (v);
                panPh += (double) panRate / sr; if (panPh > 1.0) panPh -= 1.0;
                pan = 0.5f + panDepth * 0.5f * (float) std::sin (kTwoPi * panPh);
            }
            float cl = 0.0f, cr = 0.0f;
            for (auto& c : chirps)
            {
                if (! c.on) continue;
                const float u = (float) c.age / (float) c.len;
                const float f = c.f0 * std::pow (c.f1 / c.f0, u);   // a falling sweep
                c.mod += (double) (f * c.ratio) / sr; if (c.mod > 1.0) c.mod -= 1.0;
                c.ph += (double) f / sr + 0.12 * (double) (c.index * (1.0f - u)) * std::sin (kTwoPi * c.mod) * (double) f / sr;
                c.ph -= std::floor (c.ph);
                const float e = std::min (1.0f, (float) c.age / (0.004f * (float) sr)) * (1.0f - u) * (1.0f - u);
                const float s = 0.14f * e * (float) std::sin (kTwoPi * c.ph);
                cl += s * (1.0f - c.pan); cr += s * c.pan;
                if (++c.age >= c.len) c.on = false;
            }
            L[i] += y * (1.0f - pan) * 1.4f + cl * 1.4f;
            R[i] += y * pan * 1.4f + cr * 1.4f;
        }
    }

private:
    void begin (uint32_t seed, float seconds) noexcept
    {
        rng.seed (0x7121E000ull + seed);
        dur = std::max (0.5f, seconds);
        age = 0; active = true;
        lo = rng.range (60.0f, 300.0f); hi = lo * rng.range (2.0f, 12.0f);
        hopRate = rng.range (2.0f, 22.0f);
        glide = rng.uni() < 0.5f ? 1.0f : (float) (1.0 - std::exp (-1.0 / (rng.range (0.005f, 0.08f) * sr)));
        static const float ratios[] = { 0.5f, 1.41f, 2.76f, 3.5f, 5.19f, 7.13f, 0.27f };
        ratio = ratios[rng.integer (7)];
        idxLo = rng.range (0.0f, 2.0f); idxHi = idxLo + rng.range (1.0f, 9.0f);
        lfoRate = rng.range (0.1f, 7.0f);
        noiseLevel = rng.range (0.0f, 1.2f); fmLevel = rng.range (0.4f, 1.0f);
        sweepRate = rng.range (0.05f, 3.0f); q = rng.range (0.05f, 0.6f);
        const float crush = rng.uni();
        crushHold = crush < 0.4f ? 1 : 1 + rng.integer (14);
        bits = crush < 0.5f ? 16.0f : rng.range (3.0f, 9.0f);
        panRate = rng.range (0.1f, 4.0f); panDepth = rng.range (0.2f, 1.0f);
        freq = target = lo; holdLeft = 0; crushLeft = 0; held = 0.0f;
        carPh = modPh = lfoPh = sweepPh = panPh = 0.0; bpLow = bpBand = 0.0f;
    }

    void chirp() noexcept
    {
        for (auto& c : chirps)
            if (! c.on)
            {
                c.on = true; c.age = 0; c.len = (int) (sr * rng.range (0.12f, 0.35f));
                c.f0 = rng.range (1500.0f, 4500.0f); c.f1 = rng.range (90.0f, 400.0f);
                c.ratio = rng.range (0.7f, 3.3f); c.index = rng.range (2.0f, 10.0f);
                c.pan = rng.uni(); c.ph = c.mod = 0.0;
                return;
            }
    }

    struct Chirp { bool on = false; int age = 0, len = 1; float f0 = 2000, f1 = 200, ratio = 1, index = 4, pan = 0.5f; double ph = 0, mod = 0; };

    double sr = 48000.0;
    std::atomic<int> startCount { 0 }, zapCount { 0 };
    std::atomic<uint32_t> reqSeed { 0 };
    std::atomic<float> reqSeconds { 5.0f };
    std::atomic<bool> playing { false };
    int lastStart = 0, lastZap = 0;
    bool active = false;
    int age = 0, holdLeft = 0, crushLeft = 0, crushHold = 1;
    float dur = 5.0f, lo = 100, hi = 800, hopRate = 6, glide = 1, ratio = 1.41f, idxLo = 0, idxHi = 4, lfoRate = 1;
    float noiseLevel = 0.5f, fmLevel = 0.7f, sweepRate = 0.5f, q = 0.2f, bits = 16, panRate = 1, panDepth = 0.5f;
    float freq = 100, target = 100, held = 0, bpLow = 0, bpBand = 0;
    double carPh = 0, modPh = 0, lfoPh = 0, sweepPh = 0, panPh = 0;
    Chirp chirps[6];
    Rng rng;
};
} // namespace fp

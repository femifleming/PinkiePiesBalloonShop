#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Utils.h"
#include <vector>

namespace fp
{
//==============================================================================
// Heatwave: the sound melts into a slow smear that keeps drifting.
//
// The same long STFT as the snow (16384-point Hann at 48 kHz, 8x overlap), but where the snow
// holds the spectrum still, the heatwave lets it creep: each bin's level follows the live sound
// with an 8 s time constant, so whatever you play is spread over many seconds and the held
// sound slowly turns into the next one. Every bin gets a fresh random phase every frame, the
// way a paulstretch works, so there's no pitch to lock to and no loop: just a still, glassy
// wash that keeps changing underneath.
//
// On top of that a gentle 12 dB/oct lowpass wanders between about 6 and 14 kHz on two slow,
// unrelated swells, like air shimmering. It takes the top off without the rain's darkness.
//
// It comes in and goes away over its fade times, 5 s to 5 min each (an equal-power crossfade; the wash has random phases, so
// it can't comb against the dry sound). Off and settled, the output is untouched (bit-exact).
class HeatWave
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        order = sr <= 50000.0 ? 14 : (sr <= 100000.0 ? 15 : 16);
        N = 1 << order;
        hop = N / 8;
        bins = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / N));
        frame.assign ((size_t) (2 * N), 0.0f);
        for (auto& c : ch)
        {
            c.in.assign ((size_t) N, 0.0f); c.out.assign ((size_t) N, 0.0f);
            c.env.assign ((size_t) bins, 0.0f);
            c.lp1 = c.lp2 = 0.0f;
        }
        follow = (float) (1.0 - std::exp (-(double) hop / (driftSec * sr)));
        setFadeTimes (5.0f, 5.0f);
        idx = 0; hopCount = 0; ramp = 0.0; amount = 0.0f; fresh = true;
        lfoA = 0.0; lfoB = 0.37; ctl = 0;
        updateFilter();
    }

    void process (float* L, float* R, int n, bool on) noexcept
    {
        if (! on && ramp <= 0.0)
        {
            // resting: keep the input ring filled so the first frame has something to hear
            for (int i = 0; i < n; ++i)
            {
                ch[0].in[(size_t) idx] = L[i]; ch[1].in[(size_t) idx] = R[i];
                ch[0].out[(size_t) idx] = 0.0f; ch[1].out[(size_t) idx] = 0.0f;
                idx = (idx + 1) & (N - 1);
                ch[0].lp1 = ch[0].lp2 = ch[1].lp1 = ch[1].lp2 = 0.0f;
            }
            fresh = true;
            amount = 0.0f;
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            ramp = on ? std::min (1.0, ramp + upStep) : std::max (0.0, ramp - downStep);
            amount = (float) ramp;
            if (++ctl >= 32) { ctl = 0; updateFilter(); }
            const float wet = std::sin (amount * (float) kPi * 0.5f), dry = std::cos (amount * (float) kPi * 0.5f);
            const float lpMix = amount * amount * (3.0f - 2.0f * amount);
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                auto& C = ch[(size_t) c];
                const float x = io[c][i];
                const float w = C.out[(size_t) idx];
                C.out[(size_t) idx] = 0.0f;
                C.in[(size_t) idx] = x;
                const float y = dry * x + wet * w;
                // TPT state-variable lowpass (Butterworth)
                const float v3 = y - C.lp2;
                const float v1 = a1 * C.lp1 + a2 * v3;
                const float v2 = C.lp2 + a2 * C.lp1 + a3 * v3;
                C.lp1 = flushDenormal (2.0f * v1 - C.lp1);
                C.lp2 = flushDenormal (2.0f * v2 - C.lp2);
                io[c][i] = y + lpMix * (v2 - y);
            }
            idx = (idx + 1) & (N - 1);
            if (++hopCount >= hop) { hopCount = 0; processFrame(); }
        }
    }

    float getAmount() const noexcept { return amount; }   // 0 clear .. 1 full heat
    float getCutoff() const noexcept { return cutoff; }

    // how long the heat takes to come in and to go away, in seconds (5 s to 5 min), like the snow's
    void setFadeTimes (float inSec, float outSec) noexcept
    {
        upStep = 1.0 / (std::clamp ((double) inSec, minFadeSec, maxFadeSec) * sr);
        downStep = 1.0 / (std::clamp ((double) outSec, minFadeSec, maxFadeSec) * sr);
    }
    static constexpr double minFadeSec = 5.0, maxFadeSec = 300.0, driftSec = 8.0;

private:
    struct Channel { std::vector<float> in, out, env; float lp1 = 0, lp2 = 0; double energy = 0.0; };

    void updateFilter() noexcept
    {
        // two slow swells (about 11 s and 29 s), so it never settles into a steady wobble
        lfoA += 32.0 * 0.09 / sr;  if (lfoA > 1.0) lfoA -= 1.0;
        lfoB += 32.0 * 0.034 / sr; if (lfoB > 1.0) lfoB -= 1.0;
        const double u = 0.6 * std::sin (kTwoPi * lfoA) + 0.4 * std::sin (kTwoPi * lfoB);   // -1..1
        cutoff = (float) (9200.0 * std::pow (2.0, 0.6 * u));                               // ~6.1 .. 14 kHz
        const double g = std::tan (kPi * std::min ((double) cutoff, sr * 0.45) / sr);
        const double k = 1.41421356;
        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) g * a1;
        a3 = (float) g * a2;
    }

    void processFrame() noexcept
    {
        // Loudness: each grain is scaled to carry the energy the plain (untouched) grain would have,
        // followed as slowly as the spectrum. Then, because grains with random phases add by power
        // rather than amplitude, the lot is lifted (2.03 in theory at 8x Hann overlap; 2.7 measured best:
        // a held tone comes out near 85 % and noise near 100 % before the lowpass, where 2.03 left them
        // at 65 % and 75 %). Matching per grain keeps tones and noise close; a fixed gain was 30 % apart.
        for (auto& c : ch)
        {
            double ecoh = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const float w = window[(size_t) i], x = c.in[(size_t) ((idx + i) & (N - 1))] * w;
                frame[(size_t) i] = x;
                ecoh += (double) (x * w) * (double) (x * w);
            }
            c.energy = fresh ? ecoh : c.energy + (double) follow * (ecoh - c.energy);
            fft->performRealOnlyForwardTransform (frame.data(), true);
            for (int k = 0; k < bins; ++k)
            {
                const size_t K = (size_t) k;
                const float re = frame[2 * K], im = frame[2 * K + 1];
                const float m = std::sqrt (re * re + im * im);
                c.env[K] = fresh ? m : flushDenormal (c.env[K] + follow * (m - c.env[K]));
                const float ph = (float) kPi * rng.bi();
                frame[2 * K]     = c.env[K] * std::cos (ph);
                frame[2 * K + 1] = c.env[K] * std::sin (ph);
            }
            frame[1] = 0.0f;
            frame[(size_t) (2 * (bins - 1) + 1)] = 0.0f;
            fft->performRealOnlyInverseTransform (frame.data());
            double ey = 0.0;
            for (int i = 0; i < N; ++i) { const float v = frame[(size_t) i] * window[(size_t) i]; ey += (double) v * (double) v; }
            const float g = ey > 1.0e-18 ? (float) std::min (40.0, 2.7 * std::sqrt (c.energy / ey)) : 0.0f;
            const float ola = g / 3.0f;   // Hann squared at 8x overlap sums to 3
            for (int i = 0; i < N; ++i) c.out[(size_t) ((idx + i) & (N - 1))] += frame[(size_t) i] * window[(size_t) i] * ola;
        }
        fresh = false;
    }

    double sr = 48000.0;
    int order = 14, N = 16384, hop = 2048, bins = 8193, idx = 0, hopCount = 0, ctl = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frame;
    Channel ch[2];
    float follow = 0.03f, amount = 0.0f, cutoff = 9200.0f, a1 = 0, a2 = 0, a3 = 0;
    double ramp = 0.0, upStep = 0.0, downStep = 0.0, lfoA = 0.0, lfoB = 0.0;
    bool fresh = true;
    Rng rng;
};
} // namespace fp

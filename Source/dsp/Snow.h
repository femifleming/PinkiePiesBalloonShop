#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Utils.h"
#include <vector>

namespace fp
{
//==============================================================================
// Snow: a master spectral freeze that comes in like frost.
//
// The output runs through an STFT (16384-point Hann at 48 kHz, 8x overlap) and is resynthesised
// with a phase vocoder. Tones (peaks standing well clear of their neighbourhood) are phase-locked:
// each bin keeps its phase relation to the peak it belongs to, so a tone holds pure and steady.
// Everything else (breath, noise, the wash between tones) gets a fresh random phase every frame
// as the freeze deepens, so a held noise is a smooth, still texture with no flutter or loop in it
// (a held phase there would beat against the overlapping frames and repeat at the frame rate).
//
// When the snow starts, the output hands over to the resynthesis in 40 ms (once it has heard
// a full window). At first it follows the live sound closely; then each bin's level and pitch
// follow it at a rate that slows as the snow builds, (1 - amount)^2 per frame, so the spectrum
// stiffens and stops: after the fade-in time (5 s to 5 min, set by the player) the amount reaches 1 and every bin is held, ringing at the
// pitch it last had, until the snow is switched off. (The handover is quick on purpose: the
// resynthesis runs about 0.34 s behind the live sound, and any crossfade between them comb-filters
// while they overlap, so it's kept to a moment.) Switching it off thaws it over the fade-out time (also 5 s to 5 min), the bins
// following the live sound again, then hands back in 40 ms. With no snow the output is untouched (bit-exact) and the STFT rests.
class SnowFreeze
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        order = sr <= 50000.0 ? 14 : (sr <= 100000.0 ? 15 : 16);   // 16384 at 44.1/48 kHz: a long, smooth window (about 0.34 s)
        N = 1 << order;
        hop = N / 8;
        bins = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / N));
        frame.assign ((size_t) (2 * N), 0.0f);
        omega.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k) omega[(size_t) k] = (float) (kTwoPi * k * hop / N);
        for (auto& c : ch)
        {
            c.in.assign ((size_t) N, 0.0f); c.out.assign ((size_t) N, 0.0f);
            for (auto* v : { &c.env, &c.adv, &c.prevPhase, &c.outPhase, &c.refPhase }) v->assign ((size_t) bins, 0.0f);
            c.peakOf.assign ((size_t) bins, 0);
            c.tonal.assign ((size_t) bins, 0);
        }
        setFadeTimes (5.0f, 5.0f);
        xfStep = (float) (1.0 / (0.04 * sr));
        idx = 0; hopCount = 0; ramp = 0.0; amount = 0.0f; xf = 0.0f; framesHeard = 0; fresh = true;
    }

    void process (float* L, float* R, int n, bool on) noexcept
    {
        if (! on && amount <= 0.0f && xf <= 0.0f)
        {
            // resting: keep the input ring filled so the first frame has something to hear
            for (int i = 0; i < n; ++i)
            {
                ch[0].in[(size_t) idx] = L[i]; ch[1].in[(size_t) idx] = R[i];
                ch[0].out[(size_t) idx] = 0.0f; ch[1].out[(size_t) idx] = 0.0f;
                idx = (idx + 1) & (N - 1);
            }
            fresh = true;
            framesHeard = 0;
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            // counted in double: a 5 min fade moves 1/14,400,000 a sample, below a float's step near 1
            ramp = on ? std::min (1.0, ramp + upStep) : std::max (0.0, ramp - downStep);
            amount = (float) ramp;
            // hand over to the resynthesis once it has a full window (8 frames), hand back once thawed
            if ((on || amount > 0.0f) && framesHeard >= 8) xf = std::min (1.0f, xf + xfStep);
            else if (! on && amount <= 0.0f) xf = std::max (0.0f, xf - xfStep);
            const float wet = std::sin (xf * (float) kPi * 0.5f), dry = std::cos (xf * (float) kPi * 0.5f);
            const float wl = ch[0].out[(size_t) idx], wr = ch[1].out[(size_t) idx];
            ch[0].out[(size_t) idx] = 0.0f; ch[1].out[(size_t) idx] = 0.0f;
            ch[0].in[(size_t) idx] = L[i]; ch[1].in[(size_t) idx] = R[i];
            idx = (idx + 1) & (N - 1);
            L[i] = dry * L[i] + wet * wl;
            R[i] = dry * R[i] + wet * wr;
            if (++hopCount >= hop) { hopCount = 0; processFrame(); ++framesHeard; }
        }
    }

    float getAmount() const noexcept { return amount; }   // 0 clear .. 1 frozen solid

    // how long the freeze takes to come in and to thaw, in seconds (5 s to 5 min). The amount
    // moves in a straight line, so changing a time halfway carries on from where it is.
    void setFadeTimes (float inSec, float outSec) noexcept
    {
        upStep = 1.0 / (std::clamp ((double) inSec, minFadeSec, maxFadeSec) * sr);
        downStep = 1.0 / (std::clamp ((double) outSec, minFadeSec, maxFadeSec) * sr);
    }
    static constexpr double minFadeSec = 5.0, maxFadeSec = 300.0;

private:
    struct Channel { std::vector<float> in, out, env, adv, prevPhase, outPhase, refPhase; std::vector<int> peakOf; std::vector<char> tonal; };

    static float wrap (float p) noexcept { return p - (float) kTwoPi * std::floor ((p + (float) kPi) / (float) kTwoPi); }

    void processFrame() noexcept
    {
        const float follow = fresh ? 1.0f : (1.0f - amount) * (1.0f - amount);   // how much of the live spectrum gets through
        for (auto& c : ch)
        {
            for (int i = 0; i < N; ++i) frame[(size_t) i] = c.in[(size_t) ((idx + i) & (N - 1))] * window[(size_t) i];
            fft->performRealOnlyForwardTransform (frame.data(), true);
            for (int k = 0; k < bins; ++k)
            {
                const size_t K = (size_t) k;
                const float re = frame[2 * K], im = frame[2 * K + 1];
                const float m = std::sqrt (re * re + im * im), ph = std::atan2 (im, re);
                if (fresh)
                {
                    // the first frame after resting: no earlier phase to measure a pitch from yet
                    c.env[K] = m; c.adv[K] = omega[K]; c.refPhase[K] = ph; c.outPhase[K] = ph;
                }
                else
                {
                    const float a = omega[K] + wrap (ph - c.prevPhase[K] - omega[K]);   // this bin's true phase advance
                    if (follow > 0.0f)
                    {
                        c.env[K] = flushDenormal (c.env[K] + follow * (m - c.env[K]));
                        c.adv[K] += follow * (a - c.adv[K]);
                        c.refPhase[K] = ph;   // how this bin sits against its neighbours, as last heard
                    }
                }
                c.prevPhase[K] = ph;
            }
            if (! fresh)
            {
                // each bin belongs to the nearest peak below or above it; the peaks carry the phase on
                int lastPeak = 0;
                for (int k = 0; k < bins; ++k)
                {
                    const float e = c.env[(size_t) k];
                    const bool peak = (k == 0 || e > c.env[(size_t) k - 1]) && (k == bins - 1 || e >= c.env[(size_t) k + 1]);
                    if (peak) lastPeak = k;
                    c.peakOf[(size_t) k] = lastPeak;
                }
                int nextPeak = bins - 1;
                for (int k = bins - 1; k >= 0; --k)
                {
                    if (c.peakOf[(size_t) k] == k) { nextPeak = k; continue; }
                    const int below = c.peakOf[(size_t) k];
                    if (nextPeak - k < k - below && c.env[(size_t) nextPeak] > 0.0f) c.peakOf[(size_t) k] = nextPeak;
                }
                // is each peak a tone? it must stand 12 dB clear of the average around it (+-24 bins)
                {
                    double run = 0.0;
                    const int R = 24;
                    for (int k = 0; k < std::min (bins, R); ++k) run += c.env[(size_t) k];
                    for (int k = 0; k < bins; ++k)
                    {
                        if (k + R < bins) run += c.env[(size_t) (k + R)];
                        if (k - R - 1 >= 0) run -= c.env[(size_t) (k - R - 1)];
                        const int span = std::min (bins - 1, k + R) - std::max (0, k - R) + 1;
                        c.tonal[(size_t) k] = (char) (c.peakOf[(size_t) k] == k && c.env[(size_t) k] > 4.0f * (float) (run / span));
                    }
                }
                const float scatter = amount * amount * (float) kPi;   // how much a non-tone's phase is thrown each frame
                for (int k = 0; k < bins; ++k)
                    if (c.peakOf[(size_t) k] == k)
                    {
                        float a = c.adv[(size_t) k];
                        if (! c.tonal[(size_t) k]) a += scatter * rng.bi();
                        c.outPhase[(size_t) k] = wrap (c.outPhase[(size_t) k] + a);
                    }
                for (int k = 0; k < bins; ++k)
                {
                    const int pk = c.peakOf[(size_t) k];
                    if (pk == k) continue;
                    if (c.tonal[(size_t) pk]) c.outPhase[(size_t) k] = wrap (c.outPhase[(size_t) pk] + c.refPhase[(size_t) k] - c.refPhase[(size_t) pk]);
                    else c.outPhase[(size_t) k] = wrap (c.outPhase[(size_t) pk] + c.refPhase[(size_t) k] - c.refPhase[(size_t) pk] + scatter * rng.bi());
                }
            }
            // overlapping frames with random phases add up by power, not amplitude: about half as loud at
            // 8x overlap (sqrt of the sum of Hann^4 against the sum of Hann^2), so the scattered parts
            // are lifted by as much as they're scattered
            const float comp = 1.0f + 1.03f * amount * amount;
            for (int k = 0; k < bins; ++k)
            {
                const size_t K = (size_t) k;
                const float g = (fresh || c.tonal[(size_t) c.peakOf[K]]) ? 1.0f : comp;
                frame[2 * K]     = g * c.env[K] * std::cos (c.outPhase[K]);
                frame[2 * K + 1] = g * c.env[K] * std::sin (c.outPhase[K]);
            }
            frame[1] = 0.0f;
            frame[(size_t) (2 * (bins - 1) + 1)] = 0.0f;
            fft->performRealOnlyInverseTransform (frame.data());
            const float ola = 1.0f / 3.0f;   // Hann squared at 8x overlap sums to 3
            for (int i = 0; i < N; ++i) c.out[(size_t) ((idx + i) & (N - 1))] += frame[(size_t) i] * window[(size_t) i] * ola;
        }
        fresh = false;
    }

    double sr = 48000.0;
    int order = 14, N = 16384, hop = 2048, bins = 8193, idx = 0, hopCount = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frame, omega;
    Channel ch[2];
    float amount = 0.0f, xf = 0.0f, xfStep = 0.0f;
    double ramp = 0.0, upStep = 0.0, downStep = 0.0;
    int framesHeard = 0;
    bool fresh = true;
    Rng rng;
};
} // namespace fp

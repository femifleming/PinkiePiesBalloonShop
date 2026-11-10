#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Utils.h"

namespace fp
{
//==============================================================================
// Spectral gate + smear.
//
// STFT (Hann, 4x overlap). Each bin is gated against a cutoff relative to the
// loudest bin of the frame (soft-knee expander, so no hard "musical noise"),
// then its magnitude is smeared in time (fast attack, very slow release) and
// resynthesised with a phase-vocoder: every bin keeps rotating at the last
// frequency it heard while it was open. Result: the input is followed and melted
// into smooth sustained spectral tones.
//
// Processing is sample-by-sample (FIFO in/out), so it works at any host block size.
class SpectralGate
{
public:
    void prepare (double sampleRate)
    {
        sr    = sampleRate;
        order = sr <= 50000.0 ? 12 : (sr <= 100000.0 ? 13 : 14);
        N     = 1 << order;
        hop   = N / 4;
        bins  = N / 2 + 1;
        fft   = std::make_unique<juce::dsp::FFT> (order);

        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / N)); // periodic Hann

        frame.assign ((size_t) (2 * N), 0.0f);
        for (auto& c : ch) c.prepare (N, bins);

        omega.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k)
            omega[(size_t) k] = (float) (kTwoPi * k * hop / N);

        hopSec      = (double) hop / sr;
        gateCoef    = (float) (1.0 - std::exp (-hopSec / 0.030));
        magNorm     = 4.0f / (float) N; // full-scale sine -> 1.0
        blurBuf.assign ((size_t) bins, 0.0f);
        tiltGain.assign ((size_t) bins, 1.0f);
        lastAttack = lastSmear = lastTilt = -1.0e9f;
        setAttackMs (45.0f); setSmearSeconds (1.8f); setTiltDb (0.0f);
        reset();
    }

    void reset()
    {
        for (auto& c : ch) c.clear();
        idx = 0; hopCount = 0;
        fz = 0.0f; captured = false; followFrames = 0; frozenEnergy = 0.0f;
    }

    void setCutoffDb (float db) noexcept { cutoffLin = std::pow (10.0f, db / 20.0f); }

    // how fast an opening bin swells up
    void setAttackMs (float ms) noexcept
    {
        if (ms == lastAttack) return;
        lastAttack = ms;
        attackCoef = (float) (1.0 - std::exp (-hopSec / std::max (0.0005, (double) ms * 0.001)));
    }
    // how long a bin keeps ringing after it closes (the smear)
    void setSmearSeconds (float s) noexcept
    {
        if (s == lastSmear) return;
        lastSmear = s;
        releaseCoef = (float) (1.0 - std::exp (-hopSec / std::max (0.01, (double) s)));
    }
    // expander slope below the cutoff: 1 = gentle fade, 8 = hard gate
    void setKnee (float k) noexcept { knee = std::clamp (k, 0.5f, 12.0f); }
    // smoothing of the smeared spectrum across neighbouring bins (0..1)
    void setBlur (float b) noexcept { blur = std::clamp (b, 0.0f, 1.0f); }
    // random phase jitter per bin per frame: tones dissolve into breath (0..1)
    void setScatter (float s) noexcept { scatter = std::clamp (s, 0.0f, 1.0f); }
    // spectral freeze: hold the current smeared spectrum and keep resynthesising it.
    // Engaging/releasing crossfades between live and frozen over ~250 ms.
    void setFreeze (bool on) noexcept { freezeOn = on; }
    float getFreezeAmount() const noexcept { return fz; }

    // spectral tilt around 1 kHz, dB per octave: above 0 the highs come up and the lows go down,
    // below 0 the other way round. It's level-matched (equal loudness per octave, as pink noise
    // has), so the tilt moves the balance without making the smear louder or quieter.
    void setTiltDb (float dbPerOct) noexcept
    {
        if (std::abs (dbPerOct - lastTilt) < 0.01f) return;
        if (tiltGain.empty()) return; // not prepared yet; prepare() re-applies
        lastTilt = dbPerOct;
        double num = 0.0, den = 0.0;
        for (int k = 0; k < (int) tiltGain.size(); ++k)
        {
            const double f = std::max (30.0, (double) k * sr / (double) N);
            const double gDb = std::clamp ((double) dbPerOct * std::log2 (f / 1000.0), -30.0, 30.0);
            const double g = std::pow (10.0, gDb / 20.0);
            tiltGain[(size_t) k] = (float) g;
            if (k > 0) { num += 1.0 / f; den += g * g / f; }
        }
        const double norm = (dbPerOct == 0.0f || den <= 0.0) ? 1.0 : std::sqrt (num / den);
        for (auto& g : tiltGain) g = (float) (g * norm);
    }

    // returns the wet (smeared) signal; latency is part of the smear character
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        outL = ch[0].out[(size_t) idx]; ch[0].out[(size_t) idx] = 0.0f; ch[0].in[(size_t) idx] = inL;
        outR = ch[1].out[(size_t) idx]; ch[1].out[(size_t) idx] = 0.0f; ch[1].in[(size_t) idx] = inR;
        idx = (idx + 1) & (N - 1);

        if (++hopCount >= hop)
        {
            hopCount = 0;
            processFrame();
        }
    }

private:
    struct Channel
    {
        std::vector<float> in, out, prevPhase, outPhase, adv, env, gate, mag, ph;
        std::vector<float> fMag, fAdv, fPhase; // the frozen spectrum

        void prepare (int n, int b)
        {
            fMag.assign ((size_t) b, 0.0f); fAdv.assign ((size_t) b, 0.0f); fPhase.assign ((size_t) b, 0.0f);
            in.assign ((size_t) n, 0.0f); out.assign ((size_t) n, 0.0f);
            prevPhase.assign ((size_t) b, 0.0f); outPhase.assign ((size_t) b, 0.0f);
            adv.assign ((size_t) b, 0.0f); env.assign ((size_t) b, 0.0f);
            gate.assign ((size_t) b, 0.0f); mag.assign ((size_t) b, 0.0f); ph.assign ((size_t) b, 0.0f);
        }
        void clear()
        {
            for (auto* v : { &in, &out, &prevPhase, &outPhase, &env, &gate, &mag, &ph, &fMag, &fPhase })
                std::fill (v->begin(), v->end(), 0.0f);
        }
    };

    static float wrap (float p) noexcept
    {
        return p - (float) kTwoPi * std::floor ((p + (float) kPi) / (float) kTwoPi);
    }

    void processFrame() noexcept
    {
        // 1) analyse both channels
        float peak = 0.0f;
        for (auto& c : ch)
        {
            for (int i = 0; i < N; ++i)
                frame[(size_t) i] = c.in[(size_t) ((idx + i) & (N - 1))] * window[(size_t) i];
            fft->performRealOnlyForwardTransform (frame.data(), true);

            for (int k = 0; k < bins; ++k)
            {
                const float re = frame[(size_t) (2 * k)], im = frame[(size_t) (2 * k + 1)];
                const float m = std::sqrt (re * re + im * im);
                c.mag[(size_t) k] = m;
                c.ph[(size_t) k]  = std::atan2 (im, re);
                peak = std::max (peak, m);
            }
        }

        // linked threshold: relative to loudest bin, never below an absolute floor
        const float thr = std::max (peak * magNorm * cutoffLin, 1.0e-4f) / magNorm; // floor = -80 dBFS

        // freeze state (shared by both channels)
        const float fzStep = (float) (hopSec / 0.25);
        // capture. If what we caught is silence (e.g. freeze was already on at load),
        // keep trying until sound arrives, then follow it for ~170 ms so we hold the
        // sound itself rather than its first faint frame.
        if (freezeOn)
        {
            const bool silent = captured && frozenEnergy < 1.0e-3f;
            if (silent) followFrames = 8;
            if (! captured || silent || followFrames > 0)
            {
                if (captured && ! silent) --followFrames;
                frozenEnergy = 0.0f;
                for (auto& c : ch)
                {
                    c.fMag = c.env; c.fAdv = c.adv; c.fPhase = c.outPhase;
                    for (auto v : c.env) frozenEnergy += v * magNorm;
                }
                captured = true;
            }
        }
        else followFrames = 0;
        fz = freezeOn ? std::min (1.0f, fz + fzStep) : std::max (0.0f, fz - fzStep);
        if (! freezeOn && fz <= 0.0f) captured = false;
        const float liveGain = 1.0f - fz;

        // 2) gate, smear, resynthesise
        for (auto& c : ch)
        {
            for (int k = 0; k < bins; ++k)
            {
                const size_t K = (size_t) k;
                const float m = c.mag[K];

                // soft-knee expander below the cutoff, smoothed per bin
                const float r = m / thr;
                const float g = r > 1.0e-9f ? 1.0f / (1.0f + std::pow (r, -knee)) : 0.0f;
                c.gate[K] += gateCoef * (g - c.gate[K]);

                // instantaneous frequency of this bin
                const float dphi = wrap (c.ph[K] - c.prevPhase[K] - omega[K]);
                c.prevPhase[K] = c.ph[K];
                if (c.gate[K] > 0.25f)
                    c.adv[K] = omega[K] + dphi;  // remember the tone while the gate is open
                else if (c.env[K] < 1.0e-9f)
                    c.adv[K] = omega[K];

                // magnitude smear: fast attack, long release
                const float target = m * c.gate[K] * tiltGain[K];
                const float coef   = target > c.env[K] ? attackCoef : releaseCoef;
                c.env[K] = flushDenormal (c.env[K] + coef * (target - c.env[K]));
                blurBuf[K] = c.env[K];
            }

            blurInPlace (blurBuf);

            const float scat = scatter * scatter * (float) kPi;
            for (int k = 0; k < bins; ++k)
            {
                const size_t K = (size_t) k;
                float advance = c.adv[K];
                if (scat > 0.0f) advance += scat * rng.bi();
                c.outPhase[K] = wrap (c.outPhase[K] + advance);
                frame[2 * K]     = liveGain * blurBuf[K] * std::cos (c.outPhase[K]);
                frame[2 * K + 1] = liveGain * blurBuf[K] * std::sin (c.outPhase[K]);
            }

            if (captured && fz > 0.0f)
            {
                blurBuf = c.fMag;
                blurInPlace (blurBuf);
                for (int k = 0; k < bins; ++k)
                {
                    const size_t K = (size_t) k;
                    float advance = c.fAdv[K];
                    if (scat > 0.0f) advance += scat * rng.bi();
                    c.fPhase[K] = wrap (c.fPhase[K] + advance);
                    frame[2 * K]     += fz * blurBuf[K] * std::cos (c.fPhase[K]);
                    frame[2 * K + 1] += fz * blurBuf[K] * std::sin (c.fPhase[K]);
                }
            }
            frame[1] = 0.0f;                         // DC has no imaginary part
            frame[(size_t) (2 * (bins - 1) + 1)] = 0.0f; // nor does Nyquist

            fft->performRealOnlyInverseTransform (frame.data());

            const float olaGain = 0.7f / 1.5f; // sum of Hann^2 at 4x overlap (x0.7 level match)
            for (int i = 0; i < N; ++i)
                c.out[(size_t) ((idx + i) & (N - 1))] += frame[(size_t) i] * window[(size_t) i] * olaGain;
        }
    }

    // blur: forward + backward one-pole smoothing across bins (keeps the mean)
    void blurInPlace (std::vector<float>& v) const noexcept
    {
        if (blur <= 0.001f) return;
        const float a = 1.0f - 0.97f * blur;
        float s = v[0];
        for (int k = 0; k < bins; ++k) { s += a * (v[(size_t) k] - s); v[(size_t) k] = s; }
        s = v[(size_t) bins - 1];
        for (int k = bins - 1; k >= 0; --k) { s += a * (v[(size_t) k] - s); v[(size_t) k] = s; }
    }

    double sr = 48000.0;
    bool freezeOn = false, captured = false;
    float fz = 0.0f, frozenEnergy = 0.0f;
    int followFrames = 0;
    int order = 12, N = 4096, hop = 1024, bins = 2049;
    int idx = 0, hopCount = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frame, omega, blurBuf, tiltGain;
    Channel ch[2];
    Rng rng;
    double hopSec = 0.02;
    float cutoffLin = 0.05f, attackCoef = 0.1f, releaseCoef = 0.01f, gateCoef = 0.3f, magNorm = 1.0f;
    float knee = 4.0f, blur = 0.0f, scatter = 0.0f;
    float lastAttack = -1.0f, lastSmear = -1.0f, lastTilt = -1.0e9f;
};
} // namespace fp

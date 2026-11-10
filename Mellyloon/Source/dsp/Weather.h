#pragma once
#include "Utils.h"

namespace fp
{
//==============================================================================
// The master-output weather.
//   rain   : a gentle 2-pole lowpass, sweeping down to ~6 kHz as the storm builds
//   helium : a gentle 2-pole highpass, rising to ~200 Hz
//   clouds : thunder. The rain's lowpass comes in too (wide open), and rolls of thunder
//            pull its cutoff down at random, fluttering as they rumble away. A clap()
//            (after a lightning strike) brings a big roll a moment later.
// Each switch eases in and out over ~1 s (so it never clicks and the balloons can
// follow it). The filters always run so their state stays continuous; when all three
// amounts are back at zero the output is bit-identical to the input.
class Weather
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        coef = (float) (1.0 - std::exp (-1.0 / (0.35 * sr)));   // ~1 s to settle
        reset();
    }

    void reset()
    {
        for (auto& s : lp) s = {};
        for (auto& s : hp) s = {};
        rainAmt = heliumAmt = cloudAmt = 0.0f;
        roll = thunderEnv = thunderDepth = flutter = flutterTarget = 0.0f;
        clapIn = -1; flutterIn = 0;
        counter = 0;
        update();
    }

    // a lightning strike: its thunder arrives after 'delaySamples', loudness 0..1
    void clap (int delaySamples, float loudness) noexcept { clapIn = std::max (0, delaySamples); clapSize = loudness; }

    void process (float* L, float* R, int n, bool rainOn, bool heliumOn, bool cloudOn = false) noexcept
    {
        const float rainT = rainOn ? 1.0f : 0.0f, heT = heliumOn ? 1.0f : 0.0f, clT = cloudOn ? 1.0f : 0.0f;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            rainAmt += coef * (rainT - rainAmt);
            heliumAmt += coef * (heT - heliumAmt);
            cloudAmt += coef * (clT - cloudAmt);
            if (rainT == 0.0f && rainAmt < 1.0e-4f) rainAmt = 0.0f;
            if (heT == 0.0f && heliumAmt < 1.0e-4f) heliumAmt = 0.0f;
            if (clT == 0.0f && cloudAmt < 1.0e-4f) cloudAmt = 0.0f;
            if (++counter >= 32) { counter = 0; thunderTick(); update(); }

            const float lpAmt = std::max (rainAmt, cloudAmt);
            const float wl = smoothstep (lpAmt * 3.0f), wh = smoothstep (heliumAmt * 3.0f);
            for (int c = 0; c < 2; ++c)
            {
                // in series: rain's lowpass, then helium's highpass (both always running)
                const float x  = ch[c][i];
                const float lo = lp[c].process (x, lpC, true);
                const float y1 = lpAmt > 0.0f ? x + wl * (lo - x) : x;
                const float hy = hp[c].process (y1, hpC, false);
                ch[c][i] = heliumAmt > 0.0f ? y1 + wh * (hy - y1) : y1;
            }
        }
    }

    float getRain() const noexcept   { return rainAmt; }
    float getHelium() const noexcept { return heliumAmt; }
    float getClouds() const noexcept { return cloudAmt; }
    float getThunder() const noexcept { return thunderDepth; }   // how far the thunder has the cutoff down (0..1)

private:
    struct Coefs { float a1 = 0, a2 = 0, a3 = 0, k = 1.4142f; };

    // TPT state-variable filter (Zavalishin), one per channel and per filter
    struct SVF
    {
        float ic1 = 0, ic2 = 0;
        float process (float x, const Coefs& c, bool lowpass) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = c.a1 * ic1 + c.a2 * v3;
            const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = flushDenormal (2.0f * v1 - ic1);
            ic2 = flushDenormal (2.0f * v2 - ic2);
            return lowpass ? v2 : x - c.k * v1 - v2;
        }
    };

    static Coefs design (double sr, double f) noexcept
    {
        Coefs c;
        const double g = std::tan (kPi * std::clamp (f, 10.0, sr * 0.45) / sr);
        c.k  = 1.41421356f;                                   // Butterworth
        c.a1 = (float) (1.0 / (1.0 + g * (g + c.k)));
        c.a2 = (float) g * c.a1;
        c.a3 = (float) g * c.a2;
        return c;
    }

    // thunder, at the control rate (every 32 samples): rolls that swell fast and rumble away
    // over a couple of seconds, with a random flutter riding on them
    void thunderTick() noexcept
    {
        const double tick = 32.0 / sr;
        if (clapIn >= 0)
        {
            clapIn -= 32;
            if (clapIn < 0) roll = std::max (roll, clapSize);
        }
        // distant rumbles now and then while it's cloudy (one every ~9 s on average)
        if (cloudAmt > 0.5f && rand01() < (float) (tick / 9.0))
            roll = std::max (roll, 0.25f + 0.35f * rand01());
        if (cloudAmt <= 0.0f) roll = 0.0f;
        roll *= (float) std::exp (-tick / 1.7);
        thunderEnv += (roll > thunderEnv ? (float) (1.0 - std::exp (-tick / 0.06)) : 1.0f) * (roll - thunderEnv);
        if ((flutterIn -= 32) <= 0)
        {
            flutterIn = (int) (sr * (0.05 + 0.12 * rand01()));
            flutterTarget = rand01();
        }
        flutter += (float) (1.0 - std::exp (-tick / 0.04)) * (flutterTarget - flutter);
        thunderDepth = flushDenormal (thunderEnv * (0.55f + 0.45f * flutter) * cloudAmt);
    }

    float rand01() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) * (1.0f / 16777216.0f);
    }

    void update() noexcept
    {
        // the cutoffs travel with the amounts: rain darkens further as it builds, thunder pulls it
        // down further still (to a tenth at the height of a roll)
        lpC = design (sr, 16000.0 * std::pow (6000.0 / 16000.0, (double) rainAmt) * std::pow (0.1, (double) thunderDepth));
        hpC = design (sr, 20.0 * std::pow (200.0 / 20.0, (double) heliumAmt));
    }

    double sr = 48000.0;
    float coef = 0.0001f, rainAmt = 0.0f, heliumAmt = 0.0f, cloudAmt = 0.0f;
    float roll = 0.0f, thunderEnv = 0.0f, thunderDepth = 0.0f, flutter = 0.0f, flutterTarget = 0.0f, clapSize = 1.0f;
    int clapIn = -1, flutterIn = 0;
    uint32_t seed = 0x5eed1234u;
    int counter = 0;
    SVF lp[2], hp[2];
    Coefs lpC, hpC;
};
} // namespace fp

#pragma once
#include "Utils.h"

namespace fp
{
//==============================================================================
// Zero-latency delay-line pitch shifter ("rotating tape heads"): two read heads,
// half a window apart, each faded with sin^2 so their gains always sum to 1.
// Hermite interpolation on reads. Feedback re-injects the shifted signal into
// the line, so each repeat climbs/falls by another scale step.
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        windowLen = (float) (0.065 * sr);
        for (auto& d : line) d.prepare ((int) (windowLen + 16));
        for (int c = 0; c < 2; ++c)
        {
            fbLP[c].setup (sr, 9000.0);
            fbHP[c].setHighpass (sr, 60.0, 0.707);
            dc[c].setup (sr, 8.0);
        }
        logRatio.setup (sr, 0.02);
        logRatio.snap (0.0f);
        reset();
    }

    void reset()
    {
        for (auto& d : line) d.reset();
        for (int c = 0; c < 2; ++c) { fbLP[c].reset(); fbHP[c].reset(); dc[c].reset(); fbState[c] = 0.0f; }
        phase = 0.0f;
    }

    // ratio as log2 (octaves). Glides over 20 ms so scale steps never click.
    void setPitchOctaves (float oct) noexcept { logRatio.set (oct); }

    void process (float inL, float inR, float feedback, float& outL, float& outR) noexcept
    {
        const float ratio = std::exp2 (logRatio.next());

        // write input + feedback
        const float in[2] = { inL, inR };
        for (int c = 0; c < 2; ++c)
            line[c].push (in[c] + feedback * fbState[c]);

        // heads move through the window at (1 - ratio) samples per sample
        phase += (1.0f - ratio) / windowLen;
        phase -= std::floor (phase);

        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;

        const float d1 = minDelay + phase * windowLen;
        const float d2 = minDelay + p2 * windowLen;
        const float s1 = std::sin ((float) kPi * phase);
        const float s2 = std::sin ((float) kPi * p2);
        const float g1 = s1 * s1, g2 = s2 * s2; // g1 + g2 == 1

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            out[c] = g1 * line[c].read (d1) + g2 * line[c].read (d2);
            // feedback path: gentle band-limit + saturation keeps it musical and bounded
            float f = fbHP[c].process (fbLP[c].process (dc[c].process (out[c])));
            fbState[c] = softClip (f);
        }
        outL = out[0]; outR = out[1];
    }

private:
    double sr = 48000.0;
    float windowLen = 3000.0f, minDelay = 4.0f, phase = 0.0f;
    DelayLine line[2];
    OnePoleLP fbLP[2];
    Biquad fbHP[2];
    DCBlocker dc[2];
    float fbState[2] {};
    Ramp logRatio;
};
} // namespace fp

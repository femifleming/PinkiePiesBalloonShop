#pragma once
#include "Utils.h"
#include <atomic>
#include <vector>
#include <algorithm>

namespace fp
{
//==============================================================================
// The gumball machine: a generative stutter on the main output, after the ideas in Mesker's
// "Analysis and recreation of key features in selected Autechre tracks 1998-2005" (2007).
//
// A clock runs in 16th notes (the host's tempo and position when it's playing, otherwise its
// own at the last tempo it knew, 120 to start with). On every step, with a probability set by
// 'stutter', an event happens, and its type is drawn from weights (the thesis's event-type
// table), leaning towards straight rolls or towards bouncing, grainy events with 'bounce':
//
//   rolls of 2, 3, 4 or 5   the start of the step repeated to fill it
//   a 1/32 delay            the step with an echo half a step behind it (a flam)
//   recursive division      halves, then quarters, eighths, sixteenths of the step: a roll
//                           that keeps speeding up for two steps
//   bounce                  a bouncing ball: each repeat comes 0.82 times as soon as the last,
//                           from a dotted 16th down into audio rate, so it sweeps up into a
//                           pitch (Gantz Graf), a little quieter each time
//   expand                  the same, backwards: from a buzz down to slow repeats
//   freeze                  one 20 to 80 ms grain looped for a step or two: a held, pitched
//                           buzz with the attack gone (V-Proc)
//   jump                    the step replaced by another step from the last bar
//
// 'cut-up' is Booth's restarting patterns: how often an event takes its sound from a different
// step of the last bar instead of the current one, which rearranges the bar like a breakbeat.
// 'sway' is the swing lookup table: it holds back every second 16th, up to nearly a full step.
//
// It sits on the shelf with the other effects, so it can stutter before or after any of them.
// Everything is causal (it only ever replays what it has already heard), so there's no
// latency. Grains are windowed at their edges (1.5 ms), and with 'stutter' at zero and no
// event running the output is untouched.
class Stutter
{
public:
    enum Type { roll2 = 0, roll3, roll4, roll5, delay32, recursive, bounce, expand, freeze, jump, numTypes };
    // on: bought at the balloon store. Off, it fades out over 30 ms and then leaves the sound untouched.
    struct Settings { float amount = 0.0f, bounce = 0.5f, sway = 0.0f, cutup = 0.25f; bool on = true; };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        int size = 1;
        while (size < (int) (sr * 8.0)) size <<= 1;
        mask = size - 1;
        for (auto& b : ring) b.assign ((size_t) size, 0.0f);
        fadeLen = std::max (16, (int) (0.0015 * sr));
        presenceStep = (float) (1.0 / (0.03 * sr));
        presence = set.on ? 1.0f : 0.0f;
        intervals.reserve (512); gains.reserve (512);
        rng.seed (0x57A7);
        now = 0; pos16 = 0.0; lastStep = -1; active = false; tail = 0; bpmKnown = 120.0;
    }

    // once per block. ppq < 0, or not playing: the stutter keeps its own clock
    void begin (const Settings& s, double bpm, double ppq, bool playing) noexcept
    {
        set = s;
        if (bpm > 20.0 && bpm < 999.0) bpmKnown = bpm;
        inc = bpmKnown / 60.0 * 4.0 / sr;                    // 16ths per sample
        stepLen = std::max (32, (int) (sr * 60.0 / bpmKnown / 4.0));
        if (playing && ppq >= 0.0) pos16 = ppq * 4.0;
    }

    // one sample, wherever it sits in the chain
    void tick (float& l, float& r) noexcept
    {
        const float xl = l, xr = r;
        ring[0][(size_t) (now & mask)] = xl;
        ring[1][(size_t) (now & mask)] = xr;

        // the swayed step grid: every second 16th is held back by up to 0.45 of a step
        const double f = std::floor (pos16);
        int64_t k = (int64_t) f;
        if ((k & 1) && (pos16 - f) < 0.45 * (double) set.sway) --k;
        if (k != lastStep)
        {
            lastStep = k;
            if (set.on && set.amount > 0.0f) onStep (set);
        }
        pos16 += inc;

        if (! set.on && presence <= 0.0f)
        {
            active = false; tail = 0;   // put away: the sound goes straight through
            ++now;
            return;
        }

        if (active) render (xl, xr, l, r);
        else if (tail > 0)
        {
            const float g = 1.0f - (float) tail / (float) fadeLen;   // the dry sound comes back in
            l = xl * g; r = xr * g;
            --tail;
        }
        // bought or given back: a 30 ms crossfade between the stutter and the plain sound
        if (! set.on || presence < 1.0f)
        {
            presence = std::clamp (presence + (set.on ? presenceStep : -presenceStep), 0.0f, 1.0f);
            l = xl + presence * (l - xl);
            r = xr + presence * (r - xr);
        }
        ++now;
    }

    void process (float* L, float* R, int n, const Settings& s, double bpm, double ppq, bool playing) noexcept
    {
        begin (s, bpm, ppq, playing);
        for (int i = 0; i < n; ++i) tick (L[i], R[i]);
    }

    std::atomic<int> eventCount { 0 }, typesSeen { 0 }, lastType { -1 };

private:
    void onStep (const Settings& s) noexcept
    {
        if (active || rng.uni() >= s.amount) return;

        const float b = std::clamp (s.bounce, 0.0f, 1.0f), cut = std::clamp (s.cutup, 0.0f, 1.0f);
        const float w[numTypes] = { (1.0f - b) * 1.0f, (1.0f - b) * 0.7f, (1.0f - b) * 0.6f, (1.0f - b) * 0.4f, (1.0f - b) * 0.5f,
                                    0.25f + 0.5f * b, b * 1.0f, b * 0.6f, b * 0.6f, cut * 1.2f };
        float total = 0.0f;
        for (float v : w) total += v;
        if (total <= 0.0f) return;
        float pick = rng.uni() * total;
        int ti = 0;
        while (ti < numTypes - 1 && pick >= w[ti]) { pick -= w[ti]; ++ti; }
        type = (Type) ti;

        // where the sound comes from: this step (heard live first), or another step of the last bar
        const bool elsewhere = type == jump || rng.uni() < cut;
        src = elsewhere ? now - (int64_t) stepLen * (1 + rng.integer (15)) : now;
        liveFirst = ! elsewhere;
        intervals.clear(); gains.clear();
        const int S = stepLen;
        auto add = [this] (int len, float g) { if (len > 0) { intervals.push_back (len); gains.push_back (g); } };

        switch (type)
        {
            case roll2: case roll3: case roll4: case roll5:
            {
                const int k = 2 + (int) type;
                for (int j = 0; j < k; ++j) add (j == k - 1 ? S - (k - 1) * (S / k) : S / k, 1.0f);
                break;
            }
            case delay32: add (S, 1.0f); break;
            case recursive:
                for (int level = 1; level <= 4; ++level)
                {
                    const int len = S >> level, count = 1 << (level - 1);
                    for (int j = 0; j < count * 2 && (int) intervals.size() < 64; ++j) add (len, 1.0f);
                }
                break;
            case bounce: case expand:
            {
                float len = 0.75f * (float) S, g = 1.0f;
                const float stop = (float) (0.0015 * sr);
                while (len > stop && intervals.size() < 400) { add ((int) len, g); len *= 0.82f; g *= 0.97f; }
                if (type == expand)
                {
                    std::reverse (intervals.begin(), intervals.end());
                    std::reverse (gains.begin(), gains.end());
                    // a slowing-down event reads from the past, so even its longest grain has been heard
                    src = now - (int64_t) intervals.back() - (elsewhere ? (int64_t) stepLen * (1 + rng.integer (14)) : 0);
                    liveFirst = false;
                }
                break;
            }
            case freeze:
            {
                const int width = std::max (8, (int) (sr * (0.020 + 0.060 * rng.uni())));
                const int steps = 1 + rng.integer (2);
                for (int j = 0; j < (S * steps) / width && j < 400; ++j) add (width, 1.0f);
                if (! elsewhere) src = now - width;
                liveFirst = false;
                break;
            }
            case jump: add (S, 1.0f); break;
            default: break;
        }
        if (intervals.empty()) return;
        active = true; grain = 0; tPos = 0;
        eventCount.fetch_add (1);
        typesSeen.fetch_or (1 << (int) type);
        lastType.store ((int) type);
    }

    float window (int pos, int len) const noexcept
    {
        const int fl = std::min (fadeLen, std::max (1, len / 3));
        if (pos < fl) return (float) (pos + 1) / (float) fl;
        if (pos >= len - fl) return (float) (len - pos) / (float) fl;
        return 1.0f;
    }

    float readRing (int c, int64_t at) const noexcept { return ring[c][(size_t) (at & mask)]; }

    void render (float xl, float xr, float& yl, float& yr) noexcept
    {
        const int len = intervals[(size_t) grain];
        if (type == delay32)
        {
            // the step goes on as it is, with itself half a step later on top
            const int64_t back = now - (int64_t) (stepLen / 2);
            const float e = tPos < fadeLen ? (float) tPos / (float) fadeLen : (tPos >= len - fadeLen ? (float) (len - tPos) / (float) fadeLen : 1.0f);
            yl = xl + 0.55f * e * readRing (0, back);
            yr = xr + 0.55f * e * readRing (1, back);
        }
        else if (grain == 0 && liveFirst)
        {
            // the first time through it's the live sound, fading out only where the repeats take over
            const int fl = std::min (fadeLen, std::max (1, len / 3));
            const float g = (intervals.size() > 1 && tPos >= len - fl) ? (float) (len - tPos) / (float) fl : 1.0f;
            yl = xl * g; yr = xr * g;
        }
        else
        {
            const float g = gains[(size_t) grain] * window (tPos, len);
            yl = g * readRing (0, src + tPos);
            yr = g * readRing (1, src + tPos);
        }
        if (++tPos >= len)
        {
            tPos = 0;
            if (++grain >= (int) intervals.size()) { active = false; tail = fadeLen; }
        }
    }

    double sr = 48000.0, pos16 = 0.0, bpmKnown = 120.0, inc = 0.0;
    Settings set;
    std::vector<float> ring[2];
    int64_t mask = 0, now = 0, src = 0, lastStep = -1;
    float presence = 1.0f, presenceStep = 0.001f;
    int fadeLen = 72, stepLen = 6000, grain = 0, tPos = 0, tail = 0;
    bool active = false, liveFirst = true;
    Type type = roll2;
    std::vector<int> intervals;
    std::vector<float> gains;
    Rng rng;
};
} // namespace fp

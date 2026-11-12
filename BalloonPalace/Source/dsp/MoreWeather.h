#pragma once
#include "Utils.h"
#include <array>
#include <vector>
#include <atomic>

namespace fp
{
//==============================================================================
// More weather: fog, wind, hail, the rainbow after rain, the aurora and the twister.
//
// Three of them change the sound that's playing and work on any number of channels at once, every
// channel through exactly the same filter and the same moving delay (so in the sphere the
// Ambisonic picture stays where it is: only time and colour change):
//
//   fog      six short allpasses in a row blur every attack (about a tenth of a second of smear,
//            no tail), and the highs sink to 4.5 kHz
//   wind     gusts: the sound is read through a delay that keeps drifting (a few cents sharp, then
//            flat, up to about 20 cents in a strong gust) and swells a little with each gust
//   twister  a whirlwind for 10 to 20 s: the pitch climbs a fifth and falls back while it spins
//
// The other three make new sound out of what's playing (mono in, separate outputs so the stereo
// shop can pan them and the sphere can place them):
//
//   hail     hailstones: tiny slices of the last quarter second, each one hit hard and then
//            bouncing, quicker and quieter, like a ball dropped on a table
//   rainbow  when the rain stops: seven octave bands (red 110 Hz .. violet 7 kHz), each arriving a
//            little after the last, for about 25 s
//   aurora   the sound an octave up, ringing in a long reverb whose four curtains swell and fade
//
// Every one of them does nothing at all while it's off (the output is the input, bit for bit).

// a TPT state-variable bandpass, peak gain 1
struct SkyBandpass
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    void setup (double sr, float hz, float q)
    {
        const float g = std::tan ((float) kPi * std::min (hz, (float) sr * 0.45f) / (float) sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    float process (float x) noexcept
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (2.0f * v1 - ic1); ic2 = flushDenormal (2.0f * v2 - ic2);
        return k * v1;
    }
};

//==============================================================================
class Fog
{
public:
    static constexpr int kStages = 6;

    void prepare (double sampleRate, int maxChannels, int maxBlock)
    {
        sr = sampleRate;
        coef = (float) (1.0 - std::exp (-1.0 / (0.35 * sr)));
        static const float ms[kStages] = { 3.1f, 5.3f, 7.9f, 11.3f, 14.9f, 19.1f };
        for (int k = 0; k < kStages; ++k) len[k] = std::max (2, (int) (ms[k] * 0.001 * sr));
        chans.assign ((size_t) std::max (1, maxChannels), {});
        for (auto& c : chans)
            for (int k = 0; k < kStages; ++k) c.line[k].assign ((size_t) len[k], 0.0f);
        wBuf.assign ((size_t) std::max (1, maxBlock), 0.0f);
        aBuf.assign ((size_t) std::max (1, maxBlock), 1.0f);
        reset();
    }

    void reset()
    {
        clear();
        amt = 0.0f; counter = 0; a = 1.0f;
    }

    float amount() const noexcept { return amt; }

    void process (float* const* x, int numCh, int n, bool on) noexcept
    {
        if (! on && amt == 0.0f) return;
        if (amt == 0.0f) clear();   // (coming in from rest: nothing stale in the lines)
        numCh = std::min (numCh, (int) chans.size());
        if ((int) wBuf.size() < n) { wBuf.resize ((size_t) n); aBuf.resize ((size_t) n); }
        const float target = on ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            amt += coef * (target - amt);
            if (! on && amt < 1.0e-4f) amt = 0.0f;
            if (++counter >= 32 || i == 0)
            {
                counter = 0;
                const float hz = 18000.0f * std::pow (4500.0f / 18000.0f, amt);
                a = 1.0f - std::exp (-(float) kTwoPi * std::min (hz, (float) sr * 0.45f) / (float) sr);
            }
            wBuf[(size_t) i] = smoothstep (amt * 1.5f);
            aBuf[(size_t) i] = a;
        }
        constexpr float g = 0.6f;
        for (int c = 0; c < numCh; ++c)
        {
            auto& ch = chans[(size_t) c];
            int p[kStages];
            for (int k = 0; k < kStages; ++k) p[k] = pos[k];
            float lp = ch.lp;
            float* d = x[c];
            for (int i = 0; i < n; ++i)
            {
                float v = d[i];
                for (int k = 0; k < kStages; ++k)
                {
                    float& cell = ch.line[k][(size_t) p[k]];
                    const float del = cell;
                    const float w = v + g * del;
                    cell = flushDenormal (w);
                    if (++p[k] >= len[k]) p[k] = 0;
                    v = del - g * w;
                }
                lp += aBuf[(size_t) i] * (v - lp);
                const float w = wBuf[(size_t) i];
                if (w > 0.0f) d[i] += w * (lp - d[i]);
            }
            ch.lp = flushDenormal (lp);
        }
        for (int k = 0; k < kStages; ++k) pos[k] = (pos[k] + n) % len[k];
    }

private:
    void clear()
    {
        for (auto& c : chans) { for (auto& l : c.line) std::fill (l.begin(), l.end(), 0.0f); c.lp = 0.0f; }
        for (auto& p : pos) p = 0;
    }
    struct Chan { std::vector<float> line[kStages]; float lp = 0.0f; };
    double sr = 48000.0;
    std::vector<Chan> chans;
    std::vector<float> wBuf, aBuf;
    int len[kStages] {}, pos[kStages] {};
    float amt = 0.0f, coef = 0.0001f, a = 1.0f;
    int counter = 0;
};

//==============================================================================
class Wind
{
public:
    void prepare (double sampleRate, int maxChannels, int maxBlock)
    {
        sr = sampleRate;
        coef = (float) (1.0 - std::exp (-1.0 / (0.35 * sr)));
        size = 1;
        while (size < (int) (0.05 * sr) + 8) size <<= 1;
        rings.assign ((size_t) std::max (1, maxChannels), std::vector<float> ((size_t) size, 0.0f));
        for (auto* b : { &wBuf, &dBuf, &gBuf, &lBuf }) b->assign ((size_t) std::max (1, maxBlock), 0.0f);
        rng.seed (0x9057);
        reset();
    }

    void reset()
    {
        for (auto& r : rings) std::fill (r.begin(), r.end(), 0.0f);
        w = 0; amt = 0.0f; counter = 0;
        roll = env = 0.0f; leanNow = leanTarget = 0.0f; headingNow = 0.0f; headingVel = 0.0f;
        delay = delayTarget = (float) (0.012 * sr); vel = 0.0f; holdLeft = 0.0f; gain = 1.0f; gainTarget = 1.0f;
    }

    float amount() const noexcept { return amt; }
    float gust() const noexcept { return amt * std::min (1.0f, 0.15f + env); }   // how hard it's blowing now (0..1)
    float lean() const noexcept { return amt * leanNow; }                         // -1 left .. 1 right
    float heading() const noexcept { return headingNow; }                         // where it blows to (radians, wandering)

    void process (float* const* x, int numCh, int n, bool on) noexcept
    {
        if (! on && amt == 0.0f) return;
        if (amt == 0.0f) { for (auto& r : rings) std::fill (r.begin(), r.end(), 0.0f); }
        numCh = std::min (numCh, (int) rings.size());
        if ((int) wBuf.size() < n) for (auto* b : { &wBuf, &dBuf, &gBuf, &lBuf }) b->resize ((size_t) n);
        const float target = on ? 1.0f : 0.0f;
        const float lo = (float) (0.002 * sr), hi = (float) (0.030 * sr);
        for (int i = 0; i < n; ++i)
        {
            amt += coef * (target - amt);
            if (! on && amt < 1.0e-4f) amt = 0.0f;
            if (++counter >= 32) { counter = 0; tick(); }
            delay = std::clamp (delay + vel, lo, hi);
            gain += 0.0005f * (gainTarget - gain);
            wBuf[(size_t) i] = smoothstep (amt * 1.5f);
            dBuf[(size_t) i] = delay;
            gBuf[(size_t) i] = gain;
            lBuf[(size_t) i] = amt * leanNow;
        }
        const int m = size - 1;
        for (int c = 0; c < numCh; ++c)
        {
            auto& r = rings[(size_t) c];
            float* d = x[c];
            int wp = w;
            for (int i = 0; i < n; ++i)
            {
                r[(size_t) wp] = d[i];
                const float pos = (float) wp - dBuf[(size_t) i];
                const float f = std::floor (pos);
                const int k = (int) f;
                const float wet = gBuf[(size_t) i] * hermite (r[(size_t) ((k - 1) & m)], r[(size_t) (k & m)], r[(size_t) ((k + 1) & m)], r[(size_t) ((k + 2) & m)], pos - f);
                const float mix = wBuf[(size_t) i];
                if (mix > 0.0f) d[i] += mix * (wet - d[i]);
                wp = (wp + 1) & m;
            }
        }
        w = (w + n) & m;
        lastN = n;
    }

    // the stereo shop: the gust leans the sound to one side (call after process, same n)
    void leanStereo (float* L, float* R, int n) const noexcept
    {
        if (amt == 0.0f || n > lastN) return;
        for (int i = 0; i < n; ++i)
        {
            const float l = lBuf[(size_t) i];
            if (l == 0.0f) continue;
            L[i] *= 1.0f - 0.35f * std::max (0.0f, l) + 0.12f * std::max (0.0f, -l);
            R[i] *= 1.0f - 0.35f * std::max (0.0f, -l) + 0.12f * std::max (0.0f, l);
        }
    }

private:
    // at the control rate (every 32 samples): gusts that swell in and die away, and the delay's drift
    void tick() noexcept
    {
        const float dt = (float) (32.0 / sr);
        if (rng.uni() < dt / 3.5f) { roll = std::max (roll, 0.4f + 0.6f * rng.uni()); leanTarget = rng.bi(); }
        roll *= std::exp (-dt / 2.2f);
        env += (roll > env ? 1.0f - std::exp (-dt / 0.5f) : 1.0f) * (roll - env);
        const float g = std::min (1.0f, 0.15f + env);
        leanNow += (1.0f - std::exp (-dt / 0.6f)) * (leanTarget * g - leanNow);
        headingVel += rng.bi() * 0.4f * dt;
        headingVel = std::clamp (headingVel, -0.25f, 0.25f);
        headingNow += headingVel * dt;
        if ((holdLeft -= dt) <= 0.0f)
        {
            holdLeft = rng.range (0.3f, 1.4f);
            delayTarget = (float) sr * rng.range (0.004f, 0.028f);
        }
        const float maxV = 0.012f * (0.25f + 0.75f * g);   // (samples per sample: 1.2 % is about 20 cents)
        const float want = std::clamp ((delayTarget - delay) / (0.5f * (float) sr), -maxV, maxV);
        vel += (1.0f - std::exp (-dt / 0.12f)) * (want - vel);
        gainTarget = 1.0f + 0.25f * (g - 0.3f);
    }

    double sr = 48000.0;
    std::vector<std::vector<float>> rings;
    std::vector<float> wBuf, dBuf, gBuf, lBuf;
    int size = 1, w = 0, counter = 0, lastN = 0;
    float amt = 0.0f, coef = 0.0001f;
    float roll = 0.0f, env = 0.0f, leanNow = 0.0f, leanTarget = 0.0f, headingNow = 0.0f, headingVel = 0.0f;
    float delay = 500.0f, delayTarget = 500.0f, vel = 0.0f, holdLeft = 0.0f, gain = 1.0f, gainTarget = 1.0f;
    Rng rng;
};

//==============================================================================
class Twister
{
public:
    void prepare (double sampleRate, int maxChannels, int maxBlock)
    {
        sr = sampleRate;
        window = (float) (0.06 * sr);
        size = 1;
        while (size < (int) window + 16) size <<= 1;
        rings.assign ((size_t) std::max (1, maxChannels), std::vector<float> ((size_t) size, 0.0f));
        for (auto* b : { &aBuf, &pBuf, &sBuf }) b->assign ((size_t) std::max (1, maxBlock), 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& r : rings) std::fill (r.begin(), r.end(), 0.0f);
        w = 0; left = len = 0; amt = 0.0f; ph = 0.0f; ang = 0.0f;
    }

    // one starts, 'seconds' long (ignored while one is already turning)
    bool start (float seconds) noexcept
    {
        if (left > 0) return false;
        for (auto& r : rings) std::fill (r.begin(), r.end(), 0.0f);
        len = left = std::max (1, (int) (seconds * sr));
        ph = 0.0f;
        ++count;
        return true;
    }

    bool active() const noexcept { return left > 0; }
    int numStarted() const noexcept { return count; }
    float amount() const noexcept { return amt; }                 // 0..1: how far it has you
    float progress() const noexcept { return len > 0 ? 1.0f - (float) left / (float) len : 0.0f; }
    float angle() const noexcept { return ang; }                  // how far round it has spun (radians, keeps growing)

    void process (float* const* x, int numCh, int n) noexcept
    {
        lastN = 0;
        if (left <= 0) { amt = 0.0f; return; }
        numCh = std::min (numCh, (int) rings.size());
        if ((int) aBuf.size() < n) for (auto* b : { &aBuf, &pBuf, &sBuf }) b->resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float a = 0.0f;
            if (left > 0)
            {
                --left;
                const float u = 1.0f - (float) left / (float) len;
                const float out = smoothstep ((1.0f - u) / 0.15f);
                a = std::min (smoothstep (u / 0.12f), out);
                const float semis = 7.0f * std::pow (u, 1.3f) * out;
                const float ratio = std::exp2 (semis / 12.0f);
                ph -= (ratio - 1.0f) / window;
                if (ph < 0.0f) ph += 1.0f;
                ang += (float) kTwoPi * (0.3f + 5.0f * u * u * out) / (float) sr;
            }
            amt = a;
            aBuf[(size_t) i] = a;
            pBuf[(size_t) i] = ph;
            sBuf[(size_t) i] = std::sin (ang);
        }
        const int m = size - 1;
        for (int c = 0; c < numCh; ++c)
        {
            auto& r = rings[(size_t) c];
            float* d = x[c];
            int wp = w;
            auto tap = [&] (float phase, int at)
            {
                const float pos = (float) at - (2.0f + phase * window);
                const float f = std::floor (pos);
                const int k = (int) f;
                return (0.5f - 0.5f * std::cos ((float) kTwoPi * phase))
                       * hermite (r[(size_t) ((k - 1) & m)], r[(size_t) (k & m)], r[(size_t) ((k + 1) & m)], r[(size_t) ((k + 2) & m)], pos - f);
            };
            for (int i = 0; i < n; ++i)
            {
                r[(size_t) wp] = d[i];
                const float a = aBuf[(size_t) i];
                if (a > 0.0f)
                {
                    const float p1 = pBuf[(size_t) i], p2 = p1 < 0.5f ? p1 + 0.5f : p1 - 0.5f;
                    d[i] += a * (tap (p1, wp) + tap (p2, wp) - d[i]);
                }
                wp = (wp + 1) & m;
            }
        }
        w = (w + n) & m;
        lastN = n;
    }

    // the stereo shop: it whirls from side to side, faster and faster (call after process, same n)
    void spinStereo (float* L, float* R, int n) const noexcept
    {
        if (n > lastN) return;
        for (int i = 0; i < n; ++i)
        {
            const float a = aBuf[(size_t) i];
            if (a <= 0.0f) continue;
            const float s = sBuf[(size_t) i], up = 1.0f + 0.3f * a;
            L[i] *= up * (1.0f - 0.8f * a * (0.5f + 0.5f * s));
            R[i] *= up * (1.0f - 0.8f * a * (0.5f - 0.5f * s));
        }
    }

private:
    double sr = 48000.0;
    std::vector<std::vector<float>> rings;
    std::vector<float> aBuf, pBuf, sBuf;
    int size = 1, w = 0, left = 0, len = 0, count = 0, lastN = 0;
    float window = 2880.0f, amt = 0.0f, ph = 0.0f, ang = 0.0f;
};

//==============================================================================
class Hail
{
public:
    static constexpr int kStones = 8;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        size = 1;
        while (size < (int) (2.0 * sr) + maxBlock + 8) size <<= 1;
        ring.assign ((size_t) size, 0.0f);
        for (auto& o : outs) o.assign ((size_t) std::max (1, maxBlock), 0.0f);
        coef = (float) (1.0 - std::exp (-(double) std::max (1, maxBlock) / (0.35 * sr)));
        rng.seed (0x4A11);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        wAbs = 0; amt = 0.0f;
        for (auto& s : stones) s = Stone {};
    }

    float amount() const noexcept { return amt; }
    bool anyActive() const noexcept { for (auto& s : stones) if (s.on) return true; return false; }
    bool active (int k) const noexcept { return stones[(size_t) k].on; }
    const float* out (int k) const noexcept { return outs[(size_t) k].data(); }
    float pan (int k) const noexcept { return stones[(size_t) k].pan; }          // -1 .. 1
    float azimuth (int k) const noexcept { return stones[(size_t) k].az; }       // radians
    float height (int k) const noexcept { return stones[(size_t) k].height; }    // 1 just landed .. 0 its last bounce
    bool fresh (int k) const noexcept { return stones[(size_t) k].fresh; }       // started this block
    std::atomic<int> hits { 0 };   // every bounce counts (for the pictures)

    // n samples of what's playing (mono); afterwards out(k) holds each active stone's n samples
    void process (const float* in, int n, bool on) noexcept
    {
        for (int i = 0; i < n; ++i) ring[(size_t) ((wAbs + i) & (size - 1))] = in[i];
        wAbs += n;
        amt += coef * ((on ? 1.0f : 0.0f) - amt);
        if (! on && amt < 1.0e-3f) amt = 0.0f;
        for (auto& s : stones) s.fresh = false;
        if (amt == 0.0f && ! anyActive()) return;
        if ((int) outs[0].size() < n) for (auto& o : outs) o.resize ((size_t) n);

        // a new stone now and then (about two a second when it's really coming down)
        if (on && rng.uni() < amt * 2.2f * (float) n / (float) sr)
            for (auto& s : stones)
                if (! s.on)
                {
                    s = Stone {};
                    s.on = s.fresh = true;
                    s.len = (int) (sr * rng.range (0.005f, 0.030f));
                    static const float rates[] = { 0.5f, 1.0f, 1.0f, 1.5f, 2.0f };
                    s.rate = rates[rng.integer (5)];
                    s.start = wAbs - (int64_t) ((float) s.len * s.rate) - 8 - (int64_t) (sr * rng.range (0.0f, 0.2f));
                    s.bounces = 4 + rng.integer (5);
                    s.gap = (float) sr * rng.range (0.09f, 0.22f);
                    s.ratio = rng.range (0.62f, 0.78f);
                    s.pan = rng.bi(); s.az = rng.uni() * (float) kTwoPi;
                    s.nextIn = rng.integer (std::max (1, n));
                    s.height = 1.0f;
                    break;
                }

        const float attack = 1.0f / (float) (0.0005 * sr);
        for (int k = 0; k < kStones; ++k)
        {
            auto& s = stones[(size_t) k];
            if (! s.on) continue;
            float* o = outs[(size_t) k].data();
            for (int i = 0; i < n; ++i)
            {
                if (--s.nextIn <= 0 && s.readPos < 0)
                {
                    if (s.hit < s.bounces)
                    {
                        s.readPos = 0;
                        s.hitGain = 0.9f * std::pow (0.72f, (float) s.hit);
                        s.nextIn = (int) (s.gap * std::pow (s.ratio, (float) s.hit));
                        s.height = 1.0f - (float) s.hit / (float) s.bounces;
                        ++s.hit;
                        hits.fetch_add (1, std::memory_order_relaxed);
                    }
                    else s.done = true;
                }
                float y = 0.0f;
                if (s.readPos >= 0)
                {
                    const float t = (float) s.readPos / (float) s.len;
                    const float e = std::min (1.0f, (float) s.readPos * attack) * (1.0f - t) * (1.0f - t);
                    const float p = (float) s.readPos * s.rate;
                    const float f = std::floor (p);
                    const int64_t a = s.start + (int64_t) f;
                    auto at = [&] (int64_t q) { return ring[(size_t) (q & (int64_t) (size - 1))]; };
                    y = s.hitGain * e * hermite (at (a - 1), at (a), at (a + 1), at (a + 2), p - f);
                    if (++s.readPos >= s.len) s.readPos = -1;
                }
                o[i] = y;
            }
            if (s.done && s.readPos < 0) s.on = false;
        }
    }

private:
    struct Stone
    {
        bool on = false, fresh = false, done = false;
        int len = 1, readPos = -1, nextIn = 0, hit = 0, bounces = 5;
        int64_t start = 0;
        float rate = 1.0f, gap = 4800.0f, ratio = 0.7f, hitGain = 0.0f, pan = 0.0f, az = 0.0f, height = 1.0f;
    };
    double sr = 48000.0;
    std::vector<float> ring;
    std::array<std::vector<float>, kStones> outs;
    std::array<Stone, kStones> stones {};
    int size = 1;
    int64_t wAbs = 0;
    float amt = 0.0f, coef = 0.01f;
    Rng rng;
};

//==============================================================================
class Rainbow
{
public:
    static constexpr int kBands = 7;
    static float bandHz (int k) noexcept { return 110.0f * std::exp2 ((float) k); }   // red .. violet
    static constexpr float gapSeconds = 0.09f;   // each colour this much after the last

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        size = 1;
        while (size < (int) (gapSeconds * kBands * sr) + maxBlock + 8) size <<= 1;
        ring.assign ((size_t) size, 0.0f);
        for (auto& o : outs) o.assign ((size_t) std::max (1, maxBlock), 0.0f);
        for (int k = 0; k < kBands; ++k) bp[(size_t) k].setup (sr, bandHz (k), 1.2f);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& b : bp) b.reset();
        w = 0; left = len = 0; env = 0.0f;
    }

    void start (float seconds) noexcept
    {
        if (left <= 0) for (auto& b : bp) b.reset();
        len = std::max (1, (int) (seconds * sr));
        if (left <= 0) left = len;                       // (one already out: it just carries on)
        ++count;
    }

    bool active() const noexcept { return left > 0; }
    float amount() const noexcept { return env; }
    int numStarted() const noexcept { return count; }
    const float* band (int k) const noexcept { return outs[(size_t) k].data(); }

    // n samples of what's playing (mono); afterwards, while it's active, band(k) holds n samples each
    void process (const float* in, int n) noexcept
    {
        const int m = size - 1;
        for (int i = 0; i < n; ++i) ring[(size_t) ((w + i) & m)] = in[i];
        if (left <= 0) { w = (w + n) & m; env = 0.0f; return; }
        if ((int) outs[0].size() < n) for (auto& o : outs) o.resize ((size_t) n);
        const int gap = (int) (gapSeconds * sr);
        for (int i = 0; i < n; ++i)
        {
            float e = 0.0f;
            if (left > 0)
            {
                --left;
                const float u = 1.0f - (float) left / (float) len;
                e = smoothstep (u / 0.1f) * smoothstep ((1.0f - u) / 0.3f);
            }
            env = e;
            for (int k = 0; k < kBands; ++k)
                outs[(size_t) k][(size_t) i] = 0.6f * e * bp[(size_t) k].process (ring[(size_t) ((w + i - gap * k) & m)]);
        }
        w = (w + n) & m;
    }

private:
    double sr = 48000.0;
    std::vector<float> ring;
    std::array<std::vector<float>, kBands> outs;
    std::array<SkyBandpass, kBands> bp;
    int size = 1, w = 0, left = 0, len = 0, count = 0;
    float env = 0.0f;
};

//==============================================================================
class Aurora
{
public:
    static constexpr int kCurtains = 4;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        window = (float) (0.05 * sr);
        size = 1;
        while (size < (int) window + 16) size <<= 1;
        ring.assign ((size_t) size, 0.0f);
        static const float ms[kCurtains] = { 61.3f, 79.9f, 97.1f, 113.7f };
        for (int j = 0; j < kCurtains; ++j)
        {
            line[(size_t) j].assign ((size_t) std::max (8, (int) (ms[j] * 0.001 * sr)), 0.0f);
            g[j] = std::pow (10.0f, -3.0f * (float) line[(size_t) j].size() / (float) sr / 9.0f);   // 9 s to die away
            outs[(size_t) j].assign ((size_t) std::max (1, maxBlock), 0.0f);
        }
        damp = 1.0f - std::exp (-(float) kTwoPi * 6000.0f / (float) sr);
        hp.setHighpass (sr, 500.0, 0.707);
        coef = (float) (1.0 - std::exp (-1.0 / (1.2 * sr)));
        rng.seed (0xA020);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        for (int j = 0; j < kCurtains; ++j) { pos[j] = 0; lp[j] = 0.0f; cur[j] = 0.5f; curTarget[j] = 0.5f; curLeft[j] = 0.0f; }
        hp.reset();
        w = 0; ph = 0.0f; amt = 0.0f; quietFor = 1 << 30; counter = 0;
    }

    float amount() const noexcept { return amt; }
    bool active() const noexcept { return amt > 0.0f || quietFor < (int) (0.5 * sr); }
    const float* out (int j) const noexcept { return outs[(size_t) j].data(); }
    float curtain (int j) const noexcept { return cur[j]; }   // how bright each curtain is now (0..1)

    // n samples of what's playing (mono); afterwards, while it's active, out(j) holds each curtain's n samples
    void process (const float* in, int n, bool on) noexcept
    {
        const int m = size - 1;
        if (! on && ! active()) { for (int i = 0; i < n; ++i) ring[(size_t) ((w + i) & m)] = in[i]; w = (w + n) & m; return; }
        if ((int) outs[0].size() < n) for (auto& o : outs) o.resize ((size_t) n);
        const float target = on ? 1.0f : 0.0f;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            amt += coef * (target - amt);
            if (! on && amt < 1.0e-4f) amt = 0.0f;
            if (++counter >= 64) { counter = 0; curtains ((float) (64.0 / sr)); }

            // an octave up: two taps half a window apart, each read twice as fast as it's written
            ring[(size_t) w] = in[i];
            ph -= 1.0f / window;
            if (ph < 0.0f) ph += 1.0f;
            auto tap = [&] (float phase)
            {
                const float p = (float) w - (2.0f + phase * window);
                const float f = std::floor (p);
                const int k = (int) f;
                return (0.5f - 0.5f * std::cos ((float) kTwoPi * phase))
                       * hermite (ring[(size_t) ((k - 1) & m)], ring[(size_t) (k & m)], ring[(size_t) ((k + 1) & m)], ring[(size_t) ((k + 2) & m)], p - f);
            };
            const float up = hp.process (tap (ph) + tap (ph < 0.5f ? ph + 0.5f : ph - 0.5f)) * amt;
            w = (w + 1) & m;

            // four lines mixing into each other (a Hadamard matrix), the highs damped as they go round
            float o[kCurtains];
            for (int j = 0; j < kCurtains; ++j) { o[j] = line[(size_t) j][(size_t) pos[j]]; lp[j] += damp * (o[j] - lp[j]); }
            const float h[kCurtains] = { 0.5f * (lp[0] + lp[1] + lp[2] + lp[3]), 0.5f * (lp[0] - lp[1] + lp[2] - lp[3]),
                                         0.5f * (lp[0] + lp[1] - lp[2] - lp[3]), 0.5f * (lp[0] - lp[1] - lp[2] + lp[3]) };
            for (int j = 0; j < kCurtains; ++j)
            {
                line[(size_t) j][(size_t) pos[j]] = flushDenormal (g[j] * h[j] + ((j & 1) ? -up : up) * 0.5f);
                if (++pos[j] >= (int) line[(size_t) j].size()) pos[j] = 0;
                lp[j] = flushDenormal (lp[j]);
                const float y = 0.45f * o[j] * (0.2f + 0.8f * cur[j]);
                outs[(size_t) j][(size_t) i] = y;
                peak = std::max (peak, std::abs (y));
            }
        }
        quietFor = peak > 1.0e-5f || on ? 0 : std::min (1 << 30, quietFor + n);
    }

private:
    // each curtain's brightness wanders to a new place every few seconds
    void curtains (float dt) noexcept
    {
        for (int j = 0; j < kCurtains; ++j)
        {
            if ((curLeft[j] -= dt) <= 0.0f) { curLeft[j] = rng.range (2.0f, 7.0f); curTarget[j] = rng.uni(); }
            cur[j] += (1.0f - std::exp (-dt / 1.5f)) * (curTarget[j] - cur[j]);
        }
    }

    double sr = 48000.0;
    std::vector<float> ring;
    std::array<std::vector<float>, kCurtains> line, outs;
    int pos[kCurtains] {};
    float g[kCurtains] {}, lp[kCurtains] {}, cur[kCurtains] {}, curTarget[kCurtains] {}, curLeft[kCurtains] {};
    Biquad hp;
    int size = 1, w = 0, quietFor = 1 << 30, counter = 0;
    float window = 2400.0f, ph = 0.0f, amt = 0.0f, coef = 0.0001f, damp = 0.5f;
    Rng rng;
};
} // namespace fp

#pragma once
#include "Utils.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace fp
{
//==============================================================================
// The sonic rainboom: something flies so fast that a sonic boom and a rainbow happen at once.
//
//   * the dive: before the boom your sound is squeezed, the way a sound coming at you faster and
//     faster would be. It is read back through a delay that shrinks quicker and quicker (a real
//     Doppler shift, rising about five semitones by the end), with a rush of air under it whose
//     band climbs from 300 Hz to 6 kHz.
//   * the boom: a real sonic boom is an N-wave: the pressure jumps up, falls in a straight line to
//     as far below, and jumps back (the bow and tail shocks, heard as a double crack). Here it is
//     160 ms long, its corners softened a little, with a short crack of noise at each shock.
//   * the rainbow: the sound split into seven octave bands, red the lowest (110 Hz) to violet the
//     highest (7 kHz), the order of the colours in light. The ring reaches you one colour after
//     another (red first, the outside of a rainbow), so the colours sweep up the spectrum; each one
//     lights up your sound in its band for a moment, pings once, then keeps ringing in its own small
//     reverb and fades over the rainbow's length: the rainbow left in the sky.
//
// Mono in, separate outputs (the dive, the boom, each band) so the stereo shop can pan them and
// the sphere can place them in 3-D. Nothing runs while it's quiet.
class Rainboom
{
public:
    static constexpr int kBands = 7;
    static float bandHz (int k) noexcept { return 110.0f * std::exp2 ((float) k); }   // red .. violet

    struct Settings
    {
        float tailSeconds = 5.0f;   // how long the rainbow rings on
        float spread = 1.0f;        // how far apart the colours arrive (1: 35 ms each)
    };

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        ringSize = 1;
        while (ringSize < (int) (0.6 * sr) + maxBlock + 8) ringSize <<= 1;
        ring.assign ((size_t) ringSize, 0.0f);
        const int mb = std::max (1, maxBlock);
        diveOut.assign ((size_t) mb, 0.0f);
        boomOut.assign ((size_t) mb, 0.0f);
        for (auto& b : bandOut) b.assign ((size_t) mb, 0.0f);
        for (auto& b : bandSide) b.assign ((size_t) mb, 0.0f);
        for (int k = 0; k < kBands; ++k)
        {
            auto& bd = bands[(size_t) k];
            bd.svf.setup (sr, bandHz (k), 1.6f);
            const float scale = 1.0f + 0.045f * (float) k;
            const float ms[4] = { 29.7f, 37.1f, 41.3f, 53.9f };
            for (int j = 0; j < 4; ++j)
            {
                const int len = std::max (8, (int) (ms[j] * scale * 0.001 * sr));
                bd.line[(size_t) j].assign ((size_t) len, 0.0f);
                bd.pos[(size_t) j] = 0;
            }
            bd.damp = 1.0f - std::exp (-(float) kTwoPi * std::min (bandHz (k) * 3.0f, 18000.0f) / (float) sr);
        }
        soft = 1.0f - std::exp (-(float) kTwoPi * 2500.0f / (float) sr);
        pingDecay = std::exp (-1.0f / (0.008f * (float) sr));
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        w = 0;
        diveLeft = diveLen = 0;
        sinceBoom = 1 << 30;
        quietFor = 1 << 30;
        for (auto& bd : bands)
        {
            bd.svf.reset();
            for (auto& l : bd.line) std::fill (l.begin(), l.end(), 0.0f);
            bd.lp.fill (0.0f);
            bd.ping = 0.0f;
        }
        whoosh.reset();
        lpA = lpB = 0.0f;
    }

    // start a rainboom: a dive of 'diveSeconds' (0: the boom at once). Ignored while one is already
    // diving. Returns true if it started.
    bool fire (float diveSeconds) noexcept
    {
        if (diveLeft > 0) return false;
        diveLen = diveLeft = std::max (0, (int) (diveSeconds * sr));
        if (diveLeft > 0) ++dives;
        if (diveLeft == 0) startBoom();
        quietFor = 0;
        return true;
    }

    bool diving() const noexcept { return diveLeft > 0; }
    int numBooms() const noexcept { return booms; }   // (counted, for the pictures)
    int numDives() const noexcept { return dives; }
    float diveSecondsLeft() const noexcept { return (float) diveLeft / (float) sr; }
    float diveProgress() const noexcept { return diveLen > 0 ? 1.0f - (float) diveLeft / (float) diveLen : 1.0f; }
    double secondsSinceBoom() const noexcept { return (double) sinceBoom / sr; }
    bool active() const noexcept { return quietFor < (int) (0.5 * sr) || diveLeft > 0; }

    // n samples of the sound it colours (mono). Afterwards dive(), boom() and band(k) hold n samples
    // each, and bandSide(k) the band's reverb in a second, decorrelated version (for width).
    void process (const float* in, int n, const Settings& s) noexcept
    {
        if ((int) diveOut.size() < n)   // (a host sending more than it said it would)
        {
            diveOut.resize ((size_t) n); boomOut.resize ((size_t) n);
            for (auto& b : bandOut) b.resize ((size_t) n);
            for (auto& b : bandSide) b.resize ((size_t) n);
        }
        for (int i = 0; i < n; ++i) { ring[(size_t) w] = in[i]; w = (w + 1) & (ringSize - 1); }
        const bool was = active();
        std::fill (diveOut.begin(), diveOut.begin() + n, 0.0f);
        std::fill (boomOut.begin(), boomOut.begin() + n, 0.0f);
        for (int k = 0; k < kBands; ++k)
        {
            std::fill (bandOut[(size_t) k].begin(), bandOut[(size_t) k].begin() + n, 0.0f);
            std::fill (bandSide[(size_t) k].begin(), bandSide[(size_t) k].begin() + n, 0.0f);
        }
        if (! was) { sinceBoom += n; return; }

        const float tail = std::clamp (s.tailSeconds, 0.5f, 20.0f);
        const float gap = 0.035f * std::clamp (s.spread, 0.0f, 3.0f);
        double peak = 0.0;
        for (auto& bd : bands)   // each line's decay for the rainbow's length (T60)
            for (int j = 0; j < 4; ++j) bd.g[(size_t) j] = std::pow (10.0f, -3.0f * (float) bd.line[(size_t) j].size() / (float) sr / tail);
        for (int i = 0; i < n; ++i)
        {
            // the dive
            if (diveLeft > 0)
            {
                const float u = 1.0f - (float) diveLeft / (float) diveLen;
                const float T = (float) diveLen / (float) sr;
                const float D0 = std::min (0.25f, 0.18f * T);
                const float delay = D0 * (1.0f - u * u) * (float) sr;   // shrinking faster and faster
                const float x = read ((float) (w - n + i) - delay - 2.0f);
                const float air = whoosh.process (noise() * 0.5f, std::min (300.0f * std::pow (20.0f, std::pow (u, 1.5f)), (float) sr * 0.4f), (float) sr);
                diveOut[(size_t) i] = x * std::pow (u, 1.5f) * 0.9f + air * u * u * u * 0.5f;
                if (--diveLeft == 0) startBoom();
            }

            // the boom: an N-wave (+1 falling to -1 over 160 ms), corners softened, a crack at each shock
            float b = 0.0f;
            const int t = sinceBoom;
            const int L = (int) (0.16 * sr);
            if (t >= 0 && t < L + (int) (0.06 * sr))
            {
                const float nw = t < L ? 1.0f - 2.0f * (float) t / (float) L : 0.0f;
                lpA += soft * (nw - lpA);
                lpB += soft * (lpA - lpB);
                const float c1 = t < (int) (0.03 * sr) ? std::exp (-(float) t / (0.006f * (float) sr)) : 0.0f;
                const int t2 = t - L;
                const float c2 = t2 >= 0 && t2 < (int) (0.03 * sr) ? std::exp (-(float) t2 / (0.006f * (float) sr)) : 0.0f;
                b = 0.55f * lpB + 0.22f * (c1 + c2) * noise();
            }
            else { lpA = flushDenormal (lpA * 0.995f); lpB = flushDenormal (lpB * 0.995f); b = 0.55f * lpB; }
            boomOut[(size_t) i] = b;

            // the rainbow: each colour lit as the ring reaches you, then ringing on
            const float x = in[i];
            const double ts = (double) sinceBoom / sr;
            for (int k = 0; k < kBands; ++k)
            {
                auto& bd = bands[(size_t) k];
                const double a = ts - gap * (double) k;   // seconds since this colour arrived
                float gate = 0.0f;
                if (a >= 0.0 && a < 0.4)
                    gate = a < 0.01 ? (float) (a / 0.01) : a < 0.13 ? 1.0f : (float) std::exp (-(a - 0.13) / 0.08);
                if (a >= 0.0 && a < 1.0 / sr * 1.5) bd.ping = 0.35f;   // the colour pings as it arrives
                const float e = (x * gate + b * 0.5f) + bd.ping * noise();
                bd.ping = flushDenormal (bd.ping * pingDecay);
                const float y = bd.svf.bandpass (e);

                // its own little reverb (4 lines, a Hadamard mix, decaying over the tail)
                float o[4];
                for (int j = 0; j < 4; ++j) o[j] = bd.line[(size_t) j][(size_t) bd.pos[(size_t) j]];
                const float h0 = 0.5f * (o[0] + o[1] + o[2] + o[3]), h1 = 0.5f * (o[0] - o[1] + o[2] - o[3]);
                const float h2 = 0.5f * (o[0] + o[1] - o[2] - o[3]), h3 = 0.5f * (o[0] - o[1] - o[2] + o[3]);
                const float hs[4] = { h0, h1, h2, h3 };
                const float ins[4] = { y, -y, y, -y };
                for (int j = 0; j < 4; ++j)
                {
                    auto& line = bd.line[(size_t) j];
                    bd.lp[(size_t) j] += bd.damp * (hs[j] - bd.lp[(size_t) j]);
                    bd.lp[(size_t) j] = flushDenormal (bd.lp[(size_t) j]);
                    line[(size_t) bd.pos[(size_t) j]] = softClip (bd.lp[(size_t) j] * bd.g[(size_t) j] + ins[j] * 0.5f);
                    if (++bd.pos[(size_t) j] >= (int) line.size()) bd.pos[(size_t) j] = 0;
                }
                const float verb = 0.5f * (o[0] + o[2]), verb2 = 0.5f * (o[1] + o[3]);
                bandOut[(size_t) k][(size_t) i] = y * 0.8f + (verb + verb2) * 0.45f;
                bandSide[(size_t) k][(size_t) i] = (verb - verb2) * 0.45f;
                peak = std::max (peak, (double) std::abs (bandOut[(size_t) k][(size_t) i]) + std::abs (y));
            }
            peak = std::max (peak, (double) std::abs (b) + std::abs (diveOut[(size_t) i]));
            if (sinceBoom < (1 << 30)) ++sinceBoom;
        }
        // asleep once everything has rung out (after the last colour's window)
        const bool ringing = peak > 1.0e-5 || sinceBoom < (int) ((gap * kBands + 0.5) * sr);
        quietFor = ringing ? 0 : quietFor + n;
    }

    const float* dive() const noexcept { return diveOut.data(); }
    const float* boom() const noexcept { return boomOut.data(); }
    const float* band (int k) const noexcept { return bandOut[(size_t) k].data(); }
    const float* bandSideOut (int k) const noexcept { return bandSide[(size_t) k].data(); }

    // the shop's stereo mix: the dive and the boom in the middle (the boom a touch wide), the colours
    // fanning out from the middle, red to the left and violet to the right, as the ring opens
    void addStereo (float* oL, float* oR, int n, float level, const Settings& s) const noexcept
    {
        if (level <= 0.0f) return;
        const float gap = 0.035f * std::clamp (s.spread, 0.0f, 3.0f);
        const double t0 = (double) (sinceBoom - n) / sr;
        for (int k = 0; k < kBands; ++k)
        {
            const float* y = bandOut[(size_t) k].data();
            const float* sd = bandSide[(size_t) k].data();
            const float side = ((float) k - 3.0f) / 3.0f;
            for (int i = 0; i < n; ++i)
            {
                const double a = t0 + (double) i / sr - gap * (double) k;
                const float open = a <= 0.0 ? 0.0f : smoothstep ((float) (a / 0.45));
                const float p = 0.5f + 0.5f * side * 0.85f * open;   // 0 left .. 1 right
                const float gl = std::cos (p * 1.5707963f), gr = std::sin (p * 1.5707963f);
                oL[i] += level * gl * (y[i] + sd[i]);   // (its reverb's width stays on its own side)
                oR[i] += level * gr * (y[i] - sd[i]);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const float d = level * (diveOut[(size_t) i] + boomOut[(size_t) i]);
            oL[i] += d;
            oR[i] += d;
        }
    }

private:
    struct SVF   // a TPT state-variable bandpass, peak gain 1
    {
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
        void setup (double sr, float hz, float q)
        {
            g = (float) std::tan (kPi * std::min ((double) hz, sr * 0.45) / sr);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0; }
        float bandpass (float x) noexcept
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = flushDenormal (2.0f * v1 - ic1); ic2 = flushDenormal (2.0f * v2 - ic2);
            return k * v1;
        }
    };
    struct Whoosh   // noise through a bandpass whose centre moves (set per sample)
    {
        float ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        float process (float x, float hz, float sr) noexcept
        {
            const float g = std::tan (3.14159265f * hz / sr), k = 0.5f;
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = flushDenormal (2.0f * v1 - ic1); ic2 = flushDenormal (2.0f * v2 - ic2);
            return k * v1;
        }
    };
    struct Band
    {
        SVF svf;
        std::array<std::vector<float>, 4> line;
        std::array<int, 4> pos {};
        std::array<float, 4> lp {};
        std::array<float, 4> g {};
        float damp = 1.0f, ping = 0.0f;
    };

    void startBoom() noexcept { sinceBoom = 0; lpA = lpB = 0.0f; ++booms; }

    float read (float pos) const noexcept
    {
        const float f = std::floor (pos);
        const int i = (int) f;
        const float t = pos - f;
        const int m = ringSize - 1;
        return hermite (ring[(size_t) ((i - 1) & m)], ring[(size_t) (i & m)], ring[(size_t) ((i + 1) & m)], ring[(size_t) ((i + 2) & m)], t);
    }
    float noise() noexcept { seed = seed * 1664525u + 1013904223u; return (float) (int32_t) seed * (1.0f / 2147483648.0f); }

    double sr = 48000.0;
    std::vector<float> ring;
    int ringSize = 1, w = 0;
    int diveLeft = 0, diveLen = 0, booms = 0, dives = 0;
    int sinceBoom = 1 << 30, quietFor = 1 << 30;
    float lpA = 0.0f, lpB = 0.0f;
    float soft = 0.25f;        // the N-wave's corners: a one-pole at 2.5 kHz, twice
    float pingDecay = 0.999f;  // each colour's ping dies away in about 30 ms
    uint32_t seed = 0x5eed1234u;
    Whoosh whoosh;
    std::array<Band, kBands> bands;
    std::vector<float> diveOut, boomOut;
    std::array<std::vector<float>, kBands> bandOut, bandSide;
};

//==============================================================================
// A hit in the input, for the rainboom to answer: a fast envelope jumping well above a slow one
// (and above -30 dBFS), at most once every 'gap' seconds.
class HitDetector
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        fastA = 1.0f - std::exp (-1.0f / (0.001f * (float) sr)); fastR = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        slowA = 1.0f - std::exp (-1.0f / (0.3f * (float) sr));
        fast = slow = 0.0f; wait = 0;
    }
    bool process (const float* l, const float* r, int n, float gapSeconds) noexcept
    {
        bool hit = false;
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (l[i]), std::abs (r[i]));
            fast += (x > fast ? fastA : fastR) * (x - fast);
            slow += slowA * (x - slow);
            if (wait > 0) --wait;
            else if (fast > 0.0316f && fast > 4.0f * slow + 0.005f) { hit = true; wait = (int) (gapSeconds * sr); }
        }
        fast = flushDenormal (fast); slow = flushDenormal (slow);
        return hit;
    }
private:
    double sr = 48000.0;
    float fastA = 0, fastR = 0, slowA = 0, fast = 0, slow = 0;
    int wait = 0;
};
} // namespace fp

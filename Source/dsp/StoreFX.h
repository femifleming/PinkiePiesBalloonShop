#pragma once
#include "Utils.h"
#include "Scales.h"
#include <array>
#include <atomic>
#include <vector>

namespace fp
{
//==============================================================================
// The balloon store: the ten FX balls from SoundGrain (Olivier Belanger), rebuilt here, plus a
// ring modulator (after the ring-modulated hats on Autechre's Rpeg) whose carrier can follow
// the input's loudness, one signal steering another.
// Each one you buy sits on the shelf with the main effects, in whatever order you put them:
// what reaches it goes in (mono, DC-blocked), and the effect comes out with its own level
// (0..2) and width (mono .. stereo) and is added to it, as a send would be in SoundGrain.
// Two controls per effect, with SoundGrain's ranges.
//
// Pitch-type controls follow the scale: the harmonizer's transposition snaps to the nearest
// scale degree, and the three resonators' frequencies snap to the scale's notes, rooted on C.
class StoreFX
{
public:
    static constexpr int kNum = 11;
    enum Kind { reverb = 0, delay, disto, waveguide, complexRes, degrade, harmonizer, clipper, phaser, allpassWG, ringMod };
    enum Pitch { notPitched = 0, pitchHz, pitchSemis };

    struct Control { const char* name; float lo, hi, def; bool log; const char* unit; int pitch; };
    struct Info { const char* key; const char* name; const char* shortName; Control c1, c2; };

    static const std::array<Info, kNum>& infos()
    {
        static const std::array<Info, kNum> list { {
            { "Reverb",     "Reverb",            "reverb",     { "feedback", 0.0f, 1.0f, 0.75f, false, "", 0 },          { "cutoff", 100.0f, 15000.0f, 5000.0f, true, "Hz", 0 } },
            { "Delay",      "Delay",             "delay",      { "delay", 0.01f, 1.0f, 0.25f, false, "s", 0 },           { "feedback", 0.0f, 1.0f, 0.5f, false, "", 0 } },
            { "Disto",      "Disto",             "disto",      { "drive", 0.0f, 1.0f, 0.75f, false, "", 0 },             { "slope", 0.0f, 0.99f, 0.75f, false, "", 0 } },
            { "Waveguide",  "Waveguide",         "waveguide",  { "frequency", 20.0f, 500.0f, 100.0f, true, "Hz", pitchHz }, { "fall time", 1.0f, 60.0f, 30.0f, false, "s", 0 } },
            { "ComplexRes", "Complex Resonator", "resonator",  { "frequency", 20.0f, 4000.0f, 500.0f, true, "Hz", pitchHz }, { "decay", 0.001f, 5.0f, 1.0f, true, "s", 0 } },
            { "Degrade",    "Degrade",           "degrade",    { "bit depth", 2.0f, 32.0f, 8.0f, true, "bits", 0 },     { "sr scale", 0.01f, 1.0f, 0.25f, true, "", 0 } },
            { "Harmonizer", "Harmonizer",        "harmonizer", { "transpose", -12.0f, 12.0f, -7.0f, false, "st", pitchSemis }, { "feedback", 0.0f, 1.0f, 0.25f, false, "", 0 } },
            { "Clipper",    "Clipper",           "clipper",    { "threshold", 0.001f, 0.25f, 0.1f, true, "", 0 },        { "cutoff", 100.0f, 15000.0f, 5000.0f, true, "Hz", 0 } },
            { "Phaser",     "Phaser",            "phaser",     { "lfo freq", 0.05f, 10.0f, 0.3f, true, "Hz", 0 },        { "feedback", 0.0f, 1.0f, 0.5f, false, "", 0 } },
            { "AllpassWG",  "Detuned Resonator", "detuned",    { "frequency", 20.0f, 500.0f, 100.0f, true, "Hz", pitchHz }, { "detune", 0.0f, 1.0f, 0.5f, false, "", 0 } },
            { "RingMod",    "Ring Mod",          "ring mod",   { "frequency", 20.0f, 5000.0f, 440.0f, true, "Hz", pitchHz }, { "follow", 0.0f, 1.0f, 0.3f, false, "", 0 } },
        } };
        return list;
    }

    struct Settings { bool owned = false; float c1 = 0, c2 = 0, level = 1, width = 0.5f; };
    using AllSettings = std::array<Settings, kNum>;

    // the scale the pitch controls snap to (also read by the parameter text, so the balloons show the snapped value)
    static std::atomic<int>& displayScale() { static std::atomic<int> s { 0 }; return s; }

    static double snapHz (double hz, const Scale& s)
    {
        constexpr double root = 261.6255653;   // C4
        const double cents = 1200.0 * std::log2 (std::max (1.0, hz) / root);
        return root * std::exp2 (quantise (s, cents).cents / 1200.0);
    }
    static double snapSemis (double st, const Scale& s) { return quantise (s, st * 100.0).cents / 100.0; }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double k = sr / 44100.0;
        static const int rvLen[8] = { 2473, 2767, 3217, 3557, 3907, 4127, 2143, 1933 };   // WGVerb's lines
        for (int i = 0; i < 8; ++i) rv.lines[i].init ((int) (rvLen[i] * k) + 8);
        for (int i = 0; i < 8; ++i) rv.len[i] = (float) (rvLen[i] * k);
        dl.line.init ((int) (1.05 * sr) + 8);
        wg.line.init ((int) (sr / 20.0) + 16);
        hz.line.init ((int) (0.25 * sr) + 8);
        ap.line.init ((int) (sr / 20.0) + 16);
        for (auto& a : ap.ap) a.init ((int) (sr / 20.0) + 16);
        fadeCoef = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        smoothCoef = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
        for (auto& g : gain) g = 0.0f;
        for (int i = 0; i < kNum; ++i)
        {
            for (int k = 0; k < 2; ++k)
            {
                decorLen[i][k] = std::max (8, (int) (sr * 0.001 * (k == 0 ? 4.7 + 0.37 * i : 7.9 + 0.53 * i)));
                decor[i][k].init (decorLen[i][k] + 4);
            }
            resetEffect (i);
        }
    }

    // once per block: who's on, their controls and coefficients
    void beginBlock (const AllSettings& set, const Scale& scale) noexcept
    {
        for (int e = 0; e < kNum; ++e)
        {
            const auto& s = set[(size_t) e];
            target[e] = s.owned ? 1.0f : 0.0f;
            live[e] = ! (target[e] == 0.0f && gain[e] < 1.0e-5f);
            if (! live[e])
            {
                if (gain[e] != 0.0f) { gain[e] = 0.0f; resetEffect (e); }
                continue;
            }
            level[e] = s.level;
            width[e] = std::clamp (s.width, 0.0f, 1.0f);
            prepareBlock (e, s, scale);
        }
    }

    // one effect, one sample: it hears what reaches it, and adds itself to it. Width spreads its
    // sound from mono in the middle to wide stereo (through a pair of decorrelating allpasses),
    // at the same loudness.
    void tickOne (int e, float& l, float& r) noexcept
    {
        if (! live[e]) return;
        gain[e] += fadeCoef * (target[e] - gain[e]);
        const float x = 0.5f * (l + r);
        // SoundGrain runs each FX input through DCBlock and Denorm
        const float dc = x - dcX[e] + 0.995f * dcY[e];
        dcX[e] = x; dcY[e] = flushDenormal (dc);
        const float y = tick (e, dc) * level[e] * gain[e];
        const float w = width[e];
        float side = 0.0f;
        if (w > 0.0f)
        {
            float v = y;
            for (int k = 0; k < 2; ++k)
            {
                auto& line = decor[e][k];
                const float d = line.at (decorLen[e][k]);
                const float out = d - 0.6f * v;
                line.push (v + 0.6f * out);
                v = out;
            }
            side = v;
        }
        const float c = 0.70710678f / std::sqrt (1.0f + w * w);
        l += c * (y + w * side);
        r += c * (y - w * side);
    }

    // all of them in turn (in store order), on a block
    void process (float* L, float* R, int n, const AllSettings& set, const Scale& scale) noexcept
    {
        beginBlock (set, scale);
        for (int i = 0; i < n; ++i)
            for (int e = 0; e < kNum; ++e) tickOne (e, L[i], R[i]);
    }

private:
    struct Line
    {
        std::vector<float> b; int w = 0, size = 1;
        void init (int n) { size = std::max (4, n); b.assign ((size_t) size, 0.0f); w = 0; }
        void clear() { std::fill (b.begin(), b.end(), 0.0f); w = 0; }
        void push (float x) noexcept { b[(size_t) w] = flushDenormal (x); if (++w >= size) w = 0; }
        float at (int d) const noexcept { int i = w - d; while (i < 0) i += size; return b[(size_t) i]; }
        float read (float d) const noexcept     // linear, d >= 1 samples back
        {
            d = std::clamp (d, 1.0f, (float) size - 2.0f);
            const int i = (int) d; const float f = d - (float) i;
            return at (i) + f * (at (i + 1) - at (i));
        }
        float lagrange (float d) const noexcept  // 3rd-order Lagrange, d >= 2
        {
            d = std::clamp (d, 2.0f, (float) size - 3.0f);
            const int i = (int) d - 1; const float f = d - (float) i;   // f in [1, 2)
            const float y0 = at (i), y1 = at (i + 1), y2 = at (i + 2), y3 = at (i + 3);
            const float d0 = f, d1 = f - 1.0f, d2 = f - 2.0f, d3 = f - 3.0f;
            return -y0 * d1 * d2 * d3 / 6.0f + y1 * d0 * d2 * d3 / 2.0f - y2 * d0 * d1 * d3 / 2.0f + y3 * d0 * d1 * d2 / 6.0f;
        }
    };

    struct Svf   // TPT lowpass, Butterworth
    {
        float a1 = 0, a2 = 0, a3 = 0, s1 = 0, s2 = 0;
        void set (double sr, double f) noexcept
        {
            const double g = std::tan (kPi * std::clamp (f, 10.0, sr * 0.45) / sr), k = 1.41421356;
            a1 = (float) (1.0 / (1.0 + g * (g + k))); a2 = (float) g * a1; a3 = (float) g * a2;
        }
        float lp (float x) noexcept
        {
            const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
            s1 = flushDenormal (2.0f * v1 - s1); s2 = flushDenormal (2.0f * v2 - s2);
            return v2;
        }
    };

    struct { Line lines[8]; float len[8] {}, lp[8] {}, fb = 0, a = 0; } rv;
    struct { Line line; float time = 0.25f, target = 0.25f, fb = 0; } dl;
    struct { float d = 0.4f, slope = 0, y = 0; } ds;
    struct { Line line; float len = 100, g = 0, prev = 0, in = 0; } wg;
    struct { float re = 0, im = 0, cr = 0, ci = 0, norm = 0; } cx;
    struct { float steps = 128, rate = 0.25f, phase = 1, held = 0; } dg;
    struct { Line line; float ratio = 1, fb = 0, phase = 0, out = 0, win = 4410; } hz;
    struct { float t = 0.1f, mul = 3.3f; Svf f; } cl;
    struct { float z[6] {}, last = 0; double ph = 0; float inc = 0, fb = 0; double lo = 0, hi = 0; } ph;   // six allpass stages
    struct Allpass { Line line; void init (int n) { line.init (n); } };
    struct { Line line; Allpass ap[3]; float len = 100, dly[3] {}, out = 0; } ap;
    struct { double ph = 0; float inc = 0, follow = 0, env = 0, atk = 0.01f, rel = 0.001f; } rm;

    void resetEffect (int e) noexcept
    {
        dcX[e] = dcY[e] = 0.0f;
        for (auto& d : decor[e]) d.clear();
        switch (e)
        {
            case reverb:     for (auto& l : rv.lines) l.clear(); for (auto& v : rv.lp) v = 0; break;
            case delay:      dl.line.clear(); break;
            case disto:      ds.y = 0; break;
            case waveguide:  wg.line.clear(); wg.prev = 0; break;
            case complexRes: cx.re = cx.im = 0; break;
            case degrade:    dg.phase = 1; dg.held = 0; break;
            case harmonizer: hz.line.clear(); hz.out = 0; hz.phase = 0; break;
            case clipper:    cl.f.s1 = cl.f.s2 = 0; break;
            case phaser:     for (auto& z : ph.z) z = 0; ph.last = 0; ph.ph = 0; break;
            case allpassWG:  ap.line.clear(); for (auto& a : ap.ap) a.line.clear(); ap.out = 0; break;
            case ringMod:    rm.ph = 0; rm.env = 0; break;
            default: break;
        }
    }

    // per-block coefficients
    void prepareBlock (int e, const Settings& s, const Scale& scale) noexcept
    {
        switch (e)
        {
            case reverb:
                rv.fb = std::min (s.c1, 0.995f);
                rv.a = (float) std::exp (-kTwoPi * std::clamp ((double) s.c2, 20.0, sr * 0.45) / sr);
                break;
            case delay:      dl.target = std::clamp (s.c1, 0.01f, 1.0f); dl.fb = std::min (s.c2, 0.995f); break;
            case disto:      ds.d = 0.4f - 0.3999f * std::clamp (s.c1, 0.0f, 1.0f); ds.slope = std::clamp (s.c2, 0.0f, 0.99f); break;
            case waveguide:
            {
                const double f = snapHz (s.c1, scale);
                wg.len = (float) std::min ((double) wg.line.size - 4.0, sr / f);
                wg.g = (float) std::pow (0.01, 1.0 / (f * std::max (0.1, (double) s.c2)));   // -40 dB after the fall time
                wg.in = 1.5f * std::sqrt (1.0f - wg.g * wg.g);   // about unity energy gain for noise; tanh catches pure tones
                break;
            }
            case complexRes:
            {
                const double f = snapHz (s.c1, scale);
                const double r = std::exp (-1.0 / (std::max (0.001, (double) s.c2) * sr));
                const double w = kTwoPi * std::min (f, sr * 0.45) / sr;
                cx.cr = (float) (r * std::cos (w)); cx.ci = (float) (r * std::sin (w));
                cx.norm = (float) (1.5 * std::sqrt (1.0 - r * r));
                break;
            }
            case degrade:    dg.steps = std::exp2 (std::clamp (s.c1, 2.0f, 32.0f) - 1.0f); dg.rate = std::clamp (s.c2, 0.01f, 1.0f); break;
            case harmonizer:
                hz.ratio = (float) std::exp2 (snapSemis (s.c1, scale) / 12.0);
                hz.fb = std::min (s.c2, 0.99f);
                hz.win = (float) (0.1 * sr);
                break;
            case clipper:    cl.t = std::clamp (s.c1, 0.001f, 0.25f); cl.mul = 0.33f / cl.t; cl.f.set (sr, s.c2); break;
            case phaser:
                ph.inc = (float) (kTwoPi * s.c1 / sr);
                ph.fb = 0.85f * std::clamp (s.c2, 0.0f, 1.0f);
                ph.lo = std::log (200.0); ph.hi = std::log (std::min (5000.0, sr * 0.4));   // the notches sweep 200 Hz .. 5 kHz
                break;
            case ringMod:
                rm.inc = (float) (kTwoPi * snapHz (s.c1, scale) / sr);
                rm.follow = std::clamp (s.c2, 0.0f, 1.0f);
                rm.atk = (float) (1.0 - std::exp (-1.0 / (0.003 * sr)));
                rm.rel = (float) (1.0 - std::exp (-1.0 / (0.120 * sr)));
                break;
            case allpassWG:
            {
                const double f = snapHz (s.c1, scale);
                ap.len = (float) std::min ((double) ap.line.size - 4.0, sr / f);
                const float det = std::clamp (s.c2, 0.0f, 1.0f);
                static const float spread[3] = { 1.0f, 0.9981f, 0.9957f };
                for (int k = 0; k < 3; ++k) ap.dly[k] = std::max (1.0f, ap.len * 0.5f * det * spread[k]);
                break;
            }
            default: break;
        }
    }

    float tick (int e, float x) noexcept
    {
        switch (e)
        {
            case reverb:    // eight waveguides meeting at a scattering junction, lowpassed in the loop
            {
                float y[8], sum = 0.0f;
                for (int k = 0; k < 8; ++k)
                {
                    const float o = rv.lines[k].at ((int) rv.len[k]);
                    rv.lp[k] = flushDenormal (o + rv.a * (rv.lp[k] - o));
                    y[k] = rv.lp[k] * rv.fb;
                    sum += y[k];
                }
                const float junction = 0.25f * sum;
                for (int k = 0; k < 8; ++k) rv.lines[k].push (x + junction - y[k]);
                return 0.7f * 0.25f * sum;
            }
            case delay:
            {
                dl.time += smoothCoef * (dl.target - dl.time);
                const float y = dl.line.read (dl.time * (float) sr);
                dl.line.push (x + y * dl.fb);
                return y;
            }
            case disto:     // arc-tangent waveshaper, then a one-pole lowpass whose slope darkens it
            {
                const float v = std::atan2 (x, ds.d) * 0.6366f;
                ds.y = flushDenormal (v + (ds.y - v) * ds.slope);
                return 0.5f * ds.y;
            }
            case waveguide: // one delay line, a two-point lowpass, Lagrange-interpolated
            {
                const float o = wg.line.lagrange (wg.len - 0.5f);
                const float lp = 0.5f * (o + wg.prev);
                wg.prev = o;
                wg.line.push (x * wg.in + lp * wg.g);
                return softClip (lp);
            }
            case complexRes:
            {
                const float re = x + cx.cr * cx.re - cx.ci * cx.im;
                const float im = cx.ci * cx.re + cx.cr * cx.im;
                cx.re = flushDenormal (re); cx.im = flushDenormal (im);
                return softClip (cx.im * cx.norm);
            }
            case degrade:
            {
                dg.phase += dg.rate;
                if (dg.phase >= 1.0f) { dg.phase -= 1.0f; dg.held = std::round (x * dg.steps) / dg.steps; }
                return dg.held;
            }
            case harmonizer: // two crossfading taps sweeping through a 100 ms window
            {
                hz.phase += (1.0f - hz.ratio) / hz.win;
                hz.phase -= std::floor (hz.phase);
                const float p2 = hz.phase + 0.5f - std::floor (hz.phase + 0.5f);
                const float w1 = std::sin ((float) kPi * hz.phase), w2 = std::sin ((float) kPi * p2);
                const float y = hz.line.read (1.0f + hz.phase * hz.win) * w1 * w1 + hz.line.read (1.0f + p2 * hz.win) * w2 * w2;
                hz.line.push (x + y * hz.fb);
                return y;
            }
            case clipper:   return cl.f.lp (std::clamp (x, -cl.t, cl.t) * cl.mul);
            case phaser:    // six first-order allpasses swept by a sine, fed back; added to the dry sound for the notches
            {
                ph.ph += ph.inc; if (ph.ph > kTwoPi) ph.ph -= kTwoPi;
                const double f = std::exp (ph.lo + (ph.hi - ph.lo) * (0.5 + 0.5 * std::sin (ph.ph)));
                const double tn = std::tan (kPi * f / sr);
                const float a = (float) ((tn - 1.0) / (tn + 1.0));
                float v = x + ph.fb * ph.last;
                for (auto& z : ph.z)
                {
                    const float y = a * v + z;
                    z = flushDenormal (v - a * y);
                    v = y;
                }
                ph.last = std::tanh (v);
                return 0.7f * (x + v);
            }
            case allpassWG: // a waveguide with three allpasses in the loop, which pull its partials out of tune
            {
                float v = ap.line.read (ap.len) * 0.999f;
                for (int k = 0; k < 3; ++k)
                {
                    const float d = ap.ap[k].line.read (ap.dly[k]);
                    const float in = v - 0.3f * d;
                    ap.ap[k].line.push (in);
                    v = d + 0.3f * in;
                }
                ap.line.push (x * 0.1f + v);
                return softClip (0.6f * v);
            }
            case ringMod:   // the input times a sine; with follow, the carrier rises as the input gets louder
            {
                const float ax = std::abs (x);
                rm.env = flushDenormal (rm.env + (ax > rm.env ? rm.atk : rm.rel) * (ax - rm.env));
                rm.ph += (double) rm.inc * (1.0 + 6.0 * (double) rm.follow * std::min (1.0f, rm.env * 3.0f));
                if (rm.ph > kTwoPi) rm.ph -= kTwoPi;
                return 1.4f * x * (float) std::sin (rm.ph);
            }
            default: return 0.0f;
        }
    }

    double sr = 48000.0;
    float fadeCoef = 0.001f, smoothCoef = 0.001f;
    float gain[kNum] {}, dcX[kNum] {}, dcY[kNum] {}, target[kNum] {}, level[kNum] {}, width[kNum] {};
    bool live[kNum] {};
    Line decor[kNum][2];
    int decorLen[kNum][2] {};
};
} // namespace fp

#pragma once
#include "Utils.h"
#include <array>
#include <atomic>
#include <complex>
#include <algorithm>

namespace fp
{
//==============================================================================
// The master EQ you draw on the chalkboard.
//
// EqCurve holds the drawing: N points spread evenly over log frequency (20 Hz .. 20 kHz),
// each 0..1 with 0.5 = 0 dB and the ends at +-range dB. fit() turns it into gains for a
// 16-band graphic EQ (a low shelf, 14 bells at Q 1, a high shelf) by solving for the gains
// whose combined response matches the drawing at each band, since neighbouring bands overlap.
// fit() runs on whichever thread changed the drawing (the editor, or loading a session); the
// audio thread only reads the band gains.
struct EqCurve
{
    static constexpr int N = 48, B = 16;
    static constexpr float range = 24.0f;    // dB at the top and bottom of the board
    std::array<std::atomic<float>, N> v;
    std::array<std::atomic<float>, B> bandDb;

    EqCurve() { flatten(); }

    static double freqAt (double t) noexcept { return 20.0 * std::pow (1000.0, std::clamp (t, 0.0, 1.0)); }
    static double tOf (double f) noexcept    { return std::log (std::max (1.0, f) / 20.0) / std::log (1000.0); }
    static double bandFreq (int b) noexcept  { return 25.0 * std::pow (640.0, (double) b / (double) (B - 1)); }

    float at (float t) const noexcept   // 0..1
    {
        const float x = std::clamp (t, 0.0f, 1.0f) * (float) (N - 1);
        const int i = std::min ((int) x, N - 2);
        const float f = x - (float) i;
        const float a = v[(size_t) i].load (std::memory_order_relaxed), b = v[(size_t) i + 1].load (std::memory_order_relaxed);
        return a + f * (b - a);
    }
    float dbAt (float t) const noexcept { return (at (t) - 0.5f) * 2.0f * range; }
    void set (int i, float h) noexcept { v[(size_t) std::clamp (i, 0, N - 1)].store (std::clamp (h, 0.0f, 1.0f), std::memory_order_relaxed); }
    void flatten() noexcept { for (auto& x : v) x.store (0.5f); for (auto& g : bandDb) g.store (0.0f); }
    bool isFlat() const noexcept { for (auto& x : v) if (std::abs (x.load() - 0.5f) > 1.0e-4f) return false; return true; }

    // one band's biquad (RBJ cookbook), unnormalised
    struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0; };
    static Coefs design (int b, double db, double sr) noexcept
    {
        const int type = b == 0 ? 1 : (b == B - 1 ? 2 : 0);   // low shelf, bells, high shelf
        const double q = type == 0 ? 1.0 : 0.7071;
        const double A = std::pow (10.0, db / 40.0), w = kTwoPi * std::min (bandFreq (b), sr * 0.45) / sr;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q), sa = 2.0 * std::sqrt (A) * al;
        if (type == 0) return { 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A };
        if (type == 1) return { A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                                (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa };
        return { A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                 (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa };
    }
    static double responseDb (const Coefs& k, double f, double sr) noexcept
    {
        const auto z = std::polar (1.0, -kTwoPi * std::min (f, sr * 0.499) / sr);
        return 20.0 * std::log10 (std::abs ((k.b0 + k.b1 * z + k.b2 * z * z) / (k.a0 + k.a1 * z + k.a2 * z * z)));
    }
    // what the EQ actually does at f, with the current band gains (for drawing it)
    double actualDb (double f, double sr = 48000.0) const noexcept
    {
        double s = 0.0;
        for (int b = 0; b < B; ++b)
            if (const float g = bandDb[(size_t) b].load(); g != 0.0f) s += responseDb (design (b, g, sr), f, sr);
        return s;
    }

    void fit() noexcept
    {
        constexpr double sr = 48000.0;
        if (isFlat()) { for (auto& g : bandDb) g.store (0.0f); return; }
        std::array<double, B> m {}, target {}, g {}, best {};
        for (int b = 0; b < B; ++b)
        {
            m[(size_t) b] = b == 0 ? 20.0 : (b == B - 1 ? 18000.0 : bandFreq (b));   // the shelves are matched at the ends
            target[(size_t) b] = dbAt ((float) tOf (m[(size_t) b]));
            g[(size_t) b] = target[(size_t) b];
        }
        double bestErr = 1.0e9;
        for (int it = 0; it < 40; ++it)
        {
            std::array<double, B> act {};
            for (int b = 0; b < B; ++b)
            {
                const auto k = design (b, g[(size_t) b], sr);
                for (int j = 0; j < B; ++j) act[(size_t) j] += responseDb (k, m[(size_t) j], sr);
            }
            double err = 0.0;
            for (int j = 0; j < B; ++j) err = std::max (err, std::abs (target[(size_t) j] - act[(size_t) j]));
            if (err < bestErr) { bestErr = err; best = g; }
            if (err < 0.01) break;
            for (int b = 0; b < B; ++b) g[(size_t) b] = std::clamp (g[(size_t) b] + 0.6 * (target[(size_t) b] - act[(size_t) b]), -36.0, 36.0);
        }
        for (int b = 0; b < B; ++b) bandDb[(size_t) b].store (std::abs (best[(size_t) b]) < 0.01 ? 0.0f : (float) best[(size_t) b]);
    }
};

//==============================================================================
// The EQ itself, on the master output. Each band's gain glides (~40 ms) to where the drawing
// puts it, so drawing never zips; with every band at 0 dB it's skipped (bit-exact).
class MasterEq
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        glide = (float) (1.0 - std::exp (-32.0 / (0.04 * sr)));
        for (auto& b : bands) for (auto& f : b.f) f.reset();
        for (auto& b : bands) b.db = 0.0f;
        counter = 0;
        active = false;
    }

    void process (float* L, float* R, int n, const EqCurve& curve) noexcept
    {
        bool any = false;
        for (int b = 0; b < EqCurve::B; ++b)
            any |= curve.bandDb[(size_t) b].load (std::memory_order_relaxed) != 0.0f || bands[(size_t) b].db != 0.0f;
        if (! any)
        {
            if (active) { for (auto& b : bands) for (auto& f : b.f) f.reset(); active = false; }
            return;
        }
        active = true;
        for (int i = 0; i < n; ++i)
        {
            if (counter-- <= 0) { counter = 31; update (curve); }
            float l = L[i], r = R[i];
            for (auto& b : bands)
                if (b.db != 0.0f) { l = b.f[0].process (l); r = b.f[1].process (r); }
            L[i] = flushDenormal (l);
            R[i] = flushDenormal (r);
        }
    }

private:
    void update (const EqCurve& curve) noexcept
    {
        for (int b = 0; b < EqCurve::B; ++b)
        {
            auto& band = bands[(size_t) b];
            const float t = curve.bandDb[(size_t) b].load (std::memory_order_relaxed);
            if (band.db == t) continue;
            band.db += glide * (t - band.db);
            if (std::abs (t - band.db) < 0.002f) band.db = t;
            const auto k = EqCurve::design (b, band.db, sr);
            for (auto& f : band.f) f.set (k.b0, k.b1, k.b2, k.a0, k.a1, k.a2);
        }
    }

    struct Band { Biquad f[2]; float db = 0.0f; };
    std::array<Band, EqCurve::B> bands;
    double sr = 48000.0;
    float glide = 0.1f;
    int counter = 0;
    bool active = false;
};
} // namespace fp

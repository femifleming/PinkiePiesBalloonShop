// Offline DSP checks for FreakiePie (not shipped).
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/dsp/Engine.h"
#include <chrono>
#include <cstdio>

using namespace fp;

static constexpr double SR = 48000.0;
static constexpr int BLOCK = 128;

struct Stats { double rmsIn = 0, rmsOut = 0, peak = 0, maxD2 = 0; bool finite = true; };

static EngineParams allDry()
{
    EngineParams p;
    p.sgMix = p.psMix = p.grMix = p.rvMix = 0.0f; p.masterFb = 0.0f; p.psFeedback = 0.0f;
    return p;
}

template <typename Gen, typename Mod>
static Stats run (EngineParams p, double seconds, Gen gen, Mod mod, std::vector<float>* capL = nullptr, double skip = 0.5)
{
    Engine e;
    e.setParams (p);
    e.prepare (SR);
    const int total = (int) (seconds * SR);
    std::vector<float> iL (BLOCK), iR (BLOCK), oL (BLOCK), oR (BLOCK);
    Stats s; double sIn = 0, sOut = 0; long cnt = 0;
    float p1 = 0, p2 = 0;
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        mod (p, pos);
        e.setParams (p);
        for (int i = 0; i < BLOCK; ++i) { auto v = gen (pos + i); iL[(size_t) i] = v.first; iR[(size_t) i] = v.second; }
        e.process (iL.data(), iR.data(), oL.data(), oR.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i)
        {
            const float o = oL[(size_t) i];
            if (! std::isfinite (o) || ! std::isfinite (oR[(size_t) i])) s.finite = false;
            if (capL) capL->push_back (o);
            if (pos + i > skip * SR)
            {
                sIn += iL[(size_t) i] * iL[(size_t) i]; sOut += o * o; ++cnt;
                s.peak = std::max (s.peak, (double) std::abs (o));
                s.maxD2 = std::max (s.maxD2, (double) std::abs (o - 2 * p1 + p2));
            }
            p2 = p1; p1 = o;
        }
    }
    s.rmsIn = std::sqrt (sIn / std::max (1L, cnt)); s.rmsOut = std::sqrt (sOut / std::max (1L, cnt));
    return s;
}

static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what); if (! ok) ++failures; };

    Rng noiseRng; noiseRng.seed (1234);
    auto noise = [&] (int) { const float v = 0.25f * noiseRng.bi(); return std::make_pair (v, 0.25f * noiseRng.bi()); };
    auto sine  = [] (int n) { const float v = 0.5f * (float) std::sin (kTwoPi * 220.0 * n / SR); return std::make_pair (v, v); };
    auto nomod = [] (EngineParams&, int) {};

    // 1. transparency
    {
        std::puts ("1. transparency (all mixes 0, feedback 0)");
        Engine e; auto p = allDry(); e.setParams (p); e.prepare (SR);
        std::vector<float> iL (BLOCK), iR (BLOCK), oL (BLOCK), oR (BLOCK);
        double maxErr = 0;
        for (int pos = 0; pos < (int) (5 * SR); pos += BLOCK)
        {
            for (int i = 0; i < BLOCK; ++i) { auto v = noise (0); iL[(size_t) i] = v.first; iR[(size_t) i] = v.second; }
            e.process (iL.data(), iR.data(), oL.data(), oR.data(), BLOCK);
            for (int i = 0; i < BLOCK; ++i)
                maxErr = std::max ({ maxErr, (double) std::abs (oL[(size_t) i] - iL[(size_t) i]), (double) std::abs (oR[(size_t) i] - iR[(size_t) i]) });
        }
        std::printf ("     max error = %g\n", maxErr);
        check (maxErr == 0.0, "bit-exact passthrough");
    }

    // 2. wet levels for each effect (100% wet, noise in)
    std::puts ("2. wet levels (noise in, 100% wet), want within about +-6 dB");
    {
        struct Case { const char* name; std::function<void (EngineParams&)> set; };
        std::vector<Case> cases {
            { "spectral gate", [] (EngineParams& p) { p.sgMix = 1; p.sgCutoffDb = -60; } },
            { "pitch +7 (fb 0)", [] (EngineParams& p) { p.psMix = 1; p.psShiftSemis = 7; } },
            { "pitch +7 (fb .9)", [] (EngineParams& p) { p.psMix = 1; p.psShiftSemis = 7; p.psFeedback = 0.9f; } },
            { "grains", [] (EngineParams& p) { p.grMix = 1; } },
            { "reverb 4s", [] (EngineParams& p) { p.rvMix = 1; p.rvDecay = 4; } },
            { "reverb 20s", [] (EngineParams& p) { p.rvMix = 1; p.rvDecay = 20; } },
        };
        for (auto& c : cases)
        {
            auto p = allDry(); c.set (p);
            noiseRng.seed (77);
            auto s = run (p, 12.0, noise, nomod, nullptr, 5.0);
            std::printf ("     %-18s in %6.1f dB  out %6.1f dB  (diff %+5.1f)  peak %.2f\n", c.name, db (s.rmsIn), db (s.rmsOut), db (s.rmsOut) - db (s.rmsIn), s.peak);
            check (s.finite && std::abs (db (s.rmsOut) - db (s.rmsIn)) < 9.0, c.name);
        }
    }

    // 3. clicks: pure sine through each wet effect while parameters move/randomise
    std::puts ("3. click detection (220 Hz sine, wet only, params automated)");
    {
        struct Case { const char* name; std::function<void (EngineParams&)> set; std::function<void (EngineParams&, int)> mod; double limit; };
        const double tone = 0.5 * std::pow (kTwoPi * 220.0 / SR, 2.0);
        std::vector<Case> cases {
            { "grains live, random octave", [] (EngineParams& p) { p.grMix = 1; p.grRandOct = true; }, [] (EngineParams&, int) {}, 0.0 },
            { "grains freeze toggling", [] (EngineParams& p) { p.grMix = 1; p.grRandOct = true; p.grFade = 0.3f; },
              [] (EngineParams& p, int pos) { p.grFreeze = ((pos / (int) (1.7 * SR)) % 2) == 1; }, 0.0 },
            { "grains freeze fast fade", [] (EngineParams& p) { p.grMix = 1; p.grRandOct = false; p.grOctave = 1; p.grFade = 0.01f; },
              [] (EngineParams& p, int pos) { p.grFreeze = ((pos / (int) (0.9 * SR)) % 2) == 1; }, 0.0 },
            { "pitch knob sweeping", [] (EngineParams& p) { p.psMix = 1; p.psFeedback = 0.0f; },
              [] (EngineParams& p, int pos) { p.psShiftSemis = (float) (24.0 * std::sin (pos / SR * 0.7)); }, 0.0 },
            { "scale switching", [] (EngineParams& p) { p.psMix = 1; p.psShiftSemis = 5.3f; },
              [] (EngineParams& p, int pos) { p.psScale = (pos / (int) (0.5 * SR)) % (int) getScales().size(); }, 0.0 },
            { "mix knobs moving", [] (EngineParams& p) { p.rvDecay = 3; },
              [] (EngineParams& p, int pos) { const float t = (float) (pos / SR);
                  p.sgMix = 0.5f + 0.5f * std::sin (t * 1.3f); p.psMix = 0.5f + 0.5f * std::sin (t * 2.1f);
                  p.grMix = 0.5f + 0.5f * std::sin (t * 0.9f); p.rvMix = 0.5f + 0.5f * std::sin (t * 1.7f); }, 0.0 },
            { "reverb decay sweeping", [] (EngineParams& p) { p.rvMix = 1; },
              [] (EngineParams& p, int pos) { p.rvDecay = (float) (10.1 + 9.9 * std::sin (pos / SR * 0.5)); }, 0.0 },
        };
        for (auto& c : cases)
        {
            auto p = allDry(); c.set (p);
            auto s = run (p, 20.0, sine, c.mod, nullptr, 1.0);
            // with octaves up to +2, a clean tone can have up to 16x the base 2nd difference
            const double limit = tone * 16.0 * 3.0 + 0.004;
            std::printf ("     %-28s max|d2| = %.5f (limit %.5f)  rms %.1f dB  peak %.2f\n", c.name, s.maxD2, limit, db (s.rmsOut), s.peak);
            check (s.finite && s.maxD2 < limit, c.name);
        }
    }

    // 4. feedback stability
    std::puts ("4. main feedback stability (60 s, feedback 100%)");
    {
        auto p = allDry(); p.masterFb = 1.0f;
        auto s = run (p, 60.0, noise, nomod);
        std::printf ("     all dry   : peak %.2f rms %.1f dB\n", s.peak, db (s.rmsOut));
        check (s.finite && s.peak < 4.0, "bounded with dry chain");

        EngineParams q; q.masterFb = 1.0f; q.psFeedback = 0.95f; q.sgMix = 0.8f; q.psMix = 0.8f; q.grMix = 0.8f; q.rvMix = 0.8f; q.rvDecay = 20;
        auto s2 = run (q, 60.0, noise, nomod);
        std::printf ("     all wet   : peak %.2f rms %.1f dB\n", s2.peak, db (s2.rmsOut));
        check (s2.finite && s2.peak < 4.0, "bounded with everything maxed");
    }

    // 5. CPU
    std::puts ("5. CPU (defaults, everything on)");
    {
        EngineParams p; p.masterFb = 0.4f; p.sgMix = 0.5f;
        auto t0 = std::chrono::high_resolution_clock::now();
        auto s = run (p, 60.0, noise, nomod);
        auto t1 = std::chrono::high_resolution_clock::now();
        const double secs = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("     60 s of stereo audio in %.2f s  ->  %.1f%% of one core (realtime x%.0f)\n", secs, secs / 60.0 * 100.0, 60.0 / secs);
        check (s.finite && secs < 15.0, "fast enough for live use");
    }

    // 6. render a demo file for listening
    {
        std::vector<float> cap;
        EngineParams p; p.masterFb = 0.3f;
        auto pluck = [] (int n) {
            const double t = n / SR; const double ph = std::fmod (t, 1.5);
            const double f = 220.0 * std::pow (2.0, std::floor (t / 1.5) * 0.25 - std::floor (t / 6.0));
            const float v = (float) (0.4 * std::exp (-ph * 3.0) * (std::sin (kTwoPi * f * t) + 0.3 * std::sin (kTwoPi * 2 * f * t)));
            return std::make_pair (v, v); };
        run (p, 20.0, pluck, [] (EngineParams& q, int pos) { q.grFreeze = pos > 10 * SR && pos < 16 * SR; }, &cap, 0.0);
        juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("freakiepie_demo.wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
        if (auto* w = wav.createWriterFor (os.get(), SR, 1, 24, {}, 0))
        {
            os.release();
            std::unique_ptr<juce::AudioFormatWriter> writer (w);
            const float* ch[1] = { cap.data() };
            writer->writeFromFloatArrays (ch, 1, (int) cap.size());
            std::printf ("6. wrote %s\n", out.getFullPathName().toRawUTF8());
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL GOOD" : "PROBLEMS", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

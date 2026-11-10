// Offline DSP checks for FreakiePie (not shipped).
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/dsp/Engine.h"
#include "../Source/dsp/Rainboom.h"
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

    // 7. the sonic rainboom: the dive rises in pitch, the boom is an N-wave on time, the colours
    // arrive red first and violet last, red on the left and violet on the right, and it rings out
    {
        auto check = [&] (bool ok, const char* what, const juce::String& detail = {})
        {
            std::printf ("7. [%s] %s %s\n", ok ? " ok " : "FAIL", what, detail.toRawUTF8());
            if (! ok) ++failures;
        };
        constexpr int B = 256;
        auto render = [&] (Rainboom& rb, double seconds, auto gen, float level, std::vector<float>* L, std::vector<float>* R,
                           std::vector<float>* dive, std::vector<float>* boom, std::array<std::vector<float>, Rainboom::kBands>* bands,
                           double fireAt = 0.0, float diveSec = 0.7f)
        {
            Rainboom::Settings st; st.tailSeconds = 5.0f;
            std::vector<float> in (B), oL (B), oR (B);
            bool fired = false;
            for (int pos = 0; pos < (int) (seconds * SR); pos += B)
            {
                if (! fired && pos >= (int) (fireAt * SR)) { rb.fire (diveSec); fired = true; }
                for (int i = 0; i < B; ++i) in[(size_t) i] = gen (pos + i);
                rb.process (in.data(), B, st);
                std::fill (oL.begin(), oL.end(), 0.0f); std::fill (oR.begin(), oR.end(), 0.0f);
                rb.addStereo (oL.data(), oR.data(), B, level, st);
                if (L) L->insert (L->end(), oL.begin(), oL.end());
                if (R) R->insert (R->end(), oR.begin(), oR.end());
                if (dive) dive->insert (dive->end(), rb.dive(), rb.dive() + B);
                if (boom) boom->insert (boom->end(), rb.boom(), rb.boom() + B);
                if (bands) for (int k = 0; k < Rainboom::kBands; ++k) (*bands)[(size_t) k].insert ((*bands)[(size_t) k].end(), rb.band (k), rb.band (k) + B);
            }
        };
        auto tone = [] (int i) { return 0.25f * (float) std::sin (kTwoPi * 220.0 * i / SR); };
        auto silence = [] (int) { return 0.0f; };
        auto zcHz = [] (const std::vector<float>& x, double from, double to)
        {
            int zc = 0;
            for (size_t i = (size_t) (from * SR); i + 1 < (size_t) (to * SR); ++i) if (x[i] <= 0 && x[i + 1] > 0) ++zc;
            return zc / (to - from);
        };
        {
            Rainboom rb; rb.prepare (SR, B);
            std::vector<float> dive, boom;
            render (rb, 1.2, tone, 1.0f, nullptr, nullptr, &dive, &boom, nullptr, 0.0, 0.7f);
            {   // (under the rush of air: listen below 500 Hz)
                juce::dsp::IIR::Filter<float> a1, a2;
                *a1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (SR, 500.0f);
                *a2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (SR, 500.0f);
                for (auto& v : dive) v = a2.processSample (a1.processSample (v));
            }
            const double early = zcHz (dive, 0.15, 0.3), late = zcHz (dive, 0.58, 0.68);
            const double semis = 12.0 * std::log2 (late / early);
            check (semis > 2.5 && semis < 8.0, "the dive squeezes the sound up in pitch (Doppler)", juce::String (early, 0) + " Hz -> " + juce::String (late, 0) + " Hz, +" + juce::String (semis, 1) + " st");
            size_t at = 0; double pk = 0;
            for (size_t i = 0; i < boom.size(); ++i) if (std::abs (boom[i]) > pk) { pk = std::abs (boom[i]); at = i; }
            const size_t b0 = (size_t) (0.7 * SR);
            double first = 0, second = 0;
            for (size_t i = b0; i < b0 + (size_t) (0.08 * SR); ++i) first += boom[i];
            for (size_t i = b0 + (size_t) (0.08 * SR); i < b0 + (size_t) (0.16 * SR); ++i) second += boom[i];
            check (std::abs ((double) at / SR - 0.7) < 0.01 && first > 0 && second < 0, "the boom lands when the dive ends, an N-wave (up, then down)",
                   "peak at " + juce::String ((double) at / SR * 1000.0, 1) + " ms, halves " + juce::String (first / SR * 1000.0, 1) + " / " + juce::String (second / SR * 1000.0, 1));
        }
        {
            Rainboom rb; rb.prepare (SR, B);
            std::array<std::vector<float>, Rainboom::kBands> bands;
            std::vector<float> L, R;
            render (rb, 7.0, silence, 1.0f, &L, &R, nullptr, nullptr, &bands, 0.0, 0.0f);
            // when each colour first comes up (its ping), from silence
            juce::String times; bool inOrder = true; double last = -1;
            for (int k = 0; k < Rainboom::kBands; ++k)
            {
                const auto& x = bands[(size_t) k];
                double pk = 0; for (size_t i = 0; i < (size_t) (0.6 * SR); ++i) pk = std::max (pk, (double) std::abs (x[i]));
                size_t t = 0; while (t < x.size() && std::abs (x[t]) < 0.3 * pk) ++t;
                // (the boom itself sets the low colours ringing at once: time the ping, after the boom's first 10 ms)
                size_t tp = (size_t) (0.01 * SR) + (size_t) (0.035 * SR * k);
                double pkPing = 0; size_t atPing = 0;
                for (size_t i = (size_t) (0.035 * SR * k); i < (size_t) (0.035 * SR * k + 0.03 * SR); ++i) if (std::abs (x[i]) > pkPing) { pkPing = std::abs (x[i]); atPing = i; }
                juce::ignoreUnused (t, tp);
                const double ms = (double) atPing / SR * 1000.0;
                inOrder = inOrder && ms > last; last = ms;
                times << juce::String (ms, 0) << (k < Rainboom::kBands - 1 ? " " : " ms");
            }
            check (inOrder, "the colours arrive red first, violet last", times);
            // red (the lows) on the left, violet (the highs) on the right, once the ring has opened
            auto bandEnergy = [&] (const std::vector<float>& x, float lo, float hi)
            {
                juce::dsp::IIR::Filter<float> f1, f2;
                *f1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass (SR, lo);
                *f2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass (SR, hi);
                double e = 0;
                for (size_t i = 0; i < x.size(); ++i) { const float y = f2.processSample (f1.processSample (x[i])); if (i > (size_t) (0.6 * SR) && i < (size_t) (2.0 * SR)) e += (double) y * y; }
                return e;
            };
            const double redLR = 10.0 * std::log10 (bandEnergy (L, 80.0f, 160.0f) / bandEnergy (R, 80.0f, 160.0f));
            const double vioLR = 10.0 * std::log10 (bandEnergy (L, 5000.0f, 10000.0f) / bandEnergy (R, 5000.0f, 10000.0f));
            check (redLR > 3.0 && vioLR < -3.0, "red on the left, violet on the right", "red L/R " + juce::String (redLR, 1) + " dB, violet " + juce::String (vioLR, 1) + " dB");
            auto rmsAt = [&] (double t) { double e = 0; for (size_t i = (size_t) (t * SR); i < (size_t) ((t + 0.2) * SR); ++i) e += (double) L[i] * L[i] + (double) R[i] * R[i]; return std::sqrt (e / (0.4 * SR)); };
            const double d1 = db (rmsAt (2.5) / rmsAt (0.5)), d2 = db (rmsAt (5.2) / rmsAt (0.5));
            check (d1 < -12.0 && d1 > -45.0 && d2 < d1 - 10.0, "the rainbow rings on and fades over its length (5 s)", juce::String (d1, 1) + " dB at 2.5 s, " + juce::String (d2, 1) + " dB at 5.2 s");
            double pk = 0; for (size_t i = 0; i < L.size(); ++i) pk = std::max ({ pk, (double) std::abs (L[i]), (double) std::abs (R[i]) });
            check (pk < 1.0, "from silence at full level: under 0 dBFS before the limiter", juce::String (db (pk), 1) + " dBFS peak");
            std::vector<float> more;
            render (rb, 6.0, silence, 1.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 1e9);
            check (! rb.active(), "then it goes to sleep (no CPU)");
        }
        {
            // with sound coming through: how loud, and how much CPU
            Rainboom rb; rb.prepare (SR, B);
            std::vector<float> L, R;
            auto loud = [] (int i) { return 0.25f * (float) std::sin (kTwoPi * 220.0 * i / SR) + 0.1f * (float) std::sin (kTwoPi * 1760.0 * i / SR); };
            const auto t0 = std::chrono::steady_clock::now();
            render (rb, 4.0, loud, 0.7f, &L, &R, nullptr, nullptr, nullptr, 0.2, 0.7f);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            double pk = 0; for (size_t i = 0; i < L.size(); ++i) pk = std::max ({ pk, (double) std::abs (L[i]), (double) std::abs (R[i]) });
            check (pk < 1.4, "over a -12 dBFS sound at the default level: peaks the limiter can hold", juce::String (db (pk), 1) + " dBFS");
            check (secs / 4.0 < 0.05, "cheap", juce::String (secs / 4.0 * 100.0, 2) + " % of real time while ringing");
            // a demo: the plucks, then a rainboom at 2 s
            Rainboom rd; rd.prepare (SR, B);
            std::vector<float> dL, dR;
            auto pluck = [] (int n) { const double t = n / SR, ph = std::fmod (t, 0.5); return (float) (0.3 * std::exp (-ph * 6.0) * std::sin (kTwoPi * (220.0 + 110.0 * std::floor (std::fmod (t, 2.0) * 2.0)) * t)); };
            render (rd, 10.0, pluck, 0.7f, &dL, &dR, nullptr, nullptr, nullptr, 1.3, 0.7f);
            for (size_t i = 0; i < dL.size(); ++i) { const float d = pluck ((int) i); dL[i] = juce::jlimit (-1.0f, 1.0f, dL[i] + d); dR[i] = juce::jlimit (-1.0f, 1.0f, dR[i] + d); }
            juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("rainboom_demo.wav");
            out.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
            if (auto* w = wav.createWriterFor (os.get(), SR, 2, 24, {}, 0))
            {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                const float* ch[2] = { dL.data(), dR.data() };
                writer->writeFromFloatArrays (ch, 2, (int) dL.size());
                std::printf ("7. wrote %s\n", out.getFullPathName().toRawUTF8());
            }
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL GOOD" : "PROBLEMS", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

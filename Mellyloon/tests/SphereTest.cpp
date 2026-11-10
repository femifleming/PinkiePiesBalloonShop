// Offline checks for the sphere (not shipped): the spherical harmonics, the encoders and decoders,
// the whole processor in each output mode with 1 to 16 grains, and how much CPU it takes.
#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <cstdio>

using namespace fp;
using ambi::Vec3;

static int failures = 0;
static void check (bool ok, const char* what, const juce::String& detail = {})
{
    std::printf ("  [%s] %s %s\n", ok ? " ok " : "FAIL", what, detail.toRawUTF8());
    if (! ok) ++failures;
}
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

static constexpr double SR = 48000.0;
static constexpr int BLOCK = 256;

static void setParam (FreakiePieProcessor& p, const juce::String& id, float real)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (real));
}

struct Run { std::vector<std::vector<float>> out; double seconds = 0, cpu = 0; bool finite = true; float peak = 0; };

// run the processor on noise (or a function) for 'seconds', 'outCh' output channels
template <typename Gen>
static Run render (FreakiePieProcessor& p, int outCh, double seconds, Gen gen, bool keep = true)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (outCh == 16 ? juce::AudioChannelSet::discreteChannels (16) : juce::AudioChannelSet::stereo());
    p.setBusesLayout (layout);
    p.setRateAndBufferSizeDetails (SR, BLOCK);
    p.prepareToPlay (SR, BLOCK);
    const int total = (int) (seconds * SR);
    juce::AudioBuffer<float> buf (std::max (2, outCh), BLOCK);
    juce::MidiBuffer midi;
    Run r;
    if (keep) r.out.assign ((size_t) outCh, {});
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        buf.clear();
        for (int i = 0; i < BLOCK; ++i) { auto v = gen (pos + i); buf.setSample (0, i, v.first); buf.setSample (1, i, v.second); }
        p.processBlock (buf, midi);
        for (int c = 0; c < outCh; ++c)
            for (int i = 0; i < BLOCK; ++i)
            {
                const float x = buf.getSample (c, i);
                if (! std::isfinite (x)) r.finite = false;
                r.peak = std::max (r.peak, std::abs (x));
                if (keep) r.out[(size_t) c].push_back (x);
            }
    }
    r.cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    r.seconds = seconds;
    return r;
}

static double rms (const std::vector<float>& x, size_t from = 0)
{
    double s = 0; size_t n = 0;
    for (size_t i = from; i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; }
    return std::sqrt (s / std::max<size_t> (1, n));
}

static void writeWav (const juce::File& f, const std::vector<std::vector<float>>& ch)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (f), SR, (unsigned int) ch.size(), 24, {}, 0)))
    {
        juce::AudioBuffer<float> b ((int) ch.size(), (int) ch[0].size());
        for (int c = 0; c < (int) ch.size(); ++c) b.copyFrom (c, 0, ch[(size_t) c].data(), (int) ch[0].size());
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    FreakiePieProcessor::rememberLastState = false;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();

    std::printf ("spherical harmonics (ACN / SN3D)\n");
    {
        const auto pts = ambi::fibonacci (40000);
        double m[16][16] {};
        for (auto& d : pts)
        {
            const auto y = ambi::sh (d);
            for (int a = 0; a < 16; ++a) for (int b = 0; b < 16; ++b) m[a][b] += (double) y[(size_t) a] * y[(size_t) b];
        }
        double worstDiag = 0, worstOff = 0;
        for (int a = 0; a < 16; ++a)
            for (int b = 0; b < 16; ++b)
            {
                const double v = m[a][b] / (double) pts.size();
                if (a == b) worstDiag = std::max (worstDiag, std::abs (v * (2 * ambi::orderOf (a) + 1) - 1.0));
                else worstOff = std::max (worstOff, std::abs (v));
            }
        check (worstDiag < 0.005, "each order has mean square 1/(2l+1)", juce::String (worstDiag, 5));
        check (worstOff < 0.002, "the 16 are orthogonal", juce::String (worstOff, 5));
        const auto y = ambi::sh (ambi::fromAzEl (juce::MathConstants<float>::halfPi, 0.0f));
        check (std::abs (y[1] - 1.0f) < 1e-5f && std::abs (y[3]) < 1e-5f, "+90 degrees azimuth is the left (ACN 1 = Y)");
        // against an independent formula: associated Legendre functions (no Condon-Shortley phase) with
        // SN3D weights, the AmbiX / IEM definition, at random directions
        {
            auto fact = [] (int k) { double f = 1; for (int i = 2; i <= k; ++i) f *= i; return f; };
            auto legendre = [] (int l, int m, double x)   // P_l^m(x), m >= 0, no (-1)^m
            {
                double pmm = 1.0;
                const double sx = std::sqrt (std::max (0.0, 1.0 - x * x));
                for (int i = 1; i <= m; ++i) pmm *= (2 * i - 1) * sx;
                if (l == m) return pmm;
                double pm1 = x * (2 * m + 1) * pmm;
                if (l == m + 1) return pm1;
                double pl = 0;
                for (int ll = m + 2; ll <= l; ++ll) { pl = (x * (2 * ll - 1) * pm1 - (ll + m - 1) * pmm) / (ll - m); pmm = pm1; pm1 = pl; }
                return pl;
            };
            juce::Random rr (99);
            double worst = 0;
            for (int t = 0; t < 500; ++t)
            {
                const double az = rr.nextDouble() * 2 * juce::MathConstants<double>::pi, el = std::asin (rr.nextDouble() * 2 - 1);
                const auto y = ambi::sh (ambi::fromAzEl ((float) az, (float) el));
                for (int l = 0; l <= 3; ++l)
                    for (int m = -l; m <= l; ++m)
                    {
                        const int am = std::abs (m);
                        const double norm = std::sqrt ((am == 0 ? 1.0 : 2.0) * fact (l - am) / fact (l + am));
                        const double ref = norm * legendre (l, am, std::sin (el)) * (m < 0 ? std::sin (am * az) : std::cos (am * az));
                        worst = std::max (worst, std::abs (ref - y[(size_t) (l * l + l + m)]));
                    }
            }
            check (worst < 1e-5, "all 16 match the AmbiX (ACN / SN3D, no Condon-Shortley) definition", "worst error " + juce::String (worst, 8));
        }
        const auto e = ambi::encodeInside ({ 0.0f, 0.0f, 0.0f });
        bool omni = std::abs (e[0] - 2.0f) < 1e-5f;
        for (int c = 1; c < 16; ++c) omni &= std::abs (e[(size_t) c]) < 1e-6f;
        check (omni, "a grain at the very centre is omni, +6 dB");
    }

    juce::Random noise (11);
    auto stereoNoise = [&] (int) { const float v = (noise.nextFloat() * 2.0f - 1.0f) * 0.25f; return std::pair<float, float> { v, v }; };
    // something closer to music: plucked tones, a few a second
    auto plucks = [] (int i)
    {
        const double t = i / SR;
        const int k = (int) (t * 3.0);
        const double tt = t - k / 3.0, f = 220.0 * std::pow (2.0, ((k * 5) % 12) / 12.0);
        const float v = (float) (0.3 * std::exp (-tt * 6.0) * std::sin (2.0 * juce::MathConstants<double>::pi * f * tt));
        return std::pair<float, float> { v, v };
    };
    auto allOff = [] (FreakiePieProcessor& p)
    {
        for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback }) setParam (p, id, 0.0f);
        setParam (p, ids::grains, 1.0f); setParam (p, ids::flight, 0.0f);   // (one grain, standing still, unless a test says otherwise)
    };
    // how sharply the scene points somewhere: level towards 'd' over level towards its opposite (dB)
    auto ratio = [] (FreakiePieProcessor& p, Vec3 d) { return 10.0 * std::log10 ((p.energy.levelAt (d) + 1e-12) / (p.energy.levelAt (d * -1.0f) + 1e-12)); };

    std::printf ("can you point at it?\n");
    {
        // your dry sound: one point, ahead
        FreakiePieProcessor p;
        allOff (p);
        auto r = render (p, 16, 2.0, stereoNoise);
        const double w = rms (r.out[0], (size_t) SR);
        check (r.finite && w > 0.01, "dry sound comes out on 16 channels", juce::String (db (w), 1) + " dBFS in W");
        check (ratio (p, { 1, 0, 0 }) > 15.0, "the dry sound is ahead (front / back)", juce::String (ratio (p, { 1, 0, 0 }), 1) + " dB");
        check (std::abs (ratio (p, { 0, 1, 0 })) < 1.0, "and centred (left / right)", juce::String (ratio (p, { 0, 1, 0 }), 2) + " dB");

        // one copy of the smear, on the left: the smear is on the left, the dry sound still ahead
        p.setGrainHome (0, { 0.0f, 0.95f, 0.0f });
        setParam (p, ids::sgMix, 1.0f);
        r = render (p, 16, 3.0, stereoNoise);
        check (ratio (p, { 0, 1, 0 }) > 12.0, "an effect's sound sits at its grain (left / right)", juce::String (ratio (p, { 0, 1, 0 }), 1) + " dB");

        // the same, the source moved to the right: the dry sound follows
        allOff (p);
        p.srcX.store (0.0f); p.srcY.store (-1.0f); p.srcZ.store (0.0f);
        r = render (p, 16, 1.5, stereoNoise);
        check (ratio (p, { 0, -1, 0 }) > 15.0, "drag the source and the dry sound goes there", juce::String (ratio (p, { 0, -1, 0 }), 1) + " dB");
        p.srcX.store (1.0f); p.srcY.store (0.0f); p.srcZ.store (0.0f);
    }
    {
        // four copies at four places, the dry ahead: sound at all five, and nothing below
        FreakiePieProcessor p;
        allOff (p);
        setParam (p, ids::grains, 4.0f); setParam (p, ids::deviate, 0.6f);
        setParam (p, ids::sgMix, 0.7f); setParam (p, ids::psMix, 0.5f); setParam (p, ids::psShift, 7.0f);
        const Vec3 at[4] = { { 0, 0.95f, 0 }, { 0, -0.95f, 0 }, { -0.95f, 0, 0 }, { 0, 0, 0.95f } };
        for (int k = 0; k < 4; ++k) p.setGrainHome (k, at[k]);
        auto r = render (p, 16, 4.0, plucks, false);
        double lo = 1e9, hi = -1e9;
        for (auto d : { Vec3 { 1, 0, 0 }, at[0], at[1], at[2], at[3] }) { const double l = db (std::sqrt (p.energy.levelAt (d.normalised()))); lo = std::min (lo, l); hi = std::max (hi, l); }
        const double below = db (std::sqrt (p.energy.levelAt ({ 0, 0, -1 })));
        check (r.finite && hi - lo < 9.0, "four copies and the dry sound: all five places sound", juce::String (hi - lo, 1) + " dB between them");
        check (lo - below > 8.0, "and there's nothing where nothing is (below)", juce::String (lo - below, 1) + " dB quieter below");
    }

    std::printf ("what reaches REAPER, channel by channel\n");
    {
        // the dry sound at a few places: each of the 16 output channels must carry exactly that place's
        // harmonic times the omni channel (so any AmbiX decoder sees one clean point)
        const Vec3 places[] = { { 1, 0, 0 }, ambi::fromAzEl (0.7f, 0.4f), ambi::fromAzEl (-2.3f, -0.6f), ambi::fromAzEl (2.0f, 1.1f) };
        double worst = 0;
        fp::DistanceSource::withWalls = false;   // (the direct path alone: the walls' reflections come from elsewhere, on purpose)
        for (auto d : places)
        {
            FreakiePieProcessor p;
            allOff (p);
            p.srcX.store (d.x); p.srcY.store (d.y); p.srcZ.store (d.z);
            auto r = render (p, 16, 1.0, stereoNoise);
            const auto y = ambi::sh (d);
            double w2 = 0;
            for (size_t i = (size_t) SR / 2; i < r.out[0].size(); ++i) w2 += (double) r.out[0][i] * r.out[0][i];
            for (int c = 0; c < 16; ++c)
            {
                double cw = 0;
                for (size_t i = (size_t) SR / 2; i < r.out[0].size(); ++i) cw += (double) r.out[(size_t) c][i] * r.out[0][i];
                worst = std::max (worst, std::abs (cw / w2 - y[(size_t) c]));
            }
        }
        fp::DistanceSource::withWalls = true;
        check (worst < 0.005, "each channel is its harmonic: order, signs and SN3D scaling all as AmbiX", "worst error " + juce::String (worst, 5));

        // two copies of one effect, left and right, nothing deviating: without decorrelation they'd be the
        // same signal from two sides (Y would cancel and a decoder would comb-filter); they must not be
        FreakiePieProcessor p;
        allOff (p);
        setParam (p, ids::grains, 2.0f); setParam (p, ids::deviate, 0.0f);
        setParam (p, ids::psMix, 1.0f); setParam (p, ids::psShift, 5.0f);
        p.setGrainHome (0, { 0, 0.95f, 0 }); p.setGrainHome (1, { 0, -0.95f, 0 });
        auto r = render (p, 16, 3.0, stereoNoise);
        const double yw = rms (r.out[1], (size_t) SR) / rms (r.out[0], (size_t) SR);
        check (yw > 0.5, "two copies of the same effect add up as two sounds, not one phasing one", "Y / W " + juce::String (yw, 2) + " (0 would be fully coherent)");
        FreakiePieProcessor::decorrelateCopies = false;   // the control: what it was before
        r = render (p, 16, 3.0, stereoNoise);
        FreakiePieProcessor::decorrelateCopies = true;
        const double yw0 = rms (r.out[1], (size_t) SR) / rms (r.out[0], (size_t) SR);
        check (yw0 < 0.2, "(control: without the decorrelators the two copies were one coherent, phasing sound)", "Y / W " + juce::String (yw0, 2));
    }

    std::printf ("frozen balloons\n");
    {
        // a tone caught in a full sound balloon and frozen: about as loud as the tone that went in
        FreakiePieProcessor p;
        allOff (p);
        auto& v = p.balloonFX.voice[0];
        double inW = 0, frozenW = 0;
        // one run: 2 s of the tone alone (the level to match), 2 s caught into the balloon, then it's tied
        // off (frozen) and the input goes quiet
        auto tone = [&] (int i)
        {
            if (i == (int) (2 * SR)) v.mode = 1;
            if (i == (int) (4 * SR)) v.mode = 2;
            const float x = i < (int) (4 * SR) ? 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / SR) : 0.0f;
            return std::pair<float, float> { x, x };
        };
        v.size = 1.0f; v.semis = 0.0f;
        auto r = render (p, 16, 8.0, tone);
        std::vector<float> a (r.out[0].begin() + (long) (SR / 2), r.out[0].begin() + (long) (2 * SR)), b (r.out[0].begin() + (long) (6 * SR), r.out[0].end());
        inW = rms (a); frozenW = rms (b);
        std::printf ("    (tone in: %.1f dBFS in W, frozen balloon: %.1f dBFS)\n", db (inW), db (frozenW));
        check (std::abs (db (frozenW) - db (inW)) < 2.0, "a full frozen balloon is as loud as what went in", juce::String (db (frozenW) - db (inW), 1) + " dB");
        // and a small one (pumped for a moment): muffled, but no quieter
        {
            FreakiePieProcessor q;
            allOff (q);
            auto& w = q.balloonFX.voice[0];
            auto tone2 = [&] (int i)
            {
                if (i == (int) (2 * SR)) w.mode = 1;
                if (i == (int) (4 * SR)) w.mode = 2;
                const float x = i < (int) (4 * SR) ? 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / SR) : 0.0f;
                return std::pair<float, float> { x, x };
            };
            w.size = 0.3f; w.semis = 0.0f;
            auto r2 = render (q, 16, 8.0, tone2);
            std::vector<float> a2 (r2.out[0].begin() + (long) (SR / 2), r2.out[0].begin() + (long) (2 * SR)), b2 (r2.out[0].begin() + (long) (6 * SR), r2.out[0].end());
            const double d2 = db (rms (b2)) - db (rms (a2));
            check (std::abs (d2) < 3.0, "a small frozen balloon is muffled but as loud", juce::String (d2, 1) + " dB");
        }
    }

    std::printf ("distance (after the IEM RoomEncoder)\n");
    {
        // level: 1 / distance (near is louder)
        auto levelAt = [&] (float r)
        {
            FreakiePieProcessor p;
            allOff (p);
            p.srcX.store (r); p.srcY.store (0.0f); p.srcZ.store (0.0f);
            auto out = render (p, 16, 1.0, stereoNoise);
            return rms (out.out[0], (size_t) SR / 2);
        };
        const double nearL = levelAt (0.2f), farL = levelAt (0.8f);
        check (db (nearL / farL) > 7.0 && db (nearL / farL) < 14.0, "nearer is louder (1 / distance, with the room)", juce::String (db (nearL / farL), 1) + " dB");
        // the bubble: the same sound in the same place, louder in a small bubble than a big one
        auto bubbleLevel = [&] (float knob)
        {
            FreakiePieProcessor p;
            allOff (p);
            setParam (p, ids::spread, knob);
            auto out = render (p, 16, 1.0, stereoNoise);
            return rms (out.out[0], (size_t) SR / 2);
        };
        const double small = bubbleLevel (0.0f), big = bubbleLevel (1.0f);
        check (db (small / big) > 15.0, "a small bubble (1.5 m) is much louder than a big one (24 m)", juce::String (db (small / big), 1) + " dB");

        // time: the sound arrives distance / 343 m/s later, and the walls answer after it
        fp::DistanceSource ds;
        ds.prepare (SR, BLOCK);
        std::vector<std::vector<float>> o (16, std::vector<float> ((size_t) (0.5 * SR), 0.0f));
        std::vector<float> in (BLOCK, 0.0f), dummy (BLOCK);
        for (int pos = 0; pos + BLOCK <= (int) o[0].size(); pos += BLOCK)
        {
            std::fill (in.begin(), in.end(), 0.0f);
            if (pos == 0) in[0] = 1.0f;
            float* op[16]; for (int c = 0; c < 16; ++c) op[c] = o[(size_t) c].data() + pos;
            ds.process (in.data(), BLOCK, { 0.75f, 0.0f, 0.0f }, 12.0f, op, nullptr, 0.0f);
        }
        int peak = 0; double best = 0;
        for (size_t i = 0; i < o[0].size(); ++i) if (std::abs (o[0][i]) > best) { best = std::abs (o[0][i]); peak = (int) i; }
        const double expect = 9.0 / 343.0 * SR;
        check (std::abs (peak - expect) < 3.0, "9 m away it arrives 26 ms later", juce::String (peak / SR * 1000.0, 2) + " ms");
        double later = 0;
        for (size_t i = (size_t) (peak + 0.005 * SR); i < o[0].size(); ++i) later = std::max (later, (double) std::abs (o[0][i]));
        check (later > 0.1 * best, "and the walls' reflections follow", juce::String (db (later / best), 1) + " dB after the direct sound");

        // Doppler: flying in from 12 m to 0 in half a second (24 m/s), a 1 kHz tone sounds higher
        fp::DistanceSource dp;
        dp.prepare (SR, BLOCK);
        fp::DistanceSource::withWalls = false;
        std::vector<float> w ((size_t) SR, 0.0f);
        std::vector<std::vector<float>> o2 (16, std::vector<float> (BLOCK));
        for (int pos = 0; pos + BLOCK <= (int) SR; pos += BLOCK)
        {
            for (int i = 0; i < BLOCK; ++i) in[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (pos + i) / SR);
            const float r = std::max (0.05f, 1.0f - (float) (pos + BLOCK) / (float) (0.5 * SR));
            float* op[16]; for (int c = 0; c < 16; ++c) { std::fill (o2[(size_t) c].begin(), o2[(size_t) c].end(), 0.0f); op[c] = o2[(size_t) c].data(); }
            dp.process (in.data(), BLOCK, { r, 0.0f, 0.0f }, 12.0f, op, nullptr, 0.0f);
            std::copy (o2[0].begin(), o2[0].end(), w.begin() + pos);
        }
        fp::DistanceSource::withWalls = true;
        int zc = 0;
        for (size_t i = (size_t) (0.1 * SR); i < (size_t) (0.4 * SR); ++i) if (w[i] <= 0 && w[i + 1] > 0) ++zc;
        const double hz = zc / 0.3;
        check (std::abs (hz - 1000.0 / (1.0 - 24.0 / 343.0)) < 20.0, "flying in at 24 m/s, 1 kHz sounds at 1075 Hz (Doppler)", juce::String (hz, 1) + " Hz");
    }

    std::printf ("the granulator\n");
    {
        FreakiePieProcessor p;
        allOff (p);
        setParam (p, ids::grMix, 1.0f);
        p.setGrainHome (0, { 0.0f, 0.95f, 0.0f });
        setParam (p, ids::grPan, 0.0f);   // grain spread 0: every grain right at its place
        auto r = render (p, 16, 3.0, stereoNoise, false);
        check (r.finite && ratio (p, { 0, 1, 0 }) > 15.0, "spread 0: the grains are a point on the left", juce::String (ratio (p, { 0, 1, 0 }), 1) + " dB");
        setParam (p, ids::grPan, 1.0f);
        r = render (p, 16, 3.0, stereoNoise, false);
        check (std::abs (ratio (p, { 0, 1, 0 })) < 6.0, "spread 1: they land all round", juce::String (ratio (p, { 0, 1, 0 }), 1) + " dB left / right");
        setParam (p, ids::grPan, 0.25f);

        // pitch: a 440 Hz tone through grains an octave up comes out at 880, with no clicks
        allOff (p); setParam (p, ids::grMix, 1.0f); setParam (p, ids::grOctave, 1.0f); setParam (p, ids::grRandOct, 0.0f);
        auto tone = [] (int i) { const float v = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / SR); return std::pair<float, float> { v, v }; };
        r = render (p, 16, 3.0, tone);
        int zc = 0; double maxStep = 0, ss = 0;
        const auto& wv = r.out[0];
        for (size_t i = (size_t) SR; i + 1 < wv.size(); ++i)
        {
            if (wv[i] <= 0 && wv[i + 1] > 0) ++zc;
            ss += (double) wv[i] * wv[i];
            if (i > (size_t) SR + 1) maxStep = std::max (maxStep, (double) std::abs (wv[i + 1] - 2 * wv[i] + wv[i - 1]));
        }
        const double hz = zc / ((double) wv.size() / SR - 1.0), wr = std::sqrt (ss / ((double) wv.size() - SR));
        // a pure 880 Hz sine of this rms has a second difference of about rms * 1.41 * (2 pi 880 / sr)^2 = 0.19 rms
        check (std::abs (hz - 880.0) < 25.0, "an octave up: 440 Hz in, 880 Hz out", juce::String (hz, 1) + " Hz");
        check (maxStep < 0.6 * wr, "smooth: no clicks at the grain edges", "worst second difference " + juce::String (maxStep / wr, 2) + " x rms");
    }

    std::printf ("the reverb\n");
    {
        fp::ambi::SphereReverb rv;
        rv.prepare (SR);
        rv.setDecay (3.0f);
        const int N = (int) (5.0 * SR);
        std::vector<std::vector<float>> out (16, std::vector<float> ((size_t) N));
        std::vector<std::vector<float>> in (16, std::vector<float> (BLOCK, 0.0f));
        const auto y = ambi::sh ({ 0, 1, 0 });
        for (int pos = 0; pos < N; pos += BLOCK)
        {
            const float* ip[16]; float* op[16];
            for (int c = 0; c < 16; ++c) { std::fill (in[(size_t) c].begin(), in[(size_t) c].end(), 0.0f); if (pos == 0) in[(size_t) c][0] = y[(size_t) c]; ip[c] = in[(size_t) c].data(); op[c] = out[(size_t) c].data() + pos; }
            rv.process (ip, op, std::min (BLOCK, N - pos));
        }
        // energy decay of the omni channel, in the band around 500 Hz (the shelves let the highs go
        // faster and the lows a touch slower, on purpose): the slope from -5 to -25 dB gives the decay time
        std::vector<double> mid ((size_t) N);
        {
            fp::Biquad bp;
            const double w0 = 2.0 * juce::MathConstants<double>::pi * 500.0 / SR, al = std::sin (w0) / 2.0;
            bp.set (al, 0.0, -al, 1.0 + al, -2.0 * std::cos (w0), 1.0 - al);
            for (int i = 0; i < N; ++i) mid[(size_t) i] = bp.process (out[0][(size_t) i]);
        }
        std::vector<double> edc ((size_t) N);
        double acc = 0;
        for (int i = N - 1; i >= 0; --i) { acc += mid[(size_t) i] * mid[(size_t) i]; edc[(size_t) i] = acc; }
        int t5 = -1, t25 = -1;
        for (int i = 0; i < N; ++i)
        {
            const double l = 10.0 * std::log10 (edc[(size_t) i] / edc[0]);
            if (t5 < 0 && l < -5.0) t5 = i;
            if (t25 < 0 && l < -25.0) { t25 = i; break; }
        }
        const double t60 = t25 > t5 && t5 >= 0 ? 3.0 * (t25 - t5) / SR : 0.0;
        check (std::abs (t60 - 3.0) < 0.45, "decay 3 s: at 500 Hz it takes about 3 s to fall 60 dB", juce::String (t60, 2) + " s");
        // smooth, not grainy or metallic: the 10 ms envelope after 150 ms follows its decay closely
        double dev = 0; int cnt = 0;
        const int win = (int) (0.01 * SR);
        for (int s0 = (int) (0.15 * SR); s0 + win < (int) (1.5 * SR); s0 += win)
        {
            double e = 0, e2 = 0;
            for (int i = s0; i < s0 + win; ++i) e += (double) out[0][(size_t) i] * out[0][(size_t) i];
            for (int i = s0 + win; i < s0 + 2 * win; ++i) e2 += (double) out[0][(size_t) i] * out[0][(size_t) i];
            dev += std::abs (10.0 * std::log10 ((e2 + 1e-20) / (e + 1e-20)) + 600.0 * (double) win / SR / 3.0);
            ++cnt;
        }
        check (dev / cnt < 2.0, "dense: the tail's envelope is smooth", juce::String (dev / cnt, 2) + " dB average wobble per 10 ms");
        // after half a second it has spread all round (a diffuse field)
        double eL = 0, eR = 0;
        for (int i = (int) (0.5 * SR); i < (int) (1.5 * SR); ++i)
        {
            float l = 0, r = 0;
            const auto yl = ambi::sh ({ 0, 1, 0 }), yr = ambi::sh ({ 0, -1, 0 });
            for (int c = 0; c < 4; ++c) { l += yl[(size_t) c] * out[(size_t) c][(size_t) i]; r += yr[(size_t) c] * out[(size_t) c][(size_t) i]; }
            eL += (double) l * l; eR += (double) r * r;
        }
        check (std::abs (10.0 * std::log10 (eL / eR)) < 3.0, "the tail surrounds you", juce::String (10.0 * std::log10 (eL / eR), 2) + " dB left / right after 0.5 s");
    }

    std::printf ("loudness and the track\n");
    {
        FreakiePieProcessor p;
        allOff (p);
        juce::Random rr (5);
        auto gen = [&] (int) { const float a = (rr.nextFloat() * 2.0f - 1.0f) * 0.1f, b = (rr.nextFloat() * 2.0f - 1.0f) * 0.1f; return std::pair<float, float> { a, b }; };
        auto r = render (p, 16, 2.0, gen);
        const double gain = db (rms (r.out[0], (size_t) SR) / (0.1 / std::sqrt (3.0)));
        check (gain > -3.0 && gain < 4.0, "a stereo input comes out at about its own level (W)", juce::String (gain, 1) + " dB");
        auto r2 = render (p, 2, 0.5, gen);
        check (p.ambiFallback.load() && r2.finite, "on a 2-channel track it says so (and sends the first two channels)");
    }

    std::printf ("gain staging (stereo noise in at -18 dBFS per channel, uncorrelated; W out)\n");
    {
        juce::Random gr (9);
        const float amp = 0.125f * std::sqrt (3.0f) * 1.0f;   // uniform noise this wide: -18 dBFS rms
        auto noise18 = [&] (int) { return std::pair<float, float> { (gr.nextFloat() * 2.0f - 1.0f) * amp, (gr.nextFloat() * 2.0f - 1.0f) * amp }; };
        const double inDb = db (amp / std::sqrt (3.0));
        double dryDb = 0;
        auto measure = [&] (const char* what, std::function<void (FreakiePieProcessor&)> setup, double lo, double hi, bool relToDry)
        {
            FreakiePieProcessor p;
            allOff (p);
            setup (p);
            auto r = render (p, 16, 4.0, noise18);
            const double w = db (rms (r.out[0], (size_t) (2 * SR)));
            float pk = 0; for (size_t i = (size_t) (2 * SR); i < r.out[0].size(); ++i) pk = std::max (pk, std::abs (r.out[0][i]));
            const double rel = w - (relToDry ? dryDb : inDb);
            std::printf ("    %-44s W %6.1f dBFS  (%+5.1f dB vs %s)  peak %6.1f dBFS\n", what, w, rel, relToDry ? "dry" : "input", db (pk));
            check (r.finite && rel >= lo && rel <= hi, (juce::String ("  levels: ") + what).toRawUTF8());
            return w;
        };
        dryDb = measure ("dry, one grain, effects off", [] (FreakiePieProcessor&) {}, -2.0, 2.0, false);
        measure ("spectral smear, mix 50 %", [] (FreakiePieProcessor& p) { setParam (p, ids::sgMix, 0.5f); }, -4.0, 2.0, true);
        measure ("pitch shifter +7, mix 50 %", [] (FreakiePieProcessor& p) { setParam (p, ids::psMix, 0.5f); }, -4.0, 2.0, true);
        measure ("granulator, mix 50 %", [] (FreakiePieProcessor& p) { setParam (p, ids::grMix, 0.5f); }, -4.0, 2.0, true);
        measure ("granulator, mix 100 % (grains alone)", [] (FreakiePieProcessor& p) { setParam (p, ids::grMix, 1.0f); }, -5.0, 2.0, true);
        measure ("reverb, mix 30 %", [] (FreakiePieProcessor& p) { setParam (p, ids::rvMix, 0.3f); }, -3.0, 2.0, true);
        measure ("reverb, mix 100 % (wet alone)", [] (FreakiePieProcessor& p) { setParam (p, ids::rvMix, 1.0f); }, -6.0, 2.0, true);
        measure ("gumball machine, stutter 60 %", [] (FreakiePieProcessor& p) { setParam (p, ids::stOwned, 1.0f); setParam (p, ids::stAmount, 0.6f); }, -3.0, 2.0, true);
        for (int e = 0; e < fp::StoreFX::kNum; ++e)
        {
            const juce::String nm = juce::String ("store: ") + fp::StoreFX::infos()[(size_t) e].name;
            measure (nm.toRawUTF8(), [e] (FreakiePieProcessor& p) { setParam (p, ids::store (e, ids::storeOwned), 1.0f); }, -3.0, 4.0, true);
        }
        measure ("4 grains orbiting, copies spatial, smear 30 %", [] (FreakiePieProcessor& p) { setParam (p, ids::grains, 4.0f); setParam (p, ids::flight, 1.0f); setParam (p, ids::sgMix, 0.3f); }, -3.0, 3.0, true);
        measure ("16 grains, smear + pitch 50 %", [] (FreakiePieProcessor& p) { setParam (p, ids::grains, 16.0f); setParam (p, ids::flight, 1.0f); setParam (p, ids::sgMix, 0.5f); setParam (p, ids::psMix, 0.5f); }, -4.0, 3.0, true);
        measure ("the default preset (Sugarcube Corner)", [] (FreakiePieProcessor& p) { p.setCurrentProgram (0); }, -3.0, 3.0, true);
        measure ("copies mode, 4 grains", [] (FreakiePieProcessor& p) { setParam (p, ids::grainMode, 2.0f); setParam (p, ids::grains, 4.0f); setParam (p, ids::flight, 1.0f); }, -3.0, 3.0, true);
        // at a normal level the leveler and limiter should do nothing
        {
            FreakiePieProcessor p;
            p.setCurrentProgram (0);
            render (p, 16, 3.0, noise18, false);
            check (p.levelerGainNow() > 0.999f && p.limiterGainNow() > 0.999f, "  levels: at -18 dBFS the leveler and limiter stay out of it",
                   "leveler " + juce::String (db (p.levelerGainNow()), 2) + " dB, limiter " + juce::String (db (p.limiterGainNow()), 2) + " dB");
        }
    }

    std::printf ("many grains, every way of flying, all the weather\n");
    {
        const char* names[] = { "still", "orbit", "lorenz", "flock", "random" };
        for (int mode = 0; mode < 5; ++mode)
        {
            FreakiePieProcessor p;
            p.setCurrentProgram (0);
            setParam (p, ids::grains, 16.0f);
            setParam (p, ids::deviate, 1.0f);
            setParam (p, ids::flight, (float) mode);
            setParam (p, ids::flightSpeed, 0.8f);
            setParam (p, ids::stOwned, 1.0f); setParam (p, ids::stAmount, 0.6f);
            setParam (p, ids::store (fp::StoreFX::delay, ids::storeOwned), 1.0f);
            setParam (p, ids::cloudy, 1.0f); setParam (p, ids::rain, 1.0f);
            auto r = render (p, 16, 8.0, plucks, false);
            // the grains should be spread out (not all on top of each other)
            float minD = 9.0f, maxR = 0.0f;
            for (int a = 0; a < 16; ++a)
            {
                const Vec3 pa { p.grainView[(size_t) a].x.load(), p.grainView[(size_t) a].y.load(), p.grainView[(size_t) a].z.load() };
                maxR = std::max (maxR, pa.length());
                for (int b = a + 1; b < 16; ++b)
                    minD = std::min (minD, (pa - Vec3 { p.grainView[(size_t) b].x.load(), p.grainView[(size_t) b].y.load(), p.grainView[(size_t) b].z.load() }).length());
            }
            const double rt = r.cpu / r.seconds;
            check (r.finite && r.peak <= 0.892f, (juce::String ("16 grains, ") + names[mode] + ": finite, under the ceiling").toRawUTF8(),
                   "peak " + juce::String (db (r.peak), 1) + " dBFS, " + juce::String (rt * 100.0, 1) + " % of real time, inside r <= " + juce::String (maxR, 2)
                   + ", closest pair " + juce::String (minD, 2));
            check (maxR <= 1.0001f, "stays inside the sphere");
        }
    }

    std::printf ("scatter: one chain, its grains flying to 8 places\n");
    {
        FreakiePieProcessor p;
        setParam (p, ids::grainMode, 1.0f);
        setParam (p, ids::grains, 8.0f);
        setParam (p, ids::flight, 1.0f);   // orbit
        setParam (p, ids::grMix, 1.0f);
        setParam (p, ids::grSize, 300.0f);
        setParam (p, ids::spread, 1.0f);
        auto r = render (p, 16, 6.0, stereoNoise, false);
        float lo = 1e9f, hi = 0.0f;
        for (auto d : { Vec3 { 1, 0, 0 }, Vec3 { -1, 0, 0 }, Vec3 { 0, 1, 0 }, Vec3 { 0, -1, 0 } })
        { lo = std::min (lo, p.energy.levelAt (d)); hi = std::max (hi, p.energy.levelAt (d)); }
        check (r.finite && r.peak <= 0.892f && hi > 0.0f, "scatter plays", juce::String (r.cpu / r.seconds * 100.0, 1) + " % of real time");
        check (db (hi / std::max (1e-12f, lo)) < 15.0, "its grains are all round, not in one spot", "spread " + juce::String (0.5 * db (hi / std::max (1e-12f, lo)), 1) + " dB between the four sides");
    }

    std::printf ("snow and heat on 16 channels\n");
    {
        FreakiePieProcessor p;
        setParam (p, ids::grains, 4.0f);
        setParam (p, ids::snow, 1.0f);
        setParam (p, ids::snowIn, 5.0f);
        auto r = render (p, 16, 8.0, plucks, false);
        check (r.finite && r.peak <= 0.892f, "snow on the scene", juce::String (r.cpu / r.seconds * 100.0, 1) + " % of real time");
        setParam (p, ids::snow, 0.0f); setParam (p, ids::heat, 1.0f);
        r = render (p, 16, 6.0, plucks, false);
        check (r.finite && r.peak <= 0.892f, "heat on the scene", juce::String (r.cpu / r.seconds * 100.0, 1) + " % of real time");
    }

    std::printf ("copies mode (each grain the whole shop, dry and all)\n");
    {
        FreakiePieProcessor p;
        p.setCurrentProgram (0);
        setParam (p, ids::grainMode, 2.0f); setParam (p, ids::grains, 16.0f); setParam (p, ids::flight, 3.0f); setParam (p, ids::deviate, 1.0f);
        auto r = render (p, 16, 6.0, plucks, false);
        check (r.finite && r.peak <= 0.892f, "16 whole copies flocking", juce::String (r.cpu / r.seconds * 100.0, 1) + " % of real time");
    }

    std::printf ("demo renders (for listening) in %s\n", outDir.getFullPathName().toRawUTF8());
    {
        auto scene = [&] (const char* file, std::function<void (FreakiePieProcessor&)> setup)
        {
            FreakiePieProcessor p;
            p.setCurrentProgram (0);
            setup (p);
            auto r = render (p, 16, 20.0, plucks);
            writeWav (outDir.getChildFile (file), r.out);
            check (r.finite, (juce::String ("wrote ") + file).toRawUTF8());
        };
        scene ("mellyloon_spatial_lorenz.wav", [] (FreakiePieProcessor& p)
        {
            setParam (p, ids::grains, 4.0f); setParam (p, ids::flight, 2.0f); setParam (p, ids::deviate, 0.7f);
            setParam (p, ids::stOwned, 1.0f); setParam (p, ids::stAmount, 0.5f); setParam (p, ids::grMix, 0.5f);
        });
        scene ("mellyloon_scatter_flock.wav", [] (FreakiePieProcessor& p)
        {
            setParam (p, ids::grainMode, 1.0f); setParam (p, ids::grains, 12.0f); setParam (p, ids::flight, 3.0f);
            setParam (p, ids::grMix, 0.8f); setParam (p, ids::grSize, 180.0f); setParam (p, ids::grPan, 0.15f);
        });
        scene ("mellyloon_copies_orbit.wav", [] (FreakiePieProcessor& p)
        {
            setParam (p, ids::grainMode, 2.0f); setParam (p, ids::grains, 4.0f); setParam (p, ids::flight, 1.0f); setParam (p, ids::deviate, 0.6f);
        });
    }

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}

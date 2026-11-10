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
static Run render (FreakiePieProcessor& p, int outCh, double seconds, Gen gen, bool keep = true, bool fresh = true)
{
    if (fresh)   // (fresh = false carries on from the last render without preparing again)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (outCh > 2 ? juce::AudioChannelSet::discreteChannels (outCh) : juce::AudioChannelSet::stereo());
        p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (SR, BLOCK);
        p.prepareToPlay (SR, BLOCK);
    }
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
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // (every line out as it happens, so a crash shows where)
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

    std::printf ("higher orders (up to 7th, 64 channels)\n");
    {
        // the harmonics themselves, at every order: SN3D means each order l has mean square 1/(2l+1) over
        // the sphere, and every pair of channels is orthogonal (averaged over 20000 even points)
        const auto pts = ambi::fibonacci (20000);
        std::vector<double> gram ((size_t) ambi::kCh * ambi::kCh, 0.0);
        for (auto& d : pts)
        {
            const auto y = ambi::sh (d, 7);
            for (int a = 0; a < ambi::kCh; ++a)
                for (int b = a; b < ambi::kCh; ++b) gram[(size_t) (a * ambi::kCh + b)] += (double) y[(size_t) a] * y[(size_t) b];
        }
        double worstNorm = 0, worstOrtho = 0;
        for (int a = 0; a < ambi::kCh; ++a)
            for (int b = a; b < ambi::kCh; ++b)
            {
                const double v = gram[(size_t) (a * ambi::kCh + b)] / (double) pts.size();
                if (a == b) worstNorm = std::max (worstNorm, std::abs (v * (2 * ambi::orderOf (a) + 1) - 1.0));
                else worstOrtho = std::max (worstOrtho, std::abs (v));
            }
        check (worstNorm < 0.002 && worstOrtho < 0.002, "all 64 harmonics to 7th order: SN3D scaling and orthogonal",
               "worst scaling error " + juce::String (worstNorm, 5) + ", worst overlap " + juce::String (worstOrtho, 5));
        // and two written out by hand (4th order): ACN 20 = (35 z^4 - 30 z^2 + 3) / 8, ACN 24 = sqrt(35) / 8 (x^4 - 6 x^2 y^2 + y^4)
        const auto d = ambi::fromAzEl (0.6f, 0.3f);
        const auto y = ambi::sh (d, 4);
        const double a20 = (35.0 * std::pow (d.z, 4) - 30.0 * d.z * d.z + 3.0) / 8.0;
        const double a24 = std::sqrt (35.0) / 8.0 * (std::pow (d.x, 4) - 6.0 * d.x * d.x * d.y * d.y + std::pow (d.y, 4));
        check (std::abs (y[20] - a20) < 1e-5 && std::abs (y[24] - a24) < 1e-5, "4th order written out by hand matches",
               juce::String (y[20], 5) + " / " + juce::String (a20, 5) + ", " + juce::String (y[24], 5) + " / " + juce::String (a24, 5));

        // the plug-in at 5th and 7th order: every output channel is its harmonic, as at 3rd
        for (int order : { 5, 7 })
        {
            const int nch = ambi::chFor (order);
            double worst = 0;
            fp::DistanceSource::withWalls = false;
            for (auto dd : { ambi::fromAzEl (0.7f, 0.4f), ambi::fromAzEl (-2.3f, -0.6f) })
            {
                FreakiePieProcessor p;
                allOff (p);
                setParam (p, ids::ambiOrder, (float) (order - 1));
                p.srcX.store (dd.x); p.srcY.store (dd.y); p.srcZ.store (dd.z);
                auto r = render (p, nch, 1.0, stereoNoise);
                const auto yy = ambi::sh (dd, order);
                double w2 = 0;
                for (size_t i = (size_t) SR / 2; i < r.out[0].size(); ++i) w2 += (double) r.out[0][i] * r.out[0][i];
                for (int c = 0; c < nch; ++c)
                {
                    double cw = 0;
                    for (size_t i = (size_t) SR / 2; i < r.out[0].size(); ++i) cw += (double) r.out[(size_t) c][i] * r.out[0][i];
                    worst = std::max (worst, std::abs (cw / w2 - yy[(size_t) c]));
                }
                if (p.orderNow.load() != order) worst = 1.0;
            }
            fp::DistanceSource::withWalls = true;
            check (worst < 0.005, (juce::String (order) + (order == 5 ? "th" : "th") + " order: all " + juce::String (nch) + " channels are their harmonics").toRawUTF8(),
                   "worst error " + juce::String (worst, 5));
        }
        // the order asked for, but a track with fewer channels: the highest that fits
        {
            FreakiePieProcessor p;
            allOff (p);
            setParam (p, ids::ambiOrder, 6.0f);   // 7th
            render (p, 36, 0.2, stereoNoise, false);
            check (p.orderNow.load() == 5, "7th order asked for on a 36-channel track: 5th comes out", "order " + juce::String (p.orderNow.load()));
        }
        // sharper: a decoder of the same order hears a point source narrower at 7th order than at 3rd
        auto spread = [&] (int order)
        {
            const auto dirs = ambi::fibonacci (2000);
            const auto D = ambi::samplingDecoder (dirs, order);
            const auto y = ambi::sh ({ 1, 0, 0 }, order);
            double e = 0, ec = 0;
            for (size_t k = 0; k < dirs.size(); ++k)
            {
                double v = 0;
                for (int c = 0; c < ambi::chFor (order); ++c) v += D[k][(size_t) c] * y[(size_t) c];
                e += v * v;
                if (dirs[k].x > std::cos (0.35)) ec += v * v;   // within 20 degrees
            }
            return ec / e;
        };
        check (spread (7) > spread (3) + 0.2, "a point is sharper at 7th order (energy within 20 degrees of it)",
               juce::String (juce::roundToInt (spread (3) * 100.0)) + " % at 3rd order, " + juce::String (juce::roundToInt (spread (5) * 100.0)) + " % at 5th, " + juce::String (juce::roundToInt (spread (7) * 100.0)) + " % at 7th");
        // the whole sphere at 7th order: 16 grains, the reverb, the weather; finite, under the ceiling, and what it costs
        for (int order : { 5, 7 })
        {
            FreakiePieProcessor p;
            p.setCurrentProgram (0);
            setParam (p, ids::ambiOrder, (float) (order - 1));
            setParam (p, ids::grains, 16.0f); setParam (p, ids::flight, 1.0f); setParam (p, ids::flightSpeed, 0.8f);
            setParam (p, ids::cloudy, 1.0f); setParam (p, ids::rain, 1.0f);
            auto r = render (p, ambi::chFor (order), 6.0, plucks, false);
            check (r.finite && r.peak <= 0.892f, (juce::String ("16 grains at ") + juce::String (order) + "th order: finite, under the ceiling").toRawUTF8(),
                   "peak " + juce::String (db (r.peak), 1) + " dBFS, " + juce::String (r.cpu / r.seconds * 100.0, 1) + " % of real time");
        }
        {
            // the snow on all 64 channels (the heaviest master stage)
            FreakiePieProcessor p;
            allOff (p);
            setParam (p, ids::ambiOrder, 6.0f);
            setParam (p, ids::snowIn, 5.0f); setParam (p, ids::snow, 1.0f);
            auto r = render (p, 64, 6.0, plucks, false);
            check (r.finite && r.peak <= 0.892f, "snow at 7th order: finite, under the ceiling", juce::String (r.cpu / r.seconds * 100.0, 1) + " % of real time");
        }
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
        check (db (small / big) > 20.0, "a small bubble (1 m) is much louder than a big one (50 m)", juce::String (db (small / big), 1) + " dB");

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

        // Doppler off: the same flight keeps its pitch
        auto flyIn = [&] (bool dop)
        {
            fp::DistanceSource d2;
            d2.prepare (SR, BLOCK);
            d2.doppler = dop;
            fp::DistanceSource::withWalls = false;
            std::vector<float> w2 ((size_t) SR, 0.0f);
            for (int pos = 0; pos + BLOCK <= (int) SR; pos += BLOCK)
            {
                for (int i = 0; i < BLOCK; ++i) in[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (pos + i) / SR);
                const float r = std::max (0.05f, 1.0f - (float) (pos + BLOCK) / (float) (0.5 * SR));
                float* op[16]; for (int c = 0; c < 16; ++c) { std::fill (o2[(size_t) c].begin(), o2[(size_t) c].end(), 0.0f); op[c] = o2[(size_t) c].data(); }
                d2.process (in.data(), BLOCK, { r, 0.0f, 0.0f }, 12.0f, op, nullptr, 0.0f);
                std::copy (o2[0].begin(), o2[0].end(), w2.begin() + pos);
            }
            fp::DistanceSource::withWalls = true;
            int z = 0;
            for (size_t i = (size_t) (0.1 * SR); i < (size_t) (0.4 * SR); ++i) if (w2[i] <= 0 && w2[i + 1] > 0) ++z;
            return z / 0.3;
        };
        const double still = flyIn (false);
        check (std::abs (still - 1000.0) < 6.0, "Doppler off: the same flight stays at 1 kHz", juce::String (still, 1) + " Hz");

        // 50 m: the biggest bubble's far side and its walls still fit in the delay line
        fp::DistanceSource far;
        far.prepare (SR, BLOCK);
        std::vector<std::vector<float>> o3 (16, std::vector<float> ((size_t) (0.6 * SR), 0.0f));
        for (int pos = 0; pos + BLOCK <= (int) o3[0].size(); pos += BLOCK)
        {
            std::fill (in.begin(), in.end(), 0.0f);
            if (pos == 0) in[0] = 1.0f;
            float* op[16]; for (int c = 0; c < 16; ++c) op[c] = o3[(size_t) c].data() + pos;
            far.process (in.data(), BLOCK, { 1.0f, 0.0f, 0.0f }, 50.0f, op, nullptr, 0.0f);
        }
        int pk = 0; double bst = 0;
        for (size_t i = 0; i < o3[0].size(); ++i) if (std::abs (o3[0][i]) > bst) { bst = std::abs (o3[0][i]); pk = (int) i; }
        check (std::abs (pk - 50.0 / 343.0 * SR) < 3.0, "50 m away it arrives 146 ms later", juce::String (pk / SR * 1000.0, 1) + " ms");
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
        const char* names[] = { "still", "orbit", "lorenz", "flock", "random", "spiral", "lissajous", "rossler", "breathe", "fall", "ping-pong" };
        for (int mode = 0; mode < 11; ++mode)
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
            if (mode > 0)
            {
                std::vector<Vec3> before;
                for (int a = 0; a < 16; ++a) before.push_back ({ p.grainView[(size_t) a].x.load(), p.grainView[(size_t) a].y.load(), p.grainView[(size_t) a].z.load() });
                render (p, 16, 1.5, plucks, false, false);
                float moved = 0;
                for (int a = 0; a < 16; ++a) moved = std::max (moved, (Vec3 { p.grainView[(size_t) a].x.load(), p.grainView[(size_t) a].y.load(), p.grainView[(size_t) a].z.load() } - before[(size_t) a]).length());
                check (moved > 0.05f, "and the grains fly", "furthest move in 1.5 s " + juce::String (moved, 2));
            }
        }
    }

    std::printf ("the sonic rainboom in the sphere\n");
    {
        auto silence = [] (int) { return std::pair<float, float> { 0.0f, 0.0f }; };
        // where a stretch of the output points (the W-weighted first-order intensity) and how sharply
        // (1: a single point, 0: all round you)
        struct Aim { Vec3 dir; double sharp; };
        auto aim = [] (const Run& r, double from, double to)
        {
            double ix = 0, iy = 0, iz = 0, e = 0;
            for (size_t i = (size_t) (from * SR); i < std::min (r.out[0].size(), (size_t) (to * SR)); ++i)
            {
                const double w = r.out[0][i];
                ix += w * r.out[3][i]; iy += w * r.out[1][i]; iz += w * r.out[2][i]; e += w * w;
            }
            const double l = std::sqrt (ix * ix + iy * iy + iz * iz);
            return Aim { Vec3 { (float) (ix / std::max (l, 1e-12)), (float) (iy / std::max (l, 1e-12)), (float) (iz / std::max (l, 1e-12)) }, l / std::max (e, 1e-12) };
        };
        auto dot = [] (Vec3 a, Vec3 b) { return (double) (a.x * b.x + a.y * b.y + a.z * b.z); };

        FreakiePieProcessor p;
        allOff (p);
        setParam (p, ids::balloonSfx, 0.0f);
        setParam (p, ids::rbLevel, 1.0f);
        fp::DistanceSource::withWalls = false;   // (the direct paths alone, to see where things are)
        render (p, 16, 0.2, silence, false);
        const int b0 = p.rainboomBooms.load();
        p.requestRainboom (0.7f);
        auto r = render (p, 16, 3.0, silence, true, false);
        fp::DistanceSource::withWalls = true;
        const Vec3 at { p.rbAtX.load(), p.rbAtY.load(), p.rbAtZ.load() };
        const auto early = aim (r, 0.15, 0.3), late = aim (r, 0.6, 0.69);
        const Vec3 from { p.rbFromX.load(), p.rbFromY.load(), p.rbFromZ.load() };
        check (dot (early.dir, from.normalised()) > 0.6 && dot (late.dir, at.normalised()) > 0.6,
               "the dive comes in from outside the glass and flies to where it breaks",
               "early in the dive " + juce::String (dot (early.dir, from.normalised()), 2) + " towards where it came from, late " + juce::String (dot (late.dir, at.normalised()), 2) + " towards where it breaks");
        const double tb = 0.7 + at.length() * 7.07 / 343.0;   // (it reaches you at the speed of sound)
        const auto boom = aim (r, tb, tb + 0.05);
        check (p.rainboomBooms.load() == b0 + 1 && dot (boom.dir, at.normalised()) > 0.8 && boom.sharp > 0.5,
               "the boom comes from where it broke", "cosine " + juce::String (dot (boom.dir, at.normalised()), 2) + ", sharpness " + juce::String (boom.sharp, 2));
        const auto ring = aim (r, tb + 0.9, tb + 1.6);
        check (ring.sharp < 0.6 * boom.sharp, "then the rainbow ring opens out past you and surrounds you",
               "sharpness " + juce::String (boom.sharp, 2) + " at the boom, " + juce::String (ring.sharp, 2) + " a second later");
        double pk = 0; bool fin = true;
        for (auto& ch : r.out) for (float v : ch) { pk = std::max (pk, (double) std::abs (v)); fin &= std::isfinite (v); }
        check (fin && pk <= 0.892, "finite, under the ceiling", juce::String (db (pk), 1) + " dBFS");

        // a grain flying fast enough breaks it where the grain is
        FreakiePieProcessor q;
        allOff (q);
        setParam (q, ids::grains, 4.0f); setParam (q, ids::flight, 1.0f); setParam (q, ids::flightSpeed, 1.0f);
        setParam (q, ids::spread, 1.0f);   // a 50 m bubble: the grains go fast
        setParam (q, ids::rbFast, 1.0f);
        const auto rq = render (q, 16, 6.0, silence, false);
        check (q.rainboomBooms.load() >= 1 && rq.finite, "a grain flying past 30 m/s sets one off", juce::String (q.rainboomBooms.load()) + " in 6 s");
        FreakiePieProcessor q2;
        allOff (q2);
        setParam (q2, ids::grains, 4.0f); setParam (q2, ids::flight, 1.0f); setParam (q2, ids::flightSpeed, 0.2f);
        setParam (q2, ids::rbFast, 1.0f);
        render (q2, 16, 6.0, silence, false);
        check (q2.rainboomBooms.load() == 0, "slow grains don't", juce::String (q2.rainboomBooms.load()) + " in 6 s");
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

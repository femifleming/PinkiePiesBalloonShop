#pragma once
#include "Utils.h"
#include "Scales.h"
#include <atomic>
#include <array>

namespace fp
{
//==============================================================================
// The playable balloons' sound.
//
//  * Pop balloons: while the pump is held, air hiss (on the output, or into the effects) and short plucked saws
//    on random notes of the selected scale (fed into the effect chain). A pop
//    is a loud, randomised burst modelled on a real one (see Pop below).
//
//  * Sound balloons (up to kVoices at once): while inflating, the plugin input is
//    recorded into the balloon's own buffer and heard through a lowpass that opens
//    as the balloon grows. On release the cutoff is held, recording stops, and the
//    capture is played as frozen saw-shaped grains (octave / random octave / fade
//    taken from the main grain controls), pitched by the balloon's semitone offset.
//
// The editor talks to this through the atomics below; the audio thread owns all
// buffers. A voice slot is only reused once the audio side reports it idle.
class BalloonFX
{
public:
    static constexpr int kVoices = 6;
    const ChalkShape* chalk = nullptr;   // the chalkboard grain shape (owned by the processor)

    struct VoiceCtl
    {
        std::atomic<int>   mode { 0 };        // editor's wish: 0 gone, 1 inflating (recording), 2 frozen (playing)
        std::atomic<float> size { 0.0f };     // 0..1 while inflating; sets the cutoff
        std::atomic<float> semis { 0.0f };    // pitch offset relative to the first balloon
        std::atomic<int>   audioMode { 0 };   // what the audio side is actually doing
        std::atomic<float> level { 0.0f };    // output level, for the balloon to wobble with
    };
    VoiceCtl voice[kVoices];

    std::atomic<bool>  pumping { false };     // pop pump held
    std::atomic<float> pumpSize { 0.0f };
    std::atomic<int>   popRequests { 0 };     // bump to fire a pop
    std::atomic<float> popSize { 1.0f };
    std::atomic<bool>  sfxOn { true };        // air hiss + pop sounds (the switch on the pump)

    // the main grain controls; the frozen sound balloons follow them too
    struct GrainSettings
    {
        int octave = 0; bool randomOctave = false; float fadeSeconds = 1.0f;
        float sizeMs = 120.0f, density = 7.0f, pan = 0.45f, reverse = 0.0f; int shape = 0;
        float position = 0.5f, positionRandom = 1.0f;   // where in the caught sound (0 start .. 1 end), and how far grains wander from it
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        cap = (int) (std::min (sr, 96000.0) * 13.0);   // 13 s per balloon (6.5 s at 192 kHz)
        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            for (auto& b : v.buf) b.assign ((size_t) cap, 0.0f);
            v.rng.seed (0xBA11 + (uint64_t) i * 7919);
            v.mode = 0; v.len = 0;
        }
        noise.seed (0xF00);
        hissBP.set (sr, 2600.0, 1.6);
        lastPop = popRequests.load();
        for (auto& p : pops) p.active = false;
        for (auto& p : plucks) p.on = false;
        ball.on = false;
        inflEnv = 0.0f;
        pluckTimer = 0;
        wasPumping = false;
    }

    // The inflation tone: while the pop balloon inflates, short plucked saws at random slow
    // intervals, each on a random note of the given scale (two octaves up from C3). Every pluck
    // gets its own wavefold, attack (mostly plucky, sometimes slower) and pan. Plucks ring out
    // after you let go. It is added to the *input of the effect chain* (so the gate, pitch
    // shifter, grains and reverb all play with it) and never reaches the sound-balloon sampler.
    void addInflationTone (float* L, float* R, int n, const Scale& scale) noexcept
    {
        const bool pump = pumping.load();
        const float size = std::clamp (pumpSize.load(), 0.0f, 1.3f);
        if (pump && ! wasPumping) pluckTimer = (int) (0.05 * sr);   // the first one comes straight away
        wasPumping = pump;

        for (int s = 0; s < n; ++s)
        {
            if (ball.on && --ball.countdown <= 0)
            {
                // a dropped ball: each bounce comes 0.85 times as soon as the last, and a little softer
                startVoice (ball.semis, ball.gain, ball.drive, ball.pan, 0.001f);
                ball.interval *= 0.85; ball.gain *= 0.85f;
                ball.countdown = (int) ball.interval;
                if (ball.interval < 0.025 * sr) ball.on = false;
            }
            if (pump && --pluckTimer <= 0)
            {
                // slow and irregular, a little busier as the balloon fills
                pluckTimer = std::max (1, (int) (sr * (double) noise.range (0.4f, 1.5f) * (1.15 - 0.45 * std::min (1.0, (double) size))));
                pluck (scale);
            }
            float yl = 0.0f, yr = 0.0f;
            for (auto& p : plucks)
            {
                if (! p.on) continue;
                if (p.attacking) { p.env += p.atk; if (p.env >= 1.0f) { p.env = 1.0f; p.attacking = false; } }
                else
                {
                    p.env *= p.dec;
                    if (p.env < 1.0e-4f) { p.on = false; continue; }
                }
                p.ph += p.dphi;
                if (p.ph >= 1.0) p.ph -= 1.0;
                const float saw = (float) (2.0 * p.ph - 1.0) - polyBlep (p.ph, p.dphi);
                const float y = 0.26f * p.amp * p.env * std::sin (0.5f * (float) kPi * p.drive * saw);   // the wavefold
                yl += y * p.gl;
                yr += y * p.gr;
            }
            L[s] += yl;
            R[s] += yr;
        }
    }

    static float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)       { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    // The air going in (hiss) and the pops, written into L/R (cleared first). The processor
    // either adds them to the output or feeds them into the effects (the pop sounds' right-click
    // menu); with the balloon sounds switched off it's silence.
    void renderSfx (float* L, float* R, int n) noexcept
    {
        // pops requested by the editor
        const int req = popRequests.load();
        while (lastPop != req)
        {
            ++lastPop;
            for (auto& p : pops)
                if (! p.active) { trigger (p, std::clamp (popSize.load(), 0.0f, 1.0f)); break; }
        }
        const bool sfx = sfxOn.load();
        if (! sfx) for (auto& p : pops) p.active = false;
        const bool pump = pumping.load() && sfx;
        const float size = std::clamp (pumpSize.load(), 0.0f, 1.3f);
        const float envCoef = (float) (1.0 - std::exp (-1.0 / (0.03 * sr)));
        for (int s = 0; s < n; ++s)
        {
            inflEnv += envCoef * ((pump ? 1.0f : 0.0f) - inflEnv);
            float yl = 0.0f, yr = 0.0f;
            if (inflEnv > 1.0e-4f)
            {
                const float hiss = 0.16f * inflEnv * hissBP.process (noise.bi()) * (0.5f + 0.5f * size);
                yl += hiss; yr += hiss;
            }
            float pl = 0.0f, pr = 0.0f;
            bool popping = false;
            for (auto& p : pops)
            {
                if (! p.active) continue;
                popping = true;
                float a, b;
                renderPop (p, a, b);
                pl += a; pr += b;
            }
            if (popping)
            {
                // loud: driven into a soft limiter, so the crack is as hot as it can be without clipping
                yl += 0.99f * std::tanh (pl);
                yr += 0.99f * std::tanh (pr);
            }
            L[s] = yl;
            R[s] = yr;
        }
    }

    // The sound balloons. grainL/grainR: where the frozen balloons' grains go (null: into out,
    // with everything else). The live monitor of a balloon that's still filling always goes to out.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const GrainSettings& gs,
                  float* grainL = nullptr, float* grainR = nullptr) noexcept
    {
        if (grainL == nullptr || grainR == nullptr) { grainL = outL; grainR = outR; }
        for (int i = 0; i < kVoices; ++i) voiceBlock (i, inL, inR, outL, outR, grainL, grainR, n, gs);
    }

    std::atomic<float> toneSemis { 0.0f };   // the last plucked note, for the UI
    std::atomic<int>   pluckCount { 0 };     // goes up by one per pluck
    std::atomic<int>   bounceCount { 0 };    // and per pluck that bounces

private:
    struct Grain { bool on = false; double pos = 0, rate = 1; int age = 0, len = 1, shape = 0; float gl = 1, gr = 1; };

    struct Voice
    {
        std::array<std::vector<float>, 2> buf;
        int len = 0, mode = 0;                       // 0 idle, 1 recording, 2 frozen, 3 fading away
        float x = 0.0f;                              // 0 = hearing the live input, 1 = hearing the frozen grains
        float fade = 1.0f;                           // removal fade
        float cutoff = 90.0f, cutoffSm = 90.0f;
        float ic1[2] {}, ic2[2] {};                  // 2-pole lowpass state, for the live monitor
        float gc1[2] {}, gc2[2] {};                  // and for the grains (same filter, kept apart so they can be routed)
        std::array<Grain, 40> grains;
        int nextSpawn = 0;
        float lvl = 0.0f;
        // loudness matching: how loud the caught sound was, how loud the frozen grains come out (both
        // mean squares), and the make-up gain that brings the one up to the other
        double recPow = 0.0;
        float outPow = 0.0f, makeup = 1.0f;
        Rng rng;
    };

    static float lowpass (float& s1, float& s2, float x, float a1, float a2, float a3) noexcept
    {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = flushDenormal (2.0f * v1 - s1);
        s2 = flushDenormal (2.0f * v2 - s2);
        return v2;
    }

    void voiceBlock (int i, const float* inL, const float* inR, float* outL, float* outR, float* grL, float* grR, int n, const GrainSettings& gs) noexcept
    {
        auto& ctl = voice[i];
        auto& v = voices[(size_t) i];
        const int want = ctl.mode.load();

        // state changes
        if (v.mode == 0 && want == 1)
        {
            v.mode = 1; v.len = 0; v.x = 0.0f; v.fade = 1.0f; v.lvl = 0.0f;
            v.recPow = 0.0; v.outPow = 0.0f; v.makeup = 1.0f;
            v.ic1[0] = v.ic1[1] = v.ic2[0] = v.ic2[1] = 0.0f;
            v.gc1[0] = v.gc1[1] = v.gc2[0] = v.gc2[1] = 0.0f;
            for (auto& g : v.grains) g.on = false;
            v.nextSpawn = 0;
        }
        else if (v.mode == 1 && want == 2)
            v.mode = v.len < (int) (0.15 * sr) ? 3 : 2;   // too short to hold anything: let it go
        else if ((v.mode == 1 || v.mode == 2) && want == 0)
            v.mode = 3;

        if (v.mode == 0) { ctl.audioMode.store (0); ctl.level.store (0.0f); return; }

        if (v.mode == 1)    // the cutoff follows the balloon while it fills; after that it's held
        {
            const float sz = std::clamp (ctl.size.load(), 0.0f, 1.0f);
            v.cutoff = (float) (90.0 * std::pow (18000.0 / 90.0, std::pow ((double) sz, 0.85)));
        }
        v.cutoffSm += 0.25f * (v.cutoff - v.cutoffSm);
        const double g = std::tan (kPi * std::min ((double) v.cutoffSm, sr * 0.45) / sr);
        const float k = 1.41421356f;
        const float a1 = (float) (1.0 / (1.0 + g * (g + k))), a2 = (float) g * a1, a3 = (float) g * a2;

        const float xInc = 1.0f / (float) std::max (1.0, (double) std::max (0.05f, gs.fadeSeconds) * sr);
        const float fadeOut = 1.0f / (float) (0.02 * sr);
        const float ratioBase = std::exp2 (ctl.semis.load() / 12.0f);
        float peak = 0.0f;

        for (int s = 0; s < n; ++s)
        {
            if (v.mode == 1 && v.len < cap)
            {
                v.buf[0][(size_t) v.len] = inL[s];
                v.buf[1][(size_t) v.len] = inR[s];
                v.recPow += 0.5 * ((double) inL[s] * inL[s] + (double) inR[s] * inR[s]);
                ++v.len;
            }
            if (v.mode == 2) v.x = std::min (1.0f, v.x + xInc);
            if (v.mode == 3) { v.fade -= fadeOut; if (v.fade <= 0.0f) { v.mode = 0; break; } }

            float gL = 0.0f, gR = 0.0f;
            if (v.x > 0.0f && v.len > 16)
            {
                if (--v.nextSpawn <= 0) spawn (v, ratioBase, gs);
                for (auto& gr : v.grains)
                {
                    if (! gr.on) continue;
                    const float t = (float) gr.age / (float) gr.len;
                    const float env = grainShape (gr.shape, t, chalk);
                    const int p0 = std::clamp ((int) gr.pos, 0, v.len - 1);
                    const float fr = (float) (gr.pos - p0);
                    const int p1 = std::min (p0 + 1, v.len - 1);
                    const float l = v.buf[0][(size_t) p0] + fr * (v.buf[0][(size_t) p1] - v.buf[0][(size_t) p0]);
                    const float r = v.buf[1][(size_t) p0] + fr * (v.buf[1][(size_t) p1] - v.buf[1][(size_t) p0]);
                    gL += env * gr.gl * l;
                    gR += env * gr.gr * r;
                    gr.pos += gr.rate;
                    if (++gr.age >= gr.len || gr.pos >= (double) (v.len - 2) || gr.pos < 1.0) gr.on = false;
                }
                const float norm = std::min (3.0f, 1.07f / std::sqrt (std::max (0.05f, gs.density * grainShapePower (gs.shape, chalk))));
                gL *= norm; gR *= norm;
            }

            const float mL = v.mode == 1 || v.x < 1.0f ? inL[s] : 0.0f;
            const float mR = v.mode == 1 || v.x < 1.0f ? inR[s] : 0.0f;
            // the filter is linear, so filtering the monitor and the grains apart sums to the same thing
            const float g8 = 0.8f * v.fade;
            const float mlo = lowpass (v.ic1[0], v.ic2[0], mL * (1.0f - v.x), a1, a2, a3) * g8;
            const float mro = lowpass (v.ic1[1], v.ic2[1], mR * (1.0f - v.x), a1, a2, a3) * g8;
            // the frozen grains come out as loud as the sound that was caught: the balloon's filter and
            // the grains' layering change the colour, not the level (a make-up gain, up to +12 dB,
            // gliding over about half a second so it never pumps)
            float glo = lowpass (v.gc1[0], v.gc2[0], gL * v.x, a1, a2, a3);
            float gro = lowpass (v.gc1[1], v.gc2[1], gR * v.x, a1, a2, a3);
            if (v.mode == 2 && v.len > 0)
            {
                v.outPow += 0.0002f * (0.5f * (glo * glo + gro * gro) - v.outPow);
                const float want = (float) (v.recPow / (double) v.len);
                const float target = v.outPow > 1.0e-9f ? std::clamp (std::sqrt (want / v.outPow), 0.25f, 4.0f) : v.makeup;
                v.makeup += 0.00004f * (target - v.makeup);
            }
            glo *= v.makeup * v.fade; gro *= v.makeup * v.fade;
            outL[s] += mlo; outR[s] += mro;
            grL[s] += glo;  grR[s] += gro;
            peak = std::max (peak, std::max (std::abs (mlo + glo), std::abs (mro + gro)));
        }
        v.lvl = std::max (peak, v.lvl * 0.9f);
        ctl.level.store (v.lvl);
        ctl.audioMode.store (v.mode == 3 ? 3 : v.mode);
    }

    void spawn (Voice& v, float ratioBase, const GrainSettings& gs) noexcept
    {
        v.nextSpawn = (int) (0.01 * sr);
        Grain* g = nullptr;
        for (auto& c : v.grains) if (! c.on) { g = &c; break; }
        if (g == nullptr) return;

        int oct = std::clamp (gs.octave, -2, 2);
        if (gs.randomOctave && v.rng.uni() < 0.3f) oct = std::clamp (oct + (v.rng.uni() < 0.5f ? -1 : 1), -2, 2);
        const double rate = (double) ratioBase * std::exp2 ((double) oct);
        const bool backwards = v.rng.uni() < gs.reverse;

        double L = sr * (double) gs.sizeMs * 0.001 * (double) v.rng.range (0.75f, 1.33f);
        const double usable = (double) v.len - 4.0;
        if (L * rate > usable * 0.9) L = usable * 0.9 / rate;
        if (L < 0.008 * sr) return;

        g->on = true;
        g->age = 0;
        g->len = (int) L;
        g->shape = gs.shape;
        const double where = std::clamp ((double) gs.position + (double) gs.positionRandom * ((double) v.rng.uni() - 0.5), 0.0, 1.0);
        const double start = where * std::max (0.0, usable - L * rate);
        g->rate = backwards ? -rate : rate;
        g->pos = backwards ? start + L * rate + 1.0 : start + 1.0;      // backwards grains start at their far end
        const float pan = v.rng.bi() * gs.pan;
        g->gl = 1.0f - std::max (0.0f, pan);
        g->gr = 1.0f + std::min (0.0f, pan);
        v.nextSpawn = std::max (1, (int) (L / std::max (0.5, (double) gs.density) * (double) v.rng.range (0.6f, 1.4f)));
    }

    // One balloon burst. Modelled on what a real pop is made of, every part randomised:
    //   tearing   a few micro-clicks as the rubber rips (first ~4 ms)
    //   N-wave    the pressure pulse itself: a sharp positive jump falling through zero (the bang)
    //   burst     broadband noise that starts very bright and darkens within ~20 ms
    //   ring      the torn membrane's short resonance
    //   thump     the air mass, a low pitch-dropping knock
    //   room      a little reflected tail
    struct Pop
    {
        bool active = false;
        int age = 0, nLen = 1, tearAt[5] {};
        float tearAmp[5] {}, gain = 1.0f, decay = 0.02f, ringDecay = 0.02f, roomDecay = 0.1f, room = 0.1f;
        float thumpHz = 60.0f, pan = 0.0f, lpL = 0.0f, lpR = 0.0f, roomL = 0.0f, roomR = 0.0f, bright = 14000.0f;
        double thumpPhase = 0.0;
        struct { float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0; } ring[2];
    };

    void trigger (Pop& p, float size) noexcept
    {
        p.active = true;
        p.age = 0;
        p.gain = (1.5f + 0.9f * size) * noise.range (0.85f, 1.15f);
        p.nLen = std::max (8, (int) (sr * 0.001 * (1.0 + 2.6 * size) * noise.range (0.75f, 1.3f)));
        p.decay = (0.010f + 0.022f * size) * noise.range (0.7f, 1.35f);
        p.ringDecay = p.decay * noise.range (0.8f, 1.8f);
        p.roomDecay = noise.range (0.06f, 0.16f);
        p.room = noise.range (0.05f, 0.14f);
        p.thumpHz = (90.0f - 45.0f * size) * noise.range (0.8f, 1.25f);
        p.bright = noise.range (9000.0f, 16000.0f);
        p.pan = noise.range (-0.25f, 0.25f);
        p.thumpPhase = 0.0;
        p.lpL = p.lpR = p.roomL = p.roomR = 0.0f;
        const int tears = 2 + noise.integer (4);
        for (int k = 0; k < 5; ++k)
        {
            p.tearAt[k] = k < tears ? (int) (sr * noise.range (0.0f, 0.0042f)) : -1;
            p.tearAmp[k] = noise.range (0.3f, 0.9f) * (noise.uni() < 0.5f ? -1.0f : 1.0f);
        }
        // the membrane rings higher for small balloons
        const double rf = (3200.0 - 1900.0 * size) * (double) noise.range (0.75f, 1.3f);
        const double g = std::tan (kPi * std::min (rf, sr * 0.45) / sr);
        for (auto& r : p.ring)
        {
            r.k = 1.0f / noise.range (2.0f, 5.0f);
            r.a1 = (float) (1.0 / (1.0 + g * (g + r.k))); r.a2 = (float) g * r.a1; r.a3 = (float) g * r.a2;
            r.ic1 = r.ic2 = 0.0f;
        }
    }

    void renderPop (Pop& p, float& outL, float& outR) noexcept
    {
        const float t = (float) p.age / (float) sr;
        float common = 0.0f;
        if (p.age < p.nLen) common += 1.0f - 2.0f * (float) p.age / (float) p.nLen;            // N-wave
        for (int k = 0; k < 5; ++k) if (p.age == p.tearAt[k]) common += p.tearAmp[k];          // tearing
        const double f = (double) p.thumpHz * (1.0 + 1.2 * std::exp (-t / 0.012));
        p.thumpPhase += kTwoPi * f / sr;
        common += 0.45f * (float) std::sin (p.thumpPhase) * std::exp (-t / 0.06f);           // thump

        // burst: bright noise, darkening fast (a one-pole lowpass whose cutoff falls)
        const float fc = 900.0f + p.bright * std::exp (-t / 0.008f);
        const float a = 1.0f - std::exp (-(float) kTwoPi * fc / (float) sr);
        const float env = std::exp (-t / p.decay);
        float ch[2];
        for (int c = 0; c < 2; ++c)
        {
            const float nz = noise.bi();
            float& lp = c == 0 ? p.lpL : p.lpR;
            lp += a * (nz - lp);
            auto& r = p.ring[c];
            const float v3 = nz - r.ic2, v1 = r.a1 * r.ic1 + r.a2 * v3, v2 = r.ic2 + r.a2 * r.ic1 + r.a3 * v3;
            r.ic1 = 2.0f * v1 - r.ic1; r.ic2 = 2.0f * v2 - r.ic2;
            const float ring = r.k * v1 * std::exp (-t / p.ringDecay);
            float& rm = c == 0 ? p.roomL : p.roomR;
            rm += 0.15f * (noise.bi() - rm);
            ch[c] = common + 1.3f * lp * env + 0.7f * ring + p.room * rm * std::exp (-t / p.roomDecay);
        }
        outL = p.gain * ch[0] * (1.0f - std::max (0.0f, p.pan));
        outR = p.gain * ch[1] * (1.0f + std::min (0.0f, p.pan));
        if (++p.age > (int) (sr * 0.6)) p.active = false;
    }

    struct BP   // TPT bandpass / highpass for the noise colours
    {
        float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0; bool hp = false;
        void set (double sr, double f, double q, bool highpass = false)
        {
            const double g = std::tan (kPi * f / sr);
            k = (float) (1.0 / q); hp = highpass;
            a1 = (float) (1.0 / (1.0 + g * (g + k))); a2 = (float) g * a1; a3 = (float) g * a2;
        }
        float process (float x) noexcept
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = flushDenormal (2.0f * v1 - ic1); ic2 = flushDenormal (2.0f * v2 - ic2);
            return hp ? x - k * v1 - v2 : k * v1;
        }
    };

    double sr = 48000.0;
    int cap = 1;
    std::array<Voice, kVoices> voices;
    std::array<Pop, 6> pops;
    int lastPop = 0;
    Rng noise;
    BP hissBP;
    float inflEnv = 0.0f;

    // one plucked saw
    struct Pluck { bool on = false, attacking = true; double ph = 0.0, dphi = 0.0; float env = 0.0f, atk = 1.0f, dec = 0.999f, drive = 1.0f, gl = 1.0f, gr = 1.0f, amp = 1.0f; };
    struct Ball { bool on = false; float semis = 0.0f, gain = 1.0f, drive = 1.0f, pan = 0.5f; double interval = 0.0; int countdown = 0; } ball;
    std::array<Pluck, 8> plucks;
    int pluckTimer = 0;
    bool wasPumping = false;

    void pluck (const Scale& scale) noexcept
    {
        const int nd = std::max (1, (int) scale.degrees.size());
        const int pick = noise.integer (nd * 2 + 1);                 // two octaves, plus the top
        const float semis = pick == nd * 2 ? 24.0f : 12.0f * (float) (pick / nd) + (float) (scale.degrees[(size_t) (pick % nd)].cents / 100.0);
        // mostly plucky (1 to 8 ms), sometimes a softer swell (30 to 150 ms)
        const float atkSec = noise.uni() < 0.65f ? noise.range (0.001f, 0.008f) : noise.range (0.03f, 0.15f);
        const float drive = 1.0f + 4.0f * noise.uni() * noise.uni();   // from barely folded to folded over a few times
        const float pan = noise.uni();
        startVoice (semis, 1.0f, drive, pan, atkSec);
        // now and then a pluck is dropped like a ball and bounces (after the bouncing-ball timing in
        // Mesker's study of Autechre): repeats closing in until they blur
        if (! ball.on && noise.uni() < 0.22f)
        {
            ball.on = true;
            ball.semis = semis; ball.gain = 0.8f; ball.drive = drive; ball.pan = pan;
            ball.interval = sr * (double) noise.range (0.2f, 0.38f);
            ball.countdown = (int) ball.interval;
            bounceCount.fetch_add (1);
        }
        toneSemis.store (semis);
        pluckCount.fetch_add (1);
    }

    void startVoice (float semis, float amp, float drive, float pan01, float atkSec) noexcept
    {
        // a free voice, or the quietest one
        Pluck* v = &plucks[0];
        for (auto& p : plucks) { if (! p.on) { v = &p; break; } if (p.env * p.amp < v->env * v->amp) v = &p; }
        v->dphi = 130.8128 * std::exp2 (semis / 12.0) / sr;
        v->ph = noise.uni();
        v->atk = 1.0f / std::max (1.0f, atkSec * (float) sr);
        const float ringSec = noise.range (0.35f, 1.2f);              // to -60 dB
        v->dec = (float) std::exp (-6.9078 / (ringSec * sr));
        v->drive = drive;
        const float pan = pan01 * (float) kPi * 0.5f;
        v->gl = 1.2f * std::cos (pan);
        v->gr = 1.2f * std::sin (pan);
        v->amp = amp;
        v->env = 0.0f;
        v->attacking = true;
        v->on = true;
    }
};
} // namespace fp

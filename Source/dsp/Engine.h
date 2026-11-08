#pragma once
#include "SpectralGate.h"
#include "PitchShifter.h"
#include "GrainShifter.h"
#include "FDNReverb.h"
#include "Scales.h"
#include "Stutter.h"
#include "StoreFX.h"
#include <array>

namespace fp
{
struct EngineParams
{
    float sgCutoffDb = -24.0f, sgMix = 0.35f;
    float sgAttackMs = 45.0f, sgSmearS = 1.8f, sgKnee = 4.0f, sgBlur = 0.0f, sgScatter = 0.0f, sgTilt = 0.0f;
    bool sgFreeze = false;
    float psShiftSemis = 7.0f, psFeedback = 0.35f, psMix = 0.3f; int psScale = 0;
    float grMix = 0.4f, grFade = 1.0f; int grOctave = 0; bool grRandOct = true, grFreeze = false;
    float grSizeMs = 120.0f, grDensity = 7.0f, grPan = 0.45f, grReverse = 0.0f; int grShape = 0;
    float grPos = 0.5f, grPosRand = 1.0f;
    float rvDecay = 4.0f, rvMix = 0.3f;
    float masterFb = 0.0f;
};

//==============================================================================
// The whole FreakiePie chain, processed sample by sample so the global
// output -> input feedback loop has no block-size dependence.
//   in (+ feedback) -> [the shelf, in your order] -> out
// The shelf holds the spectral smear, the pitch shifter, the grains, the reverb, the stutter and
// whatever you've bought at the store; setOrder() rearranges them (the change dips the sound for
// 16 ms so it never clicks). The default is spectral, pitch, grains, reverb, stutter, then the store.
// Every dry/wet is equal-power and every control is ramped. With all mixes at 0
// and feedback at 0 the output is bit-identical to the input.
class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        sg.prepare (sr);
        ps.prepare (sr);
        gr.prepare (sr);
        rv.prepare (sr);

        for (auto* r : { &sgMix, &psMix, &psFb, &grMix, &rvMix, &masterFb })
            r->setup (sr, 0.04);
        sgCut.setup (sr, 0.05);

        for (int c = 0; c < 2; ++c)
        {
            fbDC[c].setup (sr, 15.0);
            fbHP[c].setHighpass (sr, 70.0, 0.707);
            fbLP[c].setup (sr, 14000.0);
        }
        dipStep = (float) (1.0 / (0.008 * sr));
        fbAtk = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));
        fbRel = (float) (1.0 - std::exp (-1.0 / (0.350 * sr)));
        setParams (params);   // the modules reset their own defaults in prepare(); put ours back
        snapToParams();
        reset();
    }

    void reset()
    {
        sg.reset(); ps.reset(); gr.reset(); rv.reset();
        for (int c = 0; c < 2; ++c) { fbDC[c].reset(); fbHP[c].reset(); fbLP[c].reset(); fbState[c] = 0.0f; }
        fbEnv = 0.0f;
    }

    void setChalk (const ChalkShape* c) noexcept { gr.chalk = c; }

    // the shelf: 0 spectral, 1 pitch, 2 grains, 3 reverb, 4 stutter, 5.. store effect (id - 5)
    enum Slot { slotSpectral = 0, slotPitch, slotGrains, slotReverb, slotStutter, slotStore };
    static constexpr int kSlots = slotStore + StoreFX::kNum;
    using Order = std::array<int, kSlots>;
    static Order defaultOrder() { Order o {}; for (int i = 0; i < kSlots; ++i) o[(size_t) i] = i; return o; }
    void setRack (Stutter* st, StoreFX* store) noexcept { stutter = st; storeFx = store; }
    void setOrder (const Order& o) noexcept { if (o != order && o != pendingOrder) { pendingOrder = o; if (dipStage == 0) dipStage = 1; } }
    const Order& getOrder() const noexcept { return order; }

    void setParams (const EngineParams& p)
    {
        params = p;
        sgMix.set (p.sgMix); sgCut.set (p.sgCutoffDb);
        psMix.set (p.psMix); psFb.set (p.psFeedback);
        grMix.set (p.grMix); rvMix.set (p.rvMix);
        masterFb.set (p.masterFb);

        const auto& scales = getScales();
        const auto& s = scales[(size_t) std::clamp (p.psScale, 0, (int) scales.size() - 1)];
        const auto q = quantise (s, (double) p.psShiftSemis * 100.0);
        quantisedCents = (float) q.cents;
        quantisedLabel = &s.degrees[(size_t) q.degreeIndex].label;
        quantisedPeriod = q.period;
        ps.setPitchOctaves ((float) (q.cents / 1200.0));

        sg.setAttackMs (p.sgAttackMs);
        sg.setSmearSeconds (p.sgSmearS);
        sg.setKnee (p.sgKnee);
        sg.setBlur (p.sgBlur);
        sg.setScatter (p.sgScatter);
        sg.setTiltDb (p.sgTilt);
        sg.setFreeze (p.sgFreeze);

        gr.setGrainParams (p.grSizeMs, p.grDensity, p.grShape, p.grPan, p.grReverse);
        gr.setPosition (p.grPos, p.grPosRand);
        gr.setFadeTime (p.grFade);
        gr.setOctave (p.grOctave, p.grRandOct);
        gr.setFreeze (p.grFreeze);
        rv.setDecay (p.rvDecay);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        sg.setCutoffDb (sgCut.target);
        for (int i = 0; i < n; ++i)
        {
            const float fb = masterFb.next();
            float l = inL[i] + fb * fbState[0];
            float r = inR[i] + fb * fbState[1];

            const float sgM = sgMix.next(), psF = psFb.next(), psM = psMix.next(), grM = grMix.next(), rvM = rvMix.next();
            for (int slot : order)
            {
                float wl, wr;
                switch (slot)
                {
                    case slotSpectral: sg.process (l, r, wl, wr);      mix (sgM, l, r, wl, wr); break;   // spectral gate / smear
                    case slotPitch:    ps.process (l, r, psF, wl, wr); mix (psM, l, r, wl, wr); break;   // quantised pitch shifter
                    case slotGrains:   gr.process (l, r, wl, wr);      mix (grM, l, r, wl, wr); break;   // grains
                    case slotReverb:   rv.process (l, r, wl, wr);      mix (rvM, l, r, wl, wr); break;   // high-pass FDN reverb
                    case slotStutter:  if (stutter != nullptr) stutter->tick (l, r); break;
                    default:           if (storeFx != nullptr) storeFx->tickOne (slot - slotStore, l, r); break;
                }
            }
            if (dipStage != 0)
            {
                // a new order: down over 8 ms, swap, back up over 8 ms
                dip += (dipStage == 1 ? -1.0f : 1.0f) * dipStep;
                if (dipStage == 1 && dip <= 0.0f) { dip = 0.0f; order = pendingOrder; dipStage = 2; }
                else if (dipStage == 2 && dip >= 1.0f) { dip = 1.0f; dipStage = pendingOrder != order ? 1 : 0; }
                l *= dip; r *= dip;
            }

            // safety: never let a NaN/inf escape (and never let it poison the loop)
            if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; reset(); }

            outL[i] = l; outR[i] = r;

            // global feedback: DC-blocked, band-limited, soft-saturated so it can sing but never explode
            float f[2] = { l, r };
            for (int c = 0; c < 2; ++c)
                f[c] = fbLP[c].process (fbHP[c].process (fbDC[c].process (f[c])));
            // level-riding compressor in the loop: regeneration stays near -10 dBFS
            const float pk = std::max (std::abs (f[0]), std::abs (f[1]));
            fbEnv += (pk > fbEnv ? fbAtk : fbRel) * (pk - fbEnv);
            fbEnv = flushDenormal (fbEnv);
            const float g = fbEnv > fbThresh ? fbThresh / fbEnv : 1.0f;
            for (int c = 0; c < 2; ++c)
                fbState[c] = softClip (f[c] * g);
        }
    }

    float getQuantisedCents() const noexcept { return quantisedCents; }
    const std::string& getQuantisedLabel() const noexcept { static std::string e; return quantisedLabel ? *quantisedLabel : e; }
    int getQuantisedPeriod() const noexcept { return quantisedPeriod; }
    float getFreezeAmount() const noexcept { return gr.getFreezeAmount(); }
    float getSpectralFreezeAmount() const noexcept { return sg.getFreezeAmount(); }

private:
    static void mix (float m, float& l, float& r, float wl, float wr) noexcept
    {
        if (m <= 0.0f) return; // exact bypass
        const float a = m * (float) kPi * 0.5f;
        const float dry = std::cos (a), wet = std::sin (a);
        l = dry * l + wet * wl;
        r = dry * r + wet * wr;
    }

    void snapToParams()
    {
        sgMix.snap (params.sgMix); sgCut.snap (params.sgCutoffDb);
        psMix.snap (params.psMix); psFb.snap (params.psFeedback);
        grMix.snap (params.grMix); rvMix.snap (params.rvMix);
        masterFb.snap (params.masterFb);
    }

    double sr = 48000.0;
    EngineParams params;
    SpectralGate sg;
    PitchShifter ps;
    GrainShifter gr;
    FDNReverb rv;
    Ramp sgMix, sgCut, psMix, psFb, grMix, rvMix, masterFb;
    DCBlocker fbDC[2];
    Biquad fbHP[2];
    OnePoleLP fbLP[2];
    float fbState[2] {};
    float fbEnv = 0.0f, fbAtk = 0.01f, fbRel = 0.0001f, fbThresh = 0.3f;
    float quantisedCents = 0.0f;
    const std::string* quantisedLabel = nullptr;
    Order order = defaultOrder(), pendingOrder = defaultOrder();
    int dipStage = 0;
    float dip = 1.0f, dipStep = 0.002f;
    Stutter* stutter = nullptr;
    StoreFX* storeFx = nullptr;
    int quantisedPeriod = 0;
};
} // namespace fp

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static String pct (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; }

AudioProcessorValueTreeState::ParameterLayout FreakiePieProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    using F = AudioParameterFloat;
    using A = AudioParameterFloatAttributes;

    auto mixParam = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<F> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                         A().withStringFromValueFunction (pct)));
    };

    // Spectral gate
    mixParam (ids::sgMix, "Spectral Mix", 0.35f);

    // Pitch shifter
    StringArray scaleNames;
    for (auto& s : fp::getScales()) scaleNames.add (s.name);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::psScale, 1 }, "Scale", scaleNames, 0));
    layout.add (std::make_unique<F> (ParameterID { ids::psShift, 1 }, "Pitch Shift",
                                     NormalisableRange<float> (-24.0f, 24.0f, 0.0f), 7.0f,
                                     A().withLabel ("st").withStringFromValueFunction ([] (float v, int)
                                         { return (v > 0 ? "+" : "") + String (v, 1) + " st"; })));
    layout.add (std::make_unique<F> (ParameterID { ids::psFeedback, 1 }, "Pitch Feedback",
                                     NormalisableRange<float> (0.0f, 0.95f), 0.35f, A().withStringFromValueFunction (pct)));
    mixParam (ids::psMix, "Pitch Mix", 0.3f);

    // Grains
    mixParam (ids::grMix, "Grain Mix", 0.4f);
    layout.add (std::make_unique<F> (ParameterID { ids::grFade, 1 }, "Freeze Fade",
                                     NormalisableRange<float> (0.01f, 10.0f, 0.0f, 0.35f), 1.0f,
                                     A().withLabel ("s").withStringFromValueFunction ([] (float v, int)
                                         { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; })));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::grOctave, 1 }, "Grain Octave", -2, 2, 0,
                                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return (v > 0 ? "+" : "") + String (v) + " oct"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::grRandOct, 1 }, "Random Octave", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::grFreeze, 1 }, "Freeze", false));

    // Reverb
    layout.add (std::make_unique<F> (ParameterID { ids::rvDecay, 1 }, "Reverb Decay",
                                     NormalisableRange<float> (0.2f, 20.0f, 0.0f, 0.4f), 4.0f,
                                     A().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; })));
    mixParam (ids::rvMix, "Reverb Mix", 0.3f);

    // Master
    mixParam (ids::masterFb, "Main Feedback", 0.0f);

    // Weather on the master output
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rain, 1 }, "Rain", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::helium, 1 }, "Helium", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::cloudy, 1 }, "Cloudy", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::snow, 1 }, "Snow", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::heat, 1 }, "Heatwave", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::autoWeather, 1 }, "Auto Weather", false));
    // the sonic rainboom
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbBoom, 1 }, "Rainboom", false));
    layout.add (std::make_unique<F> (ParameterID { ids::rbLevel, 1 }, "Rainboom Level", NormalisableRange<float> (0.0f, 1.0f), 0.7f,
                                     A().withStringFromValueFunction (pct)));
    layout.add (std::make_unique<F> (ParameterID { ids::rbTail, 1 }, "Rainbow Length", NormalisableRange<float> (1.0f, 12.0f, 0.0f, 0.6f), 5.0f,
                                     A().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbHits, 1 }, "Rainboom On Hits", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbStorms, 1 }, "Rainboom After Storms", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbThrows, 1 }, "Rainboom On Throws", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbFast, 1 }, "Rainboom On Fast Grains", false));
    {
        // 5 s to 5 min; the middle of the range sits at 30 s so the short times are easy to set
        auto secs = [] (float v, int) { return snowFadeText (v); };
        for (auto [id, name] : { std::pair { ids::snowIn, "Snow Fade In" }, std::pair { ids::snowOut, "Snow Fade Out" },
                                 std::pair { ids::heatIn, "Heatwave Fade In" }, std::pair { ids::heatOut, "Heatwave Fade Out" } })
        {
            NormalisableRange<float> r (5.0f, 300.0f);
            r.setSkewForCentre (30.0f);
            layout.add (std::make_unique<F> (ParameterID { id, 1 }, name, r, 5.0f, A().withStringFromValueFunction (secs)));
        }
    }
    mixParam (ids::stAmount, "Stutter", 0.0f);
    mixParam (ids::stBounce, "Stutter Bounce", 0.5f);
    mixParam (ids::stSway,   "Stutter Sway", 0.0f);
    mixParam (ids::stCutup,  "Stutter Cut-up", 0.25f);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::fxBypass, 1 }, "Bypass Effects", false));
    layout.add (std::make_unique<F> (ParameterID { ids::grainsLevel, 1 }, "Frozen Balloons Volume", NormalisableRange<float> (0.0f, 2.0f), 1.0f,
                                     A().withStringFromValueFunction ([] (float v, int)
                                     { return v < 0.001f ? String ("silent") : String (Decibels::gainToDecibels (v), 1) + " dB"; })));

    auto logRange = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; };
    auto ms = [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " ms" : String (v * 0.001f, 2) + " s"; };

    // more spectral smear
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::sgFreeze, 1 }, "Spectral Freeze", false));

    // more grains
    layout.add (std::make_unique<F> (ParameterID { ids::grSize, 1 }, "Grain Size", logRange (10.0f, 1500.0f, 150.0f), 120.0f, A().withStringFromValueFunction (ms)));
    layout.add (std::make_unique<F> (ParameterID { ids::grDensity, 1 }, "Grain Density", logRange (1.0f, 24.0f, 6.0f), 7.0f,
                                     A().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + "x"; })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::grShape, 1 }, "Grain Shape",
                                                        StringArray { "saw", "reverse saw", "bell", "triangle", "pulse", "sine", "chalk" }, 2));   // (bell: a Hann window, as IEM's grains)
    mixParam (ids::grPan, "Grain Spread", 0.25f);   // in the sphere: how far round its place each grain may land
    mixParam (ids::grReverse, "Grain Reverse", 0.0f);
    layout.add (std::make_unique<F> (ParameterID { ids::grPos, 1 }, "Grain Position", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                     A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " % back"; })));
    mixParam (ids::grPosRand, "Grain Position Random", 1.0f);

    // the inflation tone, and what the sound pump catches
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::catchFrom, 1 }, "Catch From", StringArray { "effects", "input" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::balloonSfx, 1 }, "Balloon Sounds", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::popsThru, 1 }, "Pop Sounds Through FX", false));
    layout.add (std::make_unique<F> (ParameterID { ids::grainsThru, 1 }, "Frozen Grains Through FX", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                     A().withStringFromValueFunction ([] (float v, int)
                                     {
                                         if (v < 0.005f) return String ("straight out");
                                         if (v > 0.995f) return String ("through fx");
                                         return String (roundToInt (v * 100.0f)) + " % through fx";
                                     })));

    // the balloon store (SoundGrain's FX balls)
    for (int e = 0; e < fp::StoreFX::kNum; ++e)
    {
        const auto& info = fp::StoreFX::infos()[(size_t) e];
        const String nm (info.name);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::store (e, ids::storeOwned), 1 }, nm + " Bought", false));
        auto control = [&] (const fp::StoreFX::Control& c, ids::StorePart part)
        {
            NormalisableRange<float> range (c.lo, c.hi);
            if (c.log) range.setSkewForCentre (std::sqrt (c.lo * c.hi));
            const int pitch = c.pitch;
            const String unit (c.unit);
            layout.add (std::make_unique<F> (ParameterID { ids::store (e, part), 1 }, nm + " " + String (c.name).toUpperCase().substring (0, 1) + String (c.name).substring (1),
                                             range, c.def, A().withLabel (unit).withStringFromValueFunction ([pitch, unit] (float v, int)
            {
                const auto& sc = fp::getScales()[(size_t) jlimit (0, (int) fp::getScales().size() - 1, fp::StoreFX::displayScale().load())];
                if (pitch == fp::StoreFX::pitchHz)    v = (float) fp::StoreFX::snapHz (v, sc);
                if (pitch == fp::StoreFX::pitchSemis)
                {
                    const double st = fp::StoreFX::snapSemis (v, sc);
                    return std::abs (st - std::round (st)) < 0.01 ? (st > 0 ? "+" : "") + String (roundToInt (st)) + " st"
                                                                  : (st > 0 ? "+" : "") + String (st, 2) + " st";
                }
                if (unit == "Hz") return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, v < 10.0f ? 2 : 0) + " Hz";
                if (unit == "s")  return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 1) + " s";
                if (unit == "bits") return String (v, 1) + " bits";
                return String (v, 2);
            })));
        };
        control (info.c1, ids::storeA);
        control (info.c2, ids::storeB);
        layout.add (std::make_unique<F> (ParameterID { ids::store (e, ids::storeLevel), 1 }, nm + " Level", NormalisableRange<float> (0.0f, 2.0f), 1.0f,
                                         A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
        layout.add (std::make_unique<F> (ParameterID { ids::store (e, ids::storeWidth), 1 }, nm + " Width", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                         A().withStringFromValueFunction ([] (float v, int)
                                         {
                                             if (v < 0.005f) return String ("mono");
                                             if (v > 0.995f) return String ("stereo");
                                             return String (roundToInt (v * 100.0f)) + " % wide";
                                         })));
    }

    // the gumball machine (the stutter) is sold at the balloon store too
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::stOwned, 1 }, "Stutter Bought", false));
    // the spectral smear's tilt: up brightens it (the highs up, the lows down), down darkens it
    layout.add (std::make_unique<F> (ParameterID { ids::sgTilt, 1 }, "Spectral Tilt", NormalisableRange<float> (-6.0f, 6.0f), 0.0f,
                                     A().withLabel ("dB/oct").withStringFromValueFunction ([] (float v, int)
                                     {
                                         if (std::abs (v) < 0.05f) return String ("flat");
                                         return (v > 0 ? "+" : "") + String (v, 1) + " dB/oct " + (v > 0 ? "brighter" : "darker");
                                     })));

    // the sphere
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::grainMode, 1 }, "Grain Mode",
                                                        StringArray { "spatial", "scatter", "copies" }, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::grains, 1 }, "Grains", 1, fp::Flight::kMax, 4,
                                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                         { return String (v) + (v == 1 ? " grain" : " grains"); })));
    mixParam (ids::deviate, "Deviate", 0.35f);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::flight, 1 }, "Flight",
                                                        StringArray { "still", "orbit", "lorenz", "flock", "random", "spiral", "lissajous", "rossler", "breathe", "fall", "ping-pong" }, 1));   // (orbit: things flying round you)
    layout.add (std::make_unique<F> (ParameterID { ids::flightSpeed, 1 }, "Flight Speed", NormalisableRange<float> (0.0f, 1.0f), 0.4f,
                                     A().withStringFromValueFunction ([] (float v, int)
                                     {
                                         const float sf = 0.05f * std::pow (60.0f, v);
                                         return sf < 1.0f ? String (sf, 2) + "x" : String (sf, 1) + "x";
                                     })));
    layout.add (std::make_unique<F> (ParameterID { ids::spread, 1 }, "Distance", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                     A().withStringFromValueFunction ([] (float v, int) { const float m = FreakiePieProcessor::bubbleMetres (v); return (m < 10.0f ? String (m, 1) : String (roundToInt (m))) + " m bubble"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::doppler, 1 }, "Doppler", true));
    return layout;
}

// Stereo in; out, the 16 channels of 3rd-order Ambisonics (ACN/SN3D, ready for the IEM decoders),
// or two for binaural and stereo. The standalone app opens in stereo.
AudioProcessor::BusesProperties FreakiePieProcessor::sphereBuses()
{
    return BusesProperties()
        .withInput  ("Input",  AudioChannelSet::stereo(), true)
        .withOutput ("Output", AudioChannelSet::ambisonic (3), true);   // (published as ACN/SN3D 3rd order: the AU host checks the default is one it can see)
}

FreakiePieProcessor::FreakiePieProcessor()
    : AudioProcessor (sphereBuses()),
      apvts (*this, nullptr, "FreakiePie", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p_sgMix = get (ids::sgMix);
    p_psShift = get (ids::psShift);    p_psFeedback = get (ids::psFeedback);
    p_psMix = get (ids::psMix);        p_psScale = get (ids::psScale);
    p_grMix = get (ids::grMix);        p_grFade = get (ids::grFade);
    p_grOctave = get (ids::grOctave);  p_grRandOct = get (ids::grRandOct);
    p_grFreeze = get (ids::grFreeze);  p_rvDecay = get (ids::rvDecay);
    p_rvMix = get (ids::rvMix);        p_masterFb = get (ids::masterFb);
    p_rain = get (ids::rain);          p_helium = get (ids::helium);
    p_cloudy = get (ids::cloudy);      p_fxBypass = get (ids::fxBypass);
    p_grainsLevel = get (ids::grainsLevel);
    p_snow = get (ids::snow);
    p_snowIn = get (ids::snowIn); p_snowOut = get (ids::snowOut);
    p_heat = get (ids::heat); p_autoWeather = get (ids::autoWeather);
    p_rbBoom = get (ids::rbBoom); p_rbLevel = get (ids::rbLevel); p_rbTail = get (ids::rbTail);
    p_rbHits = get (ids::rbHits); p_rbStorms = get (ids::rbStorms); p_rbFast = get (ids::rbFast);
    p_stAmount = get (ids::stAmount); p_stBounce = get (ids::stBounce);
    p_stSway = get (ids::stSway);     p_stCutup = get (ids::stCutup);
    p_sgFreeze = get (ids::sgFreeze);
    p_stOwned = get (ids::stOwned);   p_sgTilt = get (ids::sgTilt);
    p_grSize = get (ids::grSize);      p_grDensity = get (ids::grDensity); p_grShape = get (ids::grShape);
    p_grPan = get (ids::grPan);        p_grReverse = get (ids::grReverse);
    p_grPos = get (ids::grPos);        p_grPosRand = get (ids::grPosRand);
    p_popsThru = get (ids::popsThru);
    p_heatIn = get (ids::heatIn); p_heatOut = get (ids::heatOut);
    p_catchFrom = get (ids::catchFrom);
    p_balloonSfx = get (ids::balloonSfx);
    p_grainsThru = get (ids::grainsThru);
    for (int e = 0; e < fp::StoreFX::kNum; ++e)
        for (int k = 0; k < ids::numStoreParts; ++k)
            p_store[e][k] = apvts.getRawParameterValue (ids::store (e, (ids::StorePart) k));
    p_grains = get (ids::grains); p_deviate = get (ids::deviate); p_flight = get (ids::flight);
    p_flightSpeed = get (ids::flightSpeed); p_spread = get (ids::spread);
    p_grainMode = get (ids::grainMode); p_doppler = get (ids::doppler);
    for (int v = 0; v < kGrains; ++v)
    {
        auto g = std::make_unique<Grain>();
        g->engine.setChalk (&chalk);
        g->engine.setRack (&g->stutter, &g->store);
        g->engine.setWithReverb (false);   // one reverb for the whole sphere
        g->engine.setSeed (0x51ED + 7919u * (uint64_t) v);
        g->rng.seed (0xC0FFEE + 104729u * (uint64_t) v);
        for (auto& d : g->dev) d = 0.0f;
        for (size_t k = 0; k < g->devTarget.size(); ++k) { g->devTarget[k] = g->rng.bi(); g->devClock[k] = g->rng.range (0.0f, 6.0f); }
        g->dev = g->devTarget;
        g->nextFlip = g->rng.range (2.0f, 12.0f);
        grainsArr[(size_t) v] = std::move (g);
        homes[(size_t) v] = pendingHomes[(size_t) v] = fp::Flight::defaultHome (v, 0.9f);
    }
    for (int k = 0; k < kPairs; ++k)
    {
        snowFreeze[(size_t) k] = std::make_unique<fp::SnowFreeze>();
        heatWave[(size_t) k] = std::make_unique<fp::HeatWave>();
    }
    setShelfOrder (fp::Engine::defaultOrder());
    balloonFX.chalk = &chalk;

    setCurrentProgram (0);   // a fresh one opens on the default patch

    // start where you left off
    if (rememberLastState)
        if (auto xml = parseXML (lastStateFile()))
            if (xml->hasTagName (apvts.state.getType()))
                loadStateXml (*xml);
    startTimer (1000);   // auto weather ticks once a second
}

FreakiePieProcessor::~FreakiePieProcessor()
{
    stopTimer();
    if (MessageManager::existsAndIsCurrentThread()) saveLastState();
}

File FreakiePieProcessor::lastStateFile()
{
    if (lastStateOverride != File()) return lastStateOverride;
   #if JUCE_MAC
    const auto folder = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Application Support");
   #else
    const auto folder = File::getSpecialLocation (File::userApplicationDataDirectory);   // AppData\Roaming on Windows, ~/.config on Linux
   #endif
    return folder.getChildFile ("FreakiePie").getChildFile ("Mellyloon last patch.xml");   // (its own, apart from the shop's)
}

void FreakiePieProcessor::saveLastState()
{
    if (! rememberLastState) return;
    MemoryBlock mb;
    getStateInformation (mb);
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize()))
    {
        const auto text = xml->toString();
        if (text == lastSaved) return;   // nothing changed since the last save
        const auto f = lastStateFile();
        f.getParentDirectory().createDirectory();
        if (f.replaceWithText (text)) lastSaved = text;
    }
}

//==============================================================================
int FreakiePieProcessor::getNumPrograms() { return (int) presets::list().size(); }
const String FreakiePieProcessor::getProgramName (int i)
{
    return isPositiveAndBelow (i, getNumPrograms()) ? String (presets::list()[(size_t) i].name) : String();
}

// a preset: every parameter back to its default, then the preset's own values and store purchases
void FreakiePieProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, getNumPrograms())) return;
    const auto& pr = presets::list()[(size_t) index];
    auto set = [this] (const String& id, float real)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (real));
    };
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
            r->setValueNotifyingHost (r->getDefaultValue());
    if (pr.scale.isNotEmpty())
        for (int i = 0; i < (int) fp::getScales().size(); ++i)
            if (fp::getScales()[(size_t) i].name == pr.scale.toStdString()) set (ids::psScale, (float) i);
    for (auto& v : pr.values) set (v.id, v.v);
    for (int fx : pr.bought) set (ids::store (fx, ids::storeOwned), 1.0f);
    currentPreset.store (index);
    apvts.state.setProperty ("preset", index, nullptr);
}

bool FreakiePieProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    // 16 plain channels too (some hosts ask for those), except as an Audio Unit, which only publishes the ambisonic layout
    const bool discrete16 = out == AudioChannelSet::discreteChannels (16) && wrapperType != wrapperType_AudioUnit;
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono() && ! discrete16 && out != AudioChannelSet::ambisonic (3)) return false;
    return in == AudioChannelSet::mono() || in == AudioChannelSet::stereo();
}

fp::EngineParams FreakiePieProcessor::readParams() const
{
    fp::EngineParams p;
    // the spectral smear has two controls now (mix, freeze); the rest is set for a smooth, even smear:
    // every bin passes (soft knee, gate wide open), swells in over ~150 ms and lets go over ~3 s
    p.sgCutoffDb   = -60.0f;
    p.sgMix        = p_sgMix->load();
    p.psShiftSemis = p_psShift->load();
    p.psFeedback   = p_psFeedback->load();
    p.psMix        = p_psMix->load();
    p.psScale      = (int) p_psScale->load();
    p.grMix        = p_grMix->load();
    p.grFade       = p_grFade->load();
    p.sgAttackMs   = 150.0f;
    p.sgSmearS     = 3.0f;
    p.sgKnee       = 1.0f;
    p.sgBlur       = 0.25f;
    p.sgScatter    = 0.0f;
    p.sgTilt       = p_sgTilt->load();
    p.sgFreeze     = p_sgFreeze->load() > 0.5f;
    p.grSizeMs     = p_grSize->load();
    p.grDensity    = p_grDensity->load();
    p.grShape      = (int) p_grShape->load();
    p.grPan        = p_grPan->load();
    p.grReverse    = p_grReverse->load();
    p.grPos        = p_grPos->load();
    p.grPosRand    = p_grPosRand->load();
    p.grOctave     = (int) std::lround (p_grOctave->load());
    p.grRandOct    = p_grRandOct->load() > 0.5f;
    p.grFreeze     = p_grFreeze->load() > 0.5f;
    p.rvDecay      = p_rvDecay->load();
    p.rvMix        = p_rvMix->load();
    p.masterFb     = p_masterFb->load();
    return p;
}

void FreakiePieProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int nb = std::max (1, samplesPerBlock);
    const auto base = readParams();
    const int count = jlimit (1, kGrains, (int) std::lround (p_grains->load()));
    flightPaths.prepare (sampleRate);
    {
        const SpinLock::ScopedLockType sl (homeLock);
        homes = pendingHomes;
    }
    placeSeen = placeVersion.load();
    for (int v = 0; v < kGrains; ++v)
    {
        auto& g = *grainsArr[(size_t) v];
        g.engine.setParams (base);
        g.engine.prepare (sampleRate);
        g.stutter.prepare (sampleRate, 0x57A7 + 104729u * (uint64_t) v);
        g.store.prepare (sampleRate);
        g.decor[0].prepare (sampleRate, 2 * v);
        g.decor[1].prepare (sampleRate, 2 * v + 1);
        g.gain = v < count ? 1.0f : 0.0f;
        g.levelSm = 0.0f;
        g.stutterSeen = g.stutter.eventCount.load();
        flightPaths.place (v, homes[(size_t) v]);
        g.dist.prepare (sampleRate, nb);
    }
    voiceNormSm = 1.0f / std::sqrt ((float) count);
    sky.prepare (0x5C1E);
    for (auto& w : weather) w.prepare (sampleRate);
    for (auto& e : masterEq) e.prepare (sampleRate);
    for (auto& f : snowFreeze) f->prepare (sampleRate);
    for (auto& h : heatWave) h->prepare (sampleRate);
    balloonFX.prepare (sampleRate);
    storeFX.prepare (sampleRate);
    leveler.prepare (sampleRate);
    limiter.prepare (sampleRate);
    trixieNoise.prepare (sampleRate);
    stutter.prepare (sampleRate);
    rainboom.prepare (sampleRate, samplesPerBlock);
    rbHitsDetector.prepare (sampleRate);
    rbDiveDist.prepare (sampleRate, samplesPerBlock);
    rbBoomDist.prepare (sampleRate, samplesPerBlock);
    rbMono.assign ((size_t) jmax (1, samplesPerBlock), 0.0f);
    rbTmp.assign ((size_t) jmax (1, samplesPerBlock), 0.0f);
    for (auto& c : rbRingWas) c.fill (0.0f);
    rbButtonWas = p_rbBoom->load() > 0.5f; rbRainWas = p_rain->load() > 0.5f; rbRequestsSeen = rbRequests.load(); rbPrevValid = false;
    granulator.prepare (sampleRate, nb);
    srcDist.prepare (sampleRate, nb);
    popsDist.prepare (sampleRate, nb);
    monoBuf.assign ((size_t) nb, 0.0f);
    sphereVerb.prepare (sampleRate);
    energy.prepare (sampleRate);
    verbMixSm = 0.0f;
    thruSm = p_grainsThru->load();
    bypassSm = p_fxBypass->load() > 0.5f ? 1.0f : 0.0f;
    grainsLevelSm = p_grainsLevel->load();
    nextStrikeIn = (int) (sampleRate * 2.0);
    for (auto& s : struckLeft) s = 0;
    struckMask.store (0);
    scratch.setSize (2, nb, false, true, true);
    chainIn.setSize (2, nb, false, true, true);
    grainBuf.setSize (2, nb, false, true, true);
    sfxBuf.setSize (2, nb, false, true, true);
    for (auto* b : { &bus, &sendBus, &verbBus }) b->setSize (fp::ambi::kCh, nb, false, true, true);
    for (auto* b : { &voiceBuf, &decoded, &sumBuf }) b->setSize (2, nb, false, true, true);
    dryGains.setSize (2, nb, false, true, true);
    grainBus.setSize (fp::ambi::kCh, nb, false, true, true);
    ramp.assign ((size_t) nb, 0.0f);
    popsThruSm = p_popsThru->load() > 0.5f ? 1.0f : 0.0f;
    grainFifo.setSize (2, std::max (8192, 4 * samplesPerBlock));
    grainFifo.clear();
    grainFifoCount = 0;
    setLatencySamples (limiter.latency());   // the limiter's look-ahead (1.5 ms)
}

void FreakiePieProcessor::setGrainHome (int v, fp::ambi::Vec3 p)
{
    if (! isPositiveAndBelow (v, kGrains)) return;
    {
        const SpinLock::ScopedLockType sl (homeLock);
        pendingHomes[(size_t) v] = fp::Flight::clampBall (p, 1.0f);
    }
    placeVersion.fetch_add (1);
}

fp::ambi::Vec3 FreakiePieProcessor::grainHome (int v) const
{
    const SpinLock::ScopedLockType sl (homeLock);
    return pendingHomes[(size_t) jlimit (0, kGrains - 1, v)];
}

// Lightning, while it's cloudy and raining: every 6 to 18 s a strike lends a random store effect
// you haven't bought for 45 to 90 s, and its thunder rolls in a moment later.
void FreakiePieProcessor::lightning (int n)
{
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    uint32_t mask = struckMask.load();
    const uint32_t early = popStruckRequest.exchange (0);
    for (int e = 0; e < fp::StoreFX::kNum; ++e)
    {
        if ((early >> e) & 1u) struckLeft[e] = 0;
        if (struckLeft[e] > 0) struckLeft[e] = std::max (0, struckLeft[e] - n);
        if (struckLeft[e] == 0) mask &= ~(1u << e);
    }
    const bool storm = p_cloudy->load() > 0.5f && p_rain->load() > 0.5f;
    if (storm && (nextStrikeIn -= n) <= 0)
    {
        nextStrikeIn = (int) (sr * (6.0 + 12.0 * strikeRnd.nextDouble()));
        int pool[fp::StoreFX::kNum], numFree = 0;
        for (int e = 0; e < fp::StoreFX::kNum; ++e)
            if (((mask >> e) & 1u) == 0 && p_store[e][ids::storeOwned]->load() < 0.5f) pool[numFree++] = e;
        if (numFree > 0)
        {
            const int e = pool[strikeRnd.nextInt (numFree)];
            struckLeft[e] = (int) (sr * (45.0 + 45.0 * strikeRnd.nextDouble()));
            mask |= 1u << e;
        }
        {
            const int at = (int) (sr * (0.25 + 1.0 * strikeRnd.nextDouble()));
            const float loud = 0.7f + 0.3f * strikeRnd.nextFloat();
            for (auto& w : weather) w.clap (at, loud);   // (every pair hears the same thunder)
        }
        struckMask.store (mask);
        lightningCount.fetch_add (1);   // after the mask, so the editor sees both together
        return;
    }
    if (! storm) nextStrikeIn = std::max (nextStrikeIn, (int) (sr * 1.5));   // the first strike comes a moment after the storm starts
    struckMask.store (mask);
}

void FreakiePieProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    // (never more channels than the buffer really has, whatever the layout says)
    const int numIn  = jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
    if (n == 0 || numIn < 1 || numOut < 1) return;
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const float seconds = (float) n / (float) sr;

    if (scratch.getNumSamples() < n) // hosts shouldn't do this, but stay safe
    {
        for (auto* b : { &scratch, &chainIn, &grainBuf, &sfxBuf, &voiceBuf, &decoded, &sumBuf }) b->setSize (2, n, false, true, true);
        for (auto* b : { &bus, &sendBus, &verbBus }) b->setSize (fp::ambi::kCh, n, false, true, true);
        ramp.resize ((size_t) n);
        monoBuf.resize ((size_t) n);
        dryGains.setSize (2, n, false, true, true);
        grainBus.setSize (fp::ambi::kCh, n, false, true, true);
    }

    const auto base = readParams();
    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    // the inflation tone joins the input on its way into the effects (but not the sound-balloon sampler)
    float* cL = chainIn.getWritePointer (0);
    float* cR = chainIn.getWritePointer (1);
    std::copy (inL, inL + n, cL);
    std::copy (inR, inR + n, cR);
    const auto& scales = fp::getScales();
    const int si = juce::jlimit (0, (int) scales.size() - 1, (int) p_psScale->load());
    const auto& sc = scales[(size_t) si];
    balloonFX.addInflationTone (cL, cR, n, sc);   // (the plucks are always on)
    trixieNoise.process (cL, cR, n);   // Trixie's noise, when she's playing one, goes in with your sound
    // the hiss and pops: straight out or into the effects with your sound (crossfading over ~50 ms)
    balloonFX.sfxOn = p_balloonSfx->load() > 0.5f;
    float* sL = sfxBuf.getWritePointer (0);
    float* sR = sfxBuf.getWritePointer (1);
    balloonFX.renderSfx (sL, sR, n);
    {
        const float target = p_popsThru->load() > 0.5f ? 1.0f : 0.0f;
        const float k = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            popsThruSm += k * (target - popsThruSm);
            cL[i] += popsThruSm * sL[i];
            cR[i] += popsThruSm * sR[i];
            sL[i] *= 1.0f - popsThruSm;
            sR[i] *= 1.0f - popsThruSm;
        }
        if (std::abs (target - popsThruSm) < 1.0e-5f) popsThruSm = target;
    }
    // the frozen balloons' grains from the last block, back in through the effects
    if (grainFifoCount > 0)
    {
        const int m = std::min (n, grainFifoCount);
        for (int c = 0; c < 2; ++c)
        {
            float* f = grainFifo.getWritePointer (c);
            FloatVectorOperations::add (c == 0 ? cL : cR, f, m);
            std::move (f + m, f + grainFifoCount, f);
        }
        grainFifoCount -= m;
    }
    lightning (n);

    // the shelf's order, the host's tempo, the store: shared by every grain (each strays from them)
    const int shelfV = shelfVersion.load();
    const bool newOrder = shelfV != shelfSeen;
    shelfSeen = shelfV;
    const auto order = getShelfOrder();
    double bpm = 0.0, ppq = -1.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    fp::Stutter::Settings ss;
    ss.amount = p_stAmount->load(); ss.bounce = p_stBounce->load();
    ss.sway = p_stSway->load();     ss.cutup = p_stCutup->load();
    ss.on = p_stOwned->load() > 0.5f;
    fp::StoreFX::displayScale().store (si);
    fp::StoreFX::AllSettings st;
    {
        const uint32_t struck = struckMask.load();
        for (int e = 0; e < fp::StoreFX::kNum; ++e)
        {
            auto& s = st[(size_t) e];
            s.owned = p_store[e][ids::storeOwned]->load() > 0.5f || ((struck >> e) & 1u) != 0;
            s.c1 = p_store[e][ids::storeA]->load();
            s.c2 = p_store[e][ids::storeB]->load();
            s.level = p_store[e][ids::storeLevel]->load();
            s.width = p_store[e][ids::storeWidth]->load();
        }
    }

    // the sphere: where the grains are, and the weather drifting through it
    const int points = jlimit (1, kGrains, (int) std::lround (p_grains->load()));   // how many places fly in the sphere
    // spatial: each copy's effects at its own place, the dry sound at one place, the grains spread over the places
    // scatter: the same with one copy of the effects (at the first place)
    // copies:  each copy is the whole shop, dry sound and all, at its place (the first way it worked)
    const int grainMode = jlimit (0, 2, (int) std::lround (p_grainMode->load()));
    const bool wholeCopies = grainMode == 2;
    const int count = grainMode == 1 ? 1 : points;
    const float deviation = jlimit (0.0f, 1.0f, p_deviate->load());
    {
        const int pv = placeVersion.load();
        if (pv != placeSeen)
        {
            placeSeen = pv;
            const SpinLock::ScopedTryLockType sl (homeLock);
            if (sl.isLocked()) { homes = pendingHomes; for (int v = 0; v < kGrains; ++v) flightPaths.setHome (v, homes[(size_t) v]); }
            else placeSeen = pv - 1;   // (try again next block)
        }
        const int held = heldGrain.load();
        if (isPositiveAndBelow (held, kGrains)) flightPaths.place (held, { heldX.load(), heldY.load(), heldZ.load() });
    }
    // (the grains fly through most of the bubble; how big the bubble is, is the Distance knob)
    flightPaths.step (seconds, (int) std::lround (p_flight->load()), p_flightSpeed->load(), 0.85f, points);
    bubbleNow += (bubbleMetres (p_spread->load()) - bubbleNow) * std::min (1.0f, seconds / 0.15f);   // (glides, so turning it does not jump)
    const float sphereMetres = bubbleNow;
    {
        const bool dop = p_doppler->load() > 0.5f;
        srcDist.doppler = popsDist.doppler = dop;
        for (auto& g : grainsArr) g->dist.doppler = dop;
    }
    {
        const bool on[fp::SkyPatches::numKinds] = { p_cloudy->load() > 0.5f, p_rain->load() > 0.5f, p_snow->load() > 0.5f, p_heat->load() > 0.5f };
        sky.step (seconds, on);
        for (int i = 0; i < fp::SkyPatches::kNum; ++i)
        {
            const auto& pa = sky.patches[(size_t) i];
            auto& pv = patchView[(size_t) i];
            pv.x.store (pa.pos.x); pv.y.store (pa.pos.y); pv.z.store (pa.pos.z);
            pv.radius.store (pa.radius); pv.amount.store (pa.amount);
        }
    }

    // bypass: the effects crossfade out over ~50 ms, leaving the input and the inflation tone
    {
        const float target = p_fxBypass->load() > 0.5f ? 1.0f : 0.0f;
        const float k = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
        for (int i = 0; i < n; ++i) { bypassSm += k * (target - bypassSm); ramp[(size_t) i] = bypassSm; }
        if (target == 0.0f && bypassSm < 1.0e-4f) bypassSm = 0.0f;
        if (target == 1.0f && bypassSm > 0.9999f) bypassSm = 1.0f;
        bypassAmount.store (bypassSm);
    }

    bus.clear (0, n); sendBus.clear (0, n); sumBuf.clear (0, n);
    float* const* busW = bus.getArrayOfWritePointers();
    float* const* sendW = sendBus.getArrayOfWritePointers();
    float* vL = voiceBuf.getWritePointer (0);
    float* vR = voiceBuf.getWritePointer (1);
    float* sumL = sumBuf.getWritePointer (0);
    float* sumR = sumBuf.getWritePointer (1);
    // with more grains each one is quieter, so the whole stays about as loud (1 / sqrt(N))
    const float normFrom = voiceNormSm, normTo = 1.0f / std::sqrt ((float) count);
    const float fadeStep = seconds / 0.06f;   // grains fade in and out over 60 ms
    int stutterEvents = 0;
    for (int v = 0; v < kGrains; ++v)
    {
        auto& g = *grainsArr[(size_t) v];
        auto& view = grainView[(size_t) v];
        const float target = v < count ? 1.0f : 0.0f;
        if (target == 0.0f && g.gain == 0.0f) { view.presence.store (0.0f); view.level.store (0.0f); continue; }
        const bool starting = g.gain == 0.0f;
        if (starting) { g.engine.reset(); g.stutterSeen = g.stutter.eventCount.load(); }
        if (newOrder || starting) g.engine.setOrder (order);

        const auto pos = flightPaths.position (v);
        float weatherAll = 0.0f;
        for (int k = 0; k < fp::SkyPatches::numKinds; ++k) weatherAll = std::max (weatherAll, sky.exposure (pos, k));
        const float inSnow = sky.exposure (pos, fp::SkyPatches::snow), inHeat = sky.exposure (pos, fp::SkyPatches::heat);
        // the first grain is you: it only strays when the weather gets to it
        const float amount = jlimit (0.0f, 1.0f, (v == 0 ? 0.0f : deviation) + 0.8f * weatherAll);
        stepGrainDeviation (g, v, amount, weatherAll, seconds);
        auto prm = grainParams (base, g, amount, base.grFreeze, base.sgFreeze);
        if (inSnow > 0.5f) { prm.sgFreeze = true; prm.sgMix = std::max (prm.sgMix, 0.6f * inSnow); }   // snow freezes it while it's in there
        if (inHeat > 0.0f) prm.sgMix = std::max (prm.sgMix, 0.7f * inHeat);                          // the heat smears it
        g.engine.setParams (prm);

        // its own store and stutter, strayed the same way
        auto stv = st;
        const auto& d = g.dev;
        for (int e = 0; e < fp::StoreFX::kNum; ++e)
        {
            const auto& in = fp::StoreFX::infos()[(size_t) e];
            auto stray = [&] (float val, const fp::StoreFX::Control& c, float dv)
            {
                if (amount <= 0.0f) return val;
                float u = c.log ? std::log (std::max (c.lo, val) / c.lo) / std::log (c.hi / c.lo) : (val - c.lo) / (c.hi - c.lo);
                u = jlimit (0.0f, 1.0f, u + 0.25f * amount * dv);
                return c.log ? c.lo * std::pow (c.hi / c.lo, u) : c.lo + u * (c.hi - c.lo);
            };
            stv[(size_t) e].c1 = stray (st[(size_t) e].c1, in.c1, d[(size_t) ((17 + 2 * e) % 24)]);
            stv[(size_t) e].c2 = stray (st[(size_t) e].c2, in.c2, d[(size_t) ((18 + 2 * e) % 24)]);
        }
        g.store.beginBlock (stv, sc);
        auto ssv = ss;
        if (ssv.amount > 0.0f) ssv.amount = jlimit (0.0f, 1.0f, ssv.amount + 0.3f * amount * d[13]);
        ssv.bounce = jlimit (0.0f, 1.0f, ssv.bounce + 0.4f * amount * d[14]);
        ssv.cutup  = jlimit (0.0f, 1.0f, ssv.cutup  + 0.4f * amount * d[15]);
        ssv.sway   = jlimit (0.0f, 1.0f, ssv.sway   + 0.3f * amount * d[16]);
        g.stutter.begin (ssv, bpm, ppq, playing);

        g.engine.setGrainLanes (0, nullptr);
        g.engine.setGrainsInChain (wholeCopies);
        float* dg = dryGains.getWritePointer (v == 0 ? 0 : 1);
        g.engine.process (cL, cR, vL, vR, n, dg);

        // bypass, the fade in or out, and the 1 / sqrt(N). In the spatial modes only the effects'
        // own sound goes to the grain's place: the dry part (out - dryGain x in) is taken off here
        // and played once, at the source
        const float gFrom = g.gain;
        const float gTo = target > gFrom ? std::min (1.0f, gFrom + fadeStep) : std::max (0.0f, gFrom - fadeStep);
        double e2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float u = (float) (i + 1) / (float) n;
            const float b = ramp[(size_t) i];
            const float w = (gFrom + (gTo - gFrom) * u) * (normFrom + (normTo - normFrom) * u);
            if (wholeCopies)
            {
                vL[i] = (vL[i] + b * (cL[i] - vL[i])) * w;
                vR[i] = (vR[i] + b * (cR[i] - vR[i])) * w;
            }
            else
            {
                vL[i] = (vL[i] - dg[i] * cL[i]) * (1.0f - b) * w;
                vR[i] = (vR[i] - dg[i] * cR[i]) * (1.0f - b) * w;
            }
            sumL[i] += vL[i]; sumR[i] += vR[i];
            e2 += (double) (vL[i] * vL[i] + vR[i] * vR[i]);
        }
        g.levelSm += 0.3f * ((float) std::sqrt (e2 / (2.0 * n)) - g.levelSm);
        // every copy after the first goes through its own decorrelator: the copies run the same effects on the
        // same input, and the same signal from two places comb-filters (phasing) once it's decoded
        if (v > 0 && decorrelateCopies)
        {
            if (starting) { g.decor[0].reset(); g.decor[1].reset(); }
            g.decor[0].process (vL, n);
            g.decor[1].process (vR, n);
        }

        // into the scene: from where it was to where it is now, across the block
        // one point, at its distance (delay, level, air, the room's walls), sending more to the reverb the further out it is
        if (starting) g.dist.reset();
        float* mono = monoBuf.data();
        for (int i = 0; i < n; ++i) mono[i] = 0.70710678f * (vL[i] + vR[i]);
        g.dist.process (mono, n, pos, sphereMetres, busW, sendW, 0.3f + 0.7f * std::min (1.0f, pos.length()));
        g.gain = gTo;

        const int ev = g.stutter.eventCount.load();
        if (ev != g.stutterSeen)
        {
            view.stutters.fetch_add (ev - g.stutterSeen); stutterEvents += ev - g.stutterSeen; g.stutterSeen = ev;
            stutter.typesSeen.fetch_or (g.stutter.typesSeen.load());   // (the shared counter keeps what kinds have happened too)
            stutter.lastType.store (g.stutter.lastType.load());
        }
        view.x.store (pos.x); view.y.store (pos.y); view.z.store (pos.z);
        view.presence.store (gTo);
        view.level.store (g.levelSm);
        view.weather.store (weatherAll);
        view.frozen.store ((g.engine.getFreezeAmount() > 0.5f ? 1 : 0) | (g.engine.getSpectralFreezeAmount() > 0.5f ? 2 : 0));
    }
    voiceNormSm = normTo;
    if (! wholeCopies)
    {
        // the dry sound, once, at the source (where you put it: ahead to start with)
        const float* dg0 = dryGains.getReadPointer (0);
        const float grWet = std::sin (jlimit (0.0f, 1.0f, p_grMix->load()) * MathConstants<float>::halfPi);
        const float grDry = std::cos (jlimit (0.0f, 1.0f, p_grMix->load()) * MathConstants<float>::halfPi);
        float* dL = decoded.getWritePointer (0);
        float* dR = decoded.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float b = ramp[(size_t) i];
            const float k = dg0[i] * grDry * (1.0f - b) + b;   // (bypassed: all of the input, here)
            dL[i] = k * cL[i]; dR[i] = k * cR[i];
            sumL[i] += dL[i]; sumR[i] += dR[i];
        }
        const fp::ambi::Vec3 src { srcX.load(), srcY.load(), srcZ.load() };
        if (! spatialWas) srcDist.reset();
        float* mono = monoBuf.data();
        for (int i = 0; i < n; ++i) mono[i] = 0.70710678f * (dL[i] + dR[i]);
        srcDist.process (mono, n, src, sphereMetres, busW, sendW, 0.3f + 0.7f * std::min (1.0f, src.length()));

        // the grains: one granulator, every grain at its own direction round one of the places
        fp::SpatialGranulator::Settings gs;
        gs.lengthMs = p_grSize->load();
        gs.overlap = 3.0f * p_grDensity->load();   // (density 7 -> about 20 grains sounding at once)
        gs.posS = 4.0f * p_grPos->load();
        gs.posRandS = 4.0f * p_grPosRand->load() * 0.5f;
        gs.semis = 12.0f * (float) std::lround (p_grOctave->load());
        gs.randomOctave = p_grRandOct->load() > 0.5f;
        gs.reverse = p_grReverse->load();
        gs.shape = (int) p_grShape->load();
        gs.chalk = &chalk;
        gs.spread = p_grPan->load();
        gs.deviation = deviation;
        gs.freeze = p_grFreeze->load() > 0.5f;
        gs.numPlaces = points;
        gs.radiusM = sphereMetres;
        for (int k = 0; k < points; ++k) gs.places[(size_t) k] = flightPaths.position (k);
        const float bypassAvg = 0.5f * (ramp[0] + ramp[(size_t) n - 1]);
        const float level = grWet * (1.0f - bypassAvg);
        grainBus.clear (0, n);
        if (level > 0.0f || granulator.activeGrains() > 0)
            granulator.process (cL, cR, grainBus.getArrayOfWritePointers(), n, gs, level);
        for (int c = 0; c < fp::ambi::kCh; ++c)
        {
            FloatVectorOperations::add (busW[c], grainBus.getReadPointer (c), n);
            FloatVectorOperations::addWithMultiply (sendW[c], grainBus.getReadPointer (c), 0.8f, n);
        }
        // every place is shown, whether or not a copy of the effects is there
        for (int k = count; k < points; ++k)
        {
            const auto pos = flightPaths.position (k);
            auto& view = grainView[(size_t) k];
            view.x.store (pos.x); view.y.store (pos.y); view.z.store (pos.z);
            view.presence.store (1.0f);
            view.level.store (0.3f * level);
        }
    }
    spatialWas = ! wholeCopies;
    if (stutterEvents > 0) stutter.eventCount.fetch_add (stutterEvents);   // (the gumball balloons hop on any grain's stutter)
    freezeAmount.store (grainsArr[0]->engine.getFreezeAmount());
    spectralFreezeAmount.store (grainsArr[0]->engine.getSpectralFreezeAmount());

    // the balloons you've caught, the hiss and the pops: they play at the front of the sphere
    {
        fp::BalloonFX::GrainSettings gs;
        gs.octave = (int) std::lround (p_grOctave->load());
        gs.randomOctave = p_grRandOct->load() > 0.5f;
        gs.fadeSeconds = p_grFade->load();
        gs.sizeMs = p_grSize->load();
        gs.density = p_grDensity->load();
        gs.shape = (int) p_grShape->load();
        gs.pan = p_grPan->load();
        gs.reverse = p_grReverse->load();
        gs.position = p_grPos->load();
        gs.positionRandom = p_grPosRand->load();

        // what the sound pump catches: the grains' output (all of them, as you hear them) or the input
        const float* recL = inL;
        const float* recR = inR;
        if (p_catchFrom->load() < 0.5f)
        {
            std::copy (sumL, sumL + n, cL);
            std::copy (sumR, sumR + n, cR);
            recL = cL; recR = cR;
        }
        float* oL = scratch.getWritePointer (0);
        float* oR = scratch.getWritePointer (1);
        std::copy (sL, sL + n, oL);   // the hiss and pops that go straight out
        std::copy (sR, sR + n, oR);
        grainBuf.clear (0, n);
        float* gL = grainBuf.getWritePointer (0);
        float* gR = grainBuf.getWritePointer (1);
        balloonFX.process (recL, recR, oL, oR, n, gs, gL, gR);
        {
            const float target = juce::jlimit (0.0f, 2.0f, p_grainsLevel->load());
            if (target != 1.0f || grainsLevelSm != 1.0f)
            {
                const float step = (target - grainsLevelSm) / (float) n;
                for (int i = 0; i < n; ++i) { grainsLevelSm += step; gL[i] *= grainsLevelSm; gR[i] *= grainsLevelSm; }
                grainsLevelSm = target;
            }
        }
        const float thruT = p_fxBypass->load() > 0.5f ? 0.0f : juce::jlimit (0.0f, 1.0f, p_grainsThru->load());
        const float thruCoef = 1.0f - std::exp (-1.0f / (0.12f * (float) sr));
        const int room = grainFifo.getNumSamples() - grainFifoCount;
        float* fL = grainFifo.getWritePointer (0) + grainFifoCount;
        float* fR = grainFifo.getWritePointer (1) + grainFifoCount;
        for (int i = 0; i < n; ++i)
        {
            thruSm += thruCoef * (thruT - thruSm);
            oL[i] += gL[i] * (1.0f - thruSm);
            oR[i] += gR[i] * (1.0f - thruSm);
            if (i < room) { fL[i] = gL[i] * thruSm; fR[i] = gR[i] * thruSm; }
        }
        grainFifoCount += std::min (n, room);
        // (from the source, as one point)
        float* mono = monoBuf.data();
        for (int i = 0; i < n; ++i) mono[i] = 0.70710678f * (oL[i] + oR[i]);
        const fp::ambi::Vec3 src { srcX.load(), srcY.load(), srcZ.load() };
        popsDist.process (mono, n, src, sphereMetres, busW, sendW, 0.5f);
    }

    rainboomBlock (inL, inR, busW, sendW, n, sphereMetres, count, seconds);   // (before the reverb, so it can ring in the sphere too)

    // the one reverb, for the whole sphere (the lilac balloons)
    {
        const float target = (1.0f - (p_fxBypass->load() > 0.5f ? 1.0f : 0.0f)) * jlimit (0.0f, 1.0f, p_rvMix->load());
        sphereVerb.setDecay (p_rvDecay->load());
        if (target > 0.0f || verbMixSm > 0.0f)
        {
            if (verbMixSm <= 0.0f) sphereVerb.reset();
            float* const* vb = verbBus.getArrayOfWritePointers();
            sphereVerb.process (sendBus.getArrayOfReadPointers(), vb, n);
            const float k = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                verbMixSm += k * (target - verbMixSm);
                const float a = verbMixSm * MathConstants<float>::halfPi;
                const float dry = std::cos (a), wet = std::sin (a);
                for (int c = 0; c < fp::ambi::kCh; ++c) busW[c][i] = dry * busW[c][i] + wet * vb[c][i];
            }
            if (target == 0.0f && verbMixSm < 1.0e-4f) verbMixSm = 0.0f;
        }
    }
    energy.process (bus.getArrayOfReadPointers(), n);

    // out: the 16 channels (3rd-order Ambisonics, ACN / SN3D). On a track with fewer, only the first
    // ones come out, and the map says so.
    ambiFallback.store (numOut < fp::ambi::kCh);
    outChannels.store (numOut);
    float* chans[fp::ambi::kCh] {};
    const int C = fp::ambi::kCh;
    for (int c = 0; c < C; ++c) chans[c] = busW[c];

    // the master: weather, heat, snow and the chalkboard EQ, two channels at a time, then the leveler and limiter, linked
    const bool rainOn = p_rain->load() > 0.5f, heliumOn = p_helium->load() > 0.5f, cloudOn = p_cloudy->load() > 0.5f;
    const bool heatOn = p_heat->load() > 0.5f, snowOn = p_snow->load() > 0.5f;
    for (int k = 0; k < C / 2; ++k)
    {
        auto& w = weather[(size_t) k];
        w.process (chans[2 * k], chans[2 * k + 1], n, rainOn, heliumOn, cloudOn);
        heatWave[(size_t) k]->setFadeTimes (p_heatIn->load(), p_heatOut->load());
        heatWave[(size_t) k]->process (chans[2 * k], chans[2 * k + 1], n, heatOn);
        snowFreeze[(size_t) k]->setFadeTimes (p_snowIn->load(), p_snowOut->load());
        snowFreeze[(size_t) k]->process (chans[2 * k], chans[2 * k + 1], n, snowOn);
        masterEq[(size_t) k].process (chans[2 * k], chans[2 * k + 1], n, eq);
    }
    for (int k = C / 2; k < kPairs; ++k)   // the pairs resting this block keep up with the switches, quietly
    {
        float* z0 = verbBus.getWritePointer (0);
        float* z1 = verbBus.getWritePointer (1);
        FloatVectorOperations::clear (z0, n); FloatVectorOperations::clear (z1, n);
        weather[(size_t) k].process (z0, z1, n, rainOn, heliumOn, cloudOn);
    }
    rainAmount.store (weather[0].getRain());
    cloudAmount.store (weather[0].getClouds());
    thunderAmount.store (weather[0].getThunder());
    heliumAmount.store (weather[0].getHelium());
    heatAmount.store (heatWave[0]->getAmount());
    snowAmount.store (snowFreeze[0]->getAmount());
    leveler.process (chans, C, n);
    limiter.process (chans, C, n);

    for (int c = 0; c < numOut; ++c)
    {
        if (c < C) buffer.copyFrom (c, 0, chans[c], n);
        else buffer.clear (c, 0, n);
    }
    const float peak = buffer.getMagnitude (0, 0, n);   // the omni channel
    outputLevel.store (peak);
}

// a grain's settings stray slowly: each of its deviations glides to a new random target every
// 3 to 12 seconds (much sooner in the weather); with enough deviation its freezes flip on and off
// by themselves now and then, so the copies freeze at different times
void FreakiePieProcessor::stepGrainDeviation (Grain& g, int, float amount, float exposure, float secs)
{
    for (size_t k = 0; k < g.dev.size(); ++k)
    {
        g.devClock[k] -= secs * (1.0f + 6.0f * exposure);
        if (g.devClock[k] <= 0.0f) { g.devTarget[k] = g.rng.bi(); g.devClock[k] = g.rng.range (3.0f, 12.0f); }
        g.dev[k] += (g.devTarget[k] - g.dev[k]) * std::min (1.0f, secs * (0.15f + 1.2f * exposure));
    }
    if (amount < 0.05f) { g.freezeFlip = g.sgFlip = false; g.flipLeft = 0.0f; return; }
    if (g.flipLeft > 0.0f)
    {
        g.flipLeft -= secs;
        if (g.flipLeft <= 0.0f) g.freezeFlip = g.sgFlip = false;
        return;
    }
    g.nextFlip -= secs * amount;
    if (g.nextFlip <= 0.0f)
    {
        g.nextFlip = g.rng.range (4.0f, 16.0f);
        g.flipLeft = g.rng.range (1.0f, 6.0f);
        if (g.rng.uni() < 0.5f) g.freezeFlip = true; else g.sgFlip = true;
    }
}

// one grain's chain settings: yours, strayed by 'a' (0 none .. 1 a lot). Mixes that are off stay off.
fp::EngineParams FreakiePieProcessor::grainParams (const fp::EngineParams& b, const Grain& g, float a, bool grFreeze, bool sgFreeze) const
{
    auto p = b;
    const auto& d = g.dev;
    auto clamp01 = [] (float v) { return jlimit (0.0f, 1.0f, v); };
    p.grFreeze = grFreeze != g.freezeFlip;
    p.sgFreeze = sgFreeze != g.sgFlip;
    if (a <= 0.0f) return p;
    p.sgMix = clamp01 (b.sgMix * (1.0f + 0.6f * a * d[0]));
    p.sgTilt = jlimit (-6.0f, 6.0f, b.sgTilt + 4.0f * a * d[1]);
    p.psShiftSemis = jlimit (-24.0f, 24.0f, b.psShiftSemis + std::round (7.0f * a * d[2]));
    p.psFeedback = jlimit (0.0f, 0.95f, b.psFeedback * (1.0f + 0.5f * a * d[3]));
    p.psMix = clamp01 (b.psMix * (1.0f + 0.6f * a * d[4]));
    p.grMix = clamp01 (b.grMix * (1.0f + 0.6f * a * d[5]));
    p.grSizeMs = jlimit (10.0f, 1500.0f, b.grSizeMs * std::exp2 (1.5f * a * d[6]));
    p.grDensity = jlimit (1.0f, 24.0f, b.grDensity * std::exp2 (a * d[7]));
    p.grPos = clamp01 (b.grPos + 0.4f * a * d[8]);
    p.grPan = clamp01 (b.grPan + 0.4f * a * d[9]);
    p.grReverse = clamp01 (b.grReverse + 0.4f * a * d[10]);
    p.grFade = jlimit (0.01f, 10.0f, b.grFade * std::exp2 (a * d[11]));
    p.masterFb = jlimit (0.0f, 1.0f, b.masterFb * (1.0f + 0.15f * a * d[12]));
    return p;
}


// The rainboom, in the sphere. What sets one off (the switch, the editor, a hit, the rain stopping, a
// grain flying fast), then:
//   * the dive: the flyer comes in from outside the glass to where it breaks (a moving point, through
//     the same distance model as everything else, so it Doppler-shifts on the way in too),
//   * the boom: a point where it breaks,
//   * the rainbow: seven rings, one per colour, opening out flat round where it broke (red outermost,
//     the first to reach you); each ring is a dozen points summed into one set of 16 gains per block,
//     so a ring off to one side sounds like an arc there, and one passing over you surrounds you.
void FreakiePieProcessor::rainboomBlock (const float* inL, const float* inR, float* const* busW, float* const* sendW, int n,
                                         float sphereMetres, int count, float seconds)
{
    using fp::ambi::Vec3;
    if ((int) rbMono.size() < n) { rbMono.resize ((size_t) n); rbTmp.resize ((size_t) n); }
    float dive = -1.0f;
    bool follow = false;
    int followGrain = -1;
    const bool button = p_rbBoom->load() > 0.5f;
    if (button && ! rbButtonWas) dive = 0.7f;
    rbButtonWas = button;
    if (const int r = rbRequests.load(); r != rbRequestsSeen) { rbRequestsSeen = r; dive = jlimit (0.0f, 3.0f, rbRequestDive.load()); follow = rbRequestFollow.load(); }
    const bool rainNow = p_rain->load() > 0.5f;
    if (rbRainWas && ! rainNow && p_rbStorms->load() > 0.5f) dive = 0.9f;
    rbRainWas = rainNow;
    const bool hit = rbHitsDetector.process (inL, inR, n, 1.5f);
    if (hit && p_rbHits->load() > 0.5f && dive < 0.0f && rainboom.secondsSinceBoom() > 1.5) dive = 0.0f;
    // a grain flying faster than 30 m/s breaks it where it is (at most every 4 s)
    rbFastWait = jmax (0.0f, rbFastWait - seconds);
    {
        float fastest = 0.0f;
        for (int v = 0; v < jmin (count, (int) rbPrev.size()); ++v)
        {
            const auto pos = flightPaths.position (v);
            const float jump = (pos - rbPrev[(size_t) v]).length();
            const float sp = rbPrevValid && jump < 0.3f ? jump * sphereMetres / jmax (1.0e-4f, seconds) : 0.0f;   // (a jump is a grain put somewhere, not flying)
            if (sp > fastest && p_rbFast->load() > 0.5f && rbFastWait <= 0.0f && ! rainboom.diving() && dive < 0.0f && sp > 30.0f)
            { fastest = sp; followGrain = v; }
            rbPrev[(size_t) v] = pos;
        }
        if (followGrain >= 0) { dive = 0.5f; rbFastWait = 4.0f; }
        rbPrevValid = true;
    }
    if (dive >= 0.0f && rainboom.fire (dive))
    {
        rbFollowGrain = followGrain;
        rbFollowTarget = follow;
        auto randomPlace = [this]
        {
            const float az = rbRnd.nextFloat() * MathConstants<float>::twoPi, el = (rbRnd.nextFloat() - 0.3f) * 0.9f;
            return fp::ambi::fromAzEl (az, el) * (0.3f + 0.45f * rbRnd.nextFloat());
        };
        if (followGrain >= 0) rbAt = flightPaths.position (followGrain);
        else if (follow) rbAt = { rbTargetX.load(), rbTargetY.load(), rbTargetZ.load() };
        else rbAt = randomPlace();
        // it comes in from outside the glass, from above and to one side
        const float az = rbRnd.nextFloat() * MathConstants<float>::twoPi;
        rbFrom = fp::ambi::fromAzEl (az, 0.5f) * 1.7f;
        if (follow || followGrain >= 0) rbFrom = rbAt + (rbAt - rbFrom).normalised() * -0.9f;
        rbNow = rbFrom;
        rbDiveDist.reset();
    }
    if (rainboom.diving())
    {
        if (rbFollowGrain >= 0) rbAt = flightPaths.position (rbFollowGrain);
        else if (rbFollowTarget) rbAt = { rbTargetX.load(), rbTargetY.load(), rbTargetZ.load() };
        const float u = rainboom.diveProgress();
        const float e = u * u, v = 1.0f - e;   // (faster and faster, on a swoop down and up into it)
        const auto ctrl = (rbFrom + rbAt) * 0.5f + fp::ambi::Vec3 { 0.0f, 0.0f, -0.8f };
        rbNow = rbFrom * (v * v) + ctrl * (2.0f * v * e) + rbAt * (e * e);
    }

    // what it colours: everything the sphere is playing (its W)
    float* m = rbMono.data();
    for (int i = 0; i < n; ++i) m[i] = busW[0][i];
    fp::Rainboom::Settings rs;
    rs.tailSeconds = p_rbTail->load();
    const int boomsBefore = rainboom.numBooms();
    rainboom.process (m, n, rs);
    if (rainboom.numBooms() != boomsBefore) rbBoomDist.reset();
    rbAtX.store (rbAt.x); rbAtY.store (rbAt.y); rbAtZ.store (rbAt.z);
    rbNowX.store (rbNow.x); rbNowY.store (rbNow.y); rbNowZ.store (rbNow.z);
    rbFromX.store (rbFrom.x); rbFromY.store (rbFrom.y); rbFromZ.store (rbFrom.z);
    rainboomBooms.store (rainboom.numBooms());
    rainboomDives.store (rainboom.numDives());
    rainboomDiveLeft.store (rainboom.diveSecondsLeft());
    if (! rainboom.active()) return;
    const float level = p_rbLevel->load();
    if (level <= 0.0f) return;

    rbDiveDist.doppler = rbBoomDist.doppler = p_doppler->load() > 0.5f;   // (the Doppler switch: the dive's own squeeze stays either way)
    float* t = rbTmp.data();
    for (int i = 0; i < n; ++i) t[i] = level * rainboom.dive()[i];
    rbDiveDist.process (t, n, rbNow, sphereMetres, busW, sendW, 0.4f);
    for (int i = 0; i < n; ++i) t[i] = level * rainboom.boom()[i];
    rbBoomDist.process (t, n, rbAt, sphereMetres, busW, sendW, 0.6f);

    // the rings
    const double since = rainboom.secondsSinceBoom();
    const float inv = 1.0f / (float) n;
    for (int k = 0; k < fp::Rainboom::kBands; ++k)
    {
        const float r = ringRadius (since - rbGap * (double) k);
        fp::ambi::Coefs c {};
        constexpr int N = 12;
        for (int j = 0; j < N; ++j)
        {
            const float a = (float) j / (float) N * MathConstants<float>::twoPi;
            const Vec3 q = rbAt + Vec3 { std::cos (a), std::sin (a), 0.0f } * r;
            const float d = q.length();
            const float g = jmin (1.0f, 0.6f / jmax (0.05f, d)) / (float) N;
            const auto e = d <= 1.0f ? fp::ambi::encodeNear (q) : fp::ambi::sh (q.normalised());
            for (int ch = 0; ch < fp::ambi::kCh; ++ch) c[(size_t) ch] += g * e[(size_t) ch];
        }
        const float* y = rainboom.band (k);
        auto& was = rbRingWas[(size_t) k];
        for (int ch = 0; ch < fp::ambi::kCh; ++ch)
        {
            const float c0 = was[(size_t) ch], dc = (c[(size_t) ch] - c0) * inv;
            float* o = busW[ch];
            float* sd = sendW[ch];
            for (int i = 0; i < n; ++i)
            {
                const float v = level * (c0 + dc * (float) (i + 1)) * y[i];
                o[i] += v;
                sd[i] += 0.3f * v;
            }
        }
        was = c;
    }
}

AudioProcessorEditor* FreakiePieProcessor::createEditor() { return new FreakiePieEditor (*this); }

void FreakiePieProcessor::getStateInformation (MemoryBlock& destData)
{
    // the chalkboard drawing and the preset (for its picture) travel with the parameters
    StringArray pts;
    for (int i = 0; i < fp::ChalkShape::N; ++i) pts.add (String (chalk.v[(size_t) i].load(), 4));
    apvts.state.setProperty ("chalk", pts.joinIntoString (" "), nullptr);
    StringArray eqPts;
    for (int i = 0; i < fp::EqCurve::N; ++i) eqPts.add (String (eq.v[(size_t) i].load(), 4));
    apvts.state.setProperty ("eq", eqPts.joinIntoString (" "), nullptr);
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    {
        // where you put each grain
        StringArray h;
        for (int v = 0; v < kGrains; ++v) { const auto q = grainHome (v); h.add (String (q.x, 4)); h.add (String (q.y, 4)); h.add (String (q.z, 4)); }
        apvts.state.setProperty ("homes", h.joinIntoString (" "), nullptr);
        apvts.state.setProperty ("source", String (srcX.load(), 4) + " " + String (srcY.load(), 4) + " " + String (srcZ.load(), 4), nullptr);
    }
    {
        StringArray o;
        for (int id : getShelfOrder()) o.add (String (id));
        apvts.state.setProperty ("order", o.joinIntoString (" "), nullptr);
    }
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FreakiePieProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            loadStateXml (*xml);
}

void FreakiePieProcessor::loadStateXml (const XmlElement& xml)
{
    // sessions from before the stutter was sold at the store: if it was in use, it comes already bought
    bool knowsOwned = false;
    float oldStutter = 0.0f;
    for (auto* e : xml.getChildWithTagNameIterator ("PARAM"))
    {
        if (e->getStringAttribute ("id") == ids::stOwned) knowsOwned = true;
        if (e->getStringAttribute ("id") == ids::stAmount) oldStutter = (float) e->getDoubleAttribute ("value");
    }
    apvts.replaceState (ValueTree::fromXml (xml));
    if (! knowsOwned && oldStutter > 0.0f)
        if (auto* prm = apvts.getParameter (ids::stOwned)) prm->setValueNotifyingHost (1.0f);
    const auto pts = StringArray::fromTokens (apvts.state.getProperty ("chalk").toString(), " ", "");
    if (pts.size() == fp::ChalkShape::N)
    {
        for (int i = 0; i < fp::ChalkShape::N; ++i) chalk.set (i, pts[i].getFloatValue());
        chalk.updatePower();
    }
    currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
    {
        const auto h = StringArray::fromTokens (apvts.state.getProperty ("homes").toString(), " ", "");
        for (int v = 0; v < kGrains; ++v)
            setGrainHome (v, h.size() == 3 * kGrains ? fp::ambi::Vec3 { h[3 * v].getFloatValue(), h[3 * v + 1].getFloatValue(), h[3 * v + 2].getFloatValue() }
                                                     : fp::Flight::defaultHome (v, 0.9f));
        const auto sp = StringArray::fromTokens (apvts.state.getProperty ("source").toString(), " ", "");
        if (sp.size() == 3) { srcX.store (sp[0].getFloatValue()); srcY.store (sp[1].getFloatValue()); srcZ.store (sp[2].getFloatValue()); }
        else { srcX.store (0.5f); srcY.store (0.0f); srcZ.store (0.0f); }
    }
    const auto eqPts = StringArray::fromTokens (apvts.state.getProperty ("eq").toString(), " ", "");
    if (eqPts.size() == fp::EqCurve::N)
        for (int i = 0; i < fp::EqCurve::N; ++i) eq.set (i, eqPts[i].getFloatValue());
    else
        eq.flatten();
    eq.fit();
    {
        // the shelf's order (missing or old: the default, with anything left out added at the end)
        const auto toks = StringArray::fromTokens (apvts.state.getProperty ("order").toString(), " ", "");
        Order o; o.fill (-1);
        for (int i = 0; i < jmin ((int) o.size(), toks.size()); ++i) o[(size_t) i] = toks[i].getIntValue();
        setShelfOrder (toks.isEmpty() ? fp::Engine::defaultOrder() : o);
    }
}

void FreakiePieProcessor::resetToNormal()
{
    setCurrentProgram (0);
    eq.flatten(); eq.fit();
    for (int i = 0; i < fp::ChalkShape::N; ++i) chalk.set (i, std::sin (MathConstants<float>::pi * (float) i / (float) (fp::ChalkShape::N - 1)));
    chalk.updatePower();
    setShelfOrder (fp::Engine::defaultOrder());
    popStruckRequest.fetch_or ((1u << fp::StoreFX::kNum) - 1u);   // lightning's loans go back too
}

bool FreakiePieProcessor::trixieMayTouch (const String& id)
{
    for (auto* no : { ids::rbBoom, ids::rbHits, ids::rbStorms, ids::rbThrows, ids::rbFast, ids::grains, ids::grainMode, ids::fxBypass, ids::snow, ids::snowIn, ids::snowOut, ids::heat, ids::heatIn, ids::heatOut, ids::autoWeather, ids::balloonSfx, ids::popsThru, ids::grainsLevel, ids::catchFrom })
        if (id == no) return false;
    return ! id.endsWith ("Owned");
}

void FreakiePieProcessor::randomise (RangedAudioParameter& prm, Random& r)
{
    float v = r.nextFloat();
    if (dynamic_cast<AudioParameterBool*> (&prm) != nullptr) v = r.nextBool() ? 1.0f : 0.0f;
    prm.beginChangeGesture();
    prm.setValueNotifyingHost (v);
    prm.endChangeGesture();
}

void FreakiePieProcessor::trixieShuffle (Random& r)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (trixieMayTouch (rp->getParameterID())) randomise (*rp, r);
}

FreakiePieProcessor::Order FreakiePieProcessor::normaliseOrder (const Order& in)
{
    Order out; out.fill (-1);
    bool used[fp::Engine::kSlots] {};
    int n = 0;
    for (int id : in)
        if (isPositiveAndBelow (id, fp::Engine::kSlots) && ! used[id]) { used[id] = true; out[(size_t) n++] = id; }
    for (int id = 0; id < fp::Engine::kSlots; ++id)
        if (! used[id]) out[(size_t) n++] = id;
    return out;
}

void FreakiePieProcessor::setShelfOrder (const Order& o)
{
    const auto norm = normaliseOrder (o);
    for (size_t i = 0; i < norm.size(); ++i) shelf[i].store (norm[i]);
    shelfVersion.fetch_add (1);
}

FreakiePieProcessor::Order FreakiePieProcessor::getShelfOrder() const
{
    Order o;
    for (size_t i = 0; i < o.size(); ++i) o[i] = shelf[i].load();
    return o;
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FreakiePieProcessor(); }

//==============================================================================
// auto weather, on the message thread: once a second, let the sky move on
void FreakiePieProcessor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const double dt = lastTimerMs > 0.0 ? jlimit (0.0, 5.0, (now - lastTimerMs) * 0.001) : 0.0;
    lastTimerMs = now;
    keepSnowAndHeatApart();
    autoWeatherStep (dt);
}

// The snow and the heatwave never fall together: whichever came on last stays, and the other
// goes off (a session, a preset or host automation could ask for both; the switches don't).
void FreakiePieProcessor::keepSnowAndHeatApart()
{
    const bool snowOn = p_snow->load() > 0.5f, heatOn = p_heat->load() > 0.5f;
    if (snowOn && heatOn)
    {
        const char* off = ! heatWasOn ? ids::snow : ids::heat;   // the one that was already on gives way
        if (auto* prm = apvts.getParameter (off))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (0.0f);
            prm->endChangeGesture();
        }
        snowWasOn = off != ids::snow;
        heatWasOn = off != ids::heat;
        return;
    }
    snowWasOn = snowOn;
    heatWasOn = heatOn;
}

void FreakiePieProcessor::autoWeatherStep (double seconds)
{
    const bool on = p_autoWeather->load() > 0.5f;
    auto sw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load() > 0.5f; };
    if (! on) { autoWasOn = false; return; }
    if (! autoWasOn)
    {
        autoWasOn = true;
        autoSky.seed ((uint64_t) Time::currentTimeMillis());
        autoSky.start ({ sw (ids::rain), sw (ids::cloudy), sw (ids::snow), sw (ids::heat) });
        return;
    }
    if (! autoSky.step (seconds)) return;
    const auto k = autoSky.sky();
    auto set = [this] (const char* id, bool v)
    {
        if (auto* prm = apvts.getParameter (id))
            if ((prm->getValue() > 0.5f) != v)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (v ? 1.0f : 0.0f);
                prm->endChangeGesture();
            }
    };
    set (ids::rain, k.rain); set (ids::cloudy, k.cloudy); set (ids::snow, k.snow); set (ids::heat, k.heat);
}

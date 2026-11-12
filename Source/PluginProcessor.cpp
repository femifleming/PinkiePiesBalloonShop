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
    for (auto [id, name] : { std::pair { ids::fog, "Fog" }, std::pair { ids::wind, "Wind" }, std::pair { ids::hail, "Hail" },
                             std::pair { ids::aurora, "Aurora" }, std::pair { ids::twister, "Twister" } })
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, false));
    // the sonic rainboom
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbBoom, 1 }, "Rainboom", false));
    layout.add (std::make_unique<F> (ParameterID { ids::rbLevel, 1 }, "Rainboom Level", NormalisableRange<float> (0.0f, 1.0f), 0.7f,
                                     A().withStringFromValueFunction (pct)));
    layout.add (std::make_unique<F> (ParameterID { ids::rbTail, 1 }, "Rainbow Length", NormalisableRange<float> (1.0f, 12.0f, 0.0f, 0.6f), 5.0f,
                                     A().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbHits, 1 }, "Rainboom On Hits", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbStorms, 1 }, "Rainboom After Storms", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rbThrows, 1 }, "Rainboom On Throws", true));
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
                                                        StringArray { "saw", "reverse saw", "bell", "triangle", "pulse", "sine", "chalk" }, 0));
    mixParam (ids::grPan, "Grain Random Pan", 0.45f);
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
    return layout;
}

FreakiePieProcessor::FreakiePieProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
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
    p_fog = get (ids::fog); p_wind = get (ids::wind); p_hail = get (ids::hail);
    p_aurora = get (ids::aurora); p_twister = get (ids::twister);
    p_rbBoom = get (ids::rbBoom); p_rbLevel = get (ids::rbLevel); p_rbTail = get (ids::rbTail);
    p_rbHits = get (ids::rbHits); p_rbStorms = get (ids::rbStorms);
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
    engine.setChalk (&chalk);
    engine.setRack (&stutter, &storeFX);
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
    return folder.getChildFile ("FreakiePie").getChildFile ("Balloon Shoppe last patch.xml");   // (the file keeps the shop's old name, so the last patch carries over)
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
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
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
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    weather.prepare (sampleRate);
    balloonFX.prepare (sampleRate);
    storeFX.prepare (sampleRate);
    leveler.prepare (sampleRate);
    limiter.prepare (sampleRate);
    masterEq.prepare (sampleRate);
    snowFreeze.prepare (sampleRate);
    heatWave.prepare (sampleRate);
    trixieNoise.prepare (sampleRate);
    stutter.prepare (sampleRate);
    rainboom.prepare (sampleRate, samplesPerBlock);
    fog.prepare (sampleRate, 2, samplesPerBlock);
    wind.prepare (sampleRate, 2, samplesPerBlock);
    twister.prepare (sampleRate, 2, samplesPerBlock);
    hail.prepare (sampleRate, samplesPerBlock);
    rainbow.prepare (sampleRate, samplesPerBlock);
    aurora.prepare (sampleRate, samplesPerBlock);
    twButtonWas = p_twister->load() > 0.5f; rwRainWas = p_rain->load() > 0.5f;
    twRequestsSeen = twRequests.load(); rwRequestsSeen = rwRequests.load();
    rbHitsDetector.prepare (sampleRate);
    rbMono.setSize (1, jmax (1, samplesPerBlock), false, true, true);
    rbButtonWas = p_rbBoom->load() > 0.5f; rbRainWas = p_rain->load() > 0.5f; rbRequestsSeen = rbRequests.load();
    thruSm = p_grainsThru->load();
    bypassSm = p_fxBypass->load() > 0.5f ? 1.0f : 0.0f;
    grainsLevelSm = p_grainsLevel->load();
    nextStrikeIn = (int) (sampleRate * 2.0);
    for (auto& s : struckLeft) s = 0;
    struckMask.store (0);
    scratch.setSize (2, std::max (1, samplesPerBlock), false, true, true);
    chainIn.setSize (2, std::max (1, samplesPerBlock), false, true, true);
    grainBuf.setSize (2, std::max (1, samplesPerBlock), false, true, true);
    sfxBuf.setSize (2, std::max (1, samplesPerBlock), false, true, true);
    popsThruSm = p_popsThru->load() > 0.5f ? 1.0f : 0.0f;
    grainFifo.setSize (2, std::max (8192, 4 * samplesPerBlock));
    grainFifo.clear();
    grainFifoCount = 0;
    setLatencySamples (limiter.latency());   // the limiter's look-ahead (1.5 ms)
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
        weather.clap ((int) (sr * (0.25 + 1.0 * strikeRnd.nextDouble())), 0.7f + 0.3f * strikeRnd.nextFloat());
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
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (n == 0) return;

    if (scratch.getNumSamples() < n) // hosts shouldn't do this, but stay safe
        scratch.setSize (2, n, false, true, true);
    if (chainIn.getNumSamples() < n)
        chainIn.setSize (2, n, false, true, true);
    if (grainBuf.getNumSamples() < n)
        grainBuf.setSize (2, n, false, true, true);
    if (sfxBuf.getNumSamples() < n)
        sfxBuf.setSize (2, n, false, true, true);

    engine.setParams (readParams());

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    float* oL = scratch.getWritePointer (0);
    float* oR = scratch.getWritePointer (1);

    // the inflation tone joins the input on its way into the effects (but not the sound-balloon sampler)
    float* cL = chainIn.getWritePointer (0);
    float* cR = chainIn.getWritePointer (1);
    std::copy (inL, inL + n, cL);
    std::copy (inR, inR + n, cR);
    {
        const auto& scales = fp::getScales();
        const auto& sc = scales[(size_t) juce::jlimit (0, (int) scales.size() - 1, (int) p_psScale->load())];
        balloonFX.addInflationTone (cL, cR, n, sc);   // (the plucks are always on)
        trixieNoise.process (cL, cR, n);   // Trixie's noise, when she's playing one, goes in with your sound
    }
    // the hiss and pops: straight out (added after the effects, below) or into the effects with your
    // sound, crossfading over ~50 ms when you change it
    balloonFX.sfxOn = p_balloonSfx->load() > 0.5f;
    float* sL = sfxBuf.getWritePointer (0);
    float* sR = sfxBuf.getWritePointer (1);
    balloonFX.renderSfx (sL, sR, n);
    {
        const float target = p_popsThru->load() > 0.5f ? 1.0f : 0.0f;
        const float k = 1.0f - std::exp (-1.0f / (0.015f * (float) getSampleRate()));
        for (int i = 0; i < n; ++i)
        {
            popsThruSm += k * (target - popsThruSm);
            cL[i] += popsThruSm * sL[i];
            cR[i] += popsThruSm * sR[i];
            sL[i] *= 1.0f - popsThruSm;   // what's left goes straight out
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
    {
        // the shelf's order, if you've rearranged it
        const int v = shelfVersion.load();
        if (v != shelfSeen) { shelfSeen = v; engine.setOrder (getShelfOrder()); }
    }
    {
        // the gumball machine, on the host's tempo and position when it's playing
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
        stutter.begin (ss, bpm, ppq, playing);
    }
    {
        // whatever you've bought from the balloon store (they run on the shelf, in the engine)
        const auto& scales = fp::getScales();
        const int si = juce::jlimit (0, (int) scales.size() - 1, (int) p_psScale->load());
        fp::StoreFX::displayScale().store (si);
        fp::StoreFX::AllSettings st;
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
        storeFX.beginBlock (st, scales[(size_t) si]);
    }
    engine.process (cL, cR, oL, oR, n);
    {
        // bypass: the effects (chain and store) crossfade out over ~50 ms, leaving the input and the
        // inflation tone; the frozen balloons are added below and keep their grain controls
        const float target = p_fxBypass->load() > 0.5f ? 1.0f : 0.0f;
        if (target > 0.0f || bypassSm > 0.0f)
        {
            const float k = 1.0f - std::exp (-1.0f / (0.015f * (float) getSampleRate()));
            for (int i = 0; i < n; ++i)
            {
                bypassSm += k * (target - bypassSm);
                oL[i] += bypassSm * (cL[i] - oL[i]);
                oR[i] += bypassSm * (cR[i] - oR[i]);
            }
            if (target == 0.0f && bypassSm < 1.0e-4f) bypassSm = 0.0f;
            if (target == 1.0f && bypassSm > 0.9999f) bypassSm = 1.0f;
        }
        bypassAmount.store (bypassSm);
    }
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

        // what the sound pump catches: the effects' output (a clean copy, so balloons don't catch
        // each other or the pops) or the plugin's input
        const float* recL = inL;
        const float* recR = inR;
        if (p_catchFrom->load() < 0.5f)
        {
            std::copy (oL, oL + n, cL);
            std::copy (oR, oR + n, cR);
            recL = cL; recR = cR;
        }
        FloatVectorOperations::add (oL, sL, n);   // the hiss and pops that go straight out
        FloatVectorOperations::add (oR, sR, n);
        // frozen sound balloons; the grains come out separately so the canister can send them
        // straight out or round again (into the fifo, to join the effects' input next block), crossfading smoothly
        grainBuf.clear (0, n);
        float* gL = grainBuf.getWritePointer (0);
        float* gR = grainBuf.getWritePointer (1);
        balloonFX.process (recL, recR, oL, oR, n, gs, gL, gR);
        {
            // the frozen balloons' volume, ramped across the block
            const float target = juce::jlimit (0.0f, 2.0f, p_grainsLevel->load());
            if (target != 1.0f || grainsLevelSm != 1.0f)
            {
                const float step = (target - grainsLevelSm) / (float) n;
                for (int i = 0; i < n; ++i) { grainsLevelSm += step; gL[i] *= grainsLevelSm; gR[i] *= grainsLevelSm; }
                grainsLevelSm = target;
            }
        }
        // (with the effects bypassed there's nothing to go round through: the grains come straight out)
        const float thruT = p_fxBypass->load() > 0.5f ? 0.0f : juce::jlimit (0.0f, 1.0f, p_grainsThru->load());
        const float thruCoef = 1.0f - std::exp (-1.0f / (0.12f * (float) getSampleRate()));
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
    }
    rainboomBlock (inL, inR, oL, oR, n);   // (after the effects: the weather, the EQ and the limiter still shape it)
    spectralFreezeAmount.store (engine.getSpectralFreezeAmount());
    moreWeatherBefore (oL, oR, n);
    weather.process (oL, oR, n, p_rain->load() > 0.5f, p_helium->load() > 0.5f, p_cloudy->load() > 0.5f);
    rainAmount.store (weather.getRain());
    cloudAmount.store (weather.getClouds());
    thunderAmount.store (weather.getThunder());
    moreWeatherAfter (oL, oR, n);

    heatWave.setFadeTimes (p_heatIn->load(), p_heatOut->load());
    heatWave.process (oL, oR, n, p_heat->load() > 0.5f);   // heat: a slow smear (the snow can still freeze it)
    heatAmount.store (heatWave.getAmount());
    snowFreeze.setFadeTimes (p_snowIn->load(), p_snowOut->load());
    snowFreeze.process (oL, oR, n, p_snow->load() > 0.5f);   // snow: everything freezes, then the EQ still shapes it
    snowAmount.store (snowFreeze.getAmount());
    masterEq.process (oL, oR, n, eq);   // the curve drawn on the chalkboard

    // never clips, and never distorts doing it: a gentle leveler, then a look-ahead limiter at -1 dBFS
    leveler.process (oL, oR, n);
    limiter.process (oL, oR, n);
    heliumAmount.store (weather.getHelium());

    if (numOut > 1)
    {
        buffer.copyFrom (0, 0, oL, n);
        buffer.copyFrom (1, 0, oR, n);
    }
    else
    {
        buffer.copyFrom (0, 0, oL, n, 0.5f);
        buffer.addFrom  (0, 0, oR, n, 0.5f);
    }
    freezeAmount.store (engine.getFreezeAmount());
    outputLevel.store (jmax (buffer.getMagnitude (0, 0, n), numOut > 1 ? buffer.getMagnitude (1, 0, n) : 0.0f));
}

// The rainboom: what sets one off (the switch, the editor, a hit, the rain stopping), then the dive,
// the boom and the rainbow, made from what the effects are playing, mixed in on top.
void FreakiePieProcessor::rainboomBlock (const float* inL, const float* inR, float* oL, float* oR, int n)
{
    float dive = -1.0f;   // < 0: nothing new
    const bool button = p_rbBoom->load() > 0.5f;
    if (button && ! rbButtonWas) dive = 0.7f;
    rbButtonWas = button;
    if (const int r = rbRequests.load(); r != rbRequestsSeen) { rbRequestsSeen = r; dive = jlimit (0.0f, 3.0f, rbRequestDive.load()); }
    const bool rainNow = p_rain->load() > 0.5f;
    if (rbRainWas && ! rainNow && p_rbStorms->load() > 0.5f) dive = 0.9f;   // the storm clears
    rbRainWas = rainNow;
    const bool hit = rbHitsDetector.process (inL, inR, n, 1.5f);
    if (hit && p_rbHits->load() > 0.5f && dive < 0.0f && rainboom.secondsSinceBoom() > 1.5) dive = 0.0f;
    if (dive >= 0.0f) rainboom.fire (dive);

    if (rbMono.getNumSamples() < n) rbMono.setSize (1, n, false, true, true);
    float* m = rbMono.getWritePointer (0);
    for (int i = 0; i < n; ++i) m[i] = 0.5f * (oL[i] + oR[i]);
    fp::Rainboom::Settings rs;
    rs.tailSeconds = p_rbTail->load();
    rainboom.process (m, n, rs);
    if (rainboom.active()) rainboom.addStereo (oL, oR, n, p_rbLevel->load(), rs);
    rainboomBooms.store (rainboom.numBooms());
    rainboomDives.store (rainboom.numDives());
    rainboomDiveLeft.store (rainboom.diveSecondsLeft());
}

// The weather that changes the sound itself: the twister (a switch, the editor, or by itself now and
// then while it's windy and cloudy), the wind and the fog.
void FreakiePieProcessor::moreWeatherBefore (float* oL, float* oR, int n)
{
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    float* ch[2] = { oL, oR };
    const bool windOn = p_wind->load() > 0.5f;
    {
        bool go = false;
        const bool button = p_twister->load() > 0.5f;
        if (button && ! twButtonWas) go = true;
        twButtonWas = button;
        if (const int r = twRequests.load(); r != twRequestsSeen) { twRequestsSeen = r; go = true; }
        // windy and cloudy: one comes by itself about every two and a half minutes
        if (windOn && p_cloudy->load() > 0.5f && ! twister.active() && skyRnd.nextDouble() < (double) n / sr / 150.0) go = true;
        if (go) twister.start (10.0f + 10.0f * skyRnd.nextFloat());
    }
    twister.process (ch, 2, n);
    twister.spinStereo (oL, oR, n);
    wind.process (ch, 2, n, windOn);
    wind.leanStereo (oL, oR, n);
    fog.process (ch, 2, n, p_fog->load() > 0.5f);
    twisterAmount.store (twister.amount()); twisterAngle.store (twister.angle()); twisters.store (twister.numStarted());
    windAmount.store (wind.amount()); windGust.store (wind.gust()); windLean.store (wind.lean());
    fogAmount.store (fog.amount());
}

// The weather that adds to the sound, made from what's playing: hailstones, the rainbow when the rain
// stops under a clear sky, and the aurora.
void FreakiePieProcessor::moreWeatherAfter (float* oL, float* oR, int n)
{
    if (rbMono.getNumSamples() < n) rbMono.setSize (1, n, false, true, true);
    float* m = rbMono.getWritePointer (0);
    for (int i = 0; i < n; ++i) m[i] = 0.5f * (oL[i] + oR[i]);
    auto addPanned = [&] (const float* y, float pan)   // pan -1 left .. 1 right, equal power
    {
        const float a = (0.5f + 0.5f * jlimit (-1.0f, 1.0f, pan)) * MathConstants<float>::halfPi;
        const float gl = std::cos (a) * 1.41421356f, gr = std::sin (a) * 1.41421356f;
        for (int i = 0; i < n; ++i) { oL[i] += gl * y[i]; oR[i] += gr * y[i]; }
    };

    hail.process (m, n, p_hail->load() > 0.5f);
    for (int k = 0; k < fp::Hail::kStones; ++k)
        if (hail.active (k) || hail.fresh (k)) addPanned (hail.out (k), 0.9f * hail.pan (k));
    hailAmount.store (hail.amount()); hailHits.store (hail.hits.load());

    {
        const bool rainNow = p_rain->load() > 0.5f;
        // the sun comes out after a real shower (20 s of rain or more: a quick flick of the switch brings none)
        bool go = rwRainWas && ! rainNow && rwRainFor >= 20.0 && p_cloudy->load() < 0.5f && p_snow->load() < 0.5f;
        rwRainFor = rainNow ? rwRainFor + (double) n / (getSampleRate() > 0.0 ? getSampleRate() : 48000.0) : 0.0;
        rwRainWas = rainNow;
        if (const int r = rwRequests.load(); r != rwRequestsSeen) { rwRequestsSeen = r; go = true; }
        if (go) rainbow.start (25.0f);
    }
    rainbow.process (m, n);
    if (rainbow.active())
        for (int k = 0; k < fp::Rainbow::kBands; ++k)
            addPanned (rainbow.band (k), 0.85f * ((float) k - 3.0f) / 3.0f * 0.7071f);   // an arc, red on the left to violet on the right
    rainbowAmount.store (rainbow.amount()); rainbows.store (rainbow.numStarted());

    aurora.process (m, n, p_aurora->load() > 0.5f);
    if (aurora.active())
    {
        static const float pans[fp::Aurora::kCurtains] = { -0.85f, -0.3f, 0.3f, 0.85f };
        for (int j = 0; j < fp::Aurora::kCurtains; ++j) addPanned (aurora.out (j), pans[j] * 0.7071f);
    }
    auroraAmount.store (aurora.amount());
    for (int j = 0; j < fp::Aurora::kCurtains; ++j) auroraCurtain[(size_t) j].store (aurora.curtain (j));
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
    for (auto* no : { ids::rbBoom, ids::rbHits, ids::rbStorms, ids::rbThrows, ids::fxBypass, ids::twister, ids::snow, ids::snowIn, ids::snowOut, ids::heat, ids::heatIn, ids::heatOut, ids::autoWeather, ids::balloonSfx, ids::popsThru, ids::grainsLevel, ids::catchFrom })
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
    keepFogAndWindApart();
    autoWeatherStep (dt);
}

// The wind blows the fog away: whichever came on last stays, the other goes off.
void FreakiePieProcessor::keepFogAndWindApart()
{
    const bool fogOn = p_fog->load() > 0.5f, windOn = p_wind->load() > 0.5f;
    if (fogOn && windOn)
    {
        const char* off = ! windWasOn ? ids::fog : ids::wind;
        if (auto* prm = apvts.getParameter (off))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (0.0f);
            prm->endChangeGesture();
        }
        fogWasOn = off != ids::fog;
        windWasOn = off != ids::wind;
        return;
    }
    fogWasOn = fogOn;
    windWasOn = windOn;
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
        autoSky.start ({ sw (ids::rain), sw (ids::cloudy), sw (ids::snow), sw (ids::heat), sw (ids::fog), sw (ids::wind), sw (ids::hail), sw (ids::aurora) });
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
    set (ids::fog, k.fog); set (ids::wind, k.wind); set (ids::hail, k.hail); set (ids::aurora, k.aurora);
}

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "dsp/Weather.h"
#include "dsp/BalloonFX.h"
#include "dsp/StoreFX.h"
#include "dsp/Limiter.h"
#include "dsp/MasterEq.h"
#include "dsp/Snow.h"
#include "dsp/Heat.h"
#include "dsp/AutoWeather.h"
#include "dsp/Stutter.h"
#include "dsp/TrixieNoise.h"
#include "Presets.h"

// a snow fade time as people say it: "12 s", "1 min 20 s", "5 min"
inline juce::String snowFadeText (float seconds)
{
    const int s = juce::roundToInt (seconds);
    if (s < 60) return juce::String (s) + " s";
    return juce::String (s / 60) + " min" + (s % 60 != 0 ? " " + juce::String (s % 60) + " s" : juce::String());
}

namespace ids
{
    inline constexpr const char* sgMix      = "sgMix";
    inline constexpr const char* psShift    = "psShift";
    inline constexpr const char* psFeedback = "psFeedback";
    inline constexpr const char* psMix      = "psMix";
    inline constexpr const char* psScale    = "psScale";
    inline constexpr const char* grMix      = "grMix";
    inline constexpr const char* grFade     = "grFade";
    inline constexpr const char* grOctave   = "grOctave";
    inline constexpr const char* grRandOct  = "grRandOct";
    inline constexpr const char* grFreeze   = "grFreeze";
    inline constexpr const char* rvDecay    = "rvDecay";
    inline constexpr const char* rvMix      = "rvMix";
    inline constexpr const char* masterFb   = "masterFb";
    inline constexpr const char* rain       = "rain";     // gentle lowpass on the master out
    inline constexpr const char* helium     = "helium";   // gentle highpass on the master out
    // spectral smear
    inline constexpr const char* sgFreeze   = "sgFreeze";
    // grains (also shape the frozen sound balloons)
    inline constexpr const char* grSize     = "grSize";
    inline constexpr const char* grDensity  = "grDensity";
    inline constexpr const char* grShape    = "grShape";
    inline constexpr const char* grPan      = "grPan";
    inline constexpr const char* grReverse  = "grReverse";
    inline constexpr const char* grPos      = "grPos";        // where the grains read from
    inline constexpr const char* grPosRand  = "grPosRand";    // how far they wander from it
    // the inflation tone + what the sound pump catches
    inline constexpr const char* catchFrom  = "catchFrom";
    inline constexpr const char* balloonSfx = "balloonSfx";   // air hiss + pop sounds
    inline constexpr const char* popsThru   = "popsThru";     // the hiss and pops go into the effects instead of straight out
    inline constexpr const char* grainsThru = "grainsThru";   // frozen balloons: 0 straight out .. 1 back through the effects (the canister)
    // the gumball machine: a generative stutter on the main output
    inline constexpr const char* stAmount   = "stAmount";     // how often an event happens on a 16th
    inline constexpr const char* stBounce   = "stBounce";     // straight rolls .. bouncing, grainy events
    inline constexpr const char* stSway     = "stSway";       // swing: every second 16th held back
    inline constexpr const char* stCutup    = "stCutup";      // events taken from other steps of the last bar
    inline constexpr const char* snow       = "snow";         // a master spectral freeze that comes in over the fade-in time
    inline constexpr const char* snowIn     = "snowIn";       // seconds for the snow to freeze everything (5 s .. 5 min)
    inline constexpr const char* snowOut    = "snowOut";      // seconds for it to thaw (5 s .. 5 min)
    inline constexpr const char* heat       = "heat";         // a heatwave: a slow drifting spectral smear and a wandering lowpass
    inline constexpr const char* heatIn     = "heatIn";       // seconds for the heat to come in (5 s .. 5 min)
    inline constexpr const char* heatOut    = "heatOut";      // seconds for it to go away (5 s .. 5 min)
    inline constexpr const char* autoWeather = "autoWeather"; // the weather changes by itself, a spell every few minutes
    inline constexpr const char* cloudy     = "cloudy";       // clouds drift among the balloons; thunder; with rain, lightning
    inline constexpr const char* fxBypass   = "fxBypass";
    inline constexpr const char* grainsLevel = "grainsLevel"; // the frozen balloons' volume     // the effects (chain + store) step aside: input, tone and frozen balloons only

    // the balloon store: per effect, whether you own it, its two controls, its level and its width
    enum StorePart { storeOwned = 0, storeA, storeB, storeLevel, storeWidth, numStoreParts };
    inline juce::String store (int fx, StorePart part)
    {
        static const char* suffix[] = { "Owned", "A", "B", "Level", "Width" };
        return juce::String ("st") + fp::StoreFX::infos()[(size_t) fx].key + suffix[part];
    }
}

class FreakiePieProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    FreakiePieProcessor();
    ~FreakiePieProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    float grainsThruNow() const noexcept { return thruSm; }
    float limiterGainNow() const noexcept { return limiter.getGain(); }
    float levelerGainNow() const noexcept { return leveler.getGain(); }   // where the canister's crossfade has got to

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pinkie Pie's Balloon Shop"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentPreset.load()); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // the last patch you had open: every new instance starts from it (a session's own state still
    // wins, since the host restores that after). Saved by the editor as you play, and on close.
    static inline bool rememberLastState = true;   // (the tests turn this off)
    static inline juce::File lastStateOverride;     // (and point it somewhere else)
    static juce::File lastStateFile();
    void saveLastState();

    // the shelf: the order the effects run in (see fp::Engine::Slot). The editor sets it when you
    // rearrange the bows and boxes; it's saved with the session.
    using Order = fp::Engine::Order;
    void setShelfOrder (const Order&);
    Order getShelfOrder() const;
    static Order normaliseOrder (const Order&);   // every slot exactly once

    // the reset button: everything back to the first preset, a flat EQ, the plain grain shape and
    // the shelf in its first order (caught sounds stay)
    void resetToNormal();
    // Trixie's magic: one parameter, or every one she's allowed to touch, set at random. She leaves
    // alone what would silence or stop things (the bypass, the snow, the heat and their fade times, auto weather, the sound switches, the frozen
    // balloons' volume) and never buys anything at the store.
    static bool trixieMayTouch (const juce::String& paramID);
    static void randomise (juce::RangedAudioParameter&, juce::Random&);
    void trixieShuffle (juce::Random&);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> freezeAmount { 0.0f }, spectralFreezeAmount { 0.0f };
    std::atomic<float> outputLevel { 0.0f }; // block peak of the output, for the balloons to dance to
    std::atomic<float> rainAmount { 0.0f }, heliumAmount { 0.0f }; // how far each filter has eased in (0..1)
    fp::ChalkShape chalk;      // the grain shape drawn on the chalkboard
    fp::EqCurve eq;            // the master EQ drawn on the chalkboard
    std::atomic<int> currentPreset { 0 };   // which preset was last loaded (the editor shows its picture)
    fp::StoreFX storeFX;       // the effects bought from the balloon store
    fp::BalloonFX balloonFX;   // pop balloons + sound balloons (the editor drives it through its atomics)
    fp::TrixieNoise trixieNoise;   // what Trixie sometimes plays into the input

    // lightning (cloudy + rain): each strike lends a random store effect for a few seconds
    std::atomic<int> lightningCount { 0 };       // goes up by one per strike
    std::atomic<uint32_t> struckMask { 0 };      // store effects the lightning has brought (bit per effect)
    std::atomic<uint32_t> popStruckRequest { 0 }; // the editor asks for these to go early (popped with the pin)
    std::atomic<float> thunderAmount { 0.0f }, cloudAmount { 0.0f }, bypassAmount { 0.0f };
    std::atomic<float> snowAmount { 0.0f };   // how frozen the output is (0..1)
    std::atomic<float> heatAmount { 0.0f };   // how far the heatwave has come in (0..1)

    // auto weather: while its switch is on, time passing (on the message thread) changes the sky
    void autoWeatherStep (double seconds);
    void keepSnowAndHeatApart();   // (on the message thread, once a second)
    fp::AutoWeather::Spell autoWeatherSpell() const noexcept { return autoSky.current(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    fp::EngineParams readParams() const;

    fp::Engine engine;
    // grains on their way back into the effects: rendered after the chain, fed in on the next block
    juce::AudioBuffer<float> grainBuf, grainFifo;
    int grainFifoCount = 0;
    juce::AudioBuffer<float> sfxBuf;   // the hiss and pops, before they go out or into the effects
    float popsThruSm = 0.0f;
    bool snowWasOn = false, heatWasOn = false;   // (the snow and the heat never fall together: see timerCallback)
    juce::AudioBuffer<float> scratch, chainIn;   // chainIn: the input plus the inflation tone, for the effect chain
    fp::Weather weather;
    fp::Leveler leveler;
    fp::PeakLimiter limiter;
    fp::MasterEq masterEq;
    fp::SnowFreeze snowFreeze;
    fp::HeatWave heatWave;
    fp::AutoWeather autoSky;
    bool autoWasOn = false;
    double lastTimerMs = 0.0;
    void timerCallback() override;
    std::atomic<float>* p_stAmount = nullptr; std::atomic<float>* p_stBounce = nullptr;
    std::atomic<float>* p_stSway = nullptr;   std::atomic<float>* p_stCutup = nullptr;
public:
    fp::Stutter stutter;   // (the editor watches its event counter)
private:
    std::atomic<float>* p_snow = nullptr;
    std::atomic<float>* p_snowIn = nullptr; std::atomic<float>* p_snowOut = nullptr;
    std::atomic<float>* p_heat = nullptr; std::atomic<float>* p_autoWeather = nullptr;
    std::atomic<float>* p_rain = nullptr;
    std::atomic<float>* p_sgFreeze = nullptr;
    std::atomic<float>* p_grSize = nullptr;   std::atomic<float>* p_grDensity = nullptr; std::atomic<float>* p_grShape = nullptr;
    std::atomic<float>* p_grPan = nullptr;    std::atomic<float>* p_grReverse = nullptr;
    std::atomic<float>* p_grPos = nullptr;    std::atomic<float>* p_grPosRand = nullptr;
    std::atomic<float>* p_popsThru = nullptr;
    std::atomic<float>* p_heatIn = nullptr; std::atomic<float>* p_heatOut = nullptr;
    std::atomic<float>* p_catchFrom = nullptr;
    std::atomic<float>* p_balloonSfx = nullptr;
    std::atomic<float>* p_grainsThru = nullptr;
    std::atomic<float>* p_store[fp::StoreFX::kNum][ids::numStoreParts] {};
    float thruSm = 0.0f;   // the canister's position, smoothed per sample
    std::atomic<float>* p_helium = nullptr;
    std::atomic<float>* p_cloudy = nullptr;   std::atomic<float>* p_fxBypass = nullptr;
    std::atomic<float>* p_grainsLevel = nullptr;
    float grainsLevelSm = 1.0f;
    std::array<std::atomic<int>, fp::Engine::kSlots> shelf {};
    std::atomic<int> shelfVersion { 0 };
    int shelfSeen = -1;
    juce::String lastSaved;
    void loadStateXml (const juce::XmlElement&);
    float bypassSm = 0.0f;
    juce::Random strikeRnd;
    int nextStrikeIn = 0;                       // samples until the next lightning strike
    int struckLeft[fp::StoreFX::kNum] {};       // samples each lent effect has left
    void lightning (int n);

    std::atomic<float>* p_sgMix = nullptr;
    std::atomic<float>* p_psShift = nullptr;  std::atomic<float>* p_psFeedback = nullptr;
    std::atomic<float>* p_psMix = nullptr;    std::atomic<float>* p_psScale = nullptr;
    std::atomic<float>* p_grMix = nullptr;    std::atomic<float>* p_grFade = nullptr;
    std::atomic<float>* p_grOctave = nullptr; std::atomic<float>* p_grRandOct = nullptr;
    std::atomic<float>* p_grFreeze = nullptr; std::atomic<float>* p_rvDecay = nullptr;
    std::atomic<float>* p_rvMix = nullptr;    std::atomic<float>* p_masterFb = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreakiePieProcessor)
};

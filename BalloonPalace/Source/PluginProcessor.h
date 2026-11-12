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
#include "dsp/Rainboom.h"
#include "dsp/MoreWeather.h"
#include "dsp/Ambi.h"
#include "dsp/Flight.h"
#include "dsp/SpatialGranulator.h"
#include "dsp/Distance.h"
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
    inline constexpr const char* sgTilt     = "sgTilt";       // the smear's tilt: highs up and lows down, or the other way (dB per octave)
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
    inline constexpr const char* stOwned    = "stOwned";      // bought at the balloon store (it isn't on the shelf until you buy it)
    inline constexpr const char* snow       = "snow";         // a master spectral freeze that comes in over the fade-in time
    inline constexpr const char* snowIn     = "snowIn";       // seconds for the snow to freeze everything (5 s .. 5 min)
    inline constexpr const char* snowOut    = "snowOut";      // seconds for it to thaw (5 s .. 5 min)
    inline constexpr const char* heat       = "heat";         // a heatwave: a slow drifting spectral smear and a wandering lowpass
    inline constexpr const char* heatIn     = "heatIn";       // seconds for the heat to come in (5 s .. 5 min)
    inline constexpr const char* heatOut    = "heatOut";      // seconds for it to go away (5 s .. 5 min)
    inline constexpr const char* autoWeather = "autoWeather"; // the weather changes by itself, a spell every few minutes
    inline constexpr const char* cloudy     = "cloudy";       // clouds drift among the balloons; thunder; with rain, lightning
    // more weather (see dsp/MoreWeather.h)
    inline constexpr const char* fog        = "fog";          // every attack blurred, the highs sinking; fog banks hide where a grain is
    inline constexpr const char* wind       = "wind";         // gusts bend the pitch and blow the grains across the sphere
    inline constexpr const char* hail       = "hail";         // hailstones: tiny slices of the sound, bouncing down from overhead
    inline constexpr const char* aurora     = "aurora";       // an octave up, ringing in slow curtains high round the sphere
    inline constexpr const char* twister    = "twister";      // a switch: off -> on starts a whirlwind that takes the grains up
    inline constexpr const char* fxBypass   = "fxBypass";
    // the sphere
    inline constexpr const char* ambiOrder  = "ambiOrder";    // the Ambisonic order put out: 1st .. 7th (4 .. 64 channels)
    inline constexpr const char* doppler    = "doppler";      // things moving bend in pitch (on) or keep it (off)
    inline constexpr const char* grainMode  = "grainMode";    // copies: each grain is a whole copy of the chain; scatter: one chain, its grains fly about
    inline constexpr const char* grains     = "grains";       // how many copies of the whole chain fly in the sphere (1..16)
    inline constexpr const char* deviate    = "deviate";      // how far each copy's settings stray from yours, and how restlessly
    inline constexpr const char* flight     = "flight";       // how they move: still, orbit, lorenz, flock, random
    inline constexpr const char* flightSpeed = "flightSpeed";
    inline constexpr const char* spread     = "spread";       // how far out they fly (0 the middle, 1 the sphere's skin)
    // the sonic rainboom: something flies so fast the sound breaks, with a boom and a rainbow ring
    inline constexpr const char* rbBoom     = "rbBoom";       // a switch: off -> on starts one (a dive, then the boom)
    inline constexpr const char* rbLevel    = "rbLevel";      // how loud it is
    inline constexpr const char* rbTail     = "rbTail";       // how long the rainbow rings on (s)
    inline constexpr const char* rbHits     = "rbHits";       // a hit in the input sets one off (no dive)
    inline constexpr const char* rbStorms   = "rbStorms";     // the rain stopping sets one off
    inline constexpr const char* rbThrows   = "rbThrows";     // flinging a balloon hard enough sets one off
    inline constexpr const char* rbFast     = "rbFast";       // a grain flying fast enough sets one off, where it is
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

    const juce::String getName() const override { return "Pinkie Pie's Balloon Palace"; }
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
    static inline bool decorrelateCopies = true;   // (the tests turn this off once, to show what it prevents)
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

    // more weather, for the pictures
    std::atomic<float> fogAmount { 0.0f }, windAmount { 0.0f }, windGust { 0.0f }, windLean { 0.0f }, windHeading { 0.0f };
    std::atomic<float> hailAmount { 0.0f }, rainbowAmount { 0.0f }, auroraAmount { 0.0f }, twisterAmount { 0.0f }, twisterAngle { 0.0f };
    std::array<std::atomic<float>, fp::Aurora::kCurtains> auroraCurtain {};
    std::atomic<int> hailHits { 0 }, rainbows { 0 }, twisters { 0 };
    std::atomic<float> rainbowAz { 0.0f }, auroraTurn { 0.0f };   // where the rainbow stands (its middle, radians), how far the curtains have drifted
    struct StoneView { std::atomic<float> x { 0.0f }, y { 0.0f }, z { 0.0f }, on { 0.0f }; };
    std::array<StoneView, fp::Hail::kStones> stoneView;
    void requestTwister() noexcept { twRequests.fetch_add (1); }
    void requestRainbow() noexcept { rwRequests.fetch_add (1); }
    static fp::ambi::Vec3 rainbowPoint (float az, int band, float along) noexcept;   // a point of the bow (along: 0..1 from one foot to the other)

    // the rainboom: the editor asks for one (the button; a balloon flung hard, following it to where it
    // breaks); where it is, for the pictures
    void requestRainboom (float diveSeconds, bool followTarget = false) noexcept
    {
        rbRequestDive.store (diveSeconds); rbRequestFollow.store (followTarget); rbRequests.fetch_add (1);
    }
    std::atomic<float> rbTargetX { 0.0f }, rbTargetY { 0.0f }, rbTargetZ { 0.0f };   // (the flung balloon, written by the editor)
    std::atomic<int> rainboomBooms { 0 }, rainboomDives { 0 };
    std::atomic<float> rainboomDiveLeft { 0.0f };
    std::atomic<float> rbAtX { 0.0f }, rbAtY { 0.0f }, rbAtZ { 0.0f };      // where it breaks (fractions of the radius)
    std::atomic<float> rbNowX { 0.0f }, rbNowY { 0.0f }, rbNowZ { 0.0f };   // the flyer, while diving
    std::atomic<float> rbFromX { 0.0f }, rbFromY { 0.0f }, rbFromZ { 0.0f };   // where it dived in from
    static float ringRadius (double secondsSinceArrival) noexcept   // the rainbow ring (fractions of the radius): the sound and the picture share it
    {
        return secondsSinceArrival <= 0.0 ? 0.0f : 2.4f * (1.0f - (float) std::exp (-secondsSinceArrival / 0.55));
    }
    static constexpr float rbGap = 0.035f;   // seconds between the colours

    // auto weather: while its switch is on, time passing (on the message thread) changes the sky
    void autoWeatherStep (double seconds);
    void keepSnowAndHeatApart();   // (on the message thread, once a second)
    void keepFogAndWindApart();    // (the wind blows the fog away)
    fp::AutoWeather::Spell autoWeatherSpell() const noexcept { return autoSky.current(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==========================================================================
    // the sphere: up to 16 grains, each a whole copy of the chain, flying round inside it
    static constexpr int kGrains = fp::Flight::kMax;
    // the Distance knob is the size of the bubble you're in: its radius, 1 m (everything close and loud)
    // to 50 m (everything far, quiet and roomy)
    static float bubbleMetres (float knob) noexcept { return std::pow (50.0f, juce::jlimit (0.0f, 1.0f, knob)); }   // 1 m .. 50 m
    float bubbleNow = 7.07f;
    struct GrainView   // what the editor sees of each grain (written by the audio thread)
    {
        std::atomic<float> x { 0.0f }, y { 0.0f }, z { 0.0f }, level { 0.0f }, presence { 0.0f };
        std::atomic<int> stutters { 0 };   // goes up by one per stutter event
        std::atomic<int> frozen { 0 };      // bit 0 grains frozen, bit 1 spectrum frozen
        std::atomic<float> weather { 0.0f }; // how deep in the weather it is (0..1)
    };
    std::array<GrainView, kGrains> grainView;
    struct PatchView { std::atomic<float> x { 0.0f }, y { 0.0f }, z { 0.0f }, radius { 0.4f }, amount { 0.0f }; };
    std::array<PatchView, fp::SkyPatches::kNum> patchView;
    fp::ambi::EnergyMeter energy;            // the spatial level meter (the editor reads it)
    std::atomic<int> orderNow { 3 };          // the order coming out right now (the setting, or less if the track has too few channels)
    std::atomic<bool> ambiFallback { false }; // the track has fewer than 16 channels (only the first ones come out)
    std::atomic<int> outChannels { 2 };
    // the editor holds a grain (its index, or -1) and puts it somewhere
    std::atomic<int> heldGrain { -1 };
    std::atomic<float> heldX { 0.0f }, heldY { 0.0f }, heldZ { 0.0f };
    std::atomic<int> placeVersion { 0 };     // a grain was dropped somewhere new (homes changed)
    // where the dry sound is (the source: ahead, on the sphere's skin, to start with)
    std::atomic<float> srcX { 0.5f }, srcY { 0.0f }, srcZ { 0.0f };   // (6 m ahead)
    void setGrainHome (int v, fp::ambi::Vec3 p);
    fp::ambi::Vec3 grainHome (int v) const;

private:
    static BusesProperties sphereBuses();
    fp::EngineParams readParams() const;

    // one grain: a copy of the chain (its own stutter and store too), its own seed and its own
    // deviations from your settings
    struct Grain
    {
        fp::Engine engine;
        fp::Stutter stutter;
        fp::StoreFX store;
        std::array<float, 24> dev {}, devTarget {};   // -1..1 each: how this copy strays
        std::array<float, 24> devClock {};
        float gain = 0.0f;                           // fades in and out as grains are added or taken away
        float levelSm = 0.0f;
        bool freezeFlip = false, sgFlip = false;     // the copy's own freezes, now and then
        float flipLeft = 0.0f, nextFlip = 4.0f;
        fp::DistanceSource dist;                     // its distance: delay (Doppler), level, air, the walls
        fp::ambi::Decorrelator decor[2];             // (so no two copies are the same signal from two places)
        fp::Rng rng;
        int stutterSeen = 0;
    };
    std::array<std::unique_ptr<Grain>, kGrains> grainsArr;
    fp::Flight flightPaths;
    fp::SkyPatches sky;
    fp::ambi::SphereReverb sphereVerb;
    juce::AudioBuffer<float> bus, sendBus, verbBus, voiceBuf, decoded, sumBuf;
    std::array<fp::ambi::Vec3, kGrains> homes {};
    juce::SpinLock homeLock;
    std::array<fp::ambi::Vec3, kGrains> pendingHomes {};
    int placeSeen = 0;
    float voiceNormSm = 1.0f, verbMixSm = 0.0f;
    std::vector<float> ramp;
    juce::AudioBuffer<float> dryGains, grainBus;   // how much dry each copy passes (sample by sample); the granulator's 16 channels
    fp::DistanceSource srcDist, popsDist;
    std::vector<float> monoBuf;
    bool spatialWas = false;
    fp::SpatialGranulator granulator;   // the bypass, sample by sample, shared by every grain
    void stepGrainDeviation (Grain&, int v, float amount, float exposure, float seconds);
    fp::EngineParams grainParams (const fp::EngineParams& base, const Grain&, float a, bool grFreeze, bool sgFreeze) const;


    // grains on their way back into the effects: rendered after the chain, fed in on the next block
    juce::AudioBuffer<float> grainBuf, grainFifo;
    int grainFifoCount = 0;
    juce::AudioBuffer<float> sfxBuf;   // the hiss and pops, before they go out or into the effects
    float popsThruSm = 0.0f;
    bool snowWasOn = false, heatWasOn = false;   // (the snow and the heat never fall together: see timerCallback)
    juce::AudioBuffer<float> scratch, chainIn;   // chainIn: the input plus the inflation tone, for the effect chain
    static constexpr int kPairs = 32;  // the master stages run on the scene's channels two at a time (64 at 7th order)
    std::atomic<int> preparedPairs { 0 };   // how many of them are ready (the snow and the heat are big: made as the order needs them)
    double preparedRate = 0.0;
    void preparePairs (int upTo);
    std::array<fp::Weather, kPairs> weather;
    fp::MultiLeveler leveler;
    fp::MultiLimiter limiter;
    std::array<fp::MasterEq, kPairs> masterEq;
    std::array<std::unique_ptr<fp::SnowFreeze>, kPairs> snowFreeze;
    std::array<std::unique_ptr<fp::HeatWave>, kPairs> heatWave;
    fp::AutoWeather autoSky;
    fp::Fog fog;
    fp::Wind wind;
    fp::Twister twister;
    fp::Hail hail;
    fp::Rainbow rainbow;
    fp::Aurora aurora;
    std::array<fp::ambi::Coefs, fp::Hail::kStones> hailWas {};
    std::array<fp::ambi::Coefs, fp::Rainbow::kBands> rainbowWas {};
    std::array<fp::ambi::Coefs, fp::Aurora::kCurtains> auroraWas {};
    float rainbowAzNow = 0.0f, auroraTurnNow = 0.0f, fogTaperWas = 0.0f;
    bool rainbowWasActive = false, auroraWasActive = false;
    std::atomic<int> twRequests { 0 }, rwRequests { 0 };
    int twRequestsSeen = 0, rwRequestsSeen = 0;
    double rwRainFor = 0.0;   // how long it has been raining (seconds)
    bool twButtonWas = false, rwRainWas = false, fogWasOn = false, windWasOn = false;
    juce::Random skyRnd;
    std::vector<float> skyMono;
    std::atomic<float>* p_fog = nullptr; std::atomic<float>* p_wind = nullptr; std::atomic<float>* p_hail = nullptr;
    std::atomic<float>* p_aurora = nullptr; std::atomic<float>* p_twister = nullptr;
    void moreWeatherInSphere (float* const* busW, float* const* sendW, int n, int order, float seconds);   // hail, rainbow, aurora: points and arcs in the sphere
    void moreWeatherOnMaster (float* const* chans, int numCh, int n, int order);                           // twister, wind, fog: every channel alike
    fp::Rainboom rainboom;
    fp::HitDetector rbHitsDetector;
    fp::DistanceSource rbDiveDist, rbBoomDist;
    std::vector<float> rbMono, rbTmp;
    std::array<fp::ambi::Coefs, fp::Rainboom::kBands> rbRingWas {};
    std::array<fp::ambi::Vec3, 16> rbPrev {};
    fp::ambi::Vec3 rbFrom {}, rbAt {}, rbNow {};
    int rbFollowGrain = -1;
    bool rbFollowTarget = false;
    float rbFastWait = 0.0f;
    bool rbPrevValid = false;
    std::atomic<int> rbRequests { 0 };
    std::atomic<float> rbRequestDive { 0.7f };
    std::atomic<bool> rbRequestFollow { false };
    int rbRequestsSeen = 0, rbBoomsSeen = 0;
    bool rbButtonWas = false, rbRainWas = false;
    juce::Random rbRnd;
    std::atomic<float>* p_rbBoom = nullptr; std::atomic<float>* p_rbLevel = nullptr; std::atomic<float>* p_rbTail = nullptr;
    std::atomic<float>* p_rbHits = nullptr; std::atomic<float>* p_rbStorms = nullptr; std::atomic<float>* p_rbFast = nullptr;
    void rainboomBlock (const float* inL, const float* inR, float* const* busW, float* const* sendW, int n, float sphereMetres, int count, float seconds);
    bool autoWasOn = false;
    double lastTimerMs = 0.0;
    void timerCallback() override;
    std::atomic<float>* p_stAmount = nullptr; std::atomic<float>* p_stBounce = nullptr;
    std::atomic<float>* p_stSway = nullptr;   std::atomic<float>* p_stCutup = nullptr;
    std::atomic<float>* p_stOwned = nullptr;  std::atomic<float>* p_sgTilt = nullptr;
public:
    fp::Stutter stutter;   // (the editor watches its event counter: the grains' stutters all count on it)
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
    std::atomic<float>* p_grains = nullptr; std::atomic<float>* p_deviate = nullptr; std::atomic<float>* p_flight = nullptr;
    std::atomic<float>* p_flightSpeed = nullptr; std::atomic<float>* p_spread = nullptr;
    std::atomic<float>* p_ambiOrder = nullptr;
    std::atomic<float>* p_grainMode = nullptr; std::atomic<float>* p_doppler = nullptr;
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

#include "Presets.h"
#include "PluginProcessor.h"

namespace presets
{
using K = fp::StoreFX::Kind;

static Value st (int fx, ids::StorePart part, float v) { return { ids::store (fx, part), v }; }

const std::vector<Preset>& list()
{
    static const std::vector<Preset> l = []
    {
        constexpr float sine = 5.0f;   // grain shapes: 0 saw, 1 reverse saw, 2 bell, 3 triangle, 4 pulse, 5 sine, 6 chalk
        std::vector<Preset> p;

        // the default: Ponyville. A touch of reverb and smear, and long grains drifting about the
        // stereo field; no pitch shifting, no random octaves, nothing else
        p.push_back ({ "Sugarcube Corner", "start", 13, {},
                       { { ids::rvMix, 0.2f }, { ids::sgMix, 0.2f },
                         { ids::grMix, 0.25f }, { ids::grSize, 800.0f }, { ids::grPosRand, 1.0f }, { ids::grPan, 0.8f },
                         { ids::psMix, 0.0f }, { ids::psFeedback, 0.0f }, { ids::grRandOct, 0.0f } }, {} });

        // ---- the sphere: grains flying round you
        p.push_back ({ "Cloudsdale Carousel", "start", 3, {},
                       { { ids::grains, 6.0f }, { ids::flight, 1.0f }, { ids::flightSpeed, 0.35f }, { ids::spread, 0.71f }, { ids::deviate, 0.5f },
                         { ids::rvMix, 0.3f }, { ids::sgMix, 0.25f }, { ids::grMix, 0.35f }, { ids::grSize, 600.0f }, { ids::psMix, 0.0f }, { ids::psFeedback, 0.0f } }, {} });
        p.push_back ({ "Butterfly Falls", "start", 11, "Major Pentatonic",
                       { { ids::grains, 4.0f }, { ids::flight, 2.0f }, { ids::flightSpeed, 0.5f }, { ids::spread, 0.74f }, { ids::deviate, 0.7f },
                         { ids::psShift, 7.0f }, { ids::psMix, 0.3f }, { ids::rvMix, 0.35f }, { ids::rvDecay, 7.0f }, { ids::grMix, 0.3f },
                         { ids::stOwned, 1.0f }, { ids::stAmount, 0.25f } }, {} });
        p.push_back ({ "Flock Over Ponyville", "start", 8, {},
                       { { ids::grainMode, 1.0f }, { ids::grains, 10.0f }, { ids::flight, 3.0f }, { ids::flightSpeed, 0.55f }, { ids::spread, 0.67f },
                         { ids::grMix, 0.75f }, { ids::grSize, 180.0f }, { ids::grDensity, 10.0f }, { ids::grPosRand, 1.0f },
                         { ids::rvMix, 0.25f }, { ids::psMix, 0.0f }, { ids::psFeedback, 0.0f } }, {} });

        // ---- feedback: the main loop turned up until it sings by itself
        p.push_back ({ "Canterlot Chimes", "feedback", 1, "Major (Ionian)",
                       { { ids::masterFb, 0.90f }, { ids::psShift, 7.0f }, { ids::psFeedback, 0.6f }, { ids::psMix, 0.5f },
                         { ids::rvDecay, 9.0f }, { ids::rvMix, 0.55f }, { ids::sgMix, 0.4f }, { ids::grMix, 0.2f } }, {} });
        p.push_back ({ "Moonlight Howl", "feedback", 6, "Slendro",
                       { { ids::masterFb, 0.96f }, { ids::psShift, -12.0f }, { ids::psFeedback, 0.5f }, { ids::psMix, 0.45f },
                         { ids::rvDecay, 16.0f }, { ids::rvMix, 0.6f }, { ids::sgMix, 0.7f },
                         { ids::grMix, 0.3f }, { ids::grSize, 400.0f }, { ids::grDensity, 4.0f }, { ids::grShape, sine } }, {} });
        p.push_back ({ "Smokey Mountain Squall", "feedback", 12, "Blues",
                       { { ids::masterFb, 0.93f }, { ids::psShift, 5.0f }, { ids::psFeedback, 0.75f }, { ids::psMix, 0.6f },
                         { ids::rvDecay, 4.0f }, { ids::rvMix, 0.35f }, { ids::sgMix, 0.25f },
                         st (K::disto, ids::storeA, 0.6f), st (K::disto, ids::storeB, 0.6f), st (K::disto, ids::storeLevel, 0.6f) },
                       { K::disto } });
        p.push_back ({ "Las Pegasus Neon Loop", "feedback", 5, "Lydian",
                       { { ids::masterFb, 0.88f }, { ids::psShift, 12.0f }, { ids::psFeedback, 0.85f }, { ids::psMix, 0.55f },
                         { ids::rvDecay, 7.0f }, { ids::rvMix, 0.5f }, { ids::sgMix, 0.3f },
                         st (K::phaser, ids::storeA, 0.3f), st (K::phaser, ids::storeB, 0.6f), st (K::phaser, ids::storeLevel, 0.7f) },
                       { K::phaser } });

        // ---- stretch: big, sparse, sine-shaped grains that pull the sound out long
        p.push_back ({ "Cloudsdale Stretch", "stretch", 3, {},
                       { { ids::grMix, 1.0f }, { ids::grSize, 1500.0f }, { ids::grDensity, 1.6f }, { ids::grShape, sine },
                         { ids::grPan, 0.7f }, { ids::sgMix, 0.5f }, { ids::rvDecay, 8.0f }, { ids::rvMix, 0.35f }, { ids::psMix, 0.0f } }, {} });
        p.push_back ({ "Still Lake", "stretch", 10, {},
                       { { ids::grMix, 0.95f }, { ids::grSize, 1200.0f }, { ids::grDensity, 2.0f }, { ids::grShape, sine },
                         { ids::grRandOct, 0.0f }, { ids::sgMix, 0.6f }, { ids::rvDecay, 12.0f }, { ids::rvMix, 0.5f }, { ids::psMix, 0.0f } }, {} });
        p.push_back ({ "Slow Rainbow Falls", "stretch", 9, {},
                       { { ids::grMix, 0.9f }, { ids::grSize, 1500.0f }, { ids::grDensity, 1.2f }, { ids::grShape, sine },
                         { ids::grReverse, 0.5f }, { ids::grPan, 0.9f }, { ids::sgMix, 0.3f }, { ids::rvDecay, 6.0f }, { ids::rvMix, 0.4f },
                         { ids::psMix, 0.0f },
                         st (K::delay, ids::storeA, 0.6f), st (K::delay, ids::storeB, 0.45f), st (K::delay, ids::storeLevel, 0.5f) },
                       { K::delay } });
        p.push_back ({ "Golden Hour", "stretch", 2, {},
                       { { ids::grMix, 0.9f }, { ids::grSize, 900.0f }, { ids::grDensity, 2.5f }, { ids::grShape, sine },
                         { ids::grOctave, -1.0f }, { ids::grRandOct, 1.0f }, { ids::sgMix, 0.5f }, { ids::rvDecay, 10.0f },
                         { ids::rvMix, 0.45f }, { ids::psMix, 0.0f } }, {} });

        // ---- useful everyday ones
        p.push_back ({ "Ponyville Morning", "useful", 8, {},
                       { { ids::sgMix, 0.2f }, { ids::psShift, 12.0f }, { ids::psFeedback, 0.2f }, { ids::psMix, 0.15f },
                         { ids::rvDecay, 3.0f }, { ids::rvMix, 0.25f }, { ids::grMix, 0.2f } }, {} });
        p.push_back ({ "Main Street Shimmer", "useful", 13, {},
                       { { ids::psShift, 12.0f }, { ids::psFeedback, 0.5f }, { ids::psMix, 0.35f }, { ids::rvDecay, 8.0f },
                         { ids::rvMix, 0.45f }, { ids::sgMix, 0.3f }, { ids::grMix, 0.15f } }, {} });
        p.push_back ({ "Ghastly Gorge Echo", "useful", 0, {},
                       { { ids::rvMix, 0.2f }, { ids::sgMix, 0.15f }, { ids::psMix, 0.0f }, { ids::grMix, 0.1f },
                         st (K::delay, ids::storeA, 0.38f), st (K::delay, ids::storeB, 0.55f), st (K::delay, ids::storeLevel, 0.8f),
                         st (K::reverb, ids::storeA, 0.85f), st (K::reverb, ids::storeB, 4000.0f), st (K::reverb, ids::storeLevel, 0.6f) },
                       { K::delay, K::reverb } });
        p.push_back ({ "Froggy Bottom Chorus", "useful", 4, {},
                       { { ids::psMix, 0.0f }, { ids::sgMix, 0.2f }, { ids::grMix, 0.25f }, { ids::rvMix, 0.3f },
                         st (K::phaser, ids::storeA, 0.15f), st (K::phaser, ids::storeB, 0.4f), st (K::phaser, ids::storeLevel, 0.8f),
                         st (K::harmonizer, ids::storeA, -5.0f), st (K::harmonizer, ids::storeB, 0.2f), st (K::harmonizer, ids::storeLevel, 0.6f) },
                       { K::phaser, K::harmonizer } });
        p.push_back ({ "Rock Farm Crunch", "useful", 7, {},
                       { { ids::sgMix, 0.1f }, { ids::psMix, 0.0f }, { ids::grMix, 0.2f }, { ids::rvMix, 0.15f },
                         st (K::disto, ids::storeA, 0.8f), st (K::disto, ids::storeB, 0.5f), st (K::disto, ids::storeLevel, 0.7f),
                         st (K::degrade, ids::storeA, 6.0f), st (K::degrade, ids::storeB, 0.2f), st (K::degrade, ids::storeLevel, 0.6f),
                         st (K::clipper, ids::storeA, 0.05f), st (K::clipper, ids::storeB, 3000.0f), st (K::clipper, ids::storeLevel, 0.5f) },
                       { K::disto, K::degrade, K::clipper } });
        p.push_back ({ "Rainbow Resonators", "useful", 11, "Major Pentatonic",
                       { { ids::sgMix, 0.3f }, { ids::grMix, 0.3f }, { ids::rvMix, 0.4f }, { ids::rvDecay, 6.0f }, { ids::psMix, 0.0f },
                         st (K::waveguide, ids::storeA, 130.0f), st (K::waveguide, ids::storeB, 8.0f), st (K::waveguide, ids::storeLevel, 0.7f),
                         st (K::complexRes, ids::storeA, 520.0f), st (K::complexRes, ids::storeB, 1.5f), st (K::complexRes, ids::storeLevel, 0.6f),
                         st (K::allpassWG, ids::storeA, 196.0f), st (K::allpassWG, ids::storeB, 0.4f), st (K::allpassWG, ids::storeLevel, 0.5f) },
                       { K::waveguide, K::complexRes, K::allpassWG } });
        return p;
    }();
    return l;
}
}

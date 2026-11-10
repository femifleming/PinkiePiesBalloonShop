#pragma once
#include "Utils.h"

namespace fp
{
//==============================================================================
// Auto weather: the sky changes on its own, slowly, the way it does outside.
//
// The weather is one of six spells, and each one lasts a while before it turns into a
// neighbour, never jumping from a heatwave straight to snow:
//
//   clear   3..12 min  -> cloudy (most often), a heatwave, or more clear sky
//   cloudy  2..8 min   -> clears up, rains, a storm, or snow
//   rain    2..7 min   -> a storm builds, or it eases back to cloud or clear
//   storm   1..4 min   -> back to rain, or to cloud
//   snow    3..10 min  -> cloud or clear
//   heat    4..12 min  -> clear, a summer storm, or cloud
//
// Each spell is a set of switches (rain, clouds, snow, heat). The first change comes sooner
// (30..90 s) so you can hear it's working. Nothing here touches the audio thread: the owner
// calls step() with the time gone by and applies the switches it gets back.
class AutoWeather
{
public:
    enum Spell { clear = 0, cloudy, rain, storm, snow, heat, numSpells };
    struct Sky { bool rain = false, cloudy = false, snow = false, heat = false; };

    static Sky skyOf (Spell s) noexcept
    {
        switch (s)
        {
            case cloudy: return { false, true, false, false };
            case rain:   return { true, false, false, false };
            case storm:  return { true, true, false, false };
            case snow:   return { false, true, true, false };
            case heat:   return { false, false, false, true };
            case clear: case numSpells: default: return {};
        }
    }

    // the spell closest to what the switches show now (so it carries on from whatever you set)
    static Spell spellOf (const Sky& k) noexcept
    {
        if (k.heat) return heat;
        if (k.snow) return snow;
        if (k.rain && k.cloudy) return storm;
        if (k.rain) return rain;
        if (k.cloudy) return cloudy;
        return clear;
    }

    void seed (uint64_t s) noexcept { rng.seed (s); }

    // auto weather was just switched on, with the sky as it is now
    void start (const Sky& now) noexcept
    {
        spell = spellOf (now);
        left = rng.range (30.0f, 90.0f);
    }

    // time passes; returns true when the spell changes (then read sky())
    bool step (double seconds) noexcept
    {
        left -= (float) seconds;
        if (left > 0.0f) return false;
        spell = next (spell);
        left = 60.0f * lengthMinutes (spell);
        return true;
    }

    Spell current() const noexcept { return spell; }
    Sky sky() const noexcept { return skyOf (spell); }
    float secondsLeft() const noexcept { return left; }

private:
    float lengthMinutes (Spell s) noexcept
    {
        switch (s)
        {
            case clear:  return rng.range (3.0f, 12.0f);
            case cloudy: return rng.range (2.0f, 8.0f);
            case rain:   return rng.range (2.0f, 7.0f);
            case storm:  return rng.range (1.0f, 4.0f);
            case snow:   return rng.range (3.0f, 10.0f);
            case heat:   return rng.range (4.0f, 12.0f);
            case numSpells: default: return 5.0f;
        }
    }

    Spell next (Spell s) noexcept
    {
        struct W { Spell to; float w; };
        auto pick = [this] (std::initializer_list<W> ws)
        {
            float total = 0.0f;
            for (auto& w : ws) total += w.w;
            float r = rng.uni() * total;
            for (auto& w : ws) { if ((r -= w.w) < 0.0f) return w.to; }
            return ws.begin()->to;
        };
        switch (s)
        {
            case clear:  return pick ({ { cloudy, 0.55f }, { heat, 0.25f }, { clear, 0.20f } });
            case cloudy: return pick ({ { clear, 0.35f }, { rain, 0.35f }, { storm, 0.15f }, { snow, 0.15f } });
            case rain:   return pick ({ { storm, 0.30f }, { cloudy, 0.50f }, { clear, 0.20f } });
            case storm:  return pick ({ { rain, 0.60f }, { cloudy, 0.40f } });
            case snow:   return pick ({ { cloudy, 0.60f }, { clear, 0.40f } });
            case heat:   return pick ({ { clear, 0.60f }, { storm, 0.25f }, { cloudy, 0.15f } });
            case numSpells: default: return clear;
        }
    }

    Spell spell = clear;
    float left = 60.0f;
    Rng rng;
};
} // namespace fp

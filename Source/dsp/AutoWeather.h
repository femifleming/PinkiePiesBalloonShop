#pragma once
#include "Utils.h"

namespace fp
{
//==============================================================================
// Auto weather: the sky changes on its own, slowly, the way it does outside.
//
// The weather is one of ten spells, and each one lasts a while before it turns into a
// neighbour, never jumping from a heatwave straight to snow:
//
//   clear   3..12 min  -> cloudy (most often), a heatwave, wind, an aurora, or more clear sky
//   cloudy  2..8 min   -> clears up, rains, a storm, snow, fog or wind
//   rain    2..7 min   -> a storm builds, or it eases back to cloud, clear (with a rainbow) or fog
//   storm   1..4 min   -> back to rain, to cloud, or it hails
//   snow    3..10 min  -> cloud, clear or fog
//   heat    4..12 min  -> clear, a summer storm, or cloud
//   fog     2..7 min   -> lifts to clear or cloud, or turns to rain
//   wind    2..6 min   -> drops to clear or cloud, or blows up a storm
//   hail    30 s..2 min -> back to a storm, rain or cloud
//   aurora  3..8 min   -> clear or cloud
//
// Each spell is a set of switches (rain, clouds, snow, heat, fog, wind, hail, aurora). The first change comes sooner
// (30..90 s) so you can hear it's working. Nothing here touches the audio thread: the owner
// calls step() with the time gone by and applies the switches it gets back.
class AutoWeather
{
public:
    enum Spell { clear = 0, cloudy, rain, storm, snow, heat, fog, wind, hail, aurora, numSpells };
    struct Sky { bool rain = false, cloudy = false, snow = false, heat = false, fog = false, wind = false, hail = false, aurora = false; };

    static Sky skyOf (Spell s) noexcept
    {
        switch (s)
        {
            case cloudy: return { false, true, false, false };
            case rain:   return { true, false, false, false };
            case storm:  return { true, true, false, false };
            case snow:   return { false, true, true, false };
            case heat:   return { false, false, false, true };
            case fog:    { Sky k; k.fog = true; return k; }
            case wind:   { Sky k; k.wind = true; return k; }
            case hail:   { Sky k; k.rain = k.cloudy = k.hail = true; return k; }
            case aurora: { Sky k; k.aurora = true; return k; }
            case clear: case numSpells: default: return {};
        }
    }

    // the spell closest to what the switches show now (so it carries on from whatever you set)
    static Spell spellOf (const Sky& k) noexcept
    {
        if (k.hail) return hail;
        if (k.heat) return heat;
        if (k.snow) return snow;
        if (k.rain && k.cloudy) return storm;
        if (k.rain) return rain;
        if (k.cloudy) return cloudy;
        if (k.fog) return fog;
        if (k.wind) return wind;
        if (k.aurora) return aurora;
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
            case fog:    return rng.range (2.0f, 7.0f);
            case wind:   return rng.range (2.0f, 6.0f);
            case hail:   return rng.range (0.5f, 2.0f);
            case aurora: return rng.range (3.0f, 8.0f);
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
            case clear:  return pick ({ { cloudy, 0.45f }, { heat, 0.20f }, { clear, 0.10f }, { wind, 0.15f }, { aurora, 0.10f } });
            case cloudy: return pick ({ { clear, 0.25f }, { rain, 0.30f }, { storm, 0.12f }, { snow, 0.13f }, { fog, 0.12f }, { wind, 0.08f } });
            case rain:   return pick ({ { storm, 0.30f }, { cloudy, 0.40f }, { clear, 0.20f }, { fog, 0.10f } });
            case storm:  return pick ({ { rain, 0.50f }, { cloudy, 0.30f }, { hail, 0.20f } });
            case snow:   return pick ({ { cloudy, 0.45f }, { clear, 0.35f }, { fog, 0.20f } });
            case heat:   return pick ({ { clear, 0.60f }, { storm, 0.25f }, { cloudy, 0.15f } });
            case fog:    return pick ({ { clear, 0.50f }, { cloudy, 0.35f }, { rain, 0.15f } });
            case wind:   return pick ({ { clear, 0.35f }, { cloudy, 0.35f }, { storm, 0.30f } });
            case hail:   return pick ({ { storm, 0.50f }, { rain, 0.30f }, { cloudy, 0.20f } });
            case aurora: return pick ({ { clear, 0.70f }, { cloudy, 0.30f } });
            case numSpells: default: return clear;
        }
    }

    Spell spell = clear;
    float left = 60.0f;
    Rng rng;
};
} // namespace fp

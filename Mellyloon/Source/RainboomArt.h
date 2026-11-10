#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// The sonic rainboom's pictures, cartoon style (shared by the Balloon Shop and Mellyloon; Mellyloon
// projects its 3-D points to the screen first, then draws with the same brushes):
//   ribbon   the rainbow trail: a smooth, tapered ribbon of seven bands that flutters a little,
//            with a soft white glow and a white shine along it; it fades from its tail end
//   comet    the flyer: a cyan comet with a white teardrop glow, and near the barrier a white cone
//            with jagged shock lines at its nose
//   burst    the boom: a white starburst that pops and fades, with pow lines
//   donut    the ring: seven bands round a centre (any shape: a function gives each point), with
//            puffy cloud edges outside and in, and white shock ripples inside
//   sparkles stars thrown out from the boom, falling and twinkling
namespace rbart
{
using juce::Point; using juce::Colour; using juce::Path; using juce::Graphics;

inline const Colour& band (int k)   // outside (red) to inside (violet)
{
    static const Colour c[7] = { Colour (0xffff3b47), Colour (0xffff9a1f), Colour (0xffffe03a), Colour (0xff3fd16b),
                                 Colour (0xff2f9bff), Colour (0xff5b5cf0), Colour (0xffc05cf5) };
    return c[juce::jlimit (0, 6, k)];
}

inline float smooth01 (float x) { x = juce::jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); }

// a smooth line through the points (Catmull-Rom), every few pixels
inline std::vector<Point<float>> smoothLine (const std::vector<Point<float>>& p, float step = 5.0f)
{
    std::vector<Point<float>> out;
    if (p.size() < 2) return p;
    for (size_t i = 0; i + 1 < p.size(); ++i)
    {
        const auto p0 = p[i > 0 ? i - 1 : i], p1 = p[i], p2 = p[i + 1], p3 = p[i + 2 < p.size() ? i + 2 : i + 1];
        const int n = juce::jmax (1, (int) (p1.getDistanceFrom (p2) / step));
        for (int j = 0; j < n; ++j)
        {
            const float t = (float) j / (float) n, t2 = t * t, t3 = t2 * t;
            out.push_back ((p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f);
        }
    }
    out.push_back (p.back());
    return out;
}

// the rainbow trail. width: across all seven bands at its widest; fade: 0..1, how much of it (from
// its tail end) has faded away; time: for the flutter
inline void ribbon (Graphics& g, const std::vector<Point<float>>& pts, float width, float alpha, float fade, float time)
{
    const auto c = smoothLine (pts);
    const int n = (int) c.size();
    if (n < 3 || alpha <= 0.001f) return;
    std::vector<float> s ((size_t) n, 0.0f);
    for (int i = 1; i < n; ++i) s[(size_t) i] = s[(size_t) i - 1] + c[(size_t) i].getDistanceFrom (c[(size_t) i - 1]);
    const float L = juce::jmax (1.0f, s.back());
    std::vector<Point<float>> nrm ((size_t) n);
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        auto d = c[(size_t) juce::jmin (n - 1, i + 1)] - c[(size_t) juce::jmax (0, i - 1)];
        const float l = juce::jmax (1.0e-3f, d.getDistanceFromOrigin());
        nrm[(size_t) i] = { -d.y / l, d.x / l };
        const float u = s[(size_t) i] / L;
        // thin at the tail, full from a third of the way, a little rounder at the head
        w[(size_t) i] = width * (0.18f + 0.82f * smooth01 (u / 0.35f)) * (1.0f + 0.06f * std::sin (s[(size_t) i] * 0.045f - time * 7.0f));
    }
    // fades from the tail end: a gradient along the trail
    const float f0 = juce::jlimit (0.0f, 0.95f, fade);
    auto along = [&] (Colour col, float a)
    {
        const auto start = c.front(), end = c.back();
        juce::ColourGradient gr (col.withAlpha (0.0f), start, col.withAlpha (a), end, false);
        gr.addColour (juce::jlimit (0.01, 0.98, (double) f0 + 0.05), col.withAlpha (0.0f));
        gr.addColour (juce::jlimit (0.02, 0.99, (double) f0 + 0.35), col.withAlpha (a));
        return gr;
    };
    auto strip = [&] (float a0, float a1)   // the part of the ribbon between two fractions across it (-0.5..0.5)
    {
        Path p;
        for (int i = 0; i < n; ++i)
        {
            const float flutter = 1.5f * std::sin (s[(size_t) i] * 0.06f + time * 9.0f);
            const auto q = c[(size_t) i] + nrm[(size_t) i] * (a0 * w[(size_t) i] + flutter);
            if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        for (int i = n; --i >= 0;)
        {
            const float flutter = 1.5f * std::sin (s[(size_t) i] * 0.06f + time * 9.0f);
            p.lineTo (c[(size_t) i] + nrm[(size_t) i] * (a1 * w[(size_t) i] + flutter));
        }
        p.closeSubPath();
        return p;
    };
    // the glow, then the bands (red outside), then a shine
    g.setGradientFill (along (juce::Colours::white, 0.28f * alpha));
    g.fillPath (strip (-0.62f, 0.62f));
    for (int k = 0; k < 7; ++k)
    {
        const float a0 = -0.5f + (float) k / 7.0f, a1 = a0 + 1.0f / 7.0f + 0.004f;
        g.setGradientFill (along (band (k), 0.92f * alpha));
        g.fillPath (strip (a0, a1));
    }
    g.setGradientFill (along (juce::Colours::white, 0.45f * alpha));
    g.fillPath (strip (-0.36f, -0.3f));
    // a round head where it ends
    const auto h = c.back();
    for (int k = 0; k < 7; ++k)
    {
        const float r = w.back() * (0.5f - (float) k / 14.0f);
        g.setColour (band (k).withAlpha (0.92f * alpha));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (h));
    }
}

// four-pointed twinkle
inline void twinkle (Graphics& g, Point<float> at, float size, float alpha)
{
    if (alpha <= 0.01f || size <= 0.2f) return;
    Path st;
    st.addStar (at, 4, size * 0.22f, size);
    g.setColour (juce::Colours::white.withAlpha (alpha));
    g.fillPath (st);
}

// sparkles along a trail
inline void trailSparkles (Graphics& g, const std::vector<Point<float>>& pts, float alpha, float time, int seed)
{
    if (pts.size() < 2) return;
    juce::Random r (seed);
    for (int i = 0; i < 16; ++i)
    {
        const auto q = pts[(size_t) r.nextInt ((int) pts.size())] + Point<float> (r.nextFloat() * 36.0f - 18.0f, r.nextFloat() * 36.0f - 18.0f);
        const float tw = 0.5f + 0.5f * std::sin (time * (5.0f + 3.0f * r.nextFloat()) + (float) i * 1.7f);
        twinkle (g, q, 3.0f + 6.0f * tw, alpha * tw);
    }
}

// the flyer: dir is where it's heading (unit); u how far through the dive (0..1)
inline void comet (Graphics& g, Point<float> head, Point<float> dir, float u, float time, bool flyer = true)
{
    const auto side = Point<float> (-dir.y, dir.x);
    // the white teardrop glow behind it
    Path tear;
    const float len = 40.0f + 70.0f * u, wd = 14.0f;
    tear.startNewSubPath (head + dir * 12.0f);
    tear.cubicTo (head + dir * 12.0f + side * wd, head - dir * (len * 0.4f) + side * wd, head - dir * len);
    tear.cubicTo (head - dir * (len * 0.4f) - side * wd, head + dir * 12.0f - side * wd, head + dir * 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.85f), head, juce::Colours::white.withAlpha (0.0f), head - dir * len, false));
    g.fillPath (tear);
    // the sound barrier: a white cone forming round the nose, with jagged shock lines
    if (u > 0.5f)
    {
        const float c = (u - 0.5f) / 0.5f;
        Path cone;
        const auto nose = head + dir * 10.0f;
        cone.startNewSubPath (nose);
        cone.quadraticTo (head - dir * 20.0f + side * (34.0f - 10.0f * c), head - dir * (70.0f + 40.0f * c) + side * (40.0f - 8.0f * c));
        cone.lineTo (head - dir * (70.0f + 40.0f * c) - side * (40.0f - 8.0f * c));
        cone.quadraticTo (head - dir * 20.0f - side * (34.0f - 10.0f * c), nose);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.7f * c), nose, juce::Colours::white.withAlpha (0.0f), head - dir * 110.0f, false));
        g.fillPath (cone);
        juce::Random r ((int) (time * 30.0f));
        for (int k = 0; k < 2; ++k)   // zigzag shocks off the nose
        {
            const float sd = k == 0 ? 1.0f : -1.0f;
            Path z;
            auto q = nose + side * (sd * 6.0f);
            z.startNewSubPath (q);
            for (int j = 1; j <= 4; ++j)
            {
                q = nose - dir * (9.0f * (float) j) + side * (sd * (10.0f + 7.0f * (float) j + (j % 2 == 0 ? 6.0f : -2.0f) + r.nextFloat() * 3.0f));
                z.lineTo (q);
            }
            g.setColour (juce::Colours::white.withAlpha (0.9f * c));
            g.strokePath (z, juce::PathStrokeType (2.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
    }
    // the flyer itself (a flung balloon is its own)
    if (! flyer) return;
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (head));
    g.setColour (Colour (0xff5ec8ff));
    g.fillEllipse (juce::Rectangle<float> (15.0f, 15.0f).withCentre (head));
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (head + side * 2.5f - dir * 2.0f));
}

// the boom: a white starburst popping out and fading, with pow lines. t: seconds since the boom
inline void burst (Graphics& g, Point<float> at, float t, float scale = 1.0f)
{
    if (t < 0.0f || t > 0.7f) return;
    const float pop = t < 0.08f ? t / 0.08f * 1.25f : 1.25f - 0.25f * smooth01 ((t - 0.08f) / 0.12f);
    const float a = 1.0f - smooth01 ((t - 0.15f) / 0.55f);
    const float R = 70.0f * scale * pop, r = R * 0.5f;
    Path star;
    star.addStar (at, 12, r, R, t * 0.6f);
    g.setColour (juce::Colours::white.withAlpha (0.95f * a));
    g.fillPath (star);
    Path inner;
    inner.addStar (at, 12, r * 0.55f, R * 0.62f, t * 0.6f + 0.13f);
    g.setColour (Colour (0xfffff2a8).withAlpha (0.95f * a));
    g.fillPath (inner);
    for (int i = 0; i < 16; ++i)   // pow lines
    {
        const float ang = (float) i / 16.0f * juce::MathConstants<float>::twoPi + 0.1f;
        const Point<float> d (std::cos (ang), std::sin (ang));
        const float r0 = R * (1.1f + 0.9f * t / 0.7f), r1 = r0 + R * 0.5f * (i % 2 == 0 ? 1.0f : 0.6f);
        g.setColour (juce::Colours::white.withAlpha (0.8f * a));
        g.drawLine ({ at + d * r0, at + d * r1 }, 3.0f * scale);
    }
}

// the ring of seven: point (k, angle) says where band k's outer edge is at that angle (screen px);
// thick: each band's width there; alpha: 0..1
inline void donut (Graphics& g, std::function<Point<float> (int band, float angle, float inset)> point, float alpha, float time)
{
    if (alpha <= 0.01f) return;
    constexpr int N = 72;
    const float tw = juce::MathConstants<float>::twoPi;
    // a soft white glow under it all
    {
        Path p;
        for (int j = 0; j <= N; ++j) { const auto q = point (0, tw * (float) j / N, -0.6f); if (j == 0) p.startNewSubPath (q); else p.lineTo (q); }
        for (int j = N; j >= 0; --j) p.lineTo (point (6, tw * (float) j / N, 1.6f));
        p.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.18f * alpha));
        g.fillPath (p);
    }
    for (int k = 0; k < 7; ++k)
    {
        Path p;
        for (int j = 0; j <= N; ++j) { const auto q = point (k, tw * (float) j / N, 0.0f); if (j == 0) p.startNewSubPath (q); else p.lineTo (q); }
        for (int j = N; j >= 0; --j) p.lineTo (point (k, tw * (float) j / N, 1.04f));
        p.closeSubPath();
        g.setColour (band (k).withAlpha (0.92f * alpha));
        g.fillPath (p);
    }
    // puffy cloud edges: round puffs along the outside (red) and the inside (violet), each with a shine
    for (int edge = 0; edge < 2; ++edge)
    {
        const int k = edge == 0 ? 0 : 6;
        const int puffs = edge == 0 ? 30 : 22;
        for (int i = 0; i < puffs; ++i)
        {
            const float ang = tw * ((float) i + 0.5f * (float) edge) / (float) puffs;
            const auto o = point (k, ang, edge == 0 ? 0.0f : 1.0f), m = point (k, ang, 0.5f);
            const float pr = juce::jmax (2.0f, o.getDistanceFrom (m) * (1.4f + 0.35f * std::sin ((float) i * 2.7f + time * 2.0f)));
            const auto c = o + (o - m) * (edge == 0 ? 0.25f : 0.25f);
            g.setColour (band (k).brighter (0.25f).withAlpha (0.9f * alpha));
            g.fillEllipse (juce::Rectangle<float> (2.0f * pr, 2.0f * pr).withCentre (c));
            g.setColour (juce::Colours::white.withAlpha (0.55f * alpha));
            g.fillEllipse (juce::Rectangle<float> (pr * 0.8f, pr * 0.55f).withCentre (c + Point<float> (-pr * 0.25f, -pr * 0.35f)));
        }
    }
    // a white shine round the middle band
    {
        Path p;
        for (int j = 0; j <= N; ++j) { const auto q = point (2, tw * (float) j / N, 0.3f); if (j == 0) p.startNewSubPath (q); else p.lineTo (q); }
        g.setColour (juce::Colours::white.withAlpha (0.3f * alpha));
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }
}

// stars thrown out from the boom, falling as they go. t: seconds since the boom; seed: the boom's number
inline void sparkles (Graphics& g, Point<float> at, float t, int seed, float scale = 1.0f)
{
    if (t < 0.0f || t > 2.2f) return;
    juce::Random r (seed * 7919 + 13);
    for (int i = 0; i < 26; ++i)
    {
        const float ang = r.nextFloat() * juce::MathConstants<float>::twoPi;
        const float sp = (180.0f + 380.0f * r.nextFloat()) * scale;
        const Point<float> v (std::cos (ang) * sp, std::sin (ang) * sp * 0.75f - 120.0f * scale);
        const float drag = (1.0f - std::exp (-t * 2.2f)) / 2.2f;
        const auto q = at + v * drag + Point<float> (0.0f, 260.0f * scale * t * t * 0.5f);
        const float life = 1.0f - smooth01 ((t - 0.6f - r.nextFloat() * 0.8f) / 0.6f);
        const float tw = 0.6f + 0.4f * std::sin (t * 14.0f + (float) i);
        const auto col = (i % 3 == 0) ? juce::Colours::white : band (i % 7);
        Path st;
        st.addStar (q, i % 2 == 0 ? 5 : 4, 2.0f * scale, (5.0f + 4.0f * tw) * scale, t * 3.0f + (float) i);
        g.setColour (col.withAlpha (life * 0.95f));
        g.fillPath (st);
    }
}
} // namespace rbart

#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;

//==============================================================================
const BalloonColours& balloonColours (int hue)
{
    // latex colours: fill is the base; line/shade/shine are only used by the little UI bits
    static const BalloonColours c[numHues] = {
        { Colour (0xff2f8fe0), Colour (0xff1b5c99), Colour (0xff2a7cc4), Colour (0xffd9eeff) },  // blue   : gate
        { Colour (0xffffcf2e), Colour (0xffb7860c), Colour (0xfff0b818), Colour (0xfffff6cf) },  // yellow : pitch
        { Colour (0xffff5d9e), Colour (0xffb02a62), Colour (0xffe84a88), Colour (0xffffdbe9) },  // pink   : the loop
        { Colour (0xff35cf92), Colour (0xff1d8a5e), Colour (0xff2bb47e), Colour (0xffd8fff0) },  // mint   : grains
        { Colour (0xff9a6cf0), Colour (0xff5d3aa8), Colour (0xff875ad8), Colour (0xffeee5ff) },  // lilac  : reverb
        { Colour (0xffff8a2a), Colour (0xffb5560c), Colour (0xffeb7616), Colour (0xffffe6d0) },  // orange
        { Colour (0xff1fc4c6), Colour (0xff12807f), Colour (0xff18adb0), Colour (0xffd4fbfb) },  // teal   : sound balloons
    };
    return c[jlimit (0, numHues - 1, hue)];
}

BalloonShape balloonShape (Rectangle<float> b, float inflate)
{
    const float fullR = jmin (b.getWidth(), b.getHeight()) * 0.38f;
    const float s = 0.68f + 0.32f * jlimit (0.0f, 1.0f, inflate);
    BalloonShape sh;
    sh.rx = fullR * 0.90f * s;
    sh.ry = fullR * 1.06f * s;
    sh.knot = jmax (3.0f, fullR * 0.16f);
    sh.cx = b.getCentreX();
    sh.cy = b.getCentreY() - fullR * 0.10f;
    sh.lw = jmax (1.5f, fullR * 0.07f);
    return sh;
}

//==============================================================================
SadLookAndFeel::SadLookAndFeel()
{
    setColour (Label::textColourId, palette::blush);
    setColour (ComboBox::textColourId, palette::blush);
    setColour (ComboBox::arrowColourId, palette::dustyPink);
    setColour (PopupMenu::backgroundColourId, Colour (0xf01a1f29));
    setColour (PopupMenu::textColourId, palette::blush.withAlpha (0.9f));
    setColour (PopupMenu::highlightedBackgroundColourId, palette::rose.withAlpha (0.55f));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
}

Font SadLookAndFeel::uiFont (float h, bool bold) { return Font (FontOptions ("Avenir Next", h, bold ? Font::bold : Font::plain)); }
Font SadLookAndFeel::titleFont (float h)         { return Font (FontOptions ("Snell Roundhand", h, Font::bold)); }
Font SadLookAndFeel::getComboBoxFont (ComboBox&) { return uiFont (12.0f); }
Font SadLookAndFeel::getPopupMenuFont()          { return uiFont (13.5f); }

PopupMenu::Options SadLookAndFeel::getOptionsForComboBoxPopupMenu (ComboBox& box, Label& label)
{
    auto o = LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label);
    if (auto* ed = box.findParentComponentOfClass<AudioProcessorEditor>())
        o = o.withParentComponent (ed);
    return o;
}

namespace
{
    inline float frac2 (float x) { return x - std::floor (x); }

    // a latex balloon's outline: round on top, tapering into the neck
    Path balloonBody (float cx, float cy, float rx, float ry)
    {
        Path body;
        body.startNewSubPath (cx, cy - ry);
        body.cubicTo (cx + rx * 0.58f, cy - ry, cx + rx, cy - ry * 0.52f, cx + rx, cy - ry * 0.02f);
        body.cubicTo (cx + rx, cy + ry * 0.48f, cx + rx * 0.42f, cy + ry * 0.90f, cx + rx * 0.08f, cy + ry * 0.99f);
        body.lineTo (cx - rx * 0.08f, cy + ry * 0.99f);
        body.cubicTo (cx - rx * 0.42f, cy + ry * 0.90f, cx - rx, cy + ry * 0.48f, cx - rx, cy - ry * 0.02f);
        body.cubicTo (cx - rx, cy - ry * 0.52f, cx - rx * 0.58f, cy - ry, cx, cy - ry);
        body.closeSubPath();
        return body;
    }

    Colour printInk (Colour base) { return base.getPerceivedBrightness() > 0.72f ? base.darker (0.9f) : Colours::white; }
}

// A realistic latex balloon: lit from the upper left, denser (darker) toward the rim, a glow
// where light comes through the rubber, a window-shaped reflection, a hotspot and a faint
// rim light, a stretched neck and tied knot. Names are printed on it like party balloons.
void SadLookAndFeel::drawBalloon (Graphics& g, const BalloonShape& sh, int hue, const String& label,
                                  const String& value, bool hot, float wrinkles, float wet, float murk, float labelSize) const
{
    const auto base = balloonColours (hue).fill;
    const float cx = sh.cx, cy = sh.cy, rx = sh.rx, ry = sh.ry, k = sh.knot;
    const Path body = balloonBody (cx, cy, rx, ry);

    if (hot)
        DropShadow (base.withAlpha (0.55f), (int) (rx * 0.9f), {}).drawForPath (g, body);

    // neck and knot
    {
        Path neck;
        neck.startNewSubPath (cx - rx * 0.09f, cy + ry * 0.97f);
        neck.quadraticTo (cx - rx * 0.02f, cy + ry + k * 0.25f, cx - rx * 0.06f, cy + ry + k * 0.6f);
        neck.lineTo (cx + rx * 0.06f, cy + ry + k * 0.6f);
        neck.quadraticTo (cx + rx * 0.02f, cy + ry + k * 0.25f, cx + rx * 0.09f, cy + ry * 0.97f);
        neck.closeSubPath();
        g.setColour (base.darker (0.45f));
        g.fillPath (neck);
        auto kr = Rectangle<float> (jmax (4.5f, rx * 0.26f), jmax (3.0f, rx * 0.15f)).withCentre ({ cx, cy + ry + k * 0.72f });
        g.setGradientFill (ColourGradient (base.brighter (0.25f), kr.getCentreX(), kr.getY(), base.darker (0.7f), kr.getCentreX(), kr.getBottom(), false));
        g.fillEllipse (kr);
        g.setColour (Colours::white.withAlpha (0.45f));
        g.fillEllipse (kr.withSizeKeepingCentre (kr.getWidth() * 0.4f, kr.getHeight() * 0.3f).translated (-kr.getWidth() * 0.15f, -kr.getHeight() * 0.18f));
    }

    // the latex
    {
        ColourGradient lit (base.brighter (0.38f), cx - rx * 0.35f, cy - ry * 0.40f, base.darker (0.85f), cx + rx * 1.05f, cy + ry * 0.95f, true);
        lit.addColour (0.42, base);
        lit.addColour (0.80, base.darker (0.35f));
        g.setGradientFill (lit);
        g.setOpacity (0.95f);
        g.fillPath (body);
        g.setOpacity (1.0f);
    }

    {
        Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (body);

        // light passing through the rubber, low in the body
        g.setGradientFill (ColourGradient (base.brighter (0.7f).withAlpha (0.45f), cx + rx * 0.12f, cy + ry * 0.55f,
                                           base.withAlpha (0.0f), cx + rx * 0.12f + rx * 0.85f, cy + ry * 0.55f, true));
        g.fillPath (body);

        // the rim is latex seen edge-on: denser and darker
        g.setColour (base.darker (1.0f).withAlpha (0.38f));
        g.strokePath (body, PathStrokeType (rx * 0.18f));

        // muffled by the lowpass (sound balloons): the colour sinks into the dark
        if (murk > 0.0f)
        {
            g.setColour (Colour (0xff0a0f1c).withAlpha (0.62f * jmin (1.0f, murk)));
            g.fillPath (body);
        }

        // soft creases when it's limp
        if (wrinkles > 0.0f)
        {
            const float a = jmin (1.0f, wrinkles);
            for (int i = -1; i <= 1; ++i)
            {
                Path w;
                const float x0 = cx + (float) i * rx * 0.42f;
                w.startNewSubPath (x0, cy + ry * 0.30f);
                w.quadraticTo (x0 + rx * 0.12f, cy + ry * 0.55f, x0 - (float) i * rx * 0.10f, cy + ry * 0.88f);
                g.setColour (base.darker (0.9f).withAlpha (0.35f * a));
                g.strokePath (w, PathStrokeType (rx * 0.07f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (Colours::white.withAlpha (0.28f * a));
                g.strokePath (w, PathStrokeType (rx * 0.03f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (-rx * 0.04f, 0.0f));
            }
        }

        // soaked: darker skin, and drops sitting on it like little lenses
        if (wet > 0.01f)
        {
            g.setColour (Colours::black.withAlpha (0.14f * wet));
            g.fillPath (body);
            static const float beads[][3] = { { -0.35f, -0.30f, 0.11f }, { 0.30f, -0.45f, 0.08f }, { 0.42f, 0.10f, 0.10f },
                                              { -0.10f, 0.35f, 0.09f }, { -0.48f, 0.18f, 0.07f }, { 0.12f, -0.05f, 0.07f },
                                              { 0.05f, 0.62f, 0.08f }, { -0.25f, 0.05f, 0.06f }, { 0.55f, -0.15f, 0.06f } };
            const int n = jlimit (0, 9, (int) std::ceil (wet * 9.0f));
            for (int i = 0; i < n; ++i)
            {
                const float bx = cx + beads[i][0] * rx, by = cy + beads[i][1] * ry, br = beads[i][2] * rx;
                auto d = Rectangle<float> (2 * br, 2.3f * br).withCentre ({ bx, by });
                g.setGradientFill (ColourGradient (base.darker (0.6f).withAlpha (0.55f * wet), d.getCentreX(), d.getY(),
                                                   base.brighter (0.6f).withAlpha (0.55f * wet), d.getCentreX(), d.getBottom(), false));
                g.fillEllipse (d);
                g.setColour (Colours::white.withAlpha (0.95f * wet));
                g.fillEllipse (Rectangle<float> (br * 0.5f, br * 0.45f).withCentre ({ bx - br * 0.35f, by - br * 0.5f }));
                g.setColour (Colours::white.withAlpha (0.35f * wet));
                g.fillEllipse (Rectangle<float> (br * 0.7f, br * 0.35f).withCentre ({ bx + br * 0.15f, by + br * 0.65f }));
            }
        }

        // reflections: a broad sheen, a window-shaped highlight, a hotspot, a faint rim light
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.20f), cx - rx * 0.30f, cy - ry * 0.30f,
                                           Colours::white.withAlpha (0.0f), cx + rx * 0.45f, cy + ry * 0.35f, true));
        g.fillPath (body);

        Path win;
        win.addRoundedRectangle (-rx * 0.21f, -ry * 0.13f, rx * 0.42f, ry * 0.26f, rx * 0.10f);
        win.applyTransform (AffineTransform::rotation (-0.6f).translated (cx - rx * 0.42f, cy - ry * 0.44f));
        const auto wb = win.getBounds();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.88f), wb.getX(), wb.getY(),
                                           Colours::white.withAlpha (0.12f), wb.getRight(), wb.getBottom(), false));
        g.fillPath (win);
        g.setColour (Colours::white.withAlpha (0.95f));
        g.fillEllipse (Rectangle<float> (rx * 0.11f, rx * 0.09f).withCentre ({ cx - rx * 0.10f, cy - ry * 0.64f }));

        Path rim;
        rim.addCentredArc (cx, cy, rx * 0.84f, ry * 0.84f, 0.0f, MathConstants<float>::pi * 0.58f, MathConstants<float>::pi * 0.92f, true);
        g.setColour (Colours::white.withAlpha (0.22f));
        g.strokePath (rim, PathStrokeType (rx * 0.07f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // printed on it
    {
        const auto ink = printInk (base);
        StringArray lines;
        lines.addTokens (label, "\n", {});
        const float lh = labelSize + 0.5f;
        const float total = (float) lines.size() * lh + (value.isNotEmpty() ? 12.0f : 0.0f);
        float y = cy + ry * 0.06f - total * 0.5f;
        g.setFont (uiFont (labelSize, true));
        for (auto& l : lines)
        {
            const auto r = Rectangle<float> (cx - rx - 10.0f, y, 2.0f * rx + 20.0f, lh);
            g.setColour (Colours::black.withAlpha (ink == Colours::white ? 0.30f : 0.0f));
            g.drawText (l, r.translated (0.0f, 0.8f), Justification::centred);
            g.setColour (ink.withAlpha (0.88f));
            g.drawText (l, r, Justification::centred);
            y += lh;
        }
        if (value.isNotEmpty())
        {
            g.setFont (uiFont (10.0f, true));
            g.setColour (ink.withAlpha (0.8f));
            g.drawText (value, Rectangle<float> (cx - rx - 14.0f, y + 1.0f, 2.0f * rx + 28.0f, 11.0f), Justification::centred);
        }
    }
}

// frost: a pale icy glaze, a white rim that thickens, and a few ice crystals spreading across
void SadLookAndFeel::drawFrost (Graphics& g, const BalloonShape& sh, float frost, float seed) const
{
    const auto body = Rectangle<float> (sh.rx * 2.0f, sh.ry * 2.0f).withCentre ({ sh.cx, sh.cy });
    Path shape;
    shape.addEllipse (body);
    g.setGradientFill (ColourGradient (Colour (0xffeef8ff).withAlpha (0.55f * frost), sh.cx, sh.cy + sh.ry,
                                       Colour (0xffcfe8ff).withAlpha (0.18f * frost), sh.cx, sh.cy - sh.ry, false));
    g.fillPath (shape);
    g.setColour (Colours::white.withAlpha (0.75f * frost));
    g.strokePath (shape, PathStrokeType (0.8f + 2.2f * frost));
    Random r ((int64) (seed * 1000.0f));
    const int crystals = 2 + (int) (5.0f * frost);
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (shape);
    g.setColour (Colours::white.withAlpha (0.8f * frost));
    for (int i = 0; i < crystals; ++i)
    {
        const float ang = r.nextFloat() * MathConstants<float>::twoPi;
        const Point<float> c (sh.cx + std::cos (ang) * sh.rx * 0.95f, sh.cy + std::sin (ang) * sh.ry * 0.95f);   // creeping in from the edge
        const float len = (5.0f + 9.0f * r.nextFloat()) * frost;
        for (int arm = 0; arm < 3; ++arm)
        {
            const float a = (float) arm * MathConstants<float>::pi / 3.0f + r.nextFloat() * 0.3f;
            g.drawLine (c.x - std::cos (a) * len, c.y - std::sin (a) * len, c.x + std::cos (a) * len, c.y + std::sin (a) * len, 0.9f);
        }
    }
}

void SadLookAndFeel::drawTinyBalloon (Graphics& g, Point<float> anchor, float r, int hue) const
{
    const auto base = balloonColours (hue).fill;
    const float cx = anchor.x, cy = anchor.y - r * 2.2f;
    Path str;
    str.startNewSubPath (cx, cy + r * 1.06f);
    str.quadraticTo (cx + 2.0f, cy + r * 1.6f, cx, anchor.y);
    g.setColour (Colour (0xffcfc6d6).withAlpha (0.7f));
    g.strokePath (str, PathStrokeType (1.0f));
    const Path body = balloonBody (cx, cy, r * 0.9f, r * 1.06f);
    g.setGradientFill (ColourGradient (base.brighter (0.4f), cx - r * 0.3f, cy - r * 0.4f, base.darker (0.6f), cx + r, cy + r, true));
    g.fillPath (body);
    g.setColour (Colours::white.withAlpha (0.8f));
    g.fillEllipse (cx - r * 0.45f, cy - r * 0.6f, r * 0.3f, r * 0.22f);
}

void SadLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (palette::ink.withAlpha (0.8f));
    g.fillRoundedRectangle (r, h * 0.5f);
    g.setColour (palette::dustyPink.withAlpha (box.isMouseOver() ? 0.7f : 0.4f));
    g.drawRoundedRectangle (r, h * 0.5f, 1.2f);
    drawTinyBalloon (g, { (float) w - (float) h * 0.6f, (float) h - 2.0f }, (float) h * 0.2f, balloonBlue);
}

void SadLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (10, 1, box.getWidth() - box.getHeight() - 10, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
// Floater: anything on a string
Floater::Floater (FreakiePieEditor& e, float len, int anchorIndex)
    : anchor (anchorIndex), stringLength (len), owner (e)
{
    setSize (S, S);
}

float Floater::collisionRadius() const noexcept
{
    const auto sh = balloonShape ({ 0, 0, (float) S, (float) S }, inflation());
    return jmax (sh.rx, sh.ry * 0.9f) * 1.06f;   // a little personal space so names stay readable
}

Point<float> Floater::knotWorld() const noexcept
{
    const auto sh = balloonShape ({ 0, 0, (float) S, (float) S }, inflation());
    const float d = (sh.knotBottom().y - (float) S * 0.5f) * scale;
    return pos + Point<float> (-d * std::sin (angle), d * std::cos (angle));
}

void Floater::place()
{
    setTransform (AffineTransform::rotation (angle, S * 0.5f, S * 0.5f).scaled (scale, scale, S * 0.5f, S * 0.5f)
                      .translated (pos.x - S * 0.5f, pos.y - S * 0.5f));
}

bool Floater::hitTest (int x, int y)
{
    const auto sh = balloonShape (getLocalBounds().toFloat(), inflation());
    const float dx = ((float) x - sh.cx) / (sh.rx + 3.0f), dy = ((float) y - sh.cy) / (sh.ry + 6.0f);
    return dx * dx + dy * dy <= 1.0f;
}

void Floater::beginMove (const MouseEvent& e)
{
    downPos = owner.toContent (e);
    moved = false;
    dragging = true;
    grabOffset = pos - downPos;
    dragTarget = pos;
    dragDepth = owner.toView (w).z;   // it moves across the view at the depth you picked it up at
    toFront (false);
}

void Floater::dragMove (const MouseEvent& e)
{
    const auto p = owner.toContent (e);
    if (p.getDistanceFrom (downPos) > 3.0f) moved = true;
    dragTarget = p + grabOffset;
}

void Floater::applySquashAndStretch (Graphics& g, const BalloonShape& sh) const
{
    const float sc = 1.0f + 0.05f * dance;
    g.addTransform (AffineTransform::scale ((1.0f + squash) * sc * (1.0f - 0.035f * stretch),
                                            (1.0f - squash) * sc * (1.0f + 0.07f * stretch), sh.cx, sh.cy));
}

//==============================================================================
Balloon::Balloon (FreakiePieEditor& e, RangedAudioParameter& p, const String& l, int h, float len, int anchorIndex)
    : Floater (e, len, anchorIndex), hue (h), label (l), param (p),
      attachment (p, [this] (float v) { value = v; repaint(); }, nullptr)
{
    isToggle = dynamic_cast<AudioParameterBool*> (&p) != nullptr;
    isChoice = dynamic_cast<AudioParameterChoice*> (&p) != nullptr;
    isStepped = dynamic_cast<AudioParameterInt*> (&p) != nullptr;
    phase = Random (l.hashCode64()).nextFloat() * 6.2832f;
    attachment.sendInitialUpdate();
}

float Balloon::inflation() const noexcept
{
    if (fill >= 0.0f) return jmax (fill, value > 0.5f ? 0.2f : 0.0f);
    if (isChoice || isStepped) return 0.45f + 0.55f * param.convertTo0to1 (value);   // always nicely full; grows a little per step
    return isToggle ? (value > 0.5f ? 1.0f : 0.0f) : param.convertTo0to1 (value);
}

void Balloon::paint (Graphics& g)
{
    const float inf = inflation();
    const auto sh = balloonShape (getLocalBounds().toFloat(), inf);
    const bool hot = isMouseOverOrDragging() || mode == Mode::adjust;
    applySquashAndStretch (g, sh);
    String valueText, text = label;
    if (isChoice || isStepped)
    {
        text = label + "\n" + param.getCurrentValueAsText();   // wears its current option / step
        if (hot) valueText = isChoice ? "click: next" : "scroll or click";
    }
    else if (hot || drifting) valueText = isToggle ? String (value > 0.5f ? "on" : "off") : param.getCurrentValueAsText();   // a cloud changing it shows it too
    owner.lnf.drawBalloon (g, sh, hue, text, valueText, hot, isToggle ? 1.0f - inf / 0.45f : 0.0f, wet, 0.55f * faded);
    if (frost > 0.01f) owner.lnf.drawFrost (g, sh, frost, phase);
}

void Balloon::stepChoice (int direction)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (&param))
    {
        const int n = c->choices.size();
        const int idx = ((roundToInt (value) + direction) % n + n) % n;
        attachment.setValueAsCompleteGesture ((float) idx);
        squashVel += 3.0f;
        owner.confetti (pos, 6);
    }
    else if (auto* ip = dynamic_cast<AudioParameterInt*> (&param))
    {
        // whole steps: scrolling stops at the ends, clicking wraps round
        const auto r = ip->getRange();
        int v = roundToInt (value) + direction;
        if (v > r.getEnd())   v = (direction == 1 && wrapSteps) ? r.getStart() : r.getEnd();
        if (v < r.getStart()) v = r.getStart();
        attachment.setValueAsCompleteGesture ((float) v);
        squashVel += 3.0f;
        owner.confetti (pos, 6);
    }
}

void Balloon::setNorm (float n)
{
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, n)));
}

void Balloon::mouseDown (const MouseEvent& e)
{
    stopDrift();   // your hand wins over the cloud
    if (! isToggle && ! isChoice && (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isPopupMenu()))
    {
        mode = Mode::adjust;
        downPos = owner.toContent (e);
        downNorm = param.convertTo0to1 (value);
        attachment.beginGesture();
    }
    else
    {
        mode = Mode::move;
        beginMove (e);
    }
    repaint();
}

void Balloon::mouseDrag (const MouseEvent& e)
{
    if (mode == Mode::move) dragMove (e);
    else if (mode == Mode::adjust)
    {
        const auto p = owner.toContent (e);
        setNorm (downNorm - (p.y - downPos.y) / (e.mods.isAltDown() ? 700.0f : 220.0f));
    }
}

void Balloon::mouseUp (const MouseEvent&)
{
    if (mode == Mode::adjust)
        attachment.endGesture();
    else if (mode == Mode::move)
    {
        dragging = false;
        if (! moved)
        {
            if (isToggle)
            {
                const bool on = value < 0.5f;
                attachment.setValueAsCompleteGesture (on ? 1.0f : 0.0f);
                if (on) owner.confetti (pos, 46);
            }
            else if (isChoice) stepChoice (1);
            else if (isStepped) { wrapSteps = true; stepChoice (1); wrapSteps = false; }
            squashVel += 4.0f;   // boop
        }
    }
    mode = Mode::none;
    repaint();
}

void Balloon::mouseDoubleClick (const MouseEvent&)
{
    if (isToggle || isChoice) return;   // (stepped balloons reset to their default like any other)
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    owner.confetti (pos, 16);
    squashVel += 3.0f;
}

void Balloon::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (isToggle) return;
    if (isChoice || isStepped)
    {
        int dir = 0;
        if (FreakiePieEditor::wheelStep (wheelAcc, w, dir)) stepChoice (dir);
        return;
    }
    float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (w.isReversed) d = -d;
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, param.convertTo0to1 (value) + d * 0.35f)));
    squashVel += jlimit (-1.5f, 1.5f, d * 6.0f);
}

// Inside a cloud: a continuous control wanders slowly towards a random value that changes every
// few seconds (at most ~7 % of its range a second); a choice or a whole-number control takes a
// random step now and then. Switches stay as they are. It's one long gesture for the host.
void Balloon::cloudDrift (bool inside, float dt, Random& r)
{
    const bool can = inside && ! isToggle && ! dragging && mode == Mode::none && alive;
    const bool steps = isChoice || isStepped;
    if (can && ! drifting)
    {
        drifting = true;
        driftTarget = r.nextFloat();
        driftClock = steps ? 3.0f + 4.0f * r.nextFloat() : 2.0f + 3.0f * r.nextFloat();
        if (! steps) attachment.beginGesture();
    }
    else if (! can) { stopDrift(); return; }

    driftClock -= dt;
    if (steps)
    {
        if (driftClock <= 0.0f) { driftClock = 4.0f + 6.0f * r.nextFloat(); stepChoice (r.nextBool() ? 1 : -1); }
        return;
    }
    if (driftClock <= 0.0f) { driftClock = 2.0f + 3.0f * r.nextFloat(); driftTarget = r.nextFloat(); }
    const float n = param.convertTo0to1 (value);
    const float lim = 0.07f * dt;
    const float next = jlimit (0.0f, 1.0f, n + jlimit (-lim, lim, (driftTarget - n) * 0.6f * dt));
    if (std::abs (next - n) > 1.0e-6f) attachment.setValueAsPartOfGesture (param.convertFrom0to1 (next));
}

void Balloon::stopDrift()
{
    if (! drifting) return;
    drifting = false;
    if (! isChoice && ! isStepped) attachment.endGesture();
}

//==============================================================================
SoundBalloon::SoundBalloon (FreakiePieEditor& e, int s, float sz, float st, int h)
    : Floater (e, 150.0f + Random().nextFloat() * 150.0f, 1), slot (s), hue (h), size (sz), semis (st)
{
    phase = Random().nextFloat() * 6.2832f;
}

void SoundBalloon::paint (Graphics& g)
{
    const auto sh = balloonShape (getLocalBounds().toFloat(), size);
    const bool hot = isMouseOverOrDragging();
    applySquashAndStretch (g, sh);
    const float murk = (1.0f - std::pow (size, 0.85f)) * 0.85f;   // the cutoff it was frozen at
    owner.lnf.drawBalloon (g, sh, hue, FreakiePieEditor::semisText (semis), hot ? "click to retune" : "",
                           hot, 0.0f, wet, murk, 15.0f);
    if (frost > 0.01f) owner.lnf.drawFrost (g, sh, frost, phase);

    // the sound inside: a little trace that moves with what it's playing
    const float lvl = jlimit (0.0f, 1.0f, owner.proc.balloonFX.voice[slot].level.load() * 3.0f);
    Path wave;
    const float w = sh.rx * 1.1f, y0 = sh.cy + sh.ry * 0.42f, t = owner.lnf.time;
    for (int i = 0; i <= 24; ++i)
    {
        const float u = (float) i / 24.0f;
        const float y = y0 + std::sin (u * 14.0f + t * 9.0f + phase) * sh.ry * 0.12f * lvl * std::sin (u * MathConstants<float>::pi);
        if (i == 0) wave.startNewSubPath (sh.cx - w * 0.5f, y); else wave.lineTo (sh.cx - w * 0.5f + u * w, y);
    }
    g.setColour (Colours::white.withAlpha (0.25f + 0.5f * lvl));
    g.strokePath (wave, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
}

void SoundBalloon::mouseUp (const MouseEvent&)
{
    dragging = false;
    if (! moved)
    {
        squashVel += 4.0f;
        owner.showPitchBubble (this);   // type an exact interval
    }
}

void SoundBalloon::mouseDoubleClick (const MouseEvent&)
{
    owner.proc.balloonFX.voice[slot].mode = 0;
    owner.pop (pos, size, hue);
    alive = false;
    setVisible (false);
}

void SoundBalloon::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    int dir = 0;
    if (! FreakiePieEditor::wheelStep (wheelAcc, w, dir)) return;
    semis = owner.stepSemis (semis, dir);
    owner.proc.balloonFX.voice[slot].semis = semis;
    squashVel += dir > 0 ? 1.5f : -1.5f;
    repaint();
}

//==============================================================================
FreakiePieEditor::FreakiePieEditor (FreakiePieProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    addAndMakeVisible (overlay);   // (the chalkboard and the library go over even this)

    // four bows on the floor, each holding its own bunch
    const float floorY = (float) designH - 30.0f;
    anchors[0] = { { 600.0f, floorY }, {}, {}, {}, false, balloonPink };    // the loop (main feedback), on the floor
    anchors[1] = { { 860.0f, floorY }, {}, {}, {}, false, balloonTeal };    // caught sounds, on the floor
    // the effects, standing on the shelf
    anchors[2] = { { 200.0f, shelfY }, {}, {}, {}, false, balloonBlue };    // spectral smear
    anchors[5] = { { 350.0f, shelfY }, {}, {}, {}, false, balloonYellow };  // pitch
    anchors[3] = { { 500.0f, shelfY }, {}, {}, {}, false, balloonMint };    // grains
    anchors[6] = { { 650.0f, shelfY }, {}, {}, {}, false, balloonLilac };   // reverb
    anchors[4] = { { 800.0f, shelfY }, {}, {}, {}, false, balloonOrange };  // the gumball machine (stutter): bought at the store
    for (int i : { 2, 3, 4, 5, 6 }) anchors[i].onShelf = true;
    anchors[4].box = true;      // it comes in a gift box like the store's other effects
    anchors[4].hidden = true;   // (until you buy it)

    struct Def { const char* id; const char* label; int hue; float len; int anchor; };
    const Def defs[] = {
        // spectral smear (blue bow)
        { ids::sgMix,     "spectral\nsmear",  balloonBlue,   240, 2 },
        { ids::sgFreeze,  "spectral\nfreeze", balloonBlue,   110, 2 },
        { ids::sgTilt,    "spectral\ntilt",   balloonBlue,   175, 2 },
        // pitch, reverb, the loop (pink bow)
        { ids::psShift,   "shift",           balloonYellow, 280, 5 }, { ids::psFeedback, "pitch\nfeedback", balloonYellow, 220, 5 },
        { ids::psMix,     "pitch\nmix",      balloonYellow, 160, 5 }, { ids::rvDecay,   "decay",           balloonLilac,  250, 6 },
        { ids::rvMix,     "verb\nmix",       balloonLilac,  180, 6 }, { ids::masterFb,  "main\nfeedback",  balloonPink,   230, 0 },
        // grains (mint bow)
        { ids::grMix,     "grain\nmix",      balloonMint,   310, 3 }, { ids::grSize,    "grain\nsize",     balloonMint,   260, 3 },
        { ids::grDensity, "density",         balloonMint,   210, 3 }, { ids::grShape,   "shape",           balloonMint,   290, 3 },
        { ids::grPan,     "grain\nspread",     balloonMint,   170, 3 }, { ids::grReverse, "reverse",         balloonMint,   240, 3 },
        { ids::grOctave,  "octave",          balloonMint,   140, 3 }, { ids::grFade,    "freeze\nfade",    balloonMint,   190, 3 },
        { ids::grFreeze,  "freeze",          balloonMint,   110, 3 }, { ids::grRandOct, "random\noctave",  balloonMint,    90, 3 },
        { ids::grPos,     "position",        balloonMint,   330, 3 }, { ids::grPosRand, "position\nrandom", balloonMint,   270, 3 },
        // the caught sounds' own bow (teal): how loud the frozen balloons are
        { ids::grainsLevel, "frozen\nvolume", balloonTeal,  210, 1 },
        // the gumball machine (orange bow)
        { ids::stAmount,  "stutter",         balloonOrange, 230, 4 }, { ids::stBounce,  "bounce",          balloonOrange, 180, 4 },
        { ids::stSway,    "sway",            balloonOrange, 130, 4 }, { ids::stCutup,   "cut-up",          balloonOrange, 90, 4 },
    };
    int perBow[7] {};
    for (auto& d : defs) ++perBow[d.anchor];
    int seen[7] {};
    for (auto& d : defs)
        if (auto* prm = p.apvts.getParameter (d.id))
        {
            auto* b = new Balloon (*this, *prm, d.label, d.hue, d.len, d.anchor);
            floaters.add (b);
            if (String (d.id) == ids::grFreeze) freezeBalloon = b;
            if (String (d.id) == ids::sgFreeze) spectralFreezeBalloon = b;
            // the gumball machine's four balloons belong to the store: hidden until you buy it
            {
                const char* stIds[] = { ids::stAmount, ids::stBounce, ids::stSway, ids::stCutup };
                for (int k = 0; k < 4; ++k)
                    if (String (d.id) == stIds[k])
                    {
                        storeBalloons[stutterItem][k] = b;
                        b->alive = false;
                        b->bypassable = true;
                    }
            }
            // the effects' own controls dim while bypassed; the grain controls still shape the frozen balloons
            for (auto* fxId : { ids::sgMix, ids::sgFreeze, ids::sgTilt, ids::psShift, ids::psFeedback, ids::psMix, ids::rvDecay,
                                ids::rvMix, ids::masterFb, ids::grMix, ids::grFreeze })
                if (String (d.id) == fxId) b->bypassable = true;
            // each bunch fans out from its own bow
            const int k = seen[d.anchor]++;
            const int n = jmax (2, perBow[d.anchor]);
            b->lean = ((float) ((k * 5) % n) / (float) (n - 1)) * 1.4f - 0.7f;
            if (d.anchor == 2) b->lean = 0.1f + ((float) ((k * 5) % n) / (float) (n - 1)) * 0.9f;   // keep clear of the bubble
            // they all start bunched in their bow and float up when the window opens
            b->placed3d = false;
            b->pos = anchors[d.anchor].pos + Point<float> (rnd.nextFloat() * 80.0f - 40.0f, -20.0f - rnd.nextFloat() * 40.0f);
            b->vel = { rnd.nextFloat() * 160.0f - 80.0f, -rnd.nextFloat() * 120.0f };
            content.addChildComponent (b);
            b->setVisible (b->alive);
            b->place();
        }

    // the store's balloons: made now, hidden until you buy them
    {
        static const int hues[] = { balloonLilac, balloonOrange, balloonPink, balloonTeal, balloonYellow,
                                    balloonBlue, balloonMint, balloonOrange, balloonPink, balloonLilac, balloonTeal };
        for (int e = 0; e < fp::StoreFX::kNum; ++e)
        {
            anchors[firstBox + e] = { { 150.0f + 70.0f * (float) e, shelfY }, {}, {}, {}, false, hues[e], false, true, true, true };
            const auto& in = fp::StoreFX::infos()[(size_t) e];
            const String sn (in.shortName);
            const String labels[4] = { sn + "\n" + in.c1.name, sn + "\n" + in.c2.name, sn + "\nlevel", sn + "\nwidth" };
            const ids::StorePart parts[4] = { ids::storeA, ids::storeB, ids::storeLevel, ids::storeWidth };
            for (int k = 0; k < 4; ++k)
                if (auto* prm = p.apvts.getParameter (ids::store (e, parts[k])))
                {
                    // each effect's four balloons are tied to its own gift box
                    auto* b = new Balloon (*this, *prm, labels[k], hues[e], 70.0f + 30.0f * (float) k, firstBox + e);
                    b->lean = 0.35f * ((float) k - 1.5f) / 1.5f;   // fanned out over the box
                    b->alive = false;
                    b->bypassable = true;
                    b->setVisible (false);
                    floaters.add (b);
                    content.addChildComponent (b);
                    storeBalloons[e][k] = b;
                }
        }
    }
    numParamBalloons = floaters.size();
    syncStore();
    layoutShelf (shelfItems());
    for (auto& a : anchors) if (a.onShelf) a.shelfX = a.slotX;   // open with everything already in its place
    // the loop's bow and the caught sounds' bow stand out on the sphere's floor
    anchors[0].w = { -0.18f * sphereR, 0.10f * sphereR, floorZ };
    anchors[1].w = { -0.05f * sphereR, -0.30f * sphereR, floorZ };
    for (auto& a : anchors) { if (a.onShelf) a.w = ringPoint (a.shelfX); a.pos = project (a.w, &a.scale, &a.depth); }

    if (auto* r = p.apvts.getParameter (ids::rain))
    {
        rainAtt = std::make_unique<ParameterAttachment> (*r, [this] (float v) { rainValue = v; });
        rainAtt->sendInitialUpdate();
    }
    if (auto* hl = p.apvts.getParameter (ids::helium))
    {
        heliumAtt = std::make_unique<ParameterAttachment> (*hl, [this] (float v) { heliumValue = v; });
        heliumAtt->sendInitialUpdate();
    }
    if (auto* c = p.apvts.getParameter (ids::cloudy))
    {
        cloudyAtt = std::make_unique<ParameterAttachment> (*c, [this] (float v) { cloudyValue = v; });
        cloudyAtt->sendInitialUpdate();
    }
    if (auto* sn = p.apvts.getParameter (ids::snow))
    {
        snowAtt = std::make_unique<ParameterAttachment> (*sn, [this] (float v) { snowValue = v; });
        snowAtt->sendInitialUpdate();
    }
    if (auto* h = p.apvts.getParameter (ids::heat))
    {
        heatAtt = std::make_unique<ParameterAttachment> (*h, [this] (float v) { heatValue = v; });
        heatAtt->sendInitialUpdate();
    }
    if (auto* aw = p.apvts.getParameter (ids::autoWeather))
    {
        autoAtt = std::make_unique<ParameterAttachment> (*aw, [this] (float v) { autoValue = v; });
        autoAtt->sendInitialUpdate();
    }
    if (auto* bp = p.apvts.getParameter (ids::fxBypass))
    {
        bypassAtt = std::make_unique<ParameterAttachment> (*bp, [this] (float v) { bypassValue = v; });
        bypassAtt->sendInitialUpdate();
    }
    // the clouds, waiting off to the sides until the weather turns
    for (int i = 0; i < 4; ++i)
    {
        const float dir = i % 2 == 0 ? 1.0f : -1.0f;
        skyClouds.push_back ({ { 150.0f + 250.0f * (float) i, 150.0f + 52.0f * (float) ((i * 3) % 4) },
                               dir * (9.0f + 7.0f * rnd.nextFloat()), 1.45f + 0.5f * rnd.nextFloat(), rnd.nextFloat() * 6.2832f });
    }
    seenLightning = p.lightningCount.load();

    // the pumps: pop balloons on the left, sound balloons on the right
    popPump.area = { 28.0f, 380.0f, 96.0f, 220.0f };
    popPump.nozzle = { 92.0f, (float) designH - 80.0f };
    popPump.hue = balloonOrange;
    popPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
    soundPump.area = { 948.0f, 380.0f, 112.0f, 220.0f };
    soundPump.nozzle = { 1020.0f, (float) designH - 80.0f };
    soundPump.hue = balloonTeal;
    soundPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
    nextBadge = { 940.0f, (float) designH - 62.0f, 56.0f, 20.0f };
    cloudButton = { 394.0f, 4.0f, 96.0f, 62.0f };   // rain, next to cloudy in the weather row
    tankArea = { railRight, (float) designH - 102.0f, 46.0f, 76.0f };
    if (auto* th = p.apvts.getParameter (ids::grainsThru))
    {
        thruAtt = std::make_unique<ParameterAttachment> (*th, [this] (float v)
        {
            thruValue = v;
            if (! tankDragging) tankArea.setX (railRight - v * (railRight - railLeft));
        });
        thruAtt->sendInitialUpdate();
    }

    // the pin lives in a pincushion on the floor
    pin = std::make_unique<PinTool> (*this);
    pin->home = { 798.0f, (float) designH - 60.0f };
    pin->pos = pin->home;
    content.addAndMakeVisible (*pin);
    pin->place();

    // switches on the pumps
    pinkie = ImageCache::getFromMemory (BinaryData::pinkie_sitting_png, BinaryData::pinkie_sitting_pngSize);
    pinkieUmbrella = ImageCache::getFromMemory (BinaryData::pinkie_umbrella_png, BinaryData::pinkie_umbrella_pngSize);
    pinkieCower = ImageCache::getFromMemory (BinaryData::pinkie_cower_png, BinaryData::pinkie_cower_pngSize);
    pinkieScared = ImageCache::getFromMemory (BinaryData::pinkie_scared_png, BinaryData::pinkie_scared_pngSize);
    pinkieSleep = ImageCache::getFromMemory (BinaryData::pinkie_sleep_png, BinaryData::pinkie_sleep_pngSize);
    pinkieCrystal = ImageCache::getFromMemory (BinaryData::pinkie_crystal_png, BinaryData::pinkie_crystal_pngSize);
    pinkieSnow = ImageCache::getFromMemory (BinaryData::pinkie_snow_png, BinaryData::pinkie_snow_pngSize);
    pinkieCold = ImageCache::getFromMemory (BinaryData::pinkie_cold_png, BinaryData::pinkie_cold_pngSize);
    pinkieHeat = ImageCache::getFromMemory (BinaryData::pinkie_heat_png, BinaryData::pinkie_heat_pngSize);
    pinkieWet = ImageCache::getFromMemory (BinaryData::pinkie_wet_png, BinaryData::pinkie_wet_pngSize);

    // the chalkboard stands on the floor by the pin cushion
    chalkboard = std::make_unique<Chalkboard> (*this);
    chalkboard->teacher = ImageCache::getFromMemory (BinaryData::cheerilee_png, BinaryData::cheerilee_pngSize);
    addChildComponent (*chalkboard);   // on the editor itself, so nothing in the room (clouds, rain) draws over it
    library = std::make_unique<Library> (*this);
    library->room = ImageCache::getFromMemory (BinaryData::library_jpg, BinaryData::library_jpgSize);
    library->twilight = ImageCache::getFromMemory (BinaryData::twilight_reading_png, BinaryData::twilight_reading_pngSize);
    trixieImg = ImageCache::getFromMemory (BinaryData::trixie_png, BinaryData::trixie_pngSize);
    addChildComponent (*library);
    storeShelves = std::make_unique<StoreShelves> (*this);
    overlay.addChildComponent (*storeShelves);
    weatherBar = std::make_unique<WeatherBar> (*this);
    content.addAndMakeVisible (*weatherBar);
    orderRow = std::make_unique<OrderRow> (*this);
    overlay.addAndMakeVisible (*orderRow);
    presetStrip = std::make_unique<PresetStrip> (*this);
    overlay.addChildComponent (*presetStrip);
    fadePopup = std::make_unique<FadePopup> (*this);
    overlay.addChildComponent (*fadePopup);
    const float H = (float) designH;
    if (auto* ps = p.apvts.getParameter (ids::balloonSfx))
    {
        popsAtt = std::make_unique<ParameterAttachment> (*ps, [this] (float v) { popsValue = v; });
        popsAtt->sendInitialUpdate();
    }
    addPill (ids::catchFrom,  "catch",      { 912.0f, H - 116.0f }, balloonTeal,   iconCatch);
    pills.getLast()->phase = 2.3f;

    // the sphere's tokens, down the right: how the grains fly, how many copies, and what comes out
    addPill (ids::flight,      "flight",  { 846.0f, 160.0f }, balloonLilac,  iconFlight);
    addPill (ids::flightSpeed, "speed",   { 904.0f, 160.0f }, balloonLilac,  iconSpeed);
    addPill (ids::spread,      "distance",  { 846.0f, 236.0f }, balloonBlue,   iconSpread);
    addPill (ids::deviate,     "deviate", { 904.0f, 236.0f }, balloonOrange, iconDeviate);
    addPill (ids::grainMode,   "grains",  { 875.0f, 312.0f }, balloonMint,   iconDeviate);
    for (int i = 0; i < pills.size(); ++i) pills[i]->phase = 0.9f * (float) i;
    if (auto* gp = p.apvts.getParameter (ids::grains))
    {
        grainsAtt = std::make_unique<ParameterAttachment> (*gp, [this] (float v) { grainsValue = v; });
        grainsAtt->sendInitialUpdate();
    }
    for (int i = 0; i <= FreakiePieProcessor::kGrains; ++i)   // (the last one is the source: where your dry sound plays)
    {
        auto* m = new GrainMarker (*this, i);
        grainMarkers.add (m);
        content.addChildComponent (m);
    }
    meterStep();

    for (int i = 0; i < 60; ++i) dust.push_back ({ rnd.nextFloat() * designW, rnd.nextFloat() * designH });

    scaleBox.addItemList (StringArray ([] { StringArray a; for (auto& s : fp::getScales()) a.add (s.name); return a; }()), 1);
    scaleBox.setJustificationType (Justification::centredLeft);
    cardPanel = std::make_unique<CardPanel> (*this);
    overlay.addChildComponent (*cardPanel);
    cardPanel->addAndMakeVisible (scaleBox);
    scaleAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::psScale, scaleBox);

    // presets, grouped
    {
        String group;
        for (int i = 0; i < (int) presets::list().size(); ++i)
        {
            const auto& pr = presets::list()[(size_t) i];
            if (group != pr.group) { group = pr.group; if (i > 0) presetBox.addSectionHeading (group); }
            presetBox.addItem (pr.name, i + 1);
        }
        presetBox.setJustificationType (Justification::centredLeft);
        presetBox.setTextWhenNothingSelected ("presets");
        presetBox.setLookAndFeel (&storeLook);   // the same sweet-shop menu as the balloon store
        presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
        presetBox.onOpen = [this] { if (presetStrip != nullptr) { if (presetStrip->isOpen()) presetStrip->close(); else presetStrip->open(); } };
        cardPanel->addAndMakeVisible (presetBox);
    }
    placeCard();   // (it starts tucked away above the window)
    followPreset (0.0f);
    lastActivity = clock;

    setResizable (true, true);
    setResizeLimits ((int) (designW * 0.6), (int) (designH * 0.6), designW * 2, designH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / designH);
    setSize ((int) (designW * 0.9f), (int) (designH * 0.9f));

    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

FreakiePieEditor::~FreakiePieEditor()
{
    stopTimer();
    proc.saveLastState();
    // don't leave a balloon recording or the pump squeaking when the window closes
    proc.balloonFX.pumping = false;
    if (soundPump.slot >= 0) proc.balloonFX.voice[soundPump.slot].mode = 0;
    presetBox.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void FreakiePieEditor::paint (Graphics& g) { g.fillAll (palette::ink); }

void FreakiePieEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content.setBounds (0, 0, designW, designH);
    content.setTransform (AffineTransform::scale (s));
    overlay.setBounds (0, 0, designW, designH);
    overlay.setTransform (AffineTransform::scale (s));
    for (Component* c : { (Component*) chalkboard.get(), (Component*) library.get() })
        if (c != nullptr) c->setTransform (AffineTransform::scale (s));
}

//==============================================================================
String FreakiePieEditor::semisText (float semis)
{
    const int s = roundToInt (semis);
    return s == 0 ? String ("0") : (s > 0 ? "+" : String (CharPointer_UTF8 ("\xe2\x88\x92"))) + String (std::abs (s));
}

float FreakiePieEditor::stepSemis (float semis, int direction) const
{
    // the notes of the current scale, in semitones, across two octaves each way
    const auto& scales = fp::getScales();
    const auto& sc = scales[(size_t) jlimit (0, (int) scales.size() - 1, (int) proc.apvts.getRawParameterValue (ids::psScale)->load())];
    std::vector<int> notes;
    for (int o = -2; o <= 2; ++o)
        for (auto& d : sc.degrees) notes.push_back (o * 12 + roundToInt (d.cents / 100.0));
    notes.push_back (24);
    std::sort (notes.begin(), notes.end());
    const int cur = roundToInt (semis);
    if (direction > 0) { for (int n : notes) if (n > cur) return (float) n; return (float) notes.back(); }
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) if (*it < cur) return (float) *it;
    return (float) notes.front();
}

void FreakiePieEditor::confetti (Point<float> at, int count)
{
    static const uint32 cols[] = { 0xff7fd0f2, 0xfffde36a, 0xfff6a2c8, 0xffa6ecc8, 0xffcfb6f6, 0xffffffff };
    for (int i = 0; i < count; ++i)
    {
        const float a = rnd.nextFloat() * MathConstants<float>::twoPi;
        const float sp = 120.0f + rnd.nextFloat() * 320.0f;
        particles.push_back ({ at, { std::cos (a) * sp, std::sin (a) * sp - 180.0f },
                               rnd.nextFloat() * 6.28f, (rnd.nextFloat() - 0.5f) * 14.0f, 1.0f + rnd.nextFloat() * 0.6f,
                               Colour (cols[rnd.nextInt (6)]), rnd.nextFloat() < 0.3f ? 1 : 0 });
    }
}

void FreakiePieEditor::pop (Point<float> at, float size, int hue)
{
    proc.balloonFX.popSize = size;
    ++proc.balloonFX.popRequests;
    bursts.push_back ({ at, 0.0f, 0.6f + 0.6f * size, hue });
    if (pinkieHop <= 0.5f) pinkieHopVel = 150.0f + 120.0f * jmin (1.0f, size);   // startled
    scaredUntil = clock + 1.4;
    scaredAlt = ! scaredAlt;   // she cowers one time, stands frozen the next
    noteActivity();
    // bits of rubber fly off
    const auto c = balloonColours (hue).fill;
    for (int i = 0; i < 18; ++i)
    {
        const float a = rnd.nextFloat() * MathConstants<float>::twoPi;
        const float sp = 160.0f + rnd.nextFloat() * 380.0f * (0.5f + size);
        particles.push_back ({ at, { std::cos (a) * sp, std::sin (a) * sp - 120.0f }, rnd.nextFloat() * 6.28f,
                               (rnd.nextFloat() - 0.5f) * 20.0f, 0.9f + rnd.nextFloat() * 0.5f, c.withMultipliedBrightness (0.8f + 0.4f * rnd.nextFloat()), 2 });
    }
    confetti (at, 14);
    // the shock wave shoves the balloons nearby
    for (auto* f : floaters)
    {
        const auto d = f->pos - at;
        const float dist = d.getDistanceFromOrigin();
        if (dist < 1.0f || dist > 230.0f) continue;
        f->vel += d / dist * (420.0f * (1.0f - dist / 230.0f) * (0.5f + size));
        f->squashVel += 3.0f * (1.0f - dist / 230.0f);
    }
}

void FreakiePieEditor::addPill (const char* id, const String& name, Point<float> centre, int hue, int icon)
{
    auto* prm = proc.apvts.getParameter (id);
    if (prm == nullptr) return;
    auto* pl = pills.add (new Pill());
    pl->c = centre; pl->home = centre; pl->icon = icon; pl->param = prm; pl->name = name; pl->hue = hue;
    pl->r = Rectangle<float> (2.0f * pl->rad + 10.0f, 2.0f * pl->rad + 10.0f).withCentre (centre);   // the click target
    pl->continuous = dynamic_cast<AudioParameterBool*> (prm) == nullptr && dynamic_cast<AudioParameterChoice*> (prm) == nullptr;
    pl->att = std::make_unique<ParameterAttachment> (*prm, [pl] (float v) { pl->value = v; }, nullptr);
    pl->att->sendInitialUpdate();
}

void FreakiePieEditor::clickPill (Pill& pl, int direction)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (pl.param))
    {
        const int n = c->choices.size();
        pl.att->setValueAsCompleteGesture ((float) (((roundToInt (pl.value) + direction) % n + n) % n));
    }
    else
        pl.att->setValueAsCompleteGesture (pl.value > 0.5f ? 0.0f : 1.0f);
    pl.bounceVel += 7.0f;                 // boing
    confetti (pl.c, 6);
}

// One round, glossy toy token with an animated picture of what it controls, and a caption.
void FreakiePieEditor::drawToken (Graphics& g, const Pill& pl) const
{
    const bool isChoice = dynamic_cast<AudioParameterChoice*> (pl.param) != nullptr;
    const bool lit = isChoice || pl.continuous || pl.value > 0.5f;
    const float R = pl.rad, t = (float) clock;
    const auto c = pl.c;
    const auto base = lit ? balloonColours (pl.hue).fill : balloonColours (pl.hue).fill.withSaturation (0.2f).darker (0.5f);
    const bool hover = content.getMouseXYRelative().toFloat().getDistanceFrom (c) < R + 5.0f;
    const auto ink = Colours::white.withAlpha (lit ? 0.95f : 0.55f);

    // shadow, then the disc (it squashes when it boings)
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (R * 1.7f, R * 0.4f).withCentre ({ c.x, c.y + R + 2.0f }));
    Graphics::ScopedSaveState ss (g);
    g.addTransform (AffineTransform::scale (1.0f + pl.bounce, 1.0f - pl.bounce, c.x, c.y + R));
    const auto disc = Rectangle<float> (2.0f * R, 2.0f * R).withCentre (c);
    if (hover)
    {
        Path p;
        p.addEllipse (disc);
        DropShadow (base.withAlpha (0.7f), (int) (R * 0.8f), {}).drawForPath (g, p);
    }
    ColourGradient grad (base.brighter (0.45f), c.x - R * 0.35f, c.y - R * 0.4f, base.darker (0.7f), c.x + R, c.y + R, true);
    grad.addColour (0.5, base);
    g.setGradientFill (grad);
    g.fillEllipse (disc);
    g.setColour (base.darker (0.9f).withAlpha (0.5f));
    g.drawEllipse (disc.reduced (0.8f), 1.6f);

    switch (pl.icon)
    {
        case iconPops:   // a party blower: unrolled when on, curled up when off
        {
            g.setColour (Colour (0xff2b3247));
            g.fillRoundedRectangle (c.x - R * 0.72f, c.y - R * 0.12f, R * 0.32f, R * 0.24f, 2.0f);
            if (lit)
            {
                for (int i = 0; i < 5; ++i)
                {
                    g.setColour (i % 2 == 0 ? Colours::white : Colour (0xffffd65c));
                    g.fillRect (c.x - R * 0.40f + (float) i * R * 0.17f, c.y - R * 0.1f, R * 0.17f, R * 0.2f);
                }
                Path curl;
                curl.addCentredArc (c.x + R * 0.47f, c.y - R * 0.22f, R * 0.13f, R * 0.13f, 0.0f,
                                    MathConstants<float>::pi, MathConstants<float>::twoPi * 1.2f, true);
                g.setColour (Colours::white);
                g.strokePath (curl, PathStrokeType (R * 0.12f, PathStrokeType::curved, PathStrokeType::rounded));
                for (int i = 0; i < 3; ++i)      // toot!
                {
                    const float a = frac2 (t * 1.5f + (float) i * 0.33f);
                    g.setColour (Colours::white.withAlpha (0.6f * (1.0f - a)));
                    g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre ({ c.x + R * (0.55f + 0.3f * a), c.y + R * (0.2f - 0.4f * a) }));
                }
            }
            else
            {
                Path coil;
                for (int i = 0; i <= 60; ++i)
                {
                    const float th = (float) i / 60.0f * MathConstants<float>::twoPi * 2.2f;
                    const float rr = R * (0.06f + 0.05f * th);
                    const Point<float> q (c.x + R * 0.05f + rr * std::cos (th), c.y + rr * std::sin (th));
                    if (i == 0) coil.startNewSubPath (q); else coil.lineTo (q);
                }
                g.setColour (ink);
                g.strokePath (coil, PathStrokeType (R * 0.1f, PathStrokeType::curved, PathStrokeType::rounded));
            }
            break;
        }
        case iconTone:   // a music note (crossed out when off)
        {
            const float bob = lit ? 1.5f * std::sin (t * 3.0f) : 0.0f;
            Path head;
            head.addEllipse (-R * 0.21f, -R * 0.15f, R * 0.42f, R * 0.30f);
            head.applyTransform (AffineTransform::rotation (-0.45f).translated (c.x - R * 0.12f, c.y + R * 0.32f + bob));
            g.setColour (ink);
            g.fillPath (head);
            g.fillRect (c.x + R * 0.04f, c.y - R * 0.5f + bob, R * 0.1f, R * 0.82f);
            Path flag;
            flag.startNewSubPath (c.x + R * 0.14f, c.y - R * 0.5f + bob);
            flag.quadraticTo (c.x + R * 0.55f, c.y - R * 0.3f + bob, c.x + R * 0.35f, c.y + R * 0.02f + bob);
            g.strokePath (flag, PathStrokeType (R * 0.1f, PathStrokeType::curved, PathStrokeType::rounded));
            if (! lit)
            {
                g.setColour (Colour (0xffff5d7a));
                g.drawLine (c.x - R * 0.55f, c.y - R * 0.55f, c.x + R * 0.55f, c.y + R * 0.55f, R * 0.12f);
            }
            break;
        }
        case iconCatch:  // effects: a magic wand.  input: a microphone
        {
            g.setColour (ink);
            if (roundToInt (pl.value) == 0)
            {
                g.drawLine (c.x - R * 0.5f, c.y + R * 0.5f, c.x + R * 0.15f, c.y - R * 0.15f, R * 0.12f);
                Path star;
                star.addStar ({ c.x + R * 0.28f, c.y - R * 0.28f }, 5, R * 0.12f, R * 0.3f, t * 0.8f);
                g.setColour (Colour (0xffffd65c));
                g.fillPath (star);
                for (int i = 0; i < 3; ++i)
                {
                    const float tw = 0.5f + 0.5f * std::sin (t * 4.0f + (float) i * 2.1f);
                    Path sp;
                    sp.addStar ({ c.x + R * (0.55f - 0.25f * (float) i), c.y - R * (0.35f + 0.25f * (float) (i % 2)) }, 4, 0.8f, 3.5f * tw + 0.5f);
                    g.setColour (Colours::white.withAlpha (tw));
                    g.fillPath (sp);
                }
            }
            else
            {
                g.fillRoundedRectangle (Rectangle<float> (R * 0.42f, R * 0.66f).withCentre ({ c.x, c.y - R * 0.18f }), R * 0.2f);
                Path cradle;
                cradle.addCentredArc (c.x, c.y - R * 0.05f, R * 0.34f, R * 0.32f, 0.0f, MathConstants<float>::halfPi, MathConstants<float>::pi * 1.5f, true);
                g.strokePath (cradle, PathStrokeType (R * 0.08f));
                g.drawLine (c.x, c.y + R * 0.27f, c.x, c.y + R * 0.5f, R * 0.08f);
                g.drawLine (c.x - R * 0.22f, c.y + R * 0.52f, c.x + R * 0.22f, c.y + R * 0.52f, R * 0.08f);
            }
            break;
        }
        case iconFlight:   // a dot flying the path it's set to
        {
            const int m = roundToInt (pl.value);
            Path path;
            for (int i = 0; i <= 48; ++i)
            {
                const float u = (float) i / 48.0f * MathConstants<float>::twoPi;
                Point<float> q;
                if (m == 1) q = { std::cos (u), std::sin (u) * 0.55f };                                       // orbit
                else if (m == 2) q = { std::sin (u) * 0.9f, std::sin (2.0f * u) * 0.45f };                     // lorenz's butterfly
                else if (m == 3) q = { std::cos (u) * 0.8f + 0.15f * std::sin (5.0f * u), std::sin (u) * 0.4f }; // flock
                else if (m == 4) q = { std::sin (u * 3.0f) * 0.8f, std::cos (u * 5.0f) * 0.5f };              // random
                else q = { 0.0f, 0.0f };
                q = c + q * (R * 0.62f);
                if (i == 0) path.startNewSubPath (q); else path.lineTo (q);
            }
            g.setColour (ink.withAlpha (0.5f));
            if (m != 0) g.strokePath (path, PathStrokeType (1.2f));
            const float u = t * 1.4f;
            const auto dot = m == 0 ? c : path.getPointAlongPath (frac2 (u * 0.25f) * path.getLength());
            g.setColour (ink);
            g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (dot));
            break;
        }
        case iconSpeed:    // speed lines, faster as it's turned up
        {
            const float n = pl.param->convertTo0to1 (pl.value);
            g.setColour (ink);
            for (int i = 0; i < 3; ++i)
            {
                const float y = c.y - R * 0.3f + (float) i * R * 0.3f;
                const float x = c.x - R * 0.6f + frac2 (t * (0.3f + 2.5f * n) + (float) i * 0.3f) * R * 0.5f;
                g.drawLine (x, y, x + R * (0.25f + 0.5f * n), y, 1.6f);
            }
            break;
        }
        case iconSpread:   // rings growing out
        {
            const float n = pl.param->convertTo0to1 (pl.value);
            g.setColour (ink.withAlpha (0.8f));
            for (int i = 1; i <= 3; ++i) g.drawEllipse (Rectangle<float> (2.0f, 2.0f).withCentre (c).expanded (R * 0.22f * (float) i * (0.4f + 0.6f * n)), 1.0f);
            g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (c));
            break;
        }
        case iconDeviate:  // little dots wandering off
        {
            const float n = dynamic_cast<AudioParameterChoice*> (pl.param) != nullptr ? (pl.value > 0.5f ? 1.0f : 0.15f) : pl.param->convertTo0to1 (pl.value);
            g.setColour (ink);
            for (int i = 0; i < 5; ++i)
            {
                const float a = (float) i * 1.2566f + t * 0.5f;
                const float rr = R * (0.15f + 0.45f * n * (0.5f + 0.5f * std::sin (t * 1.3f + (float) i * 2.1f)));
                g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (c + Point<float> (std::cos (a) * rr, std::sin (a) * rr)));
            }
            break;
        }
        case iconWidth:    // two arrows apart
        {
            const float n = pl.param->convertTo0to1 (pl.value);
            const float d = R * (0.1f + 0.45f * n);
            g.setColour (ink);
            Path a;
            a.addArrow ({ c.x - 2.0f, c.y, c.x - d - 4.0f, c.y }, 1.6f, 6.0f, 5.0f);
            a.addArrow ({ c.x + 2.0f, c.y, c.x + d + 4.0f, c.y }, 1.6f, 6.0f, 5.0f);
            g.fillPath (a);
            break;
        }
        case iconOutput:   // headphones, two speakers, or the sphere's 16
        {
            const int m = roundToInt (pl.value);
            g.setColour (ink);
            if (m == 1 || (m == 0 && proc.outChannels.load() < 16))
            {
                Path hp;
                hp.addCentredArc (c.x, c.y + 2.0f, R * 0.45f, R * 0.45f, 0.0f, -1.6f, 1.6f, true);
                g.strokePath (hp, PathStrokeType (1.8f));
                g.fillRoundedRectangle (c.x - R * 0.55f, c.y, R * 0.22f, R * 0.4f, 2.0f);
                g.fillRoundedRectangle (c.x + R * 0.33f, c.y, R * 0.22f, R * 0.4f, 2.0f);
            }
            else if (m == 2)
            {
                g.fillRoundedRectangle (c.x - R * 0.6f, c.y - R * 0.3f, R * 0.4f, R * 0.6f, 2.0f);
                g.fillRoundedRectangle (c.x + R * 0.2f, c.y - R * 0.3f, R * 0.4f, R * 0.6f, 2.0f);
            }
            else
                for (int i = 0; i < 16; ++i)
                {
                    const float a = (float) i / 16.0f * MathConstants<float>::twoPi;
                    g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (c + Point<float> (std::cos (a), std::sin (a)) * (R * 0.55f)));
                }
            break;
        }
        default: break;
    }

    // gloss on top
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f), c.x, c.y - R, Colours::white.withAlpha (0.0f), c.x, c.y - R * 0.1f, false));
    g.fillEllipse (Rectangle<float> (R * 1.3f, R * 0.8f).withCentre ({ c.x - R * 0.1f, c.y - R * 0.5f }));

    // caption: the value (with the name while you hover)
    g.setFont (SadLookAndFeel::uiFont (9.5f, true));
    g.setColour (palette::blush.withAlpha (lit ? 0.9f : 0.5f));
    String cap = pl.param->getCurrentValueAsText();
    if (dynamic_cast<AudioParameterBool*> (pl.param) != nullptr) cap = pl.name + (pl.value > 0.5f ? " on" : " off");
    if (hover && ! cap.startsWith (pl.name)) cap = pl.name + ": " + cap;
    g.drawText (cap, Rectangle<float> (96.0f, 12.0f).withCentre ({ c.x, c.y + R + 9.0f }), Justification::centred);
}

//==============================================================================
// The tokens bob gently where they sit.
void FreakiePieEditor::pillStep (float)
{
    const float t = (float) clock;
    for (auto* pl : pills)
    {
        pl->c = pl->home + Point<float> (2.0f * std::sin (t * 0.7f + pl->phase), 3.0f * std::sin (t * 0.9f + pl->phase));
        pl->r = pl->r.withCentre (pl->c);
    }
}

// Right-click the pop sounds token: switch them on or off, or feed them into the effects (then the
// hiss and the pops go through the chain with your sound, instead of straight out).
void FreakiePieEditor::openPopsMenu()
{
    auto* on = proc.apvts.getParameter (ids::balloonSfx);
    auto* thru = proc.apvts.getParameter (ids::popsThru);
    if (on == nullptr || thru == nullptr) return;
    PopupMenu m;
    m.setLookAndFeel (&storeLook);
    storeLook.time = (float) clock;
    m.addSectionHeader ("Pop sounds");
    PopupMenu::Item a ("pop sounds on");
    a.itemID = 1; a.isTicked = on->getValue() > 0.5f; a.colour = balloonColours (balloonPink).fill;
    a.shortcutKeyDescription = "the air hiss and the pops";
    m.addItem (a);
    PopupMenu::Item b ("feed into the effects");
    b.itemID = 2; b.isTicked = thru->getValue() > 0.5f; b.colour = balloonColours (balloonLilac).fill;
    b.shortcutKeyDescription = "through the chain, not straight out";
    m.addItem (b);
    const auto area = popsButton;
    const auto screen = content.localAreaToGlobal (area.getSmallestIntegerContainer());
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (screen).withMinimumWidth (330).withParentComponent (this),
                     [safe = Component::SafePointer<FreakiePieEditor> (this), on, thru] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         auto* prm = r == 1 ? on : thru;
                         prm->beginChangeGesture();
                         prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                         prm->endChangeGesture();
                         safe->confetti (safe->popsButton.getCentre(), 8);
                     });
}

void FreakiePieEditor::drawPinkie (Graphics& g) const
{
    if (! pinkie.isValid()) return;
    const float baseY = (float) designH - 12.0f;
    const float t = (float) clock;
    const float breatheRate = 1.6f - 0.8f * sleepVis;   // slower, deeper breaths asleep
    const float breathe = 1.0f + (0.014f + 0.01f * sleepVis) * std::sin (t * breatheRate);
    const float squash = pinkieHop > 0.0f ? 1.0f + 0.0006f * pinkieHopVel : 1.0f;   // stretch going up, squash coming down
    const float sy = breathe * squash, sx = 1.0f / std::sqrt (sy);
    // a scared little shiver
    const float shiver = scaredVis * 1.6f * std::sin (t * 70.0f);

    // her shadow shrinks as she leaves the floor
    const float sh = jlimit (0.4f, 1.0f, 1.0f - pinkieHop / 120.0f);
    g.setColour (Colours::black.withAlpha (0.3f * sh));
    g.fillEllipse (Rectangle<float> ((92.0f + 40.0f * sleepVis) * sh, 9.0f * sh).withCentre ({ pinkieX + 4.0f + 14.0f * sleepVis, baseY - 1.0f }));

    auto drawAt = [&] (const Image& img, Rectangle<float> area, float alpha)
    {
        if (alpha <= 0.001f || ! img.isValid()) return;
        const auto tr = AffineTransform::scale (area.getWidth() / (float) img.getWidth(), area.getHeight() / (float) img.getHeight())
                            .translated (area.getX() - pinkieX, area.getY() - baseY)
                            .scaled (sx, sy)
                            .translated (pinkieX + shiver, baseY);
        g.setOpacity (alpha);
        g.drawImageTransformed (img, tr, false);
    };
    auto sized = [&] (const Image& img, float w, float dx)
    {
        const float h = img.isValid() ? w * (float) img.getHeight() / (float) img.getWidth() : 0.0f;
        return Rectangle<float> (pinkieX + dx - w * 0.5f, baseY - h - pinkieHop, w, h);
    };

    // which picture: scared beats asleep beats soaked (rain in a heatwave) beats the umbrella beats just sitting
    const float scared = scaredVis;
    const float asleep = sleepVis * (1.0f - scared);
    const float wet = wetVis * (1.0f - scared) * (1.0f - asleep);
    const float umbrella = umbrellaVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet);
    const float crystal = crystalVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet) * (1.0f - umbrella);
    // in the snow too long she catches a cold: the cold picture takes over from the snowy one
    const float snowAll = snowyVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet) * (1.0f - umbrella) * (1.0f - crystal);
    const float cold = snowAll * coldVis, snowy = snowAll - cold;
    const float melted = meltVis * (1.0f - scared) * (1.0f - asleep) * (1.0f - wet) * (1.0f - umbrella) * (1.0f - crystal) * (1.0f - snowAll);
    const float sitting = jmax (0.0f, 1.0f - scared - asleep - wet - umbrella - crystal - snowAll - melted);
    drawAt (pinkieWet, sized (pinkieWet, 150.0f, 8.0f), wet);
    drawAt (pinkieSnow, sized (pinkieSnow, 100.0f, 2.0f), snowy);
    drawAt (pinkieCold, sized (pinkieCold, 112.0f, 4.0f), cold);
    drawAt (pinkieHeat, sized (pinkieHeat, 142.0f, 6.0f), melted);
    drawAt (pinkie, sized (pinkie, 104.0f, 0.0f), sitting);
    if (crystal > 0.001f)
    {
        const auto area = sized (pinkieCrystal, 92.0f, 0.0f);
        drawAt (pinkieCrystal, area, crystal);
        // she glitters: little four-point stars winking on and off around her
        for (int i = 0; i < 9; ++i)
        {
            const float ph = t * (1.3f + 0.17f * (float) i) + (float) i * 2.1f;
            const float tw = jmax (0.0f, std::sin (ph));
            const Point<float> c (area.getX() + area.getWidth() * (0.1f + 0.8f * std::fmod ((float) i * 0.618f, 1.0f)),
                                  area.getY() + area.getHeight() * (0.08f + 0.84f * std::fmod ((float) i * 0.382f + 0.2f, 1.0f)));
            Path star;
            star.addStar (c, 4, 0.8f, 1.5f + 4.5f * tw, ph * 0.2f);
            g.setColour (Colours::white.withAlpha (crystal * tw * 0.9f));
            g.fillPath (star);
        }
    }
    drawAt (pinkieUmbrella, umbrellaImageArea(), umbrella);
    drawAt (pinkieSleep, sized (pinkieSleep, 136.0f, -6.0f), asleep);
    if (scaredAlt) drawAt (pinkieCower, sized (pinkieCower, 138.0f, 8.0f), scared);
    else           drawAt (pinkieScared, sized (pinkieScared, 104.0f, 0.0f), scared);
    g.setOpacity (1.0f);

    // z z z
    if (asleep > 0.05f)
    {
        g.setFont (Font (FontOptions ("Chalkboard SE", 13.0f, Font::bold)));
        for (int i = 0; i < 3; ++i)
        {
            const float u = std::fmod (t * 0.35f + (float) i / 3.0f, 1.0f);
            g.setColour (palette::blush.withAlpha (asleep * (1.0f - u) * 0.9f));
            g.setFont (Font (FontOptions ("Chalkboard SE", 10.0f + 8.0f * u, Font::bold)));
            g.drawText ("z", Rectangle<float> (pinkieX + 50.0f + 18.0f * u + 6.0f * std::sin (t + (float) i), baseY - 70.0f - 60.0f * u, 30.0f, 22.0f),
                        Justification::centred);
        }
    }
}


//==============================================================================
// The balloon store
void FreakiePieEditor::openStore()
{
    if (storeShelves != nullptr)
    {
        if (storeShelves->isOpen()) storeShelves->close();
        else storeShelves->open (storeSign);
        return;
    }
    PopupMenu m;
    m.setLookAndFeel (&storeLook);
    storeLook.time = (float) clock;
    m.addSectionHeader ("Balloon Store");
    for (int e = 0; e < kStoreItems; ++e)
    {
        const bool owned = bought (e);
        PopupMenu::Item it (storeItemName (e));
        it.itemID = e + 1;
        it.isTicked = owned;
        it.colour = storeBalloons[e][0] != nullptr ? balloonColours (storeBalloons[e][0]->hue).fill : Colour (0xffff8fb8);
        it.shortcutKeyDescription = owned ? String ("yours! tap to pop")
                                  : ((lentShown >> e) & 1u) != 0 ? String ("from the lightning! tap to keep")
                                                                  : storeItemWhat (e);
        m.addItem (it);
    }
    m.addSeparator();
    PopupMenu::Item all ("pop everything you bought");
    all.itemID = 100;
    all.isEnabled = std::any_of (std::begin (storeShown), std::end (storeShown), [] (bool b) { return b; });   // (lent ones too)
    all.colour = Colour (0xffd9578e);
    m.addItem (all);
    const auto screen = content.localAreaToGlobal (storeSign.getSmallestIntegerContainer());
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (screen).withMinimumWidth (240).withParentComponent (this),
                     [safe = Component::SafePointer<FreakiePieEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         if (r == 100)
                         {
                             safe->proc.popStruckRequest.fetch_or ((1u << fp::StoreFX::kNum) - 1u);
                             for (int e = 0; e < kStoreItems; ++e) safe->setBought (e, false);
                             return;
                         }
                         safe->setBought (r - 1, ! safe->bought (r - 1));
                     });
}

String FreakiePieEditor::ownedId (int item)
{
    return item == stutterItem ? String (ids::stOwned) : ids::store (item, ids::storeOwned);
}

String FreakiePieEditor::storeItemName (int item)
{
    return item == stutterItem ? String ("Gumball Machine") : String (fp::StoreFX::infos()[(size_t) item].name);
}

String FreakiePieEditor::storeItemWhat (int item)
{
    if (item == stutterItem) return "stutter + bounce + sway + cut-up";
    const auto& in = fp::StoreFX::infos()[(size_t) item];
    return String (in.c1.name) + " + " + in.c2.name;
}

void FreakiePieEditor::setBought (int fx, bool bought)
{
    if (auto* prm = proc.apvts.getParameter (ownedId (fx)))
        prm->setValueNotifyingHost (bought ? 1.0f : 0.0f);
    syncStore();
}

bool FreakiePieEditor::bought (int fx) const
{
    return proc.apvts.getRawParameterValue (ownedId (fx))->load() > 0.5f;
}

// follow the "bought" parameters (they also change from the host, presets and undo), and what the
// lightning has lent
void FreakiePieEditor::syncStore()
{
    const uint32_t struck = proc.struckMask.load();
    for (int e = 0; e < kStoreItems; ++e)
    {
        const bool isBought = bought (e), isLent = e != stutterItem && ((struck >> e) & 1u) != 0;   // (the lightning never lends the stutter)
        const bool owned = isBought || isLent;
        if (owned == storeShown[e])
        {
            if (owned) lentShown = isBought ? (lentShown & ~(1u << e)) : (lentShown | (1u << e));   // kept: it's yours now
            continue;
        }
        storeShown[e] = owned;
        const bool wasLent = ((lentShown >> e) & 1u) != 0;
        {
            // its gift box: out of the shop (or down with the lightning) to a spot on the floor; gone when it pops
            auto& box = anchors[storeAnchor (e)];
            box.dragging = false;
            if (owned)
            {
                // out of the shop door (or down with the lightning, or out of Trixie's bolt), onto its place on the shelf
                const bool fromTrixie = ((trixieGifts >> e) & 1u) != 0;
                trixieGifts &= ~(1u << e);
                box.pos = fromTrixie ? trixieGiftAt : (isBought ? storeSign.getCentre().translated (0.0f, 24.0f) : strikeAt);
                box.vel = {};
                box.hidden = false;
                box.shelfX = shelfRight + 40.0f;   // it slides round the ring into its place
            }
            else
            {
                confetti (box.pos.translated (0.0f, 10.0f), 12);
                box.hidden = true;
            }
            layoutShelf (shelfItems());
        }
        lentShown = (owned && ! isBought) ? (lentShown | (1u << e)) : (lentShown & ~(1u << e));
        for (int k = 0; k < 4; ++k)
        {
            auto* b = storeBalloons[e][k];
            if (b == nullptr) continue;
            if (owned && ! isBought)
            {
                // dropped in by the lightning, where it struck
                b->alive = true;
                b->placed3d = false;
                b->pos = strikeAt + Point<float> ((float) k * 22.0f - 33.0f, -10.0f + rnd.nextFloat() * 20.0f);
                b->vel = { (rnd.nextFloat() - 0.5f) * 300.0f, 420.0f + rnd.nextFloat() * 200.0f };
                b->wet = 0.6f;
                b->setVisible (true);
                b->place();
            }
            else if (owned)
            {
                // out with its box (from the shop door, or wherever Trixie's bolt landed)
                b->alive = true;
                b->placed3d = false;
                b->pos = anchors[storeAnchor (e)].pos + Point<float> ((float) k * 14.0f - 21.0f, 6.0f);
                b->vel = { -260.0f - rnd.nextFloat() * 160.0f, 60.0f + rnd.nextFloat() * 80.0f };
                b->wet = 0.0f;
                b->setVisible (true);
                b->place();
            }
            else
            {
                // a lent effect pops out balloon by balloon; a bought one pops once and leaves confetti
                if (k == 0 || wasLent) pop (b->pos, wasLent ? 0.35f : 0.5f, b->hue);
                else confetti (b->pos, 10);
                b->stopDrift();
                b->alive = false;
                b->dragging = false;
                b->setVisible (false);
            }
        }
        if (owned) confetti (isBought ? storeSign.getCentre() : strikeAt, 24);
    }
}

void FreakiePieEditor::drawStoreSign (Graphics& g) const
{
    const float t = (float) clock;
    // the board (no awning over it)
    const auto board = storeSign;
    g.setColour (Colour (0xff3a2a4c));
    g.fillRoundedRectangle (board, 6.0f);
    g.setColour (Colour (0xffff9fc8).withAlpha (0.8f));
    g.drawRoundedRectangle (board, 6.0f, 1.5f);
    g.setFont (SadLookAndFeel::uiFont (11.0f, true));
    g.setColour (palette::blush);
    g.drawText ("balloon store", board.withTrimmedLeft (18.0f), Justification::centred);
    // a little balloon in the window, bobbing
    const Point<float> bc (board.getX() + 14.0f, board.getCentreY() - 2.0f + 2.0f * std::sin (t * 1.7f));
    g.setColour (balloonColours (balloonLilac).fill);
    g.fillEllipse (Rectangle<float> (11.0f, 13.0f).withCentre (bc));
    g.setColour (Colours::white.withAlpha (0.7f));
    g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (bc.translated (-2.0f, -3.0f)));
    g.setColour (Colour (0xffeae4f0).withAlpha (0.6f));
    g.drawLine (bc.x, bc.y + 6.5f, bc.x + 1.0f, board.getBottom() - 3.0f, 0.8f);
}

// where the umbrella picture sits, and its canopy as half an ellipse (measured from the picture)
Rectangle<float> FreakiePieEditor::umbrellaImageArea() const noexcept
{
    if (! pinkieUmbrella.isValid()) return {};
    const float w = 128.0f, h = w * (float) pinkieUmbrella.getHeight() / (float) pinkieUmbrella.getWidth();
    return { pinkieX + 6.0f - w * 0.5f, (float) designH - 12.0f - h - pinkieHop, w, h };
}

bool FreakiePieEditor::umbrellaHit (Point<float> p, Point<float>& normal) const noexcept
{
    const auto a = umbrellaImageArea();
    if (a.isEmpty()) return false;
    const Point<float> c (a.getX() + a.getWidth() * 0.408f, a.getY() + a.getHeight() * 0.32f);
    const float rx = a.getWidth() * 0.40f, ry = a.getHeight() * 0.20f;
    const float dx = (p.x - c.x) / rx, dy = (p.y - c.y) / ry;
    if (dy > 0.0f || dx * dx + dy * dy > 1.0f) return false;
    normal = Point<float> (dx / rx, dy / ry);
    normal /= jmax (1.0e-6f, normal.getDistanceFromOrigin());
    return true;
}


//==============================================================================
// The store's menu: a sweet-shop window
StoreMenuLook::StoreMenuLook()
{
    setColour (PopupMenu::backgroundColourId, Colour (0xf4fff4f8));   // a touch see-through, so the corners can be round
    setColour (PopupMenu::textColourId, Colour (0xff4a2c4a));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xffffd6e7));
    setColour (PopupMenu::highlightedTextColourId, Colour (0xff4a2c4a));
}

void StoreMenuLook::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    const auto r = Rectangle<float> (0, 0, (float) width, (float) height).reduced (1.0f);
    g.setColour (Colour (0xfffff6fa));
    g.fillRoundedRectangle (r, 14.0f);
    // candy-stripe awning along the top, with a scalloped edge
    {
        Graphics::ScopedSaveState ss (g);
        Path clip; clip.addRoundedRectangle (r, 14.0f);
        g.reduceClipRegion (clip);
        const float sw = 18.0f;
        for (int i = 0; i * sw < r.getWidth() + sw; ++i)
        {
            g.setColour (i % 2 == 0 ? Colour (0xffff8fb8) : Colour (0xffffffff));
            g.fillRect (r.getX() + (float) i * sw, r.getY(), sw, 16.0f);
            g.fillEllipse (r.getX() + (float) i * sw, r.getY() + 10.0f, sw, 11.0f);
        }
        // sprinkles on the wallpaper
        Random rr (5);
        for (int i = 0; i < 40; ++i)
        {
            static const uint32 cols[] = { 0xffffb3c7, 0xffb8f0c8, 0xffa8d4ff, 0xfffff1a8, 0xffc7b3ff };
            g.setColour (Colour (cols[i % 5]).withAlpha (0.55f));
            const float x = rr.nextFloat() * r.getWidth(), y = 26.0f + rr.nextFloat() * (r.getHeight() - 30.0f);
            g.fillRoundedRectangle (Rectangle<float> (5.0f, 2.0f).withCentre ({ x, y }), 1.0f);
        }
    }
    g.setColour (Colour (0xffff8fb8));
    g.drawRoundedRectangle (r, 14.0f, 2.0f);
}

void StoreMenuLook::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name)
{
    g.setFont (titleFont (24.0f));
    g.setColour (Colour (0xffd9578e));
    g.drawText (name, area.toFloat().translated (0.0f, 4.0f), Justification::centred);
}

void StoreMenuLook::getIdealPopupMenuItemSize (const String& text, bool isSeparator, int standardHeight, int& w, int& h)
{
    if (isSeparator) { w = 60; h = 12; return; }
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, w, h);
    h = text == "Balloon Store" ? 44 : 34;
    w = jmax (w + 120, 300);
}

void StoreMenuLook::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                       bool isTicked, bool, const String& text, const String& shortcutKeyText, const Drawable*, const Colour* textColour)
{
    auto r = area.toFloat().reduced (6.0f, 2.0f);
    if (isSeparator)
    {
        g.setColour (Colour (0xffffb3c7));
        for (float x = r.getX() + 6.0f; x < r.getRight() - 6.0f; x += 8.0f)
            g.fillEllipse (x, r.getCentreY() - 1.5f, 3.0f, 3.0f);   // a dotted ribbon
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour (Colour (0xffffd6e7));
        g.fillRoundedRectangle (r, 10.0f);
    }
    const auto hue = textColour != nullptr ? *textColour
                                           : balloonColours ((int) ((uint32) text.hashCode() % (uint32) numHues)).fill;
    // the balloon, bobbing a little when you point at it
    const float bob = isHighlighted ? 2.0f * std::sin (time * 6.0f) : 0.0f;
    const Point<float> bc (r.getX() + 16.0f, r.getCentreY() - 3.0f + bob);
    g.setColour (hue.darker (0.35f));
    g.drawLine (bc.x, bc.y + 9.0f, bc.x + 1.5f, r.getBottom() - 1.0f, 1.0f);
    g.setColour (hue);
    g.fillEllipse (Rectangle<float> (16.0f, 19.0f).withCentre (bc));
    g.setColour (hue.darker (0.3f));
    g.drawEllipse (Rectangle<float> (16.0f, 19.0f).withCentre (bc), 1.2f);
    g.setColour (Colours::white.withAlpha (0.75f));
    g.fillEllipse (Rectangle<float> (4.0f, 5.0f).withCentre (bc.translated (-3.5f, -4.0f)));

    auto textArea = r.withTrimmedLeft (34.0f);
    g.setColour (Colour (isActive ? 0xff4a2c4a : 0x884a2c4a));
    g.setFont (uiFont (14.0f, true));
    g.drawText (text, textArea, Justification::centredLeft);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (uiFont (10.5f));
        g.setColour (Colour (0xff9a6b8e));
        g.drawText (shortcutKeyText, textArea.withTrimmedRight (isTicked ? 30.0f : 6.0f), Justification::centredRight);
    }
    if (isTicked)   // yours: a little heart
    {
        Path heart;
        const float hx = r.getRight() - 16.0f, hy = r.getCentreY();
        heart.startNewSubPath (hx, hy + 5.0f);
        heart.cubicTo (hx - 9.0f, hy - 1.0f, hx - 5.0f, hy - 9.0f, hx, hy - 3.5f);
        heart.cubicTo (hx + 5.0f, hy - 9.0f, hx + 9.0f, hy - 1.0f, hx, hy + 5.0f);
        g.setColour (Colour (0xffff5c9a));
        g.fillPath (heart);
    }
}

//==============================================================================
// The chalkboard
namespace
{
    constexpr float boardW = 640.0f, boardH = 430.0f;   // board space: the easel, drawn at any size
    float ease (float x) { x = jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); }
    const char* chalkButtons[] = { "sine", "bell", "spiky", "smooth", "clear" };
    const char* eqButtons[]    = { "flat", "warm", "bright", "smile", "smooth" };
}

void Chalkboard::bringForward()
{
    setBounds (0, 0, FreakiePieEditor::designW, FreakiePieEditor::designH);
    setVisible (true);
    toFront (false);
    animTarget = 1.0f;
}

void Chalkboard::putBack() { animTarget = 0.0f; drawing = false; }

bool Chalkboard::step (float dt)
{
    if (anim == animTarget) return isVisible();
    anim = animTarget > anim ? jmin (animTarget, anim + dt * 3.2f) : jmax (animTarget, anim - dt * 3.2f);
    if (anim <= 0.0f) setVisible (false);
    repaint();
    return isVisible();
}

// put away, it's a little easel standing on the floor; brought forward it grows to fill the room
Rectangle<float> Chalkboard::restArea() const
{
    constexpr float k = 0.16f;   // the floor easel is this fraction of full size
    return { floorSpot.x - boardW * k * 0.5f, floorSpot.y - boardH * k, boardW * k, boardH * k };
}

Rectangle<float> Chalkboard::boardArea() const
{
    // big: to the right of centre, leaving room for the teacher on the left
    const auto big = Rectangle<float> (boardW, boardH).withCentre ({ FreakiePieEditor::designW * 0.5f + 96.0f, FreakiePieEditor::designH * 0.5f + 6.0f });
    const auto rest = restArea();
    const float e = ease (anim);
    auto mix = [e] (float a, float b) { return a + (b - a) * e; };
    return { mix (rest.getX(), big.getX()), mix (rest.getY(), big.getY()), mix (rest.getWidth(), big.getWidth()), mix (rest.getHeight(), big.getHeight()) };
}

Rectangle<float> Chalkboard::plotArea() const
{
    const auto b = boardArea();
    const float k = b.getWidth() / boardW;
    return { b.getX() + 80.0f * k, b.getY() + 78.0f * k, 480.0f * k, 190.0f * k };
}

Rectangle<float> Chalkboard::buttonArea (int i) const
{
    const auto b = boardArea();
    const float k = b.getWidth() / boardW;
    return { b.getX() + (56.0f + 108.0f * (float) i) * k, b.getY() + 284.0f * k, 96.0f * k, 26.0f * k };
}

Rectangle<float> Chalkboard::tabArea (int i) const
{
    const auto b = boardArea();
    const float k = b.getWidth() / boardW;
    return { b.getX() + (148.0f + 176.0f * (float) i) * k, b.getY() + 42.0f * k, 168.0f * k, 30.0f * k };
}

void Chalkboard::drawChalkCurve (Graphics& g, const std::function<float (float)>& at, Rectangle<float> a, float th, float alpha)
{
    // three slightly-offset passes and some dust make it look like chalk on slate
    Random rr (11);
    for (int pass = 0; pass < 3; ++pass)
    {
        Path p;
        const float ox = (rr.nextFloat() - 0.5f) * th * 0.7f, oy = (rr.nextFloat() - 0.5f) * th * 0.7f;
        for (int i = 0; i <= 96; ++i)
        {
            const float t = (float) i / 96.0f;
            const Point<float> pt (a.getX() + t * a.getWidth() + ox, a.getBottom() - at (t) * a.getHeight() + oy);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (Colours::white.withAlpha (alpha * (pass == 0 ? 0.75f : 0.3f)));
        g.strokePath (p, PathStrokeType (th * (pass == 0 ? 1.0f : 0.6f), PathStrokeType::curved, PathStrokeType::rounded));
    }
    for (int i = 0; i < 70; ++i)
    {
        const float t = rr.nextFloat();
        g.setColour (Colours::white.withAlpha (alpha * 0.25f * rr.nextFloat()));
        g.fillEllipse (a.getX() + t * a.getWidth() + (rr.nextFloat() - 0.5f) * th * 3.0f,
                       a.getBottom() - at (t) * a.getHeight() + (rr.nextFloat() - 0.5f) * th * 3.0f, th * 0.5f, th * 0.5f);
    }
}

void Chalkboard::paint (Graphics& g)
{
    const float e = ease (anim);
    g.fillAll (Colour (0xff0b0812).withAlpha (0.55f * e));   // the room dims while it's out

    const auto b = boardArea();
    const float k = b.getWidth() / boardW;

    // the teacher, waiting beside the board with her pointer
    if (teacher.isValid() && e > 0.01f)
    {
        const float h = 250.0f * k, w = h * (float) teacher.getWidth() / (float) teacher.getHeight();
        const float floorY = b.getY() + 428.0f * k;
        const float bob = 2.0f * std::sin ((float) Time::getMillisecondCounterHiRes() * 0.0021f);
        const auto area = Rectangle<float> (b.getX() + 28.0f * k - w, floorY - h + bob, w, h);
        g.setColour (Colours::black.withAlpha (0.3f * e));
        g.fillEllipse (Rectangle<float> (w * 0.7f, 10.0f * k).withCentre ({ area.getCentreX() + w * 0.08f, floorY }));
        g.setOpacity (e);
        g.drawImage (teacher, area, RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }

    drawEasel (g, b, e);

    // the cursor is a stick of chalk while you're over the board
    if (hovering && e > 0.99f)
    {
        const auto tip = hover;
        const float L = 46.0f, W = 10.0f, ang = -0.75f;   // leaning up and to the right, the way you'd hold it
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::rotation (ang, tip.x, tip.y));
        const auto stick = Rectangle<float> (tip.x, tip.y - W * 0.5f, L, W);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (stick.translated (2.0f, 3.0f), 3.0f);
        g.setGradientFill (ColourGradient (Colour (0xfffffdf6), stick.getX(), stick.getY(), Colour (0xffd9d4c4), stick.getX(), stick.getBottom(), false));
        g.fillRoundedRectangle (stick, 3.0f);
        g.setColour (Colour (0xffbab3a0));
        g.drawRoundedRectangle (stick, 3.0f, 0.8f);
        // the worn tip, and a paper sleeve near the back
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (5.0f, W * 0.8f).withCentre ({ tip.x + 1.5f, tip.y }));
        g.setColour (Colour (0xffff9fc8));
        g.fillRect (Rectangle<float> (tip.x + L * 0.62f, tip.y - W * 0.5f, L * 0.3f, W));
        g.setColour (Colour (0xffd9578e).withAlpha (0.6f));
        g.drawLine (tip.x + L * 0.62f, tip.y - W * 0.5f, tip.x + L * 0.62f, tip.y + W * 0.5f, 1.0f);
    }
}

void Chalkboard::drawEasel (Graphics& g, Rectangle<float> b, float e) const
{
    const float k = b.getWidth() / boardW;
    auto at = [&] (float x, float y, float w, float h) { return Rectangle<float> (b.getX() + x * k, b.getY() + y * k, w * k, h * k); };

    // the easel: legs and a crossbar, standing on the floor
    {
        const float legs = 1.0f;
        g.setColour (Colour (0xff8a5a32).withAlpha (legs));
        auto leg = [&] (float x0, float y0, float x1, float y1)
        {
            Path p; p.startNewSubPath (b.getX() + x0 * k, b.getY() + y0 * k); p.lineTo (b.getX() + x1 * k, b.getY() + y1 * k);
            g.setColour (Colour (0xff5e3a1e).withAlpha (legs)); g.strokePath (p, PathStrokeType (13.0f * k, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colour (0xffa8723f).withAlpha (legs)); g.strokePath (p, PathStrokeType (9.0f * k, PathStrokeType::curved, PathStrokeType::rounded));
        };
        leg (70, 300, 40, 428); leg (570, 300, 600, 428);
        leg (320, 300, 320, 410);
        leg (48, 392, 592, 392);
    }
    // the wooden frame and the slate
    const auto frame = at (20, 20, 600, 300);
    g.setColour (Colour (0xff5e3a1e));
    g.fillRoundedRectangle (frame.expanded (2.0f * k), 10.0f * k);
    g.setColour (Colour (0xffa8723f));
    g.fillRoundedRectangle (frame, 9.0f * k);
    g.setColour (Colour (0xffc48b52));
    g.drawRoundedRectangle (frame.reduced (3.0f * k), 8.0f * k, 2.0f * k);
    const auto slate = at (36, 36, 568, 268);
    g.setGradientFill (ColourGradient (Colour (0xff35574a), slate.getX(), slate.getY(), Colour (0xff27443a), slate.getRight(), slate.getBottom(), false));
    g.fillRect (slate);
    // old eraser smudges
    Random rr (3);
    for (int i = 0; i < 7; ++i)
    {
        g.setColour (Colours::white.withAlpha (0.035f));
        g.fillEllipse (Rectangle<float> ((60.0f + rr.nextFloat() * 80.0f) * k, (20.0f + rr.nextFloat() * 30.0f) * k)
                           .withCentre ({ slate.getX() + rr.nextFloat() * slate.getWidth(), slate.getY() + rr.nextFloat() * slate.getHeight() }));
    }
    // the chalk tray with a stick of chalk and an eraser
    g.setColour (Colour (0xff8a5a32));
    g.fillRoundedRectangle (at (40, 318, 560, 10), 3.0f * k);
    g.setColour (Colour (0xfff3f0e6));
    g.fillRoundedRectangle (at (440, 311, 34, 7), 3.0f * k);
    g.setColour (Colour (0xffff9fc8));
    g.fillRoundedRectangle (at (490, 307, 46, 11), 2.0f * k);
    g.setColour (Colour (0xff5e3a1e));
    g.fillRect (at (490, 314, 46, 4));

    const Rectangle<float> plot { b.getX() + 80.0f * k, b.getY() + 78.0f * k, 480.0f * k, 190.0f * k };
    const float chalkA = 0.6f + 0.4f * e;
    // writing on the board
    g.setColour (Colours::white.withAlpha (0.8f * chalkA));
    g.setFont (Font (FontOptions ("Chalkboard SE", 22.0f * k, Font::plain)));
    const auto& eqc = owner.proc.eq;
    if (e > 0.5f)
    {
        // two pages, like tabs written at the top: the grain shape and the master EQ
        const char* tabs[] = { "grain shape", "master eq" };
        for (int i = 0; i < 2; ++i)
        {
            const auto r = Rectangle<float> (b.getX() + (148.0f + 176.0f * (float) i) * k, b.getY() + 42.0f * k, 168.0f * k, 30.0f * k);
            g.setColour (Colours::white.withAlpha ((page == i ? 0.95f : 0.45f) * chalkA));
            g.drawText (tabs[i], r, Justification::centred);
            if (page == i)
                g.drawLine (r.getX() + 22.0f * k, r.getBottom() - 2.0f * k, r.getRight() - 22.0f * k, r.getBottom() - 1.0f * k, 2.0f * k);
        }
    }
    else g.drawText ("grains + eq", at (36, 40, 568, 34), Justification::centred);
    if (e > 0.5f)
    {
        g.setFont (Font (FontOptions ("Chalkboard SE", 10.5f * k, Font::plain)));
        g.setColour (Colours::white.withAlpha (0.35f * chalkA));
        g.drawText ("click outside the board to put it away", at (36, 2, 568, 16), Justification::centred);
    }

    if (page == 0 || e <= 0.5f)
    {
        // axes, like a lesson
        g.setColour (Colours::white.withAlpha (0.3f * chalkA));
        g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getBottom(), 1.5f * k);
        g.drawLine (plot.getX(), plot.getY(), plot.getX(), plot.getBottom(), 1.5f * k);
        if (e > 0.3f)
        {
            g.setFont (Font (FontOptions ("Chalkboard SE", 12.0f * k, Font::plain)));
            g.drawText ("start", Rectangle<float> (plot.getX() - 20.0f * k, plot.getBottom() + 2.0f * k, 60.0f * k, 14.0f * k), Justification::centredLeft);
            g.drawText ("end", Rectangle<float> (plot.getRight() - 40.0f * k, plot.getBottom() + 2.0f * k, 40.0f * k, 14.0f * k), Justification::centredRight);
            g.drawText ("loud", Rectangle<float> (plot.getX() - 46.0f * k, plot.getY(), 40.0f * k, 14.0f * k), Justification::centredRight);
        }
        drawChalkCurve (g, [this] (float t) { return owner.proc.chalk.at (t); }, plot, 3.0f * k, chalkA);
    }
    else
    {
        // the EQ: 0 dB across the middle, octaves marked along the bottom, boost up and cut down
        g.setColour (Colours::white.withAlpha (0.12f * chalkA));
        for (float db : { -6.0f, 6.0f })
        {
            const float y = plot.getCentreY() - db / fp::EqCurve::range * plot.getHeight() * 0.5f;
            g.drawLine (plot.getX(), y, plot.getRight(), y, 1.0f * k);
        }
        g.setFont (Font (FontOptions ("Chalkboard SE", 11.0f * k, Font::plain)));
        const float marks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        for (float f : marks)
        {
            const float x = plot.getX() + (float) fp::EqCurve::tOf (f) * plot.getWidth();
            g.setColour (Colours::white.withAlpha (0.12f * chalkA));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f * k);
            g.setColour (Colours::white.withAlpha (0.5f * chalkA));
            g.drawText (f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)),
                        Rectangle<float> (40.0f * k, 14.0f * k).withCentre ({ x, plot.getBottom() - 8.0f * k }), Justification::centred);
        }
        g.setColour (Colours::white.withAlpha (0.35f * chalkA));
        float dashes[] = { 6.0f * k, 5.0f * k };
        g.drawDashedLine (Line<float> (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY()), dashes, 2, 1.5f * k);
        g.setColour (Colours::white.withAlpha (0.55f * chalkA));
        g.drawText ("boost", Rectangle<float> (plot.getX() - 62.0f * k, plot.getY(), 56.0f * k, 14.0f * k), Justification::centredRight);
        g.drawText ("0 dB", Rectangle<float> (plot.getX() - 62.0f * k, plot.getCentreY() - 7.0f * k, 56.0f * k, 14.0f * k), Justification::centredRight);
        g.drawText ("cut", Rectangle<float> (plot.getX() - 62.0f * k, plot.getBottom() - 14.0f * k, 56.0f * k, 14.0f * k), Justification::centredRight);

        // what the EQ really does (pink), under what you drew (chalk)
        if (! eqc.isFlat())
        {
            Path real;
            for (int i = 0; i <= 120; ++i)
            {
                const float t = (float) i / 120.0f;
                const float db = jlimit (-fp::EqCurve::range * 1.1f, fp::EqCurve::range * 1.1f, (float) eqc.actualDb (fp::EqCurve::freqAt (t)));
                const Point<float> pt (plot.getX() + t * plot.getWidth(), plot.getCentreY() - db / fp::EqCurve::range * plot.getHeight() * 0.5f);
                if (i == 0) real.startNewSubPath (pt); else real.lineTo (pt);
            }
            g.setColour (Colour (0xffff9fc8).withAlpha (0.7f * chalkA));
            g.strokePath (real, PathStrokeType (1.6f * k, PathStrokeType::curved, PathStrokeType::rounded));
        }
        drawChalkCurve (g, [&eqc] (float t) { return eqc.at (t); }, plot, 3.0f * k, chalkA);

        // where the chalk is: frequency and gain
        if (hovering && e > 0.99f && plot.expanded (6.0f).contains (hover))
        {
            const double f = fp::EqCurve::freqAt ((hover.x - plot.getX()) / plot.getWidth());
            const float db = (plot.getCentreY() - hover.y) / (plot.getHeight() * 0.5f) * fp::EqCurve::range;
            const String txt = (f >= 1000.0 ? String (f / 1000.0, 1) + " kHz" : String (roundToInt (f)) + " Hz") + "   "
                             + (db >= 0.0f ? "+" : "") + String (jlimit (-fp::EqCurve::range, fp::EqCurve::range, db), 1) + " dB";
            g.setColour (Colour (0xffffe9a8).withAlpha (0.9f));
            g.drawText (txt, Rectangle<float> (150.0f * k, 16.0f * k).withCentre (hover.translated (0.0f, -26.0f * k)), Justification::centred);
        }
    }

    // chalk buttons
    if (e > 0.3f)
    {
        const bool using_ = (int) owner.proc.apvts.getRawParameterValue (ids::grShape)->load() == 6;
        for (int i = 0; i < 5; ++i)
        {
            const Rectangle<float> r { b.getX() + (56.0f + 108.0f * (float) i) * k, b.getY() + 284.0f * k, 96.0f * k, 26.0f * k };
            Path box; box.addRoundedRectangle (r, 6.0f * k);
            g.setColour (Colour (0xff142620).withAlpha (0.92f * e));   // a dark chip, so the chalk reads against the wood
            g.fillPath (box);
            g.setColour (Colours::white.withAlpha (0.55f * e));
            g.strokePath (box, PathStrokeType (1.4f * k), AffineTransform::translation (0.6f, 0.4f));
            g.setFont (Font (FontOptions ("Chalkboard SE", 14.0f * k, Font::plain)));
            g.setColour (Colours::white.withAlpha (0.85f * e));
            g.drawText (page == 0 ? chalkButtons[i] : eqButtons[i], r, Justification::centred);
        }
        g.setFont (Font (FontOptions ("Chalkboard SE", 12.0f * k, Font::plain)));
        if (page == 0)
        {
            g.setColour (Colour (using_ ? 0xffb8f0c8 : 0xffffd1a6).withAlpha (0.9f * e));
            g.drawText (using_ ? "the grains are using this shape" : "draw on it to use it for the grains",
                        at (36, 262, 568, 18), Justification::centred);
        }
        else
        {
            const bool flat = eqc.isFlat();
            g.setColour (Colour (flat ? 0xffffd1a6 : 0xffb8f0c8).withAlpha (0.9f * e));
            g.drawText (flat ? "flat: draw above the line to boost, below to cut" : "the whole output follows this curve (pink: what you hear)",
                        at (36, 264, 568, 18), Justification::centred);
        }
    }
}

void Chalkboard::drawAt (Point<float> p)
{
    const auto a = plotArea();
    if (page == 1)
    {
        auto& c = owner.proc.eq;
        const int N = fp::EqCurve::N;
        const int idx = jlimit (0, N - 1, roundToInt ((p.x - a.getX()) / a.getWidth() * (float) (N - 1)));
        float h = jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight());
        if (std::abs (h - 0.5f) < 0.012f) h = 0.5f;   // the 0 dB line is a little sticky, so you can draw back to flat
        if (lastIndex < 0) c.set (idx, h);
        else
        {
            const int lo = jmin (idx, lastIndex), hi = jmax (idx, lastIndex);
            for (int i = lo; i <= hi; ++i)
            {
                const float u = hi == lo ? 1.0f : (float) (i - lastIndex) / (float) (idx - lastIndex);
                c.set (i, lastHeight + (h - lastHeight) * u);
            }
        }
        lastIndex = idx; lastHeight = h;
        c.fit();
        repaint();
        return;
    }
    auto& c = owner.proc.chalk;
    const int idx = jlimit (0, fp::ChalkShape::N - 1, roundToInt ((p.x - a.getX()) / a.getWidth() * (float) (fp::ChalkShape::N - 1)));
    const float h = jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight());
    if (lastIndex < 0) c.set (idx, h);
    else
    {
        const int lo = jmin (idx, lastIndex), hi = jmax (idx, lastIndex);
        for (int i = lo; i <= hi; ++i)
        {
            const float u = hi == lo ? 1.0f : (float) (i - lastIndex) / (float) (idx - lastIndex);
            c.set (i, lastHeight + (h - lastHeight) * u);
        }
    }
    lastIndex = idx; lastHeight = h;
    c.updatePower();
    if (auto* prm = owner.proc.apvts.getParameter (ids::grShape))
        if (roundToInt (owner.proc.apvts.getRawParameterValue (ids::grShape)->load()) != 6)
            prm->setValueNotifyingHost (prm->convertTo0to1 (6.0f));
    repaint();
}

void Chalkboard::mouseDown (const MouseEvent& e)
{
    owner.noteActivity();
    const auto p = e.position;
    if (anim < 0.99f) return;
    if (! boardArea().contains (p) && ! plotArea().contains (p)) { putBack(); return; }
    for (int i = 0; i < 2; ++i)
        if (tabArea (i).contains (p)) { setPage (i); return; }
    if (page == 1)
        for (int i = 0; i < 5; ++i)
            if (buttonArea (i).contains (p)) { eqButton (i); return; }
    for (int i = 0; i < 5; ++i)
        if (buttonArea (i).contains (p))
        {
            auto& c = owner.proc.chalk;
            const int N = fp::ChalkShape::N;
            Random rr;
            std::vector<float> v ((size_t) N);
            for (int k = 0; k < N; ++k) v[(size_t) k] = c.at ((float) k / (float) (N - 1));
            for (int k = 0; k < N; ++k)
            {
                const float t = (float) k / (float) (N - 1);
                if (i == 0) v[(size_t) k] = std::sin (MathConstants<float>::pi * t);
                if (i == 1) v[(size_t) k] = std::exp (-std::pow ((t - 0.5f) / 0.18f, 2.0f));
                if (i == 2) v[(size_t) k] = (k % 8 < 2 ? 1.0f : 0.25f) * std::sin (MathConstants<float>::pi * t);
                if (i == 4) v[(size_t) k] = 0.0f;
            }
            if (i == 3)
                for (int pass = 0; pass < 3; ++pass)
                    for (int k = 1; k < N - 1; ++k) v[(size_t) k] = 0.25f * v[(size_t) k - 1] + 0.5f * v[(size_t) k] + 0.25f * v[(size_t) k + 1];
            for (int k = 0; k < N; ++k) c.set (k, v[(size_t) k]);
            c.updatePower();
            if (i != 4)
                if (auto* prm = owner.proc.apvts.getParameter (ids::grShape)) prm->setValueNotifyingHost (prm->convertTo0to1 (6.0f));
            repaint();
            return;
        }
    if (plotArea().expanded (8.0f).contains (p)) { drawing = true; lastIndex = -1; drawAt (p); }
}

// the EQ's ready-made curves (in dB, over 0..1 of the board), and smoothing what's drawn
void Chalkboard::eqButton (int i)
{
    auto& c = owner.proc.eq;
    const int N = fp::EqCurve::N;
    std::vector<float> v ((size_t) N);
    for (int k = 0; k < N; ++k) v[(size_t) k] = c.at ((float) k / (float) (N - 1));
    auto bump = [] (float t, float centre, float width) { return std::exp (-std::pow ((t - centre) / width, 2.0f)); };
    for (int k = 0; k < N; ++k)
    {
        const float t = (float) k / (float) (N - 1);
        float db = 0.0f;
        if (i == 1) db = 8.0f * bump (t, 0.1f, 0.22f) - 7.0f * bump (t, 1.0f, 0.25f);    // warm: fuller lows, softer top
        if (i == 2) db = 9.0f * bump (t, 1.0f, 0.3f) - 3.0f * bump (t, 0.05f, 0.15f);    // bright: air on top
        if (i == 3) db = 10.0f * bump (t, 0.0f, 0.22f) + 10.0f * bump (t, 1.0f, 0.22f) - 5.0f * bump (t, 0.5f, 0.2f);   // smile
        if (i != 4) v[(size_t) k] = 0.5f + db / (2.0f * fp::EqCurve::range);
    }
    if (i == 4)
        for (int pass = 0; pass < 3; ++pass)
            for (int k = 1; k < N - 1; ++k) v[(size_t) k] = 0.25f * v[(size_t) k - 1] + 0.5f * v[(size_t) k] + 0.25f * v[(size_t) k + 1];
    for (int k = 0; k < N; ++k) c.set (k, v[(size_t) k]);
    c.fit();
    repaint();
}

void Chalkboard::mouseDrag (const MouseEvent& e) { mouseMove (e); if (drawing) drawAt (e.position); }
void Chalkboard::mouseUp (const MouseEvent&)     { drawing = false; lastIndex = -1; }

void Chalkboard::mouseMove (const MouseEvent& e)
{
    hover = e.position;
    const bool over = anim > 0.99f && plotArea().expanded (14.0f).contains (hover);
    if (over != hovering) setMouseCursor (over ? MouseCursor::NoCursor : MouseCursor::NormalCursor);
    hovering = over;
    repaint();
}
void Chalkboard::mouseExit (const MouseEvent&) { hovering = false; setMouseCursor (MouseCursor::NormalCursor); repaint(); }

//==============================================================================
// The library (help)
namespace
{
    struct HelpChapter { const char* title; const char* body; };
    const HelpChapter helpChapters[] = {
        { "Welcome",
          "Every balloon in the shop is a control, and how full it is shows its value.\n\n"
          "In short: drag to move, scroll to inflate, shift-drag for fine changes, double-click to reset, click a caught sound to type its pitch, drag the pin to pop them.\n\n"
          "Drag a balloon to move it, and let go to watch it float back up on its string. Scroll or swipe over it to blow it up or let some air out. "
          "Hold shift and drag up or down to set it by hand (hold alt as well for finer steps). Double-click to put it back how it started.\n\n"
          "Balloons with a setting written on them, like shape or octave, step to the next one when you click. Switches like freeze turn on and off with a click.\n\n"
          "The bows hold each bunch. Drag a bow to move its whole bunch. The next page shows which colour belongs to which effect." },
        { "The sphere",
          "The shop is inside a glass sphere now, and you're the little head in the middle, looking ahead into the screen. "
          "The bows stand on a ring round its floor, in the order the sound goes through the effects, and their balloons float up inside it.\n\n"
          "Drag the empty glass to turn the sphere round and look at it from anywhere. Turning it only changes your view, never the sound.\n\n"
          "The words on the glass show which way is ahead, left, right, behind and up. The glow on the glass and the map at the top left show "
          "where the sound is loudest, like the IEM EnergyVisualizer: red is the loudest place, then yellow, green and blue." },
        { "Grains",
          "The little bouquets flying in the sphere are grains. Click the jar on the left for another one, or drag one out of the jar and drop it where you want it. "
          "Drag a grain back onto the jar, or right-click the jar, to take one away. You can have up to 16.\n\n"
          "Your own sound, dry, plays from one place: the little pink loudspeaker. Drag it anywhere in the sphere.\n\n"
          "The grains token on the right picks how the effects use the grains. On spatial, every grain carries its own copy of the effects "
          "(the smear, the pitch shifter, the gumball machine and what you bought), and each copy's sound plays at its grain as a point, so you can hear "
          "where each one is. The granulator (the mint balloons) is a cloud of short grains, each one born at its own direction round one of the grains, "
          "like the IEM GranularEncoder: grain spread sets how far round its grain each one may land, from exactly there to anywhere on the sphere. "
          "On scatter there's only one copy of the effects, at the first grain, and the granulator's grains still go to all of them. "
          "On copies, every grain is the whole shop, your dry sound and all, the way the sphere first worked.\n\n"
          "Deviate is how far the copies stray from your settings. The first grain always plays your settings as they are; the others drift away slowly, "
          "each its own way, and now and then freeze or let go by themselves. In the granulator it makes every grain's length, start and pitch a little different. "
          "Every sound in the sphere is a single point, so you can always tell where it is.\n\n"
          "A grain glows with its sound, hops when it stutters and frosts over when it freezes." },
        { "Flying",
          "The flight token picks how the grains move, and speed sets how fast.\n\n"
          "Still keeps each grain where you dropped it. Orbit sends each one round you on its own tilted ring. "
          "Lorenz flies them along the Lorenz attractor, the butterfly, each one starting somewhere else on it. "
          "Flock makes them a flock of birds (boids): they keep apart, line up and stay together. Random picks a new place every so often and glides there.\n\n"
          "Distance is the size of the bubble you're in, from 1.5 metres to 24. In a small bubble everything is close and loud and the room answers quickly; "
          "in a big one everything is far away, quieter, a little duller and roomier. It sounds the way distance does, after the IEM RoomEncoder: "
          "the sound takes longer to arrive from further away (so anything flying towards you or away bends in pitch, like a passing car), "
          "and the walls of a room round the bubble answer each sound faintly. Close in, a sound spreads round your head.\n\n"
          "You can drag a grain while it flies. Let go and it carries on from there." },
        { "Out",
          "Out comes third-order Ambisonics on 16 channels, ACN order with SN3D normalisation, the format the IEM and SPARTA plug-ins use. "
          "In REAPER, give the track 16 channels and put a decoder after it, such as the IEM AllRADecoder for loudspeakers or the BinauralDecoder for headphones. "
          "On a track with fewer channels only the first ones come out, and the map says so.\n\n"
          "There is one reverb for the whole sphere (the lilac balloons), built like the IEM FdnReverb: 64 delay lines run on the 16 channels, "
          "with the highs dying away faster than the lows. The grains further out send more into it." },
        { "Colours",
          "Each bunch has its own colour, and so does its bow." },
        { "The effects",
          "Your sound runs through four effects in a row.\n\n"
          "First the spectral smear (the blue balloons), which blurs the sound across time. Spectral freeze holds it still. "
          "Spectral tilt leans the smear from dark to bright: up lifts the highs and sinks the lows, down does the opposite, and the middle is flat. The smear stays about as loud either way.\n\n"
          "Then the pitch shifter. Shift picks the interval, snapped to the scale chosen on the card at the top left, and pitch feedback sends it round again so it keeps climbing.\n\n"
          "Then the grains, and a reverb that keeps the low end clean (in the sphere there's one reverb for all the grains together, at the end). That's the order they start in: the order chapter says how to change it.\n\n"
          "Each effect has a mix balloon. When a mix balloon is empty, that effect is out of the way.\n\n"
          "Main feedback takes the end of the chain back to the start, so the whole shop can ring on by itself. It is tamed so it sings without blowing up." },
        { "Grains",
          "The mint bunch chops your sound into small grains. Grain size sets how long each one is and density sets how many overlap.\n\n"
          "Shape is the envelope of each grain. The last shape, chalk, is one you draw yourself on the chalkboard.\n\n"
          "Random pan scatters the grains across the stereo field and reverse flips some of them backwards. Octave moves them up or down, or let random octave choose for each grain.\n\n"
          "Position picks where in the last four seconds the grains read from, from right now to four seconds back, and position random lets them wander around that spot. "
          "Click freeze to hold the sound in place. Freeze fade sets how slowly it slides in and out of the freeze.\n\n"
          "These balloons also shape every frozen balloon you catch with the sound pump, even when the effects are bypassed." },
        { "The gumball machine",
          "The gumball machine is sold at the balloon store. Buy it and it comes out in an orange gift box with four balloons, and takes its place on the shelf like the other effects; give it back (or pop one of its balloons) and the sound goes straight past it again.\n\n"
          "The orange bunch is a stutter that plays with the whole sound in time with your track (or at its own steady tempo when the track is stopped). "
          "On each sixteenth note it may grab the moment and do something with it.\n\n"
          "Stutter sets how often that happens. Bounce leans it from plain rolls and flams towards bouncing-ball repeats that speed up until they become a pitch, "
          "slowing ones that do the opposite, and frozen buzzing grains. Cut-up lets it pull its sound from other steps of the last bar, so the bar gets rearranged like a breakbeat. "
          "Sway holds back every second sixteenth, from a gentle swing to a stumble.\n\n"
          "These ideas come from Alexander Mesker's study of Autechre's music: the bouncing ball, the event types, restarting patterns and the swing table.\n\n"
          "Now and then a pluck from the pop pump bounces too." },
        { "The order",
          "The sound goes through the effects from left to right, from in to out: the spectral smear (blue), pitch (yellow), grains (mint), reverb (lilac) and a gift box for each effect from the store, the gumball machine (orange) among them.\n\n"
          "The order list waits just off the left edge of the window. Move the cursor to the left edge and it slides out, with the effects from top (in) to bottom (out). Drag one up or down to move it: the others make room, and when you let go the bows hop to their new places and the sound follows with a tiny dip so it never clicks. The die at the top rolls a new order. The order is saved with your session.\n\n"
          "The loop (main feedback) and the caught sounds stay on the floor: the loop wraps round all the effects." },
        { "The pop pump",
          "Hold the pump on the left to blow up a balloon.\n\n"
          "While it fills, short plucked saws play on random notes of the scale, slowly and unevenly. Every pluck is folded and panned at random, "
          "and its attack is sometimes snappy and sometimes slow. The plucks go through the effects.\n\n"
          "Somewhere between 5 and 12 seconds the balloon pops, and every pop is different.\n\n"
          "The pop sounds switch at the top right, above help and reset, turns the hiss and the pops on or off. Right-click it to feed them into the effects instead of straight out, so the pops go through the chain with your sound." },
        { "Catching sounds",
          "Hold the pump on the right to catch a sound in a balloon. It catches what comes out of the effects, or your plain input if you click the catch token beside it. "
          "The balloon starts muffled and opens up as it fills. Let go and it floats away, playing what it caught as frozen grains.\n\n"
          "You can keep six at once, and the teal frozen volume balloon sets how loud they all are. The badge by the pump shows the pitch of the next one. Click it, or click any caught balloon, to type an interval like +7 or -5, then press return. "
          "Scrolling over a caught balloon retunes it along the scale.\n\n"
          "To pop one, double-click it, or drag the pin from the tomato cushion onto it." },
        { "The canister",
          "Click the helium canister to fill the room with helium. It adds a gentle highpass to the output, and the balloons stretch and float higher.\n\n"
          "Slide the canister along its rail to choose where the frozen balloons go. To the right they come straight out. "
          "To the left they go back through the effects, so the pitch shifter and the reverb play with them too." },
        { "Weather",
          "The weather switches run along the top of the window: the sun and cloud (cloudy), the rain cloud, the snowflake, the hot sun (heatwave) and auto weather. Click one to turn it on and again to turn it off. Rain and clouds together make a storm.\n\n"
          "Rain eases a gentle lowpass onto the output and soaks the balloons.\n\n"
          "In the sphere the weather also drifts through the inside of the glass as patches: clouds, rain clouds, snow flurries and heat haze. "
          "Any grain a patch passes over gets its settings shaken up at random while it's in there. Snow freezes that grain's spectrum, and the heat smears it.\n\n"
          "When it's cloudy, clouds drift through the bouquet, and any balloon inside one slowly wanders to new values, with a little tag showing where it has got to. Clouds bring thunder too, which pulls the rain's lowpass down in slow rolls.\n\n"
          "In a storm, lightning strikes now and then and drops a random effect from the store into the room for a minute or so. If you like it, tick it in the store and it stays.\n\n"
          "Snow freezes everything over the fade-in time: the sound slows until it stops completely and holds, and the balloons ice over and hang still. It stays frozen until you click the snowflake again, then thaws over the fade-out time. Right-click the snowflake to set both times, anywhere from 5 seconds to 5 minutes: drag along a bar or scroll on it, and double-click puts it back to 5 seconds. Leave Pinkie out in the snow for a minute and she catches a cold; she perks up once the snow has gone.\n\n"
          "Snow and a heatwave never come together: turning one on sends the other away.\n\n"
          "A heatwave melts the sound into a slow smear over its fade-in time (right-click the hot sun to set its fade in and fade out, like the snow's): whatever you play is spread across many seconds, and the held sound keeps drifting towards whatever comes next, like a stretched recording that never quite stops. The top end softens and swells as the hot air wobbles, gently, nothing like the rain. Pinkie can't take it and flops down flat, and if it rains in the heat she's soaked through.\n\n"
          "With auto weather on, the sky looks after itself. Every few minutes the weather turns, the way it does outside: clear skies cloud over, the cloud brings rain or a storm or snow, a long hot spell might end in a summer storm. Each spell lasts somewhere between a minute and twelve, and the dial shows what the sky is doing. You can still flip the switches yourself; auto carries on from there." },
        { "The balloon store",
          "Click the balloon store sign at the top to open the store. Pick an effect and a little gift box drops out of the shop with four balloons tied to it: its two controls, then its level and its width. "
          "The box takes a place among the other effects, and the order list moves it about like the rest. Width spreads the effect from mono in the middle to wide stereo.\n\n"
          "Pick a bought effect again to give it back, or pop one of its balloons with the pin. Pop everything sends them all back at once.\n\n"
          "The tuned effects in the store follow the scale as well. The ring mod multiplies the sound by a tone, and with follow turned up the tone rises as the sound gets louder." },
        { "The chalkboard",
          "Click the little easel on the floor to bring the chalkboard forward. It has two pages, written along the top.\n\n"
          "On grain shape, draw across the board to make the life of one grain, from start on the left to end on the right, with loud at the top. Drawing switches the grains to your shape.\n\n"
          "On master eq, draw the tone of the whole output. Above the dashed line boosts and below it cuts, with the lows on the left. "
          "The pink line shows what you actually hear. The buttons underneath give you ready-made curves, smooth what you drew, or set it flat again.\n\n"
          "On master eq the board goes from a deep cut at the bottom to a big boost at the top, 24 dB each way.\n\n"
          "Click anywhere outside the board to put it away." },
        { "The card",
          "The card holds the presets and the scale. It waits just above the top left of the window: move the cursor up there and it slides down. Every preset changes the picture behind the shop as well as the sound.\n\n"
          "The scale decides which notes the pitch shifter lands on. Everything tuned in the shop follows it.\n\n"
          "The reset button under the help book in the top right corner brings everything back to normal: the first preset, a flat EQ, the plain grain shape and the effects in their first order. "
          "Caught sounds stay where they are.\n\n"
          "The switch underneath the card bypasses the effects, the store's ones included. Only your input and the frozen balloons are left, and the grain balloons still shape them." },
        { "Pinkie",
          "Pinkie sits by the pop pump watching the balloons.\n\n"
          "Pops make her jump. When it rains she gets her umbrella out (unless it's a heatwave too: then she lies in the puddle, soaked), and helium turns her to crystal.\n\n"
          "Leave the shop alone for a while and she falls asleep.\n\n"
          "Every so often, about once an hour, the Great and Powerful Trixie barges in. She zaps balloons from her amulet, each one jumping to a new value, "
"then sets everything she can reach at random with one big flash and leaves. Some visits she also plays a strange synthesised noise into the shop's input, which goes through all the effects, with a chirp for every zap. "
          "Sometimes she zaps a few effects from the store into the room too, and those stay. She leaves the bypass and the volume alone, and never touches the snow, the heat or auto weather. "
          "If you don't like what she did, the reset button under the help book puts everything back to normal." },
    };

    const Colour inkBrown   { 0xff3d2a3f };
    const Colour twiPurple  { 0xff6b3f8f };
    const Colour twiPink    { 0xffe5409a };
}

Library::Library (FreakiePieEditor& e) : owner (e)
{
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (true);
}

int Library::numChapters() const noexcept { return (int) std::size (helpChapters); }
void Library::showChapter (int i)
{
    i = jlimit (0, numChapters() - 1, i);
    if (i == chapter) return;
    flipFrom = chapter;
    flipDir = i > chapter ? 1 : -1;
    flipT = 0.0f;
    chapter = i;
    repaint();
}

void Library::open()
{
    setBounds (0, 0, FreakiePieEditor::designW, FreakiePieEditor::designH);
    setVisible (true);
    toFront (true);
    animTarget = 1.0f;
}

void Library::close() { animTarget = 0.0f; }

bool Library::step (float dt)
{
    t += dt;
    if (flipT < 1.0f) flipT = jmin (1.0f, flipT + dt / 0.6f);   // a page takes 0.6 s to turn
    if (anim != animTarget)
    {
        anim = animTarget > anim ? jmin (animTarget, anim + dt * 3.5f) : jmax (animTarget, anim - dt * 3.5f);
        if (anim <= 0.0f) setVisible (false);
    }
    if (isVisible()) repaint();
    return isVisible();
}

Rectangle<float> Library::bookArea() const
{
    const float e = ease (anim);
    return Rectangle<float> (744.0f, 486.0f).withCentre ({ 618.0f, 312.0f + 30.0f * (1.0f - e) }).withSizeKeepingCentre (744.0f * (0.92f + 0.08f * e), 486.0f * (0.92f + 0.08f * e));
}
Rectangle<float> Library::leftPage() const  { auto b = bookArea().reduced (14.0f); return b.withWidth (b.getWidth() * 0.5f); }
Rectangle<float> Library::rightPage() const { auto b = bookArea().reduced (14.0f); return b.withTrimmedLeft (b.getWidth() * 0.5f); }
Rectangle<float> Library::chapterRow (int i) const
{
    const auto p = leftPage().reduced (34.0f, 0.0f);
    const float rowH = jmin (29.0f, (p.getHeight() - 108.0f - 14.0f) / (float) numChapters());
    return { p.getX(), p.getY() + 108.0f + rowH * (float) i, p.getWidth(), rowH - 2.0f };
}
Rectangle<float> Library::closeTab() const  { const auto b = bookArea(); return { b.getRight() - 178.0f, b.getY() - 22.0f, 150.0f, 34.0f }; }
Rectangle<float> Library::pageButton (int dir) const
{
    const auto p = rightPage().reduced (30.0f, 18.0f);
    return dir < 0 ? Rectangle<float> (p.getX(), p.getBottom() - 24.0f, 80.0f, 24.0f) : Rectangle<float> (p.getRight() - 80.0f, p.getBottom() - 24.0f, 80.0f, 24.0f);
}

void Library::paint (Graphics& g)
{
    const float e = ease (anim);
    const auto full = getLocalBounds().toFloat();
    if (e <= 0.001f) return;
    g.beginTransparencyLayer (e);

    // the room
    if (room.isValid()) g.drawImage (room, full, RectanglePlacement::fillDestination);
    else g.fillAll (Colour (0xffe8a68f));
    g.setGradientFill (ColourGradient (Colour (0x00000000), full.getCentreX(), full.getCentreY(), Colour (0x66200a1c), 0.0f, 0.0f, true));
    g.fillRect (full);

    // Twilight, reading quietly on the floor in the corner; now and then her horn glows as she turns a page
    if (twilight.isValid())
    {
        const float w = 214.0f, h = w * (float) twilight.getHeight() / (float) twilight.getWidth();
        const float breathe = 1.0f + 0.012f * std::sin (t * 1.6f);
        const auto area = Rectangle<float> (14.0f, (float) FreakiePieEditor::designH - 12.0f - h * breathe, w, h * breathe);
        g.setColour (Colours::black.withAlpha (0.22f));
        g.fillEllipse (Rectangle<float> (w * 0.9f, 16.0f).withCentre ({ area.getCentreX() + 6.0f, area.getBottom() - 4.0f }));
        g.setOpacity (1.0f);   // (images draw at the current colour's alpha)
        g.drawImage (twilight, area, RectanglePlacement::stretchToFit);
        const float glow = jmax (0.0f, std::sin (t * 0.7f)) ;
        if (glow > 0.6f)
        {
            const float a = (glow - 0.6f) / 0.4f;
            const Point<float> horn (area.getX() + area.getWidth() * 0.255f, area.getY() + area.getHeight() * 0.17f);
            g.setGradientFill (ColourGradient (Colour (0xffe3a6ff).withAlpha (0.8f * a), horn.x, horn.y, Colour (0x00e3a6ff), horn.x + 22.0f, horn.y, true));
            g.fillEllipse (Rectangle<float> (44.0f, 44.0f).withCentre (horn));
        }
    }

    // the book: a purple cover, two parchment pages, a soft gutter
    const auto b = bookArea();
    g.setColour (Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (b.translated (6.0f, 10.0f), 16.0f);
    g.setColour (Colour (0xff3b1f57));
    g.fillRoundedRectangle (b, 16.0f);
    g.setColour (Colour (0xffe8c46a));
    g.drawRoundedRectangle (b.reduced (5.0f), 13.0f, 1.6f);

    const bool flipping = flipT < 1.0f;
    drawParchment (g, leftPage());
    drawContents (g);
    drawParchment (g, rightPage());
    drawChapter (g, flipping && flipDir < 0 ? flipFrom : chapter);   // turning back, the old page shows until it's covered

    // a page turning over the gutter: forward, the old page lifts off the right and lands on the left;
    // back, a page lifts off the left and lands on the right with the chapter on its other side
    if (flipping)
    {
        const float u = flipT * flipT * (3.0f - 2.0f * flipT);
        const float theta = MathConstants<float>::pi * u, c = std::cos (theta), lift = std::sin (theta);
        const float gx = rightPage().getX();
        const bool onRight = flipDir > 0 ? c > 0.0f : c < 0.0f;
        const bool showsContents = flipDir > 0 ? c < 0.0f : c > 0.0f;
        const float sx = std::abs (c);
        const auto page = onRight ? rightPage() : leftPage();
        // its shadow on the page beneath, darkest while it stands up
        {
            const float w = page.getWidth() * (sx + 0.25f * lift);
            const auto sh = onRight ? page.withWidth (jmin (page.getWidth(), w)) : page.withTrimmedLeft (jmax (0.0f, page.getWidth() - w));
            g.setGradientFill (ColourGradient (Colour (0x00000000).withAlpha (0.0f), onRight ? sh.getRight() : sh.getX(), 0.0f,
                                               Colours::black.withAlpha (0.22f * lift), gx, 0.0f, false));
            g.fillRect (sh);
        }
        if (sx > 0.01f)
        {
            Graphics::ScopedSaveState ss (g);
            g.addTransform (AffineTransform::scale (sx, 1.0f + 0.045f * lift, gx, page.getCentreY()));
            drawParchment (g, page);
            if (showsContents) drawContents (g); else drawChapter (g, flipDir > 0 ? flipFrom : chapter);
            // the sheet darkens as it turns edge-on
            g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.28f * (1.0f - sx)), gx, 0.0f,
                                               Colours::black.withAlpha (0.06f * (1.0f - sx)), onRight ? page.getRight() : page.getX(), 0.0f, false));
            g.fillRoundedRectangle (page, 6.0f);
        }
    }
    {
        // the gutter
        const float gx = rightPage().getX();
        g.setGradientFill (ColourGradient (Colour (0x40000000), gx, 0.0f, Colour (0x00000000), gx + 26.0f, 0.0f, false));
        g.fillRect (Rectangle<float> (gx, b.getY() + 14.0f, 26.0f, b.getHeight() - 28.0f));
        g.setGradientFill (ColourGradient (Colour (0x00000000), gx - 26.0f, 0.0f, Colour (0x40000000), gx, 0.0f, false));
        g.fillRect (Rectangle<float> (gx - 26.0f, b.getY() + 14.0f, 26.0f, b.getHeight() - 28.0f));
    }

    // the ribbon bookmark: back to the shop
    {
        const auto r = closeTab();
        Path ribbon;
        ribbon.addRoundedRectangle (r, 6.0f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillPath (ribbon, AffineTransform::translation (2.0f, 3.0f));
        g.setColour (twiPink);
        g.fillPath (ribbon);
        g.setColour (Colours::white.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (3.0f), 4.0f, 1.0f);
        g.setFont (SadLookAndFeel::uiFont (12.5f, true));
        g.setColour (Colours::white);
        g.drawText ("back to the shop", r, Justification::centred);
    }
    g.endTransparencyLayer();
}

void Library::drawParchment (Graphics& g, Rectangle<float> pg) const
{
    g.setGradientFill (ColourGradient (Colour (0xfffdf6e3), pg.getX(), pg.getY(), Colour (0xfff0deb8), pg.getRight(), pg.getBottom(), false));
    g.fillRoundedRectangle (pg, 6.0f);
}

void Library::drawContents (Graphics& g) const
{
    // left page: the chapters
    const auto lp = leftPage().reduced (34.0f, 26.0f);
    g.setFont (SadLookAndFeel::titleFont (34.0f));
    g.setColour (twiPurple);
    g.drawText ("The Library", lp.withHeight (44.0f), Justification::centred);
    g.setFont (SadLookAndFeel::uiFont (12.0f));
    g.setColour (inkBrown.withAlpha (0.7f));
    g.drawText ("how to play in the balloon shop", lp.withTrimmedTop (44.0f).withHeight (18.0f), Justification::centred);
    for (int i = 0; i < numChapters(); ++i)
    {
        const auto r = chapterRow (i);
        const bool on = i == chapter;
        if (on || i == hoverRow)
        {
            g.setColour ((on ? twiPink : twiPurple).withAlpha (on ? 0.16f : 0.08f));
            g.fillRoundedRectangle (r.reduced (0.0f, 2.0f), 8.0f);
        }
        // a little six-point star, like her cutie mark
        Path star;
        star.addStar (r.getTopLeft().translated (14.0f, r.getHeight() * 0.5f), 6, 2.6f, 6.5f, t * (on ? 0.8f : 0.0f));
        g.setColour (on ? twiPink : twiPurple.withAlpha (0.45f));
        g.fillPath (star);
        g.setFont (SadLookAndFeel::uiFont (jmin (14.0f, r.getHeight() * 0.6f), on));
        g.setColour (on ? twiPurple : inkBrown);
        g.drawText (String (i + 1) + ".  " + helpChapters[i].title, r.withTrimmedLeft (30.0f), Justification::centredLeft);
    }

}

void Library::drawChapter (Graphics& g, int which) const
{
    // right page: the chapter
    const auto rp = rightPage().reduced (30.0f, 18.0f);
    g.setFont (SadLookAndFeel::titleFont (30.0f));
    g.setColour (twiPurple);
    g.drawText (helpChapters[which].title, rp.withHeight (46.0f), Justification::centredLeft);
    g.setColour (twiPink.withAlpha (0.5f));
    g.drawLine (rp.getX(), rp.getY() + 48.0f, rp.getX() + 120.0f, rp.getY() + 48.0f, 1.5f);
    {
        const auto textArea = rp.withTrimmedTop (60.0f).withTrimmedBottom (34.0f);
        TextLayout layout;
        for (float size = 14.0f; size >= 10.5f; size -= 0.5f)   // as big as fits
        {
            AttributedString as;
            as.setLineSpacing (size * 0.28f);
            as.append (helpChapters[which].body, SadLookAndFeel::uiFont (size), inkBrown);
            layout.createLayout (as, textArea.getWidth());
            if (layout.getHeight() <= textArea.getHeight()) break;
        }
        layout.draw (g, textArea);
        if (String (helpChapters[which].title) == "Colours")
        {
            struct Key { int hue; const char* name; };
            const Key keys[] = { { balloonBlue, "the spectral smear" }, { balloonYellow, "the pitch shifter" }, { balloonMint, "the grains" },
                                 { balloonLilac, "the reverb" }, { balloonOrange, "the gumball machine (stutter)" }, { balloonPink, "the loop (main feedback)" },
                                 { balloonTeal, "caught sounds" } };
            float y = textArea.getY() + layout.getHeight() + 18.0f;
            g.setFont (SadLookAndFeel::uiFont (13.5f));
            for (auto& k : keys)
            {
                const auto c = balloonColours (k.hue);
                const auto body = Rectangle<float> (18.0f, 21.0f).withCentre ({ textArea.getX() + 12.0f, y + 10.0f });
                g.setColour (c.fill); g.fillEllipse (body);
                g.setColour (c.line); g.drawEllipse (body, 1.2f);
                g.setColour (Colours::white.withAlpha (0.7f)); g.fillEllipse (body.getX() + 4.0f, body.getY() + 4.0f, 5.0f, 4.0f);
                g.setColour (inkBrown);
                g.drawText (k.name, Rectangle<float> (textArea.getX() + 32.0f, y, textArea.getWidth() - 32.0f, 21.0f), Justification::centredLeft);
                y += 31.0f;
            }
        }
    }
    g.setFont (SadLookAndFeel::uiFont (12.5f, true));
    if (which > 0)                 { g.setColour (twiPurple.withAlpha (0.85f)); g.drawText (String (CharPointer_UTF8 ("\xe2\x80\xb9 back")), pageButton (-1), Justification::centredLeft); }
    if (which < numChapters() - 1) { g.setColour (twiPurple.withAlpha (0.85f)); g.drawText (String (CharPointer_UTF8 ("next \xe2\x80\xba")), pageButton (1), Justification::centredRight); }
    g.setFont (SadLookAndFeel::uiFont (11.0f));
    g.setColour (inkBrown.withAlpha (0.5f));
    g.drawText (String (which + 1) + " / " + String (numChapters()), rp.withTop (rp.getBottom() - 24.0f), Justification::centred);

}

void Library::mouseDown (const MouseEvent& e)
{
    owner.noteActivity();
    if (anim < 0.5f) return;
    const auto p = e.position;
    if (closeTab().contains (p) || ! bookArea().expanded (8.0f, 30.0f).contains (p)) { close(); return; }
    for (int i = 0; i < numChapters(); ++i)
        if (chapterRow (i).contains (p)) { showChapter (i); return; }
    if (chapter > 0 && pageButton (-1).contains (p)) { showChapter (chapter - 1); return; }
    if (chapter < numChapters() - 1 && pageButton (1).contains (p)) { showChapter (chapter + 1); return; }
}

void Library::mouseMove (const MouseEvent& e)
{
    int row = -1;
    for (int i = 0; i < numChapters(); ++i) if (chapterRow (i).contains (e.position)) row = i;
    const bool overButton = row >= 0 || closeTab().contains (e.position)
                         || (chapter > 0 && pageButton (-1).contains (e.position)) || (chapter < numChapters() - 1 && pageButton (1).contains (e.position));
    setMouseCursor (overButton ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    if (row != hoverRow) { hoverRow = row; repaint(); }
}

bool Library::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    if (k == KeyPress::leftKey || k == KeyPress::upKey)    { showChapter (chapter - 1); return true; }
    if (k == KeyPress::rightKey || k == KeyPress::downKey) { showChapter (chapter + 1); return true; }
    return false;
}

// the title, in the show's own lettering (the "Magic is Friendship" fan font, built in): a dark pink
// edge, a white rim, a glossy pink fill and a shine
void FreakiePieEditor::drawTitle (Graphics& g) const
{
    static const Typeface::Ptr face = Typeface::createSystemTypefaceFor (BinaryData::mlp_magic_is_friendship_ttf,
                                                                         (size_t) BinaryData::mlp_magic_is_friendship_ttfSize);
    const Font font = face != nullptr ? Font (FontOptions (face).withHeight (34.0f))
                                      : Font (FontOptions ("Arial Rounded MT Bold", 30.0f, Font::plain));
    const String text ("Pinkie Pie's Balloon Shop");
    GlyphArrangement ga;
    // in the clear space between the card (left) and the weather buttons (right)
    ga.addFittedText (font, text, 280.0f, 10.0f + 1.5f * std::sin ((float) clock * 1.2f), 436.0f, 44.0f, Justification::centred, 1);
    Path p;
    ga.createPath (p);
    const auto b = p.getBounds();
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillPath (p, AffineTransform::translation (0.0f, 4.0f));
    g.setColour (Colour (0xffb83c7e));
    g.strokePath (p, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::white);
    g.strokePath (p, PathStrokeType (5.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setGradientFill (ColourGradient (Colour (0xffffb3d6), 0.0f, b.getY(), Colour (0xffff5fa8), 0.0f, b.getBottom(), false));
    g.fillPath (p);
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (p);
    g.setColour (Colours::white.withAlpha (0.45f));
    g.fillRect (b.withHeight (b.getHeight() * 0.38f).translated (0.0f, 1.0f));
}

// the reset button: a little circular arrow
void FreakiePieEditor::drawResetButton (Graphics& g) const
{
    const auto r = resetButton;
    const Point<float> c (r.getX() + 16.0f, r.getCentreY());
    Path arc;
    arc.addCentredArc (c.x, c.y, 8.0f, 8.0f, 0.0f, 0.6f, MathConstants<float>::twoPi - 0.3f, true);
    g.setColour (palette::blush.withAlpha (0.85f));
    g.strokePath (arc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    Path head;
    const Point<float> tip (c.x + 8.0f * std::sin (0.6f), c.y - 8.0f * std::cos (0.6f));
    head.addTriangle (tip.x - 4.5f, tip.y - 3.0f, tip.x + 3.5f, tip.y - 4.0f, tip.x + 0.5f, tip.y + 4.0f);
    g.fillPath (head);
    g.setFont (SadLookAndFeel::uiFont (11.0f, true));
    g.drawText ("reset", r.withTrimmedLeft (30.0f), Justification::centredLeft);
}

void FreakiePieEditor::resetEverything()
{
    proc.resetToNormal();
    layoutShelf (shelfItems());
    confetti (resetButton.getCentre(), 30);
    for (auto* f : floaters) f->squashVel += 3.0f;
    if (chalkboard != nullptr) chalkboard->repaint();
}

//==============================================================================
// Trixie
Rectangle<float> FreakiePieEditor::trixieArea() const
{
    const float h = 230.0f, w = trixieImg.isValid() ? h * (float) trixieImg.getWidth() / (float) trixieImg.getHeight() : 220.0f;
    const auto& v = trixieVisit;
    // in from the right (1 s), a strut on the spot while she works, out again to the right
    float x = 720.0f;
    if (v.t < 1.0f) x = 720.0f + ((float) designW + 140.0f - 720.0f) * std::pow (1.0f - v.t, 2.0f);
    if (v.t > 5.8f) x = 720.0f + ((float) designW + 140.0f - 720.0f) * fp::smoothstep ((v.t - 5.8f) / 1.2f);
    const float hop = v.t < 1.0f || v.t > 5.8f ? std::abs (std::sin (v.t * 9.0f)) * 10.0f : 0.0f;
    return { x - w * 0.5f, (float) designH - 4.0f - h - hop, w, h };
}

Point<float> FreakiePieEditor::trixieAmulet() const
{
    const auto a = trixieArea();
    return { a.getX() + a.getWidth() * 0.37f, a.getY() + a.getHeight() * 0.585f };
}

Path FreakiePieEditor::makeBolt (Point<float> from, Point<float> to, float jag)
{
    Path p;
    p.startNewSubPath (from);
    const auto d = to - from;
    const float len = jmax (1.0f, d.getDistanceFromOrigin());
    const Point<float> perp (-d.y / len, d.x / len);
    const int segs = jmax (4, (int) (len / 40.0f));
    for (int i = 1; i <= segs; ++i)
    {
        const float u = (float) i / (float) segs;
        p.lineTo (from + d * u + perp * (i == segs ? 0.0f : (rnd.nextFloat() - 0.5f) * jag));
    }
    return p;
}

void FreakiePieEditor::startTrixie()
{
    if (trixieVisit.on) return;
    trixieVisit = {};
    trixieVisit.on = true;
    trixieVisit.nextZap = 1.2f;
    // sometimes she plays something strange into the input while she's here, and sometimes she doesn't
    trixieVisit.noisy = rnd.nextBool();
    trixieVisit.gifts = rnd.nextInt (4);   // and she may bring a few effects from the store along
    if (trixieVisit.noisy) proc.trixieNoise.request ((uint32_t) rnd.nextInt(), 6.6f);
}

void FreakiePieEditor::trixieStep (float dt)
{
    // once a minute, a small chance she drops by (not while you're reading or drawing)
    if (! trixieVisit.on && clock >= nextTrixieRoll)
    {
        nextTrixieRoll = clock + 60.0;
        const bool busy = (library != nullptr && library->isVisible()) || (chalkboard != nullptr && chalkboard->isVisible());
        if (! busy && rnd.nextFloat() < trixieChancePerMinute) startTrixie();
    }
    auto& v = trixieVisit;
    v.boltAge += dt;
    v.flash = jmax (0.0f, v.flash - dt * 1.5f);
    if (! v.on) return;
    v.t += dt;
    noteActivity();

    // zap! a balloon she can reach jumps to a random value
    if (v.t >= v.nextZap && v.t < 5.0f)
    {
        v.nextZap += 0.3f;
        // ...or, now and then, an effect from the store appears on the shelf where the bolt lands
        if (v.gifts > 0 && (rnd.nextFloat() < 0.3f || v.t > 4.4f))
        {
            std::vector<int> notYet;
            for (int e = 0; e < kStoreItems; ++e) if (! storeShown[e]) notYet.push_back (e);
            if (! notYet.empty())
            {
                const int e = notYet[(size_t) rnd.nextInt ((int) notYet.size())];
                trixieGiftAt = { shelfLeft + rnd.nextFloat() * (shelfRight - shelfLeft), shelfY - 60.0f };
                v.bolt = makeBolt (trixieAmulet(), trixieGiftAt, 34.0f);
                v.boltAge = 0.0f;
                trixieGifts |= 1u << e;
                setBought (e, true);
                if (v.noisy) proc.trixieNoise.zap();
                confetti (trixieGiftAt, 24);
                --v.gifts;
                ++v.zaps;
                return;
            }
            v.gifts = 0;
        }
        std::vector<Balloon*> targets;
        for (auto* f : floaters)
            if (auto* b = dynamic_cast<Balloon*> (f))
                if (b->alive && FreakiePieProcessor::trixieMayTouch (b->parameter().getParameterID())) targets.push_back (b);
        if (! targets.empty())
        {
            auto* b = targets[(size_t) rnd.nextInt ((int) targets.size())];
            v.bolt = makeBolt (trixieAmulet(), b->pos, 34.0f);
            v.boltAge = 0.0f;
            FreakiePieProcessor::randomise (b->parameter(), rnd);
            if (v.noisy) proc.trixieNoise.zap();
            b->squashVel += 6.0f;
            b->vel += { (rnd.nextFloat() - 0.5f) * 500.0f, -300.0f };
            for (int i = 0; i < 14; ++i)
            {
                static const Colour stars[] = { Colour (0xff7fe8ff), Colour (0xfff7e36b), Colour (0xffb48cff), Colours::white };
                particles.push_back ({ b->pos, { (rnd.nextFloat() - 0.5f) * 300.0f, -rnd.nextFloat() * 280.0f },
                                       rnd.nextFloat() * 6.0f, (rnd.nextFloat() - 0.5f) * 14.0f, 1.0f, stars[rnd.nextInt (4)], 0 });
            }
            ++v.zaps;
        }
    }
    // and the grand finale: everything at once
    if (! v.finale && v.t >= 5.0f)
    {
        v.finale = true;
        v.flash = 1.0f;
        proc.trixieShuffle (rnd);
        scaredUntil = jmax (scaredUntil, clock + 1.2);
        for (auto* f : floaters) { f->squashVel += 5.0f; f->vel += { (rnd.nextFloat() - 0.5f) * 400.0f, -250.0f }; }
        confetti (trixieAmulet(), 60);
    }
    if (v.t >= 7.0f) v.on = false;
}

void FreakiePieEditor::drawTrixie (Graphics& g) const
{
    const auto& v = trixieVisit;
    if (v.flash > 0.0f)
    {
        g.setColour (Colour (0xffcfe8ff).withAlpha (0.45f * v.flash * v.flash));
        g.fillRect (Rectangle<float> (0, 0, (float) designW, (float) designH));
    }
    if (! v.on) return;
    const auto a = trixieArea();
    // the amulet glows while she works
    const auto am = trixieAmulet();
    const float glow = v.t > 1.0f && v.t < 5.4f ? 0.6f + 0.4f * std::sin (v.t * 18.0f) : 0.0f;
    if (glow > 0.0f)
    {
        g.setGradientFill (ColourGradient (Colour (0xff9ff3ff).withAlpha (0.9f * glow), am.x, am.y, Colour (0x009ff3ff), am.x + 40.0f, am.y, true));
        g.fillEllipse (Rectangle<float> (80.0f, 80.0f).withCentre (am));
    }
    if (trixieImg.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImage (trixieImg, a, RectanglePlacement::stretchToFit);
    }
    if (v.boltAge < 0.18f)
    {
        const float k = 1.0f - v.boltAge / 0.18f;
        g.setColour (Colour (0xff7fe8ff).withAlpha (0.6f * k));
        g.strokePath (v.bolt, PathStrokeType (7.0f, PathStrokeType::mitered, PathStrokeType::rounded));
        g.setColour (Colours::white.withAlpha (k));
        g.strokePath (v.bolt, PathStrokeType (2.2f, PathStrokeType::mitered, PathStrokeType::rounded));
    }
    // music notes drifting up from her while she plays her noise
    if (v.noisy && v.t > 0.4f && v.t < 6.6f)
    {
        g.setFont (SadLookAndFeel::uiFont (20.0f, true));
        for (int i = 0; i < 5; ++i)
        {
            const float u = std::fmod (v.t * 0.7f + (float) i * 0.21f, 1.0f);
            const Point<float> p0 (a.getX() + a.getWidth() * 0.18f, a.getY() + a.getHeight() * 0.42f);
            const Point<float> p (p0.x - 30.0f * u + 14.0f * std::sin (u * 9.0f + (float) i), p0.y - 120.0f * u);
            static const Colour cols[] = { Colour (0xff7fe8ff), Colour (0xfff7e36b), Colour (0xffb48cff) };
            g.setColour (cols[i % 3].withAlpha (1.0f - u));
            g.drawText (String (CharPointer_UTF8 (i % 2 == 0 ? "\xe2\x99\xaa" : "\xe2\x99\xab")), Rectangle<float> (24.0f, 24.0f).withCentre (p), Justification::centred);
        }
    }
    // what she says
    const String line = v.finale ? "Ta-da!" : (v.noisy ? "Behold, the Great and Powerful Trixie's music!" : "Behold, the Great and Powerful Trixie!");
    if (v.t > 0.6f && v.t < 6.4f)
    {
        g.setFont (SadLookAndFeel::uiFont (13.0f, true));
        const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), line) + 28.0f;
        auto bubble = Rectangle<float> (tw, 30.0f).withCentre ({ a.getCentreX() - 40.0f, a.getY() - 6.0f });
        bubble = bubble.withX (jlimit (8.0f, (float) designW - tw - 8.0f, bubble.getX()));
        Path p;
        p.addRoundedRectangle (bubble, 14.0f);
        p.addTriangle (bubble.getCentreX() + 10.0f, bubble.getBottom() - 1.0f, bubble.getCentreX() + 30.0f, bubble.getBottom() - 1.0f, bubble.getCentreX() + 34.0f, bubble.getBottom() + 14.0f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillPath (p, AffineTransform::translation (2.0f, 3.0f));
        g.setColour (Colour (0xfff4efff));
        g.fillPath (p);
        g.setColour (Colour (0xff8a5cd8));
        g.strokePath (p, PathStrokeType (2.0f));
        g.setColour (Colour (0xff4b2a8a));
        g.drawText (line, bubble, Justification::centred);
    }
}

// the way in: a little closed book, top right
void FreakiePieEditor::drawHelpButton (Graphics& g) const
{
    const auto r = helpButton;
    const auto book = Rectangle<float> (20.0f, 24.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() + 1.0f + 1.5f * std::sin ((float) clock * 1.3f) });
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (book.translated (1.5f, 2.0f), 3.0f);
    g.setColour (Colour (0xff6b3f8f));
    g.fillRoundedRectangle (book, 3.0f);
    g.setColour (Colour (0xfffdf6e3));
    g.fillRect (book.withTrimmedLeft (book.getWidth() - 4.0f).reduced (0.0f, 2.0f));
    Path star;
    star.addStar (book.getCentre().translated (-1.0f, 0.0f), 6, 2.2f, 5.5f);
    g.setColour (Colour (0xffe5409a));
    g.fillPath (star);
    g.setFont (SadLookAndFeel::uiFont (11.0f, true));
    g.setColour (palette::blush.withAlpha (0.85f));
    g.drawText ("help", r.withTrimmedLeft (30.0f), Justification::centredLeft);
}

// the pop sounds switch, in the same style as help and reset: a little party blower, unrolled when they're on
void FreakiePieEditor::drawPopsButton (Graphics& g) const
{
    const auto r = popsButton;
    const bool on = popsValue > 0.5f;
    const Point<float> c (r.getX() + 16.0f, r.getCentreY());
    g.setColour (palette::blush.withAlpha (0.85f));
    g.fillRoundedRectangle (c.x - 9.0f, c.y - 2.5f, 5.0f, 5.0f, 1.5f);   // the mouthpiece
    if (on)
    {
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i % 2 == 0 ? Colour (0xffe5409a) : Colour (0xffffd65c));
            g.fillRect (c.x - 4.0f + 3.2f * (float) i, c.y - 2.0f, 3.2f, 4.0f);
        }
        Path tip;
        tip.addCentredArc (c.x + 9.0f, c.y + 1.0f, 2.5f, 3.0f, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::pi, true);
        g.setColour (Colour (0xffe5409a));
        g.strokePath (tip, PathStrokeType (1.6f));
    }
    else
    {
        Path curl;
        curl.addCentredArc (c.x + 1.0f, c.y, 4.0f, 4.0f, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::twoPi, true);
        g.setColour (Colour (0xffe5409a).withAlpha (0.6f));
        g.strokePath (curl, PathStrokeType (1.8f));
    }
    g.setFont (SadLookAndFeel::uiFont (11.0f, true));
    g.setColour (palette::blush.withAlpha (on ? 0.85f : 0.55f));
    g.drawText (on ? "pop sounds on" : "pop sounds off", r.withTrimmedLeft (30.0f), Justification::centredLeft);
}

//==============================================================================
// Presets: pictures, colours
const Image& FreakiePieEditor::background (int i)
{
    static const Image none;
    if (! isPositiveAndBelow (i, presets::numBackgrounds)) return none;
    if (! backgrounds[i].isValid())
    {
        int size = 0;
        const String name = "bg_" + String (i).paddedLeft ('0', 2) + "_jpg";
        if (auto* data = BinaryData::getNamedResource (name.toRawUTF8(), size))
            backgrounds[i] = ImageCache::getFromMemory (data, size);
    }
    return backgrounds[i];
}

FreakiePieEditor::Theme FreakiePieEditor::themeOf (int bg)
{
    if (bg < 0) return { Colour (0xff171226), Colour (0xffe79ab9), Colour (0xfff6c9dc) };   // the original night sky
    const auto& t = presets::themeFor (bg);
    return { Colour (t.deep), Colour (t.accent), Colour (t.text) };
}

void FreakiePieEditor::followPreset (float dt)
{
    const int idx = proc.currentPreset.load();
    if (idx != shownPreset)
    {
        const int bg = isPositiveAndBelow (idx, (int) presets::list().size()) ? presets::list()[(size_t) idx].background : -1;
        if (shownPreset == -2) { bgFrom = bgTo = bg; bgFade = 1.0f; themeFrom = themeTo = theme = themeOf (bg); }
        else { bgFrom = bgTo; themeFrom = theme; bgTo = bg; themeTo = themeOf (bg); bgFade = 0.0f; }
        shownPreset = idx;
        presetBox.setSelectedId (idx + 1, dontSendNotification);
    }
    if (bgFade < 1.0f)
    {
        bgFade = jmin (1.0f, bgFade + dt / 1.4f);   // a slow cross-fade
        const float u = ease (bgFade);
        theme = { themeFrom.deep.interpolatedWith (themeTo.deep, u), themeFrom.accent.interpolatedWith (themeTo.accent, u),
                  themeFrom.text.interpolatedWith (themeTo.text, u) };
    }
}

void FreakiePieEditor::applyTheme()
{
    palette::ink = theme.deep;
    palette::slate = theme.deep.brighter (0.25f);
    palette::dustyPink = theme.accent;
    palette::rose = theme.accent.darker (0.35f);
    palette::blush = theme.text;
    palette::tearBlue = theme.text.interpolatedWith (theme.accent, 0.45f);
    palette::faded = theme.text.withMultipliedSaturation (0.5f).darker (0.2f);
    lnf.setColour (ComboBox::textColourId, palette::blush);
    lnf.setColour (ComboBox::arrowColourId, palette::dustyPink);
    lnf.setColour (PopupMenu::textColourId, palette::blush.withAlpha (0.9f));
    lnf.setColour (PopupMenu::highlightedBackgroundColourId, palette::rose.withAlpha (0.55f));
    lnf.setColour (PopupMenu::backgroundColourId, theme.deep.withAlpha (0.94f));
    storeLook.setColour (ComboBox::textColourId, palette::blush);      // the preset box itself matches the rest;
    storeLook.setColour (ComboBox::arrowColourId, palette::dustyPink);  // only its menu is the sweet shop
}

String FreakiePieEditor::pillText (const Pill& pl) const
{
    if (pl.continuous) return pl.name + ": " + pl.param->getCurrentValueAsText();
    if (auto* c = dynamic_cast<AudioParameterChoice*> (pl.param))
        return pl.name + ": " + c->choices[jlimit (0, c->choices.size() - 1, roundToInt (pl.value))];
    return pl.name + (pl.value > 0.5f ? ": on" : ": off");
}

void FreakiePieEditor::popPumpBalloon (Pump& pmp, bool sound)
{
    const auto sh = pumpBalloonShape (pmp);
    pop ({ sh.cx, sh.cy }, jmin (1.0f, pmp.size), pmp.hue);
    if (sound)
    {
        if (pmp.slot >= 0) proc.balloonFX.voice[pmp.slot].mode = 0;   // the capture is gone
        pmp.spent = true;
        pmp.slot = -1;
    }
    else
    {
        static const int hues[] = { balloonOrange, balloonPink, balloonBlue, balloonYellow, balloonLilac, balloonMint };
        pmp.hue = hues[(size_t) rnd.nextInt (6)];
    }
    pmp.holdTime = 0.0f; pmp.size = 0.0f; pmp.strain = 0.0f;
    pmp.popAt = 5.0f + rnd.nextFloat() * 7.0f;
}

void FreakiePieEditor::pinStep (float dt)
{
    if (pin == nullptr) return;
    if (! pin->dragging)
    {
        pin->pos += (pin->home - pin->pos) * jmin (1.0f, dt * 9.0f);   // back to the cushion
        pin->place();
        return;
    }
    const auto t = pin->tip();
    // in the sphere, only the nearest balloon under the tip feels it (not the ones behind it), except
    // that a caught sound is always popped: the pin slips past the controls to reach it
    Floater* front = nullptr;
    bool frontIsSound = false;
    for (auto* f : floaters)
    {
        if (! f->alive || t.getDistanceFrom (f->pos) > f->collisionRadius() * 0.95f * f->scale) continue;
        const bool isSound = dynamic_cast<SoundBalloon*> (f) != nullptr;
        if (front == nullptr || (isSound && ! frontIsSound) || (isSound == frontIsSound && f->depth > front->depth)) { front = f; frontIsSound = isSound; }
    }
    for (auto* f : floaters)
    {
        if (f != front) continue;
        if (auto* sb = dynamic_cast<SoundBalloon*> (f))
        {
            proc.balloonFX.voice[sb->slot].mode = 0;    // pop: its frozen sound stops
            pop (sb->pos, sb->size, sb->hue);
            sb->alive = false;
            sb->setVisible (false);
            continue;
        }
        // an effect from the store pops too (with the rest of its bunch: the effect goes back on the shelf)
        int bought = -1;
        for (int e = 0; e < kStoreItems && bought < 0; ++e)
            for (auto* b : storeBalloons[e]) if (b == f) bought = e;
        if (bought >= 0)
        {
            if (bought != stutterItem) proc.popStruckRequest.fetch_or (1u << bought);
            setBought (bought, false);
            continue;
        }
        else
        {
            // control balloons don't pop: they flinch away
            auto away = f->pos - t;
            const float d = jmax (1.0f, away.getDistanceFromOrigin());
            f->vel += away / d * 520.0f;
            f->squashVel += 4.0f;
        }
    }
    auto inside = [&t, this] (const Pump& pmp)
    {
        if (pmp.size < 0.03f) return false;
        const auto sh = pumpBalloonShape (pmp);
        const float dx = (t.x - sh.cx) / sh.rx, dy = (t.y - sh.cy) / sh.ry;
        return dx * dx + dy * dy < 1.0f;
    };
    if (inside (popPump)) popPumpBalloon (popPump, false);
    if (soundPump.slot >= 0 && ! soundPump.spent && inside (soundPump)) popPumpBalloon (soundPump, true);
}

int FreakiePieEditor::freeSlot() const
{
    for (int i = 0; i < fp::BalloonFX::kVoices; ++i)
    {
        if (soundPump.slot == i) continue;
        if (proc.balloonFX.voice[i].mode.load() != 0 || proc.balloonFX.voice[i].audioMode.load() != 0) continue;
        bool used = false;
        for (int k = numParamBalloons; k < floaters.size(); ++k)
            if (auto* s = dynamic_cast<SoundBalloon*> (floaters[k])) used |= (s->alive && s->slot == i);
        if (! used) return i;
    }
    return -1;
}

void FreakiePieEditor::startSoundPump()
{
    const int slot = freeSlot();
    if (slot < 0) { fullFlash = 1.0f; return; }
    auto& v = proc.balloonFX.voice[slot];
    v.semis = nextSemis;
    v.size = 0.0f;
    v.mode = 1;                                    // start recording into this balloon
    soundPump.slot = slot;
    soundPump.holding = true;
    soundPump.spent = false;
    soundPump.holdTime = 0.0f;
    soundPump.size = 0.0f;
    soundPump.strain = 0.0f;
    soundPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
}

void FreakiePieEditor::releaseSoundPump()
{
    auto& pmp = soundPump;
    if (pmp.slot >= 0 && ! pmp.spent)
    {
        auto& v = proc.balloonFX.voice[pmp.slot];
        if (pmp.holdTime < 0.15f) v.mode = 0;      // too quick to catch anything
        else
        {
            v.mode = 2;                            // tie it off: freeze and play
            const auto sh = pumpBalloonShape (pmp);
            auto* b = new SoundBalloon (*this, pmp.slot, pmp.size, nextSemis, pmp.hue);
            b->placed3d = false;
            b->pos = { sh.cx, sh.cy };
            b->vel = { -80.0f - rnd.nextFloat() * 60.0f, -260.0f };
            b->lean = (rnd.nextFloat() - 0.5f) * 0.6f;
            floaters.add (b);
            content.addAndMakeVisible (b);
            b->place();
            if (pin != nullptr) pin->toFront (false);
            static const int hues[] = { balloonTeal, balloonOrange, balloonBlue, balloonLilac, balloonYellow, balloonMint, balloonPink };
            pmp.hue = hues[(size_t) (++nextHue % 7)];
        }
    }
    pmp.slot = -1;
    pmp.holding = false;
    pmp.spent = false;
    pmp.holdTime = 0.0f;
    pmp.size = 0.0f;
    pmp.strain = 0.0f;
}

void FreakiePieEditor::contentMouse (const MouseEvent& e, int phase)
{
    const auto p = toContent (e);
    noteActivity();
    if (phase == 0 && chalkboard != nullptr && chalkboard->restArea().contains (p) && ! chalkboard->isVisible())
    {
        chalkboard->bringForward();
        return;
    }
    if (phase == 0 && presetStrip != nullptr && presetStrip->isOpen())
    {
        presetStrip->close();   // a click anywhere else puts the filmstrip away
        return;
    }
    if (phase == 0 && storeShelves != nullptr && storeShelves->isOpen() && ! storeSign.contains (p))
    {
        storeShelves->close();   // a click anywhere else puts the store away
        return;
    }
    if (phase == 0 && fadePopup != nullptr && fadePopup->isOpen())
    {
        fadePopup->close();   // a click anywhere else puts the fade times away
        return;
    }
    if (phase == 0)
    {
        for (auto* pl : pills)
            if (pl->r.contains (p))
            {
                if (e.mods.isPopupMenu()) return;
                if (pl->continuous)
                {
                    dragPill = pl;
                    dragStartNorm = pl->param->convertTo0to1 (pl->value);
                    dragStartY = p.y;
                    pl->att->beginGesture();
                }
                else clickPill (*pl, 1);
                return;
            }
        if (helpButton.contains (p) && library != nullptr) { library->open(); library->grabKeyboardFocus(); }
        else if (resetButton.contains (p)) resetEverything();
        else if (popsButton.contains (p) && popsAtt != nullptr)
        {
            if (e.mods.isPopupMenu()) openPopsMenu();
            else { popsAtt->setValueAsCompleteGesture (popsValue > 0.5f ? 0.0f : 1.0f); confetti (popsButton.getCentre(), 8); }
        }
        else if (storeSign.contains (p)) openStore();
        else if (tankArea.contains (p))
        {
            // a click turns the helium on and off; a drag slides the canister along its rail
            tankDragging = true; tankMoved = false;
            tankGrabX = p.x - tankArea.getX(); tankDownX = p.x;
        }
        else if (nextBadge.contains (p)) showPitchBubble (nullptr);
        else if (popPump.area.contains (p)) { popPump.holding = true; }
        else if (soundPump.area.contains (p)) startSoundPump();
        else if (jarArea.contains (p))
        {
            // the jar of grains: click for one more, drag one into the sphere to drop it there, right-click to take one out
            if (e.mods.isPopupMenu()) removeGrain();
            else { jarDragging = true; jarDragAt = p; }
        }
        else
        {
            bool grabbed = false;
            for (auto& a : anchors)
                if (! grabbed && ! a.fixed && ! a.hidden && ! a.onShelf && p.getDistanceFrom (a.pos + Point<float> (0.0f, a.box ? 12.0f : 0.0f)) < 30.0f * a.scale)
                    { a.dragging = true; a.grab = a.pos - p; a.target = a.pos; grabbed = true; }
            if (! grabbed && insideSphere (p))
            {
                // the empty glass: turn the sphere round to look at it from anywhere
                rotating = true; rotateFrom = p; yawFrom = viewYaw; pitchFrom = viewPitch;
            }
            else if (! grabbed) confetti (p, 7);                // a little sparkle wherever you click the sky
        }
    }
    else if (phase == 1 && rotating)
    {
        viewYaw = yawFrom + (p.x - rotateFrom.x) * 0.009f;
        viewPitch = jlimit (-1.3f, 1.4f, pitchFrom + (p.y - rotateFrom.y) * 0.007f);
    }
    else if (phase == 1 && jarDragging) jarDragAt = p;
    else if (phase == 1 && tankDragging)
    {
        if (! tankMoved && std::abs (p.x - tankDownX) > 4.0f)
        {
            tankMoved = true;
            if (thruAtt != nullptr) thruAtt->beginGesture();
        }
        if (tankMoved)
        {
            tankArea.setX (jlimit (railLeft, railRight, p.x - tankGrabX));
            if (thruAtt != nullptr) thruAtt->setValueAsPartOfGesture ((railRight - tankArea.getX()) / (railRight - railLeft));
        }
    }
    else if (phase == 1 && dragPill != nullptr)
    {
        const float n = jlimit (0.0f, 1.0f, dragStartNorm - (p.y - dragStartY) / 120.0f);   // up = more, like a knob
        dragPill->att->setValueAsPartOfGesture (dragPill->param->convertFrom0to1 (n));
    }
    else if (phase == 1)
    {
        for (auto& a : anchors) if (a.dragging) a.target = p + a.grab;
    }
    else
    {
        if (dragPill != nullptr) { dragPill->att->endGesture(); dragPill = nullptr; }
        if (tankDragging)
        {
            tankDragging = false;
            if (tankMoved) { if (thruAtt != nullptr) thruAtt->endGesture(); }
            else if (heliumAtt != nullptr)
            {
                heliumAtt->setValueAsCompleteGesture (heliumOn() ? 0.0f : 1.0f);
                if (heliumOn())
                {
                    for (auto* f : floaters) f->squashVel -= 2.5f;   // whoosh
                    confetti ({ pinkieX, (float) designH - 70.0f }, 30);   // and Pinkie turns to crystal in a burst of sparkles
                }
            }
        }
        rotating = false;
        if (jarDragging)
        {
            jarDragging = false;
            if (insideSphere (p)) { const auto at = unproject (p, 0.0f) * (1.0f / sphereR); addGrain (&at); }
            else if (jarArea.contains (p)) addGrain (nullptr);
        }
        popPump.holding = false;
        if (soundPump.holding) releaseSoundPump();
        bool movedShelf = false;
        for (auto& a : anchors) { movedShelf |= a.dragging && a.onShelf; a.dragging = false; }
        if (movedShelf) commitShelf();
    }
}

void FreakiePieEditor::contentWheel (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto p = toContent (e);
    for (auto* pl : pills)
        if (pl->r.contains (p))
        {
            int dir = 0;
            if (pl->continuous)
            {
                float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
                if (w.isReversed) d = -d;
                const float n = jlimit (0.0f, 1.0f, pl->param->convertTo0to1 (pl->value) + d * 0.3f);
                pl->att->setValueAsCompleteGesture (pl->param->convertFrom0to1 (n));
            }
            else if (dynamic_cast<AudioParameterChoice*> (pl->param) != nullptr && wheelStep (nextWheelAcc, w, dir)) clickPill (*pl, dir);
            return;
        }
    if (jarArea.contains (p))
    {
        int dir = 0;
        if (wheelStep (nextWheelAcc, w, dir)) { if (dir > 0) addGrain (nullptr); else removeGrain(); }
        return;
    }
    if (! soundPump.area.expanded (40.0f, 0.0f).contains (p) && ! nextBadge.contains (p)) return;
    int dir = 0;
    if (wheelStep (nextWheelAcc, w, dir)) nextSemis = stepSemis (nextSemis, dir);
}

// Trackpads send lots of tiny deltas: collect them and move one note per deliberate flick.
bool FreakiePieEditor::wheelStep (float& acc, const MouseWheelDetails& w, int& direction)
{
    float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (w.isReversed) d = -d;
    if (std::abs (d) >= 0.2f && ! w.isSmooth) { acc = 0.0f; direction = d > 0 ? 1 : -1; return true; }  // a mouse wheel notch
    acc += d;
    if (std::abs (acc) < 0.3f) return false;
    direction = acc > 0 ? 1 : -1;
    acc = 0.0f;
    return true;
}

// Pop up a speech bubble at the balloon (or the pump's "next" badge) to type an exact interval.
void FreakiePieEditor::showPitchBubble (SoundBalloon* target)
{
    if (pitchBubble == nullptr)
    {
        pitchBubble = std::make_unique<PitchBubble>();
        content.addChildComponent (*pitchBubble);
    }
    Component::SafePointer<SoundBalloon> safe (target);
    const bool forNext = target == nullptr;
    pitchBubble->onCommit = [this, safe, forNext] (float st)
    {
        if (forNext) { nextSemis = st; confetti (nextBadge.getCentre(), 8); return; }
        if (auto* b = safe.getComponent())
        {
            b->semis = st;
            proc.balloonFX.voice[b->slot].semis = st;
            b->squashVel += 3.0f;
            confetti (b->pos, 10);
            b->repaint();
        }
    };
    pitchBubble->onClose = [this] { if (pitchBubble != nullptr) pitchBubble->setVisible (false); };

    const auto bounds = Rectangle<float> (0, 0, (float) designW, (float) designH);
    if (forNext)
        pitchBubble->open ({ nextBadge.getCentreX(), nextBadge.getY() - 2.0f }, nextSemis, "next balloon's pitch", bounds);
    else
    {
        const auto sh = balloonShape ({ 0, 0, (float) Floater::S, (float) Floater::S }, target->size);
        pitchBubble->open ({ target->pos.x, target->pos.y - sh.ry - 4.0f }, target->semis, "this balloon's pitch", bounds);
    }
}

//==============================================================================
// the push pin: a red glass head and a steel shaft, tip at the bottom left
void PinTool::paint (Graphics& g)
{
    const Point<float> head (30.0f, 13.0f), tipP (8.0f, 58.0f);
    if (dragging)   // its shadow, a little below and to the side
    {
        g.setColour (Colours::black.withAlpha (0.25f));
        g.drawLine (Line<float> (head.translated (5.0f, 8.0f), tipP.translated (5.0f, 8.0f)), 3.0f);
    }
    // shaft
    g.setGradientFill (ColourGradient (Colour (0xfff4f6fa), head.x - 2.0f, head.y, Colour (0xff7d8494), head.x + 2.0f, head.y + 2.0f, false));
    g.drawLine (Line<float> (head, tipP), 2.4f);
    g.setColour (Colours::white.withAlpha (0.8f));
    g.drawLine (Line<float> (head.translated (-0.8f, 0.0f), tipP.translated (-0.6f, -2.0f)), 0.7f);
    // head: a glossy red bead with a collar
    auto collar = Rectangle<float> (12.0f, 5.0f).withCentre (head.translated (-4.0f, 8.0f));
    g.setColour (Colour (0xffb01f3f));
    g.fillPath ([&] { Path p; p.addRoundedRectangle (collar, 2.0f); p.applyTransform (AffineTransform::rotation (-0.85f, collar.getCentreX(), collar.getCentreY())); return p; }());
    auto bead = Rectangle<float> (20.0f, 20.0f).withCentre (head);
    g.setGradientFill (ColourGradient (Colour (0xffff7a8e), bead.getX() + 6.0f, bead.getY() + 5.0f, Colour (0xff8e0f2c), bead.getRight(), bead.getBottom(), true));
    g.fillEllipse (bead);
    g.setColour (Colours::white.withAlpha (0.85f));
    g.fillEllipse (bead.getX() + 5.0f, bead.getY() + 4.0f, 6.0f, 4.0f);
}

bool PinTool::hitTest (int x, int y)
{
    // grab it by the head or the shaft
    const Point<float> p ((float) x, (float) y), head (30.0f, 13.0f), tipP (8.0f, 58.0f);
    if (p.getDistanceFrom (head) < 14.0f) return true;
    Point<float> nearest;
    return Line<float> (head, tipP).getDistanceFromPoint (p, nearest) < 6.0f;
}

void PinTool::mouseDown (const MouseEvent& e)
{
    dragging = true;
    grab = pos - owner.toContent (e);
    toFront (false);
}

void PinTool::mouseDrag (const MouseEvent& e)
{
    pos = owner.toContent (e) + grab;
    place();
}

//==============================================================================
PitchBubble::PitchBubble()
{
    field.setFont (SadLookAndFeel::uiFont (24.0f, true));
    field.setJustification (Justification::centred);
    field.setInputRestrictions (3, "+-0123456789");
    field.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    field.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    field.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    field.setColour (TextEditor::textColourId, Colour (0xff7a2350));
    field.setColour (TextEditor::highlightColourId, Colour (0xffff74ad).withAlpha (0.35f));
    field.setColour (CaretComponent::caretColourId, Colour (0xff7a2350));
    field.setTextToShowWhenEmpty ("+7", Colour (0xff7a2350).withAlpha (0.35f));
    field.addListener (this);
    addAndMakeVisible (field);
    setSize (170, 92);
}

void PitchBubble::open (Point<float> tip, float current, const String& t, Rectangle<float> within)
{
    title = t;
    closing = false;
    // sit above what we point at, or below it if there's no room
    tailDown = tip.y - (float) getHeight() - 6.0f > within.getY();
    auto r = Rectangle<float> ((float) getWidth(), (float) getHeight())
                 .withCentre ({ tip.x, tailDown ? tip.y - (float) getHeight() * 0.5f - 2.0f : tip.y + (float) getHeight() * 0.5f + 2.0f });
    r = r.constrainedWithin (within.reduced (4.0f));
    tailX = jlimit (24.0f, (float) getWidth() - 24.0f, tip.x - r.getX());
    setBounds (r.toNearestInt());
    const int st = roundToInt (current);
    field.setText (st == 0 ? "0" : (st > 0 ? "+" : "-") + String (std::abs (st)), false);
    setVisible (true);
    toFront (true);
    field.grabKeyboardFocus();
    field.selectAll();
    repaint();
}

void PitchBubble::resized()
{
    auto body = getLocalBounds().reduced (6).withTrimmedTop (tailDown ? 0 : 12).withTrimmedBottom (tailDown ? 12 : 0);
    field.setBounds (body.withTrimmedTop (22).reduced (14, 4));
}

void PitchBubble::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f).withTrimmedTop (tailDown ? 0.0f : 12.0f).withTrimmedBottom (tailDown ? 12.0f : 0.0f);
    Path bubble;
    bubble.addRoundedRectangle (b, 20.0f);
    Path tail;
    if (tailDown)
    {
        tail.startNewSubPath (tailX - 12.0f, b.getBottom() - 2.0f);
        tail.quadraticTo (tailX - 2.0f, b.getBottom() + 6.0f, tailX + 2.0f, b.getBottom() + 13.0f);
        tail.quadraticTo (tailX + 4.0f, b.getBottom() + 4.0f, tailX + 12.0f, b.getBottom() - 2.0f);
    }
    else
    {
        tail.startNewSubPath (tailX - 12.0f, b.getY() + 2.0f);
        tail.quadraticTo (tailX - 2.0f, b.getY() - 6.0f, tailX + 2.0f, b.getY() - 13.0f);
        tail.quadraticTo (tailX + 4.0f, b.getY() - 4.0f, tailX + 12.0f, b.getY() + 2.0f);
    }
    tail.closeSubPath();
    bubble.addPath (tail);

    DropShadow (Colours::black.withAlpha (0.45f), 10, { 0, 3 }).drawForPath (g, bubble);
    g.setColour (Colour (0xfffff6ee));
    g.fillPath (bubble);
    g.setColour (Colour (0xff7a2350));
    g.strokePath (bubble, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xfffff6ee));          // hide the seam where the tail joins
    g.fillRoundedRectangle (b.reduced (2.5f), 18.0f);

    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    g.setColour (Colour (0xff7a2350).withAlpha (0.75f));
    g.drawText (title, b.withHeight (24.0f).translated (0.0f, 4.0f), Justification::centred);
    g.setFont (SadLookAndFeel::uiFont (9.0f));
    g.setColour (Colour (0xff7a2350).withAlpha (0.45f));
    g.drawText ("type +7 or -5, then enter", b.removeFromBottom (16.0f), Justification::centred);
}

void PitchBubble::commit()
{
    if (closing) return;
    auto t = field.getText().trim();
    const bool neg = t.startsWithChar ('-');
    t = t.removeCharacters ("+-");
    if (t.isNotEmpty() && t.containsOnly ("0123456789") && onCommit)
        onCommit ((float) jlimit (-24, 24, (neg ? -1 : 1) * t.getIntValue()));
    close();
}

void PitchBubble::close()
{
    if (closing) return;
    closing = true;
    if (onClose) onClose();
}

//==============================================================================
// Physics: buoyant balloons on strings tied to heavy anchors. Strings only pull (they go
// slack when you push a balloon in), balloons bump and jiggle, anchors rest on the floor.
//==============================================================================
// The sphere seen from outside: turned by yaw round the vertical, tipped towards you by pitch,
// in perspective. World coordinates are the sound's (x ahead, y left, z up), in px; with no turn
// you look over the listener's shoulder, so the left is on the left and ahead is into the screen.
using fp::ambi::Vec3;
namespace Flight3 { inline Vec3 clamp (Vec3 p, float r) { const float l = p.length(); return l > r ? p * (r / l) : p; } }

Vec3 FreakiePieEditor::toView (Vec3 w) const noexcept
{
    const float cy = std::cos (viewYaw), sy = std::sin (viewYaw);
    const Vec3 r { w.x * cy - w.y * sy, w.x * sy + w.y * cy, w.z };
    const float X = -r.y, Y = r.z, Z = -r.x;
    const float cp = std::cos (viewPitch), sp = std::sin (viewPitch);
    return { X, Y * cp - Z * sp, Y * sp + Z * cp };
}

Vec3 FreakiePieEditor::fromView (Vec3 v) const noexcept
{
    const float cp = std::cos (viewPitch), sp = std::sin (viewPitch);
    const float X = v.x, Y = v.y * cp + v.z * sp, Z = -v.y * sp + v.z * cp;
    const Vec3 r { -Z, -X, Y };
    const float cy = std::cos (viewYaw), sy = std::sin (viewYaw);
    return { r.x * cy + r.y * sy, -r.x * sy + r.y * cy, r.z };
}

Point<float> FreakiePieEditor::project (Vec3 w, float* scale, float* depth) const noexcept
{
    const auto v = toView (w);
    const float k = camDist / std::max (1.0f, camDist - v.z);
    if (scale != nullptr) *scale = k;
    if (depth != nullptr) *depth = v.z;
    return { sphereCentre.x + v.x * k, sphereCentre.y - v.y * k };
}

Vec3 FreakiePieEditor::unproject (Point<float> s, float depth) const noexcept
{
    const float k = camDist / std::max (1.0f, camDist - depth);
    return fromView ({ (s.x - sphereCentre.x) / k, (sphereCentre.y - s.y) / k, depth });
}

bool FreakiePieEditor::floorHit (Point<float> s, Vec3& out) const noexcept
{
    // along the ray through that pixel until it reaches the floor's height
    const Vec3 eye = fromView ({ 0.0f, 0.0f, camDist });
    const Vec3 at = unproject (s, 0.0f);
    const Vec3 d = at - eye;
    if (std::abs (d.z) < 1.0e-4f) return false;
    const float t = (floorZ - eye.z) / d.z;
    if (t <= 0.0f) return false;
    out = eye + d * t;
    return true;
}

float FreakiePieEditor::sphereScreenRadius() const noexcept
{
    return sphereR * camDist / std::sqrt (camDist * camDist - sphereR * sphereR);   // the outline of a ball seen in perspective
}

// the order runs round the floor from ahead-left, behind you, to ahead-right (the front is left open)
Vec3 FreakiePieEditor::ringPoint (float shelfX) const noexcept
{
    const float u = jlimit (-0.1f, 1.1f, (shelfX - shelfLeft) / (shelfRight - shelfLeft));
    const float az = degreesToRadians (62.0f + 236.0f * u);
    const float ring = 0.74f * std::sqrt (sphereR * sphereR - floorZ * floorZ);
    return { ring * std::cos (az), ring * std::sin (az), floorZ };
}

void FreakiePieEditor::step (float h)
{
    const float t = (float) clock;
    constexpr float buoyancy = 520.0f, damping = 1.6f, maxV = 2600.0f;
    const float R = sphereR;

    // the bows and boxes: on the ring round the floor (the effects, gliding to their place in the
    // order) or out on the floor (the loop and the caught sounds, which you can drag about)
    for (auto& a : anchors)
    {
        if (a.hidden) continue;
        if (a.fixed) { a.pos = pinkieHoof(); a.w = unproject (a.pos, 0.0f); a.vel = {}; a.scale = 1.0f; continue; }
        if (a.onShelf)
        {
            a.vel.x += (a.slotX - a.shelfX) * 90.0f * h - a.vel.x * 12.0f * h;
            a.shelfX += a.vel.x * h;
            a.w = ringPoint (a.shelfX);
        }
        else if (a.dragging)
        {
            Vec3 f;
            if (floorHit (a.target, f))
            {
                const float lim = 0.92f * std::sqrt (R * R - floorZ * floorZ), r = std::hypot (f.x, f.y);
                if (r > lim) { f.x *= lim / r; f.y *= lim / r; }
                a.w = { f.x, f.y, floorZ };
            }
        }
        a.pos = project (a.w, &a.scale, &a.depth);
    }

    for (auto* b : floaters)
    {
        if (! b->alive) continue;
        if (! b->placed3d)
        {
            // new: it comes in where it appeared (or, born outside the sphere, just above its bow)
            const auto& a = anchors[b->anchor];
            b->w = insideSphere (b->pos) ? unproject (b->pos, 0.0f)
                                         : a.w + Vec3 { rnd.nextFloat() * 30.0f - 15.0f, rnd.nextFloat() * 30.0f - 15.0f, 30.0f + rnd.nextFloat() * 30.0f };
            b->w = Flight3::clamp (b->w, R - b->collisionRadius() * balloonScale);
            b->wv = {};
            b->placed3d = true;
            b->lastVel2 = b->vel;
        }
        // a push from the room (rain, wind, the pin, lightning, the music) arrives on the screen: put it into the sphere
        const auto dv = b->vel - b->lastVel2;
        if (dv.x != 0.0f || dv.y != 0.0f)
        {
            const auto o = fromView ({ 0.0f, 0.0f, 0.0f });
            b->wv += (fromView ({ dv.x, -dv.y, 0.0f }) - o) * (1.0f / std::max (0.3f, b->scale));
        }
        if (b->dragging)
        {
            const auto np = unproject (b->dragTarget, b->dragDepth);
            const auto lim = Flight3::clamp (np, R - b->collisionRadius() * balloonScale);
            b->wv = (lim - b->w) * (1.0f / h);
            const float sp = b->wv.length();
            if (sp > maxV) b->wv *= maxV / sp;
            b->w = lim;
            continue;
        }
        const auto& a = anchors[b->anchor];
        const float lift = (1.0f + 0.8f * heliumVis) * (1.0f - 0.25f * rainVis - 0.20f * b->wet);
        // the fan: round the ring (or across, for the floor bows), so each bunch opens out
        Vec3 tangent { -a.w.y, a.w.x, 0.0f };
        if (tangent.length() < 1.0f) tangent = { 0.0f, 1.0f, 0.0f };
        tangent = tangent.normalised();
        const Vec3 outward = Vec3 { a.w.x, a.w.y, 0.0f }.length() > 1.0f ? Vec3 { a.w.x, a.w.y, 0.0f }.normalised() : Vec3 { 1.0f, 0.0f, 0.0f };
        const Vec3 breeze = tangent * (200.0f * b->lean * lift + 22.0f * std::sin (t * 0.37f + b->phase))
                          + outward * (-40.0f * lift + 14.0f * std::sin (t * 0.29f + b->phase * 1.3f))
                          + Vec3 { 0.0f, 0.0f, 8.0f * std::sin (t * 0.23f + b->phase * 1.7f) };
        const float thaw = 1.0f - 0.92f * b->frost;
        b->wv.z += buoyancy * lift * h * (b->frost > 0.5f ? thaw : 1.0f);
        b->wv += breeze * (h * thaw);
        b->wv *= 1.0f - jmin (1.0f, (damping + 9.0f * b->frost) * h);
        b->w += b->wv * h;
    }

    for (int iter = 0; iter < 3; ++iter)
    {
        // strings
        for (auto* b : floaters)
        {
            if (! b->alive || b->dragging) continue;
            const auto& a = anchors[b->anchor];
            const auto d = b->w - a.w;
            const float L = (b->stringLength + 36.0f) * stringScale;
            const float dist = d.length();
            if (dist <= L || dist < 1.0e-3f) continue;
            const auto nrm = d * (1.0f / dist);
            b->w -= nrm * (dist - L);
            const float rel = b->wv.dot (nrm);
            if (rel > 0.0f) b->wv -= nrm * rel;
        }
        // bumping into each other
        for (int i = 0; i < floaters.size(); ++i)
            for (int j = i + 1; j < floaters.size(); ++j)
            {
                auto* p = floaters[i];
                auto* c = floaters[j];
                if (! p->alive || ! c->alive) continue;
                const auto d = c->w - p->w;
                const float Rr = (p->collisionRadius() + c->collisionRadius()) * balloonScale;
                const float dist2 = d.dot (d);
                if (dist2 >= Rr * Rr || dist2 < 1.0e-6f) continue;
                const float wa = p->dragging ? 0.0f : 1.0f, wc = c->dragging ? 0.0f : 1.0f;
                const float ws = wa + wc;
                if (ws <= 0.0f) continue;
                const float dist = std::sqrt (dist2);
                const auto nrm = d * (1.0f / dist);
                const float overlap = Rr - dist;
                p->w -= nrm * (overlap * wa / ws);
                c->w += nrm * (overlap * wc / ws);
                const float rel = (c->wv - p->wv).dot (nrm);
                if (rel < 0.0f)
                {
                    const float imp = -(1.0f + 0.45f) * rel / ws;
                    p->wv -= nrm * (imp * wa);
                    c->wv += nrm * (imp * wc);
                    const float j2 = jmin (6.0f, -rel * 0.012f);   // squish!
                    p->squashVel += j2; c->squashVel += j2;
                }
            }
        // the glass: nothing gets out of the sphere
        for (auto* b : floaters)
        {
            if (! b->alive) continue;
            const float lim = R - b->collisionRadius() * balloonScale;
            const float r = b->w.length();
            if (r > lim)
            {
                const auto nrm = b->w * (1.0f / r);
                b->w = nrm * lim;
                const float vn = b->wv.dot (nrm);
                if (vn > 0.0f) { b->wv -= nrm * (1.5f * vn); b->squashVel += 0.6f; }
            }
            if (b->w.z < floorZ + 20.0f) { b->w.z = floorZ + 20.0f; if (b->wv.z < 0.0f) b->wv.z *= -0.5f; }
        }
    }

    // onto the screen
    for (auto* b : floaters)
    {
        if (! b->alive) continue;
        float k = 1.0f;
        const auto np = project (b->w, &k, &b->depth);
        const auto ahead = project (b->w + b->wv * 0.01f);
        b->pos = np;
        b->vel = (ahead - np) * 100.0f;
        b->lastVel2 = b->vel;
        b->scale = k * balloonScale;
        const auto d = b->pos - anchors[b->anchor].pos;
        const float dist = (b->w - anchors[b->anchor].w).length();
        const bool taut = dist > (b->stringLength + 30.0f) * stringScale;
        float target = std::atan2 (d.x, -d.y);
        if (! taut) target = target * 0.35f + 0.08f * std::sin (t * 0.9f + b->phase);
        b->angle += (target - b->angle) * jmin (1.0f, (taut ? 7.0f : 2.5f) * h);
        b->squashVel += (-260.0f * b->squash - 7.0f * b->squashVel) * h;
        b->squash = jlimit (-0.22f, 0.22f, b->squash + b->squashVel * h);
    }
}

//==============================================================================
// The pumps: hold to blow a balloon up. Somewhere between 5 and 12 seconds of
// inflating (you never know when) it bursts.
void FreakiePieEditor::pumpStep (float dt)
{
    auto grow = [] (Pump& p)
    {
        p.size = std::pow (jmin (1.0f, p.holdTime / 5.0f), 0.7f);
        p.strain = jmax (0.0f, (p.holdTime - 5.0f) / 7.0f);
    };

    // pop balloons
    if (popPump.holding)
    {
        popPump.holdTime += dt;
        popPump.handle += dt * 9.0f;
        grow (popPump);
        if (popPump.holdTime >= popPump.popAt)
        {
            const auto sh = pumpBalloonShape (popPump);
            pop ({ sh.cx, sh.cy }, jmin (1.0f, popPump.size), popPump.hue);
            popPump.holdTime = 0.0f; popPump.size = 0.0f; popPump.strain = 0.0f;
            popPump.popAt = 5.0f + rnd.nextFloat() * 7.0f;
            static const int hues[] = { balloonOrange, balloonPink, balloonBlue, balloonYellow, balloonLilac, balloonMint };
            popPump.hue = hues[(size_t) rnd.nextInt (6)];
        }
    }
    proc.balloonFX.pumping = popPump.holding;
    proc.balloonFX.pumpSize = popPump.size + 0.3f * popPump.strain;

    // sound balloons
    if (soundPump.holding && soundPump.slot >= 0 && ! soundPump.spent)
    {
        soundPump.holdTime += dt;
        soundPump.handle += dt * 9.0f;
        grow (soundPump);
        proc.balloonFX.voice[soundPump.slot].size = soundPump.size;
        soundPump.strain = 0.0f;   // a sound balloon never bursts by itself: it only goes when you pop it
    }
    fullFlash = jmax (0.0f, fullFlash - dt * 0.6f);
}

BalloonShape FreakiePieEditor::pumpBalloonShape (const Pump& p) const
{
    const float t = (float) clock;
    const float tremble = p.strain * 3.5f * (std::sin (t * 53.0f) + 0.6f * std::sin (t * 37.0f + 1.3f));
    BalloonShape sh;
    sh.rx = 6.0f + 30.0f * p.size * (1.0f + 0.22f * p.strain);
    sh.ry = sh.rx * 1.12f;
    sh.knot = 4.0f;
    sh.cx = p.nozzle.x + tremble;
    sh.cy = p.nozzle.y - sh.knot - sh.ry + p.strain * 1.5f * std::sin (t * 41.0f);
    sh.lw = 1.5f;
    return sh;
}

//==============================================================================
void FreakiePieEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) jlimit (0.0, 0.05, now - lastTick);
    lastTick = now;
    clock += dt;
    lnf.time = (float) clock;

    // the look follows the switches straight away (the audio filters ease in on their own)
    auto follow = [dt] (float& v, float target) { v += (target - v) * jmin (1.0f, dt * 1.4f); if (target == 0.0f && v < 0.002f) v = 0.0f; };
    follow (rainVis, rainOn() ? 1.0f : 0.0f);
    follow (heliumVis, heliumOn() ? 1.0f : 0.0f);
    follow (cloudVis, cloudyOn() ? 1.0f : 0.0f);
    follow (snowVis, snowOn() ? 1.0f : 0.0f);
    follow (heatVis, heatOn() ? 1.0f : 0.0f);
    follow (autoVis, autoOn() ? 1.0f : 0.0f);
    frozen = proc.snowAmount.load();   // the ice follows the sound's own freeze
    follow (bypassVis, bypassOn() ? 1.0f : 0.0f);
    {
        const int lc = proc.lightningCount.load();   // read before the store sync, so a lent effect drops in where it struck
        if (lc != seenLightning) { seenLightning = lc; strike(); }
    }
    syncStore();
    {
        // dragging something along the shelf: the others make room as it passes
        bool draggingShelf = false;
        for (auto& a : anchors) draggingShelf |= a.dragging && a.onShelf && ! a.hidden;
        if (draggingShelf)
        {
            auto visible = shelfItems();
            std::stable_sort (visible.begin(), visible.end(), [this] (int a, int b) { return anchors[anchorOfSlot (a)].shelfX < anchors[anchorOfSlot (b)].shelfX; });
            layoutShelf (visible);
        }
    }
    {
        // the gumball machine's balloons hop on every stutter
        const int ev = proc.stutter.eventCount.load();
        if (ev != seenStutter)
        {
            seenStutter = ev;
            for (int i = 0; i < numParamBalloons; ++i)
                if (auto* f = floaters[i]; f->alive && f->anchor == 4) { f->squashVel += 3.5f; f->vel.y -= 70.0f; }
        }
    }
    followPreset (dt);
    if (chalkboard != nullptr) chalkboard->step (dt);
    if (library != nullptr) library->step (dt);
    if (storeShelves != nullptr) storeShelves->step (dt);
    if (weatherBar != nullptr)
    {
        weatherBar->step (dt);
        revealStep (dt);
        if (cardPanel != nullptr && cardPanel->isVisible()) cardPanel->repaint();
        if (fadePopup != nullptr && fadePopup->isVisible()) fadePopup->repaint();
        if (presetStrip != nullptr) presetStrip->step (dt);
        // keep the barometer above the balloons (they're added as they're caught), and the store above that
        const int n = content.getNumChildComponents();
        auto* topmost = n > 0 ? content.getChildComponent (n - 1) : nullptr;
        if (topmost != weatherBar.get()) weatherBar->toFront (false);   // (the card, lists and menus are on the overlay, above all of it)
    }
    if (clock - lastSave > 2.0) { lastSave = clock; proc.saveLastState(); }
    {
        const bool scared = clock < scaredUntil;
        // how long she's been out in the snow: a minute and she's got a cold (too poorly to doze off) until it goes
        if (snowVis > 0.3f) { if (snowSince < 0.0) snowSince = clock; }
        else snowSince = -1.0;
        const bool hasCold = snowSince >= 0.0 && clock - snowSince > coldAfter;
        const bool asleep = ! scared && ! hasCold && clock - lastActivity > sleepAfter;
        auto toward = [dt] (float& v, bool on, float speed) { v += ((on ? 1.0f : 0.0f) - v) * jmin (1.0f, dt * speed); };
        toward (scaredVis, scared, 14.0f);
        toward (sleepVis, asleep, 1.5f);
        const bool hotRain = rainVis > 0.3f && heatVis > 0.3f;   // rain in a heatwave: no umbrella, she's soaked and flat out
        toward (wetVis, ! scared && ! asleep && hotRain, 1.5f);
        toward (umbrellaVis, ! scared && ! asleep && rainVis > 0.3f && ! hotRain, 3.0f);
        toward (crystalVis, ! scared && ! asleep && heliumVis > 0.3f, 4.0f);
        toward (snowyVis, ! scared && ! asleep && snowVis > 0.3f, 3.0f);
        toward (coldVis, hasCold, 0.8f);
        toward (meltVis, ! scared && ! asleep && heatVis > 0.3f, 1.2f);   // she wilts slowly
    }

    // blow on them: moving the mouse fast near a balloon pushes it
    const bool anyDrag = std::any_of (std::begin (anchors), std::end (anchors), [] (const Anchor& a) { return a.dragging; })
                      || std::any_of (floaters.begin(), floaters.end(), [] (Floater* b) { return b->dragging; });
    const auto m = content.getMouseXYRelative().toFloat();
    if (haveMouse && dt > 0.0f && ! anyDrag && getScreenBounds().contains (Desktop::getMousePosition()))
    {
        const auto mv = (m - lastMouse) / dt;
        for (auto* b : floaters)
        {
            const float d = b->pos.getDistanceFrom (m);
            if (d < 150.0f) b->vel += mv * (0.07f * (1.0f - d / 150.0f));
        }
    }
    if (haveMouse && isShowing() && m.getDistanceFrom (lastMouse) > 0.5f && getLocalBounds().contains (getLocalPoint (&content, m.toInt()))) noteActivity();
    lastMouse = m;
    haveMouse = true;

    // dance with the sound (sound balloons dance with their own voice)
    const float lvl = jlimit (0.0f, 1.0f, proc.outputLevel.load() * 1.6f);
    for (auto* b : floaters)
    {
        float l = lvl;
        if (auto* s = dynamic_cast<SoundBalloon*> (b)) l = jlimit (0.0f, 1.0f, proc.balloonFX.voice[s->slot].level.load() * 3.0f);
        b->dance += (l * (0.5f + 0.5f * std::sin ((float) clock * 7.0f + b->phase * 3.0f)) - b->dance) * jmin (1.0f, dt * 12.0f);
        if (auto* pb = dynamic_cast<Balloon*> (b)) pb->faded = pb->bypassable ? bypassVis : 0.0f;
        if (l > 0.25f && rnd.nextFloat() < dt * 3.0f) b->vel += { (rnd.nextFloat() - 0.5f) * 140.0f * l, -60.0f * l };
    }

    const float f = proc.freezeAmount.load();
    if (freezeBalloon != nullptr)
    {
        freezeBalloon->fill = f;
        if (lastFreeze < 1.0f && f >= 1.0f) confetti (freezeBalloon->pos, 24);
    }
    lastFreeze = f;
    if (spectralFreezeBalloon != nullptr) spectralFreezeBalloon->fill = proc.spectralFreezeAmount.load();
    freezeText = f <= 0.0f ? "" : f >= 1.0f ? "frozen" : "freezing...";

    weatherStep (dt);
    cloudStep (dt);
    snowStep (dt);
    trixieStep (dt);
    pumpStep (dt);
    pinStep (dt);
    pillStep (dt);
    pinkieHopVel -= 900.0f * dt;
    pinkieHop += pinkieHopVel * dt;
    if (pinkieHop <= 0.0f) { pinkieHop = 0.0f; pinkieHopVel = 0.0f; }
    for (auto* pl : pills)
    {
        pl->bounceVel += (-320.0f * pl->bounce - 9.0f * pl->bounceVel) * dt;
        pl->bounce = jlimit (-0.3f, 0.3f, pl->bounce + pl->bounceVel * dt);
    }
    step (dt * 0.5f);
    step (dt * 0.5f);

    // tidy up popped sound balloons
    for (int i = floaters.size(); --i >= numParamBalloons;)
        if (! floaters[i]->alive && ! floaters[i]->isMouseButtonDown())
        {
            content.removeChildComponent (floaters[i]);
            floaters.remove (i);
        }
    for (auto* b : floaters) { b->place(); b->repaint(); }
    grainStep (dt);
    depthSort();
    if (++meterFrame % 3 == 0) meterStep();

    for (auto& pt : particles)
    {
        pt.v.y += (pt.kind == 2 ? 700.0f : 520.0f) * dt;
        pt.v *= 1.0f - 0.9f * dt;
        pt.p += pt.v * dt;
        pt.rot += pt.spin * dt;
        pt.life -= dt * (pt.kind == 1 ? 1.4f : 0.55f);
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& q) { return q.life <= 0.0f; }), particles.end());
    for (auto& b : bursts) b.age += dt;
    bursts.erase (std::remove_if (bursts.begin(), bursts.end(), [] (const Burst& b) { return b.age > 0.5f; }), bursts.end());
    for (auto& d : dust) { d.y -= dt * 4.0f; if (d.y < 0) d.y += designH; }


    const auto& scales = fp::getScales();
    const int si = jlimit (0, (int) scales.size() - 1, (int) proc.apvts.getRawParameterValue (ids::psScale)->load());
    const auto& sc = scales[(size_t) si];
    const auto q = fp::quantise (sc, proc.apvts.getRawParameterValue (ids::psShift)->load() * 100.0);
    String periodText;
    if (q.period != 0) periodText = (q.period > 0 ? " +" : " ") + String (q.period) + " oct";
    const int st = roundToInt (q.cents / 100.0);
    intervalText = String ("shift lands on ") + sc.degrees[(size_t) q.degreeIndex].label + periodText
                   + "  (" + (st >= 0 ? "+" : "") + String (st) + " st)";

    content.repaint();
}

// Rain falls (and really hits the balloons); helium hisses from the hose.
void FreakiePieEditor::weatherStep (float dt)
{
    const float W = (float) designW, H = (float) designH;

    dropBudget += rainVis * 420.0f * dt;
    while (dropBudget >= 1.0f)
    {
        dropBudget -= 1.0f;
        drops.push_back ({ { rnd.nextFloat() * (W + 120.0f) - 100.0f, -10.0f - rnd.nextFloat() * 30.0f },
                           { 70.0f + rnd.nextFloat() * 30.0f, 780.0f + rnd.nextFloat() * 260.0f } });
    }
    for (auto& d : drops)
    {
        if (d.bounced) d.v.y += 1500.0f * dt;     // a bounced drop arcs back down
        d.p += d.v * dt;
        bool gone = false;
        Point<float> nrm;
        if (! d.bounced && umbrellaVis > 0.5f && d.v.y > 0.0f && umbrellaHit (d.p, nrm))
        {
            const float vn = d.v.getDotProduct (nrm);
            d.v -= nrm * ((1.0f + 0.35f) * vn);                       // off the canopy, losing most of its speed
            d.v = d.v * 0.55f + Point<float> ((rnd.nextFloat() - 0.5f) * 80.0f, -40.0f * rnd.nextFloat());
            d.bounced = true;
            if (rnd.nextFloat() < 0.4f)
                particles.push_back ({ d.p, { (rnd.nextFloat() - 0.5f) * 90.0f, -50.0f - rnd.nextFloat() * 60.0f },
                                       0.0f, 0.0f, 0.25f, Colour (0xffcfe6ff), 1 });
        }
        for (auto* b : floaters)
        {
            if (! b->alive || d.p.getDistanceFrom (b->pos) > b->collisionRadius()) continue;
            for (int k = 0; k < 3; ++k)
                particles.push_back ({ d.p, { (rnd.nextFloat() - 0.5f) * 160.0f, -60.0f - rnd.nextFloat() * 90.0f },
                                       0.0f, 0.0f, 0.35f, Colour (0xffcfe6ff), 1 });
            b->vel += { d.v.x * 0.01f, 10.0f };
            b->squashVel += 0.35f;
            b->wet = jmin (1.0f, b->wet + 0.04f);
            gone = true;
            break;
        }
        if (! gone && d.p.y > H - 6.0f)
        {
            ripples.push_back ({ { d.p.x, H - 5.0f - rnd.nextFloat() * 4.0f }, 0.0f });
            gone = true;
        }
        if (gone) d.p.y = 1.0e9f;
    }
    drops.erase (std::remove_if (drops.begin(), drops.end(), [&] (const Drop& d) { return d.p.y > H + 50.0f || d.p.x > W + 60.0f; }), drops.end());

    for (auto& r : ripples) r.age += dt * 2.2f;
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.age >= 1.0f; }), ripples.end());

    puddle = jlimit (0.0f, 1.0f, puddle + (rainVis > 0.05f ? rainVis * 0.03f : -0.02f) * dt);
    for (auto* b : floaters)
    {
        b->wet = jmax (0.0f, b->wet - dt * (0.01f + 0.12f * (1.0f - rainVis)));
        b->stretch = heliumVis;
    }

    valveAngle += dt * 9.0f * heliumVis;
    puffBudget += heliumVis * 30.0f * dt;
    while (puffBudget >= 1.0f)
    {
        puffBudget -= 1.0f;
        puffs.push_back ({ anchors[0].pos + Point<float> (-8.0f, -2.0f), { (rnd.nextFloat() - 0.5f) * 40.0f, -40.0f - rnd.nextFloat() * 60.0f },
                           0.0f, 0.0f, 1.0f, Colours::white, 1 });
    }
    for (auto& pf : puffs) { pf.p += pf.v * dt; pf.v.x *= 1.0f - dt; pf.life -= dt * 0.9f; }
    puffs.erase (std::remove_if (puffs.begin(), puffs.end(), [] (const Particle& q) { return q.life <= 0.0f; }), puffs.end());
}

//==============================================================================
// Cloudy weather: the clouds come down from the sky and drift slowly through the bouquet.
Point<float> FreakiePieEditor::cloudAt (const SkyCloud& c) const
{
    const float t = (float) clock, th = proc.thunderAmount.load();
    return { c.p.x + 3.0f * th * std::sin (t * 37.0f + c.phase),
             c.p.y + 2.5f * th * std::sin (t * 29.0f + c.phase * 2.0f) };
}

// inside one of the clouds (or rain clouds) drifting through the sphere, in 3-D
bool FreakiePieEditor::insideCloud (const Floater& f) const
{
    for (auto& c : skyClouds)
        if (c.kind <= fp::SkyPatches::rain && c.amount > 0.3f && (f.w - c.w).length() < c.radius * 0.85f) return true;
    return false;
}

// the weather inside the sphere: the processor moves the patches (they change the grains they pass
// over); here they're placed on screen, and the control balloons inside a cloud wander as before
void FreakiePieEditor::cloudStep (float dt)
{
    skyClouds.resize ((size_t) fp::SkyPatches::kNum);
    for (int i = 0; i < fp::SkyPatches::kNum; ++i)
    {
        auto& c = skyClouds[(size_t) i];
        const auto& pv = proc.patchView[(size_t) i];
        c.kind = i / fp::SkyPatches::perKind;
        c.w = Vec3 { pv.x.load(), pv.y.load(), pv.z.load() } * sphereR;
        c.radius = pv.radius.load() * sphereR;
        c.amount = pv.amount.load();
        float k = 1.0f;
        c.p = project (c.w, &k, &c.depth);
        c.s = c.radius * k / 60.0f;
        c.phase = (float) i * 1.7f;
        c.vx = 0.0f;
    }
    flash = jmax (0.0f, flash - dt * 2.5f);

    const bool active = cloudyOn();
    for (auto* f : floaters)
    {
        if (! f->alive) continue;
        const bool in = active && insideCloud (*f);
        if (in) f->wv *= 1.0f - jmin (1.0f, 0.9f * dt);   // it's thick in there
        if (auto* b = dynamic_cast<Balloon*> (f)) b->cloudDrift (in, dt * (1.0f - frozen), rnd);
    }
}

// a lightning strike: a white flash, a jagged bolt from one of the clouds, and the balloons nearby jump
void FreakiePieEditor::strike()
{
    if (skyClouds.empty()) return;
    std::vector<int> onScreen;
    for (int i = 0; i < (int) skyClouds.size(); ++i)
        if (skyClouds[(size_t) i].amount > 0.3f && skyClouds[(size_t) i].kind <= fp::SkyPatches::rain) onScreen.push_back (i);
    const int ci = onScreen.empty() ? rnd.nextInt ((int) skyClouds.size()) : onScreen[(size_t) rnd.nextInt ((int) onScreen.size())];
    const auto& c = skyClouds[(size_t) ci];
    const auto from = cloudAt (c) + Point<float> (rnd.nextFloat() * 40.0f - 20.0f, 18.0f * c.s);
    auto to = from + Point<float> (rnd.nextFloat() * 140.0f - 70.0f, 150.0f + rnd.nextFloat() * 90.0f);
    to.x = jlimit (90.0f, (float) designW - 90.0f, to.x);
    to.y = jlimit (120.0f, (float) designH - 110.0f, to.y);
    strikeAt = to;

    bolt.clear();
    bolt.startNewSubPath (from);
    const int segs = 9;
    const auto dir = to - from;
    const Point<float> perp (-dir.y, dir.x);
    const float len = jmax (1.0f, dir.getDistanceFromOrigin());
    Point<float> branchFrom;
    for (int i = 1; i <= segs; ++i)
    {
        const float u = (float) i / (float) segs;
        const float j = i == segs ? 0.0f : (rnd.nextFloat() - 0.5f) * 36.0f;
        const auto q = from + dir * u + perp * (j / len);
        bolt.lineTo (q);
        if (i == 4) branchFrom = q;
    }
    bolt.startNewSubPath (branchFrom);   // a little fork
    bolt.lineTo (branchFrom + Point<float> ((rnd.nextBool() ? 1.0f : -1.0f) * (24.0f + 20.0f * rnd.nextFloat()), 30.0f));
    bolt.lineTo (branchFrom + Point<float> ((rnd.nextBool() ? 1.0f : -1.0f) * 14.0f, 58.0f));

    flash = 1.0f;
    scaredUntil = jmax (scaredUntil, clock + 0.9);
    for (auto* f : floaters)
    {
        if (! f->alive) continue;
        const auto away = f->pos - strikeAt;
        const float d = away.getDistanceFromOrigin();
        if (d > 180.0f) continue;
        f->vel += away / jmax (1.0f, d) * 360.0f * (1.0f - d / 180.0f);
        f->squashVel += 3.0f;
    }
}

void FreakiePieEditor::drawSkyClouds (Graphics& g) const
{
    // far ones first
    std::vector<const SkyCloud*> order;
    for (auto& c : skyClouds) if (c.amount > 0.01f) order.push_back (&c);
    std::sort (order.begin(), order.end(), [] (auto* a, auto* b) { return a->depth < b->depth; });
    const float t = (float) clock;
    for (auto* cp : order)
    {
        const auto& c = *cp;
        const auto at = cloudAt (c);
        const float a = fp::smoothstep (c.amount);
        switch (c.kind)
        {
            case fp::SkyPatches::cloud:
            case fp::SkyPatches::rain:
            {
                const bool rainy = c.kind == fp::SkyPatches::rain;
                const float dark = ((rainy ? 0.75f : 0.15f) + 0.25f * proc.thunderAmount.load()) * (1.0f - 0.8f * flash);
                g.beginTransparencyLayer (0.78f * a);
                drawCloud (g, at, c.s, jlimit (0.0f, 1.0f, dark));
                g.endTransparencyLayer();
                if (rainy)
                    for (int i = 0; i < 9; ++i)
                    {
                        const float u = frac2 (t * 1.4f + (float) i * 0.37f);
                        const float x = at.x + (float) ((i * 37) % 9 - 4) * 9.0f * c.s, y = at.y + 10.0f * c.s + u * 40.0f * c.s;
                        g.setColour (Colour (0xffbcd6ff).withAlpha (a * (1.0f - u) * 0.9f));
                        g.drawLine (x, y, x - 1.5f, y + 7.0f * c.s, 1.4f);
                    }
                break;
            }
            case fp::SkyPatches::snow:   // a little flurry turning round itself
                for (int i = 0; i < 26; ++i)
                {
                    const float ang = (float) i * 2.39996f + t * 0.4f, rr = std::sqrt ((float) (i + 1) / 26.0f) * 34.0f * c.s;
                    const float fall = frac2 (t * 0.15f + (float) i * 0.13f);
                    const auto q = at + Point<float> (std::cos (ang) * rr, std::sin (ang) * rr * 0.6f + (fall - 0.5f) * 24.0f * c.s);
                    const float sz = (2.0f + (float) (i % 3)) * c.s;
                    g.setColour (Colours::white.withAlpha (a * 0.85f * (0.6f + 0.4f * std::sin (t * 3.0f + (float) i))));
                    g.fillEllipse (Rectangle<float> (sz, sz).withCentre (q));
                }
                break;
            case fp::SkyPatches::heat:   // a warm wobble in the air
            default:
            {
                const float r = 40.0f * c.s;
                g.setGradientFill (ColourGradient (Colour (0xffffb15c).withAlpha (0.28f * a), at.x, at.y, Colour (0x00ff8c40), at.x + r, at.y, true));
                g.fillEllipse (Rectangle<float> (2.0f * r, 1.5f * r).withCentre (at));
                g.setColour (Colour (0xffffd29a).withAlpha (0.35f * a));
                for (int k = 0; k < 3; ++k)
                {
                    Path wv;
                    const float y0 = at.y - r * 0.3f + (float) k * r * 0.3f;
                    for (int i = 0; i <= 16; ++i)
                    {
                        const float x = at.x - r * 0.7f + (float) i / 16.0f * r * 1.4f;
                        const float y = y0 + 3.0f * c.s * std::sin ((float) i * 0.9f + t * 4.0f + (float) k);
                        if (i == 0) wv.startNewSubPath (x, y); else wv.lineTo (x, y);
                    }
                    g.strokePath (wv, PathStrokeType (1.2f));
                }
                break;
            }
        }
    }

    // over the cloud: a little tag on each balloon it's changing, so you can watch the value move
    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    std::vector<Rectangle<float>> placed;   // tags step aside so they never sit on each other
    for (auto* f : floaters)
    {
        auto* b = dynamic_cast<Balloon*> (f);
        if (b == nullptr || ! b->alive || ! b->isDrifting()) continue;
        const String text = b->label.replace ("\n", " ") + "  " + b->currentText();
        const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 18.0f;
        auto r = Rectangle<float> (w, 18.0f).withCentre (b->pos.translated (0.0f, -b->collisionRadius() - 6.0f));
        r = r.withX (jlimit (4.0f, (float) designW - w - 4.0f, r.getX())).withY (jmax (4.0f, r.getY()));
        const auto home = r;
        for (int tries = 0; tries < 12; ++tries)
        {
            bool clash = false;
            for (auto& q : placed) clash |= q.expanded (2.0f).intersects (r);
            if (! clash) break;
            r = home.translated (0.0f, (tries % 2 == 0 ? -1.0f : 1.0f) * 21.0f * (float) (tries / 2 + 1));   // above, below, further above...
        }
        placed.push_back (r);
        if (const auto c = r.getCentre(); c.getDistanceFrom (b->pos) > b->collisionRadius() + 16.0f)
        {
            g.setColour (balloonColours (b->hue).fill.withAlpha (0.7f));   // a thread back to its balloon
            g.drawLine (Line<float> (c, b->pos), 1.0f);
        }
        g.setColour (Colour (0xff1a2236).withAlpha (0.82f));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (balloonColours (b->hue).fill);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.2f);
        g.setColour (Colours::white.withAlpha (0.95f));
        g.drawText (text, r, Justification::centred);
    }
}

// Snow: flakes drift down, a few rest on the balloons' tops and melt, the rest pile up on the floor
void FreakiePieEditor::snowStep (float dt)
{
    const float W = (float) designW, H = (float) designH;
    flakeBudget += snowVis * 70.0f * dt;
    while (flakeBudget >= 1.0f)
    {
        flakeBudget -= 1.0f;
        flakes.push_back ({ { rnd.nextFloat() * W, -8.0f }, 28.0f + rnd.nextFloat() * 40.0f, 2.0f + rnd.nextFloat() * 4.5f, rnd.nextFloat() * 6.2832f });
    }
    const float t = (float) clock;
    for (auto& f : flakes)
    {
        f.p.y += f.vy * dt;
        f.p.x += (14.0f * std::sin (t * 0.9f + f.phase) + 8.0f) * dt;
        bool gone = f.p.y > H - 8.0f - 14.0f * snowDepth;
        if (gone) snowDepth = jmin (1.0f, snowDepth + 0.0009f);
        for (auto* b : floaters)
            if (! gone && b->alive && f.p.getDistanceFrom (b->pos) < b->collisionRadius() * 0.8f)
            {
                gone = true;
                b->frost = jmax (b->frost, jmin (1.0f, frozen + 0.05f));   // a touch of ice where it lands
            }
        if (gone) f.p.y = 1.0e9f;
    }
    flakes.erase (std::remove_if (flakes.begin(), flakes.end(), [&] (const Flake& f) { return f.p.y > H + 20.0f || f.p.x > W + 20.0f; }), flakes.end());
    if (snowVis < 0.05f) snowDepth = jmax (0.0f, snowDepth - dt * 0.08f);   // it melts away once the snow stops
    for (auto* b : floaters) b->frost += (frozen - b->frost) * jmin (1.0f, dt * 2.0f);
}

void FreakiePieEditor::drawSnowbank (Graphics& g) const
{
    if (snowDepth <= 0.002f) return;
    const float H = (float) designH, top = H - 4.0f - 22.0f * snowDepth;
    Path bank;
    bank.startNewSubPath (0.0f, H);
    for (int i = 0; i <= 40; ++i)
    {
        const float x = (float) designW * (float) i / 40.0f;
        bank.lineTo (x, top + 4.0f * std::sin ((float) i * 1.7f) + 3.0f * std::sin ((float) i * 0.6f));
    }
    bank.lineTo ((float) designW, H);
    bank.closeSubPath();
    g.setGradientFill (ColourGradient (Colour (0xfff7fbff), 0.0f, top, Colour (0xffcadcf2), 0.0f, H, false));
    g.fillPath (bank);
    g.setColour (Colour (0xffa9c3e6).withAlpha (0.7f));
    g.strokePath (bank, PathStrokeType (1.2f));
}

// the snow switch: a snowflake that spins slowly (faster, and glowing, while it snows)
void FreakiePieEditor::drawSnowButton (Graphics& g) const
{
    const auto c = Point<float> (snowButton.getX() + 22.0f, snowButton.getCentreY());
    const float R = 12.0f, rot = (float) clock * (0.2f + 0.6f * snowVis);
    if (frozen > 0.0f)
    {
        g.setGradientFill (ColourGradient (Colour (0xffcfe8ff).withAlpha (0.6f * frozen), c.x, c.y, Colour (0x00cfe8ff), c.x + 22.0f, c.y, true));
        g.fillEllipse (Rectangle<float> (44.0f, 44.0f).withCentre (c));
    }
    g.setColour (Colour (0xffeaf6ff).withAlpha (snowOn() ? 1.0f : 0.7f));
    for (int a = 0; a < 6; ++a)
    {
        const float ang = rot + (float) a * MathConstants<float>::pi / 3.0f;
        const Point<float> dir (std::cos (ang), std::sin (ang));
        const Point<float> side (-dir.y, dir.x);
        g.drawLine (Line<float> (c, c + dir * R), 2.0f);
        const auto m = c + dir * (R * 0.6f);
        g.drawLine (Line<float> (m, m + (dir - side) * 4.0f), 1.4f);
        g.drawLine (Line<float> (m, m + (dir + side) * 4.0f), 1.4f);
    }
    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    g.setColour (palette::blush.withAlpha (snowOn() ? 0.95f : 0.6f));
    g.drawText (snowOn() ? (frozen >= 1.0f ? "snow: frozen" : "snow: on") : "snow", snowButton.withTrimmedLeft (40.0f), Justification::centredLeft);
}

// the heatwave switch: a hot sun with the air wobbling underneath it
void FreakiePieEditor::drawHeatButton (Graphics& g) const
{
    const auto c = Point<float> (heatButton.getX() + 22.0f, heatButton.getCentreY() - 4.0f);
    const float t = (float) clock, u = fp::smoothstep (heatVis);
    if (u > 0.0f)
    {
        g.setGradientFill (ColourGradient (Colour (0xffffa040).withAlpha (0.55f * u), c.x, c.y, Colour (0x00ffa040), c.x + 24.0f, c.y, true));
        g.fillEllipse (Rectangle<float> (48.0f, 48.0f).withCentre (c));
    }
    const auto sunCol = Colour (0xffffd45c).interpolatedWith (Colour (0xffff8a3c), u);
    g.setColour (sunCol.withAlpha (heatOn() ? 1.0f : 0.75f));
    g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (c));
    for (int i = 0; i < 10; ++i)
    {
        const float a = t * (0.15f + 0.35f * u) + (float) i * MathConstants<float>::twoPi / 10.0f;
        const float r0 = 9.5f, r1 = 12.5f + 1.5f * u * std::sin (t * 3.0f + (float) i);
        g.drawLine (c.x + std::cos (a) * r0, c.y + std::sin (a) * r0, c.x + std::cos (a) * r1, c.y + std::sin (a) * r1, 1.6f);
    }
    // three wavy lines of hot air under it
    g.setColour (Colour (0xffffc89a).withAlpha (heatOn() ? 0.9f : 0.55f));
    for (int k = 0; k < 2; ++k)
    {
        Path w;
        const float y = c.y + 15.0f + 4.0f * (float) k;
        for (int i = 0; i <= 12; ++i)
        {
            const float x = c.x - 12.0f + 2.0f * (float) i;
            const float yy = y + 1.4f * std::sin ((float) i * 0.9f - t * (2.0f + 3.0f * u) + (float) k);
            if (i == 0) w.startNewSubPath (x, yy); else w.lineTo (x, yy);
        }
        g.strokePath (w, PathStrokeType (1.2f));
    }
    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    g.setColour (palette::blush.withAlpha (heatOn() ? 0.95f : 0.6f));
    g.drawText (heatOn() ? "heatwave: on" : "heatwave", heatButton.withTrimmedLeft (40.0f), Justification::centredLeft);
}

// the shop in a heatwave: warm light, hotter near the floor, and the air rippling upwards
void FreakiePieEditor::drawHeatShimmer (Graphics& g, Rectangle<float> full) const
{
    const float u = heatVis, t = (float) clock;
    g.setColour (Colour (0xffffb060).withAlpha (0.07f * u));
    g.fillRect (full);
    g.setGradientFill (ColourGradient (Colour (0x00ff8a3c), 0.0f, full.getHeight() * 0.35f,
                                       Colour (0xffff8a3c).withAlpha (0.16f * u), 0.0f, full.getBottom(), false));
    g.fillRect (full);
    // ripples of hot air drifting up from the floor, each one fading as it rises
    for (int k = 0; k < 9; ++k)
    {
        const float life = std::fmod (t * 0.07f + (float) k / 9.0f, 1.0f);
        const float y = full.getBottom() - 30.0f - life * full.getHeight() * 0.75f;
        const float a = u * 0.10f * std::sin (life * MathConstants<float>::pi);
        if (a <= 0.002f) continue;
        Path w;
        const float amp = 3.0f + 4.0f * life, len = 120.0f + 30.0f * (float) (k % 3);
        for (float x = full.getX() - 20.0f; x <= full.getRight() + 20.0f; x += 8.0f)
        {
            const float yy = y + amp * std::sin (x / len * MathConstants<float>::twoPi + t * 1.3f + (float) k * 1.7f);
            if (x <= full.getX() - 20.0f) w.startNewSubPath (x, yy); else w.lineTo (x, yy);
        }
        g.setColour (Colour (0xfffff0d8).withAlpha (a));
        g.strokePath (w, PathStrokeType (6.0f + 6.0f * life));
    }
}

// the auto weather switch: when it's on it says what the sky is doing
void FreakiePieEditor::drawAutoSwitch (Graphics& g) const
{
    const auto r = autoButton;
    g.setColour (Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (r, 9.0f);
    const auto track = Rectangle<float> (r.getX() + 5.0f, r.getCentreY() - 5.0f, 20.0f, 10.0f);
    g.setColour ((autoOn() ? balloonColours (balloonMint).fill : Colours::white).withAlpha (autoOn() ? 0.6f : 0.25f));
    g.fillRoundedRectangle (track, 5.0f);
    g.setColour (Colours::white.withAlpha (0.9f));
    g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ track.getX() + 5.0f + 10.0f * autoVis, track.getCentreY() }));
    static const char* names[] = { "clear", "cloudy", "rain", "storm", "snow", "heatwave" };
    const int s = (int) fp::AutoWeather::spellOf ({ rainOn(), cloudyOn(), snowOn(), heatOn() });   // what the sky shows now
    g.setFont (SadLookAndFeel::uiFont (10.0f, true));
    g.setColour (palette::blush.withAlpha (autoOn() ? 0.95f : 0.6f));
    g.drawText (autoOn() ? String ("auto: ") + names[jlimit (0, 5, s)] : String ("auto weather"), r.withTrimmedLeft (29.0f), Justification::centredLeft);
}

// the weather switch: a sun that hides behind clouds
void FreakiePieEditor::drawCloudyButton (Graphics& g) const
{
    const auto c = cloudyButton.getCentre().translated (0.0f, -6.0f);
    const float u = fp::smoothstep (cloudVis), t = (float) clock;
    const auto sun = c.translated (-12.0f, -6.0f + 4.0f * u);
    g.setColour (Colour (0xffffd45c).withAlpha (1.0f - 0.55f * u));
    for (int i = 0; i < 8; ++i)
    {
        const float a = t * 0.4f + (float) i * MathConstants<float>::twoPi / 8.0f;
        g.drawLine (sun.x + std::cos (a) * 13.0f, sun.y + std::sin (a) * 13.0f, sun.x + std::cos (a) * 17.0f, sun.y + std::sin (a) * 17.0f, 2.0f);
    }
    g.fillEllipse (Rectangle<float> (20.0f, 20.0f).withCentre (sun));
    drawCloud (g, c.translated (10.0f - 8.0f * u, 6.0f), 0.42f, 0.1f);
    if (u > 0.01f)
    {
        g.beginTransparencyLayer (u);
        drawCloud (g, c.translated (-16.0f + 4.0f * std::sin (t * 0.5f), 0.0f), 0.38f, 0.25f);
        g.endTransparencyLayer();
    }
    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    g.setColour (palette::blush.withAlpha (cloudyOn() ? 0.95f : 0.6f));
    g.drawText (cloudyOn() ? "cloudy: on" : "cloudy", cloudyButton.withTop (cloudyButton.getBottom() - 14.0f), Justification::centred);
}

// a little slide switch on the info card: the effects on, or stepped aside for the frozen balloons
void FreakiePieEditor::drawBypassSwitch (Graphics& g) const
{
    const auto r = bypassButton;
    g.setColour (Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (r, 10.0f);
    const auto track = Rectangle<float> (r.getX() + 6.0f, r.getCentreY() - 6.0f, 26.0f, 12.0f);
    const auto on = balloonColours (balloonMint).fill, off = balloonColours (balloonPink).fill;
    g.setColour ((bypassVis > 0.5f ? off : on).withAlpha (0.55f));
    g.fillRoundedRectangle (track, 6.0f);
    g.setColour (Colours::white.withAlpha (0.9f));
    g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre ({ track.getX() + 6.0f + 14.0f * bypassVis, track.getCentreY() }));
    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    g.setColour (palette::blush.withAlpha (0.9f));
    g.drawText (bypassOn() ? "effects bypassed: frozen balloons only" : "effects on (click to bypass)",
                r.withTrimmedLeft (38.0f), Justification::centredLeft);
}

//==============================================================================
void FreakiePieEditor::drawBow (Graphics& g, Point<float> c, int hue) const
{
    // a satin ribbon bow
    const auto base = balloonColours (hue).fill;
    auto loop = [&] (float dir)
    {
        Path p;
        p.startNewSubPath (c);
        p.cubicTo (c.x + dir * 10.0f, c.y - 20.0f, c.x + dir * 30.0f, c.y - 16.0f, c.x + dir * 28.0f, c.y - 2.0f);
        p.cubicTo (c.x + dir * 27.0f, c.y + 10.0f, c.x + dir * 12.0f, c.y + 8.0f, c.x, c.y);
        p.closeSubPath();
        return p;
    };
    auto tail = [&] (float dir)
    {
        Path p;
        p.startNewSubPath (c.x + dir * 2.0f, c.y + 3.0f);
        p.cubicTo (c.x + dir * 8.0f, c.y + 12.0f, c.x + dir * 10.0f, c.y + 18.0f, c.x + dir * 15.0f, c.y + 26.0f);
        p.lineTo (c.x + dir * 8.0f, c.y + 24.0f);
        p.lineTo (c.x + dir * 4.0f, c.y + 29.0f);
        p.cubicTo (c.x + dir * 2.0f, c.y + 20.0f, c.x, c.y + 12.0f, c.x - dir * 2.0f, c.y + 4.0f);
        p.closeSubPath();
        return p;
    };
    for (float dir : { -1.0f, 1.0f })
    {
        auto t = tail (dir);
        g.setGradientFill (ColourGradient (base.darker (0.2f), c.x, c.y, base.darker (0.7f), c.x + dir * 14.0f, c.y + 28.0f, false));
        g.fillPath (t);
    }
    for (float dir : { -1.0f, 1.0f })
    {
        auto l = loop (dir);
        g.setGradientFill (ColourGradient (base.brighter (0.35f), c.x + dir * 16.0f, c.y - 12.0f, base.darker (0.6f), c.x + dir * 6.0f, c.y + 8.0f, false));
        g.fillPath (l);
        g.setColour (Colours::white.withAlpha (0.45f));
        g.strokePath (l, PathStrokeType (1.0f), AffineTransform::translation (-dir * 0.5f, -0.8f));
    }
    auto knot = Rectangle<float> (10.0f, 11.0f).withCentre (c);
    g.setGradientFill (ColourGradient (base.brighter (0.3f), knot.getCentreX(), knot.getY(), base.darker (0.6f), knot.getCentreX(), knot.getBottom(), false));
    g.fillEllipse (knot);
}

//==============================================================================
// The shelf: the effects stand on it in the order the sound goes through them
int FreakiePieEditor::anchorOfSlot (int slot) noexcept
{
    static const int fixedSlots[] = { 2, 5, 3, 6, 4 };   // spectral, pitch, grains, reverb, stutter
    return slot < fp::Engine::slotStore ? fixedSlots[slot] : firstBox + (slot - fp::Engine::slotStore);
}

std::vector<int> FreakiePieEditor::shelfItems() const
{
    std::vector<int> v;
    for (int slot : proc.getShelfOrder())
        if (slot >= 0 && ! anchors[anchorOfSlot (slot)].hidden) v.push_back (slot);
    return v;
}

void FreakiePieEditor::layoutShelf (const std::vector<int>& visibleOrder)
{
    const float span = shelfRight - shelfLeft, gap = span / (float) jmax (1, (int) visibleOrder.size());
    for (size_t i = 0; i < visibleOrder.size(); ++i)
        anchors[anchorOfSlot (visibleOrder[i])].slotX = shelfLeft + gap * ((float) i + 0.5f);
}

// the order, read off the shelf left to right; anything not standing on it keeps its place after them
void FreakiePieEditor::commitShelf()
{
    auto visible = shelfItems();
    std::stable_sort (visible.begin(), visible.end(), [this] (int a, int b) { return anchors[anchorOfSlot (a)].shelfX < anchors[anchorOfSlot (b)].shelfX; });
    const auto old = proc.getShelfOrder();
    if (! std::equal (visible.begin(), visible.end(), shelfItems().begin())) confetti ({ (shelfLeft + shelfRight) * 0.5f, shelfY + 20.0f }, 14);
    FreakiePieProcessor::Order o; o.fill (-1);
    size_t n = 0;
    for (int slot : visible) o[n++] = slot;
    for (int slot : old) if (std::find (visible.begin(), visible.end(), slot) == visible.end()) o[n++] = slot;
    proc.setShelfOrder (o);
    layoutShelf (visible);
}

void FreakiePieEditor::drawShelf (Graphics& g) const
{
    const float top = shelfY + 22.0f;
    const auto plank = Rectangle<float> (shelfLeft - 10.0f, top, shelfRight - shelfLeft + 20.0f, 11.0f);
    // brackets under it, then the plank
    for (float x : { shelfLeft + 30.0f, (shelfLeft + shelfRight) * 0.5f, shelfRight - 30.0f })
    {
        Path br;
        br.startNewSubPath (x - 3.0f, plank.getBottom());
        br.lineTo (x + 3.0f, plank.getBottom());
        br.lineTo (x + 3.0f, plank.getBottom() + 4.0f);
        br.lineTo (x - 14.0f, plank.getBottom() + 22.0f);
        br.lineTo (x - 18.0f, plank.getBottom() + 18.0f);
        br.closeSubPath();
        g.setColour (Colour (0xff5e3a1e));
        g.fillPath (br);
    }
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (plank.translated (0.0f, 4.0f), 3.0f);
    g.setGradientFill (ColourGradient (Colour (0xffc48b52), 0.0f, plank.getY(), Colour (0xff8a5a32), 0.0f, plank.getBottom(), false));
    g.fillRoundedRectangle (plank, 3.0f);
    g.setColour (Colour (0xff5e3a1e));
    g.drawRoundedRectangle (plank, 3.0f, 1.0f);
    g.setColour (Colour (0xffe0b07a).withAlpha (0.6f));
    g.drawLine (plank.getX() + 4.0f, plank.getY() + 1.5f, plank.getRight() - 4.0f, plank.getY() + 1.5f, 1.0f);

    // in -> 1, 2, 3 ... -> out, written along its edge
    g.setFont (SadLookAndFeel::uiFont (9.5f, true));
    g.setColour (palette::blush.withAlpha (0.8f));
    g.drawText ("in", Rectangle<float> (40.0f, 12.0f).withCentre ({ plank.getX() - 4.0f, plank.getCentreY() - 13.0f }), Justification::centred);
    g.drawText ("out", Rectangle<float> (40.0f, 12.0f).withCentre ({ plank.getRight() + 4.0f, plank.getCentreY() - 13.0f }), Justification::centred);
    const auto items = shelfItems();
    g.setFont (SadLookAndFeel::uiFont (8.5f, true));
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& a = anchors[anchorOfSlot (items[i])];
        const float x = a.slotX;
        g.setColour (Colour (0xff3a2414).withAlpha (0.85f));
        g.drawText (String ((int) i + 1), Rectangle<float> (16.0f, 10.0f).withCentre ({ x, plank.getCentreY() + 0.5f }), Justification::centred);
        if (i + 1 < items.size())
        {
            // a little arrow to the next one
            const float nx = anchors[anchorOfSlot (items[i + 1])].slotX, mx = (x + nx) * 0.5f;
            Path ar;
            ar.addTriangle (mx - 3.0f, plank.getCentreY() - 3.0f, mx - 3.0f, plank.getCentreY() + 3.0f, mx + 3.0f, plank.getCentreY());
            g.setColour (Colour (0xff3a2414).withAlpha (0.5f));
            g.fillPath (ar);
        }
    }
}

// a little wrapped present in the effect's colour; the strings tie onto the bow on its lid
void FreakiePieEditor::drawGiftBox (Graphics& g, Point<float> knot, int hue) const
{
    const auto& c = balloonColours (hue);
    const auto body = Rectangle<float> (26.0f, 20.0f).withCentre (knot.translated (0.0f, 15.0f));
    const auto lid = Rectangle<float> (30.0f, 7.0f).withCentre ({ knot.x, body.getY() + 1.0f });
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (Rectangle<float> (34.0f, 6.0f).withCentre ({ knot.x, body.getBottom() + 1.0f }));
    g.setColour (c.fill);
    g.fillRoundedRectangle (body, 2.5f);
    g.setColour (c.shade);
    g.fillRect (body.withTrimmedTop (body.getHeight() * 0.6f).reduced (1.0f, 0.0f).withTrimmedBottom (1.0f));
    g.setColour (c.line);
    g.drawRoundedRectangle (body, 2.5f, 1.2f);
    g.setColour (c.fill.brighter (0.25f));
    g.fillRoundedRectangle (lid, 2.0f);
    g.setColour (c.line);
    g.drawRoundedRectangle (lid, 2.0f, 1.1f);
    // the ribbon: white, round the box and in a bow on top
    g.setColour (Colours::white.withAlpha (0.92f));
    g.fillRect (Rectangle<float> (4.0f, body.getBottom() - lid.getY()).withCentre ({ knot.x, (lid.getY() + body.getBottom()) * 0.5f }));
    for (float dir : { -1.0f, 1.0f })
    {
        Path loop;
        loop.addEllipse (Rectangle<float> (9.0f, 6.0f).withCentre ({ knot.x + dir * 5.0f, lid.getY() - 2.0f }));
        g.strokePath (loop, PathStrokeType (2.0f));
    }
    g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre ({ knot.x, lid.getY() - 1.5f }));
}

// a flat cartoon cloud: overlapping puffs, darker silhouette line, a lighter top. dark 0..1 = storm.
void FreakiePieEditor::drawCloud (Graphics& g, Point<float> c, float s, float dark) const
{
    Path cl;
    const float puffs2[][3] = { { -30, 4, 18 }, { -12, -8, 22 }, { 12, -12, 24 }, { 32, 0, 18 }, { 0, 8, 20 } };
    for (auto& p : puffs2) cl.addEllipse (c.x + p[0] * s - p[2] * s, c.y + p[1] * s - p[2] * s, 2 * p[2] * s, 2 * p[2] * s);
    const auto fill = Colour (0xffc7d2ea).interpolatedWith (Colour (0xff4a5677), dark);
    const auto line = Colour (0xff7d8db3).interpolatedWith (Colour (0xff262f47), dark);
    g.setColour (line);
    g.strokePath (cl, PathStrokeType (2.8f));
    g.setColour (fill);
    g.fillPath (cl);
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (cl);
    g.setColour (fill.darker (0.25f));
    g.fillRect (Rectangle<float> (c.x - 60.0f * s, c.y + 6.0f * s, 120.0f * s, 40.0f * s));
    g.setColour (fill.brighter (0.25f));
    g.fillEllipse (c.x - 22.0f * s, c.y - 30.0f * s, 26.0f * s, 12.0f * s);
}

// the helium tank on the floor, with its hose to the bow
void FreakiePieEditor::drawTank (Graphics& g) const
{
    // its rail: slide it left and the frozen grains go back through the effects, right and they go straight out
    {
        const float y = (float) designH - 9.0f, x0 = railLeft + 8.0f, x1 = railRight + tankArea.getWidth() - 8.0f;
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (x0, y - 2.0f, x1 - x0, 4.0f, 2.0f);
        const float k = thruValue;
        g.setColour (Colour (0xff8ce0d0).withAlpha (0.25f + 0.5f * k));
        g.fillRoundedRectangle (x0, y - 1.0f, (tankArea.getCentreX() - x0), 2.0f, 1.0f);
        g.setFont (SadLookAndFeel::uiFont (8.5f, true));
        g.setColour (palette::blush.withAlpha (0.35f + 0.5f * k));
        g.drawText ("< thru fx", Rectangle<float> (x0 - 52.0f, y - 6.0f, 50.0f, 12.0f), Justification::centredRight);
        g.setColour (palette::blush.withAlpha (0.35f + 0.5f * (1.0f - k)));
        g.drawText ("out >", Rectangle<float> (x1 + 3.0f, y - 6.0f, 40.0f, 12.0f), Justification::centredLeft);
    }
    const auto& bow = anchors[0].pos;
    const auto body = Rectangle<float> (tankArea.getX() + 4.0f, tankArea.getY() + 20.0f, 38.0f, 54.0f);
    const Point<float> nozzle (body.getCentreX() + 12.0f, body.getY() - 10.0f);

    Path hose;
    hose.startNewSubPath (nozzle);
    hose.cubicTo (nozzle.x + 30.0f, nozzle.y - 30.0f, bow.x - 60.0f, bow.y + 30.0f, bow.x - 6.0f, bow.y + 4.0f);
    g.setColour (Colour (0xff3e4a68));
    g.strokePath (hose, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff8a9bc4).withAlpha (0.6f + 0.4f * heliumVis));
    g.strokePath (hose, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    const auto fill = Colour (0xffb9c6e0), line = Colour (0xff5c6b8c);
    Path cyl;
    cyl.addRoundedRectangle (body, 10.0f);
    g.setGradientFill (ColourGradient (fill.brighter (0.2f), body.getX(), 0, fill.darker (0.35f), body.getRight(), 0, false));
    g.fillPath (cyl);
    g.setColour (Colours::white.withAlpha (0.8f));
    g.fillRoundedRectangle (body.getX() + 6.0f, body.getY() + 8.0f, 4.0f, body.getHeight() - 18.0f, 2.0f);
    g.setColour (line);  g.strokePath (cyl, PathStrokeType (1.4f));
    g.setFont (SadLookAndFeel::uiFont (13.0f, true));
    g.drawText ("He", body.withTrimmedTop (14.0f).withHeight (18.0f), Justification::centred);

    g.setColour (fill);
    g.fillRect (body.getCentreX() - 5.0f, body.getY() - 8.0f, 10.0f, 9.0f);
    g.setColour (line);
    g.drawRect (body.getCentreX() - 5.0f, body.getY() - 8.0f, 10.0f, 9.0f, 1.4f);
    g.drawLine (body.getCentreX() + 5.0f, body.getY() - 5.0f, nozzle.x, nozzle.y, 3.0f);
    const Point<float> vc (body.getCentreX(), body.getY() - 14.0f);
    g.setColour (heliumOn() ? Colour (0xff7fd96a) : Colour (0xffe8607a));
    g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (vc));
    g.setColour (line);
    g.drawEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (vc), 1.4f);
    for (int i = 0; i < 3; ++i)
    {
        const float a = valveAngle + (float) i * MathConstants<float>::twoPi / 3.0f;
        g.drawLine (vc.x, vc.y, vc.x + std::cos (a) * 7.0f, vc.y + std::sin (a) * 7.0f, 1.4f);
    }
    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    g.setColour (palette::blush.withAlpha (heliumOn() ? 0.95f : 0.6f));
    g.drawText (heliumOn() ? "helium: on" : "helium", Rectangle<float> (tankArea.getX() - 30.0f, tankArea.getBottom() + 1.0f, tankArea.getWidth() + 60.0f, 12.0f),
                Justification::centred);
}

// a hand pump: cylinder, a T-handle that pumps while you hold it, a spout with the balloon on it
void FreakiePieEditor::drawPump (Graphics& g, const Pump& p, bool sound) const
{
    const float baseX = p.nozzle.x - 22.0f;
    const float floorY = (float) designH - 6.0f;
    const auto body = Rectangle<float> (baseX - 12.0f, p.nozzle.y + 8.0f, 24.0f, floorY - p.nozzle.y - 14.0f);
    const auto col = sound ? Colour (0xff1aa7a9) : Colour (0xffe8537f);

    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (60.0f, 8.0f).withCentre ({ baseX, floorY }));
    g.setColour (Colour (0xff2b3247));
    g.fillRoundedRectangle (Rectangle<float> (46.0f, 7.0f).withCentre ({ baseX, floorY - 3.0f }), 3.0f);

    // handle rod + T bar, going up and down while held
    const float pumpY = p.holding ? 8.0f * (0.5f + 0.5f * std::sin (p.handle)) : 0.0f;
    const float rodTop = body.getY() - 22.0f + pumpY;
    g.setColour (Colour (0xffb7bfd3));
    g.fillRect (baseX - 1.5f, rodTop, 3.0f, body.getY() - rodTop + 2.0f);
    g.setColour (Colour (0xff2b3247));
    g.fillRoundedRectangle (Rectangle<float> (30.0f, 7.0f).withCentre ({ baseX, rodTop }), 3.5f);

    // cylinder
    g.setGradientFill (ColourGradient (col.brighter (0.35f), body.getX(), 0, col.darker (0.6f), body.getRight(), 0, false));
    g.fillRoundedRectangle (body, 6.0f);
    g.setColour (Colours::white.withAlpha (0.55f));
    g.fillRoundedRectangle (body.getX() + 4.0f, body.getY() + 6.0f, 3.0f, body.getHeight() - 14.0f, 1.5f);
    if (sound)   // a little mic grille
    {
        const auto mc = Point<float> (body.getCentreX(), body.getY() + 20.0f);
        g.setColour (Colour (0xff0d2a33));
        g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (mc));
        g.setColour (Colours::white.withAlpha (0.4f));
        for (int i = -1; i <= 1; ++i) g.drawLine (mc.x - 5.0f, mc.y + (float) i * 3.0f, mc.x + 5.0f, mc.y + (float) i * 3.0f, 0.8f);
    }
    // spout to the nozzle
    Path sp;
    sp.startNewSubPath (body.getRight() - 2.0f, body.getY() + 6.0f);
    sp.quadraticTo (p.nozzle.x, body.getY() + 6.0f, p.nozzle.x, p.nozzle.y + 2.0f);
    g.setColour (Colour (0xff2b3247));
    g.strokePath (sp, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));

    ignoreUnused (sound);
}

void FreakiePieEditor::paintContent (Graphics& g)
{
    const auto full = Rectangle<float> (0, 0, (float) designW, (float) designH);

    applyTheme();

    // the background: the preset's colours, its picture (cross-fading from the last one), and a veil
    // in the picture's own deep colour so the balloons stay easy to see
    g.setGradientFill (ColourGradient (theme.deep, 0, 0, theme.deep.brighter (0.45f), 0, full.getBottom(), false));
    g.fillRect (full);
    {
        const float u = ease (bgFade);
        auto picture = [&] (int bg, float alpha)
        {
            const auto& img = background (bg);
            if (! img.isValid() || alpha <= 0.001f) return;
            g.setOpacity (alpha);
            g.drawImage (img, full, RectanglePlacement::fillDestination);
            g.setOpacity (1.0f);
        };
        if (bgFrom != bgTo) picture (bgFrom, 1.0f - u);
        picture (bgTo, bgFrom == bgTo ? 1.0f : u);
        const float veil = (bgTo >= 0 ? u : 0.0f) + (bgFrom >= 0 && bgFrom != bgTo ? 1.0f - u : 0.0f);
        if (veil > 0.0f)
        {
            g.setGradientFill (ColourGradient (theme.deep.withAlpha (0.42f * veil), 0, 0, theme.deep.withAlpha (0.66f * veil), 0, full.getBottom(), false));
            g.fillRect (full);
        }
    }


    g.setGradientFill (ColourGradient (palette::dustyPink.withAlpha (0.07f), designW * 0.5f, designH * 0.6f,
                                       palette::dustyPink.withAlpha (0.0f), designW * 0.5f + 520.0f, designH * 0.6f, true));
    g.fillRect (full);
    g.setGradientFill (ColourGradient (Colour (0x00000000), 0, designH - 70.0f, Colour (0x55000000), 0, (float) designH, false));
    g.fillRect (full.withTop (designH - 70.0f));

    // (no title: the weather bar sits where it was)

    if (rainVis > 0.0f)
    {
        g.setColour (Colour (0xff070b14).withAlpha (0.38f * rainVis));
        g.fillRect (full);
        const float drop = -60.0f + 100.0f * fp::smoothstep (rainVis);
        const float xs[] = { 90.0f, 260.0f, 440.0f, 620.0f, 800.0f, 980.0f };
        for (int i = 0; i < 6; ++i)
            drawCloud (g, { xs[i] + 14.0f * std::sin (lnf.time * 0.12f + (float) i), drop + (float) ((i * 37) % 23) },
                       1.4f + 0.25f * (float) (i % 3), 1.0f);
    }
    if (cloudVis > 0.0f)
    {
        g.setColour (Colour (0xff0d1220).withAlpha (0.16f * cloudVis));
        g.fillRect (full);
    }
    if (snowVis > 0.0f || frozen > 0.0f)
    {
        // a cold, pale light that deepens as everything freezes
        g.setColour (Colour (0xffcfe6ff).withAlpha (0.06f * snowVis + 0.10f * frozen));
        g.fillRect (full);
    }
    if (heatVis > 0.0f) drawHeatShimmer (g, full);
    if (heliumVis > 0.0f)
    {
        const auto& bow = anchors[0].pos;
        g.setGradientFill (ColourGradient (Colour (0xffd8f3ff).withAlpha (0.10f * heliumVis), bow.x, bow.y - 120.0f,
                                           Colour (0x00d8f3ff), bow.x + 420.0f, bow.y - 120.0f, true));
        g.fillRect (full);
    }

    drawSphereBack (g);
    drawEnergyMap (g);
    drawJar (g);
    drawTank (g);
    drawPump (g, popPump, false);
    drawPump (g, soundPump, true);

    // the balloons on the pumps
    if (popPump.size > 0.0f || popPump.holding)
        lnf.drawBalloon (g, pumpBalloonShape (popPump), popPump.hue,
                         popPump.holding ? semisText (proc.balloonFX.toneSemis.load()) : String(), "", false, 0.0f, 0.0f, 0.0f, 11.0f);
    if (soundPump.slot >= 0)
    {
        const float murk = (1.0f - std::pow (soundPump.size, 0.85f)) * 0.85f;   // the filter opens as it fills
        const auto sh = pumpBalloonShape (soundPump);
        lnf.drawBalloon (g, sh, soundPump.hue, semisText (nextSemis), "", false, 0.0f, 0.0f, murk, 12.0f);
        g.setColour (Colour (0xffff4a4a).withAlpha (0.6f + 0.4f * std::sin ((float) clock * 8.0f)));
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ sh.cx + sh.rx * 0.55f, sh.cy - sh.ry * 0.55f }));
    }
    // next pitch
    {
        const auto r = nextBadge;
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (balloonColours (balloonTeal).fill.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
        g.setFont (SadLookAndFeel::uiFont (10.5f, true));
        g.setColour (palette::blush.withAlpha (0.9f));
        g.drawText ("next " + semisText (nextSemis), r, Justification::centred);
        if (fullFlash > 0.0f)
        {
            g.setColour (Colour (0xffffb0c8).withAlpha (fullFlash));
            g.drawText ("6 max: pop one", r.translated (0.0f, -24.0f).withWidth (110.0f), Justification::centredLeft);
        }
    }

    // puddle + ripples
    if (puddle > 0.01f)
    {
        const auto& bow = anchors[0].pos;
        const float w = 60.0f + 420.0f * puddle, ht = 8.0f + 10.0f * puddle;
        auto pr = Rectangle<float> (w, ht).withCentre ({ bow.x, (float) designH - 6.0f });
        g.setColour (Colour (0xff6f8fc8).withAlpha (0.35f));
        g.fillEllipse (pr);
        g.setColour (Colour (0xffcfe4ff).withAlpha (0.45f));
        g.drawEllipse (pr, 1.2f);
        g.fillEllipse (pr.withSizeKeepingCentre (w * 0.3f, 2.0f).translated (-w * 0.15f, -ht * 0.2f));
    }
    for (auto& r : ripples)
    {
        const float a = 1.0f - r.age;
        g.setColour (Colour (0xffcfe4ff).withAlpha (0.5f * a));
        g.drawEllipse (Rectangle<float> (6.0f + 26.0f * r.age, 2.0f + 6.0f * r.age).withCentre (r.p), 1.0f);
    }

    drawSnowbank (g);
    // (no shelf any more: the order is changed with the dice, and reads left to right)

    // strings: thin satin ribbon; taut ones are straight, slack ones droop to the floor
    for (auto* b : floaters)
    {
        if (! b->alive) continue;
        const auto& a = anchors[b->anchor].pos;
        const auto k = b->knotWorld();
        const float dist = k.getDistanceFrom (a);
        const float slack = jmax (0.0f, b->stringLength - dist);
        auto mid = (k + a) * 0.5f + Point<float> (0.0f, std::sqrt (slack * (b->stringLength + dist)) * 0.5f);
        mid.y = jmin (mid.y, (float) designH - 3.0f);
        Path str;
        str.startNewSubPath (k);
        str.quadraticTo (mid, a);
        g.setColour (Colour (0xff6c6478).withAlpha (0.8f));
        g.strokePath (str, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xffeae4f0).withAlpha (0.55f));
        g.strokePath (str, PathStrokeType (0.7f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (-0.4f, -0.4f));
    }
    for (auto& a : anchors)
    {
        if (a.fixed || a.hidden) continue;
        // standing on the sphere's floor, smaller the further away
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::scale (0.8f * a.scale, 0.8f * a.scale, a.pos.x, a.pos.y));
        if (! a.box)
        {
            g.setColour (Colours::black.withAlpha (0.25f));
            g.fillEllipse (Rectangle<float> (40.0f, 7.0f).withCentre ({ a.pos.x, a.pos.y + 14.0f }));
        }
        if (a.box) drawGiftBox (g, a.pos, a.hue);
        else drawBow (g, a.pos, a.hue);
    }

    // the pincushion (a little tomato)
    if (pin != nullptr)
    {
        const auto c = pin->home + Point<float> (-14.0f, 30.0f);
        auto body = Rectangle<float> (40.0f, 26.0f).withCentre (c);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillEllipse (body.translated (0.0f, 5.0f).withSizeKeepingCentre (44.0f, 10.0f));
        g.setGradientFill (ColourGradient (Colour (0xffff6b6b), body.getX() + 10.0f, body.getY() + 4.0f, Colour (0xffa0182c), body.getRight(), body.getBottom(), true));
        g.fillEllipse (body);
        g.setColour (Colour (0xff7a1022).withAlpha (0.6f));
        for (int i = -1; i <= 1; ++i)
            g.drawLine (c.x + (float) i * 9.0f, body.getY() + 3.0f, c.x + (float) i * 11.0f, body.getBottom() - 3.0f, 1.0f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillEllipse (body.getX() + 8.0f, body.getY() + 4.0f, 9.0f, 5.0f);
        Path leaf;
        leaf.addStar (c.translated (0.0f, -12.0f), 5, 3.0f, 9.0f, 0.3f);
        g.setColour (Colour (0xff3fae5a));
        g.fillPath (leaf);
        g.setFont (SadLookAndFeel::uiFont (10.0f, true));
        g.setColour (palette::blush.withAlpha (0.7f));
        g.drawText ("pin", Rectangle<float> (60.0f, 12.0f).withCentre ({ c.x, body.getBottom() + 9.0f }), Justification::centred);
    }

    drawPinkie (g);

    // the chalkboard, standing on the classroom floor (click it to draw a grain shape)
    if (chalkboard != nullptr && ! chalkboard->isVisible())
    {
        chalkboard->drawEasel (g, chalkboard->restArea(), 0.0f);
        g.setFont (Font (FontOptions ("Chalkboard SE", 9.0f, Font::plain)));
        g.setColour (palette::blush.withAlpha (0.6f));
        g.drawText ("grains + eq", chalkboard->restArea().withY (chalkboard->restArea().getBottom() - 2.0f).withHeight (12.0f).expanded (20.0f, 0.0f),
                    Justification::centred);
    }

    // the switches: round toy tokens
    for (auto* pl : pills) drawToken (g, *pl);

    drawStoreSign (g);
    drawHelpButton (g);
    drawResetButton (g);
    drawPopsButton (g);

    // (the weather switches are drawn by the weather row, on its own layer above the balloons)
    if (false)
    {
        const auto c = cloudButton.getCentre().translated (0.0f, -6.0f);
        drawCloud (g, c, 0.62f, rainOn() ? 1.0f : 0.0f);
        if (rainOn())
            for (int i = 0; i < 4; ++i)
            {
                const float u = frac2 ((float) clock * 1.6f + (float) i * 0.27f);
                const float x = c.x - 18.0f + (float) i * 12.0f, y = c.y + 14.0f + u * 16.0f;
                g.setColour (Colour (0xffbcd6ff).withAlpha (1.0f - u));
                g.drawLine (x, y, x - 1.5f, y + 6.0f, 1.6f);
            }
        g.setFont (SadLookAndFeel::uiFont (10.5f, true));
        g.setColour (palette::blush.withAlpha (rainOn() ? 0.95f : 0.6f));
        g.drawText (rainOn() ? "rain: on" : "rain", cloudButton.withTop (cloudButton.getBottom() - 14.0f), Justification::centred);
    }

}

// over the balloons: rain, splashes, helium puffs, confetti, rubber shreds, POP!
void FreakiePieEditor::paintOver (Graphics& g)
{
    drawSphereFront (g);
    drawSkyClouds (g);   // over the balloons: the ones inside peek through
    for (auto& f : flakes)
    {
        const float tw = 0.7f + 0.3f * std::sin ((float) clock * 3.0f + f.phase);
        g.setColour (Colours::white.withAlpha (0.85f * tw));
        g.fillEllipse (Rectangle<float> (f.size, f.size).withCentre (f.p));
        if (f.size > 4.5f)   // the big ones are little stars
        {
            g.setColour (Colours::white.withAlpha (0.55f * tw));
            for (int a = 0; a < 3; ++a)
            {
                const float ang = (float) a * MathConstants<float>::pi / 3.0f + f.phase;
                g.drawLine (f.p.x - std::cos (ang) * f.size, f.p.y - std::sin (ang) * f.size, f.p.x + std::cos (ang) * f.size, f.p.y + std::sin (ang) * f.size, 0.8f);
            }
        }
    }
    for (auto& d : drops)
    {
        g.setColour (Colour (0xffbcd6ff).withAlpha (0.8f));
        g.drawLine (Line<float> (d.p, d.p - d.v * (d.bounced ? 0.012f : 0.028f)), d.bounced ? 1.4f : 1.8f);
    }
    for (auto& pf : puffs)
    {
        const float a = jlimit (0.0f, 1.0f, pf.life);
        g.setColour (Colours::white.withAlpha (0.35f * a));
        g.fillEllipse (Rectangle<float> (4.0f + 10.0f * (1.0f - a), 4.0f + 10.0f * (1.0f - a)).withCentre (pf.p));
    }
    for (auto& pt : particles)
    {
        g.setColour (pt.c.withAlpha (jlimit (0.0f, 1.0f, pt.life * 1.5f)));
        if (pt.kind == 1)
            g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (pt.p));
        else if (pt.kind == 2)
        {
            Path sh;
            sh.startNewSubPath (-6.0f, -2.0f); sh.lineTo (5.0f, -4.0f); sh.lineTo (3.0f, 4.0f); sh.closeSubPath();
            g.fillPath (sh, AffineTransform::rotation (pt.rot).translated (pt.p));
        }
        else
        {
            Path r;
            r.addRectangle (-4.0f, -1.8f, 8.0f, 3.6f);
            g.fillPath (r, AffineTransform::rotation (pt.rot).scaled (1.0f, 0.6f + 0.4f * std::abs (std::sin (pt.rot * 1.3f))).translated (pt.p));
        }
    }
    for (auto& b : bursts)
    {
        const float u = b.age / 0.5f;
        const float a = 1.0f - u;
        const float R = 30.0f * b.size * (1.0f + 1.5f * u);
        Path star;
        for (int i = 0; i < 24; ++i)
        {
            const float ang = (float) i * MathConstants<float>::twoPi / 24.0f;
            const float rr = (i % 2 == 0 ? R : R * 0.62f);
            const Point<float> q (b.p.x + std::cos (ang) * rr, b.p.y + std::sin (ang) * rr);
            if (i == 0) star.startNewSubPath (q); else star.lineTo (q);
        }
        star.closeSubPath();
        g.setColour (Colour (0xffffe45c).withAlpha (0.9f * a));
        g.fillPath (star);
        g.setColour (Colour (0xffe2457a).withAlpha (a));
        g.strokePath (star, PathStrokeType (2.0f));
        g.setFont (SadLookAndFeel::uiFont (14.0f + 10.0f * b.size, true));
        g.drawText ("POP!", Rectangle<float> (R * 2.0f, R).withCentre (b.p), Justification::centred);
    }
    drawTrixie (g);
    if (flash > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (0.3f * flash * flash));
        g.fillRect (Rectangle<float> (0, 0, (float) designW, (float) designH));
        const float a = jmin (1.0f, flash * 1.6f);
        g.setColour (Colour (0xffbfe0ff).withAlpha (0.5f * a));
        g.strokePath (bolt, PathStrokeType (8.0f, PathStrokeType::mitered, PathStrokeType::rounded));
        g.setColour (Colours::white.withAlpha (a));
        g.strokePath (bolt, PathStrokeType (2.4f, PathStrokeType::mitered, PathStrokeType::rounded));
    }
}

//==============================================================================
// the order dice
void FreakiePieEditor::rollOrder()
{
    auto visible = shelfItems();
    if (visible.size() < 2) return;
    const auto before = visible;
    for (int tries = 0; tries < 8 && visible == before; ++tries)
        for (int i = (int) visible.size() - 1; i > 0; --i) std::swap (visible[(size_t) i], visible[(size_t) rnd.nextInt (i + 1)]);
    setVisibleOrder (visible);
}

// the effects in this order (the ones on the shelf; anything hidden keeps its place after them)
void FreakiePieEditor::setVisibleOrder (const std::vector<int>& visible)
{
    if (visible == shelfItems()) return;
    confetti ({ (shelfLeft + shelfRight) * 0.5f, shelfY + 20.0f }, 14);
    const auto old = proc.getShelfOrder();
    FreakiePieProcessor::Order o; o.fill (-1);
    size_t n = 0;
    for (int slot : visible) o[n++] = slot;
    for (int slot : old) if (std::find (visible.begin(), visible.end(), slot) == visible.end()) o[n++] = slot;
    proc.setShelfOrder (o);
    layoutShelf (visible);   // the bows and boxes hop to their new places
    for (int slot : visible) anchors[anchorOfSlot (slot)].vel.y -= 160.0f;
    noteActivity();
}

// The card waits above the window and the order list off the left edge; each slides in while the
// cursor is near it (and stays while you're using it).
void FreakiePieEditor::revealStep (float dt)
{
    const auto m = content.getMouseXYRelative().toFloat();
    const bool over = isShowing() && getScreenBounds().contains (Desktop::getMousePosition())
                      && (chalkboard == nullptr || ! chalkboard->isVisible()) && (library == nullptr || ! library->isVisible());
    // the card: near its corner of the window, or anywhere on it once it's out
    const auto home = cardHome.toFloat();
    const bool nearCard = over && (cardShown > 0.5f ? home.expanded (18.0f).contains (m)
                                                    : m.y < home.getBottom() && m.x < home.getRight() + 10.0f);
    const bool wantCard = nearCard || scaleBox.isPopupActive();
    const float before = cardShown;
    cardShown = jlimit (0.0f, 1.0f, cardShown + (wantCard ? dt * 6.0f : -dt * 3.0f));
    if (cardShown != before) placeCard();

    // the order list: the cursor at the left edge, or over the list once it's out
    if (orderRow != nullptr)
    {
        const auto list = orderRow->listArea();
        const bool nearList = over && (orderRow->shownAmount() > 0.5f ? list.expanded (20.0f).contains (m)
                                                                      : m.x < 28.0f && m.y > list.getY() - 10.0f && m.y < list.getBottom() + 10.0f);
        orderRow->step (dt, nearList || orderRow->isDragging());
    }
}

// the card: the scale and where the shift lands, the preset (its boxes are the panel's children) and the bypass
void FreakiePieEditor::drawCard (Graphics& g) const
{
    auto rf = infoCard.toFloat();
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (rf.translated (0, 2), 12.0f);
    g.setColour (palette::slate.withAlpha (0.88f));
    g.fillRoundedRectangle (rf, 12.0f);
    g.setColour (palette::dustyPink.withAlpha (0.25f));
    g.drawRoundedRectangle (rf.reduced (0.5f), 12.0f, 1.0f);
    g.setFont (SadLookAndFeel::uiFont (10.5f));
    g.setColour (palette::tearBlue.withAlpha (0.9f));
    g.drawText (intervalText + "   " + freezeText, infoCard.withTrimmedTop (59).withHeight (20), Justification::centred);
    drawBypassSwitch (g);
}

void FreakiePieEditor::CardPanel::paint (Graphics& g)
{
    g.addTransform (AffineTransform::translation ((float) -getX(), (float) -getY()));   // drawn in the room's coordinates
    ed.drawCard (g);
}

void FreakiePieEditor::CardPanel::mouseDown (const MouseEvent& e)
{
    ed.noteActivity();
    if (ed.bypassButton.contains (e.position + getPosition().toFloat()) && ed.bypassAtt != nullptr)
    {
        ed.bypassAtt->setValueAsCompleteGesture (ed.bypassOn() ? 0.0f : 1.0f);
        ed.confetti (ed.bypassButton.getCentre(), 10);
    }
}

void FreakiePieEditor::placeCard()
{
    const float u = fp::smoothstep (cardShown);
    const int dy = -roundToInt ((float) (cardHome.getBottom() + 8) * (1.0f - u));
    infoCard = cardHome.translated (0, dy);
    bypassButton = { (float) infoCard.getX() + 8.0f, (float) infoCard.getY() + 82.0f, (float) infoCard.getWidth() - 16.0f, 20.0f };
    if (cardPanel != nullptr)
    {
        cardPanel->setBounds (infoCard);
        scaleBox.setBounds (8, 34, infoCard.getWidth() - 16, 22);
        presetBox.setBounds (8, 7, infoCard.getWidth() - 16, 22);
        cardPanel->setVisible (cardShown > 0.0f);
    }
}
//==============================================================================
StoreShelves::StoreShelves (FreakiePieEditor& e) : ed (e)
{
    setWantsKeyboardFocus (true);
    setSize ((int) W, (int) H);
}

Rectangle<float> StoreShelves::tileArea (int i) const noexcept
{
    const float left = 14.0f, top = 62.0f, tw = (W - 2.0f * left) / (float) cols, th = 82.0f;
    return { left + tw * (float) (i % cols), top + th * (float) (i / cols), tw, th - 12.0f };
}

int StoreShelves::tileAt (Point<float> p) const noexcept
{
    for (int i = 0; i <= FreakiePieEditor::kStoreItems; ++i)
        if (tileArea (i).contains (p)) return i;
    return -1;
}

void StoreShelves::open (Rectangle<float> under)
{
    setBounds ((int) (under.getRight() - W), (int) (under.getBottom() + 4.0f), (int) W, (int) H);
    opening = true;
    hover = -1;
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void StoreShelves::close()
{
    opening = false;
    hover = -1;
}

void StoreShelves::step (float dt)
{
    if (! isVisible()) return;
    t += dt;
    shown += ((opening ? 1.0f : 0.0f) - shown) * jmin (1.0f, dt * 14.0f);
    if (! opening && shown < 0.02f) { shown = 0.0f; setVisible (false); }
    if (opening && shown > 0.995f) shown = 1.0f;
    // it pops open from the sign: grows from its top-right corner
    const float k = 0.85f + 0.15f * fp::smoothstep (shown);
    setTransform (AffineTransform::scale (k, k, (float) getX() + W, (float) getY()));
    setAlpha (jmin (1.0f, shown * 1.4f));
    repaint();
}

bool StoreShelves::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    return false;
}

void StoreShelves::mouseMove (const MouseEvent& e) { const int h = tileAt (e.position); if (h != hover) { hover = h; repaint(); } }
void StoreShelves::mouseExit (const MouseEvent&)   { hover = -1; repaint(); }

void StoreShelves::mouseDown (const MouseEvent& e)
{
    const int i = tileAt (e.position);
    if (i < 0 || ! opening) return;
    ed.noteActivity();
    if (i == FreakiePieEditor::kStoreItems)
    {
        if (! std::any_of (std::begin (ed.storeShown), std::end (ed.storeShown), [] (bool b) { return b; })) return;
        ed.proc.popStruckRequest.fetch_or ((1u << fp::StoreFX::kNum) - 1u);
        for (int fx = 0; fx < FreakiePieEditor::kStoreItems; ++fx) ed.setBought (fx, false);
        return;
    }
    ed.setBought (i, ! ed.bought (i));
}

void StoreShelves::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    // the shop: pink wallpaper with sprinkles, a candy-stripe awning
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (r.translated (0.0f, 4.0f), 16.0f);
    g.setColour (Colour (0xfffff4f8));
    g.fillRoundedRectangle (r, 16.0f);
    {
        Graphics::ScopedSaveState ss (g);
        Path clip; clip.addRoundedRectangle (r, 16.0f);
        g.reduceClipRegion (clip);
        Random rr (5);
        for (int i = 0; i < 50; ++i)
        {
            static const uint32 cols[] = { 0xffffb3c7, 0xffb8f0c8, 0xffa8d4ff, 0xfffff1a8, 0xffc7b3ff };
            g.setColour (Colour (cols[i % 5]).withAlpha (0.5f));
            g.fillRoundedRectangle (Rectangle<float> (5.0f, 2.0f).withCentre ({ rr.nextFloat() * W, 30.0f + rr.nextFloat() * (H - 34.0f) }), 1.0f);
        }
        const float sw = 20.0f;
        for (int i = 0; i * sw < W + sw; ++i)
        {
            g.setColour (i % 2 == 0 ? Colour (0xffff8fb8) : Colour (0xffffffff));
            g.fillRect ((float) i * sw, 0.0f, sw, 16.0f);
            g.fillEllipse ((float) i * sw, 10.0f, sw, 12.0f);
        }
    }
    g.setColour (Colour (0xffff8fb8));
    g.drawRoundedRectangle (r, 16.0f, 2.0f);
    g.setFont (SadLookAndFeel::titleFont (26.0f));
    g.setColour (Colour (0xffd9578e));
    g.drawText ("Balloon Store", Rectangle<float> (0.0f, 22.0f, W, 34.0f), Justification::centred);

    const uint32 struck = ed.proc.struckMask.load();
    for (int i = 0; i <= FreakiePieEditor::kStoreItems; ++i)
    {
        const auto a = tileArea (i);
        const bool hov = i == hover;
        if (i % cols == 0)
        {
            // the shelf this row stands on
            const auto plank = Rectangle<float> (10.0f, a.getBottom() + 1.0f, W - 20.0f, 7.0f);
            g.setColour (Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (plank.translated (0.0f, 3.0f), 2.0f);
            g.setGradientFill (ColourGradient (Colour (0xffc48b52), 0.0f, plank.getY(), Colour (0xff8a5a32), 0.0f, plank.getBottom(), false));
            g.fillRoundedRectangle (plank, 2.0f);
        }
        if (hov)
        {
            g.setColour (Colour (0xffffd6e7));
            g.fillRoundedRectangle (a.reduced (3.0f, 0.0f), 10.0f);
        }
        if (i == FreakiePieEditor::kStoreItems)
        {
            // "pop all": a pin in a pincushion
            const bool any = std::any_of (std::begin (ed.storeShown), std::end (ed.storeShown), [] (bool b) { return b; });
            const auto c = Point<float> (a.getCentreX(), a.getY() + 30.0f);
            g.setColour (Colour (0xffe84a5f).withAlpha (any ? 1.0f : 0.4f));
            g.fillEllipse (Rectangle<float> (26.0f, 16.0f).withCentre (c.translated (0.0f, 6.0f)));
            g.setColour (Colour (0xff9a8a9a).withAlpha (any ? 1.0f : 0.4f));
            g.drawLine (c.x + 2.0f, c.y + 2.0f, c.x + 10.0f, c.y - 16.0f, 1.6f);
            g.setColour (Colour (0xffff4060).withAlpha (any ? 1.0f : 0.4f));
            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ c.x + 10.0f, c.y - 16.0f }));
            g.setFont (SadLookAndFeel::uiFont (10.5f, true));
            g.setColour (Colour (any ? 0xff4a2c4a : 0x884a2c4a));
            g.drawFittedText ("pop everything", a.withTop (a.getBottom() - 26.0f).toNearestInt(), Justification::centred, 2);
            continue;
        }
        const bool owned = ed.bought (i), lent = ! owned && i != FreakiePieEditor::stutterItem && ((struck >> i) & 1u) != 0;
        const auto hue = ed.storeBalloons[i][0] != nullptr ? balloonColours (ed.storeBalloons[i][0]->hue).fill : Colour (0xffff8fb8);
        // the balloon: bobbing if you're pointing at it, swaying gently if it's yours
        const float bob = hov ? 2.5f * std::sin (t * 6.0f) : (owned ? 1.2f * std::sin (t * 2.0f + (float) i) : 0.0f);
        const auto bc = Point<float> (a.getCentreX(), a.getY() + 22.0f + bob);
        g.setColour (hue.darker (0.4f));
        Path str;
        str.startNewSubPath (bc.x, bc.y + 15.0f);
        str.quadraticTo (bc.x - 3.0f, bc.y + 22.0f, bc.x + 1.0f, a.getBottom() - 22.0f);
        g.strokePath (str, PathStrokeType (1.0f));
        g.setGradientFill (ColourGradient (hue.brighter (0.5f), bc.x - 6.0f, bc.y - 8.0f, hue.darker (0.15f), bc.x + 8.0f, bc.y + 12.0f, true));
        g.fillEllipse (Rectangle<float> (26.0f, 31.0f).withCentre (bc));
        g.setColour (hue.darker (0.35f));
        g.drawEllipse (Rectangle<float> (26.0f, 31.0f).withCentre (bc), 1.2f);
        Path knot; knot.addTriangle (bc.x - 3.0f, bc.y + 17.0f, bc.x + 3.0f, bc.y + 17.0f, bc.x, bc.y + 14.0f);
        g.fillPath (knot);
        g.setColour (Colours::white.withAlpha (0.8f));
        g.fillEllipse (Rectangle<float> (6.0f, 8.0f).withCentre (bc.translated (-5.5f, -6.5f)));
        if (owned)
        {
            Path heart;
            const float hx = a.getRight() - 14.0f, hy = a.getY() + 10.0f;
            heart.startNewSubPath (hx, hy + 5.0f);
            heart.cubicTo (hx - 9.0f, hy - 1.0f, hx - 5.0f, hy - 9.0f, hx, hy - 3.5f);
            heart.cubicTo (hx + 5.0f, hy - 9.0f, hx + 9.0f, hy - 1.0f, hx, hy + 5.0f);
            g.setColour (Colour (0xffff5c9a));
            g.fillPath (heart);
        }
        else if (lent)
        {
            Path bolt;
            const float bx = a.getRight() - 14.0f, by = a.getY() + 4.0f;
            bolt.addTriangle (bx + 2.0f, by, bx - 5.0f, by + 9.0f, bx + 1.0f, by + 9.0f);
            bolt.addTriangle (bx - 1.0f, by + 7.0f, bx + 5.0f, by + 7.0f, bx - 3.0f, by + 17.0f);
            g.setColour (Colour (0xffffc83c));
            g.fillPath (bolt);
        }
        g.setFont (SadLookAndFeel::uiFont (10.5f, true));
        g.setColour (Colour (0xff4a2c4a));
        g.drawFittedText (FreakiePieEditor::storeItemName (i), a.withTop (a.getBottom() - 24.0f).toNearestInt(), Justification::centred, 2);
    }

    // what the one you're pointing at does
    String line = "point at a balloon to see what it does";
    if (hover >= 0 && hover < FreakiePieEditor::kStoreItems)
    {
        const auto name = FreakiePieEditor::storeItemName (hover);
        line = ed.bought (hover) ? name + ": yours! click to pop it"
             : hover != FreakiePieEditor::stutterItem && ((struck >> hover) & 1u) != 0 ? name + ": from the lightning! click to keep it"
                                              : name + ": " + FreakiePieEditor::storeItemWhat (hover);
    }
    else if (hover == FreakiePieEditor::kStoreItems) line = "pops every effect you bought";
    g.setFont (SadLookAndFeel::uiFont (11.0f, false));
    g.setColour (Colour (0xff9a6b8e));
    g.drawText (line, Rectangle<float> (12.0f, H - 28.0f, W - 24.0f, 20.0f), Justification::centred);
}

//==============================================================================
// the weather row
WeatherBar::WeatherBar (FreakiePieEditor& e) : ed (e)
{
    setBounds ((int) X, (int) Y, (int) W, (int) H);
    setInterceptsMouseClicks (true, false);
}

bool WeatherBar::isOn (int which) const noexcept
{
    switch (which)
    {
        case 0: return ed.cloudyOn();
        case 1: return ed.rainOn();
        case 2: return ed.snowOn();
        case 3: return ed.heatOn();
        default: return ed.autoOn();
    }
}

int WeatherBar::buttonAt (Point<float> p) const noexcept
{
    const Rectangle<float> r[] = { ed.cloudyButton, ed.cloudButton, ed.snowButton, ed.heatButton, ed.autoButton };
    for (int i = 0; i < 5; ++i) if (r[i].contains (p)) return i;
    return -1;
}

bool WeatherBar::hitTest (int x, int y)
{
    return buttonAt (Point<float> ((float) x + X, (float) y + Y)) >= 0;
}

void WeatherBar::toggle (int which)
{
    std::unique_ptr<ParameterAttachment>* atts[] = { &ed.cloudyAtt, &ed.rainAtt, &ed.snowAtt, &ed.heatAtt, &ed.autoAtt };
    if (which < 0 || which > 4 || *atts[which] == nullptr) return;
    const bool turningOn = ! isOn (which);
    // the snow and the heatwave never fall together: turning one on sends the other away
    if (turningOn && which == 2 && ed.heatOn() && ed.heatAtt != nullptr) ed.heatAtt->setValueAsCompleteGesture (0.0f);
    if (turningOn && which == 3 && ed.snowOn() && ed.snowAtt != nullptr) ed.snowAtt->setValueAsCompleteGesture (0.0f);
    (*atts[which])->setValueAsCompleteGesture (turningOn ? 1.0f : 0.0f);
    ed.noteActivity();
}

void WeatherBar::mouseDown (const MouseEvent& e)
{
    const auto p = e.position + Point<float> (X, Y);
    const int b = buttonAt (p);
    if (b < 0) return;
    if (e.mods.isPopupMenu())
    {
        // right-click the snow or the heat: its fade times
        if ((b == 2 || b == 3) && ed.fadePopup != nullptr)
        {
            if (ed.fadePopup->showing() == b) ed.fadePopup->close();
            else ed.fadePopup->open (b, b == 2 ? ed.snowButton : ed.heatButton);
        }
        return;
    }
    if (ed.fadePopup != nullptr && ed.fadePopup->isOpen()) ed.fadePopup->close();
    toggle (b);
}
void WeatherBar::step (float) { repaint(); }   // the sun turns, the rain falls, the hot air ripples

void WeatherBar::paint (Graphics& g)
{
    g.addTransform (AffineTransform::translation (-X, -Y));   // the switches are placed in the room's coordinates
    ed.drawCloudyButton (g);
    ed.drawRainButton (g);
    ed.drawSnowButton (g);
    ed.drawHeatButton (g);
    ed.drawAutoSwitch (g);
}

//==============================================================================
// the order list, down the left edge
OrderRow::OrderRow (FreakiePieEditor& e) : ed (e)
{
    setBounds ((int) (X - W), (int) Y, (int) W, (int) H);   // (starts tucked away off the side)
}

const std::vector<OrderRow::Chip>& OrderRow::layout() const
{
    const auto items = ed.shelfItems();
    if (items != layoutFor || layoutCache.empty() != items.empty()) { layoutFor = items; layoutCache = computeLayout(); }
    return layoutCache;
}

std::vector<OrderRow::Chip> OrderRow::computeLayout() const
{
    std::vector<Chip> v;
    int i = 0;
    for (int slot : ed.shelfItems())
    {
        String name;
        switch (slot)
        {
            case fp::Engine::slotSpectral: name = "smear"; break;
            case fp::Engine::slotPitch:    name = "pitch"; break;
            case fp::Engine::slotGrains:   name = "grains"; break;
            case fp::Engine::slotReverb:   name = "reverb"; break;
            case fp::Engine::slotStutter:  name = "stutter"; break;
            default: name = fp::StoreFX::infos()[(size_t) (slot - fp::Engine::slotStore)].shortName; break;
        }
        const int hue = ed.anchors[FreakiePieEditor::anchorOfSlot (slot)].hue;
        const float w = jmin (W - left - 6.0f, GlyphArrangement::getStringWidth (SadLookAndFeel::uiFont (11.0f, true), name) + 28.0f);
        v.push_back ({ slot, name, balloonColours (hue).fill.darker (0.28f), { left, top + pitch * (float) i++, jmax (70.0f, w), chipH } });
    }
    return v;
}

Rectangle<float> OrderRow::listArea() const noexcept
{
    const auto& chips = layout();
    float right = dieArea().getRight() + 60.0f;
    for (auto& c : chips) right = jmax (right, c.r.getRight());
    const float bottom = chips.empty() ? top : chips.back().r.getBottom() + 14.0f;
    return { X, Y, right + 10.0f, bottom + 4.0f };
}

// the order as it's shown: while dragging, the dragged one sits where it would land
std::vector<int> OrderRow::displayOrder() const
{
    std::vector<int> o;
    for (auto& c : layout()) o.push_back (c.slot);
    if (dragSlot < 0 || dragTo < 0) return o;
    o.erase (std::remove (o.begin(), o.end(), dragSlot), o.end());
    o.insert (o.begin() + jlimit (0, (int) o.size(), dragTo), dragSlot);
    return o;
}

int OrderRow::indexAt (float chipTop) const noexcept
{
    const int n = (int) layout().size();
    return jlimit (0, jmax (0, n - 1), roundToInt ((chipTop - top) / pitch));
}

bool OrderRow::hitTest (int x, int y)
{
    if (shown < 0.5f) return false;
    const auto p = Point<float> ((float) x, (float) y);
    if (dieArea().expanded (2.0f).contains (p)) return true;
    for (auto& c : layout()) if (c.r.expanded (4.0f, 3.0f).contains (p)) return true;
    return false;
}

void OrderRow::mouseMove (const MouseEvent& e)
{
    const bool d = dieArea().expanded (2.0f).contains (e.position);
    int h = -1;
    if (! d) for (auto& c : layout()) if (c.r.expanded (4.0f, 3.0f).contains (e.position)) h = c.slot;
    if (d != hoverDie || h != hoverSlot) { hoverDie = d; hoverSlot = h; repaint(); }
    setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
}

void OrderRow::mouseExit (const MouseEvent&) { hoverDie = false; hoverSlot = -1; repaint(); }

void OrderRow::mouseDown (const MouseEvent& e)
{
    if (dieArea().expanded (2.0f).contains (e.position)) { spin = 1.0f; ed.rollOrder(); return; }
    for (auto& c : layout())
        if (c.r.expanded (4.0f, 3.0f).contains (e.position))
        {
            dragSlot = c.slot;
            grabDy = e.position.y - c.r.getY();
            dragY = downY = e.position.y;
            dragTo = indexAt (c.r.getY());
            moved = false;
            return;
        }
}

void OrderRow::mouseDrag (const MouseEvent& e)
{
    if (dragSlot < 0) return;
    dragY = e.position.y;
    moved |= std::abs (dragY - downY) > 3.0f;
    dragTo = indexAt (dragY - grabDy);
    repaint();
}

void OrderRow::mouseUp (const MouseEvent&)
{
    if (dragSlot < 0) return;
    const auto o = displayOrder();
    shownY[dragSlot] = dragY - grabDy;   // it settles from where you let go
    dragSlot = -1; dragTo = -1;
    if (moved) ed.setVisibleOrder (o);
    repaint();
}

void OrderRow::step (float dt, bool wanted)
{
    t += dt;
    const float before = shown;
    shown = jlimit (0.0f, 1.0f, shown + (wanted ? dt * 6.0f : -dt * 3.0f));
    if (shown != before)
        setTopLeftPosition (roundToInt (X - (listArea().getWidth() + 4.0f) * (1.0f - fp::smoothstep (shown))), (int) Y);
    if (shown <= 0.0f) return;
    spin = jmax (0.0f, spin - dt * 1.6f);
    bool moving = spin > 0.0f || dragSlot >= 0;
    const auto order = displayOrder();
    for (size_t i = 0; i < order.size(); ++i)
    {
        const float y = top + pitch * (float) i;
        auto it = shownY.find (order[i]);
        if (it == shownY.end()) { shownY[order[i]] = y; moving = true; continue; }
        const float d = y - it->second;
        if (std::abs (d) > 0.3f) { it->second += d * jmin (1.0f, dt * 12.0f); moving = true; }
        else it->second = y;
    }
    if (moving) repaint();
}

void OrderRow::paint (Graphics& g)
{
    const auto& chips = layout();
    const auto order = displayOrder();
    // a soft panel behind the list, so it reads over the room
    {
        const auto a = listArea().withPosition (0.0f, 0.0f);
        g.setColour (palette::slate.withAlpha (0.78f));
        g.fillRoundedRectangle (a.withTrimmedLeft (-12.0f), 12.0f);
        g.setColour (palette::dustyPink.withAlpha (0.25f));
        g.drawRoundedRectangle (a.withTrimmedLeft (-12.0f).reduced (0.5f), 12.0f, 1.0f);
        g.setFont (SadLookAndFeel::uiFont (10.0f, true));
        g.setColour (palette::blush.withAlpha (0.8f));
        g.drawText ("in", Rectangle<float> (dieArea().getRight() + 8.0f, 4.0f, 60.0f, 20.0f), Justification::centredLeft);
        g.drawText ("out", Rectangle<float> (left + 2.0f, a.getBottom() - 16.0f, 40.0f, 12.0f), Justification::centredLeft);
    }
    auto drawChip = [&] (const Chip& c, Rectangle<float> r, bool lifted)
    {
        g.setColour (Colours::black.withAlpha (lifted ? 0.35f : 0.22f));
        g.fillRoundedRectangle (r.translated (lifted ? 2.0f : 0.0f, lifted ? 4.0f : 2.0f), r.getHeight() * 0.5f);
        g.setColour (c.colour.brighter (lifted || hoverSlot == c.slot ? 0.15f : 0.0f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        // a little grip on the left: drag me
        g.setColour (Colours::white.withAlpha (0.55f));
        for (int k = 0; k < 3; ++k)
            g.fillRect (r.getX() + 8.0f, r.getCentreY() - 3.5f + 3.0f * (float) k, 6.0f, 1.2f);
        g.setFont (SadLookAndFeel::uiFont (11.0f, true));
        g.setColour (Colours::white);
        g.drawText (c.name, r.withTrimmedLeft (20.0f).withTrimmedRight (6.0f), Justification::centredLeft);
    };
    // the chevrons between them, down the way the sound goes
    for (size_t i = 0; i + 1 < order.size(); ++i)
    {
        const float x = left + 34.0f, y = top + pitch * (float) i + chipH + (pitch - chipH) * 0.5f;
        Path chev;
        chev.startNewSubPath (x - 3.0f, y - 1.4f);
        chev.lineTo (x, y + 1.4f);
        chev.lineTo (x + 3.0f, y - 1.4f);
        g.setColour (Colour (0xffffd6e8).withAlpha (0.8f));
        g.strokePath (chev, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    const Chip* lifted = nullptr;
    for (auto& c : chips)
    {
        if (c.slot == dragSlot) { lifted = &c; continue; }
        auto it = shownY.find (c.slot);
        drawChip (c, c.r.withY (it != shownY.end() ? it->second : c.r.getY()), false);
    }
    if (lifted != nullptr)   // the one in your hand, on top
        drawChip (*lifted, lifted->r.withY (jlimit (top - 6.0f, top + pitch * (float) (chips.size() - 1) + 6.0f, dragY - grabDy)).expanded (2.0f, 1.0f), true);

    // the die: click to roll
    const auto d = dieArea();
    const auto c = d.getCentre();
    Graphics::ScopedSaveState ss (g);
    g.addTransform (AffineTransform::rotation (spin * spin * 6.0f, c.x, c.y));
    const auto face = Rectangle<float> (15.0f, 15.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (face.translated (0.0f, 1.5f), 3.5f);
    g.setColour (hoverDie ? Colour (0xffffeef6) : Colours::white);
    g.fillRoundedRectangle (face, 3.5f);
    g.setColour (balloonColours (balloonPink).fill.darker (0.1f));
    for (auto off : { Point<float> (-3.8f, -3.8f), Point<float> (3.8f, 3.8f), Point<float> (0.0f, 0.0f), Point<float> (-3.8f, 3.8f), Point<float> (3.8f, -3.8f) })
        g.fillEllipse (Rectangle<float> (2.9f, 2.9f).withCentre (c + off));
}

//==============================================================================
// the fade times of the snow or the heat, on a right-click
FadePopup::FadePopup (FreakiePieEditor& e) : ed (e)
{
    setSize ((int) W, (int) H);
    setWantsKeyboardFocus (true);
}

void FadePopup::open (int w, Rectangle<float> under)
{
    which = w;
    params[0] = ed.proc.apvts.getParameter (w == 2 ? ids::snowIn : ids::heatIn);
    params[1] = ed.proc.apvts.getParameter (w == 2 ? ids::snowOut : ids::heatOut);
    setTopLeftPosition (roundToInt (jlimit (6.0f, (float) FreakiePieEditor::designW - W - 6.0f, under.getCentreX() - W * 0.5f)),
                        roundToInt (under.getBottom() + 6.0f));
    setVisible (true);
    toFront (true);
    repaint();
}

void FadePopup::close() { dragRow = -1; setVisible (false); }

bool FadePopup::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    return false;
}

void FadePopup::setFromX (int row, float x)
{
    if (auto* prm = params[row])
    {
        const auto tr = trackArea (row);
        prm->setValueNotifyingHost (jlimit (0.0f, 1.0f, (x - tr.getX()) / tr.getWidth()));
        repaint();
    }
}

void FadePopup::mouseDown (const MouseEvent& e)
{
    for (int row = 0; row < 2; ++row)
        if (params[row] != nullptr && rowArea (row).contains (e.position))
        {
            auto* prm = params[row];
            if (e.getNumberOfClicks() > 1)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (5.0f));
                prm->endChangeGesture();
                repaint();
                return;
            }
            dragRow = row;
            prm->beginChangeGesture();
            if (trackArea (row).expanded (6.0f, 8.0f).contains (e.position)) setFromX (row, e.position.x);
            return;
        }
}

void FadePopup::mouseDrag (const MouseEvent& e) { if (dragRow >= 0) setFromX (dragRow, e.position.x); }

void FadePopup::mouseUp (const MouseEvent&)
{
    if (dragRow >= 0 && params[dragRow] != nullptr) params[dragRow]->endChangeGesture();
    dragRow = -1;
}

void FadePopup::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    for (int row = 0; row < 2; ++row)
        if (params[row] != nullptr && rowArea (row).contains (e.position))
        {
            float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
            if (w.isReversed) d = -d;
            auto* prm = params[row];
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (jlimit (0.0f, 1.0f, prm->getValue() + d * 0.15f));
            prm->endChangeGesture();
            repaint();
            return;
        }
}

void FadePopup::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (b.translated (0.0f, 2.0f), 12.0f);
    g.setColour (palette::slate.withAlpha (0.96f));
    g.fillRoundedRectangle (b, 12.0f);
    const auto accent = which == 2 ? Colour (0xffcfe8ff) : Colour (0xffffa45c);
    g.setColour (accent.withAlpha (0.6f));
    g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.2f);
    g.setFont (SadLookAndFeel::uiFont (11.5f, true));
    g.setColour (palette::blush);
    g.drawText (which == 2 ? "snow" : "heatwave", Rectangle<float> (12.0f, 6.0f, W - 24.0f, 18.0f), Justification::centredLeft);
    g.setFont (SadLookAndFeel::uiFont (9.5f));
    g.setColour (palette::blush.withAlpha (0.6f));
    g.drawText ("double-click: 5 s", Rectangle<float> (12.0f, 6.0f, W - 24.0f, 18.0f), Justification::centredRight);
    const char* words[] = { "fade in", "fade out" };
    for (int row = 0; row < 2; ++row)
    {
        auto* prm = params[row];
        if (prm == nullptr) continue;
        const auto r = rowArea (row), tr = trackArea (row);
        g.setFont (SadLookAndFeel::uiFont (10.5f, true));
        g.setColour (palette::blush.withAlpha (0.9f));
        g.drawText (words[row], r.withWidth (tr.getX() - r.getX() - 4.0f), Justification::centredLeft);
        const float v = prm->getValue();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (tr, tr.getHeight() * 0.5f);
        g.setColour (accent);
        g.fillRoundedRectangle (tr.withWidth (jmax (tr.getHeight(), tr.getWidth() * v)), tr.getHeight() * 0.5f);
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre ({ tr.getX() + tr.getWidth() * v, tr.getCentreY() }));
        g.setFont (SadLookAndFeel::uiFont (10.5f, true));
        g.setColour (palette::blush);
        g.drawText (snowFadeText (prm->convertFrom0to1 (v)), r.withTrimmedLeft (tr.getRight() - r.getX() + 8.0f), Justification::centredLeft);
    }
}

//==============================================================================
// the preset filmstrip
const char* const PresetStrip::groups[4] = { "start", "feedback", "stretch", "useful" };

PresetStrip::PresetStrip (FreakiePieEditor& e) : ed (e)
{
    setWantsKeyboardFocus (true);
    setBounds (0, FreakiePieEditor::designH, FreakiePieEditor::designW, (int) H);
}

Rectangle<float> PresetStrip::chipArea (int g) const noexcept
{
    float x = left;
    for (int i = 0; i < g; ++i) x += GlyphArrangement::getStringWidth (SadLookAndFeel::uiFont (10.0f, true), groups[i]) + 20.0f;
    return { x, 7.0f, GlyphArrangement::getStringWidth (SadLookAndFeel::uiFont (10.0f, true), groups[g]) + 16.0f, 17.0f };
}

float PresetStrip::maxScroll() const noexcept
{
    const int n = (int) presets::list().size();
    return jmax (0.0f, left + (float) n * (frameW + gap) - gap + left - (float) getWidth());
}

int PresetStrip::frameAt (Point<float> p) const noexcept
{
    for (int i = 0; i < (int) presets::list().size(); ++i)
        if (frameArea (i).withHeight (frameH + 15.0f).contains (p)) return i;
    return -1;
}

void PresetStrip::scrollTo (int preset)
{
    scrollTarget = jlimit (0.0f, maxScroll(), left + (float) preset * (frameW + gap) + frameW * 0.5f - (float) getWidth() * 0.5f);
}

const Image& PresetStrip::thumb (int preset)
{
    const int n = (int) presets::list().size();
    if ((int) thumbs.size() != n) thumbs.assign ((size_t) n, Image());
    auto& t = thumbs[(size_t) preset];
    if (! t.isValid())
    {
        const auto& bg = ed.background (presets::list()[(size_t) preset].background);
        if (bg.isValid())
        {
            // cover-fit into the frame, at twice the size for a crisp picture
            const float tw = frameW * 2.0f, th = frameH * 2.0f;
            const float k = jmax (tw / (float) bg.getWidth(), th / (float) bg.getHeight());
            Image img (Image::RGB, (int) tw, (int) th, true);
            Graphics g (img);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImageTransformed (bg, AffineTransform::scale (k).translated ((tw - (float) bg.getWidth() * k) * 0.5f, (th - (float) bg.getHeight() * k) * 0.5f));
            t = img;
        }
    }
    return t;
}

void PresetStrip::open()
{
    opening = true;
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    scrollTo (jmax (0, ed.proc.currentPreset.load()));
    scroll = scrollTarget;
}

void PresetStrip::close() { opening = false; hover = -1; }

bool PresetStrip::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { close(); return true; }
    if (k == KeyPress::leftKey || k == KeyPress::rightKey)
    {
        const int n = (int) presets::list().size();
        const int next = jlimit (0, n - 1, jmax (0, ed.proc.currentPreset.load()) + (k == KeyPress::leftKey ? -1 : 1));
        ed.proc.setCurrentProgram (next);
        scrollTo (next);
        return true;
    }
    return false;
}

void PresetStrip::step (float dt)
{
    if (! isVisible()) return;
    shown += ((opening ? 1.0f : 0.0f) - shown) * jmin (1.0f, dt * 11.0f);
    if (! opening && shown < 0.01f) { shown = 0.0f; setVisible (false); return; }
    if (opening && shown > 0.995f) shown = 1.0f;
    setTopLeftPosition (0, (int) std::round ((float) FreakiePieEditor::designH - H * fp::smoothstep (shown)));
    scroll += (scrollTarget - scroll) * jmin (1.0f, dt * 12.0f);
    repaint();
}

void PresetStrip::mouseMove (const MouseEvent& e) { const int h = frameAt (e.position); if (h != hover) { hover = h; repaint(); } }
void PresetStrip::mouseExit (const MouseEvent&)   { hover = -1; }

void PresetStrip::mouseDown (const MouseEvent& e)
{
    ed.noteActivity();
    if (closeArea().contains (e.position)) { close(); return; }
    for (int g = 0; g < 4; ++g)
        if (chipArea (g).contains (e.position))
        {
            for (int i = 0; i < (int) presets::list().size(); ++i)
                if (String (presets::list()[(size_t) i].group) == groups[g])
                {
                    scrollTarget = jlimit (0.0f, maxScroll(), left + (float) i * (frameW + gap) - left);
                    break;
                }
            return;
        }
    pressed = frameAt (e.position);
    dragged = false;
    downScroll = scrollTarget;
}

void PresetStrip::mouseDrag (const MouseEvent& e)
{
    if (std::abs (e.getDistanceFromDragStartX()) > 4) dragged = true;
    if (dragged) scroll = scrollTarget = jlimit (0.0f, maxScroll(), downScroll - (float) e.getDistanceFromDragStartX());
}

void PresetStrip::mouseUp (const MouseEvent& e)
{
    if (! dragged && pressed >= 0 && frameAt (e.position) == pressed)
    {
        ed.proc.setCurrentProgram (pressed);   // the room fades over to it
        scrollTo (pressed);
        close();
    }
    pressed = -1;
}

void PresetStrip::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    float d = std::abs (w.deltaX) >= std::abs (w.deltaY) ? -w.deltaX : -w.deltaY;
    if (w.isReversed) d = -d;
    scrollTarget = jlimit (0.0f, maxScroll(), scrollTarget + d * 420.0f);
}

void PresetStrip::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xd914101f));
    g.fillRect (r);
    g.setColour (Colour (0xfff6a9cb));
    g.fillRect (r.withHeight (2.0f));

    const int current = ed.proc.currentPreset.load();
    // which kind is in the middle of the strip, for the chips
    int middle = jlimit (0, (int) presets::list().size() - 1, (int) ((scroll + (float) getWidth() * 0.5f - left) / (frameW + gap)));
    const String midGroup = presets::list()[(size_t) middle].group;
    for (int k = 0; k < 4; ++k)
    {
        const auto c = chipArea (k);
        const bool on = midGroup == groups[k];
        g.setColour (on ? Colour (0xfff06aa6) : Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (c, 8.5f);
        g.setFont (SadLookAndFeel::uiFont (10.0f, true));
        g.setColour (on ? Colours::white : Colour (0xffffd6e8));
        g.drawText (groups[k], c, Justification::centred);
    }
    g.setFont (SadLookAndFeel::uiFont (9.5f, false));
    g.setColour (Colour (0xffffd6e8).withAlpha (0.7f));
    g.drawText (String ("scroll or drag along ") + juce::String::fromUTF8 ("\u2194"), Rectangle<float> ((float) getWidth() - 220.0f, 7.0f, 184.0f, 17.0f), Justification::centredRight);
    // close
    {
        const auto c = closeArea();
        g.setColour (Colours::white.withAlpha (0.12f));
        g.fillEllipse (c);
        g.setColour (Colour (0xffffd6e8));
        g.drawLine (c.getX() + 7.0f, c.getY() + 7.0f, c.getRight() - 7.0f, c.getBottom() - 7.0f, 1.4f);
        g.drawLine (c.getRight() - 7.0f, c.getY() + 7.0f, c.getX() + 7.0f, c.getBottom() - 7.0f, 1.4f);
    }

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (Rectangle<int> (0, (int) frameTop - 4, getWidth(), (int) (H - frameTop + 4)));
    for (int i = 0; i < (int) presets::list().size(); ++i)
    {
        const auto f = frameArea (i);
        if (f.getRight() < -10.0f || f.getX() > (float) getWidth() + 10.0f) continue;
        const bool on = i == current, hov = i == hover;
        const float a = on || hov ? 1.0f : 0.78f;
        {
            Graphics::ScopedSaveState s2 (g);
            Path clip; clip.addRoundedRectangle (f, 6.0f);
            g.reduceClipRegion (clip);
            const auto& img = thumb (i);
            if (img.isValid())
            {
                g.setOpacity (a);
                g.drawImage (img, f);
                g.setOpacity (1.0f);
            }
            else
            {
                g.setGradientFill (ColourGradient (Colour (0xff2a2140), f.getX(), f.getY(), Colour (0xff171226), f.getX(), f.getBottom(), false));
                g.fillRect (f);
            }
        }
        if (on || hov)
        {
            g.setColour (on ? Colour (0xfff06aa6) : Colours::white.withAlpha (0.6f));
            g.drawRoundedRectangle (f.expanded (1.0f), 7.0f, on ? 2.5f : 1.2f);
        }
        g.setFont (SadLookAndFeel::uiFont (9.5f, on));
        g.setColour (Colours::white.withAlpha (on || hov ? 1.0f : 0.8f));
        g.drawFittedText (presets::list()[(size_t) i].name, Rectangle<float> (f.getX() - 4.0f, f.getBottom() + 2.0f, f.getWidth() + 8.0f, 13.0f).toNearestInt(), Justification::centred, 1, 0.85f);
    }
}

// the rain switch: a little cloud
void FreakiePieEditor::drawRainButton (Graphics& g) const
{
    const auto c = cloudButton.getCentre().translated (0.0f, -6.0f);
    drawCloud (g, c, 0.62f, rainOn() ? 1.0f : 0.0f);
    if (rainOn())
        for (int i = 0; i < 4; ++i)
        {
            const float u = frac2 ((float) clock * 1.6f + (float) i * 0.27f);
            const float x = c.x - 18.0f + (float) i * 12.0f, y = c.y + 14.0f + u * 16.0f;
            g.setColour (Colour (0xffbcd6ff).withAlpha (1.0f - u));
            g.drawLine (x, y, x - 1.5f, y + 6.0f, 1.6f);
        }
    g.setFont (SadLookAndFeel::uiFont (10.5f, true));
    g.setColour (palette::blush.withAlpha (rainOn() ? 0.95f : 0.6f));
    g.drawText (rainOn() ? "rain: on" : "rain", cloudButton.withTop (cloudButton.getBottom() - 14.0f), Justification::centred);
}

//==============================================================================
// The grains in the sphere
//==============================================================================
int FreakiePieEditor::grainCount() const { return jlimit (1, FreakiePieProcessor::kGrains, roundToInt (grainsValue)); }

void FreakiePieEditor::addGrain (const Vec3* at)
{
    const int n = grainCount();
    if (n >= FreakiePieProcessor::kGrains) { confetti (jarArea.getCentre(), 6); return; }
    if (at != nullptr) proc.setGrainHome (n, *at);
    if (grainsAtt != nullptr) grainsAtt->setValueAsCompleteGesture ((float) (n + 1));
    confetti (at != nullptr ? project (*at * sphereR) : jarArea.getCentre(), 14);
    noteActivity();
}

void FreakiePieEditor::removeGrain()
{
    const int n = grainCount();
    if (n <= 1) return;
    if (auto* m = dynamic_cast<GrainMarker*> (grainMarkers[n - 1])) pop (m->pos, 0.4f, (n - 1) % numHues);
    if (grainsAtt != nullptr) grainsAtt->setValueAsCompleteGesture ((float) (n - 1));
    noteActivity();
}

void FreakiePieEditor::grainStep (float dt)
{
    const int n = grainCount();
    for (int i = 0; i < grainMarkers.size(); ++i)
    {
        auto* m = dynamic_cast<GrainMarker*> (grainMarkers[i]);
        if (m == nullptr) continue;
        if (i == FreakiePieProcessor::kGrains)
        {
            // the source: your dry sound, where you put it (not in copies mode, where every grain carries it)
            const bool show = proc.apvts.getRawParameterValue (ids::grainMode)->load() < 1.5f;
            m->presence += ((show ? 1.0f : 0.0f) - m->presence) * jmin (1.0f, dt * 6.0f);
            if (m->isVisible() != (m->presence > 0.02f)) m->setVisible (m->presence > 0.02f);
            if (! m->isVisible()) continue;
            m->pos = project (Vec3 { proc.srcX.load(), proc.srcY.load(), proc.srcZ.load() } * sphereR, &m->scale, &m->depth);
            m->glow += (jlimit (0.0f, 1.0f, proc.outputLevel.load() * 3.0f) - m->glow) * jmin (1.0f, dt * 10.0f);
            m->place();
            m->repaint();
            continue;
        }
        const auto& v = proc.grainView[(size_t) i];
        m->presence += ((i < n ? jmax (0.3f, v.presence.load()) : 0.0f) - m->presence) * jmin (1.0f, dt * 6.0f);
        const bool show = m->presence > 0.02f;
        if (m->isVisible() != show) m->setVisible (show);
        if (! show) continue;
        const Vec3 w = Vec3 { v.x.load(), v.y.load(), v.z.load() } * sphereR;
        m->pos = project (w, &m->scale, &m->depth);
        m->glow += (jlimit (0.0f, 1.0f, v.level.load() * 6.0f) - m->glow) * jmin (1.0f, dt * 10.0f);
        m->weather = v.weather.load();
        m->frozen = v.frozen.load();
        const int st = v.stutters.load();
        if (st != m->stuttersSeen) { m->stuttersSeen = st; m->hopVel += 160.0f; }   // a stutter: it hops
        m->hopVel += (-900.0f * m->hop - 14.0f * m->hopVel) * dt;
        m->hop += m->hopVel * dt;
        m->place();
        m->repaint();
    }
}

void GrainMarker::place()
{
    setTransform (AffineTransform::scale (scale * (0.7f + 0.3f * presence), scale * (0.7f + 0.3f * presence), S * 0.5f, S * 0.5f)
                      .translated (pos.x - S * 0.5f, pos.y - S * 0.5f - hop * 0.04f));
}

void GrainMarker::paint (Graphics& g)
{
    if (index == FreakiePieProcessor::kGrains)
    {
        // the source: a little loudspeaker with rings coming off it
        const Point<float> c (S * 0.5f, S * 0.5f);
        g.setOpacity (jlimit (0.0f, 1.0f, presence));
        const float gr = 16.0f + 10.0f * glow;
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.35f + 0.4f * glow), c.x, c.y, Colours::white.withAlpha (0.0f), c.x + gr, c.y, true));
        g.fillEllipse (Rectangle<float> (2.0f * gr, 2.0f * gr).withCentre (c));
        Path spk;
        spk.addRectangle (c.x - 9.0f, c.y - 4.0f, 5.0f, 8.0f);
        spk.addTriangle (c.x - 4.0f, c.y - 4.0f, c.x + 3.0f, c.y - 10.0f, c.x + 3.0f, c.y + 10.0f);
        spk.addTriangle (c.x - 4.0f, c.y - 4.0f, c.x + 3.0f, c.y + 10.0f, c.x - 4.0f, c.y + 4.0f);
        g.setColour (balloonColours (balloonPink).fill);
        g.fillPath (spk);
        g.setColour (Colours::white.withAlpha (0.8f));
        for (int k = 1; k <= 2; ++k)
        {
            Path arc;
            arc.addCentredArc (c.x + 3.0f, c.y, 5.0f * (float) k + 2.0f, 5.0f * (float) k + 2.0f, 0.0f, 0.6f, 2.5f, true);
            g.strokePath (arc, PathStrokeType (1.3f));
        }
        g.setFont (SadLookAndFeel::uiFont (8.5f, true));
        g.drawText ("dry", Rectangle<float> (30.0f, 10.0f).withCentre ({ c.x, c.y + 18.0f }), Justification::centred);
        return;
    }
    const Point<float> c (S * 0.5f, S * 0.5f + 12.0f);
    const int hue = index % numHues;
    const auto col = balloonColours (hue).fill;
    g.setOpacity (jlimit (0.0f, 1.0f, presence));
    // its glow: brighter the louder it is
    const float gr = 17.0f + 12.0f * glow;
    g.setGradientFill (ColourGradient (col.brighter (0.6f).withAlpha (0.35f + 0.55f * glow), c.x, c.y - 8.0f, col.withAlpha (0.0f), c.x + gr, c.y - 8.0f, true));
    g.fillEllipse (Rectangle<float> (2.0f * gr, 2.0f * gr).withCentre ({ c.x, c.y - 8.0f }));
    // a little bouquet of three
    const float sq = jlimit (-0.25f, 0.25f, hop * 0.01f);
    owner.lnf.drawTinyBalloon (g, c.translated (-7.0f, 1.0f), 7.0f, (hue + 2) % numHues);
    owner.lnf.drawTinyBalloon (g, c.translated (7.0f, 1.0f), 7.0f, (hue + 4) % numHues);
    owner.lnf.drawTinyBalloon (g, c, 8.5f * (1.0f + sq), hue);
    if (frozen != 0)
    {
        // frosted: its grains (or its spectrum) are frozen
        g.setColour (Colour (0xffe8f6ff).withAlpha (0.85f));
        g.drawEllipse (Rectangle<float> (36.0f, 36.0f).withCentre ({ c.x, c.y - 16.0f }), 1.6f);
        Path flake;
        flake.addStar ({ c.x + 15.0f, c.y - 30.0f }, 6, 1.8f, 5.5f);
        g.fillPath (flake);
    }
    if (weather > 0.05f)
    {
        g.setColour (Colour (0xffa8c8ff).withAlpha (0.6f * weather));
        g.drawEllipse (Rectangle<float> (42.0f, 42.0f).withCentre ({ c.x, c.y - 14.0f }), 1.2f);
    }
    g.setFont (SadLookAndFeel::uiFont (8.5f, true));
    g.setColour (Colours::white.withAlpha (isMouseOverOrDragging() ? 0.95f : 0.6f));
    g.drawText (String (index + 1), Rectangle<float> (20.0f, 10.0f).withCentre ({ c.x, c.y + 6.0f }), Justification::centred);
}

void GrainMarker::mouseDown (const MouseEvent& e)
{
    dragging = true;
    const auto p = owner.toContent (e);
    grab = pos - p;
    grabDepth = depth / std::max (0.01f, 1.0f);
    const Vec3 w = owner.unproject (pos, depth) * (1.0f / FreakiePieEditor::sphereR);
    if (index == FreakiePieProcessor::kGrains) return;   // (the source moves as you drag it)
    owner.proc.heldX.store (w.x); owner.proc.heldY.store (w.y); owner.proc.heldZ.store (w.z);
    owner.proc.heldGrain.store (index);
    owner.noteActivity();
}

void GrainMarker::mouseDrag (const MouseEvent& e)
{
    const auto p = owner.toContent (e) + grab;
    Vec3 w = owner.unproject (p, grabDepth) * (1.0f / FreakiePieEditor::sphereR);
    const float l = w.length();
    if (l > 1.0f) w = w * (1.0f / l);
    if (index == FreakiePieProcessor::kGrains) { owner.proc.srcX.store (w.x); owner.proc.srcY.store (w.y); owner.proc.srcZ.store (w.z); return; }
    owner.proc.heldX.store (w.x); owner.proc.heldY.store (w.y); owner.proc.heldZ.store (w.z);
}

void GrainMarker::mouseUp (const MouseEvent& e)
{
    dragging = false;
    const auto p = owner.toContent (e);
    if (index == FreakiePieProcessor::kGrains) return;
    owner.proc.heldGrain.store (-1);
    if (owner.jarArea.expanded (10.0f).contains (p)) { owner.removeGrain(); return; }   // back in the jar
    owner.proc.setGrainHome (index, { owner.proc.heldX.load(), owner.proc.heldY.load(), owner.proc.heldZ.load() });
}

// everything in the sphere drawn far to near (what you're holding on top)
void FreakiePieEditor::depthSort()
{
    std::vector<std::pair<float, Component*>> items;
    for (auto* f : floaters) if (f->alive && f->isVisible()) items.push_back ({ f->dragging ? 1.0e6f : f->depth, f });
    for (auto* c : grainMarkers)
        if (auto* m = dynamic_cast<GrainMarker*> (c); m != nullptr && m->isVisible()) items.push_back ({ m->dragging ? 1.0e6f : m->depth + 1.0f, m });
    std::stable_sort (items.begin(), items.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    std::vector<Component*> order;
    for (auto& it : items) order.push_back (it.second);
    if (order == drawnOrder) return;
    drawnOrder = order;
    for (auto* c : order) c->toFront (false);
    if (pin != nullptr) pin->toFront (false);
    if (weatherBar != nullptr) weatherBar->toFront (false);
}

//==============================================================================
// The energy meter
//==============================================================================
Colour FreakiePieEditor::meterColour (float db)
{
    // like the IEM EnergyVisualizer: from nothing through blue, cyan, green and yellow to red
    const float t = jlimit (0.0f, 1.0f, (db + 30.0f) / 30.0f);   // db: relative to the loudest place (0 dB), 30 dB of range
    static const Colour stops[] = { Colour (0xff1b2a8a), Colour (0xff1f7fe0), Colour (0xff22d3c5), Colour (0xff6fe04a), Colour (0xfff2e13a), Colour (0xffff4a2a) };
    const float x = t * 5.0f;
    const int i = jlimit (0, 4, (int) x);
    return stops[i].interpolatedWith (stops[i + 1], x - (float) i).withAlpha (jlimit (0.0f, 1.0f, t * 1.6f));
}

void FreakiePieEditor::meterStep()
{
    if (meterDirs.empty())
    {
        meterDirs = fp::ambi::fibonacci (520);
        meterDb.assign (meterDirs.size(), -100.0f);
        // the flat map: Hammer-Aitoff, left on the left, ahead in the middle
        mapCols = 60; mapRows = 30;
        mapDirs.assign ((size_t) (mapCols * mapRows), Vec3 {});
        mapDb.assign (mapDirs.size(), -200.0f);
        for (int r = 0; r < mapRows; ++r)
            for (int c = 0; c < mapCols; ++c)
            {
                const float X = ((c + 0.5f) / mapCols * 2.0f - 1.0f) * 2.0f * MathConstants<float>::sqrt2;
                const float Y = (1.0f - (r + 0.5f) / mapRows * 2.0f) * MathConstants<float>::sqrt2;
                const float q = X * X / 8.0f + Y * Y / 2.0f;
                auto& d = mapDirs[(size_t) (r * mapCols + c)];
                if (q > 1.0f) { d = { 0, 0, 0 }; continue; }
                const float z = std::sqrt (1.0f - X * X / 16.0f - Y * Y / 4.0f);
                const float lon = 2.0f * std::atan2 (z * X, 2.0f * (2.0f * z * z - 1.0f));
                const float lat = std::asin (jlimit (-1.0f, 1.0f, z * Y));
                d = fp::ambi::fromAzEl (-lon, lat);
            }
    }
    // levels relative to the loudest place, which itself sinks slowly (so silence fades out instead of shooting up)
    float top = -120.0f;
    std::vector<float> now (meterDirs.size());
    for (size_t i = 0; i < meterDirs.size(); ++i)
    {
        now[i] = 10.0f * std::log10 (proc.energy.levelAt (meterDirs[i]) + 1.0e-12f);
        top = jmax (top, now[i]);
    }
    meterRef = top > meterRef ? top : meterRef + (top - meterRef) * 0.02f;
    const float ref = jmax (meterRef, -70.0f);
    for (size_t i = 0; i < meterDirs.size(); ++i) meterDb[i] += ((now[i] - ref) - meterDb[i]) * 0.5f;
    for (size_t i = 0; i < mapDirs.size(); ++i)
        mapDb[i] = mapDirs[i].length() < 0.5f ? -200.0f : 10.0f * std::log10 (proc.energy.levelAt (mapDirs[i]) + 1.0e-12f) - ref;
}

void FreakiePieEditor::drawEnergySkin (Graphics& g, bool front) const
{
    for (size_t i = 0; i < meterDirs.size(); ++i)
    {
        if (meterDb[i] < -14.0f) continue;   // on the glass, only the loud places
        float k, d;
        const auto p = project (meterDirs[i] * sphereR, &k, &d);
        if ((d > 0.0f) != front) continue;
        const auto c = meterColour (meterDb[i]);
        const float r = 15.0f * k, a = jlimit (0.0f, 1.0f, (meterDb[i] + 14.0f) / 14.0f);
        g.setGradientFill (ColourGradient (c.withAlpha (a * (front ? 0.30f : 0.45f)), p.x, p.y, c.withAlpha (0.0f), p.x + r, p.y, true));
        g.fillEllipse (Rectangle<float> (2.0f * r, 2.0f * r).withCentre (p));
    }
}

void FreakiePieEditor::drawEnergyMap (Graphics& g) const
{
    const auto frame = mapArea;
    g.setColour (Colour (0xff141a2a).withAlpha (0.72f));
    g.fillRoundedRectangle (frame, 10.0f);
    g.setColour (palette::blush.withAlpha (0.35f));
    g.drawRoundedRectangle (frame.reduced (0.5f), 10.0f, 1.0f);
    const auto area = frame.reduced (10.0f, 18.0f).translated (0.0f, 4.0f);
    const float cw = area.getWidth() / (float) mapCols, ch = area.getHeight() / (float) mapRows;
    for (int r = 0; r < mapRows; ++r)
        for (int c = 0; c < mapCols; ++c)
        {
            const float db = mapDb[(size_t) (r * mapCols + c)];
            if (db < -199.0f) continue;
            g.setColour (Colour (0xff0d1630));
            g.fillRect (area.getX() + (float) c * cw, area.getY() + (float) r * ch, cw + 0.5f, ch + 0.5f);
            g.setColour (meterColour (db));
            g.fillRect (area.getX() + (float) c * cw, area.getY() + (float) r * ch, cw + 0.5f, ch + 0.5f);
        }
    // where the grains are, as little dots
    for (int i = 0; i < grainCount(); ++i)
    {
        const auto& v = proc.grainView[(size_t) i];
        const Vec3 d = Vec3 { v.x.load(), v.y.load(), v.z.load() };
        if (d.length() < 1.0e-3f) continue;
        const auto n = d.normalised();
        const float az = std::atan2 (n.y, n.x), el = std::asin (jlimit (-1.0f, 1.0f, n.z));
        const float lon = -az, den = std::sqrt (1.0f + std::cos (el) * std::cos (lon * 0.5f));
        const float X = 2.0f * MathConstants<float>::sqrt2 * std::cos (el) * std::sin (lon * 0.5f) / den;
        const float Y = MathConstants<float>::sqrt2 * std::sin (el) / den;
        const Point<float> q (area.getCentreX() + X / (2.0f * MathConstants<float>::sqrt2) * area.getWidth() * 0.5f,
                              area.getCentreY() - Y / MathConstants<float>::sqrt2 * area.getHeight() * 0.5f);
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (q), 1.2f);
    }
    g.setFont (SadLookAndFeel::uiFont (9.5f, true));
    g.setColour (palette::blush.withAlpha (0.85f));
    g.drawText ("energy", frame.withHeight (16.0f).reduced (10.0f, 0.0f).translated (0.0f, 2.0f), Justification::centredLeft);
    g.setFont (SadLookAndFeel::uiFont (8.5f, false));
    g.setColour (palette::blush.withAlpha (0.6f));
    g.drawText ("left   ahead   right", frame.withHeight (16.0f).reduced (10.0f, 0.0f).translated (0.0f, 2.0f), Justification::centredRight);
    String out = "out: 3rd-order Ambisonics, 16 ch (ACN, SN3D)";
    if (proc.ambiFallback.load())
    {
        out = "the track has " + String (proc.outChannels.load()) + " ch: give it 16";
        g.setColour (Colour (0xffffb0c8));
    }
    g.drawText (out, frame.withTop (frame.getBottom() - 14.0f).reduced (10.0f, 0.0f), Justification::centredLeft);
}

//==============================================================================
// The sphere itself
//==============================================================================
void FreakiePieEditor::drawSphereBack (Graphics& g) const
{
    const float Rs = sphereScreenRadius();
    const auto disc = Rectangle<float> (2.0f * Rs, 2.0f * Rs).withCentre (sphereCentre);
    // the glass, a little milky inside, tinted by the place
    {
        ColourGradient fill (theme.accent.withAlpha (0.10f), sphereCentre.x - Rs * 0.3f, sphereCentre.y - Rs * 0.35f,
                             theme.deep.withAlpha (0.38f), sphereCentre.x + Rs, sphereCentre.y + Rs, true);
        g.setGradientFill (fill);
        g.fillEllipse (disc);
    }
    // the grid behind (the far half)
    auto curve = [&] (std::function<Vec3 (float)> at, bool front, float alpha, float width)
    {
        Path p;
        bool drawing = false;
        for (int i = 0; i <= 96; ++i)
        {
            float d;
            const auto q = project (at ((float) i / 96.0f) * sphereR, nullptr, &d);
            if ((d > 0.0f) == front) { if (! drawing) p.startNewSubPath (q); else p.lineTo (q); drawing = true; }
            else drawing = false;
        }
        g.setColour (palette::blush.withAlpha (alpha));
        g.strokePath (p, PathStrokeType (width));
    };
    for (float el : { -60.0f, -30.0f, 0.0f, 30.0f, 60.0f })
    {
        const float e = degreesToRadians (el);
        curve ([e] (float u) { return fp::ambi::fromAzEl (u * MathConstants<float>::twoPi, e); }, false, el == 0.0f ? 0.22f : 0.12f, el == 0.0f ? 1.2f : 0.8f);
    }
    for (int k = 0; k < 6; ++k)
    {
        const float az = (float) k * MathConstants<float>::pi / 6.0f;
        curve ([az] (float u) { return fp::ambi::fromAzEl (az, (u - 0.5f) * MathConstants<float>::twoPi); }, false, 0.10f, 0.8f);
    }
    drawEnergySkin (g, false);

    // the floor: a round lawn the bows stand on, with the order's ring round it
    {
        const float fr = std::sqrt (sphereR * sphereR - floorZ * floorZ);
        Path lawn, ring;
        for (int i = 0; i <= 72; ++i)
        {
            const float a = (float) i / 72.0f * MathConstants<float>::twoPi;
            const auto q = project ({ fr * std::cos (a), fr * std::sin (a), floorZ });
            if (i == 0) lawn.startNewSubPath (q); else lawn.lineTo (q);
        }
        lawn.closeSubPath();
        g.setColour (theme.accent.withAlpha (0.16f).interpolatedWith (Colour (0xff4f8a4a), 0.5f).withAlpha (0.28f));
        g.fillPath (lawn);
        g.setColour (palette::blush.withAlpha (0.25f));
        g.strokePath (lawn, PathStrokeType (1.0f));
        for (int i = 0; i <= 64; ++i)
        {
            const float x = shelfLeft + (shelfRight - shelfLeft) * (float) i / 64.0f;
            const auto q = project (ringPoint (x));
            if (i == 0) ring.startNewSubPath (q); else ring.lineTo (q);
        }
        Path dashed;
        const float dash[] = { 4.0f, 5.0f };
        PathStrokeType (1.2f).createDashedStroke (dashed, ring, dash, 2);
        g.setColour (Colour (0xffc48b52).withAlpha (0.7f));
        g.fillPath (dashed);
        // in -> 1, 2, 3 ... -> out, round the ring
        g.setFont (SadLookAndFeel::uiFont (9.0f, true));
        g.setColour (palette::blush.withAlpha (0.8f));
        g.drawText ("in", Rectangle<float> (30.0f, 12.0f).withCentre (project (ringPoint (shelfLeft - 30.0f)).translated (0.0f, 12.0f)), Justification::centred);
        g.drawText ("out", Rectangle<float> (30.0f, 12.0f).withCentre (project (ringPoint (shelfRight + 30.0f)).translated (0.0f, 12.0f)), Justification::centred);
        const auto items = shelfItems();
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& a = anchors[anchorOfSlot (items[i])];
            g.drawText (String ((int) i + 1), Rectangle<float> (16.0f, 10.0f).withCentre (a.pos.translated (0.0f, 20.0f * a.scale)), Justification::centred);
        }
    }
    drawListener (g);
}

// you, in the middle: a little head, nose to the front
void FreakiePieEditor::drawListener (Graphics& g) const
{
    float k;
    const auto c = project ({ 0, 0, 0 }, &k);
    const float r = 12.0f * k;
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillEllipse (Rectangle<float> (2.0f * r, 0.6f * r).withCentre (project ({ 0, 0, floorZ })));
    g.setColour (palette::blush.withAlpha (0.85f));
    g.fillEllipse (Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
    const auto nose = project ({ 16.0f, 0, 0 });
    g.setColour (palette::rose);
    g.drawLine (Line<float> (c, nose), 2.4f);
    for (float side : { 1.0f, -1.0f })
        g.fillEllipse (Rectangle<float> (4.0f * k, 6.0f * k).withCentre (project ({ 0, side * 9.5f, 0 })));
    // which way is which, round the middle
    g.setFont (SadLookAndFeel::uiFont (9.0f, true));
    const std::pair<Vec3, const char*> marks[] = { { { 1, 0, 0 }, "ahead" }, { { 0, 1, 0 }, "left" }, { { 0, -1, 0 }, "right" }, { { -1, 0, 0 }, "behind" }, { { 0, 0, 1 }, "up" } };
    for (auto& m : marks)
    {
        float d;
        const auto q = project (m.first * (sphereR * 1.04f), nullptr, &d);
        g.setColour (palette::blush.withAlpha (d > 0.0f ? 0.75f : 0.35f));
        g.drawText (m.second, Rectangle<float> (50.0f, 12.0f).withCentre (q), Justification::centred);
    }
}

void FreakiePieEditor::drawSphereFront (Graphics& g) const
{
    const float Rs = sphereScreenRadius();
    const auto disc = Rectangle<float> (2.0f * Rs, 2.0f * Rs).withCentre (sphereCentre);
    drawEnergySkin (g, true);
    auto curve = [&] (std::function<Vec3 (float)> at, float alpha)
    {
        Path p;
        bool drawing = false;
        for (int i = 0; i <= 96; ++i)
        {
            float d;
            const auto q = project (at ((float) i / 96.0f) * sphereR, nullptr, &d);
            if (d > 0.0f) { if (! drawing) p.startNewSubPath (q); else p.lineTo (q); drawing = true; }
            else drawing = false;
        }
        g.setColour (Colours::white.withAlpha (alpha));
        g.strokePath (p, PathStrokeType (0.8f));
    };
    curve ([] (float u) { return fp::ambi::fromAzEl (u * MathConstants<float>::twoPi, 0.0f); }, 0.16f);
    for (int k = 0; k < 6; ++k)
    {
        const float az = (float) k * MathConstants<float>::pi / 6.0f;
        curve ([az] (float u) { return fp::ambi::fromAzEl (az, (u - 0.5f) * MathConstants<float>::twoPi); }, 0.06f);
    }
    // the rim and the shine on the glass
    g.setColour (Colours::white.withAlpha (0.35f));
    g.drawEllipse (disc, 1.6f);
    g.setColour (theme.accent.withAlpha (0.25f));
    g.drawEllipse (disc.reduced (3.0f), 3.0f);
    Path shine;
    shine.addCentredArc (sphereCentre.x, sphereCentre.y, Rs * 0.88f, Rs * 0.88f, 0.0f, -2.2f, -1.1f, true);
    g.setColour (Colours::white.withAlpha (0.28f));
    g.strokePath (shine, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::white.withAlpha (0.5f));
    g.fillEllipse (Rectangle<float> (14.0f, 9.0f).withCentre (sphereCentre + Point<float> (-Rs * 0.48f, -Rs * 0.66f)));
}

// the jar of grains: little bouquets waiting to be dropped into the sphere
void FreakiePieEditor::drawJar (Graphics& g) const
{
    const auto r = jarArea;
    const int n = grainCount(), left = FreakiePieProcessor::kGrains - n;
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (Rectangle<float> (r.getWidth() * 0.9f, 10.0f).withCentre ({ r.getCentreX(), r.getBottom() }));
    const auto body = r.withTrimmedTop (18.0f);
    g.setColour (Colour (0xffdff3ff).withAlpha (0.16f));
    g.fillRoundedRectangle (body, 14.0f);
    // the bouquets still in it, sitting in a heap
    for (int i = 0; i < left; ++i)
    {
        const float x = body.getX() + 14.0f + (float) ((i * 37) % 56), y = body.getBottom() - 8.0f - (float) (i / 4) * 13.0f;
        lnf.drawTinyBalloon (g, { x, y }, 4.2f, (n + i) % numHues);
    }
    g.setColour (Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (body, 14.0f, 1.4f);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillRoundedRectangle (body.getX() + 8.0f, body.getY() + 10.0f, 5.0f, body.getHeight() - 26.0f, 2.5f);
    // the lid, pink
    g.setColour (balloonColours (balloonPink).fill);
    g.fillRoundedRectangle (r.getX() + 6.0f, r.getY() + 8.0f, r.getWidth() - 12.0f, 12.0f, 4.0f);
    g.setColour (balloonColours (balloonPink).line);
    g.drawRoundedRectangle (r.getX() + 6.0f, r.getY() + 8.0f, r.getWidth() - 12.0f, 12.0f, 4.0f, 1.0f);
    g.setFont (SadLookAndFeel::uiFont (10.0f, true));
    g.setColour (palette::blush.withAlpha (0.9f));
    const bool scatter = proc.apvts.getRawParameterValue (ids::grainMode)->load() > 0.5f;
    g.drawText (String (n) + (n == 1 ? " grain" : " grains") + (scatter ? ", scattered" : ""), r.withY (r.getBottom() + 4.0f).withHeight (13.0f).expanded (30.0f, 0.0f), Justification::centred);
    if (jarDragging && ! jarArea.contains (jarDragAt))
        lnf.drawTinyBalloon (g, jarDragAt, 7.0f, n % numHues);   // the one you're carrying
}

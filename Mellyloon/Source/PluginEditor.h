#pragma once
#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// The interface colours. They follow the preset's picture: each editor sets them from its
// theme just before it paints (painting is on the message thread, one editor at a time).
namespace palette
{
    inline juce::Colour ink        { 0xff111b2c };
    inline juce::Colour slate      { 0xff1c2638 };
    inline juce::Colour dustyPink  { 0xffe79ab9 };
    inline juce::Colour rose       { 0xffc26b93 };
    inline juce::Colour blush      { 0xfff6c9dc };
    inline juce::Colour tearBlue   { 0xffa7cfdc };
    inline juce::Colour faded      { 0xffb9a7b3 };
}

// Balloon colours, flat cartoon style: fill, line (darker same hue), cel shadow, highlight.
struct BalloonColours { juce::Colour fill, line, shade, shine; };
enum BalloonHue { balloonBlue = 0, balloonYellow, balloonPink, balloonMint, balloonLilac, balloonOrange, balloonTeal, numHues };
const BalloonColours& balloonColours (int hue);

// Where a balloon's body and knot sit inside a square of space, for a given inflation.
struct BalloonShape
{
    float cx, cy, rx, ry, knot, lw;
    juce::Point<float> knotBottom() const noexcept { return { cx, cy + ry + knot }; }
};
BalloonShape balloonShape (juce::Rectangle<float> bounds, float inflate);

//==============================================================================
class SadLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SadLookAndFeel();
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    // drop-down lists open inside the plugin window (scrolling if they're long), never off its edges
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;
    // (JUCE outlines menus that open inside the window with a resize frame; ours draw their own edges)
    void drawResizableFrame (juce::Graphics&, int, int, const juce::BorderSize<int>&) override {}

    // A slightly see-through cartoon balloon with writing on it.
    //   wrinkles: limp (0..1)   wet: rained on (0..1)   murk: muffled by a lowpass (0..1)
    void drawBalloon (juce::Graphics&, const BalloonShape&, int hue, const juce::String& label,
                      const juce::String& value, bool hot, float wrinkles, float wet = 0.0f,
                      float murk = 0.0f, float labelSize = 10.5f) const;
    void drawTinyBalloon (juce::Graphics&, juce::Point<float> anchor, float r, int hue) const;
    // ice over a balloon as the snow freezes everything (0..1)
    void drawFrost (juce::Graphics&, const BalloonShape&, float frost, float seed) const;

    static juce::Font uiFont (float h, bool bold = false);
    static juce::Font titleFont (float h);

    float time = 0.0f;
};

//==============================================================================
// The balloon store's menu, dressed like a little sweet shop: candy-stripe awning, a balloon
// in each effect's colour, a heart on the ones you've bought.
class StoreMenuLook : public SadLookAndFeel
{
public:
    StoreMenuLook();
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override;
    int getPopupMenuBorderSize() override { return 26; }
};

class FreakiePieEditor;

//==============================================================================
// The chalkboard: a little board on the wall shows the grain shape you drew. Click it and it
// comes forward on its easel; draw across it (left to right is the life of a grain, up is
// loud), then put it back. Drawing switches the grains to the chalk shape.
class Chalkboard : public juce::Component
{
public:
    explicit Chalkboard (FreakiePieEditor& e) : owner (e) { setInterceptsMouseClicks (true, false); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void bringForward();
    void putBack();
    bool step (float dt);   // animation; returns true while it's showing

    // the easel itself (frame, slate, legs, the drawing; buttons once it's big), drawn into 'b' (board space, scaled)
    void drawEasel (juce::Graphics&, juce::Rectangle<float> b, float big) const;
    juce::Rectangle<float> restArea() const;   // standing on the classroom floor (editor coords)
    juce::Image teacher;                        // waits beside the board while you draw

    // a curve in chalk: f maps 0..1 across to 0..1 up
    static void drawChalkCurve (juce::Graphics&, const std::function<float (float)>& f, juce::Rectangle<float> area, float thickness, float alpha);
    int page = 0;   // 0: the grain shape, 1: the master EQ
    void setPage (int p) { page = p; repaint(); }
    juce::Point<float> floorSpot { 700.0f, 590.0f };   // where its feet stand when it's put away

private:
    juce::Rectangle<float> boardArea() const;      // the board itself, in this component's coords (animated)
    juce::Rectangle<float> plotArea() const;       // where you draw
    juce::Rectangle<float> buttonArea (int i) const;
    juce::Rectangle<float> tabArea (int i) const;
    void drawAt (juce::Point<float> p);
    void eqButton (int i);
    friend struct FreakieTestAccess;
    FreakiePieEditor& owner;
    float anim = 0.0f, animTarget = 0.0f;
    int lastIndex = -1;
    float lastHeight = 0.0f;
    bool drawing = false, hovering = false;
    juce::Point<float> hover;
};

//==============================================================================
// The library: the help. Twilight reads quietly in the corner while a big open book explains
// the shop, one chapter per page. Chapters on the left page, the chapter itself on the right.
//==============================================================================
// The balloon store, as shelves in a little sweet shop: every effect is a balloon standing on a
// wooden shelf, four to a shelf, all on screen at once. Point at one to read what it does;
// click it to buy it (or pop it if it's yours). The ones you own wear a heart.
class StoreShelves : public juce::Component
{
public:
    explicit StoreShelves (FreakiePieEditor& e);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void open (juce::Rectangle<float> under);
    void close();
    bool isOpen() const noexcept { return opening; }
    void step (float dt);   // the editor animates it
    static constexpr int cols = 4, rows = 4;
    static constexpr float W = 360.0f, H = 434.0f;
    juce::Rectangle<float> tileArea (int i) const noexcept;   // i == FreakiePieEditor::kStoreItems is the "pop all" tile
    int tileAt (juce::Point<float>) const noexcept;
private:
    FreakiePieEditor& ed;
    int hover = -1;
    bool opening = false;
    float shown = 0.0f, t = 0.0f;
};

//==============================================================================
// The weather, in a row across the top where the title was: the sun and cloud (cloudy), the rain
// cloud, the snowflake, the heatwave sun and the auto switch.
// They're drawn the way they always were, straight onto the sky, but on their own layer above the
// balloons so a click always reaches them.
class WeatherBar : public juce::Component
{
public:
    explicit WeatherBar (FreakiePieEditor& e);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;   // right-click the snow or the heat for their fade times
    void step (float dt);
    void toggle (int which);   // (the snow and the heatwave never fall together: one turns the other off)   // 0 cloudy, 1 rain, 2 snow, 3 heat, 4 auto
    static constexpr float X = 290.0f, Y = 0.0f, W = 540.0f, H = 90.0f;
    int buttonAt (juce::Point<float> contentPoint) const noexcept;
private:
    bool isOn (int which) const noexcept;
    FreakiePieEditor& ed;
};

//==============================================================================
// The order of the effects: a list down the left edge, top to bottom the way the sound goes
// through them. It waits off the side of the window until the cursor comes near. Drag a pill up
// or down to move that effect (the others make room, and the sound follows when you let go);
// the die on top rolls a new order.
class OrderRow : public juce::Component
{
public:
    explicit OrderRow (FreakiePieEditor& e);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void step (float dt, bool wanted);   // wanted: the cursor is near, so it slides in
    static constexpr float X = 0.0f, Y = 128.0f, W = 150.0f, H = 462.0f;
    static constexpr float left = 12.0f, top = 30.0f, pitch = 26.0f, chipH = 19.0f;
    juce::Rectangle<float> dieArea() const noexcept { return { left, 4.0f, 20.0f, 20.0f }; }
    juce::Rectangle<float> listArea() const noexcept;   // what's drawn, in the room's coordinates (where it is when shown)
    bool isDragging() const noexcept { return dragSlot >= 0; }
    float shownAmount() const noexcept { return shown; }
    struct Chip { int slot; juce::String name; juce::Colour colour; juce::Rectangle<float> r; };
    const std::vector<Chip>& layout() const;   // worked out again only when the order changes
    std::vector<int> displayOrder() const;     // the order as shown (while dragging, with the dragged one at its new place)
private:
    std::vector<Chip> computeLayout() const;
    int indexAt (float y) const noexcept;
    mutable std::vector<int> layoutFor;
    mutable std::vector<Chip> layoutCache;
    FreakiePieEditor& ed;
    std::map<int, float> shownY;   // where each pill is drawn, easing towards its place
    int dragSlot = -1, dragTo = -1, hoverSlot = -1;
    float grabDy = 0.0f, dragY = 0.0f, downY = 0.0f;
    bool moved = false;
    float shown = 0.0f, t = 0.0f, spin = 0.0f;
    bool hoverDie = false;
};

//==============================================================================
// Right-click the snowflake or the heatwave: a little card with its fade-in and fade-out times
// (5 s to 5 min each). Drag along a bar or scroll on it; double-click puts it back to 5 s.
class FadePopup : public juce::Component
{
public:
    explicit FadePopup (FreakiePieEditor& e);
    void open (int which, juce::Rectangle<float> under);   // 2 the snow, 3 the heatwave (the weather bar's numbering)
    void close();
    bool isOpen() const noexcept { return isVisible(); }
    int showing() const noexcept { return isVisible() ? which : -1; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    static constexpr float W = 210.0f, H = 84.0f;
    juce::Rectangle<float> trackArea (int row) const noexcept { return { 70.0f, 34.0f + 24.0f * (float) row, 84.0f, 10.0f }; }
    juce::Rectangle<float> rowArea (int row) const noexcept { return { 8.0f, 28.0f + 24.0f * (float) row, W - 16.0f, 22.0f }; }
    juce::RangedAudioParameter* params[2] {};
private:
    void setFromX (int row, float x);
    FreakiePieEditor& ed;
    int which = -1, dragRow = -1;
};

//==============================================================================
// The presets as a filmstrip: it slides up from the bottom of the window with a picture of each
// preset's place. Scroll or drag along it and click one to go there; the room fades over to it.
// The kinds (start, feedback, stretch, useful) are chips above the strip that jump along it.
class PresetStrip : public juce::Component
{
public:
    explicit PresetStrip (FreakiePieEditor& e);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void open();
    void close();
    bool isOpen() const noexcept { return opening; }
    void step (float dt);
    static constexpr float H = 112.0f, frameW = 104.0f, frameH = 58.0f, gap = 10.0f, left = 14.0f, frameTop = 30.0f;
    juce::Rectangle<float> frameArea (int i) const noexcept { return { left + (float) i * (frameW + gap) - scroll, frameTop, frameW, frameH }; }
    juce::Rectangle<float> chipArea (int g) const noexcept;
    juce::Rectangle<float> closeArea() const noexcept { return { (float) getWidth() - 28.0f, 6.0f, 20.0f, 20.0f }; }
    int frameAt (juce::Point<float>) const noexcept;
    void scrollTo (int preset);
    static const char* const groups[4];
private:
    float maxScroll() const noexcept;
    const juce::Image& thumb (int preset);
    FreakiePieEditor& ed;
    std::vector<juce::Image> thumbs;
    bool opening = false, dragged = false;
    float shown = 0.0f, scroll = 0.0f, scrollTarget = 0.0f, downScroll = 0.0f;
    int hover = -1, pressed = -1;
};

class Library : public juce::Component
{
public:
    explicit Library (FreakiePieEditor& e);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void open();
    void close();
    bool step (float dt);   // animation; returns true while it's showing
    void showChapter (int i);
    int numChapters() const noexcept;
    int chapter = 0;

    juce::Image room, twilight;

private:
    juce::Rectangle<float> bookArea() const;
    juce::Rectangle<float> leftPage() const;
    juce::Rectangle<float> rightPage() const;
    juce::Rectangle<float> chapterRow (int i) const;
    juce::Rectangle<float> closeTab() const;
    juce::Rectangle<float> pageButton (int dir) const;   // -1 back, +1 next
    void drawParchment (juce::Graphics&, juce::Rectangle<float>) const;
    void drawContents (juce::Graphics&) const;              // the left page: the chapters
    void drawChapter (juce::Graphics&, int which) const;    // the right page: one chapter
    FreakiePieEditor& owner;
    float anim = 0.0f, animTarget = 0.0f, t = 0.0f;
    float flipT = 1.0f;          // a page turning: 0..1
    int flipFrom = 0, flipDir = 1;
    int hoverRow = -1;
    friend struct FreakieTestAccess;
};

//==============================================================================
// Anything that floats on a string: a physics body tied to one of the anchors.
// Plain drag moves it; let go and it floats back up.
class Floater : public juce::Component
{
public:
    Floater (FreakiePieEditor& e, float stringLength, int anchorIndex);

    virtual float inflation() const noexcept = 0;
    float collisionRadius() const noexcept;
    juce::Point<float> knotWorld() const noexcept;   // where the string ties on, in editor coords
    void place();                                    // physics state -> component transform
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    static constexpr int S = 84;
    int anchor = 0;
    bool alive = true;

    juce::Point<float> pos, vel, dragTarget, grabOffset;   // pos, vel: where it is on screen (the projection of w)
    float angle = 0.0f, squash = 0.0f, squashVel = 0.0f, stringLength = 260.0f, phase = 0.0f;
    float dance = 0.0f, lean = 0.0f, wet = 0.0f, stretch = 0.0f, frost = 0.0f;
    bool dragging = false;

    // inside the sphere: where it really is (px from the centre, x ahead, y left, z up) and how fast
    // it's going there; how big it looks from here and how near it is (for drawing in order)
    fp::ambi::Vec3 w, wv;
    float scale = 1.0f, depth = 0.0f, dragDepth = 0.0f;
    bool placed3d = false;
    juce::Point<float> lastVel2;   // (pushes from the room arrive as screen velocities; the difference is put back in 3-D)
    float flyIn = 1.0f;            // 0..1 while it flies from where it was let go (a pump, the store) into the sphere
    juce::Point<float> flyFrom;
    // its picture is cached (the squash, the dance and the stretch are in its transform), and only redrawn
    // when what it shows changes; lookKey says what it shows
    virtual juce::int64 lookKey() const { return -1; }   // -1: always redraw (the sound balloons' live trace)
    juce::int64 lastLook = -2;
    bool placing = false;          // (moving it isn't a reason to redraw its picture)

protected:
    void beginMove (const juce::MouseEvent&);
    void dragMove (const juce::MouseEvent&);
    void applySquashAndStretch (juce::Graphics&, const BalloonShape&) const;

    FreakiePieEditor& owner;
    juce::Point<float> downPos;
    bool moved = false;
};

//==============================================================================
// One parameter as a balloon in the bouquet.
//   drag: move   scroll/swipe: inflate (value)   shift-drag: fine   double-click: reset   click: boop / toggle
class Balloon : public Floater
{
public:
    Balloon (FreakiePieEditor&, juce::RangedAudioParameter&, const juce::String& label, int hue, float stringLength, int anchor);
    ~Balloon() override { stopDrift(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    float inflation() const noexcept override;
    juce::int64 lookKey() const override;

    bool isToggle = false, isChoice = false;   // switches flip on click; choices step through their options
    bool isStepped = false;                    // whole-number settings (octave): one step per click / scroll flick
    int hue = 0;
    juce::String label;
    float fill = -1.0f; // freeze balloon: shows the freeze fade instead of on/off
    bool bypassable = false;   // an effect control: dims while the effects are bypassed
    float faded = 0.0f;

    // inside a cloud the value wanders slowly to random places (switches are left alone)
    void cloudDrift (bool inside, float dt, juce::Random&);
    void stopDrift();
    bool isDrifting() const noexcept { return drifting; }
    juce::String currentText() const { return param.getCurrentValueAsText(); }
    juce::RangedAudioParameter& parameter() const noexcept { return param; }

private:
    void setNorm (float n);
    void stepChoice (int direction);
    float wheelAcc = 0.0f;
    bool wrapSteps = false;
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float value = 0.0f;
    enum class Mode { none, move, adjust } mode = Mode::none;
    float downNorm = 0.0f;
    bool drifting = false;
    float driftTarget = 0.0f, driftClock = 0.0f;
};

//==============================================================================
// A balloon full of captured sound, playing it back as frozen grains.
//   drag: move   scroll: retune (snapped to the scale, shown as +7, +12...)   double-click: pop it
class SoundBalloon : public Floater
{
public:
    SoundBalloon (FreakiePieEditor&, int slot, float size, float semis, int hue);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { beginMove (e); }
    void mouseDrag (const juce::MouseEvent& e) override { dragMove (e); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    float inflation() const noexcept override { return size; }

    int slot, hue;
    float size, semis, wheelAcc = 0.0f;
};

//==============================================================================
// A push pin. Drag it: its tip pops caught-sound balloons (and whatever's on the pumps),
// and pokes the control balloons without popping them. Let go and it flies home to its cushion.
class PinTool : public juce::Component
{
public:
    explicit PinTool (FreakiePieEditor& e) : owner (e) { setSize (44, 64); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    juce::Point<float> tip() const noexcept { return pos + juce::Point<float> (-14.0f, 26.0f); }
    void place() { setTopLeftPosition (juce::roundToInt (pos.x - 22.0f), juce::roundToInt (pos.y - 32.0f)); }

    juce::Point<float> pos, home, grab;
    bool dragging = false;

private:
    FreakiePieEditor& owner;
};

//==============================================================================
// A cartoon speech bubble you type a pitch into: "+7", "-5", "12". Enter applies, Esc cancels.
class PitchBubble : public juce::Component, private juce::TextEditor::Listener
{
public:
    PitchBubble();
    // tip = the point the bubble's tail points at (editor coords)
    void open (juce::Point<float> tip, float currentSemis, const juce::String& title, juce::Rectangle<float> within);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (float)> onCommit;
    std::function<void()> onClose;

private:
    void textEditorReturnKeyPressed (juce::TextEditor&) override { commit(); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
    void textEditorFocusLost (juce::TextEditor&) override         { commit(); }
    void commit();
    void close();

    juce::TextEditor field;
    juce::String title;
    friend struct FreakieTestAccess;
    bool tailDown = true, closing = false;
    float tailX = 0.0f;
};

//==============================================================================
// One grain in the sphere: a little bouquet of balloons, glowing with its sound. It hops on its
// stutters, frosts over when it freezes, and drags anywhere inside the sphere (drop it on the jar to
// take a grain away).
class GrainMarker : public juce::Component
{
public:
    GrainMarker (FreakiePieEditor& e, int i) : owner (e), index (i) { setSize (S, S); setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override { return juce::Point<float> ((float) x, (float) y).getDistanceFrom ({ S * 0.5f, S * 0.5f }) < S * 0.36f; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void place();
    static constexpr int S = 76;
    juce::Point<float> pos;
    float scale = 1.0f, depth = 0.0f, hop = 0.0f, hopVel = 0.0f, glow = 0.0f, presence = 0.0f, weather = 0.0f;
    float flyIn = 1.0f;            // 0..1 while it flies in from the jar
    juce::Point<float> flyFrom;
    int frozen = 0, stuttersSeen = 0;
    bool dragging = false;
private:
    FreakiePieEditor& owner;
    int index;
    float grabDepth = 0.0f;
    juce::Point<float> grab;
};

//==============================================================================
class FreakiePieEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FreakiePieEditor (FreakiePieProcessor&);
    ~FreakiePieEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // used by the balloons
    void confetti (juce::Point<float> at, int count);
    void pop (juce::Point<float> at, float size, int hue);
    float stepSemis (float semis, int direction) const;     // next/previous note of the scale, in semitones
    void showPitchBubble (SoundBalloon* target);             // type an exact interval (nullptr: the pump's next balloon)
    static bool wheelStep (float& acc, const juce::MouseWheelDetails&, int& direction); // one note per deliberate flick
    static juce::String semisText (float semis);
    juce::Point<float> toContent (const juce::MouseEvent& e) { return e.getEventRelativeTo (&content).position; }

    SadLookAndFeel lnf;
    FreakiePieProcessor& proc;
    static constexpr int designW = 1060, designH = 600;

private:
    struct Content : public juce::Component
    {
        explicit Content (FreakiePieEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void paintOverChildren (juce::Graphics& g) override { owner.paintOver (g); }
        void mouseDown (const juce::MouseEvent& e) override { owner.contentMouse (e, 0); }
        void mouseDrag (const juce::MouseEvent& e) override { owner.contentMouse (e, 1); }
        void mouseUp (const juce::MouseEvent& e) override   { owner.contentMouse (e, 2); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { owner.contentWheel (e, w); }
        void mouseMagnify (const juce::MouseEvent& e, float scale) override { const auto p = owner.toContent (e); if (owner.insideSphere (p)) owner.zoomAt (p, scale); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { if (owner.insideSphere (owner.toContent (e))) owner.resetView(); }
        FreakiePieEditor& owner;
    };

    // what the strings are tied to: a bow on the floor, or (for a store effect) a little gift box
    struct Anchor { juce::Point<float> pos, vel, target, grab; bool dragging = false; int hue = balloonPink; bool fixed = false; bool hidden = false; bool box = false;
                    bool onShelf = false; float slotX = 0.0f;   // onShelf: an effect, standing in its place in the order
                    float shelfX = 0.0f;                          // where it is along the order (it glides to slotX), round the ring on the sphere's floor
                    fp::ambi::Vec3 w; float scale = 1.0f, depth = 0.0f; };   // where it stands in the sphere; pos is where that is on screen

    // a pump on the floor with a balloon on its nozzle
    struct Pump
    {
        juce::Rectangle<float> area;
        juce::Point<float> nozzle;
        bool holding = false, spent = false;
        float holdTime = 0.0f, popAt = 8.0f, size = 0.0f, strain = 0.0f, handle = 0.0f;
        int hue = balloonPink, slot = -1;
    };

    struct Particle { juce::Point<float> p, v; float rot, spin, life; juce::Colour c; int kind; };  // 0 confetti, 1 dot, 2 shred
    struct Drop { juce::Point<float> p, v; bool bounced = false; };
    struct Ripple { juce::Point<float> p; float age; };
    struct Burst { juce::Point<float> p; float age, size; int hue; };

    void paintContent (juce::Graphics&);
    void paintOver (juce::Graphics&);
    void timerCallback() override;
    void step (float h);
    void weatherStep (float dt);
    void pumpStep (float dt);
    void contentMouse (const juce::MouseEvent&, int phase);
    void contentWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&);
    void startSoundPump();
    void releaseSoundPump();
    int freeSlot() const;

    void drawBow (juce::Graphics&, juce::Point<float> at, int hue) const;
    void drawCloud (juce::Graphics&, juce::Point<float> centre, float scale, float dark) const;
    void drawTank (juce::Graphics&) const;
    void drawPump (juce::Graphics&, const Pump&, bool sound) const;
    BalloonShape pumpBalloonShape (const Pump&) const;

    bool rainOn() const noexcept   { return rainValue > 0.5f; }
    bool cloudyOn() const noexcept { return cloudyValue > 0.5f; }
    bool heatOn() const noexcept   { return heatValue > 0.5f; }
    bool autoOn() const noexcept   { return autoValue > 0.5f; }
    bool snowOn() const noexcept   { return snowValue > 0.5f; }
    bool bypassOn() const noexcept { return bypassValue > 0.5f; }
    bool bought (int fx) const;    // bought at the store (not just lent by the lightning); fx: a store item (stutterItem is the gumball machine)
    static juce::String storeItemName (int item);   // what the store calls it
    static juce::String storeItemWhat (int item);   // its controls, for the store's hints
    static juce::String ownedId (int item);         // its "bought" parameter
    bool heliumOn() const noexcept { return heliumValue > 0.5f; }

    Content content { *this };
    // a layer above the whole room (balloons, clouds, rain and all): the card, the order list, the
    // store, the preset strip and the fade times. Clicks anywhere else fall through to the room.
    struct Overlay : public juce::Component { Overlay() { setInterceptsMouseClicks (false, true); } };
    Overlay overlay;
    // the card itself (presets, scale, bypass), on the overlay
    struct CardPanel : public juce::Component
    {
        explicit CardPanel (FreakiePieEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        FreakiePieEditor& ed;
    };
    std::unique_ptr<CardPanel> cardPanel;
    void drawCard (juce::Graphics&) const;
    juce::OwnedArray<Floater> floaters;    // the 13 parameter balloons first, then sound balloons
    int numParamBalloons = 0;
    Balloon* freezeBalloon = nullptr;
    static constexpr int firstBox = 7;
    // what the store sells: its effects (fp::StoreFX), then the gumball machine (the stutter)
    static constexpr int stutterItem = fp::StoreFX::kNum, kStoreItems = fp::StoreFX::kNum + 1;
    static int storeAnchor (int item) noexcept { return item == stutterItem ? 4 : firstBox + item; }
    Anchor anchors[firstBox + fp::StoreFX::kNum];   // on the floor: 0 pink (the loop)  1 teal (caught sounds, frozen volume)
                                                    // on the shelf: 2 blue (spectral)  3 mint (grains)  4 orange (stutter)  5 yellow (pitch)  6 lilac (reverb)
                                                    // 7..: one gift box per store effect, shown while you have it

    // the shelf: the effects stand on it left to right in the order the sound goes through them.
    // Drag one along it and the rest make room; let go and the sound follows.
    static constexpr float shelfLeft = 140.0f, shelfRight = 890.0f, shelfY = 452.0f;   // (the row of effects, standing on the grass)
    static int anchorOfSlot (int slot) noexcept;   // fp::Engine::Slot -> anchor index
    std::vector<int> shelfItems() const;         // the slots standing on the shelf, in the processor's order
    void layoutShelf (const std::vector<int>& visibleOrder);
    void commitShelf();                          // read the order off the shelf (by position) and send it
    void drawShelf (juce::Graphics&) const;
    void drawGiftBox (juce::Graphics&, juce::Point<float> knot, int hue) const;
    Balloon* spectralFreezeBalloon = nullptr;

    // round toy switches: what the sound pump catches
    struct Pill
    {
        juce::Rectangle<float> r;
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> att;
        float value = 0.0f;
        juce::String name;
        int hue = 0;
        bool continuous = false;   // turned like a knob: drag up/down or scroll
        juce::Point<float> c;      // a round toy token with an animated picture of what it does
        float rad = 20.0f, bounce = 0.0f, bounceVel = 0.0f;
        int icon = 0;
        juce::Point<float> home;
        float phase = 0.0f;
    };
    enum TokenIcon { iconPops = 0, iconTone, iconCatch, iconFlight, iconSpeed, iconSpread, iconDeviate, iconWidth, iconOutput };

    void pillStep (float dt);   // the tokens bob gently where they are
    // right-click the pop sounds token: switch them on or off, or send them into the effects
    void openPopsMenu();

    // Pinkie, sitting on the floor watching the balloons; she hops when one pops
    // (under an umbrella when it rains, with the rain bouncing off it); she holds what you buy at the store
    juce::Image pinkie, pinkieUmbrella, pinkieCower, pinkieScared, pinkieSleep, pinkieCrystal;   // crystal: while the helium's on
    juce::Image pinkieSnow, pinkieHeat, pinkieWet;   // bundled up in the snow; melted flat in the heat; soaked flat in a hot rain
    juce::Image pinkieCold;                          // left out in the snow too long: she's caught a cold
    float pinkieHop = 0.0f, pinkieHopVel = 0.0f;
    // her moods: a pop scares her for a moment (two pictures, taking turns); leave the plugin
    // alone for a while and she falls asleep
    double scaredUntil = -1.0, lastActivity = 0.0;
    bool scaredAlt = false;
    float scaredVis = 0.0f, sleepVis = 0.0f, umbrellaVis = 0.0f, crystalVis = 0.0f, snowyVis = 0.0f, coldVis = 0.0f, meltVis = 0.0f, wetVis = 0.0f;
    static constexpr double sleepAfter = 45.0;
    static constexpr double coldAfter = 60.0;   // a minute out in the snow and she catches a cold
    double snowSince = -1.0;                     // when the snow last came in (-1: it isn't snowing)
    void noteActivity() noexcept { lastActivity = clock; }

    // presets: a picture behind everything, fading across when the preset changes, and the
    // interface colours following it
    int seenStutter = 0;     // the stutter's event counter, last time we looked
    double lastSave = 0.0;   // the last patch is saved every couple of seconds while you play
    // the preset name in the card: it looks like the other boxes, but a click opens the filmstrip
    struct PresetBox : public juce::ComboBox
    {
        std::function<void()> onOpen;
        void mouseDown (const juce::MouseEvent&) override { if (onOpen) onOpen(); }
        void mouseUp (const juce::MouseEvent&) override {}
        void showPopup() override {}
    };
    PresetBox presetBox;
    int shownPreset = -2, bgFrom = -1, bgTo = -1;
    float bgFade = 1.0f;
    juce::Image backgrounds[presets::numBackgrounds];
    const juce::Image& background (int i);
    struct Theme { juce::Colour deep, accent, text; };
    Theme themeFrom, themeTo, theme;
    static Theme themeOf (int bg);
    void followPreset (float dt);
    void applyTheme();

    // the chalkboard (grain shape)
    std::unique_ptr<Chalkboard> chalkboard;
    friend class Chalkboard;

    // the library (help), opened with the little book under the rain cloud
    std::unique_ptr<Library> library;
    // the very top right corner, top to bottom: the pop sounds switch (right-click: feed them into the effects), help, reset
    juce::Rectangle<float> popsButton { 950.0f, 3.0f, 108.0f, 22.0f };
    juce::Rectangle<float> helpButton { 950.0f, 25.0f, 108.0f, 26.0f }, resetButton { 950.0f, 51.0f, 108.0f, 22.0f };
    float popsValue = 1.0f;
    std::unique_ptr<juce::ParameterAttachment> popsAtt;
    void drawPopsButton (juce::Graphics&) const;
    void drawHelpButton (juce::Graphics&) const;
    void drawResetButton (juce::Graphics&) const;
    void resetEverything();
    void drawTitle (juce::Graphics&) const;

    // the Great and Powerful Trixie: about once an hour (a 2 % chance each minute) she struts in,
    // zaps balloons from her amulet (each one she hits jumps to a random value), then with a flash
    // sets everything she can reach at random, and leaves
    struct TrixieVisit
    {
        bool on = false, finale = false, noisy = false;   // noisy: she's playing her noise into the input this time
        float t = 0.0f, nextZap = 0.0f, boltAge = 1.0f, flash = 0.0f;
        juce::Path bolt;
        int zaps = 0;
        int gifts = 0;            // store effects she'll zap onto the shelf this visit (0 to 3)
    } trixieVisit;
    uint32_t trixieGifts = 0;     // effects she just added: their boxes come from where her bolt landed
    juce::Point<float> trixieGiftAt;
    juce::Image trixieImg;
    double nextTrixieRoll = 60.0;
    static constexpr float trixieChancePerMinute = 0.02f;
    void startTrixie();
    void trixieStep (float dt);
    juce::Rectangle<float> trixieArea() const;
    juce::Point<float> trixieAmulet() const;
    void drawTrixie (juce::Graphics&) const;
    juce::Path makeBolt (juce::Point<float> from, juce::Point<float> to, float jag);
    friend class Library;
    friend class StoreShelves;
    friend class WeatherBar;
    friend class OrderRow;
    friend class PresetStrip;
    std::unique_ptr<PresetStrip> presetStrip;
    std::unique_ptr<OrderRow> orderRow;
    std::unique_ptr<WeatherBar> weatherBar;
    std::unique_ptr<StoreShelves> storeShelves;

    //==========================================================================
    // the sphere: the bouquets stand on a ring round its floor and float up inside it, the grains fly
    // through it, and you turn it round by dragging the empty glass
public:
    static constexpr float sphereR = 226.0f;            // its radius in px (at the depth of its centre)
    static constexpr float camDist = 4.2f * sphereR;    // how far away we look at it from
    static constexpr float floorZ = -0.56f * sphereR;   // the floor the bows stand on
    juce::Point<float> sphereCentre { 546.0f, 298.0f };
    float viewYaw = 0.0f, viewPitch = 0.36f;
    // zoom: scroll or pinch on the empty glass (towards the cursor); double-click the glass to set it back
    float viewZoom = 1.0f;
    juce::Point<float> viewPan;
    juce::Point<float> viewCentre() const noexcept { return sphereCentre + viewPan; }
    void zoomAt (juce::Point<float> at, float factor);
    void resetView();
    fp::ambi::Vec3 toView (fp::ambi::Vec3 w) const noexcept;        // world -> (right, up, towards you)
    fp::ambi::Vec3 fromView (fp::ambi::Vec3 v) const noexcept;
    juce::Point<float> project (fp::ambi::Vec3 w, float* scale = nullptr, float* depth = nullptr) const noexcept;
    fp::ambi::Vec3 unproject (juce::Point<float> screen, float depth) const noexcept;
    bool floorHit (juce::Point<float> screen, fp::ambi::Vec3& out) const noexcept;   // where a click lands on the floor
    float sphereScreenRadius() const noexcept;
    bool insideSphere (juce::Point<float> screen) const noexcept { return screen.getDistanceFrom (viewCentre()) < sphereScreenRadius(); }
    fp::ambi::Vec3 ringPoint (float shelfX) const noexcept;         // a place along the order -> its spot on the floor's ring
    static constexpr float stringScale = 0.8f, balloonScale = 0.82f;
private:
    bool rotating = false;
    juce::Point<float> rotateFrom;
    float yawFrom = 0.0f, pitchFrom = 0.0f;
    void drawSphereBack (juce::Graphics&) const;
    // Speed: what doesn't move every frame is drawn once into an image and reused: the room's background
    // (until the preset or the weather's tint changes), the sphere's glass, grid and floor (until you turn
    // or zoom it), and the energy on the glass (stamped into pixels a few times a second)
    struct Layer { juce::Image img; juce::Rectangle<float> area; std::vector<float> key; float scale = 0.0f; };
    mutable Layer bgLayer, backLayer, frontLayer, roomLayer;
    void drawRoom (juce::Graphics&, juce::Rectangle<float> full, float blur);
    void drawRoomItems (juce::Graphics&);
    juce::Image skinBack, skinFront;
    juce::Rectangle<float> skinArea;
    template <typename Fn> void drawCached (juce::Graphics&, Layer&, juce::Rectangle<float> area, std::vector<float> key, Fn&& paintFn) const;
    std::vector<float> viewKey() const;
    juce::Rectangle<float> sphereArea() const;
    void renderSkin();
    void drawSphereFront (juce::Graphics&) const;
    void depthSort();
    std::vector<juce::Component*> drawnOrder;
    int meterFrame = 0;
    float meterRef = -120.0f;

    // the energy meter (after the IEM EnergyVisualizer): on the sphere's skin, and flat in its own frame
    std::vector<fp::ambi::Vec3> meterDirs;
    std::vector<float> meterDb;
    std::vector<fp::ambi::Vec3> mapDirs;   // the flat map's cells (Hammer-Aitoff)
    std::vector<float> mapDb;
    int mapCols = 0, mapRows = 0;
    void meterStep();
    void drawEnergySkin (juce::Graphics&, bool front) const;
    void drawEnergyMap (juce::Graphics&) const;
    juce::Rectangle<float> mapArea { 20.0f, 134.0f, 240.0f, 132.0f };
    static juce::Colour meterColour (float db);

    // the grains: little balloon bouquets, each a whole copy of the effects, flying in the sphere.
    // Drop more in from the jar; drag one back to the jar to take one away.
    friend class GrainMarker;
    juce::OwnedArray<juce::Component> grainMarkers;
    juce::Rectangle<float> jarArea { 146.0f, 300.0f, 84.0f, 104.0f };
    bool jarDragging = false;
    juce::Point<float> jarDragAt, grainDropAt;
    void addGrain (const fp::ambi::Vec3* at);
    void removeGrain();
    int grainCount() const;
    void drawJar (juce::Graphics&) const;
    void drawListener (juce::Graphics&) const;
    void grainStep (float dt);
    std::unique_ptr<juce::ParameterAttachment> grainsAtt;
    float grainsValue = 1.0f;

    // the order of the effects (the list on the left): roll a new one, or set one dragged in the list
    void rollOrder();
    void setVisibleOrder (const std::vector<int>& visible);
    std::unique_ptr<FadePopup> fadePopup;
    friend class FadePopup;

    // the card (presets, scale, bypass) waits above the top of the window until the cursor comes near
    juce::Rectangle<int> cardHome { 16, 12, 252, 108 };
    float cardShown = 0.0f;
    void placeCard();
    void revealStep (float dt);   // the card and the order list slide in when the cursor is near them

    StoreMenuLook storeLook;
    static constexpr float pinkieX = 262.0f;
    void drawPinkie (juce::Graphics&) const;
    juce::Point<float> pinkieHoof() const noexcept { return { pinkieX - 16.0f, (float) designH - 24.0f - pinkieHop }; }
    juce::Rectangle<float> umbrellaImageArea() const noexcept;
    bool umbrellaHit (juce::Point<float> p, juce::Point<float>& normal) const noexcept;

    // the balloon store: a sign at the top; what you buy comes as four balloons (its two controls, level, pan)
    juce::Rectangle<float> storeSign { 832.0f, 8.0f, 96.0f, 38.0f };   // just the board (no awning)
    Balloon* storeBalloons[kStoreItems][4] {};
    bool storeShown[kStoreItems] {};
    void openStore();
    void setBought (int fx, bool bought);
    void syncStore();
    void drawStoreSign (juce::Graphics&) const;

    // the helium canister slides along the floor: left sends the frozen grains back through the effects, right straight out
    static constexpr float railLeft = 326.0f, railRight = 446.0f;
    std::unique_ptr<juce::ParameterAttachment> thruAtt;
    float thruValue = 0.0f, tankGrabX = 0.0f, tankDownX = 0.0f;
    bool tankDragging = false, tankMoved = false;
    void drawToken (juce::Graphics&, const Pill&) const;
    juce::OwnedArray<Pill> pills;
    Pill* dragPill = nullptr;
    float dragStartNorm = 0.0f, dragStartY = 0.0f;
    void addPill (const char* id, const juce::String& name, juce::Point<float> centre, int hue, int icon);
    void clickPill (Pill&, int direction);
    juce::String pillText (const Pill&) const;

    std::unique_ptr<PitchBubble> pitchBubble;
    std::unique_ptr<PinTool> pin;
    void pinStep (float dt);
    void popPumpBalloon (Pump&, bool sound);

    Pump popPump, soundPump;
    float nextSemis = 0.0f, nextWheelAcc = 0.0f;
    juce::Rectangle<float> nextBadge;
    int nextHue = 0;
    float fullFlash = 0.0f;

    std::vector<Particle> particles, puffs;
    std::vector<Drop> drops;
    std::vector<Ripple> ripples;
    std::vector<Burst> bursts;
    float dropBudget = 0.0f, puddle = 0.0f, valveAngle = 0.0f, puffBudget = 0.0f;
    float rainVis = 0.0f, heliumVis = 0.0f;   // the look follows the switches directly (no audio needed)

    float rainValue = 0.0f, heliumValue = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> rainAtt, heliumAtt;

    // cloudy weather: clouds drift through the bouquet (a balloon inside one slowly wanders to random
    // values); with rain, lightning strikes from them and lends a store effect for a few seconds
    struct SkyCloud { juce::Point<float> p; float vx, s, phase; fp::ambi::Vec3 w {}; float amount = 0.0f, depth = 0.0f, radius = 100.0f; int kind = 0; };   // (now: the weather drifting in the sphere)
    std::vector<SkyCloud> skyClouds;
    float cloudyValue = 0.0f, bypassValue = 0.0f, cloudVis = 0.0f, bypassVis = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> cloudyAtt, bypassAtt;
    juce::Rectangle<float> cloudyButton { 296.0f, 4.0f, 92.0f, 62.0f }, bypassButton;   // the weather, in a row across the top

    // snow: flakes drift down and pile up on the floor, the balloons ice over and stop moving
    // as the sound freezes (the look follows the audio's own freeze amount)
    struct Flake { juce::Point<float> p; float vy, size, phase; };
    std::vector<Flake> flakes;
    float snowValue = 0.0f, snowVis = 0.0f, frozen = 0.0f, snowDepth = 0.0f, flakeBudget = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> snowAtt;
    juce::Rectangle<float> snowButton { 498.0f, 12.0f, 100.0f, 40.0f };
    // (the snow's and the heat's fade times: right-click either one, see FadePopup)
    void drawRainButton (juce::Graphics&) const;

    // the heatwave: a sun with the air wobbling under it; the whole shop goes warm and shimmers
    float heatValue = 0.0f, heatVis = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> heatAtt;
    juce::Rectangle<float> heatButton { 604.0f, 12.0f, 110.0f, 40.0f };
    void drawHeatButton (juce::Graphics&) const;
    void drawHeatShimmer (juce::Graphics&, juce::Rectangle<float> full) const;

    // auto weather: a little switch; while it's on the sky changes by itself every few minutes
    float autoValue = 0.0f, autoVis = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> autoAtt;
    juce::Rectangle<float> autoButton { 720.0f, 22.0f, 96.0f, 20.0f };
    void drawAutoSwitch (juce::Graphics&) const;
    void snowStep (float dt);
    void drawSnowButton (juce::Graphics&) const;
    void drawSnowbank (juce::Graphics&) const;
    int seenLightning = 0;
    float flash = 0.0f;
    juce::Path bolt;
    juce::Point<float> strikeAt { 520.0f, 260.0f };
    uint32_t lentShown = 0;        // store effects showing because the lightning lent them
    void cloudStep (float dt);
    void strike();
    bool insideCloud (const Floater&) const;
    juce::Point<float> cloudAt (const SkyCloud&) const;   // where a cloud is drawn right now (drifting, bobbing, shaken by thunder)
    void drawSkyClouds (juce::Graphics&) const;
    void drawCloudyButton (juce::Graphics&) const;
    void drawBypassSwitch (juce::Graphics&) const;
    juce::Rectangle<float> cloudButton { 742.0f, 14.0f, 96.0f, 62.0f }, tankArea { 296.0f, 418.0f, 46.0f, 76.0f };

    juce::Point<float> lastMouse;
    bool haveMouse = false;
    std::vector<juce::Point<float>> dust;

    juce::ComboBox scaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scaleAtt;
    juce::Rectangle<int> infoCard;
    juce::String intervalText, freezeText;
    float lastFreeze = 0.0f;
    juce::Random rnd;

    double lastTick = 0.0, clock = 0.0;

    friend struct FreakieTestAccess;
    friend class Floater;
    friend class PinTool;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreakiePieEditor)
};

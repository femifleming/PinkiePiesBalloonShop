#include <sys/resource.h>
#include <map>
#include <set>
// Runs the real plugin processor + editor headlessly: odd block sizes, sample rates,
// state round trip, and renders a screenshot of the UI.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

using namespace juce;
extern int mellyloonDbgFrames;

// every failed check says where it is
#define FAIL_HERE() do { ++fails; std::printf ("  ^ FAILED (HostTest.cpp line %d)\n", __LINE__); } while (0)
// a Balloon Shop check that compares the output with the input sample for sample (or measures a level
// through it): in the sphere the output is always spatialised (encoded, then decoded to binaural), so
// these can't hold; it's reported, not counted (tests/SphereTest.cpp checks the sphere's own sound)
static int skipped = 0;
#define NOT_IN_SPHERE() do { ++skipped; std::printf ("  ^ (not applicable in the sphere: the output is spatialised; line %d)\n", __LINE__); } while (0)

// the plugin's output is a little late (the limiter looks 1.5 ms ahead): this lines the input up with it
struct DelayMatch
{
    explicit DelayMatch (int n) : h ((size_t) std::max (1, n + 1), 0.0f) {}
    float push (float x) { h[pos] = x; pos = (pos + 1) % h.size(); return h[pos]; }
    std::vector<float> h;
    size_t pos = 0;
};

struct FreakieTestAccess
{
    // run the bouquet's physics for a while, as the 60 Hz timer would
    static void run (FreakiePieEditor& e, float seconds)
    {
        for (int i = 0; i < (int) (seconds * 60.0f); ++i)
        {
            e.lastTick -= 1.0 / 60.0;
            e.timerCallback();
        }
    }
    // exactly 'seconds' of animation, however long the frames take to save
    static void runExact (FreakiePieEditor& e, float seconds)
    {
        for (int i = 0; i < (int) std::lround (seconds * 60.0f); ++i)
        {
            e.lastTick = Time::getMillisecondCounterHiRes() * 0.001 - 1.0 / 60.0;
            e.timerCallback();
        }
    }
    static Floater& balloon (FreakiePieEditor& e, int i) { return *e.floaters[i]; }
    static void setView (FreakiePieEditor& e, float yaw, float pitch) { e.viewYaw = yaw; e.viewPitch = pitch; }
    static void setZoom (FreakiePieEditor& e, float z) { e.viewZoom = z; }
    static Point<float> bow (FreakiePieEditor& e) { return e.anchors[0].pos; }
    static int count (FreakiePieEditor& e) { return e.numParamBalloons; }
    static float coldVis (FreakiePieEditor& e) { return e.coldVis; }
    static constexpr int stutterItem = FreakiePieEditor::stutterItem;
    static std::array<Balloon*, 4> stutterBalloons (FreakiePieEditor& e) { auto* b = e.storeBalloons[stutterItem]; return { b[0], b[1], b[2], b[3] }; }
    static float sleepVis (FreakiePieEditor& e) { return e.sleepVis; }
    static void buy (FreakiePieEditor& e, int fx, bool b) { e.setBought (fx, b); }
    static std::vector<Balloon*> storeBalloonsAlive (FreakiePieEditor& e)
    {
        std::vector<Balloon*> v;
        for (auto& row : e.storeBalloons) for (auto* b : row) if (b != nullptr && b->alive && b->isVisible()) v.push_back (b);
        return v;
    }
    static Point<float> hoof (FreakiePieEditor& e) { return e.pinkieHoof(); }
    static void asleepFor (FreakiePieEditor& e, double s) { e.lastActivity = e.clock - s; }
    static float scared (FreakiePieEditor& e) { return e.scaredVis; }
    static float sleepy (FreakiePieEditor& e) { return e.sleepVis; }
    static float crystal (FreakiePieEditor& e) { return e.crystalVis; }
    static int backgroundShown (FreakiePieEditor& e) { return e.bgTo; }
    static void showPresetMenu (FreakiePieEditor& e) { e.presetBox.showPopup(); }
    static void showStore (FreakiePieEditor& e) { e.openStore(); }
    static StoreShelves& shelves (FreakiePieEditor& e) { return *e.storeShelves; }
    static bool bought (FreakiePieEditor& e, int fx) { return e.bought (fx); }
    static std::vector<int> shelfItems (FreakiePieEditor& e) { return e.shelfItems(); }
    static void roll (FreakiePieEditor& e) { e.rollOrder(); }
    static void asTheyWere (FreakiePieEditor& e) { auto v = e.shelfItems(); std::sort (v.begin(), v.end()); e.setVisibleOrder (v); }
    static OrderRow& orderList (FreakiePieEditor& e) { return *e.orderRow; }
    static void showOrder (FreakiePieEditor& e) { e.orderRow->step (1.0f, true); }   // as if the cursor came to the left edge
    static void showCard (FreakiePieEditor& e, bool on) { e.cardShown = on ? 1.0f : 0.0f; e.placeCard(); }
    static Rectangle<int> card (FreakiePieEditor& e) { return e.infoCard; }
    static FadePopup& fades (FreakiePieEditor& e) { return *e.fadePopup; }
    static float wetVis (FreakiePieEditor& e) { return e.wetVis; }
    static float umbrellaVis (FreakiePieEditor& e) { return e.umbrellaVis; }
    static Rectangle<float> helpButton (FreakiePieEditor& e) { return e.helpButton; }
    static Rectangle<float> storeSign (FreakiePieEditor& e) { return e.storeSign; }
    static int numPills (FreakiePieEditor& e) { return e.pills.size(); }
    static Rectangle<float> popsButton (FreakiePieEditor& e) { return e.popsButton; }
    static Component& overlay (FreakiePieEditor& e) { return e.overlay; }
    static Rectangle<float> chalkRest (FreakiePieEditor& e) { return e.chalkboard->restArea(); }
    static Point<float> pinHome (FreakiePieEditor& e) { return e.pin->home; }

    static Point<float> pillAt (FreakiePieEditor& e, const char* id)
    {
        for (auto* pl : e.pills) if (pl->param == e.proc.apvts.getParameter (id)) return pl->home;
        return { -1.0f, -1.0f };
    }
    // a right-click on the weather row
    static void rightClick (Component& c, Point<float> local)
    {
        MouseEvent me (Desktop::getInstance().getMainMouseSource(), local, ModifierKeys (ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, Time(), local, Time(), 1, false);
        c.mouseDown (me); c.mouseUp (me);
    }
    static void drag (Component& c, Point<float> from, Point<float> to)
    {
        auto ev = [&] (Point<float> q, bool dragging) { return MouseEvent (Desktop::getInstance().getMainMouseSource(), q, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, Time(), from, Time(), 1, dragging); };
        c.mouseDown (ev (from, false));
        for (int k = 1; k <= 10; ++k) c.mouseDrag (ev (from + (to - from) * ((float) k / 10.0f), true));
        c.mouseUp (ev (to, true));
    }
    static void chalkHover (FreakiePieEditor& e, float u, float v)
    {
        auto& c = *e.chalkboard;
        const auto a = c.plotArea();
        const Point<float> p (a.getX() + u * a.getWidth(), a.getBottom() - v * a.getHeight());
        MouseEvent me (Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, Time(), p, Time(), 1, false);
        c.mouseMove (me);
    }
    static bool soundPumpAlive (FreakiePieEditor& e) { return e.soundPump.slot >= 0 && ! e.soundPump.spent; }
    static float bgFade (FreakiePieEditor& e) { return e.bgFade; }
    static Colour themeText (FreakiePieEditor& e) { return e.theme.text; }
    static void openChalk (FreakiePieEditor& e) { e.chalkboard->bringForward(); }
    static void chalkPage (FreakiePieEditor& e, int pg) { e.chalkboard->setPage (pg); }
    static void chalkAway (FreakiePieEditor& e)   // a click outside the board
    {
        auto& c = *e.chalkboard;
        const Point<float> p (6.0f, 6.0f);
        MouseEvent me (Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, Time(), p, Time(), 1, false);
        c.mouseDown (me); c.mouseUp (me);
    }
    static Point<float> boxOf (FreakiePieEditor& e, int fx) { return e.anchors[FreakiePieEditor::firstBox + fx].pos; }
    static float shelfY() { return FreakiePieEditor::shelfY; }
    static Point<float> anchorPos (FreakiePieEditor& e, int i) { return e.anchors[i].pos; }
    static void trixie (FreakiePieEditor& e) { e.startTrixie(); }
    static void trixieNoisy (FreakiePieEditor& e) { if (! e.trixieVisit.noisy) { e.trixieVisit.noisy = true; e.proc.trixieNoise.request (7u, 6.6f); } }
    static bool trixieHere (FreakiePieEditor& e) { return e.trixieVisit.on; }
    static void trixieGifts (FreakiePieEditor& e, int n) { e.trixieVisit.gifts = n; }
    static int trixieZaps (FreakiePieEditor& e) { return e.trixieVisit.zaps; }
    static Rectangle<float> resetButton (FreakiePieEditor& e) { return e.resetButton; }
    static void relayout (FreakiePieEditor& e) { e.layoutShelf (e.shelfItems()); }
    static bool boxShown (FreakiePieEditor& e, int fx) { return ! e.anchors[FreakiePieEditor::firstBox + fx].hidden; }
    static void openHelp (FreakiePieEditor& e) { mouse (e, e.helpButton.getCentre(), 0); mouse (e, e.helpButton.getCentre(), 2); }
    static void closeHelp (FreakiePieEditor& e) { e.library->close(); }
    static bool helpShowing (FreakiePieEditor& e) { return e.library->isVisible(); }
    static int helpChapters (FreakiePieEditor& e) { return e.library->numChapters(); }
    static void helpChapter (FreakiePieEditor& e, int c) { e.library->showChapter (c); }
    static bool chalkShowing (FreakiePieEditor& e) { return e.chalkboard->isVisible(); }
    // draw a stroke across the chalkboard's drawing area: (u, v) in 0..1, v up
    static void chalkStroke (FreakiePieEditor& e, const std::vector<Point<float>>& uv)
    {
        auto& c = *e.chalkboard;
        const auto a = c.plotArea();
        for (size_t i = 0; i < uv.size(); ++i)
        {
            const Point<float> p (a.getX() + uv[i].x * a.getWidth(), a.getBottom() - uv[i].y * a.getHeight());
            MouseEvent me (Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, Time(), p, Time(), 1, i > 0);
            if (i == 0) c.mouseDown (me); else c.mouseDrag (me);
            if (i + 1 == uv.size()) c.mouseUp (me);
        }
    }
    static void chalkButton (FreakiePieEditor& e, int i)
    {
        auto& c = *e.chalkboard;
        const auto p = c.buttonArea (i).getCentre();
        MouseEvent me (Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, Time(), p, Time(), 1, false);
        c.mouseDown (me); c.mouseUp (me);
    }
    // mockup helper: where every balloon and token is and what it says, as JSON (test program only)
    static void dumpLayout (FreakiePieEditor& e, const File& f)
    {
        Array<var> items;
        for (auto* fl : e.floaters)
        {
            if (! fl->alive || ! fl->isVisible()) continue;
            auto* o = new DynamicObject();
            o->setProperty ("x", fl->pos.x); o->setProperty ("y", fl->pos.y);
            o->setProperty ("r", fl->collisionRadius()); o->setProperty ("fill", fl->inflation());
            if (auto* b = dynamic_cast<Balloon*> (fl)) { o->setProperty ("label", b->label); o->setProperty ("hue", b->hue); o->setProperty ("kind", "balloon"); }
            else o->setProperty ("kind", "sound");
            items.add (var (o));
        }
        for (auto* pl : e.pills)
        {
            auto* o = new DynamicObject();
            o->setProperty ("x", pl->c.x); o->setProperty ("y", pl->c.y); o->setProperty ("r", pl->rad);
            o->setProperty ("label", pl->name); o->setProperty ("value", e.pillText (*pl)); o->setProperty ("kind", "token");
            items.add (var (o));
        }
        f.replaceWithText (JSON::toString (var (items)));
    }
    static Rectangle<float> tank (FreakiePieEditor& e) { return e.tankArea; }
    static int bouncedDrops (FreakiePieEditor& e) { int n = 0; for (auto& d : e.drops) n += d.bounced ? 1 : 0; return n; }
    static void mouse (FreakiePieEditor& e, Point<float> p, int phase)
    {
        MouseEvent me (Desktop::getInstance().getMainMouseSource(), p, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &e.content, &e.content, Time(), p, Time(), 1, phase == 1);
        e.contentMouse (me, phase);
    }
    static int floaters (FreakiePieEditor& e) { return e.floaters.size(); }
    static void holdPopPump (FreakiePieEditor& e, bool on) { e.popPump.holding = on; }
    static void startSound (FreakiePieEditor& e) { e.startSoundPump(); }
    static void releaseSound (FreakiePieEditor& e) { e.releaseSoundPump(); }
    static void setNext (FreakiePieEditor& e, float s) { e.nextSemis = s; }
    static void rain (FreakiePieEditor& e, bool on) { e.rainValue = on ? 1.0f : 0.0f; }
    // the clouds live in the processor now (they drift through the sphere): put the first one round a balloon
    static void cloudTo (FreakiePieEditor& e, const Floater& f)
    {
        auto& pv = e.proc.patchView[0];
        pv.x.store (f.w.x / FreakiePieEditor::sphereR); pv.y.store (f.w.y / FreakiePieEditor::sphereR); pv.z.store (f.w.z / FreakiePieEditor::sphereR);
        pv.amount.store (1.0f); pv.radius.store (0.45f);
    }
    static std::vector<float> cloudsAway (FreakiePieEditor& e)
    {
        std::vector<float> saved;
        for (auto& pv : e.proc.patchView) { saved.push_back (pv.amount.load()); pv.amount.store (0.0f); }
        return saved;
    }
    static void cloudsBack (FreakiePieEditor& e, const std::vector<float>& saved)
    {
        for (size_t i = 0; i < saved.size(); ++i) e.proc.patchView[i].amount.store (saved[i]);
    }
    // standing in its place round the sphere's floor
    static int boxAnchor (int fx) { return FreakiePieEditor::firstBox + fx; }
    static bool onRing (FreakiePieEditor& e, int anchor) { auto& a = e.anchors[anchor]; return a.onShelf && (a.hidden || std::abs (a.shelfX - a.slotX) < 2.0f); }
    static bool tied (FreakiePieEditor& e, const Floater& f) { return (f.w - e.anchors[f.anchor].w).length() < (f.stringLength + 36.0f) * FreakiePieEditor::stringScale + 4.0f; }
    static float cloudVis (FreakiePieEditor& e) { return e.cloudVis; }
    static float flash (FreakiePieEditor& e) { return e.flash; }
    static Rectangle<float> rainboomButton (FreakiePieEditor& e) { return e.rainboomButton; }
    static bool rainboomShowing (FreakiePieEditor& e) { return e.rainboomLayer->isVisible(); }
    static fp::ambi::Vec3 boomAt (FreakiePieEditor& e) { return e.boomScene.at * FreakiePieEditor::sphereR; }
    static void throwBalloon (FreakiePieEditor& e, Floater& f, Point<float> v) { f.flickVel = v; e.rainboomFromThrow (f); }
    static uint32_t lent (FreakiePieEditor& e) { return e.lentShown; }
    // a click goes to whatever is on top there: the weather row, the order row, or else the room itself
    static void press (Component& c, Point<float> local, int clicks = 1)
    {
        MouseEvent me (Desktop::getInstance().getMainMouseSource(), local, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, Time(), local, Time(), clicks, false);
        c.mouseDown (me); c.mouseUp (me);
    }
    static bool clickAt (FreakiePieEditor& e, Rectangle<float> r)
    {
        const auto p = r.getCentre();
        for (Component* c : { (Component*) e.presetStrip.get(), (Component*) e.cardPanel.get(), (Component*) e.fadePopup.get(), (Component*) e.orderRow.get(), (Component*) e.weatherBar.get() })
            if (c != nullptr && c->isVisible() && c->getBounds().toFloat().contains (p)
                && c->hitTest ((int) (p.x - (float) c->getX()), (int) (p.y - (float) c->getY())))
            {
                press (*c, p - c->getPosition().toFloat());
                return true;
            }
        mouse (e, p, 0); mouse (e, p, 2);
        return true;
    }
    static Rectangle<float> rainButton (FreakiePieEditor& e) { return e.cloudButton; }
    static Component& content (FreakiePieEditor& e) { return e.content; }
    static Component& weatherRow (FreakiePieEditor& e) { return *e.weatherBar; }
    static Rectangle<float> orderDie (FreakiePieEditor& e) { return e.orderRow->dieArea() + e.orderRow->getPosition().toFloat(); }
    static PresetStrip& strip (FreakiePieEditor& e) { return *e.presetStrip; }
    static Component& presetBox (FreakiePieEditor& e) { return e.presetBox; }
    static Rectangle<float> cloudyButton (FreakiePieEditor& e) { return e.cloudyButton; }
    static Rectangle<float> bypassButton (FreakiePieEditor& e) { return e.bypassButton; }
    static Rectangle<float> snowButton (FreakiePieEditor& e) { return e.snowButton; }
    static Rectangle<float> heatButton (FreakiePieEditor& e) { return e.heatButton; }
    static Rectangle<float> autoButton (FreakiePieEditor& e) { return e.autoButton; }
    static float meltVis (FreakiePieEditor& e) { return e.meltVis; }
    static int flakes (FreakiePieEditor& e) { return (int) e.flakes.size(); }
    // find a parameter balloon by its label
    static Balloon* byLabel (FreakiePieEditor& e, const String& label)
    {
        for (int i = 0; i < e.numParamBalloons; ++i)
            if (auto* b = dynamic_cast<Balloon*> (e.floaters[i])) if (b->label == label) return b;
        return nullptr;
    }
    // hold the pin with its tip on a point
    static void pinAt (FreakiePieEditor& e, Point<float> tipPoint, bool held)
    {
        e.pin->dragging = held;
        if (held) { e.pin->pos = tipPoint - (e.pin->tip() - e.pin->pos); e.pin->place(); }
    }
    static void openBubble (FreakiePieEditor& e, SoundBalloon* sb) { e.showPitchBubble (sb); e.pitchBubble->field.setText ("+12", false); }
    static void closeBubble (FreakiePieEditor& e) { e.pitchBubble->close(); }
    static float pinHomeDistance (FreakiePieEditor& e) { return e.pin->pos.getDistanceFrom (e.pin->home); }
    static SoundBalloon* firstSound (FreakiePieEditor& e)
    {
        for (int i = e.numParamBalloons; i < e.floaters.size(); ++i)
            if (auto* sb = dynamic_cast<SoundBalloon*> (e.floaters[i])) if (sb->alive) return sb;
        return nullptr;
    }
    // type into the pitch bubble of the first caught sound, return its pitch afterwards
    static float typePitch (FreakiePieEditor& e, const String& text)
    {
        SoundBalloon* sb = nullptr;
        for (int i = e.numParamBalloons; i < e.floaters.size(); ++i)
            if (auto* s = dynamic_cast<SoundBalloon*> (e.floaters[i])) { sb = s; break; }
        if (sb == nullptr) return -999.0f;
        e.showPitchBubble (sb);
        e.pitchBubble->field.setText (text, false);
        e.pitchBubble->commit();
        return sb->semis;
    }
};


// MANUAL_DUMP=<folder>: the parameters and the help chapters, as JSON, for the manual
static int dumpForManual (const char* dir)
{
    const File out (dir);
    out.createDirectory();
    FreakiePieProcessor p;
    Array<var> params;
    for (auto* ap : p.getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (ap))
        {
            DynamicObject::Ptr o = new DynamicObject();
            o->setProperty ("id", r->getParameterID());
            o->setProperty ("name", r->getName (100));
            o->setProperty ("default", r->getText (r->getDefaultValue(), 100));
            o->setProperty ("min", r->getText (0.0f, 100));
            o->setProperty ("max", r->getText (1.0f, 100));
            String type = "continuous";
            if (auto* c = dynamic_cast<AudioParameterChoice*> (r)) { type = "choice"; o->setProperty ("choices", c->choices.joinIntoString (", ")); }
            else if (dynamic_cast<AudioParameterBool*> (r) != nullptr) type = "switch";
            else if (dynamic_cast<AudioParameterInt*> (r) != nullptr) type = "steps";
            o->setProperty ("type", type);
            params.add (var (o.get()));
        }
    Array<var> help;
    for (int i = 0; i < manualChapterCount(); ++i)
    {
        DynamicObject::Ptr o = new DynamicObject();
        o->setProperty ("title", String (manualChapterTitle (i)));
        o->setProperty ("body", String::fromUTF8 (manualChapterBody (i)));
        help.add (var (o.get()));
    }
    Array<var> presets;
    for (int i = 0; i < p.getNumPrograms(); ++i) presets.add (p.getProgramName (i));
    out.getChildFile ("params.json").replaceWithText (JSON::toString (var (params)));
    out.getChildFile ("help.json").replaceWithText (JSON::toString (var (help)));
    out.getChildFile ("presets.json").replaceWithText (JSON::toString (var (presets)));
    std::printf ("manual material: %d parameters, %d chapters, %d presets in %s\n", params.size(), help.size(), presets.size(), out.getFullPathName().toRawUTF8());
    return 0;
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IOLBF, 0);   // one line at a time, so a long run shows where it is
    ScopedJuceInitialiser_GUI init;
    if (auto* md = std::getenv ("MANUAL_DUMP")) return dumpForManual (md);
    int fails = 0;
    FreakiePieProcessor::rememberLastState = false;   // start every test instance from the defaults

    // MELLY_LIVE=1: the real window on screen, sound running, frames counted in several states
    if (std::getenv ("MELLY_LIVE") != nullptr)
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 16, 48000, 512);
        p.prepareToPlay (48000, 512);
        auto set = [&] (const char* id, float v) { if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        DocumentWindow win ("Mellyloon fps", Colours::black, 0);
        win.setUsingNativeTitleBar (true);
        auto* ed = p.createEditorAndMakeActive();
        win.setContentOwned (ed, true);
        // MELLY_LIVE_SCALE: the window's size against the design (a host opens it at 0.9)
        const float sc = std::getenv ("MELLY_LIVE_SCALE") != nullptr ? (float) std::atof (std::getenv ("MELLY_LIVE_SCALE")) : 1.0f;
        ed->setSize ((int) (FreakiePieEditor::designW * sc), (int) (FreakiePieEditor::designH * sc));
        std::printf ("window %d x %d\n", ed->getWidth(), ed->getHeight());
        win.centreWithSize (win.getWidth(), win.getHeight());
        win.setVisible (true);
        win.toFront (true);
        if (auto* peer = win.getPeer(); peer != nullptr && std::getenv ("MELLY_RENDER") != nullptr)
        {
            peer->setCurrentRenderingEngine (std::atoi (std::getenv ("MELLY_RENDER")));
            std::printf ("renderer: %s\n", peer->getAvailableRenderingEngines()[peer->getCurrentRenderingEngine()].toRawUTF8());
        }
        AudioBuffer<float> buf (16, 512); MidiBuffer m;
        Random rnd (4);
        int blk = 0;
        double worst = 1e9;
        auto run = [&] (const char* what)
        {
            MessageManager::getInstance()->runDispatchLoopUntil (800);
            const int f0 = mellyloonDbgFrames;
            const double t0 = Time::getMillisecondCounterHiRes();
            auto cpuNow = [] { rusage u {}; getrusage (RUSAGE_SELF, &u); return (double) u.ru_utime.tv_sec + 1e-6 * (double) u.ru_utime.tv_usec + (double) u.ru_stime.tv_sec + 1e-6 * (double) u.ru_stime.tv_usec; };
            const double c0 = cpuNow();
            while (Time::getMillisecondCounterHiRes() - t0 < 3000.0)
            {
                buf.clear();
                for (int i = 0; i < 512; ++i) { const float v = 0.2f * std::sin ((blk * 512 + i) * 0.02f) + 0.03f * (rnd.nextFloat() - 0.5f); buf.setSample (0, i, v); buf.setSample (1, i, v); }
                ++blk; p.processBlock (buf, m);
                MessageManager::getInstance()->runDispatchLoopUntil (10);
            }
            const double fps = (mellyloonDbgFrames - f0) / ((Time::getMillisecondCounterHiRes() - t0) * 0.001);
            std::printf ("live fps (%s): %.1f   (%.1f ms of CPU a frame, sound included)\n", what, fps, 1000.0 * (cpuNow() - c0) / jmax (1, mellyloonDbgFrames - f0));
            worst = jmin (worst, fps);
        };
        run ("default: 4 grains orbiting");
        set (ids::grains, 16.0f);
        run ("16 grains");
        set (ids::cloudy, 1.0f); set (ids::rain, 1.0f);
        run ("16 grains in a storm");
        set (ids::cloudy, 0.0f); set (ids::rain, 0.0f); set (ids::snow, 1.0f);
        run ("16 grains, snowing");
        set (ids::snow, 0.0f);
        if (auto* fe = dynamic_cast<FreakiePieEditor*> (ed)) FreakieTestAccess::setZoom (*fe, 1.8f);
        run ("zoomed in");
        if (auto* fe = dynamic_cast<FreakiePieEditor*> (ed)) FreakieTestAccess::setZoom (*fe, 1.0f);
        p.requestRainboom (0.7f);
        run ("a sonic rainboom");
        win.clearContentComponent();
        std::printf (worst >= 50.0 ? "LIVE FPS OK (worst %.1f)\n" : "LIVE FPS TOO LOW (worst %.1f)\n", worst);
        return worst >= 50.0 ? 0 : 1;
    }

    // SPHERE_SHOTS=<folder>: the sphere with sound running through it, from a few angles and settings
    if (auto* dir = std::getenv ("SPHERE_SHOTS"))
    {
        const File out (dir);
        out.createDirectory();
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 16, 48000, 512);
        p.prepareToPlay (48000, 512);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto& e = *dynamic_cast<FreakiePieEditor*> (ed.get());
        ed->setSize (FreakiePieEditor::designW, FreakiePieEditor::designH);
        AudioBuffer<float> blk (16, 512); MidiBuffer mm;
        Random rnd (3);
        float uiDue = 0.0f;
        double t = 0.0;
        auto play = [&] (float seconds)
        {
            for (int k = 0; k < (int) (seconds * 48000 / 512); ++k)
            {
                blk.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const double tt = t + i / 48000.0;
                    const int n = (int) (tt * 3.0);
                    const double u = tt - n / 3.0;
                    const float v = (float) (0.3 * std::exp (-u * 6.0) * std::sin (6.2831853 * 220.0 * std::pow (2.0, ((n * 5) % 12) / 12.0) * u)) + 0.02f * (rnd.nextFloat() - 0.5f);
                    blk.setSample (0, i, v); blk.setSample (1, i, v);
                }
                t += 512 / 48000.0;
                p.processBlock (blk, mm);
                uiDue += 512.0f / 48000.0f;
                while (uiDue >= 1.0f / 60.0f) { uiDue -= 1.0f / 60.0f; FreakieTestAccess::run (e, 1.0f / 60.0f); }
            }
        };
        auto shoot = [&] (const String& name)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            File f = out.getChildFile (name + ".png");
            f.deleteFile();
            FileOutputStream os (f);
            PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        auto set = [&] (const char* id, float v) { if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        play (4.0f);
        shoot ("sphere_one_grain");
        set (ids::grains, 6.0f); set (ids::flight, 2.0f); set (ids::deviate, 0.7f); set (ids::flightSpeed, 0.6f);
        play (6.0f);
        shoot ("sphere_six_lorenz");
        FreakieTestAccess::setView (e, 1.1f, 0.95f);
        play (2.0f);
        shoot ("sphere_turned");
        FreakieTestAccess::setView (e, 0.0f, 0.36f);
        set (ids::cloudy, 1.0f); set (ids::snow, 1.0f); set (ids::grains, 12.0f); set (ids::flight, 3.0f);
        play (6.0f);
        shoot ("sphere_weather_flock");
        set (ids::cloudy, 0.0f); set (ids::snow, 0.0f); set (ids::grainMode, 1.0f); set (ids::grains, 10.0f); set (ids::flight, 1.0f); set (ids::grMix, 1.0f);
        play (6.0f);
        shoot ("sphere_scatter_orbit");
        // a sound balloon caught and let go: mid-way on its flight into the sphere
        set (ids::grainMode, 0.0f); set (ids::grains, 4.0f); set (ids::grMix, 0.25f);
        FreakieTestAccess::startSound (e);
        play (2.0f);
        FreakieTestAccess::releaseSound (e);
        FreakieTestAccess::run (e, 0.4f);
        shoot ("sphere_flying_in");
        play (2.0f);
        // zoomed in: the room behind blurs and darkens
        FreakieTestAccess::setZoom (e, 1.9f);
        play (1.0f);
        shoot ("sphere_zoomed");
        return 0;
    }

    // TRIXIE_MOVIE=<folder>: record one of Trixie's visits (a noisy one), 30 frames a second plus its sound
    if (auto* dir = std::getenv ("TRIXIE_MOVIE"))
    {
        const File out (dir);
        out.createDirectory();
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 320);
        p.prepareToPlay (48000, 320);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto& e = *dynamic_cast<FreakiePieEditor*> (ed.get());
        ed->setSize (FreakiePieEditor::designW, FreakiePieEditor::designH);
        AudioBuffer<float> blk (2, 320), sound (2, 48000 * 10); MidiBuffer mm;
        sound.clear();
        int at = 0, frame = 0;
        auto tick = [&] (bool keep)
        {
            for (int k = 0; k < 5; ++k)   // 1600 samples = one frame at 30 fps
            {
                blk.clear();
                p.processBlock (blk, mm);
                if (keep && at + 320 <= sound.getNumSamples()) { for (int c = 0; c < 2; ++c) sound.copyFrom (c, at, blk, c, 0, 320); at += 320; }
            }
            FreakieTestAccess::runExact (e, 1.0f / 30.0f);
            if (keep)
            {
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                FileOutputStream os (out.getChildFile (String::formatted ("f%04d.png", frame++)));
                PNGImageFormat().writeImageToStream (img, os);
            }
        };
        for (int i = 0; i < 150; ++i) tick (false);   // let the bouquet settle
        for (int i = 0; i < 30; ++i) tick (true);     // a second of the shop as it is
        FreakieTestAccess::trixie (e);
        FreakieTestAccess::trixieNoisy (e);
        FreakieTestAccess::trixieGifts (e, 2);
        for (int i = 0; i < 9 * 30; ++i) tick (true); // her visit, and a moment after
        File wavFile = out.getChildFile ("sound.wav");
        wavFile.deleteFile();
        WavAudioFormat wav;
        if (auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (new FileOutputStream (wavFile), 48000.0, 2, 24, {}, 0)))
            w->writeFromAudioSampleBuffer (sound, 0, at);
        std::printf ("recorded %d frames and %.1f s of sound\n", frame, at / 48000.0);
        return 0;
    }

    // presets: each one loads, stays finite; the feedback ones keep singing after the input stops,
    // the stretch ones use long, sparse sine grains
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 512);
        p.prepareToPlay (48000, 512);
        AudioBuffer<float> b (2, 512); MidiBuffer mm;
        Random nr (3);
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            p.prepareToPlay (48000, 512);
            double tail = 0; float pk = 0; bool finite = true;
            const int blocks = (int) (6.0 * 48000 / 512);
            for (int k = 0; k < blocks; ++k)
            {
                for (int c = 0; c < 2; ++c)
                    for (int s = 0; s < 512; ++s)
                        b.setSample (c, s, k * 512 < 48000 ? 0.25f * (nr.nextFloat() * 2.0f - 1.0f) : 0.0f);   // 1 s of noise, then quiet
                p.processBlock (b, mm);
                for (int c = 0; c < 2; ++c)
                    for (int s = 0; s < 512; ++s)
                    {
                        const float v = b.getSample (c, s);
                        finite = finite && std::isfinite (v);
                        pk = jmax (pk, std::abs (v));
                        if (k >= blocks - (int) (48000 / 512)) tail += v * v;
                    }
            }
            const double tailRms = std::sqrt (tail / (2.0 * (int) (48000 / 512) * 512));
            const auto& pr = presets::list()[(size_t) i];
            const String group (pr.group);
            bool ok = finite && pk <= fp::PeakLimiter::ceiling;
            if (group == "feedback") ok = ok && tailRms > 0.003;   // still ringing 4-5 s after the input stopped
            if (group == "stretch")
                ok = ok && roundToInt (p.apvts.getRawParameterValue (ids::grShape)->load()) == 5
                        && p.apvts.getRawParameterValue (ids::grSize)->load() >= 850.0f;
            for (int fx : pr.bought) ok = ok && p.apvts.getRawParameterValue (ids::store (fx, ids::storeOwned))->load() > 0.5f;
            std::printf ("preset %-24s %-8s peak %.2f, last-second rms %.4f %s\n", pr.name, pr.group, pk, tailRms, ok ? "" : "  <-- FAIL");
            if (! ok) FAIL_HERE();
        }

        // the chalk drawing and the preset come back with the session
        p.setCurrentProgram (3);
        for (int i = 0; i < fp::ChalkShape::N; ++i) p.chalk.set (i, i % 2 == 0 ? 1.0f : 0.25f);
        MemoryBlock mb; p.getStateInformation (mb);
        FreakiePieProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int i = 0; i < fp::ChalkShape::N; ++i) same = same && std::abs (q.chalk.v[(size_t) i].load() - p.chalk.v[(size_t) i].load()) < 1.0e-3f;
        std::printf ("session recall: chalk shape %d, preset %d -> %d; spectral controls now: %s %s, old ones gone %d\n", same,
                     p.getCurrentProgram(), q.getCurrentProgram(), q.apvts.getParameter (ids::sgMix) ? "mix" : "-",
                     q.apvts.getParameter (ids::sgFreeze) ? "freeze" : "-", q.apvts.getParameter ("sgSmear") == nullptr);
        if (! same || q.getCurrentProgram() != 3 || q.apvts.getParameter ("sgSmear") != nullptr) FAIL_HERE();
    }

    // the main output never clips: a hot input with the main feedback all the way up stays under the ceiling
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 512);
        p.prepareToPlay (48000, 512);
        *p.apvts.getRawParameterValue (ids::masterFb) = 1.0f;
        *p.apvts.getRawParameterValue (ids::psFeedback) = 0.95f;
        AudioBuffer<float> b (2, 512); MidiBuffer mm;
        Random r (1);
        float pk = 0.0f;
        for (int k = 0; k < (int) (6.0 * 48000 / 512); ++k)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, k < 200 ? 4.0f * (r.nextFloat() * 2.0f - 1.0f) : 0.0f);
            p.processBlock (b, mm);
            pk = jmax (pk, b.getMagnitude (0, 0, 512), b.getMagnitude (1, 0, 512));
        }
        std::printf ("limiter: +12 dB noise into full feedback, output peak %.4f (ceiling %.4f)\n", pk, fp::PeakLimiter::ceiling);
        if (pk > fp::PeakLimiter::ceiling) FAIL_HERE();
    }
    {
        // and after working hard it comes all the way back to unity (a float stalling just short of 1 once left it 0.002 dB down)
        fp::PeakLimiter lim; lim.prepare (48000.0);
        float L[256], R[256];
        Random r (3);
        for (int b = 0; b < 200; ++b) { for (int i = 0; i < 256; ++i) L[i] = R[i] = 3.0f * (r.nextFloat() - 0.5f); lim.process (L, R, 256); }
        for (int b = 0; b < 400; ++b) { for (int i = 0; i < 256; ++i) L[i] = R[i] = 0.1f * (r.nextFloat() - 0.5f); lim.process (L, R, 256); }
        std::printf ("limiter back at rest after limiting: gain %.9f\n", lim.getGain());
        if (lim.getGain() != 1.0f) FAIL_HERE();
    }

    // the output stage stays clean: a hot sine (where the leveler works) and an over-the-ceiling one
    // (where the limiter works too) come out with their harmonics far below the tone
    {
        auto thd = [] (float amp, float& levGain, float& limGain)
        {
            FreakiePieProcessor p;
            p.setPlayConfigDetails (2, 2, 48000, 512);
            p.prepareToPlay (48000, 512);
            for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
                *p.apvts.getRawParameterValue (id) = 0.0f;
            AudioBuffer<float> b (2, 512); MidiBuffer mm;
            std::vector<float> out;
            double ph = 0.0;
            const double f0 = 1000.0;
            for (int k = 0; k < (int) (2.0 * 48000 / 512); ++k)
            {
                for (int i = 0; i < 512; ++i) { const float v = amp * (float) std::sin (ph); ph += fp::kTwoPi * f0 / 48000.0; b.setSample (0, i, v); b.setSample (1, i, v); }
                p.processBlock (b, mm);
                if (k >= 94) out.insert (out.end(), b.getReadPointer (0), b.getReadPointer (0) + 512);
            }
            levGain = p.levelerGainNow(); limGain = p.limiterGainNow();
            auto mag = [&] (double f)
            {
                double re = 0, im = 0;
                for (size_t i = 0; i < out.size(); ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (fp::kTwoPi * (double) i / (double) (out.size() - 1));
                    re += w * out[i] * std::cos (fp::kTwoPi * f * (double) i / 48000.0);
                    im += w * out[i] * std::sin (fp::kTwoPi * f * (double) i / 48000.0);
                }
                return std::sqrt (re * re + im * im);
            };
            const double fund = mag (f0);
            double harm = 0.0;
            for (int h = 2; h <= 7; ++h) harm += std::pow (mag (f0 * h), 2.0);
            return 20.0 * std::log10 (std::sqrt (harm) / fund);
        };
        float lg1, mg1, lg2, mg2;
        const double hot = thd (0.8f, lg1, mg1), over = thd (2.0f, lg2, mg2);
        std::printf ("output stage: sine at -2 dBFS -> harmonics %.0f dB below (leveler %.1f dB), at +6 dBFS -> %.0f dB below (leveler %.1f dB, limiter %.1f dB)\n",
                     -hot, 20 * std::log10 (lg1), -over, 20 * std::log10 (lg2), 20 * std::log10 (mg2));
        if (hot > -60.0 || over > -50.0) FAIL_HERE();
    }

    // the last patch you had open comes back in a new instance (all the parameters, the drawings, the preset)
    {
        const auto file = File::getSpecialLocation (File::tempDirectory).getChildFile ("freakiepie_last_patch_test.xml");
        file.deleteFile();
        FreakiePieProcessor::lastStateOverride = file;
        FreakiePieProcessor::rememberLastState = true;
        bool recalled = false;
        {
            FreakiePieProcessor a;
            a.apvts.getParameter (ids::psShift)->setValueNotifyingHost (a.apvts.getParameter (ids::psShift)->convertTo0to1 (-5.0f));
            a.apvts.getParameter (ids::grainsLevel)->setValueNotifyingHost (a.apvts.getParameter (ids::grainsLevel)->convertTo0to1 (0.5f));
            for (int i = 0; i < fp::EqCurve::N; ++i) a.eq.set (i, 0.7f);
            a.eq.fit();
        }   // closing it saves
        {
            FreakiePieProcessor b;
            recalled = std::abs (b.apvts.getRawParameterValue (ids::psShift)->load() + 5.0f) < 0.01f
                    && std::abs (b.apvts.getRawParameterValue (ids::grainsLevel)->load() - 0.5f) < 0.01f
                    && std::abs (b.eq.at (0.5f) - 0.7f) < 1e-3f;
        }
        FreakiePieProcessor::rememberLastState = false;
        FreakiePieProcessor::lastStateOverride = File();
        file.deleteFile();
        std::printf ("last patch remembered by a new instance: %d\n", recalled);
        if (! recalled) FAIL_HERE();
    }

    // the balloon store's effects, on their own
    {
        const double sr = 48000.0;
        const int N = (int) sr;
        auto runStore = [&] (int fx, std::function<void (fp::StoreFX::Settings&)> tweak, std::function<float (int)> input,
                             std::vector<float>& wet, int scaleIndex = 0)
        {
            fp::StoreFX st;
            st.prepare (sr);
            fp::StoreFX::AllSettings set;
            for (int e = 0; e < fp::StoreFX::kNum; ++e)
            {
                const auto& in = fp::StoreFX::infos()[(size_t) e];
                set[(size_t) e].c1 = in.c1.def; set[(size_t) e].c2 = in.c2.def;
            }
            if (fx >= 0) { set[(size_t) fx].owned = true; if (tweak) tweak (set[(size_t) fx]); }
            wet.assign ((size_t) N, 0.0f);
            std::vector<float> L (256), R (256);
            for (int b = 0; b < N / 256; ++b)
            {
                for (int i = 0; i < 256; ++i) L[(size_t) i] = R[(size_t) i] = input (b * 256 + i);
                st.process (L.data(), R.data(), 256, set, fp::getScales()[(size_t) scaleIndex]);
                for (int i = 0; i < 256; ++i) wet[(size_t) (b * 256 + i)] = 0.5f * (L[(size_t) i] + R[(size_t) i]) - input (b * 256 + i);
            }
        };
        Random nr (7);
        std::vector<float> noiseSig ((size_t) N);
        for (int i = 0; i < N; ++i) noiseSig[(size_t) i] = i < N / 4 ? 0.3f * (nr.nextFloat() * 2.0f - 1.0f) : 0.0f;
        auto noise = [&] (int i) { return noiseSig[(size_t) i]; };
        auto sine = [&] (int i) { return 0.3f * (float) std::sin (fp::kTwoPi * 440.0 * i / sr); };
        std::vector<float> wet;

        runStore (-1, {}, noise, wet);
        float idle = 0.0f;
        for (auto v : wet) idle = jmax (idle, std::abs (v));
        std::printf ("store, nothing bought: output differs from input by %.2g\n", idle);
        if (idle != 0.0f) FAIL_HERE();

        for (int e = 0; e < fp::StoreFX::kNum; ++e)
        {
            runStore (e, {}, noise, wet);
            double ss = 0; float pk = 0; bool finite = true;
            for (auto v : wet) { ss += v * v; pk = jmax (pk, std::abs (v)); finite = finite && std::isfinite (v); }
            const double rmsW = std::sqrt (ss / N);
            std::printf ("store %-18s wet rms %.4f peak %.3f%s\n", fp::StoreFX::infos()[(size_t) e].name, rmsW, pk, finite ? "" : " NOT FINITE");
            if (! finite || rmsW < 1.0e-3 || pk > 4.0f) FAIL_HERE();
        }

        // the ring mod: 440 Hz times a 1000 Hz carrier (snapped to B, 987.8 Hz) comes out as the sum and difference
        {
            runStore (fp::StoreFX::ringMod, [] (fp::StoreFX::Settings& s) { s.c1 = 1000.0f; s.c2 = 0.0f; s.level = 1.0f; }, sine, wet);
            auto amp = [&] (double f)
            {
                double re = 0, im = 0;
                for (int i = N / 2; i < N; ++i) { re += wet[(size_t) i] * std::cos (fp::kTwoPi * f * i / sr); im += wet[(size_t) i] * std::sin (fp::kTwoPi * f * i / sr); }
                return 2.0 * std::sqrt (re * re + im * im) / (N / 2);
            };
            const double sum = amp (987.77 + 440.0), diff = amp (987.77 - 440.0), orig = amp (440.0);
            std::printf ("ring mod: 440 Hz x 987.8 Hz -> %.3f at the sum, %.3f at the difference, %.4f left at 440\n", sum, diff, orig);
            if (sum < 0.1 || diff < 0.1 || orig > 0.02) FAIL_HERE();
        }

        // pitch: the harmonizer's -4.4 st lands on the major scale's -5 (440 -> ~330 Hz)
        auto zcHz = [&] (const std::vector<float>& x, int from)
        {
            int zc = 0;
            for (int i = from + 1; i < N; ++i) if ((x[(size_t) i - 1] < 0.0f) != (x[(size_t) i] < 0.0f)) ++zc;
            return 0.5 * zc * sr / (N - from);
        };
        runStore (fp::StoreFX::harmonizer, [] (fp::StoreFX::Settings& s) { s.c1 = -4.4f; s.c2 = 0.0f; s.level = 1.0f; }, sine, wet);
        const double hHz = zcHz (wet, N / 2);
        // and the waveguide's 105 Hz lands on A (110 Hz): autocorrelation of its ring
        runStore (fp::StoreFX::waveguide, [] (fp::StoreFX::Settings& s) { s.c1 = 105.0f; s.c2 = 20.0f; }, noise, wet);
        int bestLag = 0; double best = -1e9;
        for (int lag = (int) (sr / 300); lag < (int) (sr / 60); ++lag)
        {
            double c = 0;
            for (int i = N / 2; i < N / 2 + 8192; ++i) c += wet[(size_t) i] * wet[(size_t) (i - lag)];
            if (c > best) { best = c; bestLag = lag; }
        }
        const double wgHz = sr / bestLag;
        std::printf ("scale snapping (major): harmonizer -4.4 st -> ~%.0f Hz (want 330), waveguide 105 Hz -> ~%.1f Hz (want 110)\n", hHz, wgHz);
        if (std::abs (hHz - 329.6) > 12 || std::abs (wgHz - 110.0) > 3) FAIL_HERE();
        // a microtonal scale moves it: Slendro has no note near A, so 105 Hz goes elsewhere
        int slendro = 0;
        for (int i = 0; i < (int) fp::getScales().size(); ++i) if (fp::getScales()[(size_t) i].name == "Slendro") slendro = i;
        const double slHz = fp::StoreFX::snapHz (105.0, fp::getScales()[(size_t) slendro]);
        std::printf ("scales: %d in all, Slendro snaps 105 Hz to %.1f Hz\n", (int) fp::getScales().size(), slHz);
        if (slendro == 0 || std::abs (slHz - 110.0) < 1.0 || fp::getScales().size() < 40) FAIL_HERE();
    }

    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        *p.apvts.getRawParameterValue (ids::masterFb) = 0.6f;
        Random rnd (42);
        AudioBuffer<float> buf (2, 4096);
        MidiBuffer midi;
        double peak = 0; bool finite = true;
        const int sizes[] = { 1, 7, 64, 333, 512, 1024, 4096, 13 };
        for (int it = 0; it < 600; ++it)
        {
            const int n = sizes[it % 8];
            buf.setSize (2, n, false, false, true);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) buf.setSample (c, i, rnd.nextFloat() * 0.4f - 0.2f);
            if (it % 50 == 0) *p.apvts.getRawParameterValue (ids::grFreeze) = (it / 50) % 2 ? 1.0f : 0.0f;
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i)
            { const float v = buf.getSample (c, i); if (! std::isfinite (v)) finite = false; peak = jmax (peak, (double) std::abs (v)); }
        }
        std::printf ("sr %6.0f: finite=%d peak=%.2f\n", sr, finite, peak);
        if (! finite || peak > 4.0) FAIL_HERE();
    }

    // mono in / stereo out
    {
        FreakiePieProcessor p;
        AudioProcessor::BusesLayout l; l.inputBuses.add (AudioChannelSet::mono()); l.outputBuses.add (AudioChannelSet::stereo());
        const bool ok = p.setBusesLayout (l);
        p.prepareToPlay (48000, 256);
        AudioBuffer<float> buf (2, 256); buf.clear(); MidiBuffer m;
        for (int i = 0; i < 100; ++i) { buf.setSample (0, 0, 0.5f); p.processBlock (buf, m); }
        std::printf ("mono->stereo layout supported: %d\n", ok);
        if (! ok) FAIL_HERE();
    }

    // state round trip
    {
        FreakiePieProcessor a, b;
        a.apvts.getParameter (ids::psScale)->setValueNotifyingHost (0.5f);
        a.apvts.getParameter (ids::rvDecay)->setValueNotifyingHost (0.8f);
        MemoryBlock mb; a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool same = std::abs (a.apvts.getParameter (ids::psScale)->getValue() - b.apvts.getParameter (ids::psScale)->getValue()) < 1e-6
                       && std::abs (a.apvts.getParameter (ids::rvDecay)->getValue() - b.apvts.getParameter (ids::rvDecay)->getValue()) < 1e-6;
        std::printf ("state round trip: %d\n", same);
        if (! same) FAIL_HERE();
    }

    // weather on the master out: rain = gentle lowpass, helium = gentle highpass, off = untouched
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 256);
        p.prepareToPlay (48000, 256);
        for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
            *p.apvts.getRawParameterValue (id) = 0.0f;
        Random rnd (5);
        AudioBuffer<float> buf (2, 256); MidiBuffer m;
        DelayMatch dly (p.getLatencySamples());
        auto measure = [&] (float seconds, double& hfRatio, double& lfRatio, double& maxErr)
        {
            double hin = 0, hout = 0, lin = 0, lout = 0; maxErr = 0;
            float pi = 0, po = 0, ai = 0, ao = 0;
            const int blocks = (int) (seconds * 48000 / 256);
            for (int b = 0; b < blocks; ++b)
            {
                std::vector<float> in (256);
                for (int i = 0; i < 256; ++i) { in[(size_t) i] = rnd.nextFloat() * 0.5f - 0.25f; buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]); }
                p.processBlock (buf, m);
                if (b < blocks - (int) (48000 / 256)) { for (int i = 0; i < 256; ++i) dly.push (in[(size_t) i]); continue; }   // measure the last second
                for (int i = 0; i < 256; ++i)
                {
                    const float x = dly.push (in[(size_t) i]), y = buf.getSample (0, i);
                    maxErr = jmax (maxErr, (double) std::abs (y - x));
                    hin += (x - pi) * (x - pi); hout += (y - po) * (y - po);   // first difference ~ highs
                    ai += 0.01f * (x - ai); ao += 0.01f * (y - ao);             // slow average ~ lows
                    lin += ai * ai; lout += ao * ao;
                    pi = x; po = y;
                }
            }
            hfRatio = std::sqrt (hout / hin); lfRatio = std::sqrt (lout / lin);
        };
        double hf, lf, err;
        *p.apvts.getRawParameterValue (ids::rain) = 1.0f;
        measure (3.0f, hf, lf, err);
        std::printf ("rain   : highs x%.2f  lows x%.2f\n", hf, lf);
        if (! (hf < 0.8 && lf > 0.9)) FAIL_HERE();
        *p.apvts.getRawParameterValue (ids::rain) = 0.0f;
        *p.apvts.getRawParameterValue (ids::helium) = 1.0f;
        measure (5.0f, hf, lf, err);
        std::printf ("helium : highs x%.2f  lows x%.2f\n", hf, lf);
        if (! (lf < 0.85 && hf > 0.9)) NOT_IN_SPHERE();
        *p.apvts.getRawParameterValue (ids::helium) = 0.0f;
        measure (6.0f, hf, lf, err);
        std::printf ("both off again: max difference from input %g\n", err);
        if (err != 0.0) NOT_IN_SPHERE();
    }

    // clouds: thunder rolls pull the rain's lowpass down at random; with rain too, lightning strikes and
    // lends a store effect for a few seconds (without buying it); clear skies again: untouched
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 256);
        p.prepareToPlay (48000, 256);
        for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
            *p.apvts.getRawParameterValue (id) = 0.0f;
        Random rnd (11);
        AudioBuffer<float> buf (2, 256); MidiBuffer m;
        float maxThunder = 0.0f; uint32_t lentSeen = 0; bool finite = true; double maxErr = 0.0;
        DelayMatch dly (p.getLatencySamples());
        auto run = [&] (double sec)
        {
            maxErr = 0.0;
            for (int b = 0; b < (int) (sec * 48000 / 256); ++b)
            {
                std::vector<float> in (256);
                for (int i = 0; i < 256; ++i) { in[(size_t) i] = rnd.nextFloat() * 0.5f - 0.25f; buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]); }
                p.processBlock (buf, m);
                for (int i = 0; i < 256; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    maxErr = jmax (maxErr, (double) std::abs (buf.getSample (0, i) - dly.push (in[(size_t) i])));
                }
                maxThunder = jmax (maxThunder, p.thunderAmount.load());
                lentSeen |= p.struckMask.load();
            }
        };
        *p.apvts.getRawParameterValue (ids::cloudy) = 1.0f;
        *p.apvts.getRawParameterValue (ids::rain) = 1.0f;
        run (25.0);
        bool boughtAny = false;
        for (int e = 0; e < fp::StoreFX::kNum; ++e) boughtAny |= *p.apvts.getRawParameterValue (ids::store (e, ids::storeOwned)) > 0.5f;
        std::printf ("storm: %d lightning strikes, lent effects 0x%03x, thunder down to %.2f, bought nothing %d, finite %d\n",
                     p.lightningCount.load(), (unsigned) lentSeen, maxThunder, ! boughtAny, finite);
        if (p.lightningCount.load() < 2 || lentSeen == 0 || maxThunder < 0.4f || boughtAny || ! finite) FAIL_HERE();
        *p.apvts.getRawParameterValue (ids::cloudy) = 0.0f;
        *p.apvts.getRawParameterValue (ids::rain) = 0.0f;
        run (100.0);   // lent effects stay 45 to 90 s (a strike just before the storm ends can last until 115 s)
        run (1.0);
        std::printf ("clear skies again: lent effects 0x%03x, max difference from input %g\n", (unsigned) p.struckMask.load(), maxErr);
        if (p.struckMask.load() != 0 || maxErr != 0.0) NOT_IN_SPHERE();
    }

    // snow: the output freezes over 5 s (a 440 Hz tone keeps sounding after the input moves to 660 Hz),
    // stays frozen while it snows, and thaws back to the untouched input
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 256);
        p.prepareToPlay (48000, 256);
        for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
            *p.apvts.getRawParameterValue (id) = 0.0f;
        AudioBuffer<float> buf (2, 256); MidiBuffer m;
        DelayMatch dly (p.getLatencySamples());
        double ph = 0.0, hz = 440.0;
        std::vector<float> last;
        double maxErr = 0.0;
        auto run = [&] (double sec)
        {
            last.clear(); maxErr = 0.0;
            for (int b = 0; b < (int) (sec * 48000 / 256); ++b)
            {
                std::vector<float> in (256);
                for (int i = 0; i < 256; ++i) { in[(size_t) i] = 0.1f * (float) std::sin (ph); ph += fp::kTwoPi * hz / 48000.0; buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]); }
                p.processBlock (buf, m);
                for (int i = 0; i < 256; ++i)
                {
                    last.push_back (buf.getSample (0, i));
                    maxErr = jmax (maxErr, (double) std::abs (buf.getSample (0, i) - dly.push (in[(size_t) i])));
                }
            }
        };
        auto level = [&] (double f)   // how much of f is in the last run (as a sine amplitude)
        {
            double re = 0, im = 0;
            const size_t n = std::min<size_t> (last.size(), 24000), off = last.size() - n;
            for (size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (fp::kTwoPi * (double) i / (double) (n - 1));
                re += w * last[off + i] * std::cos (fp::kTwoPi * f * (double) i / 48000.0);
                im += w * last[off + i] * std::sin (fp::kTwoPi * f * (double) i / 48000.0);
            }
            return 4.0 * std::sqrt (re * re + im * im) / (double) n;
        };
        run (1.0);
        *p.apvts.getRawParameterValue (ids::snow) = 1.0f;
        run (2.5);
        const float half = p.snowAmount.load();
        run (2.7);
        const float full = p.snowAmount.load();
        hz = 660.0;
        run (1.5);
        const double held440 = level (440.0), new660 = level (660.0);
        *p.apvts.getRawParameterValue (ids::snow) = 0.0f;
        run (5.2);
        const float thawed = p.snowAmount.load();
        run (1.0);
        std::printf ("snow: frozen %.2f after 2.5 s, %.2f after 5.2 s; input moved to 660 Hz -> 440 held at %.3f, 660 only %.4f; "
                     "thawed %.2f after 5.2 s, then untouched (max diff %g)\n", half, full, held440, new660, thawed, maxErr);
        if (std::abs (half - 0.5f) > 0.05f || full < 1.0f || held440 < 0.08 || new660 > 0.005 || thawed > 0.0f) FAIL_HERE();
        if (maxErr != 0.0) NOT_IN_SPHERE();
        // the fade times: 1 min in is half frozen after 30 s; 5 min out is still 90 % frozen 30 s into the thaw;
        // and shortening the thaw halfway carries on from where it is, without a jump
        {
            FreakiePieProcessor q;
            q.setPlayConfigDetails (2, 2, 48000, 256);
            q.prepareToPlay (48000, 256);
            q.apvts.getParameter (ids::snowIn)->setValueNotifyingHost (q.apvts.getParameter (ids::snowIn)->convertTo0to1 (60.0f));
            q.apvts.getParameter (ids::snowOut)->setValueNotifyingHost (q.apvts.getParameter (ids::snowOut)->convertTo0to1 (300.0f));
            const String inText = q.apvts.getParameter (ids::snowIn)->getCurrentValueAsText(), outText = q.apvts.getParameter (ids::snowOut)->getCurrentValueAsText();
            auto go = [&] (double sec) { for (int b = 0; b < (int) (sec * 48000 / 256); ++b) { buf.clear(); q.processBlock (buf, m); } };
            *q.apvts.getRawParameterValue (ids::snow) = 1.0f;
            go (30.0);
            const float in30 = q.snowAmount.load();
            go (31.0);
            const float in61 = q.snowAmount.load();
            *q.apvts.getRawParameterValue (ids::snow) = 0.0f;
            go (30.0);
            const float out30 = q.snowAmount.load();
            q.apvts.getParameter (ids::snowOut)->setValueNotifyingHost (q.apvts.getParameter (ids::snowOut)->convertTo0to1 (5.0f));
            go (256.0 / 48000.0);
            const float afterChange = q.snowAmount.load();
            go (4.6);
            const float out35 = q.snowAmount.load();
            std::printf ("snow fade times (in %s, out %s): %.3f after 30 s in, %.2f after 61 s; %.3f 30 s into the thaw, %.3f just after setting out to 5 s, %.2f 4.6 s later\n",
                         inText.toRawUTF8(), outText.toRawUTF8(), in30, in61, out30, afterChange, out35);
            if (inText != "1 min" || outText != "5 min" || std::abs (in30 - 0.5f) > 0.005f || in61 < 1.0f || std::abs (out30 - 0.9f) > 0.005f
                || std::abs (afterChange - out30) > 0.01f || out35 > 0.0f) FAIL_HERE();
        }
        // heatwave: the sound smears and drifts. A 440 Hz tone is held; when the input moves to 660 Hz
        // the 440 lingers for seconds and the 660 creeps in; the level stays near the input's; switched
        // off it fades over 5 s and the output is untouched again
        {
            FreakiePieProcessor q;
            q.setPlayConfigDetails (2, 2, 48000, 256);
            q.prepareToPlay (48000, 256);
            for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
                *q.apvts.getRawParameterValue (id) = 0.0f;
            DelayMatch dq (q.getLatencySamples());
            double ph3 = 0.0, f = 440.0, err = 0.0;
            std::vector<float> out;
            auto go = [&] (double sec)
            {
                out.clear(); err = 0.0;
                for (int b = 0; b < (int) (sec * 48000 / 256); ++b)
                {
                    std::vector<float> in (256);
                    for (int i = 0; i < 256; ++i) { in[(size_t) i] = 0.1f * (float) std::sin (ph3); ph3 += fp::kTwoPi * f / 48000.0; buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]); }
                    q.processBlock (buf, m);
                    for (int i = 0; i < 256; ++i) { out.push_back (buf.getSample (0, i)); err = jmax (err, (double) std::abs (buf.getSample (0, i) - dq.push (in[(size_t) i]))); }
                }
            };
            auto lev = [&] (double hzz)
            {
                double re = 0, im = 0;
                const size_t n = std::min<size_t> (out.size(), 24000), off = out.size() - n;
                for (size_t i = 0; i < n; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (fp::kTwoPi * (double) i / (double) (n - 1));
                    re += w * out[off + i] * std::cos (fp::kTwoPi * hzz * (double) i / 48000.0);
                    im += w * out[off + i] * std::sin (fp::kTwoPi * hzz * (double) i / 48000.0);
                }
                return 4.0 * std::sqrt (re * re + im * im) / (double) n;
            };
            auto rms = [&]() { double ss = 0; const size_t n = std::min<size_t> (out.size(), 48000); for (size_t i = out.size() - n; i < out.size(); ++i) ss += out[i] * out[i]; return std::sqrt (ss / (double) n) / 0.0707; };
            go (1.0);
            *q.apvts.getRawParameterValue (ids::heat) = 1.0f;
            go (8.0);
            const double steady = rms();
            const float amt = q.heatAmount.load();
            f = 660.0;
            go (2.0);
            const double old2 = lev (440.0), new2 = lev (660.0);
            go (20.0);
            const double old22 = lev (440.0), new22 = lev (660.0);
            *q.apvts.getRawParameterValue (ids::heat) = 0.0f;
            go (6.0);
            go (1.0);
            std::printf ("heatwave: in %.2f, level %.0f %% of the input; 2 s after 440 -> 660 Hz: 440 at %.3f, 660 at %.3f; 22 s after: 440 at %.3f, 660 at %.3f; "
                         "off again: max diff %g\n", amt, 100.0 * steady, old2, new2, old22, new22, err);
            // and noise comes out as loud as it goes in, too
            Random nr (7);
            double nin = 0.0, nout = 0.0;
            *q.apvts.getRawParameterValue (ids::heat) = 1.0f;
            for (int b = 0; b < (int) (16.0 * 48000 / 256); ++b)
            {
                for (int i = 0; i < 256; ++i) { const float v = 0.1f * (nr.nextFloat() * 2.0f - 1.0f); buf.setSample (0, i, v); buf.setSample (1, i, v); if (b > 10 * 48000 / 256) nin += v * v; }
                q.processBlock (buf, m);
                if (b > 10 * 48000 / 256) for (int i = 0; i < 256; ++i) nout += buf.getSample (0, i) * buf.getSample (0, i);
            }
            const double noiseLevel = std::sqrt (nout / nin);
            std::printf ("heatwave on noise: %.0f %% of the input (the gentle lowpass takes a little of the top)\n", 100.0 * noiseLevel);
            if (noiseLevel < 0.6 || noiseLevel > 1.3) NOT_IN_SPHERE();
            if (amt < 1.0f || old2 < new2 || old2 < 0.03 || new22 < 4.0 * old22) FAIL_HERE();
            if (steady < 0.75 || steady > 1.3 || err != 0.0) NOT_IN_SPHERE();
        }
        // auto weather: ten simulated days of sky. Every spell turns up, each lasts minutes, and the
        // sky only moves to a neighbour (never heat straight to snow or snow to heat)
        {
            fp::AutoWeather aw;
            aw.seed (99);
            aw.start ({});
            int changes = 0, heatToSnow = 0, firstAt = -1;
            int seen[fp::AutoWeather::numSpells] = {};
            auto prev = aw.current();
            double t = 0.0;
            for (int s = 0; s < 10 * 24 * 3600; ++s)
            {
                t += 1.0;
                if (aw.step (1.0))
                {
                    if (firstAt < 0) firstAt = (int) t;
                    const auto now = aw.current();
                    if ((prev == fp::AutoWeather::heat && now == fp::AutoWeather::snow) || (prev == fp::AutoWeather::snow && now == fp::AutoWeather::heat)) ++heatToSnow;
                    ++seen[(int) now]; ++changes; prev = now;
                }
            }
            const double meanMin = t / 60.0 / jmax (1, changes);
            int kinds = 0; for (int k : seen) kinds += k > 0 ? 1 : 0;
            std::printf ("auto weather: first change after %d s, %d changes in 10 days (a spell every %.1f min on average), %d of 6 kinds, heat<->snow jumps %d\n",
                         firstAt, changes, meanMin, kinds, heatToSnow);
            if (firstAt < 30 || firstAt > 91 || meanMin < 3.0 || meanMin > 10.0 || kinds != 6 || heatToSnow != 0) FAIL_HERE();
        }
        // auto weather in the plug-in: switched on, time passing moves the switches; switched off, it leaves them
        {
            FreakiePieProcessor q;
            q.apvts.getParameter (ids::autoWeather)->setValueNotifyingHost (1.0f);
            q.autoWeatherStep (0.0);
            int flips = 0;
            bool last[4] = {};
            for (int s = 0; s < 4 * 3600; ++s)
            {
                q.autoWeatherStep (1.0);
                const bool now[4] = { *q.apvts.getRawParameterValue (ids::rain) > 0.5f, *q.apvts.getRawParameterValue (ids::cloudy) > 0.5f,
                                      *q.apvts.getRawParameterValue (ids::snow) > 0.5f, *q.apvts.getRawParameterValue (ids::heat) > 0.5f };
                for (int k = 0; k < 4; ++k) { if (now[k] != last[k]) ++flips; last[k] = now[k]; }
            }
            q.apvts.getParameter (ids::autoWeather)->setValueNotifyingHost (0.0f);
            const float before = *q.apvts.getRawParameterValue (ids::rain) + 2 * *q.apvts.getRawParameterValue (ids::cloudy) + 4 * *q.apvts.getRawParameterValue (ids::snow) + 8 * *q.apvts.getRawParameterValue (ids::heat);
            for (int s = 0; s < 3600; ++s) q.autoWeatherStep (1.0);
            const float after = *q.apvts.getRawParameterValue (ids::rain) + 2 * *q.apvts.getRawParameterValue (ids::cloudy) + 4 * *q.apvts.getRawParameterValue (ids::snow) + 8 * *q.apvts.getRawParameterValue (ids::heat);
            std::printf ("auto weather in the plug-in: %d switch changes over 4 hours; off for an hour: sky %s\n", flips, before == after ? "left alone" : "CHANGED");
            if (flips < 20 || before != after) FAIL_HERE();
        }
        // the way in stays smooth: no dip while the snow builds (a slow crossfade against the late resynthesis would cancel)
        {
            FreakiePieProcessor q;
            q.setPlayConfigDetails (2, 2, 48000, 256);
            q.prepareToPlay (48000, 256);
            for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
                *q.apvts.getRawParameterValue (id) = 0.0f;
            double ph2 = 0.0, lowest = 1.0;
            for (int b = 0; b < (int) (7.0 * 48000 / 256); ++b)
            {
                if (b == (int) (1.0 * 48000 / 256)) *q.apvts.getRawParameterValue (ids::snow) = 1.0f;
                for (int i = 0; i < 256; ++i) { const float v = 0.1f * (float) std::sin (ph2); ph2 += fp::kTwoPi * 440.0 / 48000.0; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                q.processBlock (buf, m);
                const int start = (int) (1.0 * 48000 / 256), handover = start + (int) (0.40 * 48000 / 256);   // a full 16384 window, then 40 ms
                if (b > 10 && b % 8 == 0 && (b < start || b > handover))
                {
                    // the block's level, against the 0.1 going in (rms of a sine is 0.0707)
                    double ss = 0; for (int i = 0; i < 256; ++i) ss += buf.getSample (0, i) * buf.getSample (0, i);
                    lowest = std::min (lowest, std::sqrt (ss / 256.0) / 0.0707);
                }
            }
            std::printf ("snow coming in (after the 40 ms handover): the level never dips below %.0f %% of the input\n", 100.0 * lowest);
            if (lowest < 0.7) FAIL_HERE();
        }
        // frozen noise is smooth: its loudness, measured every 10 ms, barely moves (no flutter or loop)
        {
            FreakiePieProcessor q;
            q.setPlayConfigDetails (2, 2, 48000, 256);
            q.prepareToPlay (48000, 256);
            for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
                *q.apvts.getRawParameterValue (id) = 0.0f;
            Random rr (4);
            std::vector<double> env;
            double acc = 0; int cnt = 0;
            float lp = 0.0f;
            for (int b = 0; b < (int) (11.0 * 48000 / 256); ++b)
            {
                if (b == (int) (1.0 * 48000 / 256)) *q.apvts.getRawParameterValue (ids::snow) = 1.0f;
                for (int i = 0; i < 256; ++i)
                {
                    lp += 0.2f * ((rr.nextFloat() - 0.5f) - lp);   // a soft, dark noise
                    const float v = b < (int) (7.0 * 48000 / 256) ? 0.3f * lp : 0.0f;   // it stops after the freeze is complete
                    buf.setSample (0, i, v); buf.setSample (1, i, v);
                }
                q.processBlock (buf, m);
                if (b < (int) (8.0 * 48000 / 256)) continue;   // measure the held sound, input silent
                for (int i = 0; i < 256; ++i)
                {
                    acc += buf.getSample (0, i) * buf.getSample (0, i);
                    if (++cnt == 480) { env.push_back (std::sqrt (acc / cnt)); acc = 0; cnt = 0; }
                }
            }
            double mean = 0, var = 0;
            for (auto v : env) mean += v;
            mean /= (double) env.size();
            for (auto v : env) var += (v - mean) * (v - mean);
            const double cv = std::sqrt (var / (double) env.size()) / std::max (1e-9, mean);
            // does the texture repeat? the loudness pattern's correlation with itself, 30 ms to 1.5 s later
            double repeat = 0.0, e2 = 0.0;
            for (auto v : env) e2 += (v - mean) * (v - mean);
            for (int lag = 3; lag <= 150; ++lag)
            {
                double c = 0.0;
                for (size_t i = 0; i + (size_t) lag < env.size(); ++i) c += (env[i] - mean) * (env[i + (size_t) lag] - mean);
                repeat = std::max (repeat, c / std::max (1e-12, e2));
            }
            std::printf ("snow holding a noise (input gone): level %.4f (the noise went in at %.4f), wavers by %.1f %%, repeats itself %.2f (0 = never, 1 = a loop)\n",
                         mean, 0.3 * 0.096, 100.0 * cv, repeat);
            if (mean < 0.012 || cv > 0.15 || repeat > 0.3) FAIL_HERE();
        }
    }

    // the gumball machine: with no host clock it keeps its own 120 BPM; at full stutter it fires on most
    // steps and uses every kind of event, stays finite and under the ceiling, and at zero leaves the sound alone
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 256);
        p.prepareToPlay (48000, 256);
        for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
            *p.apvts.getRawParameterValue (id) = 0.0f;
        AudioBuffer<float> buf (2, 256); MidiBuffer m;
        DelayMatch dly (p.getLatencySamples());
        Random rnd (21);
        double ph = 0.0, maxErr = 0.0, diffEnergy = 0.0;
        float pk = 0.0f; bool finite = true;
        auto run = [&] (double sec)
        {
            maxErr = 0.0; diffEnergy = 0.0;
            for (int b = 0; b < (int) (sec * 48000 / 256); ++b)
            {
                std::vector<float> in (256);
                for (int i = 0; i < 256; ++i)
                {
                    // a tone with a beat on it, so the stutters have something to chop
                    const double tt = ph / (fp::kTwoPi * 220.0);
                    const float env = (float) std::exp (-8.0 * std::fmod (tt, 0.25));
                    in[(size_t) i] = 0.15f * env * (float) std::sin (ph) + 0.02f * (rnd.nextFloat() - 0.5f);   // under the leveler
                    ph += fp::kTwoPi * 220.0 / 48000.0;
                    buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]);
                }
                p.processBlock (buf, m);
                for (int i = 0; i < 256; ++i)
                {
                    const float y = buf.getSample (0, i), x = dly.push (in[(size_t) i]);
                    finite = finite && std::isfinite (y);
                    pk = jmax (pk, std::abs (y));
                    maxErr = jmax (maxErr, (double) std::abs (y - x));
                    diffEnergy += (y - x) * (y - x);
                }
            }
        };
        run (1.0);   // (the mix balloons glide to zero after starting)
        run (1.0);
        const double untouchedBefore = maxErr;
        // not bought yet: even at full stutter it stays out of the way
        *p.apvts.getRawParameterValue (ids::stAmount) = 1.0f;
        *p.apvts.getRawParameterValue (ids::stCutup) = 0.3f;
        run (3.0);
        const double untouchedUnbought = maxErr;
        const int eventsUnbought = p.stutter.eventCount.load();
        *p.apvts.getRawParameterValue (ids::stOwned) = 1.0f;   // bought at the store
        run (20.0);
        const int events = p.stutter.eventCount.load(), types = p.stutter.typesSeen.load();
        int kinds = 0; for (int k = 0; k < fp::Stutter::numTypes; ++k) kinds += (types >> k) & 1;
        const double changed = std::sqrt (diffEnergy / (20.0 * 48000));
        *p.apvts.getRawParameterValue (ids::stAmount) = 0.0f;
        run (1.5);
        run (1.0);
        const double offAgain = maxErr;
        // bought again at full stutter, then given back to the store mid-stutter: it fades out and leaves the sound alone
        *p.apvts.getRawParameterValue (ids::stAmount) = 1.0f;
        run (2.0);
        *p.apvts.getRawParameterValue (ids::stOwned) = 0.0f;
        const int eventsAtReturn = p.stutter.eventCount.load();
        run (0.5);
        run (1.0);
        const double givenBack = maxErr;
        const bool noNewEvents = p.stutter.eventCount.load() == eventsAtReturn;
        std::printf ("gumball machine: untouched while off (diff %g); not bought at full stutter -> diff %g, %d events; bought, full stutter for 20 s -> %d events of %d kinds, "
                     "changed the sound by rms %.3f, peak %.2f, finite %d; off again -> diff %g; given back -> diff %g, new events %d\n",
                     untouchedBefore, untouchedUnbought, eventsUnbought, events, kinds, changed, pk, finite, offAgain, givenBack, (int) ! noNewEvents);
        if (eventsUnbought != 0 || events < 60 || kinds < fp::Stutter::numTypes || changed < 0.01
            || pk > fp::PeakLimiter::ceiling || ! finite || ! noNewEvents) FAIL_HERE();
        if (untouchedBefore != 0.0 || untouchedUnbought != 0.0 || offAgain != 0.0 || givenBack != 0.0) NOT_IN_SPHERE();
    }

    // a session saved before the stutter was sold at the store: in use, it comes back already bought; unused, it stays in the store
    {
        for (float amount : { 0.6f, 0.0f })
        {
            FreakiePieProcessor a;
            *a.apvts.getRawParameterValue (ids::stAmount) = amount;
            auto xml = a.apvts.copyState().createXml();
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                if (e->getStringAttribute ("id") == ids::stOwned) { xml->removeChildElement (e, true); break; }
            MemoryBlock mb; AudioProcessor::copyXmlToBinary (*xml, mb);
            FreakiePieProcessor b;
            b.setStateInformation (mb.getData(), (int) mb.getSize());
            const bool owned = *b.apvts.getRawParameterValue (ids::stOwned) > 0.5f;
            std::printf ("old session, stutter at %.1f -> %s\n", amount, owned ? "bought" : "still in the store");
            if (owned != (amount > 0.0f)) FAIL_HERE();
        }
    }

    // the spectral smear's tilt: up brightens it, down darkens it, and on pink noise it stays about as loud
    {
        double hiLo[3] {}, rms[3] {};
        const float tilts[3] = { -6.0f, 0.0f, 6.0f };
        for (int t = 0; t < 3; ++t)
        {
            fp::SpectralGate sg;
            sg.prepare (48000.0);
            sg.setCutoffDb (-60.0f); sg.setKnee (1.0f); sg.setAttackMs (150.0f); sg.setSmearSeconds (3.0f); sg.setBlur (0.25f);
            sg.setTiltDb (tilts[t]);
            Random r (5);
            float lpS = 0.0f, hpPrev = 0.0f, hpS = 0.0f, b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
            double eLo = 0.0, eHi = 0.0, e = 0.0;
            const int n = 48000 * 4;
            for (int i = 0; i < n; ++i)
            {
                // pink noise (Paul Kellet's filter): equal energy per octave, roughly how music sits
                const float w = r.nextFloat() * 2.0f - 1.0f;
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                const float x = 0.05f * (b0 + b1 + b2 + w * 0.1848f);
                float l, rr;
                sg.process (x, x, l, rr);
                if (i < 48000 * 2) continue;   // let it settle
                lpS += 0.02f * (l - lpS);                    // lows: a one-pole lowpass at ~150 Hz
                hpS = 0.6f * (hpS + l - hpPrev); hpPrev = l;  // highs: a one-pole highpass at ~4 kHz
                eLo += lpS * lpS; eHi += hpS * hpS; e += l * l;
            }
            hiLo[t] = 10.0 * std::log10 (eHi / std::max (1.0e-20, eLo));
            rms[t] = 10.0 * std::log10 (e / (n / 2));
        }
        std::printf ("spectral tilt: highs over lows %.1f / %.1f / %.1f dB at -6 / 0 / +6 dB/oct; level %.1f / %.1f / %.1f dB\n",
                     hiLo[0], hiLo[1], hiLo[2], rms[0], rms[1], rms[2]);
        if (! (hiLo[0] < hiLo[1] - 10.0 && hiLo[2] > hiLo[1] + 10.0) || std::abs (rms[0] - rms[1]) > 6.0 || std::abs (rms[2] - rms[1]) > 6.0) FAIL_HERE();
    }

    // helpers for the tests below: a tone's amplitude in a stretch of signal
    auto toneAmp = [] (const std::vector<float>& x, double f, double sr = 48000.0)
    {
        double re = 0, im = 0;
        const size_t n = x.size();
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (fp::kTwoPi * (double) i / (double) (n - 1));
            re += w * x[i] * std::cos (fp::kTwoPi * f * (double) i / sr);
            im += w * x[i] * std::sin (fp::kTwoPi * f * (double) i / sr);
        }
        return 4.0 * std::sqrt (re * re + im * im) / (double) n;
    };

    // grain position: 220 Hz for 3 s, then 660 Hz. Half a second later, grains at position 0 (just now)
    // play the 660; grains at 0.9 (3.6 s back) still play the 220
    {
        auto grainsHear = [&] (float pos)
        {
            FreakiePieProcessor p;
            p.setPlayConfigDetails (2, 2, 48000, 256);
            p.prepareToPlay (48000, 256);
            for (auto* id : { ids::sgMix, ids::psMix, ids::rvMix, ids::masterFb, ids::psFeedback, ids::grRandOct, ids::grPosRand })
                *p.apvts.getRawParameterValue (id) = 0.0f;
            *p.apvts.getRawParameterValue (ids::grMix) = 1.0f;
            *p.apvts.getRawParameterValue (ids::grPos) = pos;
            AudioBuffer<float> buf (2, 256); MidiBuffer m;
            double ph = 0.0, hz = 220.0;
            std::vector<float> rec;
            for (int b = 0; b < (int) (4.0 * 48000 / 256); ++b)
            {
                if (b == (int) (3.0 * 48000 / 256)) hz = 660.0;
                for (int i = 0; i < 256; ++i) { const float v = 0.1f * (float) std::sin (ph); ph += fp::kTwoPi * hz / 48000.0; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                p.processBlock (buf, m);
                if (b >= (int) (3.5 * 48000 / 256)) rec.insert (rec.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256);
            }
            return std::make_pair (toneAmp (rec, 220.0), toneAmp (rec, 660.0));
        };
        const auto now = grainsHear (0.0f), back = grainsHear (0.9f);
        std::printf ("grain position: at 0 the grains play 660 Hz %.3f / 220 Hz %.3f; at 0.9 they play 220 Hz %.3f / 660 Hz %.3f\n",
                     now.second, now.first, back.first, back.second);
        if (now.second < 3.0 * now.first || back.first < 3.0 * back.second) FAIL_HERE();
    }

    // store width: at 0 the effect sits in the middle (left = right), at 1 it spreads wide at the same loudness
    {
        auto widthOf = [] (float w, double& sideRatio, double& energy)
        {
            fp::StoreFX st; st.prepare (48000.0);
            fp::StoreFX::AllSettings set;
            for (int e = 0; e < fp::StoreFX::kNum; ++e) { const auto& in = fp::StoreFX::infos()[(size_t) e]; set[(size_t) e].c1 = in.c1.def; set[(size_t) e].c2 = in.c2.def; }
            set[fp::StoreFX::ringMod].owned = true; set[fp::StoreFX::ringMod].c1 = 600.0f; set[fp::StoreFX::ringMod].width = w;
            std::vector<float> L (256), R (256);
            double side = 0, mid = 0; energy = 0;
            Random r (2);
            for (int b = 0; b < 48000 / 256; ++b)
            {
                std::vector<float> in (256);
                for (int i = 0; i < 256; ++i) { in[(size_t) i] = 0.2f * (r.nextFloat() - 0.5f); L[(size_t) i] = R[(size_t) i] = in[(size_t) i]; }
                st.process (L.data(), R.data(), 256, set, fp::getScales()[0]);
                if (b < 40) continue;
                for (int i = 0; i < 256; ++i)
                {
                    const double l = L[(size_t) i] - in[(size_t) i], rr = R[(size_t) i] - in[(size_t) i];   // the effect alone
                    side += (l - rr) * (l - rr); mid += (l + rr) * (l + rr); energy += l * l + rr * rr;
                }
            }
            sideRatio = side / std::max (1e-12, mid);
        };
        double s0, e0, s1, e1;
        widthOf (0.0f, s0, e0); widthOf (1.0f, s1, e1);
        std::printf ("store width: mono -> side/mid %.4f; stereo -> side/mid %.2f, loudness %+.1f dB against mono\n", s0, s1, 10.0 * std::log10 (e1 / e0));
        if (s0 > 1e-6 || s1 < 0.3 || std::abs (10.0 * std::log10 (e1 / e0)) > 1.5) FAIL_HERE();
    }

    // the shelf: the order changes the sound (pitch an octave up then the ring mod gives 880 x 987.8 Hz tones;
    // the ring mod first and the pitch after doesn't), a new order fades in without a click, and it's saved
    {
        auto run = [&] (const FreakiePieProcessor::Order& order, double& at1868, float& biggestJump, bool swapHalfway)
        {
            FreakiePieProcessor p;
            p.setPlayConfigDetails (2, 2, 48000, 256);
            p.prepareToPlay (48000, 256);
            for (auto* id : { ids::sgMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
                *p.apvts.getRawParameterValue (id) = 0.0f;
            *p.apvts.getRawParameterValue (ids::psMix) = 1.0f;
            *p.apvts.getRawParameterValue (ids::psShift) = 12.0f;
            for (int k = 0; k < (int) fp::getScales().size(); ++k)
                if (String (fp::getScales()[(size_t) k].name).startsWithIgnoreCase ("chromatic")) *p.apvts.getRawParameterValue (ids::psScale) = (float) k;
            *p.apvts.getRawParameterValue (ids::store (fp::StoreFX::ringMod, ids::storeOwned)) = 1.0f;
            *p.apvts.getRawParameterValue (ids::store (fp::StoreFX::ringMod, ids::storeA)) = 1000.0f;
            *p.apvts.getRawParameterValue (ids::store (fp::StoreFX::ringMod, ids::storeB)) = 0.0f;
            *p.apvts.getRawParameterValue (ids::store (fp::StoreFX::ringMod, ids::storeWidth)) = 0.0f;
            p.setShelfOrder (order);
            AudioBuffer<float> buf (2, 256); MidiBuffer m;
            double ph = 0.0; float prev = 0.0f; biggestJump = 0.0f;
            std::vector<float> rec;
            for (int b = 0; b < (int) (3.0 * 48000 / 256); ++b)
            {
                if (swapHalfway && b == (int) (1.5 * 48000 / 256))
                {
                    auto o = p.getShelfOrder();
                    std::reverse (o.begin(), o.end());
                    p.setShelfOrder (o);
                }
                for (int i = 0; i < 256; ++i) { const float v = 0.05f * (float) std::sin (ph); ph += fp::kTwoPi * 440.0 / 48000.0; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                p.processBlock (buf, m);
                for (int i = 0; i < 256; ++i)
                {
                    const float y = buf.getSample (0, i);
                    if (b > 40) biggestJump = jmax (biggestJump, std::abs (y - prev));
                    prev = y;
                }
                if (b >= (int) (1.0 * 48000 / 256) && b < (int) (1.5 * 48000 / 256)) rec.insert (rec.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256);
            }
            // (the pitch shifter's two crossfading heads split a tone into sidebands ~15 Hz either side, so look around it)
            at1868 = 0.0;
            for (double f = 1830.0; f <= 1905.0; f += 5.0) at1868 = std::max (at1868, toneAmp (rec, f));
            MemoryBlock mb; p.getStateInformation (mb);
            FreakiePieProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
            return q.getShelfOrder() == p.getShelfOrder();
        };
        using E = fp::Engine;
        FreakiePieProcessor::Order pitchThenRing { E::slotSpectral, E::slotPitch, E::slotStore + fp::StoreFX::ringMod, E::slotGrains, E::slotReverb, E::slotStutter };
        FreakiePieProcessor::Order ringThenPitch { E::slotSpectral, E::slotStore + fp::StoreFX::ringMod, E::slotPitch, E::slotGrains, E::slotReverb, E::slotStutter };
        for (size_t i = 6; i < pitchThenRing.size(); ++i) pitchThenRing[i] = ringThenPitch[i] = -1;   // (filled in with the rest)
        double a1, a2, a3; float j1, j2, j3;
        const bool saved = run (pitchThenRing, a1, j1, false);
        run (ringThenPitch, a2, j2, false);
        run (pitchThenRing, a3, j3, true);
        std::printf ("the shelf: pitch then ring mod -> 1868 Hz at %.4f; ring mod then pitch -> %.4f; reordering mid-sound: biggest step %.4f (steady %.4f); saved %d\n",
                     a1, a2, j3, j1, saved);
        if (a1 < 0.003 || a2 > a1 * 0.2 || j3 > j1 * 2.0f + 0.01f || ! saved) FAIL_HERE();   // (an absolute level: in the sphere it moves with the room round the bubble)
    }

    // Trixie's noise: into the input, so it goes through the effects; a different sound every visit, a chirp
    // per zap, clean and under the ceiling, and silence once she's gone
    {
        auto play = [&] (uint32_t seed, int zaps, float shift, std::vector<float>& out)
        {
            FreakiePieProcessor p;
            p.setPlayConfigDetails (2, 2, 48000, 256);
            p.prepareToPlay (48000, 256);
            for (auto* id : { ids::sgMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
                *p.apvts.getRawParameterValue (id) = 0.0f;
            *p.apvts.getRawParameterValue (ids::psMix) = shift != 0.0f ? 1.0f : 0.0f;
            *p.apvts.getRawParameterValue (ids::psShift) = shift;
            AudioBuffer<float> buf (2, 256); MidiBuffer m;
            out.clear();
            for (int b = 0; b < 40; ++b) { buf.clear(); p.processBlock (buf, m); }
            p.trixieNoise.request (seed, 3.0f);
            for (int b = 0; b < (int) (4.0 * 48000 / 256); ++b)
            {
                if (b > 0 && b % 40 == 0 && b / 40 <= zaps) p.trixieNoise.zap();
                buf.clear();
                p.processBlock (buf, m);
                out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256);
            }
        };
        auto rms = [] (const std::vector<float>& x, size_t from, size_t to) { double s2 = 0; for (size_t i = from; i < to; ++i) s2 += x[i] * x[i]; return std::sqrt (s2 / (double) (to - from)); };
        auto zcRate = [] (const std::vector<float>& x, size_t from, size_t to) { int z = 0; for (size_t i = from + 1; i < to; ++i) z += (x[i - 1] < 0) != (x[i] < 0); return z * 48000.0 / (double) (to - from); };
        std::vector<float> a, b, c, d;
        play (1, 0, 0.0f, a); play (2, 0, 0.0f, b); play (1, 8, 0.0f, c); play (1, 0, 12.0f, d);
        const size_t mid0 = 48000, mid1 = 96000, end0 = (size_t) (3.3 * 48000), end1 = a.size();
        float pk = 0; bool finite = true;
        for (auto* v : { &a, &b, &c, &d }) for (float s2 : *v) { pk = std::max (pk, std::abs (s2)); finite = finite && std::isfinite (s2); }
        const double ra = rms (a, mid0, mid1), rb = rms (b, mid0, mid1), rc = rms (c, mid0, mid1), after = rms (a, end0, end1);
        const double za = zcRate (a, mid0, mid1), zb = zcRate (b, mid0, mid1);
        double diffShift = 0; for (size_t i = mid0; i < mid1; ++i) diffShift += std::abs (a[i] - d[i]);
        {
            // a demo to listen to: three visits' worth (the third with zaps), dry
            std::vector<float> e2;
            play (41, 0, 0.0f, e2);
            AudioBuffer<float> demo (2, (int) (a.size() + e2.size() + c.size()));
            int at = 0;
            for (auto* v : { &a, &e2, &c }) { for (size_t i = 0; i < v->size(); ++i) { demo.setSample (0, at + (int) i, (*v)[i]); demo.setSample (1, at + (int) i, (*v)[i]); } at += (int) v->size(); }
            File f = File::getCurrentWorkingDirectory().getChildFile ("trixie_noise_demo.wav");
            f.deleteFile();
            WavAudioFormat wav;
            if (auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (new FileOutputStream (f), 48000.0, 2, 24, {}, 0)))
                w->writeFromAudioSampleBuffer (demo, 0, demo.getNumSamples());
        }
        std::printf ("trixie's noise: rms %.3f (seed 1) / %.3f (seed 2), crossing %.0f vs %.0f per second; with 8 zaps %.3f; through a +12 shift it changes %d; "
                     "after she's gone %.6f; peak %.2f, finite %d\n", ra, rb, za, zb, rc, diffShift > 10.0, after, pk, finite);
        if (ra < 0.005 || rb < 0.005 || std::abs (za - zb) < 50.0 && std::abs (ra - rb) < 0.003 || rc <= ra || diffShift < 10.0
            || after > 1e-6 || pk > fp::PeakLimiter::ceiling || ! finite) FAIL_HERE();
    }

    // the master EQ drawn on the chalkboard: a bump at 1 kHz lifts 1 kHz and leaves 100 Hz alone, the
    // fitted bands follow the drawing, flat again is bit-exact, and the drawing travels with the session
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 256);
        p.prepareToPlay (48000, 256);
        for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
            *p.apvts.getRawParameterValue (id) = 0.0f;
        AudioBuffer<float> buf (2, 256); MidiBuffer m;
        double ph = 0.0;
        DelayMatch dly (p.getLatencySamples());
        auto gainAt = [&] (double hz)
        {
            double sin2 = 0, sout2 = 0, maxErr = 0;
            for (int b = 0; b < (int) (1.0 * 48000 / 256); ++b)
            {
                std::vector<float> in (256);
                // quiet, so the boost stays under the leveler
                for (int i = 0; i < 256; ++i) { in[(size_t) i] = 0.01f * (float) std::sin (ph); ph += fp::kTwoPi * hz / 48000.0; buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]); }
                p.processBlock (buf, m);
                for (int i = 0; i < 256; ++i)
                {
                    const float x = dly.push (in[(size_t) i]);
                    if (b < 100) continue;
                    sin2 += x * x; sout2 += buf.getSample (0, i) * buf.getSample (0, i);
                    maxErr = jmax (maxErr, (double) std::abs (buf.getSample (0, i) - x));
                }
            }
            return std::make_pair (10.0 * std::log10 (sout2 / sin2), maxErr);
        };
        for (int i = 0; i < fp::EqCurve::N; ++i)
        {
            const double t = (double) i / (fp::EqCurve::N - 1), c = fp::EqCurve::tOf (1000.0);
            p.eq.set (i, 0.5f + (float) (18.0 / 48.0 * std::exp (-std::pow ((t - c) / 0.12, 2.0))));
        }
        p.eq.fit();
        const double at1k = gainAt (1000.0).first, at100 = gainAt (100.0).first;
        double fitErr = 0;
        for (int k = 0; k <= 40; ++k)
        {
            const double t = k / 40.0;
            fitErr = jmax (fitErr, std::abs (p.eq.actualDb (fp::EqCurve::freqAt (t)) - p.eq.dbAt ((float) t)));
        }
        FreakiePieProcessor q;
        MemoryBlock mb; p.getStateInformation (mb); q.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool travels = std::abs (q.eq.at (0.5f) - p.eq.at (0.5f)) < 1e-3f && std::abs (q.eq.bandDb[8].load() - p.eq.bandDb[8].load()) < 0.05f && ! q.eq.isFlat();
        p.eq.flatten(); p.eq.fit();
        gainAt (440.0);
        const double flatErr = gainAt (440.0).second;
        std::printf ("master eq: +18 dB bump at 1 kHz -> 1 kHz %+.1f dB, 100 Hz %+.1f dB, fit within %.2f dB, saved with the session %d, flat again max diff %g\n",
                     at1k, at100, fitErr, travels, flatErr);
        if (fitErr > 1.5 || ! travels) FAIL_HERE();
        if (std::abs (at1k - 18.0) > 1.0 || std::abs (at100) > 1.0 || flatErr != 0.0) NOT_IN_SPHERE();
    }

    // playable balloons: inflation squeak, loud pop, and a sound balloon that records, filters, freezes, pitches
    {
        FreakiePieProcessor p;
        p.setPlayConfigDetails (2, 2, 48000, 256);
        p.prepareToPlay (48000, 256);
        for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::masterFb, ids::psFeedback })
            *p.apvts.getRawParameterValue (id) = 0.0f;               // chain dry: out = in + balloons
        *p.apvts.getRawParameterValue (ids::grRandOct) = 0.0f;
        *p.apvts.getRawParameterValue (ids::grFade) = 0.2f;
        auto& fx = p.balloonFX;
        AudioBuffer<float> buf (2, 256); MidiBuffer m;
        double t = 0;
        DelayMatch dly (p.getLatencySamples());
        // run for 'sec' with a sine (or silence) in; returns rms/peak of (out - in), zero crossings of it
        auto run = [&] (double sec, double freq, float amp, double& rms, double& peak, double* zcHz = nullptr, std::function<void (double)> each = {})
        {
            double ss = 0; peak = 0; long cnt = 0; int zc = 0; float prev = 0;
            for (int b = 0; b < (int) (sec * 48000 / 256); ++b)
            {
                if (each) each (t);
                std::vector<float> in (256);
                for (int i = 0; i < 256; ++i) { in[(size_t) i] = amp * (float) std::sin (fp::kTwoPi * freq * (t + i / 48000.0)); buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]); }
                t += 256 / 48000.0;
                p.processBlock (buf, m);
                for (int i = 0; i < 256; ++i)
                {
                    const float v = buf.getSample (0, i) - dly.push (in[(size_t) i]);
                    ss += v * v; ++cnt; peak = jmax (peak, (double) std::abs (v));
                    if (prev <= 0 && v > 0) ++zc;
                    prev = v;
                }
            }
            rms = std::sqrt (ss / jmax (1L, cnt));
            if (zcHz) *zcHz = zc / sec;
        };
        double rms, peak, hz;

        // the inflation plucks play through the effect chain, on notes of whichever scale is selected
        fx.pumping = true; fx.pumpSize = 0.6f;
        run (0.6, 0, 0, rms, peak);
        std::printf ("inflation sound while pumping: rms %.3f\n", rms);
        if (rms < 0.01) FAIL_HERE();
        *p.apvts.getRawParameterValue (ids::psScale) = 10.0f;            // Minor Pentatonic
        {
            bool inPenta = true; int heard = 0;
            for (int k = 0; k < 80; ++k)
            {
                const int before = fx.pluckCount.load();
                run (0.1, 0, 0, rms, peak);
                if (fx.pluckCount.load() == before) continue;
                ++heard;
                const int pc = (((int) std::lround (fx.toneSemis.load()) % 12) + 12) % 12;
                inPenta &= (pc == 0 || pc == 3 || pc == 5 || pc == 7 || pc == 10);
            }
            std::printf ("plucks on minor pentatonic: %d plucks, all in the scale %d\n", heard, inPenta);
            if (heard < 3 || ! inPenta) FAIL_HERE();
        }
        *p.apvts.getRawParameterValue (ids::psScale) = 0.0f;

        // ...and in "input" mode the sound-balloon sampler never hears it: record silence while the pump plays
        *p.apvts.getRawParameterValue (ids::catchFrom) = 1.0f;
        fx.voice[3].size = 1.0f; fx.voice[3].mode = 1;
        run (1.0, 0, 0, rms, peak);
        fx.voice[3].mode = 2; fx.pumping = false;
        run (3.5, 0, 0, rms, peak);   // (let any bouncing pluck settle)
        run (1.0, 0, 0, rms, peak);
        std::printf ("sound balloon recorded during the tone plays back: rms %.5f (should be silence)\n", rms);
        if (rms > 1e-4) FAIL_HERE();
        fx.voice[3].mode = 0; run (0.2, 0, 0, rms, peak);

        // catching the effects: pitch shifter an octave up and fully wet, 220 Hz in.
        // "effects" mode must freeze 440 Hz, "input" mode 220 Hz.
        *p.apvts.getRawParameterValue (ids::psScale) = 15.0f;   // chromatic
        *p.apvts.getRawParameterValue (ids::psShift) = 12.0f;
        *p.apvts.getRawParameterValue (ids::psMix) = 1.0f;
        *p.apvts.getRawParameterValue (ids::grRandOct) = 0.0f;
        auto catchHz = [&] (float from)
        {
            *p.apvts.getRawParameterValue (ids::catchFrom) = from;
            auto& cv = fx.voice[4];
            cv.semis = 0.0f; cv.size = 1.0f; cv.mode = 1;
            run (1.5, 220, 0.3f, rms, peak);
            cv.mode = 2;
            *p.apvts.getRawParameterValue (ids::psMix) = 0.0f;     // let the chain go quiet, keep only the balloon
            run (1.0, 0, 0, rms, peak);
            double hzOut = 0;
            run (1.5, 0, 0, rms, peak, &hzOut);
            cv.mode = 0; run (0.3, 0, 0, rms, peak);
            *p.apvts.getRawParameterValue (ids::psMix) = 1.0f;
            return hzOut;
        };
        const double fromFx = catchHz (0.0f), fromIn = catchHz (1.0f);
        std::printf ("caught from effects: ~%.0f Hz, from input: ~%.0f Hz\n", fromFx, fromIn);
        if (std::abs (fromFx - 440) > 60 || std::abs (fromIn - 220) > 40) FAIL_HERE();
        *p.apvts.getRawParameterValue (ids::psMix) = 0.0f;
        *p.apvts.getRawParameterValue (ids::psShift) = 7.0f;
        *p.apvts.getRawParameterValue (ids::psScale) = 0.0f;
        *p.apvts.getRawParameterValue (ids::catchFrom) = 1.0f;

        // the grain controls shape the frozen balloons too: all-reverse, bell, dense, still on pitch
        *p.apvts.getRawParameterValue (ids::grReverse) = 1.0f;
        *p.apvts.getRawParameterValue (ids::grShape) = 2.0f;
        *p.apvts.getRawParameterValue (ids::grDensity) = 20.0f;
        *p.apvts.getRawParameterValue (ids::grSize) = 60.0f;
        {
            auto& cv = fx.voice[5];
            cv.semis = 0.0f; cv.size = 1.0f; cv.mode = 1;
            run (1.5, 220, 0.3f, rms, peak);
            cv.mode = 2;
            run (0.5, 0, 0, rms, peak);
            double hz2 = 0;
            run (1.5, 0, 0, rms, peak, &hz2);
            std::printf ("frozen balloon with reverse bell grains, density 20: rms %.3f, ~%.0f Hz, peak %.2f\n", rms, hz2, peak);
            if (rms < 0.02 || std::abs (hz2 - 220) > 40 || peak > 1.5) FAIL_HERE();
            cv.mode = 0; run (0.3, 0, 0, rms, peak);
        }
        for (auto* id : { ids::grReverse, ids::grShape }) *p.apvts.getRawParameterValue (id) = 0.0f;
        *p.apvts.getRawParameterValue (ids::grDensity) = 7.0f;
        *p.apvts.getRawParameterValue (ids::grSize) = 120.0f;

        // bypass: every effect fully wet, then bypassed: the input comes out untouched, and a frozen
        // balloon still plays, its grains still shaped by the grain controls
        {
            for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix }) *p.apvts.getRawParameterValue (id) = 1.0f;
            *p.apvts.getRawParameterValue (ids::psShift) = 12.0f;
            *p.apvts.getRawParameterValue (ids::fxBypass) = 1.0f;
            run (1.0, 330, 0.25f, rms, peak);
            run (0.5, 330, 0.25f, rms, peak);
            const double bypassErr = peak;
            auto& cv = fx.voice[5];
            cv.semis = 0.0f; cv.size = 1.0f; cv.mode = 1;
            run (1.5, 220, 0.3f, rms, peak);
            cv.mode = 2;
            *p.apvts.getRawParameterValue (ids::grShape) = 2.0f;
            *p.apvts.getRawParameterValue (ids::grReverse) = 1.0f;
            run (0.5, 0, 0, rms, peak);
            double hzB = 0;
            run (1.5, 0, 0, rms, peak, &hzB);
            std::printf ("effects bypassed: input through untouched (max diff %g), frozen balloon with reverse bell grains rms %.3f ~%.0f Hz\n", bypassErr, rms, hzB);
            if (rms < 0.02 || std::abs (hzB - 220) > 40) FAIL_HERE();
            if (bypassErr > 1e-4) NOT_IN_SPHERE();
            cv.mode = 0; run (0.3, 0, 0, rms, peak);
            for (auto* id : { ids::sgMix, ids::psMix, ids::grMix, ids::rvMix, ids::grShape, ids::grReverse, ids::fxBypass })
                *p.apvts.getRawParameterValue (id) = 0.0f;
            *p.apvts.getRawParameterValue (ids::psShift) = 7.0f;
            run (1.0, 0, 0, rms, peak);
        }

        // the plucks are always on now: pumping with the pop sounds off still plays notes
        *p.apvts.getRawParameterValue (ids::balloonSfx) = 0.0f;
        fx.pumping = true; fx.pumpSize = 0.5f;
        run (1.5, 0, 0, rms, peak);
        std::printf ("pumping with the pop sounds off (plucks only) rms %.3f\n", rms);
        if (! (rms > 0.005)) FAIL_HERE();
        *p.apvts.getRawParameterValue (ids::balloonSfx) = 1.0f;
        fx.pumping = false; run (4.0, 0, 0, rms, peak);
        // the plucks: slow and irregular, every note in the scale, panned around, ringing out after you let go
        {
            *p.apvts.getRawParameterValue (ids::balloonSfx) = 0.0f;     // hiss off, so we hear only the plucks
            fx.pumping = false; run (1.5, 0, 0, rms, peak);
            std::set<int> notes; bool allInScale = true;
            const int startCount = fx.pluckCount.load();
            double maxSide = 0.0, maxPeak = 0.0;
            fx.pumpSize = 0.5f; fx.pumping = true;
            for (int k = 0; k < 150; ++k)
            {
                const int before = fx.pluckCount.load();
                run (0.1, 0, 0, rms, peak, nullptr);
                maxPeak = jmax (maxPeak, peak);
                for (int i = 0; i < 256; ++i) maxSide = jmax (maxSide, (double) std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
                if (fx.pluckCount.load() != before)
                {
                    const int st = (int) std::lround (fx.toneSemis.load());
                    notes.insert (st);
                    const int pc = ((st % 12) + 12) % 12;
                    allInScale &= (pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11);
                }
            }
            const int plucks = fx.pluckCount.load() - startCount;
            const int bounced = fx.bounceCount.load();
            fx.pumping = false;
            run (3.8, 0, 0, rms, peak);   // a bouncing pluck can keep going a couple of seconds
            run (0.2, 0, 0, rms, peak);
            const double after = rms;
            std::printf ("plucks: %d in 15 s (%d bounced like a dropped ball), %d different notes, all in C major %d, left and right differ by up to %.3f, peak %.2f, silent 4 s after letting go %d\n",
                         plucks, bounced, (int) notes.size(), allInScale, maxSide, maxPeak, after < 1e-4);
            if (plucks < 8 || plucks > 40 || bounced < 1 || notes.size() < 5 || ! allInScale || maxSide < 0.01 || maxPeak > 1.0 || after > 1e-4) FAIL_HERE();
            *p.apvts.getRawParameterValue (ids::balloonSfx) = 1.0f;
        }

        // balloon sounds off: a pop makes no sound at all
        *p.apvts.getRawParameterValue (ids::balloonSfx) = 0.0f;
        run (0.3, 0, 0, rms, peak);
        fx.popSize = 1.0f; ++fx.popRequests;
        run (0.5, 0, 0, rms, peak);
        std::printf ("balloon sounds off: a pop peak %.6f\n", peak);
        if (peak > 1e-6) FAIL_HERE();
        *p.apvts.getRawParameterValue (ids::balloonSfx) = 1.0f;
        fx.pumping = false; run (0.2, 0, 0, rms, peak);

        // pop sounds fed into the effects: with a long reverb the pop rings on well after it's over;
        // straight out (the default) the reverb never hears it
        {
            *p.apvts.getRawParameterValue (ids::rvMix) = 1.0f;
            *p.apvts.getRawParameterValue (ids::rvDecay) = p.apvts.getParameterRange (ids::rvDecay).end;
            auto tailAfterPop = [&] (bool thru)
            {
                *p.apvts.getRawParameterValue (ids::popsThru) = thru ? 1.0f : 0.0f;
                run (6.0, 0, 0, rms, peak);   // let anything before die away (and the switch settle)
                fx.popSize = 1.0f; ++fx.popRequests;
                run (1.5, 0, 0, rms, peak);   // the pop itself is long gone by now
                run (0.5, 0, 0, rms, peak);
                return rms;
            };
            const double straight = tailAfterPop (false), through = tailAfterPop (true);
            *p.apvts.getRawParameterValue (ids::popsThru) = 0.0f;
            *p.apvts.getRawParameterValue (ids::rvMix) = 0.0f;
            run (6.0, 0, 0, rms, peak);
            std::printf ("pop sounds into the effects: reverb tail 1.5 s after a pop, straight out %.5f, fed in %.5f\n", straight, through);
            if (! (through > 0.002 && through > 4.0 * straight)) NOT_IN_SPHERE();
        }

        // pops: loud, never past full scale, and different every time
        std::vector<float> popA, popB;
        auto capturePop = [&] (std::vector<float>& into)
        {
            fx.popSize = 0.8f; ++fx.popRequests;
            for (int b = 0; b < 8; ++b)
            {
                buf.clear(); p.processBlock (buf, m);
                for (int i = 0; i < 256; ++i) into.push_back (buf.getSample (0, i));
            }
            run (0.6, 0, 0, rms, peak);
        };
        capturePop (popA); capturePop (popB);
        double pk = 0, xy = 0, xx = 0, yy = 0;
        for (size_t i = 0; i < popA.size(); ++i)
        {
            pk = jmax (pk, (double) std::abs (popA[i]), (double) std::abs (popB[i]));
            xy += popA[i] * popB[i]; xx += popA[i] * popA[i]; yy += popB[i] * popB[i];
        }
        const double corr = xy / std::sqrt (xx * yy + 1e-12);
        std::printf ("pops: peak %.3f, two pops correlate %.2f (different each time)\n", pk, corr);
        if (pk < 0.85 || pk > fp::PeakLimiter::ceiling) FAIL_HERE();
        if (corr > 0.8) NOT_IN_SPHERE();   // (the pops are measured against the input, which the sphere colours)   // loud, never over the limiter's ceiling

        // a 2 kHz tone into a small balloon is muffled, into a big one it comes through
        auto& v = fx.voice[0];
        v.semis = 0.0f; v.size = 0.05f; v.mode = 1;
        double small, big;
        run (0.5, 2000, 0.3f, small, peak);
        v.size = 1.0f;
        run (0.5, 2000, 0.3f, big, peak);
        std::printf ("sound balloon monitor at 2 kHz: small %.3f  big %.3f\n", small, big);
        if (! (small < big * 0.1)) NOT_IN_SPHERE();

        // capture a 220 Hz tone, freeze it, silence the input: it keeps playing
        v.mode = 0; run (0.1, 0, 0, rms, peak);
        v.size = 1.0f; v.mode = 1;
        run (1.5, 220, 0.3f, rms, peak);
        v.mode = 2;
        run (0.5, 0, 0, rms, peak);
        run (1.5, 0, 0, rms, peak, &hz);
        std::printf ("frozen balloon, input silent: rms %.3f, ~%.0f Hz\n", rms, hz);
        if (rms < 0.02 || std::abs (hz - 220) > 40) FAIL_HERE();
        v.semis = 12.0f;
        run (0.6, 0, 0, rms, peak);
        run (1.5, 0, 0, rms, peak, &hz);
        std::printf ("same balloon at +12: ~%.0f Hz\n", hz);
        if (std::abs (hz - 440) > 70) FAIL_HERE();
        v.semis = 0.0f;
        *p.apvts.getRawParameterValue (ids::grOctave) = 1.0f;
        run (0.6, 0, 0, rms, peak);
        run (1.5, 0, 0, rms, peak, &hz);
        std::printf ("same balloon with the octave balloon at +1: ~%.0f Hz\n", hz);
        if (std::abs (hz - 440) > 70) FAIL_HERE();
        *p.apvts.getRawParameterValue (ids::grOctave) = 0.0f;
        {
            // the frozen balloons' volume
            run (0.6, 0, 0, rms, peak);
            run (1.5, 0, 0, rms, peak);
            const double full = rms;
            *p.apvts.getRawParameterValue (ids::grainsLevel) = 0.5f;
            run (0.3, 0, 0, rms, peak);
            run (1.5, 0, 0, rms, peak);
            const double half = rms;
            *p.apvts.getRawParameterValue (ids::grainsLevel) = 0.0f;
            run (0.3, 0, 0, rms, peak);
            run (0.5, 0, 0, rms, peak);
            const double silent = rms;
            *p.apvts.getRawParameterValue (ids::grainsLevel) = 1.0f;
            std::printf ("frozen balloons volume: full rms %.3f, half %.3f (ratio %.2f), at zero %.5f\n", full, half, half / full, silent);
            if (std::abs (half / full - 0.5) > 0.12 || silent > 1e-4) NOT_IN_SPHERE();
        }

        // the grains back through the effects: a +12 pitch shift at full mix moves the frozen balloon up
        *p.apvts.getRawParameterValue (ids::psShift) = 12.0f;
        *p.apvts.getRawParameterValue (ids::psMix) = 1.0f;
        run (0.6, 0, 0, rms, peak);
        double hzOut, hzThru, rmsOut, rmsThru;
        run (1.5, 0, 0, rmsOut, peak, &hzOut);
        *p.apvts.getRawParameterValue (ids::grainsThru) = 1.0f;
        run (256.0 / 48000.0, 0, 0, rms, peak);
        const float thruAfterOneBlock = p.grainsThruNow();
        run (0.6, 0, 0, rms, peak);
        std::printf ("canister moved to through fx: crossfade %.2f after one block, %.2f after 0.6 s\n", thruAfterOneBlock, p.grainsThruNow());
        if (thruAfterOneBlock > 0.2f || p.grainsThruNow() < 0.98f) FAIL_HERE();
        run (1.5, 0, 0, rmsThru, peak, &hzThru);
        *p.apvts.getRawParameterValue (ids::grainsThru) = 0.0f;
        *p.apvts.getRawParameterValue (ids::psShift) = 0.0f;
        *p.apvts.getRawParameterValue (ids::psMix) = 0.3f;
        run (0.6, 0, 0, rms, peak);
        std::printf ("frozen grains with a +12 shift: straight out ~%.0f Hz (rms %.3f), through fx ~%.0f Hz (rms %.3f)\n", hzOut, rmsOut, hzThru, rmsThru);
        if (std::abs (hzOut - 220) > 40 || std::abs (hzThru - 440) > 70 || rmsThru < 0.01) FAIL_HERE();
        v.mode = 0;
        run (0.2, 0, 0, rms, peak);
        run (0.5, 0, 0, rms, peak);
        std::printf ("popped: rms %.5f, audio side idle %d\n", rms, v.audioMode.load() == 0);
        if (rms > 1e-4 || v.audioMode.load() != 0) FAIL_HERE();
    }

    // the bouquet: it settles, a balloon dragged to the floor floats back up, a thrown one stays in the window
    {
        FreakiePieProcessor p;
        p.prepareToPlay (48000, 512);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto& e = *dynamic_cast<FreakiePieEditor*> (ed.get());
        ed->setSize (FreakiePieEditor::designW, FreakiePieEditor::designH);
        FreakieTestAccess::run (e, 6.0f);

        bool allAbove = true;
        for (int i = 0; i < FreakieTestAccess::count (e); ++i)
            if (FreakieTestAccess::balloon (e, i).alive)
                allAbove &= FreakieTestAccess::balloon (e, i).pos.y < FreakieTestAccess::bow (e).y - 60.0f;
        std::printf ("bouquet settled above the bow: %d\n", allAbove);
        if (! allAbove) FAIL_HERE();

        const String base (argc > 1 ? argv[1] : "freakiepie_ui");
        auto shoot = [&] (const String& suffix)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            File out (base + suffix);
            out.deleteFile();
            FileOutputStream os (out);
            PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        };
        shoot (".png");

        // the octave balloon: a trackpad swipe and a mouse-wheel notch both move it a whole octave
        {
            auto* oct = FreakieTestAccess::byLabel (e, "octave");
            auto* prm = p.apvts.getRawParameterValue (ids::grOctave);
            const float start = prm->load();
            MouseWheelDetails swipe; swipe.deltaX = 0; swipe.deltaY = 0.05f; swipe.isReversed = false; swipe.isSmooth = true; swipe.isInertial = false;
            for (int k = 0; k < 8; ++k) oct->mouseWheelMove (MouseEvent (Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, oct, oct, Time(), {}, Time(), 1, false), swipe);
            const float afterSwipe = prm->load();
            MouseWheelDetails notch = swipe; notch.deltaY = -0.25f; notch.isSmooth = false;
            oct->mouseWheelMove (MouseEvent (Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, oct, oct, Time(), {}, Time(), 1, false), notch);
            const float afterNotch = prm->load();
            std::printf ("octave balloon: start %+.0f, after a swipe up %+.0f, after a notch down %+.0f\n", start, afterSwipe, afterNotch);
            if (! (afterSwipe == start + 1.0f && afterNotch == start)) FAIL_HERE();
        }

        // pull "shift" down to the floor and let go
        auto& b = FreakieTestAccess::balloon (e, 2);
        const float restY = b.pos.y;
        b.dragging = true; b.dragTarget = { 200.0f, 480.0f };
        FreakieTestAccess::run (e, 0.4f);
        shoot ("_dragged.png");
        b.dragging = false;
        FreakieTestAccess::run (e, 4.0f);
        const bool floated = b.pos.y < FreakieTestAccess::bow (e).y - 150.0f;   // well up off the floor again
        std::printf ("dragged-down balloon floated back up: %d (rest y %.0f, after release y %.0f)\n", floated, restY, b.pos.y);
        if (! floated) FAIL_HERE();

        // throw one hard
        auto& c = FreakieTestAccess::balloon (e, 0);
        c.vel = { 4000.0f, -3000.0f };
        bool inside = true;
        for (int k = 0; k < 120; ++k)
        {
            FreakieTestAccess::run (e, 1.0f / 60.0f);
            inside &= c.pos.x >= 0 && c.pos.x <= FreakiePieEditor::designW && c.pos.y >= 0 && c.pos.y <= FreakiePieEditor::designH;
        }
        std::printf ("thrown balloon stayed in the window: %d\n", inside);
        if (! inside) FAIL_HERE();

        // pumps: holding the pop pump for 13 s must burst a balloon (it pops somewhere in 5..12 s)
        const int popsBefore = p.balloonFX.popRequests.load();
        FreakieTestAccess::holdPopPump (e, true);
        FreakieTestAccess::run (e, 13.0f);
        const bool popped = p.balloonFX.popRequests.load() > popsBefore;
        std::printf ("pop pump burst a balloon within 13 s: %d\n", popped);
        if (! popped) FAIL_HERE();
        FreakieTestAccess::run (e, 3.0f);      // keep it inflating for the picture
        shoot ("_pumping.png");
        FreakieTestAccess::holdPopPump (e, false);

        // catch two sounds: the second at +7, they float off on the sound bow and play frozen
        const int before = FreakieTestAccess::floaters (e);
        FreakieTestAccess::startSound (e);
        FreakieTestAccess::run (e, 2.0f);
        FreakieTestAccess::releaseSound (e);
        FreakieTestAccess::setNext (e, 7.0f);
        FreakieTestAccess::startSound (e);
        FreakieTestAccess::run (e, 3.0f);
        shoot ("_catching.png");
        FreakieTestAccess::releaseSound (e);
        FreakieTestAccess::run (e, 3.0f);
        const bool caught = FreakieTestAccess::floaters (e) == before + 2
                         && p.balloonFX.voice[0].mode.load() == 2 && p.balloonFX.voice[1].mode.load() == 2
                         && std::abs (p.balloonFX.voice[1].semis.load() - 7.0f) < 0.01f;
        std::printf ("two sound balloons caught and frozen (second at +7): %d\n", caught);
        if (! caught) FAIL_HERE();

        // typing pitches into the bubble
        const float t1 = FreakieTestAccess::typePitch (e, "+5"), t2 = FreakieTestAccess::typePitch (e, "-13"),
                    t3 = FreakieTestAccess::typePitch (e, "7"), t4 = FreakieTestAccess::typePitch (e, "99"),
                    t5 = FreakieTestAccess::typePitch (e, "+");
        std::printf ("typed pitches: +5 -> %+.0f, -13 -> %+.0f, 7 -> %+.0f, 99 -> %+.0f, junk keeps %+.0f\n", t1, t2, t3, t4, t5);
        if (t1 != 5.0f || t2 != -13.0f || t3 != 7.0f || t4 != 24.0f || t5 != 24.0f) FAIL_HERE();
        FreakieTestAccess::typePitch (e, "0");

        // the pin: its tip pops a caught sound (stopping it), only pokes a control balloon, then flies home
        {
            auto* sb = FreakieTestAccess::firstSound (e);
            const int slot = sb != nullptr ? sb->slot : -1;
            const int before2 = FreakieTestAccess::floaters (e);
            if (sb != nullptr) FreakieTestAccess::pinAt (e, sb->pos, true);
            FreakieTestAccess::run (e, 0.1f);
            const bool poppedIt = sb != nullptr && FreakieTestAccess::floaters (e) == before2 - 1 && p.balloonFX.voice[slot].mode.load() == 0;
            auto& ctl = FreakieTestAccess::balloon (e, 0);
            FreakieTestAccess::pinAt (e, ctl.pos, true);
            FreakieTestAccess::run (e, 0.1f);
            const bool controlSurvived = FreakieTestAccess::count (e) == 26 + 44 && ctl.alive;   // (26: the spectral tilt joined the blue bunch)
            FreakieTestAccess::pinAt (e, {}, false);
            FreakieTestAccess::run (e, 1.5f);
            const bool home = FreakieTestAccess::pinHomeDistance (e) < 2.0f;
            std::printf ("pin: popped a caught sound %d, control balloon only poked %d, flew home %d\n", poppedIt, controlSurvived, home);
            if (! poppedIt || ! controlSurvived || ! home) FAIL_HERE();
        }

        // the gumball machine comes from the store: not on the shelf until bought, then a box with its four
        // balloons stands in the order with the rest; given back, it goes again
        {
            auto onShelf = [&e]
            {
                const auto items = FreakieTestAccess::shelfItems (e);
                return std::find (items.begin(), items.end(), (int) fp::Engine::slotStutter) != items.end();
            };
            auto aliveCount = [&e]
            {
                int n = 0;
                for (auto* b : FreakieTestAccess::stutterBalloons (e)) n += b != nullptr && b->alive && b->isVisible();
                return n;
            };
            const bool before = onShelf(); const int balloonsBefore = aliveCount();
            FreakieTestAccess::buy (e, FreakieTestAccess::stutterItem, true);
            FreakieTestAccess::run (e, 1.5f);
            const bool bought = onShelf() && *p.apvts.getRawParameterValue (ids::stOwned) > 0.5f; const int balloonsBought = aliveCount();
            shoot ("_gumball_bought.png");
            FreakieTestAccess::buy (e, FreakieTestAccess::stutterItem, false);
            FreakieTestAccess::run (e, 0.5f);
            const bool gone = ! onShelf() && aliveCount() == 0 && *p.apvts.getRawParameterValue (ids::stOwned) < 0.5f;
            std::printf ("gumball machine in the store: on the shelf before buying %d (%d balloons), bought %d (%d balloons), given back %d\n",
                         before, balloonsBefore, bought, balloonsBought, gone);
            if (before || balloonsBefore != 0 || ! bought || balloonsBought != 4 || ! gone) FAIL_HERE();
        }
        if (auto* sb = FreakieTestAccess::firstSound (e))
        {
            FreakieTestAccess::openBubble (e, sb);
            shoot ("_bubble.png");
            FreakieTestAccess::closeBubble (e);
        }
        {
            // no bubble, no plucks switch: pop sounds is a little switch at the top of the help and reset list, the same size, and a click flips it
            const auto pops = FreakieTestAccess::popsButton (e);
            const auto reset = FreakieTestAccess::resetButton (e), help = FreakieTestAccess::helpButton (e);
            const bool corner = help.getRight() > 1040.0f && reset.getBottom() < 80.0f;
            const bool placed = pops.getX() == reset.getX() && pops.getWidth() == reset.getWidth() && pops.getHeight() == reset.getHeight()
                                && pops.getY() < 6.0f && pops.getBottom() <= help.getY() + 0.5f && help.getBottom() <= reset.getY() + 0.5f;
            FreakieTestAccess::clickAt (e, pops);
            const bool flipped = *p.apvts.getRawParameterValue (ids::balloonSfx) < 0.5f;
            FreakieTestAccess::clickAt (e, pops);
            const bool back = *p.apvts.getRawParameterValue (ids::balloonSfx) > 0.5f;
            std::printf ("tokens: %d; pop sounds at the top of the help and reset list in their style %d, click flips it %d and back %d; help and reset in the top right corner %d\n",
                         FreakieTestAccess::numPills (e), placed, flipped, back, corner);
            if (FreakieTestAccess::numPills (e) != 8 || ! placed || ! flipped || ! back || ! corner) FAIL_HERE();
        }
        shoot ("_tokens1.png");
        {
            auto setP = [&] (const char* id, float real)
            {
                auto* prm = p.apvts.getParameter (id);
                prm->setValueNotifyingHost (prm->convertTo0to1 (real));
            };
            setP (ids::balloonSfx, 0.0f); setP (ids::catchFrom, 1.0f); setP (ids::grainsThru, 1.0f);
            FreakieTestAccess::run (e, 0.4f);
            shoot ("_tokens2.png");
            setP (ids::balloonSfx, 1.0f); setP (ids::catchFrom, 0.0f); setP (ids::grainsThru, 0.0f);
        }
        // the menus open inside the window, in the sweet-shop style
        {
            FreakieTestAccess::showPresetMenu (e);
            FreakieTestAccess::run (e, 0.2f);
            shoot ("_preset_menu.png");
            PopupMenu::dismissAllActiveMenus();
        }
        // the store is shelves of balloons now: it opens under the sign, a click on a balloon buys it,
        // a click on it again pops it, and a click anywhere else puts the store away
        {
            FreakieTestAccess::showStore (e);
            FreakieTestAccess::run (e, 0.5f);
            auto& sh = FreakieTestAccess::shelves (e);
            const bool opened = sh.isVisible() && sh.isOpen();
            const int fx = 2;
            const bool before = FreakieTestAccess::bought (e, fx);
            auto clickTile = [&] (int i) { const auto c = sh.tileArea (i).getCentre(); const MouseEvent me (Desktop::getInstance().getMainMouseSource(), c, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &sh, &sh, Time(), c, Time(), 1, false); sh.mouseMove (me); sh.mouseDown (me); };
            clickTile (fx);
            FreakieTestAccess::run (e, 0.3f);
            const bool boughtIt = FreakieTestAccess::bought (e, fx) != before;
            shoot ("_store_shelves.png");
            clickTile (fx);
            FreakieTestAccess::run (e, 0.3f);
            const bool poppedIt = FreakieTestAccess::bought (e, fx) == before;
            FreakieTestAccess::mouse (e, { 300.0f, 300.0f }, 0); FreakieTestAccess::mouse (e, { 300.0f, 300.0f }, 2);
            FreakieTestAccess::run (e, 0.6f);
            std::printf ("store shelves: opened %d, click bought %d, click again popped %d, click away closed %d\n", opened, boughtIt, poppedIt, ! sh.isVisible());
            if (! opened || ! boughtIt || ! poppedIt || sh.isVisible()) FAIL_HERE();
        }
        // the order list on the left: hidden until the cursor comes near; drag a pill to move that effect
        {
            p.setShelfOrder (fp::Engine::defaultOrder());
            FreakieTestAccess::relayout (e);
            FreakieTestAccess::run (e, 0.2f);
            auto& list = FreakieTestAccess::orderList (e);
            const bool hidden = list.getRight() <= 0;
            FreakieTestAccess::showOrder (e);
            const bool shown = list.getX() >= 0;
            const auto before = FreakieTestAccess::shelfItems (e);
            const auto chips = list.layout();
            // drag the first (smear) down onto the third's place
            FreakieTestAccess::drag (list, chips[0].r.getCentre(), chips[2].r.getCentre());
            const auto after = FreakieTestAccess::shelfItems (e);
            const bool moved = after.size() == before.size() && after[2] == before[0] && after[0] == before[1] && after[1] == before[2];
            FreakieTestAccess::showOrder (e);
            FreakieTestAccess::run (e, 0.05f);
            FreakieTestAccess::showOrder (e);
            shoot ("_order_list.png");
            // and back up to the top
            const auto chips2 = list.layout();
            FreakieTestAccess::drag (list, chips2[2].r.getCentre(), chips2[0].r.getCentre() - Point<float> (0.0f, 6.0f));
            const bool back = FreakieTestAccess::shelfItems (e) == before;
            FreakieTestAccess::showOrder (e);
            FreakieTestAccess::roll (e);
            const bool rolled = FreakieTestAccess::shelfItems (e) != before;
            FreakieTestAccess::asTheyWere (e);
            std::printf ("order list: hidden at first %d, slides in %d, dragging smear to third moved it %d, dragging it back %d, the die rolled a new order %d\n",
                         hidden, shown, moved, back, rolled);
            if (! hidden || ! shown || ! moved || ! back || ! rolled) FAIL_HERE();
        }

        // a sound balloon only goes when you pop it: hold the pump for 15 s, it's still there, let go and it floats off
        {
            const int before = FreakieTestAccess::floaters (e);
            FreakieTestAccess::startSound (e);
            FreakieTestAccess::run (e, 15.0f);
            const bool stillThere = FreakieTestAccess::soundPumpAlive (e);
            FreakieTestAccess::releaseSound (e);
            FreakieTestAccess::run (e, 1.0f);
            const bool caught = FreakieTestAccess::floaters (e) == before + 1;
            FreakieTestAccess::run (e, 20.0f);
            const bool kept = FreakieTestAccess::floaters (e) == before + 1;
            std::printf ("sound pump held 15 s: still filling %d, caught %d, still there 20 s later %d\n", stillThere, caught, kept);
            if (! stillThere || ! caught || ! kept) FAIL_HERE();
        }

        // the balloon store: buy two effects, their balloons come out of the shop tied to their own gift boxes
        {
            FreakieTestAccess::buy (e, fp::StoreFX::reverb, true);
            FreakieTestAccess::buy (e, fp::StoreFX::harmonizer, true);
            FreakieTestAccess::run (e, 6.0f);
            auto held = FreakieTestAccess::storeBalloonsAlive (e);
            bool tethered = FreakieTestAccess::boxShown (e, fp::StoreFX::reverb) && FreakieTestAccess::boxShown (e, fp::StoreFX::harmonizer)
                         && ! FreakieTestAccess::boxShown (e, fp::StoreFX::delay)
                         && FreakieTestAccess::boxOf (e, fp::StoreFX::reverb).getDistanceFrom (FreakieTestAccess::boxOf (e, fp::StoreFX::harmonizer)) > 40.0f
                         && FreakieTestAccess::onRing (e, FreakieTestAccess::boxAnchor (fp::StoreFX::reverb));   // standing on the ring, apart
            for (auto* b : held)
            {
                const int fx = b->label.startsWith ("reverb") ? fp::StoreFX::reverb : fp::StoreFX::harmonizer;
                tethered &= b->anchor == FreakieTestAccess::boxAnchor (fx) && FreakieTestAccess::tied (e, *b);
            }
            const bool ownedOn = *p.apvts.getRawParameterValue (ids::store (fp::StoreFX::reverb, ids::storeOwned)) > 0.5f;
            shoot ("_store.png");
            FreakieTestAccess::dumpLayout (e, File::getCurrentWorkingDirectory().getChildFile (base + "_store_layout.json"));
            // the pin pops the reverb's balloons (and the reverb goes back on the shelf)
            Balloon* target = nullptr;
            for (auto* bl : held) if (bl->label.startsWith ("reverb")) target = bl;
            if (target != nullptr)
            {
                // (until it pops: in the sphere the pin pokes whichever balloon is nearest you under its tip, so
                // once the reverb's gone, holding it there would pop what was behind)
                auto* own = p.apvts.getRawParameterValue (ids::store (fp::StoreFX::reverb, ids::storeOwned));
                for (int k = 0; k < 6 && own->load() > 0.5f; ++k) { FreakieTestAccess::pinAt (e, target->pos, true); FreakieTestAccess::run (e, 1.0f / 60.0f); }
                FreakieTestAccess::pinAt (e, {}, false);
                FreakieTestAccess::run (e, 0.5f);
            }
            const bool pinned = target != nullptr && *p.apvts.getRawParameterValue (ids::store (fp::StoreFX::reverb, ids::storeOwned)) < 0.5f;
            const int afterReturn = (int) FreakieTestAccess::storeBalloonsAlive (e).size();
            std::printf ("store: the pin popped the reverb %d\n", pinned);
            if (! pinned) FAIL_HERE();
            std::printf ("store: bought 2 -> %d balloons on their own gift boxes (tethered %d, param %d), popped one -> %d\n",
                         (int) held.size(), tethered, ownedOn, afterReturn);
            if (held.size() != 8 || ! tethered || ! ownedOn || afterReturn != 4) FAIL_HERE();
        }
        // the canister: drag it left -> grains through the effects; a plain click still toggles helium
        {
            const auto t0 = FreakieTestAccess::tank (e).getCentre();
            const float heliumBefore = *p.apvts.getRawParameterValue (ids::helium);
            FreakieTestAccess::mouse (e, t0, 0);
            for (int k = 1; k <= 10; ++k) FreakieTestAccess::mouse (e, t0.translated (-15.0f * (float) k, 0.0f), 1);
            FreakieTestAccess::mouse (e, t0.translated (-150.0f, 0.0f), 2);
            const float thru = *p.apvts.getRawParameterValue (ids::grainsThru);
            const bool heliumSame = *p.apvts.getRawParameterValue (ids::helium) == heliumBefore;
            FreakieTestAccess::run (e, 0.3f);
            shoot ("_canister.png");
            const auto t1 = FreakieTestAccess::tank (e).getCentre();
            FreakieTestAccess::mouse (e, t1, 0);
            FreakieTestAccess::mouse (e, t1, 2);
            const bool heliumFlipped = *p.apvts.getRawParameterValue (ids::helium) != heliumBefore;
            FreakieTestAccess::mouse (e, t1, 0); FreakieTestAccess::mouse (e, t1, 2);
            std::printf ("canister: dragged left -> through fx %.2f, helium untouched %d; click -> helium toggled %d\n", thru, heliumSame, heliumFlipped);
            if (thru < 0.99f || ! heliumSame || ! heliumFlipped) FAIL_HERE();
            *p.apvts.getRawParameterValue (ids::grainsThru) = 0.0f;
        }
        FreakieTestAccess::asleepFor (e, 0.0);   // she's awake (asleep, she doesn't hold her umbrella)
        FreakieTestAccess::rain (e, true);
        FreakieTestAccess::run (e, 4.0f);
        {
            int bounced = 0;
            for (int k = 0; k < 80; ++k) { FreakieTestAccess::run (e, 0.05f); bounced = jmax (bounced, FreakieTestAccess::bouncedDrops (e)); }   // (drops land at random: give them 4 s)
            std::printf ("rain bouncing off Pinkie's umbrella: up to %d drops in the air\n", bounced);
            if (bounced < 1) FAIL_HERE();
        }
        shoot ("_storm.png");

        // the store menu, drawn by its look-and-feel into a picture (a real pop-up can't be captured here)
        {
            StoreMenuLook look;
            look.time = 0.3f;
            const int w = 320, rowH = 34;
            Image img (Image::ARGB, w, 44 + 10 * rowH + 12 + rowH + 20, true);
            Graphics g (img);
            look.drawPopupMenuBackground (g, img.getWidth(), img.getHeight());
            look.drawPopupMenuSectionHeader (g, { 0, 14, w, 40 }, "Balloon Store");
            int y = 58;
            for (int k = 0; k < fp::StoreFX::kNum; ++k, y += rowH)
            {
                const auto& in = fp::StoreFX::infos()[(size_t) k];
                const bool owned = k == 0 || k == 6;
                const Colour c = balloonColours (k % numHues).fill;
                look.drawPopupMenuItem (g, { 10, y, w - 20, rowH }, false, true, k == 3, owned, false, in.name,
                                        owned ? String ("yours! tap to pop") : String (in.c1.name) + " + " + in.c2.name, nullptr, &c);
            }
            look.drawPopupMenuItem (g, { 10, y, w - 20, 12 }, true, true, false, false, false, {}, {}, nullptr, nullptr);
            const Colour pink (0xffd9578e);
            look.drawPopupMenuItem (g, { 10, y + 12, w - 20, rowH }, false, true, false, false, false, "pop everything you bought", {}, nullptr, &pink);
            PNGImageFormat png; FileOutputStream os (File::getCurrentWorkingDirectory().getChildFile (base + "_store_menu.png"));
            os.setPosition (0); os.truncate(); png.writeImageToStream (img, os);
        }

        // Pinkie's moods: a pop scares her for a moment; left alone, she falls asleep
        {
            FreakieTestAccess::rain (e, false);
            FreakieTestAccess::run (e, 2.0f);
            e.pop ({ 500.0f, 300.0f }, 0.6f, 0);
            FreakieTestAccess::run (e, 0.3f);
            const float sc = FreakieTestAccess::scared (e);
            shoot ("_scared.png");
            FreakieTestAccess::run (e, 2.5f);
            const float calm = FreakieTestAccess::scared (e);
            FreakieTestAccess::asleepFor (e, 60.0);
            FreakieTestAccess::run (e, 3.0f);
            const float zz = FreakieTestAccess::sleepy (e);
            shoot ("_asleep.png");
            std::printf ("pinkie: scared by a pop %.2f, calm again %.2f, asleep when left alone %.2f\n", sc, calm, zz);
            if (sc < 0.8f || calm > 0.1f || zz < 0.8f) FAIL_HERE();
        }

        // helium turns her to crystal (clicking the canister), and back
        {
            FreakieTestAccess::run (e, 0.5f);   // wake her
            const auto tc = FreakieTestAccess::tank (e).getCentre();
            FreakieTestAccess::mouse (e, tc, 0); FreakieTestAccess::mouse (e, tc, 2);
            FreakieTestAccess::run (e, 1.5f);
            const float on = FreakieTestAccess::crystal (e);
            shoot ("_crystal.png");
            FreakieTestAccess::mouse (e, tc, 0); FreakieTestAccess::mouse (e, tc, 2);
            FreakieTestAccess::run (e, 1.5f);
            const float off = FreakieTestAccess::crystal (e);
            std::printf ("crystal pinkie: helium on %.2f, helium off %.2f\n", on, off);
            if (on < 0.9f || off > 0.1f) FAIL_HERE();
        }

        // the chalkboard: comes forward, takes a drawing, and the grains switch to it
        {
            FreakieTestAccess::openChalk (e);
            FreakieTestAccess::run (e, 0.6f);
            std::vector<Point<float>> stroke;
            for (int k = 0; k <= 40; ++k) { const float u = (float) k / 40.0f; stroke.push_back ({ u, u < 0.1f ? u * 10.0f : 1.0f - 0.9f * u }); }
            FreakieTestAccess::chalkStroke (e, stroke);
            FreakieTestAccess::chalkHover (e, 0.62f, 0.55f);
            FreakieTestAccess::run (e, 0.1f);
            shoot ("_chalkboard.png");
            const float early = p.chalk.at (0.12f), late = p.chalk.at (0.9f);
            const int shape = roundToInt (p.apvts.getRawParameterValue (ids::grShape)->load());
            FreakieTestAccess::chalkAway (e);   // put it back
            FreakieTestAccess::run (e, 0.6f);
            std::printf ("chalkboard: drawn shape %.2f near the start, %.2f near the end; grains use it %d; put back %d\n",
                         early, late, shape == 6, ! FreakieTestAccess::chalkShowing (e));
            if (early < 0.8f || late > 0.3f || shape != 6 || FreakieTestAccess::chalkShowing (e)) FAIL_HERE();
        }

        // presets: the picture cross-fades in and the colours follow it
        {
            std::printf ("default preset shows background %d (13 = Ponyville)\n", FreakieTestAccess::backgroundShown (e));
            if (FreakieTestAccess::backgroundShown (e) != 13) FAIL_HERE();
            const auto before = FreakieTestAccess::themeText (e);
            int cloud = 0;
            for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "Cloudsdale Stretch") cloud = i;
            p.setCurrentProgram (cloud);
            FreakieTestAccess::run (e, 0.5f);
            const float mid = FreakieTestAccess::bgFade (e);
            shoot ("_preset_fading.png");
            FreakieTestAccess::run (e, 1.5f);
            shoot ("_preset_cloudsdale.png");
            const bool changed = FreakieTestAccess::themeText (e) != before;
            for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "Moonlight Howl") p.setCurrentProgram (i);
            FreakieTestAccess::run (e, 2.0f);
            shoot ("_preset_moonlight.png");
            for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "Rainbow Resonators") p.setCurrentProgram (i);
            FreakieTestAccess::run (e, 4.0f);
            shoot ("_preset_rainbow.png");
            std::printf ("presets: fade halfway-ish at 0.5 s (%.2f), done %.2f, colours changed %d\n", mid, FreakieTestAccess::bgFade (e), changed);
            if (mid <= 0.05f || mid >= 0.95f || FreakieTestAccess::bgFade (e) < 1.0f || ! changed) FAIL_HERE();
            p.setCurrentProgram (0);
            FreakieTestAccess::run (e, 2.0f);
        }
        FreakieTestAccess::rain (e, false);

        // cloudy: click the sun, the clouds come down; a balloon inside one wanders to new values (and
        // stops when the cloud moves on); with rain, lightning drops a store effect in, then it pops out
        {
            AudioBuffer<float> sil (2, 512); MidiBuffer mm; sil.clear();
            FreakieTestAccess::clickAt (e, FreakieTestAccess::cloudyButton (e));
            FreakieTestAccess::run (e, 3.0f);
            const bool cloudyOn = *p.apvts.getRawParameterValue (ids::cloudy) > 0.5f;
            auto* dens = FreakieTestAccess::byLabel (e, "density");
            auto* prm = p.apvts.getParameter (ids::grDensity);
            const float before = prm->getValue();
            bool drifted = false;
            float travelled = 0.0f, last = before;   // how far it wandered in all (the random targets can land near the start)
            for (int k = 0; k < 8 * 60; ++k)
            {
                FreakieTestAccess::cloudTo (e, *dens);
                FreakieTestAccess::run (e, 1.0f / 60.0f);
                drifted |= dens->isDrifting();
                travelled += std::abs (prm->getValue() - last);
                last = prm->getValue();
            }
            const float after = prm->getValue();
            shoot ("_cloudy.png");
            const auto savedClouds = FreakieTestAccess::cloudsAway (e);
            FreakieTestAccess::run (e, 0.3f);
            const bool stoppedDrifting = ! dens->isDrifting();
            FreakieTestAccess::cloudsBack (e, savedClouds);
            std::printf ("cloudy %d (clouds %.2f): density inside a cloud %.3f -> %.3f (wandered %.2f in 8 s), drifting %d, stopped once it left %d\n",
                         cloudyOn, FreakieTestAccess::cloudVis (e), before, after, travelled, drifted, stoppedDrifting);
            if (! cloudyOn || travelled < 0.08f || travelled > 0.6f || ! drifted || ! stoppedDrifting) FAIL_HERE();

            // the tokens on the right wander in the clouds too: distance (a knob) drifts, flight (a choice) steps
            {
                auto* dist = p.apvts.getParameter (ids::spread);
                auto* fl = p.apvts.getParameter (ids::flight);
                auto* dop = p.apvts.getParameter (ids::doppler);
                float tr = 0.0f, lastD = dist->getValue();
                const float fl0 = fl->getValue(), dop0 = dop->getValue();
                bool flStepped = false;
                for (int k = 0; k < 14 * 60; ++k)
                {
                    FreakieTestAccess::run (e, 1.0f / 60.0f);
                    tr += std::abs (dist->getValue() - lastD); lastD = dist->getValue();
                    flStepped |= std::abs (fl->getValue() - fl0) > 1.0e-4f;
                }
                const bool dopStill = dop->getValue() == dop0;
                std::printf ("cloudy tokens: distance wandered %.2f in 14 s, flight stepped %d, doppler switch left alone %d, %d tokens\n",
                             tr, flStepped, dopStill, FreakieTestAccess::numPills (e));
                if (tr < 0.08f || ! flStepped || ! dopStill) FAIL_HERE();
                fl->setValueNotifyingHost (fl0);
            }

            p.apvts.getParameter (ids::rain)->setValueNotifyingHost (1.0f);
            const size_t baseline = FreakieTestAccess::storeBalloonsAlive (e).size();
            size_t most = baseline; bool shotStrike = false;
            float uiDue = 0.0f;   // the editor's 60 Hz timer, kept in step with the audio
            auto tickUi = [&] { uiDue += 512.0f / 48000.0f; while (uiDue >= 1.0f / 60.0f) { uiDue -= 1.0f / 60.0f; FreakieTestAccess::run (e, 1.0f / 60.0f); } };
            for (int k = 0; k < (int) (16.0 * 48000 / 512); ++k)
            {
                p.processBlock (sil, mm);
                tickUi();
                most = jmax (most, FreakieTestAccess::storeBalloonsAlive (e).size());
                if (! shotStrike && FreakieTestAccess::flash (e) > 0.8f) { FreakieTestAccess::run (e, 0.05f); shoot ("_lightning.png"); shotStrike = true; }
            }
            p.apvts.getParameter (ids::rain)->setValueNotifyingHost (0.0f);
            FreakieTestAccess::clickAt (e, FreakieTestAccess::cloudyButton (e));
            for (int k = 0; k < (int) (92.0 * 48000 / 512); ++k) { p.processBlock (sil, mm); tickUi(); }   // lent for 45 to 90 s
            const size_t end = FreakieTestAccess::storeBalloonsAlive (e).size();
            std::printf ("lightning: store balloons %d -> up to %d, back to %d after the storm (lent 0x%03x), flash seen %d\n",
                         (int) baseline, (int) most, (int) end, (unsigned) FreakieTestAccess::lent (e), shotStrike);
            if (most < baseline + 4 || end != baseline || FreakieTestAccess::lent (e) != 0 || ! shotStrike) FAIL_HERE();

            // the sonic rainboom: click the little cloud; a dive, the boom, the ring, the rainbow left behind,
            // and the balloons knocked back by the shockwave
            {
                AudioBuffer<float> tone (2, 512);
                double ph = 0.0;
                float peak = 0.0f; bool finite = true;
                auto play = [&] (double seconds)
                {
                    for (int k = 0; k < (int) (seconds * 48000 / 512); ++k)
                    {
                        for (int i = 0; i < 512; ++i) { const float v = 0.15f * (float) std::sin (ph); ph += fp::kTwoPi * 330.0 / 48000.0; tone.setSample (0, i, v); tone.setSample (1, i, v); }
                        p.processBlock (tone, mm);
                        peak = jmax (peak, tone.getMagnitude (0, 512));
                        for (int i = 0; i < 512; ++i) finite &= std::isfinite (tone.getSample (0, i));
                        tickUi();
                    }
                };
                play (1.0);
                const int booms0 = p.rainboomBooms.load(), dives0 = p.rainboomDives.load();
                FreakieTestAccess::clickAt (e, FreakieTestAccess::rainboomButton (e));
                play (0.45);
                const bool diving = p.rainboomDives.load() == dives0 + 1 && FreakieTestAccess::rainboomShowing (e);
                shoot ("_rainboom_dive.png");
                std::vector<fp::ambi::Vec3> before;
                for (int i = 0; i < FreakieTestAccess::count (e); ++i) before.push_back (FreakieTestAccess::balloon (e, i).w);
                int guard = 0;
                while (p.rainboomBooms.load() == booms0 && ++guard < 200) play (512.0 / 48000.0);
                play (0.1);
                shoot ("_rainboom_boom.png");
                play (0.45);
                shoot ("_rainboom_ring.png");
                float pushed = 0.0f; int counted = 0;
                const auto at = FreakieTestAccess::boomAt (e);   // (in the sphere, px)
                for (int i = 0; i < (int) before.size(); ++i)
                    if (FreakieTestAccess::balloon (e, i).alive && (before[(size_t) i] - at).length() < 300.0f)
                    {
                        pushed += (FreakieTestAccess::balloon (e, i).w - at).length() - (before[(size_t) i] - at).length();
                        ++counted;
                    }
                pushed /= (float) jmax (1, counted);
                play (2.0);
                shoot ("_rainboom_rainbow.png");
                play (6.0);
                const bool gone = ! FreakieTestAccess::rainboomShowing (e);
                shoot ("_rainboom_end.png");
                std::printf ("rainboom: dived %d, boomed %d, balloons near it pushed out %.0f px on average (%d), the layer gone after %d, peak %.1f dBFS, finite %d\n",
                             diving, p.rainboomBooms.load() - booms0, pushed, counted, gone, 20.0 * std::log10 (jmax (1e-9f, peak)), finite);
                if (! diving || p.rainboomBooms.load() != booms0 + 1 || pushed < 10.0f || ! gone || peak > 0.95f || ! finite) FAIL_HERE();

                // what else sets one off: a throw, a hit in the sound, the rain stopping
                const int b1 = p.rainboomBooms.load();
                FreakieTestAccess::throwBalloon (e, FreakieTestAccess::balloon (e, 0), { 2600.0f, -1800.0f });
                play (1.0);
                const bool byThrow = p.rainboomBooms.load() == b1 + 1;
                p.apvts.getParameter (ids::rbHits)->setValueNotifyingHost (1.0f);
                const int b2 = p.rainboomBooms.load();
                play (2.0);   // (a steady tone: no hits)
                const bool steadyQuiet = p.rainboomBooms.load() == b2;
                for (int k = 0; k < 2; ++k)
                {
                    for (int i = 0; i < 2 * 48000 / 512; ++i) { sil.clear(); p.processBlock (sil, mm); tickUi(); }   // (cleared each time: it's processed in place)
                    AudioBuffer<float> hit (2, 512);
                    for (int i = 0; i < 512; ++i) { const float v = 0.6f * std::exp (-(float) i / 80.0f) * (float) std::sin (i * 0.3); hit.setSample (0, i, v); hit.setSample (1, i, v); }
                    p.processBlock (hit, mm); tickUi();
                }
                for (int i = 0; i < 48000 / 512; ++i) { sil.clear(); p.processBlock (sil, mm); tickUi(); }
                const int byHits = p.rainboomBooms.load() - b2;
                p.apvts.getParameter (ids::rbHits)->setValueNotifyingHost (0.0f);
                p.apvts.getParameter (ids::rbStorms)->setValueNotifyingHost (1.0f);
                p.apvts.getParameter (ids::rain)->setValueNotifyingHost (1.0f);
                play (0.5);
                const int b3 = p.rainboomBooms.load();
                p.apvts.getParameter (ids::rain)->setValueNotifyingHost (0.0f);
                play (1.5);
                const bool byStorm = p.rainboomBooms.load() == b3 + 1;
                p.apvts.getParameter (ids::rbStorms)->setValueNotifyingHost (0.0f);
                std::printf ("rainboom triggers: a throw %d, a steady tone sets none off %d, two hits -> %d booms, the rain stopping %d\n",
                             byThrow, steadyQuiet, byHits, byStorm);
                if (! byThrow || ! steadyQuiet || byHits != 2 || ! byStorm) FAIL_HERE();
                play (8.0);
            }

            // the bypass switch on the info card (with the card out)
            FreakieTestAccess::showCard (e, true);
            FreakieTestAccess::clickAt (e, FreakieTestAccess::bypassButton (e));
            FreakieTestAccess::run (e, 2.0f);
            const bool byp = *p.apvts.getRawParameterValue (ids::fxBypass) > 0.5f;
            shoot ("_bypass.png");
            FreakieTestAccess::showCard (e, true);
            FreakieTestAccess::clickAt (e, FreakieTestAccess::bypassButton (e));
            FreakieTestAccess::run (e, 0.5f);
            std::printf ("bypass switch: on %d, off again %d\n", byp, *p.apvts.getRawParameterValue (ids::fxBypass) < 0.5f);
            if (! byp || *p.apvts.getRawParameterValue (ids::fxBypass) > 0.5f) FAIL_HERE();
        }

        // Trixie: she struts in, zaps balloons (each jumps to a new value), shuffles everything she may touch,
        // and leaves the bypass, the snow and the store alone; then the reset button brings it all back
        {
            std::map<String, float> before;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm)) before[rp->getParameterID()] = rp->getValue();
            FreakieTestAccess::trixie (e);
            FreakieTestAccess::trixieNoisy (e);   // (make sure this visit plays its noise, to see the notes)
            FreakieTestAccess::trixieGifts (e, 2); // (and brings two effects from the store)
            FreakieTestAccess::run (e, 2.5f);
            shoot ("_trixie.png");
            FreakieTestAccess::run (e, 2.62f);
            shoot ("_trixie_finale.png");
            FreakieTestAccess::run (e, 2.5f);
            int changed = 0, touchedForbidden = 0, added = 0;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                {
                    const bool moved = std::abs (rp->getValue() - before[rp->getParameterID()]) > 1e-4f;
                    if (rp->getParameterID().endsWith ("Owned")) added += (rp->getValue() > 0.5f && before[rp->getParameterID()] < 0.5f) ? 1 : 0;
                    else if (FreakiePieProcessor::trixieMayTouch (rp->getParameterID())) changed += moved ? 1 : 0;
                    else if (rp->getParameterID() != ids::grainsLevel) touchedForbidden += moved ? 1 : 0;   // (a cloud she brought may drift the frozen volume)
                }
            const bool gone = ! FreakieTestAccess::trixieHere (e);
            const int boxes = (int) FreakieTestAccess::storeBalloonsAlive (e).size() / 4;
            std::printf ("trixie: %d zaps, changed %d parameters, added %d store effects (%d boxes on the shelf), touched %d she shouldn't, gone again %d\n",
                         FreakieTestAccess::trixieZaps (e), changed, added, boxes, touchedForbidden, gone);
            if (FreakieTestAccess::trixieZaps (e) < 10 || changed < 40 || added != 2 || boxes < 2 || touchedForbidden != 0 || ! gone) FAIL_HERE();

            // the reset button
            for (int i = 0; i < fp::EqCurve::N; ++i) p.eq.set (i, 0.8f);
            p.eq.fit();
            FreakieTestAccess::clickAt (e, FreakieTestAccess::resetButton (e));
            FreakieTestAccess::run (e, 1.0f);
            FreakiePieProcessor fresh;
            fresh.setCurrentProgram (0);
            int differ = 0;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                    if (auto* fp2 = fresh.apvts.getParameter (rp->getParameterID()))
                        differ += std::abs (rp->getValue() - fp2->getValue()) > 1e-4f ? 1 : 0;
            const bool flat = p.eq.isFlat(), ordered = p.getShelfOrder() == fp::Engine::defaultOrder();
            shoot ("_reset.png");
            std::printf ("reset: %d parameters away from the first preset, EQ flat %d, shelf in its first order %d\n", differ, flat, ordered);
            if (differ != 0 || ! flat || ! ordered) FAIL_HERE();
        }

        // the order: the bows can't be dragged out of place any more (they stand on the grass);
        // the die in the order row rolls a new order instead
        {
            const auto before = p.getShelfOrder();
            const auto from = FreakieTestAccess::anchorPos (e, 4);
            FreakieTestAccess::mouse (e, from, 0);
            for (int k = 1; k <= 12; ++k)
            {
                FreakieTestAccess::mouse (e, from + (Point<float> (150.0f, from.y - 20.0f) - from) * ((float) k / 12.0f), 1);
                FreakieTestAccess::run (e, 1.0f / 60.0f);
            }
            FreakieTestAccess::mouse (e, Point<float> (150.0f, from.y - 20.0f), 2);
            FreakieTestAccess::run (e, 1.5f);
            const bool same = p.getShelfOrder() == before;
            const bool back = FreakieTestAccess::onRing (e, 4);
            FreakieTestAccess::showOrder (e);
            FreakieTestAccess::clickAt (e, FreakieTestAccess::orderDie (e));
            FreakieTestAccess::run (e, 1.5f);
            const bool rolled = p.getShelfOrder() != before;
            shoot ("_order.png");
            std::printf ("order: dragging a bow leaves the order alone %d and it stands back on the grass %d; the die rolled a new order %d\n", same, back, rolled);
            if (! same || ! back || ! rolled) FAIL_HERE();
            FreakieTestAccess::asTheyWere (e);
            FreakieTestAccess::run (e, 1.0f);
        }

        // the weather row across the top: each switch turns its weather on with a click and off with another
        {
            const char* idsOf[] = { ids::cloudy, ids::rain, ids::snow, ids::heat, ids::autoWeather };
            const Rectangle<float> rects[] = { FreakieTestAccess::cloudyButton (e), FreakieTestAccess::rainButton (e), FreakieTestAccess::snowButton (e),
                                               FreakieTestAccess::heatButton (e), FreakieTestAccess::autoButton (e) };
            int worked = 0;
            for (int k = 0; k < 5; ++k)
            {
                const bool was = *p.apvts.getRawParameterValue (idsOf[k]) > 0.5f;
                FreakieTestAccess::clickAt (e, rects[k]);
                const bool on = *p.apvts.getRawParameterValue (idsOf[k]) > 0.5f;
                if (k == 2) { FreakieTestAccess::run (e, 0.3f); shoot ("_weather_row.png"); }
                FreakieTestAccess::clickAt (e, rects[k]);
                const bool off = *p.apvts.getRawParameterValue (idsOf[k]) > 0.5f;
                worked += (on != was && off == was) ? 1 : 0;
            }
            // and a balloon floating over a switch doesn't get in the way: the row is above them
            FreakieTestAccess::run (e, 0.1f);
            auto& room = FreakieTestAccess::content (e);
            const int topIndex = room.getNumChildComponents() - 1, rowIndex = room.getIndexOfChildComponent (&FreakieTestAccess::weatherRow (e));
            std::printf ("weather row: %d of 5 switches turn on and off with a click; it sits on top of the balloons %d\n", worked, rowIndex >= topIndex - 2);
            if (worked != 5 || rowIndex < topIndex - 2) FAIL_HERE();
        }

        // the presets' filmstrip: a click on the name opens it, a click on a picture changes the preset and
        // puts it away, and it opens again every time after that
        {
            const int start = p.getCurrentProgram();
            int opens = 0, picks = 0, closes = 0;
            for (int round = 0; round < 3; ++round)
            {
                FreakieTestAccess::press (FreakieTestAccess::presetBox (e), { 20.0f, 10.0f });
                FreakieTestAccess::run (e, 0.5f);
                auto& st = FreakieTestAccess::strip (e);
                opens += (st.isOpen() && st.isVisible() && st.getY() < FreakiePieEditor::designH - 50) ? 1 : 0;
                if (round == 0) shoot ("_presets.png");
                const int want = 2 + round * 3;
                st.scrollTo (want);
                FreakieTestAccess::run (e, 0.6f);
                FreakieTestAccess::press (st, st.frameArea (want).getCentre());
                FreakieTestAccess::run (e, 0.6f);
                picks += p.getCurrentProgram() == want ? 1 : 0;
                closes += ! st.isVisible() ? 1 : 0;
            }
            std::printf ("preset filmstrip: opened %d of 3 times, picked the preset %d of 3, put itself away %d of 3\n", opens, picks, closes);
            if (opens != 3 || picks != 3 || closes != 3) FAIL_HERE();
            p.setCurrentProgram (start);
            p.setShelfOrder (fp::Engine::defaultOrder());
            FreakieTestAccess::relayout (e);
            FreakieTestAccess::run (e, 1.5f);
        }

        // snow: the snowflake turns it on, flakes fall and pile up, the balloons ice over as the sound freezes
        {
            AudioBuffer<float> sil (2, 512); MidiBuffer mm; sil.clear();
            FreakieTestAccess::clickAt (e, FreakieTestAccess::snowButton (e));
            const bool on = *p.apvts.getRawParameterValue (ids::snow) > 0.5f;
            float uiDue = 0.0f;
            for (int k = 0; k < (int) (6.0 * 48000 / 512); ++k)
            {
                p.processBlock (sil, mm);
                uiDue += 512.0f / 48000.0f;
                while (uiDue >= 1.0f / 60.0f) { uiDue -= 1.0f / 60.0f; FreakieTestAccess::run (e, 1.0f / 60.0f); }
            }
            const float frost = FreakieTestAccess::balloon (e, 0).frost;
            const int flakes = FreakieTestAccess::flakes (e);
            shoot ("_snow.png");
            // left out in the snow a minute more (nobody touching anything), she catches a cold instead of dozing off
            const float coldEarly = FreakieTestAccess::coldVis (e);
            for (int k = 0; k < (int) (62.0 * 48000 / 512); ++k)
            {
                p.processBlock (sil, mm);
                uiDue += 512.0f / 48000.0f;
                while (uiDue >= 1.0f / 60.0f) { uiDue -= 1.0f / 60.0f; FreakieTestAccess::run (e, 1.0f / 60.0f); }
            }
            const float coldLate = FreakieTestAccess::coldVis (e), sleepLate = FreakieTestAccess::sleepVis (e);
            shoot ("_snow_cold.png");
            FreakieTestAccess::clickAt (e, FreakieTestAccess::snowButton (e));
            for (int k = 0; k < (int) (6.0 * 48000 / 512); ++k)
            {
                p.processBlock (sil, mm);
                uiDue += 512.0f / 48000.0f;
                while (uiDue >= 1.0f / 60.0f) { uiDue -= 1.0f / 60.0f; FreakieTestAccess::run (e, 1.0f / 60.0f); }
            }
            const float thawed = FreakieTestAccess::balloon (e, 0).frost;
            const float coldAfterThaw = FreakieTestAccess::coldVis (e);
            std::printf ("pinkie in the snow: cold %.2f at 6 s, %.2f (asleep %.2f) after a minute more, %.2f once the snow has gone\n", coldEarly, coldLate, sleepLate, coldAfterThaw);
            if (coldEarly > 0.01f || coldLate < 0.9f || sleepLate > 0.1f || coldAfterThaw > 0.2f) FAIL_HERE();
            std::printf ("snow in the shop: on %d, balloons iced %.2f, %d flakes in the air, thawed to %.2f after it stopped\n", on, frost, flakes, thawed);
            if (! on || frost < 0.9f || flakes < 20 || thawed > 0.05f) FAIL_HERE();
        }

        // heatwave and auto weather: their switches flip the parameters, and the shop goes warm
        {
            AudioBuffer<float> sil (2, 512); MidiBuffer mm; sil.clear();
            FreakieTestAccess::clickAt (e, FreakieTestAccess::heatButton (e));
            const bool heatOn = *p.apvts.getRawParameterValue (ids::heat) > 0.5f;
            float uiDue = 0.0f;
            for (int k = 0; k < (int) (6.0 * 48000 / 512); ++k)
            {
                p.processBlock (sil, mm);
                uiDue += 512.0f / 48000.0f;
                while (uiDue >= 1.0f / 60.0f) { uiDue -= 1.0f / 60.0f; FreakieTestAccess::run (e, 1.0f / 60.0f); }
            }
            FreakieTestAccess::clickAt (e, FreakieTestAccess::autoButton (e));
            const bool autoOn = *p.apvts.getRawParameterValue (ids::autoWeather) > 0.5f;
            FreakieTestAccess::run (e, 0.5f);
            shoot ("_heat.png");
            const float melt = FreakieTestAccess::meltVis (e);
            FreakieTestAccess::clickAt (e, FreakieTestAccess::autoButton (e));
            FreakieTestAccess::clickAt (e, FreakieTestAccess::heatButton (e));
            for (int k = 0; k < (int) (6.0 * 48000 / 512); ++k) p.processBlock (sil, mm);
            FreakieTestAccess::run (e, 3.0f);
            std::printf ("heatwave in the shop: on %d, Pinkie melted %.2f, heat in the sound %.2f after it stopped; auto weather switch %d\n",
                         heatOn, melt, p.heatAmount.load(), autoOn);
            if (! heatOn || melt < 0.9f || p.heatAmount.load() > 0.0f || ! autoOn || *p.apvts.getRawParameterValue (ids::autoWeather) > 0.5f) FAIL_HERE();
        }

        // the snow and the heatwave never together; right-click either for its fade times; a hot rain soaks Pinkie
        {
            AudioBuffer<float> sil (2, 512); MidiBuffer mm; sil.clear();
            auto on = [&] (const char* id) { return *p.apvts.getRawParameterValue (id) > 0.5f; };
            FreakieTestAccess::clickAt (e, FreakieTestAccess::snowButton (e));
            FreakieTestAccess::clickAt (e, FreakieTestAccess::heatButton (e));
            const bool heatWins = on (ids::heat) && ! on (ids::snow);
            FreakieTestAccess::clickAt (e, FreakieTestAccess::snowButton (e));
            const bool snowWins = on (ids::snow) && ! on (ids::heat);
            // both at once from outside (a preset, automation): the processor sends the older one away
            p.apvts.getParameter (ids::heat)->setValueNotifyingHost (1.0f);
            p.keepSnowAndHeatApart();
            const bool apart = on (ids::heat) && ! on (ids::snow);
            p.apvts.getParameter (ids::heat)->setValueNotifyingHost (0.0f);
            p.keepSnowAndHeatApart();

            // right-click: the popups
            auto& row = FreakieTestAccess::weatherRow (e);
            auto& fp_ = FreakieTestAccess::fades (e);
            FreakieTestAccess::rightClick (row, FreakieTestAccess::heatButton (e).getCentre() - row.getPosition().toFloat());
            FreakieTestAccess::run (e, 0.1f);
            const bool heatPopup = fp_.isVisible() && fp_.params[0] == p.apvts.getParameter (ids::heatIn);
            shoot ("_heat_fades.png");
            // drag the fade in to the far right (5 min), then double-click it back to 5 s, then to about 20 s
            const auto tr = fp_.trackArea (0);
            FreakieTestAccess::drag (fp_, tr.getCentre(), { tr.getRight() + 20.0f, tr.getCentreY() });
            const float longest = *p.apvts.getRawParameterValue (ids::heatIn);
            FreakieTestAccess::press (fp_, tr.getCentre(), 2);
            const float reset5 = *p.apvts.getRawParameterValue (ids::heatIn);
            FreakieTestAccess::rightClick (row, FreakieTestAccess::snowButton (e).getCentre() - row.getPosition().toFloat());
            const bool snowPopup = fp_.isVisible() && fp_.params[1] == p.apvts.getParameter (ids::snowOut);
            FreakieTestAccess::mouse (e, { 500.0f, 300.0f }, 0); FreakieTestAccess::mouse (e, { 500.0f, 300.0f }, 2);
            const bool closed = ! fp_.isVisible();
            // the heat's own fade in: 20 s means about a quarter of the way in after 5 s
            auto* hin = p.apvts.getParameter (ids::heatIn);
            hin->setValueNotifyingHost (hin->convertTo0to1 (20.0f));
            p.apvts.getParameter (ids::snow)->setValueNotifyingHost (0.0f);
            p.apvts.getParameter (ids::heat)->setValueNotifyingHost (1.0f);
            for (int k = 0; k < (int) (5.0 * 48000 / 512); ++k) p.processBlock (sil, mm);
            const float after5 = p.heatAmount.load();
            // rain in the heat: Pinkie lies soaked in the puddle (no umbrella)
            p.apvts.getParameter (ids::rain)->setValueNotifyingHost (1.0f);
            FreakieTestAccess::run (e, 4.0f);
            const float wet = FreakieTestAccess::wetVis (e), umb = FreakieTestAccess::umbrellaVis (e);
            shoot ("_wet.png");
            p.apvts.getParameter (ids::rain)->setValueNotifyingHost (0.0f);
            p.apvts.getParameter (ids::heat)->setValueNotifyingHost (0.0f);
            hin->setValueNotifyingHost (hin->convertTo0to1 (5.0f));
            for (int k = 0; k < (int) (6.0 * 48000 / 512); ++k) p.processBlock (sil, mm);
            FreakieTestAccess::run (e, 3.0f);
            std::printf ("snow and heat: heat sends snow away %d, snow sends heat away %d, both from outside kept apart %d\n", heatWins, snowWins, apart);
            std::printf ("fade popups: heat's on a right-click %d (dragged to %.0f s, double-click %.0f s), snow's %d, a click away closes it %d; heat 5 s into a 20 s fade in %.2f\n",
                         heatPopup, longest, reset5, snowPopup, closed, after5);
            std::printf ("hot rain: Pinkie soaked %.2f, umbrella %.2f\n", wet, umb);
            if (! heatWins || ! snowWins || ! apart || ! heatPopup || longest < 299.0f || std::abs (reset5 - 5.0f) > 0.01f || ! snowPopup || ! closed
                || std::abs (after5 - 0.25f) > 0.03f || wet < 0.9f || umb > 0.1f) FAIL_HERE();
        }

        // the card waits above the window until the cursor comes near
        {
            FreakieTestAccess::showCard (e, false);
            const bool hidden = FreakieTestAccess::card (e).getBottom() <= 0;
            FreakieTestAccess::showCard (e, true);
            const bool shown = FreakieTestAccess::card (e).getY() >= 0;
            const bool awningGone = FreakieTestAccess::storeSign (e).getHeight() < 45.0f;
            FreakieTestAccess::run (e, 0.05f);
            shoot ("_card.png");
            FreakieTestAccess::showCard (e, false);
            // the card, order list and menus sit on a layer over the whole room (so over balloons and clouds),
            // under the chalkboard and the library
            auto& ed = static_cast<Component&> (e);
            const int iRoom = ed.getIndexOfChildComponent (&FreakieTestAccess::content (e)), iOver = ed.getIndexOfChildComponent (&FreakieTestAccess::overlay (e));
            const bool layered = iOver > iRoom && FreakieTestAccess::orderList (e).getParentComponent() == &FreakieTestAccess::overlay (e)
                                 && FreakieTestAccess::fades (e).getParentComponent() == &FreakieTestAccess::overlay (e)
                                 && FreakieTestAccess::shelves (e).getParentComponent() == &FreakieTestAccess::overlay (e);
            // the chalkboard stands clear of the pin cushion (it sits 14 px left of the pin's home, 40 px wide)
            const float cushionLeft = FreakieTestAccess::pinHome (e).x - 14.0f - 20.0f;
            const float gap = cushionLeft - FreakieTestAccess::chalkRest (e).getRight();
            std::printf ("card: hidden %d, shown when near %d; store sign without its awning %d; overlays above the room %d; chalkboard %.0f px clear of the pin\n",
                         hidden, shown, awningGone, layered, gap);
            if (! hidden || ! shown || ! awningGone || ! layered || gap < 8.0f) FAIL_HERE();
        }

        // the chalkboard's master eq page: drawing on it changes the EQ; the library opens, turns pages and closes
        {
            FreakieTestAccess::openChalk (e);
            FreakieTestAccess::run (e, 0.6f);
            FreakieTestAccess::chalkPage (e, 1);
            FreakieTestAccess::chalkStroke (e, { { 0.0f, 0.5f }, { 0.2f, 0.75f }, { 0.4f, 0.55f }, { 0.6f, 0.3f }, { 0.8f, 0.5f }, { 1.0f, 0.7f } });
            FreakieTestAccess::chalkHover (e, 0.6f, 0.3f);
            const bool eqDrawn = ! p.eq.isFlat();
            const float lowBand = (float) p.eq.actualDb (fp::EqCurve::freqAt (0.2)), midBand = (float) p.eq.actualDb (fp::EqCurve::freqAt (0.6));
            FreakieTestAccess::run (e, 0.1f);
            shoot ("_eq.png");
            std::printf ("chalkboard eq: drawn %d, a fifth of the way across %+.1f dB (drawn +12), three fifths %+.1f dB (drawn -9.6)\n", eqDrawn, lowBand, midBand);
            if (! eqDrawn || std::abs (lowBand - 12.0f) > 2.5f || std::abs (midBand + 9.6f) > 2.5f) FAIL_HERE();
            FreakieTestAccess::chalkButton (e, 0);   // flat
            const bool flatAgain = p.eq.isFlat();
            FreakieTestAccess::chalkPage (e, 0);
            FreakieTestAccess::chalkAway (e);
            FreakieTestAccess::run (e, 1.0f);
            if (! flatAgain || FreakieTestAccess::chalkShowing (e)) FAIL_HERE();

            FreakieTestAccess::openHelp (e);
            FreakieTestAccess::run (e, 0.6f);
            shoot ("_library.png");
            FreakieTestAccess::helpChapter (e, 1);
            FreakieTestAccess::run (e, 0.2f);
            shoot ("_library_flip.png");
            FreakieTestAccess::run (e, 0.15f);
            shoot ("_library_flip2.png");
            FreakieTestAccess::run (e, 0.5f);
            bool allFit = true;
            for (int c = 0; c < FreakieTestAccess::helpChapters (e); ++c)
            {
                FreakieTestAccess::helpChapter (e, c);
                FreakieTestAccess::run (e, 0.7f);
                if (c == 1) shoot ("_library_colours.png");
                if (c == 9) shoot ("_library_weather.png");
            }
            FreakieTestAccess::helpChapter (e, 0);
            FreakieTestAccess::closeHelp (e);
            FreakieTestAccess::run (e, 0.6f);
            std::printf ("library: %d chapters, closed again %d\n", FreakieTestAccess::helpChapters (e), ! FreakieTestAccess::helpShowing (e));
            if (FreakieTestAccess::helpChapters (e) < 8 || FreakieTestAccess::helpShowing (e) || ! allFit) FAIL_HERE();
        }

        // weather pictures: the processor eases the filters in, the editor follows them
        AudioBuffer<float> sil (2, 512); MidiBuffer mm; sil.clear();
        auto weather = [&] (bool rain, bool helium, const String& suffix)
        {
            *p.apvts.getRawParameterValue (ids::rain) = rain ? 1.0f : 0.0f;
            *p.apvts.getRawParameterValue (ids::helium) = helium ? 1.0f : 0.0f;
            for (int k = 0; k < 400; ++k)
            {
                p.processBlock (sil, mm);
                if (k % 2 == 0) FreakieTestAccess::run (e, 1.0f / 60.0f * 10.0f / 2.0f);
            }
            shoot (suffix);
        };
        weather (true, false, "_rain.png");
        weather (false, true, "_helium.png");
        weather (true, true, "_both.png");
    }

    std::printf ("%d Balloon Shop checks not applicable in the sphere\n", skipped);
    std::printf (fails ? "HOST TEST PROBLEMS\n" : "HOST TEST OK\n");
    return fails;
}

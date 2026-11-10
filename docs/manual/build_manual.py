#!/usr/bin/env python3
"""Builds the PDF manuals for Pinkie Pie's Balloon Shop and Pinkie Pie's Balloon Palace.

The help chapters, the parameters and the presets come out of the plug-ins themselves (the host
tests dump them with MANUAL_DUMP=<folder>), so the manual can't drift from the code. Screenshots
come from the host tests too (their screenshot runs, and SPHERE_SHOTS for the Balloon Palace). The pages are
written as HTML and printed to PDF by Chrome (headless).

    python3 docs/manual/build_manual.py shop  <dump folder> <shots folder> <out.pdf>
    python3 docs/manual/build_manual.py palace <dump folder> <shots folder> <out.pdf>

The shots folder is searched for files ending in the names used below (ui_cloudy.png, ht_cloudy.png
and cloudy.png all match "cloudy").
"""
import html, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

SHOP = dict(
    key="shop", name="Pinkie Pie's Balloon Shop", short="the Balloon Shop", version="1.1.1",
    icon=os.path.join(REPO, "Resources/icon/icon_1024.png"),
    tagline="A multi-effect you play as balloons.",
    accent="#ff4f9a", accent2="#ffc4e1",
)
PALACE = dict(
    key="palace", name="Pinkie Pie's Balloon Palace", short="the Balloon Palace", version="0.2.1",
    icon=os.path.join(REPO, "BalloonPalace/Resources/icon/icon_1024.png"),
    tagline="The Balloon Shop inside a glass sphere: an Ambisonics encoder, 1st to 7th order.",
    accent="#8a6bff", accent2="#d9ccff",
)

def esc(s):
    return html.escape(s, quote=False)

def para(text):
    """Help text (blank-line paragraphs) to HTML."""
    out = []
    for p in text.split("\n\n"):
        p = p.strip()
        if p:
            out.append("<p>" + esc(p).replace("\n", "<br>") + "</p>")
    return "\n".join(out)

def tidy_number(text):
    """'97.9989 Hz' -> '98.0 Hz' (the plug-in shows the raw value for a few store controls)."""
    def fix(m):
        v = float(m.group(0))
        if abs(v) >= 100: return "%d" % round(v)
        if abs(v) >= 10:  return "%.1f" % v
        return "%.2f" % v
    return re.sub(r"\d+\.\d{3,}", fix, text)

# ------------------------------------------------------------------------------------------------
# screenshots
class Shots:
    def __init__(self, folders, tmp):
        self.files = []
        for f in folders:
            for root, _, names in os.walk(f):
                self.files += [os.path.join(root, n) for n in names if n.lower().endswith((".png", ".jpg"))]
        self.tmp = tmp

    def get(self, stem):
        for f in sorted(self.files):
            base = os.path.splitext(os.path.basename(f))[0]
            if base == stem or base.endswith("_" + stem):
                out = os.path.join(self.tmp, stem + ".jpg")
                if not os.path.exists(out):
                    subprocess.run(["sips", "-s", "format", "jpeg", "-s", "formatOptions", "84", f, "--out", out],
                                   check=True, capture_output=True)
                return out
        print("  (no screenshot for %s)" % stem)
        return None

def figure(shots, stem, caption, wide=True):
    f = shots.get(stem)
    if not f: return ""
    return ('<figure class="%s"><img src="file://%s"><figcaption>%s</figcaption></figure>'
            % ("wide" if wide else "half", f, esc(caption)))

# ------------------------------------------------------------------------------------------------
# what each chapter shows
FIGS_COMMON = {
    "Colours": [("library_colours", "The library's colour page: which balloon colour belongs to which effect.")],
    "The order": [("order_list", "The order list, pulled out from the left edge."), ("order", "The shelf after a new order.")],
    "The pop pump": [("pumping", "Blowing up a plucked note with the pink pump.")],
    "Catching sounds": [("catching", "Catching a sound with the teal pump.")],
    "The canister": [("canister", "The canister slid towards the effects: the frozen balloons go round again.")],
    "Weather": [("cloudy", "Cloudy: clouds drift through the bouquet and the balloons inside them wander."),
                ("storm", "A storm: rain and clouds together."),
                ("lightning", "Lightning lends an effect from the store."),
                ("snow", "Snow: everything frozen."),
                ("heat", "A heatwave."),
                ("heat_fades", "Right-click the snow or the heat for its fade times."),
                ("wet", "Rain in the heat: Pinkie soaked through.")],
    "Sonic rainboom": [("rainboom_dive", "The dive: Rainbow Dash speeding in, the rainbow streaming out behind her."),
                       ("rainboom_boom", "The boom: the starburst and the ring of seven colours. Pinkie jumps, amazed."),
                       ("rainboom_ring", "The ring opening out; the shockwave has knocked the balloons back."),
                       ("rainboom_rainbow", "The rainbow left in the sky, fading over the rainbow's length.")],
    "The balloon store": [("store", "The balloon store."), ("store_shelves", "Its shelves.")],
    "The gumball machine": [("gumball_bought", "The gumball machine, bought and on the shelf.")],
    "The chalkboard": [("chalkboard", "Drawing a grain shape on the chalkboard."), ("eq", "Drawing the master EQ.")],
    "The card": [("card", "The card, down from the top left corner."), ("presets", "The presets.")],
    "Pinkie": [("trixie", "Trixie drops by."), ("asleep", "Leave the shop alone for a while and Pinkie falls asleep.")],
}
FIGS = {
    "shop": dict(FIGS_COMMON, **{
        "Welcome": [("dragged", "Dragging a balloon.")],
    }),
    "palace": dict(FIGS_COMMON, **{
        "The sphere": [("sphere_turned", "The sphere turned to look at it from above."), ("sphere_zoomed", "Zoomed in: the room behind blurs and darkens.")],
        "Grains in the sphere": [("sphere_scatter_orbit", "Scatter: one copy of the effects, its granulator grains flying to every grain."),
                                 ("sphere_six_lorenz", "Six grains flying the Lorenz attractor.")],
        "Flying": [("sphere_flying_in", "A balloon flying into the sphere after it was let go."),
                   ("sphere_weather_flock", "A flock, with weather drifting through the glass.")],
        "Out": [("sphere_one_grain", "One grain: the energy map on the glass and at the top left shows where it is.")],
    }),
}

# ------------------------------------------------------------------------------------------------
# the parameters, grouped
def groups(cfg, params):
    by = {p["id"]: p for p in params}
    used = set()
    def take(ids):
        out = []
        for i in ids:
            if i in by and i not in used:
                out.append(by[i]); used.add(i)
        return out
    g = []
    if cfg["key"] == "palace":
        g.append(("The sphere", "The tokens on the right and the jar.",
                  take(["grainMode", "grains", "deviate", "flight", "flightSpeed", "spread", "doppler"])))
    g.append(("Spectral smear (blue)", "", take(["sgMix", "sgFreeze", "sgTilt"])))
    g.append(("Pitch (yellow)", "The scale is on the card.", take(["psShift", "psFeedback", "psMix", "psScale"])))
    g.append(("Grains (mint)", "These also shape the frozen sound balloons.",
              take(["grMix", "grSize", "grDensity", "grShape", "grPan", "grReverse", "grPos", "grPosRand", "grOctave", "grRandOct", "grFreeze", "grFade"])))
    g.append(("Reverb (lilac) and the loop (pink)", "", take(["rvDecay", "rvMix", "masterFb"])))
    g.append(("The gumball machine (orange)", "Bought at the balloon store.", take(["stOwned", "stAmount", "stBounce", "stSway", "stCutup"])))
    g.append(("Sound balloons, pops and the canister", "",
              take(["catchFrom", "grainsLevel", "grainsThru", "balloonSfx", "popsThru"])))
    g.append(("Weather", "", take(["cloudy", "rain", "snow", "heat", "helium", "autoWeather", "snowIn", "snowOut", "heatIn", "heatOut"])))
    g.append(("Sonic rainboom", "", take(["rbBoom", "rbLevel", "rbTail", "rbThrows", "rbHits", "rbStorms", "rbFast"])))
    g.append(("Bypass", "", take(["fxBypass"])))
    # the store's effects, one table each
    store = {}
    for p in params:
        m = re.match(r"st([A-Z][A-Za-z]*?)(Owned|A|B|Level|Width)$", p["id"])
        if m and p["id"] not in used and p["id"] != "stOwned":
            store.setdefault(m.group(1), []).append(p)
    store_tables = []
    for k, ps in store.items():
        for p in ps: used.add(p["id"])
        nm = ps[0]["name"].rsplit(" ", 1)[0] if ps else k
        store_tables.append((nm, ps))
    rest = [p for p in params if p["id"] not in used]
    if rest: g.append(("Other", "", rest))
    return g, store_tables

def param_rows(ps):
    rows = []
    for p in ps:
        if p["type"] == "choice":
            rng = esc(p.get("choices", ""))
        elif p["type"] == "switch":
            rng = "off / on"
        else:
            rng = esc(tidy_number(p["min"])) + " to " + esc(tidy_number(p["max"]))
        rows.append("<tr><td>%s</td><td class='id'>%s</td><td>%s</td><td>%s</td></tr>"
                    % (esc(p["name"]), esc(p["id"]), rng, esc(tidy_number(p["default"]))))
    return "<table class='params'><tr><th>Name in your DAW</th><th>ID</th><th>Range</th><th>Default</th></tr>%s</table>" % "".join(rows)

# ------------------------------------------------------------------------------------------------
# the window map: numbered spots on a screenshot (design coordinates, 1060 x 600)
MAP_COMMON = [
    ((560, 30), "The weather row: cloudy, rain, snow, heatwave and auto weather. Right-click the snow or the heat for their fade times."),
    ((880, 27), "The balloon store."),
    ((1003, 37), "Pop sounds, help (the library) and reset."),
    ((998, 110), "The sonic rainboom: click for one, right-click for what sets it off."),
    ((90, 520), "The pink pump: blows up plucked notes."),
    ((1000, 520), "The teal pump: catches sounds into frozen balloons. Next to it, the next note's pitch."),
    ((468, 545), "The helium tank and the canister rail along the bottom."),
    ((700, 555), "The chalkboard: grain shapes and the master EQ."),
    ((790, 545), "The pin: drag it to pop a balloon."),
    ((912, 490), "What the teal pump catches: the effects or the input."),
    ((262, 530), "Pinkie."),
    ((40, 20), "Move the cursor here and the card comes down: presets, the scale and the bypass. At the left edge, the order list slides out."),
]
MAPS = {
    "shop": ("ui", MAP_COMMON + [((500, 455), "The bows on the shelf, one per effect, in the order the sound goes through them. Each holds its bunch of balloons.")]),
    "palace": ("both", MAP_COMMON[:-1] + [
        ((545, 290), "The sphere: drag the empty glass to turn it, scroll or pinch to zoom, double-click to reset the view."),
        ((140, 200), "The energy map: where the sound is loudest, from the front."),
        ((190, 360), "The jar of grains: click for another, drag one into the sphere."),
        ((875, 235), "The tokens: flight, speed, distance, deviate, grain mode, Doppler and the Ambisonic order."),
        ((40, 20), "Move the cursor here and the card comes down: presets, the scale and the bypass. At the left edge, the order list slides out."),
    ]),
}

def window_map(cfg, shots):
    stem, spots = MAPS[cfg["key"]]
    f = shots.get(stem) or shots.get("ui") or shots.get("both")
    if not f: return ""
    pins = []
    items = []
    for i, ((x, y), text) in enumerate(spots, 1):
        pins.append("<span class='pin' style='left:%.2f%%;top:%.2f%%'>%d</span>" % (x / 10.60, y / 6.00, i))
        items.append("<li>%s</li>" % esc(text))
    return ("<div class='map'><img src='file://%s'>%s</div><ol class='maplist'>%s</ol>"
            % (f, "".join(pins), "".join(items)))

# ------------------------------------------------------------------------------------------------
# hand-written sections
def install_section(cfg):
    if cfg["key"] == "shop":
        n = "Pinkie Pies Balloon Shop"
        brew = """
<h3>macOS with Homebrew</h3>
<pre>brew tap femifleming/pinkiepiesballoonshop
brew trust femifleming/pinkiepiesballoonshop
brew install --cask pinkiepiesballoonshop</pre>
<p>Recent Homebrew asks you to trust a tap before installing from it. If you installed the shop by hand before, add <code>--force</code> to the last line so Homebrew can replace those copies. This puts the app in <code>/Applications</code> and the Audio Unit and VST3 in <code>~/Library/Audio/Plug-Ins</code>. Update with <code>brew upgrade --cask pinkiepiesballoonshop</code>.</p>"""
        zipname = "PinkiePiesBalloonShop"
        where = "the <a href='https://github.com/femifleming/PinkiePiesBalloonShop/releases/latest'>Releases page</a>"
    else:
        n = "Pinkie Pies Balloon Palace"
        brew = ""
        zipname = "PinkiePiesBalloonPalace"
        where = "the <a href='https://github.com/femifleming/PinkiePiesBalloonShop/releases'>Balloon Shop's Releases page</a> (its releases are tagged <code>balloonpalace-v...</code>)"
    return f"""
<h2 id="install">Installing</h2>
<p>Download it from {where}: <code>{zipname}-macOS.zip</code> (macOS 11 or later, Apple silicon and Intel), <code>{zipname}-Windows.zip</code> (Windows 10 or later, 64-bit) or <code>{zipname}-Linux.tar.gz</code> (x86-64). macOS gets an Audio Unit, a VST3 and a standalone app. Windows gets a VST3 and a standalone app, and Linux adds an LV2.</p>
{brew}
<h3>macOS by hand</h3>
<ol>
<li>Unzip <code>{zipname}-macOS.zip</code>.</li>
<li>Copy <code>{n}.component</code> to <code>~/Library/Audio/Plug-Ins/Components</code> and <code>{n}.vst3</code> to <code>~/Library/Audio/Plug-Ins/VST3</code>. Put the app anywhere you like.</li>
<li>The builds aren't notarised by Apple, so macOS may refuse to open them the first time. In Terminal:
<pre>xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"{n}.component" \\
  ~/Library/Audio/Plug-Ins/VST3/"{n}.vst3" "/Applications/{n}.app"</pre></li>
<li>Rescan plug-ins in your DAW. It appears under <b>FreakiePie</b>.</li>
</ol>
<h3>Windows</h3>
<p>Unzip, copy <code>{n}.vst3</code> to <code>C:\\Program Files\\Common Files\\VST3</code> and rescan. The standalone <code>{n}.exe</code> runs from anywhere. SmartScreen may warn you the first time because the build isn't signed: choose <i>More info</i>, then <i>Run anyway</i>.</p>
<h3>Linux</h3>
<pre>tar -xzf {zipname}-Linux.tar.gz
mkdir -p ~/.vst3 ~/.lv2
cp -r "{n}/{n}.vst3" ~/.vst3/
cp -r "{n}/{n}.lv2" ~/.lv2/</pre>
<p>Then rescan in your DAW. The standalone app runs with <code>"./{n}"</code> and uses ALSA or JACK.</p>
<h3>Building it yourself</h3>
<p>You need CMake 3.22 or later and a C++20 compiler (Xcode, Visual Studio 2022, or GCC or Clang).</p>
<pre>git clone --recursive https://github.com/femifleming/PinkiePiesBalloonShop.git
cd PinkiePiesBalloonShop
{"cmake -B build -DCMAKE_BUILD_TYPE=Release" if cfg["key"] == "shop" else "cmake -S BalloonPalace -B build-balloonpalace -DCMAKE_BUILD_TYPE=Release"}
{"cmake --build build --config Release --target BalloonShop_All" if cfg["key"] == "shop" else "cmake --build build-balloonpalace --config Release --target BalloonPalace_All"}</pre>
"""

def flow_section(cfg):
    if cfg["key"] == "shop":
        steps = [
            ("In", "Your input, joined by the plucked notes from the pink pump and whatever Trixie is playing."),
            ("The effects", "The spectral smear, pitch, grains and reverb, the gumball machine and anything bought at the store, in the order on the shelf. The main feedback (the pink loop) feeds the end back to the start."),
            ("Bypass", "Crossfades the effects out over about 50 ms, leaving your input."),
            ("Pops and hiss", "Straight out, or into the effects if you choose."),
            ("Frozen balloons", "The sounds you caught, played as grains. The canister sends them straight out or back round through the effects."),
            ("Sonic rainboom", "Added on top when one happens, made from what the effects are playing."),
            ("Weather", "Rain (a lowpass that eases in), helium (a highpass), clouds and thunder, then the heatwave and the snow."),
            ("Chalkboard EQ", "The curve you drew."),
            ("Leveler and limiter", "A gentle leveler, then a look-ahead limiter at -1 dBFS, so it never clips. The look-ahead is 1.5 ms, and the plug-in reports it to your DAW."),
            ("Out", "Stereo (mono tracks get the two sides mixed)."),
        ]
    else:
        steps = [
            ("In", "Stereo in: your input, the plucked notes and Trixie, as in the shop."),
            ("Your dry sound", "Encoded once, as a point at the little pink loudspeaker, through the distance model."),
            ("The grains", "On spatial, every grain runs its own copy of the effects and plays only what its effects add, as a point at the grain. On scatter, one copy. On copies, every grain is the whole shop. Copies are decorrelated so they never phase against each other."),
            ("The granulator", "A cloud of short grains, each born at its own direction round one of the grains."),
            ("Pops and frozen balloons", "From the loudspeaker's place."),
            ("Sonic rainboom", "The dive and the boom as points, the rainbow as seven rings round where it broke."),
            ("Sphere reverb", "One 64-line FDN for the whole sphere, run on every channel. Grains further out send more into it."),
            ("Energy map", "Measured here (from the first 16 channels, whatever the order), for the glass and the map at the top left."),
            ("Master", "Weather, heat, snow and the chalkboard EQ on every channel, then the leveler and the limiter (-1 dBFS), linked across the channels."),
            ("Out", "Ambisonics at the order you picked (ACN, SN3D: 16 channels at 3rd order, up to 64 at 7th), to a decoder."),
        ]
    boxes = "".join("<div class='step'><b>%s</b><span>%s</span></div>" % (esc(a), esc(b)) for a, b in steps)
    return "<h2 id='flow'>How the sound flows</h2><div class='flow'>%s</div>" % boxes

def gestures_section(cfg):
    rows = [
        ("Drag a balloon", "Move it. Let go and it floats back up on its string."),
        ("Scroll or swipe on a balloon", "Blow it up or let air out (its value)."),
        ("Shift-drag up or down", "Set it by hand. Add alt for finer steps."),
        ("Double-click a balloon", "Back to its default."),
        ("Click a switch or a choice", "Switches flip; choices like shape step to the next."),
        ("Drag a bow", "Move its whole bunch. Along the shelf, it changes the order."),
        ("Drag the pin onto a balloon", "Pop it (caught sounds, store effects)."),
        ("Click a caught sound", "Type its pitch."),
        ("Right-click the snow or the heatwave", "Their fade times."),
        ("Right-click pop sounds", "Pops on or off, or into the effects."),
        ("Click the rainboom cloud", "A rainboom now. Right-click: what sets them off, level and the rainbow's length."),
        ("Fling a balloon hard", "Breaks the sound barrier: a rainboom where it's got to (when on throws is ticked)."),
        ("Cursor to the top left corner", "The card: presets, scale and bypass."),
        ("Cursor to the left edge", "The order list. Drag an effect up or down, or roll the die."),
    ]
    if cfg["key"] == "palace":
        rows += [
            ("Drag the empty glass", "Turn the sphere (your view only, never the sound)."),
            ("Scroll or pinch on the glass", "Zoom in towards the cursor."),
            ("Double-click the glass", "Reset the view."),
            ("Click the jar", "Another grain. Drag one out to drop it somewhere; drag it back or right-click the jar to take one away."),
            ("Drag the pink loudspeaker", "Move your dry sound in the sphere."),
            ("Drag or scroll a token", "Turn it like a knob; click a choice token to step it."),
        ]
    body = "".join("<tr><td>%s</td><td>%s</td></tr>" % (esc(a), esc(b)) for a, b in rows)
    return "<h2 id='gestures'>Quick reference: what your hands do</h2><table class='plain'>%s</table>" % body

def trouble_section(cfg):
    n = "Pinkie Pies Balloon Shop" if cfg["key"] == "shop" else "Pinkie Pies Balloon Palace"
    code = "Fkpi" if cfg["key"] == "shop" else "Fkps"
    items = [
        ("macOS says it can't be opened, or the DAW skips it",
         f"The builds aren't notarised. Run the <code>xattr</code> line from Installing, then rescan. To check the Audio Unit yourself: <code>auval -v aufx {code} Frkp</code>."),
        ("The sound changes on its own",
         "Something in the weather is doing it. Clouds make the balloons inside them wander (and in the Balloon Palace the tokens too), auto weather changes the sky every few minutes, a storm's lightning lends a store effect for a while, and Trixie drops by now and then. Turn off cloudy and auto weather for a steady patch."),
        ("Everything has stopped or gone still", "It's snowing: snow freezes the sound until you click the snowflake again. Also check the bypass on the card."),
        ("A rainboom went off by itself",
         "Right-click the rainboom cloud: on hits, after storms" + (", on fast grains" if cfg["key"] == "palace" else "") + " and on throws each set one off. Untick the ones you don't want."),
        ("A new instance doesn't start from the defaults",
         "Every new instance starts from the last patch you had open. A saved session still opens as you saved it. Reset (top right) puts everything back to the first preset."),
        ("It's loud, or a rainboom is too much", "The limiter keeps the output under -1 dBFS whatever happens. Turn the rainboom down in its menu (level)."),
    ]
    if cfg["key"] == "palace":
        items.insert(0, ("I only hear one or two channels, or the map says the track has 2 channels",
                         "The track needs as many channels as the order: 16 for 3rd order, 36 for 5th, 64 for 7th. In REAPER, the track's Route button, Track channels. Then put a decoder after the Balloon Palace."))
        items.insert(1, ("It sounds wrong through my decoder",
                         "Set the decoder to the same order as the Balloon Palace's order token, ACN channel order, SN3D normalisation (AmbiX). IEM's decoders use ACN and SN3D by default; in SPARTA pick ACN and SN3D."))
    body = "".join("<h3>%s</h3><p>%s</p>" % (esc(a), b) for a, b in items)
    return "<h2 id='trouble'>Problems and answers</h2>" + body

def credits_section(cfg):
    extra = ""
    if cfg["key"] == "palace":
        extra = "<p>The ambisonic conventions follow the IEM Plug-in Suite and SPARTA; the energy map follows the IEM EnergyVisualizer; the distance model follows the IEM RoomEncoder and the reverb the IEM FdnReverb. The source-spread and distance ideas were checked against s3g-dsp. No code from these is included.</p>"
    return f"""<h2 id='credits'>Credits and license</h2>
<p>This is an unofficial fan project, not affiliated with or endorsed by Hasbro. My Little Pony and its characters are trademarks of Hasbro. The character pictures and backgrounds are fan-made, as is the "Magic is Friendship" title font, and they belong to their creators. The flying Rainbow Dash is by Nightmare Moon.</p>
<p>The balloon-store effects are rebuilt from Olivier Bélanger's SoundGrain (the phaser replaces its flanger). The gumball machine, the ring mod and the bouncing plucks take their ideas from Alexander Mesker's thesis <i>Analysis and recreation of key features in selected Autechre tracks from 1998-2005</i> (Macquarie University, 2007).</p>
{extra}
<p>Built with JUCE. The code is released under the GNU Affero General Public License v3, as JUCE's free licence requires. The artwork is not covered by it.</p>
<p>Source, releases and issues: <a href='https://github.com/femifleming/PinkiePiesBalloonShop'>github.com/femifleming/PinkiePiesBalloonShop</a>.</p>"""

def palace_extra_sections():
    flights = [
        ("still", "Each grain stays where you dropped it."),
        ("orbit", "Each grain circles you on its own tilted ring."),
        ("lorenz", "Along the Lorenz attractor (the butterfly), each grain starting somewhere else on it."),
        ("flock", "A flock of birds (boids): they keep apart, line up and stay together."),
        ("random", "A new place every so often, gliding there."),
        ("spiral", "Winding round you, climbing and sinking."),
        ("lissajous", "Looping figures, each grain at its own rates."),
        ("rossler", "The Rossler attractor: a loose coil that now and then flicks up."),
        ("breathe", "Out to the glass and back in, like breathing."),
        ("fall", "From the top of the sphere to the bottom, over and over."),
        ("ping-pong", "Straight across the sphere, from one side to the other."),
    ]
    rows = "".join("<tr><td><b>%s</b></td><td>%s</td></tr>" % (a, esc(b)) for a, b in flights)
    return f"""
<h2 id="ambisonics">Spatial audio: what comes out, and where it goes</h2>
<p>The Balloon Palace is an encoder. It never plays straight to loudspeakers or headphones: it puts out a sound field, and a decoder after it turns that into speaker feeds or binaural headphones. That's what lets the same session play on any speaker layout.</p>
<table class="plain">
<tr><td>Order</td><td>1st to 7th, set with the order token (3rd by default)</td></tr>
<tr><td>Channels</td><td>(order + 1)<sup>2</sup>: 4, 9, 16, 25, 36, 49 or 64</td></tr>
<tr><td>Channel order</td><td>ACN (0 to 15)</td></tr>
<tr><td>Normalisation</td><td>SN3D, without the Condon-Shortley phase (AmbiX)</td></tr>
<tr><td>Axes</td><td>x ahead, y to the left, z up (as in the IEM plug-ins)</td></tr>
<tr><td>Input</td><td>Stereo</td></tr>
<tr><td>VST3 layout</td><td>Ambisonics (ACN) up to 7th order, so hosts that know Ambisonics tracks offer it there; the Audio Unit publishes 3rd order, and takes the others if the host asks</td></tr>
</table>
<h3>In REAPER with the IEM plug-ins</h3>
<ol>
<li>Pick the order with the order token (bottom right; 3rd by default).</li>
<li>Give the track as many channels as that order needs, with the track's <b>Route</b> button, <i>Track channels</i>: 16 for 3rd order, 36 for 5th, 64 for 7th.</li>
<li>After it, add a decoder at the same order: the IEM <b>AllRADecoder</b> for a loudspeaker layout, or the <b>BinauralDecoder</b> for headphones (SN3D). Add the IEM <b>EnergyVisualizer</b> too if you like.</li>
<li>To send several Balloon Palace tracks to one decoder, give them all the same order, feed a bus with the same number of channels, and decode once on the bus.</li>
</ol>
<p>Any host that can run a plug-in on that many channels works the same way. SPARTA's decoders work too: choose ACN and SN3D. On a track with too few channels for the order you picked, the highest order that fits comes out, and the map says so.</p>
<h3>Which order?</h3>
<p>A higher order pins every sound down more sharply. Through a decoder of the same order, a single point puts about 36 % of its energy within 20 degrees of where it is at 3rd order, 62 % at 5th and 83 % at 7th. You hear that most on a big dome of loudspeakers; through headphones the difference is small. It costs a little more: 16 grains with the reverb and the weather take about 23 % of one core at 3rd order, 26 % at 5th and 31 % at 7th (an Apple M2 Max at 48 kHz). The first time you pick 4th order or above, the sound pauses for a moment while the snow and the heatwave get ready for the extra channels.</p>
<p>The one exception to "encoder only" is your own listening while you work: that's the decoder's job, so there's no headphone mode in the Balloon Palace itself.</p>

<h2 id="flight">The flight modes</h2>
<p>Pick one with the flight token; speed sets how fast (0.05x to 3x).</p>
<table class="plain">{rows}</table>

<h2 id="distance">Distance and Doppler</h2>
<p>The distance token is the size of the bubble you're in: its radius, from 1 metre to 50. Every sound is a point with its own distance from you, and distance sounds the way it does in a room, after the IEM RoomEncoder:</p>
<ul>
<li>Level falls with distance (1 / r), referred to 4 metres, so a sound 4 m away is at its own level and nearer ones get up to 12 dB louder. A 1 m bubble is about 24 dB louder than a 50 m one.</li>
<li>Sound takes time to arrive, at 343 metres a second, so a sound 50 m away arrives 146 ms late. Anything moving towards you or away bends in pitch, like a passing car (Doppler). The Doppler token switches that bend off: moving sounds then keep their pitch and still get louder and quieter as they come and go.</li>
<li>Air takes a little of the highs off with distance, and six walls of a room round the bubble answer each sound faintly (9 dB down), enough to tell how far without blurring where.</li>
<li>Close in, a sound spreads round your head instead of pointing.</li>
</ul>
<p>The bubble glides when you turn it, so with Doppler on, resizing it bends the pitch for a moment, as it would.</p>

<h2 id="modes">Grain modes</h2>
<table class="plain">
<tr><td><b>spatial</b></td><td>The default. Every grain carries its own copy of the effects, each copy's sound a point at its grain, its settings deviating from yours. Your dry sound plays once, from the loudspeaker.</td></tr>
<tr><td><b>scatter</b></td><td>One copy of the effects, at the first grain; the granulator's grains still fly to every grain.</td></tr>
<tr><td><b>copies</b></td><td>Every grain is the whole shop, dry sound and all.</td></tr>
</table>
<p>With more grains each is turned down (by 1 / sqrt(N)), so the whole stays about as loud. 16 grains in spatial mode take about 20 to 24 % of one core at 48 kHz on an Apple M2 Max.</p>
"""

# ------------------------------------------------------------------------------------------------
CSS = """
@page { size: A4; margin: 16mm 15mm 18mm 15mm; background: #000; }
@font-face { font-family: 'Magic'; src: url('file://%(font)s'); }
html, body { background: #000; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { font-family: 'Avenir Next', 'Helvetica Neue', Arial, sans-serif; font-size: 10.2pt; line-height: 1.45; color: #fff; }
h1, h2 { font-family: 'Magic', 'Avenir Next', sans-serif; font-weight: normal; color: %(accent)s; }
h2 { font-size: 22pt; margin: 0 0 6pt; padding-top: 4pt; page-break-before: always; letter-spacing: 0.3pt; }
h2.keep { page-break-before: auto; }
h3 { font-size: 11.5pt; margin: 12pt 0 3pt; color: %(accent2)s; }
h4 { margin: 10pt 0 2pt; font-size: 10.5pt; color: %(accent2)s; }
p { margin: 0 0 6.5pt; }
a { color: %(accent)s; text-decoration: none; }
code, pre { font-family: Menlo, Consolas, monospace; font-size: 8.4pt; color: #fff; }
pre { background: #1b1b1f; border: 1px solid #333; border-radius: 6px; padding: 6pt 8pt; white-space: pre-wrap; }
figure { margin: 8pt 0 10pt; page-break-inside: avoid; }
figure img { width: 100%%; border-radius: 6px; border: 1px solid #333; }
figcaption { font-size: 8.6pt; color: #b9b9c0; margin-top: 3pt; }
.cover { height: 255mm; display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center; color: #fff; }
.cover img.icon { width: 150px; border-radius: 34px; }
.cover h1 { font-size: 40pt; margin: 18pt 0 4pt; }
.cover .tag { font-size: 13pt; max-width: 130mm; }
.cover .ver { margin-top: 14pt; font-size: 11pt; color: #b9b9c0; }
.cover .shot { width: 150mm; margin-top: 18pt; border-radius: 10px; border: 1px solid #333; }
.toc { columns: 2; column-gap: 12mm; font-size: 10.5pt; }
.toc a { display: block; margin: 2pt 0; color: #fff; }
.toc a.ch { padding-left: 10pt; color: #c9c9d0; }
table { border-collapse: collapse; width: 100%%; margin: 4pt 0 10pt; page-break-inside: auto; }
tr { page-break-inside: avoid; }
td, th { text-align: left; vertical-align: top; padding: 3pt 5pt; border-bottom: 1px solid #2c2c32; }
th { font-size: 8.6pt; color: #b9b9c0; font-weight: 600; }
table.params td { font-size: 8.6pt; }
table.params td.id { font-family: Menlo, monospace; font-size: 7.8pt; color: #9a9aa4; }
table.plain td:first-child { width: 34%%; }
.flow { display: flex; flex-direction: column; gap: 4pt; }
.step { display: flex; gap: 8pt; padding: 5pt 8pt; border-left: 4px solid %(accent)s; background: #141418; border-radius: 0 6px 6px 0; page-break-inside: avoid; }
.step b { min-width: 34mm; color: %(accent2)s; }
.map { position: relative; margin: 6pt 0; }
.map img { width: 100%%; border-radius: 6px; }
.pin { position: absolute; transform: translate(-50%%, -50%%); width: 17px; height: 17px; border-radius: 50%%; background: %(accent)s; color: white;
       font-size: 9pt; font-weight: 700; text-align: center; line-height: 17px; border: 2px solid white; box-shadow: 0 1px 3px rgba(0,0,0,0.5); }
ol.maplist { columns: 2; column-gap: 10mm; font-size: 9pt; padding-left: 16pt; }
ol.maplist li { margin-bottom: 3pt; break-inside: avoid; }
.chapter-note, .group-note { font-size: 8.8pt; color: #b9b9c0; }
.group-note { margin: -2pt 0 2pt; }
"""

def build(cfg, dump, shots_dir, out_pdf):
    if os.path.exists(out_pdf): os.remove(out_pdf)
    tmp = tempfile.mkdtemp(prefix="manual_", dir=os.environ.get("MANUAL_TMP"))
    shots = Shots([shots_dir], tmp)
    params = json.load(open(os.path.join(dump, "params.json")))
    chapters = json.load(open(os.path.join(dump, "help.json")))
    presets = json.load(open(os.path.join(dump, "presets.json")))
    css = CSS % dict(accent=cfg["accent"], accent2=cfg["accent2"], font=os.path.join(REPO, "Resources/mlp_magic_is_friendship.ttf"))

    cover_shot = shots.get("ui" if cfg["key"] == "shop" else "both")
    parts = []
    parts.append(f"""<div class="cover"><img class="icon" src="file://{cfg['icon']}"><h1>{esc(cfg['name'])}</h1>
<div class="tag">{esc(cfg['tagline'])}</div><div class="ver">Manual for version {cfg['version']}</div>
{"<img class='shot' src='file://%s'>" % cover_shot if cover_shot else ""}</div>""")

    # contents
    toc = [("about", "About this manual"), ("install", "Installing")]
    if cfg["key"] == "palace": toc.append(("ambisonics", "Spatial audio: what comes out, and where it goes"))
    toc += [("window", "The window"), ("flow", "How the sound flows")]
    chap_ids = []
    for i, c in enumerate(chapters):
        cid = "ch%d" % i
        chap_ids.append(cid)
    toc_html = "".join("<a href='#%s'>%s</a>" % (a, esc(b)) for a, b in toc)
    toc_html += "<a href='#guide'>The guide</a>" + "".join("<a class='ch' href='#%s'>%s</a>" % (cid, esc(c["title"])) for cid, c in zip(chap_ids, chapters))
    tail = []
    if cfg["key"] == "palace": tail += [("flight", "The flight modes"), ("distance", "Distance and Doppler"), ("modes", "Grain modes")]
    tail += [("gestures", "Quick reference: what your hands do"), ("params", "Parameter reference"), ("store", "The balloon store's effects"),
             ("presets", "Presets"), ("trouble", "Problems and answers"), ("credits", "Credits and license")]
    toc_html += "".join("<a href='#%s'>%s</a>" % (a, esc(b)) for a, b in tail)
    parts.append("<h2 class='keep' style='page-break-before:always'>Contents</h2><div class='toc'>%s</div>" % toc_html)

    # about
    if cfg["key"] == "shop":
        about = """<p>The Balloon Shop is a multi-effect you play as balloons. Every balloon is a control, and how full it is shows its value. Your sound goes through a chain of effects in an order you set, the weather changes it, pumps blow up plucked notes or catch sounds into frozen balloons, and a little cloud sets off a sonic rainboom.</p>
<p>This manual covers version 1.1.1. Its middle part, the guide, is the same text you'll find in the plug-in's library: click <b>help</b> in the top right corner. Before it come the installation steps, a map of the window and how the sound flows. After it there's a quick reference for gestures, every parameter your DAW can automate, the store's effects, the presets and answers to common problems.</p>"""
    else:
        about = """<p>Pinkie Pie's Balloon Palace is the Balloon Shop inside a glass sphere: an Ambisonics encoder, from 1st to 7th order (3rd by default). Stereo comes in; a sound field comes out (16 channels at 3rd order, 36 at 5th, 64 at 7th), for a decoder to play on loudspeakers or headphones. Little bouquets of balloons fly round inside the sphere, each carrying its own copy of the effects, and every sound is a single point you can hear and find, with its distance, its Doppler shift and one reverb for the whole sphere.</p>
<p>This manual covers version 0.2.1. Its middle part, the guide, is the same text you'll find in the plug-in's library: click <b>help</b> in the top right corner. Before it come the installation steps, how to route it to a decoder, a map of the window and how the sound flows. After it there are the flight modes, distance and Doppler, a gesture reference, every parameter your DAW can automate, the store's effects, the presets and answers to common problems.</p>
<p>Everything in the Balloon Shop is here too, so the shop's own manual also applies.</p>"""
    parts.append("<h2 id='about'>About this manual</h2>" + about)
    parts.append(install_section(cfg))
    if cfg["key"] == "palace":
        parts.append(palace_extra_sections().split('<h2 id="flight">')[0])
    parts.append("<h2 id='window'>The window</h2><p>The numbers on the picture match the list under it. The window can be resized from its corner; everything scales together.</p>" + window_map(cfg, shots))
    parts.append(flow_section(cfg))

    # the guide
    parts.append("<h2 id='guide'>The guide</h2><p class='chapter-note'>The chapters of the library inside the plug-in, with pictures. Each starts on its own page.</p>")
    figs = FIGS[cfg["key"]]
    for i, (cid, c) in enumerate(zip(chap_ids, chapters)):
        body = para(c["body"])
        fs = "".join(figure(shots, s, cap) for s, cap in figs.get(c["title"], []))
        parts.append("<h2 id='%s'%s>%s</h2>%s%s" % (cid, " class='keep'" if i == 0 else "", esc(c["title"]), body, fs))

    if cfg["key"] == "palace":
        rest = palace_extra_sections()
        parts.append('<h2 id="flight">' + rest.split('<h2 id="flight">')[1])
    parts.append(gestures_section(cfg))

    # parameters
    gs, store_tables = groups(cfg, params)
    ph = ["<h2 id='params'>Parameter reference</h2><p>Every control your DAW can automate, by the name it shows there. Defaults are what a fresh instance starts with (reset brings them back). The store's effects follow on the next pages.</p>"]
    for title, note, ps in gs:
        if not ps: continue
        ph.append("<h3>%s</h3>%s%s" % (esc(title), "<p class='group-note'>%s</p>" % esc(note) if note else "", param_rows(ps)))
    parts.append("".join(ph))
    sh = ["<h2 id='store'>The balloon store's effects</h2><p>Each effect has a <i>Bought</i> switch (whether it's on the shelf), its two controls, a level (up to 200 %) and a width (from mono in the middle to wide stereo). The tuned ones follow the scale.</p>"]
    for nm, ps in store_tables:
        sh.append("<h4>%s</h4>%s" % (esc(nm), param_rows(ps)))
    parts.append("".join(sh))
    parts.append("<h2 id='presets'>Presets</h2><p>Open them from the card at the top left (or your DAW's preset list). Each has its own picture behind the shop.</p><ol>%s</ol>"
                 % "".join("<li>%s</li>" % esc(p) for p in presets))
    parts.append(trouble_section(cfg))
    parts.append(credits_section(cfg))

    doc = "<!doctype html><html><head><meta charset='utf-8'><title>%s manual</title><style>%s</style></head><body>%s</body></html>" % (esc(cfg["name"]), css, "\n".join(parts))
    page = os.path.join(tmp, "manual.html")
    open(page, "w").write(doc)
    r = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--allow-file-access-from-files",
                        "--user-data-dir=" + os.path.join(tmp, "chrome"), "--virtual-time-budget=20000",
                        "--print-to-pdf=" + os.path.abspath(out_pdf), "file://" + page], capture_output=True, text=True)
    if not os.path.exists(out_pdf) or os.path.getmtime(out_pdf) < os.path.getmtime(page):   # (Chrome can crash on its way out after writing it)
        print(r.stderr[-2000:]); sys.exit(1)
    shutil.copy(page, os.path.splitext(out_pdf)[0] + ".html") if os.environ.get("KEEP_HTML") else None
    print("wrote", out_pdf, "%.1f MB" % (os.path.getsize(out_pdf) / 1e6))

if __name__ == "__main__":
    which, dump, shots, out = sys.argv[1:5]
    build(SHOP if which == "shop" else PALACE, dump, shots, out)

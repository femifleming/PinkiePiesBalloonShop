<p align="center"><img src="docs/icon.png" width="160" alt="Mellyloon icon"></p>

# Mellyloon: the spatial audio version of Pinkie Pie's Balloon Shop

> **Spatial audio.** Mellyloon is the third-order Ambisonics version of [Pinkie Pie's Balloon Shop](../README.md), kept in the Balloon Shop's repository (this folder). It is an **encoder**: stereo goes in, 16 channels of Ambisonics come out (ACN order, SN3D, the format of the IEM and SPARTA plug-ins), and it decodes nothing itself. Route all 16 channels to a decoder, such as the IEM AllRADecoder for loudspeakers or the BinauralDecoder for headphones. For the ordinary stereo plug-in, see the [Balloon Shop](../README.md).

The same multi-effect you play as balloons, inside a glass sphere you can turn round, with its sound placed in 3-D.

- **Grains.** Little balloon bouquets fly round inside the sphere. Drop them in from the jar (up to 16). Your dry sound plays once, from a loudspeaker you can drag. On *spatial* (the default) every grain carries its own copy of the effects, each copy's sound a point at its grain, its settings deviating from yours and freezing at its own times. On *scatter* there's one copy. On *copies* every grain is the whole shop, dry sound and all.
- **The granulator** works like the IEM GranularEncoder: a cloud of short Hann-windowed mono grains (about 20 at once by default), each born at its own direction round one of the grains and encoded there, with Hermite interpolation and anti-aliased octave-up reads.
- **Flight.** Still, orbit, the Lorenz attractor, a flock (boids) or random glides, through the whole inside of the sphere, with speed and distance (up to 12 m).
- **Distance, after the IEM RoomEncoder.** Every sound is a point with its own propagation delay (so moving sounds Doppler-shift), 1/r level, a little air absorption and first-order reflections from the walls of a room round the sphere.
- **Weather in the sphere.** Clouds, rain, snow and heat drift through the glass as patches. A grain they pass over has its settings shaken up at random; snow freezes it and the heat smears it.
- **One reverb** for the whole sphere, built like the IEM FdnReverb: 64 lines with IEM's prime delay recipe, Hadamard mixing and frequency-dependent decay, run on all 16 channels.
- **An energy map** after the IEM EnergyVisualizer, on the glass and flat at the top left.

Everything else is the Balloon Shop: the order ring, the pumps, the store, the chalkboard, the weather bar, the card, the library and Trixie.

- macOS: Audio Unit, VST3 and a standalone app
- Windows: VST3 and a standalone app
- Linux: VST3, LV2 and a standalone app

![Mellyloon](docs/screenshot.jpg)

**[How to use it: the full help page](docs/HELP.md)** (the same guide is in the plug-in's library: click **help** in the top right corner).

## In REAPER with the IEM plug-ins

1. Put Mellyloon on a track and set the track to 16 channels (the track's **Route** button, *Track channels: 16*).
2. After it, add a decoder for your speakers or headphones, for example IEM **AllRADecoder** or **BinauralDecoder** (order 3, normalisation SN3D), and the IEM **EnergyVisualizer** if you like.

On a track with fewer than 16 channels only the first channels come out, and the plug-in's map says so.

## Download and build

Mellyloon isn't released as a download yet. When it is, its releases will be tagged `mellyloon-v...` on the [Balloon Shop's Releases page](https://github.com/femifleming/PinkiePiesBalloonShop/releases), separate from the shop's own. For now, build it from this folder. You need CMake 3.22 or later and a C++20 compiler (Xcode on macOS, Visual Studio 2022 on Windows, GCC or Clang on Linux):

```sh
git clone --recursive https://github.com/femifleming/PinkiePiesBalloonShop.git
cd PinkiePiesBalloonShop
cmake -S Mellyloon -B build-mellyloon -DCMAKE_BUILD_TYPE=Release
cmake --build build-mellyloon --config Release --target Mellyloon_All
```

It uses the shop's JUCE (the `JUCE` submodule) and the shop's pictures (`Resources`), so it needs the whole repository, not this folder alone. On macOS and Windows the build also copies the plug-ins into your user plug-in folders (turn that off with `-DMELLYLOON_COPY_AFTER_BUILD=OFF`); it appears in your DAW as **Mellyloon** under **FreakiePie**. Linux needs the JUCE dependencies listed in the [Balloon Shop's README](../README.md#build-it-yourself).

With `-DMELLYLOON_BUILD_TESTS=ON` you also get three test programs: `MellyloonSphereTest` checks the spatial side (the harmonics against the AmbiX definition, every channel of the output, distance and Doppler, the granulator, the reverb, every flight mode with 16 grains, and the CPU), `MellyloonTest` the effect chain, and `MellyloonHostTest` drives the whole interface headlessly, saving screenshots (`SPHERE_SHOTS=<folder>` saves a set of views of the sphere).

## How it works

- Spatial mode: each copy of the effects gives out only its effects' sound (its output minus the dry part it passed, sample by sample), and that is a single point at its grain, so every copy is a point you can find. The dry sound is encoded once, at the source. With several copies each is turned down by 1 / sqrt(N).
- Inside the sphere the higher orders fade as a sound nears the middle, so it spreads round you, and it gets up to 6 dB louder there. As things move, their 16 gains glide across each block; each granulator grain keeps its direction for its whole life, as in IEM's.
- The master weather filters, snow, heatwave, EQ, leveler and limiter run on all 16 channels, the leveler and limiter linked. Snow and the heatwave resynthesise each channel separately, which makes a frozen scene diffuse.
- On an Apple M2 Max, 16 grains in spatial mode take about 20 to 24 % of one core at 48 kHz (measured by the sphere test).

## Credits and notes

- This is an unofficial fan project, not affiliated with or endorsed by Hasbro. My Little Pony and its characters are trademarks of Hasbro. The character pictures and backgrounds are fan-made, as is the "Magic is Friendship" title font, and they belong to their creators.
- The ambisonic conventions follow the IEM Plug-in Suite and SPARTA; the energy map follows the IEM EnergyVisualizer. The source-spread and distance ideas were checked against s3g-dsp. No code from these is included.
- The balloon-store effects are rebuilt from Olivier Bélanger's SoundGrain; the gumball machine, the ring mod and the bouncing plucks take their ideas from Alexander Mesker's thesis on Autechre (Macquarie University, 2007).
- Built with [JUCE](https://juce.com).

## License

The code is released under the GNU Affero General Public License v3 (see [LICENSE](LICENSE)), as JUCE's free licence requires. The artwork is not covered by it (see above).

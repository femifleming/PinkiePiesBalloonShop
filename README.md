<p align="center"><img src="docs/icon.png" width="160" alt="Pinkie Pie's Balloon Shop icon"></p>

# Pinkie Pie's Balloon Shop

A multi-effect you play as balloons. Every balloon is a control, and how full it is shows its value.

Your sound goes through the effects in the order you set: move the cursor to the left edge and the order list slides out, then drag the effects up or down. The weather changes the sound too: rain softens it, clouds bring thunder and lightning, snow freezes it solid and a heatwave melts it into a slow smear (right-click the snow or the heatwave to set how slowly they come and go). Pumps blow up plucked notes or catch sounds into frozen balloons, and a chalkboard lets you draw grain shapes and a master EQ. The preset and scale card waits above the top left corner until you reach for it, and every so often the Great and Powerful Trixie drops by.

- macOS: Audio Unit, VST3 and a standalone app
- Windows: VST3 and a standalone app
- Linux: VST3, LV2 and a standalone app

![Pinkie Pie's Balloon Shop](docs/screenshot.jpg)

> **Spatial audio version: [Mellyloon](Mellyloon/README.md).** The same shop inside a glass sphere, as a third-order Ambisonics encoder (16 channels, ACN/SN3D, for the IEM and SPARTA decoders): grains fly round you, every sound is a point you can find, with distance, Doppler and a 64-line spatial reverb. It lives in the [`Mellyloon`](Mellyloon) folder of this repository.
>
> ![Mellyloon](Mellyloon/docs/screenshot.jpg)

**[How to use it: the full help page](docs/HELP.md)** (the same guide is in the plug-in's library: click **help** in the top right corner).

## Download

Get the latest version from the [Releases page](https://github.com/femifleming/PinkiePiesBalloonShop/releases/latest):

| System | File |
| --- | --- |
| macOS 11 or later (Apple silicon and Intel) | [PinkiePiesBalloonShop-macOS.zip](https://github.com/femifleming/PinkiePiesBalloonShop/releases/latest/download/PinkiePiesBalloonShop-macOS.zip) |
| Windows 10 or later (64-bit) | [PinkiePiesBalloonShop-Windows.zip](https://github.com/femifleming/PinkiePiesBalloonShop/releases/latest/download/PinkiePiesBalloonShop-Windows.zip) |
| Linux (x86-64) | [PinkiePiesBalloonShop-Linux.tar.gz](https://github.com/femifleming/PinkiePiesBalloonShop/releases/latest/download/PinkiePiesBalloonShop-Linux.tar.gz) |

## Install

### macOS with Homebrew

```sh
brew tap femifleming/pinkiepiesballoonshop
brew trust femifleming/pinkiepiesballoonshop      # recent Homebrew asks you to trust a tap first
brew install --cask pinkiepiesballoonshop
```

If you installed it by hand before, add `--force` to the last line so Homebrew can replace those copies.

This puts the app in `/Applications` and the Audio Unit and VST3 in `~/Library/Audio/Plug-Ins`. Update with `brew upgrade --cask pinkiepiesballoonshop`, remove with `brew uninstall --cask pinkiepiesballoonshop`.

### macOS by hand

1. Unzip `PinkiePiesBalloonShop-macOS.zip`.
2. Copy `Pinkie Pies Balloon Shop.component` to `~/Library/Audio/Plug-Ins/Components` and `Pinkie Pies Balloon Shop.vst3` to `~/Library/Audio/Plug-Ins/VST3`. Put the app anywhere you like, such as `/Applications`.
3. The builds aren't notarised by Apple, so macOS may refuse to open them the first time. Open Terminal and run:
   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Pinkie Pies Balloon Shop.component" ~/Library/Audio/Plug-Ins/VST3/"Pinkie Pies Balloon Shop.vst3" "/Applications/Pinkie Pies Balloon Shop.app"
   ```
4. In your DAW, rescan plug-ins. In Ableton Live: Settings > Plug-ins, turn on *Use Audio Units v2* and/or *Use VST3 Plug-in System Folders*, then press Rescan. It appears under **FreakiePie**.

### Windows

1. Unzip `PinkiePiesBalloonShop-Windows.zip`.
2. Copy `Pinkie Pies Balloon Shop.vst3` to `C:\Program Files\Common Files\VST3`.
3. Rescan plug-ins in your DAW. The standalone `Pinkie Pies Balloon Shop.exe` runs from anywhere. Windows SmartScreen may warn you the first time because the build isn't signed: choose *More info* > *Run anyway*.

### Linux

1. Unpack: `tar -xzf PinkiePiesBalloonShop-Linux.tar.gz`
2. Copy the plug-ins into your user folders:
   ```sh
   mkdir -p ~/.vst3 ~/.lv2
   cp -r "Pinkie Pies Balloon Shop/Pinkie Pies Balloon Shop.vst3" ~/.vst3/
   cp -r "Pinkie Pies Balloon Shop/Pinkie Pies Balloon Shop.lv2" ~/.lv2/
   ```
3. Rescan in your DAW (most Linux DAWs look in these folders). The standalone app runs with `"./Pinkie Pies Balloon Shop"` and uses ALSA or JACK.

## Build it yourself

You need CMake 3.22 or later and a C++20 compiler (Xcode on macOS, Visual Studio 2022 on Windows, GCC or Clang on Linux).

```sh
git clone --recursive https://github.com/femifleming/PinkiePiesBalloonShop.git
cd PinkiePiesBalloonShop
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target BalloonShop_All
```

On macOS and Windows the build also copies the plug-ins into your user plug-in folders (turn that off with `-DBALLOONSHOP_COPY_AFTER_BUILD=OFF`). On Linux, install the JUCE dependencies first:

```sh
sudo apt-get install libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev libegl-dev libgl1-mesa-dev
```

The tests (`-DBALLOONSHOP_BUILD_TESTS=ON`) check the sound and drive the whole interface headlessly, saving screenshots as they go.

## Credits and notes

- This is an unofficial fan project, not affiliated with or endorsed by Hasbro. My Little Pony and its characters are trademarks of Hasbro. The character pictures and backgrounds are fan-made, as is the "Magic is Friendship" title font, and they belong to their creators.
- The balloon-store effects are rebuilt from Olivier Bélanger's SoundGrain (the phaser replaces its flanger).
- The gumball machine, the ring mod and the bouncing plucks take their ideas from Alexander Mesker's thesis *Analysis and recreation of key features in selected Autechre tracks from 1998-2005* (Macquarie University, 2007).
- Built with [JUCE](https://juce.com).

## License

The code is released under the GNU Affero General Public License v3 (see [LICENSE](LICENSE)), as JUCE's free licence requires. The artwork is not covered by it (see above).

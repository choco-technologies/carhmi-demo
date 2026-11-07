# carhmi-demo

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/carhmi-demo/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/carhmi-demo/actions/workflows/ci.yml)

A car dashboard (HMI) for 480 x 272 and 320 x 240 displays - an example of how easily a
dmodOS application gets a GUI: the interface is **one HTML page**
([assets/car_hmi.html](assets/car_hmi.html)), made with Tailwind CSS and
JavaScript, with no code of its own for the screen.

At build time the page is laid out as a browser does and compiled into a
dmview view: its pages, the speedometer, the player and the climate controls,
animations and touches all run on the device, in dmview - the application
only claims the display for the view.

## What's here

| Module | Kind | What it is |
|--------|------|------------|
| `carhmi` | Application (service) | Claims the display for the view while it runs, plays the MP3 files of the SD card |
| `carhmi_view` | Data | Both resolutions of `carhmi.dmv`, each with its fonts (`.dmvf`) and images (`.dmvi`) |

The view is a package of its own, as it is too big for a microcontroller's
flash: it goes to the SD card.

## Running it on dmod-boot

```bash
cmake -S . -B build -DBOARD=stm32f746g-disco \
    -DDMBOOT_EXTRA_FLASH_DMD_FILES=<carhmi-demo>/dmod-boot/flash.dmd \
    -DDMBOOT_EXTRA_SDCARD_DMD_FILES=<carhmi-demo>/dmod-boot/sdcard.dmd
cd build
cmake --build . --target install-firmware   # the firmware
cmake --build . --target upload-files       # the view, to the SD card over the console
```

See [docs/dmod-boot.md](docs/dmod-boot.md).

## The page

[assets](assets) is the design: `car_hmi.html`, the fonts it uses
(Inter, Phosphor Icons - `assets/fonts`) and the cover every track is shown
with for now (`assets/images/cover-default.png`; the tracks' own covers, from
their ID3 tags, are to come - this one stays for the tracks without one), all
next to it, so it also opens in a browser. The PNG is drawn from
`cover-default.svg`:

```bash
google-chrome --headless=new --hide-scrollbars --window-size=512,512 \
    --screenshot=assets/images/cover-default.png assets/images/cover-default.svg
```
[docs/view.md](docs/view.md) describes how it becomes a view.

## Navigation demo

The navigation page opens at startup. It shows an original schematic street map,
a blue route, the next turn, remaining distance and a replayed vehicle position.
The map follows a 12-step demo route; it is not a live GNSS or Google Maps feed.
Press Start to begin. The replay advances only while the navigation page is visible and stops at the
arrival screen. Pause/resume, replay, two zoom levels and day/night maps work
locally on the device. Drag the map with one finger; pinch with two fingers to switch zoom levels.
Manual navigation pauses the replay and keeps the vehicle attached to its map
position. The crosshair returns to the vehicle without restarting the trip;
resume also recenters. Panning is bounded so the map cannot reveal empty space.

The page scripts compile into DMV timers and group translations. Native
contact capture and PINCH handlers are in `assets/navigation-gestures.dmvs`;
`scripts/bind_navigation.py` connects them to the converted named groups and
fails if the layout changes incompatibly. `assets/dmod.js` provides the browser
counterpart. `test_navigation_input` exercises the compiled view with real
renderer input calls in an off-screen buffer (including two contacts). The day/night
SVGs in `assets/images/navigation-*.svg` contain the map and route; no map tiles,
network connection or API key are needed. The audio and music controls remain
available through the sidebar.

The native gestures require `libdmview >= 0.15.1` with second-contact PINCH
support. Until that renderer is released, build the matching dmview change and
supply its local package manifest through `-DDMOD_DMM_URL=/path/to/manifest.dmm`.
The HTML remains the design source; no generated DMV files need manual edits.

## Fuel and refuelling

The simulated tank starts full and drains in 15 minutes of engine running time,
including while other pages are visible. Switching the engine off pauses consumption;
restarting resumes from the remaining level. At empty, the engine stops; attempts
to start it show a red empty-tank icon. Switch the engine off before refuelling:
the filler stays closed while it is running, and tapping it shows a red crossed-out
engine icon beside the filler. It flashes four times and disappears; another
attempt repeats it. The instruction below the tank says to switch the engine off.
Starting is also blocked while the hose is connected. Tap the fuel gauge on the
dashboard or the fuel percentage in the top bar to open the tank.
Tap the filler on the right once to open its cap, then again to insert the hose.
The pump hums while the level rises (20 seconds from empty to full) and stops
at full. After filling, tap once to remove the hose, then again to close the cap.
The same sequence can interrupt filling early. Closing the screen with the ×
button also disconnects and closes it; the fuel already added remains. The left
menu stays visible; choosing another page closes the tank and stops the pump.
The engine stays off until started explicitly. Restarting the application starts
with a full tank.
The HTML browser preview mirrors the interaction with local Web Audio;
the device synthesizes the pump alongside the engine and music.

## Games: Literki

The gamepad in the left sidebar opens **Gry**, with one game: **Literki**.
It has 81 original pictures (three for each of 27 Polish initial letters),
including **mama**, **tata** and **dom**, and four randomly mixed activities:
choose an initial letter, match a letter to a picture, arrange spelling tiles,
and read a word to select its picture. Correct answers earn a star, a chime
and applause. Music and car sounds are paused while the games panel is open.

Polish female-voice recordings can be generated from
[the ElevenLabs CSV](docs/games/elevenlabs.csv) or
[the copyable text list](docs/games/teksty-do-nagran.txt), with 193 named MP3
files. Place them in `assets/games/literki/audio/`; the build packages them
with both view variants. Without these recordings the STM32 still plays the
reward sounds; browser preview can use a local Polish speech voice.
See [the game, recording and testing guide](docs/games/literki.md).

## Building

```bash
cmake -S . -B build -DDMOD_TOOLS_NAME=arch/armv7/cortex-m7
cmake --build build
```

Every build, including release builds for each architecture, generates both
resolutions in one `carhmi_view.zip` with one unchanged module binary:

```text
views/480x272/carhmi.dmv + fonts/images
views/320x240/carhmi.dmv + fonts/images
```

Install a variant with a screen-aware `dmf-get`:

```bash
mkdir -p build/sdcard/bin build/sdcard/views
dmf-get install carhmi_view --screen 320x240 -t arch/armv7/cortex-m7 -y \
    -o build/sdcard/bin --views-dir build/sdcard/views
```

Or select it in a board's `.dmd`: `carhmi_view @screen 320x240`. The installer
copies only that bundle into `views/carhmi_view/`, so the application keeps the
same path. Screen selectors are opaque, case-sensitive variant identifiers;
for other interfaces names such as `epaper` or `epaper-mono` are valid too.
480x272 is this package's default. An explicit unsupported resolution
fails with the available choices. Older installers ignore the new `.dmr`
attributes: upgrade `dmf-get` before installing a package with screen variants.

The build downloads `todmvs` (and the converters it loads) with `dmf-get`, so
it needs `dmf-get` and `dmod_loader`. Pass `-DDMOD_DIR=/path/to/local/dmod`
to build against a local dmod checkout.

## Project Structure

```
carhmi-demo/
├── assets/
│   ├── car_hmi.html        # the design
│   ├── fonts/              # Inter, Phosphor Icons (+ their licenses)
│   └── images/             # the default cover and the day/night navigation maps
├── cmake/carhmi_page.cmake # page -> the view's sources (todmvs)
├── configs/carhmi.ini      # the service unit
├── dmod-boot/              # flash.dmd and sdcard.dmd to add to dmod-boot
├── src/carhmi.c            # the application
├── src/carhmi_view.c       # the data module
├── carhmi.dmr              # package of carhmi
├── carhmi_view.dmr         # package of carhmi_view
├── docs/
└── tests/
```

## License

MIT - see [LICENSE](LICENSE). The fonts and images in `assets` have their own
licenses: Inter (SIL OFL, `assets/fonts/LICENSE-Inter.txt`), Phosphor Icons
(MIT, `assets/fonts/LICENSE-Phosphor.txt`), the photos are from
[Unsplash](https://unsplash.com/license).

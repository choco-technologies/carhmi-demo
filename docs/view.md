# From the page to the view

```
assets/car_hmi.html ──(todmvs: dmvs_html + dmvs_js)──► carhmi.dmvs + fonts + images
                                  ──(dmod, DMOD_ASSETS_PATHS)──► carhmi.dmv, .dmvf, .dmvi
```

[cmake/carhmi_page.cmake](../cmake/carhmi_page.cmake) downloads `todmvs` with
`dmf-get` and converts the page when CMake configures (the sources have to
exist when dmod looks for assets). The element `hmi-screen` is the screen; the
page is laid out twice, at 480x272 and 320x240. Compact CSS rearranges the
same controls and scripts; `bind_navigation.py` derives map gesture geometry
from each converted layout. `bind_fuel.py` binds the tank well to native
BOX height/Y operands so the fuel rises from the bottom. dmod's assets step then makes
the view, the fonts (only the characters the view draws) and the images (at
the sizes it draws them).

The page requires `dmvs_js` and `dmvs_html` >= 0.4 and `dmvsi` and
`libtodmvs` >= 0.8. Update cached tools in `DMOD_VIEWS_TOOLS_DIR` together;
older versions can silently omit properties or use incompatible document layouts.

## What the page may use

The page is a design as a browser shows it; what is converted is what the
tools support (see dmvs_html and dmvs_js):

- HTML, Tailwind CSS (Play CDN script), the cascade, flexbox and grid;
- fonts and icon fonts through `<link rel="stylesheet">` of local files -
  `assets/fonts/*.css` (`@font-face` of TrueType files);
- images by relative paths (`assets/images`) - **baseline** JPEGs or PNGs: the
  JPEG decoder does not read progressive ones. Keep image file names short:
  the names of the converted images (with the size and the cut) are limited;
- JavaScript: what is known when the page is converted is computed then, the
  rest becomes the view's variables and handlers.

What the converter cannot do is reported as warnings while the page is
converted (`todmvs` without `-q`).

## Screen variants

CMake generates `view-src/<screen>/carhmi.dmvs` and the resources next to it,
then DMOD's `DMOD_ASSETS_SCREENS` converts each bundle into `views/<screen>/`.
The existing `carhmi_view` package contains both bundles and a single module
binary. Its `.dmr` declares `[screen=480x272] [default=true]` and `[screen=320x240]`.
Release builds use the same path, so every architecture ships both resolutions.

A screen-aware `dmf-get` chooses resources through `--screen 320x240` or a `.dmd`
entry `carhmi_view @screen 320x240`. Both install as `views/carhmi_view/carhmi.dmv`
with the selected bundle's fonts/images. There is no resolution-specific module
name, registry entry or application argument. An explicit unsupported screen
fails; without a selector the 480x272 default is installed.

Screen IDs are opaque to the package manager: a named profile such as `epaper`
can be selected without interpreting it as pixel dimensions. CarHMI currently
provides the `480x272` and `320x240` profiles, while HTML layout still receives
its actual viewport dimensions independently. The historical `build/views/`
path mirrors the default 480x272 bundle for host tools; only the per-profile
bundles are packaged.

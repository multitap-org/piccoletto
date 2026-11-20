# piccoletto branding

The name is always lowercase: piccoletto. It is piccolo's little sibling: same
colors, same type, same hard shadow (see piccolo's `assets/branding/README.md`).

## Files

- `piccoletto-icon.svg`: app icon (48 px and up). Two tiles: piccolo's, large and
  dimmed behind, and piccoletto's, small in front with its hard shadow, a d-pad
  and one yellow button.
- `piccoletto-icon-small.svg`: 24 and 32 px, simpler shapes, no shadow.
- `piccoletto-icon-16.svg`: 16 px, drawn on the pixel grid.
- `piccoletto-logo-dark.svg`, `piccoletto-logo-light.svg`: icon + wordmark,
  transparent background, for dark and light pages. The icon here has no
  background tile (the two tiles stand on the page).
- `piccoletto-banner-tv-dark.svg`, `piccoletto-banner-tv-light.svg`: Android TV
  banner, piccolo's frame with the logo, drawn at 640x360, exported at 320x180
  (xhdpi). Dark is the one for the launcher.
- `android/`: the Android adaptive icon and resources (see Android below).
- `png/`: the PNG exports of all of the above.

The logo and banner SVGs have their text converted to paths, so they don't need the font
installed.

## Android

`android/` has the adaptive icon (Android 8+, the APK's minimum) and a `res/` tree
ready to copy into the APK's resources:

- `ic_launcher_background.svg`, `ic_launcher_foreground.svg`: the two layers, on
  the 108 dp canvas. Launchers show the middle 72 dp through their own mask
  (circle, squircle, rounded square); everything that matters stays inside the
  centered 66 dp circle, the part no mask cuts.
- `ic_launcher_monochrome.svg`: the themed icon (Android 13+), one color; the
  system tints it by alpha.
- `res/mipmap-{mdpi..xxxhdpi}/`: the three layers as PNGs (108 to 432 px).
- `res/mipmap-anydpi-v26/ic_launcher.xml`: background + foreground.
  `res/mipmap-anydpi-v33/ic_launcher.xml`: the same plus monochrome, which is
  an API 33 element, so linking against an older android.jar still works.
- `res/drawable-xhdpi/banner.png`: the TV banner (`android:banner`).

With `android:icon="@mipmap/ic_launcher"` in the manifest. No legacy
`ic_launcher.png` is needed: with minSdk 26 the adaptive one is always used.

    inkscape android/ic_launcher_foreground.svg -w 432 -h 432 -o android/res/mipmap-xxxhdpi/ic_launcher_foreground.png

(108 mdpi, 162 hdpi, 216 xhdpi, 324 xxhdpi, 432 xxxhdpi.)

The back tile (piccolo's) is in the background layer and the front tile in the
foreground, so launchers with parallax move them apart. In the monochrome icon
the back tile is dimmed and the d-pad and button are cut out of the front tile.

## Colors

piccolo's, plus a lighter blue for the «etto» suffix:

| Use                  | Dark page | Light page |
|----------------------|-----------|------------|
| Text                 | `#E0E6F0` | `#1A2840`  |
| Suffix «etto»        | `#9DB8E8` | `#5878A8`  |
| Hard shadow (6/150)  | `#101828` | `#B4C2DC`  |
| Background           | `#2A4068` | `#E8EDF5`  |
| Front tile           | `#1A2840` | `#1A2840`  |
| Back tile (piccolo)  | `#34507C` | `#34507C`  |
| Accent (first «o»)   | `#FFC93C` | `#FFC93C`  |

The app icon's own background is `#2A4068` (piccolo's is `#1A2840`), so the two
don't look alike side by side in a launcher.

## Type

Fredoka SemiBold (600), 150 px, letter-spacing -3 (OFL, Google Fonts), as
piccolo. The first «o» is a yellow disc the size of the x-height; «etto» is the
same size as the rest, only lighter.

## Regenerating

Logos: write the text as `<text>` (font Fredoka installed), then

    inkscape logo.src.svg --export-text-to-path --export-plain-svg --export-filename=piccoletto-logo-dark.svg

PNGs:

    inkscape piccoletto-icon.svg -w 256 -h 256 -o png/piccoletto-icon-256.png
    inkscape piccoletto-logo-dark.svg -w 1632 -o png/piccoletto-logo-dark.png

48-512 from `piccoletto-icon.svg`, 24-32 from `piccoletto-icon-small.svg`, 16
from `piccoletto-icon-16.svg`.

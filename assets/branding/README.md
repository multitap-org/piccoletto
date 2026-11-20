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
- `piccoletto-banner-tv-dark.svg`: Android TV banner, piccolo's banner frame with
  the dark logo centered; drawn at 640x360, exported at 320x180 (xhdpi).
- `png/`: the PNG exports of all of the above.

Where they're used: `android/res` (the launcher icon in five densities and the
TV banner, exported from these), the Flatpak (`flatpak/`: the scalable icon and
the PNGs as hicolor icons).

The logo SVGs have their text converted to paths, so they don't need the font
installed.

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

Android, after changing the icon or the banner:

    for d in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
        inkscape piccoletto-icon.svg -w ${d#*:} -h ${d#*:} -o ../../android/res/mipmap-${d%:*}/ic_launcher.png
    done
    inkscape piccoletto-banner-tv-dark.svg -w 320 -h 180 -o png/piccoletto-banner-tv-dark.png
    cp png/piccoletto-banner-tv-dark.png ../../android/res/drawable-xhdpi/banner.png

Not made yet: an Android adaptive icon (foreground / background layers); the
legacy square icon is used.

# CLAUDE.md — piccoletto

Working brief for Claude Code sessions in this repo. Read this first, then
`STYLE.md` (mandatory for all code), then `docs/design-notes.md` (the design
record; untracked, write notes there) and `docs/pending.md` (what's left, the
handoff from the first session). What piccolo itself needs for
piccoletto (netcode, pairing, the network virtual device) is in piccolo's
`docs/design-notes.md`. The mockups are at
https://claude.ai/artifact/KrehctLfAefY3Dc7A2aPti.

## What this is

**piccoletto** is piccolo's companion app (Android first; iOS and web later).
It never runs games itself: no cores, no emulation on the phone. Per paired
host (a piccolo) it offers what that host granted it when it was paired:

- **GAMEPAD**: the phone as a controller for piccolo (a virtual network
  device on piccolo, a member of any port). Looks like piccolo's pad picture;
  the buttons can be moved (EDIT LAYOUT).
- **REMOTE PLAY**: piccolo streams the game, the phone shows it inside a
  minimal bezel flush with the phone's edges (portrait: screen on top,
  controls below; landscape: screen in the middle) and sends input back.
- **DEVICES**: manage only: move the connected devices (e.g. 4 phones) to
  the right ports.

Pairing: the host decides the access level (on piccolo's side). The UI never
says "a piccolo": it's **PAIR A DEVICE**.

Owner: Andrés (former RetroArch developer, `andres-asm` on GitHub). He wants
direct feedback: say when something is wrong, including his ideas. Reply in
the language he writes in (English or Spanish).

## Look: retro but modern

The retro feel comes from retroui's 5x5 pixel font, the theme palettes
(BLUE default, PURPLE, ZSNES, BLACK) and the pad's blocks. NOT from piccolo's
desktop chrome: no menu bar, title-bar windows, sunken fields, status line or
bevels. Flat cards on the background, big type, one primary action per
screen, touch targets of 48 dp or more (Andrés: the first pass "parece
winamp"). Full screen, no system bars. Layout in retro pixels (2 dp each on a
phone), see `src/touch_ui.h`.

## Repositories

piccoletto builds on its own: every dependency is a submodule in `deps/`.

- `deps/common/hosts-common`      CMake helpers (`cmake/multitap.cmake`), host_fs, logging
- `deps/common/frontends-common`  window, Vulkan, swapchain, ImGui glue (app_shell), settings, input
- `deps/common/retroui`           pixel font, themes, themed ImGui widgets, pad picture (shared with piccolo)
- third-party: `deps/SDL`, `deps/imgui`, `deps/libsamplerate`, `deps/gamemode` (frontends-common needs them)

**Sharing with piccolo:** what both need is moved out of piccolo into a
library, never copied: the look into retroui, frontend plumbing into
frontends-common. `src/touch_ui.*` starts here and moves to retroui once it
settles or piccolo wants it. A library change: edit it in its submodule (all
three on `main`), build and test here, commit and push THERE first, then
commit piccoletto's pointer.

## Ground rules

Same as piccolo's (its CLAUDE.md): follow `STYLE.md` (C++20 written like C: no
`auto`, lambdas, templates, inheritance, exceptions, own `#define`s; plain
structs + `module_verb()` functions; nothing allocates per frame); one feature
per commit; describe the plan before large work; third-party `deps/` are
read-only; commit and push when a feature works (libraries first, then the
pointers), big features on a branch until Andrés says to merge. Trailer:
`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
`CLAUDE.md` is committed; `docs/` stays untracked (notes go to `docs/design-notes.md`).

## Layout

```
CMakeLists.txt     options, SDL, Vulkan, imgui, the libraries, piccoletto
src/main.cpp            the app: window, files (settings/piccoletto.cfg, layouts.cfg), screens (NavAction),
                        the ☰ sheet, ROTATE, back key; for testing: --screen home|pair|gamepad|remote_play|
                        devices|settings, --landscape, --aspect W:H (the stream's shape), --edit
src/touch_fingers.*     every finger (SDL finger events; the mouse on the desktop), for the pads and the pinch
src/real_gamepad.*      gamepads connected to the phone: light the pad's parts, hide REMOTE PLAY's touch controls
src/haptics.*           VIBRATE ON PRESS (the phone's vibrator through SDL haptics; nothing on the desktop)
src/touch_ui.*          flat touch widgets: screen / overlay, text (normal / big), rects, lamps, touch areas,
                        buttons (normal, primary, ghost, card, plain, field)
src/screens.h           Screen, NavAction (what a tap asks for)
src/title_bar.*         the title bar's row: back (every screen but HOME), the host in the middle (pads), the
                        icons (+ = PAIR A DEVICE with the found count, rotate, ☰; + off on the pads); the pads'
                        port LEDs + ping in the bottom left
src/home_screen.*       HOME: card of the selected host with its granted modes; the other hosts in a sidebar,
                        closed to a tab at the right (landscape) or a bar at the bottom (portrait), up to 80%
src/pair_screen.*       PAIR: pick a found host (or an address), 6-digit code, keypad
src/pad_layout.*        pad parts (piccolo's pad picture blocks, lit while held) in groups, as dp tables fitted
                        to the screen; the user's moves / sizes / alpha per group, saved
src/pad_layouts.*       every layout's defaults: GAMEPAD portrait, landscape (one default shared by GAMEPAD and
                        REMOTE PLAY), REMOTE PLAY below the picture, overlay portrait (tall pictures: DS)
src/pad_editor.*        EDIT LAYOUT: drag groups, pinch / SIZE, OPACITY, SNAP (8 dp), RESET, DONE
src/app_settings.*      piccoletto.cfg: phone name, theme, language, vibrate, overlay opacity, quality
src/settings_screen.*   SETTINGS (HOME's ☰), drag to scroll
src/gamepad_screen.*    GAMEPAD: portrait (header + pad) and landscape (the pad picture's shape)
src/remote_play_screen.* REMOTE PLAY: the picture as big as the stream's aspect allows; the pad under it in
                        portrait if it fits, else an overlay over the picture (landscape, DS)
src/devices_screen.*    DEVICES: ports as cards, tap a device then MOVE HERE (placeholder devices)
src/menu_sheet.*        the ☰ sheet over GAMEPAD / REMOTE PLAY: switch, EDIT LAYOUT, ROTATE, LEAVE
android/           the APK without Gradle: build-apk.sh (libmain.so with hosts-common's
                   cmake/android-toolchain.cmake, SDL's Java activity + PiccolettoActivity),
                   AndroidManifest.xml (org.multitap.piccoletto, minSdk 28, targetSdk 35)
tools/             lsan-suppressions.txt
```

## Build, run, check

In a fresh clone or a new worktree the submodules are empty; then put the
three libraries on `main` (they're edited in place):

```sh
git submodule update --init --recursive
for d in deps/common/hosts-common deps/common/frontends-common deps/common/retroui; do git -C $d checkout main; done
```

```sh
git clone --recursive https://github.com/multitap-org/piccoletto.git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMULTITAP_WARNINGS_AS_ERRORS=ON
cmake --build build && ctest --test-dir build
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMULTITAP_WARNINGS_AS_ERRORS=ON -DMULTITAP_SANITIZE=ON
cmake --build build-asan && (cd build-asan && LSAN_OPTIONS=suppressions=$PWD/../tools/lsan-suppressions.txt ctest)
./build/piccoletto        # a phone-shaped window; normal builds also copy it to ~/.local/share/piccoletto/
android/build-apk.sh      # -> android/build/piccoletto.apk (adb install --no-incremental -r ...; adb logcat -s SDL)
adb shell "am start -S -n org.multitap.piccoletto/.PiccolettoActivity --es args '--screen gamepad'"
adb exec-out screencap -p > shot.png   # screenshots from the device (not spectacle)
```

Android: SDL's SDLActivity (SDL3 has no NativeActivity path), arm64 only.
The screen follows the phone (SDL's "full user", so the system rotation lock
is respected); ROTATE locks it to the other side. `MULTITAP_VULKAN_1_1=1`
tries frontends-common's Vulkan 1.1 path (older phones) on the desktop.

12 tests (the libraries'). Before calling a larger change done: warnings as
errors clean, ctest in both builds, no Vulkan validation messages, no
ASan/UBSan reports, resizing (portrait <-> landscape) and quitting work.

## Current state (2026-11-19)

- Desktop build (Linux): every screen (HOME, PAIR, GAMEPAD, REMOTE PLAY,
  DEVICES) in portrait and landscape with placeholder data, the ☰ sheet,
  ROTATE (swaps the window's sides; on a phone it will lock the orientation),
  back = Escape / Android's back. Nothing goes over the network yet. Pads:
  multi-touch (buttons by any free finger, a stick owned by its finger),
  a real gamepad lights them too, vibrate on press, pinch in EDIT LAYOUT.
- EDIT LAYOUT on every pad layout; REMOTE PLAY fits any aspect (overlay when the
  pad doesn't fit); SETTINGS (theme applies at once; language is stored, no
  translations yet; FORGET only logs).
- Not yet: opacity / quality while streaming, translations.
- Every screen, EDIT LAYOUT and SETTINGS are on main (merged 2026-11-19).
- Never switch branches in a checkout another session may be using (piccolo's
  main folder): use a worktree.
- Next: Android build (piccolo's `android` branch has the SDL setup; Andrés
  asked for NativeActivity, but SDL3 on Android runs through its SDLActivity:
  to settle), the GAMEPAD screen, pairing (needs the piccolo side), then
  REMOTE PLAY, DEVICES, settings (theme).

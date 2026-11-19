# CLAUDE.md — piccoletto

Working brief for Claude Code sessions in this repo. Read this first, then
`STYLE.md` (mandatory for all code). The design record lives in piccolo's
`docs/design-notes.md` ("piccoletto, the companion app"); the mockups are at
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
`CLAUDE.md` is committed; `docs/` stays untracked.

## Layout

```
CMakeLists.txt     options, SDL, Vulkan, imgui, the libraries, piccoletto
src/main.cpp       the app: window, theme, the current screen
src/touch_ui.*     flat touch widgets: screen, text (normal / big), rects, buttons (normal, primary, ghost, card, plain)
src/home_screen.*  HOME: card of the selected host with its granted modes, other hosts as rows, PAIR A DEVICE
tools/             lsan-suppressions.txt
```

## Build, run, check

```sh
git clone --recursive https://github.com/multitap-org/piccoletto.git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMULTITAP_WARNINGS_AS_ERRORS=ON
cmake --build build && ctest --test-dir build
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMULTITAP_WARNINGS_AS_ERRORS=ON -DMULTITAP_SANITIZE=ON
cmake --build build-asan && (cd build-asan && LSAN_OPTIONS=suppressions=$PWD/../tools/lsan-suppressions.txt ctest)
./build/piccoletto        # a phone-shaped window; normal builds also copy it to ~/.local/share/piccoletto/
```

12 tests (the libraries'). Before calling a larger change done: warnings as
errors clean, ctest in both builds, no Vulkan validation messages, no
ASan/UBSan reports, resizing (portrait <-> landscape) and quitting work.

## Current state (2026-11-19)

- Desktop build (Linux): window, BLUE theme, HOME with placeholder hosts
  (selecting a row puts it on the card; the buttons only log).
- Next: Android build (piccolo's `android` branch has the SDL setup; Andrés
  asked for NativeActivity, but SDL3 on Android runs through its SDLActivity:
  to settle), the GAMEPAD screen, pairing (needs the piccolo side), then
  REMOTE PLAY, DEVICES, settings (theme).

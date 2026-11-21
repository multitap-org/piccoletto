# piccoletto

piccolo's companion app. Use your phone as a gamepad for piccolo, play
piccolo's game on the phone (remote play), or move the connected devices
between ports. piccoletto never runs games itself: the game always runs on
piccolo.

Platforms: **Android** (APK, Android 9+; a legacy APK for Android 5+ with
OpenGL ES 2) and the desktop (Linux, Windows) for development.

## Features

### What's there

- **Pairing**: finds piccolos on the LAN, pairs with piccolo's 6-digit code,
  everything encrypted. FORGET on both sides. Ping, packet loss and a speed
  test with each host.
- **GAMEPAD**: the phone as a controller (a RetroPad): multi-touch buttons and
  sticks, vibration, the port's LED; a real gamepad connected to the phone
  works too. EDIT LAYOUT (move, resize, pinch) on every layout.
- **REMOTE PLAY**: piccolo's picture and sound on the phone, lossless, at any
  aspect ratio (the pad as an overlay when it doesn't fit). PACING (LATENCY,
  BALANCED, SMOOTH) tunes the host's frame delay; lost audio is filled in.
  Shaders: piccolo's curated presets through librashader (Vulkan).
- **GO HEADLESS / PLAY ON <host>**: take the game to the phone (piccolo hides
  behind a tray icon) and give it back.
- **DEVICES** (FULL access): the host's ports, who plays on which, move a device.
- **Quick menu**: a sidebar from the right (the see-through arrow or hold
  START): the host, the other mode, SHADER, PACING, EDIT LAYOUT, DEVICES,
  touch controls, ROTATE, LEAVE.
- Access level changes on piccolo apply at once, without reconnecting.
- Portrait and landscape, ROTATE as a lock, gamepad navigation everywhere,
  SETTINGS (theme, language).
- Vulkan, or OpenGL ES when Vulkan isn't usable.

### Not there yet

- Buttons for piccolo's actions (save / load state, pause, reset...).
- Across the internet (the beacon) and WebRTC; pairing by QR / link.
- REMOTE PLAY shaders on OpenGL ES; opacity and quality while streaming.
- Translations (the language setting is stored), HOTKEY mapping.
- iOS, the browser.
- Not tried yet: REMOTE PLAY on the legacy APK, a real Tegra 4.

## Build

```sh
git clone --recursive https://github.com/multitap-org/piccoletto.git
cmake -S piccoletto -B piccoletto/build -G Ninja
cmake --build piccoletto/build
./piccoletto/build/piccoletto
piccoletto/android/build-apk.sh [--legacy]   # the APK
```

Needs CMake 3.25+, Ninja, a C++20 compiler and the Vulkan loader + headers
(see piccolo's README for the Linux packages SDL wants). CI builds the APK,
the Flatpak and the Windows zip.

MIT License (see LICENSE).

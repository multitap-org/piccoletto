# piccoletto

piccolo's companion app. Use your phone as a gamepad for piccolo, play
piccolo's game on the phone (remote play), or move the connected devices
between ports. piccoletto never runs games itself.

Early days: the desktop build shows the home screen with placeholder hosts.

## Build

```sh
git clone --recursive https://github.com/multitap-org/piccoletto.git
cmake -S piccoletto -B piccoletto/build -G Ninja
cmake --build piccoletto/build
./piccoletto/build/piccoletto
```

Needs CMake 3.25+, Ninja, a C++20 compiler and the Vulkan loader + headers
(see piccolo's README for the Linux packages SDL wants).

MIT License (see LICENSE).

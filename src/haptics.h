#pragma once

// VIBRATE ON PRESS: a short buzz from the phone's vibrator (SDL lists it as
// a haptic device on Android). On the desktop there's none: nothing happens.

#include <cstdint>

struct SDL_Haptic;

struct Haptics
{
    SDL_Haptic* device = nullptr;
};

// Opens the vibrator if there is one (logs and carries on without it).
void haptics_open(Haptics* haptics);
void haptics_close(Haptics* haptics);

// One short buzz.
void haptics_buzz(const Haptics* haptics);

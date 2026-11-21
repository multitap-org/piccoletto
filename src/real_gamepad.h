#pragma once

// A real gamepad connected to the phone (Bluetooth, USB, a handheld's own
// controls): it's played as well as the touch pad (Andrés). On GAMEPAD its
// presses light the pad's parts; on REMOTE PLAY the touch controls hide
// while one is connected (unless shown from the ☰ sheet). Sending its input
// to the host comes with the network.
//
// Every connected gamepad counts, their presses together. Devices SDL takes
// for gamepads that aren't are left out: the known ones by SDL's ignore list
// (main.cpp: the SHIELD's "virtual-search"), any without face buttons here. The same ones drive ImGui's gamepad navigation on the other
// screens (main.cpp hands them to the ImGui backend). Mapped by
// position to piccolo's pad picture (SNES names): A right, B bottom, X top,
// Y left; HOTKEY is the guide button.

#include <cstdint>

struct SDL_Gamepad;
union SDL_Event;

constexpr int32_t REAL_GAMEPAD_MAX = 4;

struct RealGamepad
{
    SDL_Gamepad* open[REAL_GAMEPAD_MAX] = {};
    uint32_t     instance[REAL_GAMEPAD_MAX] = {};   // SDL_JoystickID of each
    int32_t      count   = 0;
    uint32_t     buttons = 0;          // SDL_GamepadButton bits, read by real_gamepad_update()
    uint32_t     pressed = 0;          // the ones that went down since the last update
    bool         changed = false;      // gamepads came or went (the caller clears it)
    float        left_x  = 0.0f;       // sticks, -1..1
    float        left_y  = 0.0f;
    float        right_x = 0.0f;
    float        right_y = 0.0f;
    float        left_trigger  = 0.0f; // 0..1
    float        right_trigger = 0.0f;
};

// Gamepads coming and going (SDL_EVENT_GAMEPAD_ADDED / _REMOVED).
void real_gamepad_event(RealGamepad* pads, const SDL_Event* event);
// Reads every open gamepad's state, once a frame.
void real_gamepad_update(RealGamepad* pads);
void real_gamepad_close(RealGamepad* pads);

bool real_gamepad_connected(const RealGamepad* pads);

struct RetroPad;

// The real gamepads as a RetroPad, by position (SNES names: A right, B
// bottom, X top, Y left; BACK is SELECT; triggers past half are L2 / R2).
void real_gamepad_retropad(const RealGamepad* pads, RetroPad* pad);

// A button (SDL_GamepadButton) down on any gamepad now / gone down since the last update.
bool real_gamepad_held(const RealGamepad* pads, int32_t button);
bool real_gamepad_pressed(const RealGamepad* pads, int32_t button);

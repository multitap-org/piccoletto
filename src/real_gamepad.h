#pragma once

// A real gamepad connected to the phone (Bluetooth, USB, a handheld's own
// controls): it's played as well as the touch pad (Andrés). On GAMEPAD its
// presses light the pad's parts; on REMOTE PLAY the touch controls hide
// while one is connected (unless shown from the ☰ sheet). Sending its input
// to the host comes with the network.
//
// Every connected gamepad counts, their presses together. Mapped by
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

// Whether the pad part with this id ("##a", "##lstick"...) is pressed on a
// real gamepad; for a stick, also where it is (-1..1).
bool real_gamepad_part_held(const RealGamepad* pads, const char* part_id, float* stick_x, float* stick_y);

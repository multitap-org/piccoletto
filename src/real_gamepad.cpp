#include "real_gamepad.h"

#include <SDL3/SDL.h>

// A stick counts as held past this (so a resting stick doesn't light), a
// trigger as pressed past half way.
constexpr float STICK_DEADZONE    = 0.2f;
constexpr float TRIGGER_THRESHOLD = 0.5f;

// Pad part ids (pad_layouts.cpp) to SDL's buttons, by position.
struct PartButton
{
    const char*       id;
    SDL_GamepadButton button;
};

static const PartButton PART_BUTTONS[] = {
    {"##a", SDL_GAMEPAD_BUTTON_EAST},
    {"##b", SDL_GAMEPAD_BUTTON_SOUTH},
    {"##x", SDL_GAMEPAD_BUTTON_NORTH},
    {"##y", SDL_GAMEPAD_BUTTON_WEST},
    {"##l", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"##r", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"##up", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"##down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"##left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"##right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"##select", SDL_GAMEPAD_BUTTON_BACK},
    {"##start", SDL_GAMEPAD_BUTTON_START},
    {"##hotkey", SDL_GAMEPAD_BUTTON_GUIDE},
};
constexpr int32_t PART_BUTTON_COUNT = static_cast<int32_t>(sizeof(PART_BUTTONS) / sizeof(PART_BUTTONS[0]));

static void add(RealGamepad* pads, SDL_JoystickID id)
{
    if (pads->count >= REAL_GAMEPAD_MAX)
    {
        return;
    }
    for (int32_t i = 0; i < pads->count; i++)
    {
        if (pads->instance[i] == id)
        {
            return;
        }
    }
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (gamepad == nullptr)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Can't open gamepad %u: %s", static_cast<unsigned>(id), SDL_GetError());
        return;
    }
    pads->open[pads->count] = gamepad;
    pads->instance[pads->count] = id;
    pads->count++;
    SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(gamepad));
}

static void remove(RealGamepad* pads, SDL_JoystickID id)
{
    for (int32_t i = 0; i < pads->count; i++)
    {
        if (pads->instance[i] != id)
        {
            continue;
        }
        SDL_Log("Gamepad disconnected: %s", SDL_GetGamepadName(pads->open[i]));
        SDL_CloseGamepad(pads->open[i]);
        for (int32_t j = i + 1; j < pads->count; j++)
        {
            pads->open[j - 1] = pads->open[j];
            pads->instance[j - 1] = pads->instance[j];
        }
        pads->count--;
        return;
    }
}

void real_gamepad_event(RealGamepad* pads, const SDL_Event* event)
{
    if (event->type == SDL_EVENT_GAMEPAD_ADDED)
    {
        add(pads, event->gdevice.which);
    }
    else if (event->type == SDL_EVENT_GAMEPAD_REMOVED)
    {
        remove(pads, event->gdevice.which);
    }
}

static float axis(SDL_Gamepad* gamepad, SDL_GamepadAxis which)
{
    return static_cast<float>(SDL_GetGamepadAxis(gamepad, which)) / 32767.0f;
}

// The value farther from rest of the two.
static float stronger(float a, float b)
{
    return SDL_fabsf(b) > SDL_fabsf(a) ? b : a;
}

void real_gamepad_update(RealGamepad* pads)
{
    pads->buttons = 0;
    pads->left_x = 0.0f;
    pads->left_y = 0.0f;
    pads->right_x = 0.0f;
    pads->right_y = 0.0f;
    pads->left_trigger = 0.0f;
    pads->right_trigger = 0.0f;
    for (int32_t i = 0; i < pads->count; i++)
    {
        SDL_Gamepad* gamepad = pads->open[i];
        for (int32_t b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 32; b++)
        {
            if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(b)))
            {
                pads->buttons |= 1u << static_cast<uint32_t>(b);
            }
        }
        pads->left_x = stronger(pads->left_x, axis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
        pads->left_y = stronger(pads->left_y, axis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
        pads->right_x = stronger(pads->right_x, axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
        pads->right_y = stronger(pads->right_y, axis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
        pads->left_trigger = SDL_max(pads->left_trigger, axis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        pads->right_trigger = SDL_max(pads->right_trigger, axis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    }
}

void real_gamepad_close(RealGamepad* pads)
{
    for (int32_t i = 0; i < pads->count; i++)
    {
        SDL_CloseGamepad(pads->open[i]);
        pads->open[i] = nullptr;
    }
    pads->count = 0;
}

bool real_gamepad_connected(const RealGamepad* pads)
{
    return pads->count > 0;
}

static bool button(const RealGamepad* pads, SDL_GamepadButton which)
{
    return (pads->buttons & (1u << static_cast<uint32_t>(which))) != 0;
}

static bool stick(float x, float y, bool pressed, float* stick_x, float* stick_y)
{
    *stick_x = x;
    *stick_y = y;
    return pressed || SDL_fabsf(x) > STICK_DEADZONE || SDL_fabsf(y) > STICK_DEADZONE;
}

bool real_gamepad_part_held(const RealGamepad* pads, const char* part_id, float* stick_x, float* stick_y)
{
    *stick_x = 0.0f;
    *stick_y = 0.0f;
    if (pads->count == 0)
    {
        return false;
    }
    if (SDL_strcmp(part_id, "##l2") == 0)
    {
        return pads->left_trigger > TRIGGER_THRESHOLD;
    }
    if (SDL_strcmp(part_id, "##r2") == 0)
    {
        return pads->right_trigger > TRIGGER_THRESHOLD;
    }
    if (SDL_strcmp(part_id, "##lstick") == 0)
    {
        return stick(pads->left_x, pads->left_y, button(pads, SDL_GAMEPAD_BUTTON_LEFT_STICK), stick_x, stick_y);
    }
    if (SDL_strcmp(part_id, "##rstick") == 0)
    {
        return stick(pads->right_x, pads->right_y, button(pads, SDL_GAMEPAD_BUTTON_RIGHT_STICK), stick_x, stick_y);
    }
    for (int32_t i = 0; i < PART_BUTTON_COUNT; i++)
    {
        if (SDL_strcmp(part_id, PART_BUTTONS[i].id) == 0)
        {
            return button(pads, PART_BUTTONS[i].button);
        }
    }
    return false;
}

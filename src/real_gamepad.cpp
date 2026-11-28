#include "real_gamepad.h"

#include <SDL3/SDL.h>

#include "input_sdl.h"
#include "retropad.h"

// A trigger counts as pressed past half way; the sticks go to the host as they are.
constexpr float TRIGGER_THRESHOLD = 0.5f;

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
    // The same tests as piccolo's router (frontends-common's input_sdl).
    bool no_face_buttons = !input_sdl_has_face_buttons(gamepad);
    if (no_face_buttons || input_sdl_axes_pinned(gamepad))
    {
        SDL_Log("Not a gamepad (%s), left out: %s", no_face_buttons ? "no face buttons" : "its axes are pinned",
                SDL_GetGamepadName(gamepad));
        SDL_CloseGamepad(gamepad);
        return;
    }
    pads->open[pads->count] = gamepad;
    pads->instance[pads->count] = id;
    pads->count++;
    pads->changed = true;
    SDL_Log("Gamepad connected: %s (vendor %04x product %04x, type %d, sticks %d)", SDL_GetGamepadName(gamepad),
            static_cast<unsigned>(SDL_GetGamepadVendor(gamepad)), static_cast<unsigned>(SDL_GetGamepadProduct(gamepad)),
            static_cast<int>(SDL_GetGamepadType(gamepad)), SDL_GamepadHasAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) ? 1 : 0);
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
        pads->changed = true;
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
    uint32_t before = pads->buttons;
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
    pads->pressed = pads->buttons & ~before;
}

bool real_gamepad_held(const RealGamepad* pads, int32_t button)
{
    return button >= 0 && button < 32 && (pads->buttons & (1u << static_cast<uint32_t>(button))) != 0;
}

bool real_gamepad_pressed(const RealGamepad* pads, int32_t button)
{
    return button >= 0 && button < 32 && (pads->pressed & (1u << static_cast<uint32_t>(button))) != 0;
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

void real_gamepad_retropad(const RealGamepad* pads, RetroPad* pad)
{
    *pad = RetroPad{};
    if (pads->count == 0)
    {
        return;
    }
    struct ButtonMap
    {
        SDL_GamepadButton sdl;
        RetroPadButton    retro;
    };
    static const ButtonMap MAP[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, RetroPadButton::b},          {SDL_GAMEPAD_BUTTON_EAST, RetroPadButton::a},
        {SDL_GAMEPAD_BUTTON_WEST, RetroPadButton::y},           {SDL_GAMEPAD_BUTTON_NORTH, RetroPadButton::x},
        {SDL_GAMEPAD_BUTTON_BACK, RetroPadButton::select},      {SDL_GAMEPAD_BUTTON_START, RetroPadButton::start},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, RetroPadButton::up},       {SDL_GAMEPAD_BUTTON_DPAD_DOWN, RetroPadButton::down},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, RetroPadButton::left},   {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RetroPadButton::right},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RetroPadButton::l},  {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RetroPadButton::r},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, RetroPadButton::l3},    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, RetroPadButton::r3},
    };
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
    {
        if (button(pads, MAP[i].sdl))
        {
            retropad_press(pad, MAP[i].retro);
        }
    }
    if (pads->left_trigger > TRIGGER_THRESHOLD)
    {
        retropad_press(pad, RetroPadButton::l2);
    }
    if (pads->right_trigger > TRIGGER_THRESHOLD)
    {
        retropad_press(pad, RetroPadButton::r2);
    }
    pad->analog[0][0] = retropad_axis(pads->left_x);
    pad->analog[0][1] = retropad_axis(pads->left_y);
    pad->analog[1][0] = retropad_axis(pads->right_x);
    pad->analog[1][1] = retropad_axis(pads->right_y);
}

void real_gamepad_keyboard_retropad(bool typing, RetroPad* pad)
{
    *pad = RetroPad{};
    if (typing)
    {
        return;
    }
    struct KeyMap
    {
        SDL_Scancode   key;
        RetroPadButton retro;
    };
    static const KeyMap MAP[] = {
        {SDL_SCANCODE_UP, RetroPadButton::up},       {SDL_SCANCODE_DOWN, RetroPadButton::down},
        {SDL_SCANCODE_LEFT, RetroPadButton::left},   {SDL_SCANCODE_RIGHT, RetroPadButton::right},
        {SDL_SCANCODE_X, RetroPadButton::a},         {SDL_SCANCODE_Z, RetroPadButton::b},
        {SDL_SCANCODE_S, RetroPadButton::x},         {SDL_SCANCODE_A, RetroPadButton::y},
        {SDL_SCANCODE_Q, RetroPadButton::l},         {SDL_SCANCODE_W, RetroPadButton::r},
        {SDL_SCANCODE_E, RetroPadButton::l2},        {SDL_SCANCODE_R, RetroPadButton::r2},
        {SDL_SCANCODE_RETURN, RetroPadButton::start}, {SDL_SCANCODE_RSHIFT, RetroPadButton::select},
    };
    const bool* keys = SDL_GetKeyboardState(nullptr);
    for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
    {
        if (keys[MAP[i].key])
        {
            retropad_press(pad, MAP[i].retro);
        }
    }
}

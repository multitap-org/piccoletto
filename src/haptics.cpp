#include "haptics.h"

#include <SDL3/SDL.h>

constexpr float    BUZZ_STRENGTH = 0.6f;
constexpr uint32_t BUZZ_MS       = 20;

void haptics_open(Haptics* haptics)
{
#if defined(SDL_PLATFORM_ANDROID)
    if (!SDL_InitSubSystem(SDL_INIT_HAPTIC))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No haptics: %s", SDL_GetError());
        return;
    }
    int count = 0;
    SDL_HapticID* ids = SDL_GetHaptics(&count);
    if (ids != nullptr && count > 0)
    {
        haptics->device = SDL_OpenHaptic(ids[0]);
    }
    SDL_free(ids);
    if (haptics->device == nullptr || !SDL_InitHapticRumble(haptics->device))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No vibrator: VIBRATE ON PRESS does nothing (%s)", SDL_GetError());
        haptics_close(haptics);
        return;
    }
    SDL_Log("Vibrator: %s", SDL_GetHapticName(haptics->device));
#else
    // Desktop: a rumbling gamepad isn't the phone in the hand.
    (void)haptics;
#endif
}

void haptics_close(Haptics* haptics)
{
    if (haptics->device != nullptr)
    {
        SDL_CloseHaptic(haptics->device);
        haptics->device = nullptr;
    }
}

void haptics_buzz(const Haptics* haptics)
{
    if (haptics->device != nullptr)
    {
        SDL_PlayHapticRumble(haptics->device, BUZZ_STRENGTH, BUZZ_MS);
    }
}

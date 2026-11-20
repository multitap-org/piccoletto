#pragma once

// piccoletto's own settings (settings/piccoletto.cfg), changed in SETTINGS.

#include <cstdint>

#include "retro_ui.h"

struct SettingsFile;

constexpr int32_t PHONE_NAME_SIZE = 32;

enum class AppLanguage
{
    english,
    spanish,
    count,
};

// REMOTE PLAY's pacing (like Moonlight's): latency against smoothness. Only
// the choice is saved; what it sets (frames in flight, the picture queue) is
// the app's own and applied only while REMOTE PLAY is open.
enum class StreamPacing
{
    latency,    // always the newest picture, 1 frame in flight, the next frame starts late
    balanced,   // the newest picture, 2 frames in flight
    smooth,     // pictures in order, one per frame (a queue of up to 3), 2 frames in flight
    count,
};

struct AppSettings
{
    char          phone_name[PHONE_NAME_SIZE] = "MY PHONE";   // how piccolo shows this phone
    RetroUiTheme  theme           = RetroUiTheme::blue;
    AppLanguage   language        = AppLanguage::english;     // no translations yet
    bool          vibrate         = true;                     // a short buzz on every press
    float         overlay_opacity = 0.6f;                     // overlays' default; adjustable while streaming
    StreamPacing  pacing          = StreamPacing::balanced;   // REMOTE PLAY's latency against smoothness
    int32_t       shader          = 0;                        // REMOTE PLAY's shader (shader_presets.h; 0 = none)
};

void app_settings_load(AppSettings* settings, const SettingsFile* file);
void app_settings_save(const AppSettings* settings, SettingsFile* file);

const char* app_theme_name(RetroUiTheme theme);   // "BLUE"...

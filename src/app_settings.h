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

enum class StreamQuality
{
    latency,    // the lowest latency the network allows
    balanced,
    quality,    // the best picture, more latency
    count,
};

struct AppSettings
{
    char          phone_name[PHONE_NAME_SIZE] = "MY PHONE";   // how piccolo shows this phone
    RetroUiTheme  theme           = RetroUiTheme::blue;
    AppLanguage   language        = AppLanguage::english;     // no translations yet
    bool          vibrate         = true;                     // a short buzz on every press
    float         overlay_opacity = 0.6f;                     // overlays' default; adjustable while streaming
    StreamQuality quality         = StreamQuality::balanced;  // the default; adjustable while streaming
};

void app_settings_load(AppSettings* settings, const SettingsFile* file);
void app_settings_save(const AppSettings* settings, SettingsFile* file);

const char* app_theme_name(RetroUiTheme theme);   // "BLUE"...

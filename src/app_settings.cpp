#include "app_settings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "settings_file.h"
#include "shader_presets.h"

static const char* const THEME_KEYS[] = {"blue", "purple", "zsnes", "black"};
static const char* const THEME_NAMES[] = {"BLUE", "PURPLE", "ZSNES", "BLACK"};
static const char* const LANGUAGE_KEYS[] = {"en", "es"};
static const char* const PACING_KEYS[] = {"latency", "balanced", "smooth"};

const char* app_theme_name(RetroUiTheme theme)
{
    int32_t index = static_cast<int32_t>(theme);
    return index >= 0 && index < static_cast<int32_t>(RetroUiTheme::count) ? THEME_NAMES[index] : "?";
}

// The index of `value` in `keys`, or `fallback`.
static int32_t find_key(const char* value, const char* const* keys, int32_t count, int32_t fallback)
{
    if (value == nullptr)
    {
        return fallback;
    }
    for (int32_t i = 0; i < count; i++)
    {
        if (std::strcmp(value, keys[i]) == 0)
        {
            return i;
        }
    }
    return fallback;
}

void app_settings_load(AppSettings* settings, const SettingsFile* file)
{
    *settings = AppSettings{};
    const char* name = settings_file_get(file, "", "phone_name");
    if (name != nullptr && name[0] != '\0')
    {
        std::snprintf(settings->phone_name, sizeof(settings->phone_name), "%s", name);
    }
    settings->theme = static_cast<RetroUiTheme>(
        find_key(settings_file_get(file, "", "theme"), THEME_KEYS, static_cast<int32_t>(RetroUiTheme::count), 0));
    settings->language = static_cast<AppLanguage>(
        find_key(settings_file_get(file, "", "language"), LANGUAGE_KEYS, static_cast<int32_t>(AppLanguage::count), 0));
    const char* vibrate = settings_file_get(file, "", "vibrate");
    settings->vibrate = vibrate == nullptr || std::strcmp(vibrate, "false") != 0;
    const char* opacity = settings_file_get(file, "", "overlay_opacity");
    if (opacity != nullptr)
    {
        float value = std::strtof(opacity, nullptr);
        if (std::isfinite(value) && value >= 0.1f && value <= 1.0f)
        {
            settings->overlay_opacity = value;
        }
    }
    settings->pacing = static_cast<StreamPacing>(
        find_key(settings_file_get(file, "", "remote_play_pacing"), PACING_KEYS, static_cast<int32_t>(StreamPacing::count), 1));
    const char* shader = settings_file_get(file, "", "remote_play_shader");
    settings->shader = shader != nullptr ? shader_preset_find(shader) : 0;
}

void app_settings_save(const AppSettings* settings, SettingsFile* file)
{
    settings_file_set(file, "", "phone_name", settings->phone_name);
    settings_file_set(file, "", "theme", THEME_KEYS[static_cast<int32_t>(settings->theme)]);
    settings_file_set(file, "", "language", LANGUAGE_KEYS[static_cast<int32_t>(settings->language)]);
    settings_file_set(file, "", "vibrate", settings->vibrate ? "true" : "false");
    char value[16];
    std::snprintf(value, sizeof(value), "%.2f", static_cast<double>(settings->overlay_opacity));
    settings_file_set(file, "", "overlay_opacity", value);
    settings_file_set(file, "", "remote_play_pacing", PACING_KEYS[static_cast<int32_t>(settings->pacing)]);
    settings_file_set(file, "", "remote_play_shader", shader_preset(settings->shader)->key);
}

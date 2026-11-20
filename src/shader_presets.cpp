#include "shader_presets.h"

#include <cstring>
#include <string>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "host_fs.h"

// Lightest first (see the header). Keys are paths under shaders/presets.
static const ShaderPreset PRESETS[] = {
    {"", "NONE"},
    {"handheld/zfast-lcd.slangp", "ZFAST LCD"},
    {"crt/zfast-crt.slangp", "ZFAST CRT"},
    {"handheld/dot.slangp", "DOT"},
    {"handheld/lcd3x.slangp", "LCD3X"},
    {"handheld/gameboy.slangp", "GAME BOY"},
    {"handheld/agb001.slangp", "GBA AGB-001"},
    {"handheld/sameboy-lcd.slangp", "SAMEBOY LCD"},
    {"crt/crt-potato-cool.slangp", "CRT POTATO"},
    {"crt/crt-easymode.slangp", "CRT EASYMODE"},
    {"crt/crt-aperture.slangp", "CRT APERTURE"},
    {"edge-smoothing/xbr/xbr-lv2.slangp", "XBR LV2"},
    {"edge-smoothing/xbrz/xbrz-freescale.slangp", "XBRZ"},
    {"edge-smoothing/hqx/hq4x.slangp", "HQ4X"},
    {"edge-smoothing/xbr/super-xbr.slangp", "SUPER XBR"},
    {"edge-smoothing/scalefx/scalefx.slangp", "SCALEFX"},
    {"crt/crt-guest-advanced-fast.slangp", "CRT GUEST FAST"},
    {"crt/crt-royale.slangp", "CRT ROYALE"},
};
constexpr int32_t PRESET_COUNT = static_cast<int32_t>(sizeof(PRESETS) / sizeof(PRESETS[0]));

int32_t shader_preset_count()
{
    return PRESET_COUNT;
}

const ShaderPreset* shader_preset(int32_t index)
{
    if (index < 0 || index >= PRESET_COUNT)
    {
        return &PRESETS[0];
    }
    return &PRESETS[index];
}

// The folder part of a path ("a/b/c.slang" -> "a/b").
static std::string parent_of(const std::string* path)
{
    size_t slash = path->rfind('/');
    return slash == std::string::npos ? std::string() : path->substr(0, slash);
}

bool shader_presets_unpack(const char* folder, const char* version)
{
    // The list in the APK (on Android a relative path reads the APK's assets).
    size_t list_size = 0;
    char* list = static_cast<char*>(SDL_LoadFile("shaders/presets.txt", &list_size));
    if (list == nullptr)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Shaders: no preset list in the APK (%s)", SDL_GetError());
        return false;
    }
    std::string wanted = std::string(version) + "\n" + std::string(list, list_size);
    std::string root = folder;
    std::string marker_path = root + "/presets.unpacked";
    size_t marker_size = 0;
    char* marker = static_cast<char*>(SDL_LoadFile(marker_path.c_str(), &marker_size));
    bool same = marker != nullptr && wanted == std::string(marker, marker_size);
    SDL_free(marker);
    if (same)
    {
        SDL_free(list);
        return true;
    }
    // One file per line.
    uint32_t copied = 0;
    bool ok = true;
    std::string text(list, list_size);
    SDL_free(list);
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string file = text.substr(start, end - start);
        start = end + 1;
        if (file.empty())
        {
            continue;
        }
        size_t size = 0;
        void* data = SDL_LoadFile(("shaders/presets/" + file).c_str(), &size);
        std::string target = root + "/presets/" + file;
        if (data == nullptr || !host_fs_make_directories(parent_of(&target).c_str()) ||
            !SDL_SaveFile(target.c_str(), data, size))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Shaders: can't copy %s (%s)", file.c_str(), SDL_GetError());
            ok = false;
        }
        else
        {
            copied++;
        }
        SDL_free(data);
    }
    if (ok)
    {
        SDL_SaveFile(marker_path.c_str(), wanted.data(), wanted.size());
    }
    SDL_Log("Shaders: %u preset files copied to %s/presets", copied, folder);
    return ok;
}

int32_t shader_preset_find(const char* key)
{
    for (int32_t i = 0; i < PRESET_COUNT; i++)
    {
        if (std::strcmp(PRESETS[i].key, key) == 0)
        {
            return i;
        }
    }
    return 0;
}

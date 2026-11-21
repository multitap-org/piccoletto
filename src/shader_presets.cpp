#include "shader_presets.h"

#include <cstring>

// Lightest first (see the header). Keys are paths under shaders/presets.
static const ShaderPreset SLANG_PRESETS[] = {
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
constexpr int32_t SLANG_PRESET_COUNT = static_cast<int32_t>(sizeof(SLANG_PRESETS) / sizeof(SLANG_PRESETS[0]));

// OpenGL ES, lightest first. Keys are paths under shaders/gl.
static const ShaderPreset GL_PRESETS[] = {
    {"", "NONE"},
    {"scanlines.glpreset", "SCANLINES"},
    {"lcd3x.glpreset", "LCD3X"},
    {"dot.glpreset", "DOT"},
    {"crt-mask.glpreset", "CRT MASK"},
};
constexpr int32_t GL_PRESET_COUNT = static_cast<int32_t>(sizeof(GL_PRESETS) / sizeof(GL_PRESETS[0]));

int32_t shader_preset_count(bool gl)
{
    return gl ? GL_PRESET_COUNT : SLANG_PRESET_COUNT;
}

const ShaderPreset* shader_preset(bool gl, int32_t index)
{
    const ShaderPreset* presets = gl ? GL_PRESETS : SLANG_PRESETS;
    if (index < 0 || index >= shader_preset_count(gl))
    {
        return &presets[0];
    }
    return &presets[index];
}

const char* shader_presets_folder(bool gl)
{
    return gl ? "gl" : "presets";
}

int32_t shader_preset_find(bool gl, const char* key)
{
    int32_t count = shader_preset_count(gl);
    for (int32_t i = 0; i < count; i++)
    {
        if (std::strcmp(shader_preset(gl, i)->key, key) == 0)
        {
            return i;
        }
    }
    return 0;
}

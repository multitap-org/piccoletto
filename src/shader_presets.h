#pragma once

// REMOTE PLAY's shaders (SETTINGS > REMOTE PLAY SHADER): piccolo's curated
// slang presets (deps/piccolo-shaders, each file under its own license), run
// on the phone over the exact picture the host streams, through
// frontends-common's shader_chain (librashader, loaded at run time; without
// it the picture is drawn as before). Listed lightest first: a phone's GPU
// (the SHIELD's Tegra X1) runs the light ones at full speed, the heavy CRTs
// may not keep up.
//
// Where they are: next to the program in shaders/presets (CMake copies them
// there; on Android build-apk.sh puts them in the APK and the app copies them
// out to its own folder, since librashader reads plain files).

#include <cstdint>

struct ShaderPreset
{
    const char* key;     // the file under shaders/presets ("" = none); what piccoletto.cfg keeps
    const char* label;   // what SETTINGS shows
};

int32_t shader_preset_count();
const ShaderPreset* shader_preset(int32_t index);
// The preset's index for a key (0, NONE, when it isn't known).
int32_t shader_preset_find(const char* key);

// Android: copies the presets out of the APK (assets shaders/presets, listed
// in shaders/presets.txt) into `folder`/presets, when that list or `version`
// changed since the last time (a marker file says what's there). False
// (logged) if it couldn't; the old copy, if any, stays.
bool shader_presets_unpack(const char* folder, const char* version);

#pragma once

// REMOTE PLAY's shaders (SETTINGS > REMOTE PLAY SHADER), run on the phone
// over the exact picture the host streams. Two lists, one per GPU path:
//
//   Vulkan:     piccolo's curated slang presets (deps/piccolo-shaders, each
//               file under its own license) through frontends-common's
//               shader_chain (librashader, loaded at run time; without it the
//               picture is drawn as before), in shaders/presets.
//   OpenGL ES:  the .glpreset presets (deps/piccolo-shaders-legacy) through
//               gl_shader_chain (the legacy APK, the browser, the GL
//               fallback), in shaders/gl.
//
// Listed lightest first: a phone's GPU (the SHIELD's Tegra X1) runs the light
// ones at full speed, the heavy CRTs may not keep up.
//
// Where they are: next to the program in shaders/ (CMake copies them there;
// on Android build-apk.sh puts them in the APK and the app copies them out to
// its own folder with frontends-common's app_assets, since librashader reads
// plain files; the browser build preloads shaders/gl).

#include <cstdint>

struct ShaderPreset
{
    const char* key;     // the file under its folder ("" = none); what piccoletto.cfg keeps
    const char* label;   // what SETTINGS shows
};

// `gl`: the OpenGL ES list (.glpreset), else the Vulkan one (.slangp).
int32_t shader_preset_count(bool gl);
const ShaderPreset* shader_preset(bool gl, int32_t index);
// The preset's index for a key (0, NONE, when it isn't known).
int32_t shader_preset_find(bool gl, const char* key);
// The list's folder under shaders/: "presets" (Vulkan) or "gl".
const char* shader_presets_folder(bool gl);

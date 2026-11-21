#!/bin/sh
# Builds piccoletto for the browser (WebAssembly, Emscripten + SDL3, WebGL 2
# through frontends-common's OpenGL ES path), the same recipe as piccolo's
# web/build.sh:
#
#   web/build.sh   -> web/build/wasm/piccoletto.{js,wasm}
#
# piccolo's web/package.sh puts it in the web zip next to piccolo, as
# piccoletto/ (its page is web/page/index.html here; the shared scripts and
# sw.js are frontends-common's web/).
#
# Needs Emscripten in the environment (emcc on PATH), the version piccolo pins:
#   git clone https://github.com/emscripten-core/emsdk ~/emsdk
#   ~/emsdk/emsdk install 6.0.13 && ~/emsdk/emsdk activate 6.0.13
#   . ~/emsdk/emsdk_env.sh
#
# Overridable from the environment: BUILD_TYPE (default Release),
# VULKAN_HEADERS (default /usr/include).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
build="$here/build"
wasm_build="$build/wasm"
BUILD_TYPE=${BUILD_TYPE:-Release}

if ! command -v emcmake >/dev/null 2>&1; then
    echo "web/build.sh: emcmake not found; source emsdk_env.sh first (see the top of this script)" >&2
    exit 1
fi

# Vulkan's headers only (no library): frontends-common's headers still name
# Vulkan types with Vulkan off, and Emscripten has no vulkan.h. The system's
# headers, seen through a folder that holds nothing else (as piccolo's build).
vulkan_headers=${VULKAN_HEADERS:-/usr/include}
if [ ! -f "$vulkan_headers/vulkan/vulkan.h" ]; then
    echo "web/build.sh: no vulkan/vulkan.h in $vulkan_headers (install the Vulkan headers or set VULKAN_HEADERS)" >&2
    exit 1
fi
vulkan_include="$build/vulkan-include"
mkdir -p "$vulkan_include"
ln -sfn "$vulkan_headers/vulkan" "$vulkan_include/vulkan"
ln -sfn "$vulkan_headers/vk_video" "$vulkan_include/vk_video"

# -pthread on every file: a threaded wasm needs all its objects built with
# atomics (SDL's own build adds it with SDL_PTHREADS).
emcmake cmake -S "$root" -B "$wasm_build" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_FLAGS="-pthread" -DCMAKE_CXX_FLAGS="-pthread -isystem $vulkan_include" \
    -DSDL_PTHREADS=ON \
    -DMULTITAP_GPU_VULKAN=OFF -DMULTITAP_BUILD_TESTS=OFF -DMULTITAP_PICCOLETTO_DIR=
cmake --build "$wasm_build" --target piccoletto

echo "web/build.sh: $wasm_build/piccoletto.js + piccoletto.wasm"

# piccoletto: piccolo's companion app. The phone as a gamepad for piccolo,
# remote play (piccolo streams, the phone plays) and managing devices. It never
# runs games itself. Self-contained: every dependency is a submodule in deps/
# (third-party ones, and multitap's libraries in deps/common). Build:
#   git clone --recursive https://github.com/multitap-org/piccoletto.git
#   cmake -S piccoletto -B piccoletto/build -G Ninja && cmake --build piccoletto/build

cmake_minimum_required(VERSION 3.25)

# ccache as a compiler launcher when it's installed and runs (see piccolo).
find_program(MULTITAP_CCACHE ccache)
if(MULTITAP_CCACHE)
    execute_process(COMMAND "${MULTITAP_CCACHE}" --version
                    RESULT_VARIABLE _ccache_result OUTPUT_QUIET ERROR_QUIET)
    if(_ccache_result EQUAL 0)
        set(CMAKE_C_COMPILER_LAUNCHER   "${MULTITAP_CCACHE}" CACHE STRING "")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${MULTITAP_CCACHE}" CACHE STRING "")
    else()
        message(WARNING "ccache (${MULTITAP_CCACHE}) doesn't run; building without it")
    endif()
endif()

project(piccoletto LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options (the MULTITAP_ names are the project's: its libraries read them)
# ---------------------------------------------------------------------------
option(MULTITAP_SYSTEM_SDL3        "Use the system SDL3 instead of deps/SDL" OFF)
option(MULTITAP_WARNINGS_AS_ERRORS "Treat warnings in our own code as errors"    OFF)
option(MULTITAP_SANITIZE           "Build our own code with ASan + UBSan"         OFF)
option(MULTITAP_BUILD_TESTS        "Build tests and register CTest tests"         ON)

# Where builds are copied so they're easy to find and run, like piccolo's:
# Linux $XDG_DATA_HOME/piccoletto (default ~/.local/share/piccoletto), macOS
# ~/Library/Application Support/piccoletto, Windows %USERPROFILE%/piccoletto.
# Not from sanitizer builds (they need the ASan runtime). Empty = don't copy.
if(MULTITAP_SANITIZE)
    set(_programs_base "")
elseif(WIN32)
    set(_programs_base "$ENV{USERPROFILE}")
elseif(APPLE)
    set(_programs_base "$ENV{HOME}/Library/Application Support")
elseif(DEFINED ENV{XDG_DATA_HOME} AND IS_ABSOLUTE "$ENV{XDG_DATA_HOME}")
    set(_programs_base "$ENV{XDG_DATA_HOME}")
else()
    set(_programs_base "$ENV{HOME}/.local/share")
endif()
set(_piccoletto_dir_default "")
if(_programs_base)
    set(_piccoletto_dir_default "${_programs_base}/piccoletto")
endif()
set(MULTITAP_PICCOLETTO_DIR "${_piccoletto_dir_default}" CACHE PATH "Copy piccoletto here after building (empty = don't)")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Submodule check
# ---------------------------------------------------------------------------
foreach(_dep imgui/imgui.cpp libsamplerate/src/samplerate.c gamemode/lib/gamemode_client.h enet/host.c
             monocypher/src/monocypher.c
             common/hosts-common/CMakeLists.txt common/frontends-common/CMakeLists.txt
             common/retroui/CMakeLists.txt)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/${_dep}")
        message(FATAL_ERROR "deps/${_dep} is missing. Run: git submodule update --init --recursive")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Third-party dependencies (built with their own defaults, not our warnings)
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

if(MULTITAP_SYSTEM_SDL3)
    find_package(SDL3 CONFIG REQUIRED)
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL/CMakeLists.txt")
        message(FATAL_ERROR "deps/SDL is empty. Run: git submodule update --init --recursive")
    endif()
    set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
    add_subdirectory(deps/SDL EXCLUDE_FROM_ALL SYSTEM)
endif()

# Dear ImGui core + SDL3 platform backend + Vulkan renderer backend, straight
# from the submodule (nothing copied or patched).
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)

if(MULTITAP_BUILD_TESTS)
    enable_testing()
endif()

# ---------------------------------------------------------------------------
# multitap's libraries (submodules). hosts-common first: it brings the
# project's CMake helpers (multitap_set_project_flags, ...).
# ---------------------------------------------------------------------------
set(MULTITAP_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps")
add_subdirectory(deps/common/hosts-common)
add_subdirectory(deps/common/frontends-common)
add_subdirectory(deps/common/retroui)

# ---------------------------------------------------------------------------
# piccoletto
# ---------------------------------------------------------------------------
set(PICCOLETTO_SOURCES
    src/main.cpp
    src/touch_ui.cpp
    src/home_screen.cpp
    src/menu_sheet.cpp
    src/pad_layout.cpp
    src/gamepad_screen.cpp
    src/remote_play_screen.cpp
    src/pair_screen.cpp
    src/devices_screen.cpp
    src/pad_layouts.cpp
    src/pad_editor.cpp
    src/app_settings.cpp
    src/settings_screen.cpp
    src/title_bar.cpp
    src/touch_fingers.cpp
    src/haptics.cpp
    src/real_gamepad.cpp
    src/hosts.cpp
    src/retropad.cpp
)
if(ANDROID)
    # Android (hosts-common's cmake/android-toolchain.cmake, android/build-apk.sh):
    # the same piccoletto as libmain.so, which SDL's Java activity loads and
    # whose SDL_main (main.cpp's main, renamed by SDL_main.h) it calls.
    add_library(piccoletto SHARED ${PICCOLETTO_SOURCES})
    set_target_properties(piccoletto PROPERTIES
        OUTPUT_NAME "main"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")   # where build-apk.sh picks it up
    target_link_libraries(piccoletto PRIVATE android log)
else()
    add_executable(piccoletto ${PICCOLETTO_SOURCES})
endif()
target_link_libraries(piccoletto PRIVATE frontend_common frontend_net retroui)

set(PICCOLETTO_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PICCOLETTO_VERSION_BASE 0.1)
multitap_version(PICCOLETTO_VERSION ${PICCOLETTO_VERSION_BASE} "${CMAKE_CURRENT_SOURCE_DIR}")
multitap_write_version_header("${PICCOLETTO_GENERATED}" piccoletto PICCOLETTO_VERSION "${PICCOLETTO_VERSION}")
target_include_directories(piccoletto PRIVATE "${PICCOLETTO_GENERATED}")
message(STATUS "piccoletto ${PICCOLETTO_VERSION}")

multitap_set_project_flags(piccoletto)
if(NOT ANDROID)
    multitap_copy_after_build(piccoletto "${MULTITAP_PICCOLETTO_DIR}")
endif()

if(MULTITAP_BUILD_TESTS)
    # src/hosts.* against a host in the same process over loopback (pairing,
    # connecting, discovery). Its scratch folder is emptied first.
    add_executable(piccoletto_hosts_test tests/hosts_test.cpp src/hosts.cpp)
    target_include_directories(piccoletto_hosts_test PRIVATE src)
    target_link_libraries(piccoletto_hosts_test PRIVATE frontend_common frontend_net)
    target_sources(piccoletto_hosts_test PRIVATE src/retropad.cpp)
    multitap_set_project_flags(piccoletto_hosts_test)
    set(HOSTS_TEST_SCRATCH "${CMAKE_CURRENT_BINARY_DIR}/hosts_test")
    add_test(NAME piccoletto_hosts_clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${HOSTS_TEST_SCRATCH}")
    set_tests_properties(piccoletto_hosts_clean PROPERTIES FIXTURES_SETUP hosts_scratch)
    add_test(NAME piccoletto_hosts COMMAND piccoletto_hosts_test "${HOSTS_TEST_SCRATCH}")
    set_tests_properties(piccoletto_hosts PROPERTIES
                         FIXTURES_REQUIRED hosts_scratch
                         FAIL_REGULAR_EXPRESSION "ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:")
endif()

#pragma once

// REMOTE PLAY's picture: the newest frame the host streamed (hosts_take_frame,
// decoded on the network thread), uploaded to a texture in the core's own
// format (R5G6B5, A1R5G5B5 or B8G8R8A8: the CPU only copies, the texture unit
// expands it) and drawn with frontends-common's Picture (sharp bilinear:
// crisp pixels without uneven sizes at a scale that isn't whole), or through
// a shader preset (shader_chain: librashader, when it loaded) when one is
// chosen (SETTINGS > REMOTE PLAY SHADER).
//
// No queue: each frame of the app shows whatever came last, so the picture
// is never later than it has to be (a frame repeats or is skipped now and
// then, as the host's rate and the display's drift apart).
//
// Each frame of the app, in this order:
//   stream_view_update()   before the UI: takes a new frame, uploads it
//   stream_view_draw()     while building the UI (remote_play_screen)
//   stream_view_record()   after the UI, before presenting

#include <cstdint>
#include <vector>

#include "net_stream.h"
#include "picture.h"
#include "shader_chain.h"
#include "vk_texture.h"

struct AppShell;
struct Hosts;
struct TouchUi;

struct StreamView
{
    StreamingTexture     texture;
    uint64_t             texture_id = 0;     // ImGui's id for the texture
    FramePixelFormat     texture_format = FramePixelFormat::rgb565;
    Picture              picture;
    std::vector<uint8_t> pixels;             // the newest frame (reused)
    uint32_t             width = 0;
    uint32_t             height = 0;
    FramePixelFormat     format = FramePixelFormat::rgb565;
    uint32_t             frame = 0;          // the host's loop number
    NetStreamInfo        info;               // what it goes with
    bool                 has_picture = false;   // something was uploaded and can be drawn
    bool                 failed = false;     // the texture couldn't be made (logged once)
    // The shader preset (stage 1 of the chain; stage 0, piccolo's pass 0, isn't used).
    ShaderChain          chain;
    const ShaderLibrary* library = nullptr;  // nullptr / not loaded: no shaders
    bool                 use_chain = false;  // this frame draws the chain's output
};

// The preset to run (a .slangp path; "" = none). Loads it (waits for the GPU)
// only when it differs from the one loaded or last tried.
void stream_view_set_shader(StreamView* view, const ShaderLibrary* library, AppShell* shell, const char* preset_path);

// Takes the newest frame (if one came) and uploads it.
void stream_view_update(StreamView* view, Hosts* hosts, AppShell* shell);

// Draws the picture filling (x, y, width, height), in retro pixels (the
// caller fitted it to the stream's aspect). False when there's nothing to draw yet.
bool stream_view_draw(StreamView* view, const TouchUi* ui, AppShell* shell, float x, float y, float width, float height);

// Records the sharp-bilinear copy (after the upload, before presenting).
void stream_view_record(StreamView* view, AppShell* shell);

// Forgets the picture (leaving REMOTE PLAY): the next stream starts black.
void stream_view_clear(StreamView* view);

// Frees the texture (waits for the GPU).
void stream_view_destroy(StreamView* view, AppShell* shell);

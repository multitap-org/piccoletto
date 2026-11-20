#include "stream_view.h"

#include <cmath>

#include <SDL3/SDL_log.h>

#include "imgui.h"

#include "app_shell.h"
#include "debug_ui.h"
#include "hosts.h"
#include "touch_ui.h"
#include "viewport.h"

// The texture's size when it's made; a bigger frame makes a bigger one.
constexpr uint32_t START_CAPACITY_WIDTH = 512;
constexpr uint32_t START_CAPACITY_HEIGHT = 512;

static VkFormat vulkan_format(FramePixelFormat format)
{
    switch (format)
    {
    case FramePixelFormat::rgb565:   return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case FramePixelFormat::xrgb1555: return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case FramePixelFormat::xrgb8888: return VK_FORMAT_B8G8R8A8_UNORM;
    }
    return VK_FORMAT_B8G8R8A8_UNORM;
}

static void free_texture(StreamView* view, AppShell* shell)
{
    if (view->texture.image == VK_NULL_HANDLE && view->picture.image == VK_NULL_HANDLE)
    {
        return;
    }
    // The GPU may still be drawing the old one (frames in flight).
    vkDeviceWaitIdle(shell->vulkan.device);
    picture_destroy(&view->picture, &shell->vulkan);
    view->picture = Picture{};
    if (view->texture_id != 0)
    {
        debug_ui_remove_texture(view->texture_id);
        view->texture_id = 0;
    }
    streaming_texture_destroy(&view->texture, &shell->vulkan);
    view->texture = StreamingTexture{};
}

// A texture for this frame's format and size (a new one only when they change).
static bool ensure_texture(StreamView* view, AppShell* shell)
{
    bool fits = view->texture.image != VK_NULL_HANDLE && view->texture_format == view->format &&
                view->width <= view->texture.capacity_width && view->height <= view->texture.capacity_height;
    if (fits)
    {
        return true;
    }
    free_texture(view, shell);
    uint32_t width = view->width > START_CAPACITY_WIDTH ? view->width : START_CAPACITY_WIDTH;
    uint32_t height = view->height > START_CAPACITY_HEIGHT ? view->height : START_CAPACITY_HEIGHT;
    VkFormat format = vulkan_format(view->format);
    if (!streaming_texture_format_supported(&shell->vulkan, format) ||
        !streaming_texture_create_format(&view->texture, &shell->vulkan, width, height, format, true))
    {
        if (!view->failed)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "REMOTE PLAY: no texture for the stream's format (%d), %ux%u",
                         static_cast<int>(view->format), width, height);
        }
        view->failed = true;
        streaming_texture_destroy(&view->texture, &shell->vulkan);
        view->texture = StreamingTexture{};
        return false;
    }
    view->texture_format = view->format;
    view->texture_id = debug_ui_add_texture(view->texture.view);
    view->failed = false;
    return true;
}

void stream_view_update(StreamView* view, Hosts* hosts, AppShell* shell)
{
    if (!hosts_take_frame(hosts, &view->pixels, &view->width, &view->height, &view->format, &view->frame, &view->info))
    {
        return;
    }
    if (view->width == 0 || view->height == 0 || !ensure_texture(view, shell) || !app_shell_acquire(shell))
    {
        return;   // the next frame tries again
    }
    // Outside the rendering pass: the copy goes first in this frame's commands.
    if (streaming_texture_upload(&view->texture, renderer_command_buffer(&shell->renderer),
                                 renderer_frame_index(&shell->renderer), view->pixels.data(), view->width, view->height))
    {
        view->has_picture = true;
    }
}

void stream_view_set_shader(StreamView* view, const ShaderLibrary* library, AppShell* shell, const char* preset_path)
{
    view->library = library;
    if (library == nullptr || !library->loaded)
    {
        return;
    }
    ShaderStage* stage = &view->chain.stages[1];
    if (preset_path[0] == '\0')
    {
        if (stage->chain != nullptr)
        {
            shader_chain_clear(&view->chain, library, &shell->vulkan, 1);
        }
        stage->failed_path.clear();
        return;
    }
    if (stage->preset_path == preset_path || stage->failed_path == preset_path)
    {
        return;   // loaded, or it failed already (logged): not every frame
    }
    shader_chain_load(&view->chain, library, &shell->vulkan, 1, preset_path);
}

bool stream_view_draw(StreamView* view, const TouchUi* ui, AppShell* shell, float x, float y, float width, float height)
{
    view->picture.ready = false;
    view->use_chain = false;
    if (!view->has_picture || view->texture_id == 0 || !view->texture.has_content)
    {
        return false;
    }
    float scale = static_cast<float>(ui->scale);
    // The rectangle already has the stream's aspect: fill it.
    ViewportInput input;
    input.frame_width = view->texture.content_width;
    input.frame_height = view->texture.content_height;
    input.core_aspect = view->info.aspect;
    input.window_width = static_cast<uint32_t>(std::lround(width * scale));
    input.window_height = static_cast<uint32_t>(std::lround(height * scale));
    input.aspect = ViewportAspect::stretch;
    input.scaling = ViewportScaling::full;
    input.filter = ViewportFilter::sharp_bilinear;
    ViewportResult viewport;
    if (!viewport_compute(&input, &viewport))
    {
        return false;
    }
    const ImGuiViewport* screen = ImGui::GetMainViewport();
    // A shader preset when there's one and it can run; else the picture as is.
    if (view->library != nullptr && view->library->loaded && shader_chain_active(&view->chain) &&
        shader_chain_prepare(&view->chain, &shell->vulkan, &view->texture, &viewport))
    {
        view->use_chain = true;
        shader_chain_draw(&view->chain, ImGui::GetWindowDrawList(), screen->Pos.x + x * scale, screen->Pos.y + y * scale);
        return true;
    }
    if (!picture_prepare(&view->picture, &shell->vulkan, &view->texture, &viewport))
    {
        return false;
    }
    picture_draw(&view->picture, ImGui::GetWindowDrawList(), &view->texture, view->texture_id,
                 screen->Pos.x + x * scale, screen->Pos.y + y * scale);
    return true;
}

void stream_view_record(StreamView* view, AppShell* shell)
{
    if (view->use_chain && view->chain.ready && app_shell_acquire(shell))
    {
        shader_chain_record(&view->chain, view->library, renderer_command_buffer(&shell->renderer), &view->texture);
        return;
    }
    if (view->picture.ready && app_shell_acquire(shell))
    {
        picture_record(&view->picture, renderer_command_buffer(&shell->renderer), &view->texture);
    }
}

void stream_view_clear(StreamView* view)
{
    view->has_picture = false;
    view->picture.ready = false;
    view->use_chain = false;
    view->chain.ready = false;
    view->info = NetStreamInfo{};
    view->width = 0;
    view->height = 0;
}

void stream_view_destroy(StreamView* view, AppShell* shell)
{
    if (view->library != nullptr && view->library->loaded)
    {
        shader_chain_destroy(&view->chain, view->library, &shell->vulkan);
    }
    free_texture(view, shell);
    stream_view_clear(view);
}

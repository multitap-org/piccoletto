#include "stream_view.h"

#include <cmath>

#include <SDL3/SDL_log.h>

#include "imgui.h"

#include "app_shell.h"
#include "hosts.h"
#include "touch_ui.h"
#include "viewport.h"

// The texture's size when it's made; a bigger frame makes a bigger one.
constexpr uint32_t START_CAPACITY_WIDTH = 512;
constexpr uint32_t START_CAPACITY_HEIGHT = 512;

static void free_texture(StreamView* view, AppShell* shell)
{
    // Both wait for the GPU: it may still be drawing them (frames in flight).
    video_output_destroy(&view->output, view->library, shell);
    video_texture_destroy(&view->texture, shell);
}

// A texture for this frame's format and size (a new one only when they change).
static bool ensure_texture(StreamView* view, AppShell* shell)
{
    bool fits = video_texture_created(&view->texture) && view->texture.format == view->format &&
                view->width <= view->texture.capacity_width && view->height <= view->texture.capacity_height;
    if (fits)
    {
        return true;
    }
    // The texture goes; the shader chain stays (only its images follow the size).
    video_texture_destroy(&view->texture, shell);
    uint32_t width = view->width > START_CAPACITY_WIDTH ? view->width : START_CAPACITY_WIDTH;
    uint32_t height = view->height > START_CAPACITY_HEIGHT ? view->height : START_CAPACITY_HEIGHT;
    if (!video_texture_format_supported(shell, view->format) ||
        !video_texture_create(&view->texture, shell, width, height, view->format, true))
    {
        if (!view->failed)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "REMOTE PLAY: no texture for the stream's format (%d), %ux%u",
                         static_cast<int>(view->format), width, height);
        }
        view->failed = true;
        return false;
    }
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
    if (video_texture_upload(&view->texture, shell, view->pixels.data(), view->width, view->height))
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
    if (preset_path[0] == '\0')
    {
        // Also forgets a preset that failed, so choosing it again retries it.
        // An empty stage costs nothing to clear (no GPU wait).
        video_output_load_stage(&view->output, library, shell, 1, "");
        return;
    }
    if (video_output_stage_preset(&view->output, 1) == preset_path ||
        video_output_stage_failed_preset(&view->output, 1) == preset_path)
    {
        return;   // loaded, or it failed already (logged): not every frame
    }
    video_output_load_stage(&view->output, library, shell, 1, preset_path);
}

bool stream_view_draw(StreamView* view, const TouchUi* ui, AppShell* shell, float x, float y, float width, float height)
{
    video_output_skip(&view->output);
    view->drawn = false;
    if (!view->has_picture || view->texture.imgui_id == 0 || !view->texture.has_content)
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
    // A shader preset when there's one and it can run; else the picture as is.
    if (!video_output_prepare(&view->output, shell, &view->texture, &viewport))
    {
        return false;
    }
    const ImGuiViewport* screen = ImGui::GetMainViewport();
    video_output_draw(&view->output, ImGui::GetWindowDrawList(), &view->texture, screen->Pos.x + x * scale,
                      screen->Pos.y + y * scale);
    view->drawn = true;
    return true;
}

void stream_view_record(StreamView* view, AppShell* shell)
{
    if (view->drawn && app_shell_acquire(shell))
    {
        video_output_record(&view->output, view->library, shell, &view->texture);
    }
}

void stream_view_clear(StreamView* view)
{
    view->has_picture = false;
    video_output_skip(&view->output);
    view->drawn = false;
    view->info = NetStreamInfo{};
    view->width = 0;
    view->height = 0;
}

void stream_view_destroy(StreamView* view, AppShell* shell)
{
    free_texture(view, shell);
    stream_view_clear(view);
}

#include "pad_layout.h"

#include <cmath>

// The yellow of a lit part: the same in every theme, as in piccolo's pad picture.
constexpr uint32_t PART_LIT      = 0xFFFFDC50u;
constexpr uint32_t PART_LIT_TEXT = 0xFF000000u;

// The mockups put 2 dp in a retro pixel.
constexpr float DP_PER_RETRO_PIXEL = 2.0f;

PadFrame pad_frame_fit(const TouchUi* ui, float width, float height)
{
    float screen_width = touch_ui_width(ui);
    float screen_height = touch_ui_height(ui);
    float scale_x = screen_width / (width / DP_PER_RETRO_PIXEL);
    float scale_y = screen_height / (height / DP_PER_RETRO_PIXEL);
    float scale = scale_x < scale_y ? scale_x : scale_y;
    PadFrame frame = {};
    frame.dp_scale = scale / DP_PER_RETRO_PIXEL;
    frame.x = std::floor((screen_width - width * frame.dp_scale) * 0.5f);
    frame.y = std::floor((screen_height - height * frame.dp_scale) * 0.5f);
    return frame;
}

float pad_x(const PadFrame* frame, float dp)
{
    return frame->x + dp * frame->dp_scale;
}

float pad_y(const PadFrame* frame, float dp)
{
    return frame->y + dp * frame->dp_scale;
}

float pad_size(const PadFrame* frame, float dp)
{
    return dp * frame->dp_scale;
}

void pad_block(const TouchUi* ui, const PadFrame* frame, float x, float y, float width, float height, uint32_t argb)
{
    touch_ui_rect(ui, pad_x(frame, x), pad_y(frame, y), pad_size(frame, width), pad_size(frame, height), argb);
}

bool pad_button(const TouchUi* ui, const PadFrame* frame, const char* id, float x, float y, float width, float height,
                const char* label, TouchButton style, TouchText size)
{
    return touch_ui_button(ui, id, pad_x(frame, x), pad_y(frame, y), pad_size(frame, width), pad_size(frame, height), label,
                           style, size);
}

void pad_text(const TouchUi* ui, const PadFrame* frame, float x, float y, const char* text, uint32_t argb, TouchText size,
              bool centered)
{
    float left = pad_x(frame, x);
    if (centered)
    {
        left -= touch_ui_text_width(ui, text, size) * 0.5f;
    }
    touch_ui_text(ui, std::floor(left), pad_y(frame, y), text, argb, size);
}

int32_t pad_parts_draw(const TouchUi* ui, const PadFrame* frame, const PadPart* parts, int32_t count)
{
    const RetroUiPalette* palette = &ui->palette;
    int32_t held_count = 0;
    for (int32_t i = 0; i < count; i++)
    {
        const PadPart* part = &parts[i];
        float x = pad_x(frame, part->x);
        float y = pad_y(frame, part->y);
        float width = pad_size(frame, part->width);
        float height = pad_size(frame, part->height);
        bool held = false;
        touch_ui_area(ui, part->id, x, y, width, height, &held);
        held_count += held ? 1 : 0;

        touch_ui_rect(ui, x, y, width, height, held ? PART_LIT : palette->field_face);
        if (part->kind == PadPartKind::stick)
        {
            // The nub: the body's color, 40% of the block, in the middle.
            float nub_width = std::floor(width * 0.4f);
            float nub_height = std::floor(height * 0.4f);
            touch_ui_rect(ui, std::floor(x + (width - nub_width) * 0.5f), std::floor(y + (height - nub_height) * 0.5f),
                          nub_width, nub_height, palette->button_face);
        }
        if (part->label[0] != '\0')
        {
            float text_width = touch_ui_text_width(ui, part->label, part->size);
            touch_ui_text(ui, std::floor(x + (width - text_width) * 0.5f),
                          touch_ui_centered_text_y(ui, y, height, part->size), part->label,
                          held ? PART_LIT_TEXT : palette->field_text, part->size);
        }
    }
    return held_count;
}

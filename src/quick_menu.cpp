#include "quick_menu.h"

#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "pixel_font_imgui.h"
#include "shader_presets.h"

// Retro pixels (2 dp each on a phone).
// Of the screen's width (Andrés): half in landscape, nearly all of it in portrait.
constexpr float MENU_SHARE_LANDSCAPE = 0.5f;
constexpr float MENU_SHARE_PORTRAIT  = 0.9f;
constexpr float MENU_PADDING   = 7.0f;
constexpr float MENU_GAP       = 4.0f;
constexpr float ROW_LANDSCAPE  = 17.0f;    // 34 dp: the landscape screen is only 195 tall
constexpr float ROW_PORTRAIT   = 22.0f;    // 44 dp
constexpr float LABEL_HEIGHT   = 8.0f;
constexpr float SLIDE_SECONDS  = 0.12f;

constexpr float    ARROW_SIZE   = 22.0f;   // 44 dp
constexpr float    ARROW_MARGIN = 4.0f;
constexpr uint32_t ARROW_FACE   = 0x73101828u;   // see-through over the game
constexpr uint32_t DIM          = 0x59000000u;   // over the screen behind (35%)

const char* nav_action_name(NavAction action)
{
    switch (action)
    {
        case NavAction::none:        return "NONE";
        case NavAction::home:        return "HOME";
        case NavAction::menu:        return "MENU";
        case NavAction::rotate:      return "ROTATE";
        case NavAction::pair:        return "PAIR A DEVICE";
        case NavAction::gamepad:     return "GAMEPAD";
        case NavAction::remote_play: return "REMOTE PLAY";
        case NavAction::devices:     return "DEVICES";
        case NavAction::edit_layout: return "EDIT LAYOUT";
        case NavAction::edit_done:   return "EDIT LAYOUT DONE";
        case NavAction::settings:    return "SETTINGS";
        case NavAction::touch_controls: return "TOUCH CONTROLS";
        case NavAction::measure:     return "MEASURE";
        default:                     return "?";
    }
}

void quick_menu_open(QuickMenu* menu)
{
    menu->slide = 0.0f;
}

bool quick_menu_arrow(const TouchUi* ui)
{
    float x = touch_ui_width(ui) - ARROW_MARGIN - ARROW_SIZE;
    touch_ui_rect(ui, x, ARROW_MARGIN, ARROW_SIZE, ARROW_SIZE, ARROW_FACE);
    return touch_ui_button(ui, "##quick_menu_arrow", x, ARROW_MARGIN, ARROW_SIZE, ARROW_SIZE, PIXEL_FONT_UTF8_ARROW_LEFT,
                           TouchButton::plain, TouchText::normal);
}

// A label over a control; returns the y under it.
static float label(const TouchUi* ui, float x, float y, const char* text)
{
    touch_ui_text(ui, x + 1.0f, y, text, ui->palette.text_disabled, TouchText::normal);
    return y + LABEL_HEIGHT;
}

NavAction quick_menu_draw(QuickMenu* menu, const TouchUi* ui, const QuickMenuInput* input, AppSettings* settings)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    float row = landscape ? ROW_LANDSCAPE : ROW_PORTRAIT;
    NavAction picked = NavAction::none;

    // It slides in (eased out); the screen behind darkens as it comes.
    menu->slide += ImGui::GetIO().DeltaTime / SLIDE_SECONDS;
    menu->slide = menu->slide > 1.0f ? 1.0f : menu->slide;
    float eased = 1.0f - (1.0f - menu->slide) * (1.0f - menu->slide);
    uint32_t dim_alpha = static_cast<uint32_t>(static_cast<float>(DIM >> 24) * eased);
    touch_ui_rect(ui, 0.0f, 0.0f, width, height, (dim_alpha << 24) | (DIM & 0x00FFFFFFu));

    float menu_width = std::floor(width * (landscape ? MENU_SHARE_LANDSCAPE : MENU_SHARE_PORTRAIT));
    float left = std::floor(width - menu_width * eased);
    touch_ui_rect(ui, left, 0.0f, menu_width, height, palette->panel_face);

    // ImGui gives a tap to the first item submitted under it: the buttons go
    // first, then the menu's and the outside's areas.
    float x = left + MENU_PADDING;
    float inner = menu_width - 2.0f * MENU_PADDING;
    float y = MENU_PADDING;

    // The host, and › to close.
    float name_y = touch_ui_centered_text_y(ui, y, row, TouchText::normal);
    touch_ui_lamp(ui, x, name_y, input->online ? TOUCH_UI_LAMP_ONLINE : palette->panel_dark);
    touch_ui_text(ui, x + TOUCH_UI_LAMP + 4.0f, name_y, input->host, palette->text_highlight, TouchText::normal);
    if (touch_ui_button(ui, "##quick_close", x + inner - row, y, row, row, PIXEL_FONT_UTF8_ARROW_RIGHT, TouchButton::plain,
                        TouchText::normal))
    {
        picked = NavAction::menu;
    }
    y += row + MENU_GAP;

    // The other mode, the main action.
    bool in_remote_play = input->current == Screen::remote_play;
    if (touch_ui_button(ui, "##quick_switch", x, y, inner, row, in_remote_play ? "GAMEPAD" : "REMOTE PLAY",
                        TouchButton::primary, TouchText::normal))
    {
        picked = in_remote_play ? NavAction::gamepad : NavAction::remote_play;
    }
    y += row + MENU_GAP;

    // REMOTE PLAY's picture: the shader (- / + step through the presets) and the pacing.
    if (in_remote_play)
    {
        y = label(ui, x, y, "SHADER");
        if (touch_ui_button(ui, "##quick_shader_less", x, y, row, row, "-", TouchButton::normal, TouchText::normal))
        {
            int32_t count = shader_preset_count();
            settings->shader = (settings->shader + count - 1) % count;
            menu->settings_changed = true;
        }
        touch_ui_rect(ui, x + row + MENU_GAP, y, inner - 2.0f * (row + MENU_GAP), row, palette->field_face);
        const char* shader = shader_preset(settings->shader)->label;
        float shader_x = x + (inner - touch_ui_text_width(ui, shader, TouchText::normal)) * 0.5f;
        touch_ui_text(ui, std::floor(shader_x), touch_ui_centered_text_y(ui, y, row, TouchText::normal), shader,
                      palette->text, TouchText::normal);
        if (touch_ui_button(ui, "##quick_shader_more", x + inner - row, y, row, row, "+", TouchButton::normal,
                            TouchText::normal))
        {
            settings->shader = (settings->shader + 1) % shader_preset_count();
            menu->settings_changed = true;
        }
        y += row + MENU_GAP;

        y = label(ui, x, y, "PACING");
        static const char* const PACINGS[] = {"LATENCY", "BALANCED", "SMOOTH"};
        float each = (inner - 2.0f * MENU_GAP) / 3.0f;
        for (int32_t i = 0; i < 3; i++)
        {
            char id[32];
            std::snprintf(id, sizeof(id), "##quick_pacing%d", static_cast<int>(i));
            TouchButton style = static_cast<int32_t>(settings->pacing) == i ? TouchButton::primary : TouchButton::normal;
            if (touch_ui_button(ui, id, x + static_cast<float>(i) * (each + MENU_GAP), y, each, row, PACINGS[i], style,
                                TouchText::normal))
            {
                settings->pacing = static_cast<StreamPacing>(i);
                menu->settings_changed = true;
            }
        }
        y += row + MENU_GAP + 2.0f;
    }

    // The rest: two columns in landscape (it has to fit 195 retro pixels), one in portrait.
    const char* labels[4] = {};
    NavAction actions[4] = {};
    int32_t count = 0;
    labels[count] = "EDIT LAYOUT";
    actions[count++] = NavAction::edit_layout;
    if (input->devices)
    {
        labels[count] = "DEVICES";
        actions[count++] = NavAction::devices;
    }
    if (input->touch_toggle)
    {
        labels[count] = landscape ? (input->touch_shown ? "HIDE TOUCH" : "SHOW TOUCH")
                                  : (input->touch_shown ? "HIDE TOUCH CONTROLS" : "SHOW TOUCH CONTROLS");
        actions[count++] = NavAction::touch_controls;
    }
    labels[count] = "ROTATE";
    actions[count++] = NavAction::rotate;
    int32_t columns = landscape ? 2 : 1;
    float column_width = (inner - static_cast<float>(columns - 1) * MENU_GAP) / static_cast<float>(columns);
    for (int32_t i = 0; i < count; i++)
    {
        char id[32];
        std::snprintf(id, sizeof(id), "##quick_item%d", static_cast<int>(i));
        float item_x = x + static_cast<float>(i % columns) * (column_width + MENU_GAP);
        float item_y = y + static_cast<float>(i / columns) * (row + MENU_GAP);
        if (touch_ui_button(ui, id, item_x, item_y, column_width, row, labels[i], TouchButton::normal, TouchText::normal))
        {
            picked = actions[i];
        }
    }
    y += static_cast<float>((count + columns - 1) / columns) * (row + MENU_GAP);
    if (touch_ui_button(ui, "##quick_leave", x, y, inner, row, "LEAVE", TouchButton::ghost, TouchText::normal))
    {
        picked = NavAction::home;
    }
    y += row + MENU_GAP;

    // How it opens and closes, at the bottom when there's room.
    float hint_y = height - MENU_PADDING - touch_ui_text_height(ui, TouchText::normal);
    if (hint_y > y)
    {
        touch_ui_text(ui, x + 1.0f, hint_y, "HOLD START · B CLOSES", palette->text_disabled, TouchText::normal);
    }

    // Taps on the menu between buttons do nothing; outside it, they close it.
    bool held = false;
    touch_ui_area(ui, "##quick_menu", left, 0.0f, menu_width, height, &held, TouchNav::taps_only);
    if (touch_ui_area(ui, "##quick_outside", 0.0f, 0.0f, width, height, &held, TouchNav::taps_only))
    {
        picked = NavAction::menu;
    }
    return picked;
}

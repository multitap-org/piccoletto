#include "quick_menu.h"

#include <cmath>
#include <cstdio>

#include <SDL3/SDL_timer.h>

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
constexpr uint64_t SLIDE_MS    = 120;

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
        case NavAction::go_headless: return "GO HEADLESS";
        case NavAction::play_on_host: return "PLAY ON THE HOST";
        default:                     return "?";
    }
}

void quick_menu_open(QuickMenu* menu)
{
    menu->opened_ms = SDL_GetTicks();
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
    float eased = retro_ui_ease_out(SDL_GetTicks(), menu->opened_ms, SLIDE_MS);
    touch_ui_rect(ui, 0.0f, 0.0f, width, height, retro_ui_color_with_alpha(DIM, eased));

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
        const char* shader = shader_preset(settings->shader_gl, settings->shader)->label;
        int32_t step = touch_ui_stepper(ui, "##quick_shader", shader, x, y, inner, row);
        if (step != 0)
        {
            int32_t count = shader_preset_count(settings->shader_gl);
            settings->shader = (settings->shader + count + step) % count;
            menu->settings_changed = true;
        }
        y += row + MENU_GAP;

        y = label(ui, x, y, "PACING");
        static const char* const PACINGS[] = {"LATENCY", "BALANCED", "SMOOTH"};
        int32_t pacing = touch_ui_choices(ui, "##quick_pacing", PACINGS, 3, static_cast<int32_t>(settings->pacing), x, y, inner,
                                          row);
        if (pacing >= 0)
        {
            settings->pacing = static_cast<StreamPacing>(pacing);
            menu->settings_changed = true;
        }
        y += row + MENU_GAP + 2.0f;
    }

    // The rest: two columns in landscape (it has to fit 195 retro pixels), one in portrait.
    const char* labels[5] = {};
    NavAction actions[5] = {};
    int32_t count = 0;
    labels[count] = "EDIT LAYOUT";
    actions[count++] = NavAction::edit_layout;
    char play_on[48];
    if (input->headless == HeadlessItem::go)
    {
        labels[count] = "GO HEADLESS";
        actions[count++] = NavAction::go_headless;
    }
    else if (input->headless == HeadlessItem::give_back)
    {
        std::snprintf(play_on, sizeof(play_on), "PLAY ON %s", input->host);
        labels[count] = play_on;
        actions[count++] = NavAction::play_on_host;
    }
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

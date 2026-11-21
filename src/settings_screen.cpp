#include "settings_screen.h"

#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "pixel_font_imgui.h"
#include "shader_presets.h"

// Layout, in retro pixels.
constexpr float MARGIN        = 8.0f;
constexpr float HEADER_HEIGHT = 24.0f;
constexpr float LABEL_HEIGHT  = 12.0f;   // a setting's name, above its control
constexpr float CONTROL       = 24.0f;   // buttons, the name field
constexpr float ROW_GAP       = 12.0f;   // between settings
constexpr float GAP           = 4.0f;

// A setting's name; returns the y under it.
static float label(const TouchUi* ui, float x, float y, const char* text)
{
    touch_ui_text(ui, x + 2.0f, y, text, ui->palette.text_disabled, TouchText::normal);
    return y + LABEL_HEIGHT;
}

// A row of buttons, one per choice, the current one highlighted. Returns the
// tapped choice or -1.
static int32_t choices(const TouchUi* ui, const char* id, const char* const* names, int32_t count, int32_t current, float x,
                       float y, float width)
{
    float each = (width - static_cast<float>(count - 1) * GAP) / static_cast<float>(count);
    int32_t picked = -1;
    for (int32_t i = 0; i < count; i++)
    {
        char button_id[48];
        std::snprintf(button_id, sizeof(button_id), "%s%d", id, static_cast<int>(i));
        TouchButton style = i == current ? TouchButton::primary : TouchButton::normal;
        if (touch_ui_button(ui, button_id, x + static_cast<float>(i) * (each + GAP), y, each, CONTROL, names[i], style,
                            TouchText::normal))
        {
            picked = i;
        }
    }
    return picked;
}

// The phone's name: an ImGui text field in the screen's colors.
static void name_field(const TouchUi* ui, AppSettings* settings, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, CONTROL, palette->field_face);
    float scale = static_cast<float>(ui->scale);
    float text_y = touch_ui_centered_text_y(ui, y, CONTROL, TouchText::normal);
    ImGui::SetCursorScreenPos(ImVec2(std::floor((x + 6.0f) * scale), std::floor(text_y * scale)));
    ImGui::PushFont(ui->font, touch_ui_imgui_font_size(ui, TouchText::normal));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(std::floor((width - 12.0f) * scale));
    ImGui::InputText("##phone_name", settings->phone_name, sizeof(settings->phone_name), ImGuiInputTextFlags_CharsUppercase);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
}

NavAction settings_screen_draw(SettingsScreen* screen, const TouchUi* ui, AppSettings* settings, const char* version,
                               const char* const* paired, int32_t paired_count, int32_t* forget)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;
    *forget = -1;

    // The fixed header.
    float header_y = landscape ? 6.0f : MARGIN;
    if (touch_ui_button(ui, "##back", MARGIN - 2.0f, header_y, HEADER_HEIGHT, HEADER_HEIGHT, PIXEL_FONT_UTF8_ARROW_LEFT,
                        TouchButton::plain, TouchText::big))
    {
        action = NavAction::home;
    }
    touch_ui_text(ui, MARGIN + HEADER_HEIGHT + 4.0f, touch_ui_centered_text_y(ui, header_y, HEADER_HEIGHT, TouchText::big),
                  "SETTINGS", palette->text_highlight, TouchText::big);
    if (touch_ui_button(ui, "##rotate", width - MARGIN - HEADER_HEIGHT, header_y, HEADER_HEIGHT, HEADER_HEIGHT,
                        PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
    {
        action = NavAction::rotate;
    }

    // The list, scrolled, clipped under the header.
    float list_top = header_y + HEADER_HEIGHT + 6.0f;
    float list_height = height - list_top;
    touch_ui_push_clip(ui, 0.0f, list_top, width, list_height);
    float x = MARGIN;
    float column = landscape ? (width - 2.0f * MARGIN) * 0.6f : width - 2.0f * MARGIN;
    if (landscape)
    {
        x = std::floor((width - column) * 0.5f);   // a readable width, centered
    }
    float y = list_top + 4.0f - screen->scroll;

    y = label(ui, x, y, "THIS PHONE'S NAME");
    name_field(ui, settings, x, y, column);
    y += CONTROL + ROW_GAP;

    y = label(ui, x, y, "THEME");
    const char* themes[4] = {app_theme_name(RetroUiTheme::blue), app_theme_name(RetroUiTheme::purple),
                             app_theme_name(RetroUiTheme::zsnes), app_theme_name(RetroUiTheme::black)};
    int32_t theme = choices(ui, "##theme", themes, 4, static_cast<int32_t>(settings->theme), x, y, column);
    if (theme >= 0)
    {
        settings->theme = static_cast<RetroUiTheme>(theme);
    }
    y += CONTROL + ROW_GAP;

    y = label(ui, x, y, "LANGUAGE");
    const char* languages[2] = {"ENGLISH", "ESPAÑOL"};
    int32_t language = choices(ui, "##language", languages, 2, static_cast<int32_t>(settings->language), x, y, column);
    if (language >= 0)
    {
        settings->language = static_cast<AppLanguage>(language);
    }
    y += CONTROL + ROW_GAP;

    y = label(ui, x, y, "VIBRATE ON PRESS");
    const char* on_off[2] = {"ON", "OFF"};
    int32_t vibrate = choices(ui, "##vibrate", on_off, 2, settings->vibrate ? 0 : 1, x, y, column);
    if (vibrate >= 0)
    {
        settings->vibrate = vibrate == 0;
    }
    y += CONTROL + ROW_GAP;

    y = label(ui, x, y, "OVERLAY OPACITY");
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(std::lround(settings->overlay_opacity * 100.0f)));
    const char* opacity_steps[3] = {"-", percent, "+"};
    int32_t opacity = choices(ui, "##opacity", opacity_steps, 3, 1, x, y, column);
    if (opacity == 0 || opacity == 2)
    {
        float next = settings->overlay_opacity + (opacity == 0 ? -0.1f : 0.1f);
        settings->overlay_opacity = next < 0.1f ? 0.1f : (next > 1.0f ? 1.0f : next);
    }
    y += CONTROL + ROW_GAP;

    y = label(ui, x, y, "REMOTE PLAY PACING");
    const char* pacings[3] = {"LATENCY", "BALANCED", "SMOOTH"};
    int32_t pacing = choices(ui, "##pacing", pacings, 3, static_cast<int32_t>(settings->pacing), x, y, column);
    if (pacing >= 0)
    {
        settings->pacing = static_cast<StreamPacing>(pacing);
    }
    y += CONTROL + ROW_GAP;

    // Lightest first; - / + step through them.
    y = label(ui, x, y, "REMOTE PLAY SHADER");
    const char* shader_steps[3] = {"-", shader_preset(settings->shader_gl, settings->shader)->label, "+"};
    int32_t shader = choices(ui, "##shader", shader_steps, 3, 1, x, y, column);
    if (shader == 0 || shader == 2)
    {
        int32_t count = shader_preset_count(settings->shader_gl);
        settings->shader = (settings->shader + (shader == 0 ? count - 1 : 1)) % count;
    }
    y += CONTROL + ROW_GAP;

    y = label(ui, x, y, "PAIRED DEVICES");
    for (int32_t i = 0; i < paired_count; i++)
    {
        touch_ui_rect(ui, x, y, column, CONTROL + 4.0f, palette->panel_face);
        touch_ui_text(ui, x + 10.0f, touch_ui_centered_text_y(ui, y, CONTROL + 4.0f, TouchText::normal), paired[i],
                      palette->text_highlight, TouchText::normal);
        char forget_id[32];
        std::snprintf(forget_id, sizeof(forget_id), "##forget%d", static_cast<int>(i));
        float forget_width = 56.0f;
        if (touch_ui_button(ui, forget_id, x + column - forget_width - 2.0f, y + 2.0f, forget_width, CONTROL, "FORGET",
                            TouchButton::normal, TouchText::normal))
        {
            *forget = i;
        }
        y += CONTROL + 4.0f + GAP;
    }
    y += ROW_GAP - GAP;

    y = label(ui, x, y, "ABOUT");
    char about[96];
    std::snprintf(about, sizeof(about), "PICCOLETTO %s", version);
    touch_ui_text(ui, x + 2.0f, y, about, palette->text, TouchText::normal);
    y += LABEL_HEIGHT;
    touch_ui_text(ui, x + 2.0f, y, "PICCOLO'S COMPANION · MIT LICENSE", palette->text_disabled, TouchText::normal);
    y += LABEL_HEIGHT + ROW_GAP;

    // Drag anywhere else to scroll (submitted last: the controls get their taps first).
    float content = y + screen->scroll - list_top;
    float most = content > list_height ? content - list_height : 0.0f;
    bool held = false;
    touch_ui_area(ui, "##settings_scroll", 0.0f, list_top, width, list_height, &held, TouchNav::taps_only);
    if (held)
    {
        screen->scroll -= ImGui::GetIO().MouseDelta.y / static_cast<float>(ui->scale);
    }
    screen->scroll -= ImGui::GetIO().MouseWheel * 16.0f;
    // The gamepad's focus kept in view (a margin of a label above it).
    if (ui->focus != nullptr && ui->focus->has)
    {
        float top = ui->focus->y - LABEL_HEIGHT;
        float bottom = ui->focus->y + ui->focus->height + ROW_GAP;
        if (top < list_top)
        {
            screen->scroll -= list_top - top;
        }
        else if (bottom > list_top + list_height)
        {
            screen->scroll += bottom - (list_top + list_height);
        }
    }
    screen->scroll = screen->scroll < 0.0f ? 0.0f : (screen->scroll > most ? most : screen->scroll);
    touch_ui_pop_clip();
    return action;
}

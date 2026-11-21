#include "pair_screen.h"

#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "pixel_font_imgui.h"

// Layout, in retro pixels (half the mockups' dp).
constexpr float MARGIN        = 8.0f;
constexpr float HEADER_HEIGHT = 24.0f;
constexpr float ROW_HEIGHT    = 32.0f;
constexpr float GAP           = 4.0f;
constexpr float CODE_HEIGHT   = 32.0f;
constexpr float KEY_HEIGHT    = 36.0f;
constexpr float SELECTED_EDGE = 2.0f;

static const char* const KEYS[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", PIXEL_FONT_UTF8_ARROW_LEFT, "0", "OK"};

void pair_screen_reset(PairScreen* pair)
{
    pair->length = 0;
    pair->code[0] = '\0';
}

void pair_screen_set_address(PairScreen* pair, const char* text)
{
    std::snprintf(pair->address, sizeof(pair->address), "%s", text);
    pair->typing_address = true;
}

// The address field: an ImGui text field in the screen's colors, like
// SETTINGS' name field; the keyboard comes up on a phone.
static void address_field(PairScreen* pair, const TouchUi* ui, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, ROW_HEIGHT, palette->field_face);
    float scale = static_cast<float>(ui->scale);
    float text_y = touch_ui_centered_text_y(ui, y, ROW_HEIGHT, TouchText::normal);
    ImGui::SetCursorScreenPos(ImVec2(std::floor((x + 10.0f) * scale), std::floor(text_y * scale)));
    ImGui::PushFont(ui->font, touch_ui_imgui_font_size(ui, TouchText::normal));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(std::floor((width - 20.0f) * scale));
    ImGui::InputTextWithHint("##pair_address", "192.168.1.20 OR THE PAIR LINK", pair->address, sizeof(pair->address),
                             ImGuiInputTextFlags_CharsNoBlank);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
}

// Step 1: the hosts found, one row each, and typing an address. Returns the
// y below it.
static float draw_pick(PairScreen* pair, const TouchUi* ui, const char* const* found, int32_t found_count, float x, float y,
                       float width)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_text(ui, x + 4.0f, y, "1 · PICK IT", palette->text_disabled, TouchText::normal);
    y += 12.0f;
    for (int32_t i = 0; i < found_count; i++)
    {
        char id[24];
        std::snprintf(id, sizeof(id), "##found%d", static_cast<int>(i));
        if (touch_ui_button(ui, id, x, y, width, ROW_HEIGHT, nullptr, TouchButton::card, TouchText::normal))
        {
            pair->selected = i;
            pair->typing_address = false;
        }
        float text_y = touch_ui_centered_text_y(ui, y, ROW_HEIGHT, TouchText::normal);
        touch_ui_lamp(ui, x + 10.0f, text_y, TOUCH_UI_LAMP_ONLINE);
        touch_ui_text(ui, x + 10.0f + TOUCH_UI_LAMP + 8.0f, text_y, found[i], palette->text_highlight, TouchText::normal);
        const char* tag = "NEW";
        touch_ui_text(ui, x + width - 10.0f - touch_ui_text_width(ui, tag, TouchText::normal), text_y, tag,
                      palette->text_disabled, TouchText::normal);
        if (i == pair->selected && !pair->typing_address)
        {
            touch_ui_outline(ui, x, y, width, ROW_HEIGHT, SELECTED_EDGE, ui->palette.highlight);   // the picked row
        }
        y += ROW_HEIGHT + GAP;
    }
    if (found_count == 0 && !pair->typing_address)
    {
        touch_ui_text(ui, x + 4.0f, y + 4.0f, "SEARCHING…", palette->text_disabled, TouchText::normal);
        y += 16.0f;
    }
    if (pair->typing_address)
    {
        address_field(pair, ui, x, y, width);
        touch_ui_outline(ui, x, y, width, ROW_HEIGHT, SELECTED_EDGE, ui->palette.highlight);   // the picked row
        return y + ROW_HEIGHT + GAP;
    }
    char address_label[64];
    std::snprintf(address_label, sizeof(address_label), "OR TYPE AN ADDRESS / PASTE A LINK %s", PIXEL_FONT_UTF8_ARROW_RIGHT);
    float address_width = touch_ui_text_width(ui, address_label, TouchText::normal) + 8.0f;
    if (touch_ui_button(ui, "##address", x, y, address_width, 24.0f, address_label, TouchButton::plain, TouchText::normal))
    {
        pair->typing_address = true;
    }
    return y + 24.0f;
}

// Step 2: the code so far, one box per digit, the next one underlined; how
// the pairing goes under it.
static void draw_code(const PairScreen* pair, const TouchUi* ui, const PairProgress* progress, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_text(ui, x + 4.0f, y, "2 · TYPE ITS CODE", palette->text_disabled, TouchText::normal);
    y += 12.0f;
    float box_gap = 5.0f;
    float box_width = (width - box_gap * (PAIR_CODE_LENGTH - 1)) / PAIR_CODE_LENGTH;
    for (int32_t i = 0; i < PAIR_CODE_LENGTH; i++)
    {
        float box_x = x + static_cast<float>(i) * (box_width + box_gap);
        touch_ui_rect(ui, box_x, y, box_width, CODE_HEIGHT, palette->panel_face);
        if (i == pair->length)
        {
            touch_ui_rect(ui, box_x, y + CODE_HEIGHT - 2.0f, box_width, 2.0f, palette->highlight);
        }
        if (i < pair->length)
        {
            char digit[2] = {pair->code[i], '\0'};
            float digit_width = touch_ui_text_width(ui, digit, TouchText::big);
            touch_ui_text(ui, box_x + (box_width - digit_width) * 0.5f, touch_ui_centered_text_y(ui, y, CODE_HEIGHT, TouchText::big),
                          digit, palette->text_highlight, TouchText::big);
        }
    }
    if (progress->text[0] != '\0')
    {
        touch_ui_text(ui, x + 4.0f, y + CODE_HEIGHT + 6.0f, progress->text, progress->error ? palette->highlight : palette->text,
                      TouchText::normal);
    }
}

// The keypad, 3 x 4 keys filling (x, y, width, height). True when OK is
// tapped with a host and the whole code.
static bool draw_keypad(PairScreen* pair, const TouchUi* ui, int32_t found_count, bool busy, float x, float y, float width,
                        float height)
{
    float key_width = (width - 2.0f * GAP) / 3.0f;
    float key_height = (height - 3.0f * GAP) / 4.0f;
    bool has_host = pair->typing_address ? pair->address[0] != '\0' : found_count > 0;
    bool complete = pair->length == PAIR_CODE_LENGTH && has_host && !busy;
    bool done = false;
    for (int32_t key = 0; key < 12; key++)
    {
        float key_x = x + static_cast<float>(key % 3) * (key_width + GAP);
        float key_y = y + static_cast<float>(key / 3) * (key_height + GAP);
        char id[24];
        std::snprintf(id, sizeof(id), "##key%d", static_cast<int>(key));
        bool ok = key == 11;
        TouchButton style = ok && complete ? TouchButton::primary : TouchButton::normal;
        if (!touch_ui_button(ui, id, key_x, key_y, key_width, key_height, KEYS[key], style, TouchText::big) || busy)
        {
            continue;
        }
        pair->typed = true;
        if (ok)
        {
            done = complete;
        }
        else if (key == 9)
        {
            if (pair->length > 0)
            {
                pair->length--;
                pair->code[pair->length] = '\0';
            }
        }
        else if (pair->length < PAIR_CODE_LENGTH)
        {
            pair->code[pair->length] = KEYS[key][0];
            pair->length++;
            pair->code[pair->length] = '\0';
        }
    }
    return done;
}

NavAction pair_screen_draw(PairScreen* pair, const TouchUi* ui, const char* const* found, int32_t found_count,
                           const PairProgress* progress, bool* done)
{
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;
    *done = false;
    pair->typed = false;
    if (pair->selected >= found_count)
    {
        pair->selected = 0;
    }

    float header_y = landscape ? 6.0f : MARGIN;
    if (touch_ui_button(ui, "##back", MARGIN - 2.0f, header_y, HEADER_HEIGHT, HEADER_HEIGHT, PIXEL_FONT_UTF8_ARROW_LEFT,
                        TouchButton::plain, TouchText::big))
    {
        action = NavAction::home;
    }
    touch_ui_text(ui, MARGIN + HEADER_HEIGHT + 4.0f, touch_ui_centered_text_y(ui, header_y, HEADER_HEIGHT, TouchText::big), "PAIR",
                  ui->palette.text_highlight, TouchText::big);
    if (touch_ui_button(ui, "##rotate", width - MARGIN - HEADER_HEIGHT, header_y, HEADER_HEIGHT, HEADER_HEIGHT,
                        PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
    {
        action = NavAction::rotate;
    }

    float content_y = header_y + HEADER_HEIGHT + (landscape ? 6.0f : 14.0f);
    if (landscape)
    {
        float column = (width - 3.0f * MARGIN) * 0.5f;
        float y = draw_pick(pair, ui, found, found_count, MARGIN, content_y, column);
        draw_code(pair, ui, progress, MARGIN, y + 8.0f, column);
        float keypad_x = MARGIN + column + MARGIN;
        *done = draw_keypad(pair, ui, found_count, progress->busy, keypad_x, content_y, column, height - content_y - MARGIN);
        return action;
    }

    float column = width - 2.0f * MARGIN;
    float y = draw_pick(pair, ui, found, found_count, MARGIN, content_y, column);
    draw_code(pair, ui, progress, MARGIN, y + 16.0f, column);
    float keypad_height = 4.0f * KEY_HEIGHT + 3.0f * GAP;
    *done = draw_keypad(pair, ui, found_count, progress->busy, MARGIN, height - 12.0f - keypad_height, column, keypad_height);
    return action;
}

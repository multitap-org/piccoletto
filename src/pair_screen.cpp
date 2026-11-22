#include "pair_screen.h"

#include <cstdio>

#include <SDL3/SDL_timer.h>

#include "pixel_font_imgui.h"

// Layout, in retro pixels (half the mockups' dp).
constexpr float MARGIN        = 8.0f;
constexpr float HEADER_HEIGHT = 24.0f;
constexpr float ROW_HEIGHT    = 32.0f;
constexpr float GAP           = 4.0f;
constexpr float LABEL_GAP     = 12.0f;   // a section's label to what's under it
constexpr float CODE_HEIGHT   = 32.0f;
constexpr float CODE_GAP      = 2.0f;    // between two digits
constexpr float GROUP_GAP     = 7.0f;    // between the code's groups of three
constexpr float MAIN_HEIGHT   = 40.0f;   // the screen's main button
constexpr float OTHER_HEIGHT  = 30.0f;   // the one under it
constexpr float SELECTED_EDGE = 2.0f;
constexpr float LAMP_GAP      = 3.0f;
constexpr uint64_t LAMP_STEP_MS = 180;   // the lit lamp moves on this often: 1 2 3 4 3 2 1 ...

void pair_screen_reset(PairScreen* pair)
{
    if (!net_pair_code_generate(pair->code))
    {
        pair->code[0] = '\0';
    }
}

// A section's label (ON THIS NETWORK, YOUR CODE...). Returns the y under it.
static float label(const TouchUi* ui, float x, float y, const char* text)
{
    touch_ui_text(ui, x + 4.0f, y, text, ui->palette.text_disabled, TouchText::normal);
    return y + LABEL_GAP;
}

// The piccolos on this network as cards; the picked one outlined (dimmed
// while waiting: nothing to pick then). Returns the y under them.
static float draw_found(PairScreen* pair, const TouchUi* ui, const PairView* view, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    y = label(ui, x, y, "ON THIS NETWORK");
    if (view->found_count == 0)
    {
        touch_ui_text(ui, x + 4.0f, y + 4.0f, "NONE FOUND", palette->text_disabled, TouchText::normal);
        return y + 20.0f;
    }
    bool picking = view->step == PairStep::pick;
    for (int32_t i = 0; i < view->found_count; i++)
    {
        char id[24];
        std::snprintf(id, sizeof(id), "##found%d", static_cast<int>(i));
        if (touch_ui_button(ui, id, x, y, width, ROW_HEIGHT, nullptr, TouchButton::card, TouchText::normal) && picking)
        {
            pair->selected = i;
        }
        float text_y = touch_ui_centered_text_y(ui, y, ROW_HEIGHT, TouchText::normal);
        uint32_t name_color = picking ? palette->text_highlight : palette->text_disabled;
        touch_ui_lamp(ui, x + 10.0f, text_y, TOUCH_UI_LAMP_ONLINE);
        touch_ui_text(ui, x + 10.0f + TOUCH_UI_LAMP + 8.0f, text_y, view->found[i], name_color, TouchText::normal);
        const char* tag = "LAN";
        touch_ui_text(ui, x + width - 10.0f - touch_ui_text_width(ui, tag, TouchText::normal), text_y, tag,
                      palette->text_disabled, TouchText::normal);
        if (i == pair->selected)
        {
            uint32_t edge = picking ? palette->highlight : palette->text_disabled;
            touch_ui_outline(ui, x, y, width, ROW_HEIGHT, SELECTED_EDGE, edge);
        }
        y += ROW_HEIGHT + GAP;
    }
    return y;
}

// YOUR CODE: nine boxes in three groups. Returns the y under them.
static float draw_code(const PairScreen* pair, const TouchUi* ui, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    y = label(ui, x, y, "YOUR CODE");
    constexpr int32_t digits = static_cast<int32_t>(NET_PAIR_CODE_DIGITS);
    float box_width = (width - 6.0f * CODE_GAP - 2.0f * GROUP_GAP) / static_cast<float>(digits);
    float box_x = x;
    for (int32_t i = 0; i < digits; i++)
    {
        touch_ui_rect(ui, box_x, y, box_width, CODE_HEIGHT, palette->panel_face);
        char digit[2] = {pair->code[i], '\0'};
        if (digit[0] != '\0')
        {
            float digit_width = touch_ui_text_width(ui, digit, TouchText::big);
            touch_ui_text(ui, box_x + (box_width - digit_width) * 0.5f,
                          touch_ui_centered_text_y(ui, y, CODE_HEIGHT, TouchText::big), digit, palette->text_highlight,
                          TouchText::big);
        }
        box_x += box_width + ((i % 3 == 2) ? GROUP_GAP : CODE_GAP);
    }
    return y + CODE_HEIGHT;
}

// The four lamps, one lit at a time (1 2 3 4 3 2 1 ...), then `text`.
static void draw_lamps(const TouchUi* ui, float x, float y, const char* text)
{
    static const int32_t SEQUENCE[6] = {0, 1, 2, 3, 2, 1};
    int32_t lit = SEQUENCE[(SDL_GetTicks() / LAMP_STEP_MS) % 6];
    float text_y = y;
    float lamp_y = text_y + (touch_ui_text_height(ui, TouchText::normal) - TOUCH_UI_LAMP) * 0.5f;
    for (int32_t i = 0; i < 4; i++)
    {
        uint32_t color = i == lit ? TOUCH_UI_LAMP_ONLINE : ui->palette.panel_face;
        touch_ui_rect(ui, x + static_cast<float>(i) * (TOUCH_UI_LAMP + LAMP_GAP), lamp_y, TOUCH_UI_LAMP, TOUCH_UI_LAMP,
                      color);
    }
    touch_ui_text(ui, x + 4.0f * (TOUCH_UI_LAMP + LAMP_GAP) + 6.0f, text_y, text, ui->palette.text_highlight,
                  TouchText::normal);
}

// Under the code: why the last try failed, or what happens while waiting.
static void draw_status(const PairScreen* pair, const TouchUi* ui, const PairView* view, float x, float y)
{
    const RetroUiPalette* palette = &ui->palette;
    float line = touch_ui_text_height(ui, TouchText::normal) + 4.0f;
    if (view->step == PairStep::pick)
    {
        if (view->error[0] != '\0')
        {
            touch_ui_text(ui, x + 4.0f, y, view->error, palette->highlight, TouchText::normal);
        }
        else if (view->found_count == 0)
        {
            touch_ui_text(ui, x + 4.0f, y, "THE PICCOLO IS ELSEWHERE?", palette->text_disabled, TouchText::normal);
        }
        return;
    }
    draw_lamps(ui, x + 4.0f, y, view->host_asked || view->internet ? "WAITING" : "CONNECTING…");
    y += line + 4.0f;
    if (view->internet)
    {
        touch_ui_text(ui, x + 4.0f, y, "SEND THE CODE TO THE PICCOLO.", palette->text, TouchText::normal);
        touch_ui_text(ui, x + 4.0f, y + line, "IT TYPES IT IN TOOLS > INVITE.", palette->text, TouchText::normal);
        return;
    }
    const char* name = pair->selected < view->found_count ? view->found[pair->selected] : "THE PICCOLO";
    char text[96];
    std::snprintf(text, sizeof(text), "TYPE THE CODE ON %s.", name);
    touch_ui_text(ui, x + 4.0f, y, text, palette->text, TouchText::normal);
}

// The buttons at the bottom of a column (x, bottom - their height). Sets
// *tap for the one tapped.
static void draw_buttons(const TouchUi* ui, const PairView* view, float x, float bottom, float width, PairTap* tap)
{
    float other_y = bottom - OTHER_HEIGHT;
    float main_y = other_y - GAP - MAIN_HEIGHT;
    if (view->step == PairStep::paired)
    {
        if (touch_ui_button(ui, "##continue", x, bottom - MAIN_HEIGHT, width, MAIN_HEIGHT, "CONTINUE",
                            TouchButton::primary, TouchText::big))
        {
            *tap = PairTap::proceed;
        }
        return;
    }
    if (view->step == PairStep::waiting)
    {
        if (view->internet && touch_ui_button(ui, "##send", x, main_y, width, MAIN_HEIGHT, "SEND CODE",
                                              TouchButton::primary, TouchText::big))
        {
            *tap = PairTap::send_code;
        }
        if (touch_ui_button(ui, "##cancel", x, other_y, width, OTHER_HEIGHT, "CANCEL", TouchButton::normal,
                            TouchText::normal))
        {
            *tap = PairTap::cancel;
        }
        return;
    }
    // Nobody on this network: INTERNET is the way, so it's the main button.
    if (view->found_count == 0)
    {
        if (touch_ui_button(ui, "##internet", x, bottom - MAIN_HEIGHT, width, MAIN_HEIGHT, "INTERNET",
                            TouchButton::primary, TouchText::big))
        {
            *tap = PairTap::internet;
        }
        return;
    }
    if (touch_ui_button(ui, "##pair", x, main_y, width, MAIN_HEIGHT, "PAIR", TouchButton::primary, TouchText::big))
    {
        *tap = PairTap::pair;
    }
    if (touch_ui_button(ui, "##internet", x, other_y, width, OTHER_HEIGHT, "INTERNET", TouchButton::normal,
                        TouchText::normal))
    {
        *tap = PairTap::internet;
    }
}

// PAIRED: the piccolo and what it gave this phone (chips).
static void draw_paired(const TouchUi* ui, const PairView* view, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_lamp(ui, x + 4.0f, y + (touch_ui_text_height(ui, TouchText::big) - TOUCH_UI_LAMP) * 0.5f,
                  TOUCH_UI_LAMP_ONLINE);
    touch_ui_text(ui, x + 4.0f + TOUCH_UI_LAMP + 6.0f, y, "PAIRED", palette->text_highlight, TouchText::big);
    y += touch_ui_text_height(ui, TouchText::big) + 12.0f;

    float card_height = 76.0f;
    touch_ui_rect(ui, x, y, width, card_height, palette->panel_face);
    touch_ui_text(ui, x + 10.0f, y + 10.0f, view->host_name, palette->text_highlight, TouchText::big);
    float chips_y = y + 10.0f + touch_ui_text_height(ui, TouchText::big) + 8.0f;
    touch_ui_text(ui, x + 10.0f, chips_y, "IT GAVE YOU", palette->text_disabled, TouchText::normal);
    chips_y += LABEL_GAP;
    const char* chips[3] = {"GAMEPAD", nullptr, nullptr};
    int32_t chip_count = 1;
    if (view->remote_play)
    {
        chips[chip_count++] = "REMOTE PLAY";
    }
    if (view->devices)
    {
        chips[chip_count++] = "DEVICES";
    }
    float chip_x = x + 10.0f;
    float chip_height = touch_ui_text_height(ui, TouchText::normal) + 10.0f;
    for (int32_t i = 0; i < chip_count; i++)
    {
        float chip_width = touch_ui_text_width(ui, chips[i], TouchText::normal) + 12.0f;
        touch_ui_rect(ui, chip_x, chips_y, chip_width, chip_height, palette->field_face);
        touch_ui_text(ui, chip_x + 6.0f, touch_ui_centered_text_y(ui, chips_y, chip_height, TouchText::normal), chips[i],
                      palette->text_highlight, TouchText::normal);
        chip_x += chip_width + GAP;
    }
    y += card_height + 12.0f;
    float line = touch_ui_text_height(ui, TouchText::normal) + 4.0f;
    touch_ui_text(ui, x + 4.0f, y, "MORE ACCESS: ON THE PICCOLO,", palette->text_disabled, TouchText::normal);
    touch_ui_text(ui, x + 4.0f, y + line, "SETTINGS > NETWORK.", palette->text_disabled, TouchText::normal);
}

NavAction pair_screen_draw(PairScreen* pair, const TouchUi* ui, const PairView* view, PairTap* tap)
{
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;
    *tap = PairTap::none;
    if (pair->selected >= view->found_count)
    {
        pair->selected = 0;
    }

    float header_y = landscape ? 6.0f : MARGIN;
    if (touch_ui_button(ui, "##back", MARGIN - 2.0f, header_y, HEADER_HEIGHT, HEADER_HEIGHT, PIXEL_FONT_UTF8_ARROW_LEFT,
                        TouchButton::plain, TouchText::big))
    {
        action = NavAction::home;
    }
    touch_ui_text(ui, MARGIN + HEADER_HEIGHT + 4.0f, touch_ui_centered_text_y(ui, header_y, HEADER_HEIGHT, TouchText::big),
                  "PAIR", ui->palette.text_highlight, TouchText::big);
    if (touch_ui_button(ui, "##rotate", width - MARGIN - HEADER_HEIGHT, header_y, HEADER_HEIGHT, HEADER_HEIGHT,
                        PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
    {
        action = NavAction::rotate;
    }

    float content_y = header_y + HEADER_HEIGHT + (landscape ? 6.0f : 14.0f);
    float bottom = height - (landscape ? 6.0f : 12.0f);
    if (landscape)
    {
        float column = (width - 3.0f * MARGIN) * 0.5f;
        float right_x = MARGIN + column + MARGIN;
        if (view->step == PairStep::paired)
        {
            draw_paired(ui, view, MARGIN, content_y, column);
        }
        else
        {
            draw_found(pair, ui, view, MARGIN, content_y, column);
            float y = draw_code(pair, ui, right_x, content_y, column);
            draw_status(pair, ui, view, right_x, y + 10.0f);
        }
        draw_buttons(ui, view, right_x, bottom, column, tap);
        return action;
    }

    float column = width - 2.0f * MARGIN;
    if (view->step == PairStep::paired)
    {
        draw_paired(ui, view, MARGIN, content_y, column);
    }
    else
    {
        float y = draw_found(pair, ui, view, MARGIN, content_y, column);
        y = draw_code(pair, ui, MARGIN, y + 14.0f, column);
        draw_status(pair, ui, view, MARGIN, y + 12.0f);
    }
    draw_buttons(ui, view, MARGIN, bottom, column, tap);
    return action;
}

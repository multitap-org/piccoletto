#include "pair_screen.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL_timer.h>

#include "pixel_font_imgui.h"

// Layout, in retro pixels (half the mockups' dp).
constexpr float MARGIN        = 8.0f;
constexpr float HEADER_HEIGHT = 24.0f;
constexpr float TAB_LINE      = 14.0f;   // a tab's height per line of its label
constexpr float ROW_HEIGHT    = 32.0f;
constexpr float GAP           = 4.0f;
constexpr float LABEL_GAP     = 12.0f;   // a section's label to what's under it
constexpr float CODE_HEIGHT   = 32.0f;
constexpr float CODE_GAP      = 2.0f;    // between two digits
constexpr float GROUP_GAP     = 7.0f;    // between the code's groups of three
constexpr float KEY_HEIGHT    = 28.0f;   // the number pad's keys, at most
constexpr float KEY_MIN       = 16.0f;   // ... and at least (a short landscape screen)
constexpr float MAIN_HEIGHT   = 40.0f;   // the screen's main button
constexpr float OTHER_HEIGHT  = 30.0f;   // the one under it
constexpr float SELECTED_EDGE = 2.0f;
constexpr float LAMP_GAP      = 3.0f;
constexpr uint64_t LAMP_STEP_MS = 180;   // the lit lamp moves on this often: 1 2 3 4 3 2 1 ...
constexpr uint64_t BLINK_MS     = 600;   // WAITING FOR ... on this long, off as long

static const char* const MODE_LABELS[2] = {"PAIR A DEVICE", "GENERATE PAIRING CODE"};

void pair_screen_reset(PairScreen* pair)
{
    pair->selected = -1;
    std::memset(pair->typed, 0, sizeof(pair->typed));
}

static float line_step(const TouchUi* ui)
{
    return touch_ui_text_height(ui, TouchText::normal) + 4.0f;
}

// `text` from (x, y), broken at spaces onto more lines to stay within
// `width`. Returns the y under its last line.
static float text_wrapped(const TouchUi* ui, float x, float y, float width, const char* text, uint32_t color)
{
    char line[340] = {};
    const char* word = text;
    while (*word != '\0')
    {
        const char* end = std::strchr(word, ' ');
        int word_length = static_cast<int>(end != nullptr ? static_cast<size_t>(end - word) : std::strlen(word));
        char tried[340];
        int written = std::snprintf(tried, sizeof(tried), "%s%s%.*s", line, line[0] != '\0' ? " " : "", word_length, word);
        bool too_long = written < 0 || static_cast<size_t>(written) >= sizeof(tried);
        if (line[0] != '\0' && (too_long || touch_ui_text_width(ui, tried, TouchText::normal) > width))
        {
            touch_ui_text(ui, x, y, line, color, TouchText::normal);
            y += line_step(ui);
            std::snprintf(line, sizeof(line), "%.*s", word_length, word);
        }
        else
        {
            std::memcpy(line, tried, sizeof(line));
        }
        word = end != nullptr ? end + 1 : word + word_length;
    }
    if (line[0] != '\0')
    {
        touch_ui_text(ui, x, y, line, color, TouchText::normal);
        y += line_step(ui);
    }
    return y;
}

// A section's label (DISCOVER DEVICES, PERMISSIONS...), on more lines when it
// doesn't fit `width`. Returns the y under it.
static float label(const TouchUi* ui, float x, float y, const char* text, float width)
{
    float end = text_wrapped(ui, x + 4.0f, y, width - 4.0f, text, ui->palette.text_disabled);
    return end - line_step(ui) + LABEL_GAP;
}

// "<PREFIX> <NAME>" as a label.
static float label_named(const TouchUi* ui, float x, float y, const char* prefix, const char* name, float width)
{
    char text[160];
    std::snprintf(text, sizeof(text), "%s %s", prefix, name[0] != '\0' ? name : "THE OTHER DEVICE");
    return label(ui, x, y, text, width);
}

// A tab's label: on one line if it fits, else split at the space nearest the
// middle.
static void tab_lines(const TouchUi* ui, const char* text, float width, char* first, char* second, size_t size)
{
    std::snprintf(first, size, "%s", text);
    second[0] = '\0';
    if (touch_ui_text_width(ui, text, TouchText::normal) <= width - 8.0f)
    {
        return;
    }
    size_t length = std::strlen(text);
    size_t half = length / 2;
    size_t best = 0;
    for (size_t i = 1; i < length; i++)
    {
        size_t distance = i > half ? i - half : half - i;
        size_t best_distance = best > half ? best - half : half - best;
        if (text[i] == ' ' && (best == 0 || distance < best_distance))
        {
            best = i;
        }
    }
    if (best == 0 || best >= size)
    {
        return;
    }
    std::snprintf(first, size, "%.*s", static_cast<int>(best), text);
    std::snprintf(second, size, "%s", text + best + 1);
}

// The two modes, splitting the width; the current one highlighted, the other
// dimmed and dead while a step of this one runs. Returns the y under them.
static float draw_tabs(PairScreen* pair, const TouchUi* ui, const PairView* view, float x, float y, float width,
                       PairTap* tap)
{
    const RetroUiPalette* palette = &ui->palette;
    float tab_width = (width - TOUCH_UI_GAP) * 0.5f;
    char first[2][64];
    char second[2][64];
    int32_t lines = 1;
    for (int32_t i = 0; i < 2; i++)
    {
        tab_lines(ui, MODE_LABELS[i], tab_width, first[i], second[i], sizeof(first[i]));
        lines = second[i][0] != '\0' ? 2 : lines;
    }
    float height = TAB_LINE * static_cast<float>(lines) + 8.0f;
    float line_height = touch_ui_text_height(ui, TouchText::normal);
    for (int32_t i = 0; i < 2; i++)
    {
        PairMode mode = i == 0 ? PairMode::device : PairMode::code;
        bool current = pair->mode == mode;
        bool locked = !current && (mode == PairMode::code ? view->busy_device : view->busy_code);
        float tab_x = x + static_cast<float>(i) * (tab_width + TOUCH_UI_GAP);
        char id[16];
        std::snprintf(id, sizeof(id), "##mode%d", static_cast<int>(i));
        if (touch_ui_button(ui, id, tab_x, y, tab_width, height, nullptr,
                            current ? TouchButton::primary : TouchButton::normal, TouchText::normal) &&
            !current && !locked)
        {
            pair->mode = mode;
            *tap = PairTap::mode;
        }
        uint32_t color = locked ? palette->text_disabled : palette->text_highlight;
        float block = second[i][0] != '\0' ? 2.0f * line_height + 3.0f : line_height;
        float text_y = y + (height - block) * 0.5f;
        float first_width = touch_ui_text_width(ui, first[i], TouchText::normal);
        touch_ui_text(ui, tab_x + (tab_width - first_width) * 0.5f, text_y, first[i], color, TouchText::normal);
        if (second[i][0] != '\0')
        {
            float second_width = touch_ui_text_width(ui, second[i], TouchText::normal);
            touch_ui_text(ui, tab_x + (tab_width - second_width) * 0.5f, text_y + line_height + 3.0f, second[i], color,
                          TouchText::normal);
        }
    }
    return y + height;
}

// DISCOVER DEVICES as cards, then ENTER A CODE; the picked one outlined.
// Returns the y under them.
static float draw_list(PairScreen* pair, const TouchUi* ui, const PairView* view, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    y = label(ui, x, y, "DISCOVER DEVICES", width);
    if (view->found_count == 0)
    {
        y = text_wrapped(ui, x + 4.0f, y, width - 4.0f, "NONE FOUND ON THIS NETWORK", palette->text_disabled) + 4.0f;
    }
    for (int32_t i = 0; i <= view->found_count; i++)
    {
        bool enter = i == view->found_count;
        char id[24];
        std::snprintf(id, sizeof(id), "##found%d", static_cast<int>(i));
        if (touch_ui_button(ui, id, x, y, width, ROW_HEIGHT, nullptr, TouchButton::card, TouchText::normal))
        {
            pair->selected = i;
        }
        float text_y = touch_ui_centered_text_y(ui, y, ROW_HEIGHT, TouchText::normal);
        if (enter)
        {
            touch_ui_text(ui, x + 10.0f, text_y, "ENTER A CODE", palette->text_highlight, TouchText::normal);
        }
        else
        {
            touch_ui_lamp(ui, x + 10.0f, text_y, TOUCH_UI_LAMP_ONLINE);
            touch_ui_text(ui, x + 10.0f + TOUCH_UI_LAMP + 8.0f, text_y, view->found[i], palette->text_highlight,
                          TouchText::normal);
            const char* kind = view->found_kinds[i];
            touch_ui_text(ui, x + width - 10.0f - touch_ui_text_width(ui, kind, TouchText::normal), text_y, kind,
                          palette->text_disabled, TouchText::normal);
        }
        if (i == pair->selected)
        {
            touch_ui_outline(ui, x, y, width, ROW_HEIGHT, SELECTED_EDGE, palette->highlight);
        }
        y += ROW_HEIGHT + GAP;
    }
    return y;
}

// Nine boxes in three groups: `code`'s digits (the next box to type outlined
// when `cursor`). Returns the y under them.
static float draw_code(const TouchUi* ui, const char* code, bool cursor, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    constexpr int32_t digits = static_cast<int32_t>(NET_PAIR_CODE_DIGITS);
    float box_width = (width - 6.0f * CODE_GAP - 2.0f * GROUP_GAP) / static_cast<float>(digits);
    float box_x = x;
    size_t typed = std::strlen(code);
    for (int32_t i = 0; i < digits; i++)
    {
        touch_ui_rect(ui, box_x, y, box_width, CODE_HEIGHT, palette->panel_face);
        char digit[2] = {static_cast<size_t>(i) < typed ? code[i] : '\0', '\0'};
        if (digit[0] != '\0')
        {
            float digit_width = touch_ui_text_width(ui, digit, TouchText::big);
            touch_ui_text(ui, box_x + (box_width - digit_width) * 0.5f,
                          touch_ui_centered_text_y(ui, y, CODE_HEIGHT, TouchText::big), digit, palette->text_highlight,
                          TouchText::big);
        }
        if (cursor && static_cast<size_t>(i) == typed)
        {
            touch_ui_outline(ui, box_x, y, box_width, CODE_HEIGHT, SELECTED_EDGE, palette->highlight);
        }
        box_x += box_width + ((i % 3 == 2) ? GROUP_GAP : CODE_GAP);
    }
    return y + CODE_HEIGHT;
}

// The number pad: 1 to 9, 0 and ◀ (the last digit off), its keys
// `key_height` tall. Returns the y under it.
static float draw_keypad(PairScreen* pair, const TouchUi* ui, bool active, float x, float y, float width,
                         float key_height)
{
    static const char* const KEYS[11] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", PIXEL_FONT_UTF8_ARROW_LEFT};
    float key_width = (width - 3.0f * GAP) / 4.0f;
    for (int32_t i = 0; i < 11; i++)
    {
        int32_t column = i % 4;
        int32_t row = i / 4;
        float key_x = x + static_cast<float>(column) * (key_width + GAP);
        float key_y = y + static_cast<float>(row) * (key_height + GAP);
        float this_width = i == 10 ? 2.0f * key_width + GAP : key_width;   // ◀ takes the last two
        char id[16];
        std::snprintf(id, sizeof(id), "##key%d", static_cast<int>(i));
        if (!touch_ui_button(ui, id, key_x, key_y, this_width, key_height, KEYS[i], TouchButton::normal,
                             TouchText::big) ||
            !active)
        {
            continue;
        }
        size_t typed = std::strlen(pair->typed);
        if (i == 10 && typed > 0)
        {
            pair->typed[typed - 1] = '\0';
        }
        else if (i < 10 && typed < NET_PAIR_CODE_DIGITS)
        {
            pair->typed[typed] = KEYS[i][0];
            pair->typed[typed + 1] = '\0';
        }
    }
    return y + 3.0f * key_height + 2.0f * GAP;
}

// The four lamps, one lit at a time (1 2 3 4 3 2 1 ...), then `text`
// blinking (on more lines within `width`): something is being waited for.
static void draw_waiting(const TouchUi* ui, float x, float y, const char* text, float width)
{
    static const int32_t SEQUENCE[6] = {0, 1, 2, 3, 2, 1};
    uint64_t now = SDL_GetTicks();
    int32_t lit = SEQUENCE[(now / LAMP_STEP_MS) % 6];
    float lamp_y = y + (touch_ui_text_height(ui, TouchText::normal) - TOUCH_UI_LAMP) * 0.5f;
    for (int32_t i = 0; i < 4; i++)
    {
        uint32_t color = i == lit ? TOUCH_UI_LAMP_ONLINE : ui->palette.panel_face;
        touch_ui_rect(ui, x + static_cast<float>(i) * (TOUCH_UI_LAMP + LAMP_GAP), lamp_y, TOUCH_UI_LAMP, TOUCH_UI_LAMP,
                      color);
    }
    float text_x = x + 4.0f * (TOUCH_UI_LAMP + LAMP_GAP) + 6.0f;
    if ((now / BLINK_MS) % 2 == 0)
    {
        text_wrapped(ui, text_x, y, x + width - text_x, text, ui->palette.text_highlight);
    }
}

// "WAITING FOR <NAME>" (THE OTHER DEVICE when unknown), and `suffix`.
static void draw_waiting_for(const TouchUi* ui, float x, float y, const char* name, const char* suffix, float width)
{
    char text[160];
    std::snprintf(text, sizeof(text), "WAITING FOR %s%s", name[0] != '\0' ? name : "THE OTHER DEVICE", suffix);
    draw_waiting(ui, x, y, text, width);
}

// The device's card: its name and kind. Returns the y under it.
static float draw_device_card(const TouchUi* ui, const char* name, const char* kind, bool lamp, float x, float y,
                              float width)
{
    const RetroUiPalette* palette = &ui->palette;
    float card_height = 34.0f;
    touch_ui_rect(ui, x, y, width, card_height, palette->panel_face);
    float text_y = touch_ui_centered_text_y(ui, y, card_height, TouchText::big);
    float name_x = x + 10.0f;
    if (lamp)
    {
        touch_ui_lamp(ui, name_x, text_y + (touch_ui_text_height(ui, TouchText::big) - TOUCH_UI_LAMP) * 0.5f,
                      TOUCH_UI_LAMP_ONLINE);
        name_x += TOUCH_UI_LAMP + 8.0f;
    }
    touch_ui_text(ui, name_x, text_y, name, palette->text_highlight, TouchText::big);
    float kind_y = touch_ui_centered_text_y(ui, y, card_height, TouchText::normal);
    touch_ui_text(ui, x + width - 10.0f - touch_ui_text_width(ui, kind, TouchText::normal), kind_y, kind,
                  palette->text_disabled, TouchText::normal);
    return y + card_height;
}

// The device and what it lets this phone use (done, both modes).
static float draw_done(const TouchUi* ui, const PairView* view, float x, float y, float width)
{
    y = draw_device_card(ui, view->done_name, view->done_kind, true, x, y, width) + 12.0f;
    y = label_named(ui, x, y, "PERMISSIONS GRANTED ON", view->done_name, width);
    return text_wrapped(ui, x + 4.0f, y, width - 4.0f, view->done_granted, ui->palette.text_highlight);
}

// The red line: why the last try failed. Returns the y under it.
static float draw_error(const TouchUi* ui, const PairView* view, float x, float y, float width)
{
    if (view->error[0] == '\0')
    {
        return y;
    }
    return text_wrapped(ui, x + 4.0f, y, width - 4.0f, view->error, ui->palette.highlight) + 2.0f;
}

// One or two buttons at the bottom of a column (`main` primary over `other`;
// nullptr: none); side by side, `other` first, when `row`. Returns which was
// tapped: 1 main, 2 other, 0 none.
static int32_t draw_buttons(const TouchUi* ui, const char* main, bool main_enabled, const char* other, float x,
                            float bottom, float width, bool row)
{
    int32_t tapped = 0;
    TouchButton main_style = main_enabled ? TouchButton::primary : TouchButton::normal;
    if (row && main != nullptr && other != nullptr)
    {
        float half = (width - GAP) * 0.5f;
        float y = bottom - OTHER_HEIGHT;
        if (touch_ui_button(ui, "##other", x, y, half, OTHER_HEIGHT, other, TouchButton::normal, TouchText::normal))
        {
            tapped = 2;
        }
        if (touch_ui_button(ui, "##main", x + half + GAP, y, half, OTHER_HEIGHT, main, main_style, TouchText::big) &&
            main_enabled)
        {
            tapped = 1;
        }
        return tapped;
    }
    float main_y = bottom - MAIN_HEIGHT;
    if (other != nullptr)
    {
        float other_y = bottom - OTHER_HEIGHT;
        main_y = other_y - GAP - MAIN_HEIGHT;
        if (touch_ui_button(ui, "##other", x, other_y, width, OTHER_HEIGHT, other, TouchButton::normal,
                            TouchText::normal))
        {
            tapped = 2;
        }
    }
    if (main != nullptr && touch_ui_button(ui, "##main", x, main_y, width, MAIN_HEIGHT, main, main_style, TouchText::big) &&
        main_enabled)
    {
        tapped = 1;
    }
    return tapped;
}

// Where a screen's parts go: `left` for what to read, `right` for the pad,
// the status and the buttons (the same column in portrait).
struct PairLayout
{
    float left_x = 0.0f;
    float right_x = 0.0f;
    float y = 0.0f;        // under the tabs
    float column = 0.0f;
    float bottom = 0.0f;
    bool  landscape = false;
};

// PAIR A DEVICE.
static void draw_device(PairScreen* pair, const TouchUi* ui, const PairView* view, const PairLayout* at, PairTap* tap)
{
    float column = at->column;
    if (view->step == PairStep::done)
    {
        draw_done(ui, view, at->left_x, at->y, column);
        if (draw_buttons(ui, "DONE", true, nullptr, at->right_x, at->bottom, column, at->landscape) == 1)
        {
            *tap = PairTap::done;
        }
        return;
    }
    if (view->step == PairStep::list)
    {
        float end = draw_list(pair, ui, view, at->left_x, at->y, column);
        draw_error(ui, view, at->landscape ? at->right_x : at->left_x, at->landscape ? at->y : end + 4.0f, column);
        bool picked = pair->selected >= 0 && pair->selected <= view->found_count;
        if (draw_buttons(ui, "PAIR", picked, nullptr, at->right_x, at->bottom, column, at->landscape) == 1)
        {
            *tap = PairTap::pair;
        }
        return;
    }
    // Typing: the code shown on the device.
    float end = label_named(ui, at->left_x, at->y, "TYPE THE CODE SHOWN ON", view->host_name, column);
    bool active = !view->checking;
    end = draw_code(ui, pair->typed, active, at->left_x, end, column) + 8.0f;
    if (view->wrong)
    {
        end = text_wrapped(ui, at->left_x + 4.0f, end, column - 4.0f, "WRONG CODE: TYPE THE NEW ONE",
                           ui->palette.highlight) + 2.0f;
    }
    end = draw_error(ui, view, at->left_x, end, column);
    bool waiting = !view->code_ready || view->checking;
    if (at->landscape && waiting)
    {
        draw_waiting_for(ui, at->left_x, end + 4.0f, view->host_name, "", column);
    }
    // The pad in what's left above the buttons (and, in portrait, the waiting line).
    float pad_y = at->landscape ? at->y : end + 4.0f;
    float buttons = at->landscape ? OTHER_HEIGHT : MAIN_HEIGHT + GAP + OTHER_HEIGHT;
    float waiting_room = at->landscape ? 0.0f : 2.0f * line_step(ui) + 8.0f;
    float key_height = (at->bottom - buttons - GAP - waiting_room - pad_y - 2.0f * GAP) / 3.0f;
    key_height = key_height > KEY_HEIGHT ? KEY_HEIGHT : (key_height < KEY_MIN ? KEY_MIN : key_height);
    pad_y = draw_keypad(pair, ui, active, at->right_x, pad_y, column, key_height);
    if (!at->landscape && waiting)
    {
        draw_waiting_for(ui, at->right_x, pad_y + 8.0f, view->host_name, "", column);
    }
    bool complete = net_pair_code_valid(pair->typed) && view->code_ready && !view->checking;
    int32_t tapped = draw_buttons(ui, "PAIR", complete, "CANCEL", at->right_x, at->bottom, column, at->landscape);
    if (tapped == 1)
    {
        *tap = PairTap::submit;
    }
    else if (tapped == 2)
    {
        *tap = PairTap::cancel;
    }
}

// GENERATE PAIRING CODE.
static void draw_shown(const TouchUi* ui, const PairView* view, const PairLayout* at, PairTap* tap)
{
    float column = at->column;
    float status_x = at->landscape ? at->right_x : at->left_x;
    if (view->code_step == PairCodeStep::done)
    {
        draw_done(ui, view, at->left_x, at->y, column);
        if (draw_buttons(ui, "DONE", true, nullptr, at->right_x, at->bottom, column, at->landscape) == 1)
        {
            *tap = PairTap::done;
        }
        return;
    }
    if (view->code_step == PairCodeStep::chosen)
    {
        float end = draw_device_card(ui, view->done_name, view->done_kind, false, at->left_x, at->y, column) + 12.0f;
        end = text_wrapped(ui, at->left_x + 4.0f, end, column - 4.0f, "TYPED YOUR CODE.", ui->palette.text) + 6.0f;
        draw_waiting_for(ui, status_x, at->landscape ? at->y : end, view->done_name, "", column);
        if (draw_buttons(ui, nullptr, false, "CANCEL", at->right_x, at->bottom, column, at->landscape) == 2)
        {
            *tap = PairTap::cancel;
        }
        return;
    }
    if (view->code_step == PairCodeStep::starting)
    {
        float end = draw_error(ui, view, at->left_x, at->y, column);
        if (view->error[0] == '\0')
        {
            draw_waiting(ui, at->left_x, end, "MAKING A CODE", column);
        }
        const char* again = view->error[0] != '\0' ? "TRY AGAIN" : nullptr;
        if (draw_buttons(ui, again, true, nullptr, at->right_x, at->bottom, column, at->landscape) == 1)
        {
            *tap = PairTap::retry;
        }
        return;
    }
    float end = label(ui, at->left_x, at->y, "TYPE THIS CODE ON THE OTHER DEVICE", column);
    end = draw_code(ui, view->shown_code, false, at->left_x, end, column);
    draw_waiting_for(ui, status_x, at->landscape ? at->y : end + 12.0f, "", " · 5 MIN", column);
    int32_t tapped = draw_buttons(ui, "SEND CODE", true, "CANCEL", at->right_x, at->bottom, column, at->landscape);
    if (tapped == 1)
    {
        *tap = PairTap::send_code;
    }
    else if (tapped == 2)
    {
        *tap = PairTap::cancel;
    }
}

NavAction pair_screen_draw(PairScreen* pair, const TouchUi* ui, const PairView* view, PairTap* tap)
{
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;
    *tap = PairTap::none;
    if (pair->selected > view->found_count)
    {
        pair->selected = -1;
    }

    bool done = (pair->mode == PairMode::device && view->step == PairStep::done) ||
                (pair->mode == PairMode::code && view->code_step == PairCodeStep::done);
    float header_y = landscape ? 6.0f : MARGIN;
    if (touch_ui_button(ui, "##back", MARGIN - 2.0f, header_y, HEADER_HEIGHT, HEADER_HEIGHT, PIXEL_FONT_UTF8_ARROW_LEFT,
                        TouchButton::plain, TouchText::big))
    {
        action = NavAction::home;
    }
    touch_ui_text(ui, MARGIN + HEADER_HEIGHT + 4.0f, touch_ui_centered_text_y(ui, header_y, HEADER_HEIGHT, TouchText::big),
                  done ? "PAIRED" : "PAIR", ui->palette.text_highlight, TouchText::big);
    if (touch_ui_button(ui, "##rotate", width - MARGIN - HEADER_HEIGHT, header_y, HEADER_HEIGHT, HEADER_HEIGHT,
                        PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
    {
        action = NavAction::rotate;
    }

    PairLayout at;
    at.landscape = landscape;
    at.column = landscape ? (width - 3.0f * MARGIN) * 0.5f : width - 2.0f * MARGIN;
    at.left_x = MARGIN;
    at.right_x = landscape ? MARGIN + at.column + MARGIN : MARGIN;
    at.bottom = height - (landscape ? 6.0f : 12.0f);
    at.y = header_y + HEADER_HEIGHT + (landscape ? 6.0f : 10.0f);
    if (!done)
    {
        at.y = draw_tabs(pair, ui, view, MARGIN, at.y, width - 2.0f * MARGIN, tap) + (landscape ? 8.0f : 12.0f);
    }
    if (pair->mode == PairMode::device)
    {
        draw_device(pair, ui, view, &at, tap);
    }
    else
    {
        draw_shown(ui, view, &at, tap);
    }
    return action;
}

#include "devices_screen.h"

#include <cstdio>

#include "pixel_font_imgui.h"

// Layout, in retro pixels.
constexpr float MARGIN        = 8.0f;
constexpr float HEADER_HEIGHT = 24.0f;
constexpr float CARD_PADDING  = 8.0f;
constexpr float CARD_TITLE    = 16.0f;   // the port's name line
constexpr float CHIP_HEIGHT   = 28.0f;   // one device
constexpr float GAP           = 4.0f;
constexpr float EMPTY_HEIGHT  = 16.0f;   // "NOBODY" in an empty port

void devices_screen_init(DevicesScreen* devices)
{
    static const char* const NAMES[] = {"THIS PHONE", "8BITDO PRO 2", "PHONE 2", "PHONE 3", "PHONE 4"};
    static const int32_t PORTS[] = {0, 0, 1, 2, 2};
    devices->count = static_cast<int32_t>(sizeof(NAMES) / sizeof(NAMES[0]));
    for (int32_t i = 0; i < devices->count; i++)
    {
        devices->names[i] = NAMES[i];
        devices->port[i] = PORTS[i];
    }
    devices->selected = -1;
}

static int32_t devices_in_port(const DevicesScreen* devices, int32_t port)
{
    int32_t count = 0;
    for (int32_t i = 0; i < devices->count; i++)
    {
        count += devices->port[i] == port ? 1 : 0;
    }
    return count;
}

// A port that can take the picked device (it's in another port).
static bool can_move_here(const DevicesScreen* devices, int32_t port)
{
    return devices->selected >= 0 && devices->port[devices->selected] != port;
}

static float card_height(const DevicesScreen* devices, int32_t port)
{
    int32_t count = devices_in_port(devices, port);
    float content = count == 0 ? EMPTY_HEIGHT : static_cast<float>(count) * CHIP_HEIGHT + static_cast<float>(count - 1) * GAP;
    float move = can_move_here(devices, port) ? GAP + CHIP_HEIGHT : 0.0f;
    return CARD_PADDING + CARD_TITLE + content + move + CARD_PADDING;
}

// One port: its name, MOVE HERE while a device from another port is picked,
// and its devices (the picked one in the highlight color).
static void draw_port(DevicesScreen* devices, const TouchUi* ui, int32_t port, float x, float y, float width, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, height, palette->panel_face);
    char title[24];
    std::snprintf(title, sizeof(title), "PORT %d", static_cast<int>(port + 1));
    touch_ui_text(ui, x + CARD_PADDING + 2.0f, y + CARD_PADDING, title, palette->text_disabled, TouchText::normal);

    float chip_y = y + CARD_PADDING + CARD_TITLE;
    float chip_width = width - 2.0f * CARD_PADDING;
    int32_t shown = 0;
    for (int32_t i = 0; i < devices->count; i++)
    {
        if (devices->port[i] != port)
        {
            continue;
        }
        char id[24];
        std::snprintf(id, sizeof(id), "##device%d", static_cast<int>(i));
        TouchButton style = i == devices->selected ? TouchButton::primary : TouchButton::normal;
        if (touch_ui_button(ui, id, x + CARD_PADDING, chip_y, chip_width, CHIP_HEIGHT, devices->names[i], style,
                            TouchText::normal))
        {
            // Tap to pick it, tap again to put it down.
            devices->selected = devices->selected == i ? -1 : i;
        }
        chip_y += CHIP_HEIGHT + GAP;
        shown++;
    }
    if (shown == 0)
    {
        touch_ui_text(ui, x + CARD_PADDING + 2.0f, chip_y + 2.0f, "NOBODY", palette->text_disabled, TouchText::normal);
        chip_y += EMPTY_HEIGHT + GAP;
    }

    // While a device from another port is picked: MOVE HERE, under the list.
    if (can_move_here(devices, port))
    {
        char id[24];
        std::snprintf(id, sizeof(id), "##move%d", static_cast<int>(port));
        if (touch_ui_button(ui, id, x + CARD_PADDING, chip_y, chip_width, CHIP_HEIGHT, "MOVE HERE", TouchButton::ghost,
                            TouchText::normal))
        {
            devices->port[devices->selected] = port;
            devices->selected = -1;
        }
    }
}

NavAction devices_screen_draw(DevicesScreen* devices, const TouchUi* ui, const char* host)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool landscape = width > height;
    NavAction action = NavAction::none;

    float header_y = landscape ? 6.0f : MARGIN;
    if (touch_ui_button(ui, "##back", MARGIN - 2.0f, header_y, HEADER_HEIGHT, HEADER_HEIGHT, PIXEL_FONT_UTF8_ARROW_LEFT,
                        TouchButton::plain, TouchText::big))
    {
        action = NavAction::home;
    }
    float title_x = MARGIN + HEADER_HEIGHT + 4.0f;
    float title_y = touch_ui_centered_text_y(ui, header_y, HEADER_HEIGHT, TouchText::big);
    touch_ui_text(ui, title_x, title_y, "DEVICES", palette->text_highlight, TouchText::big);
    if (touch_ui_button(ui, "##rotate", width - MARGIN - HEADER_HEIGHT, header_y, HEADER_HEIGHT, HEADER_HEIGHT,
                        PIXEL_FONT_UTF8_ROTATE, TouchButton::plain, TouchText::big))
    {
        action = NavAction::rotate;
    }

    float content_y = header_y + HEADER_HEIGHT + (landscape ? 6.0f : 12.0f);
    touch_ui_text(ui, MARGIN + 2.0f, content_y, host, palette->text_highlight, TouchText::normal);
    content_y += 12.0f;
    const char* hint = devices->selected >= 0 ? "TAP MOVE HERE ON ITS NEW PORT" : "TAP A DEVICE TO MOVE IT";
    touch_ui_text(ui, MARGIN + 2.0f, content_y, hint, palette->text_disabled, TouchText::normal);
    content_y += 14.0f;

    // Landscape: one column per port, side by side.
    int32_t columns = landscape ? DEVICES_PORTS : 1;
    float column_width = (width - static_cast<float>(columns + 1) * MARGIN) / static_cast<float>(columns);
    float column_y[DEVICES_PORTS] = {content_y, content_y, content_y, content_y};
    for (int32_t port = 0; port < DEVICES_PORTS; port++)
    {
        int32_t column = port % columns;
        float x = MARGIN + static_cast<float>(column) * (column_width + MARGIN);
        float card = card_height(devices, port);
        draw_port(devices, ui, port, x, column_y[column], column_width, card);
        column_y[column] += card + GAP;
    }
    return action;
}

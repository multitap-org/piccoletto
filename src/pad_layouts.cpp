#include "pad_layouts.h"

#include "pixel_font_imgui.h"

// Short names for the tables.
constexpr PadPartKind BUTTON = PadPartKind::button;
constexpr PadPartKind STICK = PadPartKind::stick;
constexpr TouchText NORMAL = TouchText::normal;
constexpr TouchText BIG = TouchText::big;
constexpr PadGroup LSH = PadGroup::left_shoulders;
constexpr PadGroup RSH = PadGroup::right_shoulders;
constexpr PadGroup DPAD = PadGroup::dpad;
constexpr PadGroup FACE = PadGroup::face;
constexpr PadGroup CENTER = PadGroup::center;
constexpr PadGroup LSTICK = PadGroup::left_stick;
constexpr PadGroup RSTICK = PadGroup::right_stick;

// GAMEPAD, portrait (390 x 844 dp): shoulders on top, the body below with the
// d-pad and the face buttons, SELECT / START / the piccolo menu, both sticks
// at the bottom. (The header line is the screen's, not the layout's.)
static const PadPart GAMEPAD_PORTRAIT[] = {
    {"##l2", "L2", 16, 92, 160, 40, BUTTON, NORMAL, LSH},
    {"##l", "L", 16, 140, 160, 44, BUTTON, NORMAL, LSH},
    {"##r2", "R2", 214, 92, 160, 40, BUTTON, NORMAL, RSH},
    {"##r", "R", 214, 140, 160, 44, BUTTON, NORMAL, RSH},
    {"##dpad_center", "", 76, 328, 56, 56, BUTTON, NORMAL, DPAD},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 76, 272, 56, 56, BUTTON, NORMAL, DPAD},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 76, 384, 56, 56, BUTTON, NORMAL, DPAD},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 20, 328, 56, 56, BUTTON, NORMAL, DPAD},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 132, 328, 56, 56, BUTTON, NORMAL, DPAD},
    {"##x", "X", 258, 272, 56, 56, BUTTON, BIG, FACE},
    {"##b", "B", 258, 384, 56, 56, BUTTON, BIG, FACE},
    {"##y", "Y", 202, 328, 56, 56, BUTTON, BIG, FACE},
    {"##a", "A", 314, 328, 56, 56, BUTTON, BIG, FACE},
    {"##select", "SELECT", 100, 496, 88, 44, BUTTON, NORMAL, CENTER},
    {"##start", "START", 202, 496, 88, 44, BUTTON, NORMAL, CENTER},
    {"##guide", PIXEL_FONT_UTF8_MENU, 167, 556, 56, 40, BUTTON, NORMAL, CENTER},
    {"##lstick", "", 48, 652, 112, 112, STICK, NORMAL, LSTICK},
    {"##rstick", "", 230, 652, 112, 112, STICK, NORMAL, RSTICK},
};

// GAMEPAD, landscape (780 x 390 dp): the pad's body is the whole screen
// (Andrés: the background fills the screen so the pad can be bigger), so the
// parts spread to its edges, laid out like piccolo's pad picture. The
// shoulders stay clear of the title bar's icons in the top right corner
// (on a 844 x 390 phone they start at 652 dp), the status line (host,
// port, ping) goes at the bottom.
static const PadPart GAMEPAD_LANDSCAPE[] = {
    {"##l2", "L2", 170, 8, 140, 36, BUTTON, NORMAL, LSH},
    {"##l", "L", 150, 52, 180, 40, BUTTON, NORMAL, LSH},
    {"##r2", "R2", 470, 8, 140, 36, BUTTON, NORMAL, RSH},
    {"##r", "R", 450, 52, 180, 40, BUTTON, NORMAL, RSH},
    {"##dpad_center", "", 160, 172, 60, 60, BUTTON, NORMAL, DPAD},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 160, 112, 60, 60, BUTTON, NORMAL, DPAD},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 160, 232, 60, 60, BUTTON, NORMAL, DPAD},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 100, 172, 60, 60, BUTTON, NORMAL, DPAD},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 220, 172, 60, 60, BUTTON, NORMAL, DPAD},
    {"##x", "X", 560, 112, 60, 60, BUTTON, BIG, FACE},
    {"##b", "B", 560, 232, 60, 60, BUTTON, BIG, FACE},
    {"##y", "Y", 500, 172, 60, 60, BUTTON, BIG, FACE},
    {"##a", "A", 620, 172, 60, 60, BUTTON, BIG, FACE},
    {"##select", "SELECT", 298, 126, 88, 40, BUTTON, NORMAL, CENTER},
    {"##start", "START", 394, 126, 88, 40, BUTTON, NORMAL, CENTER},
    {"##guide", PIXEL_FONT_UTF8_MENU, 360, 178, 60, 40, BUTTON, NORMAL, CENTER},
    {"##lstick", "", 280, 256, 84, 84, STICK, NORMAL, LSTICK},
    {"##rstick", "", 416, 256, 84, 84, STICK, NORMAL, RSTICK},
};

// REMOTE PLAY, portrait, under the picture (390 x 380 dp: the controls of the
// mockup's lower half, shrunk to whatever room the picture leaves).
static const PadPart REMOTE_BELOW[] = {
    {"##l", "L", 24, 16, 120, 40, BUTTON, NORMAL, LSH},
    {"##r", "R", 246, 16, 120, 40, BUTTON, NORMAL, RSH},
    {"##dpad_center", "", 70, 140, 52, 52, BUTTON, NORMAL, DPAD},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 70, 88, 52, 52, BUTTON, NORMAL, DPAD},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 70, 192, 52, 52, BUTTON, NORMAL, DPAD},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 18, 140, 52, 52, BUTTON, NORMAL, DPAD},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 122, 140, 52, 52, BUTTON, NORMAL, DPAD},
    {"##y", "Y", 214, 82, 52, 52, BUTTON, BIG, FACE},
    {"##x", "X", 314, 62, 52, 52, BUTTON, BIG, FACE},
    {"##b", "B", 214, 166, 68, 68, BUTTON, BIG, FACE},
    {"##a", "A", 298, 118, 68, 68, BUTTON, BIG, FACE},
    {"##select", "SELECT", 104, 284, 80, 32, BUTTON, NORMAL, CENTER},
    {"##start", "START", 196, 284, 80, 32, BUTTON, NORMAL, CENTER},
};

// REMOTE PLAY, portrait, over a tall picture (390 x 844 dp): the controls
// over its lower half (the DS's bottom screen), small and see-through.
static const PadPart REMOTE_OVERLAY_PORTRAIT[] = {
    {"##l", "L", 12, 452, 96, 36, BUTTON, NORMAL, LSH},
    {"##r", "R", 282, 452, 96, 36, BUTTON, NORMAL, RSH},
    {"##dpad_center", "", 64, 620, 46, 46, BUTTON, NORMAL, DPAD},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 64, 574, 46, 46, BUTTON, NORMAL, DPAD},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 64, 666, 46, 46, BUTTON, NORMAL, DPAD},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 18, 620, 46, 46, BUTTON, NORMAL, DPAD},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 110, 620, 46, 46, BUTTON, NORMAL, DPAD},
    {"##x", "X", 280, 574, 46, 46, BUTTON, BIG, FACE},
    {"##b", "B", 280, 666, 46, 46, BUTTON, BIG, FACE},
    {"##y", "Y", 234, 620, 46, 46, BUTTON, BIG, FACE},
    {"##a", "A", 326, 620, 46, 46, BUTTON, BIG, FACE},
    {"##select", "SELECT", 110, 770, 80, 32, BUTTON, NORMAL, CENTER},
    {"##start", "START", 200, 770, 80, 32, BUTTON, NORMAL, CENTER},
};

// REMOTE PLAY, landscape (844 x 390 dp): over the picture, the d-pad on the
// left, the face buttons on the right (all four the same size, Andrés), the
// shoulders in the top corners under the title bar's icons.
static const PadPart REMOTE_OVERLAY_LANDSCAPE[] = {
    {"##l", "L", 16, 64, 160, 40, BUTTON, NORMAL, LSH},
    {"##r", "R", 668, 64, 160, 40, BUTTON, NORMAL, RSH},
    {"##dpad_center", "", 84, 204, 52, 52, BUTTON, NORMAL, DPAD},
    {"##up", PIXEL_FONT_UTF8_ARROW_UP, 84, 152, 52, 52, BUTTON, NORMAL, DPAD},
    {"##down", PIXEL_FONT_UTF8_ARROW_DOWN, 84, 256, 52, 52, BUTTON, NORMAL, DPAD},
    {"##left", PIXEL_FONT_UTF8_ARROW_LEFT, 32, 204, 52, 52, BUTTON, NORMAL, DPAD},
    {"##right", PIXEL_FONT_UTF8_ARROW_RIGHT, 136, 204, 52, 52, BUTTON, NORMAL, DPAD},
    {"##x", "X", 680, 132, 64, 64, BUTTON, BIG, FACE},
    {"##y", "Y", 612, 200, 64, 64, BUTTON, BIG, FACE},
    {"##a", "A", 748, 200, 64, 64, BUTTON, BIG, FACE},
    {"##b", "B", 680, 268, 64, 64, BUTTON, BIG, FACE},
    {"##select", "SELECT", 24, 334, 84, 40, BUTTON, NORMAL, CENTER},
    {"##start", "START", 116, 334, 84, 40, BUTTON, NORMAL, CENTER},
};

static void define(PadLayouts* layouts, PadLayoutId id, const char* name, const PadPart* parts, int32_t count, float width,
                   float height, float alpha)
{
    PadLayout* layout = pad_layouts_get(layouts, id);
    layout->name = name;
    layout->parts = parts;
    layout->count = count;
    layout->width = width;
    layout->height = height;
    layout->default_alpha = alpha;
    layout->overlay = alpha < 1.0f;   // the see-through ones go over the picture
    pad_layout_reset(layout);
}

void pad_layouts_init(PadLayouts* layouts)
{
    define(layouts, PadLayoutId::gamepad_portrait, "gamepad_portrait", GAMEPAD_PORTRAIT,
           static_cast<int32_t>(sizeof(GAMEPAD_PORTRAIT) / sizeof(GAMEPAD_PORTRAIT[0])), 390.0f, 844.0f, 1.0f);
    define(layouts, PadLayoutId::gamepad_landscape, "gamepad_landscape", GAMEPAD_LANDSCAPE,
           static_cast<int32_t>(sizeof(GAMEPAD_LANDSCAPE) / sizeof(GAMEPAD_LANDSCAPE[0])), 780.0f, 390.0f, 1.0f);
    define(layouts, PadLayoutId::remote_below, "remote_below", REMOTE_BELOW,
           static_cast<int32_t>(sizeof(REMOTE_BELOW) / sizeof(REMOTE_BELOW[0])), 390.0f, 380.0f, 1.0f);
    define(layouts, PadLayoutId::remote_overlay_portrait, "remote_overlay_portrait", REMOTE_OVERLAY_PORTRAIT,
           static_cast<int32_t>(sizeof(REMOTE_OVERLAY_PORTRAIT) / sizeof(REMOTE_OVERLAY_PORTRAIT[0])), 390.0f, 844.0f, 0.5f);
    define(layouts, PadLayoutId::remote_overlay_landscape, "remote_overlay_landscape", REMOTE_OVERLAY_LANDSCAPE,
           static_cast<int32_t>(sizeof(REMOTE_OVERLAY_LANDSCAPE) / sizeof(REMOTE_OVERLAY_LANDSCAPE[0])), 844.0f, 390.0f, 0.6f);
}

PadLayout* pad_layouts_get(PadLayouts* layouts, PadLayoutId id)
{
    return &layouts->layouts[static_cast<int32_t>(id)];
}

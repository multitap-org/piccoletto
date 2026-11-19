#pragma once

// EDIT LAYOUT: drag any group of the pad (d-pad, buttons, a stick...) to move
// it, SIZE - / + resizes the picked group, OPACITY - / + the whole layout
// (for overlays), SNAP keeps moves on an 8 dp grid, RESET puts everything
// back, DONE leaves (the app saves). The toolbar sits at the top or the
// bottom, away from the picked group. Pinch to resize comes with
// multi-touch.

#include <cstdint>

#include "pad_layout.h"
#include "touch_ui.h"

struct PadEditor
{
    bool     active   = false;
    PadGroup selected = PadGroup::dpad;
    bool     snap     = true;
};

// Draws the layout being edited instead of playing it. True when DONE was tapped.
bool pad_editor_draw(PadEditor* editor, const TouchUi* ui, const PadFrame* frame, PadLayout* layout);

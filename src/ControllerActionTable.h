#pragma once

#include <cstddef>

namespace ControllerActionTable
{
    enum class Unfocused : unsigned char
    {
        No,
        Always,
        Browsing,
    };

    struct Binding
    {
        const char* button;
        const char* action;
        Unfocused unfocused;
    };

    struct Prompt
    {
        const char* buttons;
        const char* keyboard;
        const char* label;
        const char* elementId;
    };

    inline constexpr Binding kWorkshopBindings[] = {
        { "DUp", "nav.up", Unfocused::Browsing },
        { "DDown", "nav.down", Unfocused::Browsing },
        { "DLeft", "nav.left", Unfocused::Browsing },
        { "DRight", "nav.right", Unfocused::Browsing },
        { "A", "workshop.place", Unfocused::No },
        { "B", "workshop.back", Unfocused::Browsing },
        { "X", "workshop.scrap", Unfocused::Browsing },
        { "Y", "workshop.favorite", Unfocused::Browsing },
        { "Back", "workshop.panel_focus", Unfocused::Always },
        { "LS", "workshop.free_place", Unfocused::Always },
        { "RS", "workshop.free_cam", Unfocused::Always },
        { "Start", "workshop.snap", Unfocused::Always },
        { "LB", "workshop.shoulder_left", Unfocused::Browsing },
        { "RB", "workshop.shoulder_right", Unfocused::Browsing },
        { "LT", "workshop.mod_rotate", Unfocused::No },
        { "RT", "workshop.mod_move", Unfocused::No },
    };

    inline constexpr Prompt kWorkshopPrompts[] = {
        { "DLeft/DRight", "A/D", "Browse", "" },
        { "DUp/DDown", "Mouse", "Menus", "" },
        { "LB/RB", "W/S", "Tabs", "" },
        { "Back", "C", "Panel Focus", "" },
        { "A", "E", "Place", "act-place" },
        { "B", "Tab", "Back", "act-cancel" },
        { "X", "R", "Scrap", "act-scrap" },
        { "Y", "F/Q", "Favorite + Tag", "act-tag" },
        { "LS", "F3", "Place in Red", "" },
        { "RS", "F4", "Free Camera (Back exits)", "" },
        { "Start", "F6", "Snap (+LT Object, +RT Reset)", "" },
        { "LT", "Num 4/8/7", "Hold: Rotate", "" },
        { "RT", "Home/Ins/PgUp", "Hold: Move", "" },
    };

    inline constexpr Binding kCraftingBindings[] = {
        { "DUp", "nav.up", Unfocused::No },
        { "DDown", "nav.down", Unfocused::No },
        { "DLeft", "nav.left", Unfocused::No },
        { "DRight", "nav.right", Unfocused::No },
        { "A", "craft.confirm", Unfocused::No },
        { "B", "craft.back", Unfocused::No },
        { "LB", "craft.category_prev", Unfocused::No },
        { "RB", "craft.category_next", Unfocused::No },
    };

    inline constexpr Prompt kCraftingPrompts[] = {
        { "LB/RB", "PgUp/PgDn", "Category", "" },
        { "DUp/DDown/DLeft/DRight", "Arrows", "Recipes", "" },
        { "A", "Enter", "Craft", "" },
        { "B", "Esc", "Back", "" },
    };

    inline constexpr Binding kWeaponBenchBindings[] = {
        { "DUp", "nav.up", Unfocused::No },
        { "DDown", "nav.down", Unfocused::No },
        { "DLeft", "nav.left", Unfocused::No },
        { "DRight", "nav.right", Unfocused::No },
        { "A", "bench.confirm", Unfocused::No },
        { "B", "bench.back", Unfocused::No },
        { "LB", "bench.slot_prev", Unfocused::No },
        { "RB", "bench.slot_next", Unfocused::No },
    };

    inline constexpr Prompt kWeaponBenchPrompts[] = {
        { "DUp/DDown/DLeft/DRight", "Arrows", "Navigate", "" },
        { "A", "Enter", "Select", "" },
        { "B", "Esc", "Back", "" },
        { "LB/RB", "Left/Right", "Slot", "" },
    };
}

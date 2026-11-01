#pragma once

namespace crafting::bypass
{
    struct MenuEventFacts
    {
        bool opening = false;
        bool eventIsCraftMenu = false;

        bool weOwnAnOpenBench = false;   // g_open
        bool benchMenuRegistered = false;  // RE::UI has CookingMenu or ChemistryMenu
    };

    [[nodiscard]] constexpr bool ShouldReportBypass(const MenuEventFacts& a_f) noexcept
    {
        if (!a_f.opening) return false;

        if (a_f.eventIsCraftMenu) return false;

        if (a_f.weOwnAnOpenBench) return false;

        return a_f.benchMenuRegistered;
    }
}

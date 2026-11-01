#pragma once

#include <atomic>
#include <cstdint>

namespace crafting::dbg
{
    enum class Cat : std::uint8_t
    {
        General,     // lifecycle, config, menu open/close
        Input,       // raw input, key routing, wheel        (per-frame -- default off)
        Placement,   // ghost placement, ground snap, queue  (per-frame -- default off)
        Workshop,    // workshop free-place / free camera
        Recipes,     // cobj cache, category sync, queries
        Bench,       // workbench upgrades, co-save
        Dispatch,    // game-thread task dispatch
        View,        // PrismaUI view lifecycle and JS bridge
        Count
    };

    inline constexpr const char* kCatName[] = {
        "General", "Input", "Placement", "Workshop", "Recipes", "Bench", "Dispatch", "View"
    };
    static_assert(sizeof(kCatName) / sizeof(kCatName[0]) == static_cast<std::size_t>(Cat::Count));

    inline constexpr bool kDefaultOnWithMaster[] = {
        true,   // General
        false,  // Input      -- per-frame
        false,  // Placement  -- per-frame
        true,   // Workshop
        true,   // Recipes
        true,   // Bench
        true,   // Dispatch
        true,   // View
    };
    static_assert(sizeof(kDefaultOnWithMaster) / sizeof(kDefaultOnWithMaster[0]) ==
                  static_cast<std::size_t>(Cat::Count));

    inline std::atomic<bool> g_enabled[static_cast<std::size_t>(Cat::Count)]{};

    [[nodiscard]] inline bool On(Cat a_cat) noexcept
    {
        return g_enabled[static_cast<std::size_t>(a_cat)].load(std::memory_order_relaxed);
    }

    inline void Set(Cat a_cat, bool a_on) noexcept
    {
        g_enabled[static_cast<std::size_t>(a_cat)].store(a_on, std::memory_order_relaxed);
    }

    [[nodiscard]] constexpr bool Resolve(bool a_master, bool a_defaultOnWithMaster,
                                         int a_override) noexcept
    {
        if (!a_master) return false;
        if (a_override >= 0) return a_override != 0;
        return a_defaultOnWithMaster;
    }
}

#define DLOG(CAT, ...)                                                        \
    do {                                                                      \
        if (::crafting::dbg::On(::crafting::dbg::Cat::CAT)) {                 \
            logger::debug("[" #CAT "] " __VA_ARGS__);                         \
        }                                                                     \
    } while (false)

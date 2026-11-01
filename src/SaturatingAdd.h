#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace crafting
{
    [[nodiscard]] inline std::uint32_t SaturateAddU32(std::uint32_t a_lhs, std::uint32_t a_rhs) noexcept
    {
        const std::uint64_t total =
            static_cast<std::uint64_t>(a_lhs) + static_cast<std::uint64_t>(a_rhs);
        return static_cast<std::uint32_t>((std::min)(total, static_cast<std::uint64_t>(0xFFFFFFFFu)));
    }

    [[nodiscard]] inline std::uint64_t SaturateAddU64(std::uint64_t a_lhs, std::uint64_t a_rhs) noexcept
    {
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        return a_lhs > max - a_rhs ? max : a_lhs + a_rhs;
    }

    [[nodiscard]] inline std::uint64_t SaturatePositiveMulU64(std::uint64_t a_count,
                                                              std::int64_t a_perItem) noexcept
    {
        if (a_count == 0 || a_perItem <= 0) return 0;
        const auto perItem = static_cast<std::uint64_t>(a_perItem);
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        return a_count > max / perItem ? max : a_count * perItem;
    }
}

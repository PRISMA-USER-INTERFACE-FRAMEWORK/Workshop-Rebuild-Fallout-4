#pragma once

#include <cstdint>
#include <vector>

namespace crafting::bounds
{
    struct Range
    {
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;

        [[nodiscard]] constexpr bool valid() const noexcept { return begin != 0 && end > begin; }
    };

    [[nodiscard]] std::vector<Range> FunctionSegmentsAt(std::uintptr_t a_address) noexcept;
}

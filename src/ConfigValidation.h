#pragma once

#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace CraftingConfig::Validation
{
    inline bool ParseHex(std::string_view text, std::uint32_t& out)
    {
        if (text.empty() || text.size() > 8) return false;
        std::uint32_t value = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (ec != std::errc{} || end != text.data() + text.size() || value == 0) return false;
        out = value;
        return true;
    }

    inline bool IsConditionType(std::string_view type)
    {
        return type == "perk" || type == "global" || type == "itemcount" || type == "keyword";
    }

    inline bool IsConditionOp(std::string_view op)
    {
        return op == "=" || op == "==" || op == "!=" || op == ">=" || op == ">" ||
               op == "<=" || op == "<";
    }
}

#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace crafting
{
    inline std::vector<std::string> SplitUtf8Chunks(std::string_view input, std::size_t maxBytes)
    {
        std::vector<std::string> chunks;
        if (!maxBytes) return chunks;
        for (std::size_t pos = 0; pos < input.size();) {
            std::size_t end = (std::min)(input.size(), pos + (std::min)(maxBytes, input.size() - pos));
            while (end < input.size() && end > pos &&
                   (static_cast<unsigned char>(input[end]) & 0xC0) == 0x80) --end;
            if (end == pos) return {};
            chunks.emplace_back(input.substr(pos, end - pos));
            pos = end;
        }
        return chunks;
    }
}

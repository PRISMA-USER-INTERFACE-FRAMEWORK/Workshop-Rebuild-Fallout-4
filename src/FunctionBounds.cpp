#include "PCH.h"

#include "FunctionBounds.h"

#include <algorithm>

namespace crafting::bounds
{
    namespace
    {
        struct RuntimeFunction
        {
            std::uint32_t beginRva;
            std::uint32_t endRva;
            std::uint32_t unwindRva;
        };
        static_assert(sizeof(RuntimeFunction) == 12);

        constexpr std::uint8_t kChainInfo = 0x4;

        constexpr std::uint8_t kMaxKnownVersion = 2;

        // A malformed or hostile chain must not spin. Real chains are one or two links.
        constexpr int kMaxChainDepth = 32;

        struct Directory
        {
            std::uintptr_t          base = 0;
            const RuntimeFunction*  entries = nullptr;
            std::size_t             count = 0;
        };

        [[nodiscard]] Directory ReadDirectory(std::uintptr_t a_base) noexcept
        {
            Directory d{};
            if (!a_base) return d;

            const auto* image = reinterpret_cast<const std::uint8_t*>(a_base);
            const auto peOff = *reinterpret_cast<const std::uint32_t*>(image + 0x3C);
            const auto* nt = image + peOff;
            if (*reinterpret_cast<const std::uint32_t*>(nt) != 0x00004550u) return d;  // "PE\0\0"

            const auto* opt = nt + 4 + 20;
            if (*reinterpret_cast<const std::uint16_t*>(opt) != 0x20Bu) return d;
            const auto* dirEntry = opt + 0x70 + 3 * 8;
            const auto dirRva = *reinterpret_cast<const std::uint32_t*>(dirEntry);
            const auto dirSize = *reinterpret_cast<const std::uint32_t*>(dirEntry + 4);
            if (!dirRva || dirSize < sizeof(RuntimeFunction)) return d;

            d.base = a_base;
            d.entries = reinterpret_cast<const RuntimeFunction*>(image + dirRva);
            d.count = dirSize / sizeof(RuntimeFunction);
            return d;
        }

        const Directory& Dir() noexcept
        {
            static Directory d = [] {
                const auto& module = REX::FModule::GetExecutingModule();
                auto read = ReadDirectory(module.GetBaseAddress());
                logger::info("PrismaCrafting: exception directory -- {} RUNTIME_FUNCTION entries",
                             read.count);
                return read;
            }();
            return d;
        }

        [[nodiscard]] std::uint32_t PrimaryRvaOf(const Directory& a_dir,
                                                 const RuntimeFunction& a_entry) noexcept
        {
            const auto* image = reinterpret_cast<const std::uint8_t*>(a_dir.base);
            const RuntimeFunction* cur = &a_entry;

            for (int depth = 0; depth < kMaxChainDepth; ++depth) {
                if (!cur->unwindRva) return 0;
                const auto* info = image + cur->unwindRva;
                const auto version = static_cast<std::uint8_t>(info[0] & 0x07);
                const auto flags = static_cast<std::uint8_t>(info[0] >> 3);
                if (version == 0 || version > kMaxKnownVersion) return 0;
                if (!(flags & kChainInfo)) return cur->beginRva;

                const std::uint8_t codes = info[2];
                const std::size_t codeBytes = std::size_t{ (codes + 1u) & ~1u } * 2u;
                cur = reinterpret_cast<const RuntimeFunction*>(info + 4 + codeBytes);
            }
            return 0;  // did not terminate
        }
    }

    std::vector<Range> FunctionSegmentsAt(std::uintptr_t a_address) noexcept
    {
        const auto& dir = Dir();
        if (!dir.entries || !dir.count || !a_address || a_address < dir.base) return {};

        const auto relOffset = static_cast<std::uint32_t>(a_address - dir.base);

        const auto* first = dir.entries;
        const auto* last = dir.entries + dir.count;
        const auto* it = std::upper_bound(first, last, relOffset,
                                          [](std::uint32_t v, const RuntimeFunction& e) {
                                              return v < e.beginRva;
                                          });
        if (it == first) return {};
        --it;
        if (relOffset < it->beginRva || relOffset >= it->endRva) return {};

        const auto primary = PrimaryRvaOf(dir, *it);
        if (!primary) return {};

        std::vector<Range> out;
        for (const auto* e = first; e != last; ++e) {
            if (!e->beginRva || e->endRva <= e->beginRva) continue;
            if (PrimaryRvaOf(dir, *e) != primary) continue;
            out.push_back({ dir.base + e->beginRva, dir.base + e->endRva });
        }

        std::sort(out.begin(), out.end(),
                  [](const Range& a, const Range& b) { return a.begin < b.begin; });
        return out;
    }
}

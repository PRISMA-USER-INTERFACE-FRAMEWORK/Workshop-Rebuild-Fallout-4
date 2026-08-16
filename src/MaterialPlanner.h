#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace MaterialPlanner
{
    struct Bundle
    {
        std::vector<std::uint64_t> yields;
        std::uint32_t available = 0;
    };

    struct Plan
    {
        bool satisfied = false;
        std::vector<std::uint32_t> selected;
        std::vector<std::uint64_t> remaining;
    };

    inline Plan Build(std::vector<std::uint64_t> needs, std::vector<Bundle> bundles)
    {
        Plan result;
        result.selected.assign(bundles.size(), 0);
        result.remaining = std::move(needs);

        const auto complete = [&]() {
            return std::all_of(result.remaining.begin(), result.remaining.end(),
                               [](std::uint64_t n) { return n == 0; });
        };

        while (!complete()) {
            std::size_t best = bundles.size();
            std::uint64_t bestCoverage = 0;
            std::uint64_t bestWaste = std::numeric_limits<std::uint64_t>::max();
            std::size_t bestDimensions = 0;

            for (std::size_t i = 0; i < bundles.size(); ++i) {
                const auto& bundle = bundles[i];
                if (bundle.available == 0 || bundle.yields.size() != result.remaining.size()) continue;

                std::uint64_t coverage = 0;
                std::uint64_t waste = 0;
                std::size_t dimensions = 0;
                for (std::size_t j = 0; j < result.remaining.size(); ++j) {
                    if (result.remaining[j] == 0 || bundle.yields[j] == 0) continue;
                    ++dimensions;
                    coverage += std::min(result.remaining[j], bundle.yields[j]);
                    if (bundle.yields[j] > result.remaining[j])
                        waste += bundle.yields[j] - result.remaining[j];
                }
                if (coverage == 0) continue;

                if (dimensions > bestDimensions ||
                    (dimensions == bestDimensions && coverage > bestCoverage) ||
                    (dimensions == bestDimensions && coverage == bestCoverage && waste < bestWaste)) {
                    best = i;
                    bestCoverage = coverage;
                    bestWaste = waste;
                    bestDimensions = dimensions;
                }
            }

            if (best == bundles.size()) return result;

            auto& bundle = bundles[best];
            std::uint32_t take = bundle.available;
            for (std::size_t j = 0; j < result.remaining.size(); ++j) {
                const auto yield = bundle.yields[j];
                if (result.remaining[j] == 0 || yield == 0) continue;
                const auto toFinish = result.remaining[j] / yield +
                                      (result.remaining[j] % yield != 0 ? 1 : 0);
                take = std::min<std::uint32_t>(take, static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(toFinish, std::numeric_limits<std::uint32_t>::max())));
            }
            if (take == 0) take = 1;

            result.selected[best] += take;
            bundle.available -= take;
            for (std::size_t j = 0; j < result.remaining.size(); ++j) {
                const auto produced = bundle.yields[j] > 0 &&
                                      static_cast<std::uint64_t>(take) >
                                          std::numeric_limits<std::uint64_t>::max() / bundle.yields[j]
                    ? std::numeric_limits<std::uint64_t>::max()
                    : bundle.yields[j] * static_cast<std::uint64_t>(take);
                result.remaining[j] -= std::min(result.remaining[j], produced);
            }
        }

        result.satisfied = true;
        return result;
    }
}

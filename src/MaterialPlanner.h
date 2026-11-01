#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace MaterialPlanner
{
    struct Bundle
    {
        std::vector<std::uint64_t> yields;
        std::uint64_t available = 0;
    };

    struct Plan
    {
        bool satisfied = false;
        std::vector<std::uint64_t> selected;
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
        using Sum = std::pair<std::uint64_t, std::uint64_t>;
        const auto add = [](Sum& sum, std::uint64_t value) {
            if (sum.second > std::numeric_limits<std::uint64_t>::max() - value) ++sum.first;
            sum.second += value;
        };

        while (!complete()) {
            std::size_t best = bundles.size();
            Sum bestCoverage{};
            Sum bestWaste{ std::numeric_limits<std::uint64_t>::max(),
                           std::numeric_limits<std::uint64_t>::max() };
            std::size_t bestDimensions = 0;

            for (std::size_t i = 0; i < bundles.size(); ++i) {
                const auto& bundle = bundles[i];
                if (bundle.available == 0 || bundle.yields.size() != result.remaining.size()) continue;

                Sum coverage{};
                Sum waste{};
                std::size_t dimensions = 0;
                for (std::size_t j = 0; j < result.remaining.size(); ++j) {
                    if (result.remaining[j] == 0 || bundle.yields[j] == 0) continue;
                    ++dimensions;
                    add(coverage, std::min(result.remaining[j], bundle.yields[j]));
                    if (bundle.yields[j] > result.remaining[j])
                        add(waste, bundle.yields[j] - result.remaining[j]);
                }
                if (coverage == Sum{}) continue;

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
            std::uint64_t take = bundle.available;
            for (std::size_t j = 0; j < result.remaining.size(); ++j) {
                const auto yield = bundle.yields[j];
                if (result.remaining[j] == 0 || yield == 0) continue;
                const auto toFinish = result.remaining[j] / yield +
                                      (result.remaining[j] % yield != 0 ? 1 : 0);
                take = std::min(take, toFinish);
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

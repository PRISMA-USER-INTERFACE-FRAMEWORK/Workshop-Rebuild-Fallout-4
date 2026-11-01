#include "WorkbenchDefinitions.h"
#include "CraftingConfig.h"
#include "DebugLog.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace WorkbenchUpgrades
{
    namespace
    {
        std::string ReadText(const nlohmann::json& value)
        {
            auto text = value.get<std::string>();
            if (text.empty() || text.find('\0') != std::string::npos)
                throw std::invalid_argument("text must be nonempty and contain no NUL");
            return text;
        }

        std::uint32_t ReadUnsigned(const nlohmann::json& value, std::uint32_t limit)
        {
            if (!value.is_number_integer() ||
                (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
                throw std::invalid_argument("costs and durations must be nonnegative integers");
            const auto number = value.get<std::uint64_t>();
            if (number > limit) throw std::invalid_argument("cost or duration exceeds its supported range");
            return static_cast<std::uint32_t>(number);
        }
    }

    bool LoadBenchDefinitions(const std::filesystem::path& path,
                              std::vector<BenchDef>& benches,
                              std::unordered_map<std::string, std::string>& categoryBench)
    {
        try {
            std::ifstream input(path);
            if (!input) {
                std::error_code ec;
                if (std::filesystem::exists(path, ec) || ec)
                    logger::warn("Workbench definitions: cannot read PrismaCraftingWorkbenches.json; using defaults");
                return false;
            }
            const auto root = nlohmann::json::parse(input);
            if (!root.is_array() || root.size() != benches.size())
                throw std::invalid_argument("definitions must contain all five benches");

            auto candidate = benches;
            std::set<std::string> expectedCategories, seenIds;
            for (const auto& bench : benches)
                expectedCategories.insert(bench.categories.begin(), bench.categories.end());
            std::unordered_map<std::string, std::string> categories;
            for (const auto& value : root) {
                BenchDef bench;
                bench.id = ReadText(value.at("id"));
                const auto old = std::find_if(benches.begin(), benches.end(),
                    [&](const auto& b) { return b.id == bench.id; });
                if (old == benches.end() || !seenIds.insert(bench.id).second)
                    throw std::invalid_argument("unknown or duplicate bench id");
                bench.name = ReadText(value.at("name"));
                bench.desc = ReadText(value.at("desc"));
                bench.nif = ReadText(value.at("nif"));
                const auto& categoryValues = value.at("categories");
                if (!categoryValues.is_array()) throw std::invalid_argument("categories must be an array");
                for (const auto& category : categoryValues) {
                    auto text = ReadText(category);
                    if (!expectedCategories.contains(text) || !categories.emplace(text, bench.id).second)
                        throw std::invalid_argument("unknown or duplicate category");
                    bench.categories.push_back(std::move(text));
                }
                const auto& levels = value.at("levels");
                if (!levels.is_array() || levels.size() != old->levels.size())
                    throw std::invalid_argument("each bench must retain its three ordered levels");
                for (const auto& levelValue : levels) {
                    LevelDef level;
                    level.level = static_cast<int>(ReadUnsigned(levelValue.at("level"), 3));
                    if (level.level != static_cast<int>(bench.levels.size()) + 1)
                        throw std::invalid_argument("levels must be numbered 1, 2, 3");
                    level.timeSec = ReadUnsigned(levelValue.at("timeSec"), std::numeric_limits<std::uint32_t>::max());
                    level.caps = ReadUnsigned(levelValue.at("caps"), std::numeric_limits<std::int32_t>::max());
                    level.bonus = ReadText(levelValue.at("bonus"));
                    const auto& costs = levelValue.at("cost");
                    if (!costs.is_array()) throw std::invalid_argument("cost must be an array");
                    std::set<RE::TESForm*> materials;
                    for (const auto& cost : costs) {
                        CostItem item;
                        item.name = ReadText(cost.at("name"));
                        item.ref = ReadText(cost.at("ref"));
                        item.count = ReadUnsigned(cost.at("count"), std::numeric_limits<std::uint32_t>::max());
                        if (item.count == 0) continue;
                        item.form = CraftingConfig::ResolveForm(item.ref);
                        if (!item.form) throw std::invalid_argument("required material could not be resolved: " + item.ref);
                        // Constraint: source:src/CraftingConfig.cpp:ResolveForm; apply its existing cache-admission policy to retained costs.
                        if ((item.form->GetFormID() & 0xFF000000u) == 0xFF000000u)
                            throw std::invalid_argument("required material is excluded by the resolver retention policy: " + item.ref);
                        if (!materials.insert(item.form).second)
                            throw std::invalid_argument("duplicate required material: " + item.ref);
                        level.cost.push_back(std::move(item));
                    }
                    bench.levels.push_back(std::move(level));
                }
                candidate[static_cast<std::size_t>(old - benches.begin())] = std::move(bench);
            }
            if (categories.size() != expectedCategories.size())
                throw std::invalid_argument("every existing category must be assigned exactly once");
            logger::info("Workbench definitions: loaded PrismaCraftingWorkbenches.json");
            benches.swap(candidate);
            categoryBench.swap(categories);
            return true;
        } catch (const std::exception& error) {
            logger::warn("Workbench definitions: rejected PrismaCraftingWorkbenches.json ({}); using defaults", error.what());
            return false;
        }
    }
}

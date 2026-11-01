#include "CobjCacheInternal.h"
#include "WorkbenchUpgrades.h"
#include <charconv>
#include <cstdio>

namespace CobjCache
{
    std::string ToolGroupLabel(const ToolGroup& g)
    {
        std::string s;
        for (const auto& o : g.options) { if (!s.empty()) s += " / "; s += o.name; }
        return s;
    }

    Json RecipeJson(const Recipe& r,
                          int wbLocked, int schLocked,
                          const std::vector<uint32_t>* haveCounts,
                          const std::vector<bool>* perkOwned,
                          const std::vector<bool>* toolOk,
                          const std::vector<uint32_t>* storedCounts)
    {
        const auto hex = [](uint32_t v) {
            char b[12];
            std::snprintf(b, sizeof(b), "%08X", v);
            return std::string(b);
        };
        Json j = Json::object();
        j.get_ref<Json::object_t&>().reserve(30);
        j.emplace("formRef", hex(r.formRef));
        j.emplace("editorId", r.editorId);
        j.emplace("recipeKey", r.recipeKey);
        j.emplace("created", hex(r.createdFormId));
        j.emplace("createdKey", r.createdKey);
        j.emplace("name", r.createdName);
        j.emplace("count", r.createdCount);
        j.emplace("value", r.createdValue);
        j.emplace("benchId", hex(r.benchKeywordId));
        j.emplace("bench", r.benchKeyword);
        j.emplace("formType", r.formType);
        j.emplace("nifPath", r.nifPath);
        j.emplace("matSwap", r.matSwap);
        j.emplace("nifPaths", r.compositeNifPaths);
        j.emplace("altNifPaths", r.altNifPaths);
        j.emplace("resources", Json::array());
        j.emplace("source", r.source);
        j.emplace("wbBench", WorkbenchUpgrades::BenchNameForCategory(r.category));
        j.emplace("wbLvl", WorkbenchUpgrades::RequiredLevel(r.category, r.compScore));
        j.emplace("components", Json::array());
        j.emplace("workshop", r.isWorkshop);
        j.emplace("menuPath", r.menuPath);
        j.emplace("filters", r.filters);
        j.emplace("tools", r.tools);
        j.emplace("perks", Json::array());
        j.emplace("toolGroups", Json::array());
        j.emplace("desc", r.desc);

        for (const auto& resource : r.resources) {
            char buffer[24];
            float value = resource.value;
            if (!(value > -1.0e6f && value < 1.0e6f)) value = 0.0f;
            const auto number = std::to_chars(buffer, buffer + sizeof(buffer), value,
                std::chars_format::fixed, value == static_cast<long long>(value) ? 0 : 1);
            j["resources"].push_back({{"label", resource.label},
                {"value", number.ec == std::errc{} ? Json::parse(buffer, number.ptr) : Json(0)}});
        }
        for (size_t i = 0; i < r.components.size(); ++i) {
            const auto& c = r.components[i];
            Json component{{"formRef", hex(c.formRef)}, {"name", c.name}, {"count", c.count}};
            if (haveCounts && i < haveCounts->size()) {
                component["have"] = (*haveCounts)[i];
                if (storedCounts && i < storedCounts->size()) component["stored"] = (*storedCounts)[i];
            }
            j["components"].push_back(std::move(component));
        }
        for (size_t i = 0; i < r.reqPerks.size(); ++i) {
            Json perk{{"name", r.reqPerks[i].name}};
            if (!r.reqPerks[i].op.empty()) {
                perk["op"] = r.reqPerks[i].op;
                perk["value"] = r.reqPerks[i].value;
            }
            if (perkOwned && i < perkOwned->size()) perk["owned"] = (*perkOwned)[i];
            j["perks"].push_back(std::move(perk));
        }
        for (size_t i = 0; i < r.toolGroups.size(); ++i) {
            Json group{{"label", ToolGroupLabel(r.toolGroups[i])}};
            if (toolOk && i < toolOk->size()) group["ok"] = (*toolOk)[i];
            j["toolGroups"].push_back(std::move(group));
        }
        if (wbLocked >= 0 || schLocked >= 0) {
            const bool wb = wbLocked == 1, sch = schLocked == 1;
            j["locked"] = wb || sch;
            j["wbLocked"] = wb;
            j["schLocked"] = sch;
        }
        return j;
    }
}

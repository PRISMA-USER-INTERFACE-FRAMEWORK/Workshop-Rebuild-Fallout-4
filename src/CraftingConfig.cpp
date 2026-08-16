#include "CraftingConfig.h"
#include "ConfigValidation.h"
#include "Paths.h"

#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace CraftingConfig
{
    using json = nlohmann::json;

    static bool                                            s_loaded = false;
    static std::unordered_map<std::string, RecipeOverride> s_overrides;
    static std::unordered_map<std::string, RE::TESForm*>   s_byEditorId;

    static std::string ToLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static RecipeOverride ParseOverride(const json& v, const std::string& key)
    {
        RecipeOverride ov;
        if (v.contains("components")) {
            ov.hasComponents = true;
            if (!v["components"].is_array()) {
                ov.componentsValid = false;
                logger::warn("CraftingConfig: '{}' components must be an array -- keeping vanilla costs", key);
            }
            for (const auto& c : v["components"].is_array() ? v["components"] : json::array()) {
                CompSpec cs;
                if (!c.is_object() || !c.contains("id") || !c["id"].is_string() ||
                    !c.contains("count") || !c["count"].is_number_integer()) {
                    ov.componentsValid = false;
                    continue;
                }
                cs.id = c["id"].get<std::string>();
                std::uint64_t count = 0;
                if (c["count"].is_number_unsigned()) {
                    count = c["count"].get<std::uint64_t>();
                } else {
                    const auto signedCount = c["count"].get<std::int64_t>();
                    if (signedCount > 0) count = static_cast<std::uint64_t>(signedCount);
                }
                if (cs.id.empty() || count == 0 ||
                    count > std::numeric_limits<std::uint32_t>::max()) {
                    ov.componentsValid = false;
                    continue;
                }
                cs.count = static_cast<std::uint32_t>(count);
                ov.components.push_back(std::move(cs));
            }
            if (!ov.componentsValid)
                logger::warn("CraftingConfig: '{}' has invalid component entries -- keeping vanilla costs", key);
        }
        if (v.contains("conditions")) {
            ov.hasConditions = true;
            if (!v["conditions"].is_array()) {
                ov.conditionsValid = false;
                logger::warn("CraftingConfig: '{}' conditions must be an array -- keeping vanilla gates", key);
            }
            for (const auto& c : v["conditions"].is_array() ? v["conditions"] : json::array()) {
                CondSpec cd;
                if (!c.is_object() || !c.contains("type") || !c["type"].is_string() ||
                    !c.contains("id") || !c["id"].is_string() ||
                    !c.contains("op") || !c["op"].is_string() ||
                    !c.contains("value") || !c["value"].is_number()) {
                    ov.conditionsValid = false;
                    continue;
                }
                cd.type = ToLower(c["type"].get<std::string>());
                cd.id = c["id"].get<std::string>();
                cd.op = c["op"].get<std::string>();
                const auto value = c["value"].get<double>();
                if (cd.id.empty() || !std::isfinite(value) ||
                    value < -std::numeric_limits<float>::max() ||
                    value > std::numeric_limits<float>::max() ||
                    !Validation::IsConditionType(cd.type) || !Validation::IsConditionOp(cd.op)) {
                    ov.conditionsValid = false;
                    continue;
                }
                cd.value = static_cast<float>(value);
                ov.conditions.push_back(std::move(cd));
            }
            if (!ov.conditionsValid)
                logger::warn("CraftingConfig: '{}' has invalid conditions -- keeping vanilla gates", key);
        }
        return ov;
    }

    void Load()
    {
        if (s_loaded) return;
        s_loaded = true;

        auto path = Paths::F4SEUserDir() / "PrismaCraftingRecipes.json";
        std::ifstream f(path);
        if (!f) {
            logger::info("CraftingConfig: no override file at {} (using vanilla recipes)", path.string());
            return;
        }

        json root;
        try {
            f >> root;
        } catch (const std::exception& e) {
            logger::error("CraftingConfig: parse error in {} -- {} (ignoring all overrides)", path.string(), e.what());
            return;
        }
        if (!root.is_object()) {
            logger::error("CraftingConfig: top level of {} must be an object keyed by recipe id", path.string());
            return;
        }

        for (auto it = root.begin(); it != root.end(); ++it) {
            if (!it.value().is_object()) continue;
            auto ov = ParseOverride(it.value(), it.key());
            if (ov.hasComponents || ov.hasConditions) {
                logger::debug("CraftingConfig: override '{}' (components={} [{}], conditions={} [{}])",
                    it.key(), ov.hasComponents, ov.components.size(), ov.hasConditions, ov.conditions.size());
                s_overrides.emplace(it.key(), std::move(ov));
            }
        }
        logger::info("CraftingConfig: loaded {} recipe override(s)", s_overrides.size());
    }

    bool Empty() { return s_overrides.empty(); }

    const RecipeOverride* Find(const std::string& createdKey, const std::string& editorId, uint32_t formId)
    {
        if (s_overrides.empty()) return nullptr;
        if (!createdKey.empty()) {
            if (auto it = s_overrides.find(createdKey); it != s_overrides.end()) {
                logger::debug("CraftingConfig: matched override by createdKey '{}'", createdKey);
                return &it->second;
            }
        }
        if (!editorId.empty()) {
            if (auto it = s_overrides.find(editorId); it != s_overrides.end()) {
                logger::debug("CraftingConfig: matched override by editorId '{}'", editorId);
                return &it->second;
            }
        }
        char hex[16];
        std::snprintf(hex, sizeof(hex), "%08X", formId);
        if (auto it = s_overrides.find(hex); it != s_overrides.end()) {
            logger::debug("CraftingConfig: matched override by formId '{}'", hex);
            return &it->second;
        }
        return nullptr;
    }

    void RegisterEditorId(const std::string& editorId, RE::TESForm* form)
    {
        if (!editorId.empty() && form) s_byEditorId.emplace(editorId, form);
    }

    RE::TESForm* ResolveForm(const std::string& id)
    {
        if (id.empty()) return nullptr;

        if (auto bar = id.find('|'); bar != std::string::npos) {
            std::string plugin = id.substr(0, bar);
            std::string hex    = id.substr(bar + 1);
            if (plugin.empty() || hex.empty()) return nullptr;
            uint32_t local = 0;
            if (!Validation::ParseHex(hex, local)) return nullptr;
            if (auto* dh = RE::TESDataHandler::GetSingleton())
                return dh->LookupForm(local, plugin);
            return nullptr;
        }

        if (auto it = s_byEditorId.find(id); it != s_byEditorId.end()) return it->second;

        uint32_t fid = 0;
        if (Validation::ParseHex(id, fid)) {
            return RE::TESForm::GetFormByID(fid);
        }
        logger::debug("CraftingConfig: ResolveForm('{}') -> no match", id);
        return nullptr;
    }
}

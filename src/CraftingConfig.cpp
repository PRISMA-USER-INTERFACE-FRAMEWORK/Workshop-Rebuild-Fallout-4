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
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace CraftingConfig
{
    using json = nlohmann::json;

    static bool                                            s_loaded = false;
    static std::vector<CategoryRule> s_categoryRules;
    static std::unordered_map<std::string, RecipeOverride> s_overrides;
    static std::unordered_map<std::string, RE::TESForm*>   s_byEditorId;
    static std::unordered_map<std::string, RE::TESForm*>   s_resolvedForms;

    static std::mutex                                      s_formCacheLock;

    static std::string ToLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static RecipeOverride ParseOverride(const json& v, const std::string& key)
    {
        RecipeOverride ov;
        if (auto it = v.find("components"); it != v.end()) {
            ov.hasComponents = true;
            if (!it->is_array()) {
                ov.componentsValid = false;
                logger::warn("CraftingConfig: '{}' components must be an array -- keeping vanilla costs", key);
            } else {
                for (const auto& c : *it) {
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
            }
            if (!ov.componentsValid)
                logger::warn("CraftingConfig: '{}' has invalid component entries -- keeping vanilla costs", key);
        }
        if (auto it = v.find("conditions"); it != v.end()) {
            ov.hasConditions = true;
            if (!it->is_array()) {
                ov.conditionsValid = false;
                logger::warn("CraftingConfig: '{}' conditions must be an array -- keeping vanilla gates", key);
            } else {
                for (const auto& c : *it) {
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
            }
            if (!ov.conditionsValid)
                logger::warn("CraftingConfig: '{}' has invalid conditions -- keeping vanilla gates", key);
        }
        return ov;
    }

    static void LoadCategoryRules(const json& value)
    {
        std::vector<CategoryRule> candidate;
        std::unordered_set<std::string> seen;
        bool valid = value.is_array();
        if (valid) for (const auto& entry : value) {
            if (!entry.is_object() || entry.size() != 2 || !entry.contains("category") ||
                !entry["category"].is_string() || entry.contains("editorId") == entry.contains("keyword")) {
                valid = false;
                break;
            }
            CategoryRule rule;
            rule.keyword = entry.contains("keyword");
            const auto& id = entry[rule.keyword ? "keyword" : "editorId"];
            if (!id.is_string()) { valid = false; break; }
            rule.id = id.get<std::string>();
            rule.category = entry["category"].get<std::string>();
            if (rule.id.find_first_not_of(" \t\r\n") == std::string::npos ||
                rule.id.find('\0') != std::string::npos || !Validation::IsRecipeCategory(rule.category) ||
                !seen.insert((rule.keyword ? "keyword:" : "editorId:") + rule.id).second) {
                valid = false;
                break;
            }
            candidate.push_back(std::move(rule));
        }
        if (valid) s_categoryRules = std::move(candidate);
        else logger::warn("CraftingConfig: invalid _classification -- keeping default classification");
    }

    std::span<const CategoryRule> CategoryRules() { return s_categoryRules; }

    void Load()
    {
        if (s_loaded) return;
        s_loaded = true;

        auto path = Paths::F4SEUserDir() / "PrismaCraftingRecipes.json";
        std::ifstream f(path);
        if (!f) {
            logger::info("CraftingConfig: no PrismaCraftingRecipes.json (using vanilla recipes)");
            return;
        }

        json root;
        try {
            f >> root;
        } catch (const std::exception& e) {
            logger::error("CraftingConfig: parse error in PrismaCraftingRecipes.json -- {} (ignoring all overrides)", e.what());
            return;
        }
        if (!root.is_object()) {
            logger::error("CraftingConfig: top level of PrismaCraftingRecipes.json must be an object keyed by recipe id");
            return;
        }

        if (auto it = root.find("_classification"); it != root.end()) LoadCategoryRules(*it);
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (it.key() == "_classification") continue;
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

    const RecipeOverride* Find(const std::string& createdKey, const std::string& editorId, uint32_t formRef)
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
        std::snprintf(hex, sizeof(hex), "%08X", formRef);
        if (auto it = s_overrides.find(hex); it != s_overrides.end()) {
            logger::debug("CraftingConfig: matched override by formRef '{}'", hex);
            return &it->second;
        }
        return nullptr;
    }

    void RegisterEditorId(const std::string& editorId, RE::TESForm* form)
    {
        if (editorId.empty() || !form) return;
        if ((form->GetFormID() & 0xFF000000u) == 0xFF000000u) return;
        std::lock_guard<std::mutex> lock(s_formCacheLock);
        s_byEditorId.emplace(editorId, form);
        s_resolvedForms.emplace(editorId, form);
    }

    RE::TESForm* ResolveForm(const std::string& id)
    {
        if (id.empty()) return nullptr;

        std::lock_guard<std::mutex> lock(s_formCacheLock);

        if (auto it = s_resolvedForms.find(id); it != s_resolvedForms.end())
            return it->second;

        auto remember = [&](RE::TESForm* form) -> RE::TESForm* {
            if (form && (form->GetFormID() & 0xFF000000u) != 0xFF000000u)
                s_resolvedForms.emplace(id, form);
            return form;
        };

        if (auto bar = id.find('|'); bar != std::string::npos) {
            std::string plugin = id.substr(0, bar);
            std::string hex    = id.substr(bar + 1);
            if (plugin.empty() || hex.empty()) return nullptr;
            uint32_t local = 0;
            if (!Validation::ParseHex(hex, local)) return nullptr;
            if (auto* dh = RE::TESDataHandler::GetSingleton())
                return remember(dh->LookupForm(local, plugin));
            return nullptr;
        }

        if (auto it = s_byEditorId.find(id); it != s_byEditorId.end())
            return remember(it->second);

        uint32_t fid = 0;
        if (Validation::ParseHex(id, fid))
            return remember(RE::TESForm::GetFormByID(fid));

        logger::debug("CraftingConfig: ResolveForm('{}') -> no match", id);
        return nullptr;
    }
}

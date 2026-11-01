#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace RE { class TESForm; }

namespace CraftingConfig
{

    struct CategoryRule
    {
        bool keyword = false;
        std::string id;
        std::string category;
    };

    std::span<const CategoryRule> CategoryRules();

    struct CompSpec
    {
        std::string id;
        uint32_t    count = 1;
    };

    struct CondSpec
    {
        std::string type;
        std::string id;
        std::string op   = ">=";
        float       value = 1.0f;
    };

    struct RecipeOverride
    {
        bool                  hasComponents = false;
        bool                  componentsValid = true;
        std::vector<CompSpec> components;
        bool                  hasConditions = false;
        bool                  conditionsValid = true;
        std::vector<CondSpec> conditions;
    };

    void Load();

    bool Empty();

    const RecipeOverride* Find(const std::string& createdKey, const std::string& editorId, uint32_t formRef);

    RE::TESForm* ResolveForm(const std::string& id);

    void RegisterEditorId(const std::string& editorId, RE::TESForm* form);
}

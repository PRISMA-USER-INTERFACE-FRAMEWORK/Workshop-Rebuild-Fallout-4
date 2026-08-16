#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CraftingConfig.h"

namespace RE { class BGSKeyword; class TESObjectREFR; class TESForm; class PlayerCharacter; }

namespace CobjCache
{
    struct Component
    {
        uint32_t      formId = 0;
        std::string   name;
        uint32_t      count = 0;
        RE::TESForm*  form  = nullptr;
    };

    struct PerkReq
    {
        uint32_t    formId = 0;
        std::string name;
    };

    struct ToolOption
    {
        uint32_t    formId = 0;
        std::string name;
    };
    struct ToolGroup
    {
        std::vector<ToolOption> options;
    };

    struct Recipe
    {
        uint32_t                 formId = 0;
        std::string              editorId;
        std::string              recipeKey;
        uint32_t                 createdFormId = 0;
        std::string              createdKey;
        std::string              createdName;
        std::string              desc;
        int32_t                  createdValue = 0;
        int32_t                  compScore = 0;
        uint16_t                 createdCount = 1;
        uint32_t                 benchKeywordId = 0;
        std::string              benchKeyword;

        std::string              recipeCategory;
        std::vector<Component>   components;
        std::vector<PerkReq>     reqPerks;
        std::string              formType;
        std::string              category;
        std::string              nameKey;
        std::string              sortKey;

        std::string              nifPath;
        std::vector<std::string> compositeNifPaths;

        std::vector<std::string> altNifPaths;

        std::string              matSwap;

        std::string              source;
        std::vector<std::string> filters;
        std::string              menuPath;
        std::string              subCategory;

        std::vector<std::string> tools;
        std::vector<ToolGroup>   toolGroups;
        uint16_t                 workshopPriority = 0;
        bool                     isWorkshop = false;
        bool                     isMod = false;

        bool                     descLoaded = false;

        bool                     configGated = false;
        std::vector<CraftingConfig::CondSpec> configConds;
    };

    void Build();

    bool IsBuilt();

    uint32_t PlayerCaps();

    uint32_t CountHeld(RE::TESForm* item, const std::vector<RE::TESObjectREFR*>& sources = {});

    uint32_t CountStored(RE::TESForm* item, const std::vector<RE::TESObjectREFR*>& sources = {});

    bool ConsumeComponents(const std::vector<std::pair<RE::TESForm*, std::uint32_t>>& costs,
                           RE::PlayerCharacter* player,
                           const std::vector<RE::TESObjectREFR*>& sources = {},
                           bool dryRun = false,
                           bool includePlayer = true);

    const Recipe* FindWorkshopRecipeByFormId(std::uint32_t cobjFormId);

    const Recipe* FindWorkshopRecipeByCreated(std::uint32_t createdFormId);

    bool WorkshopRecipeAvailable(const Recipe& r,
                                 const std::vector<RE::TESObjectREFR*>& sources = {});

    bool WorkshopCanAfford(const Recipe& r, const std::vector<RE::TESObjectREFR*>& sources = {});

    bool WorkshopConsume(const Recipe& r, const std::vector<RE::TESObjectREFR*>& sources = {});

    std::vector<Recipe> All();

    std::string GetRecipeDesc(uint32_t cobjFormId);

    const std::string& CachePath();

    void ExportCurrentView(const std::vector<uint32_t>& benchIds);

    bool IsBenchKeyword(uint32_t keywordFormId);

    int CraftRecipe(uint32_t cobjFormId, bool bypassGates = false,
                    const std::vector<RE::TESObjectREFR*>& sources = {}, int times = 1);

    std::string WorkshopBuildablesJson(const std::vector<RE::TESObjectREFR*>& sources = {});

    void ApplyWorkshopCategories(const std::unordered_map<std::uint32_t, std::string>& categories);

    uint32_t FirstWorkshopBuildableCreatedId();

    std::string QueryRecipesPage(const std::vector<uint32_t>& wantIds, const std::string& menuKind,
                                 const std::string& search, const std::string& category,
                                 const std::string& sub,
                                 int lockFilter, int sort, int page, int pageSize,
                                 const std::vector<RE::TESObjectREFR*>& sources = {},

                                 bool cookingBench = false);

    std::string FilterJsonForBench(const std::vector<uint32_t>& wantIds,
                                   const std::vector<RE::TESObjectREFR*>& sources = {});
}

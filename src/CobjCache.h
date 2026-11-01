#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CraftingConfig.h"

namespace RE { class BGSKeyword; class TESObjectREFR; class TESForm; class PlayerCharacter; }

namespace CobjCache
{
    inline constexpr uint32_t kWorkbenchCookingKeyword = 0x00102152;
    struct Component
    {
        uint32_t      formRef = 0;
        std::string   name;
        uint32_t      count = 0;
        RE::TESForm*  form  = nullptr;
    };

    struct PerkReq
    {
        uint32_t    formRef = 0;
        std::string name;
        std::string op{};
        float       value = 1.0f;
    };

    struct ToolOption
    {
        uint32_t    formRef = 0;
        std::string name;
    };
    struct ToolGroup
    {
        std::vector<ToolOption> options;
    };

    struct ResourceValue
    {
        std::string label;      // display name, e.g. "Power"
        float       value = 0;  // signed: generators give, lights and turrets take
    };

    struct Recipe
    {
        uint32_t                 formRef = 0;
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

        std::vector<ResourceValue> resources;

        std::vector<std::string> tools;
        std::vector<ToolGroup>   toolGroups;
        uint16_t                 workshopPriority = 0;
        bool                     isWorkshop = false;
        bool                     workshopList = false;
        std::vector<RE::TESForm*> workshopVariants;
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

    std::optional<Recipe> FindWorkshopRecipeByFormId(std::uint32_t cobjFormId);
    bool WorkshopSelectionMatches(const Recipe& recipe, std::uint32_t selectedFormId);

    std::optional<Recipe> FindWorkshopRecipeByCreated(std::uint32_t createdFormId);

    bool WorkshopRecipeAvailable(const Recipe& r,
                                 const std::vector<RE::TESObjectREFR*>& sources = {});

    bool WorkshopCanAfford(const Recipe& r, const std::vector<RE::TESObjectREFR*>& sources = {});

    std::vector<std::pair<RE::TESForm*, std::uint32_t>> CostsOf(const Recipe& r);

    bool WorkshopConsume(const Recipe& r, const std::vector<RE::TESObjectREFR*>& sources = {});

    std::vector<Recipe> All();

    std::string GetRecipeDesc(uint32_t cobjFormId);

    void ExportCurrentView(const std::vector<uint32_t>& benchIds);

    bool IsBenchKeyword(uint32_t keywordFormId);
    void ResetCraftBenchScopeWarning();

    int CraftRecipe(uint32_t cobjFormId, bool bypassGates,
                    const std::vector<RE::TESObjectREFR*>& sources, int times,
                    const std::vector<uint32_t>& wantIds, bool cookingBench);

    struct WorkshopPageRequest
    {
        std::string search;
        std::string category = "All";
        std::string sub = "All";
        std::string filter = "ALL";
        int page = 0;
        int pageSize = 64;
        std::vector<std::string> favorites;
        bool favoritesOnly = false;
    };

    std::string QueryWorkshopPage(const WorkshopPageRequest& request,
                                  const std::vector<RE::TESObjectREFR*>& sources = {});

    void ApplyWorkshopCategories(const std::unordered_map<std::uint32_t, std::string>& categories);

    uint32_t FirstWorkshopBuildableCreatedId();

    struct LockFlags { bool wb = false; bool sch = false; bool any() const { return wb || sch; } };
    struct RecipeEval { std::optional<LockFlags> locks; std::optional<bool> affordable; };
    struct RecipePageQuery
    {
        std::vector<RecipeEval> eval;
        std::unordered_map<std::string, size_t> seen;
        std::vector<size_t> cands;
        size_t cursor = 0;
    };

}

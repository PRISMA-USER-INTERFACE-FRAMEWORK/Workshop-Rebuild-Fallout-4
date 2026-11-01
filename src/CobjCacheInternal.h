#pragma once

#include "CobjCache.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <memory>
#include <initializer_list>
#include <span>
#include <unordered_set>

namespace RE { class BGSMaterialSwap; class BGSConstructibleObject; }

namespace CobjCache
{
    bool IsToolForm(RE::TESForm* a_form);

    using Json = nlohmann::ordered_json;
    bool HasAny(const std::string& hay, std::initializer_list<const char*> needles);
    bool HasWord(const std::string& hay, std::initializer_list<const char*> needles);
    std::string ConsumableCategory(RE::TESForm* created);
    std::string Categorize(const std::string& formType, const std::string& nameLower,
                          const std::string& subCategory, const std::string& consumableCategory,
                          const std::string& mappedCategory);
    std::string MappedCategory(const Recipe& recipe, RE::TESForm* created);
    struct RecipeScan
    {
        std::vector<Recipe> recipes;
        std::vector<RE::BGSConstructibleObject*> forms;
        std::unordered_map<uint32_t, size_t> byFormId;
        std::unordered_set<uint32_t> benchKeywordIds;
        std::unordered_map<uint32_t, size_t> workshopByCreated;
        std::unordered_set<uint32_t> ambiguousWorkshopCreated;
        std::size_t resourceEntries = 0;
        std::size_t resourceMatches = 0;
        bool playerMissing = false;
    };
    RecipeScan ScanRecipes(std::span<RE::BGSConstructibleObject* const> objects);
    // Constraint: callers hold s_cacheLock; the published index is retained for the session.
    const RecipeScan& CurrentIndex();
    Recipe RecipeValue(std::size_t index);
    const std::string& WorkshopMenuPath(const Recipe& recipe);
    std::string SourceFile(RE::TESForm* form);
    extern std::size_t s_resSheetEntries;
    extern std::size_t s_resMatched;
    std::string ToolGroupLabel(const ToolGroup& g);
    std::string GetName(RE::TESForm* form);
    std::string CreatedKey(RE::TESForm* form);
    void CollectMaterialSwap(RE::BGSMaterialSwap* mswp, std::string& out);
    void CollectResourceValues(RE::TESForm* a_created, std::vector<ResourceValue>& a_out,
                               std::size_t& sheetEntries, std::size_t& matched);
    bool RecipeConditionsPass(const Recipe& r, RE::PlayerCharacter* player,
                                     const std::vector<RE::TESObjectREFR*>& sources);
    bool MatchesCraftBench(const Recipe& r, const std::vector<uint32_t>& wantIds, bool cookingBench);
    bool CmpOp(double a, const std::string& op, double b);
    Json ConfigRequirementsJson(const Recipe& r, RE::PlayerCharacter* player,
                                const std::vector<RE::TESObjectREFR*>& sources);
    void ComputeReqOwnership(const Recipe& r, RE::PlayerCharacter* player,
                                    std::vector<bool>& perkOwned, std::vector<bool>& toolOk);
    Json RecipeJson(const Recipe& r,
                                  int wbLocked = -1, int schLocked = -1,
                                  const std::vector<uint32_t>* haveCounts = nullptr,
                                  const std::vector<bool>* perkOwned = nullptr,
                                  const std::vector<bool>* toolOk = nullptr,
                                  const std::vector<uint32_t>* storedCounts = nullptr);
    Json QueryRecipesPage(RecipePageQuery& query, const std::vector<uint32_t>& wantIds,
                         const std::string& search, const std::string& category,
                         const std::string& sub, int lockFilter, int sort, int page, int pageSize,
                         const std::vector<RE::TESObjectREFR*>& sources = {}, bool cookingBench = false);
    extern std::unordered_map<std::uint32_t, std::string> s_workshopMenuPaths;
    extern std::recursive_mutex                        s_cacheLock;
}

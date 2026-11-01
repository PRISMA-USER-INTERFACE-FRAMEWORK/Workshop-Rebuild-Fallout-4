#include "CobjCacheInternal.h"
#include <chrono>

#include "WorkshopIDs.h"

#include "CraftingConfig.h"
#include "InventorySnapshot.h"
#include "Paths.h"
#include "SimpleIni.h"
#include "WorkbenchUpgrades.h"

#include <RE/B/BGSConstructibleObject.h>
#include <RE/B/BGSComponent.h>
#include <RE/B/BGSInventoryList.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/T/TESBoundObject.h>
#include <RE/B/BGSPropertySheet.h>
#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueInfo.h>
#include <RE/T/TESObjectACTI.h>
#include <RE/T/TESObjectSTAT.h>
#include <RE/T/TESObjectCONT.h>
#include <RE/T/TESObjectLIGH.h>
#include <RE/T/TESGlobal.h>
#include <RE/B/BGSTypedKeywordValue.h>
#include <RE/K/KeywordType.h>
#include <RE/B/BGSPerk.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESDescription.h>
#include <RE/B/BSStringT.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESValueForm.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESConditionItem.h>
#include <RE/C/CONDITION_ITEM_DATA.h>
#include <RE/S/SCRIPT_OUTPUT.h>
#include <RE/I/ITEM_REMOVE_REASON.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESModel.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESObjectARMA.h>
#include <RE/T/TESFile.h>
#include <RE/RTTI.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CobjCache
{
    static std::atomic<bool> s_benchScopeWarning{ false };


    static std::unique_ptr<const RecipeScan> s_recipeIndex;
    static std::unordered_map<std::size_t, std::string> s_descriptions;
    std::unordered_map<std::uint32_t, std::string> s_workshopMenuPaths;
    std::recursive_mutex                        s_cacheLock;

    const RecipeScan& CurrentIndex()
    {
        static const RecipeScan empty;
        return s_recipeIndex ? *s_recipeIndex : empty;
    }

    // Constraint: source:src/CobjCache.h:Recipe keys remain unchanged when composing metadata.
    const std::string& WorkshopMenuPath(const Recipe& recipe)
    {
        const auto it = s_workshopMenuPaths.find(recipe.formRef);
        return it == s_workshopMenuPaths.end() ? recipe.menuPath : it->second;
    }

    Recipe RecipeValue(std::size_t i)
    {
        Recipe row = CurrentIndex().recipes[i];
        if (const auto it = s_descriptions.find(i); it != s_descriptions.end()) {
            row.desc = it->second;
            row.descLoaded = true;
        }
        if (const auto it = s_workshopMenuPaths.find(row.formRef); it != s_workshopMenuPaths.end()) {
            row.menuPath = it->second;
            const auto sep = row.menuPath.find(" > ");
            row.filters.assign(1, row.menuPath.substr(0, sep));
        }
        return row;
    }

    static void EnsureDesc(size_t i)
    {
        const auto& index = CurrentIndex();
        if (i >= index.recipes.size() || i >= index.forms.size()) return;
        const auto& r = index.recipes[i];
        auto [it, inserted] = s_descriptions.try_emplace(i, r.desc);
        if (!inserted || r.descLoaded) return;
        auto* cobj = index.forms[i];
        if (!cobj) return;
        auto* created = cobj->GetCreatedItem();
        if (!created) return;
        if (auto* descForm = RE::fallout_cast<RE::TESDescription*>(created)) {
            RE::BSString s;
            descForm->GetDescription(s, created);
            if (const char* d = s.c_str(); d && d[0]) it->second = d;
        }
    }

    static std::uint64_t CountAcross(RE::TESForm* item, RE::PlayerCharacter* player,
                                     const std::vector<RE::TESObjectREFR*>& sources);
    static std::uint32_t AvailableAcross(RE::TESForm* form, RE::PlayerCharacter* player,
                                         const std::vector<RE::TESObjectREFR*>& sources);

    bool CmpOp(double a, const std::string& op, double b)
    {
        if (op == "==" || op == "=") return a == b;
        if (op == "!=")              return a != b;
        if (op == ">")               return a >  b;
        if (op == "<")               return a <  b;
        if (op == "<=")              return a <= b;
        if (op == ">=")              return a >= b;
        logger::warn("CraftingConfig: unsupported condition operator '{}' -- condition fails closed", op);
        return false;
    }

    static std::uint64_t CountItemsWithKeyword(RE::BGSKeyword* kw, RE::PlayerCharacter* player,
                                                const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!kw) return 0;
        std::uint64_t total = 0;
        auto scan = [&](RE::TESObjectREFR* ref) {
            if (!ref || !ref->inventoryList) return;
            for (auto& item : ref->inventoryList->data) {
                auto* obj = item.object;
                if (!obj) continue;
                if (auto* kwf = obj->As<RE::BGSKeywordForm>(); kwf && kwf->HasKeyword(kw))
                    total = crafting::SaturateAddU64(total, item.GetCount());
            }
        };
        scan(player);
        for (auto* ref : sources) scan(ref);
        return total;
    }

    static bool ConfigConditionPasses(const Recipe& r, const CraftingConfig::CondSpec& cd,
                                     RE::PlayerCharacter* player,
                                     const std::vector<RE::TESObjectREFR*>& sources,
                                     std::string* displayName = nullptr)
    {
        if (displayName) *displayName = cd.id;
        if (!player) return false;
        RE::TESForm* form = CraftingConfig::ResolveForm(cd.id);
        if (!form) {
            logger::debug("ConfigGate[{}]: condition {} '{}' UNRESOLVED -> locked",
                r.editorId.empty() ? r.createdKey : r.editorId, cd.type, cd.id);
            return false;
        }
        if (displayName) {
            auto name = GetName(form);
            if (!name.empty()) *displayName = std::move(name);
        }
        bool ok = false;
        if (cd.type == "perk") {
            if (form->Is(RE::ENUM_FORM_ID::kPERK))
                ok = CmpOp(player->GetPerkRank(static_cast<RE::BGSPerk*>(form)), cd.op, cd.value);
        } else if (cd.type == "global") {
            if (auto* g = RE::fallout_cast<RE::TESGlobal*>(form)) ok = CmpOp(g->value, cd.op, cd.value);
        } else if (cd.type == "itemcount") {
            ok = CmpOp(CountAcross(form, player, sources), cd.op, cd.value);
        } else if (cd.type == "keyword") {
            if (form->Is(RE::ENUM_FORM_ID::kKYWD))
                ok = CmpOp(CountItemsWithKeyword(static_cast<RE::BGSKeyword*>(form), player, sources), cd.op, cd.value);
        } else {
            logger::warn("ConfigGate[{}]: unsupported condition type '{}' -- condition fails closed",
                r.editorId.empty() ? r.createdKey : r.editorId, cd.type);
            ok = false;
        }
        logger::debug("ConfigGate[{}]: {} '{}' {} {} -> {}",
            r.editorId.empty() ? r.createdKey : r.editorId, cd.type, cd.id, cd.op, cd.value, ok ? "pass" : "FAIL");
        return ok;
    }

    static bool ConfigGatePasses(const Recipe& r, RE::PlayerCharacter* player,
                                 const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!player) return false;
        for (const auto& cd : r.configConds)
            if (!ConfigConditionPasses(r, cd, player, sources)) return false;
        return true;
    }

    Json ConfigRequirementsJson(const Recipe& r, RE::PlayerCharacter* player,
                                const std::vector<RE::TESObjectREFR*>& sources)
    {
        Json requirements = Json::array();
        for (const auto& cd : r.configConds) {
            std::string name;
            const bool met = ConfigConditionPasses(r, cd, player, sources, &name);
            requirements.push_back({{"type", cd.type}, {"id", cd.id}, {"name", name},
                                    {"op", cd.op}, {"value", cd.value}, {"met", met}});
        }
        return requirements;
    }

    bool RecipeConditionsPass(const Recipe& r, RE::PlayerCharacter* player,
                                     const std::vector<RE::TESObjectREFR*>& sources)
    {
        const auto& index = CurrentIndex();
        if (!player) return false;
        if (r.configGated) return ConfigGatePasses(r, player, sources);
        const auto it = index.byFormId.find(r.formRef);
        if (it == index.byFormId.end() || it->second >= index.forms.size() || !index.forms[it->second]) return false;
        return index.forms[it->second]->PlayerPassesConditions();
    }

    static LockFlags ComputeLocksExact(const Recipe& r, RE::PlayerCharacter* player,
                                       const std::vector<RE::TESObjectREFR*>& sources)
    {
        LockFlags lf;
        lf.wb = !WorkbenchUpgrades::IsRecipeCraftable(r.category, r.compScore);
        lf.sch = !RecipeConditionsPass(r, player, sources);
        return lf;
    }


    void Build()
    {
        static std::recursive_mutex buildLock;
        static bool building = false;
        std::lock_guard<std::recursive_mutex> buildGuard(buildLock);
        if (building) return;
        {
            std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
            if (s_recipeIndex) { logger::debug("CobjCache::Build: already built this session, skipping"); return; }
        }
        building = true;
        struct ClearBuilding {
            bool& flag;
            ~ClearBuilding() { flag = false; }
        } clearBuilding{ building };

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("CobjCache: TESDataHandler null");
            return;
        }
        auto& arr = dh->GetFormArray<RE::BGSConstructibleObject>();
        if (arr.empty()) {
            logger::warn("CobjCache::Build: COBJ array empty (data not ready) — will retry on bench open");
            return;
        }
        logger::debug("CobjCache::Build: enumerating constructible objects...");
        auto candidate = std::make_unique<const RecipeScan>(ScanRecipes({ arr.data(), arr.size() }));
        if (candidate->playerMissing) {
            logger::warn("CobjCache::Build: no player during requirement scan -- not publishing; will retry on the next bench open");
            return;
        }
        const size_t recipeCount = candidate->recipes.size();
        const size_t modCount = std::count_if(candidate->recipes.begin(), candidate->recipes.end(),
            [](const Recipe& r) { return r.isMod; });
        {
            std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
            s_resSheetEntries = candidate->resourceEntries;
            s_resMatched = candidate->resourceMatches;
            s_recipeIndex = std::move(candidate);
        }
        logger::info("CobjCache: indexed {} constructible objects ({} hidden mods)", recipeCount, modCount);
    }

    bool IsBuilt()
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        return bool(s_recipeIndex);
    }

    uint32_t CountHeld(RE::TESForm* item, const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!item) return 0;

        return AvailableAcross(item, RE::PlayerCharacter::GetSingleton(), sources);
    }

    uint32_t CountStored(RE::TESForm* item, const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!item) return 0;
        return AvailableAcross(item, nullptr, sources);
    }

    static std::uint64_t CountAcross(RE::TESForm* item, RE::PlayerCharacter* player,
                                     const std::vector<RE::TESObjectREFR*>& sources)
    {
        std::uint64_t total = 0;
        std::uint32_t c = 0;
        if (player) { c = 0; player->GetItemCount(c, item, false); total = crafting::SaturateAddU64(total, c); }
        for (auto* ref : sources) {
            if (!ref) continue;
            c = 0; ref->GetItemCount(c, item, false); total = crafting::SaturateAddU64(total, c);
        }
        return total;
    }

    static std::uint32_t AvailableAcross(RE::TESForm* form, RE::PlayerCharacter* player,
                                         const std::vector<RE::TESObjectREFR*>& sources)
    {
        return InventorySnapshot::Build(player, sources).Available(form);
    }

    std::optional<Recipe> FindWorkshopRecipeByFormId(std::uint32_t cobjFormId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        if (!s_recipeIndex || !cobjFormId) return std::nullopt;
        const auto it = index.byFormId.find(cobjFormId);
        if (it == index.byFormId.end() || it->second >= index.recipes.size()) return std::nullopt;
        const auto& recipe = index.recipes[it->second];
        if (!recipe.isWorkshop) return std::nullopt;
        return RecipeValue(it->second);
    }

    bool WorkshopSelectionMatches(const Recipe& recipe, std::uint32_t selectedFormId)
    {
        if (!selectedFormId || !recipe.isWorkshop) return false;
        if (!recipe.workshopList) return recipe.createdFormId == selectedFormId;
        return std::any_of(recipe.workshopVariants.begin(), recipe.workshopVariants.end(),
            [selectedFormId](const auto* member) { return member && member->GetFormID() == selectedFormId; });
    }

    std::optional<Recipe> FindWorkshopRecipeByCreated(std::uint32_t createdFormId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        if (!s_recipeIndex || !createdFormId) return std::nullopt;
        if (index.ambiguousWorkshopCreated.count(createdFormId)) return std::nullopt;
        const auto it = index.workshopByCreated.find(createdFormId);
        if (it == index.workshopByCreated.end() || it->second >= index.recipes.size()) return std::nullopt;
        return RecipeValue(it->second);
    }

    bool WorkshopRecipeAvailable(const Recipe& recipe,
                                 const std::vector<RE::TESObjectREFR*>& sources)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        const auto it = index.byFormId.find(recipe.formRef);
        if (it == index.byFormId.end() || it->second >= index.forms.size()) return false;
        auto* cobj = index.forms[it->second];
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!cobj || !player || !recipe.isWorkshop) return false;
        return RecipeConditionsPass(recipe, player, sources);
    }

    bool WorkshopCanAfford(const Recipe& r, const std::vector<RE::TESObjectREFR*>& sources)
    {
        auto costs = CostsOf(r);
        if (costs.empty()) return true;
        return ConsumeComponents(costs, RE::PlayerCharacter::GetSingleton(), sources, true);
    }

    bool WorkshopConsume(const Recipe& r, const std::vector<RE::TESObjectREFR*>& sources)
    {
        auto costs = CostsOf(r);
        if (costs.empty()) return true;
        return ConsumeComponents(costs, RE::PlayerCharacter::GetSingleton(), sources, false);
    }

    bool IsBenchKeyword(uint32_t keywordFormId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        if (!s_recipeIndex || keywordFormId == 0) return false;
        return index.benchKeywordIds.count(keywordFormId) != 0;
    }

    bool MatchesCraftBench(const Recipe& r, const std::vector<uint32_t>& wantIds, bool cookingBench)
    {
        // Constraint: an unresolved bench scope lists nothing; it must not open every non-workshop recipe.
        if (wantIds.empty()) {
            if (!s_benchScopeWarning.exchange(true, std::memory_order_acq_rel))
                logger::warn("CobjCache: craft bench scope is empty; listing no recipes until the bench keywords resolve");
            return false;
        }
        return !r.isWorkshop && !r.isMod &&
               std::find(wantIds.begin(), wantIds.end(), r.benchKeywordId) != wantIds.end() &&
               cookingBench == (r.benchKeywordId == kWorkbenchCookingKeyword);
    }

    void ResetCraftBenchScopeWarning()
    {
        s_benchScopeWarning.store(false, std::memory_order_release);
    }

    std::vector<Recipe> All()
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        std::vector<Recipe> recipes;
        recipes.reserve(index.recipes.size());
        for (std::size_t i = 0; i < index.recipes.size(); ++i) recipes.push_back(RecipeValue(i));
        return recipes;
    }

    std::string GetRecipeDesc(uint32_t cobjFormId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        auto it = index.byFormId.find(cobjFormId);
        if (it == index.byFormId.end()) return "";
        EnsureDesc(it->second);
        const auto description = s_descriptions.find(it->second);
        return description == s_descriptions.end() ? index.recipes[it->second].desc : description->second;
    }


    static std::string SubOf(const Recipe& r)
    {
        if (r.isWorkshop) {
            if (!r.menuPath.empty()) {
                const auto sep = r.menuPath.rfind(" > ");
                std::string leaf = (sep == std::string::npos) ? r.menuPath : r.menuPath.substr(sep + 3);
                if (!leaf.empty() && leaf != r.category) return leaf;
            }
            return {};
        }
        if (!r.subCategory.empty() && r.subCategory != r.category) return r.subCategory;
        return {};
    }

    Json QueryRecipesPage(RecipePageQuery& query, const std::vector<uint32_t>& wantIds,
                                 const std::string& search, const std::string& category,
                                 const std::string& sub,
                                 int lockFilter, int sort, int page, int pageSize,
                                 const std::vector<RE::TESObjectREFR*>& sources, bool cookingBench)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        if (!s_recipeIndex || pageSize <= 0)
            return {{"total", 0}, {"pageCount", 0}, {"page", 0},
                    {"categories", Json::array()}, {"recipes", Json::array()}};
        WorkbenchUpgrades::Poll();
        auto* player = RE::PlayerCharacter::GetSingleton();

        if (query.eval.empty()) query.eval.resize(index.recipes.size());
        auto& eval = query.eval;
        size_t work = 0;
        const auto initialCursor = query.cursor;
        const auto started = std::chrono::steady_clock::now();
        auto exactLocks = [&](size_t idx) -> LockFlags {
            auto& e = eval[idx];
            if (!e.locks) { ++work; e.locks = ComputeLocksExact(index.recipes[idx], player, sources); }
            return *e.locks;
        };
        auto affordable = [&](size_t idx) -> bool {
            auto& e = eval[idx];
            if (!e.affordable) { ++work; e.affordable = ConsumeComponents(CostsOf(index.recipes[idx]), player, sources, true); }
            return *e.affordable;
        };

        auto& seen = query.seen;
        auto& cands = query.cands;
        while (query.cursor < index.recipes.size()) {
            if (query.cursor > initialCursor && (work >= 32 ||
                std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(2))) return {};
            const size_t i = query.cursor++;
            const Recipe& r = index.recipes[i];
            if (!MatchesCraftBench(r, wantIds, cookingBench)) continue;
            if (!search.empty() && r.nameKey.find(search) == std::string::npos) continue;

            const bool locked = lockFilter != 0 && exactLocks(i).any();
            if (lockFilter == 1 && locked) continue;
            if (lockFilter == 2 && !locked) continue;
            if (lockFilter == 3) {
                if (locked) continue;

                if (!affordable(i)) continue;
            }

            const std::string& key = !r.createdKey.empty() ? r.createdKey : r.editorId;
            if (key.empty()) { cands.push_back(i); continue; }
            auto it = seen.find(key);
            if (it == seen.end()) { seen.emplace(key, cands.size()); cands.push_back(i); continue; }

            auto& incumbent = cands[it->second];
            const bool incLocked  = exactLocks(incumbent).any();
            const bool chalLocked = exactLocks(i).any();
            if (incLocked != chalLocked) {
                if (incLocked) incumbent = i;
            } else if (!incLocked && player) {

                if (!affordable(incumbent) && affordable(i))
                    incumbent = i;
            }
        }

        auto catOf = [&](const Recipe& r) -> const std::string& {
            if (cookingBench && !r.recipeCategory.empty()) return r.recipeCategory;
            return r.category;
        };
        std::map<std::string, int> catCounts;
        std::map<std::string, std::map<std::string, int>> subCounts;
        for (const auto& c : cands) {
            const Recipe& r = index.recipes[c];
            catCounts[catOf(r)]++;
            if (auto s = SubOf(r); !s.empty()) subCounts[catOf(r)][s]++;
        }

        std::vector<size_t> finalIdx;
        finalIdx.reserve(cands.size());
        const bool allCats = category.empty() || category == "ALL";
        const bool allSubs = sub.empty() || sub == "ALL";
        for (const auto& c : cands) {
            const Recipe& r = index.recipes[c];
            if (!allCats && catOf(r) != category) continue;
            if (!allSubs && SubOf(r) != sub) continue;
            finalIdx.push_back(c);
        }

        std::sort(finalIdx.begin(), finalIdx.end(), [&](size_t a, size_t b) {
            const Recipe& ra = index.recipes[a]; const Recipe& rb = index.recipes[b];
            if (sort == 1 && ra.createdValue != rb.createdValue) return ra.createdValue > rb.createdValue;
            if (ra.sortKey != rb.sortKey) return ra.sortKey < rb.sortKey;
            return ra.nameKey < rb.nameKey;
        });

        const int total = static_cast<int>(finalIdx.size());
        const int pageCount = (total + pageSize - 1) / pageSize;
        if (page < 0) page = 0;
        if (pageCount == 0) page = 0;
        else if (page >= pageCount) page = pageCount - 1;
        const size_t start = static_cast<size_t>(page) * pageSize;
        const size_t end = (std::min)(finalIdx.size(), start + static_cast<size_t>(pageSize));

        const InventorySnapshot inv = InventorySnapshot::Build(player, sources);
        auto countCached = [&](RE::TESForm* item) -> uint32_t { return inv.Available(item); };

        Json payload = {{"total", total}, {"pageCount", pageCount}, {"page", page},
                        {"categories", Json::array()}, {"recipes", Json::array()}};
        for (const auto& [cat, cnt] : catCounts) {
            Json category = {{"cat", cat}, {"count", cnt}, {"subs", Json::array()}};
            if (auto sit = subCounts.find(cat); sit != subCounts.end()) {
                for (const auto& [sname, scnt] : sit->second) {
                    category["subs"].push_back({{"sub", sname}, {"count", scnt}});
                }
            }
            payload["categories"].push_back(std::move(category));
        }
        for (size_t k = start; k < end; ++k) {
            const Recipe r = RecipeValue(finalIdx[k]);
            std::vector<uint32_t> haveCounts;
            std::vector<bool>     perkOwned, toolOk;
            if (player) {
                haveCounts.reserve(r.components.size());
                for (const auto& c : r.components) haveCounts.push_back(c.form ? countCached(c.form) : 0);
            }
            ComputeReqOwnership(r, player, perkOwned, toolOk);
            const auto lf = exactLocks(finalIdx[k]);
            Json item = RecipeJson(r, lf.wb ? 1 : 0, lf.sch ? 1 : 0,
                             haveCounts.empty() ? nullptr : &haveCounts,
                             perkOwned.empty() ? nullptr : &perkOwned,
                             toolOk.empty()    ? nullptr : &toolOk);
            if (r.configGated) item["requirements"] = ConfigRequirementsJson(r, player, sources);
            payload["recipes"].push_back(std::move(item));
        }

        logger::info("QueryRecipesPage: bench={} cat='{}' search='{}' lock={} sort={} -> total={} page {}/{}",
            wantIds.size(), category, search, lockFilter, sort, total, page, pageCount);
        return payload;
    }
}

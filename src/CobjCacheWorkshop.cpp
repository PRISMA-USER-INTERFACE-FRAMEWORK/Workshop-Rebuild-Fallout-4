#include "CobjCacheInternal.h"
#include "InventorySnapshot.h"
#include "WorkshopIDs.h"
#include <RE/W/Workshop.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESModel.h>
#include <RE/RTTI.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

namespace CobjCache
{
    // Constraint: category matching preserves source:src/CobjCache.cpp at 3f158d1.
    uint32_t FirstWorkshopBuildableCreatedId()
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);

        for (const auto& r : CurrentIndex().recipes) {
            if (r.isWorkshop && r.createdFormId && !r.nifPath.empty())
                return r.createdFormId;
        }
        return 0;
    }

    // Constraint: source:src/CobjCache.h:Recipe keys identify path overrides without changing the published index.
    void ApplyWorkshopCategories(const std::unordered_map<std::uint32_t, std::string>& categories)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (categories.empty()) return;
        const auto& recipes = CurrentIndex().recipes;

        const auto topOf = [](const std::string& p) {
            const auto sep = p.find(" > ");
            return sep == std::string::npos ? p : p.substr(0, sep);
        };
        std::unordered_map<std::string, std::string> topRename;
        for (const auto& r : recipes) {
            const auto& menuPath = WorkshopMenuPath(r);
            if (!r.isWorkshop || menuPath.empty()) continue;
            auto it = categories.find(r.formRef);
            if (it == categories.end() || it->second.empty()) continue;
            const std::string derived = topOf(menuPath);
            const std::string actual  = topOf(it->second);
            if (derived.empty() || actual.empty() || derived == actual) continue;
            topRename.emplace(derived, actual);
        }

        std::size_t applied = 0;
        std::size_t renamed = 0;
        for (const auto& r : recipes) {
            if (!r.isWorkshop) continue;
            const auto& menuPath = WorkshopMenuPath(r);
            auto it = categories.find(r.formRef);
            if (it == categories.end() || it->second.empty()) {
                if (menuPath.empty()) continue;
                const std::string derived = topOf(menuPath);
                auto ren = topRename.find(derived);
                if (ren == topRename.end()) continue;
                s_workshopMenuPaths[r.formRef] = ren->second + menuPath.substr(derived.size());
                ++renamed;
                continue;
            }
            s_workshopMenuPaths[r.formRef] = it->second;
            ++applied;
        }
        logger::info("CobjCache: applied vanilla workshop categories to {} of {} buildable(s); "
                     "{} tree-absent buildable(s) re-titled to match their siblings",
                     applied, categories.size(), renamed);
    }

    static Recipe WorkshopVariantRow(const Recipe& recipe, RE::TESForm* member)
    {
        Recipe row = recipe;
        row.createdFormId = member->GetFormID();
        row.createdKey = CreatedKey(member);
        row.recipeKey += ":" + row.createdKey;
        row.createdName = GetName(member);
        row.nifPath.clear();
        row.compositeNifPaths.clear();
        row.altNifPaths.clear();
        row.matSwap.clear();
        row.resources.clear();
        if (auto* model = RE::fallout_cast<RE::TESModel*>(member))
            if (const char* nif = model->GetModel(); nif && nif[0]) row.nifPath = nif;
        if (auto* swap = RE::fallout_cast<RE::BGSModelMaterialSwap*>(member))
            CollectMaterialSwap(swap->swapForm, row.matSwap);
        std::size_t sheetEntries = 0, matched = 0;
        CollectResourceValues(member, row.resources, sheetEntries, matched);
        return row;
    }

    std::string QueryWorkshopPage(const WorkshopPageRequest& request,
                                  const std::vector<RE::TESObjectREFR*>& sources)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        auto* player = RE::PlayerCharacter::GetSingleton();

        std::unordered_set<std::uint32_t> activeRecipes;
        if (crafting::ids::WorkshopDataResolvable()) {
            REL::Relocation<RE::Workshop::WorkshopMenuNode*> root{ crafting::ids::WorkshopMenuRoot };
            auto collect = [&](auto&& self, const RE::Workshop::WorkshopMenuNode& node) -> void {
                if (node.recipe) {
                    activeRecipes.insert(node.recipe->GetFormID());
                }
                for (const auto& child : node.children)
                    if (child) self(self, *child);
            };
            if (root.get()) collect(collect, *root.get());
        }

        const InventorySnapshot inv = InventorySnapshot::Build(player, sources);
        auto countStored = [&](RE::TESForm* item) -> uint32_t { return inv.Stored(item); };
        auto countCached = [&](RE::TESForm* item) -> uint32_t { return inv.Available(item); };
        const std::unordered_set<std::string> favorites(request.favorites.begin(), request.favorites.end());
        const auto lower = [](std::string value) {
            for (char& c : value) {
                if (c == '_') c = ' ';
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) return std::string{};
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        };
        const std::string search = lower(request.search);
        struct Candidate { size_t index; RE::TESForm* member; bool locked; bool legacyFirst; };
        std::vector<Candidate> matching;
        std::map<std::string, std::map<std::string, size_t>> facets;
        std::map<std::string, std::map<std::string, size_t>> favoriteFacets;
        size_t facetTotal = 0;

        for (size_t i = 0; i < index.recipes.size(); ++i) {
            const Recipe& r = index.recipes[i];
            const auto& menuPath = WorkshopMenuPath(r);
            if (!r.isWorkshop) continue;

            const bool treeAbsent =
                !activeRecipes.empty() && !activeRecipes.contains(r.formRef);
            if (treeAbsent && menuPath.empty()) continue;

            const bool locked = treeAbsent ||
                !RecipeConditionsPass(r, player, sources);
            bool buildable = !locked;
            if (buildable) {
                for (const auto& c : r.components) {
                    if ((player && c.form ? countCached(c.form) : 0) < c.count) {
                        buildable = false;
                        break;
                    }
                }
            }
            const auto sep = menuPath.find(" > ");
            const std::string cat = !menuPath.empty()
                ? menuPath.substr(0, sep)
                : (!r.filters.empty() && !r.filters[0].empty() ? r.filters[0] : "Misc");
            const std::string sub = sep == std::string::npos ? "General" : menuPath.substr(sep + 3);
            const auto consider = [&](RE::TESForm* member, bool firstVariant) {
                ++facetTotal;
                ++facets[cat][sub];
                bool favorite = false;
                if (!favorites.empty()) {
                    const std::string key = member
                        ? r.recipeKey + ":" + CreatedKey(member) : r.recipeKey;
                    // Constraint: matches src/CobjCacheSerialization.cpp:RecipeJson ID width.
                    char legacyId[9];
                    std::snprintf(legacyId, sizeof(legacyId), "%08X", r.formRef);
                    favorite = favorites.contains(key) ||
                        (firstVariant && favorites.contains(legacyId));
                }
                if (favorite) ++favoriteFacets[cat][sub];
                if (request.favoritesOnly && !favorite) return;
                if (request.filter == "BUILDABLE" && !buildable) return;
                if (request.filter == "LOCKED" && buildable) return;
                if (!search.empty()) {
                    std::string name = member ? GetName(member) : r.createdName;
                    if (name.empty()) name = r.editorId;
                    if (lower(std::move(name)).find(search) == std::string::npos) return;
                }
                if (search.empty() &&
                    ((request.category != "All" && request.category != cat) ||
                     (request.sub != "All" && request.sub != sub))) return;
                matching.push_back({ i, member, locked, firstVariant });
            };
            if (r.workshopList) {
                bool firstVariant = true;
                for (auto* member : r.workshopVariants) {
                    consider(member, firstVariant);
                    firstVariant = false;
                }
            } else {
                consider(nullptr, true);
            }
        }

        const size_t pageSize = static_cast<size_t>((std::clamp)(request.pageSize, 1, 64));
        const size_t pageCount = (matching.size() + pageSize - 1) / pageSize;
        const size_t page = pageCount ? (std::min)(static_cast<size_t>((std::max)(request.page, 0)),
                                                    pageCount - 1) : 0;
        const size_t start = page * pageSize;
        const size_t end = (std::min)(matching.size(), start + pageSize);
        Json payload = {{"total", matching.size()}, {"page", page}, {"pageSize", pageSize},
                        {"pageCount", pageCount}, {"facetTotal", facetTotal},
                        {"categories", Json::array()}, {"favoriteCategories", Json::array()},
                        {"recipes", Json::array()}};
        const auto appendFacets = [&](Json& target, const auto& groups) {
            for (const auto& [cat, subs] : groups) {
                size_t count = 0;
                for (const auto& [sub, n] : subs) count += n;
                Json category = {{"cat", cat}, {"count", count}, {"subs", Json::array()}};
                for (const auto& [sub, n] : subs) {
                    category["subs"].push_back({{"sub", sub}, {"count", n}});
                }
                target.push_back(std::move(category));
            }
        };
        appendFacets(payload["categories"], facets);
        appendFacets(payload["favoriteCategories"], favoriteFacets);
        for (size_t k = start; k < end; ++k) {
            const auto& candidate = matching[k];
            const Recipe recipe = RecipeValue(candidate.index);
            const Recipe row = candidate.member ? WorkshopVariantRow(recipe, candidate.member) : recipe;
            std::vector<uint32_t> haveCounts;
            std::vector<uint32_t> storedCounts;
            std::vector<bool> perkOwned;
            std::vector<bool> toolOk;
            if (candidate.index < index.forms.size() && index.forms[candidate.index]) {
                if (player) {
                    haveCounts.reserve(row.components.size());
                    storedCounts.reserve(row.components.size());
                    for (const auto& c : row.components) {
                        haveCounts.push_back(c.form ? countCached(c.form) : 0);
                        storedCounts.push_back(c.form ? countStored(c.form) : 0);
                    }
                }
                ComputeReqOwnership(row, player, perkOwned, toolOk);
            }
            Json item = RecipeJson(row, candidate.locked ? 1 : 0, candidate.locked ? 1 : 0,
                             haveCounts.empty() ? nullptr : &haveCounts,
                             perkOwned.empty() ? nullptr : &perkOwned,
                             toolOk.empty() ? nullptr : &toolOk,
                             storedCounts.empty() ? nullptr : &storedCounts);
            if (row.configGated) item["requirements"] = ConfigRequirementsJson(row, player, sources);
            if (!candidate.legacyFirst) item["legacyFirst"] = false;
            payload["recipes"].push_back(std::move(item));
        }
        logger::info("CobjCache: workshop page {}/{}: {} of {} matching buildables",
                     page, pageCount, end - start, matching.size());

        if (s_resSheetEntries && !s_resMatched)
            logger::warn("CobjCache: matched 0 of {} property entries against the resource actor "
                         "values -- no contribution badges will appear; the actor values are not "
                         "resolving by name", s_resSheetEntries);
        else
            logger::info("CobjCache: matched {} of {} property entries as settlement resources",
                         s_resMatched, s_resSheetEntries);
        return payload.dump(-1, ' ', false, Json::error_handler_t::replace);
    }


}

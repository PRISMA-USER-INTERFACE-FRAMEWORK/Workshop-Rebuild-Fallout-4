#include "CobjCacheInternal.h"
#include "WorkbenchUpgrades.h"

#include <RE/B/BGSConstructibleObject.h>
#include <RE/I/ITEM_REMOVE_REASON.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>

#include <limits>

namespace CobjCache
{
    int CraftRecipe(uint32_t cobjFormId, bool bypassGates,
                    const std::vector<RE::TESObjectREFR*>& sources, int times,
                    const std::vector<uint32_t>& wantIds, bool cookingBench)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto& index = CurrentIndex();
        if (!IsBuilt()) return 0;
        if (times < 1) times = 1;

        auto idxIt = index.byFormId.find(cobjFormId);
        if (idxIt == index.byFormId.end()) {
            logger::warn("CraftRecipe: COBJ {:08X} not found", cobjFormId);
            return 0;
        }
        const size_t idx = idxIt->second;
        RE::BGSConstructibleObject* cobj = index.forms[idx];
        const Recipe& rec = index.recipes[idx];
        if (!MatchesCraftBench(rec, wantIds, cookingBench)) return 0;
        if (!cobj) {
            logger::warn("CraftRecipe: COBJ {:08X} null form", cobjFormId);
            return 0;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;

        auto* created = cobj->GetCreatedItem();
        auto* boundObj = created ? created->As<RE::TESBoundObject>() : nullptr;
        if (!boundObj) {
            logger::warn("CraftRecipe: COBJ {:08X} has no supported bound-object output", cobjFormId);
            return 0;
        }

        WorkbenchUpgrades::Poll();
        if (!bypassGates && !WorkbenchUpgrades::IsRecipeCraftable(rec.category, rec.compScore)) {
            logger::info("CraftRecipe: bench-locked (category '{}' tier {}) for {:08X} — craft refused",
                rec.category, WorkbenchUpgrades::RequiredLevel(rec.category, rec.compScore), cobjFormId);
            return 0;
        }

        logger::debug("CraftRecipe[{:08X}] '{}': times={} cheat={} configGated={} components={} sources={}",
            cobjFormId, rec.createdName, times, bypassGates, rec.configGated,
            rec.components.size(), sources.size());

        const bool passes = bypassGates || RecipeConditionsPass(rec, player, sources);
        if (!passes) {
            logger::info("CraftRecipe: conditions failed for {:08X}", cobjFormId);
            return 0;
        }
        if (bypassGates)
            logger::info("CraftRecipe: cheat mode — bypassing conditions for {:08X}", cobjFormId);

        const int32_t per = cobj->data.numConstructed ? cobj->data.numConstructed : 1;
        auto outputCountValid = [&](int count) {
            const auto total = static_cast<std::int64_t>(per) * count;
            return total > 0 && total <= std::numeric_limits<std::int32_t>::max();
        };

        int batches = times;
        if (bypassGates) {
            logger::info("CraftRecipe: cheat mode — free craft (no materials required/consumed) x{} for {:08X}",
                batches, cobjFormId);
        } else {
            auto costsForBatches = [&](int count,
                                       std::vector<std::pair<RE::TESForm*, std::uint32_t>>& out) {
                out.clear();
                out.reserve(rec.components.size());
                for (const auto& component : rec.components) {
                    if (component.count == 0) continue;
                    const auto total = static_cast<std::uint64_t>(component.count) *
                                       static_cast<std::uint64_t>(count);
                    if (total > std::numeric_limits<std::uint32_t>::max()) return false;
                    out.emplace_back(component.form, static_cast<std::uint32_t>(total));
                }
                return true;
            };

            std::vector<std::pair<RE::TESForm*, std::uint32_t>> costs;
            int low = 0;
            int high = batches;
            while (low < high) {
                const int probe = low + (high - low + 1) / 2;
                if (costsForBatches(probe, costs) &&
                    ConsumeComponents(costs, player, sources, true)) {
                    low = probe;
                } else {
                    high = probe - 1;
                }
            }
            batches = low;
            if (batches < 1 || !costsForBatches(batches, costs)) {
                logger::info("CraftRecipe: insufficient materials for {:08X}", cobjFormId);
                return 0;
            }
            if (!outputCountValid(batches)) {
                logger::warn("CraftRecipe: output count overflow for COBJ {:08X}", cobjFormId);
                return 0;
            }

            if (!ConsumeComponents(costs, player, sources)) {
                logger::info("CraftRecipe: consume failed (materials no longer coverable) for {:08X}", cobjFormId);
                return 0;
            }
        }

        const std::int64_t total64 = static_cast<std::int64_t>(per) * batches;
        if (total64 <= 0 || total64 > std::numeric_limits<std::int32_t>::max()) {
            logger::warn("CraftRecipe: output count overflow for COBJ {:08X}", cobjFormId);
            return 0;
        }
        player->AddObjectToContainer(boundObj, RE::BSTSmartPointer<RE::ExtraDataList>{},
                                     static_cast<std::int32_t>(total64), nullptr,
                                     RE::ITEM_REMOVE_REASON::kNone);

        logger::info("CraftRecipe: crafted {:08X} batches={} (x{} each)", cobjFormId, batches, per);
        return batches;
    }
}

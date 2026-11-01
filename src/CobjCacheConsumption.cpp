#include "CobjCache.h"

#include "DebugLog.h"
#include "GodMode.h"
#include "MaterialPlanner.h"
#include "SaturatingAdd.h"

#include <RE/B/BGSComponent.h>
#include <RE/B/BGSInventoryList.h>
#include <RE/I/ITEM_REMOVE_REASON.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/T/TESObjectREFR.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CobjCache
{
    // Constraint: caps lookup and fallback retain source:src/CobjCache.cpp:PlayerCaps ownership.
    uint32_t PlayerCaps()
    {

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;

        std::uint32_t viaItem = 0, viaWalk = 0;
        if (auto* caps001 = RE::TESForm::GetFormByID(0x0000000F))
            player->GetItemCount(viaItem, caps001, false);

        if (player->inventoryList) {
            for (auto& item : player->inventoryList->data) {
                auto* obj = item.object;
                if (obj && obj->GetFormID() == 0x0000000F) {
                    const auto c = item.GetCount();
                    viaWalk = crafting::SaturateAddU32(viaWalk, c);
                }
            }
        }

        logger::info("PlayerCaps: GetItemCount={} inventoryWalk={}", viaItem, viaWalk);
        if (viaItem) return viaItem;
        return viaWalk;
    }

    std::vector<std::pair<RE::TESForm*, std::uint32_t>> CostsOf(const Recipe& r)
    {
        std::vector<std::pair<RE::TESForm*, std::uint32_t>> costs;
        costs.reserve(r.components.size());
        for (const auto& c : r.components)
            if (c.count) costs.emplace_back(c.form, c.count);
        return costs;
    }

    static std::uint64_t LiteralStack(RE::TESObjectREFR* cont, RE::TESBoundObject* obj)
    {
        if (!cont || !obj || !cont->inventoryList) return 0;
        std::uint64_t total = 0;
        for (auto& it : cont->inventoryList->data)
            if (it.object == obj) { auto c = it.GetCount(); if (c > 0) total = crafting::SaturateAddU64(total, static_cast<std::uint64_t>(c)); }
        return total;
    }

    bool ConsumeComponents(const std::vector<std::pair<RE::TESForm*, std::uint32_t>>& costs,
                           RE::PlayerCharacter* player,
                           const std::vector<RE::TESObjectREFR*>& sources,
                           bool dryRun,
                           bool includePlayer)
    {
        if (crafting::GodModeActive()) return true;
        if (includePlayer && !player) return false;

        std::vector<RE::TESObjectREFR*> conts;
        for (auto* source : sources) if (source) conts.push_back(source);
        if (includePlayer) conts.push_back(player);
        if (conts.empty()) return costs.empty();

        struct Need
        {
            RE::TESForm* form = nullptr;
            RE::BGSComponent* component = nullptr;
            RE::TESBoundObject* literal = nullptr;
            RE::TESObjectMISC* scrap = nullptr;
            std::uint64_t remaining = 0;
        };

        std::vector<Need> needs;
        std::unordered_map<RE::TESForm*, std::size_t> needIndex;
        for (const auto& [form, count] : costs) {
            if (count == 0) continue;
            if (!form) return false;
            const auto [it, inserted] = needIndex.emplace(form, needs.size());
            if (inserted) {
                auto* component = form->Is(RE::ENUM_FORM_ID::kCMPO)
                    ? static_cast<RE::BGSComponent*>(form) : nullptr;
                needs.push_back({ form, component, form->As<RE::TESBoundObject>(),
                                  component ? component->scrapItem : nullptr, count });
            } else {
                auto& remaining = needs[it->second].remaining;
                const auto max = std::numeric_limits<std::uint64_t>::max();
                remaining = remaining > max - count ? max : remaining + count;
            }
        }
        if (needs.empty()) return true;

        using ObjectKey = std::pair<RE::TESObjectREFR*, RE::TESBoundObject*>;
        std::map<ObjectKey, std::uint64_t> removals;
        std::map<ObjectKey, std::uint64_t> reserved;

        for (auto& need : needs) {
            for (auto* cont : conts) {
                if (need.remaining == 0) break;
                RE::TESBoundObject* literals[2] = {
                    need.literal,
                    static_cast<RE::TESBoundObject*>(need.scrap)
                };
                for (std::size_t i = 0; i < 2 && need.remaining > 0; ++i) {
                    auto* literal = literals[i];
                    if (!literal || (i == 1 && literal == literals[0])) continue;
                    const auto available = LiteralStack(cont, literal);
                    auto& used = reserved[{ cont, literal }];
                    if (used >= available) continue;
                    const auto take = std::min(need.remaining, available - used);
                    if (take == 0) continue;
                    removals[{ cont, literal }] += take;
                    used += take;
                    need.remaining -= take;
                }
            }
        }

        struct JunkCandidate
        {
            RE::TESObjectREFR* cont = nullptr;
            RE::TESBoundObject* obj = nullptr;
            RE::TESObjectMISC* misc = nullptr;
            MaterialPlanner::Bundle bundle;
        };
        std::vector<JunkCandidate> candidates;

        for (auto* cont : conts) {
            if (!cont || !cont->inventoryList) continue;
            std::map<RE::TESBoundObject*, std::uint64_t> stacks;
            for (auto& item : cont->inventoryList->data) {
                if (!item.object) continue;
                const auto count = item.GetCount();
                if (count > 0) stacks[item.object] = crafting::SaturateAddU64(stacks[item.object], static_cast<std::uint64_t>(count));
            }

            for (const auto& [obj, stackCount] : stacks) {
                auto* misc = obj->As<RE::TESObjectMISC>();
                if (!misc || !misc->componentData) continue;
                const auto alreadyUsed = reserved[{ cont, obj }];
                if (alreadyUsed >= stackCount) continue;

                MaterialPlanner::Bundle bundle;
                bundle.yields.assign(needs.size(), 0);
                bundle.available = stackCount - alreadyUsed;
                bool useful = false;
                std::unordered_set<RE::TESForm*> seen;
                for (const auto& entry : *misc->componentData) {
                    if (!entry.first || !seen.insert(entry.first).second || entry.second.i <= 0) continue;
                    const auto it = needIndex.find(entry.first);
                    if (it == needIndex.end()) continue;
                    bundle.yields[it->second] = crafting::SaturateAddU64(
                        bundle.yields[it->second], static_cast<std::uint32_t>(entry.second.i));
                    useful = true;
                }
                if (useful) candidates.push_back({ cont, obj, misc, std::move(bundle) });
            }
        }

        std::vector<std::uint64_t> deficits;
        deficits.reserve(needs.size());
        for (const auto& need : needs) deficits.push_back(need.remaining);

        std::vector<MaterialPlanner::Bundle> bundles;
        bundles.reserve(candidates.size());
        for (const auto& candidate : candidates) bundles.push_back(candidate.bundle);
        const auto junkPlan = MaterialPlanner::Build(deficits, std::move(bundles));
        if (!junkPlan.satisfied) {
            for (std::size_t i = 0; i < junkPlan.remaining.size(); ++i) {
                if (junkPlan.remaining[i] == 0) continue;
                const char* editorId = needs[i].form->GetFormEditorID();
                logger::info("ConsumeComponents: short on '{}' by {} -> abort (nothing consumed)",
                    (editorId && editorId[0]) ? editorId : "?", junkPlan.remaining[i]);
                break;
            }
            return false;
        }

        std::map<ObjectKey, std::uint64_t> surplus;
        auto allocationRemaining = deficits;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto selected = junkPlan.selected[i];
            if (selected == 0) continue;
            const auto& candidate = candidates[i];
            removals[{ candidate.cont, candidate.obj }] += selected;

            std::unordered_set<RE::TESForm*> seen;
            for (const auto& entry : *candidate.misc->componentData) {
                if (!entry.first || !seen.insert(entry.first).second || entry.second.i <= 0) continue;
                const auto yield = static_cast<std::uint32_t>(entry.second.i);
                if (selected > std::numeric_limits<std::uint64_t>::max() / yield) return false;
                const auto produced = selected * yield;
                std::uint64_t consumed = 0;
                if (const auto it = needIndex.find(entry.first); it != needIndex.end()) {
                    auto& remaining = allocationRemaining[it->second];
                    consumed = std::min(remaining, produced);
                    remaining -= consumed;
                }
                const auto left = produced - consumed;
                auto* component = entry.first->As<RE::BGSComponent>();
                auto* loose = component ? component->scrapItem : nullptr;
                if (left > 0 && loose) {
                    auto& total = surplus[{ candidate.cont, loose }];
                    if (total > std::numeric_limits<std::uint64_t>::max() - left) return false;
                    total += left;
                }
            }
        }

        if (dryRun) return true;

        for (const auto& [key, total] : removals) {
            auto [cont, obj] = key;
            auto remaining = total;
            while (remaining > 0) {
                const auto chunk = static_cast<std::int32_t>(std::min<std::uint64_t>(
                    remaining, static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
                RE::TESObjectREFR::RemoveItemData remove{ obj, chunk };
                cont->RemoveItem(remove);
                remaining -= static_cast<std::uint32_t>(chunk);
            }
        }

        for (const auto& [key, total] : surplus) {
            auto [cont, obj] = key;
            auto remaining = total;
            while (remaining > 0) {
                const auto chunk = static_cast<std::int32_t>(std::min<std::uint64_t>(
                    remaining, static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())));
                cont->AddObjectToContainer(obj, RE::BSTSmartPointer<RE::ExtraDataList>{}, chunk,
                    nullptr, RE::ITEM_REMOVE_REASON::kNone);
                remaining -= static_cast<std::uint32_t>(chunk);
            }
        }
        return true;
    }
}

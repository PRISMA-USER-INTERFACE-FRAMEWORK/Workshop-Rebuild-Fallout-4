#pragma once

// Constraint: snapshots must not outlive their request; inventory mutation invalidates counts.

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SaturatingAdd.h"
#include "GodMode.h"

#include <RE/B/BGSComponent.h>
#include <RE/B/BGSInventoryList.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/T/TESObjectREFR.h>

namespace CobjCache
{
    class InventorySnapshot
    {
    public:
        [[nodiscard]] static InventorySnapshot Build(RE::PlayerCharacter* a_player,
                                                     const std::vector<RE::TESObjectREFR*>& a_sources)
        {
            InventorySnapshot snap;
            if (crafting::GodModeActive()) return snap;
            std::vector<RE::TESObjectREFR*> carried;
            if (a_player) carried.push_back(a_player);
            snap.m_carried = BuildIndex(carried);
            snap.m_stored = BuildIndex(a_sources);
            return snap;
        }

        [[nodiscard]] std::uint32_t Carried(RE::TESForm* a_form) const { return AvailFrom(m_carried, a_form); }
        [[nodiscard]] std::uint32_t Stored(RE::TESForm* a_form) const { return AvailFrom(m_stored, a_form); }
        [[nodiscard]] std::uint32_t Available(RE::TESForm* a_form) const
        {
            return crafting::SaturateAddU32(Carried(a_form), Stored(a_form));
        }

    private:
        struct Index
        {
            std::unordered_map<RE::TESBoundObject*, std::uint64_t> literal;
            std::unordered_map<RE::TESForm*, std::uint64_t>        compYield;
        };

        [[nodiscard]] static Index BuildIndex(const std::vector<RE::TESObjectREFR*>& a_conts)
        {
            Index idx;
            for (auto* cont : a_conts) {
                if (!cont || !cont->inventoryList) continue;
                for (auto& it : cont->inventoryList->data) {
                    if (!it.object) continue;
                    auto c = it.GetCount();
                    if (c <= 0) continue;
                    auto& total = idx.literal[it.object];
                    total = crafting::SaturateAddU64(total, static_cast<std::uint64_t>(c));
                }
            }
            for (auto& [obj, cnt] : idx.literal) {
                auto* misc = obj->As<RE::TESObjectMISC>();
                if (!misc || !misc->componentData) continue;

                std::unordered_set<RE::TESForm*> seen;
                for (auto& entry : *misc->componentData) {
                    if (!entry.first || !seen.insert(entry.first).second) continue;
                    if (entry.first->Is(RE::ENUM_FORM_ID::kCMPO) &&
                        static_cast<RE::BGSComponent*>(entry.first)->scrapItem == misc) continue;
                    const auto contribution =
                        crafting::SaturatePositiveMulU64(cnt, entry.second.i);
                    if (contribution == 0) continue;
                    auto& total = idx.compYield[entry.first];
                    total = crafting::SaturateAddU64(total, contribution);
                }
            }
            return idx;
        }

        [[nodiscard]] static std::uint32_t AvailFrom(const Index& a_idx, RE::TESForm* a_form)
        {
            if (!a_form) return 0;
            if (crafting::GodModeActive()) return 0xFFFFFFFFu;
            RE::BGSComponent*   cmpo       = a_form->Is(RE::ENUM_FORM_ID::kCMPO)
                                              ? static_cast<RE::BGSComponent*>(a_form) : nullptr;
            RE::TESBoundObject* cmpoObj    = a_form->As<RE::TESBoundObject>();
            RE::TESObjectMISC*  scrapObj   = cmpo ? cmpo->scrapItem : nullptr;
            auto*               scrapBound = static_cast<RE::TESBoundObject*>(scrapObj);

            std::uint64_t total = 0;
            if (cmpoObj)
                if (auto it = a_idx.literal.find(cmpoObj); it != a_idx.literal.end())
                    total = crafting::SaturateAddU64(total, it->second);
            if (scrapObj && scrapBound != cmpoObj)
                if (auto it = a_idx.literal.find(scrapBound); it != a_idx.literal.end())
                    total = crafting::SaturateAddU64(total, it->second);
            if (cmpo) {
                if (auto it = a_idx.compYield.find(a_form); it != a_idx.compYield.end())
                    total = crafting::SaturateAddU64(total, it->second);

            }
            return static_cast<std::uint32_t>((std::min)(total, static_cast<std::uint64_t>(0xFFFFFFFFu)));
        }

        Index m_carried;
        Index m_stored;
    };
}

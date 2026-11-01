#include "WeaponBenchSnapshot.h"

#include "CobjCacheInternal.h"
#include "WeaponBenchJson.h"
#include "WeaponBench.h"
#include "SaturatingAdd.h"

#include <RE/B/BGSInventoryList.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSMod.h>
#include <RE/B/BGSModelMaterialSwap.h>
#include <RE/B/BGSMaterialSwap.h>
#include <RE/B/BGSObjectInstanceExtra.h>
#include <RE/B/BGSTypedKeywordValue.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/T/TESObjectWEAP.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <ranges>
#include <string_view>
#include <set>
#include <tuple>

namespace WeaponBench
{
    namespace
    {
        namespace wb = crafting::weaponbench;
        using Mod = RE::BGSMod::Attachment::Mod;
        using Stack = RE::BGSInventoryItem::Stack;

        constexpr std::size_t kMaxIndexEntries = 128;

        std::vector<RegistryEntry> g_registry;
        std::uint64_t g_revision = 0;
        std::set<std::string> g_logged;

        struct Installed
        {
            std::vector<RE::BGSMod::ObjectIndexData> entries;
            bool readable = true;
        };

        struct WeaponStack
        {
            RE::TESBoundObject* object = nullptr;
            std::uint32_t stackIndex = 0;
            bool equipped = false;
            bool locked = false;
            bool hasTemporary = false;
            Installed installed;
            std::uint64_t fingerprint = 0;
        };

        struct EnabledPart
        {
            RE::BGSMod::ObjectIndexData data{};
            Mod* mod = nullptr;
        };

        struct Loose
        {
            std::uint32_t rank1 = 0;
            bool ranked = false;
        };

        using OwnedMap = std::map<const RE::TESBoundObject*, Loose>;

        void LogOnce(const char* a_message)
        {
            if (g_logged.emplace(a_message).second) logger::warn("PrismaCrafting: weapon bench snapshot: {}", a_message);
        }

        // Constraint: GetIndexData dereferences values without a null check (lib/commonlibf4/src/RE/B/BGSObjectInstanceExtra.cpp), so values must be tested first.
        Installed ReadInstalled(const Stack& a_stack)
        {
            Installed out;
            const auto* extra = a_stack.extra.get();
            const auto* instance = extra ? extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
            if (!instance || instance->values == nullptr) return out;
            const auto data = instance->GetIndexData();
            if (data.size() > kMaxIndexEntries) {
                out.readable = false;
                return out;
            }
            out.entries.assign(data.begin(), data.end());
            return out;
        }

        std::vector<wb::PartKey> EnabledKeys(const Installed& a_installed)
        {
            std::vector<wb::PartKey> keys;
            for (const auto& e : a_installed.entries) {
                if (e.disabled == 0) keys.push_back({ e.objectID, e.rank });
            }
            return keys;
        }

        // Constraint: both inventory walks take the list read lock; stack pointers must not outlive it.
        std::vector<WeaponStack> EnumerateWeapons(RE::PlayerCharacter& a_player)
        {
            std::vector<WeaponStack> out;
            if (!a_player.inventoryList) return out;
            RE::BSAutoReadLock lock(a_player.inventoryList->rwLock);
            for (auto& item : a_player.inventoryList->data) {
                auto* object = item.object;
                if (!object || !object->Is(RE::ENUM_FORM_ID::kWEAP)) continue;
                const auto first = out.size();
                bool temporary = false;
                std::uint32_t index = 0;
                for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++index) {
                    if (stack->flags.any(Stack::Flag::kTemporary)) {
                        temporary = true;
                        continue;
                    }
                    if (stack->GetCount() == 0) continue;
                    WeaponStack w;
                    w.object = object;
                    w.stackIndex = index;
                    w.equipped = stack->IsEquipped();
                    w.locked = stack->flags.any(Stack::Flag::kEquipStateLocked);
                    w.installed = ReadInstalled(*stack);
                    wb::StackFacts facts;
                    facts.baseFormId = object->GetFormID();
                    facts.count = stack->GetCount();
                    facts.equipped = w.equipped;
                    facts.installed = EnabledKeys(w.installed);
                    w.fingerprint = wb::Fingerprint(facts);
                    out.push_back(std::move(w));
                }
                for (auto i = first; i < out.size(); ++i) out[i].hasTemporary = temporary;
            }
            return out;
        }

        OwnedMap CollectOwnedMisc(RE::PlayerCharacter& a_player)
        {
            OwnedMap owned;
            if (!a_player.inventoryList) return owned;
            RE::BSAutoReadLock lock(a_player.inventoryList->rwLock);
            for (auto& item : a_player.inventoryList->data) {
                if (!item.object || !item.object->Is(RE::ENUM_FORM_ID::kMISC)) continue;
                for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                    if (stack->GetCount() == 0) continue;
                    auto& entry = owned[item.object];
                    if (stack->extra && stack->extra->HasType(RE::EXTRA_DATA_TYPE::kModRank)) entry.ranked = true;
                    else entry.rank1 = crafting::SaturateAddU32(entry.rank1, stack->GetCount());
                }
            }
            return owned;
        }

        std::string ModName(Mod& a_mod)
        {
            return std::string(RE::TESFullName::GetFullName(a_mod));
        }

        RE::BGSKeyword* AttachKeyword(std::uint16_t a_index)
        {
            return RE::detail::BGSKeywordGetTypedKeywordByIndex(RE::KeywordType::kAttachPoint, a_index);
        }

        std::uint32_t AttachId(const Mod& a_mod)
        {
            const auto* keyword = AttachKeyword(a_mod.attachPoint.keywordIndex);
            return keyword ? keyword->GetFormID() : 0;
        }

        std::string MeshKey(std::string a_path)
        {
            for (auto& c : a_path)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c == '\\' ? '/' : c)));
            return a_path;
        }

        bool IsDummyMesh(std::string_view a_path)
        {
            std::string lower;
            lower.reserve(a_path.size());
            for (const char c : a_path)
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            return lower.find("dummy") != std::string::npos;
        }

        // Constraint: installed OMODs own the visible pieces; the base weapon model is often a receiver dummy.
        void CollectModMeshes(RE::BGSMod::Attachment::Mod* a_mod, int a_depth, std::vector<std::string>& a_out)
        {
            if (!a_mod || a_depth > 3) return;
            if (const char* own = a_mod->GetModel(); own && own[0]) {
                a_out.emplace_back(own);
                return;
            }
            RE::BGSMod::Container::Data data{};
            static_cast<RE::BGSMod::Container*>(a_mod)->GetData(&data);
            for (std::uint32_t i = 0; data.attachments && i < data.attachmentCount; ++i) {
                const auto before = a_out.size();
                CollectModMeshes(data.attachments[i].mod, a_depth + 1, a_out);
                if (a_out.size() > before) break;
            }
        }

        void AddMeshPiece(const std::string& a_path, std::vector<std::string>& a_out, std::vector<std::string>& a_keys)
        {
            if (a_path.empty()) return;
            auto key = MeshKey(a_path);
            if (std::ranges::find(a_keys, key) != a_keys.end()) return;
            a_keys.push_back(std::move(key));
            a_out.push_back(a_path);
        }

        wb::ModelInfo ModelFor(const RE::BGSModelMaterialSwap& a_source)
        {
            wb::ModelInfo out;
            if (const char* nif = a_source.GetModel(); nif && nif[0]) out.nifPath = nif;
            if (a_source.swapForm) CobjCache::CollectMaterialSwap(a_source.swapForm, out.matSwap);
            return out;
        }

        struct ModIndex
        {
            std::map<std::uint32_t, Mod*> byForm;
            std::map<std::uint32_t, std::vector<Mod*>> byAttach;
            bool built = false;
        };

        // Constraint: OMOD forms are static for the process, so the scan runs once instead of on every click.
        const ModIndex& Mods()
        {
            static ModIndex index;
            if (index.built) return index;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return index;
            const auto& mods = data->GetFormArray<Mod>();
            if (mods.empty()) return index;
            for (auto* mod : mods) {
                if (!mod) continue;
                index.byForm.emplace(mod->GetFormID(), mod);
                if (const auto attach = AttachId(*mod); attach) index.byAttach[attach].push_back(mod);
            }
            index.built = true;
            return index;
        }

        struct Slots
        {
            std::vector<std::uint32_t> ids;
            std::map<std::uint32_t, const RE::BGSKeyword*> forms;
        };

        void AddParents(const RE::BGSAttachParentArray& a_parents, Slots& a_slots)
        {
            if (!a_parents.array) return;
            for (std::uint32_t i = 0; i < a_parents.size; ++i) {
                const auto* keyword = AttachKeyword(a_parents.array[i].keywordIndex);
                if (!keyword) continue;
                const auto id = keyword->GetFormID();
                if (std::ranges::find(a_slots.ids, id) != a_slots.ids.end()) continue;
                a_slots.ids.push_back(id);
                a_slots.forms.emplace(id, keyword);
            }
        }

        struct Derived
        {
            Slots slots;
            std::vector<EnabledPart> parts;
            bool unreadable = false;
            bool hasRanked = false;
        };

        Derived DeriveSlots(const WeaponStack& a_stack, const RE::TESObjectWEAP& a_weapon)
        {
            Derived out;
            if (!a_stack.installed.readable) {
                out.unreadable = true;
                LogOnce("installed attachment list too large; weapon left read-only");
            }
            const auto& index = Mods();
            AddParents(a_weapon.attachParents, out.slots);
            for (const auto& e : a_stack.installed.entries) {
                if (e.disabled != 0) continue;
                if (e.rank > 1) out.hasRanked = true;
                const auto it = index.byForm.find(e.objectID);
                if (it == index.byForm.end()) {
                    out.unreadable = true;
                    LogOnce("installed attachment id not found among loaded OMODs; weapon left read-only");
                    continue;
                }
                out.parts.push_back({ e, it->second });
                AddParents(it->second->attachParents, out.slots);
            }
            return out;
        }

        void HarvestModMaterialSwaps(RE::BGSMod::Attachment::Mod* a_mod, int a_depth, wb::ModelInfo& a_model)
        {
            if (!a_mod || a_depth > 3) return;
            CobjCache::CollectMaterialSwap(a_mod->swapForm, a_model.matSwap);
            RE::BGSMod::Container::Data data{};
            static_cast<RE::BGSMod::Container*>(a_mod)->GetData(&data);
            for (std::uint32_t i = 0; data.propertyMods && i < data.propertyModCount; ++i) {
                auto& pm = data.propertyMods[i];
                if (pm.type == RE::BGSMod::Property::TYPE::kForm && pm.data.form)
                    if (auto* mswp = pm.data.form->As<RE::BGSMaterialSwap>())
                        CobjCache::CollectMaterialSwap(mswp, a_model.matSwap);
            }
            for (std::uint32_t i = 0; data.attachments && i < data.attachmentCount; ++i)
                HarvestModMaterialSwaps(data.attachments[i].mod, a_depth + 1, a_model);
        }

        void CollectModelMaterialSwaps(const RE::TESObjectWEAP& a_weapon, const Derived& a_derived, wb::ModelInfo& a_model)
        {
            if (auto* mms = RE::fallout_cast<RE::BGSModelMaterialSwap*>(const_cast<RE::TESObjectWEAP*>(&a_weapon)))
                CobjCache::CollectMaterialSwap(mms->swapForm, a_model.matSwap);
            if (a_weapon.weaponData.materialSwaps)
                for (auto* mswp : *a_weapon.weaponData.materialSwaps) CobjCache::CollectMaterialSwap(mswp, a_model.matSwap);
            for (const auto& p : a_derived.parts) HarvestModMaterialSwaps(p.mod, 0, a_model);
        }

        void ComposeInstalledModel(const RE::TESObjectWEAP& a_weapon, const Derived& a_derived, wb::SelectedWeapon& a_selected)
        {
            if (!a_selected.model || a_selected.model->nifPath.empty()) return;
            a_selected.model->matSwap.clear();
            CollectModelMaterialSwaps(a_weapon, a_derived, *a_selected.model);
            const auto base = a_selected.model->nifPath;
            std::vector<std::string> partMeshes;
            for (const auto& p : a_derived.parts) CollectModMeshes(p.mod, 0, partMeshes);

            std::vector<std::string> pieces;
            std::vector<std::string> keys;
            const bool dummyBase = IsDummyMesh(base);
            if (!dummyBase || partMeshes.empty()) AddMeshPiece(base, pieces, keys);
            for (const auto& mesh : partMeshes) AddMeshPiece(mesh, pieces, keys);
            if (pieces.empty()) return;

            a_selected.model->nifPath = pieces.front();
            a_selected.model->nifPaths.clear();
            if (pieces.size() > 1) a_selected.model->nifPaths = std::move(pieces);
            logger::debug("WeaponBench: model for {:08X}: base='{}' pieces={} dummyBase={}",
                          a_selected.baseId, base,
                          a_selected.model->nifPaths.empty() ? 1 : a_selected.model->nifPaths.size(), dummyBase);
        }

        std::string SlotName(const RE::BGSKeyword* a_keyword, std::size_t a_ordinal)
        {
            const char* editorId = a_keyword ? a_keyword->GetFormEditorID() : nullptr;
            std::string name = editorId ? editorId : "";
            if (name.size() > 3 && std::tolower(static_cast<unsigned char>(name[0])) == 'a' &&
                std::tolower(static_cast<unsigned char>(name[1])) == 'p' && name[2] == '_') {
                name.erase(0, 3);
            }
            for (auto& c : name) c = c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (name.empty()) {
                LogOnce("slot display name source unverified (R11); using numbered labels");
                return std::format("SLOT {}", a_ordinal + 1);
            }
            return name;
        }


        wb::SlotFacts BuildSlotFacts(std::uint32_t a_keyword, const Derived& a_derived, const OwnedMap& a_owned, wb::SlotInput& a_slot)
        {
            wb::SlotFacts facts;
            facts.keyword = a_keyword;

            for (const auto& p : a_derived.parts) {
                // Constraint: form flag bit 7 marks the null/stock attachment that must not be listed as an installed part.
                constexpr std::uint32_t kNullModFlagBit = 7;
                if (AttachId(*p.mod) != a_keyword || ((p.mod->GetFormFlags() >> kNullModFlagBit) & 1) != 0) continue;
                const auto name = ModName(*p.mod);
                if (name.empty()) continue;
                if (facts.installed) {
                    LogOnce("two installed attachments share one slot; showing the first");
                    continue;
                }
                const wb::PartKey part{ p.mod->GetFormID(), p.data.rank };
                facts.installed = part;
                wb::InstalledInput installed;
                installed.part = part;
                installed.name = name;
                installed.hasLooseForm = p.mod->GetLooseMod() != nullptr;
                for (const auto& other : a_derived.parts) {
                    if (other.mod != p.mod) installed.newConfig.push_back({ other.mod->GetFormID(), other.data.rank });
                }
                a_slot.installed = std::move(installed);
            }

            const bool dev = WeaponBench::DevEnabled();
            const auto& index = Mods();
            const auto attach = index.byAttach.find(a_keyword);
            if (attach == index.byAttach.end()) return facts;
            std::vector<std::pair<wb::ChoiceFacts, wb::ChoiceInput>> found;
            for (auto* mod : attach->second) {
                if (!mod) continue;
                auto* loose = mod->GetLooseMod();
                const auto owned = loose ? a_owned.find(loose) : a_owned.end();
                if (owned == a_owned.end() && !dev) continue;
                wb::ChoiceFacts choice;
                choice.part = { mod->GetFormID(), 1 };
                choice.mod = { static_cast<std::uint8_t>(std::to_underlying(mod->targetFormType.get())), a_keyword, mod->maxRank };
                choice.looseCount = dev ? 9 : owned->second.rank1;
                choice.looseHasRankedStack = dev ? false : owned->second.ranked;
                wb::ChoiceInput input;
                input.part = choice.part;
                input.name = ModName(*mod);
                if (input.name.empty() && loose) input.name = std::string(RE::TESFullName::GetFullName(*loose));
                if (input.name.empty()) continue;
                input.looseCount = choice.looseCount;
                if (auto model = ModelFor(*mod); !model.nifPath.empty()) input.model = std::move(model);
                found.emplace_back(choice, std::move(input));
            }
            std::ranges::sort(found, [](const auto& l, const auto& r) {
                return std::tie(l.second.name, l.first.part) < std::tie(r.second.name, r.first.part);
            });
            for (auto& [choice, input] : found) {
                facts.choices.push_back(choice);
                a_slot.choices.push_back(std::move(input));
            }
            return facts;
        }

        std::string WeaponName(const WeaponStack& a_stack)
        {
            return std::string(RE::TESFullName::GetFullName(*a_stack.object));
        }

        void RateChoices(wb::SelectedWeapon& a_selected, wb::WeaponFacts& a_facts, const Derived& a_derived,
            const std::string& a_session, std::uint64_t a_revision)
        {
            wb::LiveState state;
            state.enabled = state.viewAlive = state.menuAlive = state.playerCanAct = true;
            state.session = a_session;
            state.revision = a_revision;
            state.weapon = a_facts;
            const auto weaponKey = wb::WeaponKey{ a_selected.baseId, a_selected.stackIndex };
            for (std::size_t s = 0; s < a_facts.slots.size(); ++s) {
                auto& facts = a_facts.slots[s];
                auto& inputs = a_selected.slots[s].choices;
                for (std::size_t c = 0; c < facts.choices.size(); ++c) {
                    wb::ActionRequest request;
                    request.action = wb::Action::Install;
                    request.session = a_session;
                    request.revision = a_revision;
                    request.weaponId = wb::EncodeWeaponId(weaponKey);
                    request.slotId = wb::EncodeSlotId(facts.keyword);
                    request.partId = wb::EncodePartId(facts.choices[c].part);
                    const auto reason = wb::ValidateAction(request, state);
                    auto& input = inputs[c];
                    input.compatible = reason == wb::Reason::Ok && !a_derived.unreadable;
                    if (input.compatible) continue;
                    if (a_derived.unreadable) {
                        input.reason = "Installed attachments could not be read.";
                    } else if (reason == wb::Reason::Incompatible &&
                               wb::CheckCompat(facts.choices[c].mod, a_facts.baseFormType, facts.keyword, a_facts.attachParents) ==
                                   wb::CompatResult::RankUnsupported) {
                        input.reason = "Multi-rank attachments are not supported yet.";
                    } else if (facts.choices[c].looseHasRankedStack && reason == wb::Reason::NoLooseItem) {
                        input.reason = wb::ReasonMessage(wb::Reason::LooseRankedStack);
                    } else {
                        input.reason = wb::ReasonMessage(reason);
                    }
                }
            }
        }

        wb::SelectedWeapon BuildSelected(const WeaponStack& a_stack, const std::string& a_session,
            std::uint64_t a_revision, const OwnedMap& a_owned, std::optional<wb::WeaponFacts>* a_outFacts = nullptr)
        {
            wb::SelectedWeapon selected;
            selected.baseId = a_stack.object->GetFormID();
            selected.stackIndex = a_stack.stackIndex;
            selected.name = WeaponName(a_stack);
            const auto* weapon = a_stack.object->As<RE::TESObjectWEAP>();
            if (!weapon) return selected;
            if (auto model = ModelFor(*weapon); !model.nifPath.empty()) selected.model = std::move(model);

            const auto derived = DeriveSlots(a_stack, *weapon);
            ComposeInstalledModel(*weapon, derived, selected);
            wb::WeaponFacts facts;
            facts.registryFingerprint = facts.liveFingerprint = a_stack.fingerprint;
            const bool dev = WeaponBench::DevEnabled();
            facts.locked = dev ? false : a_stack.locked;
            facts.hasTemporaryStack = dev ? false : a_stack.hasTemporary;
            facts.hasRankedInstalled = dev ? false : (derived.hasRanked || derived.unreadable);
            facts.baseFormType = static_cast<std::uint8_t>(std::to_underlying(weapon->GetFormType()));
            facts.attachParents = derived.slots.ids;
            for (std::size_t i = 0; i < derived.slots.ids.size(); ++i) {
                const auto keyword = derived.slots.ids[i];
                wb::SlotInput slot;
                slot.keyword = keyword;
                slot.name = SlotName(derived.slots.forms.at(keyword), i);
                facts.slots.push_back(BuildSlotFacts(keyword, derived, a_owned, slot));
                selected.slots.push_back(std::move(slot));
            }
            RateChoices(selected, facts, derived, a_session, a_revision);
            if (a_outFacts) *a_outFacts = std::move(facts);
            return selected;
        }

        std::optional<std::size_t> FindStack(const std::vector<WeaponStack>& a_stacks, const wb::WeaponKey& a_key)
        {
            for (std::size_t i = 0; i < a_stacks.size(); ++i) {
                if (a_stacks[i].object->GetFormID() == a_key.baseId && a_stacks[i].stackIndex == a_key.stackIndex) return i;
            }
            return std::nullopt;
        }

        void Fill(wb::SnapshotInput& a_in, std::optional<std::string> a_selected)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;
            const auto stacks = EnumerateWeapons(*player);
            std::optional<std::size_t> chosen;
            for (std::size_t i = 0; i < stacks.size(); ++i) {
                const wb::WeaponKey key{ stacks[i].object->GetFormID(), stacks[i].stackIndex };
                g_registry.push_back({ stacks[i].object, stacks[i].stackIndex, stacks[i].fingerprint });
                a_in.weapons.push_back({ key.baseId, key.stackIndex, WeaponName(stacks[i]), stacks[i].equipped });
                if (!chosen && stacks[i].equipped) chosen = i;
            }
            if (a_selected) {
                if (const auto key = wb::DecodeWeaponId(*a_selected)) {
                    if (const auto match = FindStack(stacks, *key)) chosen = *match;
                }
            }
            if (!chosen && !stacks.empty()) chosen = 0;
            if (!chosen) return;
            a_in.selected = BuildSelected(stacks[*chosen], a_in.session, a_in.revision, CollectOwnedMisc(*player));
        }
    }

    const std::vector<RegistryEntry>& Registry()
    {
        return g_registry;
    }

    std::uint64_t Revision()
    {
        return g_revision;
    }

    wb::Reason ValidateLive(const wb::ActionRequest& a_req, wb::LiveState a_state)
    {
        a_state.revision = g_revision;
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto key = wb::DecodeWeaponId(a_req.weaponId);
        if (player && key) {
            const auto stacks = EnumerateWeapons(*player);
            if (const auto match = FindStack(stacks, *key)) {
                const auto& stack = stacks[*match];
                const auto entry = std::ranges::find_if(g_registry, [&](const RegistryEntry& a_entry) {
                    return a_entry.object == stack.object && a_entry.stackIndex == stack.stackIndex;
                });
                if (entry != g_registry.end()) {
                    BuildSelected(stack, a_state.session, a_state.revision, CollectOwnedMisc(*player), &a_state.weapon);
                    if (a_state.weapon) a_state.weapon->registryFingerprint = entry->fingerprint;
                }
            }
        }
        return wb::ValidateAction(a_req, a_state);
    }

    std::string BuildSnapshotPayload(const std::string& a_session, std::optional<std::string> a_selected)
    {
        wb::SnapshotInput in;
        in.session = a_session;
        in.revision = ++g_revision;
        g_registry.clear();
        try {
            Fill(in, a_selected);
            return wb::BuildSnapshot(in).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        } catch (const std::exception& e) {
            logger::error("PrismaCrafting: weapon bench snapshot failed, sending an empty list: {}", e.what());
            g_registry.clear();
            in.weapons.clear();
            in.selected.reset();
            return wb::BuildSnapshot(in).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
    }
}

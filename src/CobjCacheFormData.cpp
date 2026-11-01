#include "CobjCacheInternal.h"

#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueInfo.h>
#include <RE/B/BGSMaterialSwap.h>
#include <RE/B/BGSPerk.h>
#include <RE/B/BGSPropertySheet.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESFile.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESObjectACTI.h>
#include <RE/T/TESObjectCONT.h>
#include <RE/T/TESObjectLIGH.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESObjectSTAT.h>
#include <RE/RTTI.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace CobjCache
{
    std::string GetName(RE::TESForm* form)
    {
        if (!form) return {};

        if (auto sv = RE::TESFullName::GetFullName(*form); !sv.empty())
            return std::string(sv);
        if (auto* fn = RE::fallout_cast<RE::TESFullName*>(form)) {
            const char* raw = fn->GetFullName();
            if (raw && raw[0]) return raw;
        }
        const char* eid = form->GetFormEditorID();
        return (eid && eid[0]) ? eid : std::string{};
    }


    void CollectMaterialSwap(RE::BGSMaterialSwap* mswp, std::string& out)
    {
        if (!mswp) return;
        auto norm = [](const char* s) {
            std::string o;
            for (const char* p = s; p && *p; ++p) {
                char c = (*p == '\\') ? '/' : *p;
                o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (o.rfind("materials/", 0) == 0) o.erase(0, 10);
            return o;
        };
        for (auto& kv : mswp->swapMap) {
            std::string orig = norm(kv.first.c_str());
            std::string repl = norm(kv.second.swapMaterial.c_str());
            if (orig.empty() || repl.empty()) continue;
            if (!out.empty()) out += ';';
            out += orig; out += '|'; out += repl;
        }
    }

    std::string SourceFile(RE::TESForm* form)
    {
        if (!form) return {};
        if (auto* file = form->GetFile(0); file) {
            auto name = file->GetFilename();
            if (!name.empty()) return std::string(name);
        }
        return {};
    }

    std::string CreatedKey(RE::TESForm* form)
    {
        if (!form) return {};
        std::string plugin = SourceFile(form);
        if (plugin.empty()) return {};
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%X", static_cast<std::uint32_t>(form->GetLocalFormID()));
        return plugin + "|" + buf;
    }

    void ComputeReqOwnership(const Recipe& r, RE::PlayerCharacter* player,
                                    std::vector<bool>& perkOwned, std::vector<bool>& toolOk)
    {
        perkOwned.reserve(r.reqPerks.size());
        for (const auto& pr : r.reqPerks) {
            bool owned = false;
            if (player && pr.formRef)
                if (auto* perk = RE::TESForm::GetFormByID(pr.formRef); perk && perk->Is(RE::ENUM_FORM_ID::kPERK)) {
                    const auto rank = player->GetPerkRank(static_cast<RE::BGSPerk*>(perk));
                    owned = pr.op.empty() ? rank > 0 : CmpOp(rank, pr.op, pr.value);
                }
            perkOwned.push_back(owned);
        }
        toolOk.reserve(r.toolGroups.size());
        for (const auto& g : r.toolGroups) {
            bool ok = false;
            for (const auto& opt : g.options) {
                if (!opt.formRef) continue;
                auto* item = RE::TESForm::GetFormByID(opt.formRef);
                if (!item) continue;
                uint32_t c = 0; if (player) player->GetItemCount(c, item, false);
                if (c > 0) { ok = true; break; }
            }
            toolOk.push_back(ok);
        }
    }

    struct ResourceAV
    {
        const char* editorId;
        const char* label;
    };
    static constexpr ResourceAV kResourceAVs[] = {
        { "Power",       "Power"   },
        { "Food",        "Food"    },
        { "Water",       "Water"   },
        { "Safety",      "Defense" },
        { "Beds",        "Beds"    },
        { "PeopleTotal", "People"  },
    };

    std::size_t s_resSheetEntries = 0;
    std::size_t s_resMatched      = 0;

    [[nodiscard]] static bool ActorValueIsInt(RE::TESForm* a_av)
    {
        const auto* info = a_av ? a_av->As<RE::ActorValueInfo>() : nullptr;
        return info && info->avType.get() == RE::ActorValue::AVType::kIntValue;
    }

    [[nodiscard]] static bool ActorValueMatches(RE::TESForm* a_av, const char* a_editorId)
    {
        if (!a_av || !a_editorId) return false;
        if (const char* eid = a_av->GetFormEditorID(); eid && eid[0])
            return std::strcmp(eid, a_editorId) == 0;
        if (auto full = RE::TESFullName::GetFullName(*a_av); !full.empty())
            return full == a_editorId;
        return false;
    }

    void CollectResourceValues(RE::TESForm* a_created, std::vector<ResourceValue>& a_out,
                               std::size_t& sheetEntries, std::size_t& matched)
    {
        if (!a_created) return;

        const RE::BGSPropertySheet* sheet = nullptr;
        if (auto* acti = a_created->As<RE::TESObjectACTI>())      sheet = acti;
        else if (auto* stat = a_created->As<RE::TESObjectSTAT>()) sheet = stat;
        else if (auto* cont = a_created->As<RE::TESObjectCONT>()) sheet = cont;
        else if (auto* ligh = a_created->As<RE::TESObjectLIGH>()) sheet = ligh;
        if (!sheet || !sheet->properties) return;

        sheetEntries += sheet->properties->size();

        for (const auto& want : kResourceAVs) {
            for (const auto& entry : *sheet->properties) {
                RE::TESForm* av = entry.first;
                if (!ActorValueMatches(av, want.editorId)) continue;

                const float v = ActorValueIsInt(av)
                                    ? static_cast<float>(entry.second.i)
                                    : entry.second.f;
                ++matched;
                if (v != 0.0f) a_out.push_back({ want.label, v });
                break;
            }
        }
    }


}

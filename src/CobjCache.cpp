#include "CobjCache.h"

#include "CraftingConfig.h"
#include "MaterialPlanner.h"
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
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CobjCache
{

    static std::vector<Recipe>                        s_recipes;
    static std::vector<RE::BGSConstructibleObject*>   s_forms;
    static std::unordered_map<uint32_t, size_t>       s_byFormId;
    static bool                                        s_built = false;
    static std::string                                 s_cachePath;
    static std::recursive_mutex                        s_cacheLock;

    static std::string EscapeJson(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (size_t i = 0; i < s.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) break;

                if (c >= 0xF0) {
                    while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) ++i;
                    break;
                }
                out += static_cast<char>(c);
            }
        }
        return out;
    }

    static std::string GetName(RE::TESForm* form)
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

    static RE::TESForm* DisplayForm(RE::TESForm* created)
    {
        if (auto* list = created ? created->As<RE::BGSListForm>() : nullptr) {
            for (auto* entry : list->arrayOfForms)
                if (entry) return entry;
        }
        return created;
    }

    static std::string SourceFile(RE::TESForm* form)
    {
        if (!form) return {};
        if (auto* file = form->GetFile(0); file) {
            auto name = file->GetFilename();
            if (!name.empty()) return std::string(name);
        }
        return {};
    }

    static std::string CreatedKey(RE::TESForm* form)
    {
        if (!form) return {};
        std::string plugin = SourceFile(form);
        if (plugin.empty()) return {};
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%X", static_cast<std::uint32_t>(form->GetLocalFormID()));
        return plugin + "|" + buf;
    }

    static std::string LowerStr(std::string s)
    {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    static bool HasAny(const std::string& hay, std::initializer_list<const char*> needles)
    {
        for (auto* n : needles) if (hay.find(n) != std::string::npos) return true;
        return false;
    }

    static int CompRarityWeight(const std::string& nameLower)
    {
        if (HasAny(nameLower, { "nuclear material", "fiber optic", "fiberoptic", "circuitry", "ballistic fiber",
                                "gold", "silver", "crystal", "antiseptic", "lead", "fusion", "military grade" }))
            return 8;
        if (HasAny(nameLower, { "aluminum", "copper", "screw", "spring", "gear", "oil", "acid", "adhesive",
                                "asbestos", "fiberglass", "antibiotic", "ceramic", "nuclear" }))
            return 3;
        return 1;
    }

    static std::string Categorize(const std::string& formType, const std::string& nameLower)
    {
        if (HasAny(nameLower, {"backpack","duffle","rucksack","satchel","bandolier","mailbag","shoulder bag","pouch","carrying device"})) return "BACKPACKS";
        if (HasAny(nameLower, {"gas mask","gasmask","respirator","goggle","eyepatch","visor","night vision"})) return "HEADGEAR";
        if (formType == "WEAP") {
            if (HasAny(nameLower, {"grenade","molotov"})) return "GRENADES";
            if (HasAny(nameLower, {"mine","caltrop","trap"})) return "MINES";
            if (HasAny(nameLower, {"knife","blade","sword","machete"," bat","knuckle","fist","axe","hammer","ripper","melee","spear","baton","club","gauntlet"})) return "MELEE";
            return "WEAPONS";
        }
        if (formType == "ARMO") {
            if (HasAny(nameLower, {"mask","hood","helm","hat","goggle","visor","respirator","glasses","eyepatch","scarf","bandana"})) return "HEADGEAR";
            return "ARMOR";
        }
        if (formType == "AMMO") return "AMMO";
        if (formType == "BOOK") return "HOLOTAPES";
        if (formType == "ALCH" || formType == "INGR") {
            if (HasAny(nameLower, {"stim","medkit","med-x","med x","heal","bandage","gauze","splint","tourniquet","antibiotic","anodyne","cure","first aid","trauma","rad-away","radaway","rad-x","radx","addictol","antivenom","hydra","quikclot","ibuprofen","injector","iodide","painkiller","morphine","antiseptic","disinfect","salve","ointment","aspirin","penicillin","medic","pharmaceutical"})) return "HEALING";
            if (HasAny(nameLower, {"jet","psycho","buffout","mentat","daytripper","day tripper","calmex","overdrive","fury","ultra jet","x-cell","stealth boy","skeeto","bufftats","chem","pax","berry"})) return "DRUGS";
            if (HasAny(nameLower, {"water","nuka","beer","wine","vodka","whiskey","rum","liquor","beverage","drink","juice","coffee","tea","milk","cola","moonshine","bourbon","buzzbites"})) return "DRINKS";
            return "FOOD";
        }
        return "UTILITY";
    }

    static std::string WeaponSubcat(RE::TESForm* created)
    {
        auto* kwf = created->As<RE::BGSKeywordForm>();
        if (!kwf || !kwf->keywords) return {};
        std::set<std::string> have;
        for (std::uint32_t i = 0; i < kwf->numKeywords; ++i) {
            RE::BGSKeyword* kw = kwf->keywords[i];
            if (!kw) continue;
            if (const char* eid = kw->GetFormEditorID(); eid && eid[0]) have.insert(eid);
        }
        auto any = [&](std::initializer_list<const char*> names) {
            for (const char* n : names) if (have.count(n)) return true;
            return false;
        };
        if (any({ "WeaponTypeMinigun","WeaponTypeGatlingLaser","WeaponTypeFatman","WeaponTypeMissileLauncher",
                  "WeaponTypeHeavyGun","WeaponTypeBroadsider","WeaponTypeJunkJet","WeaponTypeFlamer" }))
            return "Heavy Weapons";
        if (any({ "WeaponTypeShotgun" })) return "Shotguns";
        if (any({ "WeaponTypeSniper" })) return "Sniper Rifles";
        if (any({ "WeaponTypeAssaultRifle","WeaponTypeGaussRifle","WeaponTypeRailwayRifle",
                  "WeaponTypeLaserMusket","WeaponTypeCryolater","WeaponTypeRifle" }))
            return "Rifles";
        if (any({ "WeaponTypePistol" })) return "Pistols";
        if (any({ "WeaponTypeGrenade","WeaponTypeMolotov","WeaponTypePulseGrenade","WeaponTypePlasmaGrenade",
                  "WeaponTypeNukaGrenade","WeaponTypeCryoGrenade" }))
            return "Grenades";
        if (any({ "WeaponTypeMine","WeaponTypeBottlecapMine","WeaponTypePlasmaMine","WeaponTypeNukaMine",
                  "WeaponTypePulseMine","WeaponTypeCryoMine" }))
            return "Mines";
        if (any({ "WeaponTypeMelee2H","WeaponTypeShishkebab","WeaponTypeRipper" })) return "Two-Handed Melee";
        if (any({ "WeaponTypeMelee1H" })) return "One-Handed Melee";
        if (any({ "WeaponTypeHandToHand","WeaponTypeUnarmed" })) return "Unarmed";
        if (any({ "WeaponTypeSyringer","WeaponTypeGammaGun","WeaponTypeFlareGun","WeaponTypeAlienBlaster" }))
            return "Special Weapons";
        return {};
    }

    static std::string ArmorSubcat(RE::TESObjectARMO* armo)
    {
        if (!armo) return {};
        const std::uint32_t s = armo->bipedModelData.bipedObjectSlots;
        constexpr std::uint32_t kTorso  = (1u << 3) | (1u << 6)  | (1u << 11);
        constexpr std::uint32_t kArm    = (1u << 7) | (1u << 8)  | (1u << 12) | (1u << 13);
        constexpr std::uint32_t kLeg    = (1u << 9) | (1u << 10) | (1u << 14) | (1u << 15);
        constexpr std::uint32_t kHand   = (1u << 4) | (1u << 5);
        constexpr std::uint32_t kHead   = (1u << 0) | (1u << 1)  | (1u << 2);
        constexpr std::uint32_t kShield = (1u << 29);
        constexpr std::uint32_t kAcc    = (1u << 16) | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 22);
        if (s & kTorso)  return "Chest";
        if (s & kArm)    return "Arms";
        if (s & kLeg)    return "Legs";
        if (s & kHand)   return "Hands";
        if (s & kHead)   return "Head";
        if (s & kShield) return "Shield";
        if (s & kAcc)    return "Accessories";
        return {};
    }

    static std::string FriendlyFilter(const std::string& eid)
    {
        std::string s = eid;
        for (const char* pre : { "WorkshopRecipeFilter", "RecipeFilter", "Workshop", "Recipe", "Filter" }) {
            size_t n = std::strlen(pre);
            if (s.size() >= n && s.compare(0, n, pre) == 0) { s.erase(0, n); break; }
        }
        if (s.empty()) return eid;
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
                !std::isupper(static_cast<unsigned char>(s[i - 1]))) out += ' ';
            out += c;
        }
        return out;
    }

    static std::string MenuPathFromFilter(const std::string& eid)
    {
        std::string s = eid;
        for (const char* pre : { "WorkshopRecipeFilter", "RecipeFilter", "Workshop", "Recipe", "Filter" }) {
            size_t n = std::strlen(pre);
            if (s.size() >= n && s.compare(0, n, pre) == 0) { s.erase(0, n); break; }
        }
        if (s.empty()) return {};

        std::vector<std::string> parts;
        std::string cur;
        for (char c : s) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) parts.push_back(cur);

        std::string out;
        for (const std::string& w : parts) {
            std::string seg;
            for (size_t i = 0; i < w.size(); ++i) {
                char c = w[i];
                if (i > 0 && std::isupper(static_cast<unsigned char>(c)) &&
                    !std::isupper(static_cast<unsigned char>(w[i - 1]))) seg += ' ';
                seg += c;
            }
            if (seg.empty()) continue;
            if (!out.empty()) out += " > ";
            out += seg;
        }
        return out;
    }

    static std::string CleanToolName(const std::string& in)
    {
        std::string s; s.reserve(in.size());
        int brace = 0, brack = 0;
        for (char c : in) {
            if (c == '{') { ++brace; continue; }
            if (c == '}') { if (brace) --brace; continue; }
            if (c == '[') { ++brack; continue; }
            if (c == ']') { if (brack) --brack; continue; }
            if (!brace && !brack) s += c;
        }
        size_t a = s.find_first_not_of(" \t-:|");
        size_t b = s.find_last_not_of(" \t");
        return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    }

    static void CollectPerkReqs(const RE::TESCondition& cond, std::vector<PerkReq>& out)
    {
        for (auto* item = cond.head; item && out.size() < 6; item = item->next) {
            if (item->data.functionData.function.underlying() !=
                static_cast<uint16_t>(RE::SCRIPT_OUTPUT::kScript_HasPerk))
                continue;
            auto* perk = static_cast<RE::BGSPerk*>(item->data.functionData.param[0]);
            if (!perk) continue;
            uint32_t id = perk->GetFormID();
            if (std::any_of(out.begin(), out.end(), [&](const PerkReq& p) { return p.formId == id; }))
                continue;
            std::string nm = GetName(perk);
            out.push_back(PerkReq{ id, nm.empty() ? "Perk" : nm });
        }
    }

    static void CollectToolReqs(const RE::TESCondition& cond, std::vector<ToolGroup>& out)
    {
        std::vector<std::vector<ToolOption>> groups;
        std::vector<ToolOption> group;
        std::set<uint32_t>      groupSeen;
        auto flush = [&]() {
            if (!group.empty()) groups.push_back(group);
            group.clear(); groupSeen.clear();
        };
        for (auto* item = cond.head; item && groups.size() < 8; item = item->next) {
            const uint16_t fn = item->data.functionData.function.underlying();
            auto* form = static_cast<RE::TESForm*>(item->data.functionData.param[0]);

            const bool isToolFn = (fn == static_cast<uint16_t>(RE::SCRIPT_OUTPUT::kScript_GetItemCount));
            if (isToolFn && form) {
                std::string label = CleanToolName(GetName(form));
                if (!label.empty() && groupSeen.insert(form->GetFormID()).second)
                    group.push_back(ToolOption{ form->GetFormID(), std::move(label) });
            }
            if (!item->data.compareOr) flush();
        }
        flush();

        auto optIds = [](const std::vector<ToolOption>& g) {
            std::set<uint32_t> s; for (auto& o : g) s.insert(o.formId); return s;
        };
        auto isSubset = [](const std::set<uint32_t>& a, const std::set<uint32_t>& b) {
            for (uint32_t x : a) if (!b.count(x)) return false;
            return true;
        };
        for (size_t i = 0; i < groups.size(); ++i) {
            auto idsI = optIds(groups[i]);
            bool redundant = false;
            for (size_t j = 0; j < groups.size() && !redundant; ++j) {
                if (i == j) continue;
                auto idsJ = optIds(groups[j]);
                if (isSubset(idsJ, idsI)) {
                    if (idsJ.size() < idsI.size()) redundant = true;
                    else if (j < i)                redundant = true;
                }
            }
            if (redundant) continue;
            out.push_back(ToolGroup{ groups[i] });
        }
    }

    static std::string ToolGroupLabel(const ToolGroup& g)
    {
        std::string s;
        for (const auto& o : g.options) { if (!s.empty()) s += " / "; s += o.name; }
        return s;
    }

    static void ComputeReqOwnership(const Recipe& r, RE::PlayerCharacter* player,
                                    std::vector<bool>& perkOwned, std::vector<bool>& toolOk)
    {
        perkOwned.reserve(r.reqPerks.size());
        for (const auto& pr : r.reqPerks) {
            bool owned = false;
            if (player && pr.formId)
                if (auto* perk = RE::TESForm::GetFormByID(pr.formId); perk && perk->Is(RE::ENUM_FORM_ID::kPERK))
                    owned = player->GetPerkRank(static_cast<RE::BGSPerk*>(perk)) > 0;
            perkOwned.push_back(owned);
        }
        toolOk.reserve(r.toolGroups.size());
        for (const auto& g : r.toolGroups) {
            bool ok = false;
            for (const auto& opt : g.options) {
                if (!opt.formId) continue;
                auto* item = RE::TESForm::GetFormByID(opt.formId);
                if (!item) continue;
                uint32_t c = 0; if (player) player->GetItemCount(c, item, false);
                if (c > 0) { ok = true; break; }
            }
            toolOk.push_back(ok);
        }
    }

    static void EnsureDesc(size_t i)
    {
        if (i >= s_recipes.size() || i >= s_forms.size()) return;
        Recipe& r = s_recipes[i];
        if (r.descLoaded) return;
        r.descLoaded = true;
        auto* cobj = s_forms[i];
        if (!cobj) return;
        auto* created = cobj->GetCreatedItem();
        if (!created) return;
        if (auto* descForm = RE::fallout_cast<RE::TESDescription*>(created)) {
            RE::BSString s;
            descForm->GetDescription(s, created);
            if (const char* d = s.c_str(); d && d[0]) r.desc = d;
        }
    }

    static void ComputeCachePath()
    {
        auto dir = Paths::JsonDataDir();
        dir /= "cobj_recipes.json";
        s_cachePath = dir.string();
    }

    static void AppendRecipeJson(std::string& out, const Recipe& r,
                                  int wbLocked = -1, int schLocked = -1,
                                  const std::vector<uint32_t>* haveCounts = nullptr,
                                  const std::vector<bool>* perkOwned = nullptr,
                                  const std::vector<bool>* toolOk = nullptr,
                                  const std::vector<uint32_t>* storedCounts = nullptr)
    {

        auto appendHex = [&out](uint32_t v) { char b[12]; std::snprintf(b, sizeof(b), "%08X", v); out += b; };
        out += "{\"formId\":\"";    appendHex(r.formId);
        out += "\",\"editorId\":\""; out += EscapeJson(r.editorId);
        out += "\",\"recipeKey\":\""; out += EscapeJson(r.recipeKey);
        out += "\",\"created\":\"";  appendHex(r.createdFormId);
        out += "\",\"createdKey\":\""; out += EscapeJson(r.createdKey);
        out += "\",\"name\":\"";     out += EscapeJson(r.createdName);
        out += "\",\"count\":";      out += std::to_string(static_cast<unsigned>(r.createdCount));
        out += ",\"value\":";        out += std::to_string(r.createdValue);
        out += ",\"benchId\":\"";    appendHex(r.benchKeywordId);
        out += "\",\"bench\":\"";    out += EscapeJson(r.benchKeyword);
        out += "\",\"formType\":\""; out += r.formType;
        out += "\",\"nifPath\":\"";  out += EscapeJson(r.nifPath);
        out += "\",\"matSwap\":\"";  out += EscapeJson(r.matSwap);
        out += "\",\"nifPaths\":[";
        for (size_t ni = 0; ni < r.compositeNifPaths.size(); ++ni) {
            if (ni) out += ',';
            out += '"'; out += EscapeJson(r.compositeNifPaths[ni]); out += '"';
        }
        out += "],\"altNifPaths\":[";
        for (size_t ai = 0; ai < r.altNifPaths.size(); ++ai) {
            if (ai) out += ',';
            out += '"'; out += EscapeJson(r.altNifPaths[ai]); out += '"';
        }
        out += "],\"source\":\"";    out += EscapeJson(r.source);

        out += "\",\"wbBench\":\"";  out += EscapeJson(WorkbenchUpgrades::BenchNameForCategory(r.category));
        out += "\",\"wbLvl\":";      out += std::to_string(WorkbenchUpgrades::RequiredLevel(r.category, r.compScore));
        out += ",\"components\":[";

        for (size_t i = 0; i < r.components.size(); ++i) {
            const auto& c = r.components[i];
            if (i > 0) out += ',';
            out += "{\"formId\":\"";
            appendHex(c.formId);
            out += "\",\"name\":\"";
            out += EscapeJson(c.name);
            out += "\",\"count\":";
            out += std::to_string(c.count);
            if (haveCounts && i < haveCounts->size() && storedCounts && i < storedCounts->size()) {
                out += ",\"have\":";
                out += std::to_string((*haveCounts)[i]);
                out += ",\"stored\":";
                out += std::to_string((*storedCounts)[i]);
            } else if (haveCounts && i < haveCounts->size()) {
                out += ",\"have\":";
                out += std::to_string((*haveCounts)[i]);
            }
            out += '}';
        }
        out += "]";

        out += ",\"workshop\":";
        out += (r.isWorkshop ? "true" : "false");
        out += ",\"menuPath\":\"";
        out += EscapeJson(r.menuPath);
        out += "\"";
        out += ",\"filters\":[";
        for (size_t i = 0; i < r.filters.size(); ++i) {
            if (i > 0) out += ',';
            out += '"';
            out += EscapeJson(r.filters[i]);
            out += '"';
        }
        out += "],\"tools\":[";
        for (size_t i = 0; i < r.tools.size(); ++i) {
            if (i > 0) out += ',';
            out += '"';
            out += EscapeJson(r.tools[i]);
            out += '"';
        }
        out += "]";

        out += ",\"perks\":[";
        for (size_t i = 0; i < r.reqPerks.size(); ++i) {
            if (i > 0) out += ',';
            out += "{\"name\":\""; out += EscapeJson(r.reqPerks[i].name); out += "\"";
            if (perkOwned && i < perkOwned->size()) { out += ",\"owned\":"; out += ((*perkOwned)[i] ? "true" : "false"); }
            out += "}";
        }
        out += "]";

        out += ",\"toolGroups\":[";
        for (size_t i = 0; i < r.toolGroups.size(); ++i) {
            if (i > 0) out += ',';
            out += "{\"label\":\""; out += EscapeJson(ToolGroupLabel(r.toolGroups[i])); out += "\"";
            if (toolOk && i < toolOk->size()) { out += ",\"ok\":"; out += ((*toolOk)[i] ? "true" : "false"); }
            out += "}";
        }
        out += "]";

        out += ",\"desc\":\"";
        out += EscapeJson(r.desc);
        out += "\"";

        if (wbLocked >= 0 || schLocked >= 0) {
            bool wb  = wbLocked  == 1;
            bool sch = schLocked == 1;
            out += ",\"locked\":";   out += ((wb || sch) ? "true" : "false");
            out += ",\"wbLocked\":"; out += (wb  ? "true" : "false");
            out += ",\"schLocked\":";out += (sch ? "true" : "false");
        }
        out += "}";
    }

    static bool ExportEnabled()
    {
        static int cached = -1;
        if (cached < 0) {
            CSimpleIniA ini; ini.SetUnicode();
            const auto p = (Paths::F4SEUserDir() / "PrismaCrafting.ini").string();
            ini.LoadFile(p.c_str());
            cached = ini.GetBoolValue("Author", "ExportConfig", false) ? 1 : 0;
            logger::info("CobjCache: [Author]ExportConfig = {}", cached ? "on" : "off");
        }
        return cached == 1;
    }

    static void WriteScopedFiles(const std::vector<size_t>& idxs)
    {
        if (s_cachePath.empty()) ComputeCachePath();

        std::string cache;
        cache.reserve(idxs.size() * 1300 + 64);
        cache = "{\"count\":" + std::to_string(idxs.size()) + ",\"recipes\":[";
        for (size_t k = 0; k < idxs.size(); ++k) { if (k) cache += ','; AppendRecipeJson(cache, s_recipes[idxs[k]]); }
        cache += "]}";

        std::string skel;
        skel.reserve(idxs.size() * 180 + 1024);
        skel += "{\n  \"_README\": \"";
        skel += EscapeJson(
            "Recipes at the bench you have open (settlement-workshop buildables excluded), in the override "
            "schema. Copy entries you want to change into  Documents\\My Games\\Fallout4\\F4SE\\PrismaCraftingRecipes.json  "
            "and edit ids/counts. Keys are the createdKey; to target one recipe use its _formId. Underscore "
            "fields are ignored. See docs/Prisma/PrismaCrafting/CRAFTING_CONFIG.md in the dev workspace.");
        skel += "\"";
        {
            std::set<std::string> emitted;
            char hx[16], cbuf[24];
            for (size_t i : idxs) {
                const Recipe& r = s_recipes[i];
                std::snprintf(hx, sizeof(hx), "%08X", r.formId);
                std::string key = !r.createdKey.empty() ? r.createdKey : std::string(hx);
                if (!emitted.insert(key).second) continue;
                skel += ",\n  \""; skel += EscapeJson(key);
                skel += "\": { \"_name\": \""; skel += EscapeJson(r.createdName.empty() ? r.editorId : r.createdName);
                skel += "\", \"_formId\": \""; skel += hx;
                skel += "\", \"components\": [";
                bool fc = true;
                for (const auto& c : r.components) {
                    std::string id = c.form ? CreatedKey(c.form) : std::string();
                    if (id.empty()) { char chx[16]; std::snprintf(chx, sizeof(chx), "%08X", c.formId); id = chx; }
                    if (!fc) skel += ", ";
                    fc = false;
                    skel += "{ \"id\": \""; skel += EscapeJson(id);
                    skel += "\", \"count\": "; std::snprintf(cbuf, sizeof(cbuf), "%u", c.count); skel += cbuf;
                    if (!c.name.empty()) { skel += ", \"_name\": \""; skel += EscapeJson(c.name); skel += "\""; }
                    skel += " }";
                }
                skel += "] }";
            }
        }
        skel += "\n}\n";

        std::string cat;
        cat.reserve(8192);
        cat += "{\n  \"_README\": \"Pickable forms for CraftingConfigEditor.html (components, perks, "
               "keywords referenced by recipes). id = Plugin.esp|localHex.\"";
        bool first = true;
        auto item = [&](const std::string& id, const std::string& nm) {
            if (id.empty()) return;
            if (!first) cat += ","; first = false;
            cat += "{\"id\":\""; cat += EscapeJson(id);
            cat += "\",\"name\":\""; cat += EscapeJson(nm.empty() ? id : nm); cat += "\"}";
        };
        cat += ",\n  \"components\": [";
        first = true;
        { std::set<std::string> seen;
          for (size_t i : idxs) for (const auto& c : s_recipes[i].components) {
              std::string id = c.form ? CreatedKey(c.form) : std::string();
              if (id.empty() || !seen.insert(id).second) continue;
              item(id, c.name);
          } }
        cat += "]";
        cat += ",\n  \"perks\": [";
        first = true;
        { std::set<uint32_t> seen;
          for (size_t i : idxs) for (const auto& pr : s_recipes[i].reqPerks) {
              if (!pr.formId || !seen.insert(pr.formId).second) continue;
              auto* f = RE::TESForm::GetFormByID(pr.formId);
              item(f ? CreatedKey(f) : std::string(), pr.name);
          } }
        cat += "]";
        cat += ",\n  \"globals\": []";
        cat += ",\n  \"keywords\": [";
        first = true;
        { std::set<uint32_t> seen;
          auto addKw = [&](uint32_t fid, const std::string& nm) {
              if (!fid || !seen.insert(fid).second) return;
              auto* f = RE::TESForm::GetFormByID(fid);
              item(f ? CreatedKey(f) : std::string(), (nm.empty() && f) ? GetName(f) : nm);
          };
          for (size_t i : idxs) {
              addKw(s_recipes[i].benchKeywordId, s_recipes[i].benchKeyword);
              for (const auto& g : s_recipes[i].toolGroups)
                  for (const auto& o : g.options) addKw(o.formId, o.name);
          } }
        cat += "]";
        cat += "\n}\n";

        const auto dir = Paths::JsonDataDir();
        std::string pCache = s_cachePath;
        std::string pSkel  = (dir / "PrismaCraftingRecipes.skeleton.json").string();
        std::string pCat   = (dir / "CraftingCatalog.json").string();
        logger::info("CobjCache: exporting {} recipe(s) to editor data files (async write)", idxs.size());
        std::thread([c = std::move(cache), s = std::move(skel), k = std::move(cat),
                     pCache = std::move(pCache), pSkel = std::move(pSkel), pCat = std::move(pCat)]() {
            auto wr = [](const std::string& p, const std::string& j) {
                std::ofstream f(p, std::ios::binary | std::ios::trunc);
                if (f) f.write(j.data(), static_cast<std::streamsize>(j.size()));
            };
            wr(pCache, c); wr(pSkel, s); wr(pCat, k);
        }).detach();
    }

    static std::set<std::string> s_exportedBenches;

    void ExportCurrentView(const std::vector<uint32_t>& benchIds)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (!s_built || benchIds.empty() || !ExportEnabled()) return;

        std::vector<uint32_t> sorted(benchIds);
        std::sort(sorted.begin(), sorted.end());
        std::string key;
        for (auto id : sorted) { char b[12]; std::snprintf(b, sizeof(b), "%08X,", id); key += b; }
        if (!s_exportedBenches.insert(key).second) {
            logger::debug("ExportCurrentView: bench [{}] already exported this session, skipping", key);
            return;
        }

        std::vector<size_t> idxs;
        for (size_t i = 0; i < s_recipes.size(); ++i) {
            const Recipe& r = s_recipes[i];
            if (r.isWorkshop) continue;
            if (std::find(benchIds.begin(), benchIds.end(), r.benchKeywordId) != benchIds.end())
                idxs.push_back(i);
        }
        logger::debug("ExportCurrentView: bench [{}] -> {} non-workshop recipe(s)", key, idxs.size());
        if (!idxs.empty()) WriteScopedFiles(idxs);
    }

    static uint32_t CountAcross(RE::TESForm* item, RE::PlayerCharacter* player,
                                const std::vector<RE::TESObjectREFR*>& sources);
    static std::uint32_t AvailableAcross(RE::TESForm* form, RE::PlayerCharacter* player,
                                         const std::vector<RE::TESObjectREFR*>& sources);

    static bool CmpOp(double a, const std::string& op, double b)
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

    static uint32_t CountItemsWithKeyword(RE::BGSKeyword* kw, RE::PlayerCharacter* player,
                                          const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!kw) return 0;
        uint32_t total = 0;
        auto scan = [&](RE::TESObjectREFR* ref) {
            if (!ref || !ref->inventoryList) return;
            for (auto& item : ref->inventoryList->data) {
                auto* obj = item.object;
                if (!obj) continue;
                if (auto* kwf = obj->As<RE::BGSKeywordForm>(); kwf && kwf->HasKeyword(kw))
                    total += item.GetCount();
            }
        };
        scan(player);
        for (auto* ref : sources) scan(ref);
        return total;
    }

    static bool ConfigGatePasses(const Recipe& r, RE::PlayerCharacter* player,
                                 const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!player) return false;
        for (const auto& cd : r.configConds) {
            RE::TESForm* form = CraftingConfig::ResolveForm(cd.id);
            if (!form) {
                logger::debug("ConfigGate[{}]: condition {} '{}' UNRESOLVED -> locked",
                    r.editorId.empty() ? r.createdKey : r.editorId, cd.type, cd.id);
                return false;
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
            if (!ok) return false;
        }
        return true;
    }

    static bool RecipeConditionsPass(const Recipe& r, RE::PlayerCharacter* player,
                                     const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!player) return false;
        if (r.configGated) return ConfigGatePasses(r, player, sources);
        const auto it = s_byFormId.find(r.formId);
        if (it == s_byFormId.end() || it->second >= s_forms.size() || !s_forms[it->second]) return false;
        return s_forms[it->second]->PlayerPassesConditions();
    }

    struct LockFlags { bool wb = false; bool sch = false; bool any() const { return wb || sch; } };

    static LockFlags ComputeLocksCheap(const Recipe& r, RE::PlayerCharacter* player,
                                       const std::vector<RE::TESObjectREFR*>& sources)
    {
        LockFlags lf;
        lf.wb = !WorkbenchUpgrades::IsRecipeCraftable(r.category, r.compScore);

        if (r.configGated) lf.sch = !ConfigGatePasses(r, player, sources);
        return lf;
    }

    static LockFlags ComputeLocksExact(const Recipe& r, RE::PlayerCharacter* player,
                                       const std::vector<RE::TESObjectREFR*>& sources)
    {
        LockFlags lf;
        lf.wb = !WorkbenchUpgrades::IsRecipeCraftable(r.category, r.compScore);
        lf.sch = !RecipeConditionsPass(r, player, sources);
        return lf;
    }

    static bool IsLockedCheap(const Recipe& r, RE::PlayerCharacter* player,
                              const std::vector<RE::TESObjectREFR*>& sources)
    {
        return ComputeLocksCheap(r, player, sources).any();
    }

    static void ApplyConfig()
    {
        CraftingConfig::Load();
        if (CraftingConfig::Empty()) return;

        int compOv = 0, condOv = 0, skipped = 0;
        for (auto& r : s_recipes) {
            const auto* ov = CraftingConfig::Find(r.createdKey, r.editorId, r.formId);
            if (!ov) continue;

            if (ov->hasComponents && ov->componentsValid) {
                std::vector<Component> rebuilt;
                rebuilt.reserve(ov->components.size());
                bool resolved = true;
                for (const auto& cs : ov->components) {
                    auto* form = CraftingConfig::ResolveForm(cs.id);
                    if (!form) {
                        logger::warn("CraftingConfig: recipe '{}' component '{}' unresolved -- keeping vanilla costs",
                            r.editorId.empty() ? r.createdKey : r.editorId, cs.id);
                        ++skipped;
                        resolved = false;
                        break;
                    }
                    Component c;
                    c.form   = form;
                    c.formId = form->GetFormID();
                    c.name   = GetName(form);
                    c.count  = cs.count;
                    rebuilt.push_back(std::move(c));
                }
                if (resolved) {
                    r.components = std::move(rebuilt);
                    ++compOv;

                    r.compScore = 0;
                    for (const auto& c : r.components)
                        r.compScore += static_cast<int32_t>(c.count) * CompRarityWeight(LowerStr(c.name));
                    if (r.createdValue > 0) {
                        const int topCutoff = WorkbenchUpgrades::TopTierCutoff(r.category);
                        if (topCutoff > 0) {
                            const int32_t valueBonus = r.createdValue / 5;
                            r.compScore += (std::min)(valueBonus, static_cast<int32_t>(topCutoff * 8 / 10));
                        }
                    }
                    logger::debug("ApplyConfig: '{}' components overridden -> {} item(s), compScore -> {}",
                        r.editorId.empty() ? r.createdKey : r.editorId, r.components.size(), r.compScore);
                }
            }

            if (ov->hasConditions && ov->conditionsValid) {
                r.configGated = true;
                r.configConds = ov->conditions;
                logger::debug("ApplyConfig: '{}' conditions overridden -> {} gate(s)",
                    r.editorId.empty() ? r.createdKey : r.editorId, ov->conditions.size());

                r.reqPerks.clear();
                r.toolGroups.clear();
                r.tools.clear();
                for (const auto& cd : ov->conditions) {
                    if (cd.type != "perk") continue;
                    auto* form = CraftingConfig::ResolveForm(cd.id);
                    if (!form) continue;
                    PerkReq pr; pr.formId = form->GetFormID(); pr.name = GetName(form);
                    r.reqPerks.push_back(std::move(pr));
                }

                ++condOv;
            }
        }
        logger::info("CraftingConfig: applied components to {} recipe(s), conditions to {} ({} unresolved component(s))",
            compOv, condOv, skipped);
    }

    void Build()
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (s_built) { logger::debug("CobjCache::Build: already built this session, skipping"); return; }
        if (s_cachePath.empty()) ComputeCachePath();

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("CobjCache: TESDataHandler null");
            return;
        }
        logger::debug("CobjCache::Build: enumerating constructible objects...");

        auto& arr = dh->GetFormArray<RE::BGSConstructibleObject>();
        if (arr.empty()) {

            logger::warn("CobjCache::Build: COBJ array empty (data not ready) — will retry on bench open");
            return;
        }
        s_recipes.clear();
        s_forms.clear();
        s_byFormId.clear();
        s_recipes.reserve(arr.size());
        s_forms.reserve(arr.size());

        auto containsCheat = [](const char* s) -> bool {
            if (!s || !s[0]) return false;
            std::string t(s);
            for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return t.find("cheat") != std::string::npos;
        };

        for (auto* cobj : arr) {
            if (!cobj) continue;
            if (containsCheat(cobj->GetFormEditorID())) continue;
            if (auto* cr = cobj->GetCreatedItem(); cr && containsCheat(cr->GetFormEditorID())) continue;

            Recipe r;
            r.formId = cobj->GetFormID();
            r.recipeKey = CreatedKey(cobj);
            r.source = SourceFile(cobj);
            if (const char* eid = cobj->GetFormEditorID(); eid && eid[0]) r.editorId = eid;

            if (auto* createdRaw = cobj->GetCreatedItem()) {

                r.createdFormId = createdRaw->GetFormID();
                RE::TESForm* created = DisplayForm(createdRaw);
                r.createdKey    = CreatedKey(created);
                r.createdName   = GetName(created);

                if      (created->Is(RE::ENUM_FORM_ID::kWEAP)) r.formType = "WEAP";
                else if (created->Is(RE::ENUM_FORM_ID::kARMO)) r.formType = "ARMO";
                else if (created->Is(RE::ENUM_FORM_ID::kALCH)) r.formType = "ALCH";
                else if (created->Is(RE::ENUM_FORM_ID::kINGR)) r.formType = "INGR";
                else if (created->Is(RE::ENUM_FORM_ID::kAMMO)) r.formType = "AMMO";
                else if (created->Is(RE::ENUM_FORM_ID::kBOOK)) r.formType = "BOOK";
                else if (created->Is(RE::ENUM_FORM_ID::kOMOD)) r.formType = "OMOD";
                else r.formType = "MISC";

                if (r.formType == "OMOD") r.isMod = true;

                if (r.formType == "WEAP") {
                    r.subCategory = WeaponSubcat(created);
                } else if (r.formType == "ARMO") {
                    if (auto* armoSub = RE::fallout_cast<RE::TESObjectARMO*>(created))
                        r.subCategory = ArmorSubcat(armoSub);
                }

                if (!r.isMod && r.formType == "MISC") {
                    RE::BGSKeywordForm* kwf = created->As<RE::BGSKeywordForm>();
                    if (kwf && kwf->keywords) {
                        for (std::uint32_t ki = 0; ki < kwf->numKeywords; ++ki) {
                            RE::BGSKeyword* kw = kwf->keywords[ki];
                            if (!kw) continue;
                            const char* keid = kw->GetFormEditorID();
                            if (!keid || !keid[0]) continue;
                            std::string lo;
                            for (const char* p = keid; *p; ++p)
                                lo += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
                            if (lo.find("loosemod") != std::string::npos) {
                                r.isMod = true;
                                break;
                            }
                        }
                    }
                }

                if (auto* model = RE::fallout_cast<RE::TESModel*>(created)) {
                    if (const char* nif = model->GetModel(); nif && nif[0])
                        r.nifPath = nif;
                }

                if (auto* armo = RE::fallout_cast<RE::TESObjectARMO*>(created)) {
                    std::vector<std::string> armorNifs;
                    std::set<std::string> seenArmorNifs;
                    auto addArmorNif = [&](const char* nif) {
                        if (!(nif && nif[0])) return;
                        std::string path(nif), key(path);
                        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                            return static_cast<char>(std::tolower(c == '\\' ? '/' : c));
                        });
                        if (seenArmorNifs.insert(std::move(key)).second) armorNifs.push_back(std::move(path));
                    };

                    std::set<RE::TESObjectARMO*> seenTemplates;
                    for (auto* source = armo; source && seenTemplates.insert(source).second;) {
                        for (auto& aa : source->modelArray) {
                            auto* arma = aa.armorAddon;
                            if (!arma) continue;
                            const char* nif = arma->bipedModel[0].GetModel();
                            if (!(nif && nif[0])) nif = arma->bipedModel[1].GetModel();
                            addArmorNif(nif);
                        }
                        source = source->armorTemplate;
                    }

                    const char* worldNif = armo->worldModel[0].GetModel();
                    if (!(worldNif && worldNif[0])) worldNif = armo->worldModel[1].GetModel();
                    if (worldNif && worldNif[0]) {
                        r.nifPath = worldNif;

                        r.compositeNifPaths.clear();

                        r.altNifPaths = std::move(armorNifs);
                    } else if (!armorNifs.empty()) {
                        if (r.nifPath.empty()) r.nifPath = armorNifs.front();
                        if (armorNifs.size() > 1) r.compositeNifPaths = std::move(armorNifs);
                    }
                }

                {
                    auto norm = [](const char* s) {
                        std::string o;
                        for (const char* p = s; p && *p; ++p) {
                            char c = (*p == '\\') ? '/' : *p;
                            o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        }
                        if (o.rfind("materials/", 0) == 0) o.erase(0, 10);
                        return o;
                    };
                    auto harvest = [&](RE::BGSMaterialSwap* mswp) {
                        if (!mswp) return;
                        for (auto& kv : mswp->swapMap) {
                            std::string orig = norm(kv.first.c_str());
                            std::string repl = norm(kv.second.swapMaterial.c_str());
                            if (orig.empty() || repl.empty()) continue;
                            if (!r.matSwap.empty()) r.matSwap += ';';
                            r.matSwap += orig; r.matSwap += '|'; r.matSwap += repl;
                        }
                    };

                    if (auto* mms = RE::fallout_cast<RE::BGSModelMaterialSwap*>(created))
                        harvest(mms->swapForm);

                    if (auto* armoSwap = created->As<RE::TESObjectARMO>()) {
                        const char* maleWorld = armoSwap->worldModel[0].GetModel();
                        harvest((maleWorld && maleWorld[0]) ? armoSwap->worldModel[0].swapForm
                                                            : armoSwap->worldModel[1].swapForm);
                    }

                    RE::BSTArray<RE::BGSMaterialSwap*>* baked = nullptr;
                    if (auto* armo = created->As<RE::TESObjectARMO>()) baked = armo->armorData.materialSwaps;
                    else if (auto* weap = created->As<RE::TESObjectWEAP>()) baked = weap->weaponData.materialSwaps;
                    if (baked) for (auto* mswp : *baked) harvest(mswp);

                    RE::TESObjectWEAP* weapForComposite = created->As<RE::TESObjectWEAP>();
                    if (weapForComposite && !r.nifPath.empty()) r.compositeNifPaths.push_back(r.nifPath);
                    {

                        std::function<void(RE::BGSMod::Attachment::Mod*, int)> scanOmod =
                            [&](RE::BGSMod::Attachment::Mod* omod, int depth) {
                                if (!omod || depth > 3) return;
                                harvest(omod->swapForm);
                                RE::BGSMod::Attachment::Mod::Data od{};
                                omod->GetData(od);
                                for (std::uint32_t j = 0; od.propertyMods && j < od.propertyModCount; ++j) {
                                    auto& pm = od.propertyMods[j];
                                    if (pm.type == RE::BGSMod::Property::TYPE::kForm && pm.data.form)
                                        harvest(pm.data.form->As<RE::BGSMaterialSwap>());
                                }
                                if (!weapForComposite) return;
                                if (const char* pieceNif = omod->GetModel(); pieceNif && pieceNif[0]) {
                                    r.compositeNifPaths.push_back(pieceNif);
                                    return;
                                }
                                const std::size_t before = r.compositeNifPaths.size();
                                for (std::uint32_t j = 0; od.attachments && j < od.attachmentCount; ++j) {
                                    scanOmod(od.attachments[j].mod, depth + 1);
                                    if (r.compositeNifPaths.size() > before) break;
                                }
                            };

                        auto scanContainer = [&](RE::BGSMod::Container* cont) {
                            if (!cont) return;
                            RE::BGSMod::Container::Data data{};
                            cont->GetData(&data);

                            for (std::uint32_t i = 0; data.propertyMods && i < data.propertyModCount; ++i) {
                                auto& pm = data.propertyMods[i];
                                if (pm.type == RE::BGSMod::Property::TYPE::kForm && pm.data.form)
                                    harvest(pm.data.form->As<RE::BGSMaterialSwap>());
                            }
                            for (std::uint32_t i = 0; data.attachments && i < data.attachmentCount; ++i)
                                scanOmod(data.attachments[i].mod, 0);
                        };
                        if (auto* tmpl = created->GetObjectTemplate()) {
                            for (auto* item : tmpl->items) {
                                if (!item || !item->isDefault) continue;
                                scanContainer(static_cast<RE::BGSMod::Container*>(item));
                            }
                        }

                    if (weapForComposite && r.compositeNifPaths.size() < 2) {

                        int guard = 0;
                        for (auto* base = weapForComposite->weaponTemplate;
                             base && guard++ < 4 && r.compositeNifPaths.size() < 2;
                             base = base->weaponTemplate) {
                            if (auto* bt = base->GetObjectTemplate()) {
                                for (auto* item : bt->items) {
                                    if (!item || !item->isDefault) continue;
                                    scanContainer(static_cast<RE::BGSMod::Container*>(item));
                                }
                            }
                        }

                        if (r.compositeNifPaths.size() < 2) {
                            if (auto* tmpl2 = created->GetObjectTemplate()) {
                                for (auto* item : tmpl2->items) {
                                    if (!item) continue;
                                    scanContainer(static_cast<RE::BGSMod::Container*>(item));
                                    break;
                                }
                            }
                        }
                        if (r.compositeNifPaths.size() < 2)
                            logger::debug("CobjCache: weapon '{}' has no default OMOD pieces — preview will be "
                                          "the bare receiver '{}'", r.createdName, r.nifPath);
                    }
                    }

                    if (r.compositeNifPaths.size() < 2) r.compositeNifPaths.clear();
                }

                if (auto* valForm = RE::fallout_cast<RE::TESValueForm*>(created)) {
                    r.createdValue = valForm->GetFormValue();
                }

                r.desc.clear();
            }

            if (!r.isMod && r.createdName.empty())
                r.isMod = true;

            r.createdCount = cobj->data.numConstructed ? cobj->data.numConstructed : 1;

            if (cobj->benchKeyword) {
                r.benchKeywordId = cobj->benchKeyword->GetFormID();
                if (const char* keid = cobj->benchKeyword->GetFormEditorID(); keid && keid[0])
                    r.benchKeyword = keid;
            }

            CollectPerkReqs(cobj->conditions, r.reqPerks);
            CollectToolReqs(cobj->conditions, r.toolGroups);
            for (const auto& g : r.toolGroups) r.tools.push_back(ToolGroupLabel(g));

            r.workshopPriority = cobj->data.workshopPriority;
            for (uint32_t fi = 0; fi < cobj->filterKeywords.size; ++fi) {
                auto* fkw = RE::detail::BGSKeywordGetTypedKeywordByIndex(
                    RE::KeywordType::kRecipeFilter, cobj->filterKeywords.array[fi].keywordIndex);
                const char* feid = fkw ? fkw->GetFormEditorID() : nullptr;
                if (feid && feid[0]) {
                    if (std::strstr(feid, "WorkshopRecipeFilter") == feid) {
                        r.isWorkshop = true;

                        std::string path = MenuPathFromFilter(feid);
                        if (!path.empty()) {
                            auto levels = [](const std::string& p) {
                                size_t n = 0, pos = 0;
                                while ((pos = p.find(" > ", pos)) != std::string::npos) { ++n; pos += 3; }
                                return n;
                            };
                            if (r.menuPath.empty() ||
                                levels(path) > levels(r.menuPath) ||
                                (levels(path) == levels(r.menuPath) && path.size() > r.menuPath.size()))
                                r.menuPath = path;
                        }
                    }
                    r.filters.push_back(FriendlyFilter(feid));

                    if (!r.isWorkshop && r.recipeCategory.empty()) {
                        std::string disp = GetName(fkw);
                        if (disp.empty() || disp == feid) disp = FriendlyFilter(feid);
                        for (auto& ch : disp) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                        r.recipeCategory = disp;
                    }
                }
            }

            if (!r.benchKeyword.empty()) {
                std::string be;
                for (char c : r.benchKeyword) be += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (be.find("workshop") != std::string::npos) r.isWorkshop = true;
                const bool modBench =
                    be.find("weapon") != std::string::npos ||
                    be.find("armor")  != std::string::npos ||
                    be.find("robot")  != std::string::npos;

                if (modBench && (r.formType == "MISC" || r.formType == "OMOD"))
                    r.isMod = true;
            }

            if (cobj->requiredItems) {
                for (const auto& pair : *cobj->requiredItems) {
                    Component c;
                    if (pair.first) {
                        c.formId = pair.first->GetFormID();
                        c.name   = GetName(pair.first);
                        c.form   = pair.first;
                        if (const char* ceid = pair.first->GetFormEditorID(); ceid && ceid[0])
                            CraftingConfig::RegisterEditorId(ceid, pair.first);
                    }
                    c.count = pair.second.i;
                    r.components.push_back(std::move(c));
                }
            }

            r.compScore = 0;
            for (const auto& c : r.components)
                r.compScore += static_cast<int32_t>(c.count) * CompRarityWeight(LowerStr(c.name));

            r.nameKey  = LowerStr(r.createdName + " " + r.editorId);

            r.sortKey = LowerStr(CleanToolName(r.createdName));
            if (r.sortKey.empty()) r.sortKey = LowerStr(CleanToolName(r.editorId));
            if (r.sortKey.empty()) r.sortKey = r.nameKey;
            r.category = Categorize(r.formType, r.nameKey);

            if (r.createdValue > 0) {
                const int topCutoff = WorkbenchUpgrades::TopTierCutoff(r.category);
                if (topCutoff > 0) {
                    const int32_t valueBonus = r.createdValue / 5;
                    r.compScore += (std::min)(valueBonus, static_cast<int32_t>(topCutoff * 8 / 10));
                }
            }

            uint32_t fid = r.formId;
            s_byFormId.emplace(fid, s_recipes.size());
            s_recipes.push_back(std::move(r));
            s_forms.push_back(cobj);
        }

        ApplyConfig();

        s_built = true;
        size_t modCount = 0;
        for (const auto& r : s_recipes) { if (r.isMod) ++modCount; }
        logger::info("CobjCache: indexed {} constructible objects ({} hidden mods)",
            s_recipes.size(), modCount);

    }

    bool IsBuilt()
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        return s_built;
    }

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
                    int32_t c = item.GetCount();
                    if (c > 0) viaWalk += static_cast<std::uint32_t>(c);
                }
            }
        }

        logger::info("PlayerCaps: GetItemCount={} inventoryWalk={}", viaItem, viaWalk);
        if (viaItem) return viaItem;
        return viaWalk;
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

    static uint32_t CountAcross(RE::TESForm* item, RE::PlayerCharacter* player,
                               const std::vector<RE::TESObjectREFR*>& sources)
    {
        uint32_t total = 0, c = 0;
        if (player) { c = 0; player->GetItemCount(c, item, false); total += c; }
        for (auto* ref : sources) {
            if (!ref) continue;
            c = 0; ref->GetItemCount(c, item, false); total += c;
        }
        return total;
    }

    static std::uint32_t LiteralStack(RE::TESObjectREFR* cont, RE::TESBoundObject* obj)
    {
        if (!cont || !obj || !cont->inventoryList) return 0;
        std::uint32_t total = 0;
        for (auto& it : cont->inventoryList->data)
            if (it.object == obj) { auto c = it.GetCount(); if (c > 0) total += static_cast<std::uint32_t>(c); }
        return total;
    }

    bool ConsumeComponents(const std::vector<std::pair<RE::TESForm*, std::uint32_t>>& costs,
                           RE::PlayerCharacter* player,
                           const std::vector<RE::TESObjectREFR*>& sources,
                           bool dryRun,
                           bool includePlayer)
    {
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
            if (!form || count == 0) continue;
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
        std::map<ObjectKey, std::uint32_t> reserved;

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
                    const auto take = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                        need.remaining, static_cast<std::uint64_t>(available - used)));
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
            std::map<RE::TESBoundObject*, std::uint32_t> stacks;
            for (auto& item : cont->inventoryList->data) {
                if (!item.object) continue;
                const auto count = item.GetCount();
                if (count > 0) stacks[item.object] += static_cast<std::uint32_t>(count);
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
                for (const auto& entry : *misc->componentData) {
                    if (!entry.first || entry.second.i <= 0) continue;
                    const auto it = needIndex.find(entry.first);
                    if (it == needIndex.end()) continue;
                    bundle.yields[it->second] += static_cast<std::uint32_t>(entry.second.i);
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

            for (const auto& entry : *candidate.misc->componentData) {
                if (!entry.first || entry.second.i <= 0) continue;
                const auto produced = static_cast<std::uint64_t>(selected) *
                                      static_cast<std::uint32_t>(entry.second.i);
                std::uint64_t consumed = 0;
                if (const auto it = needIndex.find(entry.first); it != needIndex.end()) {
                    auto& remaining = allocationRemaining[it->second];
                    consumed = std::min(remaining, produced);
                    remaining -= consumed;
                }
                const auto left = produced - consumed;
                auto* component = entry.first->As<RE::BGSComponent>();
                auto* loose = component ? component->scrapItem : nullptr;
                if (left > 0 && loose) surplus[{ candidate.cont, loose }] += left;
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

    static std::uint32_t AvailableAcross(RE::TESForm* form, RE::PlayerCharacter* player,
                                         const std::vector<RE::TESObjectREFR*>& sources)
    {
        if (!form) return 0;
        std::vector<RE::TESObjectREFR*> conts;
        for (auto* s : sources) if (s) conts.push_back(s);
        if (player) conts.push_back(player);

        RE::BGSComponent*   cmpo     = form->Is(RE::ENUM_FORM_ID::kCMPO)
                                        ? static_cast<RE::BGSComponent*>(form) : nullptr;
        RE::TESBoundObject* cmpoObj  = form->As<RE::TESBoundObject>();
        RE::TESObjectMISC*  scrapObj = cmpo ? cmpo->scrapItem : nullptr;

        std::uint64_t total = 0;
        for (auto* cont : conts) {
            if (!cont) continue;
            if (cmpoObj) total += LiteralStack(cont, cmpoObj);
            if (scrapObj && scrapObj != cmpoObj)
                total += LiteralStack(cont, static_cast<RE::TESBoundObject*>(scrapObj));
            if (!cmpo || !cont->inventoryList) continue;

            std::map<RE::TESBoundObject*, std::uint32_t> stacks;
            for (auto& it : cont->inventoryList->data) {
                if (!it.object) continue;
                auto c = it.GetCount();
                if (c > 0) stacks[it.object] += static_cast<std::uint32_t>(c);
            }
            for (auto& [obj, cnt] : stacks) {
                auto* misc = obj->As<RE::TESObjectMISC>();
                if (!misc || !misc->componentData) continue;
                if (misc == scrapObj) continue;
                for (auto& entry : *misc->componentData) {
                    if (entry.first == form) {
                        total += static_cast<std::uint64_t>(cnt) * entry.second.i;
                        break;
                    }
                }
            }
        }
        return static_cast<std::uint32_t>((std::min)(total, static_cast<std::uint64_t>(0xFFFFFFFFu)));
    }

    static std::vector<std::pair<RE::TESForm*, std::uint32_t>> CostsOf(const Recipe& r)
    {
        std::vector<std::pair<RE::TESForm*, std::uint32_t>> costs;
        costs.reserve(r.components.size());
        for (const auto& c : r.components)
            if (c.form && c.count) costs.emplace_back(c.form, c.count);
        return costs;
    }

    const Recipe* FindWorkshopRecipeByFormId(std::uint32_t cobjFormId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (!s_built || !cobjFormId) return nullptr;
        const auto it = s_byFormId.find(cobjFormId);
        if (it == s_byFormId.end() || it->second >= s_recipes.size()) return nullptr;
        const auto& recipe = s_recipes[it->second];
        return recipe.isWorkshop ? &recipe : nullptr;
    }

    const Recipe* FindWorkshopRecipeByCreated(std::uint32_t createdFormId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (!s_built || !createdFormId) return nullptr;
        const Recipe* match = nullptr;
        for (const auto& recipe : s_recipes) {
            if (!recipe.isWorkshop || recipe.createdFormId != createdFormId) continue;
            if (match) {
                logger::warn("FindWorkshopRecipeByCreated: created form {:08X} is ambiguous (COBJ {:08X} and {:08X})",
                    createdFormId, match->formId, recipe.formId);
                return nullptr;
            }
            match = &recipe;
        }
        return match;
    }

    bool WorkshopRecipeAvailable(const Recipe& recipe,
                                 const std::vector<RE::TESObjectREFR*>& sources)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        const auto it = s_byFormId.find(recipe.formId);
        if (it == s_byFormId.end() || it->second >= s_forms.size()) return false;
        auto* cobj = s_forms[it->second];
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
        if (!s_built || keywordFormId == 0) return false;
        for (const auto& r : s_recipes) {
            if (r.benchKeywordId == keywordFormId) return true;
        }
        return false;
    }

    int CraftRecipe(uint32_t cobjFormId, bool bypassGates,
                    const std::vector<RE::TESObjectREFR*>& sources, int times)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (!s_built) return 0;
        if (times < 1) times = 1;

        auto idxIt = s_byFormId.find(cobjFormId);
        if (idxIt == s_byFormId.end()) {
            logger::warn("CraftRecipe: COBJ {:08X} not found", cobjFormId);
            return 0;
        }
        const size_t idx = idxIt->second;
        RE::BGSConstructibleObject* cobj = s_forms[idx];
        const Recipe& rec = s_recipes[idx];
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
                    if (!component.form || component.count == 0) continue;
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

    std::vector<Recipe> All()
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        return s_recipes;
    }

    const std::string& CachePath() { return s_cachePath; }

    std::string GetRecipeDesc(uint32_t cobjFormId)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        auto it = s_byFormId.find(cobjFormId);
        if (it == s_byFormId.end()) return "";
        EnsureDesc(it->second);
        return s_recipes[it->second].desc;
    }

    uint32_t FirstWorkshopBuildableCreatedId()
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);

        for (const auto& r : s_recipes) {
            if (r.isWorkshop && r.createdFormId && !r.nifPath.empty())
                return r.createdFormId;
        }
        return 0;
    }

    void ApplyWorkshopCategories(const std::unordered_map<std::uint32_t, std::string>& categories)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (categories.empty()) return;
        std::size_t applied = 0;
        for (auto& r : s_recipes) {
            if (!r.isWorkshop) continue;
            auto it = categories.find(r.formId);
            if (it == categories.end() || it->second.empty()) continue;
            r.menuPath = it->second;

            const auto sep = r.menuPath.find(" > ");
            r.filters.assign(1, sep == std::string::npos ? r.menuPath : r.menuPath.substr(0, sep));
            ++applied;
        }
        logger::info("CobjCache: applied vanilla workshop categories to {} of {} buildable(s)",
                     applied, categories.size());
    }

    std::string WorkshopBuildablesJson(const std::vector<RE::TESObjectREFR*>& sources)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::string json = "[";
        bool first = true;
        unsigned n = 0;

        std::unordered_set<std::uint32_t> activeRecipes;
        if (REX::FModule::IsRuntimeOG()) {
            REL::Relocation<RE::Workshop::WorkshopMenuNode*> root{ REL::Offset{ 0x58E0850 } };
            auto collect = [&](auto&& self, const RE::Workshop::WorkshopMenuNode& node) -> void {
                if (node.recipe) {
                    activeRecipes.insert(node.recipe->GetFormID());
                }
                for (const auto& child : node.children)
                    if (child) self(self, *child);
            };
            if (root.get()) collect(collect, *root.get());
        }

        std::unordered_map<RE::TESForm*, uint32_t> storedCache, carriedCache;
        auto countStored = [&](RE::TESForm* item) -> uint32_t {
            auto it = storedCache.find(item);
            if (it != storedCache.end()) return it->second;
            uint32_t n = AvailableAcross(item, nullptr, sources);
            storedCache.emplace(item, n);
            return n;
        };
        auto countCarried = [&](RE::TESForm* item) -> uint32_t {
            auto it = carriedCache.find(item);
            if (it != carriedCache.end()) return it->second;
            uint32_t n = AvailableAcross(item, player, {});
            carriedCache.emplace(item, n);
            return n;
        };
        auto countCached = [&](RE::TESForm* item) -> uint32_t {
            return countStored(item) + countCarried(item);
        };

        for (size_t i = 0; i < s_recipes.size(); ++i) {
            const Recipe& r = s_recipes[i];
            if (!r.isWorkshop) continue;
            if (!activeRecipes.empty() && !activeRecipes.contains(r.formId)) continue;

            bool locked = false;
            std::vector<uint32_t> haveCounts;
            std::vector<uint32_t> storedCounts;
            std::vector<bool>     perkOwned;
            std::vector<bool>     toolOk;
            if (i < s_forms.size() && s_forms[i]) {

                locked = !RecipeConditionsPass(r, player, sources);
                if (player) {

                    haveCounts.reserve(r.components.size());
                    storedCounts.reserve(r.components.size());
                    for (const auto& c : r.components) {
                        haveCounts.push_back(c.form ? countCached(c.form) : 0);
                        storedCounts.push_back(c.form ? countStored(c.form) : 0);
                    }
                }

                ComputeReqOwnership(r, player, perkOwned, toolOk);
            }

            if (!first) json += ',';
            first = false;
            AppendRecipeJson(json, r, locked ? 1 : 0, locked ? 1 : 0,
                             haveCounts.empty() ? nullptr : &haveCounts,
                             perkOwned.empty() ? nullptr : &perkOwned,
                             toolOk.empty()    ? nullptr : &toolOk,
                             storedCounts.empty() ? nullptr : &storedCounts);
            ++n;
        }
        json += "]";
        logger::info("CobjCache: {} workshop buildables", n);
        return json;
    }

    constexpr uint32_t kWorkbenchCookingKeyword = 0x00102152;

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

    std::string QueryRecipesPage(const std::vector<uint32_t>& wantIds, const std::string& menuKind,
                                 const std::string& search, const std::string& category,
                                 const std::string& sub,
                                 int lockFilter, int sort, int page, int pageSize,
                                 const std::vector<RE::TESObjectREFR*>& sources, bool cookingBench)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        if (!s_built || pageSize <= 0)
            return "{\"total\":0,\"pageCount\":0,\"page\":0,\"categories\":[],\"recipes\":[]}";
        WorkbenchUpgrades::Poll();
        auto* player = RE::PlayerCharacter::GetSingleton();
        (void)menuKind; (void)wantIds;

        struct Cand { size_t idx; bool locked; };
        std::unordered_map<std::string, size_t> seen;
        std::vector<Cand> cands;
        cands.reserve(4096);
        for (size_t i = 0; i < s_recipes.size(); ++i) {
            const Recipe& r = s_recipes[i];
            if (r.isWorkshop) continue;

            if (r.isMod) continue;
            const bool isCookable = (r.benchKeywordId == kWorkbenchCookingKeyword);
            if (cookingBench != isCookable) continue;
            if (!search.empty() && r.nameKey.find(search) == std::string::npos) continue;

            bool locked = IsLockedCheap(r, player, sources);
            if (lockFilter == 1 && locked) continue;
            if (lockFilter == 2 && !locked) continue;
            if (lockFilter == 3) {
                if (locked) continue;

                if (!RecipeConditionsPass(r, player, sources)) continue;
                if (!ConsumeComponents(CostsOf(r), player, sources, true)) continue;
            }

            const std::string& key = !r.createdKey.empty() ? r.createdKey : r.editorId;
            if (key.empty()) { cands.push_back({ i, locked }); continue; }
            auto it = seen.find(key);
            if (it == seen.end()) { seen.emplace(key, cands.size()); cands.push_back({ i, locked }); continue; }

            Cand& incumbent = cands[it->second];
            const bool incLocked  = ComputeLocksExact(s_recipes[incumbent.idx], player, sources).any();
            const bool chalLocked = ComputeLocksExact(r, player, sources).any();
            if (incLocked != chalLocked) {
                if (incLocked) incumbent = { i, locked };
            } else if (!incLocked && player) {

                if (!ConsumeComponents(CostsOf(s_recipes[incumbent.idx]), player, sources, true) &&
                     ConsumeComponents(CostsOf(r), player, sources, true))
                    incumbent = { i, locked };
            }
        }

        auto catOf = [&](const Recipe& r) -> const std::string& {
            if (cookingBench && !r.recipeCategory.empty()) return r.recipeCategory;
            return r.category;
        };
        std::map<std::string, int> catCounts;
        std::map<std::string, std::map<std::string, int>> subCounts;
        for (const auto& c : cands) {
            const Recipe& r = s_recipes[c.idx];
            catCounts[catOf(r)]++;
            if (auto s = SubOf(r); !s.empty()) subCounts[catOf(r)][s]++;
        }

        std::vector<size_t> finalIdx;
        finalIdx.reserve(cands.size());
        const bool allCats = category.empty() || category == "ALL";
        const bool allSubs = sub.empty() || sub == "ALL";
        for (const auto& c : cands) {
            const Recipe& r = s_recipes[c.idx];
            if (!allCats && catOf(r) != category) continue;
            if (!allSubs && SubOf(r) != sub) continue;
            finalIdx.push_back(c.idx);
        }

        std::sort(finalIdx.begin(), finalIdx.end(), [&](size_t a, size_t b) {
            const Recipe& ra = s_recipes[a]; const Recipe& rb = s_recipes[b];
            if (sort == 1 && ra.createdValue != rb.createdValue) return ra.createdValue > rb.createdValue;
            if (ra.sortKey != rb.sortKey) return ra.sortKey < rb.sortKey;
            return ra.nameKey < rb.nameKey;
        });

        const int total = static_cast<int>(finalIdx.size());
        const int pageCount = (total + pageSize - 1) / pageSize;
        if (page < 0) page = 0;
        if (pageCount > 0 && page >= pageCount) page = pageCount - 1;
        const size_t start = static_cast<size_t>(page) * pageSize;
        const size_t end = (std::min)(finalIdx.size(), start + static_cast<size_t>(pageSize));

        std::unordered_map<RE::TESForm*, uint32_t> haveCache;
        auto countCached = [&](RE::TESForm* item) -> uint32_t {
            auto it = haveCache.find(item);
            if (it != haveCache.end()) return it->second;
            uint32_t have = AvailableAcross(item, player, sources);
            haveCache.emplace(item, have);
            return have;
        };

        std::string json = "{\"total\":" + std::to_string(total) +
            ",\"pageCount\":" + std::to_string(pageCount) +
            ",\"page\":" + std::to_string(page) + ",\"categories\":[";
        bool f = true;
        for (const auto& [cat, cnt] : catCounts) {
            if (!f) json += ',';
            f = false;
            json += "{\"cat\":\""; json += EscapeJson(cat);
            json += "\",\"count\":"; json += std::to_string(cnt);
            json += ",\"subs\":[";
            if (auto sit = subCounts.find(cat); sit != subCounts.end()) {
                bool fs = true;
                for (const auto& [sname, scnt] : sit->second) {
                    if (!fs) json += ',';
                    fs = false;
                    json += "{\"sub\":\""; json += EscapeJson(sname);
                    json += "\",\"count\":"; json += std::to_string(scnt); json += "}";
                }
            }
            json += "]}";
        }
        json += "],\"recipes\":[";
        f = true;
        for (size_t k = start; k < end; ++k) {
            const Recipe& r = s_recipes[finalIdx[k]];
            std::vector<uint32_t> haveCounts;
            std::vector<bool>     perkOwned, toolOk;
            if (player) {
                haveCounts.reserve(r.components.size());
                for (const auto& c : r.components) haveCounts.push_back(c.form ? countCached(c.form) : 0);
            }
            ComputeReqOwnership(r, player, perkOwned, toolOk);
            const auto lf = ComputeLocksExact(r, player, sources);
            if (!f) json += ',';
            f = false;
            AppendRecipeJson(json, r, lf.wb ? 1 : 0, lf.sch ? 1 : 0,
                             haveCounts.empty() ? nullptr : &haveCounts,
                             perkOwned.empty() ? nullptr : &perkOwned,
                             toolOk.empty()    ? nullptr : &toolOk);
        }
        json += "]}";

        logger::info("QueryRecipesPage: bench={} cat='{}' search='{}' lock={} sort={} -> total={} page {}/{}",
            wantIds.size(), category, search, lockFilter, sort, total, page, pageCount);
        return json;
    }

    std::string FilterJsonForBench(const std::vector<uint32_t>& wantIds,
                                   const std::vector<RE::TESObjectREFR*>& sources)
    {
        std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
        auto* player = RE::PlayerCharacter::GetSingleton();

        std::unordered_map<RE::TESForm*, uint32_t> haveCache;
        auto countCached = [&](RE::TESForm* item) -> uint32_t {
            auto it = haveCache.find(item);
            if (it != haveCache.end()) return it->second;
            uint32_t have = AvailableAcross(item, player, sources);
            haveCache.emplace(item, have);
            return have;
        };

        std::string json = "[";
        bool first = true;
        unsigned shown = 0, skippedWorkshop = 0;
        for (size_t i = 0; i < s_recipes.size(); ++i) {
            const Recipe& r = s_recipes[i];

            if (r.isWorkshop) { ++skippedWorkshop; continue; }

            if (!wantIds.empty() &&
                std::find(wantIds.begin(), wantIds.end(), r.benchKeywordId) == wantIds.end())
                continue;

            bool schLocked = false;
            std::vector<uint32_t> haveCounts;

            if (i < s_forms.size() && s_forms[i]) {
                schLocked = !RecipeConditionsPass(r, player, sources);

                if (player) {

                    haveCounts.reserve(r.components.size());
                    for (const auto& c : r.components)
                        haveCounts.push_back(c.form ? countCached(c.form) : 0);
                }
            }
            const bool wbLocked = !WorkbenchUpgrades::IsRecipeCraftable(r.category, r.compScore);

            if (!first) json += ',';
            first = false;
            AppendRecipeJson(json, r, wbLocked ? 1 : 0, schLocked ? 1 : 0,
                haveCounts.empty() ? nullptr : &haveCounts);
            ++shown;
        }
        json += "]";
        logger::info("FilterJsonForBench: wantIds={} -> {} recipe(s) shown ({} workshop buildables skipped)",
            wantIds.size(), shown, skippedWorkshop);
        return json;
    }
}

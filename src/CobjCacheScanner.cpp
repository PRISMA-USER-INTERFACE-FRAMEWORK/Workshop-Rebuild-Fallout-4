#include "CobjCacheInternal.h"
#include "WorkbenchUpgrades.h"
#include "SaturatingAdd.h"
#include <RE/B/BGSConstructibleObject.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BGSListForm.h>
#include <RE/B/BGSPerk.h>
#include <RE/B/BGSTypedKeywordValue.h>
#include <RE/C/CONDITIONITEMOBJECT.h>
#include <RE/K/KeywordType.h>
#include <RE/T/TESConditionItem.h>
#include <RE/T/TESModel.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESValueForm.h>
#include <RE/S/SCRIPT_OUTPUT.h>
#include <RE/RTTI.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <utility>

namespace CobjCache
{
static RE::TESForm* DisplayForm(RE::TESForm* created)
    {
        if (auto* list = created ? created->As<RE::BGSListForm>() : nullptr) {
            for (auto* entry : list->arrayOfForms)
                if (entry) return entry;
        }
        return created;
    }

static std::string LowerStr(std::string s)
    {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
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

static std::int32_t ComputeRecipeScore(const Recipe& r)
    {
        std::uint64_t score = 0;
        for (const auto& c : r.components)
            score = crafting::SaturateAddU64(score,
                static_cast<std::uint64_t>(c.count) * CompRarityWeight(LowerStr(c.name)));
        if (r.createdValue > 0) {
            const int topCutoff = WorkbenchUpgrades::TopTierCutoff(r.category);
            if (topCutoff > 0)
                score = crafting::SaturateAddU64(score, (std::min)(
                    static_cast<std::int64_t>(r.createdValue / 5),
                    static_cast<std::int64_t>(topCutoff) * 8 / 10));
        }
        return static_cast<std::int32_t>((std::min)(score,
            static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)())));
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

std::string FriendlyFilter(const std::string& eid)
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
        static constexpr const char* ops[] = { "==", "!=", ">", ">=", "<", "<=" };
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            static bool warned = false;
            if (!warned) { warned = true; logger::warn("CobjCacheScanner: no player during requirement scan; skill/perk requirements left unclassified"); }
            return;
        }
        bool prevOr = false;
        for (auto* item = cond.head; item; item = item->next) {
            const bool inOrRun = prevOr || item->data.compareOr;
            prevOr = item->data.compareOr;
            if (item->data.functionData.function.underlying() !=
                static_cast<uint16_t>(RE::SCRIPT_OUTPUT::kScript_HasPerk))
                continue;
            if (inOrRun || item->data.valueIsGlobal || item->data.aliasParams || item->data.packDataParams ||
                item->data.swapsSubjectAndTarget) continue;
            auto* perk = static_cast<RE::BGSPerk*>(item->data.functionData.param[0]);
            if (!perk) continue;
            const auto object = item->data.object.get();
            if (object == RE::CONDITIONITEMOBJECT::kRef) {
                if (const auto ref = item->data.runOnRef.get(); player && ref && ref.get() != player) continue;
            } else if (object > RE::CONDITIONITEMOBJECT::kTarget) {
                continue;
            }
            // Constraint: HasPerk outputs only 0.0 or 1.0 (verified); both are evaluated below.
            if (item->data.condition > 5) continue;
            const std::string cmp = ops[item->data.condition];
            const float operand = item->data.value;
            const bool whenNone = CmpOp(0.0, cmp, operand);
            const bool whenOne = CmpOp(1.0, cmp, operand);
            if (whenNone && whenOne) continue;
            std::string op;
            float value = 1.0f;
            if (whenNone) {
                op = "==";
                value = 0.0f;
            } else if (!whenOne) {
                op = "<";
                value = 0.0f;
            }
            uint32_t id = perk->GetFormID();
            if (std::any_of(out.begin(), out.end(), [&](const PerkReq& p) { return p.formRef == id; }))
                continue;
            std::string nm = GetName(perk);
            out.push_back(PerkReq{ id, nm.empty() ? "Perk" : nm, op, value });
        }
    }

static void CollectToolReqs(const RE::TESCondition& cond, std::vector<ToolGroup>& out)
    {
        static constexpr const char* ops[] = { "==", "!=", ">", ">=", "<", "<=" };
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            static bool warned = false;
            if (!warned) { warned = true; logger::warn("CobjCacheScanner: no player during requirement scan; tool requirements left unclassified"); }
            return;
        }
        std::vector<std::vector<ToolOption>> groups;
        std::vector<ToolOption> group;
        std::set<uint32_t>      groupSeen;
        bool unknownGroup = false;
        auto flush = [&]() {
            if (!group.empty() && !unknownGroup) groups.push_back(group);
            group.clear(); groupSeen.clear();
            unknownGroup = false;
        };
        for (auto* item = cond.head; item; item = item->next) {
            const uint16_t fn = item->data.functionData.function.underlying();
            auto* form = static_cast<RE::TESForm*>(item->data.functionData.param[0]);

            // Constraint: a GetItemCount condition is a tool badge only when the counted form is actually a tool.
            bool known = fn == static_cast<uint16_t>(RE::SCRIPT_OUTPUT::kScript_GetItemCount) && form &&
                         IsToolForm(form) &&
                         !item->data.valueIsGlobal && !item->data.aliasParams && !item->data.packDataParams &&
                         !item->data.swapsSubjectAndTarget && item->data.condition <= 5;
            if (known) {
                const auto object = item->data.object.get();
                if (object == RE::CONDITIONITEMOBJECT::kRef) {
                    if (const auto ref = item->data.runOnRef.get(); player && ref && ref.get() != player) known = false;
                } else if (object > RE::CONDITIONITEMOBJECT::kTarget) {
                    known = false;
                }
            }
            if (known) {
                const std::string cmp = ops[item->data.condition];
                known = !CmpOp(0.0, cmp, item->data.value) && CmpOp(1.0, cmp, item->data.value);
            }
            if (!known) {
                unknownGroup = true;
            } else {
                std::string label = CleanToolName(GetName(form));
                if (label.empty()) label = "Tool";
                if (groupSeen.insert(form->GetFormID()).second)
                    group.push_back(ToolOption{ form->GetFormID(), std::move(label) });
            }
            if (!item->data.compareOr) flush();
        }
        flush();

        auto optIds = [](const std::vector<ToolOption>& g) {
            std::set<uint32_t> s; for (auto& o : g) s.insert(o.formRef); return s;
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

    static void ApplyConfig(std::vector<Recipe>& recipes)
    {
        CraftingConfig::Load();
        if (CraftingConfig::Empty()) return;

        int compOv = 0, condOv = 0, skipped = 0;
        for (auto& r : recipes) {
            const auto* ov = CraftingConfig::Find(r.createdKey, r.editorId, r.formRef);
            if (!ov) continue;

            if (ov->hasComponents && ov->componentsValid) {
                std::vector<Component> rebuilt;
                rebuilt.reserve(ov->components.size());
                std::set<RE::TESForm*> forms;
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
                    if (!forms.insert(form).second) {
                        logger::warn("CraftingConfig: recipe '{}' component '{}' duplicates another component -- keeping vanilla costs",
                            r.editorId.empty() ? r.createdKey : r.editorId, cs.id);
                        ++skipped;
                        resolved = false;
                        break;
                    }
                    Component c;
                    c.form   = form;
                    c.formRef = form->GetFormID();
                    c.name   = GetName(form);
                    c.count  = cs.count;
                    rebuilt.push_back(std::move(c));
                }
                if (resolved) {
                    r.components = std::move(rebuilt);
                    ++compOv;

                    r.compScore = ComputeRecipeScore(r);
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
                    PerkReq pr; pr.formRef = form->GetFormID(); pr.name = GetName(form);
                    pr.op = cd.op; pr.value = cd.value;
                    r.reqPerks.push_back(std::move(pr));
                }

                ++condOv;
            }
        }
        logger::info("CraftingConfig: applied components to {} recipe(s), conditions to {} ({} unresolved component(s))",
            compOv, condOv, skipped);
    }

    RecipeScan ScanRecipes(std::span<RE::BGSConstructibleObject* const> objects)
    {
        CraftingConfig::Load();
        RecipeScan scan;
        scan.playerMissing = RE::PlayerCharacter::GetSingleton() == nullptr;
        scan.recipes.reserve(objects.size());
        scan.forms.reserve(objects.size());

        auto containsCheat = [](const char* s) -> bool {
            if (!s || !s[0]) return false;
            std::string t(s);
            for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return t.find("cheat") != std::string::npos;
        };

        for (auto* cobj : objects) {
            if (!cobj) continue;
            if (containsCheat(cobj->GetFormEditorID())) continue;
            if (auto* cr = cobj->GetCreatedItem(); cr && containsCheat(cr->GetFormEditorID())) continue;

            Recipe r;
            r.formRef = cobj->GetFormID();
            r.recipeKey = CreatedKey(cobj);
            r.source = SourceFile(cobj);
            if (const char* eid = cobj->GetFormEditorID(); eid && eid[0]) r.editorId = eid;

            auto* createdRaw = cobj->GetCreatedItem();
            auto* created = DisplayForm(createdRaw);
            if (createdRaw) {

                r.createdFormId = createdRaw->GetFormID();
                r.createdKey    = CreatedKey(created);
                r.createdName   = GetName(created);

                CollectResourceValues(createdRaw, r.resources, scan.resourceEntries, scan.resourceMatches);

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
                    auto harvest = [&](RE::BGSMaterialSwap* mswp) {
                        CollectMaterialSwap(mswp, r.matSwap);
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

                                RE::BGSMod::Container::Data od{};
                                static_cast<RE::BGSMod::Container*>(omod)->GetData(&od);
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
                        c.formRef = pair.first->GetFormID();
                        c.name   = GetName(pair.first);
                        c.form   = pair.first;
                        if (const char* ceid = pair.first->GetFormEditorID(); ceid && ceid[0])
                            CraftingConfig::RegisterEditorId(ceid, pair.first);
                    }
                    c.count = pair.second.i;
                    r.components.push_back(std::move(c));
                }
            }

            r.nameKey  = LowerStr(r.createdName + " " + r.editorId);

            r.sortKey = LowerStr(CleanToolName(r.createdName));
            if (r.sortKey.empty()) r.sortKey = LowerStr(CleanToolName(r.editorId));
            if (r.sortKey.empty()) r.sortKey = r.nameKey;
            r.category = Categorize(r.formType, r.nameKey, r.subCategory, ConsumableCategory(created), MappedCategory(r, created));
            std::replace(r.nameKey.begin(), r.nameKey.end(), '_', ' ');

            r.compScore = ComputeRecipeScore(r);

            if (r.isWorkshop) {
                if (auto* list = cobj->GetCreatedItem() ? cobj->GetCreatedItem()->As<RE::BGSListForm>() : nullptr) {
                    r.workshopList = true;
                    for (auto* member : list->arrayOfForms) {
                        if (!member || !member->As<RE::TESBoundObject>()) continue;
                        if (std::find(r.workshopVariants.begin(), r.workshopVariants.end(), member) == r.workshopVariants.end())
                            r.workshopVariants.push_back(member);
                    }
                    if (r.workshopVariants.empty()) r.workshopList = false;
                }
            }

            const size_t idx = scan.recipes.size();
            scan.byFormId.emplace(r.formRef, idx);
            if (r.benchKeywordId) scan.benchKeywordIds.insert(r.benchKeywordId);
            if (r.isWorkshop && r.createdFormId) {

                if (!scan.workshopByCreated.emplace(r.createdFormId, idx).second &&
                    scan.ambiguousWorkshopCreated.insert(r.createdFormId).second) {
                    const size_t first = scan.workshopByCreated[r.createdFormId];
                    logger::warn("CobjCache: created form {:08X} is ambiguous (COBJ {:08X} and {:08X}) "
                                 "-- workshop lookup will return null for it",
                                 r.createdFormId, scan.recipes[first].formRef, r.formRef);
                }
            }
            scan.recipes.push_back(std::move(r));
            scan.forms.push_back(cobj);
        }

        ApplyConfig(scan.recipes);
        return scan;
    }
}

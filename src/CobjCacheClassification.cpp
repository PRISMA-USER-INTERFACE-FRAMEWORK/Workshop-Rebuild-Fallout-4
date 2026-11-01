#include "CobjCacheInternal.h"
#include "ConfigValidation.h"
#include <RE/A/AlchemyItem.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BGSKeyword.h>
#include <RE/RTTI.h>

namespace CobjCache
{
    // Constraint: source:src/CobjCacheScanner.cpp at 57f2d5c; retain the established metadata interfaces.
    bool HasAny(const std::string& hay, std::initializer_list<const char*> needles)
    {
        for (auto* n : needles) if (hay.find(n) != std::string::npos) return true;
        return false;
    }

    std::string ConsumableCategory(RE::TESForm* created)
    {
        auto* alch = created ? created->As<RE::AlchemyItem>() : nullptr;
        if (!alch) return {};
        if (alch->IsMedicine() || alch->HasKeywordString("ObjectTypeStimpak")) return "HEALING";
        if (alch->HasKeywordString("ObjectTypeChem")) return "DRUGS";
        if (alch->HasKeywordString("ObjectTypeWater") ||
            alch->HasKeywordString("ObjectTypeNukaCola") ||
            alch->HasKeywordString("ObjectTypeAlcohol")) return "DRINKS";
        return {};
    }

    std::string Categorize(const std::string& formType, const std::string& nameLower,
                              const std::string& subCategory, const std::string& consumableCategory,
                              const std::string& mappedCategory)
    {
        if (formType == "WEAP" && !subCategory.empty()) {
            if (subCategory == "Grenades") return "GRENADES";
            if (subCategory == "Mines") return "MINES";
            if (subCategory == "One-Handed Melee" || subCategory == "Two-Handed Melee" ||
                subCategory == "Unarmed") return "MELEE";
            return "WEAPONS";
        }
        if (formType == "ARMO" && subCategory == "Head") return "HEADGEAR";
        if (formType == "AMMO") return "AMMO";
        if (formType == "BOOK") return "HOLOTAPES";
        if ((formType == "ALCH" || formType == "INGR") && !consumableCategory.empty()) return consumableCategory;
        if (CraftingConfig::Validation::IsCategoryCompatible(mappedCategory, formType)) return mappedCategory;
        if (formType == "WEAP") {
            if (HasAny(nameLower, {"grenade","molotov"})) return "GRENADES";
            if (HasAny(nameLower, {"mine","caltrop","trap"})) return "MINES";
            if (HasAny(nameLower, {"knife","blade","sword","machete"," bat","knuckle","fist","axe","hammer","ripper","melee","spear","baton","club","gauntlet"})) return "MELEE";
            return "WEAPONS";
        }
        if (formType == "ALCH" || formType == "INGR") {
            if (HasAny(nameLower, {"stim","medkit","med-x","med x","heal","bandage","gauze","splint","tourniquet","antibiotic","anodyne","cure","first aid","trauma","rad-away","radaway","rad-x","radx","addictol","antivenom","hydra","quikclot","ibuprofen","injector","iodide","painkiller","morphine","antiseptic","disinfect","salve","ointment","aspirin","penicillin","medic","pharmaceutical"})) return "HEALING";
            if (HasAny(nameLower, {"jet","psycho","buffout","mentat","daytripper","day tripper","calmex","overdrive","fury","ultra jet","x-cell","stealth boy","skeeto","bufftats","chem","pax","berry"})) return "DRUGS";
            if (HasAny(nameLower, {"water","nuka","beer","wine","vodka","whiskey","rum","liquor","beverage","drink","juice","coffee","tea","milk","cola","moonshine","bourbon","buzzbites"})) return "DRINKS";
            return "FOOD";
        }
        if (HasAny(nameLower, {"backpack","duffle","rucksack","satchel","bandolier","mailbag","shoulder bag","pouch","carrying device"})) return "BACKPACKS";
        if (HasAny(nameLower, {"gas mask","gasmask","respirator","goggle","eyepatch","visor","night vision"})) return "HEADGEAR";
        if (formType == "ARMO") {
            if (HasAny(nameLower, {"mask","hood","helm","hat","goggle","visor","respirator","glasses","eyepatch","scarf","bandana"})) return "HEADGEAR";
            return "ARMOR";
        }
        return "UTILITY";
    }

    std::string MappedCategory(const Recipe& recipe, RE::TESForm* created)
    {
        const auto rules = CraftingConfig::CategoryRules();
        if (rules.empty()) return {};
        const char* createdId = created ? created->GetFormEditorID() : nullptr;
        for (const auto& rule : rules) {
            if (!rule.keyword && CraftingConfig::Validation::IsCategoryCompatible(rule.category, recipe.formType) &&
                (rule.id == recipe.editorId || (createdId && rule.id == createdId))) return rule.category;
        }
        auto* keywords = created ? created->As<RE::BGSKeywordForm>() : nullptr;
        if (!keywords || !keywords->keywords) return {};
        for (const auto& rule : rules) {
            if (!rule.keyword || !CraftingConfig::Validation::IsCategoryCompatible(rule.category, recipe.formType)) continue;
            for (std::uint32_t i = 0; i < keywords->numKeywords; ++i) {
                auto* keyword = keywords->keywords[i];
                const char* id = keyword ? keyword->GetFormEditorID() : nullptr;
                if (id && rule.id == id) return rule.category;
            }
        }
        return {};
    }
}

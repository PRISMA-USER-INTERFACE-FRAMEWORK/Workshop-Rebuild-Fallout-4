#include "CobjCacheInternal.h"
#include "ConfigValidation.h"
#include <RE/A/AlchemyItem.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BGSKeyword.h>
#include <RE/RTTI.h>
#include <cctype>
#include <string_view>

namespace CobjCache
{
    bool HasAny(const std::string& hay, std::initializer_list<const char*> needles)
    {
        for (auto* n : needles) if (hay.find(n) != std::string::npos) return true;
        return false;
    }

    bool HasWord(const std::string& hay, std::initializer_list<const char*> needles)
    {
        const auto letterAt = [&](std::size_t i) { return i < hay.size() && std::isalpha(static_cast<unsigned char>(hay[i])) != 0; };
        for (auto* n : needles) {
            const std::string_view needle{n};
            for (auto pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + 1)) {
                const auto end = pos + needle.size();
                if ((pos == 0 || !letterAt(pos - 1)) &&
                    (!letterAt(end) || (hay.compare(end, 1, "s") == 0 && !letterAt(end + 1)) ||
                     (hay.compare(end, 2, "es") == 0 && !letterAt(end + 2)))) return true;
            }
        }
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
            if (HasWord(nameLower, {"stimpak","medkit","med-x","med x","heal","healing","bandage","gauze","splint","tourniquet","antibiotic","anodyne","cure","first aid","trauma","rad-away","radaway","rad-x","radx","addictol","antivenom","hydra","quikclot","ibuprofen","injector","iodide","painkiller","morphine","antiseptic","disinfect","disinfectant","salve","ointment","aspirin","penicillin","medic","medicine","medical","pharmaceutical"})) return "HEALING";
            if (HasWord(nameLower, {"jet","psycho","psychojet","psychobuff","buffjet","buffout","mentat","daytripper","day tripper","calmex","overdrive","fury","ultra jet","x-cell","stealth boy","skeeto","bufftats","chem","pax","berry"})) return "DRUGS";
            if (HasWord(nameLower, {"water","nuka","nukashine","beer","wine","vodka","whiskey","rum","liquor","beverage","drink","juice","coffee","tea","milk","cola","moonshine","bourbon","buzzbites"})) return "DRINKS";
            return "FOOD";
        }
        if (HasAny(nameLower, {"backpack","duffle","rucksack","satchel","bandolier","mailbag","shoulder bag","pouch","carrying device"})) return "BACKPACKS";
        if (HasAny(nameLower, {"gas mask","gasmask","respirator","goggle","eyepatch","visor","night vision"})) return "HEADGEAR";
        if (formType == "ARMO") {
            if (HasWord(nameLower, {"mask","hood","hoodie","helm","helmet","hat","goggle","visor","respirator","glasses","sunglasses","hardhat","eyepatch","scarf","bandana"})) return "HEADGEAR";
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

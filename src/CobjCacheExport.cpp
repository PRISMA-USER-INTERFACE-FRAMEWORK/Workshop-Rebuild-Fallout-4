// Constraint: source:src/CobjCache.h:Recipe; serializers must use owned recipe values and keep form pointers borrowed.
#include "CobjCacheInternal.h"

#include "Paths.h"
#include "SimpleIni.h"
#include "DefaultIni.h"

#include <RE/T/TESForm.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

// Constraint: source:src/CobjCache.cpp at d29b1aa; preserve the extracted export ordering.
namespace CobjCache
{
    static std::mutex s_exportLock;
    static std::atomic<std::uint64_t> s_exportGeneration{ 0 };

    static bool ExportEnabled()
    {
        static int cached = -1;
        if (cached < 0) {
            CSimpleIniA ini; ini.SetUnicode();
            const auto p = Paths::F4SEUserDir() / "PrismaCrafting.ini";
            ini.LoadFile(p.c_str());
            cached = ini.GetBoolValue("Author", "ExportConfig", false) ? 1 : 0;
            logger::info("CobjCache: [Author]ExportConfig = {}", cached ? "on" : "off");
        }
        return cached == 1;
    }

    static bool WriteExportFile(const std::filesystem::path& path, const std::string& data)
    {
        const auto temp = crafting::ini::MakeTempPath(path);
        std::error_code ec;
        std::ofstream out(temp, std::ios::binary | std::ios::noreplace);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        out.close();
        if (out) {
            std::filesystem::rename(temp, path, ec);
            if (!ec) return true;
        }
        std::filesystem::remove(temp, ec);
        return false;
    }

    static void WriteScopedFiles(const std::vector<Recipe>& recipes, std::uint64_t gen)
    {
        std::string cache;
        {
            Json payload = {{"count", recipes.size()}, {"recipes", Json::array()}};
            for (const auto& recipe : recipes) payload["recipes"].push_back(RecipeJson(recipe));
            cache = payload.dump(-1, ' ', false, Json::error_handler_t::replace);
        }

        std::string skel;
        {
            Json payload = {{"_README",
                "Recipes at the bench you have open (settlement-workshop buildables excluded), in the override "
                "schema. Copy entries you want to change into  Documents\\My Games\\Fallout4\\F4SE\\PrismaCraftingRecipes.json  "
                "and edit ids/counts. Keys are the createdKey; to target one recipe use its _formId. Underscore "
                "fields are ignored. See docs/Prisma/PrismaCrafting/CRAFTING_CONFIG.md in the dev workspace."}};
            std::set<std::string> emitted;
            char hx[16];
            for (const auto& r : recipes) {
                std::snprintf(hx, sizeof(hx), "%08X", r.formRef);
                std::string key = !r.createdKey.empty() ? r.createdKey : std::string(hx);
                if (!emitted.insert(key).second) continue;
                Json row = {{"_name", r.createdName.empty() ? r.editorId : r.createdName},
                            {"_formId", hx}, {"components", Json::array()}};
                for (const auto& c : r.components) {
                    std::string id = c.form ? CreatedKey(c.form) : std::string();
                    if (id.empty()) { char chx[16]; std::snprintf(chx, sizeof(chx), "%08X", c.formRef); id = chx; }
                    Json component = {{"id", id}, {"count", c.count}};
                    if (!c.name.empty()) component["_name"] = c.name;
                    row["components"].push_back(std::move(component));
                }
                payload[key] = std::move(row);
            }
            skel = payload.dump(2, ' ', false, Json::error_handler_t::replace) + '\n';
        }

        std::string cat;
        {
            Json payload = {{"_README", "Pickable forms for CraftingConfigEditor.html (components, perks, "
                                       "keywords referenced by recipes). id = Plugin.esp|localHex."},
                            {"components", Json::array()}, {"perks", Json::array()},
                            {"globals", Json::array()}, {"keywords", Json::array()}};
            const auto item = [](Json& target, const std::string& id, const std::string& nm) {
                if (!id.empty()) target.push_back({{"id", id}, {"name", nm.empty() ? id : nm}});
            };
            { std::set<std::string> seen;
              for (const auto& recipe : recipes) for (const auto& c : recipe.components) {
                  std::string id = c.form ? CreatedKey(c.form) : std::string();
                  if (id.empty() || !seen.insert(id).second) continue;
                  item(payload["components"], id, c.name);
              } }
            { std::set<uint32_t> seen;
              for (const auto& recipe : recipes) for (const auto& pr : recipe.reqPerks) {
                  if (!pr.formRef || !seen.insert(pr.formRef).second) continue;
                  auto* f = RE::TESForm::GetFormByID(pr.formRef);
                  item(payload["perks"], f ? CreatedKey(f) : std::string(), pr.name);
              } }
            { std::set<uint32_t> seen;
              auto addKw = [&](uint32_t fid, const std::string& nm) {
                  if (!fid || !seen.insert(fid).second) return;
                  auto* f = RE::TESForm::GetFormByID(fid);
                  item(payload["keywords"], f ? CreatedKey(f) : std::string(), (nm.empty() && f) ? GetName(f) : nm);
              };
              for (const auto& recipe : recipes) {
                  addKw(recipe.benchKeywordId, recipe.benchKeyword);
                  for (const auto& g : recipe.toolGroups)
                      for (const auto& o : g.options) addKw(o.formRef, o.name);
              } }
            cat = payload.dump(2, ' ', false, Json::error_handler_t::replace) + '\n';
        }

        const auto dir = Paths::JsonDataDir();
        auto pCache = dir / "cobj_recipes.json";
        auto pSkel  = dir / "PrismaCraftingRecipes.skeleton.json";
        auto pCat   = dir / "CraftingCatalog.json";
        logger::info("CobjCache: exporting {} recipe(s) to editor data files (async write)", recipes.size());
        std::thread([gen, c = std::move(cache), s = std::move(skel), k = std::move(cat),
                     pCache = std::move(pCache), pSkel = std::move(pSkel), pCat = std::move(pCat)]() {
            std::lock_guard<std::mutex> lock(s_exportLock);
            if (gen != s_exportGeneration.load()) return;

            if (!WriteExportFile(pCache, c)) logger::warn("CobjCache: failed to publish cobj_recipes.json");
            if (!WriteExportFile(pSkel, s)) logger::warn("CobjCache: failed to publish PrismaCraftingRecipes.skeleton.json");
            if (!WriteExportFile(pCat, k)) logger::warn("CobjCache: failed to publish CraftingCatalog.json");
        }).detach();
    }

    static std::set<std::string> s_exportedBenches;

    void ExportCurrentView(const std::vector<uint32_t>& benchIds)
    {
        std::vector<Recipe> recipes;
        std::uint64_t gen = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(s_cacheLock);
            if (!IsBuilt() || benchIds.empty() || !ExportEnabled()) return;
            const auto& index = CurrentIndex();

            std::vector<uint32_t> sorted(benchIds);
            std::sort(sorted.begin(), sorted.end());
            std::string key;
            for (auto id : sorted) { char b[12]; std::snprintf(b, sizeof(b), "%08X,", id); key += b; }
            if (!s_exportedBenches.insert(key).second) {
                logger::debug("ExportCurrentView: bench [{}] already exported this session, skipping", key);
                return;
            }

            for (std::size_t i = 0; i < index.recipes.size(); ++i) {
                const auto& r = index.recipes[i];
                if (r.isWorkshop) continue;
                if (std::find(benchIds.begin(), benchIds.end(), r.benchKeywordId) != benchIds.end())
                    recipes.push_back(RecipeValue(i));
            }
            logger::debug("ExportCurrentView: bench [{}] -> {} non-workshop recipe(s)", key, recipes.size());
            if (recipes.empty()) return;
            gen = s_exportGeneration.fetch_add(1) + 1;
        }
        WriteScopedFiles(recipes, gen);
    }

}

#include "PrismaUI_F4_API.h"
#include "CobjCache.h"
#include "WorkshopInputHook.h"
#include "WorkshopFreedom.h"

#include <psapi.h>
#include "WorkbenchUpgrades.h"
#include "Paths.h"
#include "TurntableRenderer.h"

#include <RE/W/WorkbenchMenuBase.h>
#include <RE/W/Workshop.h>
#include <RE/W/WorkshopMenu.h>
#include <RE/T/TESBoundObject.h>
#include <RE/P/PlayerCharacter.h>
#include <REX/FModule.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESModel.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/B/BGSComponent.h>
#include <RE/U/UI.h>
#include <RE/U/UIMessageQueue.h>
#include <RE/C/ControlMap.h>
#include <RE/S/SendHUDMessage.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/T/TESContainerChangedEvent.h>
#include <RE/I/IMenu.h>
#include <RE/RTTI.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <SimpleIni.h>

#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

static std::string SettingsIniPath()
{
    auto p = Paths::F4SEUserDir();
    p /= "PrismaCrafting.ini";
    return p.string();
}
static std::mutex g_settingsLock;
static int LoadScale(const char* key)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(SettingsIniPath().c_str());
    long v = ini.GetLongValue("UI", key, 100);
    return static_cast<int>(v < 50 ? 50 : (v > 200 ? 200 : v));
}
static void SaveScale(const char* key, int pct)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    if (pct < 50) pct = 50; else if (pct > 200) pct = 200;
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(SettingsIniPath().c_str());
    ini.SetLongValue("UI", key, pct);
    if (ini.SaveFile(SettingsIniPath().c_str()) < 0)
        logger::warn("PrismaCrafting: failed to write {} to {}", key, SettingsIniPath());
}

static std::string LoadTheme()
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(SettingsIniPath().c_str());
    return ini.GetValue("UI", "Theme", "tarkov");
}
static void SaveTheme(const char* name)
{
    std::lock_guard<std::mutex> lock(g_settingsLock);
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(SettingsIniPath().c_str());
    ini.SetValue("UI", "Theme", (name && name[0]) ? name : "tarkov");
    if (ini.SaveFile(SettingsIniPath().c_str()) < 0)
        logger::warn("PrismaCrafting: failed to write Theme to {}", SettingsIniPath());
}

static bool IsCraftMenu(const RE::BSFixedString& name)
{
    return name == "CookingMenu" ||
           name == "ChemistryMenu";
}

static PRISMA_UI_API::IVPrismaUI9* g_api         = nullptr;

static PRISMA_UI_API::IVPrismaUI10* g_api10      = nullptr;

static void MarkViewAsPanel(PrismaView view)
{
    if (g_api10 && view) g_api10->SetViewRole(view, PRISMA_UI_API::ViewRole::kPanel);
}

static std::atomic<PrismaView>     g_view{ 0 };
static std::atomic<PrismaView>     g_helperView{ 0 };
static std::atomic<bool>           g_open{ false };
static std::atomic<bool>           g_cookingBench{ false };
static bool                        g_handingOff = false;
static std::atomic<bool>           g_cheatMode{ false };

static std::mutex        g_menuNameLock;
static RE::BSFixedString g_menuNameGuarded;

static RE::BSFixedString MenuName()
{
    std::lock_guard<std::mutex> lk(g_menuNameLock);
    return g_menuNameGuarded;
}
static void SetMenuName(const RE::BSFixedString& a_name)
{
    std::lock_guard<std::mutex> lk(g_menuNameLock);
    g_menuNameGuarded = a_name;
}

static PrismaView ActiveView()
{
    return g_view.load();
}

static void CreateCraftingView();
static void CreateWorkshopHelperView();

static std::vector<uint32_t> GetBenchKeywordIdsForMenu(RE::IMenu* raw)
{
    std::vector<uint32_t> ids;
    if (!raw) return ids;
    auto* wb = static_cast<RE::WorkbenchMenuBase*>(raw);
    if (!wb) return ids;
    auto* ref = wb->workbenchRef.get();
    if (!ref) return ids;
    auto* base = ref->GetObjectReference();
    if (!base) return ids;
    auto* kwForm = RE::fallout_cast<RE::BGSKeywordForm*>(base);
    if (!kwForm) return ids;
    kwForm->ForEachKeyword([&](RE::BGSKeyword* kw) -> RE::BSContainer::ForEachResult {
        if (kw && CobjCache::IsBenchKeyword(kw->GetFormID()))
            ids.push_back(kw->GetFormID());
        return RE::BSContainer::ForEachResult::kContinue;
    });
    return ids;
}

static bool OpenBenchHasKeyword(uint32_t keywordId)
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    auto menu = ui->GetMenu(MenuName());
    if (!menu) return false;
    auto* wb = static_cast<RE::WorkbenchMenuBase*>(menu.get());
    if (!wb) return false;
    auto* ref = wb->workbenchRef.get();
    if (!ref) return false;
    auto* base = ref->GetObjectReference();
    if (!base) return false;
    auto* kwForm = RE::fallout_cast<RE::BGSKeywordForm*>(base);
    if (!kwForm) return false;
    bool found = false;
    kwForm->ForEachKeyword([&](RE::BGSKeyword* kw) -> RE::BSContainer::ForEachResult {
        if (kw && kw->GetFormID() == keywordId) { found = true; return RE::BSContainer::ForEachResult::kStop; }
        return RE::BSContainer::ForEachResult::kContinue;
    });
    return found;
}

constexpr uint32_t kKW_WorkbenchCooking = 0x00102152;


static void AddSourceRef(std::vector<RE::TESObjectREFR*>& refs, RE::TESObjectREFR* c)
{
    if (!c || c->GetFormID() == 0x14) return;
    for (auto* e : refs)
        if (e == c || e->GetFormID() == c->GetFormID()) return;
    refs.push_back(c);
}

static std::vector<RE::TESObjectREFR*> GetBenchSourceRefs()
{
    std::vector<RE::TESObjectREFR*> refs;
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return refs;
    auto menu = ui->GetMenu(MenuName());
    if (!menu) return refs;
    auto* wb = static_cast<RE::WorkbenchMenuBase*>(menu.get());
    if (!wb) return refs;
    AddSourceRef(refs, wb->workbenchContainerRef.get());
    AddSourceRef(refs, wb->sharedContainerRef.get());
    for (auto& c : wb->sharedContainers)
        AddSourceRef(refs, c.get());
    return refs;
}

static std::vector<RE::TESObjectREFR*> GetWorkshopSourceRefs()
{
    std::vector<RE::TESObjectREFR*> refs;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return refs;

    if (REX::FModule::IsRuntimeOG()) {
        using CollectFn = void (*)(RE::BSScrapArray<RE::NiPointer<RE::TESObjectREFR>>&, bool);
        static REL::Relocation<CollectFn> collectLinked{ REL::ID(911327) };
        RE::BSScrapArray<RE::NiPointer<RE::TESObjectREFR>> linked;
        collectLinked(linked, true);
        for (auto& c : linked)
            if (c) AddSourceRef(refs, c.get());
        if (!refs.empty()) {
            logger::info("PrismaCrafting: workshop supply pool = {} container(s) (settlement + supply lines)",
                         refs.size());
            return refs;
        }
    }

    AddSourceRef(refs, RE::Workshop::FindNearestValidWorkshop(*player));
    return refs;
}


static void RestoreInputAfterClose()
{
    auto* cm = RE::ControlMap::GetSingleton();
    if (!cm) return;
    if (cm->byTextEntryCount > 0) {
        logger::info("PrismaCrafting: clearing stuck text-entry mode on close (count={})", cm->byTextEntryCount);
        int guard = 0;
        while (cm->byTextEntryCount > 0 && guard++ < 16) cm->SetTextEntryMode(false);
    }
    cm->SetIgnoreKeyboardMouse(false);
}

static void StopCraftingAudio()
{
    auto av = ActiveView();
    if (g_api && g_api->IsValid(av)) g_api->InteropCall(av, "stopLamp", "");
}

static void CloseCrafting()
{
    auto av = ActiveView();
    logger::debug("CloseCrafting: menu='{}' viewValid={} helperValid={}", MenuName().c_str(),
        g_api && g_api->IsValid(av), g_api && g_api->IsValid(g_helperView));
    StopCraftingAudio();
    if (g_api) {
        if (g_api->IsValid(av))           { g_api->Unfocus(av);           g_api->Hide(av); }
        if (g_api->IsValid(g_helperView)) { g_api->Unfocus(g_helperView); g_api->Hide(g_helperView); }
    }
    RestoreInputAfterClose();

    if (auto* ui = RE::UI::GetSingleton()) {
        if (auto menu = ui->GetMenu(MenuName()); menu) {
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kRequiresUpdate, true);
            if (menu->uiMovie) menu->uiMovie->SetVisible(true);
        }
    }
    if (auto* msgQ = RE::UIMessageQueue::GetSingleton())
        msgQ->AddMessage(MenuName(), RE::UI_MESSAGE_TYPE::kHide);
    g_open = false;
}

static void DestroyAllViews()
{
    if (!g_api) return;

    logger::debug("DestroyAllViews: helper={} (crafting={} kept for reuse)", g_helperView.load(), g_view.load());
    StopCraftingAudio();
    if (g_api->IsValid(g_helperView)) { g_api->Destroy(g_helperView); g_helperView = 0; }
}


static std::vector<uint32_t> g_benchIds;

static void LogMemory(const char* a_when)
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) return;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    logger::warn("PrismaCrafting: [MEM] {:<22} private={} MB  workingSet={} MB  |  system {} / {} MB used",
                 a_when,
                 pmc.PrivateUsage / (1024 * 1024),
                 pmc.WorkingSetSize / (1024 * 1024),
                 (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024),
                 ms.ullTotalPhys / (1024 * 1024));
}

template <class F>
static void OnMainThread(const char* a_arg, F&& a_fn)
{
    std::string arg(a_arg ? a_arg : "");
    auto* task = F4SE::GetTaskInterface();
    if (!task) { a_fn(arg.c_str()); return; }
    task->AddTask([arg = std::move(arg), fn = std::forward<F>(a_fn)]() { fn(arg.c_str()); });
}

static void PushRecipesForCurrentBench()
{
    auto av = ActiveView();
    if (!g_api || !g_api->IsValid(av)) return;

    CobjCache::Build();

    g_benchIds.clear();
    if (auto* ui = RE::UI::GetSingleton()) {
        if (auto menu = ui->GetMenu(MenuName()))
            g_benchIds = GetBenchKeywordIdsForMenu(menu.get());
    }

    g_api->InteropCall(av, "setMenuKind", MenuName().c_str());

    g_api->InteropCall(av, "setBenchMode", g_cookingBench.load() ? "cooking" : "craft");

    char caps[32];
    std::snprintf(caps, sizeof(caps), "%u", CobjCache::PlayerCaps());
    g_api->InteropCall(av, "setCaps", caps);
    g_api->InteropCall(av, "setUnlockAll", WorkbenchUpgrades::UnlockAllBenches() ? "1" : "0");

    g_api->InteropCall(av, "benchReady", "");

    logger::info("PrismaCrafting: bench ready for {} ({} bench keyword(s))",
        MenuName().c_str(), g_benchIds.size());

    CobjCache::ExportCurrentView(g_benchIds);
}

static std::string JsonEsc(const std::string& s)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string o; o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                o += "\\u00";
                o += hex[(c >> 4) & 0x0F];
                o += hex[c & 0x0F];
            } else if (c >= 0xF0) {

                while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) ++i;
            } else {
                o += static_cast<char>(c);
            }
            break;
        }
    }
    return o;
}

static void PushWorkbenches()
{
    if (!g_api || !g_api->IsValid(g_view)) return;

    WorkbenchUpgrades::Poll();
    auto sources = GetBenchSourceRefs();
    const uint32_t caps = CobjCache::PlayerCaps();
    const uint64_t now  = WorkbenchUpgrades::NowEpoch();
    logger::info("PushWorkbenches: caps={} benchSourceRefs={} (menu='{}')", caps, sources.size(), MenuName().c_str());

    std::string j = "{\"now\":" + std::to_string(now) + ",\"caps\":" + std::to_string(caps) + ",\"benches\":[";
    bool firstB = true;
    for (const auto& b : WorkbenchUpgrades::Benches()) {
        auto st = WorkbenchUpgrades::GetState(b.id);
        const int  maxLvl    = static_cast<int>(b.levels.size());
        const bool upgrading = st.upgradeCompleteAt != 0;

        if (!firstB) j += ',';
        firstB = false;
        j += "{\"id\":\"" + JsonEsc(b.id) + "\",\"name\":\"" + JsonEsc(b.name) +
             "\",\"desc\":\"" + JsonEsc(b.desc) + "\",\"nif\":\"" + JsonEsc(b.nif) + "\"";
        j += ",\"level\":" + std::to_string(st.level) + ",\"maxLevel\":" + std::to_string(maxLvl);
        j += ",\"upgrading\":" + std::string(upgrading ? "true" : "false") +
             ",\"completeAt\":" + std::to_string(st.upgradeCompleteAt);

        j += ",\"levels\":[";
        for (size_t i = 0; i < b.levels.size(); ++i) {
            const auto& L = b.levels[i];
            if (i) j += ',';
            j += "{\"level\":" + std::to_string(L.level) + ",\"bonus\":\"" + JsonEsc(L.bonus) +
                 "\",\"earned\":" + std::string(st.level >= L.level ? "true" : "false") + "}";
        }
        j += "]";

        if (st.level < maxLvl) {
            const auto& L = b.levels[st.level];
            bool ready = caps >= L.caps;
            std::string mats = "[";
            for (size_t i = 0; i < L.cost.size(); ++i) {
                const auto& c = L.cost[i];
                if (i) mats += ',';
                uint32_t have       = c.form ? CobjCache::CountHeld(c.form, sources) : 0;
                uint32_t playerOnly = c.form ? CobjCache::CountHeld(c.form, {})      : 0;

                std::string nif;
                RE::TESModel* modelSrc = nullptr;
                if (auto* cmpo = c.form ? c.form->As<RE::BGSComponent>() : nullptr; cmpo && cmpo->scrapItem)
                    modelSrc = cmpo->scrapItem->As<RE::TESModel>();
                else if (c.form)
                    modelSrc = c.form->As<RE::TESModel>();
                if (modelSrc) {
                    if (auto* p = modelSrc->GetModel(); p && p[0]) nif = p;
                }

                logger::info("  mat '{}' ref='{}' form={:08X} player={} total(+settlement)={} nif='{}'",
                    c.name, c.ref, c.form ? c.form->GetFormID() : 0u, playerOnly, have, nif);
                if (!c.form || have < c.count) ready = false;
                mats += "{\"name\":\"" + JsonEsc(c.name) + "\",\"need\":" + std::to_string(c.count) +
                        ",\"have\":" + std::to_string(have) + ",\"nif\":\"" + JsonEsc(nif) + "\"}";
            }
            mats += "]";
            j += ",\"next\":{\"level\":" + std::to_string(L.level) + ",\"timeSec\":" + std::to_string(L.timeSec) +
                 ",\"caps\":" + std::to_string(L.caps) + ",\"capsHave\":" + std::to_string(caps) +
                 ",\"bonus\":\"" + JsonEsc(L.bonus) + "\",\"ready\":" + std::string(ready ? "true" : "false") +
                 ",\"mats\":" + mats + "}";
        } else {
            j += ",\"next\":null";
        }
        j += "}";
    }
    j += "]}";
    g_api->InteropCall(g_view, "setWorkbenches", j.c_str());
}

static void OnDomReady(PrismaView view)
{
    logger::info("PrismaCrafting: OnDomReady ENTER (view={}, open={})", view, g_open.load());
    if (!g_api || view == 0) return;

    TurntableRenderer::Init();

    g_api->RegisterJSListener(view, "prisma_close",   [](const char*) {
        OnMainThread(nullptr, [](const char*) { CloseCrafting(); });
    });

    g_api->RegisterJSListener(view, "requestTurntable", [](const char* arg) {
        if (!arg || !arg[0]) return;
        std::string s(arg);
        auto p1 = s.find('\n');
        if (p1 == std::string::npos) return;
        auto p2 = s.find('\n', p1 + 1);
        std::string slug = s.substr(0, p1);

        if (slug.empty() || slug.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
            return;
        std::string nif  = (p2 == std::string::npos) ? s.substr(p1 + 1)
                                                     : s.substr(p1 + 1, p2 - (p1 + 1));
        std::string key  = (p2 == std::string::npos) ? std::string() : s.substr(p2 + 1);
        TurntableRenderer::Request(slug, nif, key);
    });
    g_api->RegisterJSListener(view, "prisma_refresh", [](const char*) {
        OnMainThread(nullptr, [](const char* arg) { PushRecipesForCurrentBench();
        });
    });

    g_api->RegisterJSListener(view, "requestPage", [](const char* arg) {
        OnMainThread(arg, [](const char* arg) {
        auto av = ActiveView();
        if (!g_api || !g_api->IsValid(av)) return;
        std::string s(arg ? arg : "");
        std::string parts[7];
        for (int i = 0, pos = 0; i < 7; ++i) {
            auto nl = s.find('\n', pos);
            parts[i] = (nl == std::string::npos) ? s.substr(pos) : s.substr(pos, nl - pos);
            if (nl == std::string::npos) break;
            pos = static_cast<int>(nl) + 1;
        }
        std::string search = parts[0];
        for (auto& c : search) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string category = parts[1].empty() ? std::string("ALL") : parts[1];
        int lock = std::atoi(parts[2].c_str());
        int sort = std::atoi(parts[3].c_str());
        int page = std::atoi(parts[4].c_str());
        int pageSize = std::atoi(parts[5].c_str());
        std::string sub = parts[6].empty() ? std::string("ALL") : parts[6];
        if (pageSize <= 0 || pageSize > 500) pageSize = 60;
        std::string json = CobjCache::QueryRecipesPage(
            g_benchIds, std::string(MenuName().c_str()), search, category, sub, lock, sort, page, pageSize,
            GetBenchSourceRefs(), g_cookingBench.load());
        g_api->InteropCall(av, "setPage", json.c_str());
        });
    });

    g_api->RegisterJSListener(view, "requestWorkbenches", [](const char*) {
        OnMainThread(nullptr, [](const char* arg) { PushWorkbenches();
        });
    });

    g_api->RegisterJSListener(view, "startUpgrade", [](const char* arg) {
        OnMainThread(arg, [](const char* arg) {
        if (!arg || !arg[0]) return;
        std::string id(arg);

        while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back();
        std::string reason;
        bool ok = WorkbenchUpgrades::StartUpgrade(id, GetBenchSourceRefs(), &reason);
        logger::info("startUpgrade('{}') -> {} ({})", id, ok ? "OK" : "REJECT", reason);
        PushWorkbenches();
        });
    });

    g_api->RegisterJSListener(view, "requestDesc", [](const char* arg) {
        OnMainThread(arg, [](const char* arg) {
        auto av = ActiveView();
        if (!g_api || !g_api->IsValid(av) || !arg || !arg[0]) return;
        uint32_t formId = static_cast<uint32_t>(std::strtoul(arg, nullptr, 16));
        if (!formId) return;
        std::string desc = CobjCache::GetRecipeDesc(formId);
        char hdr[16]; std::snprintf(hdr, sizeof(hdr), "%08X\t", formId);
        g_api->InteropCall(av, "setDesc", (std::string(hdr) + desc).c_str());
        });
    });

    g_api->RegisterJSListener(view, "prisma_craft",   [](const char* arg) {
        OnMainThread(arg, [](const char* arg) {
        if (!arg || !arg[0]) { logger::debug("prisma_craft: empty arg, ignored"); return; }
        uint32_t formId = static_cast<uint32_t>(std::strtoul(arg, nullptr, 16));
        if (!formId) { logger::debug("prisma_craft: arg '{}' parsed to formId 0, ignored", arg); return; }
        int qty = 1;
        if (const char* comma = std::strchr(arg, ',')) {
            qty = std::atoi(comma + 1);
            qty = qty < 1 ? 1 : (qty > 999 ? 999 : qty);
        }
        logger::debug("prisma_craft: request formId={:08X} qty={} cheat={}", formId, qty, g_cheatMode.load());
        int made = CobjCache::CraftRecipe(formId, g_cheatMode, GetBenchSourceRefs(), qty);
        logger::info("PrismaCrafting: craft {:08X} x{} (cheat={}) -> made {}", formId, qty, g_cheatMode.load(), made);
        if (made > 0) {

            auto av = ActiveView();
            if (g_api && g_api->IsValid(av)) {
                char b[16]; std::snprintf(b, sizeof(b), "%08X", formId);
                g_api->InteropCall(av, "setPostCraft", b);
            }
            PushRecipesForCurrentBench();
        }
        });
    });

    g_api->RegisterJSListener(view, "prisma_cheat", [](const char* arg) {
        g_cheatMode = (arg && arg[0] == '1');
        logger::info("PrismaCrafting: cheat mode = {}", g_cheatMode.load());
    });

    g_api->RegisterJSListener(view, "saveTheme", [](const char* arg) {
        SaveTheme(arg ? arg : "tarkov");
        logger::info("PrismaCrafting: theme = {}", arg ? arg : "tarkov");
    });
    g_api->RegisterJSListener(view, "saveUiScale", [](const char* arg) {
        SaveScale("CraftScale", arg ? std::atoi(arg) : 100);
    });

    g_api->RegisterJSListener(view, "prisma_loaded", [](const char*) {
        OnMainThread(nullptr, [](const char*) {
        auto av = ActiveView();
        if (!g_open || !g_api || !g_api->IsValid(av)) return;
        g_api->Show(av);
        g_api->Focus(av, true, false);
        if (auto* ui = RE::UI::GetSingleton()) {
            if (auto menu = ui->GetMenu(MenuName())) {
                if (menu->uiMovie) menu->uiMovie->SetVisible(false);
            }
        }
        logger::info("PrismaCrafting: {} view activated post-load (typing enabled)", MenuName().c_str());
        });
    });

    if (g_open) PushRecipesForCurrentBench();

    {
        char sc[16]; std::snprintf(sc, sizeof(sc), "%d", LoadScale("CraftScale"));
        g_api->InteropCall(view, "setUiScale", sc);
        g_api->InteropCall(view, "setTheme", LoadTheme().c_str());
    }

    if (g_open && g_api && g_api->IsValid(view)) {
        g_api->Show(view);
        if (auto* ui = RE::UI::GetSingleton()) {
            if (auto menu = ui->GetMenu(MenuName()); menu && menu->uiMovie)
                menu->uiMovie->SetVisible(false);
        }
    }

    logger::info("PrismaCrafting: DOM ready, listeners registered (view={})", view);
}

static void CreateCraftingView()
{
    if (g_view != 0) return;
    if (!g_api) {
        logger::error("PrismaCrafting: PrismaUI API not available");
        return;
    }
    try {
        g_view = g_api->CreateView("crafting.html", OnDomReady);

        g_api->SetViewOwnsEscape(g_view, true);
        MarkViewAsPanel(g_view);

        g_api->RegisterConsoleCallback(g_view,
            [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel level, const char* msg) {
                const char* tag = (level == PRISMA_UI_API::ConsoleMessageLevel::Error)   ? "[JS ERR] " :
                                  (level == PRISMA_UI_API::ConsoleMessageLevel::Warning) ? "[JS WARN]" :
                                                                                           "[JS LOG] ";
                logger::info("{} {}", tag, msg);
            });

        g_api->Hide(g_view);
        logger::info("PrismaCrafting: crafting view created (id={})", g_view.load());
    } catch (const std::exception& e) {
        logger::error("PrismaCrafting: CreateCraftingView failed: {}", e.what());
    }
}


static float ReadGlobal(std::uint32_t formId)
{
    auto* form = RE::TESForm::GetFormByID(formId);
    if (auto* g = RE::fallout_cast<RE::TESGlobal*>(form)) return g->value;
    return -1.0f;
}


static std::string WorkshopStatsJson()
{
    struct G { const char* key; std::uint32_t id; };
    static constexpr G kGlobals[] = {
        { "population",  0x0004BE84 },
        { "unassigned",  0x0004BE92 },
        { "food",        0x0004BE82 },
        { "water",       0x0004BE96 },
        { "power",       0x0004BE93 },
        { "defense",     0x0004BE95 },
        { "beds",        0x0004BE97 },
        { "happiness",   0x0004BE7E },
        { "happyTarget", 0x0004BE98 },
    };

    std::string json = "{";
    bool first = true;
    for (const auto& g : kGlobals) {
        if (!first) json += ',';
        first = false;
        json += '"'; json += g.key; json += "\":";
        json += std::to_string(static_cast<int>(ReadGlobal(g.id)));
    }
    json += '}';
    return json;
}

static void PushWorkshopStats()
{
    if (!g_api || !g_api->IsValid(g_helperView)) return;
    g_api->InteropCall(g_helperView, "setWorkshopStats", WorkshopStatsJson().c_str());
}

static void SyncWorkshopCategories();

static void CancelPlacementFromUI();

static void PushFreeState()
{
    if (!g_api || !g_api->IsValid(g_helperView)) return;
    char json[96];
    std::snprintf(json, sizeof(json), "{\"place\":%d,\"cam\":%d,\"gsnap\":%d,\"osnap\":%d}",
        WorkshopFreedom::FreePlaceEnabled() ? 1 : 0,
        WorkshopFreedom::FreeCameraEnabled() ? 1 : 0,
        WorkshopFreedom::GroundSnapEnabled() ? 1 : 0,
        WorkshopFreedom::ObjectSnapEnabled() ? 1 : 0);
    g_api->InteropCall(g_helperView, "setFreeState", json);
}

static void PushHelperData()
{
    if (!g_api || !g_api->IsValid(g_helperView)) return;
    CobjCache::Build();
    SyncWorkshopCategories();
    std::string buildables = CobjCache::WorkshopBuildablesJson(GetWorkshopSourceRefs());
    g_api->InteropCall(g_helperView, "setBuildables", buildables.c_str());
    PushWorkshopStats();
    logger::info("PrismaCrafting: pushed workshop build helper data");
}

static void DismissHelper()
{
    logger::info("PrismaCrafting: workshop exit requested (RequestExitWorkshop)");
    RE::Workshop::RequestExitWorkshop(false);
}

static std::atomic<bool> g_placing{ false };
static std::atomic<bool> g_placementUpdateQueued{ false };
static std::atomic<bool> g_placementStartQueued{ false };

static std::atomic<std::uint32_t> g_placingFormId{ 0 };

static std::atomic<std::uint32_t> g_placingRecipeId{ 0 };

static std::atomic<bool> g_ghostBusy{ false };

struct GhostBusyGuard
{
    GhostBusyGuard()  { g_ghostBusy.store(true); }
    ~GhostBusyGuard() { g_ghostBusy.store(false); }
};

class GuardedRefHandle
{
public:
    RE::ObjectRefHandle Load() const
    {
        std::lock_guard<std::mutex> lk(m_lock);
        return m_handle;
    }
    void Store(RE::ObjectRefHandle a_handle)
    {
        std::lock_guard<std::mutex> lk(m_lock);
        m_handle = a_handle;
    }

private:
    mutable std::mutex  m_lock;
    RE::ObjectRefHandle m_handle{};
};

static GuardedRefHandle g_ghostRef;

static GuardedRefHandle g_lastPlacedRef;

static std::atomic<bool> g_scrapOverlayOpen{ false };
static std::atomic<int> g_scrapSuppressLatch{ 0 };
static constexpr int kScrapSuppressLatchTicks = 12;
static std::atomic<int> g_scrapAfterPlacementCancelTicks{ 0 };
static std::atomic<int> g_scrapAfterPlacementClearTicks{ 0 };
static constexpr int kScrapAfterPlacementCancelTimeoutTicks = 12;
static GuardedRefHandle g_scrapTarget;
static GuardedRefHandle g_scrapPendingTarget;
static std::atomic<bool> g_scrapPending{ false };

namespace WSExt
{

    inline void CreateNewReference(const RE::Workshop::ContextData& a_ctx, bool a_fromEdit)
    {
        static REL::Relocation<void (*)(const RE::Workshop::ContextData&, bool)> f{ REL::ID(1042671) };
        f(a_ctx, a_fromEdit);
    }

    inline void Update(const RE::Workshop::ContextData& a_ctx)
    {
        static REL::Relocation<void (*)(const RE::Workshop::ContextData&)> f{ REL::ID(679561) };
        f(a_ctx);
    }

    inline RE::Workshop::WorkshopMenuNode* RootNode()
    {
        static REL::Relocation<RE::Workshop::WorkshopMenuNode*> p{ REL::Offset{ 0x58E0850 } };
        return p.get();
    }

    inline std::uint32_t* CurrentMenuRow()
    {
        static REL::Relocation<std::uint32_t*> p{ REL::Offset{ 0x58DF5DC } };
        return p.get();
    }

    inline void DestroyCurrentItem(const RE::Workshop::ContextData& a_ctx, bool a_arg)
    {
        static REL::Relocation<void (*)(const RE::Workshop::ContextData&, bool)> f{ REL::ID(540264) };
        f(a_ctx, a_arg);
    }

    inline void VatsDisable(bool a_disable)
    {
        static REL::Relocation<void (*)(bool)> f{ REL::ID(845361) };
        f(a_disable);
    }

    inline void SetVATShaderColor(int a_vsColor)
    {
        static REL::Relocation<void (*)(int)> f{ REL::ID(1565251) };
        f(a_vsColor);
    }

    inline void ActorSetProcessMe(RE::Actor* a_actor, bool a_a, bool a_b)
    {
        static REL::Relocation<void (*)(RE::Actor*, bool, bool)> f{ REL::ID(7551) };
        f(a_actor, a_a, a_b);
    }
    inline void ActorSetGhost(RE::Actor* a_actor, bool a_ghost)
    {
        static REL::Relocation<void (*)(RE::Actor*, bool)> f{ REL::ID(1377793) };
        f(a_actor, a_ghost);
    }
    inline void ActorSetUnconscious(RE::Actor* a_actor, bool a_unc)
    {
        static REL::Relocation<void (*)(RE::Actor*, bool)> f{ REL::ID(862414) };
        f(a_actor, a_unc);
    }

    inline bool IsWorkshopItem(const RE::TESObjectREFR& a_ref)
    {
        static REL::Relocation<bool (*)(const RE::TESObjectREFR&)> f{ REL::ID(1386903) };
        return f(a_ref);
    }

    inline bool* ActiveEdit()
    {
        static REL::Relocation<bool*> p{ REL::Offset{ 0x58DF5F0 } };
        return p.get();
    }

    inline RE::NiPoint3* PlacementItemTranslLS()
    {
        static REL::Relocation<RE::NiPoint3*> p{ REL::Offset{ 0x58DFE40 } };
        return p.get();
    }
}

static constexpr float kPlacementForwardOffset = 300.0f;

static void SyncWorkshopCategories()
{
    if (!REX::FModule::IsRuntimeOG()) return;
    auto* root = WSExt::RootNode();
    if (!root) return;

    std::unordered_map<std::uint32_t, std::string> categories;

    auto keywordName = [](RE::BGSKeyword* kw) -> std::string {
        if (!kw) return {};
        if (auto sv = RE::TESFullName::GetFullName(*kw); !sv.empty()) return std::string(sv);
        const char* eid = kw->GetFormEditorID();
        return (eid && eid[0]) ? eid : std::string{};
    };

    struct Frame { RE::Workshop::WorkshopMenuNode* node; std::string path; };
    std::vector<Frame> stack{ { root, std::string{} } };
    while (!stack.empty()) {
        auto [node, path] = std::move(stack.back());
        stack.pop_back();
        if (!node) continue;

        std::string here = path;
        if (node != root) {
            if (auto name = keywordName(node->filterKeyword); !name.empty())
                here = here.empty() ? name : here + " > " + name;
        }

        if (node->recipe && !here.empty())
            categories.emplace(node->recipe->GetFormID(), here);

        for (auto& child : node->children)
            if (child) stack.push_back({ child.get(), here });
    }

    CobjCache::ApplyWorkshopCategories(categories);
}

static std::string DescribeRef(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return "<null>";
    std::string name;
    if (auto* base = a_ref->GetObjectReference()) {
        if (auto sv = RE::TESFullName::GetFullName(*base); !sv.empty())
            name = std::string(sv);
        else if (const char* eid = base->GetFormEditorID(); eid && eid[0])
            name = eid;
    }
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%08X '%s'", a_ref->GetFormID(), name.c_str());
    return buf;
}
static std::string DescribeRef(const RE::ObjectRefHandle& a_handle)
{
    auto ref = a_handle.get();
    return ref ? DescribeRef(ref.get()) : "<none>";
}

namespace {
std::thread::id g_mainThreadId{};

void LogPlacementThread(const char* a_site)
{
    const auto tid = std::this_thread::get_id();
    const bool onMain = (tid == g_mainThreadId);
    if (onMain) {
        logger::info("[THREAD] {}: main thread", a_site);
    } else {
        logger::warn("[THREAD] {}: OFF-MAIN (engine world mutation from a task-pool thread)", a_site);
    }
}
}

static bool DeleteGhostRef(const RE::ObjectRefHandle& a_handle, const char* a_site)
{
    LogPlacementThread("DeleteGhostRef");
    auto ref = a_handle.get();
    if (!ref) return false;

    if (a_handle == g_lastPlacedRef.Load()) {
        logger::warn("PrismaCrafting: [GHOST] REFUSED delete at {} -- {} is the ref we just committed",
                     a_site, DescribeRef(ref.get()));
        return false;
    }
    if (WSExt::IsWorkshopItem(*ref)) {
        logger::warn("PrismaCrafting: [GHOST] REFUSED delete at {} -- {} is a COMMITTED workshop item "
                     "(engine linked it to the workshop); our commit tracking missed it",
                     a_site, DescribeRef(ref.get()));
        return false;
    }

    logger::warn("PrismaCrafting: [GHOST] SetDelete on ghost {} (site: {})", DescribeRef(ref.get()), a_site);
    ref->Disable();
    ref->SetDelete(true);
    return true;
}

static bool EngineHoldsOurGhost()
{
    auto* handle = RE::Workshop::GetPlacementItem();
    if (!handle) return false;
    auto engineRef = handle->get();
    auto ourRef    = g_ghostRef.Load().get();
    if (!engineRef || !ourRef || engineRef.get() != ourRef.get()) return false;

    if (WSExt::IsWorkshopItem(*engineRef)) {
        logger::warn("PrismaCrafting: [GHOST] engine placement item {} matches our ghost handle but is a "
                     "COMMITTED workshop item -- treating as NOT ours (commit tracking missed it)",
                     DescribeRef(engineRef.get()));
        return false;
    }
    return true;
}

static void DestroyGhostNow()
{
    LogPlacementThread("DestroyGhostNow");
    GhostBusyGuard busy;

    if (const auto ghost = g_ghostRef.Load(); ghost && ghost == g_lastPlacedRef.Load()) {
        logger::warn("PrismaCrafting: [GHOST] REFUSED to destroy ref {:08X} -- it is the item just "
                     "PLACED, not a ghost (commit bookkeeping had not caught up)",
                     ghost.get() ? ghost.get()->GetFormID() : 0);
        g_ghostRef.Store({});
        if (REX::FModule::IsRuntimeOG())
            if (auto* activeEdit = WSExt::ActiveEdit()) *activeEdit = false;
        return;
    }

    auto* liveH = RE::Workshop::GetPlacementItem();
    const bool ours = EngineHoldsOurGhost();
    logger::warn("PrismaCrafting: [DESTROY-GHOST] enginePlacementItem={} | ourGhost={} | lastPlaced={} | "
                 "EngineHoldsOurGhost={}",
                 (liveH && liveH->get()) ? DescribeRef(liveH->get().get()) : "<none>",
                 DescribeRef(g_ghostRef.Load()), DescribeRef(g_lastPlacedRef.Load()), ours);

    if (ours) {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            RE::Workshop::ContextData ctx(player);
            WSExt::DestroyCurrentItem(ctx, false);
        }
    } else {
        logger::info("PrismaCrafting: [GHOST] skipping DestroyCurrentItem -- the engine's placement item "
                     "is not our ghost (crosshair is on an existing building)");
    }
    DeleteGhostRef(g_ghostRef.Load(), "DestroyGhostNow");
    g_ghostRef.Store({});
    if (REX::FModule::IsRuntimeOG())
        if (auto* activeEdit = WSExt::ActiveEdit()) *activeEdit = false;
}

static void ClearWorkshopSelection(RE::Workshop::WorkshopMenuNode& a_node)
{
    a_node.selected = false;
    for (auto& child : a_node.children)
        if (child) ClearWorkshopSelection(*child);
}

static bool FindWorkshopItemPath(RE::Workshop::WorkshopMenuNode& a_node,
                                 std::uint32_t a_recipeId,
                                 std::uint32_t a_createdId,
                                 std::vector<RE::Workshop::WorkshopMenuNode*>& a_path)
{
    a_path.push_back(&a_node);
    const auto* created = a_node.recipe ? a_node.recipe->GetCreatedItem() : nullptr;
    const bool recipeMatches = a_node.recipe && a_node.recipe->GetFormID() == a_recipeId;
    if (recipeMatches &&
        ((created && created->GetFormID() == a_createdId) ||
         (a_node.form && a_node.form->GetFormID() == a_createdId)))
        return true;
    for (auto& child : a_node.children)
        if (child && FindWorkshopItemPath(*child, a_recipeId, a_createdId, a_path)) return true;
    a_path.pop_back();
    return false;
}

static bool SpawnPlacementGhost(std::uint32_t a_recipeId, std::uint32_t a_createdId)
{
    GhostBusyGuard busy;
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* root = WSExt::RootNode();
    if (!player || !player->parentCell || !root) return false;

    std::vector<RE::Workshop::WorkshopMenuNode*> path;
    if (!FindWorkshopItemPath(*root, a_recipeId, a_createdId, path) || path.size() < 2) return false;

    auto* leaf = path.back();
    auto* object = leaf->form ? leaf->form->As<RE::TESBoundObject>() : nullptr;
    if (!object && leaf->recipe) {
        if (auto* created = leaf->recipe->GetCreatedItem()) object = created->As<RE::TESBoundObject>();
    }
    if (!object) {
        logger::warn("PrismaCrafting: selected workshop item {:08X} has no bound placement object", a_createdId);
        return false;
    }

    RE::Workshop::ContextData ctx(player);

    {
        auto* liveH = RE::Workshop::GetPlacementItem();
        logger::warn("PrismaCrafting: [SPAWN] enginePlacementItem={} | ourGhost={} | lastPlaced={} | "
                     "EngineHoldsOurGhost={}",
                     (liveH && liveH->get()) ? DescribeRef(liveH->get().get()) : "<none>",
                     DescribeRef(g_ghostRef.Load()), DescribeRef(g_lastPlacedRef.Load()), EngineHoldsOurGhost());
    }

    if (EngineHoldsOurGhost()) {
        WSExt::DestroyCurrentItem(ctx, false);
    } else if (auto* h = RE::Workshop::GetPlacementItem(); h && h->get()) {
        logger::info("PrismaCrafting: [GHOST] NOT destroying engine placement item {} -- it is not "
                     "our ghost (crosshair-selected building), only clearing our own",
                     DescribeRef(h->get().get()));
    }

    DeleteGhostRef(g_ghostRef.Load(), "SpawnPlacementGhost stale-cleanup");
    g_ghostRef.Store({});

    if (auto* offset = WSExt::PlacementItemTranslLS()) {
        offset->x = 0.0f;
        offset->y = kPlacementForwardOffset;
        offset->z = 0.0f;
    }
    RE::Workshop::InitializePlacementReference(ctx, *object);
    if (auto* handle = RE::Workshop::GetPlacementItem()) {
        auto placement = handle->get();
        if (!placement) return false;

        if (!placement->Get3D()) {

            if (auto* loaded = placement->Load3D(false)) placement->Set3D(loaded, true);
        }
        if (placement->Get3D()) {
            g_ghostRef.Store(placement->GetHandle());
            logger::info("PrismaCrafting: [GHOST] spawned ghost ref {:08X} for item {:08X}",
                         placement->GetFormID(), a_createdId);
            if (auto* data = RE::Workshop::GetCurrentPlacementItemData()) data->Set(*placement);
            if (auto* actor = placement->As<RE::Actor>()) {
                WSExt::ActorSetProcessMe(actor, false, false);
                WSExt::ActorSetGhost(actor, true);
                WSExt::ActorSetUnconscious(actor, true);
            }
            WSExt::VatsDisable(true);
            WSExt::SetVATShaderColor(0);

            if (auto* activeEdit = WSExt::ActiveEdit()) *activeEdit = true;
            WSExt::Update(ctx);
            return true;
        }
        logger::warn("PrismaCrafting: placement reference {:08X} has no loadable 3D", placement->GetFormID());
    }
    return false;
}

static void FinishStartPlacement(std::uint32_t recipeFormId, std::uint32_t createdFormId)
{
    if (!SpawnPlacementGhost(recipeFormId, createdFormId)) {
        logger::warn("PrismaCrafting: selected buildable COBJ {:08X} / object {:08X} is not in the active vanilla workshop tree",
                     recipeFormId, createdFormId);
        g_placing.store(false);
        g_placingFormId.store(0);
        g_placingRecipeId.store(0);
        WorkshopInputHook::SetActive(g_open);
        if (g_api && g_api->IsValid(g_helperView)) {
            g_api->InteropCall(g_helperView, "setPlacingMode", "0");
            g_api->InteropCall(g_helperView, "showError", "\"This item is unavailable in the active workshop\"");
        }
        return;
    }

    g_placing.store(true);
    g_placingRecipeId.store(recipeFormId);
    g_placingFormId.store(createdFormId);
    if (g_api && g_api->IsValid(g_helperView)) g_api->InteropCall(g_helperView, "setPlacingMode", "1");
}

static bool ParseHexFormId(std::string_view text, std::uint32_t& out)
{
    if (text.empty()) return false;
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (ec != std::errc{} || end != text.data() + text.size() || value == 0) return false;
    out = value;
    return true;
}

static bool ParsePlacementSelection(const char* value,
                                    std::uint32_t& recipeFormId,
                                    std::uint32_t& createdFormId)
{
    const std::string_view text = value ? std::string_view(value) : std::string_view{};
    const auto comma = text.find(',');
    if (comma != std::string_view::npos) {
        return ParseHexFormId(text.substr(0, comma), recipeFormId) &&
               ParseHexFormId(text.substr(comma + 1), createdFormId);
    }

    if (!ParseHexFormId(text, createdFormId)) return false;
    const auto* recipe = CobjCache::FindWorkshopRecipeByCreated(createdFormId);
    if (!recipe) return false;
    recipeFormId = recipe->formId;
    return true;
}

static void StartPlacement(const char* selection)
{
    LogPlacementThread("StartPlacement");
    if (!REX::FModule::IsRuntimeOG()) { logger::warn("PrismaCrafting: [PLACE] runtime not OG -- placement skipped"); return; }
    std::uint32_t recipeFormId = 0;
    std::uint32_t createdFormId = 0;
    if (!ParsePlacementSelection(selection, recipeFormId, createdFormId)) {
        logger::warn("PrismaCrafting: [PLACE] bad or ambiguous selection '{}'", selection ? selection : "");
        return;
    }

    if (g_placementStartQueued.exchange(true)) return;

    auto* task = F4SE::GetTaskInterface();
    if (!task) {
        g_placementStartQueued.store(false);
        logger::warn("PrismaCrafting: placement task interface unavailable");
        return;
    }
    task->AddTask([recipeFormId, createdFormId]() {
        struct ClearStartQueued { ~ClearStartQueued() { g_placementStartQueued.store(false); } } clearStartQueued;
        if (!g_open) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto* shop = RE::Workshop::FindNearestValidWorkshop(*player);
        if (!shop) {
            logger::warn("PrismaCrafting: [PLACE] not in a valid workshop area");
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "\"Not in a workshop area\"");
            return;
        }

        const auto sources = GetWorkshopSourceRefs();
        const auto* rec = CobjCache::FindWorkshopRecipeByFormId(recipeFormId);
        if (!rec || rec->createdFormId != createdFormId) {
            logger::warn("PrismaCrafting: [PLACE] selection mismatch COBJ {:08X} / object {:08X}",
                         recipeFormId, createdFormId);
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "\"This recipe is no longer available\"");
            return;
        }
        if (!CobjCache::WorkshopRecipeAvailable(*rec, sources)) {
            logger::info("PrismaCrafting: [PLACE] COBJ {:08X} refused -- conditions no longer pass", recipeFormId);
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "\"This item is locked\"");
            return;
        }

        if (!CobjCache::WorkshopCanAfford(*rec, sources)) {
            logger::info("PrismaCrafting: [PLACE] COBJ {:08X} / object {:08X} refused -- insufficient materials",
                         recipeFormId, createdFormId);
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "\"Not enough materials\"");
            return;
        }

        WorkshopInputHook::OnPlacementStarted();
        if (auto* mq = RE::UIMessageQueue::GetSingleton()) mq->AddMessage("CursorMenu"sv, RE::UI_MESSAGE_TYPE::kHide);

        auto* liveHandle = RE::Workshop::GetPlacementItem();
        const bool engineStillHasGhost = liveHandle && liveHandle->get();

        logger::info("PrismaCrafting: [PLACE] re-select start: want {:08X} | ghostRef={:08X} | "
                     "engine hPlacementItem={:08X} | lastPlaced={:08X}",
                     createdFormId,
                     g_ghostRef.Load().get()    ? g_ghostRef.Load().get()->GetFormID()    : 0,
                     engineStillHasGhost ? liveHandle->get()->GetFormID()   : 0,
                     g_lastPlacedRef.Load().get() ? g_lastPlacedRef.Load().get()->GetFormID() : 0);

        if (g_ghostRef.Load().get() && engineStillHasGhost) {
            logger::info("PrismaCrafting: [PLACE] re-select -- destroying previous ghost, "
                         "creating {:08X} on the next tick", createdFormId);
            DestroyGhostNow();
            if (auto* task2 = F4SE::GetTaskInterface()) {
                task2->AddTask([recipeFormId, createdFormId]() {
                    if (g_open) FinishStartPlacement(recipeFormId, createdFormId);
                });
                return;
            }

        }
        FinishStartPlacement(recipeFormId, createdFormId);
    });
}

static void OnGhostCommitted()
{

    if (g_ghostRef.Load()) {
        g_lastPlacedRef.Store(g_ghostRef.Load());
        logger::info("PrismaCrafting: [PLACE-OK] committed ref {:08X} (formId {:08X}) -- released from "
                     "ghost tracking, now permanent",
                     g_ghostRef.Load().get() ? g_ghostRef.Load().get()->GetFormID() : 0, g_placingFormId.load());
    }
    g_ghostRef.Store({});
    const std::uint32_t again = g_placingFormId.load();
    const std::uint32_t recipeAgain = g_placingRecipeId.load();
    auto* task = F4SE::GetTaskInterface();
    if (!task) {
        logger::error("PrismaCrafting: cannot settle placement payment -- task interface unavailable");
        g_placingFormId.store(0);
        g_placingRecipeId.store(0);
        return;
    }
    task->AddTask([again, recipeAgain]() {

        const auto sources = GetWorkshopSourceRefs();
        const auto* rec = CobjCache::FindWorkshopRecipeByFormId(recipeAgain);
        const bool exactRecipe = rec && rec->createdFormId == again;
        const bool paid = exactRecipe && CobjCache::WorkshopConsume(*rec, sources);
        if (!paid) {
            logger::error("PrismaCrafting: placed object {:08X} from COBJ {:08X}, but exact payment failed; ending placement session",
                          again, recipeAgain);
            g_placingFormId.store(0);
            g_placingRecipeId.store(0);
            WorkshopInputHook::SetActive(g_open);
            if (g_api && g_api->IsValid(g_helperView)) {
                g_api->InteropCall(g_helperView, "setPlacingMode", "0");
                g_api->InteropCall(g_helperView, "showError", "\"Placement payment failed\"");
                PushHelperData();
            }
            return;
        }

        if (g_open && again) {
            if (!CobjCache::WorkshopCanAfford(*rec, sources)) {
                logger::info("PrismaCrafting: out of materials for {:08X} -- placement session ended", again);
                g_placingFormId.store(0);
                g_placingRecipeId.store(0);
                WorkshopInputHook::SetActive(g_open);
                if (g_api && g_api->IsValid(g_helperView)) {
                    g_api->InteropCall(g_helperView, "setPlacingMode", "0");
                    g_api->InteropCall(g_helperView, "showError", "\"Not enough materials\"");
                    PushHelperData();
                }
                return;
            }
        }

        if (g_open && again && recipeAgain && SpawnPlacementGhost(recipeAgain, again)) {
            g_placing.store(true);
            if (g_api && g_api->IsValid(g_helperView)) {
                g_api->InteropCall(g_helperView, "setPlacingMode", "1");
                PushHelperData();
            }
            return;
        }
        g_placingFormId.store(0);
        g_placingRecipeId.store(0);
        WorkshopInputHook::SetActive(g_open);
        if (g_api && g_api->IsValid(g_helperView)) {
            g_api->InteropCall(g_helperView, "setPlacingMode", "0");
            PushHelperData();
        }
    });
}

class WorkshopItemPlacedSink : public RE::BSTEventSink<RE::Workshop::ItemPlacedEvent>
{
public:
    static WorkshopItemPlacedSink* GetSingleton() { static WorkshopItemPlacedSink s; return &s; }
    RE::BSEventNotifyControl ProcessEvent(const RE::Workshop::ItemPlacedEvent& a_event,
                                          RE::BSTEventSource<RE::Workshop::ItemPlacedEvent>*) override
    {
        if (auto* placed = a_event.placedItem.get()) {
            g_lastPlacedRef.Store(placed->GetHandle());
            logger::info("PrismaCrafting: [PLACE-OK] ItemPlacedEvent: engine committed ref {:08X}",
                         placed->GetFormID());
        }
        if (g_placing.exchange(false)) OnGhostCommitted();
        return RE::BSEventNotifyControl::kContinue;
    }
};

static constexpr std::uintptr_t kCurrentWorkshopRva = 0x58DFDBC;

static std::string ScrapDisplayName(RE::TESObjectREFR* a_ref)
{
    if (!a_ref) return std::string{};
    if (auto* base = a_ref->GetObjectReference()) {
        if (auto sv = RE::TESFullName::GetFullName(*base); !sv.empty())
            return std::string(sv);
        if (const char* eid = base->GetFormEditorID(); eid && eid[0])
            return eid;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", a_ref->GetFormID());
    return buf;
}

static void HideScrapOverlay()
{
    g_scrapOverlayOpen.store(false);
    g_scrapTarget.Store({});
    g_scrapPending.store(false);
    g_scrapPendingTarget.Store({});
    if (g_api && g_api->IsValid(g_helperView))
        g_api->InteropCall(g_helperView, "hideScrapConfirm", "");
}

static void CancelScrapConfirm()
{
    logger::info("PrismaCrafting: [SCRAP] overlay cancelled");
    HideScrapOverlay();
}

static void ConfirmScrap();

static void BeginScrapConfirm()
{
    if (!REX::FModule::IsRuntimeOG()) return;
    if (g_placing.load()) return;
    if (!g_api || !g_api->IsValid(g_helperView) || g_api->IsHidden(g_helperView)) return;

    if (g_scrapOverlayOpen.load()) {
        ConfirmScrap();
        return;
    }

    static REL::Relocation<bool*> multiSelect{ REL::ID(816517) };
    if (multiSelect.get() && *multiSelect) return;

    auto* handle = RE::Workshop::GetPlacementItem();
    if (!handle || !handle->get()) return;
    auto ref = handle->get();
    if (!ref) return;

    using CanScrapFn = bool (*)(RE::TESObjectREFR*, bool);
    static REL::Relocation<CanScrapFn> workshopCanScrapRef{ REL::ID(431436) };
    if (!workshopCanScrapRef(ref.get(), false)) return;

    using CollectFn = void (*)(const RE::TESObjectREFR&,
                               RE::BSScrapArray<RE::BSTTuple<RE::TESBoundObject*, std::uint32_t>>&);
    static constexpr std::uintptr_t kCollectScrapRewardsRva = 0x1F4B90;
    static REL::Relocation<CollectFn> collectRewards{ REL::Offset{ kCollectScrapRewardsRva } };
    RE::BSScrapArray<RE::BSTTuple<RE::TESBoundObject*, std::uint32_t>> rewards;
    collectRewards(*ref, rewards);

    const auto sources = GetWorkshopSourceRefs();
    std::string json = "{\"name\":\"";
    json += JsonEsc(ScrapDisplayName(ref.get()));
    json += "\",\"yields\":[";
    bool first = true;
    for (const auto& tup : rewards) {
        auto* obj = tup.first;
        const auto add = tup.second;
        if (!obj) continue;
        std::string nm;
        if (auto sv = RE::TESFullName::GetFullName(*obj); !sv.empty())
            nm = std::string(sv);
        else if (const char* eid = obj->GetFormEditorID(); eid && eid[0])
            nm = eid;
        else {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%08X", obj->GetFormID());
            nm = hex;
        }
        const auto stored = CobjCache::CountStored(obj, sources);
        if (!first) json += ',';
        first = false;
        json += "{\"name\":\"";
        json += JsonEsc(nm);
        json += "\",\"add\":";
        json += std::to_string(add);
        json += ",\"stored\":";
        json += std::to_string(stored);
        json += '}';
    }
    json += "]}";

    g_scrapTarget.Store(*handle);
    g_scrapOverlayOpen.store(true);
    g_api->InteropCall(g_helperView, "showScrapConfirm", json.c_str());
    logger::info("PrismaCrafting: [SCRAP] overlay for {}", DescribeRef(ref.get()));
}

static void ConfirmScrap()
{
    if (!REX::FModule::IsRuntimeOG()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->parentCell) {
        g_scrapSuppressLatch.store(kScrapSuppressLatchTicks);
        HideScrapOverlay();
        return;
    }

    auto captured = g_scrapTarget.Load();
    auto ni = captured.get();
    g_scrapSuppressLatch.store(kScrapSuppressLatchTicks);
    HideScrapOverlay();
    if (!ni) return;

    g_scrapPendingTarget.Store(captured);
    g_scrapPending.store(true);
}

static void PumpDeferredScrap()
{
    if (!g_scrapPending.exchange(false)) return;
    auto captured = g_scrapPendingTarget.Load();
    g_scrapPendingTarget.Store({});
    auto ni = captured.get();
    if (!ni) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    REL::ID placementCollisionCount{ 1013023 };
    *reinterpret_cast<std::uint32_t*>(placementCollisionCount.address()) = 0;

    if (auto* ph = RE::Workshop::GetPlacementItem(); ph) {
        if (auto cur = ph->get(); cur && cur.get() == ni.get())
            *ph = RE::ObjectRefHandle{};
    }

    static REL::Relocation<RE::ObjectRefHandle*> hCurrentWorkshop{ REL::Offset{ kCurrentWorkshopRva } };
    const RE::ObjectRefHandle previous = hCurrentWorkshop.get() ? *hCurrentWorkshop : RE::ObjectRefHandle{};
    RE::TESObjectREFR* workshopRef = previous.get() ? previous.get().get() : nullptr;
    if (!workshopRef)
        workshopRef = RE::Workshop::FindNearestValidWorkshop(*player);
    if (workshopRef && hCurrentWorkshop.get())
        *hCurrentWorkshop = workshopRef->GetHandle();

    RE::Workshop::ContextData ctx{ player };
    RE::NiPointer<RE::TESObjectREFR> ptr{ ni.get() };
    using ScrapFn = void (*)(const RE::Workshop::ContextData*,
                             RE::NiPointer<RE::TESObjectREFR>*,
                             RE::BSTArray<RE::BSTTuple<RE::TESBoundObject*, std::uint32_t>>*);
    static REL::Relocation<ScrapFn> scrapRef{ REL::ID(636327) };
    const std::string desc = DescribeRef(ni.get());
    scrapRef(&ctx, &ptr, nullptr);

    if (hCurrentWorkshop.get())
        *hCurrentWorkshop = previous;

    logger::info("PrismaCrafting: [SCRAP] ScrapReference {} (deferred render tick)", desc);
}

static void ClearScrapAfterPlacementCancel()
{
    g_scrapAfterPlacementCancelTicks.store(0);
    g_scrapAfterPlacementClearTicks.store(0);
}

static void ArmScrapAfterPlacementCancel()
{
    g_scrapAfterPlacementClearTicks.store(0);
    g_scrapAfterPlacementCancelTicks.store(1);
}

static void TickScrapAfterPlacementCancel()
{
    const int age = g_scrapAfterPlacementCancelTicks.load();
    if (age <= 0) return;
    if (!g_open.load() || g_scrapOverlayOpen.load() ||
        !g_api || !g_api->IsValid(g_helperView) || g_api->IsHidden(g_helperView)) {
        ClearScrapAfterPlacementCancel();
        return;
    }
    if (age >= kScrapAfterPlacementCancelTimeoutTicks) {
        logger::info("PrismaCrafting: [SCRAP] placement transition timed out");
        ClearScrapAfterPlacementCancel();
        return;
    }
    g_scrapAfterPlacementCancelTicks.store(age + 1);

    if (g_placing.load() || g_ghostBusy.load() || g_ghostRef.Load()) {
        g_scrapAfterPlacementClearTicks.store(0);
        return;
    }

    const int clearTicks = g_scrapAfterPlacementClearTicks.fetch_add(1) + 1;
    if (clearTicks < 2) return;

    BeginScrapConfirm();
    if (g_scrapOverlayOpen.load())
        ClearScrapAfterPlacementCancel();
}

static void ToggleRecipeTagForSearch(std::uint32_t cobjFormId)
{
    if (!REX::FModule::IsRuntimeOG()) return;
    auto* fm = RE::FavoritesManager::GetSingleton();
    if (!fm) return;
    auto* cobj = RE::fallout_cast<RE::BGSConstructibleObject*>(RE::TESForm::GetFormByID(cobjFormId));
    if (!cobj) { logger::warn("PrismaCrafting: [TAG] COBJ {:08X} not resolvable", cobjFormId); return; }
    using Fn = void (*)(RE::FavoritesManager*, RE::BGSConstructibleObject*);
    static REL::Relocation<Fn> toggleModFavorite{ REL::Offset{ 0x12702E0 } };
    toggleModFavorite(fm, cobj);
    logger::info("PrismaCrafting: [TAG] toggled tag-for-search on recipe {:08X}", cobjFormId);
}

static std::atomic<bool> g_refreshQueued{ false };
static void QueueHelperRefresh()
{
    if (!g_open || g_refreshQueued.exchange(true)) return;
    F4SE::GetTaskInterface()->AddTask([]() {
        g_refreshQueued.store(false);
        if (g_open) PushHelperData();
    });
}

class WorkshopItemDestroyedSink : public RE::BSTEventSink<RE::Workshop::ItemDestroyedEvent>
{
public:
    static WorkshopItemDestroyedSink* GetSingleton() { static WorkshopItemDestroyedSink s; return &s; }
    RE::BSEventNotifyControl ProcessEvent(const RE::Workshop::ItemDestroyedEvent& a_event,
                                          RE::BSTEventSource<RE::Workshop::ItemDestroyedEvent>*) override
    {

        auto* destroyed = a_event.objectDestroyed.get();
        logger::warn("PrismaCrafting: [DESTROYED] engine destroyed {} | our ghostRef={} | lastPlaced={} | "
                     "placing={} | ghostBusy={}",
                     DescribeRef(destroyed), DescribeRef(g_ghostRef.Load()), DescribeRef(g_lastPlacedRef.Load()),
                     g_placing.load(), g_ghostBusy.load());
        QueueHelperRefresh();
        return RE::BSEventNotifyControl::kContinue;
    }
};

static void OnHelperDomReady(PrismaView )
{
    if (!g_api || g_helperView == 0) return;

    g_api->RegisterJSListener(g_helperView, "prisma_close", [](const char*) {
        OnMainThread(nullptr, [](const char*) { DismissHelper(); });
    });

    g_api->RegisterJSListener(g_helperView, "prisma_build_item", [](const char* arg) {
        OnMainThread(arg, [](const char* arg) { StartPlacement(arg);
        });
    });

    g_api->RegisterJSListener(g_helperView, "requestBuildables", [](const char*) {
        OnMainThread(nullptr, [](const char* arg) { PushHelperData();
        });
    });

    g_api->RegisterJSListener(g_helperView, "prisma_scrap", [](const char*) {
        OnMainThread(nullptr, [](const char*) {
            if (g_scrapOverlayOpen.load()) ConfirmScrap();
            else if (g_scrapSuppressLatch.load() <= 0) {
                if (g_placing.load()) {
                    ArmScrapAfterPlacementCancel();
                    CancelPlacementFromUI();
                } else {
                    BeginScrapConfirm();
                }
            }
        });
    });
    g_api->RegisterJSListener(g_helperView, "prisma_scrap_cancel", [](const char*) {
        OnMainThread(nullptr, [](const char*) { CancelScrapConfirm(); });
    });
    g_api->RegisterJSListener(g_helperView, "prisma_cancel", [](const char*) {
        OnMainThread(nullptr, [](const char*) { CancelPlacementFromUI(); });
    });

    g_api->RegisterJSListener(g_helperView, "prisma_tag", [](const char* arg) {
        OnMainThread(arg, [](const char* arg) {
            ToggleRecipeTagForSearch(arg ? static_cast<std::uint32_t>(std::strtoul(arg, nullptr, 16)) : 0);
        });
    });

    g_api->RegisterJSListener(g_helperView, "prisma_text_focus", [](const char* arg) {
        WorkshopInputHook::SetTextFocus(arg && arg[0] == '1');
    });

    g_api->RegisterJSListener(g_helperView, "prisma_panel_rects", [](const char* arg) {
        WorkshopInputHook::SetPanelRects(arg);
    });

    g_api->RegisterJSListener(g_helperView, "saveUiScale", [](const char* arg) {
        SaveScale("HelperScale", arg ? std::atoi(arg) : 100);
    });
    if (g_open) PushHelperData();

    {
        char sc[16]; std::snprintf(sc, sizeof(sc), "%d", LoadScale("HelperScale"));
        g_api->InteropCall(g_helperView, "setUiScale", sc);
    }

    g_api->InteropCall(g_helperView, "setTheme", LoadTheme().c_str());

    PushFreeState();
    WorkshopFreedom::SetToggleChangedCallback(&PushFreeState);

    if (g_open) {

        bool transferUp = false;
        if (auto* ui = RE::UI::GetSingleton()) {
            if (auto menu = ui->GetMenu("ContainerMenu"); menu && menu->uiMovie)
                transferUp = menu->uiMovie->GetVisible();
        }
        if (transferUp) {
            g_api->Hide(g_helperView);
            logger::info("PrismaCrafting: helper DOM ready -- NOT showing, vanilla transfer screen is up");
            return;
        }
        g_api->Show(g_helperView);
        logger::info("PrismaCrafting: helper DOM ready, listeners registered, shown (hidden={})",
                     g_api->IsHidden(g_helperView));
        return;
    }
    logger::info("PrismaCrafting: helper DOM ready, listeners registered");
}

static void CreateWorkshopHelperView()
{
    if (g_helperView != 0) return;
    if (!g_api) { logger::error("PrismaCrafting: PrismaUI API not available"); return; }
    try {
        g_helperView = g_api->CreateView("workshop.html", OnHelperDomReady);
        g_api->SetViewOwnsEscape(g_helperView, true);

        MarkViewAsPanel(g_helperView);
        g_api->RegisterConsoleCallback(g_helperView,
            [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel level, const char* msg) {
                const char* tag = (level == PRISMA_UI_API::ConsoleMessageLevel::Error)   ? "[JS ERR] " :
                                  (level == PRISMA_UI_API::ConsoleMessageLevel::Warning) ? "[JS WARN]" :
                                                                                           "[JS LOG] ";
                logger::info("{} {}", tag, msg);
            });
        g_api->Hide(g_helperView);
        logger::info("PrismaCrafting: helper view created (id={})", g_helperView.load());
    } catch (const std::exception& e) {
        logger::error("PrismaCrafting: CreateWorkshopHelperView failed: {}", e.what());
    }
}

class WorkshopMenuHook
{
public:

    friend void CancelPlacementFromUI();

    static void Install()
    {

        static bool s_installed = false;
        if (s_installed) return;
        s_installed = true;
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::WorkshopMenu[0] };
        _ProcessMessage = vtable.write_vfunc(0x3, &WorkshopMenuHook::ProcessMessage);
        _AdvanceMovie   = vtable.write_vfunc(0x4, &WorkshopMenuHook::AdvanceMovie);
        logger::info("PrismaCrafting: WorkshopMenu vtable hooked (slots 3/4 -- placement engine)");
    }

private:

    static void CancelPlacement()
    {
        g_placing.store(false);

        if (REX::FModule::IsRuntimeOG())
            if (auto* activeEdit = WSExt::ActiveEdit()) *activeEdit = false;
        WorkshopInputHook::SetActive(g_open);
        if (g_api && g_api->IsValid(g_helperView))
            g_api->InteropCall(g_helperView, "setPlacingMode", "0");
        g_placingFormId.store(0);
        g_placingRecipeId.store(0);
        if (auto* task = F4SE::GetTaskInterface()) task->AddTask([]() { DestroyGhostNow(); });
        logger::info("PrismaCrafting: placement cancelled (ghost destroyed on main thread)");
    }

    static RE::UI_MESSAGE_RESULTS ProcessMessage(RE::WorkshopMenu* a_this, RE::UIMessage& a_message)
    {
        if (a_message.type.get() == RE::UI_MESSAGE_TYPE::kUpdate) {
            if (const int ticks = g_scrapSuppressLatch.load(); ticks > 0)
                g_scrapSuppressLatch.store(ticks - 1);

            if (g_open && g_api && g_api->IsValid(g_helperView)) {
                const bool placing = g_placing.load();
                const bool overlay = g_scrapOverlayOpen.load();

                const bool tabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
                if (tabDown && !s_tab) {
                    s_tab = true;
                    if (overlay) CancelScrapConfirm();
                    else if (placing) CancelPlacement();
                    else RE::Workshop::RequestExitWorkshop(false);
                } else if (!tabDown) { s_tab = false; }

                const bool rDown = (GetAsyncKeyState('R') & 0x8000) != 0;
                if (rDown && !s_scrap) {
                    s_scrap = true;
                    logger::info("PrismaCrafting: R down (placing={}, focused={}, overlay={})",
                                 placing, WorkshopInputHook::IsFocused(), overlay);
                    if (overlay) ConfirmScrap();
                    else if (placing && !WorkshopInputHook::IsFocused()) {
                        ArmScrapAfterPlacementCancel();
                        CancelPlacement();
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    } else if (!WorkshopInputHook::IsFocused())
                        BeginScrapConfirm();
                } else if (!rDown) { s_scrap = false; }

                const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_esc) {
                    s_esc = true;
                    if (overlay) CancelScrapConfirm();
                    else if (placing) CancelPlacement();
                } else if (!escDown) { s_esc = false; }
            }
        }
        return _ProcessMessage(a_this, a_message);
    }

    static void SyncHelperVsTransferScreen()
    {
        if (!g_api || !g_api->IsValid(g_helperView)) return;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;

        bool movieDrawn = false;
        if (auto menu = ui->GetMenu("ContainerMenu"); menu && menu->uiMovie)
            movieDrawn = menu->uiMovie->GetVisible();

        const bool transferVisible = movieDrawn;

        const bool actuallyHidden = g_api->IsHidden(g_helperView);
        if (transferVisible == actuallyHidden) return;

        if (auto* task = F4SE::GetTaskInterface()) task->AddTask([transferVisible]() {
            if (!g_api || !g_api->IsValid(g_helperView)) return;
            if (transferVisible) {
                logger::info("PrismaCrafting: [TRANSFER-SYNC] transfer screen up -- hiding helper");
                g_api->Hide(g_helperView);
                WorkshopInputHook::SetActive(false);
            } else {
                logger::info("PrismaCrafting: [TRANSFER-SYNC] transfer screen down -- restoring helper");
                g_api->Show(g_helperView);
                WorkshopInputHook::SetActive(true);
            }
        });
    }

    static void AdvanceMovie(RE::WorkshopMenu* a_this, float a_timeDelta, std::uint64_t a_time)
    {
        _AdvanceMovie(a_this, a_timeDelta, a_time);
        WorkshopFreedom::Tick();
        if (!g_open) return;

        SyncHelperVsTransferScreen();
        TickScrapAfterPlacementCancel();

        if (g_placing.load()) {

            static int s_nullFrames = 0;
            if (g_ghostBusy.load()) {
                s_nullFrames = 0;
            } else {
                auto* handle = RE::Workshop::GetPlacementItem();
                if (!handle || !handle->get()) {
                    if (++s_nullFrames >= 2) {
                        s_nullFrames = 0;
                        if (g_placing.exchange(false)) OnGhostCommitted();
                    }
                } else {
                    s_nullFrames = 0;
                }
            }
        } else {
            for (int i = 0; i < 4; ++i) a_this->item3DPositions[i].clear();
        }
    }

    static inline REL::Relocation<decltype(&WorkshopMenuHook::ProcessMessage)> _ProcessMessage;
    static inline REL::Relocation<decltype(&WorkshopMenuHook::AdvanceMovie)>   _AdvanceMovie;
    static inline bool s_tab = false, s_esc = false, s_scrap = false;
};

static void CancelPlacementFromUI()
{
    if (g_placing.load()) WorkshopMenuHook::CancelPlacement();
}


class MenuOpenCloseHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static MenuOpenCloseHandler* GetSingleton()
    {
        static MenuOpenCloseHandler singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {

        logger::debug("MenuOpenClose: name='{}' opening={}", a_event.menuName.c_str(), a_event.opening);

        if (a_event.menuName == "ContainerMenu" && g_api) {
            const bool opening = a_event.opening;

            if (opening || g_open) {

                if (auto* task = F4SE::GetTaskInterface()) task->AddTask([opening]() {
                    if (!g_api) return;
                    if (opening) {
                        if (g_api->IsValid(g_helperView)) {
                            logger::info("PrismaCrafting: ContainerMenu opened (Transfer) -- hiding settlement helper");
                            g_api->Hide(g_helperView);
                        }
                        WorkshopInputHook::SetActive(false);
                    } else {

                        if (!g_open.load()) return;
                        if (g_api->IsValid(g_helperView)) {
                            logger::info("PrismaCrafting: ContainerMenu closed -- restoring settlement helper");
                            g_api->Show(g_helperView);
                        }
                        WorkshopInputHook::SetActive(true);
                    }
                });
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event.menuName == "WorkshopMenu") {
            if (a_event.opening) {
                SetMenuName(a_event.menuName);
                g_handingOff = false;
                g_open       = true;
                logger::info("PrismaCrafting: WorkshopMenu opening — build helper");
                LogMemory("workshop OPEN");
                WorkshopInputHook::SetActive(false);

                if (!REX::FModule::IsRuntimeOG()) {
                    static bool s_warnedNg = false;
                    if (!s_warnedNg) {
                        s_warnedNg = true;
                        logger::warn("PrismaCrafting: runtime is not Old-Gen 1.10.163 -- the settlement build "
                                     "dock is OG-only, leaving the VANILLA workshop UI active so building "
                                     "still works.");
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (g_api) {
                    g_placing.store(false);
                    g_api->SuppressVanillaMenu("WorkshopMenu", true);
                    g_api->SuppressVanillaMenu("PromptMenu", true);
                    g_api->SuppressVanillaMenu("ButtonBarMenu", true);
                }

                OnMainThread(nullptr, [](const char*) {
                if (!g_api || !g_api->IsValid(g_helperView)) {
                    g_helperView = 0;
                    CreateWorkshopHelperView();
                } else {
                    OnHelperDomReady(g_helperView);
                }
                if (g_api && g_api->IsValid(g_helperView)) {


                    bool transferUp = false;
                    if (auto* ui = RE::UI::GetSingleton()) {
                        if (auto menu = ui->GetMenu("ContainerMenu"); menu && menu->uiMovie)
                            transferUp = menu->uiMovie->GetVisible();
                    }
                    if (transferUp) {
                        logger::info("PrismaCrafting: WorkshopMenu opened while the transfer screen is "
                                     "drawn -- helper stays hidden until it closes");
                        g_api->Hide(g_helperView);
                        WorkshopInputHook::SetActive(false);
                    } else {
                        g_api->Show(g_helperView);

                        WorkshopInputHook::SetActive(true);
                        WorkshopFreedom::SetWorkshopActive(true);
                    }
                }
                });
            } else {
                logger::info("PrismaCrafting: WorkshopMenu closing");
                LogMemory("workshop CLOSE (pre)");
                WorkshopFreedom::SetWorkshopActive(false);
                g_open = false;
                HideScrapOverlay();
                ClearScrapAfterPlacementCancel();
                const bool wasPlacing = g_placing.exchange(false);
                g_placementStartQueued.store(false);
                g_placingFormId.store(0);
                g_placingRecipeId.store(0);
                g_lastPlacedRef.Store({});
                if (wasPlacing) {

                    if (auto* task = F4SE::GetTaskInterface())
                        task->AddTask([]() { DestroyGhostNow(); });
                }

                if (g_api) {
                    g_api->SuppressVanillaMenu("WorkshopMenu", false);
                    g_api->SuppressVanillaMenu("PromptMenu", false);
                    g_api->SuppressVanillaMenu("ButtonBarMenu", false);
                }
                WorkshopInputHook::OnClosed();
                RestoreInputAfterClose();

                if (g_api && g_api->IsValid(g_helperView)) {
                    g_api->Unfocus(g_helperView); g_api->Hide(g_helperView);
                }
                DestroyAllViews();
            LogMemory("workshop CLOSE (post)");
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!IsCraftMenu(a_event.menuName))
            return RE::BSEventNotifyControl::kContinue;

        if (a_event.opening) {
            SetMenuName(a_event.menuName);
            g_handingOff = false;

            g_cookingBench.store(a_event.menuName == "CookingMenu" &&
                                 OpenBenchHasKeyword(kKW_WorkbenchCooking));

            logger::info("PrismaCrafting: {} opening (craft terminal)", MenuName().c_str());
            LogMemory("bench OPEN");

            if (auto* ui = RE::UI::GetSingleton()) {
                if (auto nativeMenu = ui->GetMenu(a_event.menuName); nativeMenu) {
                    nativeMenu->UpdateFlag(RE::UI_MENU_FLAGS::kRequiresUpdate, false);
                    if (nativeMenu->uiMovie) nativeMenu->uiMovie->SetVisible(false);
                    logger::debug("PrismaCrafting: native SWF suppressed");
                }
            }

            if (g_api) g_api->SuppressVanillaMenu("PromptMenu", true);
            if (g_api) g_api->SuppressVanillaMenu("ButtonBarMenu", true);

            if (g_api) g_api->SuppressVanillaMenu("PauseMenu", true);

            g_open = true;

            OnMainThread(nullptr, [](const char*) {
            auto av = ActiveView();
            const bool reusing = (g_api && g_api->IsValid(av));
            if (!reusing) {
                g_view = 0;
                CreateCraftingView();
                av = ActiveView();

                if (g_api && g_api->IsValid(av)) g_api->Show(av);
            } else {

                if (g_api && g_api->IsValid(av)) {
                    g_api->Show(av);
                    g_api->Focus(av, true, false);
                    if (auto* ui = RE::UI::GetSingleton())
                        if (auto menu = ui->GetMenu(MenuName()); menu && menu->uiMovie)
                            menu->uiMovie->SetVisible(false);
                }
                PushRecipesForCurrentBench();
            }
            logger::info("PrismaCrafting: {} menu-open handler done (reusing={})",
                MenuName().c_str(), reusing);
            });
        } else {
            logger::info("PrismaCrafting: {} closing", a_event.menuName.c_str());
            LogMemory("bench CLOSE (pre)");
            g_open = false;
            if (!g_handingOff && g_api) {
                auto av = ActiveView();
                if (g_api->IsValid(av)) { g_api->Unfocus(av); g_api->Hide(av); }
            }
            g_handingOff = false;
            if (g_api) g_api->SuppressVanillaMenu("PromptMenu", false);
            if (g_api) g_api->SuppressVanillaMenu("ButtonBarMenu", false);
            if (g_api) g_api->SuppressVanillaMenu("PauseMenu", false);
            RestoreInputAfterClose();

            if (auto* ui = RE::UI::GetSingleton()) {
                if (auto menu = ui->GetMenu(MenuName()); menu) {
                    menu->UpdateFlag(RE::UI_MENU_FLAGS::kRequiresUpdate, true);
                    if (menu->uiMovie) menu->uiMovie->SetVisible(true);
                }
            }
            DestroyAllViews();
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

static void CoSave_Save(const F4SE::SerializationInterface* intfc)
{
    WorkbenchUpgrades::CoSaveWrite(intfc);
}
static void CoSave_Load(const F4SE::SerializationInterface* intfc)
{
    logger::info("PrismaCrafting: co-save LOAD begin");
    std::uint32_t type = 0, ver = 0, len = 0;
    int n = 0;
    while (intfc->GetNextRecordInfo(type, ver, len)) {
        char tag[5] = { char(type & 0xFF), char((type >> 8) & 0xFF), char((type >> 16) & 0xFF), char((type >> 24) & 0xFF), 0 };
        logger::info("PrismaCrafting: co-save LOAD record '{}' (ver={} len={})", tag, ver, len);
        if (WorkbenchUpgrades::CoSaveLoadRecord(type, ver, len, intfc)) { ++n; continue; }

    }
    logger::info("PrismaCrafting: co-save LOAD end ({} records handled)", n);
}
static void CoSave_Revert(const F4SE::SerializationInterface*)
{
    logger::info("PrismaCrafting: co-save REVERT begin");
    WorkbenchUpgrades::CoSaveRevert();
    logger::info("PrismaCrafting: co-save REVERT end");
}
static void RegisterCoSave()
{
    const auto* ser = F4SE::GetSerializationInterface();
    if (!ser) { logger::error("PrismaCrafting: no serialization interface"); return; }
    ser->SetUniqueID('PCTB');
    ser->SetSaveCallback(CoSave_Save);
    ser->SetLoadCallback(CoSave_Load);
    ser->SetRevertCallback(CoSave_Revert);
    logger::info("PrismaCrafting: co-save registered (workbench upgrades)");
}


static void F4SEMessageHandler(F4SE::MessagingInterface::Message* message)
{
    logger::debug("F4SE message: type={}", static_cast<int>(message->type));
    switch (message->type) {
    case F4SE::MessagingInterface::kGameDataReady: {
        g_api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI9>();
        if (!g_api)
            logger::error("PrismaCrafting: failed to get PrismaUI API (V9) — PrismaUI_F4 must load first");
        else
            logger::info("PrismaCrafting: PrismaUI API acquired");

        g_api10 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>();
        logger::info("PrismaCrafting: PrismaUI V10 {} (view roles {})",
                     g_api10 ? "available" : "not available",
                     g_api10 ? "declared" : "cannot be declared");

        if (g_api)
            g_api->SuppressVanillaMenuIf("ExamineConfirmMenu", []() -> bool {
                return (g_open.load() && IsCraftMenu(MenuName()))
                    || g_scrapOverlayOpen.load()
                    || g_scrapAfterPlacementCancelTicks.load() > 0
                    || g_scrapSuppressLatch.load() > 0;
            });

        WorkshopInputHook::Init(&g_api, &g_helperView);

        if (auto* task = F4SE::GetTaskInterface()) {
            task->AddTask([]() { WorkshopInputHook::Install(); });

            task->AddTask([]() { CobjCache::Build(); });

            task->AddTask([]() { WorkbenchUpgrades::Init(); });
        } else {
            WorkshopInputHook::Install();
            CobjCache::Build();
            WorkbenchUpgrades::Init();
        }
        WorkshopFreedom::Init();
        WorkshopFreedom::InstallHooks();
        WorkshopFreedom::SetPreUpdateHook(&PumpDeferredScrap);

        RegisterCoSave();
        break;
    }
    case F4SE::MessagingInterface::kPostLoadGame:
    case F4SE::MessagingInterface::kNewGame: {

        static bool s_sinksRegistered = false;
        if (!s_sinksRegistered) {
            s_sinksRegistered = true;
            if (auto* ui = RE::UI::GetSingleton()) {
                ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(MenuOpenCloseHandler::GetSingleton());
                logger::info("PrismaCrafting: MenuOpenCloseEvent sink registered");
            }

            RE::Workshop::RegisterForItemPlaced(WorkshopItemPlacedSink::GetSingleton());
            logger::info("PrismaCrafting: Workshop::ItemPlacedEvent sink registered (placement restore)");

            RE::Workshop::RegisterForItemDestroyed(WorkshopItemDestroyedSink::GetSingleton());
            logger::info("PrismaCrafting: Workshop::ItemDestroyedEvent sink registered (scrap refresh)");

            WorkshopMenuHook::Install();
        }

        if (g_api) {
            if (g_view == 0) CreateCraftingView();
        }
        break;
    }
    }
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{

    bool debugOn = false;
    {
        CSimpleIniA ini; ini.SetUnicode();
        ini.LoadFile(SettingsIniPath().c_str());
        debugOn = ini.GetBoolValue("Debug", "Enabled", false);
    }

    const auto level = debugOn ? spdlog::level::debug : spdlog::level::info;
    auto path = Paths::F4SEUserDir() / "PrismaCrafting.log";
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
    auto log  = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
    log->set_level(level);
    log->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] [%t] [%s:%#] %v");

    logger::info("PrismaCrafting v1.0.0 — debug logging {} — log: {}",
        debugOn ? "ON" : "off", path.string());

    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name        = "PrismaCrafting";
    a_info->version     = 1;

    if (a_f4se->IsEditor()) {
        logger::critical("Loaded in editor — aborting.");
        return false;
    }
    if (a_f4se->RuntimeVersion() < F4SE::RUNTIME_1_10_162) {
        logger::critical("Unsupported runtime");
        return false;
    }
    return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    g_mainThreadId = std::this_thread::get_id();

    F4SE::Init(a_f4se, F4SE::InitInfo{ .log = false });

    const auto* messaging = F4SE::GetMessagingInterface();
    if (!messaging) {
        logger::critical("PrismaCrafting: failed to get F4SE messaging interface");
        return false;
    }
    messaging->RegisterListener(F4SEMessageHandler);
    return true;
}

// Constraint: craft admission keeps current menu keywords (source:src/CobjCache.h:CraftRecipe) and payload formats (source:src/main/CraftingView.cpp at e19e520).
#include "RuntimeInternal.h"
#include <nlohmann/json.hpp>

void PushRecipesForCurrentBench()
{
    InvalidateCraftPageQuery();
    auto av = ActiveView();
    if (!g_api || !g_api->IsValid(av)) return;

    CobjCache::Build();

    g_benchIds.clear();
    if (auto* ui = RE::UI::GetSingleton()) {
        if (auto menu = ui->GetMenu(MenuName()))
            g_benchIds = GetBenchKeywordIdsForMenu(menu.get());
    }

    g_api->InteropCall(av, "setBenchMode", g_cookingBench.load() ? "cooking" : "craft");

    char caps[32];
    std::snprintf(caps, sizeof(caps), "%u", CobjCache::PlayerCaps());
    g_api->InteropCall(av, "setCaps", caps);
    g_api->InteropCall(av, "setUnlockAll", WorkbenchUpgrades::UnlockAllBenches() ? "1" : "0");

    g_api->InteropCall(av, "benchReady", CraftPageEpoch().c_str());

    logger::info("PrismaCrafting: bench ready for {} ({} bench keyword(s))",
        MenuName().c_str(), g_benchIds.size());

    CobjCache::ExportCurrentView(g_benchIds);
}

static void PushWorkbenches()
{
    if (!g_api || !g_api->IsValid(g_view)) return;

    WorkbenchUpgrades::Poll();
    auto sources = GetBenchSourceRefs();
    const CobjCache::InventorySnapshot inv =
        CobjCache::InventorySnapshot::Build(RE::PlayerCharacter::GetSingleton(), sources);
    const uint32_t caps = CobjCache::PlayerCaps();
    const uint64_t now  = WorkbenchUpgrades::NowEpoch();
    logger::info("PushWorkbenches: caps={} benchSourceRefs={} (menu='{}')", caps, sources.size(), MenuName().c_str());

    using Json = nlohmann::ordered_json;
    Json payload = {{"now", now}, {"caps", caps}, {"benches", Json::array()}};
    for (const auto& b : WorkbenchUpgrades::Benches()) {
        auto st = WorkbenchUpgrades::GetState(b.id);
        const int  maxLvl    = static_cast<int>(b.levels.size());
        const bool upgrading = st.upgradeCompleteAt != 0;

        Json bench = {{"id", b.id}, {"name", b.name}, {"desc", b.desc}, {"nif", b.nif},
                      {"level", st.level}, {"maxLevel", maxLvl}, {"upgrading", upgrading},
                      {"completeAt", st.upgradeCompleteAt}, {"levels", Json::array()}};
        for (size_t i = 0; i < b.levels.size(); ++i) {
            const auto& L = b.levels[i];
            bench["levels"].push_back({{"level", L.level}, {"bonus", L.bonus}, {"earned", st.level >= L.level}});
        }

        if (st.level < maxLvl) {
            const auto& L = b.levels[st.level];
            bool ready = caps >= L.caps;
            Json mats = Json::array();
            for (size_t i = 0; i < L.cost.size(); ++i) {
                const auto& c = L.cost[i];
                uint32_t have       = c.form ? inv.Available(c.form) : 0;
                uint32_t playerOnly = c.form ? inv.Carried(c.form)   : 0;

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
                mats.push_back({{"name", c.name}, {"need", c.count}, {"have", have}, {"nif", nif}});
            }
            bench["next"] = {{"level", L.level}, {"timeSec", L.timeSec}, {"caps", L.caps}, {"capsHave", caps},
                             {"bonus", L.bonus}, {"ready", ready}, {"mats", std::move(mats)}};
        } else {
            bench["next"] = nullptr;
        }
        payload["benches"].push_back(std::move(bench));
    }
    const auto j = payload.dump(-1, ' ', false, Json::error_handler_t::replace);
    g_api->InteropCall(g_view, "setWorkbenches", j.c_str());
}

void PushControllerPrompts(PrismaView view)
{
    WorkshopControllerInput::PushCraftingPrompts(view);
}

void SetCraftVanillaSuppressed(bool suppressed)
{
    if (auto* ui = RE::UI::GetSingleton()) {
        if (auto menu = ui->GetMenu(MenuName()); menu) {
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kRequiresUpdate, !suppressed);
            if (menu->uiMovie) menu->uiMovie->SetVisible(!suppressed);
        }
    }
    if (!g_api) return;
    g_api->SuppressVanillaMenu("PromptMenu", suppressed);
    g_api->SuppressVanillaMenu("ButtonBarMenu", suppressed);
    g_api->SuppressVanillaMenu("PauseMenu", suppressed);
}

static std::atomic<std::uint32_t> g_pendingCraftAbortGeneration{ 0 };

bool HasPendingCraftAbort()
{
    auto pending = g_pendingCraftAbortGeneration.load(std::memory_order_acquire);
    if (!pending) return false;
    if (CraftSessionMatches(pending)) return true;
    g_pendingCraftAbortGeneration.compare_exchange_strong(pending, 0, std::memory_order_acq_rel);
    return g_pendingCraftAbortGeneration.load(std::memory_order_acquire) != 0;
}

void AbortCraftReplacement(const char* reason, std::uint32_t generation)
{
    if (!CraftSessionMatches(generation)) return;
    if (!g_api || !g_api->IsGameThread()) {
        auto pending = g_pendingCraftAbortGeneration.load(std::memory_order_acquire);
        for (;;) {
            if (pending == generation || !CraftSessionMatches(generation)) return;
            if (pending && CraftSessionMatches(pending)) return;
            if (g_pendingCraftAbortGeneration.compare_exchange_weak(
                    pending, generation, std::memory_order_acq_rel, std::memory_order_acquire)) break;
        }
        logger::critical("PrismaCrafting: craft replacement abort deferred until game thread: {}",
                         reason ? reason : "unknown failure");
        RetryCraftRecovery();
        return;
    }
    auto pending = generation;
    g_pendingCraftAbortGeneration.compare_exchange_strong(pending, 0, std::memory_order_acq_rel);
    InvalidateCraftPageQuery();
    logger::critical("PrismaCrafting: craft replacement aborted: {}", reason ? reason : "unknown failure");
    g_open.store(false, std::memory_order_release);
    if (g_api && g_api->IsValid(ActiveView())) {
        g_api->Unfocus(ActiveView());
        g_api->Hide(ActiveView());
    }
    SetCraftVanillaSuppressed(false);
}

void ServicePendingCraftAbort()
{
    if (!g_api || !g_api->IsGameThread() || !HasPendingCraftAbort()) return;
    AbortCraftReplacement("deferred game-thread failure",
                          g_pendingCraftAbortGeneration.load(std::memory_order_acquire));
}

bool ActivateCraftReplacement(PrismaView av)
{
    if (!CraftSessionActive() || !g_craftingControlsReady.load() || !g_api || !g_api->IsValid(av)) return false;
    g_api->Show(av);
    if (!g_api->Focus(av, true, false)) {
        AbortCraftReplacement("crafting view focus rejected", g_craftMenuGeneration.load(std::memory_order_acquire));
        return false;
    }
    WorkshopControllerInput::NoteViewFocused(av);
    SetCraftVanillaSuppressed(true);
    return true;
}

static std::atomic<PrismaView> g_failedCraftBindingsView{ 0 };

static void PrepareCraftingView(PrismaView view, bool dispatchBootstrap)
{
    logger::info("PrismaCrafting: OnDomReady ENTER (view={}, open={})", view, g_open.load());
    if (!g_api || view == 0) return;
    const auto craftGen = g_craftMenuGeneration.load(std::memory_order_acquire);
    g_craftingControlsReady.store(false, std::memory_order_release);
    bool controlsReady = true;

    if (!BindGameThreadUIEvent(view, "prisma_close", [](const char*, void*) {
        CloseCrafting();
    })) controlsReady = false;

    if (!BindGameThreadUIEvent(view, "notePrompts", [](const char* arg, void*) {
        if (!g_api) return;
        const auto v = g_view.load();
        if (!g_api->IsValid(v)) return;
        if (arg && arg[0]) g_api->NoteInputDevice(ParseIntOr(arg, 0));
        PushControllerPrompts(v);
    })) controlsReady = false;

    if (!BindGameThreadUIEvent(view, "prisma_refresh", [](const char*, void*) {
        if (!CraftSessionActive()) return;
        PushRecipesForCurrentBench();
    })) controlsReady = false;

    if (!BindGameThreadUIEvent(view, "requestPage", [](const char* arg, void*) {
        RequestCraftingPage(arg);
    })) controlsReady = false;

    if (!BindGameThreadUIEvent(view, "requestWorkbenches", [](const char*, void*) {
        if (!CraftSessionActive()) return;
        PushWorkbenches();
    })) controlsReady = false;

    if (!BindGameThreadUIEvent(view, "startUpgrade", [](const char* arg, void*) {
        if (!CraftSessionActive()) return;
        if (!arg || !arg[0]) return;
        std::string id(arg);

        while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back();
        std::string reason;
        bool ok = WorkbenchUpgrades::StartUpgrade(id, GetBenchSourceRefs(), &reason);
        logger::info("startUpgrade('{}') -> {} ({})", id, ok ? "OK" : "REJECT", reason);
        PushWorkbenches();
        if (ok) PushRecipesForCurrentBench();
    })) controlsReady = false;

    if (!BindGameThreadUIEvent(view, "requestDesc", [](const char* arg, void*) {
        if (!CraftSessionActive()) return;
        auto av = ActiveView();
        if (!g_api || !g_api->IsValid(av) || !arg || !arg[0]) return;
        uint32_t formRef = ParseHexOr(arg, 0);
        if (!formRef) return;
        std::string desc = CobjCache::GetRecipeDesc(formRef);
        char hdr[16]; std::snprintf(hdr, sizeof(hdr), "%08X\t", formRef);
        g_api->InteropCall(av, "setDesc", (std::string(hdr) + desc).c_str());
    })) controlsReady = false;

    if (!BindGameThreadUIEvent(view, "prisma_craft", [](const char* arg, void*) {
        if (!CraftSessionActive()) return;
        if (!arg || !arg[0]) { logger::debug("prisma_craft: empty arg, ignored"); return; }
        const std::string_view request(arg);
        const auto comma = request.find(',');
        uint32_t formRef = ParseHexOr(request.substr(0, comma), 0);
        if (!formRef) { logger::debug("prisma_craft: arg '{}' parsed to formRef 0, ignored", arg); return; }
        int qty = comma == std::string_view::npos ? 1 : ParseIntOr(request.substr(comma + 1), 0);
        if (qty < 1) { logger::debug("prisma_craft: invalid quantity in '{}', ignored", arg); return; }
        if (qty > 999) qty = 999;
        logger::debug("prisma_craft: request formRef={:08X} qty={} cheat={}", formRef, qty, g_cheatMode.load());
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;
        auto menu = ui->GetMenu(MenuName());
        if (!menu) return;
        const auto benchIds = GetBenchKeywordIdsForMenu(menu.get());
        int made = CobjCache::CraftRecipe(formRef, g_cheatMode, GetBenchSourceRefs(), qty,
                                          benchIds, g_cookingBench.load());
        logger::info("PrismaCrafting: craft {:08X} x{} (cheat={}) -> made {}", formRef, qty, g_cheatMode.load(), made);
        if (made > 0) {

            auto av = ActiveView();
            if (g_api && g_api->IsValid(av)) {
                char b[32]; std::snprintf(b, sizeof(b), "%08X,%d", formRef, made);
                g_api->InteropCall(av, "setPostCraft", b);
            }
            PushRecipesForCurrentBench();
        }
    })) controlsReady = false;

    g_api->RegisterJSListener(view, "prisma_cheat", [](const char* arg) {
        g_cheatMode = (arg && arg[0] == '1');
        logger::info("PrismaCrafting: cheat mode = {}", g_cheatMode.load());
    });

    g_api->RegisterJSListener(view, "saveTheme", [](const char* arg) {
        SaveTheme(arg ? arg : "tarkov");
        logger::info("PrismaCrafting: theme = {}", arg ? arg : "tarkov");
    });
    g_api->RegisterJSListener(view, "saveUiScale", [](const char* arg) {
        SaveScale("CraftScale", arg ? ParseIntOr(arg, 100) : 100);
    });

    if (!BindGameThreadUIEvent(view, "prisma_loaded", [](const char*, void*) {
        auto av = ActiveView();
        if (!ActivateCraftReplacement(av)) return;
        logger::info("PrismaCrafting: {} view activated post-load (typing enabled)", MenuName().c_str());
    })) controlsReady = false;

    if (!controlsReady) {
        g_failedCraftBindingsView.store(view, std::memory_order_release);
        logger::critical("PrismaCrafting: crafting control-plane binding failed; view will remain inactive");
        if (CraftSessionMatches(craftGen)) {
            AbortCraftReplacement("required control binding unavailable", craftGen);
        } else if (!g_open.load(std::memory_order_acquire)) {
            g_api->Hide(view);
        }
        return;
    }
    auto failedView = view;
    g_failedCraftBindingsView.compare_exchange_strong(failedView, 0, std::memory_order_acq_rel);
    g_craftingControlsReady.store(true, std::memory_order_release);

    if (PrismaFlatApi::RegisterTranslations(view)) g_api->InteropCall(view, "applyTranslations", "");

    {
        char sc[16]; std::snprintf(sc, sizeof(sc), "%d", LoadScale("CraftScale"));
        g_api->InteropCall(view, "setUiScale", sc);
        g_api->InteropCall(view, "setTheme", LoadTheme().c_str());
    }

    g_api->SetScrollingPixelSize(view, 100);

    if (dispatchBootstrap && CraftSessionActive() && !PrismaCraftingThreading::AddGameTask([view, craftGen] {
            if (!g_api || !g_api->IsGameThread() || g_view.load() != view ||
                !CraftSessionMatches(craftGen) || !CraftSessionActive()) return;
            PushRecipesForCurrentBench();
        })) {
        logger::critical("PrismaCrafting: crafting DOM-ready bootstrap was rejected; view will remain inactive");
        AbortCraftReplacement("crafting DOM-ready bootstrap dispatch rejected", craftGen);
        return;
    }

    WorkshopControllerInput::ActivateCrafting();
    logger::info("PrismaCrafting: DOM ready, listeners registered (view={})", view);
}

static void OnDomReady(PrismaView view)
{
    PrepareCraftingView(view, true);
}

bool RetryCraftingControls(PrismaView view)
{
    if (!g_api || !g_api->IsGameThread() || !CraftSessionActive() || view != ActiveView() ||
        !g_api->IsValid(view) || g_failedCraftBindingsView.load(std::memory_order_acquire) != view)
        return false;
    const auto generation = g_craftMenuGeneration.load(std::memory_order_acquire);
    PrepareCraftingView(view, false);
    return CraftSessionMatches(generation) && CraftSessionActive() && view == ActiveView() &&
           g_craftingControlsReady.load(std::memory_order_acquire);
}

void CreateCraftingView()
{
    if (g_view != 0) return;
    if (!g_api) {
        logger::error("PrismaCrafting: PrismaUI API not available");
        return;
    }
    try {
        g_failedCraftBindingsView.store(0, std::memory_order_release);
        g_cheatMode.store(false, std::memory_order_release);
        g_view = g_api->CreateView("crafting.html", OnDomReady);

        g_api->SetViewOwnsEscape(g_view, true);
        MarkViewAsPanel(g_view);
        if (!WorkshopControllerInput::InstallCrafting(&g_view))
            logger::warn("PrismaCrafting: crafting view created without controller bindings");

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

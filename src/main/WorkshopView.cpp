#include "RuntimeInternal.h"
#include <nlohmann/json.hpp>

static void PushFreeState()
{
    if (!g_api || !g_api->IsValid(g_helperView)) return;
    const auto json = nlohmann::ordered_json{
        {"place", WorkshopFreedom::FreePlaceEnabled() ? 1 : 0},
        {"cam", WorkshopFreedom::FreeCameraEnabled() ? 1 : 0},
        {"gsnap", WorkshopFreedom::GroundSnapEnabled() ? 1 : 0},
        {"osnap", WorkshopFreedom::ObjectSnapEnabled() ? 1 : 0}}.dump();
    g_api->InteropCall(g_helperView, "setFreeState", json.c_str());
}

static bool PushHudNotice(const char* payload)
{
    if (!g_api || !g_api->IsValid(g_helperView) || g_api->IsHidden(g_helperView)) return false;
    g_api->InteropCall(g_helperView, "showHudNotice", payload);
    return true;
}

static void DismissHelper()
{
    if (!WorkshopSessionActive(g_wsSession.load(std::memory_order_acquire))) return;
    logger::info("PrismaCrafting: workshop exit requested (RequestExitWorkshop)");
    RE::Workshop::RequestExitWorkshop(false);
}

static bool QueueHelperDomReadyBootstrap(PrismaView view, bool controlsReady, std::uint32_t session)
{
    return PrismaCraftingThreading::AddGameTask([view, controlsReady, session] {
        if (!g_api || !g_api->IsGameThread() || g_helperView.load() != view || !WorkshopSessionActive(session)) return;
        if (!controlsReady) {
            AbortWorkshopReplacement("required control binding unavailable", session);
            return;
        }
        ActivateWorkshopReplacementIfReady("helper DOM ready");
    });
}

static void OnHelperDomReady(PrismaView view)
{
    if (!g_api || view == 0 || g_helperView.load() != view) return;
    const auto session = g_wsSession.load(std::memory_order_acquire);
    g_helperControlsReady.store(false, std::memory_order_release);
    bool controlsReady = true;

    if (!BindGameThreadUIEventSafe(view, "prisma_close", [](const char*, void*) {
        DismissHelper();
    })) controlsReady = false;

    if (!BindGameThreadUIEventSafe(view, "prisma_build_item", [](const char* arg, void*) {
        StartPlacement(arg);
    })) controlsReady = false;

    if (!BindGameThreadUIEventSafe(view, "requestBuildables", [](const char*, void*) {
        PushHelperData();
    })) controlsReady = false;

    if (!BindGameThreadUIEventSafe(view, "requestWorkshopPage", [](const char* arg, void*) {
        RequestWorkshopPage(arg);
    })) controlsReady = false;

    if (!BindGameThreadUIEventSafe(view, "prisma_scrap", [](const char*, void*) {
        if (g_scrapOverlayOpen.load()) ConfirmScrap();
        else if (g_scrapSuppressLatch.load() <= 0) {
            if (g_placing.load()) {
                ArmScrapAfterPlacementCancel();
                CancelPlacementFromUI();
            } else {
                BeginScrapConfirm();
            }
        }
    })) controlsReady = false;
    if (!BindGameThreadUIEventSafe(view, "prisma_scrap_cancel", [](const char*, void*) {
        CancelScrapConfirm();
    })) controlsReady = false;
    if (!BindGameThreadUIEventSafe(view, "prisma_cancel", [](const char*, void*) {
        CancelPlacementFromUI();
    })) controlsReady = false;

    if (!BindGameThreadUIEventSafe(view, "prisma_freedom", [](const char* arg, void*) {
        RunFreedomCommand(arg ? arg : "");
    })) controlsReady = false;

    if (!BindGameThreadUIEventSafe(view, "prisma_tag", [](const char* arg, void*) {
        if (!arg || !arg[0]) return;
        const std::string_view request(arg);
        const auto comma = request.find(',');
        const auto recipeId = ParseHexOr(request.substr(0, comma), 0);
        if (!recipeId) return;
        std::optional<bool> desired;
        if (comma != std::string_view::npos) {
            const auto state = request.substr(comma + 1);
            if (state == "1") desired = true;
            else if (state == "0") desired = false;
        }
        ToggleRecipeTagForSearch(recipeId, desired);
    })) controlsReady = false;

    g_api->RegisterJSListener(g_helperView, "prisma_text_focus", SafeJSListener([](const char* arg) {
        WorkshopInputHook::SetTextFocus(arg && arg[0] == '1');
    }));

    g_api->RegisterJSListener(g_helperView, "prisma_panel_rects", SafeJSListener([](const char* arg) {
        WorkshopInputHook::SetPanelRects(arg);
    }));

    g_api->RegisterJSListener(g_helperView, "saveUiScale", SafeJSListener([](const char* arg) {
        SaveScale("HelperScale", arg ? ParseIntOr(arg, 100) : 100);
    }));

    if (!BindGameThreadUIEventSafe(view, "requestWorkshopStats", [](const char*, void*) {
        PushWorkshopStats();
    })) controlsReady = false;

    g_api->RegisterJSListener(g_helperView, "saveTheme", SafeJSListener([](const char* arg) {
        SaveTheme(arg ? arg : "tarkov");
    }));
    if (!controlsReady) {
        logger::critical("PrismaCrafting: workshop control-plane binding failed; helper will remain inactive");
        if (WorkshopSessionActive(session) && !QueueHelperDomReadyBootstrap(view, false, session) && WorkshopSessionActive(session)) {
            AbortWorkshopReplacement("control-binding rollback dispatch rejected", session);
        } else if (!WorkshopSessionActive(session)) {
            g_api->Hide(view);
        }
        return;
    }
    g_helperControlsReady.store(true, std::memory_order_release);

    if (PrismaFlatApi::RegisterTranslations(view)) g_api->InteropCall(view, "applyTranslations", "");

    {
        char sc[16]; std::snprintf(sc, sizeof(sc), "%d", LoadScale("HelperScale"));
        g_api->InteropCall(g_helperView, "setUiScale", sc);
    }

    g_api->InteropCall(g_helperView, "setTheme", LoadTheme().c_str());

    g_api->SetScrollingPixelSize(g_helperView, 100);

    PushFreeState();
    WorkshopFreedom::SetToggleChangedCallback(&PushFreeState);
    WorkshopFreedom::SetNoticeSink(&PushHudNotice);

    if (WorkshopSessionActive(session) && !QueueHelperDomReadyBootstrap(view, true, session) && WorkshopSessionActive(session)) {
        logger::critical("PrismaCrafting: workshop DOM-ready bootstrap was rejected; helper will remain inactive");
        AbortWorkshopReplacement("DOM-ready bootstrap dispatch rejected", session);
        return;
    }
    logger::info("PrismaCrafting: helper DOM ready, listeners registered");
}
void CreateWorkshopHelperView()
{
    if (g_helperView != 0) return;
    if (!g_api) { logger::error("PrismaCrafting: PrismaUI API not available"); return; }
    try {
        const auto view = g_api->CreateView("workshop.html", OnHelperDomReady);
        if (!view) {
            logger::error("PrismaCrafting: workshop helper view creation failed");
            return;
        }
        g_helperView = view;
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

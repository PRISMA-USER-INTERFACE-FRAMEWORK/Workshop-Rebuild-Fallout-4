// Constraint: moved native bindings must match source:src/main.cpp at 5fc7723.
#include "RuntimeInternal.h"

template <class F>
static bool OnMainThread(const char* a_arg, F&& a_fn)
{
    std::string arg(a_arg ? a_arg : "");
    if (!PrismaCraftingThreading::AddGameTask([arg, fn = std::forward<F>(a_fn)]() { fn(arg.c_str()); })) {
        logger::error("PrismaCrafting: main-thread task not queued (dispatch unavailable): {}", arg);
        return false;
    }
    return true;
}

static void QueueWorkshopTransferSync(bool transferVisible)
{
    const auto workshopSession = g_wsSession.load(std::memory_order_acquire);
    if (!workshopSession || !g_open.load() || MenuName() != "WorkshopMenu" ||
        !g_api || !g_api->IsValid(g_helperView) || !WorkshopCatalogDelivered()) return;
    if (!PrismaCraftingThreading::AddGameTask([transferVisible, workshopSession]() {
        if (!g_open.load() || MenuName() != "WorkshopMenu" ||
            g_wsSession.load(std::memory_order_acquire) != workshopSession ||
            !g_api || !g_api->IsValid(g_helperView) || !WorkshopCatalogDelivered()) return;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;
        const auto menu = ui->GetMenu("ContainerMenu");
        const bool drawn = menu && menu->uiMovie && menu->uiMovie->GetVisible();
        if (drawn != transferVisible) return;
        if (transferVisible) {
            logger::info("PrismaCrafting: [TRANSFER-SYNC] transfer screen up -- hiding helper");
            g_api->Hide(g_helperView);
            WorkshopInputHook::SetActive(false);
            WorkshopFreedom::SetWorkshopActive(false);
        } else {
            logger::info("PrismaCrafting: [TRANSFER-SYNC] transfer screen down -- restoring helper");
            g_api->Show(g_helperView);
            WorkshopInputHook::SetActive(true);
            WorkshopFreedom::SetWorkshopActive(true);
        }
    }))
        logger::warn("PrismaCrafting: [TRANSFER-SYNC] sync task not queued (dispatch unavailable)");
}

class WorkshopMenuHook
{
public:

    friend void CancelPlacementFromUI();

    static bool Install()
    {
        if (!crafting::ids::WorkshopDataResolvable()) {
            logger::error("PrismaCrafting: WorkshopMenu hook NOT installed -- unsupported runtime");
            return false;
        }

        if (s_installed) return true;
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::WorkshopMenu[0] };
        _ProcessMessage = vtable.write_vfunc(0x3, &WorkshopMenuHook::ProcessMessage);
        _AdvanceMovie   = vtable.write_vfunc(0x4, &WorkshopMenuHook::AdvanceMovie);
        if (!_ProcessMessage.address() || !_AdvanceMovie.address()) {
            logger::critical("PrismaCrafting: WorkshopMenu interception incomplete; replacement disabled");
            return false;
        }
        s_installed = true;
        logger::info("PrismaCrafting: WorkshopMenu vtable hooked (slots 3/4 -- placement engine)");
        return true;
    }

    static bool IsReady() { return s_installed; }

private:

    static void CancelPlacement()
    {
        RetirePlacementOwnership();
        g_placing.store(false);

        if (crafting::ids::WorkshopDataResolvable())
            if (auto* activeEdit = WSExt::ActiveEdit()) *activeEdit = false;
        WorkshopInputHook::OnPlacementEnded(g_open);
        if (g_api && g_api->IsValid(g_helperView))
            g_api->InteropCall(g_helperView, "setPlacingMode", "0");
        g_placingFormId.store(0);
        g_placingRecipeId.store(0);

        g_placementQueue.InvalidateSession();
        g_placementQueue.Post(placement::Command{ placement::CmdType::Destroy, 0, 0, 0 });
        logger::info("PrismaCrafting: placement cancelled (session invalidated, ghost destroy queued for render tick)");
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
                    if (overlay) {
                        CancelScrapConfirm();
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    else if (placing) CancelPlacement();
                    else RE::Workshop::RequestExitWorkshop(false);
                } else if (!tabDown) { s_tab = false; }

                const bool rDown = (GetAsyncKeyState('R') & 0x8000) != 0;
                if (rDown && !s_scrap) {
                    s_scrap = true;
                    logger::info("PrismaCrafting: R down (placing={}, focused={}, overlay={})",
                                 placing, WorkshopInputHook::IsFocused(), overlay);
                    if (overlay) {
                        ConfirmScrap();
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    else if (placing && !WorkshopInputHook::IsFocused()) {
                        ArmScrapAfterPlacementCancel();
                        CancelPlacement();
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    } else if (!WorkshopInputHook::IsFocused()) return (BeginScrapConfirm(), RE::UI_MESSAGE_RESULTS::kHandled);
                } else if (!rDown) { s_scrap = false; }

                const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                if (escDown && !s_esc) {
                    s_esc = true;
                    if (overlay) {
                        CancelScrapConfirm();
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    else if (placing) CancelPlacement();
                } else if (!escDown) { s_esc = false; }
            }
        }
        return _ProcessMessage(a_this, a_message);
    }

    static void SyncHelperVsTransferScreen()
    {
        if (!g_api || !g_api->IsValid(g_helperView) || !WorkshopCatalogDelivered()) return;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;

        bool movieDrawn = false;
        if (auto menu = ui->GetMenu("ContainerMenu"); menu && menu->uiMovie)
            movieDrawn = menu->uiMovie->GetVisible();

        const bool transferVisible = movieDrawn;

        const bool actuallyHidden = g_api->IsHidden(g_helperView);
        if (transferVisible == actuallyHidden) return;

        QueueWorkshopTransferSync(transferVisible);
    }

    static void AdvanceMovie(RE::WorkshopMenu* a_this, float a_timeDelta, std::uint64_t a_time)
    {
        _AdvanceMovie(a_this, a_timeDelta, a_time);
        WorkshopFreedom::Tick();
        if (!g_open) return;
        WorkshopControllerInput::NoteHeldObject(WorkshopInputHook::HoldingPlacement());

        TickInitialWorkshopCatalog();
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
                        OnGhostCommitted();
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
    static inline bool s_installed = false;
    static inline bool s_tab = false, s_esc = false, s_scrap = false;
};

void CancelPlacementFromUI()
{
    if (WorkshopSessionActive(g_wsSession.load(std::memory_order_acquire)) && g_placing.load()) WorkshopMenuHook::CancelPlacement();
}

static const char* WorkshopReplacementBlocker()
{
    if (!crafting::ids::WorkshopDataResolvable()) return "unsupported runtime";
    if (!g_anchorHealthReady.load(std::memory_order_acquire)) return "anchor health check failed";
    if (!g_workshopInputReady.load(std::memory_order_acquire)) return "workshop input not initialized";
    if (!g_workshopHooksReady.load(std::memory_order_acquire)) return "workshop hooks not initialized";
    if (!g_workshopMenuHookReady.load(std::memory_order_acquire)) return "workshop menu hook not initialized";
    if (!WorkshopInputHook::IsInstalled()) return "workshop input hook not installed";
    if (!WorkshopFreedom::HooksReady()) return "workshop freedom hooks not ready";
    if (!WorkshopMenuHook::IsReady()) return "workshop menu hook not ready";
    return nullptr;
}

static bool WorkshopReplacementReady()
{
    return WorkshopReplacementBlocker() == nullptr;
}

static std::atomic<bool> g_craftBypassReported{ false };
static void CheckCraftMenuBypass(const RE::MenuOpenCloseEvent& a_event);

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

        CheckCraftMenuBypass(a_event);

        if (a_event.menuName == "ContainerMenu" && g_api) {
            QueueWorkshopTransferSync(a_event.opening);
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event.menuName == "WorkshopMenu") {
            if (a_event.opening) {
                SetMenuName(a_event.menuName);
                logger::info("PrismaCrafting: WorkshopMenu opening — build helper");
                LogMemory("workshop OPEN");
                WorkshopInputHook::SetActive(false);

                if (!WorkshopReplacementReady()) {
                    const char* blocker = WorkshopReplacementBlocker();
                    logger::warn("PrismaCrafting: vanilla workshop UI kept: {}; building still works",
                                 blocker ? blocker : "prerequisite changed during check");
                    return RE::BSEventNotifyControl::kContinue;
                }
                g_open = true;
                if (g_api) g_placing.store(false);
                const std::uint32_t wsGen = ++g_wsSession;
                ArmInitialWorkshopCatalog(wsGen);
                if (!OnMainThread("workshop-open", [wsGen](const char*) {
                if (!WorkshopSessionActive(wsGen)) {
                    logger::info("PrismaCrafting: deferred helper create skipped -- Workshop session {} is no longer current", wsGen);
                    return;
                }
                if (!g_api || !g_api->IsValid(g_helperView) || !g_helperControlsReady.load(std::memory_order_acquire)) {
                    if (g_api && g_api->IsValid(g_helperView)) g_api->Destroy(g_helperView);
                    g_helperView = 0;
                    g_helperControlsReady.store(false, std::memory_order_release);
                    CreateWorkshopHelperView();
                }
                if (!g_api || !g_api->IsValid(g_helperView)) {
                    AbortWorkshopReplacement("helper view creation failed", wsGen);
                    return;
                }
                ActivateWorkshopReplacementIfReady("workshop open");
                }) && WorkshopSessionActive(wsGen)) {
                    AbortWorkshopReplacement("workshop-open dispatch rejected", wsGen);
                }
            } else {
                logger::info("PrismaCrafting: WorkshopMenu closing");
                LogMemory("workshop CLOSE (pre)");
                WorkshopFreedom::SetWorkshopActive(false);
                g_open = false;
                ++g_wsSession;
                ResetPlacementOwnership();
                ArmInitialWorkshopCatalog(0);
                HideScrapOverlay();
                ClearScrapAfterPlacementCancel();
                g_placing.store(false);
                g_placementStartQueued.store(0);
                g_placingFormId.store(0);
                g_placingRecipeId.store(0);
                g_lastPlacedRef.Store({});

                g_placementQueue.InvalidateSession();
                g_placementQueue.Clear();
                g_ghostRef.Store({});

                SetWorkshopVanillaSuppressed(false);
                WorkshopInputHook::OnClosed();

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
            if (OpenBenchIsFurnitureSwapper(a_event.menuName)) {
                if (!g_interceptFurnitureSwapperBenches.load(std::memory_order_acquire)) {
                    g_interceptingSwapperThisOpen.store(false, std::memory_order_release);
                    g_interceptingSwapperBenchId.store(0, std::memory_order_release);
                    MarkBenchBypassed(a_event.menuName);
                    logger::info("PrismaCrafting: '{}' opened on a scripted invisible furniture-swapper "
                                 "bench (e.g. ECO Universal Workbench) -- not intercepting, so the vanilla "
                                 "menu's furniture-exit still runs and the player can stand up. Set "
                                 "[Compat]InterceptFurnitureSwapperBenches=1 to show the catalog here "
                                 "instead (experimental, see #20).",
                                 a_event.menuName.c_str());
                    return RE::BSEventNotifyControl::kContinue;
                }
                g_interceptingSwapperThisOpen.store(true, std::memory_order_release);
                g_interceptingSwapperBenchId.store(OpenBenchFurnitureFormId(a_event.menuName), std::memory_order_release);
                logger::info("PrismaCrafting: '{}' opened on a scripted invisible furniture-swapper "
                             "bench (e.g. ECO Universal Workbench) -- intercepting anyway because "
                             "[Compat]InterceptFurnitureSwapperBenches=1. Close will explicitly end the "
                             "furniture interaction; watch for [FURN] lines in this log.",
                             a_event.menuName.c_str());
            } else {
                g_interceptingSwapperThisOpen.store(false, std::memory_order_release);
                g_interceptingSwapperBenchId.store(0, std::memory_order_release);
            }

            ClearBenchBypass();
            SetMenuName(a_event.menuName);

            g_cookingBench.store(a_event.menuName == "CookingMenu" &&
                                 OpenBenchHasKeyword(CobjCache::kWorkbenchCookingKeyword));

            logger::info("PrismaCrafting: {} opening (craft terminal)", MenuName().c_str());
            LogMemory("bench OPEN");

            g_open = true;
            const std::uint32_t craftGen = ++g_craftMenuGeneration;

            if (!OnMainThread("craft-menu-open", [craftGen](const char*) {
            if (!CraftSessionActive() ||
                g_craftMenuGeneration.load(std::memory_order_acquire) != craftGen) {
                logger::info("PrismaCrafting: deferred craft-menu open skipped -- session {} is no longer current", craftGen);
                return;
            }
            auto av = ActiveView();
            const bool reusing = (g_api && g_api->IsValid(av));
            if (!reusing) {
                g_view = 0;
                CreateCraftingView();
                av = ActiveView();

                if (!g_api || !g_api->IsValid(av)) {
                    AbortCraftReplacement("crafting view creation failed", craftGen);
                    return;
                }
            } else {

                if (!g_craftingControlsReady.load(std::memory_order_acquire) && !RetryCraftingControls(av)) {
                    AbortCraftReplacement("reused crafting view is not control-ready", craftGen);
                    return;
                }

                if (!ActivateCraftReplacement(av)) return;
                PushRecipesForCurrentBench();
            }
            if (g_api && g_api->IsValid(av)) PushControllerPrompts(av);
            logger::info("PrismaCrafting: {} menu-open handler done (reusing={})",
                MenuName().c_str(), reusing);
            })) {
                AbortCraftReplacement("craft-menu-open dispatch rejected", craftGen);
            }
        } else {
            if (ConsumeBenchBypass(a_event.menuName)) {
                logger::info("PrismaCrafting: bypassed bench '{}' closed -- nothing to tear down.",
                             a_event.menuName.c_str());
                return RE::BSEventNotifyControl::kContinue;
            }
            ++g_craftMenuGeneration;
            logger::info("PrismaCrafting: {} closing", a_event.menuName.c_str());
            LogMemory("bench CLOSE (pre)");
            g_open = false;
            if (g_api) {
                auto av = ActiveView();
                if (g_api->IsValid(av)) { g_api->Unfocus(av); g_api->Hide(av); }
            }
            SetCraftVanillaSuppressed(false);

            if (g_interceptingSwapperThisOpen.exchange(false, std::memory_order_acq_rel))
                EndFurnitureInteractionForClose("engine-initiated bench close", g_interceptingSwapperBenchId.exchange(0, std::memory_order_acq_rel));

            DestroyAllViews();
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
static void ReportMenuInterceptors()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return;
    auto* src = ui->GetEventSource<RE::MenuOpenCloseEvent>();
    if (!src) return;
    using SinkT = RE::BSTEventSink<RE::MenuOpenCloseEvent>;
    auto& sinks = *reinterpret_cast<RE::BSTArray<SinkT*>*>(
        reinterpret_cast<std::uintptr_t>(src) + 0x08);
    auto* ours = static_cast<SinkT*>(MenuOpenCloseHandler::GetSingleton());

    logger::warn("PrismaCrafting: [CONFLICT] a chem/cooking bench menu is open but PrismaCrafting never "
                 "received its event -- another mod intercepted it. Menu-event sinks in dispatch order:");
    std::string aheadOfUs;
    bool passedUs = false;
    int idx = 0;
    for (auto* sink : sinks) {
        if (!sink) { ++idx; continue; }
        void* vtbl = *reinterpret_cast<void* const*>(sink);
        std::string name = "<unknown>";
        HMODULE hmod{};
        char path[MAX_PATH]{};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(vtbl), &hmod) &&
            GetModuleFileNameA(hmod, path, sizeof(path))) {
            std::string p(path);
            const auto slash = p.find_last_of("\\/");
            name = (slash == std::string::npos) ? p : p.substr(slash + 1);
        }
        const bool isOurs = (sink == ours);
        logger::warn("PrismaCrafting: [CONFLICT]   #{} {}{}", idx, name, isOurs ? "  <-- PrismaCrafting" : "");
        if (isOurs) passedUs = true;
        else if (!passedUs && name != "PrismaCrafting.dll") {
            if (!aheadOfUs.empty()) aheadOfUs += ", ";
            aheadOfUs += name;
        }
        ++idx;
    }
    if (aheadOfUs.empty()) {
        logger::warn("PrismaCrafting: [CONFLICT] no other mod is registered ahead of us -- the interceptor "
                     "registers dynamically or hooks the menu directly; check other crafting/workbench UI mods.");
        return;
    }
    logger::warn("PrismaCrafting: [CONFLICT] these mods hook menu events before PrismaCrafting and may be "
                 "swallowing the crafting menu: {}", aheadOfUs);
    const std::string msg = "PrismaCrafting: crafting menu intercepted by another mod (" + aheadOfUs +
                            ") -- see PrismaCrafting.log";
    RE::SendHUDMessage::ShowHUDMessage(msg.c_str(), nullptr, true, false);
}

static void CheckCraftMenuBypass(const RE::MenuOpenCloseEvent& a_event)
{
    if (!crafting::ids::WorkshopDataResolvable()) return;
    if (g_craftBypassReported.load()) return;

    auto* ui = RE::UI::GetSingleton();
    if (!ui) return;

    crafting::bypass::MenuEventFacts facts{};
    facts.opening = a_event.opening;
    facts.eventIsCraftMenu = IsCraftMenu(a_event.menuName);
    facts.weOwnAnOpenBench = g_open.load();
    facts.benchMenuRegistered = ui->GetMenu(RE::BSFixedString("CookingMenu")) ||
                                ui->GetMenu(RE::BSFixedString("ChemistryMenu"));

    if (!crafting::bypass::ShouldReportBypass(facts)) return;

    g_craftBypassReported.store(true);
    ReportMenuInterceptors();
}

bool InstallWorkshopMenuHook()
{
    return WorkshopMenuHook::Install();
}

void RegisterMenuEvents(RE::BSTEventSource<RE::MenuOpenCloseEvent>* source)
{
    source->RegisterSink(MenuOpenCloseHandler::GetSingleton());
}

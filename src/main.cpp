#include "main/RuntimeInternal.h"

static std::atomic<bool> s_formsLoaded{ false };

static void InitUpgradesForCoSave()
{
    if (s_formsLoaded.load(std::memory_order_acquire)) WorkbenchUpgrades::Init();
}

static void CoSave_Save(const F4SE::SerializationInterface* intfc)
{
    InitUpgradesForCoSave();
    WorkbenchUpgrades::CoSaveWrite(intfc);
}
static void CoSave_Load(const F4SE::SerializationInterface* intfc)
{
    InitUpgradesForCoSave();
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
    InitUpgradesForCoSave();
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

static void InitializeRuntimeOnGameThread();

static bool RuntimeNeedsGameThreadWork()
{
    return HasPendingCraftAbort() || !g_runtimeInitComplete.load(std::memory_order_acquire) ||
           (g_postLoadReady.load(std::memory_order_acquire) &&
           !g_postLoadInitComplete.load(std::memory_order_acquire));
}

static void StartRuntimeInitializationRetry();

static bool QueueRuntimeInitializationAttempt()
{
    if (!RuntimeNeedsGameThreadWork()) return true;

    bool expected = false;
    if (!g_runtimeInitQueued.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
        return true;

    if (!PrismaCraftingThreading::AddGameTask([] {
            InitializeRuntimeOnGameThread();
            ServicePendingCraftAbort();
            g_runtimeInitQueued.store(false, std::memory_order_release);
            if (RuntimeNeedsGameThreadWork())
                StartRuntimeInitializationRetry();
        })) {
        g_runtimeInitQueued.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

static void StartRuntimeInitializationRetry()
{
    bool expected = false;
    if (!g_runtimeInitRetryRunning.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
        return;

    std::thread([] {
        for (unsigned attempt = 0; attempt < kRuntimeInitRetryAttempts; ++attempt) {
            if (!RuntimeNeedsGameThreadWork()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!QueueRuntimeInitializationAttempt()) continue;
            if (!RuntimeNeedsGameThreadWork()) break;
        }
        bool slowLogged = false;
        unsigned staleQueuedTicks = 0;
        while (RuntimeNeedsGameThreadWork()) {
            if (!slowLogged) {
                slowLogged = true;
                logger::warn("PrismaCrafting: V11 game-thread work is pending; initialization/recovery "
                             "will keep retrying every {} ms", kRuntimeInitSlowRetryMs);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kRuntimeInitSlowRetryMs));
            if (!RuntimeNeedsGameThreadWork()) break;
            if (g_runtimeInitQueued.load(std::memory_order_acquire)) {
                if (++staleQueuedTicks < kRuntimeInitStaleQueuedTicks) continue;
                logger::warn("PrismaCrafting: queued V11 game-thread work never ran; re-queueing");
                g_runtimeInitQueued.store(false, std::memory_order_release);
            }
            staleQueuedTicks = 0;
            (void)QueueRuntimeInitializationAttempt();
        }
        g_runtimeInitRetryRunning.store(false, std::memory_order_release);
        if (RuntimeNeedsGameThreadWork()) StartRuntimeInitializationRetry();
        if (slowLogged) logger::info("PrismaCrafting: V11 retry worker finished");
    }).detach();
}

void RetryCraftRecovery()
{
    StartRuntimeInitializationRetry();
}

static void QueueRuntimeInitialization()
{
    if (!g_gameDataReady.load(std::memory_order_acquire)) return;
    if (!RuntimeNeedsGameThreadWork()) return;
    if (!QueueRuntimeInitializationAttempt()) {
        logger::error("PrismaCrafting: V11 runtime initialization task was rejected");
        StartRuntimeInitializationRetry();
    }
}

void SetWorkshopVanillaSuppressed(bool suppressed)
{
    for (const char* menu : { "WorkshopMenu", "PromptMenu", "ButtonBarMenu" })
        suppressed ? VanillaMenuSuppression::Claim(menu) : VanillaMenuSuppression::Release(menu);
}

static std::atomic<std::uint32_t> g_pendingWorkshopAbortSession{ 0 };
static std::atomic<std::int64_t> g_lastWorkshopAbortDispatchMs{ 0 };

void AbortWorkshopReplacement(const char* reason, std::uint32_t session)
{
    if (!WorkshopSessionActive(session)) return;
    if (!PrismaCraftingThreading::IsGameThread()) {
        auto pending = g_pendingWorkshopAbortSession.load(std::memory_order_acquire);
        while (pending != session) {
            if (!WorkshopSessionActive(session)) return;
            if (g_pendingWorkshopAbortSession.compare_exchange_weak(
                    pending, session, std::memory_order_acq_rel, std::memory_order_acquire)) {
                logger::critical("PrismaCrafting: workshop replacement abort deferred until game thread: {}",
                                 reason ? reason : "unknown failure");
                return;
            }
        }
        return;
    }
    auto pending = session;
    g_pendingWorkshopAbortSession.compare_exchange_strong(pending, 0, std::memory_order_acq_rel);
    logger::critical("PrismaCrafting: workshop replacement aborted: {}", reason ? reason : "unknown failure");
    g_open.store(false, std::memory_order_release);
    ++g_wsSession;
    ResetPlacementOwnership();
    g_placementQueue.InvalidateSession();
    g_placementQueue.Clear();
    DestroyGhostNow();
    HideScrapOverlay();
    WorkshopInputHook::SetActive(false);
    WorkshopFreedom::SetWorkshopActive(false);
    if (g_api && g_api->IsValid(g_helperView)) g_api->Hide(g_helperView);
    SetWorkshopVanillaSuppressed(false);
}

bool ServicePendingWorkshopAbort()
{
    const auto session = g_pendingWorkshopAbortSession.load(std::memory_order_acquire);
    if (!session) return false;
    if (!WorkshopSessionActive(session)) {
        auto pending = session;
        g_pendingWorkshopAbortSession.compare_exchange_strong(pending, 0, std::memory_order_acq_rel);
        return g_pendingWorkshopAbortSession.load(std::memory_order_acquire) != 0;
    }
    if (PrismaCraftingThreading::IsGameThread()) {
        AbortWorkshopReplacement("deferred game-thread failure", session);
        return true;
    }
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto lastMs = g_lastWorkshopAbortDispatchMs.load(std::memory_order_acquire);
    if (lastMs && nowMs - lastMs < 1000) return true;
    if (!g_lastWorkshopAbortDispatchMs.compare_exchange_strong(lastMs, nowMs, std::memory_order_acq_rel)) return true;
    (void)PrismaCraftingThreading::AddGameTask([session] {
        if (g_pendingWorkshopAbortSession.load(std::memory_order_acquire) == session && WorkshopSessionActive(session))
            AbortWorkshopReplacement("deferred game-thread failure", session);
    });
    return true;
}

static void InitializePostLoadOnGameThread()
{
    if (g_postLoadInitComplete.load(std::memory_order_acquire)) return;
    if (!g_api || !g_api->IsGameThread()) {
        logger::critical("PrismaCrafting: post-load initializer reached without V11 game-thread ownership");
        return;
    }

    g_allowCheatMode.store(LoadAllowCheatMode(), std::memory_order_release);
    g_modelPreviews.store(LoadModelPreviews(), std::memory_order_release);

    static bool menuSinksRegistered = false;
    if (!menuSinksRegistered) {
        auto* ui = RE::UI::GetSingleton();
        auto* menuEvents = ui ? ui->GetEventSource<RE::MenuOpenCloseEvent>() : nullptr;
        if (!menuEvents) {
            logger::error("PrismaCrafting: post-load UI event source unavailable");
            return;
        }

        RegisterMenuEvents(menuEvents);
        RegisterWorkshopPlacedSink();
        RegisterWorkshopDestroyedSink();
        menuSinksRegistered = true;
    }
    if (!g_workshopMenuHookReady.load(std::memory_order_acquire)) {
        const bool hookReady = InstallWorkshopMenuHook();
        g_workshopMenuHookReady.store(hookReady, std::memory_order_release);
        if (!hookReady) {
            logger::critical("PrismaCrafting: WorkshopMenu hook installation failed; post-load initialization will retry");
            return;
        }
    }
    static bool sinksRegistered = false;
    if (!sinksRegistered) {
        sinksRegistered = true;
        logger::info("PrismaCrafting: post-load UI and workshop sinks initialized");
    }

    if (g_view == 0) CreateCraftingView();
    if (g_view == 0) {
        logger::error("PrismaCrafting: crafting view was not created during post-load initialization");
        return;
    }
    if (g_helperView == 0) CreateWorkshopHelperView();
    g_postLoadInitComplete.store(true, std::memory_order_release);
}

static void InitializeRuntimeOnGameThread()
{
    if (!g_api || !g_api->IsGameThread()) {
        logger::critical("PrismaCrafting: runtime initializer reached without V11 game-thread ownership");
        return;
    }

    if (!g_runtimeInitComplete.load(std::memory_order_acquire)) {
        if (!g_anchorCheckComplete.exchange(true, std::memory_order_acq_rel))
            g_anchorHealthReady.store(crafting::anchors::LogAll(), std::memory_order_release);
        static std::atomic<bool> s_inputHooksPrepared{ false };
        if (!s_inputHooksPrepared.exchange(true, std::memory_order_acq_rel)) {
            g_api->SuppressVanillaMenuIf("ExamineConfirmMenu", []() -> bool {
                return (g_open.load() && IsCraftMenu(MenuName()))
                    || g_scrapOverlayOpen.load()
                    || g_scrapAfterPlacementCancelTicks.load() > 0
                    || g_scrapSuppressLatch.load() > 0;
            });
            WorkshopInputHook::Init(&g_api, &g_helperView);
        }
        if (!WorkshopInputHook::Install()) {
            logger::error("PrismaCrafting: workshop input containment unavailable; runtime initialization will retry");
            return;
        }
        g_workshopInputReady.store(true, std::memory_order_release);
        WorkbenchUpgrades::Init();
        CobjCache::Build();
        WorkshopFreedom::Init();
        g_workshopHooksReady.store(
            g_anchorHealthReady.load(std::memory_order_acquire) && WorkshopFreedom::InstallHooks(),
            std::memory_order_release);
        WorkshopFreedom::SetPreUpdateHook(&PumpDeferredWorkshopTick);
        g_runtimeInitComplete.store(true, std::memory_order_release);
        logger::info("PrismaCrafting: V11 runtime initializer complete");
    }

    if (g_postLoadReady.load(std::memory_order_acquire) &&
        !g_postLoadInitComplete.load(std::memory_order_acquire))
        InitializePostLoadOnGameThread();
}

static void F4SEMessageHandler(F4SE::MessagingInterface::Message* message)
{
    logger::debug("F4SE message: type={}", static_cast<int>(message->type));
    switch (message->type) {
    case F4SE::MessagingInterface::kGameDataReady: {
        g_api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI11>();
        if (!g_api)
            logger::error("PrismaCrafting: failed to get PrismaUI API (V11) — PrismaUI_F4 must load first");
        else
            logger::info("PrismaCrafting: PrismaUI V11 API acquired");
        VanillaMenuSuppression::Configure([](const char* a_menu, bool a_suppress) -> bool {
            return g_api && g_api->SuppressVanillaMenu(a_menu, a_suppress);
        });
        PrismaCraftingThreading::SetApi(g_api);
        PrismaFlatApi::Init();
        s_formsLoaded.store(true, std::memory_order_release);
        g_gameDataReady.store(g_api != nullptr, std::memory_order_release);
        QueueRuntimeInitialization();
        break;
    }
    case F4SE::MessagingInterface::kPostLoadGame:
    case F4SE::MessagingInterface::kNewGame: {
        g_postLoadReady.store(true, std::memory_order_release);
        QueueRuntimeInitialization();
        break;
    }
    }
}

#if !defined(PRISMACRAFTING_VERSION_MAJOR) || !defined(PRISMACRAFTING_VERSION_MINOR) || \
    !defined(PRISMACRAFTING_VERSION_PATCH)
#    error "PRISMACRAFTING_VERSION_* not defined -- build through xmake.lua, which derives them"
#endif

#define PC_VER_STR2(x) #x
#define PC_VER_STR(x)  PC_VER_STR2(x)
#define PRISMACRAFTING_VERSION_STRING           \
    PC_VER_STR(PRISMACRAFTING_VERSION_MAJOR) "." \
    PC_VER_STR(PRISMACRAFTING_VERSION_MINOR) "." \
    PC_VER_STR(PRISMACRAFTING_VERSION_PATCH)

F4SE_PLUGIN_VERSION = []() noexcept {
    F4SE::PluginVersionData v{};

    v.PluginVersion({ PRISMACRAFTING_VERSION_MAJOR,
                      PRISMACRAFTING_VERSION_MINOR,
                      PRISMACRAFTING_VERSION_PATCH, 0 });
    v.PluginName("PrismaCrafting");
    v.AuthorName("PrismaUI");

    v.UsesAddressLibrary(true);

    v.UsesSigScanning(true);

    v.IsLayoutDependent(true);
    v.HasNoStructUse(false);

    return v;
}();

static void InitPluginRuntime()
{
    static std::atomic<bool> s_done{ false };
    if (s_done.exchange(true)) return;

    auto path = Paths::F4SEUserDir() / "PrismaCrafting.log";
    auto stream = std::make_shared<std::ofstream>(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (stream->is_open()) stream->exceptions(std::ios::badbit | std::ios::failbit);
    auto sink = std::shared_ptr<spdlog::sinks::ostream_sink_mt>(
        new spdlog::sinks::ostream_sink_mt(*stream),
        [stream](auto* owned) mutable { delete owned; stream.reset(); });
    auto log  = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
    log->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(log);
    spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] [%t] [%s:%#] %v");

    const bool iniCreated = crafting::ini::EnsureSettingsIni(SettingsIniPath());

    bool debugOn = false;
    int catOverride[static_cast<std::size_t>(crafting::dbg::Cat::Count)];
    {
        CSimpleIniA ini; ini.SetUnicode();
        ini.LoadFile(SettingsIniPath().c_str());
        debugOn = ini.GetBoolValue("Debug", "Enabled", false);

        for (std::size_t i = 0; i < static_cast<std::size_t>(crafting::dbg::Cat::Count); ++i) {
            const char* v = ini.GetValue("Debug", crafting::dbg::kCatName[i], nullptr);
            catOverride[i] = (!v || !v[0]) ? -1
                           : (ini.GetBoolValue("Debug", crafting::dbg::kCatName[i], false) ? 1 : 0);
        }
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(crafting::dbg::Cat::Count); ++i) {
        crafting::dbg::Set(static_cast<crafting::dbg::Cat>(i),
                           crafting::dbg::Resolve(debugOn,
                                                  crafting::dbg::kDefaultOnWithMaster[i],
                                                  catOverride[i]));
    }

    log->set_level(debugOn ? spdlog::level::debug : spdlog::level::info);

    static constexpr const char* PRISMACRAFTING_VERSION = PRISMACRAFTING_VERSION_STRING;
    logger::info("PrismaCrafting v{} — debug logging {} — log: PrismaCrafting.log",
        PRISMACRAFTING_VERSION, debugOn ? "ON" : "off");

    if (iniCreated)
        logger::info("PrismaCrafting: no settings file found — wrote a documented default to PrismaCrafting.ini");
    else
        logger::info("PrismaCrafting: settings: PrismaCrafting.ini");

    if (debugOn) {
        std::string on, off;
        for (std::size_t i = 0; i < static_cast<std::size_t>(crafting::dbg::Cat::Count); ++i) {
            auto& dst = crafting::dbg::On(static_cast<crafting::dbg::Cat>(i)) ? on : off;
            if (!dst.empty()) dst += ", ";
            dst += crafting::dbg::kCatName[i];
        }
        logger::info("PrismaCrafting: debug categories ON  [{}]", on.empty() ? "none" : on);
        logger::info("PrismaCrafting: debug categories off [{}]", off.empty() ? "none" : off);
    }

    g_interceptFurnitureSwapperBenches.store(LoadInterceptFurnitureSwapperBenches(),
                                             std::memory_order_release);
    logger::info("PrismaCrafting: [Compat]InterceptFurnitureSwapperBenches={} — scripted "
                 "furniture-swapper benches (ECO Universal Workbench) will be {}",
                 g_interceptFurnitureSwapperBenches.load(std::memory_order_acquire),
                 g_interceptFurnitureSwapperBenches.load(std::memory_order_acquire)
                     ? "INTERCEPTED (experimental)" : "left to the vanilla menu");

}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    InitPluginRuntime();

    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name        = "PrismaCrafting";
    a_info->version     = REL::Version{ PRISMACRAFTING_VERSION_MAJOR, PRISMACRAFTING_VERSION_MINOR,
                                        PRISMACRAFTING_VERSION_PATCH, 0 }.pack();

    if (a_f4se->IsEditor()) {
        logger::critical("Loaded in editor — aborting.");
        return false;
    }
    // Constraint: NG loads, but the workshop paths stay behind WorkshopDataResolvable() until NG addresses exist for the 47xxxxx globals.
    const auto runtime = a_f4se->RuntimeVersion();
    if (runtime != F4SE::RUNTIME_1_10_163 && runtime != F4SE::RUNTIME_1_10_980 &&
        runtime != F4SE::RUNTIME_1_10_984 && runtime != F4SE::RUNTIME_1_11_191 &&
        runtime != F4SE::RUNTIME_1_11_221 && runtime != F4SE::RUNTIME_1_11_240) {
        logger::critical("Unsupported runtime {}. Supported runtimes are OG 1.10.163, NG 1.10.980 and 1.10.984, "
                         "and AE 1.11.191, 1.11.221 and 1.11.240", runtime.string());
        return false;
    }
    return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    g_mainThreadId.store(::GetCurrentThreadId());

    InitPluginRuntime();

    F4SE::Init(a_f4se, F4SE::InitInfo{ .log = false });
    RegisterCoSave();

    const auto* messaging = F4SE::GetMessagingInterface();
    if (!messaging) {
        logger::critical("PrismaCrafting: failed to get F4SE messaging interface");
        return false;
    }
    messaging->RegisterListener(F4SEMessageHandler);
    return true;
}

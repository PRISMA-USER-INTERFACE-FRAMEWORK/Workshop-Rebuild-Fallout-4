#pragma once

#include "PrismaUI_F4_API.h"
#include "CobjCache.h"
#include "InventorySnapshot.h"
#include "WorkshopInputHook.h"
#include "WorkshopControllerInput.h"
#include "PrismaFlatApi.h"
#include "WorkshopFreedom.h"
#include "CraftMenuBypass.h"
#include "AnchorCheck.h"
#include "WorkshopIDs.h"
#include "DebugLog.h"
#include "DefaultIni.h"
#include "PlacementQueue.h"
#include "WorkshopJsonChunks.h"
#include "WorkshopCatalogDeadline.h"
#include "VanillaMenuSuppression.h"

#include <functional>
#include <map>

#include <psapi.h>
#include "WorkbenchUpgrades.h"
#include "Paths.h"

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

#include <SimpleIni.h>

#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>


inline constexpr unsigned kRuntimeInitRetryAttempts = 50;
inline constexpr unsigned kRuntimeInitSlowRetryMs = 2000;
inline constexpr unsigned kRuntimeInitStaleQueuedTicks = 5;
inline constexpr int kScrapSuppressLatchTicks = 12;
inline constexpr int kScrapAfterPlacementCancelTimeoutTicks = 12;

std::filesystem::path SettingsIniPath();
int LoadScale(const char* key);
void SaveScale(const char* key, int pct);
bool LoadInterceptFurnitureSwapperBenches();
bool LoadAllowUnknownExecutable();
bool LoadAllowCheatMode();
bool LoadModelPreviews();
void SaveUnlockAllBenches(bool enabled);
void SaveModelPreviews(bool enabled);
std::string LoadTheme();
void SaveTheme(const char* name);
int ParseIntOr(std::string_view s, int fallback);
std::uint32_t ParseHexOr(std::string_view s, std::uint32_t fallback);
bool IsCraftMenu(const RE::BSFixedString& name);
void MarkViewAsPanel(PrismaView view);
bool BindGameThreadUIEvent(PrismaView view, const char* functionName,
                                  PRISMA_UI_API::GameThreadUIEventCallback callback,
                                  void* userdata = nullptr);
template <class F>
PRISMA_UI_API::JSListenerCallback SafeJSListener(F&& a_callback)
{
    static auto callback = std::forward<F>(a_callback);
    static PRISMA_UI_API::JSListenerCallback thunk = [](const char* a_argument) {
        try {
            callback(a_argument);
        } catch (const std::exception& e) {
            logger::error("PrismaCrafting: JS listener callback threw: {}", e.what());
        } catch (...) {
            logger::error("PrismaCrafting: JS listener callback threw an unknown exception");
        }
    };
    return thunk;
}

bool BindGameThreadUIEventSafeImpl(PrismaView view, const char* functionName,
    std::function<void(const char*, void*)> callback, void* userdata);
template <class F>
bool BindGameThreadUIEventSafe(PrismaView view, const char* functionName, F&& callback, void* userdata = nullptr)
{
    return BindGameThreadUIEventSafeImpl(view, functionName,
        std::function<void(const char*, void*)>(std::forward<F>(callback)), userdata);
}
void SetWorkshopVanillaSuppressed(bool suppressed);
void AbortWorkshopReplacement(const char* reason, std::uint32_t session);
bool ServicePendingWorkshopAbort();
RE::BSFixedString MenuName();
void SetMenuName(const RE::BSFixedString& a_name);
bool CraftSessionActive();
bool WorkshopSessionActive(std::uint32_t session);
void MarkBenchBypassed(const RE::BSFixedString& a_name);
bool ConsumeBenchBypass(const RE::BSFixedString& a_name);
void ClearBenchBypass();
bool BenchBypassActive();
PrismaView ActiveView();
void CreateCraftingView();
void CreateWorkshopHelperView();
std::vector<uint32_t> GetBenchKeywordIdsForMenu(RE::IMenu* raw);
bool OpenBenchHasKeyword(uint32_t keywordId);
bool OpenBenchIsFurnitureSwapper(const RE::BSFixedString& menuName);
std::uint32_t OpenBenchFurnitureFormId(const RE::BSFixedString& menuName);
std::vector<RE::TESObjectREFR*> GetBenchSourceRefs();
std::vector<RE::TESObjectREFR*> GetWorkshopSourceRefs();
void EndFurnitureInteractionForClose(const char* a_why, std::uint32_t a_benchId);
void CloseCrafting();
void DestroyAllViews();
void LogMemory(const char* a_when);
extern PRISMA_UI_API::IVPrismaUI11* g_api;
extern std::atomic<bool> g_gameDataReady;
extern std::atomic<bool> g_postLoadReady;
extern std::atomic<bool> g_runtimeInitComplete;
extern std::atomic<bool> g_postLoadInitComplete;
extern std::atomic<bool> g_runtimeInitQueued;
extern std::atomic<bool> g_runtimeInitRetryRunning;
extern std::atomic<bool> g_anchorCheckComplete;
extern std::atomic<bool> g_anchorHealthReady;
extern std::atomic<bool> g_workshopInputReady;
extern std::atomic<bool> g_workshopHooksReady;
extern std::atomic<bool> g_workshopMenuHookReady;
extern std::atomic<bool> g_craftingControlsReady;
extern std::atomic<bool> g_helperControlsReady;
extern std::atomic<PrismaView> g_view;
extern std::atomic<PrismaView> g_helperView;
extern std::atomic<bool> g_open;
extern std::atomic<std::uint32_t> g_craftMenuGeneration;
extern std::atomic<std::uint32_t> g_wsSession;
extern std::atomic<bool> g_cookingBench;
extern std::atomic<bool> g_cheatMode;
extern std::atomic<bool> g_allowCheatMode;
void ResetCraftCheatMode();
extern std::atomic<bool> g_modelPreviews;
extern std::atomic<bool> g_interceptFurnitureSwapperBenches;
extern std::atomic<bool> g_interceptingSwapperThisOpen;
extern std::atomic<std::uint32_t> g_interceptingSwapperBenchId;
extern std::vector<uint32_t> g_benchIds;
void PushRecipesForCurrentBench();
void InvalidateCraftPageQuery();
void RefreshCraftPageAfterUpgrade();
std::string CraftPageEpoch();
void RequestCraftingPage(const char* arg);
void PushControllerPrompts(PrismaView view);
bool RefreshCraftingSessionBindings(PrismaView view, std::uint32_t generation);
void SetCraftVanillaSuppressed(bool suppressed);
bool CraftSessionMatches(std::uint32_t generation);
bool HasPendingCraftAbort();
void ServicePendingCraftAbort();
void RetryCraftRecovery();
void AbortCraftReplacement(const char* reason, std::uint32_t generation);
bool ActivateCraftReplacement(PrismaView av);
bool RetryCraftingControls(PrismaView view);
void PushWorkshopStats();
bool PushHelperData();
void RequestWorkshopPage(const char* arg);
void ArmInitialWorkshopCatalog(std::uint32_t session);
bool WorkshopCatalogDelivered();
bool WorkshopTransferScreenUp();
void ActivateWorkshopReplacementIfReady(const char* origin);
void TickInitialWorkshopCatalog();
void QueueHelperRefresh();
std::string DescribeRef(RE::TESObjectREFR* a_ref);
std::string DescribeRef(const RE::ObjectRefHandle& a_handle);
void DestroyGhostNow();
void CloseGhostForMenu();
bool SpawnPlacementGhost(std::uint32_t a_recipeId, std::uint32_t a_createdId);
void PumpDeferredWorkshopTick();
void RequestWorkshopInputRestore();
extern std::atomic<bool> g_placing;
extern std::atomic<std::uint64_t> g_placementStartQueued;
extern std::atomic<std::uint32_t> g_placingFormId;
extern std::atomic<std::uint32_t> g_placingRecipeId;
extern std::atomic<bool> g_ghostBusy;
extern std::atomic<bool> g_scrapOverlayOpen;
extern std::atomic<int> g_scrapSuppressLatch;
extern std::atomic<int> g_scrapAfterPlacementCancelTicks;
extern std::atomic<int> g_scrapAfterPlacementClearTicks;
extern placement::Queue g_placementQueue;
extern std::atomic<DWORD> g_mainThreadId;
void SpawnAndPublish(std::uint32_t recipeFormId, std::uint32_t createdFormId, std::uint64_t session);
void StartPlacement(const char* selection);
bool OnGhostCommitted(RE::TESObjectREFR* placed = nullptr);
void ResetPlacementOwnership();
bool PlacementInFlight();
void RetirePlacementOwnership();
void RunFreedomCommand(std::string_view cmd);
void HideScrapOverlay();
void CancelScrapConfirm();
void ConfirmScrap();
void BeginScrapConfirm();
void PumpDeferredScrap();
void ClearScrapAfterPlacementCancel();
void ArmScrapAfterPlacementCancel();
void TickScrapAfterPlacementCancel();
void ToggleRecipeTagForSearch(std::uint32_t cobjFormId, std::optional<bool> a_desired = std::nullopt);
void CancelPlacementFromUI();
void RegisterWorkshopPlacedSink();
void RegisterWorkshopDestroyedSink();
bool InstallWorkshopMenuHook();
void RegisterMenuEvents(RE::BSTEventSource<RE::MenuOpenCloseEvent>* source);

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

extern GuardedRefHandle g_ghostRef;
extern GuardedRefHandle g_lastPlacedRef;

namespace WSExt
{
    RE::Workshop::WorkshopMenuNode* RootNode();
    std::uint16_t* CurrentMenuRow();
    void VatsDisable(bool a_disable);
    void SetVATShaderColor(int a_vsColor);
    void ActorSetGhost(RE::Actor* a_actor, bool a_ghost);
    bool* ActiveEdit();
}

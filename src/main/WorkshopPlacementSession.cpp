// Constraint: readiness requires complete recipe data from source:src/CobjCache.h:Recipe and the actual ghost.
#include "RuntimeInternal.h"
#include <memory>

enum class PlacementPhase { AwaitingSpawn, Spawning, SpawnFailed, Armed, SettlementPending, Settling, Ended };
enum class PlacementPayment { NotAttempted, Pending, ReportedSuccess, ReportedFailure, DispatchRejected };

struct PlacementOwner
{
    PlacementPhase phase = PlacementPhase::AwaitingSpawn;
    PlacementPayment payment = PlacementPayment::NotAttempted;
    std::uint64_t session = 0;
    std::uint32_t workshopSession = 0;
    std::uint32_t recipeId = 0;
    std::uint32_t objectId = 0;
    RE::ObjectRefHandle ghost;
    RE::ObjectRefHandle player;
    std::vector<RE::ObjectRefHandle> sources;
    std::vector<std::pair<RE::TESForm*, std::uint32_t>> costs;
};

static std::mutex g_placementOwnerLock;
static std::uint64_t g_ownedPlacementSession = 0;
static std::shared_ptr<PlacementOwner> g_pendingPlacementOwner;
static std::shared_ptr<PlacementOwner> g_armedPlacementOwner;

bool WorkshopSessionActive(std::uint32_t session)
{
    return session != 0 && g_open.load(std::memory_order_acquire) &&
           MenuName() == "WorkshopMenu" && g_wsSession.load(std::memory_order_acquire) == session;
}

static bool PlacementOwnerCurrent(const std::shared_ptr<PlacementOwner>& owner)
{
    if (!WorkshopSessionActive(owner->workshopSession)) return false;
    std::lock_guard<std::mutex> lock(g_placementOwnerLock);
    return owner->session != 0 && g_ownedPlacementSession == owner->session;
}

void ResetPlacementOwnership()
{
    std::lock_guard<std::mutex> lock(g_placementOwnerLock);
    g_ownedPlacementSession = 0;
    g_placementStartQueued.store(0);
    if (g_pendingPlacementOwner) g_pendingPlacementOwner->phase = PlacementPhase::Ended;
    if (g_armedPlacementOwner) g_armedPlacementOwner->phase = PlacementPhase::Ended;
    g_pendingPlacementOwner.reset();
    g_armedPlacementOwner.reset();
    g_placing.store(false, std::memory_order_release);
}

void RetirePlacementOwnership()
{
    std::lock_guard<std::mutex> lock(g_placementOwnerLock);
    g_ownedPlacementSession = 0;
    g_placementStartQueued.store(0);
    if (g_pendingPlacementOwner) g_pendingPlacementOwner->phase = PlacementPhase::Ended;
    g_pendingPlacementOwner.reset();
    if (!g_armedPlacementOwner) {
        g_placing.store(false, std::memory_order_release);
        g_placingFormId.store(0);
        g_placingRecipeId.store(0);
    }
}

static bool EndOwnedPlacement(const std::shared_ptr<PlacementOwner>& owner)
{
    std::lock_guard<std::mutex> lock(g_placementOwnerLock);
    if (owner->payment == PlacementPayment::Pending) return false;
    owner->phase = PlacementPhase::Ended;
    if (g_ownedPlacementSession != owner->session || !WorkshopSessionActive(owner->workshopSession)) return false;
    g_ownedPlacementSession = 0;
    g_pendingPlacementOwner.reset();
    g_armedPlacementOwner.reset();
    g_placing.store(false, std::memory_order_release);
    g_placingFormId.store(0);
    g_placingRecipeId.store(0);
    g_placementQueue.InvalidateSessionIf(owner->session);
    return true;
}

static std::uint64_t BeginOwnedPlacement(const CobjCache::Recipe& recipe, std::uint32_t objectId,
                                       std::uint32_t workshopSession, RE::PlayerCharacter* player,
                                       const std::vector<RE::TESObjectREFR*>& sources, std::uint64_t request)
{
    PlacementOwner owner;
    owner.workshopSession = workshopSession;
    owner.recipeId = recipe.formRef;
    owner.objectId = objectId;
    owner.player = player->GetHandle();
    if (!owner.player) return 0;
    owner.costs = CobjCache::CostsOf(recipe);
    for (auto* source : sources) {
        if (!source) return 0;
        auto handle = source->GetHandle();
        if (!handle) return 0;
        owner.sources.push_back(handle);
    }
    std::lock_guard<std::mutex> lock(g_placementOwnerLock);
    if (!WorkshopSessionActive(workshopSession) || g_placementStartQueued.load() != request) return 0;
    owner.session = g_placementQueue.BeginSession();
    g_ownedPlacementSession = owner.session;
    if (g_pendingPlacementOwner) g_pendingPlacementOwner->phase = PlacementPhase::Ended;
    g_pendingPlacementOwner = std::make_shared<PlacementOwner>(std::move(owner));
    return g_ownedPlacementSession;
}

static void PublishPlacementResult(bool ok, const std::shared_ptr<PlacementOwner>& owner)
{
    if (!PlacementOwnerCurrent(owner)) return;
    if (ok) {
        {
            std::lock_guard<std::mutex> lock(g_placementOwnerLock);
            if (g_armedPlacementOwner != owner || owner->phase != PlacementPhase::Armed || !g_placing.load()) return;
        }
        if (g_api && g_api->IsValid(g_helperView)) g_api->InteropCall(g_helperView, "setPlacingMode", "1");
        return;
    }
    if (!EndOwnedPlacement(owner)) return;
    logger::warn("PrismaCrafting: selected buildable COBJ {:08X} / object {:08X} is not in the active vanilla workshop tree",
                 owner->recipeId, owner->objectId);
    WorkshopInputHook::OnPlacementEnded(g_open);
    if (g_api && g_api->IsValid(g_helperView)) {
        g_api->InteropCall(g_helperView, "setPlacingMode", "0");
        g_api->InteropCall(g_helperView, "showError", "This item is unavailable in the active workshop");
    }
}

void SpawnAndPublish(std::uint32_t recipeFormId, std::uint32_t createdFormId, std::uint64_t session)
{
    std::shared_ptr<PlacementOwner> owner;
    {
        std::lock_guard<std::mutex> lock(g_placementOwnerLock);
        if (!g_pendingPlacementOwner || g_pendingPlacementOwner->phase != PlacementPhase::AwaitingSpawn || g_pendingPlacementOwner->session != session ||
            g_pendingPlacementOwner->recipeId != recipeFormId || g_pendingPlacementOwner->objectId != createdFormId) return;
        owner = g_pendingPlacementOwner;
        owner->phase = PlacementPhase::Spawning;
    }
    if (!PlacementOwnerCurrent(owner) || g_placementQueue.CurrentSession() != session) return;
    g_placing.store(false, std::memory_order_release);
    bool ok = SpawnPlacementGhost(recipeFormId, createdFormId);
    if (!PlacementOwnerCurrent(owner)) {
        if (ok) DestroyGhostNow();
        return;
    }
    if (ok) {
        owner->ghost = g_ghostRef.Load();
        ok = bool(owner->ghost);
    }
    if (ok) {
        std::unique_lock<std::mutex> lock(g_placementOwnerLock);
        if (g_pendingPlacementOwner != owner || owner->phase != PlacementPhase::Spawning ||
            g_ownedPlacementSession != session || !WorkshopSessionActive(owner->workshopSession)) {
            lock.unlock();
            DestroyGhostNow();
            return;
        }
        owner->phase = PlacementPhase::Armed;
        g_pendingPlacementOwner.reset();
        if (g_armedPlacementOwner) g_armedPlacementOwner->phase = PlacementPhase::Ended;
        g_armedPlacementOwner = owner;
        g_placingRecipeId.store(recipeFormId);
        g_placingFormId.store(createdFormId);
        g_placing.store(true, std::memory_order_release);
    } else {
        {
            std::lock_guard<std::mutex> lock(g_placementOwnerLock);
            if (g_ownedPlacementSession != session) return;
            owner->phase = PlacementPhase::SpawnFailed;
            if (g_armedPlacementOwner) g_armedPlacementOwner->phase = PlacementPhase::Ended;
            g_armedPlacementOwner.reset();
        }
        if (!g_placementQueue.InvalidateSessionIf(session)) return;
    }
    if (!PrismaCraftingThreading::AddGameTask([ok, owner]() {
        PublishPlacementResult(ok, owner);
    })) {
        logger::error("PrismaCrafting: placement result was not queued (dispatch unavailable)");
        if (ok) {
            std::lock_guard<std::mutex> lock(g_placementOwnerLock);
            if (owner->phase != PlacementPhase::Armed || g_armedPlacementOwner != owner ||
                g_ownedPlacementSession != owner->session || !WorkshopSessionActive(owner->workshopSession)) return;
            owner->phase = PlacementPhase::SpawnFailed;
        }
        if (ok && PlacementOwnerCurrent(owner)) {
            DestroyGhostNow();
            if (EndOwnedPlacement(owner)) {
                if (PrismaCraftingThreading::IsGameThread())
                    WorkshopInputHook::OnPlacementEnded(g_open.load());
                else
                    logger::critical("PrismaCrafting: placement rollback left workshop input unchanged because game-thread ownership was unavailable");
            }
            logger::critical("PrismaCrafting: placement rolled back because completion notification was rejected");
        } else {
            if (EndOwnedPlacement(owner)) {
                if (PrismaCraftingThreading::IsGameThread())
                    WorkshopInputHook::OnPlacementEnded(g_open.load());
                else
                    logger::critical("PrismaCrafting: failed placement left workshop input unchanged because game-thread ownership was unavailable");
            }
        }
    }
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
    const auto recipe = CobjCache::FindWorkshopRecipeByCreated(createdFormId);
    if (!recipe) return false;
    recipeFormId = recipe->formRef;
    return true;
}

static void ReleasePlacementStart(std::uint64_t session)
{
    g_placementStartQueued.compare_exchange_strong(session, 0);
}

void StartPlacement(const char* selection)
{
    const auto workshopSession = g_wsSession.load(std::memory_order_acquire);
    if (!WorkshopSessionActive(workshopSession)) return;
    if (!crafting::ids::WorkshopDataResolvable()) {
        logger::warn("PrismaCrafting: [PLACE] unsupported runtime -- placement skipped");
        return;
    }
    std::uint32_t recipeFormId = 0;
    std::uint32_t createdFormId = 0;
    if (!ParsePlacementSelection(selection, recipeFormId, createdFormId)) {
        logger::warn("PrismaCrafting: [PLACE] bad or ambiguous selection '{}'", selection ? selection : "");
        return;
    }

    static std::atomic<std::uint64_t> requestSerial{ 0 };
    const auto request = ++requestSerial;
    std::uint64_t pending = 0;
    if (!g_placementStartQueued.compare_exchange_strong(pending, request)) return;

    if (!PrismaCraftingThreading::AddGameTask([recipeFormId, createdFormId, workshopSession, request]() {
        struct ClearStartQueued {
            std::uint64_t session;
            ~ClearStartQueued() { ReleasePlacementStart(session); }
        } clearStartQueued{ request };
        if (!WorkshopSessionActive(workshopSession) || g_placementStartQueued.load() != request) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto* shop = RE::Workshop::FindNearestValidWorkshop(*player);
        if (!shop) {
            logger::warn("PrismaCrafting: [PLACE] not in a valid workshop area");
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "Not in a workshop area");
            return;
        }

        const auto sources = GetWorkshopSourceRefs();
        const auto rec = CobjCache::FindWorkshopRecipeByFormId(recipeFormId);
        if (!rec || !CobjCache::WorkshopSelectionMatches(*rec, createdFormId)) {
            logger::warn("PrismaCrafting: [PLACE] selection mismatch COBJ {:08X} / object {:08X}",
                         recipeFormId, createdFormId);
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "This recipe is no longer available");
            return;
        }
        if (!CobjCache::WorkshopRecipeAvailable(*rec, sources)) {
            logger::info("PrismaCrafting: [PLACE] COBJ {:08X} refused -- conditions no longer pass", recipeFormId);
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "This item is locked");
            return;
        }

        if (!CobjCache::WorkshopCanAfford(*rec, sources)) {
            logger::info("PrismaCrafting: [PLACE] COBJ {:08X} / object {:08X} refused -- insufficient materials",
                         recipeFormId, createdFormId);
            if (g_api && g_api->IsValid(g_helperView))
                g_api->InteropCall(g_helperView, "showError", "Not enough materials");
            return;
        }

        const auto session = BeginOwnedPlacement(*rec, createdFormId, workshopSession, player, sources, request);
        if (!session) {
            logger::warn("PrismaCrafting: [PLACE] original payment sources unavailable; placement skipped");
            return;
        }
        try {
            WorkshopInputHook::OnPlacementStarted();
        } catch (const std::exception& e) {
            RetirePlacementOwnership();
            g_placementQueue.InvalidateSessionIf(session);
            CancelPlacementFromUI();
            logger::warn("PrismaCrafting: [PLACE] workshop focus refused -- placement retired: {}", e.what());
            return;
        }
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
            logger::info("PrismaCrafting: [PLACE] re-select -- queuing destroy+spawn of {:08X} "
                         "for the render tick (session {})", createdFormId, session);
            g_placementQueue.Post(placement::Command{
                placement::CmdType::DestroyForReselect, recipeFormId, createdFormId, session });
            return;
        }
        g_placementQueue.Post(placement::Command{
            placement::CmdType::Spawn, recipeFormId, createdFormId, session });
    })) {
        ReleasePlacementStart(request);
        logger::warn("PrismaCrafting: [PLACE] start task not queued (dispatch unavailable)");
    }
}

static bool SettleOriginalPlacement(const PlacementOwner& owner, bool dryRun)
{
    const auto retainedPlayer = owner.player.get();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !retainedPlayer || retainedPlayer.get() != player) return false;
    std::vector<RE::NiPointer<RE::TESObjectREFR>> retained;
    std::vector<RE::TESObjectREFR*> sources;
    for (const auto& handle : owner.sources) {
        auto ref = handle.get();
        if (!ref) return false;
        sources.push_back(ref.get());
        retained.push_back(std::move(ref));
    }
    return CobjCache::ConsumeComponents(owner.costs, player, sources, dryRun);
}

bool OnGhostCommitted(RE::TESObjectREFR* placed)
{
    std::shared_ptr<PlacementOwner> owner;
    {
        std::lock_guard<std::mutex> lock(g_placementOwnerLock);
        if (!g_armedPlacementOwner || g_armedPlacementOwner->phase != PlacementPhase::Armed) return false;
        const auto expected = g_armedPlacementOwner->ghost.get();
        if (!expected || (placed && placed != expected.get())) return false;
        if (!placed && !RE::Workshop::IsWorkshopItem(*expected)) return false;
        if (!g_placing.exchange(false) && (!placed || g_ghostBusy.load())) return false;
        owner = std::move(g_armedPlacementOwner);
        owner->phase = PlacementPhase::SettlementPending;
        owner->payment = PlacementPayment::Pending;
        g_lastPlacedRef.Store(owner->ghost);
        if (g_ghostRef.Load() == owner->ghost) g_ghostRef.Store({});
    }
    logger::info("PrismaCrafting: [PLACE-OK] claimed object {:08X} from COBJ {:08X} in placement session {}",
                 owner->objectId, owner->recipeId, owner->session);
    if (!PrismaCraftingThreading::AddGameTask([owner]() {
        {
            std::lock_guard<std::mutex> lock(g_placementOwnerLock);
            if (owner->phase != PlacementPhase::SettlementPending || owner->payment != PlacementPayment::Pending) return;
            owner->phase = PlacementPhase::Settling;
        }
        bool paid = false;
        try {
            paid = SettleOriginalPlacement(*owner, false);
        } catch (...) {
            logger::error("PrismaCrafting: payment threw during settlement");
        }
        {
            std::lock_guard<std::mutex> lock(g_placementOwnerLock);
            owner->payment = paid ? PlacementPayment::ReportedSuccess : PlacementPayment::ReportedFailure;
            owner->phase = PlacementPhase::Ended;
        }
        if (!paid) {
            logger::error("PrismaCrafting: placed object {:08X} from COBJ {:08X}, but exact payment failed; ending placement session",
                          owner->objectId, owner->recipeId);
            if (!EndOwnedPlacement(owner)) return;
            WorkshopInputHook::OnPlacementEnded(g_open);
            if (g_api && g_api->IsValid(g_helperView)) {
                g_api->InteropCall(g_helperView, "setPlacingMode", "0");
                g_api->InteropCall(g_helperView, "showError", "Placement payment failed");
                PushHelperData();
            }
            return;
        }
        logger::info("PrismaCrafting: [PLACE-PAID] consumed components once for object {:08X} from COBJ {:08X}",
                     owner->objectId, owner->recipeId);

        if (!PlacementOwnerCurrent(owner) || g_placementQueue.CurrentSession() != owner->session) return;
        bool canRepeat = false;
        try {
            canRepeat = SettleOriginalPlacement(*owner, true);
        } catch (...) {
            logger::error("PrismaCrafting: repeat check threw after settlement");
        }
        if (!canRepeat) {
            logger::info("PrismaCrafting: out of materials for {:08X} -- placement session ended", owner->objectId);
            if (!EndOwnedPlacement(owner)) return;
            WorkshopInputHook::OnPlacementEnded(g_open);
            if (g_api && g_api->IsValid(g_helperView)) {
                g_api->InteropCall(g_helperView, "setPlacingMode", "0");
                g_api->InteropCall(g_helperView, "showError", "Not enough materials");
                PushHelperData();
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_placementOwnerLock);
            if (g_ownedPlacementSession != owner->session || !WorkshopSessionActive(owner->workshopSession)) return;
            g_pendingPlacementOwner = std::make_shared<PlacementOwner>(*owner);
            g_pendingPlacementOwner->phase = PlacementPhase::AwaitingSpawn;
            g_pendingPlacementOwner->payment = PlacementPayment::NotAttempted;
            g_pendingPlacementOwner->ghost = {};
        }
        if (g_api && g_api->IsValid(g_helperView)) {
            g_api->InteropCall(g_helperView, "refreshWorkshopStats", "");
            PushHelperData();
        }
        g_placementQueue.Post(placement::Command{
            placement::CmdType::Spawn, owner->recipeId, owner->objectId, owner->session });
    })) {
        {
            std::lock_guard<std::mutex> lock(g_placementOwnerLock);
            owner->payment = PlacementPayment::DispatchRejected;
        }
        logger::error("PrismaCrafting: cannot settle placement payment -- dispatch unavailable; ending session");
        if (EndOwnedPlacement(owner)) {
            if (PrismaCraftingThreading::IsGameThread())
                WorkshopInputHook::OnPlacementEnded(g_open.load());
            else
                logger::critical(
                    "PrismaCrafting: payment-settle failure left workshop input unchanged because V11 game-thread ownership was unavailable");
        }
    }
    return true;
}

class WorkshopItemPlacedSink : public RE::BSTEventSink<RE::Workshop::ItemPlacedEvent>
{
public:
    static WorkshopItemPlacedSink* GetSingleton() { static WorkshopItemPlacedSink s; return &s; }
    RE::BSEventNotifyControl ProcessEvent(const RE::Workshop::ItemPlacedEvent& a_event,
                                          RE::BSTEventSource<RE::Workshop::ItemPlacedEvent>*) override
    {
        auto* placed = a_event.placedItem.get();
        const auto expected = g_ghostRef.Load().get();
        if (!placed || !expected || placed != expected.get()) return RE::BSEventNotifyControl::kContinue;
        if (!OnGhostCommitted(placed)) return RE::BSEventNotifyControl::kContinue;
        logger::info("PrismaCrafting: [PLACE-OK] ItemPlacedEvent: engine committed ref {:08X}",
                     placed->GetFormID());
        return RE::BSEventNotifyControl::kContinue;
    }
};

static bool PluginOwnsActivePlacement()
{
    if (!g_placing.load(std::memory_order_acquire)) {
        logger::info("PrismaCrafting: [FREEBUILD] owner query false -- g_placing is false");
        return false;
    }
    auto* liveHandle = RE::Workshop::GetPlacementItem();
    if (!liveHandle) {
        logger::info("PrismaCrafting: [FREEBUILD] owner query false -- no engine placement item handle");
        return false;
    }
    const auto engineRef = liveHandle->get();
    const auto ourRef = g_ghostRef.Load().get();
    const bool owns = engineRef && ourRef && engineRef.get() == ourRef.get();
    logger::info("PrismaCrafting: [FREEBUILD] owner query {} -- engine placement item {:08X}, tracked ghost {:08X}",
                 owns, engineRef ? engineRef->GetFormID() : 0, ourRef ? ourRef->GetFormID() : 0);
    return owns;
}

void RegisterWorkshopPlacedSink()
{
    RE::Workshop::RegisterForItemPlaced(WorkshopItemPlacedSink::GetSingleton());
    WorkshopFreedom::SetPlacementOwnerQuery(&PluginOwnsActivePlacement);
}

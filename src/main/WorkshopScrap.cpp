#include "RuntimeInternal.h"
#include <nlohmann/json.hpp>

struct ScrapOwner
{
    std::uint64_t request;
    std::uint32_t workshopSession;
    RE::ObjectRefHandle target;
    RE::ObjectRefHandle player;
};

static std::mutex g_scrapOwnerLock;
static std::uint64_t g_scrapRequest = 0;
static std::optional<ScrapOwner> g_scrapOverlayOwner;
static std::optional<ScrapOwner> g_scrapPendingOwner;

static bool ScrapOwnerActive(const ScrapOwner& owner)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    return WorkshopSessionActive(owner.workshopSession) && player && player->parentCell &&
           owner.player.get().get() == player;
}

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

void RunFreedomCommand(std::string_view cmd)
{
    if (cmd == "place") WorkshopFreedom::ToggleFreePlace();
    else if (cmd == "cam") WorkshopFreedom::ToggleFreeCamera();
    else if (cmd == "snap") WorkshopFreedom::CycleSnap();
    else if (cmd == "osnap") WorkshopFreedom::ToggleObjectSnap();
    else if (cmd == "reset") WorkshopFreedom::ResetTransform();
    else if (cmd.size() == 3 && cmd[1] >= '0' && cmd[1] <= '2' && (cmd[2] == '+' || cmd[2] == '-')) {
        const float sign = cmd[2] == '-' ? -1.0f : 1.0f;
        const int axis = cmd[1] - '0';
        if (cmd[0] == 'r') WorkshopFreedom::NudgeRotate(axis, WorkshopFreedom::RotateStep() * sign);
        else if (cmd[0] == 'm') WorkshopFreedom::NudgeMove(axis, WorkshopFreedom::MoveStep() * sign);
        else if (cmd[0] == 's') WorkshopFreedom::NudgeScale(WorkshopFreedom::ScaleStep() * sign);
    }
}

void HideScrapOverlay()
{
    {
        std::lock_guard<std::mutex> lock(g_scrapOwnerLock);
        ++g_scrapRequest;
        g_scrapOverlayOwner.reset();
        g_scrapPendingOwner.reset();
        g_scrapOverlayOpen.store(false);
    }
    if (g_api && g_api->IsValid(g_helperView))
        g_api->InteropCall(g_helperView, "hideScrapConfirm", "");
}

void CancelScrapConfirm()
{
    logger::info("PrismaCrafting: [SCRAP] overlay cancelled");
    HideScrapOverlay();
}

void BeginScrapConfirm()
{
    const auto workshopSession = g_wsSession.load(std::memory_order_acquire);
    if (!WorkshopSessionActive(workshopSession)) return;
    if (!crafting::ids::WorkshopDataResolvable()) return;
    if (g_placing.load() || g_ghostBusy.load() || g_ghostRef.Load()) return;
    if (!g_api || !g_api->IsValid(g_helperView) || g_api->IsHidden(g_helperView)) return;

    if (g_scrapOverlayOpen.load()) {
        ConfirmScrap();
        return;
    }

    static REL::Relocation<bool*> multiSelect{ crafting::ids::MultiSelect };
    if (multiSelect.get() && *multiSelect) return;

    auto* handle = RE::Workshop::GetPlacementItem();
    if (!handle) return;
    const auto captured = *handle;
    auto ref = captured.get();
    if (!ref) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->parentCell) return;
    ScrapOwner owner{ 0, workshopSession, captured, player->GetHandle() };
    {
        std::lock_guard<std::mutex> lock(g_scrapOwnerLock);
        if (!ScrapOwnerActive(owner)) return;
        owner.request = ++g_scrapRequest;
        g_scrapPendingOwner.reset();
    }

    using CanScrapFn = bool (*)(RE::TESObjectREFR*, bool);
    static REL::Relocation<CanScrapFn> workshopCanScrapRef{ RE::ID::Workshop::WorkshopCanScrapRef };
    if (!workshopCanScrapRef(ref.get(), false)) return;

    using CollectFn = void (*)(const RE::TESObjectREFR&,
                               RE::BSScrapArray<RE::BSTTuple<RE::TESBoundObject*, std::uint32_t>>&);
    static REL::Relocation<CollectFn> collectRewards{ crafting::ids::CollectScrapRewards };
    RE::BSScrapArray<RE::BSTTuple<RE::TESBoundObject*, std::uint32_t>> rewards;
    collectRewards(*ref, rewards);

    const auto sources = GetWorkshopSourceRefs();
    using Json = nlohmann::ordered_json;
    Json payload = {{"name", ScrapDisplayName(ref.get())}, {"yields", Json::array()}};
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
        payload["yields"].push_back({{"name", nm}, {"add", add}, {"stored", stored}});
    }
    const auto json = payload.dump(-1, ' ', false, Json::error_handler_t::replace);

    {
        std::lock_guard<std::mutex> lock(g_scrapOwnerLock);
        if (owner.request != g_scrapRequest || !ScrapOwnerActive(owner)) return;
        g_scrapOverlayOwner = owner;
        g_scrapOverlayOpen.store(true);
    }
    g_api->InteropCall(g_helperView, "showScrapConfirm", json.c_str());
    logger::info("PrismaCrafting: [SCRAP] overlay for {}", DescribeRef(ref.get()));
}

void ConfirmScrap()
{
    if (!crafting::ids::WorkshopDataResolvable()) return;
    std::optional<ScrapOwner> owner;
    {
        std::lock_guard<std::mutex> lock(g_scrapOwnerLock);
        if (!g_scrapOverlayOwner) return;
        owner = std::exchange(g_scrapOverlayOwner, std::nullopt);
        g_scrapOverlayOpen.store(false);
    }
    g_scrapSuppressLatch.store(kScrapSuppressLatchTicks);
    if (g_api && g_api->IsValid(g_helperView))
        g_api->InteropCall(g_helperView, "hideScrapConfirm", "");
    std::lock_guard<std::mutex> lock(g_scrapOwnerLock);
    if (owner->request != g_scrapRequest || !ScrapOwnerActive(*owner) || !owner->target.get()) return;
    g_scrapPendingOwner = *owner;
}

void PumpDeferredScrap()
{
    std::optional<ScrapOwner> owner;
    {
        std::lock_guard<std::mutex> lock(g_scrapOwnerLock);
        owner = std::exchange(g_scrapPendingOwner, std::nullopt);
        if (!owner || owner->request != g_scrapRequest || !ScrapOwnerActive(*owner)) return;
    }
    auto ni = owner->target.get();
    if (!ni) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->parentCell || owner->player.get().get() != player) return;
    // Constraint: the ref must be re-validated at the deferred tick; the handle alone only proves it existed at confirm time.
    if (ni->IsDeleted() || !ni->GetObjectReference() || ni->GetParentCell() != player->parentCell) {
        logger::warn("PrismaCrafting: [SCRAP] deferred scrap target {:08X} is gone or changed cells -- dropped", ni->GetFormID());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_scrapOwnerLock);
        if (owner->request != g_scrapRequest || !ScrapOwnerActive(*owner)) return;
    }

    if (crafting::ids::WorkshopDataResolvable()) {
        static REL::Relocation<std::uint32_t*> placementCollisionCount{ crafting::ids::PlacementCollisionCount };
        if (placementCollisionCount.get()) {
            *placementCollisionCount.get() = 0;
        } else {
            logger::error("PrismaCrafting: [SCRAP] placementCollisionCount failed to resolve -- skipped reset");
        }
    }

    if (auto* ph = RE::Workshop::GetPlacementItem(); ph) {
        if (auto cur = ph->get(); cur && cur.get() == ni.get())
            *ph = RE::ObjectRefHandle{};
    }

    static REL::Relocation<RE::ObjectRefHandle*> hCurrentWorkshop{ crafting::ids::CurrentWorkshopHandle };
    const RE::ObjectRefHandle previous = hCurrentWorkshop.get() ? *hCurrentWorkshop : RE::ObjectRefHandle{};
    RE::TESObjectREFR* workshopRef = previous.get() ? previous.get().get() : nullptr;
    if (!workshopRef) logger::warn("PrismaCrafting: [SCRAP] no current workshop while the deferred scrap refunds -- refunds may be lost");
    else if (hCurrentWorkshop.get())
        *hCurrentWorkshop = workshopRef->GetHandle();

    RE::Workshop::ContextData ctx{ player };
    RE::ObjectRefHandlePtr ptr;
    ptr.reset(ni.get());
    const std::string desc = DescribeRef(ni.get());
    RE::Workshop::ScrapReference(ctx, ptr, nullptr);

    if (hCurrentWorkshop.get())
        *hCurrentWorkshop = previous;

    logger::info("PrismaCrafting: [SCRAP] ScrapReference {} (deferred render tick)", desc);
}

void ClearScrapAfterPlacementCancel()
{
    g_scrapAfterPlacementCancelTicks.store(0);
    g_scrapAfterPlacementClearTicks.store(0);
}

void ArmScrapAfterPlacementCancel()
{
    g_scrapAfterPlacementClearTicks.store(0);
    g_scrapAfterPlacementCancelTicks.store(1);
}

void TickScrapAfterPlacementCancel()
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

void ToggleRecipeTagForSearch(std::uint32_t cobjFormId, std::optional<bool> a_desired)
{
    if (!WorkshopSessionActive(g_wsSession.load(std::memory_order_acquire)) || !crafting::ids::WorkshopDataResolvable()) return;
    auto* fm = RE::FavoritesManager::GetSingleton();
    if (!fm) return;
    auto* cobj = RE::fallout_cast<RE::BGSConstructibleObject*>(RE::TESForm::GetFormByID(cobjFormId));
    if (!cobj) { logger::warn("PrismaCrafting: [TAG] COBJ {:08X} not resolvable", cobjFormId); return; }
    // Constraint: a desired state only toggles when the engine tag differs, so a favorite and its tag stay in step.
    if (a_desired && fm->favoriteMods.contains(cobj) == *a_desired) return;
    using Fn = void (*)(RE::FavoritesManager*, RE::BGSConstructibleObject*);
    static REL::Relocation<Fn> toggleModFavorite{ crafting::ids::ToggleModFavorite };
    toggleModFavorite(fm, cobj);
    logger::info("PrismaCrafting: [TAG] toggled tag-for-search on recipe {:08X}", cobjFormId);
}

class WorkshopItemDestroyedSink : public RE::BSTEventSink<RE::Workshop::ItemDestroyedEvent>
{
public:
    static WorkshopItemDestroyedSink* GetSingleton() { static WorkshopItemDestroyedSink s; return &s; }
    RE::BSEventNotifyControl ProcessEvent(const RE::Workshop::ItemDestroyedEvent& a_event,
                                          RE::BSTEventSource<RE::Workshop::ItemDestroyedEvent>*) override
    {

        auto* destroyed = a_event.objectDestroyed.get();
        logger::debug("PrismaCrafting: [DESTROYED] engine destroyed {} | our ghostRef={} | lastPlaced={} | "
                     "placing={} | ghostBusy={}",
                     DescribeRef(destroyed), DescribeRef(g_ghostRef.Load()), DescribeRef(g_lastPlacedRef.Load()),
                     g_placing.load(), g_ghostBusy.load());
        QueueHelperRefresh();
        return RE::BSEventNotifyControl::kContinue;
    }
};

void RegisterWorkshopDestroyedSink()
{
    RE::Workshop::RegisterForItemDestroyed(WorkshopItemDestroyedSink::GetSingleton());
}

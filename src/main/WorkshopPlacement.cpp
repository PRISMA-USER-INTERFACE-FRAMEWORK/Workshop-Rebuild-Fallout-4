#include "RuntimeInternal.h"
#include <REX/FModule.h>

std::atomic<bool> g_placing{ false };
static std::atomic<bool> g_placementUpdateQueued{ false };
std::atomic<std::uint64_t> g_placementStartQueued{ 0 };

std::atomic<std::uint32_t> g_placingFormId{ 0 };

std::atomic<std::uint32_t> g_placingRecipeId{ 0 };

std::atomic<bool> g_ghostBusy{ false };

GuardedRefHandle g_ghostRef;

GuardedRefHandle g_lastPlacedRef;

std::atomic<bool> g_scrapOverlayOpen{ false };
std::atomic<int> g_scrapSuppressLatch{ 0 };

std::atomic<int> g_scrapAfterPlacementCancelTicks{ 0 };
std::atomic<int> g_scrapAfterPlacementClearTicks{ 0 };

placement::Queue g_placementQueue;

namespace
{
    void OnPlacementQueueOverflow(const placement::Command& a_dropped, const placement::Command& a_incoming)
    {
        logger::warn("PrismaCrafting: placement queue overflow -- dropped oldest {} (object {:08X}) for new {} (object {:08X})",
                     placement::Queue::TypeName(a_dropped), a_dropped.objectId,
                     placement::Queue::TypeName(a_incoming), a_incoming.objectId);
    }
    const bool g_placementOverflowHooked = [] {
        placement::Queue::SetOverflowHandler(&OnPlacementQueueOverflow);
        return true;
    }();
}

namespace WSExt
{

    RE::Workshop::WorkshopMenuNode* RootNode()
    {
        static REL::Relocation<RE::Workshop::WorkshopMenuNode*> p{
            crafting::ids::WorkshopMenuRoot };
        return p.get();
    }

    std::uint16_t* CurrentMenuRow()
    {
        return RE::Workshop::GetCurrentRow();
    }

    void VatsDisable(bool a_disable)
    {
        static REL::Relocation<void (*)(bool)> f{ crafting::ids::VatsEffectControlDisable };
        f(a_disable);
    }

    void SetVATShaderColor(int a_vsColor)
    {
        static REL::Relocation<void (*)(int)> f{ crafting::ids::SetVATShaderColor };
        f(a_vsColor);
    }

    void ActorSetGhost(RE::Actor* a_actor, bool a_ghost)
    {
        static REL::Relocation<void (*)(RE::Actor*, bool)> f{ crafting::ids::ActorSetGhost };
        f(a_actor, a_ghost);
    }

    bool* ActiveEdit()
    {
        static REL::Relocation<bool*> p{ crafting::ids::ActiveEdit };
        return p.get();
    }

}

static constexpr float kBoundlessGhostFallbackDistance = 300.0f;

namespace
{
    // Constraint: the vanilla selection globals are OG 1.10.163 only; every other runtime keeps the manual path.
    constexpr std::uintptr_t kVanillaTopLevelNodes = 0x58E0860;
    constexpr std::uintptr_t kVanillaMenuDepth = 0x58DF5DC;

    bool VanillaMenuSelectionSupported()
    {
        const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
        return version.major() == 1 && version.minor() == 10 && version.patch() == 163;
    }

    bool SelectVanillaMenuPath(const std::vector<RE::Workshop::WorkshopMenuNode*>& a_path)
    {
        if (!VanillaMenuSelectionSupported() || a_path.size() < 2) return false;
        const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
        auto* top = *reinterpret_cast<RE::Workshop::WorkshopMenuNode***>(base + kVanillaTopLevelNodes);
        auto* depth = reinterpret_cast<std::uint32_t*>(base + kVanillaMenuDepth);
        if (!top || !top[0] || top[0] != WSExt::RootNode() || top[0] != a_path.front()) return false;

        top[0]->selected = true;
        a_path.front()->selected = true;
        for (std::size_t i = 1; i < a_path.size(); ++i) {
            for (auto& child : a_path[i - 1]->children)
                if (child) child->selected = false;
            a_path[i]->selected = true;
        }
        *depth = static_cast<std::uint32_t>(a_path.size() - 1);
        logger::debug("PrismaCrafting: [GHOST] vanilla selection set for depth {} (root {:08X})",
                      a_path.size() - 1, a_path.front()->recipe ? a_path.front()->recipe->GetFormID() : 0);
        return true;
    }
}

std::string DescribeRef(RE::TESObjectREFR* a_ref)
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
std::string DescribeRef(const RE::ObjectRefHandle& a_handle)
{
    auto ref = a_handle.get();
    return ref ? DescribeRef(ref.get()) : "<none>";
}

std::atomic<DWORD> g_mainThreadId{ 0 };
std::atomic<DWORD> g_workshopThreadId{ 0 };

void LogPlacementThread(const char* a_site)
{
    const DWORD tid = ::GetCurrentThreadId();
    if (const DWORD ws = g_workshopThreadId.load(); ws != 0 && tid == ws) {
        logger::info("[THREAD] {}: workshop-update (render) thread", a_site);
    } else if (tid == g_mainThreadId.load()) {
        logger::warn("[THREAD] {}: main thread (NOT serialized with Workshop::Update)", a_site);
    } else {
        logger::warn("[THREAD] {}: OFF-THREAD (world mutation not serialized with Workshop::Update)", a_site);
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
    if (RE::Workshop::IsWorkshopItem(*ref)) {
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

    if (RE::Workshop::IsWorkshopItem(*engineRef)) {
        logger::warn("PrismaCrafting: [GHOST] engine placement item {} matches our ghost handle but is a "
                     "COMMITTED workshop item -- treating as NOT ours (commit tracking missed it)",
                     DescribeRef(engineRef.get()));
        return false;
    }
    return true;
}

void CloseGhostForMenu()
{
    if (EngineHoldsOurGhost()) DestroyGhostNow();
    else g_ghostRef.Store({});
}

void DestroyGhostNow()
{
    LogPlacementThread("DestroyGhostNow");
    GhostBusyGuard busy;

    if (const auto ghost = g_ghostRef.Load(); ghost && ghost == g_lastPlacedRef.Load()) {
        logger::warn("PrismaCrafting: [GHOST] REFUSED to destroy ref {:08X} -- it is the item just "
                     "PLACED, not a ghost (commit bookkeeping had not caught up)",
                     ghost.get() ? ghost.get()->GetFormID() : 0);
        g_ghostRef.Store({});
        if (crafting::ids::WorkshopDataResolvable())
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
        if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->parentCell) {
            RE::Workshop::ContextData ctx(player);
            RE::Workshop::DestroyCurrentItem(ctx, false);
        }
    } else {
        logger::info("PrismaCrafting: [GHOST] skipping DestroyCurrentItem -- the engine's placement item "
                     "is not our ghost (crosshair is on an existing building)");
    }
    DeleteGhostRef(g_ghostRef.Load(), "DestroyGhostNow");
    g_ghostRef.Store({});
    if (crafting::ids::WorkshopDataResolvable())
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

bool SpawnPlacementGhost(std::uint32_t a_recipeId, std::uint32_t a_createdId)
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
        RE::Workshop::DestroyCurrentItem(ctx, false);
    } else if (auto* h = RE::Workshop::GetPlacementItem(); h && h->get()) {
        logger::info("PrismaCrafting: [GHOST] NOT destroying engine placement item {} -- it is not "
                     "our ghost (crosshair-selected building), only clearing our own",
                     DescribeRef(h->get().get()));
    }

    DeleteGhostRef(g_ghostRef.Load(), "SpawnPlacementGhost stale-cleanup");
    g_ghostRef.Store({});

    const bool isFurniture = object->GetFormType() == RE::ENUM_FORM_ID::kFURN;
    if (object->GetFormType() == RE::ENUM_FORM_ID::kFLOR || isFurniture) {
        if (SelectVanillaMenuPath(path)) {
            RE::Workshop::CreateNewReference(ctx, false);
            if (auto* placedHandle = RE::Workshop::GetPlacementItem(); placedHandle && placedHandle->get()) {
                g_ghostRef.Store(*placedHandle);
                logger::info("PrismaCrafting: [GHOST] {:08X} spawned through the vanilla placement path",
                             object->GetFormID());
                return true;
            }
            logger::warn("PrismaCrafting: vanilla placement produced no placement item for {:08X}", object->GetFormID());
        } else {
            logger::warn("PrismaCrafting: vanilla menu selection unavailable for {:08X}", object->GetFormID());
        }
        if (!isFurniture) return false;
    }

    if (crafting::ids::WorkshopDataResolvable()) {
        static REL::Relocation<RE::NiPoint3*> translLS{ crafting::ids::PlacementItemTranslLS };
        if (auto* transl = translLS.get()) {
            transl->x = 0.0f;
            transl->y = kBoundlessGhostFallbackDistance;
            transl->z = 0.0f;
        }
    }

    const float boundRadius = RE::Workshop::GetBoundXSize(*object);
    RE::Workshop::InitializePlacementReference(
        ctx, *object, boundRadius > 0.0f ? boundRadius : kBoundlessGhostFallbackDistance);
    if (auto* handle = RE::Workshop::GetPlacementItem()) {
        auto placement = handle->get();
        if (!placement) return false;
        g_ghostRef.Store(*handle);

        if (!placement->Get3D()) {

            if (auto* loaded = placement->Load3D(false)) placement->Set3D(loaded, true);
        }
        if (placement->Get3D()) {
            if (auto* actor = placement->As<RE::Actor>()) {
                actor->SetProcessMe(false, false);
                WSExt::ActorSetGhost(actor, true);
                actor->SetUnconscious(true);
            }
            WSExt::VatsDisable(true);
            WSExt::SetVATShaderColor(0);

            if (auto* activeEdit = WSExt::ActiveEdit()) *activeEdit = true;
            logger::info("PrismaCrafting: [GHOST] armed ghost ref {:08X} for item {:08X}",
                         placement->GetFormID(), a_createdId);
            return true;
        }
        logger::warn("PrismaCrafting: placement reference {:08X} has no loadable 3D", placement->GetFormID());
        DestroyGhostNow();
    }
    return false;
}
static void PumpDeferredPlacement()
{
    const auto cmd = g_placementQueue.Take();
    if (!cmd) return;

    switch (cmd->type) {
    case placement::CmdType::Spawn:
        if (!g_placementQueue.SpawnIsLive(*cmd)) break;
        SpawnAndPublish(cmd->recipeId, cmd->objectId, cmd->session);
        break;
    case placement::CmdType::DestroyForReselect:

        DestroyGhostNow();
        if (g_placementQueue.SpawnIsLive(*cmd))
            g_placementQueue.Post(placement::Command{
                placement::CmdType::Spawn, cmd->recipeId, cmd->objectId, cmd->session });
        break;
    case placement::CmdType::Destroy:
        DestroyGhostNow();
        break;
    }
}

static std::atomic<bool> g_inputRestorePending{ false };

void RequestWorkshopInputRestore()
{
    g_inputRestorePending.store(true, std::memory_order_release);
}

void PumpDeferredWorkshopTick()
{
    DWORD expected = 0;
    g_workshopThreadId.compare_exchange_strong(expected, ::GetCurrentThreadId());
    if (g_inputRestorePending.exchange(false, std::memory_order_acq_rel))
        WorkshopInputHook::OnPlacementEnded(g_open.load());
    PumpDeferredScrap();
    PumpDeferredPlacement();
}

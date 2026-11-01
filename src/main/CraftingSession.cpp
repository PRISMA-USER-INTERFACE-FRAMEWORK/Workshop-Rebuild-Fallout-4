#include "RuntimeInternal.h"

int ParseIntOr(std::string_view s, int fallback)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    int value{};
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    return ec == std::errc{} && ptr == s.data() + s.size() ? value : fallback;
}
std::uint32_t ParseHexOr(std::string_view s, std::uint32_t fallback)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    std::uint32_t value{};
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, 16);
    return ec == std::errc{} && ptr == s.data() + s.size() ? value : fallback;
}

bool IsCraftMenu(const RE::BSFixedString& name)
{
    return name == "CookingMenu" ||
           name == "ChemistryMenu";
}

PRISMA_UI_API::IVPrismaUI11* g_api = nullptr;
std::atomic<bool> g_gameDataReady{ false };
std::atomic<bool> g_postLoadReady{ false };
std::atomic<bool> g_runtimeInitComplete{ false };
std::atomic<bool> g_postLoadInitComplete{ false };
std::atomic<bool> g_runtimeInitQueued{ false };
std::atomic<bool> g_runtimeInitRetryRunning{ false };
std::atomic<bool> g_anchorCheckComplete{ false };
std::atomic<bool> g_anchorHealthReady{ false };
std::atomic<bool> g_workshopInputReady{ false };
std::atomic<bool> g_workshopHooksReady{ false };
std::atomic<bool> g_workshopMenuHookReady{ false };

std::atomic<bool> g_craftingControlsReady{ false };
std::atomic<bool> g_helperControlsReady{ false };

void MarkViewAsPanel(PrismaView view)
{
    if (g_api && view) g_api->SetViewRole(view, PRISMA_UI_API::ViewRole::kPanel);
}

bool BindGameThreadUIEvent(PrismaView view, const char* functionName,
                                  PRISMA_UI_API::GameThreadUIEventCallback callback, void* userdata)
{
    if (!g_api || !view || !functionName || !callback)
        return false;
    if (!g_api->BindGameThreadUIEvent(view, functionName, callback, userdata)) {
        logger::error("PrismaCrafting: failed to bind game-thread UI event '{}'", functionName ? functionName : "");
        return false;
    }
    return true;
}

namespace
{
    struct UiCallbackEntry
    {
        std::function<void(const char*, void*)> callback;
        void* userdata = nullptr;
    };
    std::mutex g_uiCallbackLock;
    std::unordered_map<std::uintptr_t, std::unique_ptr<UiCallbackEntry>> g_uiCallbacks;
    std::unordered_map<std::string, std::uintptr_t> g_uiCallbackBindings;
    std::atomic<std::uintptr_t> g_uiCallbackSerial{ 0 };

    void InvokeUiCallback(const char* a_argument, void* a_token)
    {
        std::function<void(const char*, void*)> callback;
        void* userdata = nullptr;
        {
            std::lock_guard lock(g_uiCallbackLock);
            const auto it = g_uiCallbacks.find(reinterpret_cast<std::uintptr_t>(a_token));
            if (it == g_uiCallbacks.end()) return;
            callback = it->second->callback;
            userdata = it->second->userdata;
        }
        try {
            if (callback) callback(a_argument, userdata);
        } catch (const std::exception& e) {
            logger::error("PrismaCrafting: game-thread UI callback threw: {}", e.what());
        } catch (...) {
            logger::error("PrismaCrafting: game-thread UI callback threw an unknown exception");
        }
    }

    std::uintptr_t StoreUiCallback(const std::string& a_binding, std::function<void(const char*, void*)> a_callback,
        void* a_userdata)
    {
        std::lock_guard lock(g_uiCallbackLock);
        if (const auto existing = g_uiCallbackBindings.find(a_binding); existing != g_uiCallbackBindings.end()) {
            if (auto entry = g_uiCallbacks.find(existing->second); entry != g_uiCallbacks.end()) {
                entry->second->callback = std::move(a_callback);
                entry->second->userdata = a_userdata;
                return existing->second;
            }
        }
        const auto token = g_uiCallbackSerial.fetch_add(1, std::memory_order_acq_rel) + 1;
        auto entry = std::make_unique<UiCallbackEntry>();
        entry->callback = std::move(a_callback);
        entry->userdata = a_userdata;
        g_uiCallbacks.emplace(token, std::move(entry));
        g_uiCallbackBindings.emplace(a_binding, token);
        return token;
    }

    void EraseUiCallback(const std::string& a_binding, std::uintptr_t a_token)
    {
        std::lock_guard lock(g_uiCallbackLock);
        g_uiCallbacks.erase(a_token);
        g_uiCallbackBindings.erase(a_binding);
    }
}

bool BindGameThreadUIEventSafeImpl(PrismaView view, const char* functionName,
    std::function<void(const char*, void*)> callback, void* userdata)
{
    if (!functionName) return false;
    const std::string binding = std::to_string(static_cast<std::uint64_t>(view)) + "#" + functionName;
    const auto token = StoreUiCallback(binding, std::move(callback), userdata);
    if (BindGameThreadUIEvent(view, functionName, &InvokeUiCallback, reinterpret_cast<void*>(token))) return true;
    EraseUiCallback(binding, token);
    return false;
}

std::atomic<PrismaView>     g_view{ 0 };
std::atomic<PrismaView>     g_helperView{ 0 };
std::atomic<bool>           g_open{ false };
std::atomic<std::uint32_t>  g_craftMenuGeneration{ 0 };

std::atomic<std::uint32_t>  g_wsSession{ 0 };
std::atomic<bool>           g_cookingBench{ false };
std::atomic<bool>           g_cheatMode{ false };
std::atomic<bool>           g_allowCheatMode{ true };
std::atomic<bool>           g_modelPreviews{ true };

static std::mutex        g_menuNameLock;
static RE::BSFixedString g_menuNameGuarded;

RE::BSFixedString MenuName()
{
    std::lock_guard<std::mutex> lk(g_menuNameLock);
    return g_menuNameGuarded;
}
void SetMenuName(const RE::BSFixedString& a_name)
{
    InvalidateCraftPageQuery();
    std::lock_guard<std::mutex> lk(g_menuNameLock);
    g_menuNameGuarded = a_name;
}

void ResetCraftCheatMode()
{
    g_cheatMode.store(false, std::memory_order_release);
}

bool CraftSessionMatches(std::uint32_t generation)
{
    return generation && g_craftMenuGeneration.load(std::memory_order_acquire) == generation &&
           g_open.load(std::memory_order_acquire) && IsCraftMenu(MenuName());
}

bool CraftSessionActive()
{
    return CraftSessionMatches(g_craftMenuGeneration.load(std::memory_order_acquire)) && !HasPendingCraftAbort();
}

static std::mutex   g_bypassLock;
static std::string  g_bypassedMenu;
std::atomic<bool> g_interceptFurnitureSwapperBenches{ false };
std::atomic<bool> g_interceptingSwapperThisOpen{ false };
std::atomic<std::uint32_t> g_interceptingSwapperBenchId{ 0 };
void MarkBenchBypassed(const RE::BSFixedString& a_name)
{
    std::lock_guard<std::mutex> lk(g_bypassLock);
    const char* n = a_name.c_str();
    g_bypassedMenu = n ? n : "";
}

bool ConsumeBenchBypass(const RE::BSFixedString& a_name)
{
    std::lock_guard<std::mutex> lk(g_bypassLock);
    const char* n = a_name.c_str();
    if (!g_bypassedMenu.empty() && g_bypassedMenu == (n ? n : "")) {
        g_bypassedMenu.clear();
        return true;
    }
    return false;
}

void ClearBenchBypass()
{
    std::lock_guard<std::mutex> lk(g_bypassLock);
    g_bypassedMenu.clear();
}

bool BenchBypassActive()
{
    std::lock_guard<std::mutex> lk(g_bypassLock);
    return !g_bypassedMenu.empty();
}

PrismaView ActiveView()
{
    return g_view.load();
}

static RE::WorkbenchMenuBase* AsWorkbenchMenu(RE::IMenu* a_menu)
{
    return a_menu ? RE::fallout_cast<RE::WorkbenchMenuBase*>(a_menu) : nullptr;
}

std::vector<uint32_t> GetBenchKeywordIdsForMenu(RE::IMenu* raw)
{
    std::vector<uint32_t> ids;
    if (!raw) return ids;
    auto* wb = AsWorkbenchMenu(raw);
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

bool OpenBenchHasKeyword(uint32_t keywordId)
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    auto menu = ui->GetMenu(MenuName());
    if (!menu) return false;
    auto* wb = AsWorkbenchMenu(menu.get());
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

static RE::TESObjectREFR* OpenBenchWorkbenchRef(const RE::BSFixedString& menuName)
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return nullptr;
    auto menu = ui->GetMenu(menuName);
    if (!menu) return nullptr;
    auto* wb = AsWorkbenchMenu(menu.get());
    return wb ? wb->workbenchRef.get() : nullptr;
}

std::uint32_t OpenBenchFurnitureFormId(const RE::BSFixedString& menuName)
{
    auto* ref = OpenBenchWorkbenchRef(menuName);
    return ref ? ref->GetFormID() : 0;
}

bool OpenBenchIsFurnitureSwapper(const RE::BSFixedString& menuName)
{
    auto* ref = OpenBenchWorkbenchRef(menuName);
    if (!ref) return false;
    auto* base = ref->GetObjectReference();
    if (!base) return false;

    auto containsCI = [](const char* hay, const char* needle) -> bool {
        if (!hay || !needle) return false;
        auto lc = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
        for (const char* h = hay; *h; ++h) {
            const char* a = h;
            const char* b = needle;
            while (*a && *b && lc(*a) == lc(*b)) { ++a; ++b; }
            if (!*b) return true;
        }
        return false;
    };

    if (auto* model = RE::fallout_cast<RE::TESModel*>(base))
        if (containsCI(model->GetModel(), "workbench_invisible"))
            return true;

    if (containsCI(base->GetFormEditorID(), "furn_ECO_"))
        return true;

    return false;
}

static void AddSourceRef(std::vector<RE::TESObjectREFR*>& refs, RE::TESObjectREFR* c)
{
    if (!c || c->GetFormID() == 0x14) return;
    for (auto* e : refs)
        if (e == c || e->GetFormID() == c->GetFormID()) return;
    refs.push_back(c);
}

std::vector<RE::TESObjectREFR*> GetBenchSourceRefs()
{
    std::vector<RE::TESObjectREFR*> refs;
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return refs;
    auto menu = ui->GetMenu(MenuName());
    if (!menu) return refs;
    auto* wb = AsWorkbenchMenu(menu.get());
    if (!wb) return refs;
    AddSourceRef(refs, wb->workbenchContainerRef.get());
    AddSourceRef(refs, wb->sharedContainerRef.get());
    for (auto& c : wb->sharedContainers)
        AddSourceRef(refs, c.get());
    return refs;
}

std::vector<RE::TESObjectREFR*> GetWorkshopSourceRefs()
{
    std::vector<RE::TESObjectREFR*> refs;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return refs;

    if (crafting::ids::WorkshopDataResolvable()) {
        using CollectFn = void (*)(RE::BSScrapArray<RE::NiPointer<RE::TESObjectREFR>>&, bool);
        static REL::Relocation<CollectFn> collectLinked{
            crafting::ids::CollectLinkedWorkshopContainers };
        RE::BSScrapArray<RE::NiPointer<RE::TESObjectREFR>> linked;
        collectLinked(linked, true);
        std::vector<RE::NiPointer<RE::TESObjectREFR>> pinned;
        for (auto& c : linked) {
            if (!c) continue;
            AddSourceRef(refs, c.get());
            pinned.push_back(c);
        }
        // Constraint: keep the NiPointers alive while the caller uses the raw refs; the previous pins release on the next call.
        {
            static std::mutex pinLock;
            static std::vector<RE::NiPointer<RE::TESObjectREFR>> retained;
            std::lock_guard<std::mutex> lock(pinLock);
            retained = std::move(pinned);
        }
        if (!refs.empty()) {
            logger::info("PrismaCrafting: workshop supply pool = {} container(s) (settlement + supply lines)",
                         refs.size());
            return refs;
        }
    }

    AddSourceRef(refs, RE::Workshop::FindNearestValidWorkshop(*player));
    return refs;
}

static void StopCraftingAudio()
{
    auto av = ActiveView();
    if (g_api && g_api->IsValid(av)) g_api->InteropCall(av, "stopLamp", "");
}

static void EndFurnitureInteractionOnGameThread(const char* a_why, std::uint32_t a_benchId);

void EndFurnitureInteractionForClose(const char* a_why, std::uint32_t a_benchId)
{
    if (!PrismaCraftingThreading::IsGameThread()) {
        if (!PrismaCraftingThreading::AddGameTask([a_why, a_benchId]() { EndFurnitureInteractionOnGameThread(a_why, a_benchId); }))
            logger::warn("PrismaCrafting: [FURN] {}: exit not queued (game-thread dispatch rejected); the player may stay seated", a_why);
        return;
    }
    EndFurnitureInteractionOnGameThread(a_why, a_benchId);
}

static void EndFurnitureInteractionOnGameThread(const char* a_why, std::uint32_t a_benchId)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->currentProcess) return;

    const auto before = player->currentProcess->GetOccupiedFurniture();
    if (!before) {
        logger::debug("PrismaCrafting: [FURN] {}: player is not in furniture; nothing to exit", a_why);
        return;
    }

    const auto beforeId = before.get() ? before.get()->GetFormID() : 0;
    if (beforeId != a_benchId) {
        logger::info("PrismaCrafting: [FURN] {}: player is in furniture {:08X} but the intercepted bench was {:08X}; leaving it seated",
                     a_why, beforeId, a_benchId);
        return;
    }
    const bool actorInteraction = player->extraList && player->extraList->HasType(RE::EXTRA_DATA_TYPE::kActorInteraction);
    logger::info("PrismaCrafting: [FURN] {}: player extraList kActorInteraction={} before StopInteractingQuick",
                 a_why, actorInteraction);
    player->StopInteractingQuick(true, true, true);

    const auto after = player->currentProcess->GetOccupiedFurniture();
    if (!after) {
        logger::info("PrismaCrafting: [FURN] {}: exited furniture {:08X}", a_why, beforeId);
    } else {
        logger::warn("PrismaCrafting: [FURN] {}: asked to exit furniture {:08X} but the player is "
                     "STILL in {:08X} -- if the craft idle loops from here, that is this call not "
                     "taking effect, not the kHide",
                     a_why, beforeId, after.get() ? after.get()->GetFormID() : 0);
    }
}

void CloseCrafting()
{
    if (!CraftSessionActive()) return;
    InvalidateCraftPageQuery();
    auto av = ActiveView();
    logger::debug("CloseCrafting: menu='{}' viewValid={} helperValid={}", MenuName().c_str(),
        g_api && g_api->IsValid(av), g_api && g_api->IsValid(g_helperView));
    StopCraftingAudio();
    if (g_api) {
        if (g_api->IsValid(av))           { g_api->Unfocus(av);           g_api->Hide(av); }
        if (g_api->IsValid(g_helperView)) { g_api->Unfocus(g_helperView); g_api->Hide(g_helperView); }
    }
    SetCraftVanillaSuppressed(false);
    if (auto* msgQ = RE::UIMessageQueue::GetSingleton())
        msgQ->AddMessage(MenuName(), RE::UI_MESSAGE_TYPE::kHide);

    if (g_interceptingSwapperThisOpen.exchange(false, std::memory_order_acq_rel))
        EndFurnitureInteractionForClose("CloseCrafting", g_interceptingSwapperBenchId.exchange(0, std::memory_order_acq_rel));

    g_open = false;
}

void DestroyAllViews()
{
    InvalidateCraftPageQuery();
    if (g_api) {
        logger::debug("DestroyAllViews: helper={} (crafting={} kept for reuse)", g_helperView.load(), g_view.load());
        StopCraftingAudio();
        if (g_api->IsValid(g_helperView)) g_api->Destroy(g_helperView);
        g_helperView = 0;
        g_helperControlsReady.store(false, std::memory_order_release);
    }
    WorkshopInputHook::OnClosed();
}

std::vector<uint32_t> g_benchIds;

void LogMemory(const char* a_when)
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) return;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    if (pmc.PrivateUsage == 0) {
        logger::warn("PrismaCrafting: [MEM] {:<22} private=n/a  workingSet={} MB  |  system {} / {} MB used",
                     a_when,
                     pmc.WorkingSetSize / (1024 * 1024),
                     (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024),
                     ms.ullTotalPhys / (1024 * 1024));
        return;
    }

    logger::warn("PrismaCrafting: [MEM] {:<22} private={} MB  workingSet={} MB  |  system {} / {} MB used",
                 a_when,
                 pmc.PrivateUsage / (1024 * 1024),
                 pmc.WorkingSetSize / (1024 * 1024),
                 (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024),
                 ms.ullTotalPhys / (1024 * 1024));
}

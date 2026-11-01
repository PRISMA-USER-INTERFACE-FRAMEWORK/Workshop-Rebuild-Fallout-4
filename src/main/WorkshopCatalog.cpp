// Constraint: moved native bindings must match source:src/main.cpp at 5fc7723.
#include "RuntimeInternal.h"
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>

static float ReadGlobal(std::uint32_t formRef)
{
    auto* form = RE::TESForm::GetFormByID(formRef);
    if (auto* g = RE::fallout_cast<RE::TESGlobal*>(form)) return g->value;
    return -1.0f;
}

static RE::TESObjectREFR* GetActiveWorkshopRef()
{
    if (crafting::ids::WorkshopDataResolvable()) {
        static REL::Relocation<RE::ObjectRefHandle*> hCurrentWorkshop{ crafting::ids::CurrentWorkshopHandle };
        if (hCurrentWorkshop.get()) {
            const RE::ObjectRefHandle current = *hCurrentWorkshop;
            if (auto ref = current.get(); ref)
                return ref.get();
        }
    }
    if (auto* player = RE::PlayerCharacter::GetSingleton())
        return RE::Workshop::FindNearestValidWorkshop(*player);
    return nullptr;
}

static std::string WorkshopStatsJson()
{
    using Json = nlohmann::ordered_json;
    RE::TESObjectREFR* ws = GetActiveWorkshopRef();
    if (ws) {
        const int population = static_cast<int>(RE::Workshop::GetPopulation(*ws));
        const int food       = static_cast<int>(RE::Workshop::GetFood(*ws));
        const int water      = static_cast<int>(RE::Workshop::GetWater(*ws));
        const int power      = static_cast<int>(RE::Workshop::GetGeneratedPower(*ws));
        const int defense    = static_cast<int>(RE::Workshop::GetSafety(*ws));
        const int beds       = static_cast<int>(RE::Workshop::GetBeds(*ws));
        const int happiness  = static_cast<int>(RE::Workshop::GetHappiness(*ws));
        const int happyTarget = static_cast<int>(RE::Workshop::GetHappinessTarget(*ws));
        const int unassigned = static_cast<int>(ReadGlobal(0x0004BE92));

        return Json{{"population", population}, {"unassigned", unassigned}, {"food", food},
                    {"water", water}, {"power", power}, {"defense", defense}, {"beds", beds},
                    {"happiness", happiness}, {"happyTarget", happyTarget}}.dump();
    }

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

    Json payload = Json::object();
    for (const auto& g : kGlobals) {
        payload[g.key] = static_cast<int>(ReadGlobal(g.id));
    }
    return payload.dump();
}

void PushWorkshopStats()
{
    if (!WorkshopSessionActive(g_wsSession.load(std::memory_order_acquire)) || !g_api || !g_api->IsValid(g_helperView)) return;
    const std::string j = WorkshopStatsJson();

    static std::string s_lastStatsLogged;
    if (j != s_lastStatsLogged) {
        s_lastStatsLogged = j;
        logger::info("PrismaCrafting: workshop stats = {}", j);
    }
    g_api->InteropCall(g_helperView, "setWorkshopStats", j.c_str());
}

static const char* volatile g_catalogStage = "idle";
static volatile unsigned long g_catalogFaultCode = 0;
static const void* volatile g_syncNode = nullptr;
static const char* volatile g_syncStep = "start";
static bool SyncWorkshopCategoriesGuarded(unsigned long& code);
static std::mutex g_workshopPageLock;
static CobjCache::WorkshopPageRequest g_workshopPageRequest;
static std::uint32_t g_workshopPageSession = 0;
static std::uint32_t g_workshopPageRequestId = 0;
static std::uint64_t g_workshopPageSerial = 0;

static bool PushHelperDataUnguarded(bool refreshCategories = true)
{
    const auto session = g_wsSession.load(std::memory_order_acquire);
    const auto view = g_helperView.load();
    if (!WorkshopSessionActive(session) || !g_api || !g_api->IsValid(view)) return false;
    CobjCache::WorkshopPageRequest request;
    std::uint32_t requestId = 0;
    {
        std::lock_guard lock(g_workshopPageLock);
        if (g_workshopPageSession == session) {
            request = g_workshopPageRequest;
            requestId = g_workshopPageRequestId;
        }
    }
    if (refreshCategories) {
        g_catalogStage = "CobjCache::Build";
        CobjCache::Build();
        g_catalogStage = "SyncWorkshopCategories";
        g_syncNode = nullptr;
        g_syncStep = "start";
        if (unsigned long code = 0; !SyncWorkshopCategoriesGuarded(code))
            logger::error("PrismaCrafting: workshop menu tree faulted (code {:08X}) at node {} step '{}'; "
                          "continuing with recipe-derived categories",
                          code, static_cast<const void*>(g_syncNode), static_cast<const char*>(g_syncStep));
    }
    g_catalogStage = "GetWorkshopSourceRefs";
    const auto sources = GetWorkshopSourceRefs();
    g_catalogStage = "QueryWorkshopPage";
    std::string buildables = CobjCache::QueryWorkshopPage(request, sources);
    g_catalogStage = "interop delivery";
    constexpr std::size_t kWorkshopInteropChunkBytes = 32 * 1024;
    const auto chunks = crafting::SplitUtf8Chunks(buildables, kWorkshopInteropChunkBytes);
    if (chunks.empty() || chunks.size() > 256 || buildables.size() > 8 * 1024 * 1024 ||
        !WorkshopSessionActive(session) || g_helperView.load() != view) {
        logger::error("PrismaCrafting: workshop page unavailable for CEF delivery");
        return false;
    }
    const auto serialNumber = ++g_workshopPageSerial;
    const std::string serial = std::to_string(serialNumber);
    const auto begin = nlohmann::ordered_json{{"session", session}, {"requestId", requestId},
                                             {"serial", serialNumber}, {"chunks", chunks.size()}}.dump();
    g_api->InteropCall(view, "beginBuildables", begin.c_str());
    for (const auto& chunk : chunks) {
        if (!WorkshopSessionActive(session) || g_helperView.load() != view) return false;
        const std::string payload = serial + ":" + chunk;
        g_api->InteropCall(view, "appendBuildables", payload.c_str());
    }
    if (!WorkshopSessionActive(session) || g_helperView.load() != view) return false;
    g_api->InteropCall(view, "finishBuildables", serial.c_str());
    PushWorkshopStats();
    logger::info("PrismaCrafting: pushed workshop build helper data in {} CEF chunk(s), {} bytes",
                 chunks.size(), buildables.size());
    g_catalogStage = "idle";
    return true;
}

static int CatalogFaultFilter(unsigned long code)
{
    g_catalogFaultCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

// SEH frames must not require C++ object unwinding.
static bool PushHelperDataGuarded(bool& faulted, bool refreshCategories)
{
    __try {
        return PushHelperDataUnguarded(refreshCategories);
    } __except (CatalogFaultFilter(GetExceptionCode())) {
        faulted = true;
        return false;
    }
}

static bool PushWorkshopPage(bool refreshCategories)
{
    const auto session = g_wsSession.load(std::memory_order_acquire);
    bool faulted = false;
    const bool ok = PushHelperDataGuarded(faulted, refreshCategories);
    if (faulted) {
        logger::error("PrismaCrafting: workshop catalog push faulted in {} (code {:08X}); restoring the vanilla workshop UI",
                      static_cast<const char*>(g_catalogStage), static_cast<unsigned long>(g_catalogFaultCode));
        g_catalogStage = "idle";
        AbortWorkshopReplacement("catalog push faulted", session);
    }
    return ok;
}

bool PushHelperData()
{
    return PushWorkshopPage(true);
}

void RequestWorkshopPage(const char* arg)
{
    const auto session = g_wsSession.load(std::memory_order_acquire);
    const auto view = g_helperView.load();
    if (!WorkshopSessionActive(session) || !WorkshopCatalogDelivered() ||
        !g_api || !g_api->IsValid(view)) return;
    if (!arg || std::strlen(arg) > 256 * 1024) return;
    const auto json = nlohmann::json::parse(arg, nullptr, false);
    if (!json.is_object() ||
        !json.contains("session") || !json["session"].is_number_unsigned() ||
        !json.contains("requestId") || !json["requestId"].is_number_unsigned() ||
        !json.contains("page") || !json["page"].is_number_integer() ||
        !json.contains("pageSize") || !json["pageSize"].is_number_integer() ||
        !json.contains("search") || !json["search"].is_string() ||
        !json.contains("category") || !json["category"].is_string() ||
        !json.contains("sub") || !json["sub"].is_string() ||
        !json.contains("filter") || !json["filter"].is_string() ||
        !json.contains("favoritesOnly") || !json["favoritesOnly"].is_boolean() ||
        !json.contains("favorites") || !json["favorites"].is_array()) return;
    const auto boundedInt = [](const nlohmann::json& value, std::uint64_t max) {
        if (value.is_number_unsigned()) return value.get<std::uint64_t>() <= max;
        const auto number = value.get<std::int64_t>();
        return number >= 0 && static_cast<std::uint64_t>(number) <= max;
    };
    if (json["session"].get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)() ||
        json["requestId"].get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)() ||
        !boundedInt(json["page"], 1000000) ||
        !boundedInt(json["pageSize"], 64) || json["pageSize"] == 0) return;
    const auto requestSession = json["session"].get<std::uint32_t>();
    const auto requestId = json["requestId"].get<std::uint32_t>();
    if (requestSession != session || requestId == 0) return;
    CobjCache::WorkshopPageRequest request;
    request.search = json["search"].get<std::string>();
    request.category = json["category"].get<std::string>();
    request.sub = json["sub"].get<std::string>();
    request.filter = json["filter"].get<std::string>();
    request.page = json["page"].get<int>();
    request.pageSize = json["pageSize"].get<int>();
    request.favoritesOnly = json["favoritesOnly"].get<bool>();
    if (request.search.size() > 512 || request.category.size() > 512 || request.sub.size() > 512 ||
        (request.filter != "ALL" && request.filter != "BUILDABLE" && request.filter != "LOCKED") ||
        json["favorites"].size() > 8192) return;
    for (const auto& key : json["favorites"]) {
        if (!key.is_string() || key.get_ref<const std::string&>().size() > 512) return;
        request.favorites.push_back(key.get<std::string>());
    }
    {
        std::lock_guard lock(g_workshopPageLock);
        if (g_workshopPageSession != session) return;
        if (requestId < g_workshopPageRequestId) return;
        g_workshopPageRequest = std::move(request);
        g_workshopPageRequestId = requestId;
    }
    PushWorkshopPage(false);
}

static void SyncWorkshopCategories()
{
    if (!crafting::ids::WorkshopDataResolvable()) return;
    auto* root = WSExt::RootNode();
    if (!root) return;

    std::unordered_map<std::uint32_t, std::string> categories;

    auto keywordName = [](RE::BGSKeyword* kw) -> std::string {
        if (!kw) return {};
        if (auto sv = RE::TESFullName::GetFullName(*kw); !sv.empty()) return std::string(sv);
        const char* eid = kw->GetFormEditorID();
        return (eid && eid[0]) ? eid : std::string{};
    };

    std::size_t namelessNodes = 0;
    std::size_t duplicateRecipes = 0;

    struct Frame { RE::Workshop::WorkshopMenuNode* node; std::string path; };
    std::vector<Frame> stack{ { root, std::string{} } };
    while (!stack.empty()) {
        auto [node, path] = std::move(stack.back());
        stack.pop_back();
        if (!node) continue;
        g_syncNode = node;

        std::string here = path;
        if (node != root) {
            g_syncStep = "filterKeyword";
            if (auto name = keywordName(node->filterKeyword); !name.empty()) {
                here = here.empty() ? name : here + " > " + name;
            } else {
                ++namelessNodes;
                DLOG(Recipes, "category node under '{}' has no usable name (keyword={:08X}) -- "
                              "its {} child node(s) collapse into the parent level",
                     here.empty() ? "<root>" : here,
                     node->filterKeyword ? node->filterKeyword->GetFormID() : 0u,
                     node->children.size());
            }
        }

        g_syncStep = "recipe";
        if (node->recipe && !here.empty()) {
            auto [it, inserted] = categories.emplace(node->recipe->GetFormID(), here);
            if (!inserted && it->second != here) {
                ++duplicateRecipes;
                DLOG(Recipes, "recipe {:08X} appears at both '{}' and '{}' -- keeping '{}'",
                     node->recipe->GetFormID(), it->second, here, it->second);
            }
        }

        g_syncStep = "children";
        for (auto& child : node->children)
            if (child) stack.push_back({ child.get(), here });
    }
    g_syncStep = "apply";

    if (namelessNodes || duplicateRecipes)
        logger::info("PrismaCrafting: workshop category tree: {} nameless node(s) collapsed a level, "
                     "{} recipe(s) reachable by more than one path",
                     namelessNodes, duplicateRecipes);

    if (crafting::dbg::On(crafting::dbg::Cat::Recipes)) {
        std::map<std::string, std::size_t> perPath;
        for (const auto& entry : categories) ++perPath[entry.second];
        DLOG(Recipes, "workshop category tree resolved to {} distinct path(s):", perPath.size());
        for (const auto& [p, n] : perPath) DLOG(Recipes, "    {:>4} x  {}", n, p);
    }

    CobjCache::ApplyWorkshopCategories(categories);
}

// SEH frames must not require C++ object unwinding.
static bool SyncWorkshopCategoriesGuarded(unsigned long& code)
{
    __try {
        SyncWorkshopCategories();
        return true;
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static std::atomic<std::uint32_t> g_refreshQueued{ 0 };
static constexpr auto kInitialCatalogTimeout = std::chrono::seconds(4);
static std::atomic<std::uint32_t> g_initialCatalogSession{ 0 };

static std::atomic<std::uint32_t> g_initialCatalogInFlight{ 0 };
static std::atomic<std::uint32_t> g_catalogDeliveredSession{ 0 };
static std::mutex g_initialCatalogLock;
static crafting::WorkshopCatalogDeadline g_initialCatalogDeadline{ kInitialCatalogTimeout };
static const char* g_initialCatalogWaiting = nullptr;

void ArmInitialWorkshopCatalog(std::uint32_t session)
{
    {
        std::lock_guard lock(g_workshopPageLock);
        g_workshopPageSession = session;
        g_workshopPageRequest = {};
        g_workshopPageRequestId = 0;
    }
    {
        std::lock_guard lock(g_initialCatalogLock);
        if (session) g_initialCatalogDeadline.Arm(std::chrono::steady_clock::now());
        else g_initialCatalogDeadline.Disarm();
        g_initialCatalogWaiting = nullptr;
    }
    g_catalogDeliveredSession.store(0, std::memory_order_release);
    g_initialCatalogInFlight.store(0, std::memory_order_release);
    g_initialCatalogSession.store(session, std::memory_order_release);
}

static bool InitialCatalogExpired(long long& elapsedMs)
{
    std::lock_guard lock(g_initialCatalogLock);
    const auto now = std::chrono::steady_clock::now();
    elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(g_initialCatalogDeadline.Elapsed(now)).count();
    return g_initialCatalogDeadline.Expired(now);
}

bool WorkshopCatalogDelivered()
{
    const auto session = g_wsSession.load();
    return session != 0 && g_catalogDeliveredSession.load(std::memory_order_acquire) == session;
}

static bool WorkshopTransferScreenUp()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    auto menu = ui->GetMenu("ContainerMenu");
    return menu && menu->uiMovie && menu->uiMovie->GetVisible();
}

void ActivateWorkshopReplacementIfReady(const char* origin)
{
    if (!g_api || !g_api->IsGameThread() || !WorkshopSessionActive(g_wsSession.load(std::memory_order_acquire))) return;
    if (ServicePendingWorkshopAbort()) return;
    if (!g_api->IsValid(g_helperView) || !g_helperControlsReady.load(std::memory_order_acquire)) return;
    if (!WorkshopCatalogDelivered()) {
        g_api->Hide(g_helperView);
        return;
    }

    SetWorkshopVanillaSuppressed(true);
    if (WorkshopTransferScreenUp()) {
        g_api->Hide(g_helperView);
        WorkshopInputHook::SetActive(false);
        WorkshopFreedom::SetWorkshopActive(false);
        logger::info("PrismaCrafting: {} -- transfer screen is up, helper stays hidden", origin);
        return;
    }
    g_api->Show(g_helperView);
    WorkshopInputHook::SetActive(true);
    WorkshopFreedom::SetWorkshopActive(true);
    logger::info("PrismaCrafting: {} -- replacement active (hidden={})", origin, g_api->IsHidden(g_helperView));
}

static void FailInitialWorkshopCatalog(std::uint32_t session, const char* reason, long long elapsedMs)
{
    auto expected = session;
    if (!g_initialCatalogSession.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) return;
    expected = session;
    g_initialCatalogInFlight.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    logger::error("PrismaCrafting: Workshop session {} catalog not delivered after {} ms ({}); "
                  "restoring the vanilla workshop UI", session, elapsedMs, reason);
    if (WorkshopSessionActive(session)) AbortWorkshopReplacement("initial catalog unavailable", session);
}

void TickInitialWorkshopCatalog()
{
    if (ServicePendingWorkshopAbort()) return;
    const auto session = g_initialCatalogSession.load(std::memory_order_acquire);
    if (!session) return;
    if (!WorkshopSessionActive(session)) return;

    const char* waiting = nullptr;
    if (!g_api || !g_api->IsValid(g_helperView)) waiting = "helper view not valid";
    else if (!g_helperControlsReady.load(std::memory_order_acquire)) waiting = "helper controls not ready";
    else if (auto* root = WSExt::RootNode(); !root) waiting = "workshop menu root is null";
    else if (root->children.empty()) waiting = "workshop menu tree is empty";

    if (waiting) {
        bool changed = false;
        {
            std::lock_guard lock(g_initialCatalogLock);
            changed = waiting != g_initialCatalogWaiting;
            g_initialCatalogWaiting = waiting;
        }
        if (changed) logger::info("PrismaCrafting: Workshop session {} waiting for catalog: {}", session, waiting);
        long long elapsedMs = 0;
        if (InitialCatalogExpired(elapsedMs)) FailInitialWorkshopCatalog(session, waiting, elapsedMs);
        return;
    }

    const bool inFlight = g_initialCatalogInFlight.load(std::memory_order_acquire) == session;
    if (long long elapsedMs = 0; InitialCatalogExpired(elapsedMs)) {
        FailInitialWorkshopCatalog(
            session, inFlight ? "catalog task accepted but never ran" : "catalog push never succeeded", elapsedMs);
        return;
    }
    if (inFlight) return;

    g_initialCatalogInFlight.store(session, std::memory_order_release);
    if (!PrismaCraftingThreading::AddGameTask([session] {
        if (g_initialCatalogInFlight.load(std::memory_order_acquire) != session) return;
        if (!WorkshopSessionActive(session)) return;
        if (PushHelperData()) {
            auto armed = session;
            if (!g_initialCatalogSession.compare_exchange_strong(armed, 0, std::memory_order_acq_rel)) return;
            g_initialCatalogInFlight.store(0, std::memory_order_release);
            g_catalogDeliveredSession.store(session, std::memory_order_release);
            logger::info("PrismaCrafting: delivered initial catalog for Workshop session {}", session);
            ActivateWorkshopReplacementIfReady("initial catalog delivered");
        } else if (WorkshopSessionActive(session)) {
            logger::warn("PrismaCrafting: Workshop session {} catalog push failed; retrying until the deadline",
                         session);
            auto inFlight = session;
            g_initialCatalogInFlight.compare_exchange_strong(inFlight, 0, std::memory_order_acq_rel);
        }
    })) {
        auto inFlight = session;
        g_initialCatalogInFlight.compare_exchange_strong(inFlight, 0, std::memory_order_acq_rel);
        if (WorkshopSessionActive(session)) {
            long long elapsedMs = 0;
            if (InitialCatalogExpired(elapsedMs))
                FailInitialWorkshopCatalog(session, "catalog task dispatch rejected", elapsedMs);
        }
    }
}

void QueueHelperRefresh()
{
    const auto session = g_wsSession.load(std::memory_order_acquire);
    auto pending = g_refreshQueued.load();
    do {
        if (!WorkshopSessionActive(session) || pending == session) return;
    } while (!g_refreshQueued.compare_exchange_weak(pending, session));
    if (!PrismaCraftingThreading::AddGameTask([session]() {
        auto pending = session;
        if (!g_refreshQueued.compare_exchange_strong(pending, 0) || !WorkshopSessionActive(session)) return;
        PushHelperData();
    })) {
        auto pending = session;
        g_refreshQueued.compare_exchange_strong(pending, 0);
        logger::warn("PrismaCrafting: helper refresh task not queued (dispatch unavailable)");
    }
}

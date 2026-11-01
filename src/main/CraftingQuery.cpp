#include "RuntimeInternal.h"
#include "CobjCacheInternal.h"

static std::atomic<std::uint32_t> s_pageEpoch{ 0 };

void InvalidateCraftPageQuery()
{
    s_pageEpoch.fetch_add(1, std::memory_order_acq_rel);
}

std::string CraftPageEpoch()
{
    char token[16];
    std::snprintf(token, sizeof(token), "%08X", s_pageEpoch.load(std::memory_order_acquire));
    return token;
}

void RefreshCraftPageAfterUpgrade()
{
    if (!CraftSessionActive() || !g_api || !g_api->IsGameThread()) return;
    const auto view = ActiveView();
    if (!g_api->IsValid(view)) return;
    InvalidateCraftPageQuery();
    g_api->InteropCall(view, "setPageEpoch", CraftPageEpoch().c_str());
    g_api->InteropCall(view, "refreshPage", "");
}

void RequestCraftingPage(const char* arg)
{
    if (!CraftSessionActive() || !g_api || !g_api->IsGameThread()) return;
    const auto view = ActiveView();
    if (!g_api->IsValid(view)) return;
    const std::string request(arg ? arg : "");
    std::string parts[9];
    size_t pos = 0;
    for (int i = 0; i < 9; ++i) {
        const auto nl = request.find('\n', pos);
        parts[i] = request.substr(pos, nl == std::string::npos ? nl : nl - pos);
        if (nl == std::string::npos) {
            if (i != 8) return;
            break;
        }
        if (i == 8) return;
        pos = nl + 1;
    }
    const auto epoch = ParseHexOr(parts[7], 0);
    const auto session = CraftPageEpoch();
    const int id = ParseIntOr(parts[8], 0);
    if (!epoch || id <= 0 || parts[7] != session || epoch != s_pageEpoch.load(std::memory_order_acquire)) return;
    const int filter = ParseIntOr(parts[2], -1);
    const int sort = ParseIntOr(parts[3], -1);
    if (filter < 0 || filter > 3 || sort < 0 || sort > 1) return;

    static std::uint32_t queryEpoch = 0;
    static int queryId = 0;
    static std::string queryRequest;
    static CobjCache::RecipePageQuery query;
    static bool complete = false;
    if (queryEpoch != epoch) {
        queryEpoch = epoch;
        queryId = 0;
    }
    if (id < queryId) return;
    if (id > queryId) {
        queryId = id;
        queryRequest = request;
        query = {};
        complete = false;
    }
    if (complete || queryRequest != request) return;

    std::string search = parts[0];
    for (auto& c : search) c = c == '_' ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    search.erase(0, search.find_first_not_of(" \t\r\n"));
    search.erase(search.find_last_not_of(" \t\r\n") + 1);
    const auto category = parts[1].empty() ? std::string("ALL") : parts[1];
    const auto sub = parts[6].empty() ? std::string("ALL") : parts[6];
    const int page = ParseIntOr(parts[4], 0);
    int pageSize = ParseIntOr(parts[5], 0);
    if (pageSize <= 0 || pageSize > 500) pageSize = 60;
    auto payload = CobjCache::QueryRecipesPage(query, g_benchIds, search, category, sub,
        filter, sort, page, pageSize, GetBenchSourceRefs(), g_cookingBench.load());
    if (!CraftSessionActive() || epoch != s_pageEpoch.load(std::memory_order_acquire) ||
        view != ActiveView() || !g_api || !g_api->IsValid(view)) return;
    complete = !payload.is_null();
    if (!complete) payload = {{"pending", true}};
    payload["session"] = session;
    payload["requestId"] = id;
    const auto json = payload.dump(-1, ' ', false, CobjCache::Json::error_handler_t::replace);
    g_api->InteropCall(view, "setPage", json.c_str());
}

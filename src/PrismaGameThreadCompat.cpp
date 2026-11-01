namespace PrismaCraftingThreading {
namespace {

using Api = PRISMA_UI_API::IVPrismaUI11;

std::atomic<Api*> s_api{ nullptr };
std::atomic<bool> s_missingV11Logged{ false };

struct TaskBox
{
    std::function<void()> task;
};

void RunTask(void* userdata)
{
    std::unique_ptr<TaskBox> box(static_cast<TaskBox*>(userdata));
    if (!box) return;

    auto* api = s_api.load(std::memory_order_acquire);
    if (!api || !api->IsGameThread()) {
        logger::critical(
            "PrismaCrafting: verified dispatch callback arrived without game-thread ownership; mutation dropped");
        return;
    }

    try {
        box->task();
    } catch (const std::exception& e) {
        logger::error("PrismaCrafting: game-thread task threw: {}", e.what());
    } catch (...) {
        logger::error("PrismaCrafting: game-thread task threw an unknown exception");
    }
}

}

void SetApi(Api* api) noexcept
{
    s_api.store(api, std::memory_order_release);
    if (api) s_missingV11Logged.store(false, std::memory_order_release);
}

bool Dispatch(std::function<void()> task) noexcept
{
    if (!task) {
        return false;
    }

    auto* api = s_api.load(std::memory_order_acquire);
    if (!api) {
        if (!s_missingV11Logged.exchange(true, std::memory_order_acq_rel))
            logger::critical("PrismaCrafting: PrismaUI V11 is unavailable; game-thread task rejected");
        return false;
    }

    auto* box = new (std::nothrow) TaskBox{ std::move(task) };
    if (!box) {
        logger::critical("PrismaCrafting: verified game-thread task allocation failed");
        return false;
    }

    if (!api->DispatchToGameThread(&RunTask, box)) {
        delete box;
        logger::error("PrismaCrafting: PrismaUI V11 rejected game-thread task admission");
        return false;
    }

    return true;
}

bool IsGameThread() noexcept
{
    auto* api = s_api.load(std::memory_order_acquire);
    return api && api->IsGameThread();
}

}

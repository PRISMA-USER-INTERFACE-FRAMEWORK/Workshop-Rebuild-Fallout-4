#pragma once

#include "PrismaUI_F4_API.h"

#include <functional>
#include <utility>

namespace PrismaCraftingThreading {

bool Dispatch(std::function<void()> task) noexcept;

void SetApi(PRISMA_UI_API::IVPrismaUI11* api) noexcept;

bool IsGameThread() noexcept;

template <class F>
bool AddGameTask(F&& task) noexcept
{
    try {
        return Dispatch(std::function<void()>(std::forward<F>(task)));
    } catch (...) {
        return false;
    }
}

}

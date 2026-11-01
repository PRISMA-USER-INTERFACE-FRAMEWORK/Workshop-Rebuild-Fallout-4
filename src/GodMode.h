#pragma once

#include <atomic>

namespace crafting
{
    using GodModeProbe = bool (*)();

    inline std::atomic<GodModeProbe> s_godModeProbe{ nullptr };

    inline void SetGodModeProbe(GodModeProbe probe)
    {
        s_godModeProbe.store(probe, std::memory_order_release);
    }

    inline bool GodModeActive()
    {
        const auto probe = s_godModeProbe.load(std::memory_order_acquire);
        return probe && probe();
    }
}

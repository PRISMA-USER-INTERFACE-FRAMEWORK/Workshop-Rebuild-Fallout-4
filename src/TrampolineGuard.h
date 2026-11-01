#pragma once

#include <mutex>

inline void EnsurePrismaCraftingTrampoline()
{
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        REL::GetTrampoline().create(256);
        logger::info("PrismaCrafting: trampoline created (256 bytes, shared by every hook in this DLL)");
    });
}

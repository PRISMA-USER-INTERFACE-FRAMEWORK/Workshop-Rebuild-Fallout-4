#pragma once

inline void EnsurePrismaCraftingTrampoline()
{
    static bool s_created = false;
    if (s_created) return;

    REL::GetTrampoline().create(256);
    s_created = true;
    logger::info("PrismaCrafting: trampoline created (256 bytes, shared by every hook in this DLL)");
}

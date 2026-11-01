#pragma once

#include "PrismaUI_F4_API.h"

#include <atomic>

namespace WorkshopControllerInput
{
    [[nodiscard]] bool Install(std::atomic<PrismaView>* view,
                               std::atomic<bool>* active);
    void Activate();

    [[nodiscard]] bool InstallCrafting(std::atomic<PrismaView>* view);
    void ActivateCrafting();
    void PushCraftingPrompts(PrismaView view);
    void NoteViewFocused(PrismaView view);
    void NoteHeldObject(bool holding);
}

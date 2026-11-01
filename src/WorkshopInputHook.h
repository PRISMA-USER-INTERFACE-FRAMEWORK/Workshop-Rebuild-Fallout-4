#pragma once

#include "PrismaUI_F4_API.h"

#include <atomic>

namespace PRISMA_UI_API
{
    class IVPrismaUI11;
}

namespace WorkshopInputHook
{

    void Init(PRISMA_UI_API::IVPrismaUI11** apiPtr, std::atomic<PrismaView>* viewPtr);
    [[nodiscard]] bool Install();
    [[nodiscard]] bool IsInstalled();
    void SetActive(bool active);
    [[nodiscard]] bool IsActive();
    void OnClosed();
    bool IsFocused();
    [[nodiscard]] bool HoldingPlacement();

    void ToggleCursor();

    void OnPlacementStarted();
    void OnPlacementEnded(bool active);
bool IsTyping();

    void SetTextFocus(bool typing);

    void SetPanelRects(const char* json);
}

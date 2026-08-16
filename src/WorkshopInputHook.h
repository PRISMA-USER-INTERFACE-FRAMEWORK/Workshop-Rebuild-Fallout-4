#pragma once

#include "PrismaUI_F4_API.h"

#include <atomic>

namespace WorkshopInputHook
{

    void Init(PRISMA_UI_API::IVPrismaUI9** apiPtr, std::atomic<PrismaView>* viewPtr);
    void Install();
    void SetActive(bool active);
    void OnClosed();
    bool IsFocused();

    void ToggleCursor();

    void OnPlacementStarted();

    void SetTextFocus(bool typing);

    void SetPanelRects(const char* json);
}

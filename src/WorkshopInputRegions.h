#pragma once

#include "PrismaUI_F4_API.h"

#include <atomic>
#include <windows.h>

namespace WorkshopInputRegions
{
    void Init(PRISMA_UI_API::IVPrismaUI11** apiPtr, std::atomic<PrismaView>* viewPtr, HWND* hwndPtr);
    bool InPanel(HWND hwnd, int x, int y);
    void Apply();
    void Clear();
    void SetPanelRects(const char* json);
    void Reset();
}

#pragma once

#include "PrismaUI_F4_API.h"

#include <atomic>
#include <cstdint>

namespace WorkshopPlacementInput
{
    void Init(PRISMA_UI_API::IVPrismaUI11** apiPtr, std::atomic<PrismaView>* viewPtr);
    void SetActive(bool active);
    [[nodiscard]] bool BindPlaceEvent();
    bool HandlePlaceKey(std::uint16_t vk, bool up, bool placementActive, bool panelFocused);
    bool EscapeOwns(bool up, bool placementActive);
    void Reset();
}

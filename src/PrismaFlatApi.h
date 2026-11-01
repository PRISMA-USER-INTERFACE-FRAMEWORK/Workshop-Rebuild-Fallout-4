#pragma once

#include "PrismaUI_F4_API.h"
#include "PrismaUI_F4_Modern_API.h"

#include <cstdint>

namespace PrismaFlatApi
{
    bool Init() noexcept;
    [[nodiscard]] bool HasFeature(PRISMA_UI_FLAT_API::ApiFeature a_feature,
                                  std::uint32_t a_version) noexcept;

    bool SetCursorPolicy(PrismaView a_view, PRISMA_UI_API::CursorPolicy a_policy) noexcept;
    [[nodiscard]] PRISMA_UI_API::CursorPolicy GetCursorPolicy(PrismaView a_view) noexcept;
    [[nodiscard]] bool SupportsInputRegions() noexcept;
    bool RegisterTranslations(PrismaView a_view) noexcept;
}

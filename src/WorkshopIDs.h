#pragma once

#include <cstdint>

#include <REX/FModule.h>

#include <REL/ID.h>
#include <REL/VariantID.h>

namespace crafting::ids
{
    [[nodiscard]] inline bool WorkshopDataResolvable() noexcept
    {
        if (REX::FModule::IsRuntimeNG()) {
            const auto ver = REX::FModule::GetExecutingModule().GetFileVersion();
            return ver.major() == 1 && ver.minor() == 10 && ver.patch() == 984;
        }
        return REX::FModule::IsRuntimeOG() || REX::FModule::IsRuntimeAE();
    }

    inline constexpr REL::VariantID WorkshopMenuRoot{ 1421138, REL::Offset{ 0x2E75C20 }, 4797365 };

    inline constexpr REL::VariantID ActiveEdit{ 591432, REL::Offset{ 0x2E7499F }, 4797147 };

    inline constexpr REL::VariantID MultiSelect{ 816517, REL::Offset{ 0x2E749A0 }, 4797148 };

    inline constexpr REL::VariantID PlacementItemTranslLS{ 401486, REL::Offset{ 0x2E751A0 }, 4797253 };

    inline constexpr REL::VariantID PlacementCollisionCount{ 1013023, REL::Offset{ 0x2E751F0 }, 4797266 };

    inline constexpr REL::VariantID ActorSetGhost{ 1377793, 2229669, 2229669 };

    inline constexpr REL::VariantID VatsEffectControlDisable{ 845361, 2194804, 2194804 };

    inline constexpr REL::VariantID SetVATShaderColor{ 1565251, 2195650, 2195650 };

    inline constexpr REL::VariantID WorkshopCanSelectRef{ 1229851, 2194958, 2194958 };

    inline constexpr REL::VariantID PlaceCurrentReference{ 1058211, 2195155, 2195155 };
    inline constexpr REL::VariantID WorkshopFreeBuild{ 1583365, 2194924, 2194924 };
    inline constexpr REL::VariantID ContextDataCtor{ 1523699, 2195017, 2195017 };
    inline constexpr REL::VariantID MainRenderAEnd{ 468825, 2227555, 2227555 };
    inline constexpr REL::VariantID AutoAimPickRef{ 787300, 2227564, 2227564 };
    inline constexpr REL::VariantID RegisterForPlacementStatusEvent{ 1325826, 2194948, 2194948 };

    inline constexpr REL::VariantID CollectScrapRewards{ 1583041, 2194960, 2194960 };

    inline constexpr REL::VariantID CollectLinkedWorkshopContainers{ 911327, 2195116, 2195116 };

    inline constexpr REL::VariantID CurrentWorkshopHandle{ 737927, REL::Offset{ 0x2E75154 }, 4797241 };

    inline constexpr REL::VariantID ToggleModFavorite{ 1018602, 2248749, 2248749 };

}

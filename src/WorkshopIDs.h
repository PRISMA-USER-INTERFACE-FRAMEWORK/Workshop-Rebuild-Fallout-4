#pragma once

#include <cstdint>

#include <REX/FModule.h>

#include <REL/ID.h>
#include <REL/VariantID.h>

namespace crafting::ids
{
    [[nodiscard]] inline bool WorkshopDataResolvable() noexcept
    {
        return REX::FModule::IsRuntimeOG() || REX::FModule::IsRuntimeAE();
    }

    inline constexpr REL::VariantID WorkshopMenuRoot{ 1421138, 0, 4797365 };

    inline constexpr REL::VariantID ActiveEdit{ 591432, 0, 4797147 };

    inline constexpr REL::VariantID MultiSelect{ 816517, 0, 4797148 };

    inline constexpr REL::VariantID ActorSetGhost{ 1377793, 0, 2229669 };

    inline constexpr REL::VariantID VatsEffectControlDisable{ 845361, 0, 2194804 };

    inline constexpr REL::VariantID SetVATShaderColor{ 1565251, 0, 2195650 };

    inline constexpr REL::VariantID WorkshopCanSelectRef{ 1229851, 0, 2194958 };

    inline constexpr REL::VariantID PlaceCurrentReference{ 1058211, 0, 2195155 };
    inline constexpr REL::VariantID WorkshopFreeBuild{ 1583365, 0, 2194924 };
    inline constexpr REL::VariantID ContextDataCtor{ 1523699, 0, 2195017 };
    inline constexpr REL::VariantID MainRenderAEnd{ 468825, 0, 2227555 };
    inline constexpr REL::VariantID AutoAimPickRef{ 787300, 0, 2227564 };
    inline constexpr REL::VariantID RegisterForPlacementStatusEvent{ 1325826, 0, 2194948 };

    inline constexpr REL::VariantID CollectScrapRewards{ 1583041, 0, 2194960 };

    inline constexpr REL::VariantID CollectLinkedWorkshopContainers{ 911327, 0, 2195116 };

    inline constexpr REL::VariantID CurrentWorkshopHandle{ 737927, 0, 4797241 };

    inline constexpr REL::VariantID ToggleModFavorite{ 1018602, 0, 2248749 };

}

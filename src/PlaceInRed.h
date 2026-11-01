#pragma once

#include <cstdint>

namespace PlaceInRed
{
    struct Capabilities
    {
        bool placementStatus{ false };
        bool yellowMovement{ false };
        bool groundSnap{ false };
        bool objectSnap{ false };
    };

    void Init();

    [[nodiscard]] bool Available();
    [[nodiscard]] bool PlaceInRedOn();
    [[nodiscard]] Capabilities GetCapabilities();
    [[nodiscard]] bool YellowMovementAvailable();
    [[nodiscard]] bool GroundSnapAvailable();
    [[nodiscard]] bool ObjectSnapAvailable();

    void SetPlaceInRed(bool on);

    bool SetGroundSnap(bool enabled);
    bool SetObjectSnap(bool enabled);

    void HoldValid();

    void RestoreAll();
}

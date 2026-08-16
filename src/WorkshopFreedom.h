#pragma once

#include <cstdint>

namespace WorkshopFreedom
{
    void Init();
    void InstallHooks();
    void SetWorkshopActive(bool active);

    void Tick();

    void ToggleFreePlace();
    void ToggleFreeCamera();
    void CycleSnap();
    void ToggleObjectSnap();
    void ResetTransform();
    void NudgeScale(float delta);
    void NudgeRotate(int axis, float degrees);
    void NudgeMove(int axis, float units);

    [[nodiscard]] bool FreePlaceEnabled();
    [[nodiscard]] bool FreeCameraEnabled();
    [[nodiscard]] bool GroundSnapEnabled();
    [[nodiscard]] bool ObjectSnapEnabled();

    void SetToggleChangedCallback(void (*fn)());
    void SetPreUpdateHook(void (*fn)());

    [[nodiscard]] bool HandleKey(std::uint16_t vk, bool keyUp);
    [[nodiscard]] bool OwnsKey(std::uint16_t vk);
}

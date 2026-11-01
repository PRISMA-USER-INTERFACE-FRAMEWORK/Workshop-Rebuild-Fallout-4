#pragma once

#include <atomic>
#include <utility>

namespace WorkshopFreedom
{
    extern std::atomic<bool> s_enabled;
    extern std::atomic<bool> s_workshopActive;
    extern std::atomic<bool> s_camFreezeTime;
    extern std::atomic<bool> s_heldPlace;
    extern std::atomic<bool> s_heldCam;
    extern std::atomic<float> s_freeCamDistance;
    extern std::atomic<float> s_freeCamSpeed;

    template <class F>
    void PostGameTask(const char* a_what, F&& a_task)
    {
        if (!PrismaCraftingThreading::AddGameTask(std::forward<F>(a_task)))
            logger::warn("WorkshopFreedom: game-thread task '{}' not queued (dispatch unavailable)", a_what);
    }
}

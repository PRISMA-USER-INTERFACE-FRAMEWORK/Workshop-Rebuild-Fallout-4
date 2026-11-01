#include "WorkshopPlacementInput.h"

#include "DebugLog.h"
#include "PlaceInRed.h"
#include "PrismaGameThreadCompat.h"
#include "WorkshopIDs.h"

#include <RE/P/PlayerCharacter.h>
#include <RE/W/Workshop.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace WorkshopPlacementInput
{
    static PRISMA_UI_API::IVPrismaUI11** s_api = nullptr;
    static std::atomic<PrismaView>* s_view = nullptr;
    static std::atomic<PrismaView> s_boundView{ 0 };
    static std::atomic<std::uint64_t> s_bindGeneration{ 0 };
    static std::atomic<std::uint64_t> s_retryGeneration{ 0 };
    static std::atomic<bool> s_active{ false };
    static std::atomic<bool> s_escape{ false };
    static std::atomic<bool> s_placeE{ false };
    static std::atomic<bool> s_placeEnter{ false };

    static constexpr unsigned kBindRetryAttempts = 50;

    static void PlaceCurrentGhost(const char*, void*);

    static PrismaView CurrentView()
    {
        return s_view ? s_view->load(std::memory_order_acquire) : 0;
    }

    static bool ApiOk(PrismaView view)
    {
        return s_api && *s_api && view && (*s_api)->IsValid(view);
    }

    static bool RetryCurrent(PrismaView view, std::uint64_t generation)
    {
        return s_active.load(std::memory_order_acquire) &&
               s_bindGeneration.load(std::memory_order_acquire) == generation &&
               CurrentView() == view &&
               ApiOk(view);
    }

    static bool BindPlaceEventFor(PrismaView view)
    {
        if (!ApiOk(view)) return false;
        if (s_boundView.load(std::memory_order_acquire) == view) return true;
        if ((*s_api)->BindGameThreadUIEvent(view, "prisma_place", &PlaceCurrentGhost, nullptr)) {
            s_boundView.store(view, std::memory_order_release);
            logger::info("WorkshopPlacementInput: bound game-thread UI event 'prisma_place' view={}", view);
            return true;
        }
        logger::error("WorkshopPlacementInput: failed to bind game-thread UI event 'prisma_place'");
        return false;
    }

    static void StartBindRetry(PrismaView view, std::uint64_t generation)
    {
        if (!RetryCurrent(view, generation)) return;
        if (s_retryGeneration.exchange(generation, std::memory_order_acq_rel) == generation) return;

        std::thread([view, generation] {
            for (unsigned attempt = 0; attempt < kBindRetryAttempts; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!RetryCurrent(view, generation)) return;
                PrismaCraftingThreading::Dispatch([view, generation] {
                    if (RetryCurrent(view, generation)) BindPlaceEventFor(view);
                });
                if (s_boundView.load(std::memory_order_acquire) == view) return;
            }
            if (RetryCurrent(view, generation) && s_boundView.load(std::memory_order_acquire) != view)
                logger::error("WorkshopPlacementInput: bounded 'prisma_place' binding retry exhausted view={} generation={}", view, generation);
        }).detach();
    }

    static void PlaceCurrentGhost(const char*, void*)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* handle = RE::Workshop::GetPlacementItem();
        const bool placeInRed = PlaceInRed::PlaceInRedOn();
        const bool hasGhost = player && player->parentCell && handle && handle->get();
        const bool canPlace = hasGhost && RE::Workshop::CanPlaceItem();
        if (!hasGhost || (!canPlace && !placeInRed)) {
            logger::warn("WorkshopPlacementInput: UI Place requested without a placeable active ghost");
            return;
        }
        if (placeInRed) PlaceInRed::HoldValid();
        RE::Workshop::ContextData ctx(player);
        const bool placed = RE::Workshop::PlaceCurrentReference(ctx);
        logger::info("WorkshopPlacementInput: UI Place requested -- native commit {} (placeInRed={})",
                     placed ? "accepted" : "rejected", placeInRed);
    }

    void Init(PRISMA_UI_API::IVPrismaUI11** apiPtr, std::atomic<PrismaView>* viewPtr)
    {
        s_api = apiPtr;
        s_view = viewPtr;
    }

    void SetActive(bool active)
    {
        s_active.store(active, std::memory_order_release);
        s_bindGeneration.fetch_add(1, std::memory_order_acq_rel);
        if (!active) {
            s_escape.store(false, std::memory_order_release);
            s_placeE.store(false, std::memory_order_release);
            s_placeEnter.store(false, std::memory_order_release);
        }
    }

    bool BindPlaceEvent()
    {
        const PrismaView view = CurrentView();
        const std::uint64_t generation = s_bindGeneration.load(std::memory_order_acquire);
        if (BindPlaceEventFor(view)) return true;
        StartBindRetry(view, generation);
        return false;
    }

    bool HandlePlaceKey(std::uint16_t vk, bool up, bool placementActive, bool panelFocused)
    {
        if (vk != 'E' && vk != VK_RETURN) return false;
        auto* held = vk == 'E' ? &s_placeE : &s_placeEnter;
        if (up) return held->exchange(false, std::memory_order_acq_rel);
        if (!panelFocused || !placementActive) return false;
        if (held->exchange(true, std::memory_order_acq_rel)) return true;
        const PrismaView view = CurrentView();
        const std::uint64_t generation = s_bindGeneration.load(std::memory_order_acquire);
        if (!PrismaCraftingThreading::Dispatch([view, generation] {
            if (RetryCurrent(view, generation)) PlaceCurrentGhost(nullptr, nullptr);
        })) {
            held->store(false, std::memory_order_release);
            logger::error("WorkshopPlacementInput: focused placement request could not be queued");
        }
        return true;
    }

    bool EscapeOwns(bool up, bool placementActive)
    {
        if (up) return s_escape.exchange(false, std::memory_order_acq_rel);
        if (!placementActive || s_escape.exchange(true, std::memory_order_acq_rel)) return placementActive;
        const PrismaView view = CurrentView();
        const std::uint64_t generation = s_bindGeneration.load(std::memory_order_acquire);
        if (PrismaCraftingThreading::Dispatch([view, generation] {
            if (RetryCurrent(view, generation))
                (*s_api)->Invoke(view, "(function(){if(window.prisma_cancel)window.prisma_cancel('');})();", nullptr);
        }))
            return true;
        s_escape.store(false, std::memory_order_release);
        logger::error("WorkshopPlacementInput: placement Escape could not be queued");
        return false;
    }

    void Reset()
    {
        s_active.store(false, std::memory_order_release);
        s_bindGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_escape.store(false, std::memory_order_release);
        s_placeE.store(false, std::memory_order_release);
        s_placeEnter.store(false, std::memory_order_release);
        s_boundView.store(0, std::memory_order_release);
    }
}

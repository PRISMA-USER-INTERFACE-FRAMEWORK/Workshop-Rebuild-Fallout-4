#include "PCH.h"

#include "WorkshopFreedom.h"
#include "WorkshopFreedomInternal.h"

#include "FunctionBounds.h"

#include "DebugLog.h"
#include "WorkshopIDs.h"
#include "WorkshopInputHook.h"

#include "PlaceInRed.h"
#include "PrismaGameThreadCompat.h"
#include "TrampolineGuard.h"

#include <atomic>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace WorkshopFreedom
{
    std::atomic<bool> s_enabled{ true };
    std::atomic<bool> s_camFreezeTime{ false };
    std::atomic<float> s_freeCamDistance{ 0.0f };
    std::atomic<float> s_freeCamSpeed{ 10.0f };
    std::atomic<bool> s_workshopActive{ false };
    std::atomic<bool> s_heldPlace{ false };
    std::atomic<bool> s_heldCam{ false };
    std::atomic<bool> s_heldSnap{ false };
    namespace
    {

        constexpr float kDeg2Rad = 0.01745329252f;

        std::atomic<bool> s_freePlace{ false };
        std::atomic<bool> s_freeCam{ false };
        std::atomic<bool> s_groundSnapOn{ true };
        std::atomic<bool> s_objectSnapOn{ true };

        std::atomic<void (*)()> s_toggleChanged{ nullptr };
        void NotifyToggles() { if (auto* fn = s_toggleChanged.load()) fn(); }

        std::atomic<void (*)()> s_preUpdate{ nullptr };

        std::atomic<float> s_scale{ 1.0f };
        std::atomic<float> s_yaw{ 0.0f }, s_pitch{ 0.0f }, s_roll{ 0.0f };
        std::atomic<float> s_offFwd{ 0.0f }, s_offRight{ 0.0f }, s_offUp{ 0.0f };
        std::atomic<bool> s_manualZ{ false };
        std::atomic<bool> s_zResetPending{ false };
        std::atomic<std::uint32_t> s_heldHandle{ 0 };

        void RunConsoleCommand(const std::string& cmd)
        {
            // Constraint: Console::ExecuteCommand has OG and AE ids; CommonLib repeats the last value for the third runtime slot (UNVERIFIED in game).
            const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
            if (version != REL::Version{ 1, 10, 163, 0 } && version < REL::Version{ 1, 11, 191, 0 }) {
                logger::warn("WorkshopFreedom: no verified console entry for runtime {}; skipping '{}'",
                             version.string(), cmd);
                return;
            }
            RE::Console::ExecuteCommand(cmd.c_str());
            logger::info("WorkshopFreedom: ran console command '{}'", cmd);
        }

        std::atomic<NoticeSink> s_noticeSink{ nullptr };
        std::atomic<bool (*)()> s_ownsPlacement{ nullptr };
        void Notify(const char* key, std::initializer_list<std::pair<const char*, std::string>> vars = {})
        {
            auto varsJson = nlohmann::json::object();
            std::string text = key;
            for (const auto& [name, value] : vars) {
                varsJson[name] = value;
                const std::string token = std::string("{{") + name + "}}";
                if (const auto at = text.find(token); at != std::string::npos) text.replace(at, token.size(), value);
            }
            const auto payload = nlohmann::json{{"key", key}, {"vars", varsJson}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            if (auto* fn = s_noticeSink.load(); fn && fn(payload.c_str())) return;
            RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, false);
        }

        void ResetOffsets()
        {
            s_yaw.store(0.0f); s_pitch.store(0.0f); s_roll.store(0.0f);
            s_offFwd.store(0.0f); s_offRight.store(0.0f); s_offUp.store(0.0f);
            s_manualZ.store(false);
            s_scale.store(1.0f);
        }

        void SyncHeld(RE::Workshop::PlacementItemData* d)
        {
            const std::uint32_t handle = d->placementItem.get_handle();
            if (!handle) return;
            if (s_heldHandle.exchange(handle) == handle) return;

            const bool hadManualZ = s_manualZ.load();
            ResetOffsets();
            s_zResetPending.store(true);
            if (hadManualZ) {

                const bool desiredGroundSnap = s_groundSnapOn.load();
                PostGameTask("restore-ground-snap", [desiredGroundSnap] {
                    if (s_workshopActive.load()) PlaceInRed::SetGroundSnap(desiredGroundSnap);
                });
                logger::info("WorkshopFreedom: manual vertical placement cleared for new held object");
            }

            if (auto ref = d->placementItem.get()) {
                const auto& p = ref->data.location;
                logger::info("WorkshopFreedom: ghost picked -- ref {:08X} has3D={} pos ({:.0f} {:.0f} {:.0f}) "
                             "scale {:.2f} groundSnap={} objectSnap={} placeInRed={}",
                             ref->GetFormID(), ref->Get3D() != nullptr, p.x, p.y, p.z,
                             s_scale.load(), s_groundSnapOn.load(), s_objectSnapOn.load(), s_freePlace.load());
            } else {
                logger::warn("WorkshopFreedom: ghost handle {:08X} resolves to no reference", handle);
            }
        }
    }

    namespace
    {
        using FreeBuildFn = bool (*)();
        FreeBuildFn g_origFreeBuildAtPlaceCurrentReference = nullptr;
        std::uintptr_t FindUniqueCallSite(const char* a_label, std::uintptr_t a_caller,
                                          std::uintptr_t a_callee)
        {
            const auto segments = crafting::bounds::FunctionSegmentsAt(a_caller);
            if (segments.empty()) {
                logger::error("WorkshopFreedom: {} -- the exception directory does not describe the "
                              "caller at {:#x}, so its extent is unknown. Not hooking: a scan with "
                              "a guessed end can match inside a different function.",
                              a_label, a_caller);
                return 0;
            }

            std::uintptr_t found = 0;
            std::size_t count = 0;
            for (const auto& seg : segments) {
                for (auto at = seg.begin; at + 5 <= seg.end; ++at) {
                    if (*reinterpret_cast<const std::uint8_t*>(at) != 0xE8) continue;
                    const auto rel = *reinterpret_cast<const std::int32_t*>(at + 1);
                    if (at + 5 + static_cast<std::intptr_t>(rel) != a_callee) continue;
                    if (++count == 1) found = at;
                }
            }
            if (count != 1) {
                logger::error("WorkshopFreedom: {} -- expected exactly one call site across the {} "
                              "segment(s) of the function at {:#x}, found {}",
                              a_label, segments.size(), a_caller, count);
                return 0;
            }
            DLOG(Workshop, "{} call site -> {:#x}", a_label, found);
            return found;
        }

        constexpr REL::VariantID kFreeBuild = crafting::ids::WorkshopFreeBuild;

        bool Detour_FreeBuildAtPlaceCurrentReference()
        {
            if (s_freePlace.load()) return true;
            if (const auto owns = s_ownsPlacement.load(); owns && owns()) return true;
            return g_origFreeBuildAtPlaceCurrentReference ?
                g_origFreeBuildAtPlaceCurrentReference() : false;
        }

        struct ViewPoint { float x, y; const char* label; };
        constexpr ViewPoint kViewCentres[2] = {
            { 0.5f, 0.5f, "0..1" },
            { 0.0f, 0.0f, "-1..1" },
        };
        std::atomic<int> s_viewCentre{ 0 };
        std::atomic<bool> s_viewCentreMeasured{ false };

        bool Normalize(RE::NiPoint3& v)
        {
            const float len2 = v.SqrLength();
            if (!std::isfinite(len2) || len2 < 1e-8f) return false;
            const float inv = 1.0f / std::sqrt(len2);
            v.x *= inv; v.y *= inv; v.z *= inv;
            return true;
        }

        bool CrosshairRay(RE::NiCamera* cam, RE::NiPoint3& origin, RE::NiPoint3& dir)
        {
            const auto& c = kViewCentres[s_viewCentre.load()];
            cam->ViewPointToRay(c.x, c.y, &origin, &dir);
            return std::isfinite(origin.x) && std::isfinite(origin.y) &&
                   std::isfinite(origin.z) && Normalize(dir);
        }

        void MeasureViewCentre(RE::NiCamera* cam, const RE::NiPoint3& engineLookDir)
        {
            RE::NiPoint3 ref = engineLookDir;
            if (!Normalize(ref)) return;

            int best = -1;
            float bestDot = -2.0f;
            float dots[2]{};
            for (int i = 0; i < 2; ++i) {
                RE::NiPoint3 org{}, dir{};
                cam->ViewPointToRay(kViewCentres[i].x, kViewCentres[i].y, &org, &dir);
                if (!Normalize(dir)) { dots[i] = -2.0f; continue; }
                dots[i] = dir.x * ref.x + dir.y * ref.y + dir.z * ref.z;
                if (dots[i] > bestDot) { bestDot = dots[i]; best = i; }
            }
            if (best < 0 || bestDot < 0.9f) return;

            const bool changed = s_viewCentre.exchange(best) != best;
            const bool first = !s_viewCentreMeasured.exchange(true);
            if (changed || first)
                logger::info("WorkshopFreedom: viewport centre convention = {} (dot {:.3f}; 0..1 {:.3f}, -1..1 {:.3f})",
                             kViewCentres[best].label, bestDot, dots[0], dots[1]);
        }

        std::atomic<bool> s_freeCamDiag{ false };
        std::atomic<bool> s_freeCamPickDiag{ false };

        const char* PlacementStatusName(RE::Workshop::PlacementStatusValue v)
        {
            using S = RE::Workshop::PlacementStatusValue;
            switch (v) {
                case S::kNoEditItem: return "no edit item";
                case S::kValidPlacement: return "VALID";
                case S::kFloating: return "floating";
                case S::kIntersecting: return "intersecting";
                case S::kTerrainOnly: return "terrain only";
                case S::kWaterRestriction: return "water restriction";
                case S::kSplineTooLong: return "spline too long";
                case S::kOutsideBuildArea: return "outside build area";
                case S::kRedundantSpline: return "redundant spline";
                case S::kAttachedSplineInvalid: return "attached spline invalid";
                case S::kUnsupported: return "unsupported";
                case S::kMustSnap: return "must snap";
                case S::kRadiusOverlap: return "radius overlap";
                case S::kTimer: return "timer";
                default: return "unknown";
            }
        }

        class PlacementStatusSink : public RE::BSTEventSink<RE::Workshop::PlacementStatusEvent>
        {
        public:
            static PlacementStatusSink* GetSingleton() { static PlacementStatusSink s; return &s; }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::Workshop::PlacementStatusEvent& event,
                RE::BSTEventSource<RE::Workshop::PlacementStatusEvent>*) override
            {
                const auto v = static_cast<std::int8_t>(event.value);
                if (m_last.exchange(v) != v)
                    logger::info("WorkshopFreedom: placement status -> {} ({})",
                                 PlacementStatusName(event.value), v);
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            std::atomic<std::int8_t> m_last{ -1 };
        };

        using RegisterPlacementStatusFn =
            void (*)(RE::BSTEventSink<RE::Workshop::PlacementStatusEvent>*);
        constexpr REL::VariantID kRegisterForPlacementStatusEvent =
            crafting::ids::RegisterForPlacementStatusEvent;

        using ContextDataCtorFn = RE::Workshop::ContextData* (*)(RE::Workshop::ContextData*, RE::Actor*);
        ContextDataCtorFn g_origContextDataCtor = nullptr;
        ContextDataCtorFn g_origPickContextDataCtor = nullptr;
        constexpr REL::VariantID kContextDataCtor = crafting::ids::ContextDataCtor;

        RE::Workshop::ContextData* Detour_ContextDataCtor(RE::Workshop::ContextData* ctx, RE::Actor* actor)
        {
            if (g_origContextDataCtor) g_origContextDataCtor(ctx, actor);
            if (!s_workshopActive.load()) return ctx;
            auto lookPos = ctx->lookPos, lookDir = ctx->lookDir;
            float zAngle = ctx->zAngle;

            auto* placement = RE::Workshop::GetCurrentPlacementItemData();
            if (placement) {
                SyncHeld(placement);
                if (s_manualZ.load()) {
                    placement->zOffset = s_offUp.load();
                } else if (s_zResetPending.exchange(false)) {
                    placement->zOffset = 0.0f;
                }
            }

            auto* cam = RE::Main::WorldRootCamera();
            if (cam && !s_freeCam.load()) MeasureViewCentre(cam, lookDir);

            if (cam && s_freeCam.load()) {
                RE::NiPoint3 org{}, dir{};
                if (CrosshairRay(cam, org, dir)) {
                    lookPos = org;
                    lookDir = dir;
                    const float dist = s_freeCamDistance.load();
                    if (dist != 0.0f) {
                        lookPos.x += dir.x * dist;
                        lookPos.y += dir.y * dist;
                        lookPos.z += dir.z * dist;
                    }
                    const RE::NiPoint3 flat{ dir.x, dir.y, 0.0f };
                    if (flat.SqrLength() > 1e-6f) zAngle = flat.GetZAngleFromVector();

                    if (s_freeCamDiag.exchange(false))
                        logger::info("WorkshopFreedom: free-cam aim | centre {} | origin ({:.0f} {:.0f} {:.0f}) | "
                                     "dir ({:.3f} {:.3f} {:.3f}) | reach {:.0f}",
                                     kViewCentres[s_viewCentre.load()].label, org.x, org.y, org.z,
                                     dir.x, dir.y, dir.z, s_freeCamDistance.load());
                }
            }

            const float offF = s_offFwd.load(), offR = s_offRight.load();
            if (offF != 0.0f || offR != 0.0f) {
                RE::NiPoint3 f{ lookDir.x, lookDir.y, 0.0f };
                const float len2 = f.SqrLength();
                if (len2 > 1e-6f) {
                    const float inv = 1.0f / std::sqrt(len2);
                    f.x *= inv; f.y *= inv;
                } else {
                    f = { 0.0f, 1.0f, 0.0f };
                }
                lookPos.x += f.x * offF + f.y * offR;
                lookPos.y += f.y * offF - f.x * offR;
            }

            zAngle += s_yaw.load() * kDeg2Rad;
            if (std::isfinite(lookPos.x) && std::isfinite(lookPos.y) && std::isfinite(lookPos.z) &&
                std::isfinite(lookDir.x) && std::isfinite(lookDir.y) && std::isfinite(lookDir.z) && std::isfinite(zAngle)) {
                ctx->lookPos = lookPos; ctx->lookDir = lookDir; ctx->zAngle = zAngle;
            }
            return ctx;
        }

        RE::Workshop::ContextData* Detour_PickContextDataCtor(
            RE::Workshop::ContextData* ctx, RE::Actor* actor)
        {
            if (g_origPickContextDataCtor) g_origPickContextDataCtor(ctx, actor);
            if (!s_workshopActive.load() || !s_freeCam.load()) return ctx;

            auto* cam = RE::Main::WorldRootCamera();
            if (!cam) return ctx;
            RE::NiPoint3 org{}, dir{};
            if (!CrosshairRay(cam, org, dir)) return ctx;
            ctx->lookPos = org;
            ctx->lookDir = dir;

            if (s_freeCamPickDiag.exchange(false))
                logger::info("WorkshopFreedom: free-cam highlight ray | origin ({:.0f} {:.0f} {:.0f}) | "
                             "dir ({:.3f} {:.3f} {:.3f})",
                             org.x, org.y, org.z, dir.x, dir.y, dir.z);
            return ctx;
        }

        using WorkshopUpdateFn = void (*)(RE::Workshop::ContextData*);
        WorkshopUpdateFn g_origWorkshopUpdate = nullptr;
        constexpr REL::VariantID kWorkshopUpdate = RE::ID::Workshop::Update;
        thread_local std::uint32_t s_workshopUpdateDepth = 0; std::atomic<bool> s_tiltApplied{ false };

        void Detour_WorkshopUpdate(RE::Workshop::ContextData* ctx)
        {
            if (s_workshopUpdateDepth++ != 0) { logger::critical("WorkshopFreedom: re-entrant Workshop::Update detected; skipping deferred pre-update"); if (g_origWorkshopUpdate) g_origWorkshopUpdate(ctx); s_workshopUpdateDepth -= 1; return; } else if (auto* fn = s_preUpdate.load()) fn();
            if (g_origWorkshopUpdate) g_origWorkshopUpdate(ctx);
            if (!s_workshopActive.load()) { s_workshopUpdateDepth -= 1; return; }

            PlaceInRed::HoldValid();

            const float pitch = s_pitch.load(), roll = s_roll.load();
            const bool tilted = pitch != 0.0f || roll != 0.0f;
            if ((!tilted && !s_tiltApplied.load()) || !WorkshopInputHook::HoldingPlacement()) { s_workshopUpdateDepth -= 1; return; }

            auto* pd = RE::Workshop::GetCurrentPlacementItemData();
            if (!pd) { s_workshopUpdateDepth -= 1; return; }
            auto ref = pd->placementItem.get();
            if (!ref) { s_workshopUpdateDepth -= 1; return; }
            ref->data.angle.x = pitch * kDeg2Rad;
            ref->data.angle.y = roll * kDeg2Rad;
            ref->Update3DPosition(false);
            s_tiltApplied.store(tilted); s_workshopUpdateDepth -= 1;
        }

        void* HookCallSite(const char* label, std::uintptr_t a_siteAddr,
                           std::uintptr_t targetAddr, void* detour)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_siteAddr);
            bool ok = p[0] == 0xE8;
            if (ok) {
                const auto rel = *reinterpret_cast<const std::int32_t*>(p + 1);
                const auto target = a_siteAddr + 5 + static_cast<std::intptr_t>(rel);
                ok = target == targetAddr;
            }
            if (!ok) {
                logger::critical("WorkshopFreedom: REFUSING {} hook -- address {:#x} is not a CALL to "
                                 "{:#x}. Bytes {:02x} {:02x} {:02x} {:02x} {:02x}",
                                 label, a_siteAddr, targetAddr, p[0], p[1], p[2], p[3], p[4]);
                return nullptr;
            }
            return reinterpret_cast<void*>(REL::GetTrampoline().write_call<5>(a_siteAddr, detour));
        }
    }

    bool InstallHooks()
    {
        static bool installed = false;
        if (installed) return HooksReady();

        if (!crafting::ids::WorkshopDataResolvable()) {
            logger::error("WorkshopFreedom: workshop hooks NOT installed -- unsupported runtime");
            return false;
        }

        EnsurePrismaCraftingTrampoline();

        const auto placeRef = crafting::ids::PlaceCurrentReference.address();
        const auto mainRender = crafting::ids::MainRenderAEnd.address();
        const auto autoAim = crafting::ids::AutoAimPickRef.address();

        if (const auto site = FindUniqueCallSite("FreeBuild/PlaceCurrentReference", placeRef,
                                                 kFreeBuild.address()))
            g_origFreeBuildAtPlaceCurrentReference = reinterpret_cast<FreeBuildFn>(HookCallSite(
                "FreeBuild/PlaceCurrentReference", site, kFreeBuild.address(),
                &Detour_FreeBuildAtPlaceCurrentReference));
        if (const auto site = FindUniqueCallSite("placement ContextData", mainRender,
                                                 kContextDataCtor.address()))
            g_origContextDataCtor = reinterpret_cast<ContextDataCtorFn>(HookCallSite(
                "placement ContextData", site, kContextDataCtor.address(), &Detour_ContextDataCtor));
        if (const auto site = FindUniqueCallSite("highlight ContextData", autoAim,
                                                 kContextDataCtor.address()))
            g_origPickContextDataCtor = reinterpret_cast<ContextDataCtorFn>(HookCallSite(
                "highlight ContextData", site, kContextDataCtor.address(), &Detour_PickContextDataCtor));
        if (const auto site = FindUniqueCallSite("Workshop::Update", mainRender,
                                                 kWorkshopUpdate.address()))
            g_origWorkshopUpdate = reinterpret_cast<WorkshopUpdateFn>(HookCallSite(
                "Workshop::Update", site, kWorkshopUpdate.address(), &Detour_WorkshopUpdate));

        PlaceInRed::Init();

        REL::Relocation<RegisterPlacementStatusFn> reg{ kRegisterForPlacementStatusEvent };
        reg(PlacementStatusSink::GetSingleton());

        const auto caps = PlaceInRed::GetCapabilities();
        logger::info("WorkshopFreedom: hooks 1.10.163 | freeBuild={} placementCtx={} pickCtx={} wsUpdate={} | "
                     "PIR placement={} yellow={} groundSnap={} objectSnap={}",
                     g_origFreeBuildAtPlaceCurrentReference != nullptr,
                     g_origContextDataCtor != nullptr,
                     g_origPickContextDataCtor != nullptr,
                     g_origWorkshopUpdate != nullptr,
                     caps.placementStatus, caps.yellowMovement, caps.groundSnap, caps.objectSnap);

        installed = g_origWorkshopUpdate != nullptr && g_origFreeBuildAtPlaceCurrentReference != nullptr;
        if (!installed)
            logger::critical("WorkshopFreedom: required Workshop::Update or FreeBuild/PlaceCurrentReference hook is unavailable; replacement disabled");
        return installed;
    }

    bool HooksReady() { return g_origWorkshopUpdate != nullptr && g_origFreeBuildAtPlaceCurrentReference != nullptr; }

    void SetWorkshopActive(bool active)
    {
        if (active) {
            s_workshopActive.store(true, std::memory_order_release);
            return;
        }

        if (s_freeCam.load(std::memory_order_acquire)) {
            const bool freeze = s_camFreezeTime.load(std::memory_order_acquire);
            const auto exitFreeCamera = [freeze] {
                if (!PrismaCraftingThreading::IsGameThread()) {
                    logger::critical("WorkshopFreedom: free-camera teardown reached without verified game-thread ownership");
                    return;
                }
                auto* cam = RE::PlayerCamera::GetSingleton();
                if (!cam) {
                    logger::critical("WorkshopFreedom: cannot leave free camera during workshop teardown -- PlayerCamera unavailable");
                    return;
                }
                cam->ToggleFreeCameraMode(freeze);
                s_freeCam.store(false, std::memory_order_release);
                s_heldCam.store(false, std::memory_order_release);
                logger::info("WorkshopFreedom: free camera OFF during workshop teardown");
                NotifyToggles();
            };

            if (PrismaCraftingThreading::IsGameThread()) {
                exitFreeCamera();
            } else if (!PrismaCraftingThreading::AddGameTask(exitFreeCamera)) {
                logger::critical("WorkshopFreedom: free-camera teardown task not queued; camera ownership retained");
            }
        }

        s_workshopActive.store(false, std::memory_order_release);
        s_heldPlace.store(false, std::memory_order_release);
        s_heldCam.store(false, std::memory_order_release);
        s_heldSnap.store(false, std::memory_order_release);
        s_tiltApplied.store(false);
        s_heldHandle.store(0);
        const auto restorePlaceInRed = [] {
            if (!PrismaCraftingThreading::IsGameThread()) {
                logger::critical("WorkshopFreedom: place-in-red teardown reached without verified game-thread ownership");
                return;
            }
            PlaceInRed::RestoreAll();
            s_freePlace.store(false);
            s_groundSnapOn.store(true);
            s_objectSnapOn.store(true);
            ResetOffsets();
            s_zResetPending.store(false);
            NotifyToggles();
        };
        if (PrismaCraftingThreading::IsGameThread()) {
            restorePlaceInRed();
        } else if (!PrismaCraftingThreading::AddGameTask(restorePlaceInRed)) {
            logger::critical("WorkshopFreedom: place-in-red teardown task not queued; patch ownership retained");
        }
    }

    bool FreePlaceEnabled() { return s_freePlace.load(); }
    bool FreeCameraEnabled() { return s_freeCam.load(); }
    bool GroundSnapEnabled() { return s_groundSnapOn.load(); }
    bool ObjectSnapEnabled() { return s_objectSnapOn.load(); }

    void SetToggleChangedCallback(void (*fn)()) { s_toggleChanged.store(fn); }
    void SetPreUpdateHook(void (*fn)()) { s_preUpdate.store(fn); }
    void SetNoticeSink(NoticeSink fn) { s_noticeSink.store(fn); }
    void SetPlacementOwnerQuery(bool (*fn)()) { s_ownsPlacement.store(fn); }

    void Tick() {}

    void ToggleFreePlace()
    {
        if (!s_enabled.load() || !s_workshopActive.load()) return;
        if (!PlaceInRed::Available() || !g_origFreeBuildAtPlaceCurrentReference) {
            Notify("Place in red unavailable (1.10.163 placement/commit hook not armed)");
            logger::error("WorkshopFreedom: F3 refused - placementStatus={} freeBuildHook={}",
                          PlaceInRed::Available(), g_origFreeBuildAtPlaceCurrentReference != nullptr);
            return;
        }

        const bool now = !s_freePlace.load();
        s_freePlace.store(now);
        PlaceInRed::SetPlaceInRed(now);

        if (now && !PlaceInRed::YellowMovementAvailable()) {
            Notify("Place in red ON - yellow pickup patch unavailable");
            logger::error("WorkshopFreedom: F3 enabled but yellow-movement capability is missing");
        } else {
            Notify(now ? "Place in red ON" : "Place in red OFF");
        }
        NotifyToggles();
    }

    void ToggleFreeCamera()
    {
        if (!s_enabled.load() || !s_workshopActive.load()) return;
        if (!g_origContextDataCtor || !g_origPickContextDataCtor) {
            Notify("Free camera unavailable (1.10.163 targeting hooks not armed)");
            logger::error("WorkshopFreedom: F4 refused - placementCtx={} pickCtx={}",
                          g_origContextDataCtor != nullptr, g_origPickContextDataCtor != nullptr);
            return;
        }

        auto* cam = RE::PlayerCamera::GetSingleton();
        if (!cam) return;

        const bool now = !s_freeCam.load();
        s_freeCam.store(now);
        if (now) {
            s_freeCamDiag.store(true);
            s_freeCamPickDiag.store(true);
            RunConsoleCommand(std::format("sucsm {:.2f}", s_freeCamSpeed.load()));
        }
        cam->ToggleFreeCameraMode(s_camFreezeTime.load());
        Notify(now ? "Free camera ON" : "Free camera OFF");
        logger::info("WorkshopFreedom: free camera {}", now ? "ON" : "OFF");
        NotifyToggles();
    }

    void CycleSnap()
    {
        if (!s_enabled.load() || !s_workshopActive.load()) return;
        if (!PlaceInRed::GroundSnapAvailable()) {
            Notify("Free height unavailable (1.10.163 ground-snap sites not armed)");
            return;
        }

        const bool now = !s_groundSnapOn.load();
        const bool effective = now && !s_manualZ.load();
        if (!PlaceInRed::SetGroundSnap(effective)) {
            Notify("Free height toggle failed - check PrismaCrafting.log");
            return;
        }
        s_groundSnapOn.store(now);

        if (now && s_manualZ.load())
            Notify("Free height off - floor snap resumes after F7 reset");
        else
            Notify(now ? "Free height OFF (snaps to floor)" : "Free height ON (place at aimed height)");
        NotifyToggles();
    }

    void ToggleObjectSnap()
    {
        if (!s_enabled.load() || !s_workshopActive.load()) return;
        if (!PlaceInRed::ObjectSnapAvailable()) {
            Notify("Object snap unavailable (1.10.163 signature not armed)");
            return;
        }

        const bool now = !s_objectSnapOn.load();
        if (!PlaceInRed::SetObjectSnap(now)) {
            Notify("Object snap toggle failed - check PrismaCrafting.log");
            return;
        }
        s_objectSnapOn.store(now);
        Notify(now ? "Object snap ON (vanilla)" : "Object snap OFF");
        NotifyToggles();
    }

    void ResetTransform()
    {
        if (!s_enabled.load() || !s_workshopActive.load() || !WorkshopInputHook::HoldingPlacement()) return;
        const bool hadManualZ = s_manualZ.load();
        if (auto* d = RE::Workshop::GetCurrentPlacementItemData())
            if (auto ref = d->placementItem.get()) ref->SetScale(1.0f);

        ResetOffsets();
        s_zResetPending.store(true);
        if (hadManualZ) {
            PlaceInRed::SetGroundSnap(s_groundSnapOn.load());
            logger::info("WorkshopFreedom: manual vertical placement cleared by F7");
        }
        Notify("Transform reset");
    }

    void NudgeScale(float delta)
    {
        if (!s_enabled.load() || !s_workshopActive.load() || !WorkshopInputHook::HoldingPlacement()) return;
        auto* d = RE::Workshop::GetCurrentPlacementItemData();
        if (!d) return;
        auto ref = d->placementItem.get();
        if (!ref) return;
        SyncHeld(d);

        float next = s_scale.load() + delta;
        if (next < 0.1f) next = 0.1f;
        else if (next > 10.0f) next = 10.0f;
        s_scale.store(next);
        ref->SetScale(next);

        Notify("Scale {{value}}", {{"value", std::format("{:.2f}", next)}});
    }

    void NudgeRotate(int axis, float degrees)
    {
        if (!s_enabled.load() || !s_workshopActive.load() || !WorkshopInputHook::HoldingPlacement()) return;
        std::atomic<float>* t = axis == 0 ? &s_yaw : (axis == 1 ? &s_pitch : &s_roll);
        const double current = t->load();
        if (!std::isfinite(current) || !std::isfinite(degrees)) return;
        double wrapped = std::fmod(current + static_cast<double>(degrees), 360.0);
        if (wrapped < 0.0) wrapped += 360.0;
        const float next = static_cast<float>(wrapped);
        t->store(next < 360.0f ? next : 0.0f);

        Notify(axis == 0 ? "Yaw {{value}} deg" : axis == 1 ? "Pitch {{value}} deg" : "Roll {{value}} deg",
               {{"value", std::format("{:.0f}", t->load())}});
    }

    void NudgeMove(int axis, float units)
    {
        if (!s_enabled.load() || !s_workshopActive.load() || !WorkshopInputHook::HoldingPlacement() || !std::isfinite(units)) return;

        if (axis == 2) {
            auto* d = RE::Workshop::GetCurrentPlacementItemData();
            if (!d || !d->placementItem.get()) return;
            SyncHeld(d);
            const double next = s_offUp.load() + static_cast<double>(units);
            if (!std::isfinite(next) || std::abs(next) > std::numeric_limits<float>::max()) return;

            if (!g_origContextDataCtor || !PlaceInRed::GroundSnapAvailable()) {
                Notify("Vertical override unavailable (1.10.163 placement/snap hook not armed)");
                logger::error("WorkshopFreedom: PgUp/PgDn refused - placementCtx={} groundSnap={}",
                              g_origContextDataCtor != nullptr, PlaceInRed::GroundSnapAvailable());
                return;
            }

            const bool first = !s_manualZ.load();
            if (first) {

                if (!PlaceInRed::SetGroundSnap(false)) {
                    Notify("Vertical override failed - check PrismaCrafting.log");
                    return;
                }
                s_manualZ.store(true);
                logger::info("WorkshopFreedom: manual vertical placement enabled; ground snap suspended");
            }

            s_offUp.store(static_cast<float>(next));
            logger::info("WorkshopFreedom: vertical nudge {:+.0f} -> offset={:.0f}", units, next);

            Notify("Offset up {{value}}", {{"value", std::format("{:.0f}", next)}});
            return;
        }

        std::atomic<float>* t = axis == 0 ? &s_offFwd : &s_offRight;
        const double next = t->load() + static_cast<double>(units);
        if (!std::isfinite(next) || std::abs(next) > std::numeric_limits<float>::max()) return;
        t->store(static_cast<float>(next));
        Notify(axis == 0 ? "Offset forward {{value}}" : "Offset right {{value}}", {{"value", std::format("{:.0f}", t->load())}});
    }

}

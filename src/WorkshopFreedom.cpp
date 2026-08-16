#include "PCH.h"

#include "WorkshopFreedom.h"

#include "Paths.h"
#include "PlaceInRed.h"
#include "SimpleIni.h"
#include "TrampolineGuard.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace WorkshopFreedom
{
    namespace
    {
        constexpr float kDeg2Rad = 0.01745329252f;

        std::atomic<bool> s_enabled{ true };
        std::atomic<bool> s_workshopActive{ false };
        std::atomic<bool> s_freePlace{ false };
        std::atomic<bool> s_freeCam{ false };
        std::atomic<bool> s_camFreezeTime{ false };
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

        std::atomic<float> s_freeCamDistance{ 0.0f };
        std::atomic<float> s_freeCamSpeed{ 10.0f };

        void RunConsoleCommand(const std::string& cmd)
        {
            const auto factories = RE::IFormFactory::GetFormFactories();
            auto* factory = factories[std::to_underlying(RE::ENUM_FORM_ID::kSCPT)];
            if (!factory) return;
            auto* script = static_cast<RE::Script*>(factory->DoCreate());
            if (!script) return;
            script->SetText(cmd);
            script->CompileAndRun(nullptr, RE::COMPILER_NAME::kSystemWindow, nullptr);
            logger::info("WorkshopFreedom: ran console command '{}'", cmd);
        }

        std::uint16_t s_keyFreePlace = VK_F3;
        std::uint16_t s_keyFreeCam   = VK_F4;
        std::uint16_t s_keyGrow      = VK_OEM_PLUS;
        std::uint16_t s_keyShrink    = VK_OEM_MINUS;
        std::uint16_t s_keyYaw       = VK_NUMPAD4;
        std::uint16_t s_keyPitch     = VK_NUMPAD8;
        std::uint16_t s_keyRoll      = VK_NUMPAD7;
        std::uint16_t s_keySnap      = VK_F6;
        std::uint16_t s_keyReset     = VK_F7;
        std::uint16_t s_keyMod       = VK_SHIFT;
        std::uint16_t s_keyFwd       = VK_HOME;
        std::uint16_t s_keyBack      = VK_END;
        std::uint16_t s_keyRight     = VK_INSERT;
        std::uint16_t s_keyLeft      = VK_DELETE;
        std::uint16_t s_keyUp        = VK_PRIOR;
        std::uint16_t s_keyDown      = VK_NEXT;

        float s_scaleStep = 0.05f;
        float s_rotStep = 15.0f;
        float s_moveStep = 5.0f;

        std::atomic<bool> s_heldPlace{ false };
        std::atomic<bool> s_heldCam{ false };

        std::string IniPath()
        {
            auto p = Paths::F4SEUserDir();
            p /= "PrismaCrafting.ini";
            return p.string();
        }

        std::uint16_t KeyFromIni(CSimpleIniA& ini, const char* key, std::uint16_t fallback)
        {
            const char* v = ini.GetValue("Workshop", key, nullptr);
            if (!v || !v[0]) return fallback;
            if (v[1] == '\0')
                return static_cast<std::uint16_t>(std::toupper(static_cast<unsigned char>(v[0])));
            return static_cast<std::uint16_t>(std::strtoul(v, nullptr, 0));
        }

        void Notify(const char* msg)
        {
            RE::SendHUDMessage::ShowHUDMessage(msg, nullptr, true, false);
        }

        bool ModHeld() { return (GetAsyncKeyState(s_keyMod) & 0x8000) != 0; }

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
                F4SE::GetTaskInterface()->AddTask([desiredGroundSnap] {
                    PlaceInRed::SetGroundSnap(desiredGroundSnap);
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
        constexpr std::uintptr_t kPlaceCurrentReferenceFreeBuildCallSite = 0x20d02b;
        constexpr REL::ID        kFreeBuild{ 1583365 };

        bool Detour_FreeBuildAtPlaceCurrentReference()
        {
            if (s_freePlace.load()) return true;
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
            if (len2 < 1e-8f) return false;
            const float inv = 1.0f / std::sqrt(len2);
            v.x *= inv; v.y *= inv; v.z *= inv;
            return true;
        }

        bool CrosshairRay(RE::NiCamera* cam, RE::NiPoint3& origin, RE::NiPoint3& dir)
        {
            const auto& c = kViewCentres[s_viewCentre.load()];
            cam->ViewPointToRay(c.x, c.y, &origin, &dir);
            return Normalize(dir);
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
        constexpr REL::ID        kRegisterForPlacementStatusEvent{ 1325826 };

        using ContextDataCtorFn = RE::Workshop::ContextData* (*)(RE::Workshop::ContextData*, RE::Actor*);
        ContextDataCtorFn g_origContextDataCtor = nullptr;
        ContextDataCtorFn g_origPickContextDataCtor = nullptr;
        constexpr std::uintptr_t kMainRenderContextDataCallSite = 0xcbd478;
        constexpr std::uintptr_t kAutoAimPickRefContextDataCallSite = 0xcbd729;
        constexpr REL::ID        kContextDataCtor{ 1523699 };

        RE::Workshop::ContextData* Detour_ContextDataCtor(RE::Workshop::ContextData* ctx, RE::Actor* actor)
        {
            if (g_origContextDataCtor) g_origContextDataCtor(ctx, actor);
            if (!s_workshopActive.load()) return ctx;

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
            if (cam && !s_freeCam.load()) MeasureViewCentre(cam, ctx->lookDir);

            if (cam && s_freeCam.load()) {
                RE::NiPoint3 org{}, dir{};
                if (CrosshairRay(cam, org, dir)) {
                    ctx->lookPos = org;
                    ctx->lookDir = dir;
                    const float dist = s_freeCamDistance.load();
                    if (dist != 0.0f) {
                        ctx->lookPos.x += dir.x * dist;
                        ctx->lookPos.y += dir.y * dist;
                        ctx->lookPos.z += dir.z * dist;
                    }
                    const RE::NiPoint3 flat{ dir.x, dir.y, 0.0f };
                    if (flat.SqrLength() > 1e-6f) ctx->zAngle = flat.GetZAngleFromVector();

                    if (s_freeCamDiag.exchange(false))
                        logger::info("WorkshopFreedom: free-cam aim | centre {} | origin ({:.0f} {:.0f} {:.0f}) | "
                                     "dir ({:.3f} {:.3f} {:.3f}) | reach {:.0f}",
                                     kViewCentres[s_viewCentre.load()].label, org.x, org.y, org.z,
                                     dir.x, dir.y, dir.z, s_freeCamDistance.load());
                }
            }

            const float offF = s_offFwd.load(), offR = s_offRight.load();
            if (offF != 0.0f || offR != 0.0f) {
                RE::NiPoint3 f{ ctx->lookDir.x, ctx->lookDir.y, 0.0f };
                const float len2 = f.SqrLength();
                if (len2 > 1e-6f) {
                    const float inv = 1.0f / std::sqrt(len2);
                    f.x *= inv; f.y *= inv;
                } else {
                    f = { 0.0f, 1.0f, 0.0f };
                }
                ctx->lookPos.x += f.x * offF + f.y * offR;
                ctx->lookPos.y += f.y * offF - f.x * offR;
            }

            ctx->zAngle += s_yaw.load() * kDeg2Rad;
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
        constexpr std::uintptr_t kMainRenderWorkshopUpdateCallSite = 0xcbd482;
        constexpr REL::ID        kWorkshopUpdate{ 679561 };
        std::atomic<bool> s_tiltApplied{ false };

        void Detour_WorkshopUpdate(RE::Workshop::ContextData* ctx)
        {
            if (auto* fn = s_preUpdate.load()) fn();
            if (g_origWorkshopUpdate) g_origWorkshopUpdate(ctx);
            if (!s_workshopActive.load()) return;

            PlaceInRed::HoldValid();

            const float pitch = s_pitch.load(), roll = s_roll.load();
            const bool tilted = pitch != 0.0f || roll != 0.0f;
            if (!tilted && !s_tiltApplied.load()) return;

            auto* pd = RE::Workshop::GetCurrentPlacementItemData();
            if (!pd) return;
            auto ref = pd->placementItem.get();
            if (!ref) return;
            ref->data.angle.x = pitch * kDeg2Rad;
            ref->data.angle.y = roll * kDeg2Rad;
            ref->Update3DPosition(false);
            s_tiltApplied.store(tilted);
        }

        void* HookCallSite(const char* label, std::uintptr_t siteOff,
                           std::uintptr_t targetAddr, void* detour)
        {
            REL::Relocation<std::uintptr_t> site{ REL::Offset{ siteOff } };
            const auto* p = reinterpret_cast<const std::uint8_t*>(site.address());
            bool ok = p[0] == 0xE8;
            if (ok) {
                const auto rel = *reinterpret_cast<const std::int32_t*>(p + 1);
                const auto target = site.address() + 5 + rel;
                ok = target == targetAddr;
            }
            if (!ok) {
                logger::critical("WorkshopFreedom: REFUSING {} hook -- RVA {:#x} is not a CALL to {:#x}. "
                                 "Bytes {:02x} {:02x} {:02x} {:02x} {:02x}",
                                 label, siteOff, targetAddr, p[0], p[1], p[2], p[3], p[4]);
                return nullptr;
            }
            return reinterpret_cast<void*>(REL::GetTrampoline().write_call<5>(site.address(), detour));
        }
    }

    void Init()
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(IniPath().c_str());
        s_keyFreePlace = KeyFromIni(ini, "FreePlaceKey", s_keyFreePlace);
        s_keyFreeCam = KeyFromIni(ini, "FreeCameraKey", s_keyFreeCam);
        s_keyGrow = KeyFromIni(ini, "ScaleUpKey", s_keyGrow);
        s_keyShrink = KeyFromIni(ini, "ScaleDownKey", s_keyShrink);
        s_keyYaw = KeyFromIni(ini, "YawKey", s_keyYaw);
        s_keyPitch = KeyFromIni(ini, "PitchKey", s_keyPitch);
        s_keyRoll = KeyFromIni(ini, "RollKey", s_keyRoll);
        s_keySnap = KeyFromIni(ini, "SnapCycleKey", s_keySnap);
        s_keyReset = KeyFromIni(ini, "ResetKey", s_keyReset);
        s_scaleStep = static_cast<float>(ini.GetDoubleValue("Workshop", "ScaleStep", 0.05));
        s_rotStep = static_cast<float>(ini.GetDoubleValue("Workshop", "RotateStepDegrees", 15.0));
        s_moveStep = static_cast<float>(ini.GetDoubleValue("Workshop", "MoveStepUnits", 5.0));
        s_freeCamDistance.store(static_cast<float>(
            ini.GetDoubleValue("Workshop", "FreeCameraReachUnits", 0.0)));
        s_camFreezeTime.store(ini.GetBoolValue("Workshop", "FreeCameraFreezesTime", false));
        s_freeCamSpeed.store(static_cast<float>(
            ini.GetDoubleValue("Workshop", "FreeCameraSpeed", 10.0)));
        s_enabled.store(ini.GetBoolValue("Workshop", "Enabled", true));

        if (!s_enabled.load()) logger::warn("WorkshopFreedom: DISABLED via [Workshop]Enabled=0");
        logger::info("WorkshopFreedom: 1.10.163 keys place={:#x} cam={:#x} groundSnap={:#x} reset={:#x} "
                     "yaw={:#x} pitch={:#x} roll={:#x}; Shift+groundSnap toggles object snap",
                     s_keyFreePlace, s_keyFreeCam, s_keySnap, s_keyReset,
                     s_keyYaw, s_keyPitch, s_keyRoll);
    }

    void InstallHooks()
    {
        static bool installed = false;
        if (installed) return;

        if (!REX::FModule::IsRuntimeOG()) {
            logger::critical("WorkshopFreedom: settlement replacement is validated for Fallout 4 1.10.163; "
                             "workshop hooks not installed on this runtime");
            return;
        }

        EnsurePrismaCraftingTrampoline();

        g_origFreeBuildAtPlaceCurrentReference = reinterpret_cast<FreeBuildFn>(HookCallSite(
            "FreeBuild/PlaceCurrentReference", kPlaceCurrentReferenceFreeBuildCallSite,
            kFreeBuild.address(), &Detour_FreeBuildAtPlaceCurrentReference));
        g_origContextDataCtor = reinterpret_cast<ContextDataCtorFn>(HookCallSite(
            "placement ContextData", kMainRenderContextDataCallSite,
            kContextDataCtor.address(), &Detour_ContextDataCtor));
        g_origPickContextDataCtor = reinterpret_cast<ContextDataCtorFn>(HookCallSite(
            "highlight ContextData", kAutoAimPickRefContextDataCallSite,
            kContextDataCtor.address(), &Detour_PickContextDataCtor));
        g_origWorkshopUpdate = reinterpret_cast<WorkshopUpdateFn>(HookCallSite(
            "Workshop::Update", kMainRenderWorkshopUpdateCallSite,
            kWorkshopUpdate.address(), &Detour_WorkshopUpdate));

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

        installed = true;
    }

    void SetWorkshopActive(bool active)
    {
        s_workshopActive.store(active);
        if (active) return;

        if (s_freeCam.exchange(false)) {
            const bool freeze = s_camFreezeTime.load();
            F4SE::GetTaskInterface()->AddTask([freeze] {
                if (auto* cam = RE::PlayerCamera::GetSingleton()) cam->ToggleFreeCameraMode(freeze);
            });
        }

        s_freePlace.store(false);
        s_groundSnapOn.store(true);
        s_objectSnapOn.store(true);
        s_heldHandle.store(0);
        ResetOffsets();
        s_zResetPending.store(false);
        F4SE::GetTaskInterface()->AddTask([] { PlaceInRed::RestoreAll(); });
    }

    bool FreePlaceEnabled() { return s_freePlace.load(); }
    bool FreeCameraEnabled() { return s_freeCam.load(); }
    bool GroundSnapEnabled() { return s_groundSnapOn.load(); }
    bool ObjectSnapEnabled() { return s_objectSnapOn.load(); }

    void SetToggleChangedCallback(void (*fn)()) { s_toggleChanged.store(fn); }
    void SetPreUpdateHook(void (*fn)()) { s_preUpdate.store(fn); }

    void Tick() {}

    void ToggleFreePlace()
    {
        if (!s_workshopActive.load()) return;
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
        if (!s_workshopActive.load()) return;
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
        if (!s_workshopActive.load()) return;
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
        if (!s_workshopActive.load()) return;
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
        if (!s_workshopActive.load()) return;
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
        if (!s_workshopActive.load()) return;
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

        char buf[48];
        std::snprintf(buf, sizeof(buf), "Scale %.2f", next);
        Notify(buf);
    }

    void NudgeRotate(int axis, float degrees)
    {
        if (!s_workshopActive.load()) return;
        auto wrap = [](float v) {
            while (v >= 360.0f) v -= 360.0f;
            while (v < 0.0f) v += 360.0f;
            return v;
        };
        std::atomic<float>* t = axis == 0 ? &s_yaw : (axis == 1 ? &s_pitch : &s_roll);
        t->store(wrap(t->load() + degrees));

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s %.0f deg",
                      axis == 0 ? "Yaw" : (axis == 1 ? "Pitch" : "Roll"), t->load());
        Notify(buf);
    }

    void NudgeMove(int axis, float units)
    {
        if (!s_workshopActive.load()) return;

        if (axis == 2) {
            auto* d = RE::Workshop::GetCurrentPlacementItemData();
            if (!d || !d->placementItem.get()) return;
            SyncHeld(d);

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

            const float next = s_offUp.load() + units;
            s_offUp.store(next);
            logger::info("WorkshopFreedom: vertical nudge {:+.0f} -> offset={:.0f}", units, next);

            char buf[64];
            std::snprintf(buf, sizeof(buf), "Offset up %.0f", next);
            Notify(buf);
            return;
        }

        std::atomic<float>* t = axis == 0 ? &s_offFwd : &s_offRight;
        t->store(t->load() + units);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Offset %s %.0f",
                      axis == 0 ? "forward" : "right", t->load());
        Notify(buf);
    }

    bool OwnsKey(std::uint16_t vk)
    {
        if (!s_enabled.load() || !s_workshopActive.load()) return false;
        if (vk == s_keyFreePlace || vk == s_keyFreeCam || vk == s_keySnap || vk == s_keyReset ||
            vk == s_keyGrow || vk == s_keyShrink ||
            vk == s_keyYaw || vk == s_keyPitch || vk == s_keyRoll)
            return true;
        return vk == s_keyFwd || vk == s_keyBack || vk == s_keyRight ||
               vk == s_keyLeft || vk == s_keyUp || vk == s_keyDown;
    }

    bool HandleKey(std::uint16_t vk, bool keyUp)
    {
        if (!s_enabled.load() || !s_workshopActive.load()) return false;
        if (!keyUp && OwnsKey(vk)) logger::info("WorkshopFreedom: swallowed vk={:#x}", vk);

        if (vk == s_keyFreePlace) {
            if (keyUp) s_heldPlace.store(false);
            else if (!s_heldPlace.exchange(true))
                F4SE::GetTaskInterface()->AddTask([] { ToggleFreePlace(); });
            return true;
        }
        if (vk == s_keyFreeCam) {
            if (keyUp) s_heldCam.store(false);
            else if (!s_heldCam.exchange(true))
                F4SE::GetTaskInterface()->AddTask([] { ToggleFreeCamera(); });
            return true;
        }
        if (vk == s_keyGrow || vk == s_keyShrink) {
            if (!keyUp) {
                const float step = vk == s_keyGrow ? s_scaleStep : -s_scaleStep;
                F4SE::GetTaskInterface()->AddTask([step] { NudgeScale(step); });
            }
            return true;
        }
        if (vk == s_keySnap) {
            if (!keyUp) {
                const bool objectSnap = ModHeld();
                F4SE::GetTaskInterface()->AddTask([objectSnap] {
                    if (objectSnap) ToggleObjectSnap();
                    else CycleSnap();
                });
            }
            return true;
        }
        if (vk == s_keyReset) {
            if (!keyUp) F4SE::GetTaskInterface()->AddTask([] { ResetTransform(); });
            return true;
        }
        if (vk == s_keyYaw || vk == s_keyPitch || vk == s_keyRoll) {
            if (!keyUp) {
                const int axis = vk == s_keyYaw ? 0 : (vk == s_keyPitch ? 1 : 2);
                const float step = ModHeld() ? -s_rotStep : s_rotStep;
                F4SE::GetTaskInterface()->AddTask([axis, step] { NudgeRotate(axis, step); });
            }
            return true;
        }

        struct MoveKey { std::uint16_t vk; int axis; float sign; };
        const MoveKey moves[] = {
            { s_keyFwd, 0, 1.0f }, { s_keyBack, 0, -1.0f },
            { s_keyRight, 1, 1.0f }, { s_keyLeft, 1, -1.0f },
            { s_keyUp, 2, 1.0f }, { s_keyDown, 2, -1.0f },
        };
        for (const auto& m : moves) {
            if (vk != m.vk) continue;
            if (!keyUp) {
                const int axis = m.axis;
                const float step = m.sign * s_moveStep;
                F4SE::GetTaskInterface()->AddTask([axis, step] { NudgeMove(axis, step); });
            }
            return true;
        }
        return false;
    }
}

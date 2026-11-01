#include "PCH.h"
#include "WorkshopFreedom.h"
#include "WorkshopFreedomInternal.h"
#include "DebugLog.h"
#include "Paths.h"
#include "SimpleIni.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

// Constraint: source:src/WorkshopFreedom.cpp at 774f517; preserve input admission and key routing.
namespace WorkshopFreedom
{
    namespace
    {
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

        std::filesystem::path IniPath()
        {
            auto p = Paths::F4SEUserDir();
            p /= "PrismaCrafting.ini";
            return p;
        }

        std::uint16_t KeyFromIni(CSimpleIniA& ini, const char* key, std::uint16_t fallback)
        {
            const char* v = ini.GetValue("Workshop", key, nullptr);
            if (!v || !v[0]) return fallback;
            if (v[1] == '\0')
                return static_cast<std::uint16_t>(std::toupper(static_cast<unsigned char>(v[0])));
            while (std::isspace(static_cast<unsigned char>(*v))) ++v;
            if (*v == '-') return fallback;
            char* end = nullptr;
            const auto value = std::strtoul(v, &end, 0);
            if (end == v || *end || value > std::numeric_limits<std::uint16_t>::max()) return fallback;
            return static_cast<std::uint16_t>(value);
        }
        float FloatFromIni(CSimpleIniA& ini, const char* key, float fallback)
        {
            const double value = ini.GetDoubleValue("Workshop", key, fallback);
            return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max() ?
                static_cast<float>(value) : fallback;
        }
        bool ModHeld() { return (GetAsyncKeyState(s_keyMod) & 0x8000) != 0; }
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
        s_scaleStep = FloatFromIni(ini, "ScaleStep", 0.05f);
        s_rotStep = FloatFromIni(ini, "RotateStepDegrees", 15.0f);
        s_moveStep = FloatFromIni(ini, "MoveStepUnits", 5.0f);
        s_freeCamDistance.store(FloatFromIni(ini, "FreeCameraReachUnits", 0.0f));
        s_camFreezeTime.store(ini.GetBoolValue("Workshop", "FreeCameraFreezesTime", false));
        s_freeCamSpeed.store(FloatFromIni(ini, "FreeCameraSpeed", 10.0f));
        s_enabled.store(ini.GetBoolValue("Workshop", "Enabled", true));

        if (!s_enabled.load()) logger::warn("WorkshopFreedom: DISABLED via [Workshop]Enabled=0");
        logger::info("WorkshopFreedom: keys place={:#x} cam={:#x} groundSnap={:#x} reset={:#x} "
                     "yaw={:#x} pitch={:#x} roll={:#x}; Shift+groundSnap toggles object snap",
                     s_keyFreePlace, s_keyFreeCam, s_keySnap, s_keyReset,
                     s_keyYaw, s_keyPitch, s_keyRoll);
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
        if (!s_enabled.load() || !s_workshopActive.load()) {
            DLOG(Input, "HandleKey vk={:#x} ignored (enabled={} workshopActive={})",
                 vk, s_enabled.load(), s_workshopActive.load());
            return false;
        }
        if (!keyUp && OwnsKey(vk)) DLOG(Input, "WorkshopFreedom swallowed vk={:#x}", vk);

        if (vk == s_keyFreePlace) {
            if (keyUp) s_heldPlace.store(false);
            else if (!s_heldPlace.exchange(true))
                PostGameTask("toggle-free-place", [] { ToggleFreePlace(); });
            return true;
        }
        if (vk == s_keyFreeCam) {
            if (keyUp) s_heldCam.store(false);
            else if (!s_heldCam.exchange(true))
                PostGameTask("toggle-free-camera-mode", [] { ToggleFreeCamera(); });
            return true;
        }
        if (vk == s_keyGrow || vk == s_keyShrink) {
            if (!keyUp) {
                const float step = vk == s_keyGrow ? s_scaleStep : -s_scaleStep;
                PostGameTask("nudge-scale", [step] { NudgeScale(step); });
            }
            return true;
        }
        if (vk == s_keySnap) {
            if (!keyUp) {
                const bool objectSnap = ModHeld();
                PostGameTask("cycle-snap", [objectSnap] {
                    if (objectSnap) ToggleObjectSnap();
                    else CycleSnap();
                });
            }
            return true;
        }
        if (vk == s_keyReset) {
            if (!keyUp) PostGameTask("reset-transform", [] { ResetTransform(); });
            return true;
        }
        if (vk == s_keyYaw || vk == s_keyPitch || vk == s_keyRoll) {
            if (!keyUp) {
                const int axis = vk == s_keyYaw ? 0 : (vk == s_keyPitch ? 1 : 2);
                const float step = ModHeld() ? -s_rotStep : s_rotStep;
                PostGameTask("nudge-rotate", [axis, step] { NudgeRotate(axis, step); });
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
                PostGameTask("nudge-move", [axis, step] { NudgeMove(axis, step); });
            }
            return true;
        }
        return false;
    }
}

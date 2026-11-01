#include "WorkshopInputHook.h"

#include "DebugLog.h"
#include "WorkshopIDs.h"
#include "WorkshopFreedom.h"
#include "WorkshopInputRegions.h"
#include "WorkshopControllerInput.h"
#include "WorkshopPlacementInput.h"
#include "PrismaGameThreadCompat.h"
#include "PrismaFlatApi.h"

#include <RE/U/UI.h>
#include <RE/I/IMenu.h>
#include <RE/U/UI_MENU_FLAGS.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/W/Workshop.h>
#include <REX/FModule.h>

#include <commctrl.h>
#include <windowsx.h>
#include <atomic>
#include <stdexcept>

static constexpr USHORT kToggleKey = 'C';

namespace WorkshopInputHook
{
    static PRISMA_UI_API::IVPrismaUI11** s_api = nullptr;
    static std::atomic<PrismaView>*     s_view = nullptr;
    static HWND                         s_hwnd = nullptr;
    static std::atomic<bool>            s_active{ false };
    static std::atomic<bool>            s_focused{ false };
    static std::atomic<bool>            s_focusTransition{ false };
    static std::atomic<bool>            s_placementTransition{ false };
    static std::atomic<std::uint32_t>   s_transitionStartRefId{ 0 };
    static std::atomic<bool>            s_releaseGateE{ false };
    static std::atomic<bool>            s_releaseGateEnter{ false };
    static std::atomic<bool>            s_restoreAfterPlacement{ false };
    static std::atomic<bool>            s_restoreAfterReactivation{ false };
    static std::atomic<std::uint64_t>   s_focusGeneration{ 0 };
    static std::atomic<bool>            s_insHeld{ false };
    static std::atomic<bool>            s_typing{ false };
    static std::atomic<bool>            s_textFocusRequested{ false };

    static bool ApiOk() { return s_api && *s_api && s_view && (*s_api)->IsValid(s_view->load()); }

    static bool IsPanelKey(USHORT vk)
    {
        return vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' ||
               vk == 'E' || vk == 'F' || vk == 'R' || vk == 'Q' || vk == VK_RETURN;
    }

    static bool IsMovementKey(USHORT vk)
    {
        return vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D';
    }

    static bool IsPlacementKey(USHORT vk)
    {
        return vk == 'E' || vk == VK_RETURN;
    }

    static void ArmPlacementKeyReleaseGate()
    {
        s_releaseGateE.store((GetAsyncKeyState('E') & 0x8000) != 0, std::memory_order_release);
        s_releaseGateEnter.store((GetAsyncKeyState(VK_RETURN) & 0x8000) != 0, std::memory_order_release);
    }

    static void ClearPlacementKeyReleaseGate()
    {
        s_releaseGateE.store(false, std::memory_order_release);
        s_releaseGateEnter.store(false, std::memory_order_release);
    }

    static bool PlacementReleaseGateOwns(USHORT vk, bool up)
    {
        std::atomic<bool>* gate = nullptr;
        if (vk == 'E') gate = &s_releaseGateE;
        else if (vk == VK_RETURN) gate = &s_releaseGateEnter;
        else return false;

        if (!gate->load(std::memory_order_acquire)) return false;
        if (up) gate->store(false, std::memory_order_release);
        return true;
    }

    static std::uint32_t CurrentPlacementRefId()
    {
        auto* handle = RE::Workshop::GetPlacementItem();
        if (!handle) return 0;
        auto ref = handle->get();
        return ref ? ref->GetFormID() : 0;
    }

    static bool PlacementGhostReady()
    {
        if (!crafting::ids::WorkshopDataResolvable()) return false;
        static REL::Relocation<bool*> activeEdit{ crafting::ids::ActiveEdit };
        if (!activeEdit.get() || !*activeEdit) return false;

        const std::uint32_t current = CurrentPlacementRefId();
        const std::uint32_t before = s_transitionStartRefId.load(std::memory_order_acquire);
        return current != 0 && current != before;
    }

    static bool PlacementTransitionOwns(USHORT vk, bool up)
    {
        if (!s_placementTransition.load(std::memory_order_acquire) || !IsPlacementKey(vk)) return false;
        if (PlacementReleaseGateOwns(vk, up)) return true;
        if (!PlacementGhostReady()) return true;

        s_placementTransition.store(false, std::memory_order_release);
        s_transitionStartRefId.store(0, std::memory_order_release);
        logger::info("WorkshopInputHook: placement ghost live -- E/Enter returned to vanilla workshop input");
        return false;
    }

    static USHORT NormalizeNumpad(const RAWKEYBOARD& a_kb)
    {
        if (a_kb.Flags & RI_KEY_E0) return a_kb.VKey;
        switch (a_kb.MakeCode) {
            case 0x52: return VK_NUMPAD0;
            case 0x4F: return VK_NUMPAD1;
            case 0x50: return VK_NUMPAD2;
            case 0x51: return VK_NUMPAD3;
            case 0x4B: return VK_NUMPAD4;
            case 0x4C: return VK_NUMPAD5;
            case 0x4D: return VK_NUMPAD6;
            case 0x47: return VK_NUMPAD7;
            case 0x48: return VK_NUMPAD8;
            case 0x49: return VK_NUMPAD9;
            case 0x53: return VK_DECIMAL;
            case 0x4A: return VK_SUBTRACT;
            case 0x4E: return VK_ADD;
            default:   return a_kb.VKey;
        }
    }

    void SetPanelRects(const char* json)
    {
        WorkshopInputRegions::SetPanelRects(json);
    }

    static void SetCursorFlags(bool enabled)
    {
        auto* ui = RE::UI::GetSingleton();
        auto  menu = ui ? ui->GetMenu("WorkshopMenu") : nullptr;
        if (menu) {
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kUsesCursor, enabled);
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kUpdateUsesCursor, enabled);
        }
        if (ui) ui->RefreshCursor();
    }

    static bool ReadFrameworkFocus()
    {
        return ApiOk() && (*s_api)->HasFocus(s_view->load());
    }

    static bool PanelFocusedNow()
    {
        if (s_focusTransition.load(std::memory_order_acquire)) return true;
        const bool actual = ReadFrameworkFocus();
        s_focused.store(actual, std::memory_order_release);
        return actual;
    }

    static bool FocusOn()
    {
        DLOG(Input, "FocusOn: active={} apiOk={}", s_active.load(), ApiOk());
        s_focusTransition.store(true, std::memory_order_release);
        ArmPlacementKeyReleaseGate();
        if (!s_active.load() || !ApiOk()) {
            s_focused.store(false, std::memory_order_release);
            SetCursorFlags(false);
            s_focusTransition.store(false, std::memory_order_release);
            return false;
        }

        SetCursorFlags(true);
        const PrismaView view = s_view->load();
        const bool accepted = (*s_api)->FocusOverlay(view, false, true);
        WorkshopInputRegions::Apply();
        const bool actual = (*s_api)->HasFocus(view);
        s_focused.store(actual, std::memory_order_release);
        if (actual) WorkshopControllerInput::NoteViewFocused(view);

        if (!accepted || !actual) {
            if (!actual) SetCursorFlags(false);
            logger::warn("WorkshopInputHook: helper focus request accepted={} actual={}", accepted, actual);
        } else {
            logger::info("WorkshopInputHook: cursor ON + helper focused (WASD navigates), policy={}",
                         static_cast<int>(PrismaFlatApi::GetCursorPolicy(view)));
        }
        s_focusTransition.store(false, std::memory_order_release);
        return actual;
    }

    static void QueueFocusRestore(std::atomic<bool>* intent, const char* reason)
    {
        if (!intent || !intent->load(std::memory_order_acquire)) return;
        const auto generation = s_focusGeneration.load(std::memory_order_acquire);
        const auto view = s_view ? s_view->load(std::memory_order_acquire) : 0;
        if (!PrismaCraftingThreading::Dispatch([intent, reason, generation, view] {
                if (generation != s_focusGeneration.load(std::memory_order_acquire) ||
                    !s_view || view != s_view->load(std::memory_order_acquire) ||
                    !s_active.load(std::memory_order_acquire) ||
                    !intent->load(std::memory_order_acquire))
                    return;
                if (FocusOn()) {
                    if (generation == s_focusGeneration.load(std::memory_order_acquire) &&
                        s_view && view == s_view->load(std::memory_order_acquire))
                        intent->store(false, std::memory_order_release);
                } else {
                    logger::warn("WorkshopInputHook: deferred {} focus restore still pending", reason);
                }
            })) {
            logger::error("WorkshopInputHook: could not queue deferred {} focus restore", reason);
        }
    }

    static bool FocusOff()
    {
        DLOG(Input, "FocusOff: active={} apiOk={}", s_active.load(), ApiOk());
        s_focusTransition.store(true, std::memory_order_release);
        if (ApiOk()) {
            WorkshopInputRegions::Clear();
            (*s_api)->Unfocus(s_view->load());
        }

        const bool actual = ReadFrameworkFocus();
        s_focused.store(actual, std::memory_order_release);
        SetCursorFlags(actual);
        if (s_typing.exchange(false)) DLOG(Input, "text focus cleared by FocusOff");

        if (actual)
            logger::warn("WorkshopInputHook: helper remained focused after Unfocus; panel keys stay owned by Prisma");
        else
            logger::info("WorkshopInputHook: cursor OFF + back to build (WASD moves the player)");
        s_focusTransition.store(false, std::memory_order_release);
        return actual;
    }

    void ToggleCursor()
    {
        s_focusGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_restoreAfterPlacement.store(false, std::memory_order_release);
        s_restoreAfterReactivation.store(false, std::memory_order_release);
        if (!s_active.load()) {
            s_focusTransition.store(false, std::memory_order_release);
            return;
        }

        const bool actual = ReadFrameworkFocus();
        s_focused.store(actual, std::memory_order_release);
        DLOG(Input, "ToggleCursor: currently focused={} -> {}", actual, actual ? "FocusOff" : "FocusOn");
        if (actual) FocusOff(); else FocusOn();
    }

    static void QueueToggleCursor()
    {
        if (!s_active.load()) return;
        bool expected = false;
        if (!s_focusTransition.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
            return;

        const auto generation = s_focusGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
        const auto view = s_view ? s_view->load(std::memory_order_acquire) : 0;
        s_restoreAfterPlacement.store(false, std::memory_order_release);
        s_restoreAfterReactivation.store(false, std::memory_order_release);
        if (!PrismaCraftingThreading::Dispatch([generation, view] {
                if (generation != s_focusGeneration.load(std::memory_order_acquire) ||
                    !s_view || view != s_view->load(std::memory_order_acquire)) return;
                ToggleCursor();
            })) {
            if (generation == s_focusGeneration.load(std::memory_order_acquire) &&
                s_view && view == s_view->load(std::memory_order_acquire))
                s_focusTransition.store(false, std::memory_order_release);
            logger::error("WorkshopInputHook: cursor focus handoff could not be queued");
        }
    }

    static LRESULT Swallow(const char* a_reason, USHORT a_vk, bool a_up)
    {
        DLOG(Input, "swallowed vk={:#x} {} -- {}", a_vk, a_up ? "up" : "down", a_reason);
        return 0;
    }

    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR, DWORD_PTR)
    {
        if (s_active.load()) {

            if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP ||
                uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) {

                const USHORT vk = static_cast<USHORT>(wParam);
                const bool up = (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP);
                if (PlacementReleaseGateOwns(vk, up)) return Swallow("placement release gate", vk, up);
                if (PlacementTransitionOwns(vk, up)) return Swallow("placement transition", vk, up);
                if (WorkshopPlacementInput::HandlePlaceKey(vk, up, PlacementGhostReady(), PanelFocusedNow()))
                    return Swallow("focused placement", vk, up);
                if (vk == VK_ESCAPE && WorkshopPlacementInput::EscapeOwns(
                        up, s_placementTransition.load(std::memory_order_acquire) || PlacementGhostReady()))
                    return Swallow("placement escape", vk, up);

                if (s_focusTransition.load(std::memory_order_acquire) && IsPanelKey(vk) &&
                    !(up && IsMovementKey(vk)))
                    return Swallow("panel key during focus transition", vk, up);

                const bool altHeld = (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) &&
                                     (lParam & (1 << 29));

                const bool typing = s_typing.load() && PanelFocusedNow();
                if (!altHeld && !typing &&
                    WorkshopFreedom::OwnsKey(static_cast<std::uint16_t>(wParam)))
                    return Swallow("WorkshopFreedom owns this key", vk, up);
            }

            if (uMsg == WM_INPUT) {
                RAWINPUT ri{};
                UINT sz = sizeof(ri);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &ri, &sz,
                                    sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                    ri.header.dwType == RIM_TYPEKEYBOARD) {
                    const USHORT vk = NormalizeNumpad(ri.data.keyboard);
                    const bool   up = (ri.data.keyboard.Flags & RI_KEY_BREAK) != 0;

                    DLOG(Input, "raw key vk={:#x} {} typing={} panelFocused={}",
                         vk, up ? "up" : "down", s_typing.load(), PanelFocusedNow());

                    if (PlacementReleaseGateOwns(vk, up)) return Swallow("placement release gate", vk, up);
                    if (PlacementTransitionOwns(vk, up)) return Swallow("placement transition", vk, up);
                    if (WorkshopPlacementInput::HandlePlaceKey(vk, up, PlacementGhostReady(), PanelFocusedNow()))
                        return Swallow("focused placement", vk, up);
                    if (vk == VK_ESCAPE && WorkshopPlacementInput::EscapeOwns(
                            up, s_placementTransition.load(std::memory_order_acquire) || PlacementGhostReady()))
                        return Swallow("placement escape", vk, up);
                    if (s_focusTransition.load(std::memory_order_acquire) && IsPanelKey(vk) &&
                        !(up && IsMovementKey(vk)))
                        return Swallow("panel key during focus transition", vk, up);

                    const bool typing = s_typing.load() && PanelFocusedNow();
                    if (up && typing) {
                        (void)WorkshopFreedom::HandleKey(vk, true);
                        if (vk == kToggleKey) s_insHeld.store(false);
                    }
                    if (typing && vk != VK_ESCAPE)
                        return Swallow("key while typing in the search box", vk, up);
                    if (!typing && WorkshopFreedom::HandleKey(vk, up))
                        return Swallow("WorkshopFreedom handled it", vk, up);

                    if (vk == kToggleKey) {

                        if (typing) return Swallow("cursor toggle while typing", vk, up);
                        if (!up) {
                            if (!s_insHeld.exchange(true)) QueueToggleCursor();
                        } else {
                            s_insHeld = false;
                        }
                        return Swallow("cursor toggle key", vk, up);
                    }

                    if (PanelFocusedNow() && IsPanelKey(vk) &&
                        !(up && IsMovementKey(vk)))
                        return Swallow("panel key while panel focused", vk, up);
                }

                else if (ri.header.dwType == RIM_TYPEMOUSE && PanelFocusedNow()) {
                    constexpr USHORT kAnyButton =
                        RI_MOUSE_LEFT_BUTTON_DOWN   | RI_MOUSE_LEFT_BUTTON_UP |
                        RI_MOUSE_RIGHT_BUTTON_DOWN  | RI_MOUSE_RIGHT_BUTTON_UP |
                        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP;
                    if (ri.data.mouse.usButtonFlags & kAnyButton) {
                        DLOG(Input, "swallowed mouse button flags={:#x} -- panel focused",
                             ri.data.mouse.usButtonFlags);
                        return 0;
                    }

                    if (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                        DLOG(Input, "swallowed mouse wheel flags={:#x} -- panel focused",
                             ri.data.mouse.usButtonFlags);
                        return 0;
                    }
                    if (ri.data.mouse.usButtonFlags & RI_MOUSE_HWHEEL) {
                        DLOG(Input, "swallowed mouse tilt-wheel flags={:#x} -- panel focused",
                             ri.data.mouse.usButtonFlags);
                        return 0;
                    }
                }
            }

            else if (uMsg == WM_LBUTTONDOWN && PanelFocusedNow()) {
                if (!WorkshopInputRegions::InPanel(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
                    QueueToggleCursor();
            }
        }
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    static BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM lParam)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && GetParent(hwnd) == nullptr) {
            *reinterpret_cast<HWND*>(lParam) = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    void Init(PRISMA_UI_API::IVPrismaUI11** apiPtr, std::atomic<PrismaView>* viewPtr)
    {
        s_focusGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_focusTransition.store(false, std::memory_order_release);
        s_restoreAfterPlacement.store(false, std::memory_order_release);
        s_restoreAfterReactivation.store(false, std::memory_order_release);
        s_api  = apiPtr;
        s_view = viewPtr;
    }

    void OnPlacementStarted()
    {
        s_focusGeneration.fetch_add(1, std::memory_order_acq_rel);
        s_restoreAfterPlacement.store(IsFocused(), std::memory_order_release);
        s_transitionStartRefId.store(CurrentPlacementRefId(), std::memory_order_release);
        s_placementTransition.store(true, std::memory_order_release);
        ArmPlacementKeyReleaseGate();
        if (ApiOk()) {
            (*s_api)->Invoke(s_view->load(),
                "(function(){if(document.activeElement)document.activeElement.blur();})();",
                nullptr);
        }
        s_insHeld = false;
        const bool stillFocused = FocusOff();
        if (stillFocused) {
            s_placementTransition.store(false, std::memory_order_release);
            s_transitionStartRefId.store(0, std::memory_order_release);
            s_restoreAfterPlacement.store(false, std::memory_order_release);
            if (ApiOk())
                (*s_api)->InteropCall(s_view->load(), "showError", "Unable to release panel focus; placement cancelled");
            throw std::runtime_error("WorkshopInputHook: refusing placement while helper still owns focus");
        }
        logger::info("WorkshopInputHook: placement transition armed -- E/Enter held until new ghost is live");
    }

    bool Install()
    {
        if (s_hwnd) return true;
        HWND hwnd = nullptr;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&hwnd));
        if (!hwnd) {
            logger::error("WorkshopInputHook: no game window found — subclass not installed");
            return false;
        }

        HMODULE hSelf = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&SubclassProc), &hSelf);
        const UINT_PTR id = reinterpret_cast<UINT_PTR>(hSelf);
        RemoveWindowSubclass(hwnd, SubclassProc, id);
        if (!SetWindowSubclass(hwnd, SubclassProc, id, 0)) {
            logger::error("WorkshopInputHook: SetWindowSubclass failed GLE={}", GetLastError());
            return false;
        } else {
            s_hwnd = hwnd;
            if (!WorkshopControllerInput::Install(s_view, &s_active))
                logger::error("WorkshopInputHook: controller focus bridge unavailable");
            WorkshopInputRegions::Init(s_api, s_view, &s_hwnd);
            WorkshopPlacementInput::Init(s_api, s_view);
            logger::info("WorkshopInputHook: subclass installed on hwnd={:p}", static_cast<void*>(hwnd));
        }

        return true;
    }

    bool HoldingPlacement()
    {
        if (!crafting::ids::WorkshopDataResolvable()) return false;
        static REL::Relocation<bool*> activeEdit{ crafting::ids::ActiveEdit };
        return activeEdit.get() && *activeEdit && CurrentPlacementRefId() != 0;
    }

    bool IsInstalled() { return s_hwnd != nullptr; }

    void SetTextFocus(bool typing)
    {
        // Constraint: remember the raw request so a focus event that arrives before SetActive(true) is replayed, not lost.
        s_textFocusRequested.store(typing, std::memory_order_release);
        const bool now = typing && s_active.load();
        const bool was = s_typing.exchange(now);
        if (was != now) DLOG(Input, "text focus {} -> {}", was, now);
    }

    void SetActive(bool active)
    {
        const bool wasActive = s_active.exchange(active);
        if (wasActive != active) s_focusGeneration.fetch_add(1, std::memory_order_acq_rel);
        WorkshopPlacementInput::SetActive(active);
        if (active) {
            (void)WorkshopPlacementInput::BindPlaceEvent();
            WorkshopControllerInput::Activate();
            SetTextFocus(s_textFocusRequested.load(std::memory_order_acquire));
            if (!wasActive) ArmPlacementKeyReleaseGate();

            if (wasActive && s_restoreAfterPlacement.load(std::memory_order_acquire)) {
                s_placementTransition.store(false, std::memory_order_release);
                s_transitionStartRefId.store(0, std::memory_order_release);
                QueueFocusRestore(&s_restoreAfterPlacement, "placement");
            } else if (s_restoreAfterReactivation.load(std::memory_order_acquire)) {
                if (PanelFocusedNow()) {
                    s_restoreAfterReactivation.store(false, std::memory_order_release);
                } else {
                    QueueFocusRestore(&s_restoreAfterReactivation, "reactivation");
                }
            }
            return;
        }

        const bool restoreOnReactivate = wasActive && IsFocused();
        s_restoreAfterReactivation.store(restoreOnReactivate, std::memory_order_release);
        const bool stillFocused = FocusOff();
        s_active.store(stillFocused, std::memory_order_release);
        if (stillFocused)
            logger::error("WorkshopInputHook: deactivation left helper focused; native hook remains active");
        s_insHeld = false;
        s_typing  = false;
        s_textFocusRequested.store(false, std::memory_order_release);
    }

bool IsTyping()
{
    return s_typing.load(std::memory_order_acquire) && PanelFocusedNow();
}

    void OnPlacementEnded(bool active)
    {
        s_placementTransition.store(false, std::memory_order_release);
        s_transitionStartRefId.store(0, std::memory_order_release);
        SetActive(active);
    }

    void OnClosed()
    {
        s_focusGeneration.fetch_add(1, std::memory_order_acq_rel);
        SetActive(false);
        const bool stillFocused = IsFocused();
        s_active.store(stillFocused, std::memory_order_release);
        s_focused.store(stillFocused, std::memory_order_release);
        s_focusTransition.store(false, std::memory_order_release);
        s_placementTransition.store(false, std::memory_order_release);
        s_transitionStartRefId.store(0, std::memory_order_release);
        ClearPlacementKeyReleaseGate();
        s_restoreAfterPlacement.store(false, std::memory_order_release);
        s_restoreAfterReactivation.store(false, std::memory_order_release);

        WorkshopInputRegions::Reset();
        WorkshopPlacementInput::Reset();
    }

    bool IsFocused() { return PanelFocusedNow(); }
    bool IsActive() { return s_active.load(std::memory_order_acquire); }
}

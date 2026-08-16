#include "WorkshopInputHook.h"
#include "WorkshopFreedom.h"

#include <RE/U/UI.h>
#include <RE/I/IMenu.h>
#include <RE/U/UI_MENU_FLAGS.h>
#include <RE/M/MenuOpenCloseEvent.h>

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

static constexpr USHORT kToggleKey = 'C';

namespace WorkshopInputHook
{
    static PRISMA_UI_API::IVPrismaUI9** s_api  = nullptr;
    static std::atomic<PrismaView>*     s_view = nullptr;
    static HWND                         s_hwnd = nullptr;
    static std::atomic<bool>            s_active{ false };
    static std::atomic<bool>            s_focused{ false };
    static std::atomic<bool>            s_insHeld{ false };
    static std::atomic<bool>            s_typing{ false };

    static bool ApiOk() { return s_api && *s_api && s_view && (*s_api)->IsValid(s_view->load()); }

    static bool IsPanelKey(USHORT vk)
    {
        return vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' ||
               vk == 'E' || vk == 'F' || vk == 'R' || vk == 'Q' || vk == VK_RETURN;
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


    struct PanelRect { float x0, y0, x1, y1; };
    static std::mutex              s_rectMx;
    static std::vector<PanelRect>  s_rects;

    static bool InPanel(HWND hwnd, int x, int y)
    {
        RECT rc{};
        if (!GetClientRect(hwnd, &rc)) return false;
        const float w = static_cast<float>(rc.right), h = static_cast<float>(rc.bottom);
        if (w <= 0.0f || h <= 0.0f) return false;
        const float fx = x / w, fy = y / h;

        std::lock_guard<std::mutex> lk(s_rectMx);

        if (s_rects.empty()) return fy > 0.33f;
        for (const auto& r : s_rects)
            if (fx >= r.x0 && fx <= r.x1 && fy >= r.y0 && fy <= r.y1) return true;
        return false;
    }

    void SetPanelRects(const char* json)
    {
        std::vector<PanelRect> parsed;
        float v[4] = {};
        int   n    = 0;
        for (const char* p = json ? json : ""; *p; ) {
            if (*p == '-' || *p == '.' || (*p >= '0' && *p <= '9')) {
                char* end = nullptr;
                const float f = std::strtof(p, &end);
                if (end == p) { ++p; continue; }
                p = end;
                v[n++] = f;
                if (n == 4) {
                    n = 0;
                    const PanelRect r{ (std::min)(v[0], v[2]), (std::min)(v[1], v[3]),
                                       (std::max)(v[0], v[2]), (std::max)(v[1], v[3]) };
                    if (r.x1 > r.x0 && r.y1 > r.y0) parsed.push_back(r);
                }
            } else {
                ++p;
            }
        }
        std::lock_guard<std::mutex> lk(s_rectMx);
        s_rects.swap(parsed);
    }

    static void FocusOn()
    {
        if (!s_active.load() || !ApiOk() || s_focused.load()) return;
        auto* ui = RE::UI::GetSingleton();
        auto  menu = ui ? ui->GetMenu("WorkshopMenu") : nullptr;
        if (menu) {
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kUsesCursor, true);
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kUpdateUsesCursor, true);
        }
        if (ui) ui->RefreshCursor();
        (*s_api)->Focus(s_view->load(), false, true);

        s_focused = true;
        logger::info("WorkshopInputHook: cursor ON + helper focused (WASD navigates)");
    }

    static void FocusOff()
    {
        if (!s_focused.load()) return;
        if (ApiOk()) (*s_api)->Unfocus(s_view->load());
        auto* ui = RE::UI::GetSingleton();
        auto  menu = ui ? ui->GetMenu("WorkshopMenu") : nullptr;
        if (menu) {
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kUsesCursor, false);
            menu->UpdateFlag(RE::UI_MENU_FLAGS::kUpdateUsesCursor, false);
        }
        if (ui) ui->RefreshCursor();
        s_focused = false;
        s_typing  = false;
        logger::info("WorkshopInputHook: cursor OFF + back to build (WASD moves the player)");
    }

    void ToggleCursor()
    {
        if (!s_active.load()) return;
        if (s_focused.load()) FocusOff(); else FocusOn();
    }

    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR, DWORD_PTR)
    {
        if (s_active.load()) {

            if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP ||
                uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) {

                const bool altHeld = (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) &&
                                     (lParam & (1 << 29));
                if (!altHeld && !s_typing.load() &&
                    WorkshopFreedom::OwnsKey(static_cast<std::uint16_t>(wParam))) return 0;
            }

            if (uMsg == WM_INPUT) {
                RAWINPUT ri{};
                UINT sz = sizeof(ri);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &ri, &sz,
                                    sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                    ri.header.dwType == RIM_TYPEKEYBOARD) {
                    const USHORT vk = NormalizeNumpad(ri.data.keyboard);
                    const bool   up = (ri.data.keyboard.Flags & RI_KEY_BREAK) != 0;

                    if (!s_typing.load() && WorkshopFreedom::HandleKey(vk, up)) return 0;
                    if (vk == kToggleKey) {

                        if (s_typing.load()) return 0;
                        if (!up) {
                            if (!s_insHeld.exchange(true))
                                F4SE::GetTaskInterface()->AddTask([] { ToggleCursor(); });
                        } else {
                            s_insHeld = false;
                        }
                        return 0;
                    }

                    if (s_focused.load() && IsPanelKey(vk))
                        return 0;
                }

                else if (ri.header.dwType == RIM_TYPEMOUSE && s_focused.load()) {
                    constexpr USHORT kAnyButton =
                        RI_MOUSE_LEFT_BUTTON_DOWN   | RI_MOUSE_LEFT_BUTTON_UP |
                        RI_MOUSE_RIGHT_BUTTON_DOWN  | RI_MOUSE_RIGHT_BUTTON_UP |
                        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP;
                    if (ri.data.mouse.usButtonFlags & kAnyButton)
                        return 0;
                }
            }

            else if (uMsg == WM_LBUTTONDOWN && s_focused.load()) {
                if (!InPanel(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
                    F4SE::GetTaskInterface()->AddTask([] { if (s_focused.load()) ToggleCursor(); });
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

    void Init(PRISMA_UI_API::IVPrismaUI9** apiPtr, std::atomic<PrismaView>* viewPtr)
    {
        s_api  = apiPtr;
        s_view = viewPtr;
    }

    void OnPlacementStarted()
    {

        if (ApiOk()) {
            (*s_api)->Invoke(s_view->load(),
                "(function(){if(document.activeElement)document.activeElement.blur();})();",
                nullptr);
        }
        s_insHeld = false;
        FocusOff();
        logger::info("WorkshopInputHook: placement started -- unfocused, build input restored");
    }

    void Install()
    {
        if (s_hwnd) return;
        HWND hwnd = nullptr;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&hwnd));
        if (!hwnd) {
            logger::error("WorkshopInputHook: no game window found — subclass not installed");
            return;
        }
        s_hwnd = hwnd;

        HMODULE hSelf = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&SubclassProc), &hSelf);
        const UINT_PTR id = reinterpret_cast<UINT_PTR>(hSelf);
        RemoveWindowSubclass(hwnd, SubclassProc, id);
        if (!SetWindowSubclass(hwnd, SubclassProc, id, 0))
            logger::error("WorkshopInputHook: SetWindowSubclass failed GLE={}", GetLastError());
        else
            logger::info("WorkshopInputHook: subclass installed on hwnd={:p}", static_cast<void*>(hwnd));

    }

    void SetTextFocus(bool typing) { s_typing.store(typing); }

    void SetActive(bool active)
    {
        s_active = active;
        if (!active) {
            FocusOff();
            s_insHeld = false;
            s_typing  = false;

            { std::lock_guard<std::mutex> lk(s_rectMx); s_rects.clear(); }
        }
    }

    void OnClosed()
    {
        SetActive(false);
    }

    bool IsFocused() { return s_focused.load(); }
}

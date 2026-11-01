/*
 * For modders: Copy this file into your own project if you wish to use this API.
 */
#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <Windows.h>
#endif

#include <stddef.h>
#include <stdint.h>

typedef uint64_t PrismaView;

namespace PRISMA_UI_API {
    constexpr const auto PrismaUIPluginName = "PrismaUI_F4";

    // Available PrismaUI interface versions
    enum class InterfaceVersion : uint8_t {
        V1, V2, V3, V4, V5, V6, V7, V8,
        V9 = 136, V10 = 137, V11 = 138, V12 = 139
    };

    inline constexpr InterfaceVersion PrismaUIInterfaceV11 = InterfaceVersion::V11;
    inline constexpr InterfaceVersion PrismaUIInterfaceV12 = InterfaceVersion::V12;

    typedef void (*OnDomReadyCallback)(PrismaView view);
    typedef void (*JSCallback)(const char* result);
    typedef void (*JSListenerCallback)(const char* argument);

    // JavaScript console message severity level for use with RegisterConsoleCallback().
    enum class ConsoleMessageLevel : uint8_t { Log = 0, Warning, Error, Debug, Info };

    // Console message callback.
    typedef void (*ConsoleMessageCallback)(PrismaView view, ConsoleMessageLevel level, const char* message);

    enum class PrismaCapability : uint64_t {
        InputRegions = 1ull << 0,
    };

    // PrismaUI modder interface v1
    class IVPrismaUI1 {
    protected:
        ~IVPrismaUI1() = default;

    public:
        virtual PrismaView CreateView(const char* htmlPath,
                                      OnDomReadyCallback onDomReadyCallback = nullptr) noexcept = 0;

        virtual void Invoke(PrismaView view, const char* script, JSCallback callback = nullptr) noexcept = 0;

        virtual void InteropCall(PrismaView view, const char* functionName, const char* argument) noexcept = 0;

        virtual void RegisterJSListener(PrismaView view, const char* functionName,
                                        JSListenerCallback callback) noexcept = 0;

        virtual bool HasFocus(PrismaView view) noexcept = 0;

        virtual bool Focus(PrismaView view, bool pauseGame = false, bool disableFocusMenu = false) noexcept = 0;

        virtual void Unfocus(PrismaView view) noexcept = 0;

        virtual void Show(PrismaView view) noexcept = 0;

        virtual void Hide(PrismaView view) noexcept = 0;

        virtual bool IsHidden(PrismaView view) noexcept = 0;

        virtual int GetScrollingPixelSize(PrismaView view) noexcept = 0;

        virtual void SetScrollingPixelSize(PrismaView view, int pixelSize) noexcept = 0;

        virtual bool IsValid(PrismaView view) noexcept = 0;

        virtual void Destroy(PrismaView view) noexcept = 0;

        virtual void SetOrder(PrismaView view, int order) noexcept = 0;

        virtual int GetOrder(PrismaView view) noexcept = 0;

        virtual void CreateInspectorView(PrismaView view) noexcept = 0;

        virtual void SetInspectorVisibility(PrismaView view, bool visible) noexcept = 0;

        virtual bool IsInspectorVisible(PrismaView view) noexcept = 0;

        virtual void SetInspectorBounds(PrismaView view, float topLeftX, float topLeftY, unsigned int width,
                                        unsigned int height) noexcept = 0;

        virtual bool HasAnyActiveFocus() noexcept = 0;
    };

    // PrismaUI modder interface v2 (extends v1)
    class IVPrismaUI2 : public IVPrismaUI1 {
    protected:
        ~IVPrismaUI2() = default;

    public:
        virtual void RegisterConsoleCallback(PrismaView view, ConsoleMessageCallback callback) noexcept = 0;
    };

    // PrismaUI modder interface v3 (extends v2)
    class IVPrismaUI3 : public IVPrismaUI2 {
    protected:
        ~IVPrismaUI3() = default;

    public:
        virtual void RegisterTranslations(PrismaView view, const char* pluginName) noexcept = 0;
    };

    typedef void (*ViewEnumCallback)(PrismaView id, const char* htmlPath, void* userdata);

    // PrismaUI modder interface v4 (extends v3)
    class IVPrismaUI4 : public IVPrismaUI3 {
    protected:
        ~IVPrismaUI4() = default;

    public:
        virtual void BindUIEvent(PrismaView view, const char* functionName,
                                 JSListenerCallback callback) noexcept = 0;

        virtual void EnumerateViews(ViewEnumCallback callback, void* userdata) noexcept = 0;
    };

    // PrismaUI modder interface v5 (extends v4)
    class IVPrismaUI5 : public IVPrismaUI4 {
    protected:
        ~IVPrismaUI5() = default;

    public:
        virtual void* GetViewSRV(PrismaView view) noexcept = 0;

        virtual void SetViewOffscreen(PrismaView view, bool offscreen) noexcept = 0;

        virtual bool BindViewToGeometry(PrismaView view, void* rootObject, const char* geometryName) noexcept = 0;

        virtual bool BindViewToScreenTexture(PrismaView view, void* rootObject, const char* textureSubstring) noexcept = 0;

        virtual void UnbindViewFromGeometry(PrismaView view) noexcept = 0;
    };

    // PrismaUI modder interface v6 (extends v5)
    class IVPrismaUI6 : public IVPrismaUI5 {
    protected:
        ~IVPrismaUI6() = default;

    public:
        virtual bool SuppressHUDWidget(const char* className, bool suppress) noexcept = 0;

        virtual bool SuppressVanillaMenu(const char* menuName, bool suppress) noexcept = 0;

        virtual bool CloseVanillaMenu(const char* menuName) noexcept = 0;
    };

    typedef bool (*MenuSuppressPredicate)();

    // PrismaUI modder interface v7 (extends v6)
    class IVPrismaUI7 : public IVPrismaUI6 {
    protected:
        ~IVPrismaUI7() = default;

    public:
        virtual void SuppressVanillaMenuIf(const char* menuName, MenuSuppressPredicate predicate) noexcept = 0;

        virtual void EnableActivateChoiceFilter(bool enable, bool dropDefaultTake) noexcept = 0;

        virtual void SuppressActivateChoicePerk(uint32_t perkFormID, bool suppress) noexcept = 0;
    };

    typedef void (*ViewEnumCallbackEx)(PrismaView id, const char* htmlPath, const char* owner, void* userdata);

    enum class ViewHealth : int {
        kUnknown = -1,         // CEF not active, or `view` is not a currently-known handle
        kCreating = 0,         // CreateView issued, iframe mounting
        kDomReady = 1,         // OnDomReady fired
        kLive = 2,             // healthy / interactive
        kLoadFailed = 3,       // page/iframe failed to load
        kDomReadyTimeout = 4,  // never fired OnDomReady within the watchdog window
        kUnresponsive = 5,     // missed liveness pings
        kJsError = 6,          // accumulating uncaught/console errors (not fatal; flagged)
    };

    // PrismaUI modder interface v8 (extends v7)
    class IVPrismaUI8 : public IVPrismaUI7 {
    protected:
        ~IVPrismaUI8() = default;

    public:
        virtual void EnumerateViewsEx(ViewEnumCallbackEx callback, void* userdata) noexcept = 0;

        virtual bool GetActivateChoiceLabel(uint32_t buttonIndex, char* outBuffer, size_t bufferSize) noexcept = 0;

        virtual bool TriggerActivateChoice(uint32_t buttonIndex) noexcept = 0;

        virtual ViewHealth GetViewHealth(PrismaView view) noexcept = 0;

        virtual void SetViewOffscreenSize(PrismaView view, int width, int height) noexcept = 0;
    };

    // PrismaUI modder interface v9 (extends v8) -- controller / keyboard button prompts.
    //
    // Turns "which button is <this action>?" into a prompt token you hand to the shared shell script
    // window.PrismaCG, which renders it as real button art styled Xbox or PlayStation. The framework
    // tracks the active input device itself; no per-plugin setup is required beyond requesting V9.
    //
    // Rendering side (any view): include the framework's shared script/style, then
    //   element.innerHTML = window.PrismaCG.renderKey(promptToken, isPlayStation);   // a single prompt
    //   element.innerHTML = window.PrismaCG.chip("A", isPlayStation);                // a known button
    // A prompt token is either plain keyboard text ("E", "Space") or "gp:<canonical>" for a gamepad
    // button; renderKey handles both. isPlayStation comes from GetControllerStyle()==1.
    // CLOSED -- DO NOT APPEND TO THIS INTERFACE. IVPrismaUI10 derives from it, so appending here
    // shifts EVERY V10 slot for any consumer still on an older header, and the symptom is a call
    // landing on the wrong function rather than anything that looks like a version mismatch. The two
    // most recent methods below (SetViewOwnsEscape, SetViewOffscreenBackground) were appended on
    // 2026-07-19/20, BEFORE V10 existed on 2026-07-22, which is the only reason they were harmless.
    // That window is now shut. The same rule applies to every interface that has a derived one:
    // gaining a subclass closes it. New methods go in a new interface.
    class IVPrismaUI9 : public IVPrismaUI8 {
    protected:
        ~IVPrismaUI9() = default;

    public:
        // True if the player's last input came from a gamepad (not keyboard/mouse).
        virtual bool IsUsingGamepad() noexcept = 0;

        // Controller art style: 0 = Xbox, 1 = PlayStation. FO4 reads every pad as XInput (Xbox) natively,
        // so PlayStation is a display re-style, not engine behaviour -- this is the toggle for it.
        virtual int  GetControllerStyle() noexcept = 0;
        virtual void SetControllerStyle(int style) noexcept = 0;

        // Compatibility escape hatch for an input observation (0 = keyboard, 1 = mouse, 2 = gamepad)
        // seen outside PrismaUI's normal menu/gameplay input chains. The framework now self-tracks
        // ordinary input on both chains and emits `prisma-input-device-change` only when device/style
        // actually changes, so consumers should not call this once per normal key/button event.
        virtual void NoteInputDevice(int device) noexcept = 0;

        // Prompt token for a vanilla ControlMap user event ("Activate", "Ready Weapon", ...) on the
        // active device: the keyboard key text, or "gp:<canonical>" for a gamepad button. Copies up to
        // bufferSize-1 bytes + null. Returns false (outBuffer untouched) if the event has no binding on
        // the active device.
        virtual bool GetButtonPrompt(const char* userEvent, char* outBuffer, size_t bufferSize) noexcept = 0;

        // Canonical button id ("A","LB","DUp",...) for a raw RE::BS_BUTTON_CODE gamepad code, for a
        // plugin that already has a button code in hand. Returns false if it isn't a known gamepad code.
        virtual bool GetGamepadButtonName(uint32_t bsButtonCode, char* outBuffer, size_t bufferSize) noexcept = 0;

        // --- appended in-place to V9 (ABI-safe: added at the END of the vtable, existing offsets
        //     are unchanged, so a plugin built against the older V9 keeps working) ---

        // Opt this view into owning the Escape key while it is focused. Default is FALSE, which is
        // the historical behaviour: Escape is NOT forwarded to the browser and reaches the game, so
        // it toggles the vanilla PauseMenu (PauseMenu::TogglePauseMenu, gated on UI+0x1E8).
        //
        // With owns=true, Escape is delivered to the view's JS like any other key AND swallowed, so
        // the game never sees it. Only set this on a view that actually handles Escape in JS --
        // otherwise the player has no way out of the focused view, because suppressing the key also
        // removes the vanilla pause menu as an escape hatch.
        virtual void SetViewOwnsEscape(PrismaView view, bool owns) noexcept = 0;

        // Background of this view's OFFSCREEN browser, as ARGB. Default 0xFF000000 (opaque black).
        //
        // Pass 0 for a transparent browser, so a mesh-bound page can composite OVER the geometry it
        // is bound to instead of replacing that material wholesale. Without this, every pixel the
        // page does not paint arrives at alpha 255 and the page renders as a solid black quad.
        //
        // MUST be called BEFORE SetViewOffscreen(view, true). CEF fixes the background at browser
        // creation and the page cannot undo it in CSS -- its own transparency arrives too late. On a
        // live offscreen browser this only stores the value for the next creation and logs an error.
        //
        // The default is deliberately today's opaque black: existing on-mesh consumers render
        // against that backing, and making it transparent framework-wide would let the mesh show
        // through PrismaPipboy's screen. Opt in per view only.
        //
        // No-op if the Host is older than ABI v9.
        virtual void SetViewOffscreenBackground(PrismaView view, uint32_t argb) noexcept = 0;
    };

    // What kind of UI a view is, so the framework can answer "is another Prisma UI in the way?"
    // EnumerateViews deliberately reports EVERY view, including passive always-on HUD widgets, so a
    // consumer that treats "some other view exists and isn't hidden" as "something is in the way"
    // gets a permanent false positive on any setup running a HUD mod. Declaring a role is how a view
    // opts into being counted.
    enum class ViewRole : uint32_t {
        kUnspecified = 0,  // default -- never counted as an interactive panel
        kWidget = 1,       // passive always-on overlay (HUD element); never blocks anything
        kPanel = 2,        // interactive panel that occupies the screen and takes input
    };

    struct InputRegion {
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
    };

    // PrismaUI modder interface v10 (extends v9)
    //
    class IVPrismaUI10 : public IVPrismaUI9 {
    protected:
        ~IVPrismaUI10() = default;

    public:
        // Declare what this view is. Defaults to kUnspecified, which never blocks.
        //
        // DECLARE A ROLE ON ANY VIEW THAT TAKES INPUT. kUnspecified is never counted by
        // IsAnyPanelVisible, so an undeclared panel is invisible to every OTHER plugin's check: the
        // framework answers "nothing in the way" while your panel is on screen, and that plugin then
        // opens over the top of it. The failure is intermittent and looks like a bug in the ASKING
        // plugin. The framework logs a one-time warning to PrismaUI_F4.log for any view that takes
        // focus while still kUnspecified.
        virtual void SetViewRole(PrismaView view, ViewRole role) noexcept = 0;
        virtual ViewRole GetViewRole(PrismaView view) noexcept = 0;

        // The view that currently holds framework focus, or 0 if none. HasAnyActiveFocus() (V1)
        // answers the yes/no; this tells you which.
        virtual PrismaView GetFocusedView() noexcept = 0;

        // "Is another Prisma UI on screen and interactive right now?" -- the question a plugin
        // actually wants answered before opening its own panel. True if any view other than
        // ignoreView is currently focused, or is declared kPanel and not hidden. Passive HUD widgets
        // and undeclared views never make this true, so it does not fire just because a HUD is up.
        // Pass 0 for ignoreView to consider every view.
        virtual bool IsAnyPanelVisible(PrismaView ignoreView) noexcept = 0;

        virtual bool FocusOverlay(PrismaView view, bool pauseGame = false, bool disableFocusMenu = false) noexcept = 0;

        virtual bool SetInputRegions(PrismaView view, const InputRegion* regions, uint32_t count) noexcept = 0;
    };

    using GameThreadTaskCallback = void (*)(void* userdata);
    using GameThreadUIEventCallback = void (*)(const char* argument, void* userdata);

    // Additive extension surface, intentionally outside every existing virtual interface so V1-V12
    // keep their exact layout. Feature detection is the exported-function lookup performed below.
    enum class CursorPolicy : uint32_t {
        Default = 0,
        Hidden = 1,
    };

    class IVPrismaUI11 : public IVPrismaUI10 {
    protected:
        ~IVPrismaUI11() = default;

    public:
        virtual bool DispatchToGameThread(GameThreadTaskCallback callback, void* userdata) noexcept = 0;
        virtual bool IsGameThread() noexcept = 0;
        virtual bool BindGameThreadUIEvent(PrismaView view, const char* functionName,
                                           GameThreadUIEventCallback callback, void* userdata) noexcept = 0;
    };

    enum class ControllerActionBridgeState : uint8_t {
        Missing = 0,
        Pending = 1,
        Ready = 2,
        Failed = 3,
    };

    // V12 is authoritative and ABI-frozen after release; new virtuals require a new interface version.
    class IVPrismaUI12 : public IVPrismaUI11 {
    protected:
        ~IVPrismaUI12() = default;

    public:
        // true means the mapping was accepted and immediately owns matching focused physical input.
        // JS delivery is installed asynchronously per view; Pending still owns input and therefore
        // never falls through into legacy synthetic navigation or Fallout.
        virtual bool BindControllerAction(PrismaView view, const char* canonicalButton,
                                          const char* action) noexcept = 0;
        virtual bool UnbindControllerAction(PrismaView view, const char* canonicalButton) noexcept = 0;
        virtual void ClearControllerActions(PrismaView view) noexcept = 0;

        virtual ControllerActionBridgeState GetControllerActionBridgeState(PrismaView view) noexcept = 0;

        // Register an unfocused controller gesture for a visible kPanel. PrismaUI admits the callback
        // to its verified Fallout game/window-thread dispatcher before consuming the physical event.
        // If admission fails, Fallout keeps the event. Once admitted, PrismaUI owns that physical
        // sequence through release or until the target becomes focused.
        virtual bool BindControllerFocusEntry(PrismaView view, const char* canonicalButton,
                                              GameThreadTaskCallback callback, void* userdata) noexcept = 0;
        virtual bool UnbindControllerFocusEntry(PrismaView view, const char* canonicalButton) noexcept = 0;
    };

    // Maps an interface type to its version, so you can only ask for one that exists.
    template <typename T>
    struct InterfaceVersionMap;

    template <>
    struct InterfaceVersionMap<IVPrismaUI1> {
        static constexpr InterfaceVersion version = InterfaceVersion::V1;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI2> {
        static constexpr InterfaceVersion version = InterfaceVersion::V2;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI3> {
        static constexpr InterfaceVersion version = InterfaceVersion::V3;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI4> {
        static constexpr InterfaceVersion version = InterfaceVersion::V4;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI5> {
        static constexpr InterfaceVersion version = InterfaceVersion::V5;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI6> {
        static constexpr InterfaceVersion version = InterfaceVersion::V6;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI7> {
        static constexpr InterfaceVersion version = InterfaceVersion::V7;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI8> {
        static constexpr InterfaceVersion version = InterfaceVersion::V8;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI9> {
        static constexpr InterfaceVersion version = InterfaceVersion::V9;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI10> {
        static constexpr InterfaceVersion version = InterfaceVersion::V10;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI11> {
        static constexpr InterfaceVersion version = InterfaceVersion::V11;
    };

    template <>
    struct InterfaceVersionMap<IVPrismaUI12> {
        static constexpr InterfaceVersion version = InterfaceVersion::V12;
    };

    typedef void* (*RequestPluginAPIFunc)(InterfaceVersion interfaceVersion);
    typedef uint64_t (*GetPrismaCapabilitiesFunc)();
    typedef bool (*SetViewCursorPolicyFunc)(PrismaView view, CursorPolicy policy);
    typedef CursorPolicy (*GetViewCursorPolicyFunc)(PrismaView view);

    /// The one place that knows what the provider DLL is called.
    ///
    /// PrismaUI ships as a DIFFERENT FILE per game: `PrismaUI_F4.dll` on flat Fallout 4 and
    /// `PrismaUI_F4VR.dll` on Fallout 4 VR, because the two need different CommonLib builds and
    /// different game executables. A consumer plugin cannot know which one it was loaded beside,
    /// so looking up a single hardcoded name is wrong on exactly one of the two games -- it
    /// returned null on VR, and every API request silently produced nullptr.
    ///
    /// Only one provider is ever present in a process, so the order below is a fast path for the
    /// common case, not a precedence rule.
    ///
    /// Never resolve the provider by any other means, and never add a third name without adding
    /// it here: this function is what keeps "which DLL is the provider" a single fact.
#ifdef _WIN32
    [[nodiscard]] inline HMODULE GetPrismaProviderModule() noexcept {
        if (auto* flat = GetModuleHandleW(L"PrismaUI_F4.dll")) {
            return flat;
        }
        return GetModuleHandleW(L"PrismaUI_F4VR.dll");
    }

    // Controls only Prisma's software/compositor cursor. It does not release capture or reveal the
    // Fallout cursor. The setter accepts only the current focus owner and resets on handoff/unfocus.
    [[nodiscard]] inline bool SetViewCursorPolicy(PrismaView view, CursorPolicy policy) noexcept {
        auto pluginHandle = GetPrismaProviderModule();
        if (!pluginHandle) return false;
        auto fn = reinterpret_cast<SetViewCursorPolicyFunc>(
            GetProcAddress(pluginHandle, "PrismaUI_F4_SetViewCursorPolicy"));
        return fn ? fn(view, policy) : false;
    }

    [[nodiscard]] inline CursorPolicy GetViewCursorPolicy(PrismaView view) noexcept {
        auto pluginHandle = GetPrismaProviderModule();
        if (!pluginHandle) return CursorPolicy::Default;
        auto fn = reinterpret_cast<GetViewCursorPolicyFunc>(
            GetProcAddress(pluginHandle, "PrismaUI_F4_GetViewCursorPolicy"));
        return fn ? fn(view) : CursorPolicy::Default;
    }

    [[nodiscard]] inline void* RequestPluginAPI(
        InterfaceVersion a_interfaceVersion = InterfaceVersion::V1) {
        auto pluginHandle = GetPrismaProviderModule();
        if (!pluginHandle) {
            return nullptr;
        }

        auto requestAPIFunction =
            reinterpret_cast<RequestPluginAPIFunc>(GetProcAddress(pluginHandle, "RequestPluginAPI"));

        if (requestAPIFunction) {
            return requestAPIFunction(a_interfaceVersion);
        }

        return nullptr;
    }

    [[nodiscard]] inline uint64_t GetPrismaCapabilities() noexcept {
        auto pluginHandle = GetPrismaProviderModule();
        if (!pluginHandle) {
            return 0;
        }

        auto getCapabilities =
            reinterpret_cast<GetPrismaCapabilitiesFunc>(GetProcAddress(pluginHandle, "PrismaUI_F4_GetCapabilities"));
        return getCapabilities ? getCapabilities() : 0;
    }

    [[nodiscard]] inline bool HasPrismaCapability(PrismaCapability capability) noexcept {
        return (GetPrismaCapabilities() & static_cast<uint64_t>(capability)) != 0;
    }

    template <typename T>
    [[nodiscard]] inline T* RequestPluginAPI() {
        return static_cast<T*>(RequestPluginAPI(InterfaceVersionMap<T>::version));
    }
#endif  // _WIN32
}

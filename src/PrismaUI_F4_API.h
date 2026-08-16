/*
 * For modders: Copy this file into your own project if you wish to use this API.
 */
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <Windows.h>
#include <stdint.h>

typedef uint64_t PrismaView;

namespace PRISMA_UI_API {
    constexpr const auto PrismaUIPluginName = "PrismaUI_F4";

    // Available PrismaUI interface versions
    enum class InterfaceVersion : uint8_t { V1, V2, V3, V4, V5, V6, V7, V8, V9, V10 };

    typedef void (*OnDomReadyCallback)(PrismaView view);
    typedef void (*JSCallback)(const char* result);
    typedef void (*JSListenerCallback)(const char* argument);

    // JavaScript console message severity level for use with RegisterConsoleCallback().
    enum class ConsoleMessageLevel : uint8_t { Log = 0, Warning, Error, Debug, Info };

    // Console message callback.
    typedef void (*ConsoleMessageCallback)(PrismaView view, ConsoleMessageLevel level, const char* message);

    // PrismaUI modder interface v1
    class IVPrismaUI1 {
    protected:
        ~IVPrismaUI1() = default;

    public:
        // Create view.
        virtual PrismaView CreateView(const char* htmlPath,
                                      OnDomReadyCallback onDomReadyCallback = nullptr) noexcept = 0;

        // Send JS code to UI.
        virtual void Invoke(PrismaView view, const char* script, JSCallback callback = nullptr) noexcept = 0;

        // Call JS function through JS Interop API (best performance).
        virtual void InteropCall(PrismaView view, const char* functionName, const char* argument) noexcept = 0;

        // Register JS listener.
        virtual void RegisterJSListener(PrismaView view, const char* functionName,
                                        JSListenerCallback callback) noexcept = 0;

        // Returns true if view has focus.
        virtual bool HasFocus(PrismaView view) noexcept = 0;

        // Set focus on view.
        virtual bool Focus(PrismaView view, bool pauseGame = false, bool disableFocusMenu = false) noexcept = 0;

        // Remove focus from view.
        virtual void Unfocus(PrismaView view) noexcept = 0;

        // Show a hidden view.
        virtual void Show(PrismaView view) noexcept = 0;

        // Hide a visible view.
        virtual void Hide(PrismaView view) noexcept = 0;

        // Returns true if view is hidden.
        virtual bool IsHidden(PrismaView view) noexcept = 0;

        // Get scroll size in pixels.
        virtual int GetScrollingPixelSize(PrismaView view) noexcept = 0;

        // Set scroll size in pixels.
        virtual void SetScrollingPixelSize(PrismaView view, int pixelSize) noexcept = 0;

        // Returns true if view exists.
        virtual bool IsValid(PrismaView view) noexcept = 0;

        // Completely destroy view.
        virtual void Destroy(PrismaView view) noexcept = 0;

        // Set view order.
        virtual void SetOrder(PrismaView view, int order) noexcept = 0;

        // Get view order.
        virtual int GetOrder(PrismaView view) noexcept = 0;

        // Create inspector view for debugging.
        virtual void CreateInspectorView(PrismaView view) noexcept = 0;

        // Show or hide the inspector overlay.
        virtual void SetInspectorVisibility(PrismaView view, bool visible) noexcept = 0;

        // Returns true if inspector is visible.
        virtual bool IsInspectorVisible(PrismaView view) noexcept = 0;

        // No-op under CEF: DevTools opens in your external browser, not an in-game overlay, so
        // there is no window for these bounds to apply to. Kept for source compatibility.
        virtual void SetInspectorBounds(PrismaView view, float topLeftX, float topLeftY, unsigned int width,
                                        unsigned int height) noexcept = 0;

        // Returns true if any view has active focus.
        virtual bool HasAnyActiveFocus() noexcept = 0;
    };

    // PrismaUI modder interface v2 (extends v1)
    class IVPrismaUI2 : public IVPrismaUI1 {
    protected:
        ~IVPrismaUI2() = default;

    public:
        // Register a callback to receive JavaScript console messages from a view.
        // Pass nullptr to unregister.
        virtual void RegisterConsoleCallback(PrismaView view, ConsoleMessageCallback callback) noexcept = 0;
    };

    // PrismaUI modder interface v3 (extends v2)
    class IVPrismaUI3 : public IVPrismaUI2 {
    protected:
        ~IVPrismaUI3() = default;

    public:
        // Register translations for a view from a Fallout 4 translation file.
        // pluginName is the bare plugin name matching the translation file, e.g. "MyPlugin_F4".
        // The framework detects the game language, loads Data\Interface\Translations\<pluginName>_<lang>.txt,
        // and injects window.L10N / window.t into the page before scripts run (OnWindowObjectReady).
        // Call this immediately after CreateView, before the DOM is ready.
        virtual void RegisterTranslations(PrismaView view, const char* pluginName) noexcept = 0;
    };

    // Callback type for EnumerateViews. Called once per view with the view's ID and its
    // original html path (relative to Data/PrismaUI_F4/views/, e.g. "debug_panel.html").
    typedef void (*ViewEnumCallback)(PrismaView id, const char* htmlPath, void* userdata);

    // PrismaUI modder interface v4 (extends v3)
    class IVPrismaUI4 : public IVPrismaUI3 {
    protected:
        ~IVPrismaUI4() = default;

    public:
        // JS listener whose callback runs on the game thread, so you can touch RE:: state
        // directly without AddTask. Prefer this over RegisterJSListener when you need game state.
        virtual void BindUIEvent(PrismaView view, const char* functionName,
                                 JSListenerCallback callback) noexcept = 0;

        // Enumerate all currently-registered views across all plugins.
        // Callback is invoked synchronously for each view; htmlPath is the relative path
        // passed to CreateView (e.g., "Interface/PrismaMCM/mcm.html").
        // Safe to call from any thread.
        virtual void EnumerateViews(ViewEnumCallback callback, void* userdata) noexcept = 0;
    };

    // PrismaUI modder interface v5 (extends v4)
    class IVPrismaUI5 : public IVPrismaUI4 {
    protected:
        ~IVPrismaUI5() = default;

    public:
        // Returns the view's live D3D11 shader-resource view (ID3D11ShaderResourceView*),
        // cast to void* to avoid pulling d3d11.h into the API header.
        // The SRV is valid as long as the view exists and has rendered at least one frame.
        // Returns nullptr if the view is invalid or not yet rendered.
        // Do not Release() it. The framework owns the SRV.
        virtual void* GetViewSRV(PrismaView view) noexcept = 0;

        // Marks a view as offscreen: it keeps rendering to its texture (even while hidden)
        // but is never drawn as a 2D screen overlay. Use for on-mesh rendering where the
        // caller binds the view's SRV onto game geometry itself.
        virtual void SetViewOffscreen(PrismaView view, bool offscreen) noexcept = 0;

        // On-mesh rendering: bind this view's live SRV onto a named geometry's diffuse
        // texture, so the view's pixels appear on that 3D surface (and ride its UVs as it
        // animates). rootObject is an RE::NiAVObject* (e.g. player->Get3D(true)) passed as
        // void* to keep CommonLib out of this header; geometryName is the BSGeometry node
        // name to find under that root (e.g. "Screen:0" for the Pip-Boy screen).
        // The view should be SetViewOffscreen(true) first so it renders without a 2D overlay.
        // Returns true on success. The original SRV is cached for UnbindViewFromGeometry.
        // Call this AFTER the geometry exists in the scenegraph (e.g. on Pip-Boy open) and
        // re-bind each time, as the model can be rebuilt between sessions.
        virtual bool BindViewToGeometry(PrismaView view, void* rootObject, const char* geometryName) noexcept = 0;

        // Like BindViewToGeometry, but finds the target by its DIFFUSE TEXTURE path
        // (case-insensitive substring) instead of node name. This binds to ANY Pip-Boy
        // screen regardless of model: every Pip-Boy replacer maps the screen to a texture
        // named "PipBoyScreen_d" (the engine assigns the Pip-Boy render target by that
        // texture), so BindViewToScreenTexture(view, root, "pipboyscreen") just works.
        virtual bool BindViewToScreenTexture(PrismaView view, void* rootObject, const char* textureSubstring) noexcept = 0;

        // Restores the geometry's original SRV and clears the binding. Call while the bound
        // geometry is still valid (e.g. on Pip-Boy close), before the model is torn down.
        virtual void UnbindViewFromGeometry(PrismaView view) noexcept = 0;
    };

    // PrismaUI modder interface v6 (extends v5)
    class IVPrismaUI6 : public IVPrismaUI5 {
    protected:
        ~IVPrismaUI6() = default;

    public:
        // Hide or restore a vanilla HUD widget by class name, e.g. "HUDCompass",
        // "HUDAmmoCounter", "HUDPlayerHealthMeter". Works by patching the widget's own
        // CanBeVisible() vtable slot; setting _visible on the movie loses the race against the
        // widget's per-tick update. Survives HUDMenu rebuilds and save loads on its own.
        // Returns false for an unknown class name, or on NG/AE (OG 1.10.163 only for now).
        virtual bool SuppressHUDWidget(const char* className, bool suppress) noexcept = 0;

        // Suppress (true) or un-suppress (false) a full vanilla menu by its MENU_NAME (e.g.
        // "PipboyMenu") by forcing its Scaleform movie invisible. Automatically reapplied every
        // time the menu reopens (a fresh IMenu instance is created per open, unlike HUD
        // widgets) for as long as suppression is active for that name.
        virtual bool SuppressVanillaMenu(const char* menuName, bool suppress) noexcept = 0;

        // Fully closes a vanilla menu right now. Does not affect future opens on its own --
        // combine with SuppressVanillaMenu(name, true) if it should also stay hidden.
        virtual bool CloseVanillaMenu(const char* menuName) noexcept = 0;
    };

    // Checked every time a menu registered with SuppressVanillaMenuIf tries to open. Return
    // true to suppress that open, false to let it through (say, when a modifier key is held).
    // Runs synchronously on the game thread, so keep it cheap and don't block.
    typedef bool (*MenuSuppressPredicate)();

    // PrismaUI modder interface v7 (extends v6)
    class IVPrismaUI7 : public IVPrismaUI6 {
    protected:
        ~IVPrismaUI7() = default;

    public:
        // Conditional SuppressVanillaMenu: the predicate is re-checked on every open instead
        // of a fixed on/off flag. When it returns true the menu is hidden immediately (both
        // SetVisible(false) so it never renders a frame, and a queued kHide so it actually
        // leaves the menu stack rather than sitting there invisible eating input). Pass null to
        // unregister. Use this when suppression depends on live game state.
        virtual void SuppressVanillaMenuIf(const char* menuName, MenuSuppressPredicate predicate) noexcept = 0;

        // Appended to the end of V7's vtable rather than minting a new interface for two
        // related calls. Existing offsets are unchanged, so this stays ABI-safe.

        // Filter the vanilla multi-activate row on lootable corpses and containers, i.e. the
        // "E) TAKE  R) TRANSFER  Space) FIELD DRESS" prompt. Hooks
        // ActivateChoiceListener::PopulateData (OG only; warns and no-ops on NG/AE).
        // dropDefaultTake also removes button 0, the ref's default "Take". Only applies to
        // lootable refs, so doors, terminals and NPCs are left alone. The first run logs each
        // choice's parent-perk FormID to PrismaUI_F4.log so you can find the ones you want gone.
        // Perk-added choices are kept unless you pass their FormID to SuppressActivateChoicePerk.
        virtual void EnableActivateChoiceFilter(bool enable, bool dropDefaultTake) noexcept = 0;

        // Drop (suppress=true) or restore (false) a specific perk's activate-choice entry from the
        // vanilla multi-activate row, by full runtime FormID (e.g. the vanilla "Transfer" perk).
        virtual void SuppressActivateChoicePerk(uint32_t perkFormID, bool suppress) noexcept = 0;
    };

    // Callback type for EnumerateViewsEx (V8). Same as ViewEnumCallback, plus `owner`: the basename
    // of the module that created the view (e.g. "PrismaHUD_F4.dll"), resolved by the framework from
    // the calling module's return address at CreateView time -- real module identity, not a
    // self-reported string a plugin could get wrong or spoof. "" if the creator's module couldn't be
    // resolved, or the view predates ownership tracking. A distinct typedef + method rather than
    // changing ViewEnumCallback/EnumerateViews in place, so V4-V7 callers keep working unchanged
    // with no owner data and no recompile required.
    typedef void (*ViewEnumCallbackEx)(PrismaView id, const char* htmlPath, const char* owner, void* userdata);

    // Per-view health state for GetViewHealth. Kept in sync by hand with the framework's internal
    // enum: this header stays free of any CEF types so plugins don't have to pull them in.
    // kUnknown is only meaningful here, for "CEF isn't running" or "never heard of that view".
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
        // Same as EnumerateViews (V4), but also reports which plugin created each view. Callback is
        // invoked synchronously for each view; safe to call from any thread.
        virtual void EnumerateViewsEx(ViewEnumCallbackEx callback, void* userdata) noexcept = 0;

        // --- appended in-place to V8 (ABI-safe: added at the END of the vtable, existing offsets
        //     unchanged) -- same precedent as the V7 activate-choice-filter pair above ---

        // Read the label of the vanilla multi-activate choice currently captured at buttonIndex
        // (0..3) -- e.g. "Field Dress" for a hunting-perk choice. Requires
        // EnableActivateChoiceFilter(true, ...) to be active; capture only runs while that filter's
        // PopulateData detour is installed. Copies up to bufferSize-1 bytes plus a null terminator
        // into outBuffer. Returns false (outBuffer untouched) if that slot has no valid captured
        // choice right now, or the filter isn't enabled.
        virtual bool GetActivateChoiceLabel(uint32_t buttonIndex, char* outBuffer, size_t bufferSize) noexcept = 0;

        // Fire the captured choice at buttonIndex, replaying the same call the engine makes when
        // the vanilla row's button is clicked. Call it soon after reading the label with
        // GetActivateChoiceLabel: the captured listener isn't reference-counted here, so it can go
        // stale if the player's target changes in between. Returns false if that slot is empty.
        virtual bool TriggerActivateChoice(uint32_t buttonIndex) noexcept = 0;

        // Current health of a view: load errors, piling-up JS console errors, a missed onDomReady,
        // missed liveness pings. Returns kUnknown if CEF isn't running or the handle is unknown.
        // See ViewHealth below for the states.
        virtual ViewHealth GetViewHealth(PrismaView view) noexcept = 0;

        // Resolution of a view's offscreen browser (see SetViewOffscreen).
        //
        // Match the aspect ratio of the target quad, not the game window. The texture is stretched
        // over the mesh's 0..1 UVs, so a 16:9 page on the vanilla terminal screen (1.21:1) comes out
        // squashed about 32% horizontally. For that screen use something like 1308x1080.
        //
        // Safe to call before SetViewOffscreen(true): the size is remembered and applied when the
        // browser is created, so the page lays out correctly from its first frame instead of
        // reflowing mid-load. On a live offscreen browser it resizes in place. No-op if width or
        // height is <= 0, or the Host is older than ABI v6.
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

        // Feed an input-device observation (0 = keyboard, 1 = mouse, 2 = gamepad) from a plugin's own
        // input sink. The framework self-tracks menu-context input; a plugin that also handles GAMEPLAY
        // input (e.g. a crosshair looter) should call this so the device stays correct out in the world.
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

    // PrismaUI modder interface v10 (extends v9)
    //
    // NOTE FOR MAINTAINERS: new methods go in a NEW interface, never appended to an existing one.
    // V7 and V8 were each appended to in place while V8/V9 already derived from them, which shifted
    // every later vtable slot -- a plugin built against the older header then calls the wrong
    // function entirely. Do not repeat that here.
    class IVPrismaUI10 : public IVPrismaUI9 {
    protected:
        ~IVPrismaUI10() = default;

    public:
        // Declare what this view is. Defaults to kUnspecified, which never blocks.
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

    typedef void* (*RequestPluginAPIFunc)(InterfaceVersion interfaceVersion);

    [[nodiscard]] inline void* RequestPluginAPI(InterfaceVersion a_interfaceVersion = InterfaceVersion::V1) {
        auto pluginHandle = GetModuleHandleW(L"PrismaUI_F4.dll");
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

    template <typename T>
    [[nodiscard]] inline T* RequestPluginAPI() {
        return static_cast<T*>(RequestPluginAPI(InterfaceVersionMap<T>::version));
    }
}

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
#include <type_traits>

typedef uint64_t PrismaView;

namespace PRISMA_UI_FLAT_API {
    constexpr const auto PrismaUIPluginName = "PrismaUI_F4";
    using OnDomReadyCallback = void (*)(PrismaView);
    using JSCallback = void (*)(const char*);
    using JSListenerCallback = void (*)(const char*);
    using GameThreadTaskCallback = void (*)(void*);
    using GameThreadUIEventCallback = void (*)(const char*, void*);
    enum class ConsoleMessageLevel : uint8_t { Log = 0, Warning, Error, Debug, Info };
    using ConsoleMessageCallback = void (*)(PrismaView, ConsoleMessageLevel, const char*);
    using ViewEnumCallback = void (*)(PrismaView, const char*, void*);
    using ViewEnumCallbackEx = void (*)(PrismaView, const char*, const char*, void*);
    using MenuSuppressPredicate = bool (*)();
    enum class ViewHealth : int { kUnknown = -1, kCreating, kDomReady, kLive, kLoadFailed, kDomReadyTimeout, kUnresponsive, kJsError };
    enum class ViewRole : uint32_t { kUnspecified = 0, kWidget = 1, kPanel = 2 };
    enum class ControllerActionBridgeState : uint8_t { Missing = 0, Pending, Ready, Failed };
    enum class CursorPolicy : uint32_t { Default = 0, Hidden = 1 };
    struct InputRegion { int32_t x; int32_t y; int32_t width; int32_t height; };
    enum class ApiFeature : uint32_t {
        Core = 1,
        Controller = 2,
        GameThread = 3,
        Meta = 4,
        View = 5,
        Interop = 6,
        Localization = 7,
        Render = 8,
        Input = 9,
        Menu = 10
    };
    inline constexpr uint32_t CoreApiVersion = 1;
    inline constexpr uint32_t ControllerApiVersion = 1;
    inline constexpr uint32_t GameThreadApiVersion = 1;
    inline constexpr uint32_t MetaApiVersion = 1;
    inline constexpr uint32_t ViewApiVersion = 1;
    inline constexpr uint32_t InteropApiVersion = 1;
    inline constexpr uint32_t LocalizationApiVersion = 1;
    inline constexpr uint32_t RenderApiVersion = 1;
    inline constexpr uint32_t InputApiVersion = 1;
    inline constexpr uint32_t MenuApiVersion = 1;

    using CoreCreateView = PrismaView (*)(const char*, OnDomReadyCallback) noexcept;
    using CoreInvoke = void (*)(PrismaView, const char*, JSCallback) noexcept;
    using CoreInteropCall = void (*)(PrismaView, const char*, const char*) noexcept;
    using CoreRegisterJSListener = void (*)(PrismaView, const char*, JSListenerCallback) noexcept;
    using CoreHasFocus = bool (*)(PrismaView) noexcept;
    using CoreFocus = bool (*)(PrismaView, bool, bool) noexcept;
    using CoreUnfocus = void (*)(PrismaView) noexcept;
    using CoreShow = void (*)(PrismaView) noexcept;
    using CoreHide = void (*)(PrismaView) noexcept;
    using CoreIsHidden = bool (*)(PrismaView) noexcept;
    using CoreGetScrollingPixelSize = int (*)(PrismaView) noexcept;
    using CoreSetScrollingPixelSize = void (*)(PrismaView, int) noexcept;
    using CoreIsValid = bool (*)(PrismaView) noexcept;
    using CoreDestroy = void (*)(PrismaView) noexcept;
    using CoreSetOrder = void (*)(PrismaView, int) noexcept;
    using CoreGetOrder = int (*)(PrismaView) noexcept;
    using CoreCreateInspectorView = void (*)(PrismaView) noexcept;
    using CoreSetInspectorVisibility = void (*)(PrismaView, bool) noexcept;
    using CoreIsInspectorVisible = bool (*)(PrismaView) noexcept;
    using CoreSetInspectorBounds = void (*)(PrismaView, float, float, unsigned int, unsigned int) noexcept;
    using CoreHasAnyActiveFocus = bool (*)() noexcept;
    using CoreRegisterConsoleCallback = void (*)(PrismaView, ConsoleMessageCallback) noexcept;
    using CoreRegisterTranslations = void (*)(PrismaView, const char*) noexcept;
    using CoreBindUIEvent = void (*)(PrismaView, const char*, JSListenerCallback) noexcept;
    using CoreEnumerateViews = void (*)(ViewEnumCallback, void*) noexcept;
    using CoreGetViewSRV = void* (*)(PrismaView) noexcept;
    using CoreSetViewOffscreen = void (*)(PrismaView, bool) noexcept;
    using CoreBindViewToGeometry = bool (*)(PrismaView, void*, const char*) noexcept;
    using CoreBindViewToScreenTexture = bool (*)(PrismaView, void*, const char*) noexcept;
    using CoreUnbindViewFromGeometry = void (*)(PrismaView) noexcept;
    using CoreSuppressHUDWidget = bool (*)(const char*, bool) noexcept;
    using CoreSuppressVanillaMenu = bool (*)(const char*, bool) noexcept;
    using CoreCloseVanillaMenu = bool (*)(const char*) noexcept;
    using CoreSuppressVanillaMenuIf = void (*)(const char*, MenuSuppressPredicate) noexcept;
    using CoreEnableActivateChoiceFilter = void (*)(bool, bool) noexcept;
    using CoreSuppressActivateChoicePerk = void (*)(uint32_t, bool) noexcept;
    using CoreEnumerateViewsEx = void (*)(ViewEnumCallbackEx, void*) noexcept;
    using CoreGetActivateChoiceLabel = bool (*)(uint32_t, char*, size_t) noexcept;
    using CoreTriggerActivateChoice = bool (*)(uint32_t) noexcept;
    using CoreGetViewHealth = ViewHealth (*)(PrismaView) noexcept;
    using CoreSetViewOffscreenSize = void (*)(PrismaView, int, int) noexcept;
    using CoreSetViewOffscreenBackground = void (*)(PrismaView, uint32_t) noexcept;
    struct CoreAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        CoreCreateView CreateView;
        CoreInvoke Invoke;
        CoreInteropCall InteropCall;
        CoreRegisterJSListener RegisterJSListener;
        CoreHasFocus HasFocus;
        CoreFocus Focus;
        CoreUnfocus Unfocus;
        CoreShow Show;
        CoreHide Hide;
        CoreIsHidden IsHidden;
        CoreGetScrollingPixelSize GetScrollingPixelSize;
        CoreSetScrollingPixelSize SetScrollingPixelSize;
        CoreIsValid IsValid;
        CoreDestroy Destroy;
        CoreSetOrder SetOrder;
        CoreGetOrder GetOrder;
        CoreCreateInspectorView CreateInspectorView;
        CoreSetInspectorVisibility SetInspectorVisibility;
        CoreIsInspectorVisible IsInspectorVisible;
        CoreSetInspectorBounds SetInspectorBounds;
        CoreHasAnyActiveFocus HasAnyActiveFocus;
        CoreRegisterConsoleCallback RegisterConsoleCallback;
        CoreRegisterTranslations RegisterTranslations;
        CoreBindUIEvent BindUIEvent;
        CoreEnumerateViews EnumerateViews;
        CoreGetViewSRV GetViewSRV;
        CoreSetViewOffscreen SetViewOffscreen;
        CoreBindViewToGeometry BindViewToGeometry;
        CoreBindViewToScreenTexture BindViewToScreenTexture;
        CoreUnbindViewFromGeometry UnbindViewFromGeometry;
        CoreSuppressHUDWidget SuppressHUDWidget;
        CoreSuppressVanillaMenu SuppressVanillaMenu;
        CoreCloseVanillaMenu CloseVanillaMenu;
        CoreSuppressVanillaMenuIf SuppressVanillaMenuIf;
        CoreEnableActivateChoiceFilter EnableActivateChoiceFilter;
        CoreSuppressActivateChoicePerk SuppressActivateChoicePerk;
        CoreEnumerateViewsEx EnumerateViewsEx;
        CoreGetActivateChoiceLabel GetActivateChoiceLabel;
        CoreTriggerActivateChoice TriggerActivateChoice;
        CoreGetViewHealth GetViewHealth;
        CoreSetViewOffscreenSize SetViewOffscreenSize;
        CoreSetViewOffscreenBackground SetViewOffscreenBackground;
    };

    using ControllerIsUsingGamepad = bool (*)() noexcept;
    using ControllerGetStyle = int (*)() noexcept;
    using ControllerSetStyle = void (*)(int) noexcept;
    using ControllerNoteInputDevice = void (*)(int) noexcept;
    using ControllerGetButtonPrompt = bool (*)(const char*, char*, size_t) noexcept;
    using ControllerGetGamepadButtonName = bool (*)(uint32_t, char*, size_t) noexcept;
    using ControllerSetViewOwnsEscape = void (*)(PrismaView, bool) noexcept;
    using ControllerSetViewRole = void (*)(PrismaView, ViewRole) noexcept;
    using ControllerGetViewRole = ViewRole (*)(PrismaView) noexcept;
    using ControllerGetFocusedView = PrismaView (*)() noexcept;
    using ControllerIsAnyPanelVisible = bool (*)(PrismaView) noexcept;
    using ControllerFocusOverlay = bool (*)(PrismaView, bool, bool) noexcept;
    using ControllerSetInputRegions = bool (*)(PrismaView, const InputRegion*, uint32_t) noexcept;
    using ControllerBindAction = bool (*)(PrismaView, const char*, const char*) noexcept;
    using ControllerUnbindAction = bool (*)(PrismaView, const char*) noexcept;
    using ControllerClearActions = void (*)(PrismaView) noexcept;
    using ControllerGetActionBridgeState = ControllerActionBridgeState (*)(PrismaView) noexcept;
    using ControllerBindFocusEntry = bool (*)(PrismaView, const char*, GameThreadTaskCallback, void*) noexcept;
    using ControllerUnbindFocusEntry = bool (*)(PrismaView, const char*) noexcept;
    using ControllerSetNativeGamepad = bool (*)(PrismaView, bool) noexcept;
    struct ControllerAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        ControllerIsUsingGamepad IsUsingGamepad;
        ControllerGetStyle GetControllerStyle;
        ControllerSetStyle SetControllerStyle;
        ControllerNoteInputDevice NoteInputDevice;
        ControllerGetButtonPrompt GetButtonPrompt;
        ControllerGetGamepadButtonName GetGamepadButtonName;
        ControllerSetViewOwnsEscape SetViewOwnsEscape;
        ControllerSetViewRole SetViewRole;
        ControllerGetViewRole GetViewRole;
        ControllerGetFocusedView GetFocusedView;
        ControllerIsAnyPanelVisible IsAnyPanelVisible;
        ControllerFocusOverlay FocusOverlay;
        ControllerSetInputRegions SetInputRegions;
        ControllerBindAction BindControllerAction;
        ControllerUnbindAction UnbindControllerAction;
        ControllerClearActions ClearControllerActions;
        ControllerGetActionBridgeState GetControllerActionBridgeState;
        ControllerBindFocusEntry BindControllerFocusEntry;
        ControllerUnbindFocusEntry UnbindControllerFocusEntry;
        ControllerSetNativeGamepad SetNativeGamepad;
    };

    using GameThreadDispatch = bool (*)(GameThreadTaskCallback, void*) noexcept;
    using GameThreadIsCurrent = bool (*)() noexcept;
    using GameThreadBindUIEvent = bool (*)(PrismaView, const char*, GameThreadUIEventCallback, void*) noexcept;
    struct GameThreadAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        GameThreadDispatch DispatchToGameThread;
        GameThreadIsCurrent IsGameThread;
        GameThreadBindUIEvent BindGameThreadUIEvent;
    };

    static_assert(std::is_standard_layout_v<CoreAPI>);
    static_assert(std::is_standard_layout_v<ControllerAPI>);
    static_assert(std::is_standard_layout_v<GameThreadAPI>);
    static_assert(std::is_trivially_copyable_v<CoreAPI>);
    static_assert(std::is_trivially_copyable_v<ControllerAPI>);
    static_assert(std::is_trivially_copyable_v<GameThreadAPI>);
    static_assert(__builtin_offsetof(CoreAPI, structSize) == 0);
    static_assert(__builtin_offsetof(CoreAPI, apiVersion) == 4);
    static_assert(__builtin_offsetof(ControllerAPI, structSize) == 0);
    static_assert(__builtin_offsetof(ControllerAPI, apiVersion) == 4);
    static_assert(__builtin_offsetof(GameThreadAPI, structSize) == 0);
    static_assert(__builtin_offsetof(GameThreadAPI, apiVersion) == 4);

    using MetaProviderName = const char* (*)() noexcept;
    using MetaApiBuild = uint32_t (*)() noexcept;
    using MetaHasFeature = bool (*)(ApiFeature, uint32_t) noexcept;
    struct MetaAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        MetaProviderName ProviderName;
        MetaApiBuild ApiBuild;
        MetaHasFeature HasFeature;
    };

    using ViewSetCursorPolicy = bool (*)(PrismaView, CursorPolicy) noexcept;
    using ViewGetCursorPolicy = CursorPolicy (*)(PrismaView) noexcept;
    struct ViewAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        CoreCreateView CreateView;
        CoreDestroy Destroy;
        CoreShow Show;
        CoreHide Hide;
        CoreFocus Focus;
        CoreUnfocus Unfocus;
        CoreHasFocus HasFocus;
        CoreIsValid IsValid;
        CoreIsHidden IsHidden;
        CoreGetViewHealth GetViewHealth;
        CoreSetOrder SetOrder;
        CoreGetOrder GetOrder;
        ViewSetCursorPolicy SetViewCursorPolicy;
        ViewGetCursorPolicy GetViewCursorPolicy;
    };

    struct InteropAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        CoreInvoke Invoke;
        CoreInteropCall InteropCall;
        CoreRegisterJSListener RegisterJSListener;
        CoreBindUIEvent BindUIEvent;
        CoreRegisterConsoleCallback RegisterConsoleCallback;
    };

    using LocalizationRegisterTranslationsV4 = bool (*)(PrismaView, const char*) noexcept;
    struct LocalizationAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        CoreRegisterTranslations RegisterTranslations;
        LocalizationRegisterTranslationsV4 RegisterTranslationsV4;
    };

    struct RenderAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        CoreGetViewSRV GetViewSRV;
        CoreSetViewOffscreen SetViewOffscreen;
        CoreSetViewOffscreenSize SetViewOffscreenSize;
        CoreSetViewOffscreenBackground SetViewOffscreenBackground;
        CoreBindViewToGeometry BindViewToGeometry;
        CoreBindViewToScreenTexture BindViewToScreenTexture;
        CoreUnbindViewFromGeometry UnbindViewFromGeometry;
    };

    struct InputAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        CoreGetScrollingPixelSize GetScrollingPixelSize;
        CoreSetScrollingPixelSize SetScrollingPixelSize;
        ControllerSetInputRegions SetInputRegions;
    };

    struct MenuAPI {
        uint32_t structSize;
        uint32_t apiVersion;
        CoreSuppressHUDWidget SuppressHUDWidget;
        CoreSuppressVanillaMenu SuppressVanillaMenu;
        CoreCloseVanillaMenu CloseVanillaMenu;
        CoreSuppressVanillaMenuIf SuppressVanillaMenuIf;
        CoreEnableActivateChoiceFilter EnableActivateChoiceFilter;
        CoreSuppressActivateChoicePerk SuppressActivateChoicePerk;
        CoreEnumerateViews EnumerateViews;
        CoreEnumerateViewsEx EnumerateViewsEx;
        CoreGetActivateChoiceLabel GetActivateChoiceLabel;
        CoreTriggerActivateChoice TriggerActivateChoice;
    };

    static_assert(std::is_standard_layout_v<MetaAPI>);
    static_assert(std::is_standard_layout_v<ViewAPI>);
    static_assert(std::is_standard_layout_v<InteropAPI>);
    static_assert(std::is_standard_layout_v<LocalizationAPI>);
    static_assert(std::is_standard_layout_v<RenderAPI>);
    static_assert(std::is_standard_layout_v<InputAPI>);
    static_assert(std::is_standard_layout_v<MenuAPI>);
    static_assert(std::is_trivially_copyable_v<MetaAPI>);
    static_assert(std::is_trivially_copyable_v<ViewAPI>);
    static_assert(std::is_trivially_copyable_v<InteropAPI>);
    static_assert(std::is_trivially_copyable_v<LocalizationAPI>);
    static_assert(std::is_trivially_copyable_v<RenderAPI>);
    static_assert(std::is_trivially_copyable_v<InputAPI>);
    static_assert(std::is_trivially_copyable_v<MenuAPI>);

#define PRISMA_FLAT_SLOT(table, field, index) \
    static_assert(__builtin_offsetof(table, field) == 8 + (index) * sizeof(void*), #table "." #field)
    PRISMA_FLAT_SLOT(CoreAPI, CreateView, 0);
    PRISMA_FLAT_SLOT(CoreAPI, Invoke, 1);
    PRISMA_FLAT_SLOT(CoreAPI, InteropCall, 2);
    PRISMA_FLAT_SLOT(CoreAPI, RegisterJSListener, 3);
    PRISMA_FLAT_SLOT(CoreAPI, HasFocus, 4);
    PRISMA_FLAT_SLOT(CoreAPI, Focus, 5);
    PRISMA_FLAT_SLOT(CoreAPI, Unfocus, 6);
    PRISMA_FLAT_SLOT(CoreAPI, Show, 7);
    PRISMA_FLAT_SLOT(CoreAPI, Hide, 8);
    PRISMA_FLAT_SLOT(CoreAPI, IsHidden, 9);
    PRISMA_FLAT_SLOT(CoreAPI, GetScrollingPixelSize, 10);
    PRISMA_FLAT_SLOT(CoreAPI, SetScrollingPixelSize, 11);
    PRISMA_FLAT_SLOT(CoreAPI, IsValid, 12);
    PRISMA_FLAT_SLOT(CoreAPI, Destroy, 13);
    PRISMA_FLAT_SLOT(CoreAPI, SetOrder, 14);
    PRISMA_FLAT_SLOT(CoreAPI, GetOrder, 15);
    PRISMA_FLAT_SLOT(CoreAPI, CreateInspectorView, 16);
    PRISMA_FLAT_SLOT(CoreAPI, SetInspectorVisibility, 17);
    PRISMA_FLAT_SLOT(CoreAPI, IsInspectorVisible, 18);
    PRISMA_FLAT_SLOT(CoreAPI, SetInspectorBounds, 19);
    PRISMA_FLAT_SLOT(CoreAPI, HasAnyActiveFocus, 20);
    PRISMA_FLAT_SLOT(CoreAPI, RegisterConsoleCallback, 21);
    PRISMA_FLAT_SLOT(CoreAPI, RegisterTranslations, 22);
    PRISMA_FLAT_SLOT(CoreAPI, BindUIEvent, 23);
    PRISMA_FLAT_SLOT(CoreAPI, EnumerateViews, 24);
    PRISMA_FLAT_SLOT(CoreAPI, GetViewSRV, 25);
    PRISMA_FLAT_SLOT(CoreAPI, SetViewOffscreen, 26);
    PRISMA_FLAT_SLOT(CoreAPI, BindViewToGeometry, 27);
    PRISMA_FLAT_SLOT(CoreAPI, BindViewToScreenTexture, 28);
    PRISMA_FLAT_SLOT(CoreAPI, UnbindViewFromGeometry, 29);
    PRISMA_FLAT_SLOT(CoreAPI, SuppressHUDWidget, 30);
    PRISMA_FLAT_SLOT(CoreAPI, SuppressVanillaMenu, 31);
    PRISMA_FLAT_SLOT(CoreAPI, CloseVanillaMenu, 32);
    PRISMA_FLAT_SLOT(CoreAPI, SuppressVanillaMenuIf, 33);
    PRISMA_FLAT_SLOT(CoreAPI, EnableActivateChoiceFilter, 34);
    PRISMA_FLAT_SLOT(CoreAPI, SuppressActivateChoicePerk, 35);
    PRISMA_FLAT_SLOT(CoreAPI, EnumerateViewsEx, 36);
    PRISMA_FLAT_SLOT(CoreAPI, GetActivateChoiceLabel, 37);
    PRISMA_FLAT_SLOT(CoreAPI, TriggerActivateChoice, 38);
    PRISMA_FLAT_SLOT(CoreAPI, GetViewHealth, 39);
    PRISMA_FLAT_SLOT(CoreAPI, SetViewOffscreenSize, 40);
    PRISMA_FLAT_SLOT(CoreAPI, SetViewOffscreenBackground, 41);
    PRISMA_FLAT_SLOT(ControllerAPI, IsUsingGamepad, 0);
    PRISMA_FLAT_SLOT(ControllerAPI, GetControllerStyle, 1);
    PRISMA_FLAT_SLOT(ControllerAPI, SetControllerStyle, 2);
    PRISMA_FLAT_SLOT(ControllerAPI, NoteInputDevice, 3);
    PRISMA_FLAT_SLOT(ControllerAPI, GetButtonPrompt, 4);
    PRISMA_FLAT_SLOT(ControllerAPI, GetGamepadButtonName, 5);
    PRISMA_FLAT_SLOT(ControllerAPI, SetViewOwnsEscape, 6);
    PRISMA_FLAT_SLOT(ControllerAPI, SetViewRole, 7);
    PRISMA_FLAT_SLOT(ControllerAPI, GetViewRole, 8);
    PRISMA_FLAT_SLOT(ControllerAPI, GetFocusedView, 9);
    PRISMA_FLAT_SLOT(ControllerAPI, IsAnyPanelVisible, 10);
    PRISMA_FLAT_SLOT(ControllerAPI, FocusOverlay, 11);
    PRISMA_FLAT_SLOT(ControllerAPI, SetInputRegions, 12);
    PRISMA_FLAT_SLOT(ControllerAPI, BindControllerAction, 13);
    PRISMA_FLAT_SLOT(ControllerAPI, UnbindControllerAction, 14);
    PRISMA_FLAT_SLOT(ControllerAPI, ClearControllerActions, 15);
    PRISMA_FLAT_SLOT(ControllerAPI, GetControllerActionBridgeState, 16);
    PRISMA_FLAT_SLOT(ControllerAPI, BindControllerFocusEntry, 17);
    PRISMA_FLAT_SLOT(ControllerAPI, UnbindControllerFocusEntry, 18);
    PRISMA_FLAT_SLOT(ControllerAPI, SetNativeGamepad, 19);
    PRISMA_FLAT_SLOT(GameThreadAPI, DispatchToGameThread, 0);
    PRISMA_FLAT_SLOT(GameThreadAPI, IsGameThread, 1);
    PRISMA_FLAT_SLOT(GameThreadAPI, BindGameThreadUIEvent, 2);
#undef PRISMA_FLAT_SLOT

    template <ApiFeature Feature> struct FeatureTable;
    template <> struct FeatureTable<ApiFeature::Core> { using type = CoreAPI; };
    template <> struct FeatureTable<ApiFeature::Controller> { using type = ControllerAPI; };
    template <> struct FeatureTable<ApiFeature::GameThread> { using type = GameThreadAPI; };
    template <> struct FeatureTable<ApiFeature::Meta> { using type = MetaAPI; };
    template <> struct FeatureTable<ApiFeature::View> { using type = ViewAPI; };
    template <> struct FeatureTable<ApiFeature::Interop> { using type = InteropAPI; };
    template <> struct FeatureTable<ApiFeature::Localization> { using type = LocalizationAPI; };
    template <> struct FeatureTable<ApiFeature::Render> { using type = RenderAPI; };
    template <> struct FeatureTable<ApiFeature::Input> { using type = InputAPI; };
    template <> struct FeatureTable<ApiFeature::Menu> { using type = MenuAPI; };
    template <ApiFeature Feature>
    using FeatureTableT = typename FeatureTable<Feature>::type;

    using GetAPIProc = bool (*)(ApiFeature, uint32_t, void*, uint32_t) noexcept;

    inline bool HasCapability(const MetaAPI& meta, ApiFeature feature, uint32_t version) noexcept {
        return meta.HasFeature != nullptr && meta.HasFeature(feature, version);
    }
#ifdef _WIN32
    inline HMODULE GetPrismaProviderModule() noexcept {
        if (auto* flat = GetModuleHandleW(L"PrismaUI_F4.dll")) return flat;
        return GetModuleHandleW(L"PrismaUI_F4VR.dll");
    }
    inline bool GetAPI(ApiFeature feature, uint32_t version, void* table, uint32_t tableSize) noexcept {
        const auto module = GetPrismaProviderModule();
        if (!module) return false;
        const auto fn = reinterpret_cast<GetAPIProc>(GetProcAddress(module, "PrismaUI_F4_GetAPI"));
        return fn ? fn(feature, version, table, tableSize) : false;
    }
    template <ApiFeature Feature>
    inline bool Discover(uint32_t minimumVersion, FeatureTableT<Feature>& table) noexcept {
        table = {};
        if (!GetAPI(Feature, minimumVersion, &table, static_cast<uint32_t>(sizeof(table)))) return false;
        if (table.structSize < sizeof(uint32_t) * 2) return false;
        if (table.apiVersion < minimumVersion) return false;
        if (table.structSize < static_cast<uint32_t>(sizeof(table))) return false;
        return true;
    }
#endif
}

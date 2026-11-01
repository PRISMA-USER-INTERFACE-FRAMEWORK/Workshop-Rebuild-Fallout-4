#include "WorkshopControllerInput.h"

#include "ControllerActionTable.h"
#include "DebugLog.h"
#include "PrismaFlatApi.h"
#include "WorkshopFreedom.h"
#include "WorkshopInputHook.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <nlohmann/json.hpp>

namespace WorkshopControllerInput
{
    namespace
    {
        namespace Table = ControllerActionTable;
        using Json = nlohmann::ordered_json;

        PRISMA_UI_API::IVPrismaUI12* s_api = nullptr;

        struct Slot
        {
            std::atomic<PrismaView>* view = nullptr;
            std::atomic<bool>* active = nullptr;
            const Table::Binding* bindings = nullptr;
            std::size_t bindingCount = 0;
            const Table::Prompt* prompts = nullptr;
            std::size_t promptCount = 0;
            const char* promptFunction = nullptr;
            const char* owner = nullptr;
            PrismaView bound = 0;
            PrismaView focusEntryBoundView = 0;
            PrismaView browsingBoundView = 0;
        };

        enum class Phase
        {
            Focused,
            Always,
            Browsing,
        };

        std::atomic<bool> s_holding{ false };

        Slot s_workshop;
        Slot s_crafting;

        bool Ready(const Slot& slot)
        {
            if (!s_api || !slot.view) return false;
            if (slot.active && !slot.active->load(std::memory_order_acquire)) return false;
            const PrismaView view = slot.view->load(std::memory_order_acquire);
            return view != 0 && s_api->IsValid(view);
        }

        void EnterPanelFocus(void*)
        {
            if (!Ready(s_workshop)) return;
            if (WorkshopFreedom::FreeCameraEnabled()) {
                WorkshopFreedom::ToggleFreeCamera();
                return;
            }
            const PrismaView view = s_workshop.view->load(std::memory_order_acquire);
            if (!s_api->HasFocus(view)) WorkshopInputHook::ToggleCursor();
        }

        void TogglePlaceInRed(void*)
        {
            if (Ready(s_workshop)) WorkshopFreedom::ToggleFreePlace();
        }

        void ToggleFreeCam(void*)
        {
            if (Ready(s_workshop)) WorkshopFreedom::ToggleFreeCamera();
        }

        void ForwardToDock(void* userdata)
        {
            const auto* binding = static_cast<const Table::Binding*>(userdata);
            if (!binding || !Ready(s_workshop)) return;
            const PrismaView view = s_workshop.view->load(std::memory_order_acquire);
            s_api->InteropCall(view, "workshopUnfocusedAction", binding->action);
        }

        PRISMA_UI_API::GameThreadTaskCallback FocusEntryCallback(const char* action)
        {
            const std::string_view name(action);
            if (name == "workshop.panel_focus") return EnterPanelFocus;
            if (name == "workshop.free_place") return TogglePlaceInRed;
            if (name == "workshop.free_cam") return ToggleFreeCam;
            return ForwardToDock;
        }

        void ForgetBindings(Slot& slot)
        {
            slot.bound = 0;
            slot.focusEntryBoundView = 0;
            slot.browsingBoundView = 0;
        }

        bool InPhase(const Table::Binding& binding, Phase phase)
        {
            if (phase == Phase::Focused) return true;
            return binding.unfocused == (phase == Phase::Always ? Table::Unfocused::Always : Table::Unfocused::Browsing);
        }

        bool IsPanelFocus(const Table::Binding& binding)
        {
            return std::string_view(binding.action) == "workshop.panel_focus";
        }

        bool BindOne(const Slot& slot, PrismaView view, const Table::Binding& binding, Phase phase)
        {
            void* userdata = const_cast<Table::Binding*>(&binding);
            const bool bound = phase == Phase::Focused
                ? s_api->BindControllerAction(view, binding.button, binding.action)
                : s_api->BindControllerFocusEntry(view, binding.button, FocusEntryCallback(binding.action), userdata);
            if (!bound)
                logger::error("{}: PrismaUI refused controller {} on '{}'", slot.owner,
                              phase == Phase::Focused ? binding.action : "focus entry", binding.button);
            return bound;
        }

        bool BindPhase(Slot& slot, PrismaView view, Phase phase)
        {
            bool ok = true;
            bool anyFocusEntry = false;
            const Table::Binding* panelFocus = nullptr;
            for (std::size_t i = 0; i < slot.bindingCount; ++i) {
                const Table::Binding& binding = slot.bindings[i];
                if (phase != Phase::Focused && binding.unfocused == Table::Unfocused::Always && IsPanelFocus(binding))
                    panelFocus = &binding;
                if (!InPhase(binding, phase)) continue;
                if (phase != Phase::Focused && IsPanelFocus(binding)) continue;
                anyFocusEntry = anyFocusEntry || phase != Phase::Focused;
                ok = BindOne(slot, view, binding, phase) && ok;
            }
            if (phase == Phase::Browsing && slot.focusEntryBoundView != view) panelFocus = nullptr;
            if (panelFocus && (phase == Phase::Always || anyFocusEntry))
                ok = BindOne(slot, view, *panelFocus, phase) && ok;
            return ok;
        }

        void UnbindBrowsing(Slot& slot, PrismaView view)
        {
            for (std::size_t i = 0; i < slot.bindingCount; ++i) {
                const Table::Binding& binding = slot.bindings[i];
                if (InPhase(binding, Phase::Browsing)) s_api->UnbindControllerFocusEntry(view, binding.button);
            }
            slot.browsingBoundView = 0;
        }

        void SyncBrowsing(Slot& slot, PrismaView view)
        {
            if (s_holding.load(std::memory_order_acquire)) {
                UnbindBrowsing(slot, view);
                return;
            }
            if (slot.browsingBoundView == view) return;
            slot.browsingBoundView = BindPhase(slot, view, Phase::Browsing) ? view : 0;
        }

        bool BindActions(Slot& slot)
        {
            if (!Ready(slot)) return false;
            const PrismaView view = slot.view->load(std::memory_order_acquire);
            if (slot.bound != view)
                slot.bound = BindPhase(slot, view, Phase::Focused) ? view : 0;
            SyncBrowsing(slot, view);
            if (slot.focusEntryBoundView != view)
                slot.focusEntryBoundView = BindPhase(slot, view, Phase::Always) ? view : 0;
            return slot.bound == view;
        }

        void SyncHeldObject(void*)
        {
            if (!Ready(s_workshop)) return;
            SyncBrowsing(s_workshop, s_workshop.view->load(std::memory_order_acquire));
        }

        Json Tokens(const Table::Prompt& prompt)
        {
            const bool gamepad = s_api->IsUsingGamepad();
            const bool paired = prompt.buttons && std::strchr(prompt.buttons, '/') != nullptr;
            if (!paired) {
                const char* button = prompt.buttons && prompt.buttons[0] ? prompt.buttons : prompt.keyboard;
                return gamepad ? "gp:" + std::string(button) : std::string(prompt.keyboard);
            }
            if (!gamepad) return Json::array({prompt.keyboard});
            Json tokens = Json::array();
            const char* cursor = prompt.buttons;
            while (cursor && *cursor) {
                const char* end = cursor;
                while (*end && *end != '/') ++end;
                tokens.push_back("gp:" + std::string(cursor, static_cast<std::size_t>(end - cursor)));
                cursor = *end ? end + 1 : end;
            }
            return tokens;
        }

        std::string PromptJson(const Slot& slot)
        {
            Json payload = {{"ps", s_api->GetControllerStyle() == 1 ? 1 : 0},
                            {"items", Json::array()}, {"nav", Json::array()}};
            for (std::size_t i = 0; i < slot.promptCount; ++i) {
                const Table::Prompt& prompt = slot.prompts[i];
                const bool dockSlot = prompt.elementId && prompt.elementId[0];
                Json item = {{"t", Tokens(prompt)}, {"l", prompt.label}};
                if (dockSlot) item["id"] = prompt.elementId;
                payload[dockSlot ? "items" : "nav"].push_back(std::move(item));
            }
            return payload.dump(-1, ' ', false, Json::error_handler_t::replace);
        }

        void PushPrompts(const Slot& slot)
        {
            if (!s_api || !slot.view || !slot.prompts || !slot.promptFunction) return;
            const PrismaView view = slot.view->load(std::memory_order_acquire);
            if (!s_api->IsValid(view)) return;
            const std::string json = PromptJson(slot);
            s_api->InteropCall(view, slot.promptFunction, json.c_str());
        }

        void RefreshPrompts(const char*, void*)
        {
            PushPrompts(s_workshop);
            PushPrompts(s_crafting);
        }

        void ReleaseFocus(const char*, void*)
        {
            if (!Ready(s_workshop)) return;
            const PrismaView view = s_workshop.view->load(std::memory_order_acquire);
            if (s_api->HasFocus(view)) WorkshopInputHook::ToggleCursor();
        }

        void BindUIEvents(Slot& slot)
        {
            if (!Ready(slot)) return;
            const PrismaView view = slot.view->load(std::memory_order_acquire);
            if (!s_api->BindGameThreadUIEvent(view, "refreshPrompts", RefreshPrompts, nullptr))
                logger::error("{}: could not bind the prompt refresh callback", slot.owner);
        }
    }

    bool Install(std::atomic<PrismaView>* view, std::atomic<bool>* active)
    {
        s_api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI12>();
        s_workshop.view = view;
        s_workshop.active = active;
        s_workshop.bindings = Table::kWorkshopBindings;
        s_workshop.bindingCount = std::size(Table::kWorkshopBindings);
        s_workshop.prompts = Table::kWorkshopPrompts;
        s_workshop.promptCount = std::size(Table::kWorkshopPrompts);
        s_workshop.promptFunction = "setWorkshopPrompts";
        s_workshop.owner = "WorkshopControllerInput";
        ForgetBindings(s_workshop);
        if (!s_api) {
            logger::error("WorkshopControllerInput: PrismaUI V12 is unavailable");
            return false;
        }
        return true;
    }

    bool InstallCrafting(std::atomic<PrismaView>* view)
    {
        s_crafting.view = view;
        s_crafting.active = nullptr;
        s_crafting.bindings = Table::kCraftingBindings;
        s_crafting.bindingCount = std::size(Table::kCraftingBindings);
        s_crafting.prompts = Table::kCraftingPrompts;
        s_crafting.promptCount = std::size(Table::kCraftingPrompts);
        s_crafting.promptFunction = "setPrompts";
        s_crafting.owner = "WorkshopControllerInput(crafting)";
        ForgetBindings(s_crafting);
        if (!s_api) {
            s_api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI12>();
            if (!s_api) {
                logger::error("WorkshopControllerInput: PrismaUI V12 is unavailable for the crafting view");
                return false;
            }
        }
        return true;
    }

    void Activate()
    {
        if (!Ready(s_workshop)) return;
        BindUIEvents(s_workshop);
        const PrismaView view = s_workshop.view->load(std::memory_order_acquire);
        if (!s_api->BindGameThreadUIEvent(view, "prisma_release_focus", ReleaseFocus, nullptr))
            logger::error("WorkshopControllerInput: could not bind the panel focus release callback");
        if (!BindActions(s_workshop)) return;
        PushPrompts(s_workshop);
    }

    void ActivateCrafting()
    {
        if (!Ready(s_crafting)) return;
        BindUIEvents(s_crafting);
        if (!BindActions(s_crafting)) return;
        PushPrompts(s_crafting);
    }

    void PushCraftingPrompts(PrismaView view)
    {
        if (!s_api || !view || !s_api->IsValid(view)) return;
        if (!s_crafting.view || s_crafting.view->load(std::memory_order_acquire) != view) return;
        PushPrompts(s_crafting);
    }

    void NoteHeldObject(bool holding)
    {
        if (s_holding.exchange(holding, std::memory_order_acq_rel) == holding || !s_api) return;
        if (s_api->DispatchToGameThread(SyncHeldObject, nullptr)) return;
        bool expected = holding;
        s_holding.compare_exchange_strong(expected, !holding, std::memory_order_acq_rel);
        logger::error("WorkshopControllerInput: could not queue the held-object controller sync");
    }

    void NoteViewFocused(PrismaView view)
    {
        if (!view) return;
        PrismaFlatApi::SetCursorPolicy(view, PRISMA_UI_API::CursorPolicy::Default);

        if (!s_api) return;
        Slot* slot = nullptr;
        if (s_workshop.view && s_workshop.view->load(std::memory_order_acquire) == view)
            slot = &s_workshop;
        else if (s_crafting.view && s_crafting.view->load(std::memory_order_acquire) == view)
            slot = &s_crafting;
        if (!slot) return;

        if (!BindActions(*slot))
            logger::warn("WorkshopControllerInput: view={} is focused without a controller action route", view);
    }
}

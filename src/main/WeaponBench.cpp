// Constraint: close chain is VERIFIED in RE record re-pm-answers/37-weapon-bench-gaps-b.md Q2a (queueHide, hiding, AdvanceMovie sit-state branch); frame counts are untuned, TODO-verify in game.
#include "RuntimeInternal.h"
#include "WeaponBenchJson.h"
#include "WeaponBenchSnapshot.h"
#include <chrono>
#include <format>
#include <optional>
#include <windows.h>
#include <Xinput.h>

namespace WeaponBench
{
    namespace
    {
        namespace wb = crafting::weaponbench;

        enum class Phase : int { kIdle, kPending, kActive, kClosing };
        enum class Close : int { kNone, kAwaitInit, kQueued, kFallback };

        constexpr const char* kMenuName = "ExamineMenu";
        constexpr unsigned kInitRetryFrames = 30;
        constexpr unsigned kCloseWatchdogFrames = 120;
        constexpr unsigned kActivateRetryFrames = 300;
        constexpr int kFocusLostFrames = 120;
        constexpr std::chrono::seconds kReadyWaitTimeout{ 5 };

        std::atomic<bool> g_enabled{ false };
        std::atomic<bool> g_dev{ false };
        std::atomic<Phase> g_phase{ Phase::kIdle };
        std::atomic<int> g_focusLostFrames{ 0 };
        std::atomic<int> g_cursorReassertFrames{ 0 };
        std::chrono::steady_clock::time_point g_readyDeadline{};
        std::atomic<bool> g_jsReady{ false };
        std::atomic<std::uint32_t> g_menuGen{ 0 };
        std::atomic<std::uint32_t> g_sessionCounter{ 0 };
        std::atomic<PrismaView> g_wbView{ 0 };
        std::atomic<bool> g_wbViewReady{ false };
        std::atomic<std::uint32_t> g_session{ 0 };
        std::atomic<std::uint32_t> g_benchId{ 0 };
        std::atomic<Close> g_close{ Close::kNone };
        std::atomic<unsigned> g_closeFrames{ 0 };
        std::atomic<unsigned> g_activateFrames{ 0 };
        std::atomic<bool> g_fallbackSent{ false };
        std::atomic<bool> g_actionBusy{ false };

        class BusyGuard
        {
        public:
            BusyGuard() : m_owned(!g_actionBusy.exchange(true, std::memory_order_acq_rel)) {}
            ~BusyGuard()
            {
                if (m_owned) g_actionBusy.store(false, std::memory_order_release);
            }
            BusyGuard(const BusyGuard&) = delete;
            BusyGuard& operator=(const BusyGuard&) = delete;
            [[nodiscard]] bool Owned() const { return m_owned; }

        private:
            bool m_owned;
        };

        RE::ExamineMenu* FindExamineMenu()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return nullptr;
            auto menu = ui->GetMenu(kMenuName);
            return menu ? RE::fallout_cast<RE::ExamineMenu*>(menu.get()) : nullptr;
        }
        bool IsWeaponBench(RE::ExamineMenu& a_menu)
        {
            auto* ref = a_menu.workbenchRef.get();
            auto* base = ref ? ref->GetObjectReference() : nullptr;
            auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
            return furniture && furniture->wbData.type == RE::WorkbenchData::Type::kWeapons;
        }
        void SuppressVanilla(bool a_suppressed, bool a_skipExamineMovie = false)
        {
            if (auto* menu = a_skipExamineMovie ? nullptr : FindExamineMenu(); menu && menu->uiMovie) menu->uiMovie->SetVisible(!a_suppressed);
            for (const char* name : { "PromptMenu", "ButtonBarMenu", "PauseMenu", "CursorMenu" })
                a_suppressed ? VanillaMenuSuppression::Claim(name) : VanillaMenuSuppression::Release(name);
        }
        void Teardown(const char* a_why, bool a_menuClosing = false)
        {
            if (g_phase.exchange(Phase::kIdle, std::memory_order_acq_rel) == Phase::kIdle) return;
            g_close = Close::kNone;
            g_closeFrames = 0;
            g_fallbackSent.store(false, std::memory_order_release);
            g_focusLostFrames.store(0, std::memory_order_release);
            g_cursorReassertFrames.store(0, std::memory_order_release);
            const auto view = g_wbView.load(std::memory_order_acquire);
            if (g_api && g_api->IsValid(view)) {
                g_api->InteropCall(view, "hidePreviews", "");
                g_api->Unfocus(view);
                g_api->Hide(view);
            }
            SuppressVanilla(false, a_menuClosing);
            logger::info("PrismaCrafting: weapon bench session {} ended: {}", g_session.load(), a_why);
        }
        bool CloseFallback(RE::ExamineMenu& a_menu)
        {
            if (g_fallbackSent.exchange(true, std::memory_order_acq_rel)) return false;
            if (!a_menu.hiding) a_menu.queueHide = false;
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide);
            EndFurnitureInteractionForClose("weapon bench", g_benchId.load());
            logger::warn("PrismaCrafting: weapon bench close fallback sent (hide message, furniture exit)");
            return true;
        }
        void TickClose(RE::ExamineMenu& a_menu)
        {
            if (a_menu.hiding) return;
            ++g_closeFrames;
            switch (g_close) {
            case Close::kAwaitInit:
                if (a_menu.initialized) {
                    if (!a_menu.queueHide && !a_menu.hiding) a_menu.queueHide = true;
                    g_close = Close::kQueued;
                    g_closeFrames = 0;
                } else if (g_closeFrames >= kInitRetryFrames) {
                    CloseFallback(a_menu);
                    g_close = Close::kFallback;
                    g_closeFrames = 0;
                }
                break;
            case Close::kQueued:
                if (g_closeFrames >= kCloseWatchdogFrames) {
                    CloseFallback(a_menu);
                    g_close = Close::kFallback;
                    g_closeFrames = 0;
                }
                break;
            case Close::kFallback:
                if (g_closeFrames >= kCloseWatchdogFrames) Teardown("close watchdog expired; vanilla bench restored");
                break;
            case Close::kNone:
                break;
            }
        }
        void RequestClose()
        {
            const auto phase = g_phase.load(std::memory_order_acquire);
            if (phase == Phase::kClosing) {
                auto* menu = FindExamineMenu();
                if (!menu) Teardown("menu already gone");
                else if (!menu->hiding && CloseFallback(*menu)) {
                    g_close = Close::kFallback;
                    g_closeFrames = 0;
                }
                return;
            }
            if (phase != Phase::kActive) return;
            g_phase.store(Phase::kClosing, std::memory_order_release);
            g_close = Close::kAwaitInit;
            g_closeFrames = 0;
            logger::info("PrismaCrafting: weapon bench session {} closing", g_session.load());
            if (auto* menu = FindExamineMenu()) TickClose(*menu);
            else Teardown("menu already gone");
        }
        void PushBootstrap(PrismaView a_view)
        {
            const auto payload = BuildSnapshotPayload(std::format("wb-{}", g_session.load()));
            g_api->InteropCall(a_view, "setWeaponBench", payload.c_str());
        }
        void TryActivate()
        {
            if (g_phase.load(std::memory_order_acquire) != Phase::kPending ||
                !g_wbViewReady.load(std::memory_order_acquire)) return;
            const auto view = g_wbView.load(std::memory_order_acquire);
            if (!g_api || !g_api->IsValid(view)) {
                Teardown("view unavailable");
                return;
            }
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->GetMenuOpen("ExamineConfirmMenu")) {
                if (g_activateFrames.fetch_add(1, std::memory_order_acq_rel) + 1 >= kActivateRetryFrames)
                    Teardown("confirm menu stayed open");
                return;
            }
            auto* menu = FindExamineMenu();
            if (!menu || !menu->initialized) {
                // Constraint: taking focus before the examine menu finishes opening leaves the game holding the cursor.
                if (g_activateFrames.fetch_add(1, std::memory_order_acq_rel) + 1 >= kActivateRetryFrames)
                    Teardown("examine menu did not initialize");
                return;
            }
            g_api->Show(view);
            if (!g_api->Focus(view, true, false)) {
                logger::error("PrismaCrafting: weapon bench view focus rejected; vanilla bench kept");
                Teardown("focus rejected");
                return;
            }
            g_focusLostFrames.store(0, std::memory_order_release);
            g_jsReady.store(false, std::memory_order_release);
            g_readyDeadline = std::chrono::steady_clock::now() + kReadyWaitTimeout;
            WorkshopControllerInput::NoteViewFocused(view);
            SuppressVanilla(true);
            if (auto* menu = FindExamineMenu()) {
                menu->UpdateFlag(RE::UI_MENU_FLAGS::kUsesCursor, true);
                menu->UpdateFlag(RE::UI_MENU_FLAGS::kUpdateUsesCursor, true);
            }
            if (auto* ui = RE::UI::GetSingleton()) ui->RefreshCursor();
            g_cursorReassertFrames.store(5, std::memory_order_release);
            g_phase.store(Phase::kActive, std::memory_order_release);
            PushBootstrap(view);
            logger::info("PrismaCrafting: weapon bench session {} active (bench {:08X}, focused={}, cursor refreshed)",
                         g_session.load(), g_benchId.load(), g_api->HasFocus(view));
        }
        std::string SessionToken()
        {
            return std::format("wb-{}", g_session.load());
        }
        void ReplyAction(const std::string& a_requestId, wb::Reason a_reason, std::optional<std::string> a_selected)
        {
            const auto view = g_wbView.load(std::memory_order_acquire);
            if (!g_api || !g_api->IsValid(view)) return;
            const auto reply = wb::BuildActionReply(a_requestId, a_reason,
                nlohmann::json::parse(BuildSnapshotPayload(SessionToken(), a_selected)));
            const auto text = reply.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            g_api->InteropCall(view, "setWeaponBench", text.c_str());
        }
        // Constraint: Reason order puts every post-weapon check at or after RemoveUnsupported, and only those keep the requested weapon selected.
        bool KeepsSelection(wb::Reason a_reason)
        {
            return a_reason == wb::Reason::Ok || std::to_underlying(a_reason) >= std::to_underlying(wb::Reason::RemoveUnsupported);
        }
        void ReplyMinimal(const std::string& a_requestId, const char* a_reason, const char* a_message)
        {
            const auto view = g_wbView.load(std::memory_order_acquire);
            if (!g_api || !g_api->IsValid(view)) return;
            const auto reply = nlohmann::json{{"requestId", a_requestId}, {"ok", false},
                {"reason", a_reason ? a_reason : "view_closed"}, {"message", a_message ? a_message : ""}};
            g_api->InteropCall(view, "setWeaponBench", reply.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
        }
        void ReplyAfterFailure(const std::string& a_requestId) noexcept
        {
            if (a_requestId.empty()) return;
            try {
                ReplyAction(a_requestId, wb::Reason::StaleStack, std::nullopt);
            } catch (...) {
                logger::error("PrismaCrafting: weapon bench failure reply could not be sent");
            }
        }
        void HandleAction(const char* a_raw)
        {
            std::string pendingId;
            try {
                const auto in = nlohmann::json::parse(a_raw ? a_raw : "", nullptr, false);
                if (!in.is_object()) {
                    logger::warn("PrismaCrafting: weapon bench action ignored (not a JSON object)");
                    return;
                }
                const auto field = [&in](const char* a_key) -> std::optional<std::string> {
                    const auto it = in.find(a_key);
                    if (it == in.end() || !it->is_string()) return std::nullopt;
                    return it->get<std::string>();
                };
                const auto requestId = field("requestId");
                if (!requestId) {
                    logger::warn("PrismaCrafting: weapon bench action ignored (no string requestId)");
                    return;
                }
                if (g_phase.load(std::memory_order_acquire) != Phase::kActive) {
                    logger::debug("PrismaCrafting: weapon bench action received outside an active session");
                    ReplyMinimal(*requestId, "view_closed", "The weapon bench is closed.");
                    return;
                }
                pendingId = *requestId;
                if (field("session") != SessionToken()) {
                    ReplyAction(*requestId, wb::Reason::SessionMismatch, std::nullopt);
                    return;
                }
                const BusyGuard busy;
                if (!busy.Owned()) {
                    ReplyAction(*requestId, wb::Reason::Busy, std::nullopt);
                    return;
                }
                const auto name = field("action").value_or("");
                wb::ActionRequest req;
                req.session = SessionToken();
                req.requestId = *requestId;
                req.weaponId = field("weaponId").value_or("");
                req.slotId = field("slotId").value_or("");
                req.partId = field("attachmentId").value_or("");
                const auto revision = in.find("revision");
                const bool revisionOk = revision != in.end() && revision->is_number_unsigned();
                if (revisionOk) req.revision = revision->get<std::uint64_t>();
                const bool known = name == "selectWeapon" || name == "install" || name == "remove";
                if (name == "install") req.action = wb::Action::Install;
                else if (name == "remove") req.action = wb::Action::Remove;
                auto reason = wb::Reason::Ok;
                if (!known) {
                    reason = wb::Reason::BadId;
                } else if (!revisionOk || req.revision != Revision()) {
                    reason = wb::Reason::StaleRevision;
                } else {
                    wb::LiveState state;
                    state.enabled = true;
                    state.viewAlive = g_api && g_api->IsValid(g_wbView.load(std::memory_order_acquire));
                    state.menuAlive = FindExamineMenu() != nullptr;
                    state.playerCanAct = RE::PlayerCharacter::GetSingleton() != nullptr;
                    state.session = req.session;
                    reason = ValidateLive(req, state);
                    if (reason == wb::Reason::Ok && req.action == wb::Action::Install) reason = wb::Reason::InstallUnavailable;
                }
                ReplyAction(req.requestId, reason, KeepsSelection(reason) ? std::optional<std::string>(req.weaponId) : std::nullopt);
            } catch (const std::exception& e) {
                logger::error("PrismaCrafting: weapon bench action failed: {}", e.what());
                ReplyAfterFailure(pendingId);
            } catch (...) {
                logger::error("PrismaCrafting: weapon bench action failed (unknown exception)");
                ReplyAfterFailure(pendingId);
            }
        }
        void OnDomReady(PrismaView a_view)
        {
            g_wbViewReady.store(false, std::memory_order_release);
            if (!g_api || a_view == 0) return;
            if (!BindGameThreadUIEvent(a_view, "prisma_close", [](const char*, void*) {
                    // Constraint: the native Escape poll already moved the session to kClosing; the JS close is the same key.
                    if (g_phase.load(std::memory_order_acquire) != Phase::kClosing) RequestClose();
                })) {
                logger::critical("PrismaCrafting: weapon bench close binding failed; vanilla bench kept");
                return;
            }
            if (!BindGameThreadUIEvent(a_view, "weapon_bench_action", [](const char* a_arg, void*) { HandleAction(a_arg); })) {
                logger::critical("PrismaCrafting: weapon bench action binding failed; vanilla bench kept");
                return;
            }
            if (!BindGameThreadUIEvent(a_view, "weapon_bench_ready", [](const char*, void*) { g_jsReady.store(true, std::memory_order_release); })) {
                logger::critical("PrismaCrafting: weapon bench ready binding failed; vanilla bench kept");
                return;
            }
            g_wbViewReady.store(true, std::memory_order_release);
            g_jsReady.store(false, std::memory_order_release);
            logger::info("PrismaCrafting: weapon bench DOM ready (view={})", a_view);
            if (PrismaFlatApi::RegisterTranslations(a_view)) g_api->InteropCall(a_view, "applyTranslations", "");
            if (!PrismaCraftingThreading::AddGameTask([a_view] {
                    if (g_wbView.load(std::memory_order_acquire) != a_view) return;
                    if (g_phase.load(std::memory_order_acquire) == Phase::kActive && g_api->IsValid(a_view)) {
                        g_readyDeadline = std::chrono::steady_clock::now() + kReadyWaitTimeout;
                        PushBootstrap(a_view);
                    }
                    TryActivate();
                    WorkshopControllerInput::ActivateWeaponBench();
                }))
                logger::warn("PrismaCrafting: weapon bench activation not queued; vanilla bench kept");
        }
        bool EnsureView()
        {
            if (!g_api) return false;
            if (g_api->IsValid(g_wbView.load(std::memory_order_acquire))) return true;
            g_wbViewReady.store(false, std::memory_order_release);
            g_wbView.store(0, std::memory_order_release);
            try {
                const auto view = g_api->CreateView("weapon_bench.html", OnDomReady);
                g_wbView.store(view, std::memory_order_release);
                if (!g_api->IsValid(view)) return false;
                g_api->SetViewOwnsEscape(view, true);
                MarkViewAsPanel(view);
                if (!WorkshopControllerInput::InstallWeaponBench(&g_wbView))
                    logger::warn("PrismaCrafting: weapon bench view created without controller bindings");
                g_api->RegisterConsoleCallback(view,
                    [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel level, const char* msg) {
                        logger::info("[WB JS {}] {}", static_cast<int>(level), msg);
                    });
                g_api->Hide(view);
                logger::info("PrismaCrafting: weapon bench view created (id={})", view);
                return true;
            } catch (const std::exception& e) {
                logger::error("PrismaCrafting: weapon bench view creation failed: {}", e.what());
                return false;
            }
        }
        void BeginSession(std::uint32_t a_gen)
        {
            if (!g_enabled.load(std::memory_order_acquire) || g_menuGen.load(std::memory_order_acquire) != a_gen ||
                g_phase.load(std::memory_order_acquire) != Phase::kIdle) return;
            auto* menu = FindExamineMenu();
            if (menu && !menu->workbenchRef.get()) logger::debug("PrismaCrafting: ExamineMenu has no workbenchRef; vanilla bench kept");
            if (!menu || !IsWeaponBench(*menu)) return;
            if (OpenBenchIsFurnitureSwapper(kMenuName)) {
                logger::info("PrismaCrafting: ExamineMenu on a furniture-swapper bench -- vanilla weapon bench kept");
                return;
            }
            if (DevEnabled()) GrantDevTestLoadout();
            g_benchId.store(OpenBenchFurnitureFormId(kMenuName));
            g_session.store(g_sessionCounter.fetch_add(1, std::memory_order_acq_rel) + 1);
            g_close = Close::kNone;
            g_closeFrames = 0;
            g_activateFrames = 0;
            g_fallbackSent.store(false, std::memory_order_release);
            g_focusLostFrames.store(0, std::memory_order_release);
            g_phase.store(Phase::kPending, std::memory_order_release);
            logger::info("PrismaCrafting: weapon bench {:08X} opening (session {}, inspectMode={})",
                         g_benchId.load(), g_session.load(), menu->inspectMode);
            if (!EnsureView()) {
                Teardown("view creation failed");
                return;
            }
            TryActivate();
        }
        class ExamineMenuHook
        {
        public:
            static bool Install()
            {
                if (s_installed) return true;
                REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::ExamineMenu[0] };
                // Constraint: slot 4 is AdvanceMovie, VERIFIED in re-pm-answers/37-weapon-bench-gaps-b.md Q1c and lib/commonlibf4/include/RE/E/ExamineMenu.h
                const auto originalAdvance = vtable.write_vfunc(0x4, &ExamineMenuHook::AdvanceMovie);  // TODO-verify
                if (!originalAdvance) {
                    logger::critical("PrismaCrafting: ExamineMenu hook incomplete; native weapon bench disabled");
                    return false;
                }
                _AdvanceMovie = REL::Relocation<std::uintptr_t>{ originalAdvance };
                s_installed = true;
                logger::info("PrismaCrafting: ExamineMenu AdvanceMovie hooked (weapon bench close watchdog)");
                return true;
            }
        private:
            static void AdvanceMovie(RE::ExamineMenu* a_this, float a_timeDelta, std::uint64_t a_time)
            {
                _AdvanceMovie(a_this, a_timeDelta, a_time);
                const auto phase = g_phase.load(std::memory_order_acquire);
                if (phase == Phase::kIdle) return;
                if (phase == Phase::kPending) {
                    TryActivate();
                    return;
                }
                if (phase == Phase::kClosing) {
                    TickClose(*a_this);
                    return;
                }
                const auto view = g_wbView.load(std::memory_order_acquire);
                if (!g_api || !g_api->IsValid(view)) {
                    Teardown("view lost");
                    return;
                }
                if (g_cursorReassertFrames.fetch_sub(1, std::memory_order_acq_rel) > 0) {
                    if (auto* menu = FindExamineMenu()) {
                        menu->UpdateFlag(RE::UI_MENU_FLAGS::kUsesCursor, true);
                        menu->UpdateFlag(RE::UI_MENU_FLAGS::kUpdateUsesCursor, true);
                    }
                    if (auto* ui = RE::UI::GetSingleton()) ui->RefreshCursor();
                }
                if (!g_jsReady.load(std::memory_order_acquire) && std::chrono::steady_clock::now() >= g_readyDeadline) {
                    logger::error("PrismaCrafting: weapon bench view never acknowledged the bootstrap within 5s; restoring the vanilla bench");
                    Teardown("JS ready watchdog expired");
                    return;
                }
                static bool s_escapeHeld = false;
                const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                DWORD foregroundPid = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
                XINPUT_STATE pad{};
                const bool padB = XInputGetState(0, &pad) == ERROR_SUCCESS &&
                    (pad.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
                static bool s_padBHeld = false;
                const bool nativeEscape = !g_jsReady.load(std::memory_order_acquire) && foregroundPid == GetCurrentProcessId();
                if (nativeEscape && ((escapeDown && !s_escapeHeld) || (padB && !s_padBHeld))) {
                    logger::info("PrismaCrafting: native Escape/pad-B closed the weapon bench (JS not ready)");
                    RequestClose();
                    return;
                }
                s_escapeHeld = escapeDown;
                s_padBHeld = padB;
                // Constraint: a view that loses framework focus cannot receive Escape, so close instead of trapping the player.
                if (g_api->HasFocus(view)) {
                    g_focusLostFrames.store(0, std::memory_order_release);
                } else if (g_focusLostFrames.fetch_add(1, std::memory_order_acq_rel) + 1 >= kFocusLostFrames) {
                    logger::warn("PrismaCrafting: weapon bench lost view focus; forcing close");
                    RequestClose();
                }
            }
            static inline REL::Relocation<decltype(&ExamineMenuHook::AdvanceMovie)> _AdvanceMovie;
            static inline bool s_installed = false;
        };
    }

    namespace detail
    {
        bool InstallHook() { return ExamineMenuHook::Install(); }
        void SetEnabled(bool a_value) { g_enabled.store(a_value, std::memory_order_release); }
        bool EnabledValue() { return g_enabled.load(std::memory_order_acquire); }
        void SetDev(bool a_value) { g_dev.store(a_value, std::memory_order_release); }
        bool DevValue() { return g_dev.load(std::memory_order_acquire); }
        bool SessionActiveValue()
        {
            const auto phase = g_phase.load(std::memory_order_acquire);
            return phase == Phase::kActive || phase == Phase::kClosing;
        }
        void HandleMenuEvent(const RE::MenuOpenCloseEvent& a_event)
        {
            const auto gen = g_menuGen.fetch_add(1, std::memory_order_acq_rel) + 1;
            Teardown(a_event.opening ? "ExamineMenu reopened" : "ExamineMenu closed", !a_event.opening);
            if (!a_event.opening) return;
            if (!PrismaCraftingThreading::AddGameTask([gen] { BeginSession(gen); }))
                logger::warn("PrismaCrafting: weapon bench open not queued (dispatch unavailable); vanilla bench kept");
        }
    }
}

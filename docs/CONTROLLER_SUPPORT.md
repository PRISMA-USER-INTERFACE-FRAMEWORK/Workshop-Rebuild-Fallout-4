# Controller support

PrismaCrafting consumes PrismaUI 2.1.1's controller surface and owns none of it. The framework turns
the physical button into a canonical name, routes it to the focused view, owns the press/release
edges and the directional repeat delay and rate, owns the View/Share focus-entry gesture, and owns
the Xbox/PlayStation prompt vocabulary and glyph rendering. PrismaCrafting declares which *semantic*
action each canonical button means and what the pages do with it.

The controller module requests `IVPrismaUI12`; the rest of Crafting continues to use the API-11
dispatch and region contracts. The framework's modern feature tables
(`src/PrismaUI_F4_Modern_API.h`) are discovered at runtime for per-view cursor policy and feature
detection, and the plugin keeps working on a provider that only implements the frozen interfaces.

## Mappings

| Context | Controller | Keyboard | Result |
|---|---|---|---|
| Crafting/Cooking | LB / RB | PageUp / PageDown | Previous/next category |
| Crafting/Cooking | D-pad Left/Right | Left/Right | Previous/next recipe card |
| Crafting/Cooking | D-pad Up/Down | Up/Down | One grid row up/down, then filters/header above and pager below |
| Crafting/Cooking | A / Cross | Enter | Activate the selected item or control |
| Crafting/Cooking | B / Circle | Escape | Close the modal, leave Stations, or close the menu |
| Workshop panel | D-pad Up/Down | - | Move between rows: top controls (Favorites, Theme, A-/A+, refresh, Exit), categories, subcategories, All/Buildable/Locked, item strip. Empty rows are skipped |
| Workshop panel | D-pad Left/Right | A/D | Item strip: previous/next item (holding speeds up after ~1 s). Chip rows: previous/next chip, applied at once. Top row: move the focus ring |
| Workshop panel | LB / RB | W/S | Previous/next tab stop, wrapping: Favorites, Theme, each main category, All/Buildable/Locked. Categories and filters apply as the shoulder lands on them; Favorites and Theme only take the focus ring, A presses them. W/S on the keyboard steps the main category only |
| Workshop panel | A / Cross | E/Enter | Item strip: start placement of the selected item (or place the held ghost). Top row: press the focused control. Chip rows: return to the item strip |
| Workshop panel | B / Circle | Tab | Back one level: cancel the ghost, leave a nested row for the item strip; on the item strip, close the Workshop |
| Workshop panel | View / Share | Escape | Focused: release panel focus, or leave free camera if it is on. Unfocused: PrismaUI focus entry, which leaves free camera if it is on and otherwise gives the panel focus |
| Workshop panel | LS / RS click | F3 / F4 | Place in red / free camera. Both also work with the dock unfocused (no cursor): they are PrismaUI focus entries that run the same native toggles as F3/F4 |
| Workshop panel (unfocused, pad) | Start, D-pad, LB/RB, B, X, Y | - | The same actions as focused, one step per press (no hold repeat, no LT/RT chords). D-pad, LB/RB, B, X and Y return to Fallout while an object is held; Start, View, LS and RS stay with the dock. A, LT and RT always stay Fallout's (grab/place, triggers) |
| Workshop panel | View / Share (while free camera is on) | F4 | Exit free camera. Entering free camera releases panel focus so the mouse/stick turn the camera, so the page no longer receives B; View is bound as an unfocused PrismaUI focus entry and is the escape (the engine swallows R3 in free camera) |
| Workshop panel | Start | F6 | Cycle snap; with LT held object snap (Shift+F6), with RT held reset (F7). Unfocused: cycle snap |
| Workshop panel | hold LT + D-pad / LB-RB | Num 4/8/7 | Rotate yaw (Left/Right), pitch (Up/Down), roll (LB/RB) |
| Workshop panel | hold RT + D-pad / LB-RB | Home/End, Ins/Del, PgUp/PgDn | Move forward/back (Up/Down), right/left, up/down (RB/LB) |
| Workshop panel | - | - / + | Scale (keyboard only) |
| Workshop panel | X / Square | R | Open Scrap confirmation |
| Workshop panel | Y / Triangle | F and Q | Favorite the item and tag its components for search |
| Workshop panel (unfocused) | View / Share | C | Enter Prisma panel focus |
| Scrap confirmation | A / Cross | Enter | Confirm |
| Scrap confirmation | B / Circle | Escape | Cancel |

`src/ControllerActionTable.h` is the single declaration behind both halves of that table: one row per
canonical button, carrying the PrismaUI action it registers and the prompt the pages show for it.
`WorkshopControllerInput::Activate` registers the Workshop rows through API 12 and
`ActivateCrafting` registers the crafting rows; the prompt JSON is generated from the same rows, so a
glyph can never name a button that was not registered, and a registered button can never be
invisible.

The D-pad is bound as actions rather than left to PrismaUI's native menu-key conversion. That
conversion is enabled only on the flat runtimes the provider has inspected, so a page depending on it
alone would have no d-pad navigation elsewhere; the API-12 action route comes from the framework's
MenuControls/PlayerControls sinks instead. Because an owned action suppresses the converted key copy,
the pages must handle `nav.*` — they do — and their arrow-key handling remains as the keyboard path
only. There is no native-pad fallback: PrismaCrafting owns no controller sink or Gamepad poller.

Action states are per action, decided by the page from the framework's state field: `nav.*` accept
`pressed` and `repeat` (a held direction must repeat), while `workshop.place`, `workshop.scrap`,
`workshop.favorite`, `workshop.panel_focus`, `craft.confirm` and `craft.back` accept `pressed` only,
so a held A cannot place the same building repeatedly. The timing itself is PrismaUI's
(`Registry::kRepeatDelaySecs` / `kRepeatIntervalSecs`) and no second repeat timer exists here.

## Prompts

A prompt token is either the shared `gp:<canonical>` marker for the button *this plugin registered*,
or PrismaCrafting's own keyboard text for the same semantic — its keydown handling is the keyboard
route, not a `ControlMap` user event, so a Fallout user-event lookup would describe a different
button than the one bound. The two devices are resolved from `IsUsingGamepad()`, the controller style
from `GetControllerStyle()` (0 = Xbox, 1 = PlayStation), and both are re-read when PrismaUI broadcasts
`prisma-input-device-change`, which the framework emits only on a real device or style transition.

`assets/views/js/*_controller.js` render the payload with the framework's shared
`window.PrismaCG.renderKey`, loaded from `prisma_controller_glyphs.js` / `.css` in the deployed
`PrismaUI_F4/views` tree. PrismaCrafting ships no controller art of its own.

`GetButtonPrompt` and `GetGamepadButtonName` are deliberately unused: the first resolves a vanilla
`ControlMap` user event on the active device and the second converts a raw `BS_BUTTON_CODE`, and
neither is the button this plugin declared. Using either here would put a second, disagreeing source
of truth next to the binding.

## Focus entry

The Workshop dock is the one view reachable while no Prisma view holds focus, so its `Back` row
(View / Share) also registers `BindControllerFocusEntry`. PrismaUI admits the callback to its
verified Fallout game/window-thread dispatcher before consuming the physical event, and only fires it
for a visible `kPanel` when nothing else holds focus; if admission fails, Fallout keeps the event.
The same button keeps its `workshop.panel_focus` action, so View / Share enters the panel from the
unfocused state and releases it again once the dock owns focus, exactly like the keyboard `C` toggle.

View/Share is reserved and consumed only while Prisma Workshop is active, so Fallout's camera-view
action is unavailable in that context. Outside Prisma Workshop, the event passes through unchanged.

Every Workshop row carries `Unfocused::No`, `Always` or `Browsing` (`src/ControllerActionTable.h`).
`Always` rows (Back, LS, RS, Start) are focus entries for the life of the view. `Browsing` rows (D-pad,
LB/RB, B, X, Y) are focus entries only while no object is held: `WorkshopMenuHook::AdvanceMovie` reports
`WorkshopInputHook::HoldingPlacement` (ActiveEdit and a placement item) to
`WorkshopControllerInput::NoteHeldObject`, which on a change queues a game-thread sync that unbinds or
rebinds those rows through `UnbindControllerFocusEntry` / `BindControllerFocusEntry`, so a held ghost or
grabbed object keeps Fallout's own pad handling. Rows other than Back/LS/RS forward their action to
`window.workshopUnfocusedAction`, which runs the focused handler once as `pressed` with the LT/RT
modifiers cleared. A focus entry only reports the press, so unfocused input has no repeat and no chords.
A and LT/RT are never unfocused: A is Fallout's grab/place and the triggers are held chords.

These routes need a PrismaUI build that keeps one `BindControllerFocusEntry` registration per
`(view, button)` (the PrismaUI framework's controller action table).
The V12 signatures did not change, so the plugin cannot detect an older provider that keeps one entry
per view. `BindPhase` therefore binds panel focus (Back) last in every focus-entry batch and re-asserts
it after a `Browsing` rebind: on an older provider Back is the surviving entry and the dock behaves as
it did with Back as its only unfocused button, instead of losing Back to whichever row bound last.

Keyboard/mouse is unchanged: the dock's letter and arrow keys still need panel focus, so typing
never fires dock actions; only pad buttons use the unfocused route.

## Failure behaviour

`BindControllerAction` returns false if the view is not DOM-ready or the canonical name is unknown;
accepted mappings enter PrismaUI's provider-owned asynchronous bridge lifecycle. `Pending` already
owns matching physical input, so it is not a failure state.

A refusal is logged and never undoes a binding that already succeeded: `BindPhase` walks the table
and keeps each mapping PrismaUI accepted. PrismaUI replaces a mapping per `(view, button)`, so
re-binding the same list is idempotent. `ClearControllerActions` is deliberately not called.

`Pending` is owned by PrismaUI: matching physical input is admitted while the asynchronous JS
bridge settles as `Ready` or `Failed`. The public controller contract is in the
`framework-v2.1.1` header release at commit `4cadea85e147337deaa4d4f57598994e6ce8604e`, but that
release does not contain provider-owned recovery. The runtime minimum for this build is
**PrismaUI 2.2.0+** (`framework-v2.2.0`). The traced paths are
`InstallBridge` -> `WebRuntime::Invoke` -> the Ultralight callback. A failed provider callback now
schedules a bounded provider-owned retry through `WebRuntime::InvokeDeferred`; it never depends on a
PrismaCrafting V11 task chain or on the current `GameThreadDispatcher::HandleWindowMessage` drain.
The provider's `BridgeLifecycle` stops after three failed attempts and invalidates the retry when the
view is cleared. PrismaCrafting only declares mappings and focus entries through the public API; it
does not inspect bridge state, manufacture lifecycle ticks, or add a timer, controller poller, or
native fallback.

Focus entry is tracked by the registered view handle rather than a boolean. PrismaUI clears the entry
when a view is destroyed, so a recreated helper view receives exactly one registration of its own.
A refused entry is retried without disturbing bindings that already work. Replacing any of this with
a PrismaCrafting-owned native Gamepad poller would be a second controller implementation with its own
device detection, repeat timer and action synthesis — precisely what the framework already provides and
maintains.

`assets/views/js/crafting_controller.js:handleCraftingNavigation` refreshes prompts through the
existing native callback with an empty argument. It never reports a device, so PrismaUI-generated
Arrow, Enter, or Escape events cannot change the framework's authoritative active-device state.

## Keyboard-only

Free height and scaling remain keyboard-only.

`src/PrismaUI_F4_API.h:IVPrismaUI12` is the framework's canonical V1-V12 public header, vendored from
release `framework-v2.1.1`. The
versioned interfaces remain the primary route.

## Cursor

The page-level cursor is Fallout's own: while the panel is focused, `WorkshopInputHook` sets
`kUsesCursor`/`kUpdateUsesCursor` on `WorkshopMenu` and calls `UI::RefreshCursor`. Entering and
leaving that focus from a pad goes through API-12 focus entry (`Back` on the unfocused dock) and
`workshop.panel_focus` (leaving it), so a controller user never needs the keyboard `C` toggle.

PrismaUI's per-view cursor policy is a *different* cursor — the framework's own compositor cursor —
and is set to `Default` for both views when they take focus. `Hidden` would let a page's CSS cursor
replace the compositor cursor, which is the right choice for a page that draws its own pointer; these
two pages only style `cursor: pointer` / `not-allowed` and rely on the compositor cursor for aiming.

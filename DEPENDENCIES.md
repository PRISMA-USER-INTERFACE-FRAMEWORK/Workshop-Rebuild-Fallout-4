# Dependencies

PrismaCrafting is an F4SE plugin. It does not stand alone: it renders its entire UI through the
**PrismaUI_F4** framework and will do nothing at all if that framework is not installed and loaded
first.

## Runtime dependencies

| Dependency | Version this build targets | Required |
|---|---|---|
| Fallout 4 | 1.10.163 (OG), or the Anniversary Edition runtimes the loader accepts (1.11.191, 1.11.221, 1.11.240); NG 1.10.980-1.10.984 is rejected at plugin query | yes |
| F4SE | matching the game runtime | yes |
| F4SE version database (Address Lib) | the database matching the game runtime | yes — engine functions are bound by their version-database ID, not raw offsets |
| PrismaUI_F4 framework | PrismaUI 2.2.0+ runtime; 2.1.1 public API headers | yes |
| Ultralight (shipped inside PrismaUI_F4) | staged by the framework | yes, via the framework |

> **The F4SE version database** is a hard runtime dependency: the settlement-build engine hooks and helper
> calls resolve their target functions through version-database IDs, so the matching
> version-database plugin must be installed. Mid-function call sites that still use raw offsets are
> validated (each must be an `E8` call to the ID-resolved target) before a hook is installed, so a
> wrong database fails closed rather than patching the wrong bytes.

### PrismaUI_F4

The vendored public headers remain pinned to `framework-v2.1.1` for API provenance; that tag does not
contain the provider-owned failed-bridge recovery used by this build. The runtime minimum is
**PrismaUI 2.2.0+** (`framework-v2.2.0`), which includes that recovery.

PrismaCrafting uses the canonical PrismaUI V1-V12 header and requires V11 for thread-affine engine
work:

- `IVPrismaUI12` carries the string-named controller actions and the focus-entry gesture
  (`BindControllerAction`, `BindControllerFocusEntry`, …). This is the only controller contract this
  plugin uses: PrismaUI converts the physical button to a canonical name, routes it to the focused
  view, owns press/release and directional repeat timing, owns the unfocused View/Share entry, and
  owns the prompt vocabulary. PrismaCrafting declares semantic actions and nothing else.
- The modern feature tables in `src/PrismaUI_F4_Modern_API.h` are additive and discovered at
  runtime. The plugin uses them for per-view cursor policy; native Gamepad polling lives there too
  and is deliberately unused, because it is the route for consumers that need analog stick and
  trigger values and it bypasses custom controller actions for the focused view.
- `IVPrismaUI11` is now a **hard safety dependency** for every historical
  `F4SE::GetTaskInterface()->AddTask(...)` site that touches Fallout/RE state. Those calls are
  redirected inside PrismaCrafting to V11 `DispatchToGameThread`, which executes through the
  framework's independently verified Fallout window thread.
- The V11 dispatcher intentionally defers execution through the Fallout HWND even when the caller
  is already on that thread. This preserves the old code's need to escape input/menu/detour call
  stacks without sending the mutation to an F4SE worker thread.
- The required framework baseline includes the hardened bounded/coalesced dispatcher queue, early
  `kGameDataReady` window attachment, fail-closed queue admission, and queued UI-event cleanup when
  a view is destroyed.
- The flat provider also delivers callback-bearing `CreateView` / `OnDomReady` lifecycle callbacks
  through the same verified, view-scoped queue. That closes the helper-view path that can immediately
  call `PushHelperData()` from DOM-ready initialization.
- The dispatcher is fail-closed: a rejected admission is a hard guarantee the task will never run
  later, so a caller that still needs the work does its own bounded retry or gives up visibly.
  PrismaCrafting does not buffer rejected tasks and never falls back to F4SE `AddTask`.

**Framework baseline:** PrismaUI_F4 **2.1.1**, tag `framework-v2.1.1`, commit
`4cadea85e147337deaa4d4f57598994e6ce8604e`.

**Backend:** Ultralight 1.4, in-process, owned by the framework. PrismaUI 2.1.0 replaced the
out-of-process Chromium/CEF backend with it, and 2.1.1 removed CEF entirely; nothing in this plugin
links against or requires CEF.

`src/PrismaUI_F4_API.h` is the framework's canonical public consumer header. It carries `CreateView`
(with its `OnDomReady` callback), the V11 dispatcher, and the V12 controller actions in one file, so
no consumer extension header is needed.

## Build dependencies

| Dependency | Notes |
|---|---|
| xmake | the only supported build system; there is no cmake path |
| CommonLibF4 | [commonlibf4-nomad](https://github.com/G-A-R-D-E-N/commonlibf4-nomad), cloned with `--recurse-submodules` (CI builds against its `main`); point `COMMONLIBF4_PATH` at a checkout or place one next to this repo |
| spdlog | pulled in via CommonLibF4; the spdlog config must match commonlib's |
| fmt, nlohmann_json | declared by `xmake.lua` |

Build:

```
xmake
```

Output lands in `build/windows/x64/release/PrismaCrafting.dll`.

`src/SimpleIni.h` is a vendored single-header copy of SimpleIni (used for `PrismaCrafting.ini`).

## Deployment layout

```
<mod folder>/
├── F4SE/Plugins/PrismaCrafting.dll
└── PrismaUI_F4/views/          <- the contents of assets/views/
```

The views must land under the framework's `PrismaUI_F4/views/` root, because `CreateView` is passed a
filename relative to that root (`"crafting.html"`, `"workshop.html").

Settings are written to `Documents\My Games\Fallout4\F4SE\PrismaCrafting.ini`, which is deliberately
outside the mod folder so Mod Organizer does not virtualize it.

Both pages also load the framework's shared `prisma_controller_glyphs.css` and
`prisma_controller_glyphs.js` from the same `PrismaUI_F4/views/` root by relative URL. That is why
the glyph stylesheet is linked from both pages and why `workshop.css` sizes the shared `.cg-img`
art: the plugin ships no controller art of its own, and the framework's copy is what makes a prompt
render at the right size instead of as full-size artwork.

## Pinned packages

`xmake.lua` pins every `add_requires` dependency: `spdlog v1.16.0`, `fmt v11.0.2` and
`nlohmann_json v3.12.0`. A version bump is a deliberate dependency update. The pins are version tags
only, and the explicit `fmt v11.0.2` pin must follow the fmt version CommonLibF4 supplies.

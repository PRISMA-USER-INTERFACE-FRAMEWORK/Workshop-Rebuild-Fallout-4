# Workshop Rebuild

A Fallout 4 F4SE plugin that replaces the settlement build menu and the cooking and chemistry
crafting menus with a web rendered interface. It runs on the PrismaUI framework and ships as
`PrismaCrafting.dll`.

## Features

- Recipe lists at chem and cooking benches are paged in C++, so big load orders open without
  freezing the game.
- Live in engine 3D item previews, with baked flat icons as a fallback.
- Workbench upgrade tiers with material and cap costs and build timers.
- Locked recipes show the real perks and tools the game asks for, read straight from the recipe
  records. An INI switch (or the in-game UNLOCK ALL toggle) can open every bench if you would rather skip the tier system.
- A settlement build helper that keeps build mode fully interactive and uses the game's own ghost
  placement.
- Full controller navigation through the PrismaUI controller actions.

## Settings

All player settings live in `Documents\My Games\Fallout4\F4SE\PrismaCrafting.ini`. The plugin creates
the file on first launch, so it is not in the mod folder and it is safe with MO2. Edit it while the
game is closed, or use the in-game toggles.

- **UNLOCK ALL** (crafting page header, or `[Workbench] UnlockAllBenches=1`): skips workbench tier
  gating only. Recipe perk, material and condition requirements still apply.
- **3D PREVIEWS** (crafting page header, or `[UI] ModelPreviews=0`): turns the live in-engine item
  previews off. Use this if previews stutter or freeze.
- **CHEAT MODE** and `AllowCheatMode`: the crafting page toggle and its `[Gameplay]` kill switch. Off
  by default.

Both header toggles write back to the ini immediately.

## Building

You need xmake and a CommonLibF4 checkout. Set COMMONLIBF4_PATH to your CommonLibF4, or put one
next to this repo, then run:

```
xmake
```

The plugin targets Fallout 4 1.10.163 (OG) and the Anniversary Edition runtimes the loader accepts (1.11.191, 1.11.221, 1.11.240). NG 1.10.980-1.10.984 is rejected at plugin query. The build produces PrismaCrafting.dll.
See [DEPENDENCIES.md](DEPENDENCIES.md) for the framework and CommonLibF4 versions.

## Layout

- `src` is the plugin source.
- `assets/views` is the HTML, CSS, and JavaScript for the interface.
- `assets/translations` holds the string tables.
- `fomod` is the installer metadata.
- `docs` covers [recipe overrides](docs/CRAFTING_CONFIG.md), [workbench definitions](docs/WORKBENCH_CONFIG.md),
  [controller support](docs/CONTROLLER_SUPPORT.md), and [release notes](docs/WHATS_NEW.md).
- `tools/CraftingConfigEditor.html` is the visual editor for the recipe override file.

## Contributing

Explanatory notes, reasoning, and bug history do not go in source comments or in this repository.
They live in the private CodeNotes repository, one entry per symbol. Keep only a single line
constraint comment in code when an invariant is not obvious.

## License

See LICENSE.md. This plugin depends on the PrismaUI framework, which ships separately.

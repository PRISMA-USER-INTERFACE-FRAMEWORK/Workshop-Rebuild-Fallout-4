# Workshop Rebuild

A Fallout 4 F4SE plugin that replaces the settlement build menu and the cooking and chemistry
crafting menus with a web rendered interface. It runs on the PrismaUI framework.

## Features

- Recipe lists at chem and cooking benches are paged in C++, so big load orders open without
  freezing the game.
- Live in engine 3D item previews, with baked flat icons as a fallback.
- Workbench upgrade tiers with material and cap costs and build timers.
- Locked recipes show the real perks and tools the game asks for, read straight from the recipe
  records. An INI switch can open every bench if you would rather skip the tier system.
- A settlement build helper that keeps build mode fully interactive and uses the game's own ghost
  placement.

## Building

You need xmake and a CommonLibF4 checkout. Set COMMONLIBF4_PATH to your CommonLibF4, or put one
next to this repo, then run:

```
xmake
```

The target is Fallout 4 1.10.163 (the pre Next Gen runtime). The build produces PrismaCrafting.dll.

## Layout

- `src` is the plugin source.
- `assets/views` is the HTML, CSS, and JavaScript for the interface.

## License

See LICENSE.md. This plugin depends on the PrismaUI framework, which ships separately.

# Workshop Rebuild: What's New

## Workshop Rebuilt 1.3.0

Next-Gen 1.10.984 is now supported, including the settlement build helper (found by static analysis,
not yet tried in game). The rest is stability: more accurate recipe cards, steadier crafting and
settlement menus, safer settings and recipe exports, and cheat mode off unless the ini enables it.
The full list is in `changelognexus.bbcode`.

## Workshop Rebuilt 1.2.0

The interface now runs on the current PrismaUI framework, and the whole mod can be played with a
controller.

**Public API headers are based on PrismaUI_F4 2.1.1 (`framework-v2.1.1`).** Runtime installation
requires **PrismaUI 2.2.0+** (`framework-v2.2.0`)
for provider-owned failed-bridge recovery; the header tag alone is not sufficient.

### Play it with a controller

- **The D-pad navigates**, in the crafting menu and the settlement build panel alike, with an outline
  that follows your selection and scrolls it into view.
- **You no longer need a keyboard to use the settlement panel.** Back (View/Share) takes cursor focus
  and hands it back again; that used to be the C key, which a controller cannot press.
- **A** crafts, confirms and places. **B** backs out and cancels. **X** opens the scrap confirmation.
  **Y** favourites and tags the selected item.
- **Prompts show real button art** — the same shared Xbox and PlayStation glyphs the rest of Prisma
  uses — at a sensible size, with keyboard prompts keeping their keycap look.
- Held directions repeat at a comfortable rate, and a held **A** places one building rather than a
  row of them.

### Installing

The download now carries a FOMOD, so a mod manager installs the plugin, the interface files and the
sounds in one step. Supported build is still **Fallout 4 1.10.163 / Old-Gen** for the settlement
build helper; the crafting and cooking menus also run on newer runtimes.

## Previous update

A big pass across item previews, settlement building, and the workbench upgrade page.

### Item previews look right now

- **Guns show fully assembled**, not a pile of floating parts. A lot of modded weapons (Desert Eagle,
  MK12 SPR, and others) were previewing as a mess of barrels and magazines scattered around the origin
  instead of a put-together gun. Fixed.
- **Weapons preview lying flat** instead of standing straight up on end.
- **Armor that was stuck on a generic placeholder icon** now shows its real model. Some armors (the
  B-90 tactical gear among them) reference a model file their own mod never actually shipped — the
  catalog now notices that and falls back to the armor's real piece instead of giving up.
- **Item previews no longer bleed out of their boxes.** Scrolling the catalog, opening the cheat-mode
  confirmation, and the STATIONS page could all show a 3D model painted over things it shouldn't be
  over. The preview system can now properly crop a model to the box it belongs in.
- Previews are brighter, closer, and bigger than before, with a spotlight-style backdrop instead of a
  flat grey slab.
- Clicking a catalog item no longer requires a second click to actually select it.

### Settlement building is smarter about materials

- **The materials list is honest now.** It shows one number: everything you can actually spend against
  what the build costs, counting both what's stored in the workshop and what you're carrying.
- **Supply lines count.** If your settlements are linked by a provisioner, materials stored in any of
  them now count toward what you can build, the same way the vanilla counter works.
- **Building actually costs materials now**, and only takes from your inventory as a last resort — it
  empties the workshop's own storage first.
- **Selecting an item no longer instantly places it.** WASD navigates the item list, E selects, Enter
  places — each key does one thing.
- **Scrap (F) works again** at a settlement's build cursor.
- The requirements panel updates live now instead of showing stale numbers after you build or scrap
  something.

### Workbench upgrades (STATIONS)

- The cursor toggle key changed from Insert to **C** (easier to reach from WASD).
- The STATIONS page has a proper backdrop now (a dim, red-lit Vault-Tec panel) instead of empty black,
  and its panels no longer get buried under leftover item previews.

## Earlier update

A big update to the crafting terminal and the settlement build helper, plus a fix for the
Vault-Azon store. Here is everything that changed, in plain terms.

## Search actually works now

- **Crafting terminal search box.** You can finally type in the search/filter box at cooking
  and chem stations. Typing was being dropped before. Click the box and type to filter the
  catalog instantly.
- **Vault-Azon store search.** The storefront search bar had the same problem and now accepts
  typing too. Search the catalog for what you want instead of scrolling.

## Build helper: the materials are right now

- **Correct parts for every buildable.** The helper used to mix up items that share a name. If
  two different beds were both called "Bed," it could show you the wrong one's materials. Each
  buildable now shows its own real requirements.
- **No more confusing duplicates.** Items that are truly the same are merged into one entry.
  Items that share a name but are actually different (for example a basic bed versus a heavier
  one) are now told apart with a short tag, like "Bed (Hospital)."

## Build helper: it tells you what you need and where to find it

- **"Found in" location.** Every buildable now shows where it lives in the vanilla build menu,
  for example "Furniture > Beds," so you know which category to open.
- **Clear build status.** Each item shows a green "Ready to build" or an amber "Can't build yet"
  badge, and when you cannot build it the helper now names exactly what is missing, such as a
  specific perk or a tool you need to carry.
- **Schematics and perks.** If a buildable needs a schematic or a perk, it is listed and marked
  locked or unlocked so you can see what you still have to earn.
- **Tools that make sense.** The old tool list was a giant jumble with the toolbox repeated.
  Now it reads cleanly: "Carry any one" when one tool will do, or "Need one from each" when a
  recipe truly requires more than one kind of tool, and it shows which ones you already have.

## Build helper: less in the way

- **Minimize button.** A new minus button collapses the helper down to just its title bar so it
  stops covering your build view. Click it again to bring it back.
- **The X behaves.** The close button now closes only the helper. It no longer kicks you out of
  the whole settlement build menu.

## Make the menus bigger or smaller

- **Scale slider.** Both the crafting terminal and the build helper have a scale control
  (A minus, a slider, A plus) so you can size the menu text up or down for comfort, from 70% to
  140%.
- **It remembers your choice.** Your chosen size is saved and comes back next time you open the
  menu, even after restarting the game. The crafting bench and the build helper each remember
  their own size.

## Fixes

- Fixed the mouse cursor hiding behind the crafting menu after scaling.
- Fixed text getting squished when scaling the build helper.
- Toolbox-gated recipes now correctly show that they require a toolbox, matching what the game
  actually checks.

If you run into anything odd, the most useful thing to share is the log at
`Documents\My Games\Fallout4\F4SE\PrismaCrafting.log`.

# PrismaCraftingRecipes.json — recipe override config

A hand-editable JSON file that overrides any recipe's **required components** and **craft
conditions**, without editing a single plugin record. PrismaCrafting is the only craft path for
these recipes (it executes the craft itself, drawing materials from the player + bench), so
overlaying the in-memory recipe index controls both crafting and the UI.

## Where it lives
```
Documents\My Games\Fallout4\F4SE\PrismaCraftingRecipes.json
```
(next to `PrismaCrafting.ini`; MO2-safe, survives MO2 resets). Absent file = vanilla behavior.
A documented template is in [PrismaCraftingRecipes.example.json](PrismaCraftingRecipes.example.json).

## Debug logging
The log lives in a `logs\` folder inside the mod (resolved from the DLL's own location), not in
Documents. Toggle verbose debug output with one line in `PrismaCrafting.ini`:
```
[Debug]
Enabled = 1     ; 0 = info/warn/error only (default); 1 = full debug trace
```
With it on, the craft path (gate checks, per-component have/need/consume), config load/apply/resolve,
menu lifecycle, and export decisions are all traced to `…\logs\PrismaCrafting.log`. The exact path is
printed on the first line of the log (`PrismaCrafting vX — debug logging ON — log: …`). Set back to `0`
for normal play.

## When it loads
The override config is parsed once per game session, inside `CobjCache::Build()` (at save-load /
first bench open). Restart the game after changing the config; loading another save does not
reload the session's configuration.

## Author mode (data export is opt-in)
PrismaCrafting writes **no data files during normal play**. To author with the editor, enable the
toggle in `Documents\My Games\Fallout4\F4SE\PrismaCrafting.ini`:
```
[Author]
ExportConfig = 1
```
With it on, the plugin exports the editor data files for **all** recipes — `cobj_recipes.json`,
`PrismaCraftingRecipes.skeleton.json`, and `CraftingCatalog.json` — to your MO2 overwrite:
`…\overwrite\Data\PrismaUI_F4\JsonData\` (the disk write is off-thread).

**It only writes when something actually changed.** The export runs **at most once per session** (the
first time you open a crafting bench), and it stores a fingerprint of your loaded plugins in
`crafting_export.fingerprint`. On the next session, if your load order is identical, it **skips the
export entirely** — the existing files are still current. It re-exports only when you add/remove/reorder
plugins. So normal play and repeated bench visits write nothing. Set the toggle back to `0` to disable
exporting completely. (The catalog's `globals` list is empty — vanilla recipes have no global gates;
type a global id by hand in the editor if you need one.)

## Schema
Top-level object keyed by a recipe match key (tried in order):
1. `createdKey` — `"Plugin.esp|<localFormID hex>"` of the **created item** (most stable).
2. COBJ **EditorID**.
3. 8-hex **runtime COBJ Form ID**.

```jsonc
{
  "Fallout4.esm|0010C3AD": {
    "components": [
      { "id": "Fallout4.esm|0006907F", "count": 4 },
      { "id": "Steel", "count": 2 }
    ],
    "conditions": [
      { "type": "perk",      "id": "MyMod.esp|000800",     "op": ">=", "value": 1 },
      { "type": "global",    "id": "MyMod.esp|001234",     "op": "==", "value": 0 },
      { "type": "itemcount", "id": "Fallout4.esm|0006907F", "op": ">=", "value": 10 },
      { "type": "keyword",   "id": "Fallout4.esm|00021882", "op": ">=", "value": 1 }
    ]
  }
}
```

### `id` formats (components + condition targets)
- `"Plugin.esp|1A2B"` — owning plugin + **local** Form ID hex. Stable across load order. Recommended.
- `"0001A2B3"` — raw 8-hex **runtime** Form ID. Load-order dependent; test only.
- `"EditorID"` — best-effort; resolves only forms seen as a component in some recipe.

### Semantics
- **`components`** present → **replaces** the whole component list only when the entries are valid and every reference resolves. Rejected overrides retain the existing recipe costs and are logged.
- **`conditions`** present → **replaces** the craft gate: the engine `PlayerPassesConditions()` check
  is skipped and these are evaluated in C++ instead. `"conditions": []` → always unlockable.
- Omit a key to leave that aspect untouched. Conditions are **AND**-ed.
- Condition `type`: `perk` (GetPerkRank), `global` (TESGlobal value), `itemcount` (count across
  player + bench storage), `keyword` (count of items bearing the keyword across player + bench storage).
- `op`: `==  !=  >=  >  <=  <`. Unresolved condition target → the recipe stays locked (fail-safe).

Both views show all configured conditions with their operator, required value and whether they are met at query time. Missing or unnamed targets retain their configured reference as a label. An empty condition override suppresses the original requirement badges. These observations use the same configured evaluator and inventory sources as the gate; they are not an atomic snapshot of the earlier lock evaluation. Native recipe condition/operator and OR-group extraction remains a separate limitation.

### Category mappings

An optional top-level `_classification` array refines classification before the English-name fallback. It can be the only section in the file. Each rule contains `category` and exactly one `editorId` or `keyword` selector:

```json
{
  "_classification": [
    { "editorId": "MyModTravelBag", "category": "BACKPACKS" },
    { "keyword": "MyModRespirator", "category": "HEADGEAR" }
  ]
}
```

Selectors match exact, case-sensitive editor IDs. `editorId` matches either the created item or the COBJ recipe; `keyword` matches a keyword on the created item. Compatible editor-ID rules are checked first, then compatible keyword rules. File order decides ties within either pass, including competing created-item and COBJ matches. Incompatible rules are skipped rather than hiding a later compatible match. Selectors are identifiers, not translated display names, form references or substring patterns.

| Item type | Compatible category IDs |
| --- | --- |
| Weapon | `WEAPONS`, `MELEE`, `GRENADES`, `MINES` |
| Armor | `ARMOR`, `HEADGEAR`, `BACKPACKS` |
| Consumable or ingredient | `HEALING`, `DRUGS`, `DRINKS`, `FOOD` |
| Ammunition | `AMMO` |
| Book | `HOLOTAPES` |
| Miscellaneous or object-mod output | Any category ID listed above, plus `UTILITY` |

Established weapon keywords, head slots and consumable metadata retain their categories ahead of mappings. Ammunition and books keep their type categories. Mappings do not change Workshop menu paths, source records, costs or unlock conditions. Final categories feed the existing workbench tier/score policy; assigning a miscellaneous output to another category can therefore change its upgrade gate.

Unknown category IDs, extra fields, missing/both selectors, non-string fields, empty/whitespace-only IDs, embedded NULs and repeated selectors of the same kind reject the entire array. Existing recipe cost/gate overrides remain usable. An absent or empty array preserves default classification. Load the file before the recipe index is built; restart the game after changes because the index and configuration remain build-once for the session.

The visual editor preserves this section when loading and saving an existing config; edit the rules in JSON. It also preserves unsupported section shapes, leaving validation to the native loader.

## Authoring: the visual editor (easiest)
`tools/CraftingConfigEditor.html` — a standalone editor (no install; double-click to open in Edge/Chrome).

1. Set `[Author]ExportConfig = 1` (see Author mode above), then in game **open any crafting bench
   once** — the plugin exports all recipes (only re-exports when your load order changes).
2. **Open skeleton…** → pick `PrismaCraftingRecipes.skeleton.json` (in MO2 overwrite:
   `…\overwrite\Data\PrismaUI_F4\JsonData\`). It contains every recipe.
3. Optional **Load catalog…** → `CraftingCatalog.json` (same folder) for named perk/global/keyword pickers.
4. Pick a recipe; edit components (search by name) and conditions (toggle "override gate").
5. **Save config…** → save as `PrismaCraftingRecipes.json` in `Documents\My Games\Fallout4\F4SE\`.
   Restart the game to apply.

Only recipes you actually change are written; everything else stays vanilla. **Load existing config…**
re-opens a saved `PrismaCraftingRecipes.json` to keep editing it. (In Edge/Chrome the open/save dialogs
write the file directly; other browsers fall back to a normal download.)

## Authoring: the auto-generated skeleton (raw JSON)
Every session PrismaCrafting writes a ready-to-edit skeleton of **every** recipe, pre-filled with its
current components in this exact schema:
```
Data\PrismaUI_F4\JsonData\PrismaCraftingRecipes.skeleton.json
```
Open it, copy the entries you want to change into `PrismaCraftingRecipes.json` (in the F4SE user dir
above), and just edit the `count`s / `id`s. Notes:
- Keys are the **createdKey** (the crafted item). One entry per item; an override applies to every
  recipe that makes that item. To target a single recipe instead, replace the key with the recipe's
  8-hex COBJ Form ID (shown in each entry as `_formId`).
- Underscore fields (`_name`, `_formId`) are informational and ignored by the loader — keep or delete.
- The skeleton reflects the **current effective** components (after any override already applied), so
  it also serves as "show me what everything costs right now."

## Authoring from the raw dump
PrismaCrafting also writes the full recipe dump to `Data\PrismaUI_F4\JsonData\cobj_recipes.json` (every
field, including per-recipe `editorId`/`formRef`). Use it when you need detail the skeleton omits.

## Safety
Only the session-rebuilt in-memory index is mutated — never the COBJ records or the co-save.
Deleting the JSON fully reverts. Recipes reachable from a vanilla/other bench (not PrismaCrafting)
are out of scope: that path still uses the engine's `requiredItems`.

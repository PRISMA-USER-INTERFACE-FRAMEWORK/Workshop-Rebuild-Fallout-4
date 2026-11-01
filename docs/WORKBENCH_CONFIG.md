# Workbench definitions

Workbench names, descriptions, model paths, category assignments, upgrade bonuses, durations and costs can be changed without rebuilding the DLL. Copy the packaged `Data/PrismaUI_F4/JsonData/PrismaCraftingWorkbenches.example.json` to:

```text
Documents\My Games\Fallout4\F4SE\PrismaCraftingWorkbenches.json
```

Edit that copy, then restart the game. The [example](PrismaCraftingWorkbenches.example.json) matches the default balance. The optional file is read once; the plugin does not create or rewrite it. Missing or rejected files retain all default definitions. Rejection reasons appear in the plugin log.

The root is an array containing each existing bench exactly once: `weapon`, `munitions`, `armor`, `chem`, `utility`. Each bench requires these fields:

| Field | Value |
|---|---|
| `id` | One of the five existing identities |
| `name`, `desc` | Nonempty display text |
| `nif` | Nonempty model path, using the same format as the example |
| `categories` | Array of category IDs assigned to this bench |
| `levels` | Exactly three definitions, numbered 1, 2 and 3 in order |

Each existing category must occur exactly once across the complete file. Categories can move between benches; their IDs remain `WEAPONS`, `MELEE`, `AMMO`, `GRENADES`, `MINES`, `ARMOR`, `HEADGEAR`, `BACKPACKS`, `HEALING`, `DRUGS`, `UTILITY`, `HOLOTAPES`. Additional bench/category identities are not supported by this format.

Each level requires:

| Field | Value |
|---|---|
| `level` | Its ordered level number, 1–3 |
| `bonus` | Nonempty display text |
| `timeSec` | Integer seconds, 0–4294967295 |
| `caps` | Integer caps cost, 0–2147483647 |
| `cost` | Array of material entries, or `[]` for no material cost |

Each material requires nonempty `name` and `ref` strings, plus an integer `count` from 0–4294967295. Use the existing recipe-config [reference syntax](CRAFTING_CONFIG.md), preferably `Plugin.esm|LocalHexID`. Positive-count references must resolve during initialization; a missing material rejects the entire file. Each resolved material can occur only once per level, including references that resolve to the same item. Zero-count entries are ignored. Fractions, negative numbers, booleans and text containing NUL are rejected.

Zero duration completes through the existing upgrade polling. Zero caps and an empty material list intentionally make that upgrade free. Names, descriptions, bonuses and material labels use the existing translation lookup, falling back to their configured text.

Materials must also pass the existing form resolver's retention policy. References returning an identifier in its excluded `FF` prefix range reject the complete file. Use plugin/local-ID references for materials: workbench initialization precedes recipe scanning, so component EditorID registration is unavailable on the first initialization.

INI starting levels, tier thresholds and `UnlockAllBenches` remain in `PrismaCrafting.ini`. Saved bench levels and the co-save format retain their existing identities. Changing categories changes which bench gates those recipes. Changing durations can affect clock recovery for an upgrade already in progress; test changes on a separate save before applying them to an ongoing playthrough.

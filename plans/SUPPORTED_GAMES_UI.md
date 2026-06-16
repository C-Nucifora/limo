# Plan: Spruce up the "supported games" UI

Research: [`research/05-modpacks-and-ui.md`](research/05-modpacks-and-ui.md) (Part 2).

## How games are shown today

- **Active apps:** one dropdown `ui->app_selection_box` (`QComboBox`), populated in `MainWindow::onGetAppNames` (`mainwindow.cpp:2326-2390`) with name + optional icon (`Qt::UserRole` holds the icon path, `mainwindow.cpp:2336`).
- **Empty state:** `empty_state_overlay_` (`mainwindow.cpp:214-254`) — a title + one "Add application" button.
- **Adding a game:** `AddAppDialog` — name/staging fields, an "Advanced setup" toggle, a **GOG game-template combo** (`populateGogTemplateCombo`, `addappdialog.cpp:718-776`, walks every `steam_app_configs/*.json` reading `JSON_NAME`), and an "Import from Steam" button → `ImportFromSteamDialog`.
- **Presets ("supported games") are data:** `steam_app_configs/*.json` (15 bundled today — Bethesda titles, Cyberpunk, Witcher 3, Stardew, Subnautica, … **no FS/AC yet**), discovered via `gameConfigSearchDirs()` (`addappdialog.cpp:160-186`).
- **Steam import dialog:** `ImportFromSteamDialog` fills a `QTableWidget` with icon + name + app_id + has-prefix + path per installed game (`addTableRow`, `importfromsteamdialog.cpp:179-259`) — but **does not show which installed games Limo has a preset for.**

## Ideas (each grounded in existing widgets; pick a subset)

### Idea 1 — Supported-games **gallery grid** in AddAppDialog *(highest impact)*
Replace the hidden GOG combo with a visual `QListWidget` in `IconMode` (or `QListView` + small model): one tile per bundled `steam_app_configs/*.json` — icon + `json["name"]`. Reuse the exact loop in `populateGogTemplateCombo()` to build tiles. Clicking a tile = one-click add: prefill the name, apply the preset's deployers/auto-tags via the existing `initConfigForGog`/`initConfigForApp` path, then just ask for install/staging dirs.
**Touches:** `src/ui/addappdialog.{h,cpp}` + `.ui`; reuses `gameConfigSearchDirs`, `JSON_NAME`, `initConfigForGog`.

### Idea 2 — "Has preset" flag in the Steam library picker *(directly answers "which games are supported")*
In `ImportFromSteamDialog::addTableRow` (`importfromsteamdialog.cpp:179`), each row already has the Steam `app_id`. Add a column/badge that checks whether `<app_id>.json` exists in `gameConfigSearchDirs()` (the same lookup `initConfigForApp` does). Supported games get a "Preset available" ★/✓; others a neutral marker. Add an optional "Only show games Limo supports" filter.
**Touches:** `src/ui/importfromsteamdialog.{h,cpp}` + `.ui`; lift `AddAppDialog::gameConfigSearchDirs()` into a shared util / `ModdedApplication` static so both dialogs share it.

### Idea 3 — Visual app switcher *(optional)*
Promote the per-app icon (already in `Qt::UserRole`) into an optional **icon-grid sidebar** (`QListView` IconMode bound to the same name/icon arrays from `onGetAppNames`) so users pick games by cover art; keep the combo as the compact fallback. Reuse `app_combo_id_map_` so sort/selection logic is unchanged.
**Touches:** `src/ui/mainwindow.cpp` (`onGetAppNames`, `currentApp()` plumbing) + main `.ui`.

### Idea 4 — Richer empty state as a mini showcase
Replace the single button in `empty_state_overlay_` with a small grid of the most common bundled presets (same tile source as Idea 1), so a first-run user sees recognizable games and adds one in a click, falling through to the full dialog for the rest.
**Touches:** `src/ui/mainwindow.cpp` (`setupEmptyStateOverlay`).

### Idea 5 — Ship the FS/AC presets (data) so they appear everywhere for free
Because both the template combo (Idea 1) and Steam picker (Idea 2) are data-driven off `steam_app_configs/`, dropping in the FS/AC presets makes them show up in every supported-games surface with **no extra code**. (Covered by [FARMING_SIMULATOR.md](FARMING_SIMULATOR.md) / [ASSETTO_CORSA.md](ASSETTO_CORSA.md).)

## Recommended scope for the first pass

Idea 1 (gallery) + Idea 2 (Steam picker "has preset" flag) + Idea 4 (empty-state showcase) give the biggest visible improvement and share one data source. Idea 3 (grid switcher) is a nice follow-up. All are additive and low-risk.

## Files / classes touched

- `src/ui/addappdialog.{h,cpp}` + `.ui` — preset gallery (Ideas 1, 4 source).
- `src/ui/importfromsteamdialog.{h,cpp}` + `.ui` — has-preset column/badge + filter (Idea 2).
- `src/ui/mainwindow.cpp` + main `.ui` — empty-state showcase (Idea 4), optional grid switcher (Idea 3).
- Shared helper: lift `gameConfigSearchDirs()` so both dialogs reuse preset discovery.

## Work breakdown

1. Gallery grid in AddAppDialog (Idea 1). — *Issue #234*
2. "Has preset" flag + filter in Steam picker (Idea 2). — *Issue #234*
3. Empty-state showcase (Idea 4). — *Issue #234*
4. (Follow-up) visual app-switcher sidebar (Idea 3).
